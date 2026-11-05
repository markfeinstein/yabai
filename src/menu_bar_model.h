#ifndef MENU_BAR_MODEL_H
#define MENU_BAR_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared menu-bar presentation model contract.
 *
 * The native collector feeds this model with plain C values only: display IDs
 * and UUID strings, runtime space IDs, presentation labels, visibility flags,
 * app names and window classification flags. The model implementation must deep
 * copy every input string and array into malloc/free-compatible storage owned by
 * the returned snapshot. Borrowed daemon objects, CF/NS objects, ts_alloc memory
 * and manager/window/view pointers must never be stored in a snapshot.
 *
 * Snapshots returned by this API are immutable after creation. The caller owns
 * snapshots returned by create/clone/filter functions and releases them with
 * menu_bar_model_snapshot_destroy(). Destroying NULL is allowed. Functions that
 * allocate a snapshot return NULL on invalid input or allocation failure.
 */

enum menu_bar_enabled
{
    MENU_BAR_DISABLED = 0,
    MENU_BAR_ENABLED  = 1
};

enum menu_bar_interface
{
    /* Preserve the existing native NSMenu. This is the default. */
    MENU_BAR_INTERFACE_MENU = 0,
    /* Prefer the macOS 27 expanded panel, with native-menu fallback. */
    MENU_BAR_INTERFACE_PANEL = 1,
    /* Select the panel on macOS 27 and the native menu on older systems. */
    MENU_BAR_INTERFACE_AUTO = 2
};

enum menu_bar_style
{
    /* Show every space selected by the space filter. This is the default. */
    MENU_BAR_STYLE_EXPANDED  = 0,

    /* Show only the focused display's visible space in the status item. */
    MENU_BAR_STYLE_COMPACT   = 1,

    /* Use expanded layout within the automatic width budget, otherwise compact. */
    MENU_BAR_STYLE_AUTOMATIC = 2
};

enum menu_bar_space_filter
{
    /* Show occupied, unknown, or currently visible spaces. This is the default. */
    MENU_BAR_SPACE_FILTER_OCCUPIED = 0,

    /* Show every user-navigation space supplied by the native collector. */
    MENU_BAR_SPACE_FILTER_ALL      = 1
};

/* A zero-initialized settings struct is the product default: off/menu/expanded/occupied. */
struct menu_bar_settings
{
    enum menu_bar_enabled enabled;
    enum menu_bar_interface interface;
    enum menu_bar_style style;
    enum menu_bar_space_filter space_filter;
};

enum menu_bar_space_membership
{
    MENU_BAR_SPACE_MEMBERSHIP_EMPTY    = 0,
    MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED = 1,
    MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN  = 2
};

struct menu_bar_app_summary
{
    const char *name;       /* Owned by the containing snapshot; never NULL. */
    uint32_t window_count;  /* Count of user-facing windows summarized by name. */
};

struct menu_bar_space
{
    uint64_t space_id;                  /* Stable runtime macOS/yabai space identity. */
    uint32_t mission_control_index;     /* Presentation index; not an action identity. */
    const char *label;                  /* Optional yabai/user label, owned by snapshot. */
    bool is_native_fullscreen;
    bool is_visible;                    /* Visible on this display; separate from focused display. */
    enum menu_bar_space_membership membership;
    const struct menu_bar_app_summary *apps;
    size_t app_count;
};

struct menu_bar_display
{
    uint32_t display_id;            /* CGDirectDisplayID-style identity for current attachment. */
    const char *uuid;               /* Stable display UUID string when known; owned by snapshot. */
    uint32_t arrangement_index;     /* Yabai/display-manager order for presentation. */
    const char *label;              /* Optional yabai/user label, owned by snapshot. */
    const char *name;               /* Optional human display name, owned by snapshot. */
    bool has_focus;                 /* Keyboard-focused display; do not infer from visible spaces. */
    const struct menu_bar_space *spaces;
    size_t space_count;
};

struct menu_bar_snapshot
{
    uint64_t generation;            /* Monotonic collector generation used to reject stale actions. */
    struct menu_bar_settings settings;

    bool has_focused_display;
    uint32_t focused_display_id;
    const char *focused_display_uuid; /* Owned by snapshot; NULL when focus is unresolved. */

    const struct menu_bar_display *displays;
    size_t display_count;

    /* Optional status/menu feedback for the most recent failed action. */
    uint64_t action_failure_generation;
    const char *action_failure_text; /* Owned by snapshot; NULL when there is no active failure. */
};

struct menu_bar_app_summary_input
{
    const char *name;       /* Borrowed for the duration of create(). NULL entries are ignored. */
    uint32_t window_count;
};

struct menu_bar_space_input
{
    uint64_t space_id;
    uint32_t mission_control_index;
    const char *label;      /* Borrowed; optional. */
    bool is_native_fullscreen;
    bool is_visible;
    enum menu_bar_space_membership membership;
    const struct menu_bar_app_summary_input *apps; /* NULL only when app_count is zero. */
    size_t app_count;
};

struct menu_bar_display_input
{
    uint32_t display_id;
    const char *uuid;       /* Borrowed; optional but preferred for stable display identity. */
    uint32_t arrangement_index;
    const char *label;      /* Borrowed; optional. */
    const char *name;       /* Borrowed; optional. */
    const struct menu_bar_space_input *spaces; /* NULL only when space_count is zero. */
    size_t space_count;
};

/* Creates a snapshot preserving display, space and app input order for presentation. */
struct menu_bar_snapshot *menu_bar_model_snapshot_create(uint64_t generation,
                                                         struct menu_bar_settings settings,
                                                         const struct menu_bar_display_input *displays,
                                                         size_t display_count,
                                                         bool has_focused_display,
                                                         uint32_t focused_display_id,
                                                         const char *focused_display_uuid,
                                                         uint64_t action_failure_generation,
                                                         const char *action_failure_text);
struct menu_bar_snapshot *menu_bar_model_snapshot_clone(const struct menu_bar_snapshot *snapshot);

/* Returns an owned clone with unchanged inventory and newly resolved focus metadata. */
struct menu_bar_snapshot *menu_bar_model_snapshot_refocused_clone(const struct menu_bar_snapshot *snapshot,
                                                                   uint64_t generation,
                                                                   bool has_focused_display,
                                                                   uint32_t focused_display_id,
                                                                   const char *focused_display_uuid);

/* Returns an owned clone with settings.space_filter set to filter and non-matching spaces removed. */
struct menu_bar_snapshot *menu_bar_model_snapshot_filtered_clone(const struct menu_bar_snapshot *snapshot,
                                                                 enum menu_bar_space_filter filter);
void menu_bar_model_snapshot_destroy(struct menu_bar_snapshot *snapshot);

/*
 * Filtering keeps visible spaces for orientation. UNKNOWN is never treated as
 * empty, so occupied filtering retains unknown spaces until membership resolves.
 */
bool menu_bar_model_space_matches_filter(const struct menu_bar_space *space,
                                         enum menu_bar_space_filter filter);

enum menu_bar_model_compare_flag
{
    /* Structural comparison always includes settings, focus identity and display/space/app content. */
    MENU_BAR_MODEL_COMPARE_GENERATION     = 1u << 0,
    MENU_BAR_MODEL_COMPARE_ACTION_FAILURE = 1u << 1
};

#define MENU_BAR_MODEL_COMPARE_EXACT (MENU_BAR_MODEL_COMPARE_GENERATION | MENU_BAR_MODEL_COMPARE_ACTION_FAILURE)

bool menu_bar_model_snapshot_equal(const struct menu_bar_snapshot *a,
                                   const struct menu_bar_snapshot *b,
                                   uint32_t compare_flags);

enum menu_bar_window_flag
{
    /* Zero flags describe a normal user-facing application window. */
    MENU_BAR_WINDOW_FLOATING           = 1u << 0,
    MENU_BAR_WINDOW_UNMANAGED          = 1u << 1,
    MENU_BAR_WINDOW_HIDDEN             = 1u << 2,
    MENU_BAR_WINDOW_MINIMIZED          = 1u << 3,
    MENU_BAR_WINDOW_STICKY             = 1u << 4,
    MENU_BAR_WINDOW_SYSTEM             = 1u << 5,
    MENU_BAR_WINDOW_TRANSIENT          = 1u << 6,
    MENU_BAR_WINDOW_NATIVE_FULLSCREEN  = 1u << 7,
    MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN = 1u << 8
};

struct menu_bar_window_input
{
    uint64_t space_id;      /* Zero is permitted only with MEMBERSHIP_UNKNOWN. */
    const char *app_name;   /* Borrowed; optional for membership-only classification. */
    uint32_t flags;         /* Combination of enum menu_bar_window_flag values. */
};

enum menu_bar_window_occupancy
{
    MENU_BAR_WINDOW_OCCUPANCY_EXCLUDED = 0,
    MENU_BAR_WINDOW_OCCUPANCY_COUNTED  = 1,
    MENU_BAR_WINDOW_OCCUPANCY_UNKNOWN  = 2
};

/*
 * Occupancy classifier contract:
 * - normal, floating and unmanaged application windows count;
 * - hidden and minimized windows still count;
 * - native-fullscreen windows count in their native space;
 * - sticky/all-spaces, system and transient windows are excluded;
 * - unresolved membership returns UNKNOWN when the window is not otherwise excluded;
 * - UNKNOWN is not empty.
 */
enum menu_bar_window_occupancy menu_bar_model_classify_window(const struct menu_bar_window_input *window);
enum menu_bar_space_membership menu_bar_model_classify_space_membership(const struct menu_bar_window_input *windows,
                                                                        size_t window_count);

#ifdef __cplusplus
}
#endif

#endif
