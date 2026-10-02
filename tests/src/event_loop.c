static AXError application_test_create_result;
static AXError application_test_notification_results[7];
static int application_test_notification_index;
static int application_test_source_count;

static AXError application_test_observer_create(pid_t pid, AXObserverCallback callback, AXObserverRef *observer)
{
    (void) pid;
    (void) callback;
    if (application_test_create_result == kAXErrorSuccess) *observer = (AXObserverRef)(intptr_t) 1;
    return application_test_create_result;
}

static AXError application_test_observer_add_notification(AXObserverRef observer, AXUIElementRef element,
                                                          CFStringRef notification, void *context)
{
    (void) observer;
    (void) element;
    (void) notification;
    (void) context;
    return application_test_notification_results[application_test_notification_index++];
}

static void application_test_add_run_loop_source(AXObserverRef observer)
{
    (void) observer;
    ++application_test_source_count;
}

static void application_test_reset(AXError default_result)
{
    application_test_create_result = kAXErrorSuccess;
    application_test_notification_index = 0;
    application_test_source_count = 0;
    for (int i = 0; i < array_count(application_test_notification_results); ++i) {
        application_test_notification_results[i] = default_result;
    }
}

TEST_FUNC(application_observation_retries_only_transient_required_failures,
{
    __auto_type saved_create = application_observer_create;
    __auto_type saved_add_notification = application_observer_add_notification;
    __auto_type saved_add_source = application_observer_add_run_loop_source;
    application_observer_create = application_test_observer_create;
    application_observer_add_notification = application_test_observer_add_notification;
    application_observer_add_run_loop_source = application_test_add_run_loop_source;

    struct application application = { .name = "test" };
    application_test_reset(kAXErrorSuccess);
    application_test_notification_results[AX_APPLICATION_WINDOW_MENU_OPENED_INDEX] = kAXErrorNotImplemented;
    application_test_notification_results[AX_APPLICATION_WINDOW_MENU_CLOSED_INDEX] = kAXErrorNotImplemented;
    TEST_CHECK(application_observe(&application), true);
    TEST_CHECK(application.ax_retry, false);
    TEST_CHECK(application_test_source_count, 1);

    application = (struct application) { .name = "test" };
    application_test_reset(kAXErrorSuccess);
    application_test_notification_results[AX_APPLICATION_WINDOW_CREATED_INDEX] = kAXErrorCannotComplete;
    TEST_CHECK(application_observe(&application), false);
    TEST_CHECK(application.ax_retry, true);
    TEST_CHECK(application_test_source_count, 1);

    application = (struct application) { .name = "test" };
    application_test_reset(kAXErrorSuccess);
    application_test_notification_results[AX_APPLICATION_WINDOW_CREATED_INDEX] = kAXErrorCannotComplete;
    application_test_notification_results[AX_APPLICATION_WINDOW_FOCUSED_INDEX] = kAXErrorNotImplemented;
    TEST_CHECK(application_observe(&application), false);
    TEST_CHECK(application.ax_retry, false);

    application = (struct application) { .name = "test" };
    application_test_reset(kAXErrorSuccess);
    application_test_create_result = kAXErrorCannotComplete;
    TEST_CHECK(application_observe(&application), false);
    TEST_CHECK(application.ax_retry, true);
    TEST_CHECK(application_test_notification_index, 0);
    TEST_CHECK(application_test_source_count, 0);

    application_observer_create = saved_create;
    application_observer_add_notification = saved_add_notification;
    application_observer_add_run_loop_source = saved_add_source;
});

static uint64_t test_publish_tracked_front_switch(ProcessSerialNumber psn)
{
    pthread_mutex_lock(&g_process_manager_front_switch_lock);
    uint64_t generation = process_manager_publish_tracked_front_switch_locked(&g_process_manager, psn);
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);
    return generation;
}

TEST_FUNC(application_front_switched_owned_identity_rejects_stale_generation_and_pid_reuse,
{
    struct table saved_application_table = g_window_manager.application;
    struct table saved_lost_event_table = g_window_manager.application_lost_front_switched_event;
    struct event_loop saved_event_loop = g_event_loop;
    pid_t saved_front_pid = g_process_manager.front_pid;
    pid_t saved_last_front_pid = g_process_manager.last_front_pid;
    ProcessSerialNumber saved_processed_front_psn = g_process_manager.front_psn;
    ProcessSerialNumber saved_processed_last_front_psn = g_process_manager.last_front_psn;
    uint64_t saved_generation = g_process_manager.front_switch_generation;
    uint64_t saved_front_psn = g_process_manager.front_switch_psn;
    g_window_manager.application = (struct table) {0};
    g_window_manager.application_lost_front_switched_event = (struct table) {0};
    table_init(&g_window_manager.application, 4, hash_wm, compare_wm);
    table_init(&g_window_manager.application_lost_front_switched_event, 4, hash_psn, compare_psn);

    ProcessSerialNumber psn_a = { .highLongOfPSN = 1, .lowLongOfPSN = 2 };
    ProcessSerialNumber psn_b = { .highLongOfPSN = 9, .lowLongOfPSN = 10 };
    __atomic_store_n(&g_process_manager.front_switch_generation, 10, __ATOMIC_RELEASE);
    __atomic_store_n(&g_process_manager.front_switch_psn, 0, __ATOMIC_RELEASE);
    g_process_manager.front_pid = 777;

    uint64_t generation_a = test_publish_tracked_front_switch(psn_a);
    uint64_t generation_b = test_publish_tracked_front_switch(psn_b);
    TEST_CHECK(generation_a, 11U);
    TEST_CHECK(generation_b, 12U);
    process_manager_invalidate_tracked_front_switch_if_matches(&g_process_manager, psn_a);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE), generation_b);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_psn, __ATOMIC_ACQUIRE), process_manager_pack_psn(psn_b));

    struct application_front_switched_event *identity = malloc(sizeof(struct application_front_switched_event));
    *identity = (struct application_front_switched_event) { .psn = psn_a, .pid = 100, .generation = generation_a, .replay = false };
    EVENT_HANDLER_APPLICATION_FRONT_SWITCHED(identity, 0);
    TEST_CHECK(g_process_manager.front_pid, 777);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_a), 0U);

    identity = malloc(sizeof(struct application_front_switched_event));
    *identity = (struct application_front_switched_event) { .psn = psn_a, .pid = 100, .generation = generation_a, .replay = true };
    EVENT_HANDLER_APPLICATION_FRONT_SWITCHED(identity, 0);
    TEST_CHECK(g_process_manager.front_pid, 777);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_a), 0U);

    struct application reused_application = { .psn = psn_b, .pid = 100, .name = "reused" };
    table_add(&g_window_manager.application, &reused_application.pid, &reused_application);
    g_process_manager.front_pid = 100;
    g_process_manager.front_psn = psn_a;
    g_process_manager.last_front_pid = 100;
    g_process_manager.last_front_psn = psn_a;
    TEST_CHECK(event_loop_find_processed_front_application() == NULL, true);
    TEST_CHECK(event_signal_application_is_processed_front(&reused_application, false), false);
    TEST_CHECK(event_signal_application_is_processed_front(&reused_application, true), false);
    g_process_manager.front_pid = 777;
    g_process_manager.front_psn = (ProcessSerialNumber) {0};
    identity = malloc(sizeof(struct application_front_switched_event));
    *identity = (struct application_front_switched_event) { .psn = psn_a, .pid = 100, .generation = generation_b, .replay = false };
    EVENT_HANDLER_APPLICATION_FRONT_SWITCHED(identity, 0);
    TEST_CHECK(g_process_manager.front_pid, 777);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_a), 0U);

    struct application_front_switched_event commit_event = { .psn = psn_b, .pid = 100, .generation = generation_b, .replay = false };
    struct application *committed_application;
    struct application *committed_deactivated_application;
    TEST_CHECK(event_loop_commit_application_front_switched(&commit_event, &committed_application, &committed_deactivated_application), true);
    TEST_CHECK(committed_application == &reused_application, true);
    TEST_CHECK(committed_deactivated_application == NULL, true);
    TEST_CHECK(pthread_mutex_trylock(&g_process_manager_front_switch_lock), 0);
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);
    g_process_manager.front_pid = 777;
    g_process_manager.front_psn = (ProcessSerialNumber) {0};

    identity = malloc(sizeof(struct application_front_switched_event));
    *identity = (struct application_front_switched_event) { .psn = psn_b, .pid = 100, .generation = generation_b, .replay = true };
    process_manager_invalidate_tracked_front_switch_if_matches(&g_process_manager, psn_b);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE), 13U);
    EVENT_HANDLER_APPLICATION_FRONT_SWITCHED(identity, 0);
    TEST_CHECK(g_process_manager.front_pid, 777);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), 0U);
    table_remove(&g_window_manager.application, &reused_application.pid);

    uint64_t current_generation = test_publish_tracked_front_switch(psn_b);
    identity = malloc(sizeof(struct application_front_switched_event));
    *identity = (struct application_front_switched_event) { .psn = psn_b, .pid = 100, .generation = current_generation, .replay = false };
    EVENT_HANDLER_APPLICATION_FRONT_SWITCHED(identity, 0);
    TEST_CHECK(g_process_manager.front_pid, 777);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), current_generation);

    process_manager_invalidate_tracked_front_switch_if_matches(&g_process_manager, psn_b);
    g_process_manager.front_pid = 100;
    g_process_manager.front_psn = psn_b;
    g_process_manager.last_front_pid = 100;
    g_process_manager.last_front_psn = psn_b;
    struct process *terminated_process = calloc(1, sizeof(struct process));
    terminated_process->psn = psn_b;
    terminated_process->pid = 100;
    terminated_process->name = strdup("terminated");
    EVENT_HANDLER_APPLICATION_TERMINATED(terminated_process, 0);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), 0U);
    TEST_CHECK(g_process_manager.front_pid, 0);
    TEST_CHECK(g_process_manager.last_front_pid, 0);
    g_process_manager.front_pid = 777;

    identity = malloc(sizeof(struct application_front_switched_event));
    *identity = (struct application_front_switched_event) { .psn = psn_b, .pid = 100, .generation = current_generation, .replay = true };
    EVENT_HANDLER_APPLICATION_FRONT_SWITCHED(identity, 0);
    TEST_CHECK(g_process_manager.front_pid, 777);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), 0U);

    struct process replay_process = { .psn = psn_b, .pid = 100, .name = "reused" };
    event_loop_replay_lost_front_switched(&replay_process);
    TEST_CHECK(pthread_mutex_trylock(&g_process_manager_front_switch_lock), 0);
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);

    window_manager_add_lost_front_switched_event(&g_window_manager, &psn_b, generation_b);
    event_loop_replay_lost_front_switched(&replay_process);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), 0U);
    TEST_CHECK(pthread_mutex_trylock(&g_process_manager_front_switch_lock), 0);
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);

    replay_process.terminated = true;
    uint64_t terminated_generation = g_process_manager.front_switch_generation;
    window_manager_add_lost_front_switched_event(&g_window_manager, &psn_b, terminated_generation);
    event_loop_replay_lost_front_switched(&replay_process);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), 0U);
    TEST_CHECK(pthread_mutex_trylock(&g_process_manager_front_switch_lock), 0);
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);

    replay_process.terminated = false;
    uint64_t replay_generation = test_publish_tracked_front_switch(psn_b);
    window_manager_add_lost_front_switched_event(&g_window_manager, &psn_b, replay_generation);

    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 4), true);
    char replay_semaphore_name[64];
    snprintf(replay_semaphore_name, sizeof(replay_semaphore_name), "/yabai_replay_test_%d", getpid());
    sem_t *replay_semaphore = sem_open(replay_semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(replay_semaphore_name);
    TEST_CHECK(replay_semaphore != SEM_FAILED, true);
    g_event_loop.semaphore = replay_semaphore;
    event_loop_replay_lost_front_switched(&replay_process);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), 0U);
    TEST_CHECK(g_event_loop.queue.count, 1U);
    struct event accepted_replay_event;
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &accepted_replay_event), true);
    event_loop_release_event_context(accepted_replay_event.type, accepted_replay_event.context);
    event_queue_destroy(&g_event_loop.queue);
    if (replay_semaphore != SEM_FAILED) sem_close(replay_semaphore);
    TEST_CHECK(pthread_mutex_trylock(&g_process_manager_front_switch_lock), 0);
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);

    window_manager_add_lost_front_switched_event(&g_window_manager, &psn_b, replay_generation);
    g_event_loop = (struct event_loop) {0};
    struct event replaced = {0};
    uint32_t capacity = 0;
    bool post_wake = false;
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 1), true);
    TEST_CHECK(event_queue_push(&g_event_loop.queue, (struct event) { .type = WINDOW_MOVED }, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    g_event_queue_test_fail_allocation = true;
    uint64_t generation_before_rejection = g_process_manager.front_switch_generation;
    uint64_t psn_before_rejection = g_process_manager.front_switch_psn;
    struct process rejected_process = { .psn = psn_a, .pid = 101, .name = "rejected" };
    TEST_CHECK(event_loop_post_tracked_application_front_switched(&rejected_process), false);
    TEST_CHECK(g_process_manager.front_switch_generation, generation_before_rejection);
    TEST_CHECK(g_process_manager.front_switch_psn, psn_before_rejection);
    event_loop_replay_lost_front_switched(&replay_process);
    g_event_queue_test_fail_allocation = false;
    TEST_CHECK(g_event_loop.queue.count, 1U);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), replay_generation);
    TEST_CHECK(pthread_mutex_trylock(&g_process_manager_front_switch_lock), 0);
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);

    event_queue_destroy(&g_event_loop.queue);
    g_event_loop = saved_event_loop;
    table_free(&g_window_manager.application);
    table_free(&g_window_manager.application_lost_front_switched_event);
    g_window_manager.application = saved_application_table;
    g_window_manager.application_lost_front_switched_event = saved_lost_event_table;
    g_process_manager.front_pid = saved_front_pid;
    g_process_manager.last_front_pid = saved_last_front_pid;
    g_process_manager.front_psn = saved_processed_front_psn;
    g_process_manager.last_front_psn = saved_processed_last_front_psn;
    __atomic_store_n(&g_process_manager.front_switch_generation, saved_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_process_manager.front_switch_psn, saved_front_psn, __ATOMIC_RELEASE);
});

TEST_FUNC(window_removal_clears_focus_and_recycled_identity_state,
{
    struct window_manager wm = { 0 };
    table_init(&wm.window_lost_focused_event, 4, hash_wm, compare_wm);
    wm.focused_window_id = 42;
    wm.focused_window_psn = (ProcessSerialNumber) { .highLongOfPSN = 1, .lowLongOfPSN = 2 };
    wm.last_window_id = 42;
    window_manager_add_lost_focused_event(&wm, 42);

    event_loop_clear_window_identity(&wm, 42);

    TEST_CHECK(wm.focused_window_id, 0U);
    TEST_CHECK(wm.focused_window_psn.highLongOfPSN, 0U);
    TEST_CHECK(wm.focused_window_psn.lowLongOfPSN, 0U);
    TEST_CHECK(wm.last_window_id, 0U);
    TEST_CHECK(window_manager_find_lost_focused_event(&wm, 42), false);
    table_free(&wm.window_lost_focused_event);
});

TEST_FUNC(window_minimize_events_resolve_stable_ids,
{
    struct table saved_window_table = g_window_manager.window;
    struct table saved_lost_event_table = g_window_manager.window_lost_focused_event;
    g_window_manager.window = (struct table) {0};
    g_window_manager.window_lost_focused_event = (struct table) {0};
    table_init(&g_window_manager.window, 4, hash_wm, compare_wm);
    table_init(&g_window_manager.window_lost_focused_event, 4, hash_wm, compare_wm);

    window_manager_add_lost_focused_event(&g_window_manager, 77);
    EVENT_HANDLER_WINDOW_MINIMIZED((void *)(intptr_t) 77, 0);
    TEST_CHECK(window_manager_find_lost_focused_event(&g_window_manager, 77), true);
    EVENT_HANDLER_WINDOW_DEMINIMIZED((void *)(intptr_t) 77, 0);
    TEST_CHECK(window_manager_find_lost_focused_event(&g_window_manager, 77), false);

    table_free(&g_window_manager.window);
    table_free(&g_window_manager.window_lost_focused_event);
    g_window_manager.window = saved_window_table;
    g_window_manager.window_lost_focused_event = saved_lost_event_table;
});

TEST_FUNC(display_change_rejects_invalid_pairs_before_commit,
{
    struct display_manager dm = { .last_display_id = 1, .current_display_id = 2 };
    struct space_manager sm = { .last_space_id = 10, .current_space_id = 20 };

    TEST_CHECK(event_loop_commit_display_change(&dm, &sm, 0, 30, 0), false);
    TEST_CHECK(event_loop_commit_display_change(&dm, &sm, 3, 0, 3), false);
    TEST_CHECK(event_loop_commit_display_change(&dm, &sm, 3, 30, 4), false);
    TEST_CHECK(dm.last_display_id, 1U);
    TEST_CHECK(dm.current_display_id, 2U);
    TEST_CHECK(sm.last_space_id, 10ULL);
    TEST_CHECK(sm.current_space_id, 20ULL);

    TEST_CHECK(event_loop_commit_display_change(&dm, &sm, 3, 30, 3), true);
    TEST_CHECK(dm.last_display_id, 2U);
    TEST_CHECK(dm.current_display_id, 3U);
    TEST_CHECK(sm.last_space_id, 20ULL);
    TEST_CHECK(sm.current_space_id, 30ULL);
});

TEST_FUNC(update_window_notifications_append_grows_past_fixed_boundary,
{
    uint32_t *window_list = NULL;
    size_t window_count = 0;
    size_t window_capacity = 0;
    const size_t fixed_boundary = 1024;

    for (size_t i = 0; i < fixed_boundary + 3; ++i) {
        TEST_CHECK(update_window_notifications_append(&window_list, &window_count, &window_capacity, (uint32_t) (1000 + i)), true);
    }

    TEST_CHECK(window_count, fixed_boundary + 3);
    TEST_CHECK(window_capacity >= window_count, true);
    TEST_CHECK(window_list[0], 1000u);
    TEST_CHECK(window_list[fixed_boundary + 2], (uint32_t) (1000 + fixed_boundary + 2));

    free(window_list);
});
