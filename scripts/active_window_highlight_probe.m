#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "../src/misc/extern.h"
#include "../src/window_server_tags.h"


static CGFloat probe_width = 2.0;

struct inspection {
    bool target_present;
    bool target_on_screen;
    int overlay_matches;
    bool overlay_above;
    bool overlay_adjacent;
    bool overlay_on_screen;
    bool center_hit_target;
    bool inner_edge_hit_target;
    bool overlay_floating_tag;
    bool overlay_ignore_events_tag;
    bool overlay_owner_is_daemon;
    int daemon_window_count;
    double nearest_dx;
    double nearest_dy;
    double nearest_dw;
    double nearest_dh;
};

static bool number_u32(CFDictionaryRef dictionary, CFStringRef key, uint32_t *value)
{
    CFNumberRef number = dictionary ? CFDictionaryGetValue(dictionary, key) : NULL;
    return number && CFGetTypeID(number) == CFNumberGetTypeID() &&
           CFNumberGetValue(number, kCFNumberSInt32Type, value);
}

static bool number_i32(CFDictionaryRef dictionary, CFStringRef key, int *value)
{
    CFNumberRef number = dictionary ? CFDictionaryGetValue(dictionary, key) : NULL;
    return number && CFGetTypeID(number) == CFNumberGetTypeID() &&
           CFNumberGetValue(number, kCFNumberIntType, value);
}

static bool bounds_value(CFDictionaryRef dictionary, CGRect *bounds)
{
    CFDictionaryRef value = dictionary ? CFDictionaryGetValue(dictionary, kCGWindowBounds) : NULL;
    return value && CGRectMakeWithDictionaryRepresentation(value, bounds);
}

static bool rect_near(CGRect a, CGRect b)
{
    return fabs(a.origin.x - b.origin.x) <= 1.0 &&
           fabs(a.origin.y - b.origin.y) <= 1.0 &&
           fabs(a.size.width - b.size.width) <= 1.0 &&
           fabs(a.size.height - b.size.height) <= 1.0;
}

static bool hit_is_target(int connection, CGPoint point, uint32_t target)
{
    CGPoint local = CGPointZero;
    uint32_t window = 0;
    int owner_connection = 0;
    return SLSFindWindowAndOwner(connection, 0, 1, 0, &point, &local,
                                 &window, &owner_connection) == noErr &&
           window == target;
}

static bool copy_tags(int connection, uint32_t window, uint64_t *tags)
{
    int32_t value = (int32_t)window;
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
    if (!number) return false;
    const void *values[] = { number };
    CFArrayRef ids = CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
    CFRelease(number);
    if (!ids) return false;
    CFTypeRef query = SLSWindowQueryWindows(connection, ids, 1);
    CFRelease(ids);
    if (!query) return false;
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    bool result = false;
    if (iterator && SLSWindowIteratorGetCount(iterator) == 1 &&
        SLSWindowIteratorAdvance(iterator)) {
        *tags = SLSWindowIteratorGetTags(iterator);
        result = true;
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
    return result;
}

static struct inspection inspect_window(pid_t daemon_pid, uint32_t target, CGFloat width,
                                        const CGRect *fallback_bounds)
{
    struct inspection result = {0};
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (!windows) return result;

    CGRect target_bounds = fallback_bounds ? *fallback_bounds : CGRectZero;
    CFIndex target_index = -1;
    for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
        CFDictionaryRef dictionary = CFArrayGetValueAtIndex(windows, i);
        uint32_t number = 0;
        int owner = 0;
        if (!number_u32(dictionary, kCGWindowNumber, &number) ||
            !number_i32(dictionary, kCGWindowOwnerPID, &owner)) continue;
        if (owner == daemon_pid) ++result.daemon_window_count;
        if (number == target && bounds_value(dictionary, &target_bounds)) {
            result.target_present = true;
            target_index = i;
            CFBooleanRef on_screen = CFDictionaryGetValue(dictionary, kCGWindowIsOnscreen);
            result.target_on_screen = on_screen == kCFBooleanTrue;
        }
    }

    CGRect expected = CGRectInset(target_bounds, -width, -width);
    uint32_t overlay = 0;
    CFIndex overlay_index = -1;
    double nearest_score = HUGE_VAL;
    for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
        CFDictionaryRef dictionary = CFArrayGetValueAtIndex(windows, i);
        uint32_t number = 0;
        int owner = 0;
        CGRect bounds = CGRectZero;
        if (!number_u32(dictionary, kCGWindowNumber, &number) ||
            !number_i32(dictionary, kCGWindowOwnerPID, &owner) ||
            number == target || !bounds_value(dictionary, &bounds)) continue;
        if (owner != daemon_pid) {
            if (!rect_near(bounds, expected)) continue;
            ++result.overlay_matches;
            overlay = number;
            overlay_index = i;
            continue;
        }
        double dx = bounds.origin.x - expected.origin.x;
        double dy = bounds.origin.y - expected.origin.y;
        double dw = bounds.size.width - expected.size.width;
        double dh = bounds.size.height - expected.size.height;
        double score = fabs(dx) + fabs(dy) + fabs(dw) + fabs(dh);
        if (score < nearest_score) {
            nearest_score = score;
            result.nearest_dx = dx;
            result.nearest_dy = dy;
            result.nearest_dw = dw;
            result.nearest_dh = dh;
        }
        if (!rect_near(bounds, expected)) continue;
        ++result.overlay_matches;
        overlay = number;
        overlay_index = i;
        result.overlay_owner_is_daemon = true;
    }

    if (result.overlay_matches == 1) {
        result.overlay_above = target_index >= 0 && overlay_index >= 0 && overlay_index < target_index;
        result.overlay_adjacent = target_index >= 0 && overlay_index + 1 == target_index;
        if (overlay_index >= 0) {
            CFDictionaryRef dictionary = CFArrayGetValueAtIndex(windows, overlay_index);
            CFBooleanRef on_screen = CFDictionaryGetValue(dictionary, kCGWindowIsOnscreen);
            result.overlay_on_screen = on_screen == kCFBooleanTrue;
        }
        int connection = SLSMainConnectionID();
        uint64_t tags = 0;
        if (copy_tags(connection, overlay, &tags)) {
            result.overlay_floating_tag = (tags & WINDOW_SERVER_TAG_FLOATING) != 0;
            result.overlay_ignore_events_tag = (tags & WINDOW_SERVER_TAG_IGNORE_FOR_EVENTS) != 0;
        }
        CGPoint center = CGPointMake(CGRectGetMidX(target_bounds), CGRectGetMidY(target_bounds));
        CGPoint inner_edge = CGPointMake(CGRectGetMinX(target_bounds) + 1.0,
                                         CGRectGetMidY(target_bounds));
        result.center_hit_target = hit_is_target(connection, center, target);
        result.inner_edge_hit_target = hit_is_target(connection, inner_edge, target);
    }

    CFRelease(windows);
    return result;
}

static void emit(const char *phase, const char *expectation, struct inspection value)
{
    printf("{\"schema\":\"yabai.active-window-highlight-probe/v1\","
           "\"type\":\"observation\",\"phase\":\"%s\",\"expectation\":\"%s\","
           "\"target_present\":%s,\"target_on_screen\":%s,"
           "\"overlay_matches\":%d,\"overlay_above\":%s,"
           "\"overlay_adjacent\":%s,\"overlay_on_screen\":%s,"
           "\"center_hit_target\":%s,"
           "\"inner_edge_hit_target\":%s,\"floating_tag\":%s,"
           "\"ignore_events_tag\":%s,\"overlay_owner_is_daemon\":%s,"
           "\"daemon_window_count\":%d,"
           "\"nearest_delta\":[%.3f,%.3f,%.3f,%.3f],"
           "\"app_active\":%s,\"app_hidden\":%s,\"key_window_present\":%s}\n",
           phase, expectation,
           value.target_present ? "true" : "false",
           value.target_on_screen ? "true" : "false",
           value.overlay_matches,
           value.overlay_above ? "true" : "false",
           value.overlay_adjacent ? "true" : "false",
           value.overlay_on_screen ? "true" : "false",
           value.center_hit_target ? "true" : "false",
           value.inner_edge_hit_target ? "true" : "false",
           value.overlay_floating_tag ? "true" : "false",
           value.overlay_ignore_events_tag ? "true" : "false",
           value.overlay_owner_is_daemon ? "true" : "false",
           value.daemon_window_count,
           value.nearest_dx, value.nearest_dy, value.nearest_dw, value.nearest_dh,
           [NSApp isActive] ? "true" : "false",
           [NSApp isHidden] ? "true" : "false",
           [NSApp keyWindow] ? "true" : "false");
    fflush(stdout);
}

static bool emit_run(const char *mode)
{
    NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
    char build[256] = {0};
    size_t build_size = sizeof(build);
    struct utsname system = {0};
    if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 ||
        !memchr(build, '\0', sizeof(build)) || uname(&system) != 0) return false;
    printf("{\"schema\":\"yabai.active-window-highlight-probe/v1\","
           "\"type\":\"run\",\"mode\":\"%s\","
           "\"os_version\":\"%ld.%ld.%ld\",\"os_build\":\"%s\","
           "\"architecture\":\"%s\",\"configured_width\":%.3f,"
           "\"production_configuration_changed\":false}\n",
           mode, version.majorVersion, version.minorVersion, version.patchVersion,
           build, system.machine, probe_width);
    fflush(stdout);
    return true;
}

static void spin(NSTimeInterval seconds)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
}

static bool wait_for_fullscreen(NSWindow *window, bool fullscreen, NSTimeInterval timeout)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    while ([deadline timeIntervalSinceNow] > 0) {
        bool current = ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
        if (current == fullscreen) return true;
        spin(0.05);
    }
    return false;
}

static NSWindow *make_window(NSRect frame, NSWindowStyleMask style, NSWindowLevel level)
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setReleasedWhenClosed:NO];
    [window setLevel:level];
    [window setTitle:@"Controlled Fixture"];
    [window setContentView:[[NSView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)]];
    return window;
}

static CGRect current_bounds(uint32_t window_number)
{
    CGRect result = CGRectZero;
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (!windows) return result;
    for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
        CFDictionaryRef dictionary = CFArrayGetValueAtIndex(windows, i);
        uint32_t number = 0;
        if (number_u32(dictionary, kCGWindowNumber, &number) && number == window_number &&
            bounds_value(dictionary, &result)) break;
    }
    CFRelease(windows);
    return result;
}

static void inspect_phase(const char *phase, const char *expectation, pid_t daemon_pid,
                          NSWindow *window, const CGRect *fallback)
{
    spin(0.45);
    emit(phase, expectation,
         inspect_window(daemon_pid, (uint32_t)[window windowNumber], probe_width, fallback));
}

static bool run_application_action(const char *action)
{
    NSURL *executable = NSBundle.mainBundle.executableURL;
    if (!executable) return false;
    NSTask *task = [NSTask new];
    [task setExecutableURL:executable];
    [task setArguments:@[@"--application-action",
                         [NSString stringWithUTF8String:action],
                         [NSString stringWithFormat:@"%d", getpid()]]];
    NSError *error = nil;
    if (![task launchAndReturnError:&error]) return false;
    [task waitUntilExit];
    return [task terminationStatus] == 0;
}

static int run_fixture(pid_t daemon_pid)
{
    NSWindowStyleMask ordinary_style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    NSWindow *primary = make_window(NSMakeRect(180, 180, 620, 420), ordinary_style, NSNormalWindowLevel);
    [primary makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    spin(1.5);
    inspect_phase("ordinary", "present", daemon_pid, primary, NULL);

    [primary setFrame:NSMakeRect(240, 220, 760, 500) display:YES animate:NO];
    inspect_phase("resize-move", "present", daemon_pid, primary, NULL);

    NSWindow *floating = make_window(NSMakeRect(330, 300, 480, 300), ordinary_style, NSFloatingWindowLevel);
    [floating makeKeyAndOrderFront:nil];
    inspect_phase("floating", "present", daemon_pid, floating, NULL);
    [floating close];
    [primary makeKeyAndOrderFront:nil];

    NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(360, 340, 420, 240)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                          NSWindowStyleMaskUtilityWindow
                                                  backing:NSBackingStoreBuffered defer:NO];
    [panel setReleasedWhenClosed:NO];
    [panel setFloatingPanel:YES];
    [panel setBecomesKeyOnlyIfNeeded:NO];
    [panel makeKeyAndOrderFront:nil];
    inspect_phase("panel", "present", daemon_pid, panel, NULL);
    [panel close];
    [primary makeKeyAndOrderFront:nil];

    NSWindow *secondary = make_window(NSMakeRect(300, 260, 680, 440), ordinary_style, NSNormalWindowLevel);
    [secondary makeKeyAndOrderFront:nil];
    for (int i = 0; i < 30; ++i) {
        [(i & 1 ? primary : secondary) makeKeyAndOrderFront:nil];
        spin(0.02);
    }
    [secondary makeKeyAndOrderFront:nil];
    inspect_phase("rapid-focus", "present", daemon_pid, secondary, NULL);

    CGRect minimized_bounds = current_bounds((uint32_t)[secondary windowNumber]);
    [secondary miniaturize:nil];
    inspect_phase("minimized", "absent", daemon_pid, secondary, &minimized_bounds);
    [secondary deminiaturize:nil];
    [secondary makeKeyAndOrderFront:nil];
    inspect_phase("restored", "present", daemon_pid, secondary, NULL);

    CGRect ordered_out_bounds = current_bounds((uint32_t)[secondary windowNumber]);
    [secondary orderOut:nil];
    spin(1.0);
    emit("ordered-out", "absent",
         inspect_window(daemon_pid, (uint32_t)[secondary windowNumber],
                        probe_width, &ordered_out_bounds));
    [secondary makeKeyAndOrderFront:nil];
    inspect_phase("ordered-in", "present", daemon_pid, secondary, NULL);

    NSWindow *fullscreen_window = make_window(NSMakeRect(260, 230, 700, 460), ordinary_style,
                                              NSNormalWindowLevel);
    [fullscreen_window makeKeyAndOrderFront:nil];
    inspect_phase("fullscreen-baseline", "present", daemon_pid, fullscreen_window, NULL);
    [fullscreen_window toggleFullScreen:nil];
    (void) wait_for_fullscreen(fullscreen_window, true, 4.0);
    spin(0.5);
    CGRect fullscreen_bounds = current_bounds((uint32_t)[fullscreen_window windowNumber]);
    emit("fullscreen", "absent",
         inspect_window(daemon_pid, (uint32_t)[fullscreen_window windowNumber],
                        probe_width, &fullscreen_bounds));
    [fullscreen_window toggleFullScreen:nil];
    (void) wait_for_fullscreen(fullscreen_window, false, 8.0);
    (void) run_application_action("activate");
    [fullscreen_window makeKeyAndOrderFront:nil];
    spin(2.0);
    inspect_phase("fullscreen-exit", "present", daemon_pid, fullscreen_window, NULL);
    [fullscreen_window close];
    [secondary makeKeyAndOrderFront:nil];
    spin(1.0);

    [primary setTabbingIdentifier:@"controlled-tabs"];
    [secondary setTabbingIdentifier:@"controlled-tabs"];
    [primary addTabbedWindow:secondary ordered:NSWindowAbove];
    [secondary makeKeyAndOrderFront:nil];
    inspect_phase("native-tab-secondary", "present", daemon_pid, secondary, NULL);
    [primary makeKeyAndOrderFront:nil];
    inspect_phase("native-tab-primary", "present", daemon_pid, primary, NULL);
    [secondary close];
    [primary close];
    spin(0.5);

    NSWindow *closing = make_window(NSMakeRect(280, 240, 640, 400), ordinary_style,
                                    NSNormalWindowLevel);
    [closing makeKeyAndOrderFront:nil];
    inspect_phase("close-baseline", "present", daemon_pid, closing, NULL);
    CGRect closed_bounds = current_bounds((uint32_t)[closing windowNumber]);
    [closing close];
    spin(1.5);
    emit("closed", "absent",
         inspect_window(daemon_pid, (uint32_t)[closing windowNumber], probe_width, &closed_bounds));
    return 0;
}

static pid_t fixture_daemon_pid;
static int fixture_result;

@interface FixtureDelegate : NSObject <NSApplicationDelegate>
@end

@implementation FixtureDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void) notification;
    dispatch_async(dispatch_get_main_queue(), ^{
        fixture_result = run_fixture(fixture_daemon_pid);
        [NSApp terminate:nil];
    });
}
@end

static bool parse_positive_double(const char *text, double *value)
{
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (!text[0] || !end || *end || !isfinite(parsed) || parsed <= 0.0) return false;
    *value = parsed;
    return true;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --fixture DAEMON_PID OUTPUT [WIDTH]\n"
            "       %s --check-window DAEMON_PID WINDOW_ID [WIDTH]\n",
            program, program);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc == 4 && strcmp(argv[1], "--application-action") == 0) {
            pid_t pid = (pid_t)atoi(argv[3]);
            NSRunningApplication *application =
                [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
            if (!application) return 1;
            if (strcmp(argv[2], "hide") == 0) return [application hide] ? 0 : 1;
            if (strcmp(argv[2], "activate") == 0) {
                return [application activateWithOptions:NSApplicationActivateAllWindows] ? 0 : 1;
            }
            return 64;
        }
        if (argc == 2 && strcmp(argv[1], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if ((argc == 4 || argc == 5) && strcmp(argv[1], "--fixture") == 0) {
            if (argc == 5) {
                double width = 0.0;
                if (!parse_positive_double(argv[4], &width)) return 64;
                probe_width = width;
            }
            fixture_daemon_pid = (pid_t)atoi(argv[2]);
            if (fixture_daemon_pid <= 0 || !freopen(argv[3], "w", stdout) ||
                !emit_run("controlled-fixture")) return 74;
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            FixtureDelegate *delegate = [FixtureDelegate new];
            [NSApp setDelegate:delegate];
            [NSApp run];
            return fixture_result;
        }
        if ((argc == 4 || argc == 5) && strcmp(argv[1], "--check-window") == 0) {
            if (argc == 5) {
                double width = 0.0;
                if (!parse_positive_double(argv[4], &width)) return 64;
                probe_width = width;
            }
            pid_t daemon_pid = (pid_t)atoi(argv[2]);
            uint32_t target = (uint32_t)strtoul(argv[3], NULL, 10);
            if (daemon_pid <= 0 || !target || !emit_run("existing-window")) return 64;
            emit("external-window", "present",
                 inspect_window(daemon_pid, target, probe_width, NULL));
            return 0;
        }
        usage(argv[0]);
        return 64;
    }
}
