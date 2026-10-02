struct declarative_runtime_state g_declarative_runtime_state;

static bool declarative_animation_sip_friendly(void) { return scripting_addition_is_sip_friendly(); }
static bool declarative_animation_preflight(void) { return CGPreflightScreenCaptureAccess(); }
static void declarative_animation_request(void) { CGRequestScreenCaptureAccess(); }

#ifdef TESTS
static const struct declarative_animation_permission_ops declarative_default_animation_permission_ops = {
    .sip_friendly = declarative_animation_sip_friendly,
    .screen_capture_preflight = declarative_animation_preflight,
    .screen_capture_request = declarative_animation_request
};
static struct declarative_animation_permission_ops declarative_animation_permission_ops = {
    .sip_friendly = declarative_animation_sip_friendly,
    .screen_capture_preflight = declarative_animation_preflight,
    .screen_capture_request = declarative_animation_request
};

void declarative_runtime_set_animation_permission_ops(const struct declarative_animation_permission_ops *ops)
{
    if (ops) declarative_animation_permission_ops = *ops;
}

void declarative_runtime_reset_animation_permission_ops(void)
{
    declarative_animation_permission_ops = declarative_default_animation_permission_ops;
}
#else
static const struct declarative_animation_permission_ops declarative_animation_permission_ops = {
    .sip_friendly = declarative_animation_sip_friendly,
    .screen_capture_preflight = declarative_animation_preflight,
    .screen_capture_request = declarative_animation_request
};
#endif

void declarative_runtime_request_animation_permission(void)
{
    declarative_animation_permission_ops.screen_capture_request();
}

static bool declarative_operation_set_ffm(void *manager, int mode)
{
    return window_manager_set_focus_follows_mouse(manager, mode);
}
static bool declarative_operation_set_global_layout(void *manager, int layout)
{
    return space_manager_set_layout_for_all_spaces(manager, layout);
}
static bool declarative_operation_set_space_layout(void *manager, uint64_t sid, int layout)
{
    return space_manager_set_layout_for_space(manager, sid, layout);
}
static void declarative_operation_set_split_type(void *manager, int split_type)
{
    space_manager_set_split_type_for_all_spaces(manager, split_type);
}
static void declarative_operation_set_auto_balance(void *manager, uint32_t auto_balance)
{
    space_manager_set_auto_balance_for_all_spaces(manager, auto_balance);
}
static void declarative_operation_update_and_flush_space(void *space)
{
    view_update(space);
    view_flush(space);
}
static void declarative_operation_set_global_space_geometry(void *manager, enum declarative_config_id id, int value)
{
    struct space_manager *sm = manager;
    switch (id) {
    case DECLARATIVE_CONFIG_TOP_PADDING: space_manager_set_top_padding_for_all_spaces(sm, value); break;
    case DECLARATIVE_CONFIG_BOTTOM_PADDING: space_manager_set_bottom_padding_for_all_spaces(sm, value); break;
    case DECLARATIVE_CONFIG_LEFT_PADDING: space_manager_set_left_padding_for_all_spaces(sm, value); break;
    case DECLARATIVE_CONFIG_RIGHT_PADDING: space_manager_set_right_padding_for_all_spaces(sm, value); break;
    case DECLARATIVE_CONFIG_WINDOW_GAP: space_manager_set_window_gap_for_all_spaces(sm, value); break;
    default: break;
    }
}
static void declarative_operation_mark_menu_bar_dirty(uint32_t reasons) { menu_bar_manager_mark_dirty(reasons); }
static void declarative_operation_set_opacity_enabled(void *manager, bool value) { window_manager_set_window_opacity_enabled(manager, value); }
static void declarative_operation_set_menubar_opacity(void *manager, float value) { window_manager_set_menubar_opacity(manager, value); }
static void declarative_operation_set_active_opacity(void *manager, float value) { window_manager_set_active_window_opacity(manager, value); }
static void declarative_operation_set_normal_opacity(void *manager, float value) { window_manager_set_normal_window_opacity(manager, value); }
static void declarative_operation_set_shadow(void *manager, int value) { window_manager_set_purify_mode(manager, value); }
static void declarative_operation_set_menu_enabled(int value) { menu_bar_manager_set_enabled(value); }
static void declarative_operation_set_menu_style(int value) { menu_bar_manager_set_style(value); }
static void declarative_operation_set_menu_space_filter(int value) { menu_bar_manager_set_space_filter(value); }
static void declarative_operation_set_mouse_modifier(void *state, int value) { mouse_state_set_modifier(state, value); }
static void declarative_operation_set_mouse_action1(void *state, int value) { mouse_state_set_action1(state, value); }
static void declarative_operation_set_mouse_action2(void *state, int value) { mouse_state_set_action2(state, value); }
static void declarative_operation_set_mouse_drop_action(void *state, int value) { ((struct mouse_state *) state)->drop_action = value; }
static void declarative_operation_invalidate_spaces(void *manager) { space_manager_mark_spaces_invalid(manager); }
#define DECLARATIVE_OPERATION_DEFAULTS \
    .space_is_user = space_is_user, \
    .set_focus_follows_mouse = declarative_operation_set_ffm, \
    .set_layout_for_all_spaces = declarative_operation_set_global_layout, \
    .set_layout_for_space = declarative_operation_set_space_layout, \
    .set_split_type_for_all_spaces = declarative_operation_set_split_type, \
    .set_auto_balance_for_all_spaces = declarative_operation_set_auto_balance, \
    .update_and_flush_space = declarative_operation_update_and_flush_space, \
    .set_global_space_geometry = declarative_operation_set_global_space_geometry, \
    .mark_menu_bar_dirty = declarative_operation_mark_menu_bar_dirty, \
    .set_window_opacity_enabled = declarative_operation_set_opacity_enabled, \
    .set_menubar_opacity = declarative_operation_set_menubar_opacity, \
    .set_active_window_opacity = declarative_operation_set_active_opacity, \
    .set_normal_window_opacity = declarative_operation_set_normal_opacity, \
    .set_shadow = declarative_operation_set_shadow, \
    .set_menu_enabled = declarative_operation_set_menu_enabled, \
    .set_menu_style = declarative_operation_set_menu_style, \
    .set_menu_space_filter = declarative_operation_set_menu_space_filter, \
    .set_mouse_modifier = declarative_operation_set_mouse_modifier, \
    .set_mouse_action1 = declarative_operation_set_mouse_action1, \
    .set_mouse_action2 = declarative_operation_set_mouse_action2, \
    .set_mouse_drop_action = declarative_operation_set_mouse_drop_action, \
    .invalidate_spaces = declarative_operation_invalidate_spaces
static const struct declarative_runtime_operation_ops declarative_default_operation_ops = { DECLARATIVE_OPERATION_DEFAULTS };
static struct declarative_runtime_operation_ops declarative_runtime_operation_ops = { DECLARATIVE_OPERATION_DEFAULTS };
#undef DECLARATIVE_OPERATION_DEFAULTS

#ifdef TESTS
void declarative_runtime_set_operation_ops(const struct declarative_runtime_operation_ops *ops)
{
    if (ops) declarative_runtime_operation_ops = *ops;
}

void declarative_runtime_reset_operation_ops(void)
{
    declarative_runtime_operation_ops = declarative_default_operation_ops;
}
#endif

void declarative_runtime_reset_ownership(void)
{
    memset(&g_declarative_runtime_state, 0, sizeof(g_declarative_runtime_state));
}

static union declarative_runtime_scalar declarative_runtime_scalar_from_value(enum declarative_config_id id, const struct declarative_value *value)
{
    union declarative_runtime_scalar scalar = { 0 };
    switch (declarative_config_descriptors[id].kind) {
    case DECLARATIVE_VALUE_BOOL: scalar.boolean = value->boolean; break;
    case DECLARATIVE_VALUE_ENUM: scalar.integer = value->enumeration; break;
    case DECLARATIVE_VALUE_INT: scalar.integer = value->integer; break;
    case DECLARATIVE_VALUE_UINT: scalar.u32 = value->u32; break;
    case DECLARATIVE_VALUE_FLOAT: scalar.real = value->real; break;
    case DECLARATIVE_VALUE_STRUCTURED:
        scalar.external_bar = (struct declarative_runtime_external_bar) {
            .mode = value->external_bar.mode,
            .top_padding = value->external_bar.top_padding,
            .bottom_padding = value->external_bar.bottom_padding
        };
        break;
    }
    return scalar;
}

int declarative_space_field_for_id(enum declarative_config_id id)
{
    switch (id) {
    case DECLARATIVE_CONFIG_LAYOUT:         return DECLARATIVE_RUNTIME_SPACE_LAYOUT;
    case DECLARATIVE_CONFIG_TOP_PADDING:    return DECLARATIVE_RUNTIME_SPACE_TOP_PADDING;
    case DECLARATIVE_CONFIG_BOTTOM_PADDING: return DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING;
    case DECLARATIVE_CONFIG_LEFT_PADDING:   return DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING;
    case DECLARATIVE_CONFIG_RIGHT_PADDING:  return DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING;
    case DECLARATIVE_CONFIG_WINDOW_GAP:     return DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP;
    case DECLARATIVE_CONFIG_SPLIT_TYPE:     return DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE;
    case DECLARATIVE_CONFIG_AUTO_BALANCE:   return DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE;
    default: return -1;
    }
}

static void declarative_record_global(enum declarative_config_id id, const struct declarative_value *value)
{
    g_declarative_runtime_state.global[id] = (struct declarative_runtime_cell) {
        .present = true,
        .source = DECLARATIVE_SETTING_RUNTIME_GLOBAL,
        .value = declarative_runtime_scalar_from_value(id, value)
    };
}

static void declarative_record_space(struct view *view, enum declarative_config_id id, const struct declarative_value *value)
{
    int field = declarative_space_field_for_id(id);
    if (field < 0) return;
    view->runtime_ownership[field] = (struct declarative_runtime_cell) {
        .present = true,
        .source = DECLARATIVE_SETTING_RUNTIME_SPACE,
        .value = declarative_runtime_scalar_from_value(id, value)
    };
}

static bool declarative_value_valid(enum declarative_config_id id, const struct declarative_value *value)
{
    if (!value || !value->present) return false;
    switch (id) {
    case DECLARATIVE_CONFIG_FFM: return value->enumeration >= FFM_DISABLED && value->enumeration <= FFM_AUTORAISE;
    case DECLARATIVE_CONFIG_DISPLAY_ORDER: return value->enumeration >= DISPLAY_ARRANGEMENT_ORDER_DEFAULT && value->enumeration <= DISPLAY_ARRANGEMENT_ORDER_Y;
    case DECLARATIVE_CONFIG_WINDOW_ORIGIN: return value->enumeration >= WINDOW_ORIGIN_DEFAULT && value->enumeration <= WINDOW_ORIGIN_CURSOR;
    case DECLARATIVE_CONFIG_WINDOW_PLACEMENT: return value->enumeration == CHILD_FIRST || value->enumeration == CHILD_SECOND;
    case DECLARATIVE_CONFIG_WINDOW_INSERT_POINT: return value->enumeration >= INSERT_FOCUSED && value->enumeration <= INSERT_LAST;
    case DECLARATIVE_CONFIG_OPACITY_DURATION: return value->real >= 0.0f && isfinite(value->real) && sa_opacity_duration_is_safe(value->real);
    case DECLARATIVE_CONFIG_ANIMATION_DURATION: return value->real >= 0.0f && isfinite(value->real);
    case DECLARATIVE_CONFIG_ANIMATION_EASING: return value->enumeration >= 0 && value->enumeration < EASING_TYPE_COUNT;
    case DECLARATIVE_CONFIG_SHADOW: return value->enumeration >= PURIFY_DISABLED && value->enumeration <= PURIFY_ALWAYS;
    case DECLARATIVE_CONFIG_MENUBAR: return value->enumeration == MENU_BAR_DISABLED || value->enumeration == MENU_BAR_ENABLED;
    case DECLARATIVE_CONFIG_MENUBAR_STYLE: return value->enumeration == MENU_BAR_STYLE_COMPACT || value->enumeration == MENU_BAR_STYLE_EXPANDED;
    case DECLARATIVE_CONFIG_MENUBAR_SPACE_FILTER: return value->enumeration == MENU_BAR_SPACE_FILTER_OCCUPIED || value->enumeration == MENU_BAR_SPACE_FILTER_ALL;
    case DECLARATIVE_CONFIG_MENUBAR_OPACITY: return value->real >= 0.0f && value->real <= 1.0f && isfinite(value->real);
    case DECLARATIVE_CONFIG_ACTIVE_WINDOW_OPACITY:
    case DECLARATIVE_CONFIG_NORMAL_WINDOW_OPACITY: return value->real > 0.0f && value->real <= 1.0f && isfinite(value->real);
    case DECLARATIVE_CONFIG_INSERT_FEEDBACK_COLOR: return value->u32 != 0;
    case DECLARATIVE_CONFIG_LAYOUT: return value->enumeration == VIEW_BSP || value->enumeration == VIEW_STACK || value->enumeration == VIEW_FLOAT;
    case DECLARATIVE_CONFIG_SPLIT_RATIO: return value->real >= 0.1f && value->real <= 0.9f && isfinite(value->real);
    case DECLARATIVE_CONFIG_SPLIT_TYPE: return value->enumeration == SPLIT_Y || value->enumeration == SPLIT_X || value->enumeration == SPLIT_AUTO;
    case DECLARATIVE_CONFIG_AUTO_BALANCE: return value->u32 <= (SPLIT_X | SPLIT_Y);
    case DECLARATIVE_CONFIG_MOUSE_MOD: return value->enumeration == MOUSE_MOD_ALT || value->enumeration == MOUSE_MOD_SHIFT || value->enumeration == MOUSE_MOD_CMD || value->enumeration == MOUSE_MOD_CTRL || value->enumeration == MOUSE_MOD_FN;
    case DECLARATIVE_CONFIG_MOUSE_ACTION1:
    case DECLARATIVE_CONFIG_MOUSE_ACTION2: return value->enumeration == MOUSE_MODE_MOVE || value->enumeration == MOUSE_MODE_RESIZE;
    case DECLARATIVE_CONFIG_MOUSE_DROP_ACTION: return value->enumeration == MOUSE_MODE_SWAP || value->enumeration == MOUSE_MODE_STACK;
    case DECLARATIVE_CONFIG_EXTERNAL_BAR: return value->external_bar.mode >= EXTERNAL_BAR_OFF && value->external_bar.mode <= EXTERNAL_BAR_ALL;
    default: return true;
    }
}

enum declarative_apply_result declarative_apply_runtime_layout_for_space(uint64_t sid, enum view_type layout)
{
    if (!declarative_runtime_operation_ops.space_is_user(sid)) return DECLARATIVE_APPLY_NON_USER_SPACE;
    if (!declarative_runtime_operation_ops.set_layout_for_space(&g_space_manager, sid, layout)) return DECLARATIVE_APPLY_LAYOUT_FAILED;
    struct view *view = space_manager_query_view(&g_space_manager, sid);
    if (!view) return DECLARATIVE_APPLY_LAYOUT_FAILED;
    struct declarative_value value = { .present = true, .enumeration = layout };
    view_set_flag(view, VIEW_LAYOUT);
    declarative_record_space(view, DECLARATIVE_CONFIG_LAYOUT, &value);
    return DECLARATIVE_APPLY_OK;
}

enum declarative_apply_result declarative_apply_runtime_setting(enum declarative_config_id id, const struct declarative_value *value, uint64_t sid)
{
    if ((unsigned) id >= DECLARATIVE_CONFIG_COUNT || !declarative_value_valid(id, value)) return DECLARATIVE_APPLY_INVALID_VALUE;
    int space_field = declarative_space_field_for_id(id);
    if (sid && space_field < 0) return DECLARATIVE_APPLY_INVALID_SCOPE;
    if (sid && id == DECLARATIVE_CONFIG_LAYOUT) return declarative_apply_runtime_layout_for_space(sid, value->enumeration);

    struct view *view = NULL;
    if (sid) view = space_manager_find_view(&g_space_manager, sid);

    switch (id) {
    case DECLARATIVE_CONFIG_DEBUG_OUTPUT: g_verbose = value->boolean; break;
    case DECLARATIVE_CONFIG_MFF: g_window_manager.enable_mff = value->boolean; break;
    case DECLARATIVE_CONFIG_FFM:
        if (!declarative_runtime_operation_ops.set_focus_follows_mouse(&g_window_manager, value->enumeration)) return DECLARATIVE_APPLY_LAYOUT_FAILED;
        break;
    case DECLARATIVE_CONFIG_DISPLAY_ORDER:
        g_display_manager.order = value->enumeration;
        declarative_runtime_operation_ops.mark_menu_bar_dirty(MENU_BAR_DIRTY_CONFIG | MENU_BAR_DIRTY_DISPLAY);
        break;
    case DECLARATIVE_CONFIG_WINDOW_ORIGIN: g_window_manager.window_origin_mode = value->enumeration; break;
    case DECLARATIVE_CONFIG_WINDOW_PLACEMENT: g_space_manager.window_placement = value->enumeration; break;
    case DECLARATIVE_CONFIG_WINDOW_INSERT_POINT: g_space_manager.window_insertion_point = value->enumeration; break;
    case DECLARATIVE_CONFIG_WINDOW_ZOOM_PERSIST: g_space_manager.window_zoom_persist = value->boolean; break;
    case DECLARATIVE_CONFIG_SKIP_SPACE_ANIMATION: g_space_manager.skip_window_focus_animation = value->boolean; break;
    case DECLARATIVE_CONFIG_OPACITY: declarative_runtime_operation_ops.set_window_opacity_enabled(&g_window_manager, value->boolean); break;
    case DECLARATIVE_CONFIG_OPACITY_DURATION: g_window_manager.window_opacity_duration = value->real; break;
    case DECLARATIVE_CONFIG_ANIMATION_DURATION:
        if (value->real != 0.0f) {
            if (!declarative_animation_permission_ops.sip_friendly()) return DECLARATIVE_APPLY_SIP_DENIED;
            if (!declarative_animation_permission_ops.screen_capture_preflight()) {
                return DECLARATIVE_APPLY_SCREEN_CAPTURE_DENIED;
            }
        }
        g_window_manager.window_animation_duration = value->real;
        break;
    case DECLARATIVE_CONFIG_ANIMATION_EASING: g_window_manager.window_animation_easing = value->enumeration; break;
    case DECLARATIVE_CONFIG_SHADOW: declarative_runtime_operation_ops.set_shadow(&g_window_manager, value->enumeration); break;
    case DECLARATIVE_CONFIG_MENUBAR: declarative_runtime_operation_ops.set_menu_enabled(value->enumeration); break;
    case DECLARATIVE_CONFIG_MENUBAR_STYLE: declarative_runtime_operation_ops.set_menu_style(value->enumeration); break;
    case DECLARATIVE_CONFIG_MENUBAR_SPACE_FILTER: declarative_runtime_operation_ops.set_menu_space_filter(value->enumeration); break;
    case DECLARATIVE_CONFIG_MENUBAR_OPACITY: declarative_runtime_operation_ops.set_menubar_opacity(&g_window_manager, value->real); break;
    case DECLARATIVE_CONFIG_ACTIVE_WINDOW_OPACITY: declarative_runtime_operation_ops.set_active_window_opacity(&g_window_manager, value->real); break;
    case DECLARATIVE_CONFIG_NORMAL_WINDOW_OPACITY: declarative_runtime_operation_ops.set_normal_window_opacity(&g_window_manager, value->real); break;
    case DECLARATIVE_CONFIG_INSERT_FEEDBACK_COLOR: g_window_manager.insert_feedback_color = rgba_color_from_hex(value->u32); break;
    case DECLARATIVE_CONFIG_TOP_PADDING:
        if (view) { view_set_flag(view, VIEW_TOP_PADDING); view->top_padding = value->integer; declarative_runtime_operation_ops.update_and_flush_space(view); }
        else declarative_runtime_operation_ops.set_global_space_geometry(&g_space_manager, id, value->integer);
        break;
    case DECLARATIVE_CONFIG_BOTTOM_PADDING:
        if (view) { view_set_flag(view, VIEW_BOTTOM_PADDING); view->bottom_padding = value->integer; declarative_runtime_operation_ops.update_and_flush_space(view); }
        else declarative_runtime_operation_ops.set_global_space_geometry(&g_space_manager, id, value->integer);
        break;
    case DECLARATIVE_CONFIG_LEFT_PADDING:
        if (view) { view_set_flag(view, VIEW_LEFT_PADDING); view->left_padding = value->integer; declarative_runtime_operation_ops.update_and_flush_space(view); }
        else declarative_runtime_operation_ops.set_global_space_geometry(&g_space_manager, id, value->integer);
        break;
    case DECLARATIVE_CONFIG_RIGHT_PADDING:
        if (view) { view_set_flag(view, VIEW_RIGHT_PADDING); view->right_padding = value->integer; declarative_runtime_operation_ops.update_and_flush_space(view); }
        else declarative_runtime_operation_ops.set_global_space_geometry(&g_space_manager, id, value->integer);
        break;
    case DECLARATIVE_CONFIG_WINDOW_GAP:
        if (view) { view_set_flag(view, VIEW_WINDOW_GAP); view->window_gap = value->integer; declarative_runtime_operation_ops.update_and_flush_space(view); }
        else declarative_runtime_operation_ops.set_global_space_geometry(&g_space_manager, id, value->integer);
        break;
    case DECLARATIVE_CONFIG_LAYOUT:
        if (!declarative_runtime_operation_ops.set_layout_for_all_spaces(&g_space_manager, value->enumeration)) return DECLARATIVE_APPLY_LAYOUT_FAILED;
        break;
    case DECLARATIVE_CONFIG_SPLIT_RATIO: g_space_manager.split_ratio = value->real; break;
    case DECLARATIVE_CONFIG_SPLIT_TYPE:
        if (view) { view_set_flag(view, VIEW_SPLIT_TYPE); view->split_type = value->enumeration; }
        else declarative_runtime_operation_ops.set_split_type_for_all_spaces(&g_space_manager, value->enumeration);
        break;
    case DECLARATIVE_CONFIG_AUTO_BALANCE:
        if (view) { view_set_flag(view, VIEW_AUTO_BALANCE); view->auto_balance = value->u32; }
        else declarative_runtime_operation_ops.set_auto_balance_for_all_spaces(&g_space_manager, value->u32);
        break;
    case DECLARATIVE_CONFIG_MOUSE_MOD: declarative_runtime_operation_ops.set_mouse_modifier(&g_mouse_state, value->enumeration); break;
    case DECLARATIVE_CONFIG_MOUSE_ACTION1: declarative_runtime_operation_ops.set_mouse_action1(&g_mouse_state, value->enumeration); break;
    case DECLARATIVE_CONFIG_MOUSE_ACTION2: declarative_runtime_operation_ops.set_mouse_action2(&g_mouse_state, value->enumeration); break;
    case DECLARATIVE_CONFIG_MOUSE_DROP_ACTION: declarative_runtime_operation_ops.set_mouse_drop_action(&g_mouse_state, value->enumeration); break;
    case DECLARATIVE_CONFIG_EXTERNAL_BAR:
        g_display_manager.mode = value->external_bar.mode;
        g_display_manager.top_padding = value->external_bar.top_padding;
        g_display_manager.bottom_padding = value->external_bar.bottom_padding;
        declarative_runtime_operation_ops.invalidate_spaces(&g_space_manager);
        break;
    default: return DECLARATIVE_APPLY_INVALID_VALUE;
    }

    if (view) declarative_record_space(view, id, value); else declarative_record_global(id, value);
    return DECLARATIVE_APPLY_OK;
}

bool declarative_prepare_setting(struct declarative_prepared_settings *prepared, enum declarative_config_id id, const struct declarative_value *value, enum declarative_apply_result *result)
{
    enum declarative_apply_result local = DECLARATIVE_APPLY_OK;
    if (!prepared || (unsigned) id >= DECLARATIVE_CONFIG_COUNT || !declarative_value_valid(id, value)) {
        local = DECLARATIVE_APPLY_INVALID_VALUE;
    } else if (id == DECLARATIVE_CONFIG_ANIMATION_DURATION && value->real != 0.0f) {
        if (!declarative_animation_permission_ops.sip_friendly()) local = DECLARATIVE_APPLY_SIP_DENIED;
        else if (!declarative_animation_permission_ops.screen_capture_preflight()) local = DECLARATIVE_APPLY_SCREEN_CAPTURE_DENIED;
    }
    if (result) *result = local;
    if (local != DECLARATIVE_APPLY_OK) return false;
    prepared->values[id] = *value;
    return true;
}
