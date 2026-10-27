#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <sys/sysctl.h>
#include <sys/utsname.h>

#include "../src/misc/extern.h"
#include "../src/window_server_snapshot.c"
#include "window_server_tag_corpus_core.c"

static bool corpus_number(NSDictionary *dictionary, CFStringRef key, int64_t *value)
{
    id object = dictionary[(NSString *) key];
    if (![object isKindOfClass:[NSNumber class]]) return false;
    *value = [object longLongValue];
    return true;
}

static void corpus_usage(const char *program)
{
    fprintf(stderr, "usage: %s --scenario NAME --output PATH\n", program);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *scenario = NULL;
        const char *output = NULL;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
                scenario = argv[++i];
            } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                output = argv[++i];
            } else {
                corpus_usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        if (!window_server_tag_corpus_scenario_is_valid(scenario) || !output) {
            corpus_usage(argv[0]);
            return EXIT_FAILURE;
        }

        CFArrayRef info_ref = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
        if (!info_ref || CFGetTypeID(info_ref) != CFArrayGetTypeID() ||
            CFArrayGetCount(info_ref) < 0 || (uintmax_t) CFArrayGetCount(info_ref) > INT_MAX) {
            if (info_ref) CFRelease(info_ref);
            return EXIT_FAILURE;
        }
        size_t count = (size_t) CFArrayGetCount(info_ref);
        struct window_server_tag_corpus_public_window *windows =
            calloc(count ? count : 1, sizeof(*windows));
        CFMutableArrayRef ids = CFArrayCreateMutable(NULL, (CFIndex) count, &kCFTypeArrayCallBacks);
        bool valid = windows && ids;
        for (size_t i = 0; valid && i < count; ++i) {
            id value = (id) CFArrayGetValueAtIndex(info_ref, (CFIndex) i);
            if (![value isKindOfClass:[NSDictionary class]]) { valid = false; break; }
            NSDictionary *dictionary = value;
            int64_t window_id = 0, owner_pid = 0, layer = 0;
            if (!corpus_number(dictionary, kCGWindowNumber, &window_id) || window_id <= 0 ||
                window_id > UINT32_MAX ||
                !corpus_number(dictionary, kCGWindowOwnerPID, &owner_pid) ||
                owner_pid < INT_MIN || owner_pid > INT_MAX ||
                !corpus_number(dictionary, kCGWindowLayer, &layer) ||
                layer < INT_MIN || layer > INT_MAX) {
                valid = false;
                break;
            }
            id on_screen = dictionary[(NSString *) kCGWindowIsOnscreen];
            if (on_screen && ![on_screen isKindOfClass:[NSNumber class]]) { valid = false; break; }
            windows[i] = (struct window_server_tag_corpus_public_window) {
                .window_id = (uint32_t) window_id,
                .owner_pid = (int) owner_pid,
                .cg_layer = (int) layer,
                .on_screen = on_screen ? [on_screen boolValue] != NO : false
            };
            int32_t id_value = (int32_t) window_id;
            CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &id_value);
            if (!number) { valid = false; break; }
            CFArrayAppendValue(ids, number);
            CFRelease(number);
        }
        CFRelease(info_ref);
        if (!valid) {
            if (ids) CFRelease(ids);
            free(windows);
            return EXIT_FAILURE;
        }

        struct window_server_snapshot *snapshot =
            window_server_snapshot_create(SLSMainConnectionID(), ids, (int) count);
        CFRelease(ids);
        if (!snapshot) {
            free(windows);
            return EXIT_FAILURE;
        }

        NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
        char os_version[64];
        snprintf(os_version, sizeof(os_version), "%ld.%ld.%ld",
                 version.majorVersion, version.minorVersion, version.patchVersion);
        char os_build[256] = {0};
        size_t os_build_size = sizeof(os_build);
        if (sysctlbyname("kern.osversion", os_build, &os_build_size, NULL, 0) != 0 ||
            !memchr(os_build, '\0', sizeof(os_build))) {
            window_server_snapshot_destroy(snapshot);
            free(windows);
            return EXIT_FAILURE;
        }
        struct utsname system_info;
        if (uname(&system_info) != 0) {
            window_server_snapshot_destroy(snapshot);
            free(windows);
            return EXIT_FAILURE;
        }
        struct window_server_tag_corpus_metadata metadata = {
            .scenario = scenario,
            .os_version = os_version,
            .os_build = os_build,
            .architecture = system_info.machine
        };
        bool success = window_server_tag_corpus_write_file(
            output, &metadata, windows, count, snapshot);
        window_server_snapshot_destroy(snapshot);
        free(windows);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
