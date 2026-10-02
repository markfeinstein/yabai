TEST_FUNC(ffm_configured_and_effective_state_handles_nested_suspension_and_latest_updates,
    struct window_manager wm = {0};
    window_manager_set_focus_follows_mouse(&wm, FFM_AUTOFOCUS);
    TEST_CHECK(wm.configured_ffm_mode, FFM_AUTOFOCUS);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_AUTOFOCUS);

    window_manager_ffm_menu_opened(&wm);
    window_manager_ffm_menu_opened(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, 2U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);

    window_manager_set_focus_follows_mouse(&wm, FFM_AUTORAISE);
    TEST_CHECK(wm.configured_ffm_mode, FFM_AUTORAISE);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);
    window_manager_ffm_menu_closed(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, 1U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);
    window_manager_ffm_menu_closed(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, 0U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_AUTORAISE);

    window_manager_ffm_menu_closed(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, 0U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_AUTORAISE);
)

TEST_FUNC(ffm_menu_suspension_saturates_and_extra_close_is_safe,
    struct window_manager wm = { .configured_ffm_mode = FFM_AUTOFOCUS, .ffm_menu_suspend_count = UINT32_MAX };
    window_manager_recompute_effective_ffm_mode(&wm);
    window_manager_ffm_menu_opened(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, UINT32_MAX);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);
    window_manager_ffm_menu_closed(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, UINT32_MAX - 1);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);

    wm.ffm_menu_suspend_count = 0;
    window_manager_ffm_menu_closed(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, 0U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_AUTOFOCUS);
)

TEST_FUNC(ffm_config_query_reports_configured_mode_while_effective_is_suspended,
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    uint32_t saved_suspend_count = g_window_manager.ffm_menu_suspend_count;
    g_window_manager.ffm_menu_suspend_count = 0;
    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTORAISE);
    window_manager_ffm_menu_opened(&g_window_manager);
    char *output = declarative_test_run_config("focus_follows_mouse", NULL);
    TEST_CHECK(strcmp(output, "autoraise\n"), 0);
    TEST_CHECK(window_manager_effective_ffm_mode(&g_window_manager), FFM_DISABLED);
    free(output);
    g_window_manager.configured_ffm_mode = saved_configured_mode;
    g_window_manager.ffm_menu_suspend_count = saved_suspend_count;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
)

static int ffm_test_handler_snapshot_loads;
static enum ffm_mode ffm_test_handler_snapshot_mode;
static void ffm_test_change_mode_after_snapshot(enum ffm_mode mode)
{
    ++ffm_test_handler_snapshot_loads;
    ffm_test_handler_snapshot_mode = mode;
    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTORAISE);
}

TEST_FUNC(ffm_moved_handler_uses_one_atomic_snapshot_during_mode_change,
    enum mission_control_mode saved_mission_control_mode = g_mission_control_mode;
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    uint32_t saved_suspend_count = g_window_manager.ffm_menu_suspend_count;
    g_window_manager.ffm_menu_suspend_count = 0;
    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTOFOCUS);
    g_mission_control_mode = MISSION_CONTROL_MODE_SHOW;
    ffm_test_handler_snapshot_loads = 0;
    ffm_test_handler_snapshot_mode = FFM_DISABLED;
    window_manager_test_effective_ffm_load_observer = ffm_test_change_mode_after_snapshot;
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, CGPointZero, kCGMouseButtonLeft);
    TEST_CHECK(event != NULL, true);
    if (event) EVENT_HANDLER_MOUSE_MOVED(event, 0);
    window_manager_test_effective_ffm_load_observer = NULL;
    TEST_CHECK(ffm_test_handler_snapshot_loads, 1);
    TEST_CHECK(ffm_test_handler_snapshot_mode, FFM_AUTOFOCUS);
    TEST_CHECK(g_window_manager.configured_ffm_mode, FFM_AUTORAISE);
    TEST_CHECK(window_manager_effective_ffm_mode(&g_window_manager), FFM_AUTORAISE);
    g_mission_control_mode = saved_mission_control_mode;
    g_window_manager.configured_ffm_mode = saved_configured_mode;
    g_window_manager.ffm_menu_suspend_count = saved_suspend_count;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
)

static int ffm_test_ordered_move_loads;
static enum ffm_mode ffm_test_ordered_move_mode;
static void ffm_test_record_ordered_move_mode(enum ffm_mode mode)
{
    ++ffm_test_ordered_move_loads;
    ffm_test_ordered_move_mode = mode;
}

TEST_FUNC(ffm_menu_bar_actions_apply_before_causally_queued_mouse_moves,
    struct event_loop saved_event_loop = g_event_loop;
    enum mission_control_mode saved_mission_control_mode = g_mission_control_mode;
    struct mouse_state saved_mouse_state = g_mouse_state;
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    uint32_t saved_suspend_count = g_window_manager.ffm_menu_suspend_count;

    g_event_loop = (struct event_loop) {0};
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(11, 11);
    g_mouse_state.ffm_window_id = 0;
    g_mission_control_mode = MISSION_CONTROL_MODE_SHOW;
    g_window_manager.ffm_menu_suspend_count = 0;
    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTOFOCUS);

    struct menu_bar_action *open = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 11);
    open->data.menu_tracking.is_tracking = true;
    CGEventRef open_move = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, CGPointZero, kCGMouseButtonLeft);
    TEST_CHECK(open_move != NULL, true);
    event_loop_post(&g_event_loop, MENU_BAR_ACTION, open, 0);
    if (open_move) event_loop_post(&g_event_loop, MOUSE_MOVED, open_move, 0);

    struct event event = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, MENU_BAR_ACTION);
    if (event.type == MENU_BAR_ACTION) EVENT_HANDLER_MENU_BAR_ACTION(event.context, event.param1);
    TEST_CHECK(g_window_manager.ffm_menu_suspend_count, 1U);
    TEST_CHECK(g_event_loop.queue.count, 2U);

    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, MOUSE_MOVED);
    ffm_test_ordered_move_loads = 0;
    ffm_test_ordered_move_mode = FFM_AUTOFOCUS;
    window_manager_test_effective_ffm_load_observer = ffm_test_record_ordered_move_mode;
    if (event.type == MOUSE_MOVED) EVENT_HANDLER_MOUSE_MOVED(event.context, event.param1);
    window_manager_test_effective_ffm_load_observer = NULL;
    TEST_CHECK(ffm_test_ordered_move_loads, 1);
    TEST_CHECK(ffm_test_ordered_move_mode, FFM_DISABLED);
    TEST_CHECK(g_mouse_state.ffm_window_id, 0U);

    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, MENU_BAR_REFRESH);
    g_menu_bar_manager.refresh_pending = false;
    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTORAISE);

    struct menu_bar_action *close = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 11);
    close->data.menu_tracking.is_tracking = false;
    CGEventRef close_move = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, CGPointZero, kCGMouseButtonLeft);
    TEST_CHECK(close_move != NULL, true);
    event_loop_post(&g_event_loop, MENU_BAR_ACTION, close, 0);
    if (close_move) event_loop_post(&g_event_loop, MOUSE_MOVED, close_move, 0);

    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, MENU_BAR_ACTION);
    if (event.type == MENU_BAR_ACTION) EVENT_HANDLER_MENU_BAR_ACTION(event.context, event.param1);
    TEST_CHECK(g_window_manager.ffm_menu_suspend_count, 0U);
    TEST_CHECK(g_event_loop.queue.count, 2U);

    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, MOUSE_MOVED);
    ffm_test_ordered_move_loads = 0;
    ffm_test_ordered_move_mode = FFM_DISABLED;
    window_manager_test_effective_ffm_load_observer = ffm_test_record_ordered_move_mode;
    if (event.type == MOUSE_MOVED) EVENT_HANDLER_MOUSE_MOVED(event.context, event.param1);
    window_manager_test_effective_ffm_load_observer = NULL;
    TEST_CHECK(ffm_test_ordered_move_loads, 1);
    TEST_CHECK(ffm_test_ordered_move_mode, FFM_AUTORAISE);

    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, MENU_BAR_REFRESH);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
    g_event_loop = saved_event_loop;
    g_mission_control_mode = saved_mission_control_mode;
    g_mouse_state = saved_mouse_state;
    g_window_manager.configured_ffm_mode = saved_configured_mode;
    g_window_manager.ffm_menu_suspend_count = saved_suspend_count;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
)

TEST_FUNC(ffm_menu_bar_tracking_transitions_count_once_without_derived_events,
    struct event_loop saved_event_loop = g_event_loop;
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    uint32_t saved_suspend_count = g_window_manager.ffm_menu_suspend_count;

    g_event_loop = (struct event_loop) {0};
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(21, 21);
    g_window_manager.ffm_menu_suspend_count = 0;
    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTOFOCUS);

    struct menu_bar_action *action = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 21);
    action->data.menu_tracking.is_tracking = true;
    EVENT_HANDLER_MENU_BAR_ACTION(action, 0);
    action = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 21);
    action->data.menu_tracking.is_tracking = true;
    EVENT_HANDLER_MENU_BAR_ACTION(action, 0);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, 2U);
    TEST_CHECK(g_window_manager.ffm_menu_suspend_count, 2U);

    action = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 20);
    action->data.menu_tracking.is_tracking = true;
    EVENT_HANDLER_MENU_BAR_ACTION(action, 0);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, 2U);
    TEST_CHECK(g_window_manager.ffm_menu_suspend_count, 2U);

    action = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 21);
    action->data.menu_tracking.is_tracking = false;
    EVENT_HANDLER_MENU_BAR_ACTION(action, 0);
    action = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 21);
    action->data.menu_tracking.is_tracking = false;
    EVENT_HANDLER_MENU_BAR_ACTION(action, 0);
    action = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 21);
    action->data.menu_tracking.is_tracking = false;
    EVENT_HANDLER_MENU_BAR_ACTION(action, 0);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, 0U);
    TEST_CHECK(g_window_manager.ffm_menu_suspend_count, 0U);
    TEST_CHECK(window_manager_effective_ffm_mode(&g_window_manager), FFM_AUTOFOCUS);

    g_menu_bar_manager.menu_tracking_depth = UINT32_MAX;
    g_window_manager.ffm_menu_suspend_count = UINT32_MAX;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
    action = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 21);
    action->data.menu_tracking.is_tracking = true;
    EVENT_HANDLER_MENU_BAR_ACTION(action, 0);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, UINT32_MAX);
    TEST_CHECK(g_window_manager.ffm_menu_suspend_count, UINT32_MAX);
    action = menu_bar_manager_test_action(MENU_BAR_ACTION_MENU_TRACKING_CHANGED, 21);
    action->data.menu_tracking.is_tracking = false;
    EVENT_HANDLER_MENU_BAR_ACTION(action, 0);
    TEST_CHECK(g_menu_bar_manager.menu_tracking_depth, UINT32_MAX - 1);
    TEST_CHECK(g_window_manager.ffm_menu_suspend_count, UINT32_MAX - 1);

    TEST_CHECK(g_event_loop.queue.count, 1U);
    struct event event = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, MENU_BAR_REFRESH);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
    g_event_loop = saved_event_loop;
    g_window_manager.configured_ffm_mode = saved_configured_mode;
    g_window_manager.ffm_menu_suspend_count = saved_suspend_count;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
)

TEST_FUNC(ffm_mouse_callback_suppresses_disabled_moves_and_posts_enabled_moves_once,
    struct event_loop saved_event_loop = g_event_loop;
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    uint32_t saved_suspend_count = g_window_manager.ffm_menu_suspend_count;
    struct mouse_state mouse_state = { .modifier = MOUSE_MOD_ALT, .ffm_window_id = 77 };
    struct mouse_handler_context handler_context = { .mouse_state = &mouse_state, .handle = NULL };
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, CGPointMake(10, 20), kCGMouseButtonLeft);
    TEST_CHECK(event != NULL, true);
    if (!event) return result;

    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 4), true);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    g_event_loop.queue.wake_pending = true;

    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_DISABLED);
    CFIndex disabled_retain_count = CFGetRetainCount(event);
    TEST_CHECK(mouse_handler(NULL, kCGEventMouseMoved, event, &handler_context) == event, true);
    TEST_CHECK(g_event_loop.queue.count, 0U);
    TEST_CHECK(CFGetRetainCount(event), disabled_retain_count);
    TEST_CHECK(mouse_state.ffm_window_id, 77U);

    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTOFOCUS);
    CFIndex enabled_retain_count = CFGetRetainCount(event);
    TEST_CHECK(mouse_handler(NULL, kCGEventMouseMoved, event, &handler_context) == event, true);
    TEST_CHECK(g_event_loop.queue.count, 1U);
    TEST_CHECK(CFGetRetainCount(event), enabled_retain_count + 1);
    TEST_CHECK(mouse_state.ffm_window_id, 77U);
    struct event posted = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &posted), true);
    TEST_CHECK(posted.type, MOUSE_MOVED);
    TEST_CHECK(posted.context == event, true);
    event_loop_release_event_context(posted.type, posted.context);
    TEST_CHECK(CFGetRetainCount(event), enabled_retain_count);

    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    event_queue_destroy(&g_event_loop.queue);
    CFRelease(event);
    g_event_loop = saved_event_loop;
    g_window_manager.configured_ffm_mode = saved_configured_mode;
    g_window_manager.ffm_menu_suspend_count = saved_suspend_count;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
)
