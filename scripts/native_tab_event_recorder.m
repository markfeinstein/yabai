#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    FILE *control_events;
    struct {
        uint32_t window_id;
        uint32_t generation;
    } subscriptions[NATIVE_TAB_INVENTORY_CAPACITY];
    size_t subscription_count;
    uint32_t subscription_generation;
    volatile uint32_t delivered_815;
    volatile uint32_t delivered_816;
    uint32_t last_delivery_code;
    uint32_t last_delivery_window;
    uint64_t last_delivery_ns;
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
        /* Per-window AX notifications are optional evidence. Inventory remains
         * authoritative when a target does not support one of them. */
        (void) result;
    }
}

static uint32_t native_tab_ax_application_window(AXUIElementRef application, CFStringRef attribute)
{
    CFTypeRef value = NULL;
    uint32_t window_id = 0;
    if (application && AXUIElementCopyAttributeValue(application, attribute, &value) == kAXErrorSuccess) {
        window_id = native_tab_ax_window_id((AXUIElementRef) value);
    }
    if (value) CFRelease(value);
    return window_id;
}

static void native_tab_enrich_ax_inventory(struct native_tab_tool *tool,
                                           struct native_tab_inventory *inventory)
{
    if (!tool->ax_available || !tool->application) return;
    uint32_t focused_window_id = native_tab_ax_application_window(
        tool->application, kAXFocusedWindowAttribute);
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
        fact->ax_member = true;
        fact->focused = window_id && window_id == focused_window_id;
        fact->validity |= NATIVE_TAB_FACT_AX | NATIVE_TAB_FACT_STATE;
        CFTypeRef title = NULL, role = NULL, subrole = NULL;
        if (AXUIElementCopyAttributeValue(window, kAXTitleAttribute, &title) == kAXErrorSuccess &&
            title && CFGetTypeID(title) == CFStringGetTypeID()) {
            fact->title_token = (uint64_t) CFHash(title) + 1;
            fact->validity |= NATIVE_TAB_FACT_TITLE;
        }
        if (AXUIElementCopyAttributeValue(window, kAXRoleAttribute, &role) == kAXErrorSuccess &&
            role && CFGetTypeID(role) == CFStringGetTypeID()) {
            fact->role_token = (uint32_t)CFHash(role) + 1;
            fact->validity |= NATIVE_TAB_FACT_ROLE;
        }
        if (AXUIElementCopyAttributeValue(window, kAXSubroleAttribute, &subrole) == kAXErrorSuccess &&
            subrole && CFGetTypeID(subrole) == CFStringGetTypeID()) {
            fact->subrole_token = (uint32_t)CFHash(subrole) + 1;
            fact->validity |= NATIVE_TAB_FACT_SUBROLE;
        }
        CFTypeRef state = NULL;
        if (AXUIElementCopyAttributeValue(window, kAXMinimizedAttribute, &state) == kAXErrorSuccess &&
            state && CFGetTypeID(state) == CFBooleanGetTypeID()) {
            fact->minimized = CFBooleanGetValue((CFBooleanRef)state);
        }
        if (state) CFRelease(state); state = NULL;
        if (AXUIElementCopyAttributeValue(window, CFSTR("AXFullScreen"), &state) == kAXErrorSuccess &&
            state && CFGetTypeID(state) == CFBooleanGetTypeID()) {
            fact->fullscreen = CFBooleanGetValue((CFBooleanRef)state);
        }
        if (state) CFRelease(state);
        if (title) CFRelease(title);
        if (role) CFRelease(role);
        if (subrole) CFRelease(subrole);
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
    if (!count) return;
    uint32_t generation = ++tool->subscription_generation;
    if (SLSRequestNotificationsForWindows(tool->connection, window_ids, count) != kCGErrorSuccess) return;
    for (int i = 0; i < count; ++i) {
        size_t index = 0;
        while (index < tool->subscription_count &&
               tool->subscriptions[index].window_id != window_ids[i]) ++index;
        if (index == tool->subscription_count) {
            if (tool->subscription_count >= NATIVE_TAB_INVENTORY_CAPACITY) continue;
            ++tool->subscription_count;
        }
        tool->subscriptions[index].window_id = window_ids[i];
        tool->subscriptions[index].generation = generation;
    }
}

static bool native_tab_subscription_generation(const struct native_tab_tool *tool,
                                                uint32_t window_id, uint32_t *generation)
{
    for (size_t i = 0; i < tool->subscription_count; ++i) {
        if (tool->subscriptions[i].window_id != window_id) continue;
        if (generation) *generation = tool->subscriptions[i].generation;
        return true;
    }
    if (generation) *generation = 0;
    return false;
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
                                  const struct native_tab_event *details,
                                  bool capture_inventory)
{
    uint64_t now_ns = native_tab_now_ns();
    struct native_tab_inventory inventory = {0};
    struct native_tab_window_fact *fact = NULL;
    if (capture_inventory) {
        (void) native_tab_capture_inventory(tool, &inventory);
        fact = native_tab_inventory_find(&inventory, window_id);
    }
    struct native_tab_event event = details ? *details : (struct native_tab_event) {0};
    event.type = type;
    event.time_ns = details && details->time_ns ? details->time_ns : now_ns;
    event.window_id = window_id;
    event.ordered_in_known = ordered_in_known;
    event.ordered_in = ordered_in;
    if (fact) {
        event.has_fact = true;
        event.fact = *fact;
        event.target_pid_known = true;
        event.target_pid = true;
    }
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
    struct native_tab_tool *tool = context;
    uint64_t callback_ns = native_tab_now_ns();
    bool payload_valid = type == 1204 || (data && data_length >= sizeof(uint32_t));
    uint32_t window_id = 0;
    if (payload_valid && data && data_length >= sizeof(window_id)) {
        memcpy(&window_id, data, sizeof(window_id));
    }
    uint32_t delivered_index = 0;
    if (type == 815) delivered_index = __atomic_add_fetch(&tool->delivered_815, 1, __ATOMIC_RELAXED);
    if (type == 816) delivered_index = __atomic_add_fetch(&tool->delivered_816, 1, __ATOMIC_RELAXED);
    uint32_t subscription_generation = 0;
    bool subscribed = window_id && native_tab_subscription_generation(
        tool, window_id, &subscription_generation);
    bool duplicate = payload_valid && window_id && tool->last_delivery_code == type &&
                     tool->last_delivery_window == window_id &&
                     callback_ns >= tool->last_delivery_ns &&
                     callback_ns - tool->last_delivery_ns <= NATIVE_TAB_SETTLE_DELAY_NS;
    if (type == 815 || type == 816) {
        tool->last_delivery_code = type;
        tool->last_delivery_window = window_id;
        tool->last_delivery_ns = callback_ns;
    }
    struct native_tab_event details = {
        .time_ns = callback_ns,
        .sls_code = type,
        .payload_length = data_length > UINT32_MAX ? UINT32_MAX : (uint32_t)data_length,
        .delivered_index = delivered_index,
        .subscription_generation = subscription_generation,
        .payload_valid = payload_valid,
        .duplicate = duplicate,
        .subscribed_before_known = window_id != 0,
        .subscribed_before = subscribed
    };
    dispatch_async(dispatch_get_main_queue(), ^{
        if (tool->stopping) return;
        bool success = true;
        if (type == 815 || type == 816) {
            uint8_t ordered_in = 0;
            bool ordered_known = payload_valid && window_id &&
                SLSWindowIsOrderedIn(tool->connection, window_id, &ordered_in) == kCGErrorSuccess;
            success = native_tab_emit_event(tool,
                type == 815 ? NATIVE_TAB_EVENT_SLS_VISIBLE : NATIVE_TAB_EVENT_SLS_INVISIBLE,
                window_id, ordered_known, ordered_in != 0, &details, true);
        } else if (type == 804) {
            success = native_tab_emit_event(tool, NATIVE_TAB_EVENT_SLS_DESTROYED,
                                            window_id, false, false, &details, true);
        } else if (type == 1204) {
            success = native_tab_emit_event(tool, NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER,
                                            0, false, false, &details, false);
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
    struct native_tab_event details = {0};
    if (type == NATIVE_TAB_EVENT_AX_FOCUSED) {
        details.focus_subject_known = window_id != 0;
        details.focus_subject_window_id = window_id;
        details.app_focus_window_id = native_tab_ax_application_window(
            tool->application, kAXFocusedWindowAttribute);
        details.app_focus_known = details.app_focus_window_id != 0;
        details.main_window_id = native_tab_ax_application_window(
            tool->application, kAXMainWindowAttribute);
        details.main_window_known = details.main_window_id != 0;
    }
    if (!native_tab_emit_event(tool, type, window_id, false, false, &details, true)) {
        tool->success = false;
        native_tab_stop(tool);
        return;
    }
    if (type == NATIVE_TAB_EVENT_AX_FOCUSED) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)NATIVE_TAB_SETTLE_DELAY_NS),
                       dispatch_get_main_queue(), ^{
            if (tool->stopping) return;
            uint32_t stable = native_tab_ax_application_window(
                tool->application, kAXFocusedWindowAttribute);
            struct native_tab_event stable_details = {
                .app_focus_known = stable != 0,
                .app_focus_window_id = stable
            };
            if (!native_tab_emit_event(tool, NATIVE_TAB_EVENT_AX_FOCUS_STABLE,
                                       stable, false, false, &stable_details, true)) {
                tool->success = false;
                native_tab_stop(tool);
            }
        });
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
                !native_tab_emit_event(tool, NATIVE_TAB_EVENT_APP_HIDDEN, 0, false, false, NULL, false)) {
                tool->success = false; native_tab_stop(tool);
            }
        }];
    tool->workspace_tokens[1] = [center addObserverForName:NSWorkspaceDidUnhideApplicationNotification
        object:nil queue:queue usingBlock:^(NSNotification *notification) {
            NSRunningApplication *application = notification.userInfo[NSWorkspaceApplicationKey];
            if (application.processIdentifier == tool->target_pid &&
                !native_tab_emit_event(tool, NATIVE_TAB_EVENT_APP_VISIBLE, 0, false, false, NULL, true)) {
                tool->success = false; native_tab_stop(tool);
            }
        }];
    tool->workspace_tokens[2] = [center addObserverForName:NSWorkspaceDidTerminateApplicationNotification
        object:nil queue:queue usingBlock:^(NSNotification *notification) {
            NSRunningApplication *application = notification.userInfo[NSWorkspaceApplicationKey];
            if (application.processIdentifier == tool->target_pid) {
                tool->success &= native_tab_emit_event(tool, NATIVE_TAB_EVENT_APP_TERMINATED,
                                                       0, false, false, NULL, false);
                native_tab_stop(tool);
            }
        }];
    tool->workspace_tokens[3] = [center addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
        object:nil queue:queue usingBlock:^(NSNotification *notification) {
            (void) notification;
            if (!native_tab_emit_event(tool, NATIVE_TAB_EVENT_SPACE_CHANGED, 0, false, false, NULL, true)) {
                tool->success = false; native_tab_stop(tool);
            }
        }];
}

static bool native_tab_control_event_type(const char *line,
                                          enum native_tab_event_type *type)
{
    if (!strcmp(line, "close")) *type = NATIVE_TAB_EVENT_CONTROL_CLOSE;
    else if (!strcmp(line, "minimize")) *type = NATIVE_TAB_EVENT_CONTROL_MINIMIZE;
    else if (!strcmp(line, "restore")) *type = NATIVE_TAB_EVENT_CONTROL_RESTORE;
    else if (!strcmp(line, "hide")) *type = NATIVE_TAB_EVENT_CONTROL_HIDE;
    else if (!strcmp(line, "unhide")) *type = NATIVE_TAB_EVENT_CONTROL_UNHIDE;
    else if (!strcmp(line, "space-change")) *type = NATIVE_TAB_EVENT_CONTROL_SPACE_CHANGE;
    else return false;
    return true;
}

static bool native_tab_poll_control_events(struct native_tab_tool *tool)
{
    if (!tool->control_events) return true;
    clearerr(tool->control_events);
    char line[64];
    while (fgets(line, sizeof(line), tool->control_events)) {
        size_t length = strlen(line);
        if (!length || line[length - 1] != '\n') return false;
        line[--length] = '\0';
        if (length && line[length - 1] == '\r') line[--length] = '\0';
        enum native_tab_event_type type;
        if (!native_tab_control_event_type(line, &type) ||
            !native_tab_emit_event(tool, type, 0, false, false, NULL, true)) return false;
    }
    return !ferror(tool->control_events);
}

static void native_tab_timer(CFRunLoopTimerRef timer, void *context)
{
    (void) timer;
    struct native_tab_tool *tool = context;
    if (!tool || tool->stopping) return;
    if (!native_tab_poll_control_events(tool)) {
        tool->success = false;
        native_tab_stop(tool);
        return;
    }
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
    if (tool->control_events) fclose(tool->control_events);
}

static bool native_tab_open_control_events(const char *path, FILE **stream)
{
    if (!path || !stream) return false;
    *stream = NULL;
    struct stat before;
    if (lstat(path, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_uid != getuid() || (before.st_mode & 0777) != 0600) return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat after;
    if (fstat(fd, &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_nlink != 1) {
        close(fd); return false;
    }
    FILE *result = fdopen(fd, "r");
    if (!result) { close(fd); return false; }
    *stream = result;
    return true;
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
    fprintf(stderr,
            "usage: %s --pid PID --scenario NAME --fixture-token TOKEN --batch ID "
            "--repeat 1..5 --direction TOKEN --expectation candidate|negative-control "
            "--duration SECONDS --output PATH [--control-events PATH]\n", program);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        long pid_value = 0, duration = 0, planned_repeat = 0;
        const char *scenario = NULL, *output = NULL, *fixture_token = NULL;
        const char *batch_id = NULL, *direction = NULL, *expectation = NULL;
        const char *control_events_path = NULL;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
                if (!native_tab_parse_positive(argv[++i], 2, INT_MAX, &pid_value)) return EXIT_FAILURE;
            } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
                scenario = argv[++i];
            } else if (strcmp(argv[i], "--fixture-token") == 0 && i + 1 < argc) {
                fixture_token = argv[++i];
            } else if (strcmp(argv[i], "--batch") == 0 && i + 1 < argc) {
                batch_id = argv[++i];
            } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
                if (!native_tab_parse_positive(argv[++i], 1, 5, &planned_repeat)) return EXIT_FAILURE;
            } else if (strcmp(argv[i], "--direction") == 0 && i + 1 < argc) {
                direction = argv[++i];
            } else if (strcmp(argv[i], "--expectation") == 0 && i + 1 < argc) {
                expectation = argv[++i];
            } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
                if (!native_tab_parse_positive(argv[++i], 1, 600, &duration)) return EXIT_FAILURE;
            } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                output = argv[++i];
            } else if (strcmp(argv[i], "--control-events") == 0 && i + 1 < argc) {
                control_events_path = argv[++i];
            } else {
                native_tab_usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        if (!pid_value || !duration || !planned_repeat ||
            !native_tab_scenario_is_valid(scenario) ||
            !native_tab_scenario_is_valid(fixture_token) ||
            !native_tab_scenario_is_valid(batch_id) ||
            !native_tab_scenario_is_valid(direction) ||
            (!expectation || (strcmp(expectation, "candidate") != 0 &&
                              strcmp(expectation, "negative-control") != 0)) || !output) {
            native_tab_usage(argv[0]);
            return EXIT_FAILURE;
        }

        NSRunningApplication *running = [NSRunningApplication runningApplicationWithProcessIdentifier:(pid_t) pid_value];
        if (!running || running.terminated) return EXIT_FAILURE;
        FILE *stream = NULL;
        if (!native_tab_trace_open(output, &stream)) return EXIT_FAILURE;

        struct native_tab_tool *tool = calloc(1, sizeof(*tool));
        if (!tool) {
            native_tab_trace_close(stream, output, false);
            return EXIT_FAILURE;
        }
        *tool = (struct native_tab_tool) {
            .target_pid = (pid_t) pid_value,
            .connection = SLSMainConnectionID(),
            .output_path = output,
            .start_ns = native_tab_now_ns(),
            .success = true
        };
        tool->stop_ns = tool->start_ns + (uint64_t) duration * UINT64_C(1000000000);
        tool->next_poll_ns = tool->start_ns + NATIVE_TAB_SETTLE_DELAY_NS;
        if (control_events_path &&
            !native_tab_open_control_events(control_events_path, &tool->control_events)) {
            native_tab_trace_close(stream, output, false);
            free(tool);
            return EXIT_FAILURE;
        }
        if (!native_tab_setup_ax(tool)) tool->success = false;

        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char os_version[64];
        snprintf(os_version, sizeof(os_version), "%ld.%ld.%ld",
                 version.majorVersion, version.minorVersion, version.patchVersion);
        char build[256] = {0}; size_t build_size = sizeof(build); struct utsname system = {0};
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 ||
            !memchr(build, '\0', sizeof(build)) || uname(&system) != 0) tool->success = false;
        struct native_tab_recorder_metadata metadata = {
            .os_version = os_version, .os_build = build,
            .architecture = system.machine, .fixture_token = fixture_token,
            .batch_id = batch_id, .direction = direction,
            .expectation = expectation, .planned_repeat = (unsigned)planned_repeat,
            .ax_available = tool->ax_available,
            .control_events_available = tool->control_events != NULL
        };
        tool->success &= native_tab_recorder_init(&tool->recorder, stream, scenario,
                                                  tool->start_ns, &metadata);
        struct native_tab_inventory baseline;
        (void) native_tab_capture_inventory(tool, &baseline);
        tool->success &= native_tab_recorder_baseline(&tool->recorder, tool->start_ns, &baseline);

        const uint32_t events[] = { 815, 816, 804, 1204 };
        for (size_t i = 0; tool->success && i < sizeof(events) / sizeof(*events); ++i) {
            tool->success &= SLSRegisterConnectionNotifyProc(tool->connection,
                native_tab_connection_handler, events[i], tool) == kCGErrorSuccess;
        }
        native_tab_setup_workspace(tool);

        CFRunLoopTimerContext timer_context = { .info = tool };
        tool->timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.02, 0.02, 0, 0,
                                           native_tab_timer, &timer_context);
        if (tool->timer) {
            CFRunLoopAddTimer(CFRunLoopGetMain(), tool->timer, kCFRunLoopCommonModes);
        } else {
            tool->success = false;
        }

        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        dispatch_source_t interrupt = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0,
                                                              dispatch_get_main_queue());
        dispatch_source_t terminate = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0,
                                                              dispatch_get_main_queue());
        dispatch_source_set_event_handler(interrupt, ^{ native_tab_stop(tool); });
        dispatch_source_set_event_handler(terminate, ^{ native_tab_stop(tool); });
        dispatch_resume(interrupt);
        dispatch_resume(terminate);

        if (tool->success) CFRunLoopRun();
        else native_tab_stop(tool);
        native_tab_cleanup(tool);
        bool success = native_tab_trace_close(stream, output,
                                               tool->success && tool->recorder.stopped);
        free(tool);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
