#import <AppKit/AppKit.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include "../src/misc/extern.h"
#include "../src/window_server_hit_test.c"

static int compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
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

static uint32_t parent_id(int cid, uint32_t wid)
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
    uint32_t parent = 0;
    if (iterator && SLSWindowIteratorGetCount(iterator) == 1 && SLSWindowIteratorAdvance(iterator)) {
        parent = SLSWindowIteratorGetParentID(iterator);
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
    return parent;
}

static bool associated(int cid, uint32_t a, uint32_t b)
{
    CFArrayRef values = SLSCopyAssociatedWindows(cid, a);
    bool result = false;
    if (values && CFGetTypeID(values) == CFArrayGetTypeID()) {
        for (CFIndex i = 0; i < CFArrayGetCount(values); ++i) {
            CFTypeRef item = CFArrayGetValueAtIndex(values, i);
            uint32_t wid = 0;
            if (item && CFGetTypeID(item) == CFNumberGetTypeID() &&
                CFNumberGetValue((CFNumberRef)item, kCFNumberSInt32Type, &wid) && wid == b) {
                result = true; break;
            }
        }
    }
    if (values) CFRelease(values);
    return result;
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

static uint64_t now_ns(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        CGPoint point = CGPointZero; bool explicit_point = false, verbose = false; int iterations = 100;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--point") == 0 && i + 1 < argc) {
                if (!parse_point(argv[++i], &point)) return EXIT_FAILURE;
                explicit_point = true;
            } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
                iterations = atoi(argv[++i]); if (iterations < 1 || iterations > 10000) return EXIT_FAILURE;
            } else if (strcmp(argv[i], "--verbose-ids") == 0) verbose = true;
            else return EXIT_FAILURE;
        }
        int cid = SLSMainConnectionID();
        if (!explicit_point && SLSGetCurrentCursorLocation(cid, &point) != kCGErrorSuccess) return EXIT_FAILURE;
        struct window_server_hit_test_request request = {
            .connection = cid, .flags = 1, .reserved = 0, .screen_point = point
        };
        struct window_server_hit_test_comparison basic;
        window_server_hit_test_compare(&request, &basic);
        if (!basic.geometry_available) {
            puts("{\"schema\":\"yabai.window-server-geometry-probe/v2\",\"geometry_available\":false}");
            return 2;
        }
        if (!result_valid(&basic.legacy) || !result_valid(&basic.geometry)) return EXIT_FAILURE;

        struct window_server_hit_test_comparison filtered = {0};
        bool filtered_ran = basic.legacy.status == kCGErrorSuccess && basic.legacy.window_id;
        if (filtered_ran) {
            request.filter_window_id = basic.legacy.window_id;
            request.flags = -1;
            window_server_hit_test_compare(&request, &filtered);
            if (!result_valid(&filtered.legacy) || !result_valid(&filtered.geometry)) return EXIT_FAILURE;
        }

        uint32_t legacy_parent = basic.legacy.status == kCGErrorSuccess
                               ? parent_id(cid, basic.legacy.window_id) : 0;
        uint32_t geometry_parent = basic.geometry.status == kCGErrorSuccess
                                 ? parent_id(cid, basic.geometry.window_id) : 0;
        bool parent_child = basic.legacy.window_id && basic.geometry.window_id &&
            (legacy_parent == basic.geometry.window_id || geometry_parent == basic.legacy.window_id);
        bool is_associated = basic.legacy.window_id && basic.geometry.window_id &&
            (associated(cid, basic.legacy.window_id, basic.geometry.window_id) ||
             associated(cid, basic.geometry.window_id, basic.legacy.window_id));
        enum window_server_hit_test_relation relation = window_server_hit_test_classify_relation(
            basic.legacy.status == kCGErrorSuccess && basic.geometry.status == kCGErrorSuccess,
            basic.same_window, parent_child, is_associated, basic.same_owner);
        CGRect legacy_bounds = CGRectZero, geometry_bounds = CGRectZero;
        bool legacy_contains = basic.legacy.status == kCGErrorSuccess &&
            bounds_contain(cid, basic.legacy.window_id, point, &legacy_bounds);
        bool geometry_contains = basic.geometry.status == kCGErrorSuccess &&
            bounds_contain(cid, basic.geometry.window_id, point, &geometry_bounds);
        bool legacy_local_matches = legacy_contains &&
            local_point_matches(point, basic.legacy.window_point, legacy_bounds);
        bool geometry_local_matches = geometry_contains &&
            local_point_matches(point, basic.geometry.window_point, geometry_bounds);

        sls_find_window_by_geometry_fn geometry = window_server_hit_test_geometry();
        uint64_t *legacy_ns = calloc((size_t)iterations, sizeof(*legacy_ns));
        uint64_t *geometry_ns = calloc((size_t)iterations, sizeof(*geometry_ns));
        if (!legacy_ns || !geometry_ns) { free(legacy_ns); free(geometry_ns); return EXIT_FAILURE; }
        int legacy_errors = 0, geometry_errors = 0, disagreements = 0;
        request.filter_window_id = 0; request.flags = 1;
        for (int warmup = 0; warmup < 10; ++warmup) {
            struct window_server_hit_test_comparison value; window_server_hit_test_compare(&request, &value);
        }
        for (int i = 0; i < iterations; ++i) {
            CGPoint lp = CGPointZero, gp = CGPointZero; uint32_t lw = 0, gw = 0; int lc = 0, gc = 0;
            uint64_t start;
            if (i & 1) {
                start = now_ns();
                CGError gs = geometry(cid, 0, 1, 0, &point, &gp, &gw, &gc);
                geometry_ns[i] = now_ns() - start;
                start = now_ns();
                OSStatus ls = SLSFindWindowAndOwner(cid, 0, 1, 0, &point, &lp, &lw, &lc);
                legacy_ns[i] = now_ns() - start;
                geometry_errors += gs != kCGErrorSuccess; legacy_errors += ls != noErr;
            } else {
                start = now_ns();
                OSStatus ls = SLSFindWindowAndOwner(cid, 0, 1, 0, &point, &lp, &lw, &lc);
                legacy_ns[i] = now_ns() - start;
                start = now_ns();
                CGError gs = geometry(cid, 0, 1, 0, &point, &gp, &gw, &gc);
                geometry_ns[i] = now_ns() - start;
                geometry_errors += gs != kCGErrorSuccess; legacy_errors += ls != noErr;
            }
            disagreements += lw != gw || lc != gc;
        }
        qsort(legacy_ns, (size_t)iterations, sizeof(*legacy_ns), compare_u64);
        qsort(geometry_ns, (size_t)iterations, sizeof(*geometry_ns), compare_u64);
        int p95 = (iterations * 95 + 99) / 100 - 1;
        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char build[256] = {0}; size_t build_size = sizeof(build); struct utsname system;
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 || uname(&system) != 0) return EXIT_FAILURE;
        printf("{\"schema\":\"yabai.window-server-geometry-probe/v2\","
               "\"os_version\":\"%ld.%ld.%ld\",\"os_build\":\"%s\","
               "\"architecture\":\"%s\",\"geometry_available\":true,"
               "\"legacy_internal_mode\":0,\"geometry_internal_mode\":1,"
               "\"basic_relation\":\"%s\",\"basic_parent_child\":%s,"
               "\"basic_associated\":%s,\"legacy_bounds_contain_point\":%s,"
               "\"geometry_bounds_contain_point\":%s,\"legacy_local_point_matches_bounds\":%s,"
               "\"geometry_local_point_matches_bounds\":%s,"
               "\"basic_same_window\":%s,\"basic_same_owner\":%s,"
               "\"basic_same_window_point\":%s,\"filtered_ran\":%s,"
               "\"filtered_same_window\":%s,\"filtered_same_owner\":%s,"
               "\"filtered_same_window_point\":%s,\"iterations\":%d,"
               "\"legacy_p50_ns\":%llu,\"legacy_p95_ns\":%llu,"
               "\"geometry_p50_ns\":%llu,\"geometry_p95_ns\":%llu,"
               "\"legacy_errors\":%d,\"geometry_errors\":%d,\"disagreements\":%d",
               version.majorVersion, version.minorVersion, version.patchVersion, build, system.machine,
               window_server_hit_test_relation_string(relation), parent_child ? "true" : "false",
               is_associated ? "true" : "false", legacy_contains ? "true" : "false",
               geometry_contains ? "true" : "false", legacy_local_matches ? "true" : "false",
               geometry_local_matches ? "true" : "false",
               basic.same_window ? "true" : "false", basic.same_owner ? "true" : "false",
               basic.same_window_point ? "true" : "false", filtered_ran ? "true" : "false",
               filtered.same_window ? "true" : "false", filtered.same_owner ? "true" : "false",
               filtered.same_window_point ? "true" : "false", iterations,
               (unsigned long long)legacy_ns[iterations/2], (unsigned long long)legacy_ns[p95],
               (unsigned long long)geometry_ns[iterations/2], (unsigned long long)geometry_ns[p95],
               legacy_errors, geometry_errors, disagreements);
        if (verbose) printf(",\"legacy_window_id\":%u,\"geometry_window_id\":%u", basic.legacy.window_id, basic.geometry.window_id);
        puts("}");
        free(legacy_ns); free(geometry_ns);
        return EXIT_SUCCESS;
    }
}
