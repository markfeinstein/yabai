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
            puts("{\"schema\":\"yabai.window-server-geometry-probe/v1\",\"geometry_available\":false}");
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
        printf("{\"schema\":\"yabai.window-server-geometry-probe/v1\","
               "\"os_version\":\"%ld.%ld.%ld\",\"os_build\":\"%s\","
               "\"architecture\":\"%s\",\"geometry_available\":true,"
               "\"basic_same_window\":%s,\"basic_same_owner\":%s,"
               "\"basic_same_window_point\":%s,\"filtered_ran\":%s,"
               "\"filtered_same_window\":%s,\"filtered_same_owner\":%s,"
               "\"filtered_same_window_point\":%s,\"iterations\":%d,"
               "\"legacy_p50_ns\":%llu,\"legacy_p95_ns\":%llu,"
               "\"geometry_p50_ns\":%llu,\"geometry_p95_ns\":%llu,"
               "\"legacy_errors\":%d,\"geometry_errors\":%d,\"disagreements\":%d",
               version.majorVersion, version.minorVersion, version.patchVersion, build, system.machine,
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
