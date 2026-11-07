#include "declarative_reload_summary.h"
#include "declarative_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool declarative_reload_string_equal(const char *a, const char *b)
{
    return (!a && !b) || (a && b && strcmp(a, b) == 0);
}

static bool declarative_reload_value_equal(enum declarative_config_id id,
                                           const struct declarative_value *a,
                                           const struct declarative_value *b)
{
    if (a->present != b->present) return false;
    if (!a->present) return true;

    switch (declarative_config_descriptors[id].kind) {
    case DECLARATIVE_VALUE_BOOL: return a->boolean == b->boolean;
    case DECLARATIVE_VALUE_ENUM: return a->enumeration == b->enumeration;
    case DECLARATIVE_VALUE_INT: return a->integer == b->integer;
    case DECLARATIVE_VALUE_UINT: return a->u32 == b->u32;
    case DECLARATIVE_VALUE_FLOAT: return a->real == b->real;
    case DECLARATIVE_VALUE_STRUCTURED:
        return a->external_bar.mode == b->external_bar.mode &&
               a->external_bar.top_padding == b->external_bar.top_padding &&
               a->external_bar.bottom_padding == b->external_bar.bottom_padding;
    }
    return false;
}

static bool declarative_reload_matcher_equal(const struct declarative_matcher *a,
                                             const struct declarative_matcher *b)
{
    return a->exclude == b->exclude && declarative_reload_string_equal(a->pattern, b->pattern);
}

static bool declarative_reload_space_equal(const struct declarative_space_policy *a,
                                           const struct declarative_space_policy *b)
{
    if (a->by_index != b->by_index || a->index != b->index ||
        !declarative_reload_string_equal(a->uuid, b->uuid) ||
        a->scaffold_present != b->scaffold_present ||
        a->scaffold_count != b->scaffold_count) return false;

    for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) {
        if (!declarative_reload_value_equal((enum declarative_config_id) id,
                                            &a->settings[id], &b->settings[id])) return false;
    }
    for (size_t i = 0; i < a->scaffold_count; ++i) {
        if (!declarative_reload_matcher_equal(&a->scaffold[i], &b->scaffold[i])) return false;
    }
    return true;
}

static bool declarative_reload_space_list_equal(const struct declarative_space_policy *a,
                                                size_t a_count,
                                                const struct declarative_space_policy *b,
                                                size_t b_count)
{
    if (a_count != b_count) return false;
    for (size_t i = 0; i < a_count; ++i) {
        if (!declarative_reload_space_equal(&a[i], &b[i])) return false;
    }
    return true;
}

static bool declarative_reload_modes_equal(const struct declarative_config_model *a,
                                           const struct declarative_config_model *b)
{
    if (a->mode_count != b->mode_count) return false;
    for (size_t i = 0; i < a->mode_count; ++i) {
        const struct declarative_mode_model *x = &a->modes[i];
        const struct declarative_mode_model *y = &b->modes[i];
        if (!declarative_reload_string_equal(x->name, y->name) ||
            !declarative_reload_space_list_equal(x->spaces, x->space_count,
                                                 y->spaces, y->space_count)) return false;
    }
    return true;
}

static bool declarative_reload_rule_equal(const struct declarative_rule_model *a,
                                          const struct declarative_rule_model *b)
{
    if (!declarative_reload_string_equal(a->label, b->label) ||
        a->one_shot != b->one_shot ||
        a->display_present != b->display_present || a->display_index != b->display_index ||
        a->space_present != b->space_present || a->space_index != b->space_index ||
        a->follow_present != b->follow_present || a->follow != b->follow ||
        a->grid_present != b->grid_present ||
        a->opacity_present != b->opacity_present || a->opacity != b->opacity ||
        a->manage_present != b->manage_present || a->manage != b->manage ||
        a->sticky_present != b->sticky_present || a->sticky != b->sticky ||
        a->mff_present != b->mff_present || a->mff != b->mff ||
        a->layer_present != b->layer_present || a->layer != b->layer ||
        a->fullscreen_present != b->fullscreen_present || a->fullscreen != b->fullscreen ||
        !declarative_reload_string_equal(a->scratchpad, b->scratchpad)) return false;

    for (size_t i = 0; i < 5; ++i) {
        if (a->matcher_present[i] != b->matcher_present[i]) return false;
        if (a->matcher_present[i] &&
            !declarative_reload_matcher_equal(&a->matchers[i], &b->matchers[i])) return false;
    }
    if (a->grid_present && memcmp(a->grid, b->grid, sizeof(a->grid)) != 0) return false;
    return true;
}

static bool declarative_reload_rules_equal(const struct declarative_config_model *a,
                                           const struct declarative_config_model *b)
{
    if (a->rule_count != b->rule_count) return false;
    for (size_t i = 0; i < a->rule_count; ++i) {
        if (!declarative_reload_rule_equal(&a->rules[i], &b->rules[i])) return false;
    }
    return true;
}

static bool declarative_reload_keystroke_equal(struct hotkey_keystroke a,
                                               struct hotkey_keystroke b)
{
    return a.keycode == b.keycode && a.modifiers == b.modifiers;
}

static bool declarative_reload_action_equal(const struct hotkey_action *a,
                                            const struct hotkey_action *b)
{
    if (a->type != b->type) return false;
    switch (a->type) {
    case HOTKEY_ACTION_FOCUS_WINDOW:
        return a->data.focus_window.direction == b->data.focus_window.direction &&
               a->data.focus_window.robust == b->data.focus_window.robust;
    case HOTKEY_ACTION_WARP_WINDOW:
        return a->data.warp_window.direction == b->data.warp_window.direction;
    case HOTKEY_ACTION_RESIZE_WINDOW:
        return a->data.resize_window.handle == b->data.resize_window.handle &&
               a->data.resize_window.dx == b->data.resize_window.dx &&
               a->data.resize_window.dy == b->data.resize_window.dy;
    case HOTKEY_ACTION_CHANGE_WINDOW_RATIO:
        return a->data.change_window_ratio.delta == b->data.change_window_ratio.delta;
    case HOTKEY_ACTION_ADAPTIVE_RESIZE:
        return a->data.adaptive_resize.direction == b->data.adaptive_resize.direction;
    case HOTKEY_ACTION_ROTATE_SPACE:
        return a->data.rotate_space.degrees == b->data.rotate_space.degrees;
    case HOTKEY_ACTION_SET_SPACE_LAYOUT:
        return a->data.set_space_layout.layout == b->data.set_space_layout.layout;
    case HOTKEY_ACTION_FOCUS_SPACE:
    case HOTKEY_ACTION_MOVE_WINDOW_TO_SPACE:
        return a->data.space.index == b->data.space.index;
    case HOTKEY_ACTION_FOCUS_OCCUPIED_SPACE:
        return a->data.occupied_space.direction == b->data.occupied_space.direction;
    case HOTKEY_ACTION_RESTART_SERVICE:
        return strcmp(a->data.restart_service.label, b->data.restart_service.label) == 0;
    case HOTKEY_ACTION_YABAI_COMMAND:
        return a->data.yabai_command.length == b->data.yabai_command.length &&
               memcmp(a->data.yabai_command.bytes, b->data.yabai_command.bytes,
                      a->data.yabai_command.length) == 0;
    case HOTKEY_ACTION_SYNTHESIZE_KEY:
        return declarative_reload_keystroke_equal(a->data.synthesize_key,
                                                  b->data.synthesize_key);
    case HOTKEY_ACTION_TOGGLE_FLOATING:
    case HOTKEY_ACTION_TOGGLE_ZOOM_FULLSCREEN:
    case HOTKEY_ACTION_BALANCE_SPACE:
    case HOTKEY_ACTION_CLOSE_WINDOW:
    case HOTKEY_ACTION_CLOSE_OTHER_WINDOWS:
    case HOTKEY_ACTION_CYCLE_SPACE_LAYOUT:
    case HOTKEY_ACTION_RELOAD_CONFIGURATION:
        return true;
    }
    return false;
}

static bool declarative_reload_binding_equal(const struct hotkey_binding *a,
                                             const struct hotkey_binding *b)
{
    if (!declarative_reload_keystroke_equal(a->keystroke, b->keystroke) ||
        a->repeat != b->repeat || a->case_count != b->case_count) return false;
    for (size_t i = 0; i < a->case_count; ++i) {
        const struct hotkey_binding_case *x = &a->cases[i];
        const struct hotkey_binding_case *y = &b->cases[i];
        if (!declarative_reload_string_equal(x->application.bundle_id, y->application.bundle_id) ||
            !declarative_reload_string_equal(x->application.name, y->application.name) ||
            !declarative_reload_string_equal(x->window.title, y->window.title) ||
            !declarative_reload_string_equal(x->window.title_prefix, y->window.title_prefix) ||
            x->disposition != y->disposition || x->action_count != y->action_count ||
            !declarative_reload_string_equal(x->transition_name, y->transition_name) ||
            x->transition_mode != y->transition_mode) return false;
        for (size_t j = 0; j < x->action_count; ++j) {
            if (!declarative_reload_action_equal(&x->actions[j], &y->actions[j])) return false;
        }
    }
    return true;
}

static bool declarative_reload_binding_list_equal(const struct hotkey_binding *a, size_t a_count,
                                                  const struct hotkey_binding *b, size_t b_count)
{
    if (a_count != b_count) return false;
    for (size_t i = 0; i < a_count; ++i) {
        if (!declarative_reload_binding_equal(&a[i], &b[i])) return false;
    }
    return true;
}

static bool declarative_reload_hotkeys_equal(const struct hotkey_config_model *a,
                                             const struct hotkey_config_model *b)
{
    if (a->present != b->present || a->enabled != b->enabled ||
        !declarative_reload_binding_list_equal(a->bindings, a->binding_count,
                                               b->bindings, b->binding_count) ||
        a->mode_count != b->mode_count) return false;
    for (size_t i = 0; i < a->mode_count; ++i) {
        const struct hotkey_input_mode *x = &a->modes[i];
        const struct hotkey_input_mode *y = &b->modes[i];
        if (!declarative_reload_string_equal(x->name, y->name) ||
            x->capture_unbound != y->capture_unbound || x->timeout_ms != y->timeout_ms ||
            !declarative_reload_binding_list_equal(x->bindings, x->binding_count,
                                                   y->bindings, y->binding_count)) return false;
    }
    return true;
}

static void declarative_reload_summary_append(struct declarative_reload_summary *summary,
                                              size_t *length, const char *format, ...)
{
    if (*length >= sizeof(summary->text)) return;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(summary->text + *length, sizeof(summary->text) - *length, format, args);
    va_end(args);
    if (written < 0) return;
    size_t available = sizeof(summary->text) - *length;
    *length += (size_t) written < available ? (size_t) written : available - 1;
}

void declarative_reload_summary_build(struct declarative_reload_summary *summary,
                                      const struct declarative_config_model *previous,
                                      const struct declarative_config_model *current)
{
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
    if (!previous || !current) {
        snprintf(summary->text, sizeof(summary->text), "Configuration reloaded.");
        return;
    }

    const char *changes[DECLARATIVE_CONFIG_COUNT + 6];
    size_t count = 0;
    if (previous->version != current->version) changes[count++] = "version";
    for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) {
        if (!declarative_reload_value_equal((enum declarative_config_id) id,
                                            &previous->config[id], &current->config[id])) {
            changes[count++] = declarative_config_descriptors[id].name;
        }
    }
    if (!declarative_reload_space_list_equal(previous->spaces, previous->space_count,
                                             current->spaces, current->space_count)) changes[count++] = "spaces";
    if (!declarative_reload_modes_equal(previous, current)) changes[count++] = "modes";
    if (!declarative_reload_string_equal(previous->active_mode, current->active_mode)) changes[count++] = "active_mode";
    if (!declarative_reload_rules_equal(previous, current)) changes[count++] = "rules";
    if (!declarative_reload_hotkeys_equal(&previous->keyboard, &current->keyboard)) changes[count++] = "hotkeys";

    summary->change_count = count;
    if (!count) {
        snprintf(summary->text, sizeof(summary->text), "Configuration reloaded. No changes detected.");
        return;
    }

    size_t length = 0;
    declarative_reload_summary_append(summary, &length, "Configuration reloaded. Changed: ");
    size_t shown = count > 6 ? 5 : count;
    for (size_t i = 0; i < shown; ++i) {
        declarative_reload_summary_append(summary, &length, "%s%s", i ? ", " : "", changes[i]);
    }
    if (shown < count) {
        declarative_reload_summary_append(summary, &length, ", and %zu more", count - shown);
    }
    declarative_reload_summary_append(summary, &length, ".");
}
