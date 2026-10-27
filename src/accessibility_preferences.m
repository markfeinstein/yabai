#include "accessibility_preferences.h"
#include "menu_bar.h"

#import <AppKit/AppKit.h>

#include <stdint.h>

#define ACCESSIBILITY_PREFERENCE_REDUCE_MOTION              (1u << 0)
#define ACCESSIBILITY_PREFERENCE_REDUCE_TRANSPARENCY        (1u << 1)
#define ACCESSIBILITY_PREFERENCE_DIFFERENTIATE_WITHOUT_COLOR (1u << 2)

typedef void (*accessibility_preferences_callback)(void *context);
typedef bool (*accessibility_preferences_observer_begin_fn)(accessibility_preferences_callback callback,
                                                            void *context, void **token_out);
typedef void (*accessibility_preferences_observer_end_fn)(void *token);

static uint32_t g_accessibility_preferences_bits;
static uint64_t g_accessibility_preferences_generation;
static bool g_accessibility_preferences_started;
static void *g_accessibility_preferences_observer;

static bool accessibility_preferences_workspace_bool(NSString *selector_name)
{
    NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
    SEL selector = NSSelectorFromString(selector_name);
    if (!workspace || !selector || ![workspace respondsToSelector:selector]) return false;

    typedef BOOL (*accessibility_preferences_getter)(id, SEL);
    accessibility_preferences_getter getter =
        (accessibility_preferences_getter) [workspace methodForSelector:selector];
    return getter ? getter(workspace, selector) != NO : false;
}

static bool accessibility_preferences_read_reduce_motion(void)
{
    return accessibility_preferences_workspace_bool(@"accessibilityDisplayShouldReduceMotion");
}

static bool accessibility_preferences_read_reduce_transparency(void)
{
    return accessibility_preferences_workspace_bool(@"accessibilityDisplayShouldReduceTransparency");
}

static bool accessibility_preferences_read_differentiate_without_color(void)
{
    return accessibility_preferences_workspace_bool(@"accessibilityDisplayShouldDifferentiateWithoutColor");
}

static bool accessibility_preferences_observer_begin_production(accessibility_preferences_callback callback,
                                                                 void *context, void **token_out)
{
    if (token_out) *token_out = NULL;
    NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
    NSNotificationCenter *center = [workspace notificationCenter];
    if (!center || !callback || !token_out) return false;

    NSString *name = @"NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification";
    id observer = [center addObserverForName:name object:nil queue:nil usingBlock:^(NSNotification *notification) {
        (void) notification;
        callback(context);
    }];
    if (!observer) return false;

    *token_out = [observer retain];
    return true;
}

static void accessibility_preferences_observer_end_production(void *token)
{
    if (!token) return;
    NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
    [[workspace notificationCenter] removeObserver:(id) token];
    [(id) token release];
}

#ifdef TESTS
static bool (*accessibility_preferences_reader_reduce_motion)(void) =
    accessibility_preferences_read_reduce_motion;
static bool (*accessibility_preferences_reader_reduce_transparency)(void) =
    accessibility_preferences_read_reduce_transparency;
static bool (*accessibility_preferences_reader_differentiate_without_color)(void) =
    accessibility_preferences_read_differentiate_without_color;
static accessibility_preferences_observer_begin_fn accessibility_preferences_observer_begin_impl =
    accessibility_preferences_observer_begin_production;
static accessibility_preferences_observer_end_fn accessibility_preferences_observer_end_impl =
    accessibility_preferences_observer_end_production;
static void (*accessibility_preferences_invalidate_menu)(void) =
    menu_bar_accessibility_preferences_changed;
#else
#define accessibility_preferences_reader_reduce_motion accessibility_preferences_read_reduce_motion
#define accessibility_preferences_reader_reduce_transparency accessibility_preferences_read_reduce_transparency
#define accessibility_preferences_reader_differentiate_without_color accessibility_preferences_read_differentiate_without_color
#define accessibility_preferences_observer_begin_impl accessibility_preferences_observer_begin_production
#define accessibility_preferences_observer_end_impl accessibility_preferences_observer_end_production
#define accessibility_preferences_invalidate_menu menu_bar_accessibility_preferences_changed
#endif

static uint32_t accessibility_preferences_read_bits(void)
{
    uint32_t bits = 0;
    if (accessibility_preferences_reader_reduce_motion &&
        accessibility_preferences_reader_reduce_motion()) {
        bits |= ACCESSIBILITY_PREFERENCE_REDUCE_MOTION;
    }
    if (accessibility_preferences_reader_reduce_transparency &&
        accessibility_preferences_reader_reduce_transparency()) {
        bits |= ACCESSIBILITY_PREFERENCE_REDUCE_TRANSPARENCY;
    }
    if (accessibility_preferences_reader_differentiate_without_color &&
        accessibility_preferences_reader_differentiate_without_color()) {
        bits |= ACCESSIBILITY_PREFERENCE_DIFFERENTIATE_WITHOUT_COLOR;
    }
    return bits;
}

static void accessibility_preferences_refresh(void *context)
{
    uint64_t generation = (uint64_t)(uintptr_t) context;
    if (!__atomic_load_n(&g_accessibility_preferences_started, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&g_accessibility_preferences_generation, __ATOMIC_ACQUIRE) != generation) {
        return;
    }

    uint32_t bits = accessibility_preferences_read_bits();
    __atomic_store_n(&g_accessibility_preferences_bits, bits, __ATOMIC_RELEASE);
    accessibility_preferences_invalidate_menu();
}

bool accessibility_preferences_begin(void)
{
    accessibility_preferences_end();

    uint32_t bits = accessibility_preferences_read_bits();
    __atomic_store_n(&g_accessibility_preferences_bits, bits, __ATOMIC_RELEASE);
    uint64_t generation = __atomic_add_fetch(&g_accessibility_preferences_generation, 1, __ATOMIC_ACQ_REL);
    if (!generation) generation = __atomic_add_fetch(&g_accessibility_preferences_generation, 1, __ATOMIC_ACQ_REL);
    __atomic_store_n(&g_accessibility_preferences_started, true, __ATOMIC_RELEASE);

    void *observer = NULL;
    if (!accessibility_preferences_observer_begin_impl(
            accessibility_preferences_refresh, (void *)(uintptr_t) generation, &observer)) {
        __atomic_store_n(&g_accessibility_preferences_started, false, __ATOMIC_RELEASE);
        return false;
    }

    g_accessibility_preferences_observer = observer;
    return true;
}

void accessibility_preferences_end(void)
{
    __atomic_store_n(&g_accessibility_preferences_started, false, __ATOMIC_RELEASE);
    (void) __atomic_add_fetch(&g_accessibility_preferences_generation, 1, __ATOMIC_ACQ_REL);

    void *observer = g_accessibility_preferences_observer;
    g_accessibility_preferences_observer = NULL;
    if (observer) accessibility_preferences_observer_end_impl(observer);
}

struct accessibility_display_preferences accessibility_preferences_snapshot(void)
{
    uint32_t bits = __atomic_load_n(&g_accessibility_preferences_bits, __ATOMIC_ACQUIRE);
    return (struct accessibility_display_preferences) {
        .reduce_motion = (bits & ACCESSIBILITY_PREFERENCE_REDUCE_MOTION) != 0,
        .reduce_transparency = (bits & ACCESSIBILITY_PREFERENCE_REDUCE_TRANSPARENCY) != 0,
        .differentiate_without_color = (bits & ACCESSIBILITY_PREFERENCE_DIFFERENTIATE_WITHOUT_COLOR) != 0
    };
}
