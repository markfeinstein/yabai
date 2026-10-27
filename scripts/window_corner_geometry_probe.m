#import <AppKit/AppKit.h>

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <time.h>

#include "../src/misc/extern.h"
#include "../src/window_corner_geometry.c"

typedef CFArrayRef (*window_corner_geometry_probe_getter)(CFTypeRef iterator,
                                                           uint32_t window_id);

static int window_corner_geometry_probe_compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *) left;
    uint64_t b = *(const uint64_t *) right;
    return (a > b) - (a < b);
}

static uint64_t window_corner_geometry_probe_now_ns(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static bool window_corner_geometry_probe_parse_count(const char *value, int maximum, int *result)
{
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || *end || parsed < 0 || parsed > maximum) return false;
    *result = (int) parsed;
    return true;
}

static const char *window_corner_geometry_probe_type(CFTypeRef value)
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

static CFMutableArrayRef window_corner_geometry_probe_window_ids(void)
{
    CFArrayRef info = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (!info || CFGetTypeID(info) != CFArrayGetTypeID()) {
        if (info) CFRelease(info);
        return NULL;
    }

    CFMutableArrayRef ids = CFArrayCreateMutable(NULL, CFArrayGetCount(info), &kCFTypeArrayCallBacks);
    if (!ids) { CFRelease(info); return NULL; }
    for (CFIndex i = 0; i < CFArrayGetCount(info); ++i) {
        CFTypeRef item = CFArrayGetValueAtIndex(info, i);
        if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID()) continue;
        CFTypeRef number = CFDictionaryGetValue((CFDictionaryRef) item, kCGWindowNumber);
        int64_t window_id = 0;
        if (number && CFGetTypeID(number) == CFNumberGetTypeID() &&
            CFNumberGetValue((CFNumberRef) number, kCFNumberSInt64Type, &window_id) &&
            window_id > 0 && window_id <= UINT32_MAX) {
            CFArrayAppendValue(ids, number);
        }
    }
    CFRelease(info);
    return ids;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        int limit = 200;
        int sample_limit = 24;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
                if (!window_corner_geometry_probe_parse_count(argv[++i], 10000, &limit) || limit < 1) return EXIT_FAILURE;
            } else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc) {
                if (!window_corner_geometry_probe_parse_count(argv[++i], 100, &sample_limit)) return EXIT_FAILURE;
            } else {
                return EXIT_FAILURE;
            }
        }

        window_corner_geometry_probe_getter resolved = (window_corner_geometry_probe_getter)
            dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetResolvedCornerRadii");
        bool raw_available = dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetCornerRadii") != NULL;
        bool mask_flags_available = dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetCornerMaskFlags") != NULL;
        bool copy_mask_available = dlsym(RTLD_DEFAULT, "SLSCopyWindowCornerMask") != NULL;
        struct window_corner_geometry_capability capability = window_corner_geometry_capability();

        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char build[256] = {0};
        size_t build_size = sizeof(build);
        struct utsname system;
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 ||
            !memchr(build, '\0', sizeof(build)) || uname(&system) != 0) return EXIT_FAILURE;

        printf("{\"schema\":\"yabai.window-corner-geometry-probe/v1\","
               "\"os_version\":\"%ld.%ld.%ld\",\"os_build\":\"%s\","
               "\"architecture\":\"%s\",\"symbols\":{"
               "\"resolved_corner_radii\":%s,\"corner_radii\":%s,"
               "\"corner_mask_flags\":%s,\"copy_corner_mask\":%s},"
               "\"collection_enabled\":%s,\"proxy_rendering_enabled\":%s,\"samples\":[",
               version.majorVersion, version.minorVersion, version.patchVersion, build, system.machine,
               resolved ? "true" : "false", raw_available ? "true" : "false",
               mask_flags_available ? "true" : "false", copy_mask_available ? "true" : "false",
               capability.collection_enabled ? "true" : "false",
               capability.proxy_rendering_enabled ? "true" : "false");

        if (!resolved) {
            puts("],\"summary\":{\"getter_calls\":0,\"valid_observations\":0}}");
            return 2;
        }

        CFMutableArrayRef ids = window_corner_geometry_probe_window_ids();
        if (!ids || CFArrayGetCount(ids) > INT_MAX) { if (ids) CFRelease(ids); return EXIT_FAILURE; }
        int requested = (int) CFArrayGetCount(ids);
        CFTypeRef query = SLSWindowQueryWindows(SLSMainConnectionID(), ids, requested);
        CFRelease(ids);
        if (!query) return EXIT_FAILURE;
        CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
        if (!iterator) { CFRelease(query); return EXIT_FAILURE; }

        int declared = SLSWindowIteratorGetCount(iterator);
        int getter_calls = 0, nonnull = 0, arrays = 0, exact_four = 0;
        int numeric_four = 0, valid_observations = 0, asymmetric = 0, emitted = 0;
        int counts[9] = {0};
        uint64_t *latency = calloc((size_t) limit, sizeof(*latency));
        if (!latency) { CFRelease(iterator); CFRelease(query); return EXIT_FAILURE; }

        while (getter_calls < limit && SLSWindowIteratorAdvance(iterator)) {
            uint32_t window_id = SLSWindowIteratorGetWindowID(iterator);
            uint64_t start = window_corner_geometry_probe_now_ns();
            CFArrayRef value = resolved(iterator, window_id);
            latency[getter_calls] = window_corner_geometry_probe_now_ns() - start;
            ++getter_calls;
            if (value) ++nonnull;
            bool is_array = value && CFGetTypeID(value) == CFArrayGetTypeID();
            CFIndex count = is_array ? CFArrayGetCount(value) : -1;
            if (is_array) {
                ++arrays;
                if (count >= 0 && count < 8) ++counts[count]; else ++counts[8];
                if (count == WINDOW_CORNER_GEOMETRY_RADIUS_COUNT) ++exact_four;
            }
            struct window_corner_geometry geometry = {0};
            bool valid = window_corner_geometry_normalize(value,
                WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED, &geometry);
            if (count == WINDOW_CORNER_GEOMETRY_RADIUS_COUNT) {
                bool all_numbers = true;
                for (CFIndex slot = 0; slot < count; ++slot) {
                    CFTypeRef item = CFArrayGetValueAtIndex(value, slot);
                    all_numbers &= item && CFGetTypeID(item) == CFNumberGetTypeID();
                }
                if (all_numbers) ++numeric_four;
            }
            bool all_equal = valid;
            if (valid) {
                ++valid_observations;
                for (size_t slot = 1; slot < WINDOW_CORNER_GEOMETRY_RADIUS_COUNT; ++slot) {
                    all_equal &= geometry.radius[slot] == geometry.radius[0];
                }
                if (!all_equal) ++asymmetric;
            }

            if (emitted < sample_limit) {
                if (emitted) putchar(',');
                printf("{\"index\":%d,\"result_type\":\"%s\",\"count\":%ld,\"slots\":[",
                       emitted, window_corner_geometry_probe_type(value), (long) count);
                if (is_array) {
                    for (CFIndex slot = 0; slot < count; ++slot) {
                        if (slot) putchar(',');
                        CFTypeRef item = CFArrayGetValueAtIndex(value, slot);
                        double number = 0.0;
                        bool numeric = item && CFGetTypeID(item) == CFNumberGetTypeID() &&
                                       CFNumberGetValue((CFNumberRef) item, kCFNumberDoubleType, &number);
                        printf("{\"type\":\"%s\",\"numeric\":%s",
                               window_corner_geometry_probe_type(item), numeric ? "true" : "false");
                        if (numeric && isfinite(number)) printf(",\"value\":%.6f", number);
                        putchar('}');
                    }
                }
                CGRect frame = CGRectZero;
                bool frame_valid = SLSGetWindowBounds(SLSMainConnectionID(), window_id, &frame) == kCGErrorSuccess;
                printf("],\"normalized\":%s,\"all_equal\":%s,\"asymmetric\":%s,"
                       "\"frame_valid\":%s",
                       valid ? "true" : "false", valid && all_equal ? "true" : "false",
                       valid && !all_equal ? "true" : "false", frame_valid ? "true" : "false");
                if (frame_valid) printf(",\"frame_width\":%.3f,\"frame_height\":%.3f",
                                        frame.size.width, frame.size.height);
                printf(",\"getter_latency_ns\":%llu}",
                       (unsigned long long) latency[getter_calls - 1]);
                ++emitted;
            }
            /* Getter ownership is unresolved. Consume the bounded result only
             * while its iterator is alive and deliberately do not release it. */
        }

        qsort(latency, (size_t) getter_calls, sizeof(*latency), window_corner_geometry_probe_compare_u64);
        int p95 = getter_calls ? (getter_calls * 95 + 99) / 100 - 1 : 0;
        printf("],\"summary\":{\"requested_windows\":%d,\"iterator_windows\":%d,"
               "\"getter_calls\":%d,\"nonnull_results\":%d,\"array_results\":%d,"
               "\"exact_four\":%d,\"numeric_four\":%d,\"valid_observations\":%d,"
               "\"asymmetric_observations\":%d,\"count_histogram\":{"
               "\"0\":%d,\"1\":%d,\"2\":%d,\"3\":%d,\"4\":%d,"
               "\"5\":%d,\"6\":%d,\"7\":%d,\"other\":%d},"
               "\"getter_p50_ns\":%llu,\"getter_p95_ns\":%llu}}\n",
               requested, declared, getter_calls, nonnull, arrays, exact_four, numeric_four,
               valid_observations, asymmetric, counts[0], counts[1], counts[2], counts[3],
               counts[4], counts[5], counts[6], counts[7], counts[8],
               (unsigned long long) (getter_calls ? latency[getter_calls / 2] : 0),
               (unsigned long long) (getter_calls ? latency[p95] : 0));

        free(latency);
        CFRelease(iterator);
        CFRelease(query);
        return EXIT_SUCCESS;
    }
}
