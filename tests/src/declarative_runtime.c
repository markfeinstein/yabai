static bool declarative_test_permission_yes(void) { return true; }
static bool declarative_test_permission_no(void) { return false; }
static int declarative_test_permission_requests;
static void declarative_test_permission_request(void) { ++declarative_test_permission_requests; }

static bool declarative_test_operation_success = true;
static int declarative_test_ffm_calls;
static int declarative_test_global_layout_calls;
static int declarative_test_space_layout_calls;
static int declarative_test_space_is_user_calls;
enum declarative_test_effect {
    DECLARATIVE_TEST_EFFECT_FFM,
    DECLARATIVE_TEST_EFFECT_DISPLAY_DIRTY,
    DECLARATIVE_TEST_EFFECT_OPACITY_ENABLE,
    DECLARATIVE_TEST_EFFECT_MENUBAR_OPACITY,
    DECLARATIVE_TEST_EFFECT_ACTIVE_OPACITY,
    DECLARATIVE_TEST_EFFECT_NORMAL_OPACITY,
    DECLARATIVE_TEST_EFFECT_SHADOW,
    DECLARATIVE_TEST_EFFECT_MENU_ENABLED,
    DECLARATIVE_TEST_EFFECT_MENU_STYLE,
    DECLARATIVE_TEST_EFFECT_MENU_FILTER,
    DECLARATIVE_TEST_EFFECT_GLOBAL_GEOMETRY,
    DECLARATIVE_TEST_EFFECT_SPACE_GEOMETRY,
    DECLARATIVE_TEST_EFFECT_LAYOUT,
    DECLARATIVE_TEST_EFFECT_SPLIT_TYPE,
    DECLARATIVE_TEST_EFFECT_AUTO_BALANCE,
    DECLARATIVE_TEST_EFFECT_MOUSE_MODIFIER,
    DECLARATIVE_TEST_EFFECT_MOUSE_ACTION1,
    DECLARATIVE_TEST_EFFECT_MOUSE_ACTION2,
    DECLARATIVE_TEST_EFFECT_MOUSE_DROP,
    DECLARATIVE_TEST_EFFECT_EXTERNAL_INVALIDATE,
    DECLARATIVE_TEST_EFFECT_COUNT
};
static int declarative_test_effects[DECLARATIVE_TEST_EFFECT_COUNT];
static int declarative_test_space_geometry_calls;
static bool declarative_test_user_space = true;
static bool declarative_test_space_is_user(uint64_t sid)
{
    ++declarative_test_space_is_user_calls;
    return sid != 0 && declarative_test_user_space;
}
static bool declarative_test_set_ffm(void *manager, int mode)
{
    struct window_manager *wm = manager;
    ++declarative_test_ffm_calls;
    ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_FFM];
    wm->configured_ffm_mode = mode;
    window_manager_recompute_effective_ffm_mode(wm);
    return true;
}
static bool declarative_test_set_global_layout(void *manager, int layout)
{
    struct space_manager *sm = manager;
    ++declarative_test_global_layout_calls;
    ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_LAYOUT];
    if (!declarative_test_operation_success) return false;
    sm->layout = layout;
    return true;
}
static bool declarative_test_set_space_layout(void *manager, uint64_t sid, int layout)
{
    struct space_manager *sm = manager;
    ++declarative_test_space_layout_calls;
    ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_LAYOUT];
    if (!declarative_test_operation_success) return false;
    struct view *view = space_manager_find_view(sm, sid);
    if (!view) return false;
    view->layout = layout;
    return true;
}
static void declarative_test_set_split_type(void *manager, int value)
{
    ((struct space_manager *) manager)->split_type = value;
    ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_SPLIT_TYPE];
}
static void declarative_test_set_auto_balance(void *manager, uint32_t value)
{
    ((struct space_manager *) manager)->auto_balance = value;
    ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_AUTO_BALANCE];
}
static void declarative_test_update_and_flush_space(void *space)
{
    (void) space;
    ++declarative_test_space_geometry_calls;
    ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_SPACE_GEOMETRY];
}
static void declarative_test_global_geometry(void *manager, enum declarative_config_id id, int value)
{
    struct space_manager *sm = manager;
    ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_GLOBAL_GEOMETRY];
    switch (id) {
    case DECLARATIVE_CONFIG_TOP_PADDING: sm->top_padding = value; break;
    case DECLARATIVE_CONFIG_BOTTOM_PADDING: sm->bottom_padding = value; break;
    case DECLARATIVE_CONFIG_LEFT_PADDING: sm->left_padding = value; break;
    case DECLARATIVE_CONFIG_RIGHT_PADDING: sm->right_padding = value; break;
    case DECLARATIVE_CONFIG_WINDOW_GAP: sm->window_gap = value; break;
    case DECLARATIVE_CONFIG_ACCORDION_PADDING: sm->accordion_padding = value; break;
    default: break;
    }
}
static uint32_t declarative_test_dirty_reasons;
static void declarative_test_display_dirty(uint32_t reasons) { declarative_test_dirty_reasons |= reasons; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_DISPLAY_DIRTY]; }
static void declarative_test_opacity_enabled(void *manager, bool value) { ((struct window_manager *) manager)->enable_window_opacity = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_OPACITY_ENABLE]; }
static void declarative_test_menubar_opacity(void *manager, float value) { ((struct window_manager *) manager)->menubar_opacity = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_MENUBAR_OPACITY]; }
static void declarative_test_active_opacity(void *manager, float value) { ((struct window_manager *) manager)->active_window_opacity = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_ACTIVE_OPACITY]; }
static void declarative_test_normal_opacity(void *manager, float value) { ((struct window_manager *) manager)->normal_window_opacity = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_NORMAL_OPACITY]; }
static void declarative_test_shadow(void *manager, int value) { ((struct window_manager *) manager)->purify_mode = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_SHADOW]; }
static void declarative_test_menu_enabled(int value) { g_menu_bar_manager.settings.enabled = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_MENU_ENABLED]; }
static void declarative_test_menu_style(int value) { g_menu_bar_manager.settings.style = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_MENU_STYLE]; }
static void declarative_test_menu_filter(int value) { g_menu_bar_manager.settings.space_filter = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_MENU_FILTER]; }
static void declarative_test_mouse_modifier(void *state, int value) { ((struct mouse_state *) state)->modifier = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_MOUSE_MODIFIER]; }
static void declarative_test_mouse_action1(void *state, int value) { ((struct mouse_state *) state)->action1 = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_MOUSE_ACTION1]; }
static void declarative_test_mouse_action2(void *state, int value) { ((struct mouse_state *) state)->action2 = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_MOUSE_ACTION2]; }
static void declarative_test_mouse_drop(void *state, int value) { ((struct mouse_state *) state)->drop_action = value; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_MOUSE_DROP]; }
static void declarative_test_invalidate_spaces(void *manager) { (void) manager; ++declarative_test_effects[DECLARATIVE_TEST_EFFECT_EXTERNAL_INVALIDATE]; }
static const struct declarative_runtime_operation_ops declarative_test_operation_ops = {
    .space_is_user = declarative_test_space_is_user,
    .set_focus_follows_mouse = declarative_test_set_ffm,
    .set_layout_for_all_spaces = declarative_test_set_global_layout,
    .set_layout_for_space = declarative_test_set_space_layout,
    .set_split_type_for_all_spaces = declarative_test_set_split_type,
    .set_auto_balance_for_all_spaces = declarative_test_set_auto_balance,
    .update_and_flush_space = declarative_test_update_and_flush_space,
    .set_global_space_geometry = declarative_test_global_geometry,
    .mark_menu_bar_dirty = declarative_test_display_dirty,
    .set_window_opacity_enabled = declarative_test_opacity_enabled,
    .set_menubar_opacity = declarative_test_menubar_opacity,
    .set_active_window_opacity = declarative_test_active_opacity,
    .set_normal_window_opacity = declarative_test_normal_opacity,
    .set_shadow = declarative_test_shadow,
    .set_menu_enabled = declarative_test_menu_enabled,
    .set_menu_style = declarative_test_menu_style,
    .set_menu_space_filter = declarative_test_menu_filter,
    .set_mouse_modifier = declarative_test_mouse_modifier,
    .set_mouse_action1 = declarative_test_mouse_action1,
    .set_mouse_action2 = declarative_test_mouse_action2,
    .set_mouse_drop_action = declarative_test_mouse_drop,
    .invalidate_spaces = declarative_test_invalidate_spaces
};

static union declarative_runtime_scalar declarative_test_expected_scalar(enum declarative_config_id id, const struct declarative_value *value)
{
    union declarative_runtime_scalar expected = {0};
    switch (declarative_config_descriptors[id].kind) {
    case DECLARATIVE_VALUE_BOOL: expected.boolean = value->boolean; break;
    case DECLARATIVE_VALUE_ENUM: expected.integer = value->enumeration; break;
    case DECLARATIVE_VALUE_INT: expected.integer = value->integer; break;
    case DECLARATIVE_VALUE_UINT: expected.u32 = value->u32; break;
    case DECLARATIVE_VALUE_FLOAT: expected.real = value->real; break;
    case DECLARATIVE_VALUE_STRUCTURED:
        expected.external_bar = (struct declarative_runtime_external_bar) { value->external_bar.mode, value->external_bar.top_padding, value->external_bar.bottom_padding };
        break;
    }
    return expected;
}

struct declarative_command_case {
    enum declarative_config_id id;
    const char *value;
    const char *query;
    int effect;
};

static const struct declarative_command_case declarative_command_cases[DECLARATIVE_CONFIG_COUNT] = {
    { DECLARATIVE_CONFIG_DEBUG_OUTPUT, "on", "on\n", -1 },
    { DECLARATIVE_CONFIG_MFF, "on", "on\n", -1 },
    { DECLARATIVE_CONFIG_FFM, "off", "disabled\n", DECLARATIVE_TEST_EFFECT_FFM },
    { DECLARATIVE_CONFIG_DISPLAY_ORDER, "horizontal", "horizontal\n", DECLARATIVE_TEST_EFFECT_DISPLAY_DIRTY },
    { DECLARATIVE_CONFIG_WINDOW_ORIGIN, "cursor", "cursor\n", -1 },
    { DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK, "recent", "recent\n", -1 },
    { DECLARATIVE_CONFIG_WINDOW_PLACEMENT, "second_child", "second_child\n", -1 },
    { DECLARATIVE_CONFIG_WINDOW_INSERT_POINT, "last", "last\n", -1 },
    { DECLARATIVE_CONFIG_WINDOW_ZOOM_PERSIST, "on", "on\n", -1 },
    { DECLARATIVE_CONFIG_OPACITY, "on", "on\n", DECLARATIVE_TEST_EFFECT_OPACITY_ENABLE },
    { DECLARATIVE_CONFIG_OPACITY_DURATION, "0.25", "0.250000\n", -1 },
    { DECLARATIVE_CONFIG_ANIMATION_DURATION, "0.0", "0.000000\n", -1 },
    { DECLARATIVE_CONFIG_ANIMATION_EASING, "ease_in_sine", "ease_in_sine\n", -1 },
    { DECLARATIVE_CONFIG_SHADOW, "float", "float\n", DECLARATIVE_TEST_EFFECT_SHADOW },
    { DECLARATIVE_CONFIG_MENUBAR, "on", "on\n", DECLARATIVE_TEST_EFFECT_MENU_ENABLED },
    { DECLARATIVE_CONFIG_MENUBAR_STYLE, "expanded", "expanded\n", DECLARATIVE_TEST_EFFECT_MENU_STYLE },
    { DECLARATIVE_CONFIG_MENUBAR_SPACE_FILTER, "all", "all\n", DECLARATIVE_TEST_EFFECT_MENU_FILTER },
    { DECLARATIVE_CONFIG_MENUBAR_OPACITY, "0.5", "0.5000\n", DECLARATIVE_TEST_EFFECT_MENUBAR_OPACITY },
    { DECLARATIVE_CONFIG_ACTIVE_WINDOW_OPACITY, "0.8", "0.8000\n", DECLARATIVE_TEST_EFFECT_ACTIVE_OPACITY },
    { DECLARATIVE_CONFIG_NORMAL_WINDOW_OPACITY, "0.7", "0.7000\n", DECLARATIVE_TEST_EFFECT_NORMAL_OPACITY },
    { DECLARATIVE_CONFIG_INSERT_FEEDBACK_COLOR, "0xff", "0xff\n", -1 },
    { DECLARATIVE_CONFIG_TOP_PADDING, "11", "11\n", DECLARATIVE_TEST_EFFECT_GLOBAL_GEOMETRY },
    { DECLARATIVE_CONFIG_BOTTOM_PADDING, "12", "12\n", DECLARATIVE_TEST_EFFECT_GLOBAL_GEOMETRY },
    { DECLARATIVE_CONFIG_LEFT_PADDING, "13", "13\n", DECLARATIVE_TEST_EFFECT_GLOBAL_GEOMETRY },
    { DECLARATIVE_CONFIG_RIGHT_PADDING, "14", "14\n", DECLARATIVE_TEST_EFFECT_GLOBAL_GEOMETRY },
    { DECLARATIVE_CONFIG_LAYOUT, "bsp", "bsp\n", DECLARATIVE_TEST_EFFECT_LAYOUT },
    { DECLARATIVE_CONFIG_WINDOW_GAP, "15", "15\n", DECLARATIVE_TEST_EFFECT_GLOBAL_GEOMETRY },
    { DECLARATIVE_CONFIG_ACCORDION_PADDING, "30", "30\n", DECLARATIVE_TEST_EFFECT_GLOBAL_GEOMETRY },
    { DECLARATIVE_CONFIG_SPLIT_RATIO, "0.6", "0.6000\n", -1 },
    { DECLARATIVE_CONFIG_SPLIT_TYPE, "auto", "auto\n", DECLARATIVE_TEST_EFFECT_SPLIT_TYPE },
    { DECLARATIVE_CONFIG_AUTO_BALANCE, "on", "on\n", DECLARATIVE_TEST_EFFECT_AUTO_BALANCE },
    { DECLARATIVE_CONFIG_MOUSE_MOD, "alt", "alt\n", DECLARATIVE_TEST_EFFECT_MOUSE_MODIFIER },
    { DECLARATIVE_CONFIG_MOUSE_ACTION1, "resize", "resize\n", DECLARATIVE_TEST_EFFECT_MOUSE_ACTION1 },
    { DECLARATIVE_CONFIG_MOUSE_ACTION2, "move", "move\n", DECLARATIVE_TEST_EFFECT_MOUSE_ACTION2 },
    { DECLARATIVE_CONFIG_MOUSE_DROP_ACTION, "stack", "stack\n", DECLARATIVE_TEST_EFFECT_MOUSE_DROP },
    { DECLARATIVE_CONFIG_EXTERNAL_BAR, "main:2:3", "main:2:3\n", DECLARATIVE_TEST_EFFECT_EXTERNAL_INVALIDATE },
    { DECLARATIVE_CONFIG_SKIP_SPACE_ANIMATION, "on", "on\n", -1 }
};

static char *declarative_test_config_message(const char *command, const char *value)
{
    size_t command_length = strlen(command);
    size_t value_length = value ? strlen(value) : 0;
    char *message = calloc(command_length + value_length + 3, 1);
    memcpy(message, command, command_length);
    if (value) memcpy(message + command_length + 1, value, value_length);
    return message;
}

static char *declarative_test_run_config(const char *command, const char *value)
{
    char *output = NULL;
    size_t output_length = 0;
    FILE *rsp = open_memstream(&output, &output_length);
    char *message = declarative_test_config_message(command, value);
    handle_domain_config(rsp, (struct token) { .text = "config", .length = 6 }, message);
    fclose(rsp);
    free(message);
    return output;
}

static char *declarative_test_run_space_layout_surface(bool config_surface)
{
    char *output = NULL;
    size_t output_length = 0;
    FILE *rsp = open_memstream(&output, &output_length);
    if (config_surface) {
        char message[] = "--space\0recent\0layout\0bsp\0";
        handle_domain_config(rsp, (struct token) { .text = "config", .length = 6 }, message);
    } else {
        char message[] = "recent\0--layout\0bsp\0";
        handle_domain_space(rsp, (struct token) { .text = "space", .length = 5 }, message);
    }
    fclose(rsp);
    return output;
}

static char *declarative_test_run_scoped_focus_fallback(void)
{
    char *output = NULL;
    size_t output_length = 0;
    FILE *rsp = open_memstream(&output, &output_length);
    char message[] = "--space\0recent\0window_focus_fallback\0recent\0";
    handle_domain_config(rsp, (struct token) { .text = "config", .length = 6 }, message);
    fclose(rsp);
    return output;
}

static struct declarative_value declarative_test_value(enum declarative_config_id id)
{
    struct declarative_value value = { .present = true };
    switch (id) {
    case DECLARATIVE_CONFIG_FFM: value.enumeration = FFM_DISABLED; break;
    case DECLARATIVE_CONFIG_DISPLAY_ORDER: value.enumeration = DISPLAY_ARRANGEMENT_ORDER_DEFAULT; break;
    case DECLARATIVE_CONFIG_WINDOW_ORIGIN: value.enumeration = WINDOW_ORIGIN_DEFAULT; break;
    case DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK: value.enumeration = WINDOW_FOCUS_FALLBACK_OFF; break;
    case DECLARATIVE_CONFIG_WINDOW_PLACEMENT: value.enumeration = CHILD_FIRST; break;
    case DECLARATIVE_CONFIG_WINDOW_INSERT_POINT: value.enumeration = INSERT_FOCUSED; break;
    case DECLARATIVE_CONFIG_OPACITY_DURATION: value.real = 0.1f; break;
    case DECLARATIVE_CONFIG_ANIMATION_DURATION: value.real = 0.1f; break;
    case DECLARATIVE_CONFIG_ANIMATION_EASING: value.enumeration = 0; break;
    case DECLARATIVE_CONFIG_SHADOW: value.enumeration = PURIFY_DISABLED; break;
    case DECLARATIVE_CONFIG_MENUBAR: value.enumeration = MENU_BAR_DISABLED; break;
    case DECLARATIVE_CONFIG_MENUBAR_STYLE: value.enumeration = MENU_BAR_STYLE_COMPACT; break;
    case DECLARATIVE_CONFIG_MENUBAR_SPACE_FILTER: value.enumeration = MENU_BAR_SPACE_FILTER_OCCUPIED; break;
    case DECLARATIVE_CONFIG_MENUBAR_OPACITY: value.real = 0.5f; break;
    case DECLARATIVE_CONFIG_ACTIVE_WINDOW_OPACITY:
    case DECLARATIVE_CONFIG_NORMAL_WINDOW_OPACITY: value.real = 0.9f; break;
    case DECLARATIVE_CONFIG_INSERT_FEEDBACK_COLOR: value.u32 = 1; break;
    case DECLARATIVE_CONFIG_LAYOUT: value.enumeration = VIEW_BSP; break;
    case DECLARATIVE_CONFIG_SPLIT_RATIO: value.real = 0.5f; break;
    case DECLARATIVE_CONFIG_SPLIT_TYPE: value.enumeration = SPLIT_AUTO; break;
    case DECLARATIVE_CONFIG_AUTO_BALANCE: value.u32 = SPLIT_X | SPLIT_Y; break;
    case DECLARATIVE_CONFIG_MOUSE_MOD: value.enumeration = MOUSE_MOD_ALT; break;
    case DECLARATIVE_CONFIG_MOUSE_ACTION1:
    case DECLARATIVE_CONFIG_MOUSE_ACTION2: value.enumeration = MOUSE_MODE_MOVE; break;
    case DECLARATIVE_CONFIG_MOUSE_DROP_ACTION: value.enumeration = MOUSE_MODE_SWAP; break;
    case DECLARATIVE_CONFIG_EXTERNAL_BAR:
        value.external_bar = (struct declarative_external_bar) { .mode = EXTERNAL_BAR_OFF, .top_padding = -2, .bottom_padding = 3 };
        break;
    default: value.boolean = true; break;
    }
    return value;
}

TEST_FUNC(declarative_runtime_preparation_covers_all_settings_without_live_mutation,
    struct declarative_prepared_settings prepared = {0};
    struct declarative_runtime_state ownership_before = g_declarative_runtime_state;
    struct space_manager spaces_before = g_space_manager;
    struct window_manager windows_before = g_window_manager;
    struct display_manager displays_before = g_display_manager;
    struct mouse_state mouse_before = g_mouse_state;
    struct declarative_animation_permission_ops permissions = { declarative_test_permission_yes, declarative_test_permission_yes, declarative_test_permission_request };
    declarative_runtime_set_animation_permission_ops(&permissions);
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        struct declarative_value value = declarative_test_value(i);
        enum declarative_apply_result apply_result = DECLARATIVE_APPLY_INVALID_VALUE;
        TEST_CHECK(declarative_prepare_setting(&prepared, i, &value, &apply_result), true);
        TEST_CHECK(apply_result, DECLARATIVE_APPLY_OK);
        TEST_CHECK(prepared.values[i].present, true);
    }
    TEST_CHECK(memcmp(&ownership_before, &g_declarative_runtime_state, sizeof(ownership_before)), 0);
    TEST_CHECK(memcmp(&spaces_before, &g_space_manager, sizeof(spaces_before)), 0);
    TEST_CHECK(memcmp(&windows_before, &g_window_manager, sizeof(windows_before)), 0);
    TEST_CHECK(memcmp(&displays_before, &g_display_manager, sizeof(displays_before)), 0);
    TEST_CHECK(memcmp(&mouse_before, &g_mouse_state, sizeof(mouse_before)), 0);
    for (int easing = 0; easing < EASING_TYPE_COUNT; ++easing) {
        struct declarative_value value = { .present = true, .enumeration = easing };
        TEST_CHECK(declarative_prepare_setting(&prepared, DECLARATIVE_CONFIG_ANIMATION_EASING, &value, NULL), true);
    }
    declarative_runtime_reset_animation_permission_ops();
)

TEST_FUNC(declarative_command_matrix_covers_all_37_query_invalid_and_mutation_paths,
    TEST_CHECK(sizeof(declarative_command_cases) / sizeof(declarative_command_cases[0]), 37);
    struct declarative_animation_permission_ops permissions = { declarative_test_permission_yes, declarative_test_permission_yes, declarative_test_permission_request };
    declarative_runtime_set_animation_permission_ops(&permissions);
    declarative_runtime_set_operation_ops(&declarative_test_operation_ops);
    declarative_runtime_reset_ownership();
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        const struct declarative_command_case *test = &declarative_command_cases[i];
        const char *command = declarative_config_descriptors[i].name;
        TEST_CHECK(test->id, i);
        char *before_query = declarative_test_run_config(command, NULL);
        struct declarative_runtime_cell ownership_before = g_declarative_runtime_state.global[i];
        memset(declarative_test_effects, 0, sizeof(declarative_test_effects));
        declarative_test_dirty_reasons = 0;
        char *invalid = declarative_test_run_config(command, "invalid");
        char expected_error[256];
        snprintf(expected_error, sizeof(expected_error), FAILURE_MESSAGE "unknown value 'invalid' given to command '%s' for domain 'config'\n", command);
        if (strcmp(invalid, expected_error)) fprintf(stderr, "matrix invalid %s (%zu/%zu): [%s] expected [%s]\n", command, strlen(invalid), strlen(expected_error), invalid, expected_error);
        TEST_CHECK(strcmp(invalid, expected_error), 0);
        char *after_invalid_query = declarative_test_run_config(command, NULL);
        TEST_CHECK(strcmp(before_query, after_invalid_query), 0);
        TEST_CHECK(memcmp(&ownership_before, &g_declarative_runtime_state.global[i], sizeof(ownership_before)), 0);
        for (int effect = 0; effect < DECLARATIVE_TEST_EFFECT_COUNT; ++effect) TEST_CHECK(declarative_test_effects[effect], 0);
        free(before_query);
        free(after_invalid_query);
        free(invalid);

        memset(declarative_test_effects, 0, sizeof(declarative_test_effects));
        char *mutation = declarative_test_run_config(command, test->value);
        if (strcmp(mutation, "")) fprintf(stderr, "matrix mutation %s: [%s]\n", command, mutation);
        TEST_CHECK(strcmp(mutation, ""), 0);
        free(mutation);
        char *query = declarative_test_run_config(command, NULL);
        if (strcmp(query, test->query)) fprintf(stderr, "matrix query %s: [%s] expected [%s]\n", command, query, test->query);
        TEST_CHECK(strcmp(query, test->query), 0);
        free(query);
        TEST_CHECK(g_declarative_runtime_state.global[i].present, true);
        TEST_CHECK(g_declarative_runtime_state.global[i].source, DECLARATIVE_SETTING_RUNTIME_GLOBAL);
        for (int effect = 0; effect < DECLARATIVE_TEST_EFFECT_COUNT; ++effect) {
            TEST_CHECK(declarative_test_effects[effect], effect == test->effect ? 1 : 0);
        }
        if (i == DECLARATIVE_CONFIG_DISPLAY_ORDER) TEST_CHECK(declarative_test_dirty_reasons, MENU_BAR_DIRTY_CONFIG | MENU_BAR_DIRTY_DISPLAY);
    }
    declarative_runtime_reset_operation_ops();
    declarative_runtime_reset_animation_permission_ops();
)

TEST_FUNC(declarative_window_focus_fallback_scoped_command_rejects_before_mutation,
    uint64_t saved_last_space_id = g_space_manager.last_space_id;
    enum window_focus_fallback saved_policy = g_window_manager.window_focus_fallback;
    struct declarative_runtime_cell saved_ownership = g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK];
    g_space_manager.last_space_id = UINT64_MAX - 60;
    g_window_manager.window_focus_fallback = WINDOW_FOCUS_FALLBACK_OFF;
    memset(&g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK], 0,
           sizeof(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK]));
    char *output = declarative_test_run_scoped_focus_fallback();
    TEST_CHECK(strcmp(output, FAILURE_MESSAGE "command 'window_focus_fallback' for domain 'config' is global-only.\n"), 0);
    TEST_CHECK(g_window_manager.window_focus_fallback, WINDOW_FOCUS_FALLBACK_OFF);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK].present, false);
    free(output);
    g_space_manager.last_space_id = saved_last_space_id;
    g_window_manager.window_focus_fallback = saved_policy;
    g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK] = saved_ownership;
)

TEST_FUNC(declarative_runtime_live_matrix_covers_all_37_typed_setters,
    TEST_CHECK(DECLARATIVE_CONFIG_COUNT, 37);
    bool initialized_application_table = false;
    if (!g_window_manager.application.buckets) {
        table_init(&g_window_manager.application, 23, hash_wm, compare_wm);
        initialized_application_table = true;
    }
    if (!g_space_manager.view.buckets) table_init(&g_space_manager.view, 23, hash_view, compare_view);
    uint64_t sid = UINT64_MAX - 20;
    struct view *view = calloc(1, sizeof(*view));
    view->sid = sid;
    struct declarative_animation_permission_ops permissions = { declarative_test_permission_yes, declarative_test_permission_yes, declarative_test_permission_request };
    declarative_runtime_set_animation_permission_ops(&permissions);
    declarative_runtime_set_operation_ops(&declarative_test_operation_ops);
    declarative_test_operation_success = true;
    g_window_manager.focused_window_id = 0;
    declarative_runtime_reset_ownership();
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        struct declarative_value value;
        memset(&value, 0xa5, sizeof(value));
        value.present = true;
        struct declarative_value typed = declarative_test_value(i);
        switch (declarative_config_descriptors[i].kind) {
        case DECLARATIVE_VALUE_BOOL: value.boolean = typed.boolean; break;
        case DECLARATIVE_VALUE_ENUM: value.enumeration = typed.enumeration; break;
        case DECLARATIVE_VALUE_INT: value.integer = typed.integer; break;
        case DECLARATIVE_VALUE_UINT: value.u32 = typed.u32; break;
        case DECLARATIVE_VALUE_FLOAT: value.real = typed.real; break;
        case DECLARATIVE_VALUE_STRUCTURED: value.external_bar = typed.external_bar; break;
        }
        uint64_t target_sid = declarative_config_descriptors[i].scope == DECLARATIVE_SCOPE_SPACE ? sid : 0;
        if (declarative_config_descriptors[i].scope == DECLARATIVE_SCOPE_GLOBAL) {
            TEST_CHECK(declarative_apply_runtime_setting(i, &value, sid), DECLARATIVE_APPLY_INVALID_SCOPE);
        } else {
            TEST_CHECK(declarative_apply_runtime_setting(i, &value, 0), DECLARATIVE_APPLY_OK);
            TEST_CHECK(g_declarative_runtime_state.global[i].present, true);
            TEST_CHECK(g_declarative_runtime_state.global[i].source, DECLARATIVE_SETTING_RUNTIME_GLOBAL);
            table_add(&g_space_manager.view, &sid, view);
        }
        TEST_CHECK(declarative_apply_runtime_setting(i, &value, target_sid), DECLARATIVE_APPLY_OK);
        struct declarative_runtime_cell *cell;
        if (target_sid) {
            int field = declarative_space_field_for_id(i);
            TEST_CHECK(field >= 0, true);
            cell = &view->runtime_ownership[field];
            TEST_CHECK(cell->source, DECLARATIVE_SETTING_RUNTIME_SPACE);
        } else {
            cell = &g_declarative_runtime_state.global[i];
            TEST_CHECK(cell->source, DECLARATIVE_SETTING_RUNTIME_GLOBAL);
        }
        TEST_CHECK(cell->present, true);
        union declarative_runtime_scalar expected = declarative_test_expected_scalar(i, &value);
        TEST_CHECK(memcmp(&cell->value, &expected, sizeof(expected)), 0);
        if (target_sid) table_remove(&g_space_manager.view, &sid);
    }
    TEST_CHECK(declarative_test_ffm_calls > 0, true);
    TEST_CHECK(declarative_test_global_layout_calls > 0, true);
    TEST_CHECK(declarative_test_space_layout_calls > 0, true);
    TEST_CHECK(declarative_test_space_geometry_calls, 5);
    for (int easing = 0; easing < EASING_TYPE_COUNT; ++easing) {
        struct declarative_value value = { .present = true, .enumeration = easing };
        TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_ANIMATION_EASING, &value, 0), DECLARATIVE_APPLY_OK);
        TEST_CHECK(g_window_manager.window_animation_easing, easing);
    }
    free(view);
    if (initialized_application_table) {
        table_free(&g_window_manager.application);
        memset(&g_window_manager.application, 0, sizeof(g_window_manager.application));
    }
    declarative_runtime_reset_operation_ops();
    declarative_runtime_reset_animation_permission_ops();
)

TEST_FUNC(declarative_animation_preparation_never_prompts_and_preserves_candidate,
    struct declarative_prepared_settings prepared = {0};
    prepared.values[DECLARATIVE_CONFIG_ANIMATION_DURATION] = (struct declarative_value) { .present = true, .real = 0.25f };
    struct declarative_animation_permission_ops permissions = { declarative_test_permission_yes, declarative_test_permission_no, declarative_test_permission_request };
    declarative_test_permission_requests = 0;
    declarative_runtime_set_animation_permission_ops(&permissions);
    struct declarative_value replacement = { .present = true, .real = 0.5f };
    enum declarative_apply_result apply_result = DECLARATIVE_APPLY_OK;
    TEST_CHECK(declarative_prepare_setting(&prepared, DECLARATIVE_CONFIG_ANIMATION_DURATION, &replacement, &apply_result), false);
    TEST_CHECK(apply_result, DECLARATIVE_APPLY_SCREEN_CAPTURE_DENIED);
    TEST_CHECK(declarative_test_permission_requests, 0);
    TEST_CHECK(prepared.values[DECLARATIVE_CONFIG_ANIMATION_DURATION].real, 0.25f);
    declarative_runtime_reset_animation_permission_ops();
)

TEST_FUNC(declarative_runtime_scalar_application_records_ownership_after_success,
    declarative_runtime_reset_ownership();
    struct declarative_value debug = { .present = true, .boolean = true };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_DEBUG_OUTPUT, &debug, 0), DECLARATIVE_APPLY_OK);
    TEST_CHECK(g_verbose, true);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_DEBUG_OUTPUT].present, true);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_DEBUG_OUTPUT].source, DECLARATIVE_SETTING_RUNTIME_GLOBAL);
    struct declarative_value external = { .present = true, .external_bar = { .mode = EXTERNAL_BAR_MAIN, .top_padding = -7, .bottom_padding = 9 } };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_EXTERNAL_BAR, &external, 0), DECLARATIVE_APPLY_OK);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_EXTERNAL_BAR].value.external_bar.mode, EXTERNAL_BAR_MAIN);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_EXTERNAL_BAR].value.external_bar.top_padding, -7);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_EXTERNAL_BAR].value.external_bar.bottom_padding, 9);
    struct declarative_value invalid = { .present = true, .real = -1.0f };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_ANIMATION_DURATION, &invalid, 0), DECLARATIVE_APPLY_INVALID_VALUE);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_ANIMATION_DURATION].present, false);
)

TEST_FUNC(declarative_runtime_operation_failures_do_not_publish_targets_or_ownership,
    if (!g_space_manager.view.buckets) table_init(&g_space_manager.view, 23, hash_view, compare_view);
    uint64_t sid = UINT64_MAX - 30;
    struct view *view = calloc(1, sizeof(*view));
    view->sid = sid;
    view->layout = VIEW_FLOAT;
    table_add(&g_space_manager.view, &sid, view);
    declarative_runtime_reset_ownership();
    declarative_runtime_set_operation_ops(&declarative_test_operation_ops);
    declarative_test_operation_success = false;

    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_DISABLED);
    struct declarative_value ffm = { .present = true, .enumeration = FFM_AUTOFOCUS };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_FFM, &ffm, 0), DECLARATIVE_APPLY_OK);
    TEST_CHECK(g_window_manager.configured_ffm_mode, FFM_AUTOFOCUS);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_FFM].present, true);

    g_space_manager.layout = VIEW_FLOAT;
    struct declarative_value layout = { .present = true, .enumeration = VIEW_BSP };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_LAYOUT, &layout, 0), DECLARATIVE_APPLY_LAYOUT_FAILED);
    TEST_CHECK(g_space_manager.layout, VIEW_FLOAT);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_LAYOUT].present, false);

    struct declarative_runtime_cell space_before = view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT];
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_LAYOUT, &layout, sid), DECLARATIVE_APPLY_LAYOUT_FAILED);
    TEST_CHECK(view->layout, VIEW_FLOAT);
    TEST_CHECK(memcmp(&space_before, &view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT], sizeof(space_before)), 0);

    struct declarative_animation_permission_ops permissions = { declarative_test_permission_yes, declarative_test_permission_no, declarative_test_permission_request };
    declarative_runtime_set_animation_permission_ops(&permissions);
    g_window_manager.window_animation_duration = 0.75f;
    struct declarative_value animation = { .present = true, .real = 0.5f };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_ANIMATION_DURATION, &animation, 0), DECLARATIVE_APPLY_SCREEN_CAPTURE_DENIED);
    TEST_CHECK(g_window_manager.window_animation_duration, 0.75f);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_ANIMATION_DURATION].present, false);

    table_remove(&g_space_manager.view, &sid);
    free(view);
    declarative_test_operation_success = true;
    declarative_runtime_reset_operation_ops();
    declarative_runtime_reset_animation_permission_ops();
)

TEST_FUNC(declarative_space_layout_message_surfaces_use_single_authority_and_preserve_failures,
    bool initialized_application_table = false;
    if (!g_window_manager.application.buckets) {
        table_init(&g_window_manager.application, 23, hash_wm, compare_wm);
        initialized_application_table = true;
    }
    if (!g_space_manager.view.buckets) table_init(&g_space_manager.view, 23, hash_view, compare_view);
    uint64_t sid = UINT64_MAX - 41;
    struct view *view = calloc(1, sizeof(*view));
    view->sid = sid;
    table_add(&g_space_manager.view, &sid, view);
    uint64_t last_space_id_before = g_space_manager.last_space_id;
    g_space_manager.last_space_id = sid;
    declarative_runtime_set_operation_ops(&declarative_test_operation_ops);

    for (int config_surface = 0; config_surface <= 1; ++config_surface) {
        view->layout = VIEW_FLOAT;
        memset(&view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT], 0, sizeof(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT]));
        declarative_test_user_space = false;
        declarative_test_operation_success = true;
        declarative_test_space_is_user_calls = 0;
        declarative_test_space_layout_calls = 0;
        char *output = declarative_test_run_space_layout_surface(config_surface);
        if (strcmp(output, FAILURE_MESSAGE "cannot set layout for a macOS fullscreen space!\n")) fprintf(stderr, "layout surface %d non-user: [%s]\n", config_surface, output);
        TEST_CHECK(strcmp(output, FAILURE_MESSAGE "cannot set layout for a macOS fullscreen space!\n"), 0);
        TEST_CHECK(declarative_test_space_is_user_calls, 1);
        TEST_CHECK(declarative_test_space_layout_calls, 0);
        TEST_CHECK(view->layout, VIEW_FLOAT);
        TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT].present, false);
        free(output);

        declarative_test_user_space = true;
        declarative_test_operation_success = false;
        declarative_test_space_is_user_calls = 0;
        declarative_test_space_layout_calls = 0;
        output = declarative_test_run_space_layout_surface(config_surface);
        if (strcmp(output, FAILURE_MESSAGE "could not set layout.\n")) fprintf(stderr, "layout surface %d failure: [%s]\n", config_surface, output);
        TEST_CHECK(strcmp(output, FAILURE_MESSAGE "could not set layout.\n"), 0);
        TEST_CHECK(declarative_test_space_is_user_calls, 1);
        TEST_CHECK(declarative_test_space_layout_calls, 1);
        TEST_CHECK(view->layout, VIEW_FLOAT);
        TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT].present, false);
        free(output);

        declarative_test_operation_success = true;
        declarative_test_space_is_user_calls = 0;
        declarative_test_space_layout_calls = 0;
        output = declarative_test_run_space_layout_surface(config_surface);
        if (strcmp(output, "")) fprintf(stderr, "layout surface %d success: [%s]\n", config_surface, output);
        TEST_CHECK(strcmp(output, ""), 0);
        TEST_CHECK(declarative_test_space_is_user_calls, 1);
        TEST_CHECK(declarative_test_space_layout_calls, 1);
        TEST_CHECK(view->layout, VIEW_BSP);
        TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT].present, true);
        TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT].source, DECLARATIVE_SETTING_RUNTIME_SPACE);
        free(output);
    }

    declarative_test_user_space = true;
    declarative_test_operation_success = true;
    declarative_runtime_reset_operation_ops();
    g_space_manager.last_space_id = last_space_id_before;
    table_remove(&g_space_manager.view, &sid);
    free(view);
    if (initialized_application_table) {
        table_free(&g_window_manager.application);
        memset(&g_window_manager.application, 0, sizeof(g_window_manager.application));
    }
)

TEST_FUNC(declarative_accordion_layouts_round_trip_through_imperative_surfaces,
    bool initialized_application_table = false;
    if (!g_window_manager.application.buckets) {
        table_init(&g_window_manager.application, 23, hash_wm, compare_wm);
        initialized_application_table = true;
    }
    if (!g_space_manager.view.buckets) table_init(&g_space_manager.view, 23, hash_view, compare_view);
    uint64_t sid = UINT64_MAX - 42;
    struct view *view = calloc(1, sizeof(*view));
    view->sid = sid; view->layout = VIEW_FLOAT;
    table_add(&g_space_manager.view, &sid, view);
    uint64_t last_space_id_before = g_space_manager.last_space_id;
    g_space_manager.last_space_id = sid;
    declarative_test_user_space = true;
    declarative_test_operation_success = true;
    declarative_runtime_set_operation_ops(&declarative_test_operation_ops);

    char *output = declarative_test_run_config("layout", "accordion_horizontal");
    TEST_CHECK(strcmp(output, ""), 0); free(output);
    TEST_CHECK(g_space_manager.layout, VIEW_ACCORDION_HORIZONTAL);
    output = declarative_test_run_config("layout", NULL);
    TEST_CHECK(strcmp(output, "accordion_horizontal\n"), 0); free(output);

    char scoped[] = "--space\0recent\0layout\0accordion_vertical\0";
    size_t output_length = 0; output = NULL;
    FILE *rsp = open_memstream(&output, &output_length);
    handle_domain_config(rsp, (struct token) { .text = "config", .length = 6 }, scoped);
    fclose(rsp);
    TEST_CHECK(strcmp(output, ""), 0); free(output);
    TEST_CHECK(view->layout, VIEW_ACCORDION_VERTICAL);

    char imperative[] = "recent\0--layout\0accordion_horizontal\0";
    output = NULL; output_length = 0; rsp = open_memstream(&output, &output_length);
    handle_domain_space(rsp, (struct token) { .text = "space", .length = 5 }, imperative);
    fclose(rsp);
    TEST_CHECK(strcmp(output, ""), 0); free(output);
    TEST_CHECK(view->layout, VIEW_ACCORDION_HORIZONTAL);
    TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer, VIEW_ACCORDION_HORIZONTAL);

    output = declarative_test_run_config("accordion_padding", "37");
    TEST_CHECK(strcmp(output, ""), 0); free(output);
    TEST_CHECK(g_space_manager.accordion_padding, 37);
    output = declarative_test_run_config("accordion_padding", NULL);
    TEST_CHECK(strcmp(output, "37\n"), 0); free(output);
    output = declarative_test_run_config("accordion_padding", "-1");
    TEST_CHECK(strncmp(output, FAILURE_MESSAGE, strlen(FAILURE_MESSAGE)), 0); free(output);

    declarative_runtime_reset_operation_ops();
    g_space_manager.last_space_id = last_space_id_before;
    table_remove(&g_space_manager.view, &sid);
    free(view);
    if (initialized_application_table) {
        table_free(&g_window_manager.application);
        memset(&g_window_manager.application, 0, sizeof(g_window_manager.application));
    }
)

TEST_FUNC(declarative_non_user_space_layout_boundary_preserves_target_and_ownership,
    if (!g_space_manager.view.buckets) table_init(&g_space_manager.view, 23, hash_view, compare_view);
    uint64_t sid = UINT64_MAX - 40;
    struct view *view = calloc(1, sizeof(*view));
    view->sid = sid;
    view->layout = VIEW_FLOAT;
    table_add(&g_space_manager.view, &sid, view);
    declarative_runtime_set_operation_ops(&declarative_test_operation_ops);
    declarative_runtime_reset_ownership();
    declarative_test_user_space = false;
    int layout_calls_before = declarative_test_space_layout_calls;
    struct declarative_value layout = { .present = true, .enumeration = VIEW_BSP };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_LAYOUT, &layout, sid), DECLARATIVE_APPLY_NON_USER_SPACE);
    TEST_CHECK(view->layout, VIEW_FLOAT);
    TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT].present, false);
    TEST_CHECK(declarative_test_space_layout_calls, layout_calls_before);
    declarative_test_user_space = true;
    declarative_runtime_reset_operation_ops();
    table_remove(&g_space_manager.view, &sid);
    free(view);
)

TEST_FUNC(declarative_imperative_failures_preserve_target_and_ownership_with_exact_errors,
    declarative_runtime_reset_ownership();
    declarative_runtime_set_operation_ops(&declarative_test_operation_ops);
    declarative_test_operation_success = false;
    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_DISABLED);
    char *output = declarative_test_run_config("focus_follows_mouse", "autofocus");
    TEST_CHECK(strcmp(output, ""), 0);
    TEST_CHECK(g_window_manager.configured_ffm_mode, FFM_AUTOFOCUS);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_FFM].present, true);
    free(output);
    g_space_manager.layout = VIEW_FLOAT;
    output = declarative_test_run_config("layout", "bsp");
    TEST_CHECK(strcmp(output, FAILURE_MESSAGE "could not set layout.\n"), 0);
    TEST_CHECK(g_space_manager.layout, VIEW_FLOAT);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_LAYOUT].present, false);
    free(output);
    declarative_test_operation_success = true;
    struct declarative_animation_permission_ops permissions = { declarative_test_permission_no, declarative_test_permission_yes, declarative_test_permission_request };
    declarative_runtime_set_animation_permission_ops(&permissions);
    g_window_manager.window_animation_duration = 0.75f;
    output = declarative_test_run_config("window_animation_duration", "0.5");
    TEST_CHECK(strcmp(output, FAILURE_MESSAGE "command 'window_animation_duration' for domain 'config' requires System Integrity Protection to be partially disabled! ignoring request..\n"), 0);
    TEST_CHECK(g_window_manager.window_animation_duration, 0.75f);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_ANIMATION_DURATION].present, false);
    free(output);
    declarative_runtime_reset_operation_ops();
    declarative_runtime_reset_animation_permission_ops();
)

TEST_FUNC(declarative_imperative_animation_denial_requests_permission_with_exact_error,
    struct declarative_animation_permission_ops permissions = { declarative_test_permission_yes, declarative_test_permission_no, declarative_test_permission_request };
    declarative_test_permission_requests = 0;
    declarative_runtime_set_animation_permission_ops(&permissions);
    char *output = NULL;
    size_t output_length = 0;
    FILE *rsp = open_memstream(&output, &output_length);
    char message[] = "window_animation_duration\0" "0.5\0";
    struct token domain = { .text = "config", .length = 6 };
    handle_domain_config(rsp, domain, message);
    fclose(rsp);
    TEST_CHECK(strcmp(output, FAILURE_MESSAGE "command 'window_animation_duration' for domain 'config' requires Screen Recording permissions! ignoring request..\n"), 0);
    TEST_CHECK(declarative_test_permission_requests, 1);
    free(output);
    declarative_runtime_reset_animation_permission_ops();
)

TEST_FUNC(declarative_per_space_ownership_follows_retained_view_identity,
    if (!g_space_manager.view.buckets) table_init(&g_space_manager.view, 23, hash_view, compare_view);
    uint64_t first_sid = UINT64_MAX - 10;
    uint64_t remapped_sid = UINT64_MAX - 11;
    struct view *view = calloc(1, sizeof(*view));
    view->sid = first_sid;
    table_add(&g_space_manager.view, &first_sid, view);
    struct declarative_value split = { .present = true, .enumeration = SPLIT_X };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_SPLIT_TYPE, &split, first_sid), DECLARATIVE_APPLY_OK);
    TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].present, true);
    TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].source, DECLARATIVE_SETTING_RUNTIME_SPACE);
    TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].present, false);
    table_remove(&g_space_manager.view, &first_sid);
    view->sid = remapped_sid;
    table_add(&g_space_manager.view, &remapped_sid, view);
    TEST_CHECK(space_manager_query_view(&g_space_manager, remapped_sid) == view, true);
    TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].present, true);
    TEST_CHECK(view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].present, false);
    table_remove(&g_space_manager.view, &remapped_sid);
    free(view);
)

TEST_FUNC(declarative_menu_startup_staging_is_silent_and_sealed,
    memset(&g_menu_bar_manager, 0, sizeof(g_menu_bar_manager));
    menu_bar_manager_startup_staging_is_open = false;
    TEST_CHECK(menu_bar_manager_startup_stage_enabled(MENU_BAR_ENABLED), false);
    TEST_CHECK(menu_bar_manager_startup_stage_style(MENU_BAR_STYLE_EXPANDED), false);
    TEST_CHECK(menu_bar_manager_startup_stage_space_filter(MENU_BAR_SPACE_FILTER_ALL), false);
    menu_bar_manager_startup_staging_open();
    TEST_CHECK(menu_bar_manager_startup_stage_enabled(MENU_BAR_ENABLED), true);
    TEST_CHECK(menu_bar_manager_startup_stage_style(MENU_BAR_STYLE_EXPANDED), true);
    TEST_CHECK(menu_bar_manager_startup_stage_space_filter(MENU_BAR_SPACE_FILTER_ALL), true);
    TEST_CHECK(g_menu_bar_manager.settings.enabled, MENU_BAR_ENABLED);
    TEST_CHECK(g_menu_bar_manager.settings.style, MENU_BAR_STYLE_EXPANDED);
    TEST_CHECK(g_menu_bar_manager.settings.space_filter, MENU_BAR_SPACE_FILTER_ALL);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0U);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.last_snapshot == NULL, true);
    TEST_CHECK(g_menu_bar_manager.generation, 0ULL);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, 0U);
    TEST_CHECK(menu_bar_manager_startup_stage_style((enum menu_bar_style) 99), false);
    menu_bar_manager_startup_staging_seal();
    TEST_CHECK(menu_bar_manager_startup_stage_enabled(MENU_BAR_DISABLED), false);
    TEST_CHECK(g_menu_bar_manager.settings.enabled, MENU_BAR_ENABLED);
    TEST_CHECK(menu_bar_manager_set_style(MENU_BAR_STYLE_COMPACT), true);
    TEST_CHECK(g_menu_bar_manager.settings.style, MENU_BAR_STYLE_COMPACT);
)
