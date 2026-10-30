#ifndef MENU_BAR_H
#define MENU_BAR_H

#include <stdbool.h>
#include <stdint.h>

#include "menu_bar_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native AppKit menu-bar boundary.
 *
 * The implementation owns all NSStatusItem/NSMenu/custom-view state on the main
 * thread and exposes no mutable UI objects. It receives immutable owned model
 * snapshots from the daemon/event worker and emits small typed actions through
 * the injected callback. The callback handles Space focus, settings and
 * menu-tracking state on the daemon worker; it must not run shell commands to
 * mutate yabai state.
 *
 * This API is safe to include from C or Objective-C under -fno-objc-arc.
 */

enum menu_bar_action_type
{
    MENU_BAR_ACTION_NONE = 0,
    MENU_BAR_ACTION_FOCUS_SPACE,
    MENU_BAR_ACTION_SET_STYLE,
    MENU_BAR_ACTION_SET_SPACE_FILTER,
    MENU_BAR_ACTION_MENU_TRACKING_CHANGED
};

struct menu_bar_action_focus_space
{
    uint64_t space_id;
};

struct menu_bar_action_set_style
{
    enum menu_bar_style style;
};

struct menu_bar_action_set_space_filter
{
    enum menu_bar_space_filter filter;
};

struct menu_bar_action_menu_tracking
{
    bool is_tracking;              /* true on menu-open begin, false on close/end. */
    uint32_t focused_display_id;   /* Source snapshot focus at menu-open begin, or 0 if unresolved. */
};

struct menu_bar_action
{
    enum menu_bar_action_type type;

    /* Source snapshot generation. The worker must reject stale targets safely. */
    uint64_t generation;

    union {
        struct menu_bar_action_focus_space focus_space;
        struct menu_bar_action_set_style set_style;
        struct menu_bar_action_set_space_filter set_space_filter;
        struct menu_bar_action_menu_tracking menu_tracking;
    } data;
};

/*
 * Invoked on the main thread. The action contains no Objective-C references or
 * borrowed strings; copy the struct by value before returning if asynchronous
 * processing is needed. The callback must return promptly and must not wait for
 * synchronous UI work on the main thread.
 */
typedef void menu_bar_action_callback(const struct menu_bar_action *action, void *context);

struct menu_bar_callbacks
{
    menu_bar_action_callback *action;
    void *context;
};

/*
 * Main-thread lifecycle. Initializing does not create a visible status item until
 * an enabled snapshot is delivered. Shutdown releases the status item, menus,
 * pending snapshots and menu-open state. Calls must be balanced by the owner.
 */
bool menu_bar_init(const struct menu_bar_callbacks *callbacks);
void menu_bar_shutdown(void);

/*
 * Thread-safe latest-only delivery from the event worker.
 *
 * Ownership of snapshot transfers to the UI layer on every call, including when
 * the UI is disabled or shutting down. Passing NULL clears the current/pending
 * snapshot and removes visible UI. The caller must not inspect or destroy the
 * snapshot after publishing it.
 *
 * Delivery is bounded: at most the newest unpublished snapshot is retained, and
 * superseded snapshots are destroyed. The function must not synchronously wait
 * for the main thread. While a menu is open, the UI keeps the menu's immutable
 * action identities stable, stores only the newest replacement snapshot, and
 * reconciles after tracking ends.
 */
void menu_bar_publish_snapshot(struct menu_bar_snapshot *snapshot);

/* Thread-safe in-place redraw request for public accessibility display option
 * changes. This never requests a new model or WindowServer inventory snapshot. */
void menu_bar_accessibility_preferences_changed(void);

#ifdef __cplusplus
}
#endif

#endif
