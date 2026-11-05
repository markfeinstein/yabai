#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <time.h>

#include "../src/misc/extern.h"
#include "../src/window_corner_geometry.c"

typedef CFArrayRef (*corner_array_getter)(CFTypeRef iterator);
typedef uint32_t (*corner_mask_getter)(CFTypeRef iterator);

struct array_observation
{
    bool called;
    const char *type;
    CFIndex count;
    uint8_t numeric_mask;
    uint8_t finite_mask;
    uint8_t nonnegative_mask;
    double slots[4];
    bool normalized;
    bool repeat_same_pointer;
    bool repeat_same_values;
};

struct corner_observation
{
    uint32_t before_window_id;
    uint32_t after_window_id;
    struct array_observation raw;
    struct array_observation resolved;
    uint32_t mask_flags;
    bool mask_called;
    bool mask_repeat_same;
    uint64_t latency_ns;
};

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

static const char *cf_type(CFTypeRef value)
{
    if (!value) return "null";
    CFTypeID type = CFGetTypeID(value);
    if (type == CFArrayGetTypeID()) return "array";
    if (type == CFNumberGetTypeID()) return "number";
    if (type == CFDataGetTypeID()) return "data";
    if (type == CFDictionaryGetTypeID()) return "dictionary";
    if (type == CFStringGetTypeID()) return "string";
    return "other";
}

static struct array_observation observe_array(CFArrayRef first, CFArrayRef second,
                                              enum window_corner_geometry_source source)
{
    struct array_observation result = {
        .called = true,
        .type = cf_type(first),
        .count = first && CFGetTypeID(first) == CFArrayGetTypeID() ? CFArrayGetCount(first) : -1,
        .repeat_same_pointer = first == second
    };
    if (result.count == 4) {
        for (CFIndex i = 0; i < 4; ++i) {
            CFTypeRef item = CFArrayGetValueAtIndex(first, i);
            double value = NAN;
            bool numeric = item && CFGetTypeID(item) == CFNumberGetTypeID() &&
                           CFNumberGetValue((CFNumberRef)item, kCFNumberDoubleType, &value);
            if (numeric) result.numeric_mask |= 1u << i;
            if (numeric && isfinite(value)) result.finite_mask |= 1u << i;
            if (numeric && isfinite(value) && value >= 0.0) result.nonnegative_mask |= 1u << i;
            result.slots[i] = value;
        }
        struct window_corner_geometry normalized = {0}, repeated = {0};
        result.normalized = window_corner_geometry_normalize(first, source, &normalized);
        bool repeat_valid = window_corner_geometry_normalize(second, source, &repeated);
        result.repeat_same_values = result.normalized && repeat_valid;
        for (size_t i = 0; i < 4 && result.repeat_same_values; ++i) {
            result.repeat_same_values &= fabs(normalized.radius[i] - repeated.radius[i]) <= 0.01;
        }
    }
    return result;
}

static struct corner_observation observe_corner(CFTypeRef iterator,
                                                corner_array_getter raw,
                                                corner_array_getter resolved,
                                                corner_mask_getter mask,
                                                bool abi_verified)
{
    struct corner_observation result = {0};
    result.before_window_id = SLSWindowIteratorGetWindowID(iterator);
    uint64_t start = now_ns();
    if (abi_verified && raw) {
        CFArrayRef raw_a = raw(iterator);
        CFArrayRef raw_b = raw(iterator);
        result.raw = observe_array(raw_a, raw_b, WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RAW);
        if (raw_a) CFRelease(raw_a);
        if (raw_b) CFRelease(raw_b);
    }
    if (abi_verified && resolved) {
        CFArrayRef resolved_a = resolved(iterator);
        CFArrayRef resolved_b = resolved(iterator);
        result.resolved = observe_array(resolved_a, resolved_b,
                                        WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED);
        if (resolved_a) CFRelease(resolved_a);
        if (resolved_b) CFRelease(resolved_b);
    }
    if (abi_verified && mask) {
        result.mask_called = true;
        result.mask_flags = mask(iterator);
        result.mask_repeat_same = result.mask_flags == mask(iterator);
    }
    result.latency_ns = now_ns() - start;
    result.after_window_id = SLSWindowIteratorGetWindowID(iterator);
    return result;
}

static void print_array(const char *name, const struct array_observation *value)
{
    printf("\"%s\":{\"called\":%s,\"result_cf_type\":\"%s\",\"count\":%ld,"
           "\"numeric_mask\":\"0x%02x\",\"finite_mask\":\"0x%02x\","
           "\"nonnegative_mask\":\"0x%02x\",\"slots\":[",
           name, value->called ? "true" : "false", value->called ? value->type : "not-called",
           (long)(value->called ? value->count : -1), value->numeric_mask,
           value->finite_mask, value->nonnegative_mask);
    for (int i = 0; i < 4; ++i) {
        if (i) putchar(',');
        if (value->finite_mask & (1u << i)) printf("%.6f", value->slots[i]); else fputs("null", stdout);
    }
    printf("],\"normalized\":%s,\"repeat_same_pointer\":%s,"
           "\"repeat_same_values\":%s}",
           value->normalized ? "true" : "false",
           value->repeat_same_pointer ? "true" : "false",
           value->repeat_same_values ? "true" : "false");
}

static NSArray<NSDictionary *> *fixtures(void)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
    NSMutableArray<NSDictionary *> *result = [NSMutableArray array];
    NSWindowStyleMask titled = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                               NSWindowStyleMaskResizable;
    NSArray<NSString *> *labels = @[
        @"standard-titled", @"opaque-borderless", @"content-layer-uniform",
        @"content-layer-top-left", @"content-layer-top-right",
        @"content-layer-bottom-left", @"content-layer-bottom-right",
        @"content-layer-clamped", @"utility-panel"
    ];
    CACornerMask masks[] = {
        0, 0, kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner |
              kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner,
        kCALayerMinXMinYCorner, kCALayerMaxXMinYCorner,
        kCALayerMinXMaxYCorner, kCALayerMaxXMaxYCorner,
        kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner |
        kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner, 0
    };
    for (NSUInteger i = 0; i < labels.count; ++i) {
        bool titled_fixture = i == 0 || i == 8;
        NSRect rect = NSMakeRect(80 + (CGFloat)(i % 5) * 160,
                                 100 + (CGFloat)(i / 5) * 150,
                                 i == 7 ? 80 : 145, i == 7 ? 80 : 105);
        NSWindowStyleMask style = titled_fixture ? titled :
            (NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable);
        NSWindow *window = i == 8
            ? (NSWindow *)[[NSPanel alloc] initWithContentRect:rect
                styleMask:style | NSWindowStyleMaskUtilityWindow
                  backing:NSBackingStoreBuffered defer:NO]
            : [[NSWindow alloc] initWithContentRect:rect styleMask:style
                                            backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO;
        window.ignoresMouseEvents = YES;
        window.title = @"Controlled Fixture";
        if (i >= 2 && i <= 7) {
            window.opaque = NO;
            window.backgroundColor = NSColor.clearColor;
            window.contentView.wantsLayer = YES;
            window.contentView.layer.backgroundColor =
                [NSColor colorWithCalibratedRed:0.15 green:0.45 blue:0.85 alpha:0.7].CGColor;
            window.contentView.layer.cornerRadius = i == 7 ? 60.0 : 24.0;
            window.contentView.layer.maskedCorners = masks[i];
            window.contentView.layer.masksToBounds = YES;
        } else {
            window.backgroundColor = NSColor.systemBlueColor;
        }
        [window orderFront:nil];
        [result addObject:@{ @"label":labels[i], @"window":window,
                             @"expected": i == 0 || i == 8 ? @"uniform-or-opaque" : @"none" }];
    }
    NSWindow *parent = [[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,600,400)
        styleMask:titled backing:NSBackingStoreBuffered defer:NO];
    parent.releasedWhenClosed = NO;
    NSPanel *sheet = [[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,440,260)
        styleMask:titled backing:NSBackingStoreBuffered defer:NO];
    sheet.releasedWhenClosed = NO;
    [parent orderFront:nil];
    [parent beginSheet:sheet completionHandler:nil];
    [result addObject:@{ @"label":@"attached-sheet", @"window":sheet,
                         @"parent":parent, @"expected":@"natural-asymmetry-control" }];
    [CATransaction flush];
    spin_ms(500);
    return result;
}

static CFArrayRef fixture_ids(NSArray<NSDictionary *> *fixtures)
{
    CFMutableArrayRef ids = CFArrayCreateMutable(NULL, (CFIndex)fixtures.count,
                                                 &kCFTypeArrayCallBacks);
    if (!ids) return NULL;
    for (NSDictionary *fixture in fixtures) {
        int32_t value = (int32_t)[fixture[@"window"] windowNumber];
        CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
        if (!number) { CFRelease(ids); return NULL; }
        CFArrayAppendValue(ids, number); CFRelease(number);
    }
    return ids;
}

static bool capture(NSArray<NSDictionary *> *fixtures, const char *phase, int cycle,
                    unsigned settle_ms, corner_array_getter raw,
                    corner_array_getter resolved, corner_mask_getter mask,
                    bool abi_verified, int *valid_raw, int *valid_resolved,
                    int *asymmetric)
{
    CFArrayRef ids = fixture_ids(fixtures);
    if (!ids || CFArrayGetCount(ids) > INT_MAX) { if (ids) CFRelease(ids); return false; }
    CFTypeRef query = SLSWindowQueryWindows(SLSMainConnectionID(), ids, (int)CFArrayGetCount(ids));
    CFRelease(ids);
    if (!query) return false;
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    CFRelease(query);
    if (!iterator) return false;
    size_t index = 0;
    while (index < fixtures.count && SLSWindowIteratorAdvance(iterator)) {
        NSDictionary *fixture = fixtures[index++];
        NSWindow *window = fixture[@"window"];
        struct corner_observation value = observe_corner(iterator, raw, resolved, mask, abi_verified);
        bool raw_valid = value.raw.normalized;
        bool resolved_valid = value.resolved.normalized;
        if (raw_valid) ++*valid_raw;
        if (resolved_valid) ++*valid_resolved;
        bool asym = false;
        if (resolved_valid) for (int slot = 1; slot < 4; ++slot) {
            asym |= fabs(value.resolved.slots[slot] - value.resolved.slots[0]) > 0.01;
        }
        if (asym) ++*asymmetric;
        printf("{\"schema\":\"yabai.window-corner-geometry-probe/v3\","
               "\"type\":\"observation\",\"fixture\":\"%s\","
               "\"expected_windowserver_effect\":\"%s\",\"phase\":\"%s\","
               "\"cycle\":%d,\"settle_ms\":%u,\"frame\":{\"width\":%.3f,"
               "\"height\":%.3f},\"iterator_window_stable\":%s,",
               [fixture[@"label"] UTF8String], [fixture[@"expected"] UTF8String],
               phase, cycle, settle_ms, window.frame.size.width, window.frame.size.height,
               value.before_window_id && value.before_window_id == value.after_window_id ? "true" : "false");
        print_array("raw_radii", &value.raw); putchar(',');
        print_array("resolved_radii", &value.resolved);
        fputs(",\"raw_vs_resolved\":{\"equal_mask\":\"0x", stdout);
        uint8_t equal_mask = 0;
        for (int slot = 0; slot < 4; ++slot) {
            if ((value.raw.finite_mask & (1u << slot)) &&
                (value.resolved.finite_mask & (1u << slot)) &&
                fabs(value.raw.slots[slot] - value.resolved.slots[slot]) <= 0.01) {
                equal_mask |= 1u << slot;
            }
        }
        printf("%02x\",\"delta\":[", equal_mask);
        for (int slot = 0; slot < 4; ++slot) {
            if (slot) putchar(',');
            if ((value.raw.finite_mask & (1u << slot)) &&
                (value.resolved.finite_mask & (1u << slot))) {
                printf("%.6f", value.resolved.slots[slot] - value.raw.slots[slot]);
            } else fputs("null", stdout);
        }
        printf("],\"physical_slot_order\":\"unknown\"},"
               "\"mask_flags\":{\"called\":%s,\"value_u32\":%u,"
               "\"value_hex\":\"0x%08x\",\"repeat_same\":%s},"
               "\"copy_mask\":{\"abi_state\":\"partial\",\"called\":false},"
               "\"getter_latency_ns\":%llu,\"collection_enabled\":false,"
               "\"proxy_rendering_enabled\":false}\n",
               value.mask_called ? "true" : "false", value.mask_flags, value.mask_flags,
               value.mask_repeat_same ? "true" : "false",
               (unsigned long long)value.latency_ns);
    }
    CFRelease(iterator);
    return index == fixtures.count;
}

static void close_fixtures(NSArray<NSDictionary *> *values)
{
    for (NSDictionary *fixture in values) {
        NSWindow *window = fixture[@"window"];
        NSWindow *parent = fixture[@"parent"];
        if (parent && window.sheetParent == parent) [parent endSheet:window];
        [window close];
        if (parent) [parent close];
    }
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --source-revision REV --fixture-revision REV\n", program);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *source_revision = NULL, *fixture_revision = NULL;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--source-revision") == 0 && i + 1 < argc) source_revision = argv[++i];
            else if (strcmp(argv[i], "--fixture-revision") == 0 && i + 1 < argc) fixture_revision = argv[++i];
            else if (strcmp(argv[i], "--help") == 0) { usage(argv[0]); return EXIT_SUCCESS; }
            else { usage(argv[0]); return EXIT_FAILURE; }
        }
        if (!safe_token(source_revision) || !safe_token(fixture_revision)) {
            usage(argv[0]); return EXIT_FAILURE;
        }
        corner_array_getter raw = (corner_array_getter)dlsym(RTLD_DEFAULT,
            "SLSWindowIteratorGetCornerRadii");
        corner_array_getter resolved = (corner_array_getter)dlsym(RTLD_DEFAULT,
            "SLSWindowIteratorGetResolvedCornerRadii");
        corner_mask_getter mask = (corner_mask_getter)dlsym(RTLD_DEFAULT,
            "SLSWindowIteratorGetCornerMaskFlags");
        bool copy_mask_available = dlsym(RTLD_DEFAULT, "SLSCopyWindowCornerMask") != NULL;
        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char build[256] = {0}; size_t build_size = sizeof(build); struct utsname system = {0};
        int translated = 0; size_t translated_size = sizeof(translated);
        (void)sysctlbyname("sysctl.proc_translated", &translated, &translated_size, NULL, 0);
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 ||
            !memchr(build, '\0', sizeof(build)) || uname(&system) != 0) return EXIT_FAILURE;
        bool abi_verified = version.majorVersion == 27 && version.minorVersion == 0 &&
                            version.patchVersion == 1 && strcmp(build, "26A434") == 0;
        printf("{\"schema\":\"yabai.window-corner-geometry-probe/v3\",\"type\":\"run\","
               "\"source_revision\":\"%s\",\"fixture_revision\":\"%s\","
               "\"os_version\":\"%ld.%ld.%ld\",\"os_build\":\"%s\","
               "\"process_arch\":\"%s\",\"host_arch\":\"%s\",\"translated\":%s,"
               "\"pointer_bits\":%zu,\"cgfloat_bits\":%zu,\"compiler\":\"%s\","
               "\"sdk_max_allowed\":%d,\"deployment_target\":\"14.0\","
               "\"symbols\":{\"raw\":%s,\"resolved\":%s,\"mask_flags\":%s,"
               "\"copy_mask\":%s},\"abi\":{\"raw\":\"%s\","
               "\"resolved\":\"%s\",\"mask_flags\":\"%s\","
               "\"copy_mask\":\"partial\",\"verified_build\":\"26A434\"},"
               "\"copy_mask_called\":false,\"physical_slot_order\":\"unknown\","
               "\"collection_enabled\":false,\"proxy_rendering_enabled\":false}\n",
               source_revision, fixture_revision,
               version.majorVersion, version.minorVersion, version.patchVersion, build,
               system.machine, system.machine, translated ? "true" : "false",
               sizeof(void *) * 8, sizeof(CGFloat) * 8, __clang_version__,
               __MAC_OS_X_VERSION_MAX_ALLOWED,
               raw ? "true" : "false", resolved ? "true" : "false",
               mask ? "true" : "false", copy_mask_available ? "true" : "false",
               abi_verified ? "verified" : "unverified",
               abi_verified ? "verified" : "unverified",
               abi_verified ? "verified" : "unverified");

        NSArray<NSDictionary *> *values = fixtures();
        int valid_raw = 0, valid_resolved = 0, asymmetric = 0, observations = 0;
        bool success = true;
        for (int cycle = 1; cycle <= 5; ++cycle) {
            success &= capture(values, "steady", cycle, 500, raw, resolved, mask,
                               abi_verified, &valid_raw, &valid_resolved, &asymmetric);
            observations += (int)values.count;
        }
        NSDictionary *standard = values[0], *borderless = values[1];
        NSArray<NSDictionary *> *resize_values = @[standard, borderless];
        NSWindow *standard_window = standard[@"window"];
        NSWindow *borderless_window = borderless[@"window"];
        NSRect standard_baseline = standard_window.frame;
        NSRect borderless_baseline = borderless_window.frame;
        for (int cycle = 1; cycle <= 5; ++cycle) {
            NSRect phases[] = {
                standard_baseline,
                NSMakeRect(standard_baseline.origin.x, standard_baseline.origin.y,
                           standard_baseline.size.width / 2, standard_baseline.size.height),
                NSMakeRect(standard_baseline.origin.x, standard_baseline.origin.y,
                           standard_baseline.size.width, standard_baseline.size.height / 2),
                NSMakeRect(standard_baseline.origin.x, standard_baseline.origin.y, 120, 80),
                standard_baseline
            };
            const char *labels[] = { "baseline", "half-width", "half-height", "smallest", "restored" };
            for (int phase = 0; phase < 5; ++phase) {
                [standard_window setFrame:phases[phase] display:YES animate:NO];
                NSRect border = phases[phase];
                border.origin = borderless_baseline.origin;
                [borderless_window setFrame:border display:YES animate:NO];
                [CATransaction flush]; spin_ms(250);
                success &= capture(resize_values, labels[phase], cycle, 250,
                                   raw, resolved, mask, abi_verified,
                                   &valid_raw, &valid_resolved, &asymmetric);
                observations += (int)resize_values.count;
            }
        }
        printf("{\"schema\":\"yabai.window-corner-geometry-probe/v3\",\"type\":\"summary\","
               "\"observations\":%d,\"valid_raw\":%d,\"valid_resolved\":%d,"
               "\"asymmetric_resolved\":%d,\"steady_batches_per_fixture\":5,"
               "\"resize_cycles\":5,\"fullscreen_cycles\":0,\"tile_cycles\":0,"
               "\"physical_slot_order\":\"unknown\","
               "\"external_blockers\":[\"macos-26.6-unavailable\","
               "\"macos-27.2-unavailable\",\"native-tile-fixture-unavailable\"],"
               "\"collection_enabled\":false,\"proxy_rendering_enabled\":false}\n",
               observations, valid_raw, valid_resolved, asymmetric);
        close_fixtures(values);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
