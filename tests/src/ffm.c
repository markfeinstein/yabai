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
    struct window_manager wm = {
        .configured_ffm_mode = FFM_AUTOFOCUS,
        .ffm_menu_suspend_count = UINT32_MAX,
        .ffm_anonymous_menu_suspend_count = UINT32_MAX
    };
    window_manager_recompute_effective_ffm_mode(&wm);
    window_manager_ffm_menu_opened(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, UINT32_MAX);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);
    window_manager_ffm_menu_closed(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, UINT32_MAX - 1);
    TEST_CHECK(wm.ffm_anonymous_menu_suspend_count, UINT32_MAX - 1);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);

    wm.ffm_menu_suspend_count = 0;
    wm.ffm_anonymous_menu_suspend_count = 0;
    window_manager_recompute_effective_ffm_mode(&wm);
    window_manager_ffm_menu_closed(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, 0U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_AUTOFOCUS);
)

TEST_FUNC(ffm_application_menu_termination_releases_only_owned_suspensions,
    struct window_manager wm = {0};
    struct application first = { .name = "first" };
    struct application second = { .name = "second" };
    window_manager_set_focus_follows_mouse(&wm, FFM_AUTOFOCUS);

    window_manager_ffm_menu_opened(&wm);
    window_manager_ffm_application_menu_opened(&wm, &first);
    window_manager_ffm_application_menu_opened(&wm, &first);
    window_manager_ffm_application_menu_opened(&wm, &second);
    TEST_CHECK(wm.ffm_menu_suspend_count, 4U);
    TEST_CHECK(first.ffm_menu_suspend_count, 2U);
    TEST_CHECK(second.ffm_menu_suspend_count, 1U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);

    window_manager_ffm_application_terminated(&wm, &first);
    TEST_CHECK(wm.ffm_menu_suspend_count, 2U);
    TEST_CHECK(first.ffm_menu_suspend_count, 0U);
    TEST_CHECK(second.ffm_menu_suspend_count, 1U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);

    window_manager_ffm_application_menu_closed(&wm, &first);
    TEST_CHECK(wm.ffm_menu_suspend_count, 2U);
    window_manager_ffm_application_menu_closed(&wm, &second);
    TEST_CHECK(wm.ffm_menu_suspend_count, 1U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_DISABLED);
    window_manager_ffm_menu_closed(&wm);
    TEST_CHECK(wm.ffm_menu_suspend_count, 0U);
    TEST_CHECK(window_manager_effective_ffm_mode(&wm), FFM_AUTOFOCUS);
)

TEST_FUNC(ffm_config_query_reports_configured_mode_while_effective_is_suspended,
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    TEST_OVERRIDE(g_window_manager.ffm_menu_suspend_count, 0);
    TEST_OVERRIDE(g_window_manager.ffm_anonymous_menu_suspend_count, 0);
    window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTORAISE);
    window_manager_ffm_menu_opened(&g_window_manager);
    char *output = declarative_test_run_config("focus_follows_mouse", NULL);
    TEST_CHECK(strcmp(output, "autoraise\n"), 0);
    TEST_CHECK(window_manager_effective_ffm_mode(&g_window_manager), FFM_DISABLED);
    free(output);
    g_window_manager.configured_ffm_mode = saved_configured_mode;
    TEST_RESTORE(g_window_manager.ffm_menu_suspend_count);
    TEST_RESTORE(g_window_manager.ffm_anonymous_menu_suspend_count);
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
    TEST_OVERRIDE(g_window_manager.ffm_menu_suspend_count, 0);
    TEST_OVERRIDE(g_window_manager.ffm_anonymous_menu_suspend_count, 0);
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
    TEST_RESTORE(g_window_manager.ffm_menu_suspend_count);
    TEST_RESTORE(g_window_manager.ffm_anonymous_menu_suspend_count);
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
    enum mission_control_mode saved_mission_control_mode = g_mission_control_mode;
    struct mouse_state saved_mouse_state = g_mouse_state;
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    uint32_t saved_suspend_count = g_window_manager.ffm_menu_suspend_count;
    uint32_t saved_anonymous_count = g_window_manager.ffm_anonymous_menu_suspend_count;

    TEST_OVERRIDE(g_event_loop, (struct event_loop) {0});
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(11, 11);
    g_mouse_state.ffm_window_id = 0;
    g_mission_control_mode = MISSION_CONTROL_MODE_SHOW;
    g_window_manager.ffm_menu_suspend_count = 0;
    g_window_manager.ffm_anonymous_menu_suspend_count = 0;
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
    TEST_RESTORE(g_event_loop);
    g_mission_control_mode = saved_mission_control_mode;
    g_mouse_state = saved_mouse_state;
    g_window_manager.configured_ffm_mode = saved_configured_mode;
    g_window_manager.ffm_menu_suspend_count = saved_suspend_count;
    g_window_manager.ffm_anonymous_menu_suspend_count = saved_anonymous_count;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
)

TEST_FUNC(ffm_menu_bar_tracking_transitions_count_once_without_derived_events,
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    uint32_t saved_suspend_count = g_window_manager.ffm_menu_suspend_count;
    uint32_t saved_anonymous_count = g_window_manager.ffm_anonymous_menu_suspend_count;

    TEST_OVERRIDE(g_event_loop, (struct event_loop) {0});
    menu_bar_manager_test_reset();
    menu_bar_manager_test_begin_queue();
    menu_bar_manager_test_set_enabled_snapshot(21, 21);
    g_window_manager.ffm_menu_suspend_count = 0;
    g_window_manager.ffm_anonymous_menu_suspend_count = 0;
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
    g_window_manager.ffm_anonymous_menu_suspend_count = UINT32_MAX;
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
    TEST_CHECK(g_window_manager.ffm_anonymous_menu_suspend_count, UINT32_MAX - 1);

    TEST_CHECK(g_event_loop.queue.count, 1U);
    struct event event = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, MENU_BAR_REFRESH);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);

    menu_bar_manager_test_end_queue();
    menu_bar_manager_test_reset();
    TEST_RESTORE(g_event_loop);
    g_window_manager.configured_ffm_mode = saved_configured_mode;
    g_window_manager.ffm_menu_suspend_count = saved_suspend_count;
    g_window_manager.ffm_anonymous_menu_suspend_count = saved_anonymous_count;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
)

TEST_FUNC(ffm_mouse_callback_suppresses_disabled_moves_and_posts_enabled_moves_once,
    struct event_loop saved_event_loop = g_event_loop;
    enum ffm_mode saved_configured_mode = g_window_manager.configured_ffm_mode;
    uint32_t saved_suspend_count = g_window_manager.ffm_menu_suspend_count;
    uint32_t saved_anonymous_count = g_window_manager.ffm_anonymous_menu_suspend_count;
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
    g_window_manager.ffm_anonymous_menu_suspend_count = saved_anonymous_count;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
)

static int ffm_autoraise_test_level_calls;
static int ffm_autoraise_test_sub_level_calls;
static int ffm_autoraise_test_levels[8];
static int ffm_autoraise_test_level(uint32_t wid)
{
    ++ffm_autoraise_test_level_calls;
    return wid < 8 ? ffm_autoraise_test_levels[wid] : 0;
}

static int ffm_autoraise_test_sub_level(uint32_t wid)
{
    (void) wid;
    ++ffm_autoraise_test_sub_level_calls;
    return 0;
}

TEST_FUNC(ffm_autoraise_occlusion_queries_target_levels_once,
    bool initialized_window = false;
    if (!g_window_manager.window.buckets) {
        table_init(&g_window_manager.window, 8, hash_wm, compare_wm);
        initialized_window = true;
    }
    struct application application = { 0 };
    struct window target = { .id = 1, .application = &application, .frame = { { 0, 0 }, { 1000, 1000 } } };
    struct window outside = { .id = 2, .application = &application, .frame = { { 900, 900 }, { 200, 200 } } };
    struct window tiled = { .id = 3, .application = &application, .frame = { { 10, 10 }, { 100, 100 } } };
    struct window other_level = { .id = 4, .application = &application, .frame = { { 20, 20 }, { 100, 100 } } };
    struct window covered = { .id = 5, .application = &application, .frame = { { 30, 30 }, { 100, 100 } } };
    window_set_flag(&outside, WINDOW_FLOAT);
    window_set_flag(&other_level, WINDOW_FLOAT);
    window_set_flag(&covered, WINDOW_FLOAT);
    struct window *windows[] = { &target, &outside, &tiled, &other_level, &covered };
    for (size_t i = 0; i < array_count(windows); ++i) {
        table_add(&g_window_manager.window, &windows[i]->id, windows[i]);
    }
    memset(ffm_autoraise_test_levels, 0, sizeof(ffm_autoraise_test_levels));
    ffm_autoraise_test_levels[4] = 3;
    ffm_autoraise_window_level = ffm_autoraise_test_level;
    ffm_autoraise_window_sub_level = ffm_autoraise_test_sub_level;

    // Unknown, non-contained, tiled, and other-level windows do not block.
    // The target is queried once, and non-contained or tiled windows not at all.
    uint32_t blocked[] = { 9, 2, 3, 4, 5, 1 };
    ffm_autoraise_test_level_calls = 0;
    ffm_autoraise_test_sub_level_calls = 0;
    TEST_CHECK(ffm_autoraise_occludes_floating_window(&target, blocked, array_count(blocked)), true);
    TEST_CHECK(ffm_autoraise_test_level_calls, 3);
    TEST_CHECK(ffm_autoraise_test_sub_level_calls, 2);

    // Windows behind the target never block, and no candidate means no query.
    uint32_t behind[] = { 2, 3, 1, 5 };
    ffm_autoraise_test_level_calls = 0;
    ffm_autoraise_test_sub_level_calls = 0;
    TEST_CHECK(ffm_autoraise_occludes_floating_window(&target, behind, array_count(behind)), false);
    TEST_CHECK(ffm_autoraise_test_level_calls, 0);
    TEST_CHECK(ffm_autoraise_test_sub_level_calls, 0);

    // A different-level candidate alone does not block.
    uint32_t level_only[] = { 4, 1 };
    TEST_CHECK(ffm_autoraise_occludes_floating_window(&target, level_only, array_count(level_only)), false);

    // The local precheck finds contained floating windows and ignores tiled,
    // non-contained, and the target itself.
    TEST_CHECK(ffm_autoraise_has_floating_candidate(&target), true);
    window_clear_flag(&other_level, WINDOW_FLOAT);
    window_clear_flag(&covered, WINDOW_FLOAT);
    TEST_CHECK(ffm_autoraise_has_floating_candidate(&target), false);
    window_set_flag(&target, WINDOW_FLOAT);
    TEST_CHECK(ffm_autoraise_has_floating_candidate(&target), false);
    window_clear_flag(&target, WINDOW_FLOAT);

    ffm_autoraise_window_level = window_level;
    ffm_autoraise_window_sub_level = window_sub_level;
    for (size_t i = 0; i < array_count(windows); ++i) {
        table_remove(&g_window_manager.window, &windows[i]->id);
    }
    if (initialized_window) { table_free(&g_window_manager.window); memset(&g_window_manager.window, 0, sizeof(g_window_manager.window)); }
)
