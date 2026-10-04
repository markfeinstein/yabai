static size_t declarative_install_test_success_allocations;
static size_t declarative_install_test_mutex_init_calls;
static size_t declarative_install_test_mutex_destroy_calls;

static int declarative_install_test_mutex_init_counting(pthread_mutex_t *mutex,
                                                        const pthread_mutexattr_t *attributes)
{
    ++declarative_install_test_mutex_init_calls;
    return pthread_mutex_init(mutex, attributes);
}

static int declarative_install_test_mutex_init_failure(pthread_mutex_t *mutex,
                                                       const pthread_mutexattr_t *attributes)
{
    (void) mutex;
    (void) attributes;
    ++declarative_install_test_mutex_init_calls;
    return EAGAIN;
}

static int declarative_install_test_mutex_init_unexpected(pthread_mutex_t *mutex,
                                                          const pthread_mutexattr_t *attributes)
{
    (void) mutex;
    (void) attributes;
    ++declarative_install_test_mutex_init_calls;
    return EBUSY;
}

static int declarative_install_test_mutex_destroy_counting(pthread_mutex_t *mutex)
{
    ++declarative_install_test_mutex_destroy_calls;
    return pthread_mutex_destroy(mutex);
}

#define DECLARATIVE_INSTALL_TEST_WINDOW_MANAGER_FIELD_LIST(X) \
    X(AXUIElementRef, system_element) \
    X(struct table, application) \
    X(struct table, window) \
    X(struct table, managed_window) \
    X(struct table, window_lost_focused_event) \
    X(struct table, application_lost_front_switched_event) \
    X(struct table, window_animations_table) \
    X(struct table, insert_feedback) \
    X(struct rule *, rules) \
    X(struct application **, applications_to_refresh) \
    X(int, application_refresh_cursor) \
    X(uint32_t, focused_window_id) \
    X(ProcessSerialNumber, focused_window_psn) \
    X(uint32_t, last_window_id) \
    X(bool, enable_mff) \
    X(enum ffm_mode, configured_ffm_mode) \
    X(ffm_atomic_mode, effective_ffm_mode) \
    X(uint32_t, ffm_menu_suspend_count) \
    X(enum purify_mode, purify_mode) \
    X(enum window_origin_mode, window_origin_mode) \
    X(enum window_focus_fallback, window_focus_fallback) \
    X(bool, enable_window_opacity) \
    X(float, menubar_opacity) \
    X(float, active_window_opacity) \
    X(float, normal_window_opacity) \
    X(float, window_opacity_duration) \
    X(float, window_animation_duration) \
    X(int, window_animation_easing) \
    X(struct rgba_color, insert_feedback_color) \
    X(struct scratchpad *, scratchpad_window)

struct declarative_install_test_window_manager_state
{
#define DECLARATIVE_INSTALL_TEST_DECLARE(type, name) type name;
    DECLARATIVE_INSTALL_TEST_WINDOW_MANAGER_FIELD_LIST(DECLARATIVE_INSTALL_TEST_DECLARE)
#undef DECLARATIVE_INSTALL_TEST_DECLARE
};

static struct declarative_install_test_window_manager_state
    declarative_install_test_window_manager_capture(const struct window_manager *wm)
{
    struct declarative_install_test_window_manager_state state = {0};
#define DECLARATIVE_INSTALL_TEST_CAPTURE(type, name) state.name = wm->name;
    DECLARATIVE_INSTALL_TEST_WINDOW_MANAGER_FIELD_LIST(DECLARATIVE_INSTALL_TEST_CAPTURE)
#undef DECLARATIVE_INSTALL_TEST_CAPTURE
    return state;
}

static void declarative_install_test_window_manager_clear_nonmutex(struct window_manager *wm)
{
#define DECLARATIVE_INSTALL_TEST_CLEAR(type, name) wm->name = (type) {0};
    DECLARATIVE_INSTALL_TEST_WINDOW_MANAGER_FIELD_LIST(DECLARATIVE_INSTALL_TEST_CLEAR)
#undef DECLARATIVE_INSTALL_TEST_CLEAR
}

static void declarative_install_test_window_manager_restore(
    struct window_manager *wm, const struct declarative_install_test_window_manager_state *state)
{
#define DECLARATIVE_INSTALL_TEST_RESTORE(type, name) wm->name = state->name;
    DECLARATIVE_INSTALL_TEST_WINDOW_MANAGER_FIELD_LIST(DECLARATIVE_INSTALL_TEST_RESTORE)
#undef DECLARATIVE_INSTALL_TEST_RESTORE
}

struct declarative_install_test_context
{
    struct declarative_startup_state *startup;
    const struct declarative_topology_capture_ops *ops;
};

static bool declarative_install_test_bootstrap(void *context)
{
    struct declarative_install_test_context *test = context;
    return declarative_startup_install(test->startup, test->ops);
}

static CFStringRef declarative_install_test_copy_space_uuid(uint64_t sid, void *context)
{
    (void) context;
    char uuid[64];
    snprintf(uuid, sizeof(uuid), "created-space-%llu", (unsigned long long) sid);
    return CFStringCreateWithCString(NULL, uuid, kCFStringEncodingUTF8);
}

static bool declarative_install_test_space_is_user(uint64_t sid, void *context)
{
    (void) sid;
    (void) context;
    return true;
}

static void declarative_install_test_update_created_view(struct view *view, void *context)
{
    (void) context;
    view->root->area = (struct area) { .x = 10.0f, .y = 20.0f, .w = 800.0f, .h = 600.0f };
}

static bool declarative_install_test_candidate_value_applied(
    const struct declarative_startup_bundle *bundle, enum declarative_config_id id,
    const struct declarative_value *value)
{
    switch (id) {
    case DECLARATIVE_CONFIG_DEBUG_OUTPUT: return bundle->verbose == value->boolean;
    case DECLARATIVE_CONFIG_MFF: return bundle->window_manager.enable_mff == value->boolean;
    case DECLARATIVE_CONFIG_FFM: return bundle->window_manager.configured_ffm_mode == value->enumeration;
    case DECLARATIVE_CONFIG_DISPLAY_ORDER: return bundle->display_manager.order == value->enumeration;
    case DECLARATIVE_CONFIG_WINDOW_ORIGIN: return bundle->window_manager.window_origin_mode == value->enumeration;
    case DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK: return bundle->window_manager.window_focus_fallback == value->enumeration;
    case DECLARATIVE_CONFIG_WINDOW_PLACEMENT: return bundle->space_manager.window_placement == value->enumeration;
    case DECLARATIVE_CONFIG_WINDOW_INSERT_POINT: return bundle->space_manager.window_insertion_point == value->enumeration;
    case DECLARATIVE_CONFIG_WINDOW_ZOOM_PERSIST: return bundle->space_manager.window_zoom_persist == value->boolean;
    case DECLARATIVE_CONFIG_SKIP_SPACE_ANIMATION: return bundle->space_manager.skip_window_focus_animation == value->boolean;
    case DECLARATIVE_CONFIG_OPACITY: return bundle->window_manager.enable_window_opacity == value->boolean;
    case DECLARATIVE_CONFIG_OPACITY_DURATION: return bundle->window_manager.window_opacity_duration == value->real;
    case DECLARATIVE_CONFIG_ANIMATION_DURATION: return bundle->window_manager.window_animation_duration == value->real;
    case DECLARATIVE_CONFIG_ANIMATION_EASING: return bundle->window_manager.window_animation_easing == value->enumeration;
    case DECLARATIVE_CONFIG_SHADOW: return bundle->window_manager.purify_mode == value->enumeration;
    case DECLARATIVE_CONFIG_MENUBAR: return bundle->menu_settings.enabled == value->enumeration;
    case DECLARATIVE_CONFIG_MENUBAR_STYLE: return bundle->menu_settings.style == value->enumeration;
    case DECLARATIVE_CONFIG_MENUBAR_SPACE_FILTER: return bundle->menu_settings.space_filter == value->enumeration;
    case DECLARATIVE_CONFIG_MENUBAR_OPACITY: return bundle->window_manager.menubar_opacity == value->real;
    case DECLARATIVE_CONFIG_ACTIVE_WINDOW_OPACITY: return bundle->window_manager.active_window_opacity == value->real;
    case DECLARATIVE_CONFIG_NORMAL_WINDOW_OPACITY: return bundle->window_manager.normal_window_opacity == value->real;
    case DECLARATIVE_CONFIG_INSERT_FEEDBACK_COLOR: return bundle->window_manager.insert_feedback_color.p == value->u32;
    case DECLARATIVE_CONFIG_TOP_PADDING: return bundle->space_manager.top_padding == value->integer;
    case DECLARATIVE_CONFIG_BOTTOM_PADDING: return bundle->space_manager.bottom_padding == value->integer;
    case DECLARATIVE_CONFIG_LEFT_PADDING: return bundle->space_manager.left_padding == value->integer;
    case DECLARATIVE_CONFIG_RIGHT_PADDING: return bundle->space_manager.right_padding == value->integer;
    case DECLARATIVE_CONFIG_WINDOW_GAP: return bundle->space_manager.window_gap == value->integer;
    case DECLARATIVE_CONFIG_ACCORDION_PADDING: return bundle->space_manager.accordion_padding == value->integer;
    case DECLARATIVE_CONFIG_LAYOUT: return bundle->space_manager.layout == value->enumeration;
    case DECLARATIVE_CONFIG_SPLIT_RATIO: return bundle->space_manager.split_ratio == value->real;
    case DECLARATIVE_CONFIG_SPLIT_TYPE: return bundle->space_manager.split_type == value->enumeration;
    case DECLARATIVE_CONFIG_AUTO_BALANCE: return bundle->space_manager.auto_balance == value->u32;
    case DECLARATIVE_CONFIG_MOUSE_MOD: return bundle->mouse_state.modifier == value->enumeration;
    case DECLARATIVE_CONFIG_MOUSE_ACTION1: return bundle->mouse_state.action1 == value->enumeration;
    case DECLARATIVE_CONFIG_MOUSE_ACTION2: return bundle->mouse_state.action2 == value->enumeration;
    case DECLARATIVE_CONFIG_MOUSE_DROP_ACTION: return bundle->mouse_state.drop_action == value->enumeration;
    case DECLARATIVE_CONFIG_EXTERNAL_BAR:
        return bundle->display_manager.mode == value->external_bar.mode &&
               bundle->display_manager.top_padding == value->external_bar.top_padding &&
               bundle->display_manager.bottom_padding == value->external_bar.bottom_padding;
    default: return false;
    }
}

TEST_FUNC(declarative_startup_install_candidate_mapping_covers_all_settings,
{
    struct declarative_startup_bundle defaults = {0};
    TEST_CHECK(defaults.window_manager.window_focus_fallback, WINDOW_FOCUS_FALLBACK_OFF);
    for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) {
        struct declarative_startup_bundle bundle = {0};
        struct declarative_value value = { .present = true };
        switch (declarative_config_descriptors[id].kind) {
        case DECLARATIVE_VALUE_BOOL: value.boolean = true; break;
        case DECLARATIVE_VALUE_INT: value.integer = 7; break;
        case DECLARATIVE_VALUE_UINT: value.u32 = 7; break;
        case DECLARATIVE_VALUE_FLOAT: value.real = 0.25f; break;
        case DECLARATIVE_VALUE_ENUM: value.enumeration = 1; break;
        case DECLARATIVE_VALUE_STRUCTURED:
            value.external_bar = (struct declarative_external_bar) {
                .mode = EXTERNAL_BAR_MAIN, .top_padding = 7, .bottom_padding = 3
            };
            break;
        }
        declarative_install_apply_candidate_value(&bundle, id, &value);
        TEST_CHECK(declarative_install_test_candidate_value_applied(&bundle, id, &value), true);
    }
});

static struct declarative_config_model *declarative_install_test_model(void)
{
    static const char json[] =
        "{\"version\":1,\"config\":{\"layout\":\"bsp\",\"window_gap\":9,\"focus_follows_mouse\":\"autofocus\","
        "\"window_focus_fallback\":\"recent\",\"menubar\":\"on\",\"menubar_style\":\"expanded\",\"menubar_space_filter\":\"all\"},"
        "\"spaces\":[{\"match\":{\"index\":2},\"settings\":{\"layout\":\"accordion_vertical\","
        "\"top_padding\":1,\"bottom_padding\":2,\"left_padding\":3,\"right_padding\":4,"
        "\"window_gap\":5,\"accordion_padding\":27,\"split_type\":\"horizontal\",\"auto_balance\":\"x-axis\"}}],"
        "\"rules\":[{\"label\":\"native-once\",\"one_shot\":true,\"match\":{\"app\":{\"pattern\":\"^App$\"}},"
        "\"effects\":{\"display\":{\"index\":1},\"manage\":true}}]}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    if (!declarative_config_parse_bytes((const uint8_t *) json, strlen(json), "/tmp/install.jsonc", &model, &error)) return NULL;
    return model;
}

TEST_FUNC(declarative_startup_install_publishes_owned_generation_atomically,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct declarative_install_test_window_manager_state saved_window_manager =
        declarative_install_test_window_manager_capture(&g_window_manager);
    struct space_manager saved_space_manager = g_space_manager;
    struct display_manager saved_display_manager = g_display_manager;
    struct mouse_state saved_mouse_state = g_mouse_state;
    bool saved_verbose = g_verbose;
    struct menu_bar_manager_state saved_menu_manager = g_menu_bar_manager;

    g_event_loop = (struct event_loop) {0};
    declarative_install_test_window_manager_clear_nonmutex(&g_window_manager);
    g_space_manager = (struct space_manager) {0};
    g_display_manager = (struct display_manager) {0};
    g_mouse_state = (struct mouse_state) { .modifier = MOUSE_MOD_FN, .action1 = MOUSE_MODE_MOVE, .action2 = MOUSE_MODE_RESIZE, .drop_action = MOUSE_MODE_SWAP };
    g_menu_bar_manager = (struct menu_bar_manager_state) { .initialized = true };
    g_menu_bar_manager.settings = (struct menu_bar_settings) {
        .enabled = MENU_BAR_DISABLED, .style = MENU_BAR_STYLE_COMPACT,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };
    menu_bar_manager_startup_staging_open();
    declarative_install_test_reset();

    struct declarative_topology_fixture fixture;
    declarative_topology_test_fixture_init(&fixture, false);
    struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
    struct declarative_startup_state startup = {
        .model = declarative_install_test_model(), .present = true
    };
    snprintf(startup.path, sizeof(startup.path), "%s", "/tmp/install.jsonc");
    TEST_CHECK(startup.model != NULL, true);
    struct declarative_install_test_context context = { .startup = &startup, .ops = &ops };
    __auto_type saved_mutex_init = declarative_install_mutex_init;
    __auto_type saved_mutex_destroy = declarative_install_mutex_destroy;
    declarative_install_test_mutex_init_calls = 0;
    declarative_install_test_mutex_destroy_calls = 0;
    declarative_install_mutex_init = declarative_install_test_mutex_init_counting;
    declarative_install_mutex_destroy = declarative_install_test_mutex_destroy_counting;
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, declarative_install_test_bootstrap, &context), true);

    declarative_install_test_success_allocations = declarative_install_test_allocation_count();
    TEST_CHECK(declarative_install_test_success_allocations > 0, true);
    TEST_CHECK(declarative_installed_generation(), 1ULL);
    TEST_CHECK(declarative_installed_model() != NULL, true);
    TEST_CHECK(strcmp(declarative_installed_path(), "/tmp/install.jsonc"), 0);
    TEST_CHECK(startup.model == NULL, true);
    TEST_CHECK(startup.detached_views.count, 0U);
    TEST_CHECK(g_space_manager.view.count, 3);
    TEST_CHECK(g_space_manager.did_begin, true);
    TEST_CHECK(g_space_manager.layout, VIEW_BSP);
    TEST_CHECK(g_space_manager.window_gap, 9);
    TEST_CHECK(g_space_manager.accordion_padding, 30);
    uint64_t policy_sid = 12;
    struct view *policy_view = table_find(&g_space_manager.view, &policy_sid);
    TEST_CHECK(policy_view != NULL, true);
    TEST_CHECK(policy_view->layout, VIEW_ACCORDION_VERTICAL);
    TEST_CHECK(policy_view->accordion_padding, 27);
    TEST_CHECK(policy_view->top_padding, 1);
    TEST_CHECK(policy_view->bottom_padding, 2);
    TEST_CHECK(policy_view->left_padding, 3);
    TEST_CHECK(policy_view->right_padding, 4);
    TEST_CHECK(policy_view->window_gap, 5);
    TEST_CHECK(policy_view->split_type, SPLIT_X);
    TEST_CHECK(policy_view->auto_balance, SPLIT_X);
    TEST_CHECK(g_window_manager.configured_ffm_mode, FFM_AUTOFOCUS);
    TEST_CHECK(g_window_manager.window_focus_fallback, WINDOW_FOCUS_FALLBACK_RECENT);
    TEST_CHECK(g_menu_bar_manager.settings.enabled, MENU_BAR_ENABLED);
    TEST_CHECK(g_menu_bar_manager.settings.style, MENU_BAR_STYLE_EXPANDED);
    TEST_CHECK(g_menu_bar_manager.settings.space_filter, MENU_BAR_SPACE_FILTER_ALL);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0U);
    pthread_mutex_lock(&g_event_loop.queue.lock);
    uint32_t queued_after_publication = g_event_loop.queue.count;
    pthread_mutex_unlock(&g_event_loop.queue.lock);
    TEST_CHECK(queued_after_publication, 0U);
    TEST_CHECK(buf_len(g_window_manager.rules), 1);
    TEST_CHECK(rule_native_prefix_length(), 1U);
    TEST_CHECK(rule_native_label_is_protected("native-once"), true);

    struct rule_effects effects = {0};
    TEST_CHECK(rule_combine_rule_effects(&g_window_manager.rules[0], &effects, NULL), RULE_FOLD_OK);
    TEST_CHECK(effects.did, 1U);
    TEST_CHECK(effects.manage, RULE_PROP_ON);
    rule_set_flag(&g_window_manager.rules[0], RULE_ONE_SHOT_REMOVE);
    rule_cleanup_one_shots();
    TEST_CHECK(buf_len(g_window_manager.rules), 0);
    TEST_CHECK(rule_native_prefix_length(), 0U);
    TEST_CHECK(rule_native_label_is_protected("native-once"), true);
    struct rule collision = { .label = strdup("native-once") };
    TEST_CHECK(rule_add(&collision), RULE_ADD_PROTECTED_COLLISION);
    free(collision.label);
    TEST_CHECK(rule_remove_by_label("native-once"), RULE_REMOVE_PROTECTED_DENIED);
    TEST_CHECK(declarative_install_test_mutex_init_calls, 1U);
    TEST_CHECK(pthread_mutex_lock(&g_window_manager.window_animations_lock), 0);
    TEST_CHECK(pthread_mutex_unlock(&g_window_manager.window_animations_lock), 0);

    uint64_t removed_sid = 11;
    struct view *removed = table_find(&g_space_manager.view, &removed_sid);
    TEST_CHECK(removed != NULL, true);
    table_remove(&g_space_manager.view, &removed_sid);
    view_destroy(removed);
    free(removed);
    TEST_CHECK(table_find(&g_space_manager.view, &removed_sid) == NULL, true);
    struct view_test_platform_ops view_ops = {
        .update_windows = declarative_install_test_update_created_view,
        .copy_space_uuid = declarative_install_test_copy_space_uuid,
        .space_is_user = declarative_install_test_space_is_user
    };
    view_test_set_platform_ops(&view_ops);
    uint64_t created_sid = 99;
    struct view *created = space_manager_query_view(&g_space_manager, created_sid);
    view_test_set_platform_ops(NULL);
    TEST_CHECK(created != NULL, true);
    TEST_CHECK(created->sid, created_sid);
    TEST_CHECK(created->layout, VIEW_BSP);
    TEST_CHECK(created->window_gap, 9);
    TEST_CHECK(view_check_flag(created, VIEW_IS_VALID) != 0, true);
    TEST_CHECK(table_find(&g_space_manager.view, &created_sid), created);

    event_loop_end(&g_event_loop);
    declarative_install_test_reset();
    TEST_CHECK(declarative_install_test_mutex_destroy_calls, 1U);
    declarative_install_mutex_init = saved_mutex_init;
    declarative_install_mutex_destroy = saved_mutex_destroy;
    declarative_startup_destroy(&startup);
    declarative_topology_test_fixture_destroy(&fixture);
    menu_bar_manager_startup_staging_abort();
    g_menu_bar_manager = saved_menu_manager;
    g_verbose = saved_verbose;
    g_mouse_state = saved_mouse_state;
    g_display_manager = saved_display_manager;
    g_space_manager = saved_space_manager;
    declarative_install_test_window_manager_restore(&g_window_manager, &saved_window_manager);
    g_event_loop = saved_event_loop;
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_startup_install_failure_preserves_live_managers,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct declarative_install_test_window_manager_state saved_window_manager =
        declarative_install_test_window_manager_capture(&g_window_manager);
    struct space_manager saved_space_manager = g_space_manager;
    struct display_manager saved_display_manager = g_display_manager;
    struct mouse_state saved_mouse_state = g_mouse_state;
    struct menu_bar_manager_state saved_menu_manager = g_menu_bar_manager;
    __auto_type saved_mutex_init = declarative_install_mutex_init;
    __auto_type saved_mutex_destroy = declarative_install_mutex_destroy;

    TEST_CHECK(declarative_install_test_success_allocations > 0, true);

    g_event_loop = (struct event_loop) {0};
    declarative_install_test_window_manager_clear_nonmutex(&g_window_manager);
    g_window_manager.configured_ffm_mode = FFM_AUTORAISE;
    g_space_manager = (struct space_manager) { .layout = VIEW_STACK, .window_gap = 77 };
    g_display_manager = (struct display_manager) { .order = DISPLAY_ARRANGEMENT_ORDER_Y };
    g_mouse_state = (struct mouse_state) { .modifier = MOUSE_MOD_ALT };
    g_menu_bar_manager = (struct menu_bar_manager_state) { .initialized = true };
    g_menu_bar_manager.settings = (struct menu_bar_settings) {
        .enabled = MENU_BAR_DISABLED,
        .style = MENU_BAR_STYLE_COMPACT,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };
    menu_bar_manager_startup_staging_open();
    declarative_install_test_reset();

    struct declarative_topology_fixture closed_fixture;
    declarative_topology_test_fixture_init(&closed_fixture, false);
    struct declarative_topology_capture_ops closed_ops = declarative_topology_test_ops(&closed_fixture);
    struct declarative_startup_state closed_startup = { .model = declarative_install_test_model(), .present = true };
    snprintf(closed_startup.path, sizeof(closed_startup.path), "%s", "/tmp/install.jsonc");
    struct declarative_install_test_context closed_context = { .startup = &closed_startup, .ops = &closed_ops };
    menu_bar_manager_startup_staging_abort();
    declarative_install_test_mutex_init_calls = 0;
    declarative_install_mutex_init = declarative_install_test_mutex_init_unexpected;
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, declarative_install_test_bootstrap, &closed_context), false);
    TEST_CHECK(declarative_install_test_mutex_init_calls, 0U);
    TEST_CHECK(declarative_installed_generation(), 0ULL);
    TEST_CHECK(g_window_manager.configured_ffm_mode, FFM_AUTORAISE);
    TEST_CHECK(g_space_manager.layout, VIEW_STACK);
    TEST_CHECK(g_space_manager.window_gap, 77);
    TEST_CHECK(g_display_manager.order, DISPLAY_ARRANGEMENT_ORDER_Y);
    TEST_CHECK(g_mouse_state.modifier, MOUSE_MOD_ALT);
    TEST_CHECK(g_menu_bar_manager.settings.enabled, MENU_BAR_DISABLED);
    TEST_CHECK(g_menu_bar_manager.settings.style, MENU_BAR_STYLE_COMPACT);
    TEST_CHECK(g_menu_bar_manager.settings.space_filter, MENU_BAR_SPACE_FILTER_OCCUPIED);
    declarative_startup_destroy(&closed_startup);
    declarative_topology_test_fixture_destroy(&closed_fixture);

    menu_bar_manager_startup_staging_open();
    TEST_CHECK(pthread_mutex_init(&g_window_manager.window_animations_lock, NULL), 0);
    struct declarative_topology_fixture mutex_fixture;
    declarative_topology_test_fixture_init(&mutex_fixture, false);
    struct declarative_topology_capture_ops mutex_ops = declarative_topology_test_ops(&mutex_fixture);
    struct declarative_startup_state mutex_startup = { .model = declarative_install_test_model(), .present = true };
    snprintf(mutex_startup.path, sizeof(mutex_startup.path), "%s", "/tmp/install.jsonc");
    struct declarative_install_test_context mutex_context = { .startup = &mutex_startup, .ops = &mutex_ops };
    declarative_install_test_mutex_init_calls = 0;
    declarative_install_mutex_init = declarative_install_test_mutex_init_failure;
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, declarative_install_test_bootstrap, &mutex_context), false);
    TEST_CHECK(declarative_install_test_mutex_init_calls, 1U);
    TEST_CHECK(declarative_installed_generation(), 0ULL);
    TEST_CHECK(g_window_manager.configured_ffm_mode, FFM_AUTORAISE);
    TEST_CHECK(g_space_manager.layout, VIEW_STACK);
    TEST_CHECK(g_space_manager.window_gap, 77);
    TEST_CHECK(g_display_manager.order, DISPLAY_ARRANGEMENT_ORDER_Y);
    TEST_CHECK(g_mouse_state.modifier, MOUSE_MOD_ALT);
    TEST_CHECK(pthread_mutex_lock(&g_window_manager.window_animations_lock), 0);
    TEST_CHECK(pthread_mutex_unlock(&g_window_manager.window_animations_lock), 0);
    TEST_CHECK(pthread_mutex_destroy(&g_window_manager.window_animations_lock), 0);
    declarative_install_test_reset();
    declarative_startup_destroy(&mutex_startup);
    declarative_topology_test_fixture_destroy(&mutex_fixture);
    menu_bar_manager_startup_staging_abort();

    for (size_t ordinal = 1; ordinal <= declarative_install_test_success_allocations; ++ordinal) {
        g_event_loop = (struct event_loop) {0};
        declarative_install_test_window_manager_clear_nonmutex(&g_window_manager);
        g_window_manager.configured_ffm_mode = FFM_AUTORAISE;
        g_space_manager = (struct space_manager) { .layout = VIEW_STACK, .window_gap = 77 };
        g_display_manager = (struct display_manager) { .order = DISPLAY_ARRANGEMENT_ORDER_Y };
        g_mouse_state = (struct mouse_state) { .modifier = MOUSE_MOD_ALT };
        g_menu_bar_manager = (struct menu_bar_manager_state) { .initialized = true };
        menu_bar_manager_startup_staging_open();
        declarative_install_test_reset();
        TEST_CHECK(pthread_mutex_init(&g_window_manager.window_animations_lock, NULL), 0);

        struct declarative_topology_fixture fixture;
        declarative_topology_test_fixture_init(&fixture, false);
        struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
        struct declarative_startup_state startup = { .model = declarative_install_test_model(), .present = true };
        snprintf(startup.path, sizeof(startup.path), "%s", "/tmp/install.jsonc");
        declarative_install_test_fail_allocation_at(ordinal);
        declarative_install_test_mutex_init_calls = 0;
        declarative_install_mutex_init = declarative_install_test_mutex_init_unexpected;
        struct declarative_install_test_context context = { .startup = &startup, .ops = &ops };
        TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, declarative_install_test_bootstrap, &context), false);
        TEST_CHECK(declarative_install_test_mutex_init_calls, 0U);
        TEST_CHECK(declarative_installed_generation(), 0ULL);
        TEST_CHECK(g_window_manager.configured_ffm_mode, FFM_AUTORAISE);
        TEST_CHECK(g_space_manager.layout, VIEW_STACK);
        TEST_CHECK(g_space_manager.window_gap, 77);
        TEST_CHECK(g_display_manager.order, DISPLAY_ARRANGEMENT_ORDER_Y);
        TEST_CHECK(g_mouse_state.modifier, MOUSE_MOD_ALT);
        TEST_CHECK(pthread_mutex_lock(&g_window_manager.window_animations_lock), 0);
        TEST_CHECK(pthread_mutex_unlock(&g_window_manager.window_animations_lock), 0);
        TEST_CHECK(pthread_mutex_destroy(&g_window_manager.window_animations_lock), 0);

        declarative_install_test_reset();
        declarative_startup_destroy(&startup);
        declarative_topology_test_fixture_destroy(&fixture);
        menu_bar_manager_startup_staging_abort();
        TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
    }
    declarative_install_mutex_init = saved_mutex_init;
    declarative_install_mutex_destroy = saved_mutex_destroy;
    g_menu_bar_manager = saved_menu_manager;
    g_mouse_state = saved_mouse_state;
    g_display_manager = saved_display_manager;
    g_space_manager = saved_space_manager;
    declarative_install_test_window_manager_restore(&g_window_manager, &saved_window_manager);
    g_event_loop = saved_event_loop;
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
});
