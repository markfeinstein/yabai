#include "menu_bar_manager.h"
#include "event_loop.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern struct event_loop g_event_loop;
extern struct display_manager g_display_manager;
extern struct space_manager g_space_manager;
extern struct window_manager g_window_manager;
extern int g_connection;

struct menu_bar_space_builder
{
    struct menu_bar_space_input *input;
    struct menu_bar_window_input *windows;
    size_t window_count;
    size_t window_capacity;
    size_t app_capacity;
    uint32_t *raw_window_ids;
    size_t raw_window_count;
    bool raw_query_succeeded;
};

struct menu_bar_display_builder
{
    struct menu_bar_display_input input;
    struct menu_bar_space_input *space_inputs;
    struct menu_bar_space_builder *space_builders;
};

struct menu_bar_manager_state
{
    bool initialized;
    bool refresh_pending;
    uint32_t dirty_reasons;
    struct menu_bar_settings settings;
    uint64_t generation;
    uint64_t enabled_generation_floor;
    struct menu_bar_snapshot *last_snapshot;
    uint64_t action_failure_generation;
    char *action_failure_text;
    uint32_t menu_tracking_depth;
    uint32_t pre_menu_focused_display_id;
};

struct menu_bar_manager_native_seams
{
    CFArrayRef (*managed_display_spaces)(void);
    int (*active_display_count)(void);
    uint32_t (*arrangement_display_id)(int arrangement);
    CFStringRef (*display_uuid)(uint32_t did);
    uint64_t *(*display_space_list)(uint32_t did, int *count);
    uint64_t (*display_space_id)(uint32_t did);
    int (*space_type)(int cid, uint64_t sid);
    int (*mission_control_index)(uint64_t sid);
    uint32_t *(*space_window_list)(uint64_t sid, int *count, bool include_minimized, bool *success,
                                   struct window_server_snapshot **snapshot_out);
    uint64_t *(*window_space_list)(uint32_t wid, int *count);
};

static struct menu_bar_manager_state g_menu_bar_manager;
static struct menu_bar_manager_native_seams g_menu_bar_manager_native;

static bool menu_bar_manager_valid_enabled(enum menu_bar_enabled enabled)
{
    return enabled == MENU_BAR_DISABLED || enabled == MENU_BAR_ENABLED;
}

static bool menu_bar_manager_valid_interface(enum menu_bar_interface interface)
{
    return interface == MENU_BAR_INTERFACE_MENU || interface == MENU_BAR_INTERFACE_PANEL ||
           interface == MENU_BAR_INTERFACE_AUTO;
}

static bool menu_bar_manager_valid_thumbnails(enum menu_bar_thumbnails thumbnails)
{
    return thumbnails == MENU_BAR_THUMBNAILS_OFF || thumbnails == MENU_BAR_THUMBNAILS_ON;
}

static bool menu_bar_manager_valid_style(enum menu_bar_style style)
{
    return style == MENU_BAR_STYLE_COMPACT || style == MENU_BAR_STYLE_EXPANDED;
}

static bool menu_bar_manager_valid_space_filter(enum menu_bar_space_filter filter)
{
    return filter == MENU_BAR_SPACE_FILTER_OCCUPIED || filter == MENU_BAR_SPACE_FILTER_ALL;
}

static bool menu_bar_manager_text_equals(const char *text, size_t length, const char *literal)
{
    return text && literal && strlen(literal) == length && memcmp(text, literal, length) == 0;
}

static char *menu_bar_manager_copy_string(const char *string)
{
    if (!string) return NULL;

    size_t length = strlen(string);
    char *copy = malloc(length + 1);
    if (!copy) return NULL;

    memcpy(copy, string, length + 1);
    return copy;
}

static char *menu_bar_manager_copy_cfstring(CFStringRef string)
{
    if (!string) return NULL;

    CFIndex length = CFStringGetLength(string);
    CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    if (max_size <= 0 || (uint64_t) max_size > SIZE_MAX) return NULL;

    char *buffer = malloc((size_t) max_size);
    if (!buffer) return NULL;

    if (!CFStringGetCString(string, buffer, max_size, kCFStringEncodingUTF8)) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

static void menu_bar_manager_clear_action_failure(void)
{
    free(g_menu_bar_manager.action_failure_text);
    g_menu_bar_manager.action_failure_text = NULL;
    g_menu_bar_manager.action_failure_generation = 0;
}

static void menu_bar_manager_set_action_failure(uint64_t generation, const char *message)
{
    char *copy = menu_bar_manager_copy_string(message ? message : "action failed");
    if (!copy) return;

    menu_bar_manager_clear_action_failure();
    g_menu_bar_manager.action_failure_generation = generation;
    g_menu_bar_manager.action_failure_text = copy;
}

static int menu_bar_manager_native_active_display_count(void)
{
    return g_menu_bar_manager_native.active_display_count
        ? g_menu_bar_manager_native.active_display_count()
        : display_manager_active_display_count();
}

static uint32_t menu_bar_manager_native_arrangement_display_id(int arrangement)
{
    return g_menu_bar_manager_native.arrangement_display_id
        ? g_menu_bar_manager_native.arrangement_display_id(arrangement)
        : display_manager_arrangement_display_id(arrangement);
}

static CFStringRef menu_bar_manager_native_display_uuid(uint32_t did)
{
    return g_menu_bar_manager_native.display_uuid
        ? g_menu_bar_manager_native.display_uuid(did)
        : display_uuid(did);
}

static CFArrayRef menu_bar_manager_native_managed_display_spaces(void)
{
    return g_menu_bar_manager_native.managed_display_spaces
        ? g_menu_bar_manager_native.managed_display_spaces()
        : SLSCopyManagedDisplaySpaces(g_connection);
}

static uint64_t *menu_bar_manager_native_display_space_list(CFArrayRef topology,
                                                             CFStringRef uuid,
                                                             uint32_t did,
                                                             int *count)
{
    if (g_menu_bar_manager_native.display_space_list) {
        return g_menu_bar_manager_native.display_space_list(did, count);
    }

    if (count) *count = 0;
    if (!count || !uuid || !topology) return NULL;

    CFArrayRef spaces = managed_space_spaces_for_display(topology, uuid);
    int valid_count = 0;
    if (!managed_space_count_valid_checked(spaces, &valid_count) || valid_count <= 0) return NULL;

    uint64_t *result = ts_alloc_list(uint64_t, valid_count);
    if (result) *count = managed_space_copy_valid(spaces, result);
    return result;
}

static uint64_t menu_bar_manager_native_display_space_id(uint32_t did)
{
    return g_menu_bar_manager_native.display_space_id
        ? g_menu_bar_manager_native.display_space_id(did)
        : display_space_id(did);
}

static int menu_bar_manager_native_space_type(uint64_t sid)
{
    return g_menu_bar_manager_native.space_type
        ? g_menu_bar_manager_native.space_type(g_connection, sid)
        : SLSSpaceGetType(g_connection, sid);
}

static int menu_bar_manager_native_mission_control_index(CFArrayRef topology, uint64_t sid)
{
    return g_menu_bar_manager_native.mission_control_index
        ? g_menu_bar_manager_native.mission_control_index(sid)
        : managed_space_index(topology, sid);
}

static uint32_t *menu_bar_manager_native_space_window_list(
    uint64_t sid, int *count, bool include_minimized, bool *success,
    struct window_server_snapshot **snapshot_out)
{
    if (snapshot_out) *snapshot_out = NULL;
    if (g_menu_bar_manager_native.space_window_list) {
        return g_menu_bar_manager_native.space_window_list(
            sid, count, include_minimized, success, snapshot_out);
    }
    return space_window_list_for_connection_with_snapshot(
        &sid, 1, 0, count, include_minimized, success, snapshot_out);
}

static uint64_t *menu_bar_manager_native_window_space_list(uint32_t wid, int *count)
{
    return g_menu_bar_manager_native.window_space_list
        ? g_menu_bar_manager_native.window_space_list(wid, count)
        : window_space_list(wid, count);
}

static bool menu_bar_manager_resize(void **items, size_t item_size, size_t *capacity, size_t needed)
{
    if (needed <= *capacity) return true;

    size_t new_capacity = *capacity ? *capacity * 2 : 4;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2) return false;
        new_capacity *= 2;
    }

    if (item_size && new_capacity > SIZE_MAX / item_size) return false;
    void *new_items = realloc(*items, new_capacity * item_size);
    if (!new_items) return false;

    *items = new_items;
    *capacity = new_capacity;
    return true;
}

static bool menu_bar_manager_space_add_window(struct menu_bar_space_builder *space,
                                              struct menu_bar_window_input window)
{
    if (!menu_bar_manager_resize((void **) &space->windows,
                                 sizeof(struct menu_bar_window_input),
                                 &space->window_capacity,
                                 space->window_count + 1)) {
        return false;
    }

    space->windows[space->window_count++] = window;
    return true;
}

static bool menu_bar_manager_space_add_app(struct menu_bar_space_builder *builder, const char *name)
{
    if (!builder || !builder->input) return false;
    if (!name || !name[0]) name = "Unknown";

    struct menu_bar_space_input *space = builder->input;
    struct menu_bar_app_summary_input *apps = (struct menu_bar_app_summary_input *) space->apps;
    for (size_t i = 0; i < space->app_count; ++i) {
        if (strcmp(apps[i].name, name) == 0) {
            ++apps[i].window_count;
            return true;
        }
    }

    if (space->app_count == SIZE_MAX ||
        !menu_bar_manager_resize((void **) &apps,
                                 sizeof(struct menu_bar_app_summary_input),
                                 &builder->app_capacity,
                                 space->app_count + 1)) return false;

    apps[space->app_count].name = name;
    apps[space->app_count].window_count = 1;
    space->apps = apps;
    ++space->app_count;
    return true;
}

static int menu_bar_manager_app_compare(const void *a, const void *b)
{
    const struct menu_bar_app_summary_input *app_a = a;
    const struct menu_bar_app_summary_input *app_b = b;
    return strcmp(app_a->name, app_b->name);
}

static int menu_bar_manager_space_compare(const void *a, const void *b)
{
    const struct menu_bar_space_input *space_a = a;
    const struct menu_bar_space_input *space_b = b;

    if (space_a->mission_control_index < space_b->mission_control_index) return -1;
    if (space_a->mission_control_index > space_b->mission_control_index) return 1;
    if (space_a->space_id < space_b->space_id) return -1;
    if (space_a->space_id > space_b->space_id) return 1;
    return 0;
}

static void menu_bar_manager_destroy_builders(struct menu_bar_display_builder *displays, size_t display_count)
{
    if (!displays) return;

    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        struct menu_bar_display_builder *display = &displays[display_index];
        free((void *) display->input.uuid);

        for (size_t space_index = 0; space_index < display->input.space_count; ++space_index) {
            struct menu_bar_space_builder *space = &display->space_builders[space_index];
            struct menu_bar_space_input *input = &display->space_inputs[space_index];
            free(space->windows);
            free(space->raw_window_ids);
            free((void *) input->apps);
        }

        free(display->space_builders);
        free(display->space_inputs);
    }

    free(displays);
}

static struct menu_bar_space_builder *menu_bar_manager_find_space_builder(struct menu_bar_display_builder *displays,
                                                                          size_t display_count,
                                                                          uint64_t sid,
                                                                          struct menu_bar_space_input **input_out)
{
    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        struct menu_bar_display_builder *display = &displays[display_index];
        for (size_t space_index = 0; space_index < display->input.space_count; ++space_index) {
            struct menu_bar_space_input *space_input = &display->space_inputs[space_index];
            if (space_input->space_id == sid) {
                if (input_out) *input_out = space_input;
                return &display->space_builders[space_index];
            }
        }
    }

    return NULL;
}

static uint32_t menu_bar_manager_window_flags(struct window *window)
{
    uint32_t flags = 0;

    if (window_check_flag(window, WINDOW_FLOAT)) flags |= MENU_BAR_WINDOW_FLOATING;
    if (!window_manager_find_managed_window(&g_window_manager, window)) flags |= MENU_BAR_WINDOW_UNMANAGED;
    if (window->application && window->application->is_hidden) flags |= MENU_BAR_WINDOW_HIDDEN;
    if (window_check_flag(window, WINDOW_MINIMIZE)) flags |= MENU_BAR_WINDOW_MINIMIZED;
    if (window_check_flag(window, WINDOW_STICKY)) flags |= MENU_BAR_WINDOW_STICKY;
    if (window_check_flag(window, WINDOW_FULLSCREEN)) flags |= MENU_BAR_WINDOW_NATIVE_FULLSCREEN;
    if (!window_is_real(window)) flags |= MENU_BAR_WINDOW_TRANSIENT;

    return flags;
}

static uint32_t menu_bar_manager_window_flags_for_space(uint32_t window_flags, bool is_native_fullscreen)
{
    return is_native_fullscreen ? window_flags | MENU_BAR_WINDOW_NATIVE_FULLSCREEN : window_flags;
}

static bool menu_bar_manager_add_window_to_space(struct menu_bar_display_builder *displays,
                                                 size_t display_count,
                                                 struct window *window,
                                                 uint64_t sid,
                                                 uint32_t window_flags)
{
    struct menu_bar_space_input *space_input = NULL;
    struct menu_bar_space_builder *space = menu_bar_manager_find_space_builder(displays, display_count, sid, &space_input);
    if (!space || !space_input) return true;

    struct menu_bar_window_input window_input = {
        .space_id = sid,
        .app_name = window->application ? window->application->name : NULL,
        .flags = menu_bar_manager_window_flags_for_space(window_flags, space_input->is_native_fullscreen)
    };

    enum menu_bar_window_occupancy occupancy = menu_bar_model_classify_window(&window_input);
    if (!menu_bar_manager_space_add_window(space, window_input)) return false;

    if (occupancy == MENU_BAR_WINDOW_OCCUPANCY_COUNTED) {
        if (!menu_bar_manager_space_add_app(space, window_input.app_name)) return false;
    }

    return true;
}

static bool menu_bar_manager_mark_space_unknown(struct menu_bar_space_builder *space, uint64_t sid)
{
    struct menu_bar_window_input unknown = {
        .space_id = sid,
        .app_name = NULL,
        .flags = MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN
    };

    return menu_bar_manager_space_add_window(space, unknown);
}

static bool menu_bar_manager_collect_spaces_for_display(struct menu_bar_display_builder *display,
                                                        CFArrayRef topology,
                                                        uint32_t did,
                                                        uint32_t arrangement_index)
{
    CFStringRef uuid = menu_bar_manager_native_display_uuid(did);
    int native_space_count = 0;
    uint64_t *native_spaces = menu_bar_manager_native_display_space_list(topology, uuid, did, &native_space_count);
    uint64_t visible_sid = menu_bar_manager_native_display_space_id(did);

    display->input.display_id = did;
    display->input.arrangement_index = arrangement_index;

    if (uuid) {
        display->input.uuid = menu_bar_manager_copy_cfstring(uuid);
        CFRelease(uuid);
    }

    struct display_label *display_label = display_manager_get_label_for_display(&g_display_manager, did);
    display->input.label = display_label ? display_label->label : NULL;
    display->input.name = NULL;

    if (!native_spaces || native_space_count <= 0) return true;

    size_t space_capacity = (size_t) native_space_count;
    display->space_inputs = calloc(space_capacity, sizeof(struct menu_bar_space_input));
    display->space_builders = calloc(space_capacity, sizeof(struct menu_bar_space_builder));
    if (!display->space_inputs || !display->space_builders) {
        free(display->space_inputs);
        free(display->space_builders);
        display->space_inputs = NULL;
        display->space_builders = NULL;
        return false;
    }

    display->input.spaces = display->space_inputs;

    size_t output_index = 0;
    for (int i = 0; i < native_space_count; ++i) {
        uint64_t sid = native_spaces[i];
        int type = menu_bar_manager_native_space_type(sid);
        if (type != 0 && type != 4) continue;

        struct menu_bar_space_input *space = &display->space_inputs[output_index];
        struct menu_bar_space_builder *builder = &display->space_builders[output_index];
        builder->input = space;

        struct space_label *space_label = space_manager_get_label_for_space(&g_space_manager, sid);
        space->space_id = sid;
        space->mission_control_index = (uint32_t) menu_bar_manager_native_mission_control_index(topology, sid);
        space->label = space_label ? space_label->label : NULL;
        space->is_native_fullscreen = type == 4;
        space->is_visible = sid == visible_sid;
        space->membership = MENU_BAR_SPACE_MEMBERSHIP_EMPTY;

        ++output_index;
    }

    display->input.space_count = output_index;
    qsort(display->space_inputs, display->input.space_count, sizeof(struct menu_bar_space_input), menu_bar_manager_space_compare);

    for (size_t i = 0; i < display->input.space_count; ++i) {
        display->space_builders[i].input = &display->space_inputs[i];
    }

    return true;
}

static bool menu_bar_manager_collect_raw_untracked(struct menu_bar_display_builder *displays,
                                                   size_t display_count)
{
    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        struct menu_bar_display_builder *display = &displays[display_index];
        for (size_t space_index = 0; space_index < display->input.space_count; ++space_index) {
            struct menu_bar_space_input *space_input = &display->space_inputs[space_index];
            struct menu_bar_space_builder *space = &display->space_builders[space_index];

            int raw_window_count = 0;
            bool query_succeeded = false;
            struct window_server_snapshot *window_server_snapshot = NULL;
            uint32_t *raw_window_list = menu_bar_manager_native_space_window_list(
                space_input->space_id, &raw_window_count, true, &query_succeeded,
                &window_server_snapshot);
            space->raw_query_succeeded = query_succeeded && raw_window_count >= 0 &&
                                         (raw_window_count == 0 || raw_window_list);
            if (space->raw_query_succeeded && raw_window_count > 0) {
                if (!window_server_snapshot) {
                    space->raw_query_succeeded = false;
                } else {
                    for (int i = 0; i < raw_window_count; ++i) {
                        if (!window_server_snapshot_find(window_server_snapshot, raw_window_list[i])) {
                            space->raw_query_succeeded = false;
                            break;
                        }
                    }
                }
            }
            if (!space->raw_query_succeeded) {
                window_server_snapshot_destroy(window_server_snapshot);
                if (!menu_bar_manager_mark_space_unknown(space, space_input->space_id)) return false;
                continue;
            }
            if (raw_window_count == 0) {
                window_server_snapshot_destroy(window_server_snapshot);
                continue;
            }

            space->raw_window_ids = malloc((size_t) raw_window_count * sizeof(uint32_t));
            if (!space->raw_window_ids) {
                window_server_snapshot_destroy(window_server_snapshot);
                return false;
            }
            memcpy(space->raw_window_ids, raw_window_list,
                   (size_t) raw_window_count * sizeof(uint32_t));
            space->raw_window_count = (size_t) raw_window_count;
            window_server_snapshot_destroy(window_server_snapshot);

            for (size_t i = 0; i < space->raw_window_count; ++i) {
                if (!window_manager_find_window(&g_window_manager, space->raw_window_ids[i])) {
                    if (!menu_bar_manager_mark_space_unknown(space, space_input->space_id)) return false;
                    break;
                }
            }
        }
    }

    return true;
}

static bool menu_bar_manager_mark_visible_spaces_unknown(struct menu_bar_display_builder *displays,
                                                         size_t display_count)
{
    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        struct menu_bar_display_builder *display = &displays[display_index];
        for (size_t space_index = 0; space_index < display->input.space_count; ++space_index) {
            struct menu_bar_space_input *space_input = &display->space_inputs[space_index];
            if (!space_input->is_visible) continue;

            if (!menu_bar_manager_mark_space_unknown(&display->space_builders[space_index], space_input->space_id)) {
                return false;
            }
        }
    }

    return true;
}

static bool menu_bar_manager_add_tracked_window_from_raw_membership(struct menu_bar_display_builder *displays,
                                                                    size_t display_count,
                                                                    struct window *window,
                                                                    uint32_t window_flags)
{
    bool found_raw_membership = false;

    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        struct menu_bar_display_builder *display = &displays[display_index];
        for (size_t space_index = 0; space_index < display->input.space_count; ++space_index) {
            uint64_t sid = display->space_inputs[space_index].space_id;
            struct menu_bar_space_builder *space = &display->space_builders[space_index];
            if (!space->raw_query_succeeded) {
                if (!menu_bar_manager_mark_space_unknown(space, sid)) return false;
                continue;
            }

            for (size_t i = 0; i < space->raw_window_count; ++i) {
                if (space->raw_window_ids[i] != window->id) continue;
                found_raw_membership = true;
                if (!menu_bar_manager_add_window_to_space(displays, display_count, window, sid, window_flags)) return false;
                break;
            }
        }
    }

    if (found_raw_membership) return true;

    /* The tracked window remains unresolved despite one coherent raw snapshot.
     * Keep uncertainty bounded to visible Spaces until another event refreshes it. */
    return menu_bar_manager_mark_visible_spaces_unknown(displays, display_count);
}

static bool menu_bar_manager_reconcile_raw_membership(struct menu_bar_display_builder *displays,
                                                       size_t display_count, struct window *window,
                                                       uint64_t *space_list, int space_count)
{
    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        struct menu_bar_display_builder *display = &displays[display_index];
        for (size_t space_index = 0; space_index < display->input.space_count; ++space_index) {
            struct menu_bar_space_builder *space = &display->space_builders[space_index];
            if (!space->raw_query_succeeded) continue;

            uint64_t sid = display->space_inputs[space_index].space_id;
            bool raw_contains = false;
            bool window_contains = false;
            for (size_t i = 0; i < space->raw_window_count; ++i) {
                if (space->raw_window_ids[i] == window->id) { raw_contains = true; break; }
            }
            for (int i = 0; i < space_count; ++i) {
                if (space_list[i] == sid) { window_contains = true; break; }
            }
            if (raw_contains != window_contains &&
                !menu_bar_manager_mark_space_unknown(space, sid)) return false;
        }
    }
    return true;
}

static bool menu_bar_manager_collect_tracked_windows(struct menu_bar_display_builder *displays,
                                                     size_t display_count)
{
    table_for (struct window *window, g_window_manager.window, {
        if (!window || !window->id) continue;

        uint32_t window_flags = menu_bar_manager_window_flags(window);
        if (window_flags & (MENU_BAR_WINDOW_STICKY | MENU_BAR_WINDOW_TRANSIENT)) continue;

        int space_count = 0;
        uint64_t *space_list = menu_bar_manager_native_window_space_list(window->id, &space_count);

        // A sticky window is defined by membership in multiple Spaces. Reuse this
        // list instead of issuing the same SLSCopySpacesForWindows query twice.
        if (space_list && space_count > 1) continue;
        if (!space_list || space_count <= 0) {
            if (!menu_bar_manager_add_tracked_window_from_raw_membership(displays, display_count, window, window_flags)) {
                return false;
            }
            continue;
        }

        if (!menu_bar_manager_reconcile_raw_membership(displays, display_count, window,
                                                        space_list, space_count)) return false;
        for (int i = 0; i < space_count; ++i) {
            if (!menu_bar_manager_add_window_to_space(displays, display_count, window, space_list[i], window_flags)) {
                return false;
            }
        }
    })

    return true;
}

static void menu_bar_manager_finalize_membership(struct menu_bar_display_builder *displays,
                                                 size_t display_count)
{
    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        struct menu_bar_display_builder *display = &displays[display_index];
        for (size_t space_index = 0; space_index < display->input.space_count; ++space_index) {
            struct menu_bar_space_builder *builder = &display->space_builders[space_index];
            struct menu_bar_space_input *space = &display->space_inputs[space_index];

            space->membership = menu_bar_model_classify_space_membership(builder->windows, builder->window_count);
            if (space->app_count > 1) {
                qsort((void *) space->apps, space->app_count, sizeof(struct menu_bar_app_summary_input), menu_bar_manager_app_compare);
            }
        }
    }
}

static uint32_t menu_bar_manager_focused_display_id(void)
{
    if (g_menu_bar_manager.menu_tracking_depth) {
        return g_menu_bar_manager.pre_menu_focused_display_id;
    }

    if (g_display_manager.current_display_id) return g_display_manager.current_display_id;
    return display_manager_active_display_id();
}

static uint64_t menu_bar_manager_next_generation(void)
{
    ++g_menu_bar_manager.generation;
    if (g_menu_bar_manager.generation == 0) ++g_menu_bar_manager.generation;
    return g_menu_bar_manager.generation;
}

static struct menu_bar_snapshot *menu_bar_manager_create_focus_snapshot(void)
{
    if (!g_menu_bar_manager.last_snapshot) return NULL;

    uint32_t focused_did = menu_bar_manager_focused_display_id();
    char *focused_uuid = NULL;
    if (focused_did) {
        CFStringRef uuid = menu_bar_manager_native_display_uuid(focused_did);
        if (uuid) {
            focused_uuid = menu_bar_manager_copy_cfstring(uuid);
            CFRelease(uuid);
        }
    }

    struct menu_bar_snapshot *snapshot = menu_bar_model_snapshot_refocused_clone(
        g_menu_bar_manager.last_snapshot,
        menu_bar_manager_next_generation(),
        focused_did != 0,
        focused_did,
        focused_uuid);
    free(focused_uuid);

    if (snapshot && focused_did && !snapshot->has_focused_display) {
        menu_bar_model_snapshot_destroy(snapshot);
        return NULL;
    }
    return snapshot;
}

static struct menu_bar_snapshot *menu_bar_manager_create_snapshot(void)
{
    int active_display_count = menu_bar_manager_native_active_display_count();
    size_t display_capacity = active_display_count > 0 ? (size_t) active_display_count : 0;
    struct menu_bar_display_builder *displays = display_capacity ? calloc(display_capacity, sizeof(struct menu_bar_display_builder)) : NULL;
    if (display_capacity && !displays) return NULL;

    CFArrayRef topology = display_capacity ? menu_bar_manager_native_managed_display_spaces() : NULL;
    size_t display_count = 0;
    for (int arrangement = 1; arrangement <= active_display_count; ++arrangement) {
        uint32_t did = menu_bar_manager_native_arrangement_display_id(arrangement);
        if (!did) continue;

        if (!menu_bar_manager_collect_spaces_for_display(&displays[display_count], topology,
                                                         did, (uint32_t) arrangement - 1)) {
            if (topology) CFRelease(topology);
            menu_bar_manager_destroy_builders(displays, display_count + 1);
            return NULL;
        }

        ++display_count;
    }
    if (topology) CFRelease(topology);

    if (!menu_bar_manager_collect_raw_untracked(displays, display_count) ||
        !menu_bar_manager_collect_tracked_windows(displays, display_count)) {
        menu_bar_manager_destroy_builders(displays, display_count);
        return NULL;
    }

    menu_bar_manager_finalize_membership(displays, display_count);

    uint32_t focused_did = menu_bar_manager_focused_display_id();
    char *focused_uuid = NULL;
    if (focused_did) {
        CFStringRef uuid = menu_bar_manager_native_display_uuid(focused_did);
        if (uuid) {
            focused_uuid = menu_bar_manager_copy_cfstring(uuid);
            CFRelease(uuid);
        }
    }

    struct menu_bar_display_input *display_inputs = NULL;
    if (display_count) {
        display_inputs = calloc(display_count, sizeof(struct menu_bar_display_input));
        if (!display_inputs) {
            free(focused_uuid);
            menu_bar_manager_destroy_builders(displays, display_count);
            return NULL;
        }

        for (size_t i = 0; i < display_count; ++i) {
            display_inputs[i] = displays[i].input;
        }
    }

    struct menu_bar_snapshot *snapshot = menu_bar_model_snapshot_create(menu_bar_manager_next_generation(),
                                                                        g_menu_bar_manager.settings,
                                                                        display_inputs,
                                                                        display_count,
                                                                        focused_did != 0,
                                                                        focused_did,
                                                                        focused_uuid,
                                                                        g_menu_bar_manager.action_failure_generation,
                                                                        g_menu_bar_manager.action_failure_text);
    free(display_inputs);
    free(focused_uuid);
    menu_bar_manager_destroy_builders(displays, display_count);
    return snapshot;
}

static void menu_bar_manager_publish_if_changed(struct menu_bar_snapshot *snapshot)
{
    if (!snapshot) return;

    if (g_menu_bar_manager.last_snapshot &&
        menu_bar_model_snapshot_equal(g_menu_bar_manager.last_snapshot,
                                      snapshot,
                                      MENU_BAR_MODEL_COMPARE_ACTION_FAILURE)) {
        menu_bar_model_snapshot_destroy(snapshot);
        return;
    }

    struct menu_bar_snapshot *snapshot_for_compare = menu_bar_model_snapshot_clone(snapshot);
    if (snapshot_for_compare) {
        menu_bar_model_snapshot_destroy(g_menu_bar_manager.last_snapshot);
        g_menu_bar_manager.last_snapshot = snapshot_for_compare;
    }

    menu_bar_publish_snapshot(snapshot);
}

static void menu_bar_manager_ui_action_callback(const struct menu_bar_action *action, void *context)
{
    (void) context;
    if (!action) return;

    struct menu_bar_action *copy = malloc(sizeof(struct menu_bar_action));
    if (!copy) return;

    *copy = *action;
    event_loop_post(&g_event_loop, MENU_BAR_ACTION, copy, 0);
}

static bool menu_bar_manager_action_in_enabled_lifecycle(const struct menu_bar_action *action)
{
    if (!action) return false;
    if (!g_menu_bar_manager.initialized) return false;
    if (g_menu_bar_manager.settings.enabled != MENU_BAR_ENABLED) return false;
    if (!g_menu_bar_manager.last_snapshot) return false;
    if (!action->generation) return false;
    if (action->generation < g_menu_bar_manager.enabled_generation_floor) return false;
    if (action->generation > g_menu_bar_manager.generation) return false;

    return true;
}

static struct menu_bar_snapshot *menu_bar_manager_create_disabled_snapshot(void)
{
    return menu_bar_model_snapshot_create(menu_bar_manager_next_generation(),
                                          g_menu_bar_manager.settings,
                                          NULL,
                                          0,
                                          false,
                                          0,
                                          NULL,
                                          0,
                                          NULL);
}

static void menu_bar_manager_publish_disabled_snapshot(void)
{
    struct menu_bar_snapshot *snapshot = menu_bar_manager_create_disabled_snapshot();
    struct menu_bar_snapshot *snapshot_for_compare = snapshot ? menu_bar_model_snapshot_clone(snapshot) : NULL;

    menu_bar_model_snapshot_destroy(g_menu_bar_manager.last_snapshot);
    g_menu_bar_manager.last_snapshot = snapshot_for_compare;

    if (snapshot) {
        menu_bar_publish_snapshot(snapshot);
    } else {
        menu_bar_publish_snapshot(NULL);
    }
}

static bool menu_bar_manager_startup_staging_is_open;

bool menu_bar_manager_init(void)
{
    struct menu_bar_callbacks callbacks = {
        .action = menu_bar_manager_ui_action_callback,
        .context = NULL
    };

    if (!menu_bar_init(&callbacks)) return false;

    g_menu_bar_manager.initialized = true;
    g_menu_bar_manager.refresh_pending = false;
    g_menu_bar_manager.dirty_reasons = 0;
    g_menu_bar_manager.settings = (struct menu_bar_settings) { 0 };
    g_menu_bar_manager.generation = 0;
    g_menu_bar_manager.enabled_generation_floor = 1;
    g_menu_bar_manager.menu_tracking_depth = 0;
    g_menu_bar_manager.pre_menu_focused_display_id = 0;
    menu_bar_manager_clear_action_failure();
    menu_bar_manager_startup_staging_is_open = false;

    return true;
}

void menu_bar_manager_shutdown(void)
{
    menu_bar_manager_startup_staging_is_open = false;
    g_menu_bar_manager.initialized = false;
    g_menu_bar_manager.refresh_pending = false;
    g_menu_bar_manager.dirty_reasons = 0;
    while (g_menu_bar_manager.menu_tracking_depth > 0) {
        --g_menu_bar_manager.menu_tracking_depth;
        event_loop_post(&g_event_loop, MENU_CLOSED, NULL, 0);
    }
    g_menu_bar_manager.pre_menu_focused_display_id = 0;

    menu_bar_model_snapshot_destroy(g_menu_bar_manager.last_snapshot);
    g_menu_bar_manager.last_snapshot = NULL;
    g_menu_bar_manager.enabled_generation_floor = g_menu_bar_manager.generation + 1;
    if (g_menu_bar_manager.enabled_generation_floor == 0) g_menu_bar_manager.enabled_generation_floor = 1;
    menu_bar_manager_clear_action_failure();
    menu_bar_publish_snapshot(NULL);
    menu_bar_shutdown();
}

struct menu_bar_settings menu_bar_manager_settings(void)
{
    return g_menu_bar_manager.settings;
}

enum menu_bar_enabled menu_bar_manager_enabled(void)
{
    return g_menu_bar_manager.settings.enabled;
}

enum menu_bar_interface menu_bar_manager_interface(void)
{
    return g_menu_bar_manager.settings.interface;
}

enum menu_bar_thumbnails menu_bar_manager_thumbnails(void)
{
    return g_menu_bar_manager.settings.thumbnails;
}

enum menu_bar_style menu_bar_manager_style(void)
{
    return g_menu_bar_manager.settings.style;
}

enum menu_bar_space_filter menu_bar_manager_space_filter(void)
{
    return g_menu_bar_manager.settings.space_filter;
}

const char *menu_bar_manager_enabled_string(enum menu_bar_enabled enabled)
{
    switch (enabled) {
    case MENU_BAR_ENABLED:  return "on";
    case MENU_BAR_DISABLED: return "off";
    }

    return "off";
}

const char *menu_bar_manager_interface_string(enum menu_bar_interface interface)
{
    switch (interface) {
    case MENU_BAR_INTERFACE_MENU:  return MENU_BAR_INTERFACE_MENU_STR;
    case MENU_BAR_INTERFACE_PANEL: return MENU_BAR_INTERFACE_PANEL_STR;
    case MENU_BAR_INTERFACE_AUTO:  return MENU_BAR_INTERFACE_AUTO_STR;
    }
    return MENU_BAR_INTERFACE_MENU_STR;
}

const char *menu_bar_manager_thumbnails_string(enum menu_bar_thumbnails thumbnails)
{
    return thumbnails == MENU_BAR_THUMBNAILS_ON ? MENU_BAR_THUMBNAILS_ON_STR : MENU_BAR_THUMBNAILS_OFF_STR;
}

const char *menu_bar_manager_style_string(enum menu_bar_style style)
{
    switch (style) {
    case MENU_BAR_STYLE_EXPANDED: return MENU_BAR_STYLE_EXPANDED_STR;
    case MENU_BAR_STYLE_COMPACT:  return MENU_BAR_STYLE_COMPACT_STR;
    }

    return MENU_BAR_STYLE_EXPANDED_STR;
}

const char *menu_bar_manager_space_filter_string(enum menu_bar_space_filter filter)
{
    switch (filter) {
    case MENU_BAR_SPACE_FILTER_ALL:      return MENU_BAR_SPACE_FILTER_ALL_STR;
    case MENU_BAR_SPACE_FILTER_OCCUPIED: return MENU_BAR_SPACE_FILTER_OCCUPIED_STR;
    }

    return MENU_BAR_SPACE_FILTER_OCCUPIED_STR;
}

bool menu_bar_manager_parse_enabled(const char *text, size_t length, enum menu_bar_enabled *enabled)
{
    if (menu_bar_manager_text_equals(text, length, "on")) {
        if (enabled) *enabled = MENU_BAR_ENABLED;
        return true;
    }

    if (menu_bar_manager_text_equals(text, length, "off")) {
        if (enabled) *enabled = MENU_BAR_DISABLED;
        return true;
    }

    return false;
}

bool menu_bar_manager_parse_interface(const char *text, size_t length,
                                      enum menu_bar_interface *interface)
{
    if (menu_bar_manager_text_equals(text, length, MENU_BAR_INTERFACE_MENU_STR)) {
        if (interface) *interface = MENU_BAR_INTERFACE_MENU;
        return true;
    }
    if (menu_bar_manager_text_equals(text, length, MENU_BAR_INTERFACE_PANEL_STR)) {
        if (interface) *interface = MENU_BAR_INTERFACE_PANEL;
        return true;
    }
    if (menu_bar_manager_text_equals(text, length, MENU_BAR_INTERFACE_AUTO_STR)) {
        if (interface) *interface = MENU_BAR_INTERFACE_AUTO;
        return true;
    }
    return false;
}

bool menu_bar_manager_parse_thumbnails(const char *text, size_t length,
                                       enum menu_bar_thumbnails *thumbnails)
{
    if (menu_bar_manager_text_equals(text, length, MENU_BAR_THUMBNAILS_OFF_STR)) {
        if (thumbnails) *thumbnails = MENU_BAR_THUMBNAILS_OFF;
        return true;
    }
    if (menu_bar_manager_text_equals(text, length, MENU_BAR_THUMBNAILS_ON_STR)) {
        if (thumbnails) *thumbnails = MENU_BAR_THUMBNAILS_ON;
        return true;
    }
    return false;
}

bool menu_bar_manager_parse_style(const char *text, size_t length, enum menu_bar_style *style)
{
    if (menu_bar_manager_text_equals(text, length, MENU_BAR_STYLE_COMPACT_STR)) {
        if (style) *style = MENU_BAR_STYLE_COMPACT;
        return true;
    }

    if (menu_bar_manager_text_equals(text, length, MENU_BAR_STYLE_EXPANDED_STR)) {
        if (style) *style = MENU_BAR_STYLE_EXPANDED;
        return true;
    }

    return false;
}

bool menu_bar_manager_parse_space_filter(const char *text, size_t length, enum menu_bar_space_filter *filter)
{
    if (menu_bar_manager_text_equals(text, length, MENU_BAR_SPACE_FILTER_OCCUPIED_STR)) {
        if (filter) *filter = MENU_BAR_SPACE_FILTER_OCCUPIED;
        return true;
    }

    if (menu_bar_manager_text_equals(text, length, MENU_BAR_SPACE_FILTER_ALL_STR)) {
        if (filter) *filter = MENU_BAR_SPACE_FILTER_ALL;
        return true;
    }

    return false;
}

void menu_bar_manager_startup_staging_open(void)
{
    menu_bar_manager_startup_staging_is_open = true;
}

bool menu_bar_manager_startup_staging_seal(void)
{
    if (!menu_bar_manager_startup_staging_is_open) return false;
    menu_bar_manager_startup_staging_is_open = false;

    if (!g_menu_bar_manager.initialized || g_menu_bar_manager.settings.enabled != MENU_BAR_ENABLED) return true;
    g_menu_bar_manager.dirty_reasons |= MENU_BAR_DIRTY_STARTUP;
    if (g_menu_bar_manager.refresh_pending) return true;

    g_menu_bar_manager.refresh_pending = true;
    if (!event_loop_try_post(&g_event_loop, MENU_BAR_REFRESH, NULL, 0)) {
        g_menu_bar_manager.refresh_pending = false;
        return false;
    }
    return true;
}

void menu_bar_manager_startup_staging_abort(void)
{
    menu_bar_manager_startup_staging_is_open = false;
    g_menu_bar_manager.refresh_pending = false;
    g_menu_bar_manager.dirty_reasons = 0;
}

bool menu_bar_manager_startup_stage_enabled(enum menu_bar_enabled enabled)
{
    if (!menu_bar_manager_startup_staging_is_open || !menu_bar_manager_valid_enabled(enabled)) return false;
    g_menu_bar_manager.settings.enabled = enabled;
    return true;
}

bool menu_bar_manager_startup_stage_interface(enum menu_bar_interface interface)
{
    if (!menu_bar_manager_startup_staging_is_open || !menu_bar_manager_valid_interface(interface)) return false;
    g_menu_bar_manager.settings.interface = interface;
    return true;
}

bool menu_bar_manager_startup_stage_thumbnails(enum menu_bar_thumbnails thumbnails)
{
    if (!menu_bar_manager_startup_staging_is_open || !menu_bar_manager_valid_thumbnails(thumbnails)) return false;
    g_menu_bar_manager.settings.thumbnails = thumbnails;
    return true;
}

bool menu_bar_manager_startup_stage_style(enum menu_bar_style style)
{
    if (!menu_bar_manager_startup_staging_is_open || !menu_bar_manager_valid_style(style)) return false;
    g_menu_bar_manager.settings.style = style;
    return true;
}

bool menu_bar_manager_startup_stage_space_filter(enum menu_bar_space_filter filter)
{
    if (!menu_bar_manager_startup_staging_is_open || !menu_bar_manager_valid_space_filter(filter)) return false;
    g_menu_bar_manager.settings.space_filter = filter;
    return true;
}

bool menu_bar_manager_startup_settings_can_commit(const struct menu_bar_settings *settings)
{
    return settings && menu_bar_manager_startup_staging_is_open &&
           menu_bar_manager_valid_enabled(settings->enabled) &&
           menu_bar_manager_valid_interface(settings->interface) &&
           menu_bar_manager_valid_thumbnails(settings->thumbnails) &&
           menu_bar_manager_valid_style(settings->style) &&
           menu_bar_manager_valid_space_filter(settings->space_filter);
}

void menu_bar_manager_startup_settings_commit(const struct menu_bar_settings *settings)
{
    assert(menu_bar_manager_startup_settings_can_commit(settings));
    g_menu_bar_manager.settings = *settings;
}

void menu_bar_manager_settings_commit_no_post(const struct menu_bar_settings *settings)
{
    assert(settings && menu_bar_manager_valid_enabled(settings->enabled) &&
           menu_bar_manager_valid_interface(settings->interface) &&
           menu_bar_manager_valid_thumbnails(settings->thumbnails) &&
           menu_bar_manager_valid_style(settings->style) &&
           menu_bar_manager_valid_space_filter(settings->space_filter));
    g_menu_bar_manager.settings = *settings;
}

void menu_bar_manager_settings_postcommit(const struct menu_bar_settings *previous)
{
    if (!previous) return;
    if (previous->enabled != g_menu_bar_manager.settings.enabled) {
        menu_bar_manager_clear_action_failure();
        if (g_menu_bar_manager.settings.enabled == MENU_BAR_ENABLED) {
            g_menu_bar_manager.enabled_generation_floor = g_menu_bar_manager.generation + 1;
            if (g_menu_bar_manager.enabled_generation_floor == 0) g_menu_bar_manager.enabled_generation_floor = 1;
        } else {
            g_menu_bar_manager.refresh_pending = false;
            g_menu_bar_manager.dirty_reasons = 0;
            while (g_menu_bar_manager.menu_tracking_depth > 0) {
                --g_menu_bar_manager.menu_tracking_depth;
                event_loop_post(&g_event_loop, MENU_CLOSED, NULL, 0);
            }
            g_menu_bar_manager.pre_menu_focused_display_id = 0;
            menu_bar_manager_publish_disabled_snapshot();
            return;
        }
    }
    if (g_menu_bar_manager.settings.enabled == MENU_BAR_ENABLED &&
        (previous->enabled != g_menu_bar_manager.settings.enabled ||
         previous->interface != g_menu_bar_manager.settings.interface ||
         previous->thumbnails != g_menu_bar_manager.settings.thumbnails ||
         previous->style != g_menu_bar_manager.settings.style ||
         previous->space_filter != g_menu_bar_manager.settings.space_filter)) {
        menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_CONFIG | MENU_BAR_DIRTY_STARTUP);
    }
}

bool menu_bar_manager_set_enabled(enum menu_bar_enabled enabled)
{
    if (!menu_bar_manager_valid_enabled(enabled)) return false;
    if (g_menu_bar_manager.settings.enabled == enabled) return true;

    g_menu_bar_manager.settings.enabled = enabled;
    menu_bar_manager_clear_action_failure();

    if (enabled == MENU_BAR_ENABLED) {
        g_menu_bar_manager.enabled_generation_floor = g_menu_bar_manager.generation + 1;
        if (g_menu_bar_manager.enabled_generation_floor == 0) g_menu_bar_manager.enabled_generation_floor = 1;
        menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_CONFIG | MENU_BAR_DIRTY_STARTUP);
    } else {
        g_menu_bar_manager.refresh_pending = false;
        g_menu_bar_manager.dirty_reasons = 0;
        while (g_menu_bar_manager.menu_tracking_depth > 0) {
            --g_menu_bar_manager.menu_tracking_depth;
            event_loop_post(&g_event_loop, MENU_CLOSED, NULL, 0);
        }
        g_menu_bar_manager.pre_menu_focused_display_id = 0;
        menu_bar_manager_publish_disabled_snapshot();
    }

    return true;
}

bool menu_bar_manager_set_interface(enum menu_bar_interface interface)
{
    if (!menu_bar_manager_valid_interface(interface)) return false;
    if (g_menu_bar_manager.settings.interface == interface) return true;
    g_menu_bar_manager.settings.interface = interface;
    menu_bar_manager_clear_action_failure();
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_CONFIG);
    return true;
}

bool menu_bar_manager_set_thumbnails(enum menu_bar_thumbnails thumbnails)
{
    if (!menu_bar_manager_valid_thumbnails(thumbnails)) return false;
    if (g_menu_bar_manager.settings.thumbnails == thumbnails) return true;
    g_menu_bar_manager.settings.thumbnails = thumbnails;
    menu_bar_manager_clear_action_failure();
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_CONFIG);
    return true;
}

bool menu_bar_manager_set_style(enum menu_bar_style style)
{
    if (!menu_bar_manager_valid_style(style)) return false;
    if (g_menu_bar_manager.settings.style == style) return true;

    g_menu_bar_manager.settings.style = style;
    menu_bar_manager_clear_action_failure();
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_CONFIG);
    return true;
}

bool menu_bar_manager_set_space_filter(enum menu_bar_space_filter filter)
{
    if (!menu_bar_manager_valid_space_filter(filter)) return false;
    if (g_menu_bar_manager.settings.space_filter == filter) return true;

    g_menu_bar_manager.settings.space_filter = filter;
    menu_bar_manager_clear_action_failure();
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_CONFIG);
    return true;
}

void menu_bar_manager_mark_dirty(uint32_t reason)
{
    if (!g_menu_bar_manager.initialized) return;

    if (menu_bar_manager_startup_staging_is_open) {
        g_menu_bar_manager.dirty_reasons |= reason;
        return;
    }
    if (g_menu_bar_manager.settings.enabled != MENU_BAR_ENABLED) return;

    g_menu_bar_manager.dirty_reasons |= reason;
    if (g_menu_bar_manager.refresh_pending) return;

    g_menu_bar_manager.refresh_pending = true;
    if (!event_loop_try_post(&g_event_loop, MENU_BAR_REFRESH, NULL, 0)) {
        g_menu_bar_manager.refresh_pending = false;
    }
}

void menu_bar_manager_refresh(void)
{
    g_menu_bar_manager.refresh_pending = false;
    if (menu_bar_manager_startup_staging_is_open) return;

    uint32_t dirty_reasons = g_menu_bar_manager.dirty_reasons;
    g_menu_bar_manager.dirty_reasons = 0;
    if (!g_menu_bar_manager.initialized || g_menu_bar_manager.settings.enabled != MENU_BAR_ENABLED) return;

    struct menu_bar_snapshot *snapshot = NULL;
    if (dirty_reasons == MENU_BAR_DIRTY_FOCUS) {
        snapshot = menu_bar_manager_create_focus_snapshot();
    }
    if (!snapshot) snapshot = menu_bar_manager_create_snapshot();
    menu_bar_manager_publish_if_changed(snapshot);
}

enum menu_bar_tracking_transition menu_bar_manager_handle_action(struct menu_bar_action *action)
{
    enum menu_bar_tracking_transition transition = MENU_BAR_TRACKING_TRANSITION_NONE;
    if (!action) return transition;

    switch (action->type) {
    case MENU_BAR_ACTION_SET_STYLE: {
        if (!menu_bar_manager_action_in_enabled_lifecycle(action)) break;

        if (!menu_bar_manager_set_style(action->data.set_style.style)) {
            menu_bar_manager_set_action_failure(action->generation, "invalid menu bar style");
            menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_ACTION | MENU_BAR_DIRTY_CONFIG);
        }
    } break;
    case MENU_BAR_ACTION_SET_SPACE_FILTER: {
        if (!menu_bar_manager_action_in_enabled_lifecycle(action)) break;

        if (!menu_bar_manager_set_space_filter(action->data.set_space_filter.filter)) {
            menu_bar_manager_set_action_failure(action->generation, "invalid menu bar space filter");
            menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_ACTION | MENU_BAR_DIRTY_CONFIG);
        }
    } break;
    case MENU_BAR_ACTION_MENU_TRACKING_CHANGED: {
        if (!menu_bar_manager_action_in_enabled_lifecycle(action)) break;

        if (action->data.menu_tracking.is_tracking) {
            if (g_menu_bar_manager.menu_tracking_depth == 0) {
                g_menu_bar_manager.pre_menu_focused_display_id = action->data.menu_tracking.focused_display_id;
            }
            if (g_menu_bar_manager.menu_tracking_depth != UINT32_MAX) ++g_menu_bar_manager.menu_tracking_depth;
            transition = MENU_BAR_TRACKING_TRANSITION_OPENED;
            menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_FOCUS);
        } else if (g_menu_bar_manager.menu_tracking_depth > 0) {
            --g_menu_bar_manager.menu_tracking_depth;
            if (g_menu_bar_manager.menu_tracking_depth == 0) g_menu_bar_manager.pre_menu_focused_display_id = 0;
            transition = MENU_BAR_TRACKING_TRANSITION_CLOSED;
            menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_FOCUS);
        }
    } break;
    case MENU_BAR_ACTION_NONE:
    default:
        break;
    }

    menu_bar_manager_release_action(action);
    return transition;
}

void menu_bar_manager_release_action(struct menu_bar_action *action)
{
    free(action);
}
