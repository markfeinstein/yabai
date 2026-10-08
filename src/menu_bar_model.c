#include "menu_bar_model.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool menu_bar_model_valid_enabled(enum menu_bar_enabled enabled)
{
    return enabled == MENU_BAR_DISABLED || enabled == MENU_BAR_ENABLED;
}

static bool menu_bar_model_valid_style(enum menu_bar_style style)
{
    return style == MENU_BAR_STYLE_COMPACT || style == MENU_BAR_STYLE_EXPANDED;
}

static bool menu_bar_model_valid_filter(enum menu_bar_space_filter filter)
{
    return filter == MENU_BAR_SPACE_FILTER_OCCUPIED || filter == MENU_BAR_SPACE_FILTER_ALL;
}

static bool menu_bar_model_valid_membership(enum menu_bar_space_membership membership)
{
    return membership == MENU_BAR_SPACE_MEMBERSHIP_EMPTY ||
           membership == MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED ||
           membership == MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN;
}

static bool menu_bar_model_valid_settings(struct menu_bar_settings settings)
{
    return menu_bar_model_valid_enabled(settings.enabled) &&
           menu_bar_model_valid_style(settings.style) &&
           menu_bar_model_valid_filter(settings.space_filter);
}

static void *menu_bar_model_calloc(size_t count, size_t size)
{
    if (count && size > SIZE_MAX / count) return NULL;
    return calloc(count, size);
}

static char *menu_bar_model_copy_string(const char *string)
{
    if (!string) return NULL;

    size_t length = strlen(string);
    if (length == SIZE_MAX) return NULL;

    char *copy = malloc(length + 1);
    if (!copy) return NULL;

    memcpy(copy, string, length + 1);
    return copy;
}

static bool menu_bar_model_strings_equal(const char *a, const char *b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static bool menu_bar_model_uuid_known(const char *uuid)
{
    return uuid && uuid[0] != '\0';
}

static bool menu_bar_model_focus_matches_display(const struct menu_bar_display *display,
                                                 uint32_t focused_display_id,
                                                 const char *focused_display_uuid)
{
    bool id_match = focused_display_id != 0 && display->display_id == focused_display_id;
    bool focus_uuid_known = menu_bar_model_uuid_known(focused_display_uuid);
    bool display_uuid_known = menu_bar_model_uuid_known(display->uuid);
    bool uuid_match = focus_uuid_known && display_uuid_known && strcmp(display->uuid, focused_display_uuid) == 0;

    if (focus_uuid_known && display_uuid_known) {
        return uuid_match && (focused_display_id == 0 || id_match);
    }

    if (focus_uuid_known && !display_uuid_known) {
        return id_match;
    }

    return id_match;
}

static bool menu_bar_model_validate_display_inputs(const struct menu_bar_display_input *displays,
                                                   size_t display_count)
{
    if (display_count && !displays) return false;

    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        const struct menu_bar_display_input *display = &displays[display_index];
        if (display->space_count && !display->spaces) return false;

        for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
            const struct menu_bar_space_input *space = &display->spaces[space_index];
            if (!menu_bar_model_valid_membership(space->membership)) return false;
            if (space->app_count && !space->apps) return false;
        }
    }

    return true;
}

static bool menu_bar_model_validate_snapshot(const struct menu_bar_snapshot *snapshot)
{
    if (!snapshot) return false;
    if (!menu_bar_model_valid_settings(snapshot->settings)) return false;
    if (snapshot->display_count && !snapshot->displays) return false;

    for (size_t display_index = 0; display_index < snapshot->display_count; ++display_index) {
        const struct menu_bar_display *display = &snapshot->displays[display_index];
        if (display->space_count && !display->spaces) return false;

        for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
            const struct menu_bar_space *space = &display->spaces[space_index];
            if (!menu_bar_model_valid_membership(space->membership)) return false;
            if (space->app_count && !space->apps) return false;

            for (size_t app_index = 0; app_index < space->app_count; ++app_index) {
                if (!space->apps[app_index].name) return false;
            }
        }
    }

    return true;
}

static bool menu_bar_model_resolve_focus(struct menu_bar_snapshot *snapshot,
                                         bool has_focused_display,
                                         uint32_t focused_display_id,
                                         const char *focused_display_uuid)
{
    if (!has_focused_display) return true;

    struct menu_bar_display *displays = (struct menu_bar_display *) snapshot->displays;
    struct menu_bar_display *focused_display = NULL;

    for (size_t display_index = 0; display_index < snapshot->display_count; ++display_index) {
        if (menu_bar_model_focus_matches_display(&displays[display_index], focused_display_id, focused_display_uuid)) {
            focused_display = &displays[display_index];
            break;
        }
    }

    if (!focused_display) return true;

    focused_display->has_focus = true;
    snapshot->has_focused_display = true;
    snapshot->focused_display_id = focused_display->display_id;

    const char *resolved_uuid = focused_display->uuid ? focused_display->uuid : focused_display_uuid;
    if (resolved_uuid) {
        char *focused_display_uuid_copy = menu_bar_model_copy_string(resolved_uuid);
        if (!focused_display_uuid_copy) return false;
        snapshot->focused_display_uuid = focused_display_uuid_copy;
    }

    return true;
}

static bool menu_bar_model_copy_string_field(const char **destination, const char *source)
{
    *destination = NULL;
    if (!source) return true;

    char *copy = menu_bar_model_copy_string(source);
    if (!copy) return false;

    *destination = copy;
    return true;
}

struct menu_bar_snapshot *menu_bar_model_snapshot_create(uint64_t generation,
                                                         struct menu_bar_settings settings,
                                                         const struct menu_bar_display_input *displays,
                                                         size_t display_count,
                                                         bool has_focused_display,
                                                         uint32_t focused_display_id,
                                                         const char *focused_display_uuid,
                                                         uint64_t action_failure_generation,
                                                         const char *action_failure_text)
{
    if (!menu_bar_model_valid_settings(settings)) return NULL;
    if (!menu_bar_model_validate_display_inputs(displays, display_count)) return NULL;

    struct menu_bar_snapshot *snapshot = calloc(1, sizeof(struct menu_bar_snapshot));
    if (!snapshot) return NULL;

    snapshot->generation = generation;
    snapshot->settings = settings;
    snapshot->action_failure_generation = action_failure_generation;

    if (action_failure_text) {
        char *failure_text_copy = menu_bar_model_copy_string(action_failure_text);
        if (!failure_text_copy) {
            menu_bar_model_snapshot_destroy(snapshot);
            return NULL;
        }
        snapshot->action_failure_text = failure_text_copy;
    }

    if (display_count) {
        struct menu_bar_display *display_copies = menu_bar_model_calloc(display_count, sizeof(struct menu_bar_display));
        if (!display_copies) {
            menu_bar_model_snapshot_destroy(snapshot);
            return NULL;
        }

        snapshot->displays = display_copies;
        snapshot->display_count = display_count;
    }

    struct menu_bar_display *display_copies = (struct menu_bar_display *) snapshot->displays;
    for (size_t display_index = 0; display_index < display_count; ++display_index) {
        const struct menu_bar_display_input *display_input = &displays[display_index];
        struct menu_bar_display *display = &display_copies[display_index];

        display->display_id = display_input->display_id;
        display->arrangement_index = display_input->arrangement_index;

        if (!menu_bar_model_copy_string_field(&display->uuid, display_input->uuid) ||
            !menu_bar_model_copy_string_field(&display->label, display_input->label) ||
            !menu_bar_model_copy_string_field(&display->name, display_input->name)) {
            menu_bar_model_snapshot_destroy(snapshot);
            return NULL;
        }

        if (display_input->space_count) {
            struct menu_bar_space *space_copies = menu_bar_model_calloc(display_input->space_count, sizeof(struct menu_bar_space));
            if (!space_copies) {
                menu_bar_model_snapshot_destroy(snapshot);
                return NULL;
            }

            display->spaces = space_copies;
            display->space_count = display_input->space_count;
        }

        struct menu_bar_space *space_copies = (struct menu_bar_space *) display->spaces;
        for (size_t space_index = 0; space_index < display_input->space_count; ++space_index) {
            const struct menu_bar_space_input *space_input = &display_input->spaces[space_index];
            struct menu_bar_space *space = &space_copies[space_index];

            space->space_id = space_input->space_id;
            space->mission_control_index = space_input->mission_control_index;
            space->is_native_fullscreen = space_input->is_native_fullscreen;
            space->is_visible = space_input->is_visible;
            space->membership = space_input->membership;

            if (!menu_bar_model_copy_string_field(&space->label, space_input->label)) {
                menu_bar_model_snapshot_destroy(snapshot);
                return NULL;
            }

            size_t app_count = 0;
            for (size_t app_index = 0; app_index < space_input->app_count; ++app_index) {
                if (space_input->apps[app_index].name) ++app_count;
            }

            if (app_count) {
                struct menu_bar_app_summary *app_copies = menu_bar_model_calloc(app_count, sizeof(struct menu_bar_app_summary));
                if (!app_copies) {
                    menu_bar_model_snapshot_destroy(snapshot);
                    return NULL;
                }

                space->apps = app_copies;
                space->app_count = app_count;
            }

            struct menu_bar_app_summary *app_copies = (struct menu_bar_app_summary *) space->apps;
            size_t app_output_index = 0;
            for (size_t app_index = 0; app_index < space_input->app_count; ++app_index) {
                const struct menu_bar_app_summary_input *app_input = &space_input->apps[app_index];
                if (!app_input->name) continue;

                struct menu_bar_app_summary *app = &app_copies[app_output_index++];
                app->window_count = app_input->window_count;

                if (!menu_bar_model_copy_string_field(&app->name, app_input->name)) {
                    menu_bar_model_snapshot_destroy(snapshot);
                    return NULL;
                }
            }
        }
    }

    if (!menu_bar_model_resolve_focus(snapshot, has_focused_display, focused_display_id, focused_display_uuid)) {
        menu_bar_model_snapshot_destroy(snapshot);
        return NULL;
    }

    return snapshot;
}

static bool menu_bar_model_copy_space(struct menu_bar_space *destination,
                                      const struct menu_bar_space *source)
{
    destination->space_id = source->space_id;
    destination->mission_control_index = source->mission_control_index;
    destination->is_native_fullscreen = source->is_native_fullscreen;
    destination->is_visible = source->is_visible;
    destination->membership = source->membership;

    if (!menu_bar_model_copy_string_field(&destination->label, source->label)) return false;

    if (source->app_count) {
        struct menu_bar_app_summary *apps = menu_bar_model_calloc(source->app_count, sizeof(struct menu_bar_app_summary));
        if (!apps) return false;
        destination->apps = apps;
        destination->app_count = source->app_count;
    }

    struct menu_bar_app_summary *destination_apps = (struct menu_bar_app_summary *) destination->apps;
    for (size_t app_index = 0; app_index < source->app_count; ++app_index) {
        destination_apps[app_index].window_count = source->apps[app_index].window_count;
        if (!menu_bar_model_copy_string_field(&destination_apps[app_index].name, source->apps[app_index].name)) return false;
    }

    return true;
}

static struct menu_bar_snapshot *menu_bar_model_snapshot_copy(const struct menu_bar_snapshot *snapshot,
                                                              bool apply_filter,
                                                              enum menu_bar_space_filter filter)
{
    if (!menu_bar_model_validate_snapshot(snapshot)) return NULL;
    if (apply_filter && !menu_bar_model_valid_filter(filter)) return NULL;

    struct menu_bar_snapshot *copy = calloc(1, sizeof(struct menu_bar_snapshot));
    if (!copy) return NULL;

    copy->generation = snapshot->generation;
    copy->settings = snapshot->settings;
    if (apply_filter) copy->settings.space_filter = filter;
    copy->has_focused_display = snapshot->has_focused_display;
    copy->focused_display_id = snapshot->focused_display_id;
    copy->action_failure_generation = snapshot->action_failure_generation;

    if (!menu_bar_model_copy_string_field(&copy->focused_display_uuid, snapshot->focused_display_uuid) ||
        !menu_bar_model_copy_string_field(&copy->action_failure_text, snapshot->action_failure_text)) {
        menu_bar_model_snapshot_destroy(copy);
        return NULL;
    }

    if (snapshot->display_count) {
        struct menu_bar_display *displays = menu_bar_model_calloc(snapshot->display_count, sizeof(struct menu_bar_display));
        if (!displays) {
            menu_bar_model_snapshot_destroy(copy);
            return NULL;
        }

        copy->displays = displays;
        copy->display_count = snapshot->display_count;
    }

    struct menu_bar_display *destination_displays = (struct menu_bar_display *) copy->displays;
    for (size_t display_index = 0; display_index < snapshot->display_count; ++display_index) {
        const struct menu_bar_display *source_display = &snapshot->displays[display_index];
        struct menu_bar_display *destination_display = &destination_displays[display_index];

        destination_display->display_id = source_display->display_id;
        destination_display->arrangement_index = source_display->arrangement_index;
        destination_display->has_focus = source_display->has_focus;

        if (!menu_bar_model_copy_string_field(&destination_display->uuid, source_display->uuid) ||
            !menu_bar_model_copy_string_field(&destination_display->label, source_display->label) ||
            !menu_bar_model_copy_string_field(&destination_display->name, source_display->name)) {
            menu_bar_model_snapshot_destroy(copy);
            return NULL;
        }

        size_t kept_space_count = 0;
        for (size_t space_index = 0; space_index < source_display->space_count; ++space_index) {
            if (!apply_filter || menu_bar_model_space_matches_filter(&source_display->spaces[space_index], filter)) {
                ++kept_space_count;
            }
        }

        if (kept_space_count) {
            struct menu_bar_space *spaces = menu_bar_model_calloc(kept_space_count, sizeof(struct menu_bar_space));
            if (!spaces) {
                menu_bar_model_snapshot_destroy(copy);
                return NULL;
            }

            destination_display->spaces = spaces;
            destination_display->space_count = kept_space_count;
        }

        struct menu_bar_space *destination_spaces = (struct menu_bar_space *) destination_display->spaces;
        size_t destination_space_index = 0;
        for (size_t source_space_index = 0; source_space_index < source_display->space_count; ++source_space_index) {
            const struct menu_bar_space *source_space = &source_display->spaces[source_space_index];
            if (apply_filter && !menu_bar_model_space_matches_filter(source_space, filter)) continue;

            if (!menu_bar_model_copy_space(&destination_spaces[destination_space_index++], source_space)) {
                menu_bar_model_snapshot_destroy(copy);
                return NULL;
            }
        }
    }

    return copy;
}

struct menu_bar_snapshot *menu_bar_model_snapshot_clone(const struct menu_bar_snapshot *snapshot)
{
    return menu_bar_model_snapshot_copy(snapshot, false, MENU_BAR_SPACE_FILTER_OCCUPIED);
}

struct menu_bar_snapshot *menu_bar_model_snapshot_refocused_clone(const struct menu_bar_snapshot *snapshot,
                                                                   uint64_t generation,
                                                                   bool has_focused_display,
                                                                   uint32_t focused_display_id,
                                                                   const char *focused_display_uuid)
{
    struct menu_bar_snapshot *copy = menu_bar_model_snapshot_clone(snapshot);
    if (!copy) return NULL;

    struct menu_bar_display *displays = (struct menu_bar_display *) copy->displays;
    for (size_t display_index = 0; display_index < copy->display_count; ++display_index) {
        displays[display_index].has_focus = false;
    }
    free((void *) copy->focused_display_uuid);
    copy->generation = generation;
    copy->has_focused_display = false;
    copy->focused_display_id = 0;
    copy->focused_display_uuid = NULL;

    if (!menu_bar_model_resolve_focus(copy, has_focused_display,
                                      focused_display_id, focused_display_uuid)) {
        menu_bar_model_snapshot_destroy(copy);
        return NULL;
    }
    return copy;
}

struct menu_bar_snapshot *menu_bar_model_snapshot_filtered_clone(const struct menu_bar_snapshot *snapshot,
                                                                 enum menu_bar_space_filter filter)
{
    return menu_bar_model_snapshot_copy(snapshot, true, filter);
}

void menu_bar_model_snapshot_destroy(struct menu_bar_snapshot *snapshot)
{
    if (!snapshot) return;

    free((void *) snapshot->focused_display_uuid);
    free((void *) snapshot->action_failure_text);

    struct menu_bar_display *displays = (struct menu_bar_display *) snapshot->displays;
    for (size_t display_index = 0; display_index < snapshot->display_count; ++display_index) {
        struct menu_bar_display *display = &displays[display_index];

        free((void *) display->uuid);
        free((void *) display->label);
        free((void *) display->name);

        struct menu_bar_space *spaces = (struct menu_bar_space *) display->spaces;
        for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
            struct menu_bar_space *space = &spaces[space_index];

            free((void *) space->label);

            struct menu_bar_app_summary *apps = (struct menu_bar_app_summary *) space->apps;
            for (size_t app_index = 0; app_index < space->app_count; ++app_index) {
                free((void *) apps[app_index].name);
            }
            free(apps);
        }
        free(spaces);
    }
    free(displays);
    free(snapshot);
}

bool menu_bar_model_space_matches_filter(const struct menu_bar_space *space,
                                         enum menu_bar_space_filter filter)
{
    if (!space) return false;

    switch (filter) {
    case MENU_BAR_SPACE_FILTER_ALL:
        return true;
    case MENU_BAR_SPACE_FILTER_OCCUPIED:
        return space->is_visible ||
               space->membership == MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED ||
               space->membership == MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN;
    }

    return false;
}

bool menu_bar_model_snapshot_equal(const struct menu_bar_snapshot *a,
                                   const struct menu_bar_snapshot *b,
                                   uint32_t compare_flags)
{
    if (compare_flags & ~MENU_BAR_MODEL_COMPARE_EXACT) return false;
    if (a == b) return true;
    if (!a || !b) return false;

    if (compare_flags & MENU_BAR_MODEL_COMPARE_GENERATION) {
        if (a->generation != b->generation) return false;
    }

    if (a->settings.enabled != b->settings.enabled ||
        a->settings.style != b->settings.style ||
        a->settings.space_filter != b->settings.space_filter ||
        a->has_focused_display != b->has_focused_display ||
        a->focused_display_id != b->focused_display_id ||
        !menu_bar_model_strings_equal(a->focused_display_uuid, b->focused_display_uuid) ||
        a->display_count != b->display_count) {
        return false;
    }

    if (compare_flags & MENU_BAR_MODEL_COMPARE_ACTION_FAILURE) {
        if (a->action_failure_generation != b->action_failure_generation ||
            !menu_bar_model_strings_equal(a->action_failure_text, b->action_failure_text)) {
            return false;
        }
    }

    for (size_t display_index = 0; display_index < a->display_count; ++display_index) {
        const struct menu_bar_display *display_a = &a->displays[display_index];
        const struct menu_bar_display *display_b = &b->displays[display_index];

        if (display_a->display_id != display_b->display_id ||
            display_a->arrangement_index != display_b->arrangement_index ||
            display_a->has_focus != display_b->has_focus ||
            display_a->space_count != display_b->space_count ||
            !menu_bar_model_strings_equal(display_a->uuid, display_b->uuid) ||
            !menu_bar_model_strings_equal(display_a->label, display_b->label) ||
            !menu_bar_model_strings_equal(display_a->name, display_b->name)) {
            return false;
        }

        for (size_t space_index = 0; space_index < display_a->space_count; ++space_index) {
            const struct menu_bar_space *space_a = &display_a->spaces[space_index];
            const struct menu_bar_space *space_b = &display_b->spaces[space_index];

            if (space_a->space_id != space_b->space_id ||
                space_a->mission_control_index != space_b->mission_control_index ||
                space_a->is_native_fullscreen != space_b->is_native_fullscreen ||
                space_a->is_visible != space_b->is_visible ||
                space_a->membership != space_b->membership ||
                space_a->app_count != space_b->app_count ||
                !menu_bar_model_strings_equal(space_a->label, space_b->label)) {
                return false;
            }

            for (size_t app_index = 0; app_index < space_a->app_count; ++app_index) {
                const struct menu_bar_app_summary *app_a = &space_a->apps[app_index];
                const struct menu_bar_app_summary *app_b = &space_b->apps[app_index];

                if (app_a->window_count != app_b->window_count ||
                    !menu_bar_model_strings_equal(app_a->name, app_b->name)) {
                    return false;
                }
            }
        }
    }

    return true;
}

enum menu_bar_window_occupancy menu_bar_model_classify_window(const struct menu_bar_window_input *window)
{
    if (!window) return MENU_BAR_WINDOW_OCCUPANCY_UNKNOWN;

    const uint32_t known_flags = MENU_BAR_WINDOW_FLOATING |
                                 MENU_BAR_WINDOW_UNMANAGED |
                                 MENU_BAR_WINDOW_HIDDEN |
                                 MENU_BAR_WINDOW_MINIMIZED |
                                 MENU_BAR_WINDOW_STICKY |
                                 MENU_BAR_WINDOW_SYSTEM |
                                 MENU_BAR_WINDOW_TRANSIENT |
                                 MENU_BAR_WINDOW_NATIVE_FULLSCREEN |
                                 MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN;

    if (window->flags & (MENU_BAR_WINDOW_STICKY | MENU_BAR_WINDOW_SYSTEM | MENU_BAR_WINDOW_TRANSIENT)) {
        return MENU_BAR_WINDOW_OCCUPANCY_EXCLUDED;
    }

    if ((window->flags & ~known_flags) ||
        (window->flags & MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN) ||
        window->space_id == 0) {
        return MENU_BAR_WINDOW_OCCUPANCY_UNKNOWN;
    }

    return MENU_BAR_WINDOW_OCCUPANCY_COUNTED;
}

enum menu_bar_space_membership menu_bar_model_classify_space_membership(const struct menu_bar_window_input *windows,
                                                                        size_t window_count)
{
    if (window_count && !windows) return MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN;

    bool has_unknown = false;
    for (size_t window_index = 0; window_index < window_count; ++window_index) {
        switch (menu_bar_model_classify_window(&windows[window_index])) {
        case MENU_BAR_WINDOW_OCCUPANCY_COUNTED:
            return MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED;
        case MENU_BAR_WINDOW_OCCUPANCY_UNKNOWN:
            has_unknown = true;
            break;
        case MENU_BAR_WINDOW_OCCUPANCY_EXCLUDED:
            break;
        }
    }

    return has_unknown ? MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN : MENU_BAR_SPACE_MEMBERSHIP_EMPTY;
}
