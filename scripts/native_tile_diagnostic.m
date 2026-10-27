#import <Foundation/Foundation.h>

#include <sys/sysctl.h>
#include <sys/utsname.h>

#include "native_tile_diagnostic_core.h"

static void native_tile_usage(const char *program)
{
    printf("usage: %s [--help]\n", program);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc == 2 && strcmp(argv[1], "--help") == 0) {
            native_tile_usage(argv[0]);
            return 0;
        }
        if (argc != 1) {
            native_tile_usage(argv[0]);
            return 64;
        }

        struct wm_tile_membership_diagnostic diagnostic;
        if (!window_management_bridge_diagnose_tile_membership_readonly(&diagnostic)) return 3;
        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char build[256] = {0}; size_t build_size = sizeof(build); struct utsname system = {0};
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 ||
            !memchr(build, '\0', sizeof(build)) || uname(&system) != 0) return 3;
        struct native_tile_platform platform = {
            .os_major = (int) version.majorVersion,
            .os_minor = (int) version.minorVersion,
            .os_patch = (int) version.patchVersion,
            .build = build,
            .architecture = system.machine
        };
        if (!native_tile_diagnostic_write_json(stdout, &platform, &diagnostic)) return 3;
        switch (diagnostic.status) {
        case WM_TILE_MEMBERSHIP_OK: return 0;
        case WM_TILE_MEMBERSHIP_UNAVAILABLE:
        case WM_TILE_MEMBERSHIP_ABI_MISMATCH: return 2;
        case WM_TILE_MEMBERSHIP_PARTIAL:
        case WM_TILE_MEMBERSHIP_QUERY_FAILED: return 3;
        }
        return 3;
    }
}
