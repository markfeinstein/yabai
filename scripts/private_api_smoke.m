// Read-only live smoke test: no windows, prompts, services, or SA mutations.
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <sys/sysctl.h>

#include "../src/misc/extern.h"
#include "../src/managed_space.h"
#include "../src/window_corner_geometry.c"
#include "../src/window_server_snapshot.c"

static bool smoke_check(NSMutableDictionary *report, NSString *name, bool passed)
{
    report[@"checks"][name] = @(passed);
    return passed;
}

static void smoke_collect(NSMutableDictionary *report)
{
    NSDictionary *session = [(id) CGSessionCopyCurrentDictionary() autorelease];
    if (!smoke_check(report, @"console_session", [session[(id) kCGSessionOnConsoleKey] boolValue])) return;
    int connection = SLSMainConnectionID();
    if (!smoke_check(report, @"connection", connection > 0)) return;

    NSArray *displays = [(id) SLSCopyManagedDisplays(connection) autorelease];
    NSArray *topology = [(id) SLSCopyManagedDisplaySpaces(connection) autorelease];
    if (!smoke_check(report, @"managed_displays", [displays isKindOfClass:[NSArray class]] &&
                     displays.count > 0 && displays.count <= 64)) return;
    report[@"display_count"] = @(displays.count);
    if (!smoke_check(report, @"managed_spaces", [topology isKindOfClass:[NSArray class]] &&
                     topology.count > 0 && topology.count <= 64)) return;

    NSMutableArray *spaces = [NSMutableArray array];
    for (id display in displays) {
        if (!smoke_check(report, @"current_spaces", [display isKindOfClass:[NSString class]])) return;
        uint64_t sid = SLSManagedDisplayGetCurrentSpace(connection, (CFStringRef) display);
        if (!smoke_check(report, @"current_spaces", sid &&
                         managed_space_display_identifier_for_space((CFArrayRef) topology, sid))) return;
        int type = SLSSpaceGetType(connection, sid);
        if (!smoke_check(report, @"space_types", type == 0 || type == 2 || type == 4)) return;
        NSNumber *number = @(sid);
        if (![spaces containsObject:number]) [spaces addObject:number];
    }
    report[@"current_space_count"] = @(spaces.count);

    uint64_t set_tags = 0, clear_tags = 0;
    NSArray *windows = [(id) SLSCopyWindowsWithOptionsAndTags(connection, 0, (CFArrayRef) spaces,
                                                            0x2, &set_tags, &clear_tags) autorelease];
    if (!smoke_check(report, @"window_inventory", [windows isKindOfClass:[NSArray class]] &&
                     windows.count > 0 && windows.count <= 100000)) return;
    report[@"window_count"] = @(windows.count);
    // A quiet host with at least one existing visible app window is required.
    // Bound work and retain only counts, never IDs, owners, titles, or geometry.
    NSArray *sample = [windows subarrayWithRange:NSMakeRange(0, MIN(windows.count, 32u))];
    for (id number in sample) {
        if (!smoke_check(report, @"window_inventory", [number isKindOfClass:[NSNumber class]] &&
                         [number unsignedLongLongValue] > 0 &&
                         [number unsignedLongLongValue] <= UINT32_MAX)) return;
    }
    struct window_server_snapshot *snapshot =
        window_server_snapshot_create(connection, (CFArrayRef) sample, (int) sample.count);
    if (!smoke_check(report, @"production_snapshot", snapshot && snapshot->count == sample.count)) {
        window_server_snapshot_destroy(snapshot);
        return;
    }
    bool records_valid = true, bounds_valid = true, ordered_valid = true;
    for (size_t i = 0; i < snapshot->count; ++i) {
        const struct window_server_window_record *record = snapshot->records + i;
        records_valid &= (record->valid_fields & WINDOW_SERVER_WINDOW_FIELD_BASE) == WINDOW_SERVER_WINDOW_FIELD_BASE &&
                         window_server_snapshot_find(snapshot, [sample[i] unsignedIntValue]) != NULL;
        CGRect bounds = CGRectZero;
        bounds_valid &= SLSGetWindowBounds(connection, record->id, &bounds) == kCGErrorSuccess &&
                        isfinite(bounds.origin.x) && isfinite(bounds.origin.y) &&
                        isfinite(bounds.size.width) && isfinite(bounds.size.height) &&
                        bounds.size.width >= 0 && bounds.size.height >= 0;
        uint8_t ordered = 0;
        ordered_valid &= SLSWindowIsOrderedIn(connection, record->id, &ordered) == kCGErrorSuccess && ordered <= 1;
    }
    report[@"sample_count"] = @(snapshot->count);
    window_server_snapshot_destroy(snapshot);
    smoke_check(report, @"snapshot_records", records_valid);
    smoke_check(report, @"window_bounds", bounds_valid);
    smoke_check(report, @"window_order", ordered_valid);
}

int main(void)
{
    @autoreleasepool {
        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char build[128] = {0};
        size_t build_size = sizeof(build);
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0) return 1;
        int translated = 0;
        size_t translated_size = sizeof(translated);
        if (sysctlbyname("sysctl.proc_translated", &translated, &translated_size, NULL, 0) != 0 && errno != ENOENT) return 1;
#if defined(__arm64__)
        NSString *architecture = @"arm64";
#elif defined(__x86_64__)
        NSString *architecture = @"x86_64";
#else
#error Unsupported smoke-test architecture
#endif
        NSMutableDictionary *report = [NSMutableDictionary dictionaryWithDictionary:@{
            @"schema": @"yabai.private-api-smoke/v1",
            @"os_version": [NSString stringWithFormat:@"%ld.%ld.%ld", (long) version.majorVersion,
                            (long) version.minorVersion, (long) version.patchVersion],
            @"os_build": [NSString stringWithUTF8String:build],
            @"architecture": architecture,
            @"translated": translated ? @YES : @NO,
            @"checks": [NSMutableDictionary dictionary]
        }];
        smoke_collect(report);
        NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:NULL];
        if (!json) return 1;
        return fwrite(json.bytes, 1, json.length, stdout) == json.length && putchar('\n') != EOF ? 0 : 1;
    }
}
