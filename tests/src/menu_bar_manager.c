static void menu_bar_manager_test_reset(void);

TEST_FUNC(menu_bar_manager_config_value_parsing, {
    menu_bar_manager_test_reset();

    struct menu_bar_settings defaults = menu_bar_manager_settings();
    TEST_CHECK(defaults.enabled, MENU_BAR_DISABLED);
    TEST_CHECK(defaults.style, MENU_BAR_STYLE_EXPANDED);
    TEST_CHECK(defaults.space_filter, MENU_BAR_SPACE_FILTER_OCCUPIED);

    TEST_CHECK(strcmp(menu_bar_manager_style_string(MENU_BAR_STYLE_COMPACT), "compact"), 0);
    TEST_CHECK(strcmp(menu_bar_manager_style_string(MENU_BAR_STYLE_EXPANDED), "expanded"), 0);
    TEST_CHECK(strcmp(menu_bar_manager_style_string((enum menu_bar_style) -1), "expanded"), 0);

    enum menu_bar_enabled enabled = MENU_BAR_ENABLED;
    enum menu_bar_style style = MENU_BAR_STYLE_EXPANDED;
    enum menu_bar_space_filter filter = MENU_BAR_SPACE_FILTER_ALL;

    TEST_CHECK(menu_bar_manager_parse_enabled("off", 3, &enabled), true);
    TEST_CHECK(enabled, MENU_BAR_DISABLED);
    TEST_CHECK(menu_bar_manager_parse_enabled("on", 2, &enabled), true);
    TEST_CHECK(enabled, MENU_BAR_ENABLED);
    TEST_CHECK(menu_bar_manager_parse_enabled("enabled", 7, &enabled), false);
    TEST_CHECK(enabled, MENU_BAR_ENABLED);

    TEST_CHECK(menu_bar_manager_parse_style("compact", 7, &style), true);
    TEST_CHECK(style, MENU_BAR_STYLE_COMPACT);
    TEST_CHECK(menu_bar_manager_parse_style("expanded", 8, &style), true);
    TEST_CHECK(style, MENU_BAR_STYLE_EXPANDED);
    TEST_CHECK(menu_bar_manager_parse_style("wide", 4, &style), false);
    TEST_CHECK(style, MENU_BAR_STYLE_EXPANDED);

    TEST_CHECK(menu_bar_manager_parse_space_filter("occupied", 8, &filter), true);
    TEST_CHECK(filter, MENU_BAR_SPACE_FILTER_OCCUPIED);
    TEST_CHECK(menu_bar_manager_parse_space_filter("all", 3, &filter), true);
    TEST_CHECK(filter, MENU_BAR_SPACE_FILTER_ALL);
    TEST_CHECK(menu_bar_manager_parse_space_filter("empty", 5, &filter), false);
    TEST_CHECK(filter, MENU_BAR_SPACE_FILTER_ALL);

    menu_bar_manager_test_reset();
});

static sem_t g_menu_bar_manager_test_sem;
static bool g_menu_bar_manager_test_raw_contains_window;
static bool g_menu_bar_manager_test_raw_queries_fail;

static int menu_bar_manager_test_active_display_count(void)
{
    return 1;
}

static uint32_t menu_bar_manager_test_arrangement_display_id(int arrangement)
{
    return arrangement == 1 ? 1 : 0;
}

static uint64_t *menu_bar_manager_test_display_space_list(uint32_t did, int *count)
{
    static uint64_t spaces[] = { 42 };
    if (did != 1) {
        if (count) *count = 0;
        return NULL;
    }

    if (count) *count = 1;
    return spaces;
}

static int menu_bar_manager_test_space_type(int cid, uint64_t sid)
{
    (void) cid;
    return sid == 42 ? 0 : 1;
}

static uint32_t *menu_bar_manager_test_space_window_list(uint64_t sid, int *count, bool include_minimized)
{
    static uint32_t windows[] = { 1001 };
    (void) include_minimized;

    if (g_menu_bar_manager_test_raw_queries_fail || sid != 42 || !g_menu_bar_manager_test_raw_contains_window) {
        if (count) *count = 0;
        return NULL;
    }

    if (count) *count = 1;
    return windows;
}

static bool menu_bar_manager_test_window_is_sticky(uint32_t wid)
{
    (void) wid;
    return false;
}

static bool menu_bar_manager_test_space_is_fullscreen(uint64_t sid)
{
    (void) sid;
    return false;
}

static struct menu_bar_snapshot *menu_bar_manager_test_snapshot(uint64_t generation,
                                                                enum menu_bar_enabled enabled,
                                                                uint64_t sid)
{
    struct menu_bar_space_input spaces[] = {
        { sid, 1, NULL, false, true, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 }
    };
    struct menu_bar_display_input displays[] = {
        { 1, "display", 0, NULL, NULL, spaces, 1 }
    };
    struct menu_bar_settings settings = {
        .enabled = enabled,
        .style = MENU_BAR_STYLE_COMPACT,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };

    return menu_bar_model_snapshot_create(generation, settings, displays, sid ? 1 : 0, true, 1, "display", 0, NULL);
}

static void menu_bar_manager_test_reset(void)
{
    menu_bar_model_snapshot_destroy(g_menu_bar_manager.last_snapshot);
    g_menu_bar_manager.last_snapshot = NULL;
    menu_bar_manager_clear_action_failure();
    memset(&g_menu_bar_manager, 0, sizeof(g_menu_bar_manager));
    memset(&g_menu_bar_manager_native, 0, sizeof(g_menu_bar_manager_native));

    g_menu_bar_manager_test_raw_contains_window = false;
    g_menu_bar_manager_test_raw_queries_fail = false;
}

static void menu_bar_manager_test_install_native_seams(void)
{
    g_menu_bar_manager_native.active_display_count = menu_bar_manager_test_active_display_count;
    g_menu_bar_manager_native.arrangement_display_id = menu_bar_manager_test_arrangement_display_id;
    g_menu_bar_manager_native.display_space_list = menu_bar_manager_test_display_space_list;
    g_menu_bar_manager_native.space_type = menu_bar_manager_test_space_type;
    g_menu_bar_manager_native.space_window_list = menu_bar_manager_test_space_window_list;
    g_menu_bar_manager_native.window_is_sticky = menu_bar_manager_test_window_is_sticky;
    g_menu_bar_manager_native.space_is_fullscreen = menu_bar_manager_test_space_is_fullscreen;
}

static void menu_bar_manager_test_begin_queue(void)
{
    event_queue_init(&g_event_loop.queue, 16);
    sem_init(&g_menu_bar_manager_test_sem, 0, 0);
    g_event_loop.semaphore = &g_menu_bar_manager_test_sem;
}

static void menu_bar_manager_test_end_queue(void)
{
    struct event event;
    while (event_queue_pop(&g_event_loop.queue, &event)) {
        event_loop_release_event_context(event.type, event.context);
    }
    pthread_mutex_destroy(&g_event_loop.queue.lock);
    free(g_event_loop.queue.events);
    g_event_loop.queue = (struct event_queue) { 0 };
    sem_destroy(&g_menu_bar_manager_test_sem);
    g_event_loop.semaphore = NULL;
}

static int menu_bar_manager_test_count_events(enum event_type type)
{
    int count = 0;
    struct event event;
    while (event_queue_pop(&g_event_loop.queue, &event)) {
        if (event.type == type) ++count;
        event_loop_release_event_context(event.type, event.context);
    }
    return count;
}

static struct menu_bar_action *menu_bar_manager_test_action(enum menu_bar_action_type type, uint64_t generation)
{
    struct menu_bar_action *action = calloc(1, sizeof(struct menu_bar_action));
    action->type = type;
    action->generation = generation;
    return action;
}

static void menu_bar_manager_test_set_enabled_snapshot(uint64_t generation, uint64_t floor)
{
    g_menu_bar_manager.initialized = true;
    g_menu_bar_manager.settings = (struct menu_bar_settings) {
        .enabled = MENU_BAR_ENABLED,
        .style = MENU_BAR_STYLE_COMPACT,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };
    g_menu_bar_manager.generation = generation;
    g_menu_bar_manager.enabled_generation_floor = floor;
    g_menu_bar_manager.last_snapshot = menu_bar_manager_test_snapshot(generation, MENU_BAR_ENABLED, 42);
}

TEST_FUNC(menu_bar_manager_application_inventory_lifecycle_boundaries, {
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(1, 1);

    // Terminating an application with no tracked windows must not dirty inventory.
    if (application_window_collection_changed_inventory(0, 0)) {
        application_window_inventory_did_change();
    }
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);

    // An unresolved retry returning false with no progress must not dirty inventory.
    if (application_window_collection_changed_inventory(2, 2)) {
        application_window_inventory_did_change();
    }
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);

    // A partial retry returning false can still add a tracked window and dirty inventory.
    if (application_window_collection_changed_inventory(2, 3)) {
        application_window_inventory_did_change();
    }
    TEST_CHECK(g_menu_bar_manager.refresh_pending, true);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW);

    // A fully resolved retry returning true that adds a window also dirties and coalesces.
    if (application_window_collection_changed_inventory(3, 4)) {
        application_window_inventory_did_change();
    }
    TEST_CHECK(g_menu_bar_manager.refresh_pending, true);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 1);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
});

TEST_FUNC(menu_bar_manager_ignores_setting_actions_after_disable, {
    menu_bar_manager_test_reset();
    menu_bar_manager_test_install_native_seams();
    menu_bar_manager_test_set_enabled_snapshot(5, 5);

    TEST_CHECK(menu_bar_manager_set_enabled(MENU_BAR_DISABLED), true);

    struct menu_bar_action *style = menu_bar_manager_test_action(MENU_BAR_ACTION_SET_STYLE, 5);
    style->data.set_style.style = MENU_BAR_STYLE_EXPANDED;
    menu_bar_manager_handle_action(style);

    struct menu_bar_action *filter = menu_bar_manager_test_action(MENU_BAR_ACTION_SET_SPACE_FILTER, 5);
    filter->data.set_space_filter.filter = MENU_BAR_SPACE_FILTER_ALL;
    menu_bar_manager_handle_action(filter);

    TEST_CHECK(g_menu_bar_manager.settings.style, MENU_BAR_STYLE_COMPACT);
    TEST_CHECK(g_menu_bar_manager.settings.space_filter, MENU_BAR_SPACE_FILTER_OCCUPIED);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0);
    TEST_CHECK(g_menu_bar_manager.action_failure_text == NULL, true);

    menu_bar_manager_test_reset();
});

TEST_FUNC(menu_bar_manager_rejects_old_lifecycle_but_accepts_valid_setting_generation, {
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_install_native_seams();
    menu_bar_manager_test_set_enabled_snapshot(5, 5);

    TEST_CHECK(menu_bar_manager_set_enabled(MENU_BAR_DISABLED), true);
    TEST_CHECK(menu_bar_manager_set_enabled(MENU_BAR_ENABLED), true);
    uint64_t new_floor = g_menu_bar_manager.enabled_generation_floor;

    struct menu_bar_action *old_style = menu_bar_manager_test_action(MENU_BAR_ACTION_SET_STYLE, 5);
    old_style->data.set_style.style = MENU_BAR_STYLE_EXPANDED;
    menu_bar_manager_handle_action(old_style);
    TEST_CHECK(g_menu_bar_manager.settings.style, MENU_BAR_STYLE_COMPACT);

    struct menu_bar_action *old_open = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 5);
    old_open->data.menu_tracking.is_tracking = true;
    menu_bar_manager_handle_action(old_open);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_OPENED), 0);

    menu_bar_model_snapshot_destroy(g_menu_bar_manager.last_snapshot);
    g_menu_bar_manager.generation = new_floor + 2;
    g_menu_bar_manager.last_snapshot = menu_bar_manager_test_snapshot(g_menu_bar_manager.generation, MENU_BAR_ENABLED, 42);

    struct menu_bar_action *valid_old_filter = menu_bar_manager_test_action(MENU_BAR_ACTION_SET_SPACE_FILTER, new_floor);
    valid_old_filter->data.set_space_filter.filter = MENU_BAR_SPACE_FILTER_ALL;
    menu_bar_manager_handle_action(valid_old_filter);
    TEST_CHECK(g_menu_bar_manager.settings.space_filter, MENU_BAR_SPACE_FILTER_ALL);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
});

TEST_FUNC(menu_bar_manager_preserves_action_focus_during_async_menu_open, {
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(7, 7);

    // AppKit captured display 1 from its source snapshot before this action
    // crossed the event queue; native/current focus has since moved to display 2.
    g_display_manager.current_display_id = 2;
    struct menu_bar_action *open = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 7);
    open->data.menu_tracking.is_tracking = true;
    open->data.menu_tracking.focused_display_id = 1;
    menu_bar_manager_handle_action(open);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, 1);
    TEST_CHECK(g_menu_bar_manager.pre_menu_focused_display_id, 1);
    TEST_CHECK(menu_bar_manager_focused_display_id(), 1);

    struct menu_bar_action *close = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 7);
    close->data.menu_tracking.is_tracking = false;
    menu_bar_manager_handle_action(close);
    TEST_CHECK(g_menu_bar_manager.pre_menu_focused_display_id, 0);

    // Unresolved source focus must remain unresolved while tracking rather than
    // falling back to the now-current native display.
    struct menu_bar_action *unresolved = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 7);
    unresolved->data.menu_tracking.is_tracking = true;
    unresolved->data.menu_tracking.focused_display_id = 0;
    menu_bar_manager_handle_action(unresolved);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, 1);
    TEST_CHECK(menu_bar_manager_focused_display_id(), 0);

    close = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 7);
    close->data.menu_tracking.is_tracking = false;
    menu_bar_manager_handle_action(close);

    g_display_manager.current_display_id = 0;
    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
});

TEST_FUNC(menu_bar_manager_late_close_after_disable_does_not_overemit, {
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(3, 3);
    g_menu_bar_manager.menu_tracking_depth = 1;
    g_menu_bar_manager.pre_menu_focused_display_id = 1;

    TEST_CHECK(menu_bar_manager_set_enabled(MENU_BAR_DISABLED), true);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_CLOSED), 1);

    struct menu_bar_action *late_close = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 3);
    late_close->data.menu_tracking.is_tracking = false;
    menu_bar_manager_handle_action(late_close);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_CLOSED), 0);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, 0);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
});

TEST_FUNC(menu_bar_manager_tracked_membership_failure_uses_raw_or_unknown, {
    menu_bar_manager_test_reset();
    menu_bar_manager_test_install_native_seams();
    table_init(&g_window_manager.managed_window, 8, hash_wm, compare_wm);

    struct application application;
    memset(&application, 0, sizeof(application));
    application.name = "Editor";

    struct window window;
    memset(&window, 0, sizeof(window));
    window.application = &application;
    window.id = 1001;
    window.role = kAXWindowRole;
    window.subrole = kAXStandardWindowSubrole;

    struct menu_bar_space_input spaces[2];
    memset(spaces, 0, sizeof(spaces));
    spaces[0].space_id = 42;
    spaces[0].mission_control_index = 1;
    spaces[0].is_visible = true;
    spaces[0].membership = MENU_BAR_SPACE_MEMBERSHIP_EMPTY;
    spaces[1].space_id = 84;
    spaces[1].mission_control_index = 2;
    spaces[1].membership = MENU_BAR_SPACE_MEMBERSHIP_EMPTY;

    struct menu_bar_space_builder builders[2];
    memset(builders, 0, sizeof(builders));
    struct menu_bar_display_builder displays[1];
    memset(displays, 0, sizeof(displays));
    displays[0].input.display_id = 1;
    displays[0].input.spaces = spaces;
    displays[0].input.space_count = 2;
    displays[0].space_inputs = spaces;
    displays[0].space_builders = builders;
    builders[0].input = &spaces[0];
    builders[1].input = &spaces[1];

    g_menu_bar_manager_test_raw_contains_window = true;
    TEST_CHECK(menu_bar_manager_add_tracked_window_from_raw_membership(displays, 1, &window), true);
    menu_bar_manager_finalize_membership(displays, 1);
    TEST_CHECK(spaces[0].membership, MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED);
    TEST_CHECK(spaces[0].app_count, 1);
    TEST_CHECK(strcmp(spaces[0].apps[0].name, "Editor") == 0, true);
    TEST_CHECK(spaces[1].membership, MENU_BAR_SPACE_MEMBERSHIP_EMPTY);

    free(builders[0].windows);
    free((void *) spaces[0].apps);
    memset(builders, 0, sizeof(builders));
    builders[0].input = &spaces[0];
    builders[1].input = &spaces[1];
    spaces[0].apps = NULL;
    spaces[0].app_count = 0;
    spaces[0].membership = MENU_BAR_SPACE_MEMBERSHIP_EMPTY;
    spaces[1].membership = MENU_BAR_SPACE_MEMBERSHIP_EMPTY;
    g_menu_bar_manager_test_raw_contains_window = false;
    g_menu_bar_manager_test_raw_queries_fail = true;

    TEST_CHECK(menu_bar_manager_add_tracked_window_from_raw_membership(displays, 1, &window), true);
    menu_bar_manager_finalize_membership(displays, 1);
    TEST_CHECK(spaces[0].membership, MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN);
    TEST_CHECK(spaces[1].membership, MENU_BAR_SPACE_MEMBERSHIP_EMPTY);

    free(builders[0].windows);
    table_free(&g_window_manager.managed_window);
    menu_bar_manager_test_reset();
});
