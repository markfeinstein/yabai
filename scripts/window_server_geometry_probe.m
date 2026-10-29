#import <AppKit/AppKit.h>

#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "../src/misc/extern.h"
#include "../src/window_server_hit_test.c"
#include "../src/window_server_tags.h"

struct fixture_entry
{
    uint32_t window_id;
    pid_t owner_pid;
    const char *kind;
};

struct fixture_manifest
{
    struct fixture_entry entries[32];
    size_t count;
    uint32_t highlight_id;
};

struct relation_facts
{
    bool parent_known;
    bool legacy_parent_geometry;
    bool geometry_parent_legacy;
    bool legacy_association_known;
    bool geometry_association_known;
    bool legacy_associated_geometry;
    bool geometry_associated_legacy;
};

static int compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t now_ns(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static void spin_ms(unsigned milliseconds)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:(double)milliseconds / 1000.0];
    while ([deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
}

static bool safe_token(const char *value)
{
    if (!value || !value[0]) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.')) return false;
    }
    return true;
}

static bool parse_point(const char *text, CGPoint *point)
{
    char *end = NULL; double x = strtod(text, &end);
    if (!end || *end != ',') return false;
    double y = strtod(end + 1, &end);
    if (!end || *end || !isfinite(x) || !isfinite(y)) return false;
    *point = CGPointMake(x, y); return true;
}

static bool result_valid(const struct window_server_hit_test_result *result)
{
    return result->status != kCGErrorSuccess ||
           (result->window_id && result->window_connection &&
            isfinite(result->window_point.x) && isfinite(result->window_point.y));
}

static bool parent_id(int cid, uint32_t wid, uint32_t *parent)
{
    if (parent) *parent = 0;
    int32_t value = (int32_t)wid;
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
    if (!number) return false;
    const void *values[] = { number };
    CFArrayRef ids = CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
    CFRelease(number);
    if (!ids) return false;
    CFTypeRef query = SLSWindowQueryWindows(cid, ids, 1);
    CFRelease(ids);
    if (!query) return false;
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    bool result = false;
    if (iterator && SLSWindowIteratorGetCount(iterator) == 1 && SLSWindowIteratorAdvance(iterator)) {
        if (parent) *parent = SLSWindowIteratorGetParentID(iterator);
        result = true;
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
    return result;
}

static bool associated(int cid, uint32_t a, uint32_t b, bool *known)
{
    if (known) *known = false;
    CFArrayRef values = SLSCopyAssociatedWindows(cid, a);
    if (!values || CFGetTypeID(values) != CFArrayGetTypeID()) {
        if (values) CFRelease(values);
        return false;
    }
    if (known) *known = true;
    bool result = false;
    for (CFIndex i = 0; i < CFArrayGetCount(values); ++i) {
        CFTypeRef item = CFArrayGetValueAtIndex(values, i);
        uint32_t wid = 0;
        if (item && CFGetTypeID(item) == CFNumberGetTypeID() &&
            CFNumberGetValue((CFNumberRef)item, kCFNumberSInt32Type, &wid) && wid == b) {
            result = true; break;
        }
    }
    CFRelease(values);
    return result;
}

static struct relation_facts relation_facts(int cid, uint32_t legacy, uint32_t geometry)
{
    struct relation_facts facts = {0};
    if (!legacy || !geometry) return facts;
    uint32_t legacy_parent = 0, geometry_parent = 0;
    bool legacy_parent_known = parent_id(cid, legacy, &legacy_parent);
    bool geometry_parent_known = parent_id(cid, geometry, &geometry_parent);
    facts.parent_known = legacy_parent_known && geometry_parent_known;
    facts.legacy_parent_geometry = facts.parent_known && geometry_parent == legacy;
    facts.geometry_parent_legacy = facts.parent_known && legacy_parent == geometry;
    facts.legacy_associated_geometry = associated(cid, legacy, geometry,
                                                  &facts.legacy_association_known);
    facts.geometry_associated_legacy = associated(cid, geometry, legacy,
                                                  &facts.geometry_association_known);
    return facts;
}

static const char *detailed_relation(const struct window_server_hit_test_comparison *comparison,
                                     const struct relation_facts *facts)
{
    bool legacy_hit = comparison->legacy.status == kCGErrorSuccess && comparison->legacy.window_id;
    bool geometry_hit = comparison->geometry.status == kCGErrorSuccess && comparison->geometry.window_id;
    if (comparison->legacy.status != kCGErrorSuccess &&
        comparison->geometry.status != kCGErrorSuccess) return "both-error";
    if (comparison->legacy.status != kCGErrorSuccess) return "legacy-error";
    if (comparison->geometry.status != kCGErrorSuccess) return "geometry-error";
    if (!legacy_hit && !geometry_hit) return "both-no-hit";
    if (legacy_hit && !geometry_hit) return "legacy-only-hit";
    if (!legacy_hit && geometry_hit) return "geometry-only-hit";
    if (comparison->same_window) return "same-window";
    if (facts->legacy_parent_geometry) return "legacy-parent-geometry-child";
    if (facts->geometry_parent_legacy) return "legacy-child-geometry-parent";
    if (facts->legacy_association_known && facts->geometry_association_known) {
        if (facts->legacy_associated_geometry && facts->geometry_associated_legacy) return "associated-bidirectional";
        if (facts->legacy_associated_geometry) return "associated-legacy-to-geometry";
        if (facts->geometry_associated_legacy) return "associated-geometry-to-legacy";
    } else if (!facts->parent_known) {
        return "relationship-metadata-unknown";
    }
    return comparison->same_owner ? "same-owner-unrelated" : "different-owner-overlap";
}

static bool bounds_contain(int cid, uint32_t wid, CGPoint point, CGRect *bounds)
{
    return wid && SLSGetWindowBounds(cid, wid, bounds) == kCGErrorSuccess &&
           CGRectContainsPoint(*bounds, point);
}

static bool local_point_matches(CGPoint screen, CGPoint local, CGRect bounds)
{
    return fabs(local.x - (screen.x - bounds.origin.x)) < 1.0 &&
           fabs(local.y - (screen.y - bounds.origin.y)) < 1.0;
}

static const char *fixture_kind(const struct fixture_manifest *manifest, uint32_t wid)
{
    if (!wid) return "no-hit";
    if (!manifest) return "external-window";
    uint32_t current = wid;
    for (int depth = 0; depth < 4 && current; ++depth) {
        if (manifest->highlight_id == current) return "active-highlight";
        for (size_t i = 0; i < manifest->count; ++i) {
            if (manifest->entries[i].window_id == current) return manifest->entries[i].kind;
        }
        uint32_t parent = 0;
        if (!parent_id(SLSMainConnectionID(), current, &parent) || parent == current) break;
        current = parent;
    }
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (windows) {
        for (id value in (__bridge NSArray *)windows) {
            if (![value isKindOfClass:[NSDictionary class]]) continue;
            NSNumber *number = value[(NSString *)kCGWindowNumber];
            NSNumber *owner = value[(NSString *)kCGWindowOwnerPID];
            if (![number isKindOfClass:[NSNumber class]] ||
                ![owner isKindOfClass:[NSNumber class]] || number.unsignedIntValue != wid) continue;
            pid_t owner_pid = (pid_t)owner.intValue;
            for (size_t i = 0; i < manifest->count; ++i) {
                if (manifest->entries[i].owner_pid == owner_pid) {
                    const char *kind = manifest->entries[i].kind;
                    CFRelease(windows);
                    return kind;
                }
            }
            if (owner_pid == getpid()) {
                CFRelease(windows);
                return "controlled-app-surface";
            }
        }
        CFRelease(windows);
    }
    return "external-window";
}

static void add_fixture(struct fixture_manifest *manifest, NSWindow *window, const char *kind)
{
    if (!manifest || !window || manifest->count >= 32) return;
    manifest->entries[manifest->count++] = (struct fixture_entry) {
        .window_id = (uint32_t)window.windowNumber,
        .owner_pid = getpid(),
        .kind = kind
    };
}

static bool load_fixture_manifest(const char *path, struct fixture_manifest *manifest)
{
    if (!path || !manifest) return false;
    struct stat before;
    if (lstat(path, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_uid != getuid() || (before.st_mode & 0777) != 0600 ||
        before.st_size <= 0 || before.st_size > 1024 * 1024) return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    NSMutableData *data = [NSMutableData dataWithLength:(NSUInteger)before.st_size];
    ssize_t used = 0;
    while (used < before.st_size) {
        ssize_t count = read(fd, (uint8_t *)data.mutableBytes + used,
                             (size_t)(before.st_size - used));
        if (count <= 0) { close(fd); return false; }
        used += count;
    }
    struct stat after;
    bool stable = fstat(fd, &after) == 0 && before.st_dev == after.st_dev &&
                  before.st_ino == after.st_ino && before.st_size == after.st_size &&
                  before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec &&
                  before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec;
    close(fd);
    if (!stable) return false;
    id object = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    if (![object isKindOfClass:[NSDictionary class]] ||
        ![object[@"schema"] isEqualToString:@"yabai.window-server-geometry-fixtures/v1"] ||
        ![object[@"entries"] isKindOfClass:[NSArray class]]) return false;
    for (id row in object[@"entries"]) {
        if (![row isKindOfClass:[NSDictionary class]] ||
            ![row[@"kind"] isKindOfClass:[NSString class]] ||
            !safe_token([row[@"kind"] UTF8String]) || manifest->count >= 32) return false;
        NSNumber *window = row[@"window_id"];
        NSNumber *owner = row[@"owner_pid"];
        uint64_t window_id = [window isKindOfClass:[NSNumber class]] ? window.unsignedLongLongValue : 0;
        long long owner_pid = [owner isKindOfClass:[NSNumber class]] ? owner.longLongValue : 0;
        if ((!window_id && owner_pid <= 0) || window_id > UINT32_MAX || owner_pid > INT_MAX) return false;
        manifest->entries[manifest->count++] = (struct fixture_entry) {
            .window_id = (uint32_t)window_id,
            .owner_pid = (pid_t)owner_pid,
            .kind = [row[@"kind"] UTF8String]
        };
    }
    return manifest->count != 0;
}

static uint64_t window_tags(int cid, uint32_t wid)
{
    int32_t value = (int32_t)wid;
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
    if (!number) return 0;
    const void *values[] = { number };
    CFArrayRef ids = CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
    CFRelease(number);
    if (!ids) return 0;
    CFTypeRef query = SLSWindowQueryWindows(cid, ids, 1);
    CFRelease(ids);
    if (!query) return 0;
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    uint64_t tags = 0;
    if (iterator && SLSWindowIteratorGetCount(iterator) == 1 && SLSWindowIteratorAdvance(iterator)) {
        tags = SLSWindowIteratorGetTags(iterator);
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
    return tags;
}

static void discover_highlight(struct fixture_manifest *manifest, CGRect target_bounds, CGFloat width)
{
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (!windows) return;
    CGRect expected = CGRectInset(target_bounds, -width, -width);
    int cid = SLSMainConnectionID();
    for (id value in (__bridge NSArray *)windows) {
        if (![value isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *dictionary = value;
        NSNumber *number = dictionary[(NSString *)kCGWindowNumber];
        NSDictionary *bounds_value = dictionary[(NSString *)kCGWindowBounds];
        CGRect bounds = CGRectZero;
        if (![number isKindOfClass:[NSNumber class]] ||
            ![bounds_value isKindOfClass:[NSDictionary class]] ||
            !CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)bounds_value, &bounds)) continue;
        if (fabs(bounds.origin.x - expected.origin.x) > 1 ||
            fabs(bounds.origin.y - expected.origin.y) > 1 ||
            fabs(bounds.size.width - expected.size.width) > 1 ||
            fabs(bounds.size.height - expected.size.height) > 1) continue;
        uint32_t wid = number.unsignedIntValue;
        uint64_t tags = window_tags(cid, wid);
        if ((tags & WINDOW_SERVER_TAG_IGNORE_FOR_EVENTS) && (tags & WINDOW_SERVER_TAG_FLOATING)) {
            manifest->highlight_id = wid;
            break;
        }
    }
    CFRelease(windows);
}

static bool platform(char os_version[64], char build[256], char architecture[64])
{
    NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
    snprintf(os_version, 64, "%ld.%ld.%ld", version.majorVersion, version.minorVersion,
             version.patchVersion);
    size_t build_size = 256;
    struct utsname system = {0};
    if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 ||
        !memchr(build, '\0', 256) || uname(&system) != 0) return false;
    snprintf(architecture, 64, "%s", system.machine);
    return true;
}

static bool emit_point(const char *fixture_id, const char *point_role, CGPoint point,
                       int iterations, const char *source_revision,
                       const char *fixture_revision,
                       const struct fixture_manifest *manifest)
{
    int cid = SLSMainConnectionID();
    struct window_server_hit_test_request request = {
        .connection = cid, .flags = 1, .reserved = 0, .screen_point = point
    };
    struct window_server_hit_test_comparison basic;
    window_server_hit_test_compare(&request, &basic);
    char os_version[64], build[256], architecture[64];
    if (!platform(os_version, build, architecture)) return false;
    if (!basic.geometry_available) {
        printf("{\"schema\":\"yabai.window-server-geometry-probe/v3\","
               "\"type\":\"point\",\"fixture_id\":\"%s\",\"point_role\":\"%s\","
               "\"os_version\":\"%s\",\"os_build\":\"%s\","
               "\"architecture\":\"%s\",\"source_revision\":\"%s\","
               "\"fixture_revision\":\"%s\",\"fixture_manifest_entries\":%zu,"
               "\"geometry_available\":false}\n",
               fixture_id, point_role, os_version, build, architecture,
               source_revision, fixture_revision, manifest ? manifest->count : 0);
        return true;
    }
    if (!result_valid(&basic.legacy) || !result_valid(&basic.geometry)) return false;

    struct relation_facts basic_facts = relation_facts(cid, basic.legacy.window_id,
                                                       basic.geometry.window_id);
    struct window_server_hit_test_comparison filtered = {0};
    bool filtered_ran = basic.legacy.status == kCGErrorSuccess && basic.legacy.window_id;
    if (filtered_ran) {
        request.filter_window_id = basic.legacy.window_id;
        request.flags = -1;
        window_server_hit_test_compare(&request, &filtered);
        if (!result_valid(&filtered.legacy) || !result_valid(&filtered.geometry)) return false;
    }
    struct relation_facts filtered_facts = relation_facts(cid, filtered.legacy.window_id,
                                                          filtered.geometry.window_id);

    CGRect legacy_bounds = CGRectZero, geometry_bounds = CGRectZero;
    bool legacy_contains = basic.legacy.status == kCGErrorSuccess &&
        bounds_contain(cid, basic.legacy.window_id, point, &legacy_bounds);
    bool geometry_contains = basic.geometry.status == kCGErrorSuccess &&
        bounds_contain(cid, basic.geometry.window_id, point, &geometry_bounds);
    bool legacy_local = legacy_contains && local_point_matches(point, basic.legacy.window_point, legacy_bounds);
    bool geometry_local = geometry_contains && local_point_matches(point, basic.geometry.window_point, geometry_bounds);

    sls_find_window_by_geometry_fn geometry = window_server_hit_test_geometry();
    uint64_t *legacy_ns = calloc((size_t)iterations, sizeof(*legacy_ns));
    uint64_t *geometry_ns = calloc((size_t)iterations, sizeof(*geometry_ns));
    if (!legacy_ns || !geometry_ns) { free(legacy_ns); free(geometry_ns); return false; }
    int legacy_errors = 0, geometry_errors = 0, disagreements = 0;
    request.filter_window_id = 0; request.flags = 1;
    for (int i = 0; i < iterations; ++i) {
        CGPoint lp = CGPointZero, gp = CGPointZero; uint32_t lw = 0, gw = 0; int lc = 0, gc = 0;
        uint64_t start = now_ns();
        OSStatus ls = SLSFindWindowAndOwner(cid, 0, 1, 0, &point, &lp, &lw, &lc);
        legacy_ns[i] = now_ns() - start;
        start = now_ns();
        CGError gs = geometry(cid, 0, 1, 0, &point, &gp, &gw, &gc);
        geometry_ns[i] = now_ns() - start;
        legacy_errors += ls != noErr; geometry_errors += gs != kCGErrorSuccess;
        disagreements += lw != gw || lc != gc;
    }
    qsort(legacy_ns, (size_t)iterations, sizeof(*legacy_ns), compare_u64);
    qsort(geometry_ns, (size_t)iterations, sizeof(*geometry_ns), compare_u64);
    int p95 = (iterations * 95 + 99) / 100 - 1;
    double tx = trunc(point.x), ty = trunc(point.y);
    NSScreen *screen = NSScreen.mainScreen;
    printf("{\"schema\":\"yabai.window-server-geometry-probe/v3\","
           "\"type\":\"point\",\"fixture_id\":\"%s\",\"point_role\":\"%s\","
           "\"os_version\":\"%s\",\"os_build\":\"%s\",\"architecture\":\"%s\","
           "\"source_revision\":\"%s\",\"fixture_revision\":\"%s\","
           "\"fixture_manifest_entries\":%zu,\"geometry_available\":true,"
           "\"abi\":{\"arguments\":8,"
           "\"legacy_internal_mode\":0,\"geometry_internal_mode\":1,"
           "\"verified_on_exact_build\":%s},"
           "\"display\":{\"count\":%lu,\"main_scale\":%.3f,"
           "\"multi_display_fixture\":%s},"
           "\"input\":{\"fractional\":%s,\"truncation_delta\":[%.3f,%.3f],"
           "\"basic_filter\":\"none\",\"basic_flags\":1,\"reserved\":0},"
           "\"basic\":{\"legacy_status\":%d,\"geometry_status\":%d,"
           "\"relation\":\"%s\",\"legacy_kind\":\"%s\","
           "\"geometry_kind\":\"%s\",\"same_owner\":%s,"
           "\"parent_metadata_known\":%s,\"association_metadata_known\":%s,"
           "\"legacy_bounds_contain\":%s,\"geometry_bounds_contain\":%s,"
           "\"legacy_local_matches\":%s,\"geometry_local_matches\":%s},"
           "\"filtered\":{\"ran\":%s,\"filter_alias\":\"legacy-result\","
           "\"flags\":-1,\"legacy_status\":%d,\"geometry_status\":%d,"
           "\"relation\":\"%s\",\"legacy_kind\":\"%s\","
           "\"geometry_kind\":\"%s\"},"
           "\"timing\":{\"iterations\":%d,\"legacy_p50_ns\":%llu,"
           "\"legacy_p95_ns\":%llu,\"geometry_p50_ns\":%llu,"
           "\"geometry_p95_ns\":%llu,\"legacy_errors\":%d,"
           "\"geometry_errors\":%d,\"disagreements\":%d},"
           "\"production_selector\":\"legacy\"}\n",
           fixture_id, point_role, os_version, build, architecture,
           source_revision, fixture_revision, manifest ? manifest->count : 0,
           !strcmp(os_version, "27.0.1") && !strcmp(build, "26A434") ? "true" : "false",
           (unsigned long)NSScreen.screens.count, screen ? screen.backingScaleFactor : 0.0,
           NSScreen.screens.count > 1 ? "true" : "false",
           point.x != tx || point.y != ty ? "true" : "false", point.x - tx, point.y - ty,
           basic.legacy.status, basic.geometry.status,
           detailed_relation(&basic, &basic_facts), fixture_kind(manifest, basic.legacy.window_id),
           fixture_kind(manifest, basic.geometry.window_id), basic.same_owner ? "true" : "false",
           basic_facts.parent_known ? "true" : "false",
           basic_facts.legacy_association_known && basic_facts.geometry_association_known ? "true" : "false",
           legacy_contains ? "true" : "false", geometry_contains ? "true" : "false",
           legacy_local ? "true" : "false", geometry_local ? "true" : "false",
           filtered_ran ? "true" : "false", filtered.legacy.status, filtered.geometry.status,
           filtered_ran ? detailed_relation(&filtered, &filtered_facts) : "unavailable",
           fixture_kind(manifest, filtered.legacy.window_id),
           fixture_kind(manifest, filtered.geometry.window_id),
           iterations,
           (unsigned long long)legacy_ns[iterations/2], (unsigned long long)legacy_ns[p95],
           (unsigned long long)geometry_ns[iterations/2], (unsigned long long)geometry_ns[p95],
           legacy_errors, geometry_errors, disagreements);
    free(legacy_ns); free(geometry_ns);
    return true;
}

static NSWindow *make_window(NSRect frame, NSWindowStyleMask style, NSWindowLevel level)
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:frame styleMask:style
                                                    backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed = NO;
    window.level = level;
    window.title = @"Controlled Fixture";
    return window;
}

static CGRect cg_bounds(uint32_t wid)
{
    CGRect bounds = CGRectZero;
    SLSGetWindowBounds(SLSMainConnectionID(), wid, &bounds);
    return bounds;
}

static bool run_controlled(int iterations, const char *source_revision,
                           const char *fixture_revision)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];
    struct fixture_manifest manifest = {0};
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                              NSWindowStyleMaskResizable;
    NSWindow *base = make_window(NSMakeRect(180, 180, 720, 460), style, NSNormalWindowLevel);
    [base makeKeyAndOrderFront:nil]; spin_ms(750); add_fixture(&manifest, base, "ordinary-window");
    CGRect base_bounds = cg_bounds((uint32_t)base.windowNumber);
    discover_highlight(&manifest, base_bounds, 2.0);
    bool success = emit_point("ordinary", "client-center",
        CGPointMake(CGRectGetMidX(base_bounds), CGRectGetMidY(base_bounds)), iterations,
        source_revision, fixture_revision, &manifest);
    success &= emit_point("ordinary", "rounded-corner-cutout",
        CGPointMake(CGRectGetMinX(base_bounds) + 1.25, CGRectGetMinY(base_bounds) + 1.25),
        iterations, source_revision, fixture_revision, &manifest);
    success &= emit_point("active-highlight", "inner-edge",
        CGPointMake(CGRectGetMinX(base_bounds) + 1.0, CGRectGetMidY(base_bounds)),
        iterations, source_revision, fixture_revision, &manifest);

    NSWindow *overlap = make_window(NSMakeRect(base.frame.origin.x + 180,
        base.frame.origin.y + 120, 520, 320), style, NSNormalWindowLevel);
    [overlap makeKeyAndOrderFront:nil]; spin_ms(300); add_fixture(&manifest, overlap, "overlap-front");
    CGRect overlap_bounds = cg_bounds((uint32_t)overlap.windowNumber);
    success &= emit_point("overlap-same-process", "shared-interior",
        CGPointMake(CGRectGetMidX(overlap_bounds), CGRectGetMidY(overlap_bounds)), iterations,
        source_revision, fixture_revision, &manifest);

    NSPanel *panel = (NSPanel *)make_window(NSMakeRect(base.frame.origin.x + 220,
        base.frame.origin.y + 150, 420, 240), style | NSWindowStyleMaskUtilityWindow,
        NSFloatingWindowLevel);
    [panel makeKeyAndOrderFront:nil]; spin_ms(300); add_fixture(&manifest, panel, "utility-panel");
    CGRect panel_bounds = cg_bounds((uint32_t)panel.windowNumber);
    success &= emit_point("panel", "panel-center",
        CGPointMake(CGRectGetMidX(panel_bounds), CGRectGetMidY(panel_bounds)), iterations,
        source_revision, fixture_revision, &manifest);

    NSPanel *clickthrough = (NSPanel *)make_window(NSMakeRect(base.frame.origin.x + 120,
        base.frame.origin.y + 80, 440, 260), NSWindowStyleMaskBorderless, NSFloatingWindowLevel);
    clickthrough.opaque = NO;
    clickthrough.backgroundColor = NSColor.clearColor;
    clickthrough.ignoresMouseEvents = YES;
    [clickthrough orderFront:nil]; spin_ms(300); add_fixture(&manifest, clickthrough, "click-through-panel");
    CGRect click_bounds = cg_bounds((uint32_t)clickthrough.windowNumber);
    success &= emit_point("click-through", "transparent-center",
        CGPointMake(CGRectGetMidX(click_bounds), CGRectGetMidY(click_bounds)), iterations,
        source_revision, fixture_revision, &manifest);

    NSPanel *sheet = [[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,480,280)
        styleMask:style backing:NSBackingStoreBuffered defer:NO];
    sheet.releasedWhenClosed = NO;
    [base beginSheet:sheet completionHandler:nil]; spin_ms(400); add_fixture(&manifest, sheet, "attached-sheet");
    CGRect sheet_bounds = cg_bounds((uint32_t)sheet.windowNumber);
    success &= emit_point("sheet", "sheet-center",
        CGPointMake(CGRectGetMidX(sheet_bounds), CGRectGetMidY(sheet_bounds)), iterations,
        source_revision, fixture_revision, &manifest);
    [base endSheet:sheet]; [sheet close];

    CGRect display = CGDisplayBounds(CGMainDisplayID());
    success &= emit_point("display-edge", "left-inside",
        CGPointMake(CGRectGetMinX(display) + 0.75, CGRectGetMidY(display)), iterations,
        source_revision, fixture_revision, &manifest);
    success &= emit_point("display-edge", "left-exact",
        CGPointMake(CGRectGetMinX(display), CGRectGetMidY(display)), iterations,
        source_revision, fixture_revision, &manifest);
    success &= emit_point("display-edge", "left-outside",
        CGPointMake(CGRectGetMinX(display) - 0.75, CGRectGetMidY(display)), iterations,
        source_revision, fixture_revision, &manifest);

    [clickthrough close]; [panel close]; [overlap close]; [base close];
    return success;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s (--controlled | --point X,Y) --source-revision REV "
            "--fixture-revision REV [--iterations N] [--fixture-manifest PATH]\n", program);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        CGPoint point = CGPointZero;
        bool controlled = false, explicit_point = false;
        int iterations = 100;
        const char *source_revision = NULL, *fixture_revision = NULL;
        const char *fixture_manifest_path = NULL;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--controlled") == 0) controlled = true;
            else if (strcmp(argv[i], "--point") == 0 && i + 1 < argc) {
                if (!parse_point(argv[++i], &point)) return EXIT_FAILURE;
                explicit_point = true;
            } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
                iterations = atoi(argv[++i]);
                if (iterations < 1 || iterations > 10000) return EXIT_FAILURE;
            } else if (strcmp(argv[i], "--source-revision") == 0 && i + 1 < argc) {
                source_revision = argv[++i];
            } else if (strcmp(argv[i], "--fixture-revision") == 0 && i + 1 < argc) {
                fixture_revision = argv[++i];
            } else if (strcmp(argv[i], "--fixture-manifest") == 0 && i + 1 < argc) {
                fixture_manifest_path = argv[++i];
            } else if (strcmp(argv[i], "--help") == 0) {
                usage(argv[0]); return EXIT_SUCCESS;
            } else { usage(argv[0]); return EXIT_FAILURE; }
        }
        if (controlled == explicit_point || !safe_token(source_revision) ||
            !safe_token(fixture_revision)) {
            usage(argv[0]); return EXIT_FAILURE;
        }
        if (controlled && fixture_manifest_path) return EXIT_FAILURE;
        if (controlled) return run_controlled(iterations, source_revision, fixture_revision)
                            ? EXIT_SUCCESS : EXIT_FAILURE;
        struct fixture_manifest manifest = {0};
        if (fixture_manifest_path && !load_fixture_manifest(fixture_manifest_path, &manifest)) {
            return EXIT_FAILURE;
        }
        return emit_point("manual", "manual-point", point, iterations, source_revision,
                          fixture_revision, fixture_manifest_path ? &manifest : NULL)
             ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
