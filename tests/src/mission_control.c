struct mission_control_test_state {
    pid_t pid;
    bool create_application;
    AXError observer_result;
    bool create_observer;
    int failed_notification;
    AXError notification_results[4];
    bool create_source;
    bool create_run_loop;
    int application_create_count;
    int observer_create_count;
    int notification_add_count;
    int notification_remove_count;
    int removed_notifications[4];
    int observer_source_count;
    int run_loop_add_count;
    int invalidate_count;
    int application_release_count;
    int observer_release_count;
};

static struct mission_control_test_state mission_control_test;
static AXUIElementRef mission_control_test_application = (AXUIElementRef) (uintptr_t) 0x101;
static AXObserverRef mission_control_test_observer = (AXObserverRef) (uintptr_t) 0x102;
static CFRunLoopSourceRef mission_control_test_source = (CFRunLoopSourceRef) (uintptr_t) 0x103;
static CFRunLoopRef mission_control_test_run_loop = (CFRunLoopRef) (uintptr_t) 0x104;

static pid_t mission_control_test_dock_pid(void)
{
    return mission_control_test.pid;
}

static AXUIElementRef mission_control_test_application_create(pid_t pid)
{
    ++mission_control_test.application_create_count;
    return mission_control_test.create_application ? mission_control_test_application : NULL;
}

static AXError mission_control_test_observer_create(pid_t pid, AXObserverCallback callback, AXObserverRef *observer)
{
    ++mission_control_test.observer_create_count;
    *observer = mission_control_test.create_observer ? mission_control_test_observer : NULL;
    return mission_control_test.observer_result;
}

static AXError mission_control_test_notification_add(AXObserverRef observer, AXUIElementRef element,
                                                      CFStringRef notification, void *context)
{
    int index = mission_control_test.notification_add_count++ % 4;
    return mission_control_test.notification_results[index];
}

static int mission_control_test_notification_index(CFStringRef notification)
{
    for (int i = 0; i < array_count(mission_control_notifications); ++i) {
        if (notification == mission_control_notifications[i]) return i;
    }
    return -1;
}

static AXError mission_control_test_notification_remove(AXObserverRef observer, AXUIElementRef element,
                                                         CFStringRef notification)
{
    int count = mission_control_test.notification_remove_count++;
    if (count < array_count(mission_control_test.removed_notifications)) {
        mission_control_test.removed_notifications[count] = mission_control_test_notification_index(notification);
    }
    return kAXErrorSuccess;
}

static CFRunLoopSourceRef mission_control_test_observer_source(AXObserverRef observer)
{
    ++mission_control_test.observer_source_count;
    return mission_control_test.create_source ? mission_control_test_source : NULL;
}

static CFRunLoopRef mission_control_test_main_run_loop(void)
{
    return mission_control_test.create_run_loop ? mission_control_test_run_loop : NULL;
}

static void mission_control_test_run_loop_add_source(CFRunLoopRef run_loop, CFRunLoopSourceRef source,
                                                      CFRunLoopMode mode)
{
    ++mission_control_test.run_loop_add_count;
}

static void mission_control_test_source_invalidate(CFRunLoopSourceRef source)
{
    ++mission_control_test.invalidate_count;
}

static void mission_control_test_release(CFTypeRef ref)
{
    if (ref == mission_control_test_application) ++mission_control_test.application_release_count;
    if (ref == mission_control_test_observer) ++mission_control_test.observer_release_count;
}

static void mission_control_test_reset(void)
{
    mission_control_test = (struct mission_control_test_state) {
        .pid = 42,
        .create_application = true,
        .observer_result = kAXErrorSuccess,
        .create_observer = true,
        .create_source = true,
        .create_run_loop = true,
        .notification_results = {
            kAXErrorSuccess, kAXErrorSuccess, kAXErrorSuccess, kAXErrorSuccess
        }
    };
    g_mission_control_observer = (struct mission_control_observer) {0};
    g_mission_control_accepting = true;
    g_mission_control_lifetime_generation = event_loop_generation_snapshot(&g_event_loop);
    g_mission_control_observer_operations = (struct mission_control_observer_operations) {
        mission_control_test_dock_pid,
        mission_control_test_application_create,
        mission_control_test_observer_create,
        mission_control_test_notification_add,
        mission_control_test_notification_remove,
        mission_control_test_observer_source,
        mission_control_test_main_run_loop,
        mission_control_test_run_loop_add_source,
        mission_control_test_source_invalidate,
        mission_control_test_release
    };
}

static void mission_control_test_check_unpublished(bool *result, char *test_name)
{
    if (g_mission_control_observer.ref || g_mission_control_observer.observer_ref ||
        g_mission_control_observer.is_observing) {
        printf("                   \e[1;33m%s\e[m\e[1;31m observer state is unpublished\e[m\n", test_name);
        *result = false;
    }
}

TEST_FUNC(mission_control_observe_rejects_zero_pid_and_creation_failures,
    mission_control_test_reset();
    mission_control_test.pid = 0;
    TEST_CHECK(mission_control_observe(), false);
    TEST_CHECK(mission_control_test.application_create_count, 0);
    mission_control_test_check_unpublished(&result, test_name);

    mission_control_test_reset();
    mission_control_test.create_application = false;
    TEST_CHECK(mission_control_observe(), false);
    TEST_CHECK(mission_control_test.application_create_count, 1);
    TEST_CHECK(mission_control_test.observer_create_count, 0);
    TEST_CHECK(mission_control_test.application_release_count, 0);
    mission_control_test_check_unpublished(&result, test_name);

    mission_control_test_reset();
    mission_control_test.observer_result = kAXErrorFailure;
    mission_control_test.create_observer = false;
    TEST_CHECK(mission_control_observe(), false);
    TEST_CHECK(mission_control_test.application_release_count, 1);
    TEST_CHECK(mission_control_test.observer_release_count, 0);
    TEST_CHECK(mission_control_test.invalidate_count, 0);
    mission_control_test_check_unpublished(&result, test_name);

    mission_control_test_reset();
    mission_control_test.observer_result = kAXErrorFailure;
    TEST_CHECK(mission_control_observe(), false);
    TEST_CHECK(mission_control_test.application_release_count, 1);
    TEST_CHECK(mission_control_test.observer_release_count, 1);
    TEST_CHECK(mission_control_test.invalidate_count, 1);
    mission_control_test_check_unpublished(&result, test_name);

    mission_control_test_reset();
    mission_control_test.create_source = false;
    TEST_CHECK(mission_control_observe(), false);
    TEST_CHECK(mission_control_test.notification_add_count, 0);
    TEST_CHECK(mission_control_test.invalidate_count, 0);
    TEST_CHECK(mission_control_test.observer_release_count, 1);
    TEST_CHECK(mission_control_test.application_release_count, 1);
    mission_control_test_check_unpublished(&result, test_name);

    mission_control_test_reset();
    mission_control_test.create_run_loop = false;
    TEST_CHECK(mission_control_observe(), false);
    TEST_CHECK(mission_control_test.notification_add_count, 0);
    TEST_CHECK(mission_control_test.invalidate_count, 1);
    TEST_CHECK(mission_control_test.observer_release_count, 1);
    TEST_CHECK(mission_control_test.application_release_count, 1);
    mission_control_test_check_unpublished(&result, test_name);
)

TEST_FUNC(mission_control_observe_rolls_back_each_notification_failure,
    for (int failed = 0; failed < 4; ++failed) {
        mission_control_test_reset();
        mission_control_test.notification_results[failed] = kAXErrorFailure;
        TEST_CHECK(mission_control_observe(), false);
        TEST_CHECK(mission_control_test.notification_add_count, failed + 1);
        TEST_CHECK(mission_control_test.notification_remove_count, failed);
        for (int removed = 0; removed < failed; ++removed) {
            TEST_CHECK(mission_control_test.removed_notifications[removed], failed - removed - 1);
        }
        TEST_CHECK(mission_control_test.invalidate_count, 1);
        TEST_CHECK(mission_control_test.observer_release_count, 1);
        TEST_CHECK(mission_control_test.application_release_count, 1);
        TEST_CHECK(mission_control_test.run_loop_add_count, 0);
        mission_control_test_check_unpublished(&result, test_name);
    }
)

TEST_FUNC(mission_control_observe_publishes_only_complete_success_and_is_repeatable,
    mission_control_test_reset();
    mission_control_test.notification_results[1] = kAXErrorNotificationAlreadyRegistered;
    TEST_CHECK(mission_control_observe(), true);
    TEST_CHECK(mission_control_test.notification_add_count, 4);
    TEST_CHECK(mission_control_test.notification_remove_count, 0);
    TEST_CHECK(mission_control_test.run_loop_add_count, 1);
    TEST_CHECK(g_mission_control_observer.ref, mission_control_test_application);
    TEST_CHECK(g_mission_control_observer.observer_ref, mission_control_test_observer);
    TEST_CHECK(g_mission_control_observer.is_observing, true);

    TEST_CHECK(mission_control_observe(), true);
    TEST_CHECK(mission_control_test.application_create_count, 1);
    TEST_CHECK(mission_control_test.observer_create_count, 1);
    TEST_CHECK(mission_control_test.notification_add_count, 4);
    TEST_CHECK(mission_control_test.run_loop_add_count, 1);

    mission_control_unobserve();
    TEST_CHECK(mission_control_test.notification_remove_count, 4);
    TEST_CHECK(mission_control_test.invalidate_count, 1);
    TEST_CHECK(mission_control_test.observer_release_count, 1);
    TEST_CHECK(mission_control_test.application_release_count, 1);
    mission_control_test_check_unpublished(&result, test_name);

    mission_control_unobserve();
    TEST_CHECK(mission_control_test.notification_remove_count, 4);
    TEST_CHECK(mission_control_test.invalidate_count, 1);
    TEST_CHECK(mission_control_test.observer_release_count, 1);
    TEST_CHECK(mission_control_test.application_release_count, 1);
)

TEST_FUNC(mission_control_unobserve_cleans_partial_state_without_boolean_gate,
    mission_control_test_reset();
    g_mission_control_observer.ref = mission_control_test_application;
    mission_control_unobserve();
    TEST_CHECK(mission_control_test.notification_remove_count, 0);
    TEST_CHECK(mission_control_test.application_release_count, 1);
    TEST_CHECK(mission_control_test.observer_release_count, 0);
    TEST_CHECK(mission_control_test.invalidate_count, 0);
    mission_control_test_check_unpublished(&result, test_name);

    mission_control_test_reset();
    g_mission_control_observer.observer_ref = mission_control_test_observer;
    mission_control_unobserve();
    TEST_CHECK(mission_control_test.notification_remove_count, 0);
    TEST_CHECK(mission_control_test.application_release_count, 0);
    TEST_CHECK(mission_control_test.observer_release_count, 1);
    TEST_CHECK(mission_control_test.invalidate_count, 1);
    mission_control_test_check_unpublished(&result, test_name);

    mission_control_test_reset();
    g_mission_control_observer.ref = mission_control_test_application;
    g_mission_control_observer.observer_ref = mission_control_test_observer;
    mission_control_unobserve();
    TEST_CHECK(mission_control_test.notification_remove_count, 4);
    TEST_CHECK(mission_control_test.application_release_count, 1);
    TEST_CHECK(mission_control_test.observer_release_count, 1);
    TEST_CHECK(mission_control_test.invalidate_count, 1);
    mission_control_test_check_unpublished(&result, test_name);
)

TEST_FUNC(mission_control_observe_retries_after_transaction_failure,
    mission_control_test_reset();
    mission_control_test.notification_results[2] = kAXErrorFailure;
    TEST_CHECK(mission_control_observe(), false);
    mission_control_test.notification_results[2] = kAXErrorSuccess;
    TEST_CHECK(mission_control_observe(), true);
    TEST_CHECK(mission_control_test.application_create_count, 2);
    TEST_CHECK(mission_control_test.observer_create_count, 2);
    TEST_CHECK(mission_control_test.notification_add_count, 7);
    TEST_CHECK(mission_control_test.run_loop_add_count, 1);
    TEST_CHECK(g_mission_control_observer.is_observing, true);
    mission_control_unobserve();
)

TEST_FUNC(os_timer_elapsed_is_baseline_independent_and_regression_safe,
    const uint64_t day = 86400ULL * 1000000000ULL;
    const uint64_t baselines[] = {
        0, day, 13 * day, 26 * day, 194 * day, 388 * day,
        UINT64_MAX - 2000000000ULL
    };
    for (int i = 0; i < array_count(baselines); ++i) {
        uint64_t base = baselines[i];
        TEST_CHECK(os_timer_elapsed_milliseconds(base, base + 1249999999ULL) < 1250.0, true);
        TEST_CHECK(os_timer_elapsed_milliseconds(base, base + 1250000000ULL) == 1250.0, true);
        TEST_CHECK(os_timer_elapsed_milliseconds(base, base + 1500000000ULL) == 1500.0, true);
        TEST_CHECK(os_timer_elapsed_milliseconds(base, base + 1500000001ULL) > 1500.0, true);
    }
    TEST_CHECK(os_timer_elapsed_milliseconds(100, 99), 0.0);
    TEST_CHECK(os_timer_elapsed_milliseconds(UINT64_MAX, 0), 0.0);
    const uint64_t high = 388 * day;
    TEST_CHECK(os_timer_cmd_tab_should_fence(high, high + 1500000000ULL), false);
    TEST_CHECK(os_timer_cmd_tab_should_fence(high, high + 1500000001ULL), true);
    TEST_CHECK(os_timer_gesture_suppression_active(high, high + 1249999999ULL), true);
    TEST_CHECK(os_timer_gesture_suppression_active(high, high + 1250000000ULL), false);
    TEST_CHECK(os_timer_mouse_resize_ready(high, high + 16666666ULL), false);
    TEST_CHECK(os_timer_mouse_resize_ready(high, high + 16666667ULL), true);
    TEST_CHECK(os_timer_cmd_tab_should_fence(UINT64_MAX, 0), false);
    TEST_CHECK(os_timer_gesture_suppression_active(UINT64_MAX, 0), true);
    TEST_CHECK(os_timer_mouse_resize_ready(UINT64_MAX, 0), false);
)

static CFArrayRef mission_control_samples[4];
static int mission_control_sample_index;
static int mission_control_copy_count;

static CFArrayRef mission_control_test_copy_window_list(CGWindowListOption option, CGWindowID relative)
{
    ++mission_control_copy_count;
    CFArrayRef result = mission_control_samples[mission_control_sample_index++];
    return result ? CFRetain(result) : NULL;
}

static int mission_control_effect_enter;
static int mission_control_effect_exit;
static int mission_control_effect_correct;
static int mission_control_effect_dirty;
static int mission_control_effect_reconcile;
static int mission_control_effect_menu_alpha;
static uint64_t mission_control_effect_poll_generation;
static uint64_t mission_control_effect_poll_token;
static int mission_control_effect_schedule_count;
static double mission_control_effect_delay;

static void mission_control_effect_signal(enum signal_type type, void *context)
{
    if (type == SIGNAL_MISSION_CONTROL_ENTER) ++mission_control_effect_enter;
    if (type == SIGNAL_MISSION_CONTROL_EXIT) ++mission_control_effect_exit;
}

static void mission_control_effect_correct_fn(struct space_manager *sm, struct window_manager *wm)
{
    ++mission_control_effect_correct;
}

static void mission_control_effect_menu_alpha_fn(int index, int enabled, float alpha)
{
    ++mission_control_effect_menu_alpha;
}

static void mission_control_effect_dirty_fn(uint32_t flags)
{
    ++mission_control_effect_dirty;
}

static void mission_control_effect_reconcile_fn(void)
{
    ++mission_control_effect_reconcile;
}

static void mission_control_effect_schedule(uint64_t generation, uint64_t token, double delay)
{
    ++mission_control_effect_schedule_count;
    mission_control_effect_poll_generation = generation;
    mission_control_effect_poll_token = token;
    mission_control_effect_delay = delay;
}

TEST_FUNC(mission_control_linearized_contexts_polls_and_side_effects,
    uint64_t saved_generation = g_mission_control_generation;
    uint64_t saved_notification_generation = g_mission_control_notification_token;
    g_mission_control_generation = UINT64_MAX;
    g_mission_control_notification_token = UINT64_MAX;
    TEST_CHECK(mission_control_next_generation(), 1ULL);
    TEST_CHECK(mission_control_next_nonzero(&g_mission_control_notification_token), 1ULL);
    g_mission_control_generation = saved_generation;
    g_mission_control_notification_token = saved_notification_generation;
    struct mission_control_session_state saved_session = g_mission_control_session;
    enum mission_control_mode saved_mode = g_mission_control_mode;
    float saved_opacity = g_window_manager.menubar_opacity;
    __auto_type saved_signal = event_loop_test_mission_control_signal;
    __auto_type saved_correct = event_loop_test_mission_control_correct;
    __auto_type saved_dirty = event_loop_test_mission_control_menu_dirty;
    __auto_type saved_alpha = event_loop_test_mission_control_menu_alpha;
    __auto_type saved_reconcile = event_loop_test_mission_control_reconcile;
    __auto_type saved_schedule = event_loop_test_mission_control_schedule;
    __auto_type saved_copy = event_loop_test_window_list_copy;

    mission_control_effect_enter = mission_control_effect_exit = 0;
    mission_control_effect_correct = mission_control_effect_dirty = mission_control_effect_reconcile = 0;
    mission_control_effect_menu_alpha = 0;
    mission_control_effect_schedule_count = 0;
    event_loop_test_mission_control_signal = mission_control_effect_signal;
    event_loop_test_mission_control_correct = mission_control_effect_correct_fn;
    event_loop_test_mission_control_menu_dirty = mission_control_effect_dirty_fn;
    event_loop_test_mission_control_menu_alpha = mission_control_effect_menu_alpha_fn;
    event_loop_test_mission_control_reconcile = mission_control_effect_reconcile_fn;
    event_loop_test_mission_control_schedule = mission_control_effect_schedule;
    g_window_manager.menubar_opacity = 0.5f;
    __atomic_store_n(&g_mission_control_observer.epoch, 7, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observer.notification_token, 11, __ATOMIC_RELEASE);

    struct mission_control_event_context *stale = malloc(sizeof(*stale));
    *stale = (struct mission_control_event_context) { .epoch = 6, .token = 11, .kind = MISSION_CONTROL_NOTIFICATION_SHOW_ALL };
    EVENT_HANDLER_MISSION_CONTROL_SHOW_ALL_WINDOWS(stale, 1);
    TEST_CHECK(mission_control_effect_enter, 0);

    struct mission_control_event_context *show = malloc(sizeof(*show));
    *show = (struct mission_control_event_context) { .epoch = 7, .token = 11, .kind = MISSION_CONTROL_NOTIFICATION_SHOW_ALL };
    EVENT_HANDLER_MISSION_CONTROL_SHOW_ALL_WINDOWS(show, 1);
    TEST_CHECK(mission_control_effect_enter, 1);
    uint64_t generation = g_mission_control_session.generation;

    struct mission_control_event_context *exit = malloc(sizeof(*exit));
    *exit = (struct mission_control_event_context) { .epoch = 7, .token = 11, .kind = MISSION_CONTROL_NOTIFICATION_EXIT };
    EVENT_HANDLER_MISSION_CONTROL_EXIT(exit, 1);
    TEST_CHECK(g_mission_control_session.active, true);
    TEST_CHECK(g_mission_control_session.poll_pending, true);
    EVENT_HANDLER_MISSION_CONTROL_EXIT(NULL, 1);
    TEST_CHECK(mission_control_effect_exit, 0);

    mission_control_samples[0] = CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
    mission_control_sample_index = 0;
    event_loop_test_window_list_copy = mission_control_test_copy_window_list;
    struct mission_control_poll_context *poll = malloc(sizeof(*poll));
    *poll = (struct mission_control_poll_context) { mission_control_effect_poll_generation, mission_control_effect_poll_token };
    EVENT_HANDLER_MISSION_CONTROL_CHECK_FOR_EXIT(poll, 0);
    TEST_CHECK(g_mission_control_session.active, false);
    TEST_CHECK(mission_control_effect_exit, 1);
    TEST_CHECK(mission_control_effect_correct, 1);
    TEST_CHECK(mission_control_effect_dirty, 1);
    TEST_CHECK(mission_control_effect_reconcile, 1);
    TEST_CHECK(mission_control_effect_menu_alpha, 1);
    mission_control_finish_exit(generation);
    TEST_CHECK(mission_control_effect_exit, 1);
    CFRelease(mission_control_samples[0]);
    mission_control_samples[0] = NULL;

    event_loop_test_mission_control_signal = saved_signal;
    event_loop_test_mission_control_correct = saved_correct;
    event_loop_test_mission_control_menu_dirty = saved_dirty;
    event_loop_test_mission_control_menu_alpha = saved_alpha;
    event_loop_test_mission_control_reconcile = saved_reconcile;
    event_loop_test_mission_control_schedule = saved_schedule;
    event_loop_test_window_list_copy = saved_copy;
    g_mission_control_session = saved_session;
    g_mission_control_mode = saved_mode;
    g_window_manager.menubar_opacity = saved_opacity;
)

static struct mission_control_ingress_item *mission_control_barrier_item;
static struct mission_control_event_context *mission_control_captured_events[128];
static enum event_type mission_control_captured_types[128];
static int mission_control_captured_count;
static double mission_control_last_delay;

static void mission_control_capture_ingress(struct mission_control_ingress_item *item)
{
    mission_control_barrier_item = item;
}

static bool mission_control_capture_post(enum event_type type, void *context, int param1)
{
    mission_control_captured_types[mission_control_captured_count] = type;
    mission_control_captured_events[mission_control_captured_count++] = context;
    return true;
}

static void mission_control_capture_observe_retry(double delay)
{
    mission_control_last_delay = delay;
}

TEST_FUNC(mission_control_ingress_epoch_ordering_and_observer_retry,
    bool saved_running = g_event_loop.is_running;
    uint64_t saved_loop_generation = g_event_loop.generation;
    __auto_type saved_submit = mission_control_test_ingress_submit;
    __auto_type saved_post = mission_control_test_post;
    __auto_type saved_retry = mission_control_test_observe_retry;
    __auto_type saved_signal = event_loop_test_mission_control_signal;
    struct mission_control_session_state saved_session = g_mission_control_session;
    enum mission_control_mode saved_mode = g_mission_control_mode;

    mission_control_test_reset();
    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 31, __ATOMIC_RELEASE);
    mission_control_admission_open(31);
    mission_control_test_ingress_submit = mission_control_capture_ingress;
    mission_control_test_post = mission_control_capture_post;
    event_loop_test_mission_control_signal = mission_control_effect_signal;
    mission_control_effect_enter = 0;
    TEST_CHECK(mission_control_observe(), true);
    uint64_t old_epoch = g_mission_control_observer.epoch;
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[0], (void *) (uintptr_t) old_epoch);
    TEST_CHECK(mission_control_barrier_item != NULL, true);
    mission_control_unobserve();
    TEST_CHECK(mission_control_observe(), true);
    mission_control_captured_count = 0;
    mission_control_ingress_append(mission_control_barrier_item);
    mission_control_barrier_item = NULL;
    TEST_CHECK(mission_control_captured_count, 0);
    TEST_CHECK(mission_control_effect_enter, 0);

    uint64_t epoch = g_mission_control_observer.epoch;
    mission_control_test_ingress_submit = NULL;
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[0], (void *) (uintptr_t) epoch);
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[2], (void *) (uintptr_t) epoch);
    TEST_CHECK(mission_control_captured_count, 2);
    EVENT_HANDLER_MISSION_CONTROL_SHOW_ALL_WINDOWS(mission_control_captured_events[0], 1);
    TEST_CHECK(mission_control_effect_enter, 0);
    EVENT_HANDLER_MISSION_CONTROL_SHOW_DESKTOP(mission_control_captured_events[1], 1);
    TEST_CHECK(mission_control_effect_enter, 1);

    mission_control_unobserve();
    mission_control_test.pid = 0;
    mission_control_test_observe_retry = mission_control_capture_observe_retry;
    mission_control_last_delay = 0.0;
    mission_control_ensure_observed();
    TEST_CHECK(mission_control_last_delay, 0.1);
    mission_control_test.pid = 42;
    uint64_t retry_generation = g_mission_control_observe_retry_generation;
    mission_control_observe_retry_attempt(retry_generation, 1);
    TEST_CHECK(mission_control_is_observing(), true);
    mission_control_unobserve();

    mission_control_test_ingress_submit = saved_submit;
    mission_control_test_post = saved_post;
    mission_control_test_observe_retry = saved_retry;
    event_loop_test_mission_control_signal = saved_signal;
    g_mission_control_session = saved_session;
    g_mission_control_mode = saved_mode;
    __atomic_store_n(&g_event_loop.generation, saved_loop_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.is_running, saved_running, __ATOMIC_RELEASE);
)

TEST_FUNC(mission_control_sample_tristate_is_defensive,
    TEST_CHECK(mission_control_classify_sample(NULL), MISSION_CONTROL_SAMPLE_INCONCLUSIVE);
    TEST_CHECK(mission_control_classify_sample(CFSTR("bad")), MISSION_CONTROL_SAMPLE_INCONCLUSIVE);
    const void *bad_item[] = { CFSTR("bad") };
    CFArrayRef bad_array = CFArrayCreate(NULL, bad_item, 1, &kCFTypeArrayCallBacks);
    TEST_CHECK(mission_control_classify_sample(bad_array), MISSION_CONTROL_SAMPLE_INCONCLUSIVE);

    CFDictionaryRef missing = CFDictionaryCreate(NULL, NULL, NULL, 0,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
    const void *missing_item[] = { missing };
    CFArrayRef missing_array = CFArrayCreate(NULL, missing_item, 1, &kCFTypeArrayCallBacks);
    TEST_CHECK(mission_control_classify_sample(missing_array), MISSION_CONTROL_SAMPLE_INCONCLUSIVE);

    int64_t layer = 18;
    CFNumberRef layer_number = CFNumberCreate(NULL, kCFNumberSInt64Type, &layer);
    const void *keys[] = { kCGWindowOwnerName, kCGWindowLayer };
    const void *dock_values[] = { CFSTR("Dock"), layer_number };
    CFDictionaryRef dock = CFDictionaryCreate(NULL, keys, dock_values, 2,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    const void *dock_item[] = { dock };
    CFArrayRef dock_array = CFArrayCreate(NULL, dock_item, 1, &kCFTypeArrayCallBacks);
    TEST_CHECK(mission_control_classify_sample(dock_array), MISSION_CONTROL_SAMPLE_ACTIVE);
    CFArrayRef empty = CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
    TEST_CHECK(mission_control_classify_sample(empty), MISSION_CONTROL_SAMPLE_INACTIVE);

    CFRelease(empty); CFRelease(dock_array); CFRelease(dock); CFRelease(layer_number);
    CFRelease(missing_array); CFRelease(missing); CFRelease(bad_array);
)

static CFArrayRef mission_control_always_null(CGWindowListOption option, CGWindowID relative)
{
    return NULL;
}

TEST_FUNC(mission_control_poll_reservation_and_inconclusive_backoff,
    struct mission_control_session_state saved_session = g_mission_control_session;
    enum mission_control_mode saved_mode = g_mission_control_mode;
    __auto_type saved_schedule = event_loop_test_mission_control_schedule;
    __auto_type saved_copy = event_loop_test_window_list_copy;
    event_loop_test_mission_control_schedule = mission_control_effect_schedule;
    event_loop_test_window_list_copy = mission_control_always_null;
    mission_control_effect_schedule_count = 0;
    g_mission_control_session = (struct mission_control_session_state) { .generation = 90, .active = true };
    g_mission_control_mode = MISSION_CONTROL_MODE_SHOW;

    mission_control_schedule_poll(0.1);
    mission_control_schedule_poll(0.1);
    mission_control_schedule_poll(0.1);
    TEST_CHECK(mission_control_effect_schedule_count, 1);
    for (int i = 0; i < 10; ++i) {
        struct mission_control_poll_context *poll = malloc(sizeof(*poll));
        *poll = (struct mission_control_poll_context) {
            mission_control_effect_poll_generation, mission_control_effect_poll_token
        };
        EVENT_HANDLER_MISSION_CONTROL_CHECK_FOR_EXIT(poll, 0);
        TEST_CHECK(g_mission_control_session.active, true);
    }
    TEST_CHECK(mission_control_effect_schedule_count, 11);
    TEST_CHECK(mission_control_effect_delay, 1.0);

    event_loop_test_mission_control_schedule = saved_schedule;
    event_loop_test_window_list_copy = saved_copy;
    g_mission_control_session = saved_session;
    g_mission_control_mode = saved_mode;
)

static int mission_control_reject_remaining;
static int mission_control_reject_attempts;
static double mission_control_reject_delay;

static bool mission_control_reject_then_capture(enum event_type type, void *context, int param1)
{
    ++mission_control_reject_attempts;
    if (mission_control_reject_remaining-- > 0) return false;
    return mission_control_capture_post(type, context, param1);
}

static void mission_control_capture_ingress_retry(double delay)
{
    mission_control_reject_delay = delay;
}

TEST_FUNC(mission_control_rejected_ingress_recovers_without_token_corruption,
    bool saved_running = g_event_loop.is_running;
    uint64_t saved_loop_generation = g_event_loop.generation;
    __auto_type saved_post = mission_control_test_post;
    __auto_type saved_retry = mission_control_test_ingress_retry;
    __auto_type saved_signal = event_loop_test_mission_control_signal;
    __auto_type saved_schedule = event_loop_test_mission_control_schedule;
    struct mission_control_session_state saved_session = g_mission_control_session;
    enum mission_control_mode saved_mode = g_mission_control_mode;

    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 44, __ATOMIC_RELEASE);
    mission_control_admission_open(44);
    __atomic_store_n(&g_mission_control_observer.epoch, 12, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observer.notification_token, 0, __ATOMIC_RELEASE);
    mission_control_test_post = mission_control_reject_then_capture;
    mission_control_test_ingress_retry = mission_control_capture_ingress_retry;
    event_loop_test_mission_control_signal = mission_control_effect_signal;
    event_loop_test_mission_control_schedule = mission_control_effect_schedule;
    mission_control_effect_enter = 0;
    mission_control_captured_count = 0;
    mission_control_reject_attempts = 0;
    mission_control_reject_remaining = 1;
    mission_control_reject_delay = 0.0;

    mission_control_notification_handler(NULL, NULL, mission_control_notifications[0], (void *) (uintptr_t) 12);
    TEST_CHECK(mission_control_reject_attempts, 1);
    TEST_CHECK(g_mission_control_observer.notification_token, 0ULL);
    TEST_CHECK(mission_control_reject_delay, 0.1);
    g_mission_control_ingress_retry_pending = false;
    mission_control_ingress_process();
    TEST_CHECK(mission_control_reject_attempts, 2);
    TEST_CHECK(mission_control_captured_count, 1);
    uint64_t token = mission_control_captured_events[0]->token;
    TEST_CHECK(g_mission_control_observer.notification_token, token);
    EVENT_HANDLER_MISSION_CONTROL_SHOW_ALL_WINDOWS(mission_control_captured_events[0], 1);
    TEST_CHECK(mission_control_effect_enter, 1);

    mission_control_reject_remaining = 1;
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[3], (void *) (uintptr_t) 12);
    TEST_CHECK(g_mission_control_observer.notification_token, token);
    g_mission_control_ingress_retry_pending = false;
    mission_control_ingress_process();
    TEST_CHECK(mission_control_captured_count, 2);
    EVENT_HANDLER_MISSION_CONTROL_EXIT(mission_control_captured_events[1], 1);
    TEST_CHECK(g_mission_control_session.active, true);
    TEST_CHECK(g_mission_control_session.poll_pending, true);

    mission_control_test_post = saved_post;
    mission_control_test_ingress_retry = saved_retry;
    event_loop_test_mission_control_signal = saved_signal;
    event_loop_test_mission_control_schedule = saved_schedule;
    g_mission_control_session = saved_session;
    g_mission_control_mode = saved_mode;
    __atomic_store_n(&g_event_loop.generation, saved_loop_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.is_running, saved_running, __ATOMIC_RELEASE);
)

static int mission_control_fallback_schedule_count;
static double mission_control_fallback_delay;

static void mission_control_capture_fallback(double delay)
{
    ++mission_control_fallback_schedule_count;
    mission_control_fallback_delay = delay;
}

TEST_FUNC(mission_control_initial_allocation_fallback_recovers_all_kinds,
    bool saved_running = g_event_loop.is_running;
    uint64_t saved_loop_generation = g_event_loop.generation;
    __auto_type saved_post = mission_control_test_post;
    __auto_type saved_fallback = mission_control_test_fallback_schedule;
    __auto_type saved_signal = event_loop_test_mission_control_signal;
    __auto_type saved_schedule = event_loop_test_mission_control_schedule;
    struct mission_control_session_state saved_session = g_mission_control_session;
    enum mission_control_mode saved_mode = g_mission_control_mode;

    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 71, __ATOMIC_RELEASE);
    mission_control_admission_open(71);
    __atomic_store_n(&g_mission_control_observer.epoch, 19, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observer.notification_token, 0, __ATOMIC_RELEASE);
    mission_control_test_post = mission_control_capture_post;
    mission_control_test_fallback_schedule = mission_control_capture_fallback;
    event_loop_test_mission_control_signal = mission_control_effect_signal;
    event_loop_test_mission_control_schedule = mission_control_effect_schedule;
    mission_control_captured_count = 0;
    mission_control_fallback_schedule_count = 0;
    mission_control_effect_enter = mission_control_effect_exit = 0;
    mission_control_test_ingress_allocated = mission_control_test_ingress_freed = 0;
    mission_control_test_event_allocated = mission_control_test_event_freed = 0;
    mission_control_test_fail_ingress_allocation = true;

    for (int kind = 0; kind < 3; ++kind) {
        mission_control_notification_handler(NULL, NULL, mission_control_notifications[kind], (void *) (uintptr_t) 19);
    }
    TEST_CHECK(mission_control_fallback_schedule_count, 1);
    TEST_CHECK(mission_control_captured_count, 0);
    mission_control_test_fail_ingress_allocation = false;
    mission_control_fallback_drain(NULL);
    TEST_CHECK(mission_control_captured_count, 3);
    TEST_CHECK(mission_control_test_ingress_allocated, 3);
    TEST_CHECK(mission_control_test_ingress_freed, 3);
    EVENT_HANDLER_MISSION_CONTROL_SHOW_ALL_WINDOWS(mission_control_captured_events[0], 1);
    EVENT_HANDLER_MISSION_CONTROL_SHOW_FRONT_WINDOWS(mission_control_captured_events[1], 1);
    EVENT_HANDLER_MISSION_CONTROL_SHOW_DESKTOP(mission_control_captured_events[2], 1);
    TEST_CHECK(mission_control_effect_enter, 1);
    TEST_CHECK(g_mission_control_mode, MISSION_CONTROL_MODE_SHOW_DESKTOP);

    mission_control_test_fail_ingress_allocation = true;
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[3], (void *) (uintptr_t) 19);
    TEST_CHECK(mission_control_fallback_schedule_count, 2);
    mission_control_test_fail_ingress_allocation = false;
    mission_control_fallback_drain(NULL);
    TEST_CHECK(mission_control_captured_count, 4);
    EVENT_HANDLER_MISSION_CONTROL_EXIT(mission_control_captured_events[3], 1);
    TEST_CHECK(g_mission_control_session.poll_pending, true);

    int captured = mission_control_captured_count;
    mission_control_test_fail_ingress_allocation = true;
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[3], (void *) (uintptr_t) 18);
    mission_control_test_fail_ingress_allocation = false;
    TEST_CHECK(mission_control_fallback_schedule_count, 2);
    TEST_CHECK(mission_control_captured_count, captured);
    TEST_CHECK(mission_control_test_event_allocated, mission_control_test_event_freed);

    mission_control_test_fail_ingress_allocation = false;
    mission_control_test_post = saved_post;
    mission_control_test_fallback_schedule = saved_fallback;
    event_loop_test_mission_control_signal = saved_signal;
    event_loop_test_mission_control_schedule = saved_schedule;
    g_mission_control_session = saved_session;
    g_mission_control_mode = saved_mode;
    mission_control_shutdown();
    __atomic_store_n(&g_event_loop.generation, saved_loop_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.is_running, saved_running, __ATOMIC_RELEASE);
)

static pthread_mutex_t mission_control_pause_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mission_control_pause_cond = PTHREAD_COND_INITIALIZER;
static bool mission_control_pause_entered;
static bool mission_control_pause_release;

static void mission_control_pause_hook(void)
{
    pthread_mutex_lock(&mission_control_pause_lock);
    mission_control_pause_entered = true;
    pthread_cond_broadcast(&mission_control_pause_cond);
    while (!mission_control_pause_release) pthread_cond_wait(&mission_control_pause_cond, &mission_control_pause_lock);
    pthread_mutex_unlock(&mission_control_pause_lock);
}

static void mission_control_pause_reset(void)
{
    pthread_mutex_lock(&mission_control_pause_lock);
    mission_control_pause_entered = false;
    mission_control_pause_release = false;
    pthread_mutex_unlock(&mission_control_pause_lock);
}

static void mission_control_pause_wait(void)
{
    pthread_mutex_lock(&mission_control_pause_lock);
    while (!mission_control_pause_entered) pthread_cond_wait(&mission_control_pause_cond, &mission_control_pause_lock);
    pthread_mutex_unlock(&mission_control_pause_lock);
}

static void mission_control_pause_continue(void)
{
    pthread_mutex_lock(&mission_control_pause_lock);
    mission_control_pause_release = true;
    pthread_cond_broadcast(&mission_control_pause_cond);
    pthread_mutex_unlock(&mission_control_pause_lock);
}

static void *mission_control_retry_thread(void *context)
{
    mission_control_observe_retry_attempt(*(uint64_t *) context, 1);
    return NULL;
}

static void *mission_control_restart_thread(void *context)
{
    mission_control_unobserve();
    mission_control_ensure_observed();
    return NULL;
}

TEST_FUNC(mission_control_shutdown_closes_retry_and_restart_admission,
    bool saved_running = g_event_loop.is_running;
    uint64_t saved_loop_generation = g_event_loop.generation;
    __auto_type saved_retry_hook = mission_control_test_retry_before_lock;
    __auto_type saved_ensure_hook = mission_control_test_ensure_before_lock;
    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 81, __ATOMIC_RELEASE);

    mission_control_test_reset();
    mission_control_admission_open(81);
    uint64_t retry_generation = g_mission_control_observe_retry_generation;
    mission_control_pause_reset();
    mission_control_test_retry_before_lock = mission_control_pause_hook;
    pthread_t retry_thread;
    pthread_create(&retry_thread, NULL, mission_control_retry_thread, &retry_generation);
    mission_control_pause_wait();
    mission_control_shutdown();
    mission_control_pause_continue();
    pthread_join(retry_thread, NULL);
    TEST_CHECK(mission_control_test.observer_create_count, 0);
    TEST_CHECK(mission_control_test.notification_add_count, 0);
    TEST_CHECK(mission_control_is_observing(), false);

    mission_control_test_retry_before_lock = NULL;
    mission_control_test_reset();
    mission_control_admission_open(81);
    TEST_CHECK(mission_control_observe(), true);
    mission_control_pause_reset();
    mission_control_test_ensure_before_lock = mission_control_pause_hook;
    pthread_t restart_thread;
    pthread_create(&restart_thread, NULL, mission_control_restart_thread, NULL);
    mission_control_pause_wait();
    mission_control_shutdown();
    mission_control_pause_continue();
    pthread_join(restart_thread, NULL);
    TEST_CHECK(mission_control_is_observing(), false);
    TEST_CHECK(mission_control_test.observer_create_count, 1);
    TEST_CHECK(mission_control_test.notification_add_count, 4);
    TEST_CHECK(mission_control_test.observer_release_count, 1);
    TEST_CHECK(mission_control_test.application_release_count, 1);
    TEST_CHECK(mission_control_test.invalidate_count, 1);

    mission_control_test_retry_before_lock = saved_retry_hook;
    mission_control_test_ensure_before_lock = saved_ensure_hook;
    __atomic_store_n(&g_event_loop.generation, saved_loop_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.is_running, saved_running, __ATOMIC_RELEASE);
)

TEST_FUNC(mission_control_real_event_loop_context_ownership,
    int saved_event_allocated = mission_control_test_event_allocated;
    int saved_event_freed = mission_control_test_event_freed;
    int saved_poll_allocated = event_loop_test_mission_control_poll_allocated;
    int saved_poll_freed = event_loop_test_mission_control_poll_freed;
    mission_control_test_event_allocated = mission_control_test_event_freed = 0;
    event_loop_test_mission_control_poll_allocated = event_loop_test_mission_control_poll_freed = 0;

    struct event_loop loop = {0};
    TEST_CHECK(event_queue_init(&loop.queue, 1), true);
    TEST_CHECK(pthread_mutex_init(&loop.producer_lock, NULL), 0);
    TEST_CHECK(pthread_cond_init(&loop.producer_cond, NULL), 0);
    loop.producer_sync_initialized = true;
    __atomic_store_n(&loop.generation, 101, __ATOMIC_RELEASE);
    __atomic_store_n(&loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);

    struct mission_control_event_context *stale = mission_control_event_context_create();
    *stale = (struct mission_control_event_context) { .epoch = 1, .token = 1, .event_loop_generation = 100,
                                                       .kind = MISSION_CONTROL_NOTIFICATION_SHOW_ALL };
    TEST_CHECK(event_loop_try_post_generation(&loop, 100, MISSION_CONTROL_SHOW_ALL_WINDOWS, stale, 1), false);

    struct mission_control_event_context *queued = mission_control_event_context_create();
    *queued = (struct mission_control_event_context) { .epoch = 1, .token = 2, .event_loop_generation = 101,
                                                        .kind = MISSION_CONTROL_NOTIFICATION_SHOW_DESKTOP };
    TEST_CHECK(event_loop_try_post_generation(&loop, 101, MISSION_CONTROL_SHOW_DESKTOP, queued, 1), true);

    g_event_queue_test_fail_allocation = true;
    struct mission_control_poll_context *rejected_poll = mission_control_poll_context_create();
    *rejected_poll = (struct mission_control_poll_context) { 1, 1 };
    TEST_CHECK(event_loop_try_post_generation(&loop, 101, MISSION_CONTROL_CHECK_FOR_EXIT, rejected_poll, 0), false);
    g_event_queue_test_fail_allocation = false;

    event_loop_cleanup(&loop);

    struct event_loop drain_loop = {0};
    TEST_CHECK(event_queue_init(&drain_loop.queue, 4), true);
    TEST_CHECK(pthread_mutex_init(&drain_loop.producer_lock, NULL), 0);
    TEST_CHECK(pthread_cond_init(&drain_loop.producer_cond, NULL), 0);
    drain_loop.producer_sync_initialized = true;
    __atomic_store_n(&drain_loop.generation, 102, __ATOMIC_RELEASE);
    __atomic_store_n(&drain_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&drain_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    struct mission_control_event_context *drain_show = mission_control_event_context_create();
    *drain_show = (struct mission_control_event_context) { .event_loop_generation = 102,
        .kind = MISSION_CONTROL_NOTIFICATION_SHOW_ALL };
    struct mission_control_event_context *drain_exit = mission_control_event_context_create();
    *drain_exit = (struct mission_control_event_context) { .event_loop_generation = 102,
        .kind = MISSION_CONTROL_NOTIFICATION_EXIT };
    struct mission_control_poll_context *drain_poll = mission_control_poll_context_create();
    *drain_poll = (struct mission_control_poll_context) { 2, 2 };
    TEST_CHECK(event_loop_try_post_generation(&drain_loop, 102, MISSION_CONTROL_SHOW_ALL_WINDOWS, drain_show, 1), true);
    TEST_CHECK(event_loop_try_post_generation(&drain_loop, 102, MISSION_CONTROL_EXIT, drain_exit, 1), true);
    TEST_CHECK(event_loop_try_post_generation(&drain_loop, 102, MISSION_CONTROL_CHECK_FOR_EXIT, drain_poll, 0), true);
    event_loop_cleanup(&drain_loop);

    TEST_CHECK(mission_control_test_event_allocated, 4);
    TEST_CHECK(mission_control_test_event_freed, 4);
    TEST_CHECK(event_loop_test_mission_control_poll_allocated, 2);
    TEST_CHECK(event_loop_test_mission_control_poll_freed, 2);

    mission_control_test_event_allocated = saved_event_allocated;
    mission_control_test_event_freed = saved_event_freed;
    event_loop_test_mission_control_poll_allocated = saved_poll_allocated;
    event_loop_test_mission_control_poll_freed = saved_poll_freed;
)

TEST_FUNC(mission_control_show_a_show_b_delayed_exit_confirms_b,
    bool saved_running = g_event_loop.is_running;
    uint64_t saved_loop_generation = g_event_loop.generation;
    __auto_type saved_post = mission_control_test_post;
    __auto_type saved_signal = event_loop_test_mission_control_signal;
    __auto_type saved_correct = event_loop_test_mission_control_correct;
    __auto_type saved_dirty = event_loop_test_mission_control_menu_dirty;
    __auto_type saved_reconcile = event_loop_test_mission_control_reconcile;
    __auto_type saved_schedule = event_loop_test_mission_control_schedule;
    __auto_type saved_copy = event_loop_test_window_list_copy;
    struct mission_control_session_state saved_session = g_mission_control_session;
    enum mission_control_mode saved_mode = g_mission_control_mode;
    float saved_opacity = g_window_manager.menubar_opacity;

    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 91, __ATOMIC_RELEASE);
    mission_control_admission_open(91);
    __atomic_store_n(&g_mission_control_observer.epoch, 29, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observer.notification_token, 0, __ATOMIC_RELEASE);
    mission_control_test_post = mission_control_capture_post;
    event_loop_test_mission_control_signal = mission_control_effect_signal;
    event_loop_test_mission_control_correct = mission_control_effect_correct_fn;
    event_loop_test_mission_control_menu_dirty = mission_control_effect_dirty_fn;
    event_loop_test_mission_control_reconcile = mission_control_effect_reconcile_fn;
    event_loop_test_mission_control_schedule = mission_control_effect_schedule;
    event_loop_test_window_list_copy = mission_control_test_copy_window_list;
    g_window_manager.menubar_opacity = 1.0f;
    mission_control_captured_count = 0;
    mission_control_effect_enter = mission_control_effect_exit = 0;
    mission_control_effect_correct = mission_control_effect_dirty = mission_control_effect_reconcile = 0;

    int64_t layer = 18;
    CFNumberRef layer_number = CFNumberCreate(NULL, kCFNumberSInt64Type, &layer);
    const void *keys[] = { kCGWindowOwnerName, kCGWindowLayer };
    const void *values[] = { CFSTR("Dock"), layer_number };
    CFDictionaryRef dock = CFDictionaryCreate(NULL, keys, values, 2,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    const void *dock_item[] = { dock };
    mission_control_samples[0] = CFArrayCreate(NULL, dock_item, 1, &kCFTypeArrayCallBacks);
    mission_control_samples[1] = CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
    mission_control_sample_index = 0;

    mission_control_notification_handler(NULL, NULL, mission_control_notifications[2], (void *) (uintptr_t) 29);
    EVENT_HANDLER_MISSION_CONTROL_SHOW_DESKTOP(mission_control_captured_events[0], 1);
    uint64_t generation_a = g_mission_control_session.generation;
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[0], (void *) (uintptr_t) 29);
    EVENT_HANDLER_MISSION_CONTROL_SHOW_ALL_WINDOWS(mission_control_captured_events[1], 1);
    uint64_t generation_b = g_mission_control_session.generation;
    TEST_CHECK(generation_b != generation_a, true);

    mission_control_notification_handler(NULL, NULL, mission_control_notifications[3], (void *) (uintptr_t) 29);
    EVENT_HANDLER_MISSION_CONTROL_EXIT(mission_control_captured_events[2], 1);
    struct mission_control_poll_context *active_poll = malloc(sizeof(*active_poll));
    *active_poll = (struct mission_control_poll_context) { mission_control_effect_poll_generation, mission_control_effect_poll_token };
    EVENT_HANDLER_MISSION_CONTROL_CHECK_FOR_EXIT(active_poll, 0);
    TEST_CHECK(g_mission_control_session.active, true);
    TEST_CHECK(g_mission_control_session.generation, generation_b);
    TEST_CHECK(mission_control_effect_exit, 0);

    mission_control_notification_handler(NULL, NULL, mission_control_notifications[3], (void *) (uintptr_t) 29);
    EVENT_HANDLER_MISSION_CONTROL_EXIT(mission_control_captured_events[3], 1);
    struct mission_control_poll_context *inactive_poll = malloc(sizeof(*inactive_poll));
    *inactive_poll = (struct mission_control_poll_context) { mission_control_effect_poll_generation, mission_control_effect_poll_token };
    EVENT_HANDLER_MISSION_CONTROL_CHECK_FOR_EXIT(inactive_poll, 0);
    TEST_CHECK(g_mission_control_session.active, false);
    TEST_CHECK(mission_control_effect_enter, 2);
    TEST_CHECK(mission_control_effect_exit, 1);
    TEST_CHECK(mission_control_effect_correct, 1);
    TEST_CHECK(mission_control_effect_dirty, 1);
    TEST_CHECK(mission_control_effect_reconcile, 1);

    CFRelease(mission_control_samples[0]); CFRelease(mission_control_samples[1]);
    CFRelease(dock); CFRelease(layer_number);
    mission_control_samples[0] = mission_control_samples[1] = NULL;
    mission_control_test_post = saved_post;
    event_loop_test_mission_control_signal = saved_signal;
    event_loop_test_mission_control_correct = saved_correct;
    event_loop_test_mission_control_menu_dirty = saved_dirty;
    event_loop_test_mission_control_reconcile = saved_reconcile;
    event_loop_test_mission_control_schedule = saved_schedule;
    event_loop_test_window_list_copy = saved_copy;
    g_mission_control_session = saved_session;
    g_mission_control_mode = saved_mode;
    g_window_manager.menubar_opacity = saved_opacity;
    mission_control_shutdown();
    __atomic_store_n(&g_event_loop.generation, saved_loop_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.is_running, saved_running, __ATOMIC_RELEASE);
)

static void mission_control_dispatch_captured(int index)
{
    switch (mission_control_captured_types[index]) {
    case MISSION_CONTROL_SHOW_ALL_WINDOWS:
        EVENT_HANDLER_MISSION_CONTROL_SHOW_ALL_WINDOWS(mission_control_captured_events[index], 1);
        break;
    case MISSION_CONTROL_SHOW_FRONT_WINDOWS:
        EVENT_HANDLER_MISSION_CONTROL_SHOW_FRONT_WINDOWS(mission_control_captured_events[index], 1);
        break;
    case MISSION_CONTROL_SHOW_DESKTOP:
        EVENT_HANDLER_MISSION_CONTROL_SHOW_DESKTOP(mission_control_captured_events[index], 1);
        break;
    case MISSION_CONTROL_EXIT:
        EVENT_HANDLER_MISSION_CONTROL_EXIT(mission_control_captured_events[index], 1);
        break;
    default:
        mission_control_event_context_destroy(mission_control_captured_events[index]);
        break;
    }
}

TEST_FUNC(mission_control_fallback_ring_preserves_order_overflow_and_reuse,
    bool saved_running = g_event_loop.is_running;
    uint64_t saved_loop_generation = g_event_loop.generation;
    __auto_type saved_post = mission_control_test_post;
    __auto_type saved_fallback = mission_control_test_fallback_schedule;
    __auto_type saved_signal = event_loop_test_mission_control_signal;
    __auto_type saved_schedule = event_loop_test_mission_control_schedule;
    struct mission_control_session_state saved_session = g_mission_control_session;
    enum mission_control_mode saved_mode = g_mission_control_mode;

    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 92, __ATOMIC_RELEASE);
    mission_control_admission_open(92);
    __atomic_store_n(&g_mission_control_observer.epoch, 39, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observer.notification_token, 0, __ATOMIC_RELEASE);
    mission_control_test_post = mission_control_capture_post;
    mission_control_test_fallback_schedule = mission_control_capture_fallback;
    event_loop_test_mission_control_signal = mission_control_effect_signal;
    event_loop_test_mission_control_schedule = mission_control_effect_schedule;
    mission_control_captured_count = 0;
    mission_control_fallback_schedule_count = 0;
    mission_control_effect_enter = mission_control_effect_exit = 0;
    mission_control_test_event_allocated = mission_control_test_event_freed = 0;
    mission_control_test_fail_ingress_allocation = true;

    int stream_one[] = { 0, 2, 0 };
    for (int i = 0; i < 3; ++i) {
        mission_control_notification_handler(NULL, NULL, mission_control_notifications[stream_one[i]], (void *) (uintptr_t) 39);
    }
    TEST_CHECK(mission_control_fallback_schedule_count, 1);
    mission_control_test_fail_ingress_allocation = false;
    mission_control_fallback_drain(NULL);
    TEST_CHECK(mission_control_captured_types[0], MISSION_CONTROL_SHOW_ALL_WINDOWS);
    TEST_CHECK(mission_control_captured_types[1], MISSION_CONTROL_SHOW_DESKTOP);
    TEST_CHECK(mission_control_captured_types[2], MISSION_CONTROL_SHOW_ALL_WINDOWS);
    for (int i = 0; i < 3; ++i) mission_control_dispatch_captured(i);
    TEST_CHECK(g_mission_control_mode, MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS);
    TEST_CHECK(mission_control_effect_enter, 1);

    int start = mission_control_captured_count;
    mission_control_test_fail_ingress_allocation = true;
    int stream_two[] = { 0, 3, 0 };
    for (int i = 0; i < 3; ++i) {
        mission_control_notification_handler(NULL, NULL, mission_control_notifications[stream_two[i]], (void *) (uintptr_t) 39);
    }
    mission_control_test_fail_ingress_allocation = false;
    mission_control_fallback_drain(NULL);
    TEST_CHECK(mission_control_captured_types[start], MISSION_CONTROL_SHOW_ALL_WINDOWS);
    TEST_CHECK(mission_control_captured_types[start + 1], MISSION_CONTROL_EXIT);
    TEST_CHECK(mission_control_captured_types[start + 2], MISSION_CONTROL_SHOW_ALL_WINDOWS);
    for (int i = start; i < start + 3; ++i) mission_control_dispatch_captured(i);
    TEST_CHECK(g_mission_control_mode, MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS);
    TEST_CHECK(mission_control_effect_exit, 0);

    start = mission_control_captured_count;
    mission_control_test_fail_ingress_allocation = true;
    for (int i = 0; i < MISSION_CONTROL_FALLBACK_CAPACITY + 8; ++i) {
        int notification = (i & 1) ? 2 : 0;
        mission_control_notification_handler(NULL, NULL, mission_control_notifications[notification], (void *) (uintptr_t) 39);
    }
    TEST_CHECK(g_mission_control_fallback.count, MISSION_CONTROL_FALLBACK_CAPACITY);
    mission_control_test_fail_ingress_allocation = false;
    mission_control_fallback_drain(NULL);
    int overflow_end = mission_control_captured_count;
    TEST_CHECK(overflow_end - start, MISSION_CONTROL_FALLBACK_CAPACITY);
    TEST_CHECK(mission_control_captured_types[overflow_end - 2], MISSION_CONTROL_SHOW_DESKTOP);
    TEST_CHECK(mission_control_captured_types[overflow_end - 1], MISSION_CONTROL_EXIT);
    for (int i = start; i < overflow_end; ++i) mission_control_dispatch_captured(i);
    TEST_CHECK(g_mission_control_mode, MISSION_CONTROL_MODE_SHOW_DESKTOP);
    TEST_CHECK(g_mission_control_session.active, true);
    TEST_CHECK(g_mission_control_session.poll_pending, true);

    start = mission_control_captured_count;
    mission_control_test_fail_ingress_allocation = true;
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[0], (void *) (uintptr_t) 39);
    mission_control_admission_open(92);
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[2], (void *) (uintptr_t) 39);
    mission_control_test_fail_ingress_allocation = false;
    mission_control_fallback_drain(NULL);
    mission_control_fallback_drain(NULL);
    TEST_CHECK(mission_control_captured_count - start, 1);
    TEST_CHECK(mission_control_captured_types[start], MISSION_CONTROL_SHOW_DESKTOP);
    mission_control_dispatch_captured(start);
    TEST_CHECK(mission_control_effect_enter, 4);
    TEST_CHECK(mission_control_effect_exit, 0);
    TEST_CHECK(mission_control_test_ingress_allocated, mission_control_test_ingress_freed);
    TEST_CHECK(mission_control_test_event_allocated, mission_control_test_event_freed);

    mission_control_test_fail_ingress_allocation = false;
    mission_control_test_post = saved_post;
    mission_control_test_fallback_schedule = saved_fallback;
    event_loop_test_mission_control_signal = saved_signal;
    event_loop_test_mission_control_schedule = saved_schedule;
    g_mission_control_session = saved_session;
    g_mission_control_mode = saved_mode;
    mission_control_shutdown();
    __atomic_store_n(&g_event_loop.generation, saved_loop_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.is_running, saved_running, __ATOMIC_RELEASE);
)

static void *mission_control_fallback_drain_thread(void *context)
{
    mission_control_fallback_drain(NULL);
    return NULL;
}

static void *mission_control_unobserve_thread(void *context)
{
    mission_control_unobserve();
    return NULL;
}

TEST_FUNC(mission_control_fallback_concurrent_cancel_and_drain_is_owned,
    bool saved_running = g_event_loop.is_running;
    uint64_t saved_loop_generation = g_event_loop.generation;
    __auto_type saved_post = mission_control_test_post;
    __auto_type saved_fallback = mission_control_test_fallback_schedule;
    __auto_type saved_pause = mission_control_test_fallback_before_allocate;

    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 93, __ATOMIC_RELEASE);
    mission_control_admission_open(93);
    __atomic_store_n(&g_mission_control_observer.epoch, 49, __ATOMIC_RELEASE);
    mission_control_test_post = mission_control_capture_post;
    mission_control_test_fallback_schedule = mission_control_capture_fallback;
    mission_control_captured_count = 0;
    mission_control_test_ingress_allocated = mission_control_test_ingress_freed = 0;
    mission_control_test_event_allocated = mission_control_test_event_freed = 0;
    mission_control_test_fail_ingress_allocation = true;
    mission_control_notification_handler(NULL, NULL, mission_control_notifications[0], (void *) (uintptr_t) 49);
    mission_control_test_fail_ingress_allocation = false;

    mission_control_pause_reset();
    mission_control_test_fallback_before_allocate = mission_control_pause_hook;
    pthread_t drain_thread;
    pthread_create(&drain_thread, NULL, mission_control_fallback_drain_thread, NULL);
    mission_control_pause_wait();
    pthread_t cancel_thread;
    pthread_create(&cancel_thread, NULL, mission_control_unobserve_thread, NULL);
    mission_control_pause_continue();
    pthread_join(drain_thread, NULL);
    pthread_join(cancel_thread, NULL);
    mission_control_test_fallback_before_allocate = NULL;

    TEST_CHECK(g_mission_control_fallback.count, 0);
    TEST_CHECK(mission_control_is_observing(), false);
    for (int i = 0; i < mission_control_captured_count; ++i) mission_control_dispatch_captured(i);
    mission_control_shutdown();
    TEST_CHECK(mission_control_test_ingress_allocated, mission_control_test_ingress_freed);
    TEST_CHECK(mission_control_test_event_allocated, mission_control_test_event_freed);

    mission_control_test_post = saved_post;
    mission_control_test_fallback_schedule = saved_fallback;
    mission_control_test_fallback_before_allocate = saved_pause;
    __atomic_store_n(&g_event_loop.generation, saved_loop_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.is_running, saved_running, __ATOMIC_RELEASE);
)
