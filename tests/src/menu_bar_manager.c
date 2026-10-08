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

static uint64_t menu_bar_manager_test_display_space_id(uint32_t did)
{
    return did == 1 ? 42 : 0;
}

static int menu_bar_manager_test_space_type(int cid, uint64_t sid)
{
    (void) cid;
    return sid == 42 ? 0 : 1;
}

static int menu_bar_manager_test_mission_control_index(uint64_t sid)
{
    return sid == 42 ? 1 : 0;
}

static uint64_t *menu_bar_manager_test_window_space_list(uint32_t wid, int *count)
{
    static uint64_t spaces[] = { 42 };
    if (wid != 1001) {
        if (count) *count = 0;
        return NULL;
    }

    if (count) *count = 1;
    return spaces;
}

static uint32_t *menu_bar_manager_test_space_window_list(uint64_t sid, int *count,
                                                         bool include_minimized, bool *success)
{
    static uint32_t windows[] = { 1001 };
    (void) include_minimized;

    if (g_menu_bar_manager_test_raw_queries_fail || sid != 42) {
        if (count) *count = 0;
        if (success) *success = false;
        return NULL;
    }

    if (success) *success = true;
    if (!g_menu_bar_manager_test_raw_contains_window) {
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

static int menu_bar_manager_test_space_type_call_count;

static uint64_t *menu_bar_manager_test_unstable_display_space_list(uint32_t did, int *count)
{
    static uint64_t spaces[] = { 42, 43 };
    if (did != 1) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = 2;
    return spaces;
}

static int menu_bar_manager_test_unstable_space_type(int cid, uint64_t sid)
{
    (void) cid;
    ++menu_bar_manager_test_space_type_call_count;
    return sid == 42 ? 0 : 1;
}

TEST_FUNC(menu_bar_manager_samples_each_space_type_once,
{
    menu_bar_manager_test_reset();
    g_menu_bar_manager_native.display_space_list = menu_bar_manager_test_unstable_display_space_list;
    g_menu_bar_manager_native.display_space_id = menu_bar_manager_test_display_space_id;
    g_menu_bar_manager_native.space_type = menu_bar_manager_test_unstable_space_type;
    g_menu_bar_manager_native.mission_control_index = menu_bar_manager_test_mission_control_index;

    struct menu_bar_display_builder display = {0};
    menu_bar_manager_test_space_type_call_count = 0;
    TEST_CHECK(menu_bar_manager_collect_spaces_for_display(&display, 1, 0), true);
    TEST_CHECK(menu_bar_manager_test_space_type_call_count, 2);
    TEST_CHECK(display.input.space_count, 1U);
    TEST_CHECK(display.space_inputs[0].space_id, 42ULL);

    free((void *) display.input.uuid);
    free(display.space_builders);
    free(display.space_inputs);
    menu_bar_manager_test_reset();
});

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
    g_menu_bar_manager_native.display_space_id = menu_bar_manager_test_display_space_id;
    g_menu_bar_manager_native.space_type = menu_bar_manager_test_space_type;
    g_menu_bar_manager_native.mission_control_index = menu_bar_manager_test_mission_control_index;
    g_menu_bar_manager_native.space_window_list = menu_bar_manager_test_space_window_list;
    g_menu_bar_manager_native.window_space_list = menu_bar_manager_test_window_space_list;
    g_menu_bar_manager_native.window_is_sticky = menu_bar_manager_test_window_is_sticky;
    g_menu_bar_manager_native.space_is_fullscreen = menu_bar_manager_test_space_is_fullscreen;
}

static void menu_bar_manager_test_begin_queue(void)
{
    event_queue_init(&g_event_loop.queue, 16);
    sem_init(&g_menu_bar_manager_test_sem, 0, 0);
    g_event_loop.semaphore = &g_menu_bar_manager_test_sem;
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
}

static void menu_bar_manager_test_end_queue(void)
{
    event_loop_close_producer_admission(&g_event_loop);
    struct event event;
    while (event_queue_pop(&g_event_loop.queue, &event)) {
        event_loop_release_event_context(event.type, event.context);
    }
    pthread_mutex_destroy(&g_event_loop.queue.lock);
    free(g_event_loop.queue.events);
    g_event_loop.queue = (struct event_queue) { 0 };
    sem_destroy(&g_menu_bar_manager_test_sem);
    g_event_loop.semaphore = NULL;
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    g_event_loop.producer_sync_initialized = false;
    __atomic_store_n(&g_event_loop.producer_admission, 0, __ATOMIC_RELEASE);
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

struct menu_bar_manager_live_staging_test_context
{
    pthread_t first_worker_thread;
    pthread_t seal_worker_thread;
};

static bool menu_bar_manager_test_live_stage_dirty(void *context)
{
    struct menu_bar_manager_live_staging_test_context *test = context;
    test->first_worker_thread = pthread_self();
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_DISPLAY | MENU_BAR_DIRTY_WINDOW);
    return true;
}

static bool menu_bar_manager_test_live_stage_seal(void *context)
{
    struct menu_bar_manager_live_staging_test_context *test = context;
    test->seal_worker_thread = pthread_self();
    return menu_bar_manager_startup_staging_seal();
}

static bool menu_bar_manager_test_live_noop(void *context)
{
    (void) context;
    return true;
}

TEST_FUNC(menu_bar_manager_live_worker_startup_seal_is_ordered_and_single_refresh,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct display_manager saved_display_manager = g_display_manager;
    g_event_loop = (struct event_loop) {0};
    g_display_manager.current_display_id = 0;
    menu_bar_manager_test_reset();
    menu_bar_manager_test_install_native_seams();
    g_menu_bar_manager.initialized = true;
    g_menu_bar_manager.settings.enabled = MENU_BAR_ENABLED;
    g_menu_bar_manager.settings.style = MENU_BAR_STYLE_COMPACT;
    g_menu_bar_manager.settings.space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED;

    struct menu_bar_manager_live_staging_test_context test = {0};
    menu_bar_manager_startup_staging_open();
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, NULL, NULL), true);
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, menu_bar_manager_test_live_stage_dirty, &test), true);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_DISPLAY);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, menu_bar_manager_test_live_stage_seal, &test), true);
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, menu_bar_manager_test_live_noop, NULL), true);

    TEST_CHECK(pthread_equal(test.first_worker_thread, g_event_loop.thread), true);
    TEST_CHECK(pthread_equal(test.seal_worker_thread, g_event_loop.thread), true);
    TEST_CHECK(menu_bar_manager_startup_staging_is_open, false);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0U);
    TEST_CHECK(g_menu_bar_manager.generation, 1ULL);
    TEST_CHECK(g_menu_bar_manager.last_snapshot != NULL, true);

    event_loop_end(&g_event_loop);
    menu_bar_manager_test_reset();
    g_event_loop = saved_event_loop;
    g_display_manager = saved_display_manager;
});

TEST_FUNC(menu_bar_manager_startup_staging_coalesces_until_sealed_and_recovers,
{
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    g_menu_bar_manager.initialized = true;
    g_menu_bar_manager.settings.enabled = MENU_BAR_ENABLED;

    menu_bar_manager_startup_staging_open();
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_DISPLAY | MENU_BAR_DIRTY_WINDOW);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_DISPLAY);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);

    menu_bar_manager_refresh();
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_DISPLAY);
    TEST_CHECK(g_menu_bar_manager.generation, 0ULL);

    TEST_CHECK(menu_bar_manager_startup_staging_seal(), true);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, true);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons,
               MENU_BAR_DIRTY_STARTUP | MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_DISPLAY);
    TEST_CHECK(menu_bar_manager_startup_staging_seal(), false);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 1);

    g_menu_bar_manager.refresh_pending = false;
    g_menu_bar_manager.dirty_reasons = 0;
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_FOCUS);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_SPACE);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, true);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_FOCUS | MENU_BAR_DIRTY_SPACE);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 1);

    menu_bar_manager_startup_staging_open();
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_CONFIG);
    menu_bar_manager_startup_staging_abort();
    TEST_CHECK(menu_bar_manager_startup_staging_is_open, false);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0U);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);

    menu_bar_manager_startup_staging_open();
    g_menu_bar_manager.initialized = false;
    menu_bar_manager_shutdown();
    TEST_CHECK(menu_bar_manager_startup_staging_is_open, false);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
});

TEST_FUNC(menu_bar_manager_startup_staging_seal_reports_refresh_rejection,
{
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    g_menu_bar_manager.initialized = true;
    g_menu_bar_manager.settings.enabled = MENU_BAR_ENABLED;

    menu_bar_manager_startup_staging_open();
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
    event_loop_close_producer_admission(&g_event_loop);
    TEST_CHECK(menu_bar_manager_startup_staging_seal(), false);
    TEST_CHECK(menu_bar_manager_startup_staging_is_open, false);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_STARTUP);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
});

TEST_FUNC(menu_bar_manager_application_inventory_lifecycle_boundaries, {
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;
    int saved_window_notification_rebuild_count = g_window_notification_rebuild_count;

    workspace_set_macos_version(15);
    g_window_notification_rebuild_count = 0;
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(1, 1);

    // Terminating an application with no tracked windows must not dirty inventory.
    application_window_collection_update_inventory(0, 0);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);
    TEST_CHECK(g_window_notification_rebuild_count, 0);

    // An unresolved retry returning false with no progress must not dirty inventory.
    application_window_collection_update_inventory(2, 2);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);
    TEST_CHECK(g_window_notification_rebuild_count, 0);

    // A partial retry returning false can still add a tracked window and dirty inventory.
    application_window_collection_update_inventory(2, 3);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, true);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW);
    TEST_CHECK(g_window_notification_rebuild_count, 1);

    // Newer-than-listed macOS releases use the Tahoe notification path.
    workspace_set_macos_version(27);

    // A fully resolved retry returning true that adds a window also dirties and coalesces.
    application_window_collection_update_inventory(3, 4);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, true);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 1);
    TEST_CHECK(g_window_notification_rebuild_count, 2);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
    g_window_notification_rebuild_count = saved_window_notification_rebuild_count;
});

static bool g_scratchpad_test_inventory_succeeds;
static bool g_scratchpad_test_has_window_list;
static bool g_scratchpad_test_order_succeeds;
static bool g_scratchpad_test_refresh_changes_inventory;
static int g_scratchpad_test_window_list_calls;
static int g_scratchpad_test_order_calls;
static int g_scratchpad_test_refresh_calls;
static struct application *g_scratchpad_test_list_application;
static uint32_t *g_scratchpad_test_order_window_list;
static int g_scratchpad_test_order_window_count;
static struct space_manager *g_scratchpad_test_refresh_space_manager;
static uint32_t g_scratchpad_test_window_list[] = { 1001, 1002 };

static bool menu_bar_manager_test_scratchpad_window_list(struct application *application,
                                                          uint32_t **window_list, int *window_count)
{
    ++g_scratchpad_test_window_list_calls;
    g_scratchpad_test_list_application = application;
    *window_list = NULL;
    *window_count = 0;
    if (!g_scratchpad_test_inventory_succeeds) return false;
    if (!g_scratchpad_test_has_window_list) return true;

    *window_count = 2;
    *window_list = g_scratchpad_test_window_list;
    return true;
}

static bool menu_bar_manager_test_scratchpad_order_window_in(uint32_t *window_list, int window_count)
{
    ++g_scratchpad_test_order_calls;
    g_scratchpad_test_order_window_list = window_list;
    g_scratchpad_test_order_window_count = window_count;
    return g_scratchpad_test_order_succeeds;
}

static bool menu_bar_manager_test_scratchpad_refresh_application_windows(struct space_manager *space_manager)
{
    ++g_scratchpad_test_refresh_calls;
    g_scratchpad_test_refresh_space_manager = space_manager;
    return g_scratchpad_test_refresh_changes_inventory;
}

TEST_FUNC(window_manager_scratchpad_refresh_routes_inventory_changes, {
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;
    int saved_window_notification_rebuild_count = g_window_notification_rebuild_count;
    __auto_type saved_window_list = window_manager_scratchpad_existing_application_window_list;
    __auto_type saved_order_window_in = window_manager_scratchpad_order_window_in;
    __auto_type saved_refresh_application_windows = window_manager_scratchpad_refresh_application_windows;

    workspace_set_macos_version(15);
    g_window_notification_rebuild_count = 0;
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(1, 1);
    window_manager_scratchpad_existing_application_window_list = menu_bar_manager_test_scratchpad_window_list;
    window_manager_scratchpad_order_window_in = menu_bar_manager_test_scratchpad_order_window_in;
    window_manager_scratchpad_refresh_application_windows = menu_bar_manager_test_scratchpad_refresh_application_windows;
    g_scratchpad_test_inventory_succeeds = false;
    g_scratchpad_test_has_window_list = false;
    g_scratchpad_test_order_succeeds = false;
    g_scratchpad_test_refresh_changes_inventory = false;
    g_scratchpad_test_window_list_calls = 0;
    g_scratchpad_test_order_calls = 0;
    g_scratchpad_test_refresh_calls = 0;
    g_scratchpad_test_list_application = (struct application *) 1;
    g_scratchpad_test_order_window_list = NULL;
    g_scratchpad_test_order_window_count = 0;
    g_scratchpad_test_refresh_space_manager = NULL;

    TEST_CHECK(window_manager_scratchpad_recover_windows(), false);
    TEST_CHECK(g_scratchpad_test_window_list_calls, 1);
    TEST_CHECK(g_scratchpad_test_list_application == NULL, true);
    TEST_CHECK(g_scratchpad_test_order_calls, 0);
    TEST_CHECK(g_scratchpad_test_refresh_calls, 0);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);
    TEST_CHECK(g_window_notification_rebuild_count, 0);

    g_scratchpad_test_inventory_succeeds = true;
    TEST_CHECK(window_manager_scratchpad_recover_windows(), true);
    TEST_CHECK(g_scratchpad_test_window_list_calls, 2);
    TEST_CHECK(g_scratchpad_test_order_calls, 0);
    TEST_CHECK(g_scratchpad_test_refresh_calls, 0);

    g_scratchpad_test_has_window_list = true;
    TEST_CHECK(window_manager_scratchpad_recover_windows(), false);
    TEST_CHECK(g_scratchpad_test_window_list_calls, 3);
    TEST_CHECK(g_scratchpad_test_order_calls, 1);
    TEST_CHECK(g_scratchpad_test_order_window_list == g_scratchpad_test_window_list, true);
    TEST_CHECK(g_scratchpad_test_order_window_count, 2);
    TEST_CHECK(g_scratchpad_test_refresh_calls, 0);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);
    TEST_CHECK(g_window_notification_rebuild_count, 0);

    g_scratchpad_test_order_succeeds = true;
    TEST_CHECK(window_manager_scratchpad_recover_windows(), false);
    TEST_CHECK(g_scratchpad_test_window_list_calls, 4);
    TEST_CHECK(g_scratchpad_test_order_calls, 2);
    TEST_CHECK(g_scratchpad_test_refresh_calls, 1);
    TEST_CHECK(g_scratchpad_test_refresh_space_manager == &g_space_manager, true);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, 0);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 0);
    TEST_CHECK(g_window_notification_rebuild_count, 0);

    g_scratchpad_test_refresh_changes_inventory = true;
    TEST_CHECK(window_manager_scratchpad_recover_windows(), true);
    TEST_CHECK(g_scratchpad_test_window_list_calls, 5);
    TEST_CHECK(g_scratchpad_test_order_calls, 3);
    TEST_CHECK(g_scratchpad_test_refresh_calls, 2);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, true);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW);
    TEST_CHECK(menu_bar_manager_test_count_events(MENU_BAR_REFRESH), 1);
    TEST_CHECK(g_window_notification_rebuild_count, 1);

    window_manager_scratchpad_existing_application_window_list = saved_window_list;
    window_manager_scratchpad_order_window_in = saved_order_window_in;
    window_manager_scratchpad_refresh_application_windows = saved_refresh_application_windows;
    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
    g_window_notification_rebuild_count = saved_window_notification_rebuild_count;
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

static int menu_bar_manager_test_raw_capture_calls;
static uint32_t *menu_bar_manager_test_two_raw_windows(uint64_t sid, int *count,
                                                       bool include_minimized, bool *success)
{
    static uint32_t windows[] = {1001, 1002};
    (void) include_minimized;
    ++menu_bar_manager_test_raw_capture_calls;
    if (success) *success = sid == 42;
    if (count) *count = sid == 42 ? 2 : 0;
    return sid == 42 ? windows : NULL;
}

TEST_FUNC(menu_bar_manager_raw_membership_is_captured_once_per_space,
{
    menu_bar_manager_test_reset();
    g_menu_bar_manager_native.space_window_list = menu_bar_manager_test_two_raw_windows;
    struct table saved_windows = g_window_manager.window;
    g_window_manager.window = (struct table) {0};
    table_init(&g_window_manager.window, 8, hash_wm, compare_wm);

    struct application application = { .name = "Editor" };
    struct window first = { .application = &application, .id = 1001 };
    struct window second = { .application = &application, .id = 1002 };
    table_add(&g_window_manager.window, &first.id, &first);
    table_add(&g_window_manager.window, &second.id, &second);

    struct menu_bar_space_input input = {
        .space_id = 42,
        .is_visible = true,
        .membership = MENU_BAR_SPACE_MEMBERSHIP_EMPTY
    };
    struct menu_bar_space_builder builder = { .input = &input };
    struct menu_bar_display_builder display = {
        .input = { .spaces = &input, .space_count = 1 },
        .space_inputs = &input,
        .space_builders = &builder
    };

    menu_bar_manager_test_raw_capture_calls = 0;
    TEST_CHECK(menu_bar_manager_collect_raw_untracked(&display, 1), true);
    TEST_CHECK(menu_bar_manager_add_tracked_window_from_raw_membership(&display, 1, &first), true);
    TEST_CHECK(menu_bar_manager_add_tracked_window_from_raw_membership(&display, 1, &second), true);
    TEST_CHECK(menu_bar_manager_test_raw_capture_calls, 1);
    TEST_CHECK(builder.raw_window_count, 2U);

    free(builder.raw_window_ids);
    free(builder.windows);
    free((void *) input.apps);
    table_free(&g_window_manager.window);
    g_window_manager.window = saved_windows;
    menu_bar_manager_test_reset();
});

TEST_FUNC(menu_bar_manager_raw_and_window_membership_disagreement_is_unknown,
{
    struct application application = { .name = "Editor" };
    struct window window = { .application = &application, .id = 1001 };
    uint32_t raw_ids[] = {1001};
    uint64_t declared_spaces[] = {84};
    struct menu_bar_space_input inputs[2] = {
        { .space_id = 42 },
        { .space_id = 84 }
    };
    struct menu_bar_space_builder builders[2] = {
        { .input = &inputs[0], .raw_window_ids = raw_ids, .raw_window_count = 1, .raw_query_succeeded = true },
        { .input = &inputs[1], .raw_query_succeeded = true }
    };
    struct menu_bar_display_builder display = {
        .input = { .spaces = inputs, .space_count = 2 },
        .space_inputs = inputs,
        .space_builders = builders
    };

    TEST_CHECK(menu_bar_manager_reconcile_raw_membership(&display, 1, &window,
                                                          declared_spaces, 1), true);
    TEST_CHECK(builders[0].window_count, 1U);
    TEST_CHECK(builders[0].windows[0].flags & MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN,
               MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN);
    TEST_CHECK(builders[1].window_count, 1U);
    TEST_CHECK(builders[1].windows[0].flags & MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN,
               MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN);
    free(builders[0].windows);
    free(builders[1].windows);
});

TEST_FUNC(menu_bar_manager_tracked_membership_failure_uses_raw_or_unknown, {
    menu_bar_manager_test_reset();
    menu_bar_manager_test_install_native_seams();
    struct table saved_managed_window = g_window_manager.managed_window;
    g_window_manager.managed_window = (struct table) {0};
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

    uint32_t raw_window_ids[] = {1001};
    builders[0].raw_query_succeeded = true;
    builders[0].raw_window_ids = raw_window_ids;
    builders[0].raw_window_count = 1;
    builders[1].raw_query_succeeded = true;
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
    builders[0].raw_query_succeeded = false;
    builders[1].raw_query_succeeded = true;

    TEST_CHECK(menu_bar_manager_add_tracked_window_from_raw_membership(displays, 1, &window), true);
    menu_bar_manager_finalize_membership(displays, 1);
    TEST_CHECK(spaces[0].membership, MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN);
    TEST_CHECK(spaces[1].membership, MENU_BAR_SPACE_MEMBERSHIP_EMPTY);

    free(builders[0].windows);
    table_free(&g_window_manager.managed_window);
    memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window));
    g_window_manager.managed_window = saved_managed_window;
    menu_bar_manager_test_reset();
});
