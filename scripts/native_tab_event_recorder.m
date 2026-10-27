#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <time.h>

#include "../src/misc/extern.h"
#include "native_tab_event_recorder_core.h"

struct native_tab_tool
{
    pid_t target_pid;
    int connection;
    const char *output_path;
    uint64_t start_ns;
    uint64_t stop_ns;
    uint64_t next_poll_ns;
    struct native_tab_recorder recorder;
    AXUIElementRef application;
    AXObserverRef observer;
    CFMutableSetRef observed_ax_windows;
    id workspace_tokens[4];
    CFRunLoopTimerRef timer;
    bool ax_available;
    bool success;
    bool stopping;
};

static uint64_t native_tab_now_ns(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static bool native_tab_number(NSDictionary *dictionary, CFStringRef key, int64_t *value)
{
    id number = dictionary[(NSString *) key];
    if (![number isKindOfClass:[NSNumber class]]) return false;
    *value = [number longLongValue];
    return true;
}

static struct native_tab_window_fact *native_tab_inventory_find(
    struct native_tab_inventory *inventory, uint32_t window_id)
{
    for (size_t i = 0; i < inventory->count; ++i) {
        if (inventory->windows[i].window_id == window_id) return inventory->windows + i;
    }
    return NULL;
}

static uint64_t native_tab_window_space(int connection, uint32_t window_id)
{
    int32_t value = (int32_t) window_id;
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
    if (!number) return 0;
    const void *values[] = { number };
    CFArrayRef windows = CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
    CFRelease(number);
    if (!windows) return 0;
    CFArrayRef spaces = SLSCopySpacesForWindows(connection, 0x7, windows);
    CFRelease(windows);
    if (!spaces || CFGetTypeID(spaces) != CFArrayGetTypeID() || CFArrayGetCount(spaces) < 1) {
        if (spaces) CFRelease(spaces);
        return 0;
    }
    CFTypeRef item = CFArrayGetValueAtIndex(spaces, 0);
    uint64_t space_id = 0;
    if (!item || CFGetTypeID(item) != CFNumberGetTypeID() ||
        !CFNumberGetValue((CFNumberRef) item, kCFNumberSInt64Type, &space_id)) space_id = 0;
    CFRelease(spaces);
    return space_id;
}

static uint32_t native_tab_ax_window_id(AXUIElementRef element)
{
    uint32_t window_id = 0;
    if (element) (void) _AXUIElementGetWindow(element, &window_id);
    return window_id;
}

static void native_tab_ax_observe_window(struct native_tab_tool *tool, AXUIElementRef window)
{
    if (!tool->observer || !window || !tool->observed_ax_windows ||
        CFSetContainsValue(tool->observed_ax_windows, window)) return;
    CFSetAddValue(tool->observed_ax_windows, window);
    const CFStringRef notifications[] = {
        kAXUIElementDestroyedNotification,
        kAXWindowMiniaturizedNotification,
        kAXWindowDeminiaturizedNotification
    };
    for (size_t i = 0; i < sizeof(notifications) / sizeof(*notifications); ++i) {
        AXError result = AXObserverAddNotification(tool->observer, window, notifications[i], tool);
        if (result != kAXErrorSuccess && result != kAXErrorNotificationAlreadyRegistered &&
            result != kAXErrorNotificationUnsupported) tool->success = false;
    }
}

static void native_tab_enrich_ax_inventory(struct native_tab_tool *tool,
                                           struct native_tab_inventory *inventory)
{
    if (!tool->ax_available || !tool->application) return;
    CFTypeRef value = NULL;
    AXError result = AXUIElementCopyAttributeValue(tool->application, kAXWindowsAttribute, &value);
    if (result != kAXErrorSuccess || !value || CFGetTypeID(value) != CFArrayGetTypeID()) {
        if (value) CFRelease(value);
        inventory->complete = false;
        return;
    }
    CFArrayRef windows = (CFArrayRef) value;
    if (CFArrayGetCount(windows) > NATIVE_TAB_INVENTORY_CAPACITY) inventory->complete = false;
    for (CFIndex i = 0; i < CFArrayGetCount(windows) && i < NATIVE_TAB_INVENTORY_CAPACITY; ++i) {
        AXUIElementRef window = (AXUIElementRef) CFArrayGetValueAtIndex(windows, i);
        native_tab_ax_observe_window(tool, window);
        uint32_t window_id = native_tab_ax_window_id(window);
        struct native_tab_window_fact *fact = native_tab_inventory_find(inventory, window_id);
        if (!fact) continue;
        CFTypeRef title = NULL;
        if (AXUIElementCopyAttributeValue(window, kAXTitleAttribute, &title) == kAXErrorSuccess &&
            title && CFGetTypeID(title) == CFStringGetTypeID()) {
            fact->title_token = (uint64_t) CFHash(title) + 1;
            fact->validity |= NATIVE_TAB_FACT_TITLE;
        }
        if (title) CFRelease(title);
    }
    CFRelease(windows);
}

static void native_tab_subscribe_window_notifications(struct native_tab_tool *tool,
                                                       const struct native_tab_inventory *inventory)
{
    uint32_t window_ids[NATIVE_TAB_INVENTORY_CAPACITY];
    int count = 0;
    for (size_t i = 0; i < inventory->count && count < NATIVE_TAB_INVENTORY_CAPACITY; ++i) {
        window_ids[count++] = inventory->windows[i].window_id;
    }
    if (count) (void) SLSRequestNotificationsForWindows(tool->connection, window_ids, count);
}

static bool native_tab_capture_inventory(struct native_tab_tool *tool,
                                         struct native_tab_inventory *inventory)
{
    *inventory = (struct native_tab_inventory) { .complete = true };
    CFArrayRef info_ref = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!info_ref || CFGetTypeID(info_ref) != CFArrayGetTypeID()) {
        if (info_ref) CFRelease(info_ref);
        inventory->complete = false;
        return false;
    }

    CFMutableArrayRef ids = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    if (!ids) { CFRelease(info_ref); inventory->complete = false; return false; }
    int order = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(info_ref); ++i) {
        id value = (id) CFArrayGetValueAtIndex(info_ref, i);
        if (![value isKindOfClass:[NSDictionary class]]) { inventory->complete = false; continue; }
        NSDictionary *dictionary = value;
        int64_t owner_pid = 0, window_id = 0, level = 0;
        if (!native_tab_number(dictionary, kCGWindowOwnerPID, &owner_pid) || owner_pid != tool->target_pid) continue;
        if (!native_tab_number(dictionary, kCGWindowNumber, &window_id) || window_id <= 0 || window_id > UINT32_MAX) {
            inventory->complete = false;
            continue;
        }
        if (inventory->count >= NATIVE_TAB_INVENTORY_CAPACITY) {
            inventory->complete = false;
            continue;
        }
        if (native_tab_inventory_find(inventory, (uint32_t) window_id)) {
            inventory->complete = false;
            continue;
        }
        struct native_tab_window_fact *fact = inventory->windows + inventory->count++;
        *fact = (struct native_tab_window_fact) {
            .window_id = (uint32_t) window_id,
            .order = order++,
            .validity = NATIVE_TAB_FACT_ORDER
        };
        if (native_tab_number(dictionary, kCGWindowLayer, &level) && level >= INT_MIN && level <= INT_MAX) {
            fact->level = (int) level;
            fact->validity |= NATIVE_TAB_FACT_LEVEL;
        }
        id bounds_value = dictionary[(NSString *) kCGWindowBounds];
        CGRect frame = CGRectZero;
        if ([bounds_value isKindOfClass:[NSDictionary class]] &&
            CGRectMakeWithDictionaryRepresentation((CFDictionaryRef) bounds_value, &frame) &&
            isfinite(frame.origin.x) && isfinite(frame.origin.y) &&
            isfinite(frame.size.width) && isfinite(frame.size.height)) {
            fact->x = frame.origin.x;
            fact->y = frame.origin.y;
            fact->width = frame.size.width;
            fact->height = frame.size.height;
            fact->validity |= NATIVE_TAB_FACT_FRAME;
        }
        int32_t id_value = (int32_t) window_id;
        CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &id_value);
        if (!number) { inventory->complete = false; continue; }
        CFArrayAppendValue(ids, number);
        CFRelease(number);
    }
    CFRelease(info_ref);

    if (CFArrayGetCount(ids) != (CFIndex) inventory->count) inventory->complete = false;
    CFTypeRef query = inventory->count
        ? SLSWindowQueryWindows(tool->connection, ids, (int) inventory->count) : NULL;
    CFRelease(ids);
    if (inventory->count && !query) {
        inventory->complete = false;
    } else if (query) {
        CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
        if (!iterator || SLSWindowIteratorGetCount(iterator) != (int) inventory->count) {
            inventory->complete = false;
        } else {
            int advanced = 0;
            while (SLSWindowIteratorAdvance(iterator)) {
                ++advanced;
                uint32_t window_id = SLSWindowIteratorGetWindowID(iterator);
                struct native_tab_window_fact *fact = native_tab_inventory_find(inventory, window_id);
                if (!fact) { inventory->complete = false; continue; }
                fact->parent_window_id = SLSWindowIteratorGetParentID(iterator);
                fact->level = SLSWindowIteratorGetLevel(iterator);
                fact->validity |= NATIVE_TAB_FACT_PARENT | NATIVE_TAB_FACT_LEVEL;
            }
            if (advanced != (int) inventory->count) inventory->complete = false;
        }
        if (iterator) CFRelease(iterator);
        CFRelease(query);
    }

    for (size_t i = 0; i < inventory->count; ++i) {
        struct native_tab_window_fact *fact = inventory->windows + i;
        fact->sublevel = SLSGetWindowSubLevel(tool->connection, fact->window_id);
        fact->validity |= NATIVE_TAB_FACT_SUBLEVEL;
        uint64_t space_id = native_tab_window_space(tool->connection, fact->window_id);
        if (space_id) {
            fact->space_id = space_id;
            fact->validity |= NATIVE_TAB_FACT_SPACE;
        }
    }
    native_tab_enrich_ax_inventory(tool, inventory);
    native_tab_subscribe_window_notifications(tool, inventory);
    return inventory->complete;
}

static bool native_tab_emit_event(struct native_tab_tool *tool,
                                  enum native_tab_event_type type, uint32_t window_id,
                                  bool ordered_in_known, bool ordered_in,
                                  bool capture_inventory)
{
    uint64_t now_ns = native_tab_now_ns();
    struct native_tab_inventory inventory = {0};
    struct native_tab_window_fact *fact = NULL;
    if (capture_inventory) {
        (void) native_tab_capture_inventory(tool, &inventory);
        fact = native_tab_inventory_find(&inventory, window_id);
    }
    struct native_tab_event event = {
        .type = type,
        .time_ns = now_ns,
        .window_id = window_id,
        .ordered_in_known = ordered_in_known,
        .ordered_in = ordered_in
    };
    if (fact) { event.has_fact = true; event.fact = *fact; }
    if (!native_tab_recorder_event(&tool->recorder, &event)) return false;
    if (capture_inventory && !native_tab_recorder_inventory(&tool->recorder, now_ns, &inventory)) return false;
    return true;
}

static void native_tab_stop(struct native_tab_tool *tool)
{
    if (!tool || tool->stopping) return;
    tool->stopping = true;
    uint64_t now_ns = native_tab_now_ns();
    struct native_tab_inventory inventory;
    (void) native_tab_capture_inventory(tool, &inventory);
    tool->success &= native_tab_recorder_inventory(&tool->recorder, now_ns, &inventory);
    tool->success &= native_tab_recorder_stop(&tool->recorder, now_ns);
    CFRunLoopStop(CFRunLoopGetMain());
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static CONNECTION_CALLBACK(native_tab_connection_handler)
{
    if (!context) return;
    if (type != 1204 && (!data || data_length < sizeof(uint32_t))) return;
    struct native_tab_tool *tool = context;
    uint32_t window_id = 0;
    if (data && data_length >= sizeof(window_id)) memcpy(&window_id, data, sizeof(window_id));
    dispatch_async(dispatch_get_main_queue(), ^{
        if (tool->stopping) return;
        bool success = true;
        if (type == 815 || type == 816) {
            uint8_t ordered_in = 0;
            bool ordered_known = SLSWindowIsOrderedIn(tool->connection, window_id, &ordered_in) == kCGErrorSuccess;
            success = native_tab_emit_event(tool,
                type == 815 ? NATIVE_TAB_EVENT_SLS_VISIBLE : NATIVE_TAB_EVENT_SLS_INVISIBLE,
                window_id, ordered_known, ordered_in != 0, true);
        } else if (type == 804) {
            success = native_tab_emit_event(tool, NATIVE_TAB_EVENT_SLS_DESTROYED,
                                            window_id, false, false, true);
        } else if (type == 1204) {
            success = native_tab_emit_event(tool, NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER,
                                            0, false, false, false);
        }
        if (!success) { tool->success = false; native_tab_stop(tool); }
    });
}

static void native_tab_ax_handler(AXObserverRef observer, AXUIElementRef element,
                                  CFStringRef notification, void *context)
{
    struct native_tab_tool *tool = context;
    if (!tool || tool->stopping) return;
    uint32_t window_id = native_tab_ax_window_id(element);
    enum native_tab_event_type type;
    if (CFEqual(notification, kAXCreatedNotification)) {
        type = NATIVE_TAB_EVENT_AX_CREATED;
        native_tab_ax_observe_window(tool, element);
    } else if (CFEqual(notification, kAXUIElementDestroyedNotification)) {
        type = NATIVE_TAB_EVENT_AX_DESTROYED;
    } else if (CFEqual(notification, kAXWindowMiniaturizedNotification)) {
        type = NATIVE_TAB_EVENT_AX_MINIMIZED;
    } else if (CFEqual(notification, kAXWindowDeminiaturizedNotification)) {
        type = NATIVE_TAB_EVENT_AX_DEMINIMIZED;
    } else if (CFEqual(notification, kAXFocusedWindowChangedNotification)) {
        type = NATIVE_TAB_EVENT_AX_FOCUSED;
        if (!window_id && tool->application) {
            CFTypeRef focused = NULL;
            if (AXUIElementCopyAttributeValue(tool->application, kAXFocusedWindowAttribute, &focused) == kAXErrorSuccess) {
                window_id = native_tab_ax_window_id((AXUIElementRef) focused);
            }
            if (focused) CFRelease(focused);
        }
    } else {
        return;
    }
    if (!native_tab_emit_event(tool, type, window_id, false, false, true)) {
        tool->success = false;
        native_tab_stop(tool);
    }
}
#pragma clang diagnostic pop

static bool native_tab_setup_ax(struct native_tab_tool *tool)
{
    tool->ax_available = AXIsProcessTrusted();
    if (!tool->ax_available) return true;
    tool->application = AXUIElementCreateApplication(tool->target_pid);
    if (!tool->application || AXObserverCreate(tool->target_pid, native_tab_ax_handler, &tool->observer) != kAXErrorSuccess) {
        tool->ax_available = false;
        return true;
    }
    tool->observed_ax_windows = CFSetCreateMutable(NULL, 0, &kCFTypeSetCallBacks);
    if (!tool->observed_ax_windows) return false;
    const CFStringRef notifications[] = {
        kAXCreatedNotification,
        kAXFocusedWindowChangedNotification,
        kAXWindowMiniaturizedNotification,
        kAXWindowDeminiaturizedNotification
    };
    for (size_t i = 0; i < sizeof(notifications) / sizeof(*notifications); ++i) {
        AXError result = AXObserverAddNotification(tool->observer, tool->application,
                                                   notifications[i], tool);
        if (result != kAXErrorSuccess && result != kAXErrorNotificationAlreadyRegistered &&
            result != kAXErrorNotificationUnsupported) return false;
    }
    CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(tool->observer),
                       kCFRunLoopDefaultMode);
    return true;
}

static void native_tab_setup_workspace(struct native_tab_tool *tool)
{
    NSNotificationCenter *center = NSWorkspace.sharedWorkspace.notificationCenter;
    NSOperationQueue *queue = NSOperationQueue.mainQueue;
    tool->workspace_tokens[0] = [center addObserverForName:NSWorkspaceDidHideApplicationNotification
        object:nil queue:queue usingBlock:^(NSNotification *notification) {
            NSRunningApplication *application = notification.userInfo[NSWorkspaceApplicationKey];
            if (application.processIdentifier == tool->target_pid &&
                !native_tab_emit_event(tool, NATIVE_TAB_EVENT_APP_HIDDEN, 0, false, false, false)) {
                tool->success = false; native_tab_stop(tool);
            }
        }];
    tool->workspace_tokens[1] = [center addObserverForName:NSWorkspaceDidUnhideApplicationNotification
        object:nil queue:queue usingBlock:^(NSNotification *notification) {
            NSRunningApplication *application = notification.userInfo[NSWorkspaceApplicationKey];
            if (application.processIdentifier == tool->target_pid &&
                !native_tab_emit_event(tool, NATIVE_TAB_EVENT_APP_VISIBLE, 0, false, false, true)) {
                tool->success = false; native_tab_stop(tool);
            }
        }];
    tool->workspace_tokens[2] = [center addObserverForName:NSWorkspaceDidTerminateApplicationNotification
        object:nil queue:queue usingBlock:^(NSNotification *notification) {
            NSRunningApplication *application = notification.userInfo[NSWorkspaceApplicationKey];
            if (application.processIdentifier == tool->target_pid) {
                tool->success &= native_tab_emit_event(tool, NATIVE_TAB_EVENT_APP_TERMINATED,
                                                       0, false, false, false);
                native_tab_stop(tool);
            }
        }];
    tool->workspace_tokens[3] = [center addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
        object:nil queue:queue usingBlock:^(NSNotification *notification) {
            (void) notification;
            if (!native_tab_emit_event(tool, NATIVE_TAB_EVENT_SPACE_CHANGED, 0, false, false, true)) {
                tool->success = false; native_tab_stop(tool);
            }
        }];
}

static void native_tab_timer(CFRunLoopTimerRef timer, void *context)
{
    (void) timer;
    struct native_tab_tool *tool = context;
    if (!tool || tool->stopping) return;
    uint64_t now_ns = native_tab_now_ns();
    if (native_tab_recorder_hard_due(&tool->recorder, now_ns)) {
        struct native_tab_inventory inventory;
        (void) native_tab_capture_inventory(tool, &inventory);
        uint64_t deadline = tool->recorder.episode.hard_deadline_ns;
        tool->success &= native_tab_recorder_inventory(&tool->recorder, deadline, &inventory);
        tool->success &= native_tab_recorder_tick(&tool->recorder, now_ns);
    } else if (native_tab_recorder_settle_due(&tool->recorder, now_ns)) {
        struct native_tab_inventory inventory;
        (void) native_tab_capture_inventory(tool, &inventory);
        tool->success &= native_tab_recorder_inventory(&tool->recorder, now_ns, &inventory);
        tool->success &= native_tab_recorder_tick(&tool->recorder, now_ns);
    } else if (now_ns >= tool->next_poll_ns) {
        struct native_tab_inventory inventory;
        (void) native_tab_capture_inventory(tool, &inventory);
        tool->success &= native_tab_recorder_inventory(&tool->recorder, now_ns, &inventory);
        tool->next_poll_ns = now_ns + NATIVE_TAB_SETTLE_DELAY_NS;
    } else {
        tool->success &= native_tab_recorder_tick(&tool->recorder, now_ns);
    }
    if (!tool->success || now_ns >= tool->stop_ns) native_tab_stop(tool);
}

static void native_tab_cleanup(struct native_tab_tool *tool)
{
    NSNotificationCenter *center = NSWorkspace.sharedWorkspace.notificationCenter;
    for (size_t i = 0; i < sizeof(tool->workspace_tokens) / sizeof(*tool->workspace_tokens); ++i) {
        if (tool->workspace_tokens[i]) [center removeObserver:tool->workspace_tokens[i]];
    }
    if (tool->timer) {
        CFRunLoopTimerInvalidate(tool->timer);
        CFRelease(tool->timer);
    }
    if (tool->observer) {
        CFRunLoopSourceInvalidate(AXObserverGetRunLoopSource(tool->observer));
        CFRelease(tool->observer);
    }
    if (tool->observed_ax_windows) CFRelease(tool->observed_ax_windows);
    if (tool->application) CFRelease(tool->application);
}

static bool native_tab_parse_positive(const char *text, long minimum, long maximum, long *value)
{
    char *end = NULL;
    long result = strtol(text, &end, 10);
    if (!end || *end || result < minimum || result > maximum) return false;
    *value = result;
    return true;
}

static void native_tab_usage(const char *program)
{
    fprintf(stderr, "usage: %s --pid PID --scenario NAME --duration SECONDS --output PATH\n", program);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        long pid_value = 0, duration = 0;
        const char *scenario = NULL, *output = NULL;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
                if (!native_tab_parse_positive(argv[++i], 2, INT_MAX, &pid_value)) return EXIT_FAILURE;
            } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
                scenario = argv[++i];
            } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
                if (!native_tab_parse_positive(argv[++i], 1, 600, &duration)) return EXIT_FAILURE;
            } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                output = argv[++i];
            } else {
                native_tab_usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        if (!pid_value || !duration || !native_tab_scenario_is_valid(scenario) || !output) {
            native_tab_usage(argv[0]);
            return EXIT_FAILURE;
        }

        NSRunningApplication *running = [NSRunningApplication runningApplicationWithProcessIdentifier:(pid_t) pid_value];
        if (!running || running.terminated) return EXIT_FAILURE;
        FILE *stream = NULL;
        if (!native_tab_trace_open(output, &stream)) return EXIT_FAILURE;

        struct native_tab_tool tool = {
            .target_pid = (pid_t) pid_value,
            .connection = SLSMainConnectionID(),
            .output_path = output,
            .start_ns = native_tab_now_ns(),
            .success = true
        };
        tool.stop_ns = tool.start_ns + (uint64_t) duration * UINT64_C(1000000000);
        tool.next_poll_ns = tool.start_ns + NATIVE_TAB_SETTLE_DELAY_NS;
        if (!native_tab_setup_ax(&tool)) tool.success = false;

        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char os_version[64];
        snprintf(os_version, sizeof(os_version), "%ld.%ld.%ld",
                 version.majorVersion, version.minorVersion, version.patchVersion);
        char build[256] = {0}; size_t build_size = sizeof(build); struct utsname system = {0};
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 ||
            !memchr(build, '\0', sizeof(build)) || uname(&system) != 0) tool.success = false;
        struct native_tab_recorder_metadata metadata = {
            .os_version = os_version, .os_build = build,
            .architecture = system.machine, .ax_available = tool.ax_available
        };
        tool.success &= native_tab_recorder_init(&tool.recorder, stream, scenario,
                                                 tool.start_ns, &metadata);
        struct native_tab_inventory baseline;
        (void) native_tab_capture_inventory(&tool, &baseline);
        tool.success &= native_tab_recorder_baseline(&tool.recorder, tool.start_ns, &baseline);

        const uint32_t events[] = { 815, 816, 804, 1204 };
        for (size_t i = 0; tool.success && i < sizeof(events) / sizeof(*events); ++i) {
            tool.success &= SLSRegisterConnectionNotifyProc(tool.connection,
                native_tab_connection_handler, events[i], &tool) == kCGErrorSuccess;
        }
        native_tab_setup_workspace(&tool);

        CFRunLoopTimerContext timer_context = { .info = &tool };
        tool.timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.02, 0.02, 0, 0,
                                          native_tab_timer, &timer_context);
        if (tool.timer) {
            CFRunLoopAddTimer(CFRunLoopGetMain(), tool.timer, kCFRunLoopCommonModes);
        } else {
            tool.success = false;
        }

        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        dispatch_source_t interrupt = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0,
                                                              dispatch_get_main_queue());
        dispatch_source_t terminate = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0,
                                                              dispatch_get_main_queue());
        dispatch_source_set_event_handler(interrupt, ^{ native_tab_stop(&tool); });
        dispatch_source_set_event_handler(terminate, ^{ native_tab_stop(&tool); });
        dispatch_resume(interrupt);
        dispatch_resume(terminate);

        if (tool.success) CFRunLoopRun();
        else native_tab_stop(&tool);
        native_tab_cleanup(&tool);
        bool success = native_tab_trace_close(stream, output, tool.success && tool.recorder.stopped);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
