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

TEST_FUNC(application_termination_rejects_stale_pid_reuse_identity,
{
    struct table saved_applications = g_window_manager.application;
    struct table saved_windows = g_window_manager.window;
    struct table saved_lost = g_window_manager.application_lost_front_switched_event;
    struct application **saved_refresh = g_window_manager.applications_to_refresh;
    struct rule *saved_rules = g_window_manager.rules;
    g_window_manager.application = (struct table) {0};
    g_window_manager.window = (struct table) {0};
    g_window_manager.application_lost_front_switched_event = (struct table) {0};
    g_window_manager.applications_to_refresh = NULL;
    g_window_manager.rules = NULL;
    table_init(&g_window_manager.application, 4, hash_wm, compare_wm);
    table_init(&g_window_manager.window, 4, hash_wm, compare_wm);
    table_init(&g_window_manager.application_lost_front_switched_event, 4, hash_psn, compare_psn);

    ProcessSerialNumber old_psn = { .highLongOfPSN = 1, .lowLongOfPSN = 1 };
    ProcessSerialNumber replacement_psn = { .highLongOfPSN = 2, .lowLongOfPSN = 2 };
    struct application replacement = { .pid = 404, .psn = replacement_psn, .name = "replacement", .launch_refresh = true };
    struct window replacement_window = { .id = 505, .application = &replacement };
    struct rule reserved = { .flags = RULE_ONE_SHOT, .one_shot_owner_pid = 404 };
    table_add(&g_window_manager.application, &replacement.pid, &replacement);
    table_add(&g_window_manager.window, &replacement_window.id, &replacement_window);
    buf_push(g_window_manager.applications_to_refresh, &replacement);
    buf_push(g_window_manager.rules, reserved);

    struct process *stale = calloc(1, sizeof(*stale));
    stale->pid = replacement.pid;
    stale->psn = old_psn;
    stale->name = strdup("stale");
    EVENT_HANDLER_APPLICATION_TERMINATED(stale, 0);

    TEST_CHECK(window_manager_find_application(&g_window_manager, replacement.pid) == &replacement, true);
    TEST_CHECK(window_manager_find_window(&g_window_manager, replacement_window.id) == &replacement_window, true);
    TEST_CHECK(buf_len(g_window_manager.applications_to_refresh), 1);
    TEST_CHECK(g_window_manager.applications_to_refresh[0] == &replacement, true);
    TEST_CHECK(buf_len(g_window_manager.rules), 1);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, replacement.pid);

    buf_free(g_window_manager.applications_to_refresh);
    buf_free(g_window_manager.rules);
    table_free(&g_window_manager.application);
    table_free(&g_window_manager.window);
    table_free(&g_window_manager.application_lost_front_switched_event);
    g_window_manager.application = saved_applications;
    g_window_manager.window = saved_windows;
    g_window_manager.application_lost_front_switched_event = saved_lost;
    g_window_manager.applications_to_refresh = saved_refresh;
    g_window_manager.rules = saved_rules;
})

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
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    event_loop_replay_lost_front_switched(&replay_process);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), 0U);
    TEST_CHECK(g_event_loop.queue.count, 1U);
    struct event accepted_replay_event;
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &accepted_replay_event), true);
    event_loop_release_event_context(accepted_replay_event.type, accepted_replay_event.context);
    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
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
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
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

    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
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

struct event_loop_bootstrap_test_state
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool entered;
    bool release;
    bool callback_result;
    pthread_t callback_thread;
    uint32_t queued_while_blocked;
};

struct event_loop_begin_test_context
{
    struct event_loop *event_loop;
    struct event_loop_bootstrap_test_state *bootstrap;
    bool result;
};

struct event_loop_end_test_context
{
    struct event_loop *event_loop;
};

struct event_loop_paused_admission_test_context
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool entered;
    bool release;
    bool accepted;
    CFMutableArrayRef context;
};

struct event_loop_worker_guard_test_context
{
    bool sync_result;
    bool end_left_loop_live;
    pthread_t worker_thread;
};

struct event_loop_paused_leave_test_context
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint32_t pause_after;
    uint32_t observed;
    bool entered;
    bool release;
};

struct event_loop_sync_thread_test_context
{
    bool result;
    uint32_t callback_count;
};

struct event_loop_thread_create_interval_test_context
{
    bool fail_create;
    uint32_t create_calls;
    pthread_t sync_thread;
    struct event_loop_sync_thread_test_context sync;
};

struct event_loop_cross_loop_test_context
{
    struct event_loop *other_loop;
    pthread_t source_worker;
    pthread_t target_worker;
    bool target_sync_result;
    bool target_cleanup_complete;
    uint32_t target_callback_count;
};

static uint32_t event_loop_test_queue_count(struct event_loop *event_loop)
{
    pthread_mutex_lock(&event_loop->queue.lock);
    uint32_t count = event_loop->queue.count;
    pthread_mutex_unlock(&event_loop->queue.lock);
    return count;
}

static bool event_loop_test_bootstrap_callback(void *context)
{
    struct event_loop_bootstrap_test_state *state = context;
    pthread_mutex_lock(&state->mutex);
    state->callback_thread = pthread_self();
    state->entered = true;
    pthread_cond_signal(&state->cond);
    while (!state->release) pthread_cond_wait(&state->cond, &state->mutex);
    state->queued_while_blocked = event_loop_test_queue_count(&g_event_loop);
    bool result = state->callback_result;
    pthread_mutex_unlock(&state->mutex);
    return result;
}

static void *event_loop_test_begin_bootstrap(void *context)
{
    struct event_loop_begin_test_context *begin = context;
    begin->result = event_loop_begin_bootstrap(begin->event_loop, event_loop_test_bootstrap_callback, begin->bootstrap);
    return NULL;
}

static void *event_loop_test_concurrent_producer(void *context)
{
    bool *accepted = context;
    *accepted = event_loop_try_post(&g_event_loop, MENU_BAR_REFRESH, NULL, 0);
    return NULL;
}

static void *event_loop_test_end_thread(void *context)
{
    struct event_loop_end_test_context *end = context;
    event_loop_end(end->event_loop);
    return NULL;
}

static bool event_loop_test_fd_is_open(int fd)
{
    return fcntl(fd, F_GETFD) != -1;
}

static void event_loop_test_close_fd_if_open(int fd)
{
    if (event_loop_test_fd_is_open(fd)) close(fd);
}

static uint32_t event_loop_test_admitted_count(struct event_loop *event_loop)
{
    return event_loop_admission_count(__atomic_load_n(&event_loop->producer_admission, __ATOMIC_ACQUIRE));
}

static bool event_loop_test_admission_is_closed(struct event_loop *event_loop)
{
    return !event_loop_admission_is_open(__atomic_load_n(&event_loop->producer_admission, __ATOMIC_ACQUIRE));
}

static bool event_loop_test_wait_for_active_posters(struct event_loop *event_loop, uint32_t active_posters)
{
    for (int i = 0; i < 1000; ++i) {
        if (event_loop_test_admitted_count(event_loop) == active_posters) return true;
        usleep(1000);
    }
    return false;
}

static bool event_loop_test_wait_for_closed_admission(struct event_loop *event_loop)
{
    for (int i = 0; i < 1000; ++i) {
        if (event_loop_test_admission_is_closed(event_loop)) return true;
        usleep(1000);
    }
    return false;
}

static struct event_loop_paused_admission_test_context *event_loop_paused_admission_test;
static struct event_loop_paused_leave_test_context *event_loop_paused_leave_test;

static void event_loop_test_pause_before_admission(void)
{
    struct event_loop_paused_admission_test_context *test = event_loop_paused_admission_test;
    if (!test) return;

    pthread_mutex_lock(&test->mutex);
    test->entered = true;
    pthread_cond_signal(&test->cond);
    while (!test->release) pthread_cond_wait(&test->cond, &test->mutex);
    pthread_mutex_unlock(&test->mutex);
}

static void event_loop_test_pause_during_leave(void)
{
    struct event_loop_paused_leave_test_context *test = event_loop_paused_leave_test;
    if (!test) return;

    pthread_mutex_lock(&test->mutex);
    if (test->observed++ < test->pause_after) {
        pthread_mutex_unlock(&test->mutex);
        return;
    }
    test->entered = true;
    pthread_cond_signal(&test->cond);
    while (!test->release) pthread_cond_wait(&test->cond, &test->mutex);
    pthread_mutex_unlock(&test->mutex);
}

static void *event_loop_test_paused_window_created_post(void *context)
{
    struct event_loop_paused_admission_test_context *test = context;
    test->accepted = event_loop_try_post(&g_event_loop, WINDOW_CREATED, test->context, 0);
    return NULL;
}

static bool event_loop_test_worker_guard_callback(void *context)
{
    struct event_loop_worker_guard_test_context *test = context;
    test->worker_thread = pthread_self();
    test->sync_result = event_loop_perform_sync(&g_event_loop, NULL, NULL);
    event_loop_end(&g_event_loop);
    test->end_left_loop_live = g_event_loop.semaphore != NULL && g_event_loop.queue.events != NULL;
    return true;
}

static bool event_loop_test_sync_callback(void *context)
{
    struct event_loop_sync_thread_test_context *test = context;
    ++test->callback_count;
    return true;
}

static void *event_loop_test_sync_thread(void *context)
{
    struct event_loop_sync_thread_test_context *test = context;
    test->result = event_loop_perform_sync(&g_event_loop, event_loop_test_sync_callback, test);
    return NULL;
}

static bool event_loop_test_cross_loop_target(void *context)
{
    struct event_loop_cross_loop_test_context *test = context;
    test->target_worker = pthread_self();
    ++test->target_callback_count;
    return true;
}

static bool event_loop_test_cross_loop_source(void *context)
{
    struct event_loop_cross_loop_test_context *test = context;
    test->source_worker = pthread_self();
    test->target_sync_result = event_loop_perform_sync(test->other_loop, event_loop_test_cross_loop_target, test);
    event_loop_end(test->other_loop);
    test->target_cleanup_complete = test->other_loop->semaphore == NULL && test->other_loop->queue.events == NULL;
    return true;
}

static struct event_loop_thread_create_interval_test_context *event_loop_thread_create_interval_test;

static int event_loop_test_interval_thread_create(pthread_t *thread, const pthread_attr_t *attr,
                                                 void *(*start_routine)(void *), void *context)
{
    struct event_loop_thread_create_interval_test_context *test = event_loop_thread_create_interval_test;
    if (!test) return pthread_create(thread, attr, start_routine, context);

    ++test->create_calls;
    if (pthread_create(&test->sync_thread, NULL, event_loop_test_sync_thread, &test->sync) != 0) return EAGAIN;
    for (int i = 0; i < 1000; ++i) {
        if (event_loop_test_queue_count(&g_event_loop) == 1) break;
        usleep(1000);
    }

    return test->fail_create ? EAGAIN : pthread_create(thread, attr, start_routine, context);
}

TEST_FUNC(event_loop_zero_initialized_state_rejects_without_queue_access,
{
    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};

    CFMutableArrayRef rejected_context = CFArrayCreateMutable(NULL, 0, NULL);
    TEST_CHECK(rejected_context != NULL, true);
    if (rejected_context) {
        CFRetain(rejected_context);
        TEST_CHECK((int) CFGetRetainCount(rejected_context), 2);
        TEST_CHECK(event_loop_try_post(&g_event_loop, WINDOW_CREATED, rejected_context, 0), false);
        TEST_CHECK((int) CFGetRetainCount(rejected_context), 1);
        CFRelease(rejected_context);
    }
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, NULL, NULL), false);
    TEST_CHECK(event_loop_test_admission_is_closed(&g_event_loop), true);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);

    g_event_loop = saved_event_loop;
});

TEST_FUNC(declarative_space_create_destroy_and_absent_event_coalesce_reconcile,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct space_manager saved_space_manager = g_space_manager;
    g_event_loop = (struct event_loop) {0};
    g_space_manager = (struct space_manager) {0};
    table_init(&g_space_manager.view, 4, hash_view, compare_view);
    declarative_install_test_set_generation(1);

    TEST_CHECK(event_queue_init(&g_event_loop.queue, 4), true);
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai_decl_lifecycle_%d", getpid());
    g_event_loop.semaphore = sem_open(semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(semaphore_name);
    TEST_CHECK(g_event_loop.semaphore != SEM_FAILED, true);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);

    handle_space_lifecycle_success();
    handle_space_lifecycle_success();
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);

    /* Destruction of a SID with no local view must still request work, and coalesce. */
    EVENT_HANDLER_SLS_SPACE_DESTROYED((void *)(intptr_t) 9999, 0);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);

    struct event event = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, CONFIGURATION_RECONCILE);
    TEST_CHECK(event.context == NULL, true);
    TEST_CHECK(event.param1, 0);
    __atomic_store_n(&declarative_reconcile_reserved, 0, __ATOMIC_RELEASE);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 0U);

    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    sem_close(g_event_loop.semaphore);
    event_queue_destroy(&g_event_loop.queue);
    declarative_install_test_set_generation(0);
    table_free(&g_space_manager.view);
    g_space_manager = saved_space_manager;
    g_event_loop = saved_event_loop;
});

static uint32_t declarative_reconcile_entry_test_reserved;
static uint32_t declarative_reconcile_entry_test_queue_count;
static void declarative_reconcile_entry_test_hook(void)
{
    declarative_reload_test_set_reconcile_entry_hook(NULL);
    declarative_configuration_request_reconcile();
    declarative_reconcile_entry_test_reserved = declarative_reload_test_reconcile_reservation();
    declarative_reconcile_entry_test_queue_count = event_loop_test_queue_count(&g_event_loop);
}

TEST_FUNC(declarative_dock_restart_and_system_wake_coalesce_reconcile,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct table saved_window_table = g_window_manager.window;
    uint32_t saved_focused_window_id = g_window_manager.focused_window_id;
    bool saved_menu_bar_initialized = g_menu_bar_manager.initialized;
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;

    g_event_loop = (struct event_loop) {0};
    g_window_manager.window = (struct table) {0};
    table_init(&g_window_manager.window, 4, hash_wm, compare_wm);
    g_window_manager.focused_window_id = 0;
    g_menu_bar_manager.initialized = false;
    workspace_set_macos_version(10);
    declarative_install_test_set_generation(1);
    __atomic_store_n(&declarative_reconcile_reserved, 0, __ATOMIC_RELEASE);

    TEST_CHECK(event_queue_init(&g_event_loop.queue, 4), true);
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai_decl_restart_wake_%d", getpid());
    g_event_loop.semaphore = sem_open(semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(semaphore_name);
    TEST_CHECK(g_event_loop.semaphore != SEM_FAILED, true);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);

    EVENT_HANDLER_DOCK_DID_RESTART(NULL, 0);
    EVENT_HANDLER_DOCK_DID_RESTART(NULL, 0);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);

    struct event event = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, CONFIGURATION_RECONCILE);
    TEST_CHECK(event.context == NULL, true);
    TEST_CHECK(event.param1, 0);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);
    __atomic_store_n(&declarative_reconcile_reserved, 0, __ATOMIC_RELEASE);

    EVENT_HANDLER_SYSTEM_WOKE(NULL, 0);
    EVENT_HANDLER_SYSTEM_WOKE(NULL, 0);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, CONFIGURATION_RECONCILE);
    TEST_CHECK(event.context == NULL, true);
    TEST_CHECK(event.param1, 0);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);

    __atomic_store_n(&declarative_reconcile_reserved, 0, __ATOMIC_RELEASE);
    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    sem_close(g_event_loop.semaphore);
    event_queue_destroy(&g_event_loop.queue);
    declarative_install_test_set_generation(0);
    table_free(&g_window_manager.window);
    g_window_manager.window = saved_window_table;
    g_window_manager.focused_window_id = saved_focused_window_id;
    g_menu_bar_manager.initialized = saved_menu_bar_initialized;
    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
    g_event_loop = saved_event_loop;
});

TEST_FUNC(declarative_reconcile_reserves_before_post_and_coalesces,
{
    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    declarative_install_test_set_generation(1);

    /* Rejected admission must roll the reservation back for the later live loop. */
    declarative_configuration_request_reconcile();

    struct event_loop_bootstrap_test_state bootstrap = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .callback_result = true
    };
    struct event_loop_begin_test_context begin = { .event_loop = &g_event_loop, .bootstrap = &bootstrap };
    pthread_t begin_thread;
    TEST_CHECK(pthread_create(&begin_thread, NULL, event_loop_test_begin_bootstrap, &begin), 0);
    pthread_mutex_lock(&bootstrap.mutex);
    while (!bootstrap.entered) pthread_cond_wait(&bootstrap.cond, &bootstrap.mutex);
    pthread_mutex_unlock(&bootstrap.mutex);

    declarative_configuration_request_reconcile();
    declarative_configuration_request_reconcile();
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);
    declarative_reconcile_entry_test_reserved = 0;
    declarative_reconcile_entry_test_queue_count = 0;
    declarative_reload_test_set_reconcile_entry_hook(declarative_reconcile_entry_test_hook);
    pthread_mutex_lock(&bootstrap.mutex);
    bootstrap.release = true;
    pthread_cond_signal(&bootstrap.cond);
    pthread_mutex_unlock(&bootstrap.mutex);
    pthread_join(begin_thread, NULL);
    TEST_CHECK(begin.result, true);
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, NULL, NULL), true);
    TEST_CHECK(declarative_reconcile_entry_test_reserved, 1U);
    TEST_CHECK(declarative_reconcile_entry_test_queue_count >= 1U, true);
    /* The first barrier may already have been queued ahead of the admitted follow-up. */
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, NULL, NULL), true);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 0U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 0U);
    printf("                   phase5 reconcile queue accepted=2 skipped=1 rejected=1 followup=1 owned_contexts=0 owned_sockets=0\n");
    declarative_install_test_set_generation(0);
    event_loop_end(&g_event_loop);

    pthread_cond_destroy(&bootstrap.cond);
    pthread_mutex_destroy(&bootstrap.mutex);
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_bootstrap_is_first_worker_turn_and_barrier_reports_result,
{
    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    struct event_loop_bootstrap_test_state bootstrap = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .callback_result = true
    };
    struct event_loop_begin_test_context begin = { .event_loop = &g_event_loop, .bootstrap = &bootstrap };
    pthread_t begin_thread;
    TEST_CHECK(pthread_create(&begin_thread, NULL, event_loop_test_begin_bootstrap, &begin), 0);

    pthread_mutex_lock(&bootstrap.mutex);
    while (!bootstrap.entered) pthread_cond_wait(&bootstrap.cond, &bootstrap.mutex);
    pthread_mutex_unlock(&bootstrap.mutex);

    TEST_CHECK(pthread_equal(bootstrap.callback_thread, g_event_loop.thread), true);
    pthread_t producers[4];
    bool accepted[4] = {0};
    for (int i = 0; i < 4; ++i) {
        TEST_CHECK(pthread_create(&producers[i], NULL, event_loop_test_concurrent_producer, &accepted[i]), 0);
    }
    for (int i = 0; i < 4; ++i) {
        pthread_join(producers[i], NULL);
        TEST_CHECK(accepted[i], true);
    }
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);

    pthread_mutex_lock(&bootstrap.mutex);
    bootstrap.release = true;
    pthread_cond_signal(&bootstrap.cond);
    pthread_mutex_unlock(&bootstrap.mutex);
    pthread_join(begin_thread, NULL);

    TEST_CHECK(begin.result, true);
    TEST_CHECK(bootstrap.queued_while_blocked, 1U);
    event_loop_end(&g_event_loop);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);
    pthread_cond_destroy(&bootstrap.cond);
    pthread_mutex_destroy(&bootstrap.mutex);

    bootstrap = (struct event_loop_bootstrap_test_state) {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .callback_result = false,
        .release = true
    };
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, event_loop_test_bootstrap_callback, &bootstrap), false);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);
    pthread_cond_destroy(&bootstrap.cond);
    pthread_mutex_destroy(&bootstrap.mutex);
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_shutdown_waits_for_inflight_posters_and_rejects_after_close,
{
    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, NULL, NULL), true);

    pthread_mutex_lock(&g_event_loop.queue.lock);
    pthread_t producer;
    bool accepted = false;
    TEST_CHECK(pthread_create(&producer, NULL, event_loop_test_concurrent_producer, &accepted), 0);
    TEST_CHECK(event_loop_test_wait_for_active_posters(&g_event_loop, 1), true);

    struct event_loop_paused_leave_test_context leave = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .pause_after = 1
    };
    event_loop_paused_leave_test = &leave;
    event_loop_leave_producer_test_hook = event_loop_test_pause_during_leave;

    struct event_loop_end_test_context end = { .event_loop = &g_event_loop };
    pthread_t end_thread;
    TEST_CHECK(pthread_create(&end_thread, NULL, event_loop_test_end_thread, &end), 0);
    TEST_CHECK(event_loop_test_wait_for_closed_admission(&g_event_loop), true);
    TEST_CHECK(g_event_loop.queue.events != NULL, true);

    CFMutableArrayRef rejected_context = CFArrayCreateMutable(NULL, 0, NULL);
    TEST_CHECK(rejected_context != NULL, true);
    if (rejected_context) {
        CFRetain(rejected_context);
        TEST_CHECK((int) CFGetRetainCount(rejected_context), 2);
        TEST_CHECK(event_loop_try_post(&g_event_loop, WINDOW_CREATED, rejected_context, 0), false);
        TEST_CHECK((int) CFGetRetainCount(rejected_context), 1);
        CFRelease(rejected_context);
    }

    pthread_mutex_unlock(&g_event_loop.queue.lock);
    pthread_mutex_lock(&leave.mutex);
    while (!leave.entered) pthread_cond_wait(&leave.cond, &leave.mutex);
    pthread_mutex_unlock(&leave.mutex);
    TEST_CHECK(g_event_loop.queue.events != NULL, true);

    pthread_mutex_lock(&leave.mutex);
    leave.release = true;
    pthread_cond_signal(&leave.cond);
    pthread_mutex_unlock(&leave.mutex);
    pthread_join(producer, NULL);
    TEST_CHECK(accepted, true);
    pthread_join(end_thread, NULL);
    event_loop_leave_producer_test_hook = NULL;
    event_loop_paused_leave_test = NULL;
    pthread_cond_destroy(&leave.cond);
    pthread_mutex_destroy(&leave.mutex);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_front_switch_helpers_use_admission_and_rollback,
{
    struct event_loop saved_event_loop = g_event_loop;
    pid_t saved_front_pid = g_process_manager.front_pid;
    uint64_t saved_generation = g_process_manager.front_switch_generation;
    uint64_t saved_front_psn = g_process_manager.front_switch_psn;
    g_event_loop = (struct event_loop) {0};

    struct process process = {
        .pid = 1234,
        .psn = { .highLongOfPSN = 42, .lowLongOfPSN = 24 },
        .name = "front-switch-test"
    };

    struct event_loop_bootstrap_test_state bootstrap = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .callback_result = false
    };
    struct event_loop_begin_test_context begin = { .event_loop = &g_event_loop, .bootstrap = &bootstrap };
    pthread_t begin_thread;
    TEST_CHECK(pthread_create(&begin_thread, NULL, event_loop_test_begin_bootstrap, &begin), 0);

    pthread_mutex_lock(&bootstrap.mutex);
    while (!bootstrap.entered) pthread_cond_wait(&bootstrap.cond, &bootstrap.mutex);
    pthread_mutex_unlock(&bootstrap.mutex);

    TEST_CHECK(event_loop_post_application_front_switched(&process, 99, false), true);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);

    pthread_mutex_lock(&bootstrap.mutex);
    bootstrap.release = true;
    pthread_cond_signal(&bootstrap.cond);
    pthread_mutex_unlock(&bootstrap.mutex);
    pthread_join(begin_thread, NULL);
    TEST_CHECK(begin.result, false);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);
    pthread_cond_destroy(&bootstrap.cond);
    pthread_mutex_destroy(&bootstrap.mutex);

    g_event_loop = (struct event_loop) {0};
    __atomic_store_n(&g_process_manager.front_switch_generation, 70, __ATOMIC_RELEASE);
    __atomic_store_n(&g_process_manager.front_switch_psn, process_manager_pack_psn((ProcessSerialNumber) { .highLongOfPSN = 7, .lowLongOfPSN = 8 }), __ATOMIC_RELEASE);
    TEST_CHECK(event_loop_post_tracked_application_front_switched(&process), false);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE), 70ULL);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_psn, __ATOMIC_ACQUIRE), process_manager_pack_psn((ProcessSerialNumber) { .highLongOfPSN = 7, .lowLongOfPSN = 8 }));

    g_process_manager.front_pid = saved_front_pid;
    __atomic_store_n(&g_process_manager.front_switch_generation, saved_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_process_manager.front_switch_psn, saved_front_psn, __ATOMIC_RELEASE);
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_late_pre_admission_post_after_cleanup_releases_context,
{
    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, NULL, NULL), true);

    struct event_loop_paused_admission_test_context paused = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER
    };
    paused.context = CFArrayCreateMutable(NULL, 0, NULL);
    TEST_CHECK(paused.context != NULL, true);
    if (paused.context) CFRetain(paused.context);

    event_loop_paused_admission_test = &paused;
    event_loop_admit_producer_test_hook = event_loop_test_pause_before_admission;
    pthread_t producer;
    TEST_CHECK(pthread_create(&producer, NULL, event_loop_test_paused_window_created_post, &paused), 0);

    pthread_mutex_lock(&paused.mutex);
    while (!paused.entered) pthread_cond_wait(&paused.cond, &paused.mutex);
    pthread_mutex_unlock(&paused.mutex);
    event_loop_admit_producer_test_hook = NULL;

    event_loop_end(&g_event_loop);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);
    TEST_CHECK(event_loop_test_admission_is_closed(&g_event_loop), true);

    pthread_mutex_lock(&paused.mutex);
    paused.release = true;
    pthread_cond_signal(&paused.cond);
    pthread_mutex_unlock(&paused.mutex);
    pthread_join(producer, NULL);

    TEST_CHECK(paused.accepted, false);
    if (paused.context) {
        TEST_CHECK((int) CFGetRetainCount(paused.context), 1);
        CFRelease(paused.context);
    }

    event_loop_paused_admission_test = NULL;
    pthread_cond_destroy(&paused.cond);
    pthread_mutex_destroy(&paused.mutex);
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_worker_guards_reject_sync_and_self_shutdown,
{
    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, NULL, NULL), true);

    struct event_loop_worker_guard_test_context guard = {0};
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, event_loop_test_worker_guard_callback, &guard), true);
    TEST_CHECK(pthread_equal(guard.worker_thread, g_event_loop.thread), true);
    TEST_CHECK(guard.sync_result, false);
    TEST_CHECK(guard.end_left_loop_live, true);
    TEST_CHECK(g_event_loop.semaphore != NULL, true);
    TEST_CHECK(g_event_loop.queue.events != NULL, true);

    event_loop_end(&g_event_loop);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);
    TEST_CHECK(event_loop_test_admission_is_closed(&g_event_loop), true);
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_worker_identity_is_scoped_to_the_supplied_loop,
{
    struct event_loop source_loop = {0};
    struct event_loop target_loop = {0};
    TEST_CHECK(event_loop_begin_bootstrap(&source_loop, NULL, NULL), true);
    TEST_CHECK(event_loop_begin_bootstrap(&target_loop, NULL, NULL), true);
    pthread_t target_thread = target_loop.thread;

    struct event_loop_cross_loop_test_context test = { .other_loop = &target_loop };
    TEST_CHECK(event_loop_perform_sync(&source_loop, event_loop_test_cross_loop_source, &test), true);
    TEST_CHECK(pthread_equal(test.source_worker, source_loop.thread), true);
    TEST_CHECK(pthread_equal(test.target_worker, target_thread), true);
    TEST_CHECK(pthread_equal(test.source_worker, test.target_worker), false);
    TEST_CHECK(test.target_sync_result, true);
    TEST_CHECK(test.target_callback_count, 1U);
    TEST_CHECK(test.target_cleanup_complete, true);
    TEST_CHECK(target_loop.semaphore == NULL, true);
    TEST_CHECK(target_loop.queue.events == NULL, true);
    TEST_CHECK(source_loop.semaphore != NULL, true);
    TEST_CHECK(source_loop.queue.events != NULL, true);

    event_loop_end(&source_loop);
    TEST_CHECK(source_loop.semaphore == NULL, true);
    TEST_CHECK(source_loop.queue.events == NULL, true);
});

TEST_FUNC(event_loop_sync_during_thread_create_interval_completes_on_success_and_failure,
{
    struct event_loop saved_event_loop = g_event_loop;
    __auto_type saved_thread_create = event_loop_thread_create;

    for (int fail_create = 0; fail_create <= 1; ++fail_create) {
        g_event_loop = (struct event_loop) {0};
        struct event_loop_thread_create_interval_test_context test = { .fail_create = fail_create != 0 };
        event_loop_thread_create_interval_test = &test;
        event_loop_thread_create = event_loop_test_interval_thread_create;

        bool began = event_loop_begin_bootstrap(&g_event_loop, NULL, NULL);
        pthread_join(test.sync_thread, NULL);

        TEST_CHECK(test.create_calls, 1U);
        TEST_CHECK(began, !test.fail_create);
        TEST_CHECK(test.sync.result, !test.fail_create);
        TEST_CHECK(test.sync.callback_count, test.fail_create ? 0U : 1U);

        if (began) event_loop_end(&g_event_loop);
        TEST_CHECK(g_event_loop.semaphore == NULL, true);
        TEST_CHECK(g_event_loop.queue.events == NULL, true);
    }

    event_loop_thread_create = saved_thread_create;
    event_loop_thread_create_interval_test = NULL;
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_sync_submission_concurrent_with_shutdown_completes,
{
    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, NULL, NULL), true);

    pthread_mutex_lock(&g_event_loop.queue.lock);
    struct event_loop_sync_thread_test_context sync = {0};
    pthread_t sync_thread;
    TEST_CHECK(pthread_create(&sync_thread, NULL, event_loop_test_sync_thread, &sync), 0);
    TEST_CHECK(event_loop_test_wait_for_active_posters(&g_event_loop, 1), true);

    struct event_loop_end_test_context end = { .event_loop = &g_event_loop };
    pthread_t end_thread;
    TEST_CHECK(pthread_create(&end_thread, NULL, event_loop_test_end_thread, &end), 0);
    TEST_CHECK(event_loop_test_wait_for_closed_admission(&g_event_loop), true);

    pthread_mutex_unlock(&g_event_loop.queue.lock);
    pthread_join(sync_thread, NULL);
    pthread_join(end_thread, NULL);
    TEST_CHECK(sync.callback_count <= 1U, true);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);
    TEST_CHECK(event_loop_test_admission_is_closed(&g_event_loop), true);
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_bootstrap_failure_drains_queued_daemon_messages,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};

    struct event_loop_bootstrap_test_state bootstrap = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .callback_result = false
    };
    struct event_loop_begin_test_context begin = { .event_loop = &g_event_loop, .bootstrap = &bootstrap };
    pthread_t begin_thread;
    TEST_CHECK(pthread_create(&begin_thread, NULL, event_loop_test_begin_bootstrap, &begin), 0);

    pthread_mutex_lock(&bootstrap.mutex);
    while (!bootstrap.entered) pthread_cond_wait(&bootstrap.cond, &bootstrap.mutex);
    pthread_mutex_unlock(&bootstrap.mutex);

    int pair[2] = { -1, -1 };
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    if (pair[1] != -1) {
        int flags = fcntl(pair[1], F_GETFL, 0);
        if (flags != -1) fcntl(pair[1], F_SETFL, flags | O_NONBLOCK);
    }
    if (pair[0] != -1) {
        TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
        TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 1U);
        TEST_CHECK(event_loop_try_post(&g_event_loop, DAEMON_MESSAGE, NULL, pair[0]), true);
    }

    pthread_mutex_lock(&bootstrap.mutex);
    bootstrap.release = true;
    pthread_cond_signal(&bootstrap.cond);
    pthread_mutex_unlock(&bootstrap.mutex);
    pthread_join(begin_thread, NULL);
    TEST_CHECK(begin.result, false);
    TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 0U);
    TEST_CHECK(event_loop_test_fd_is_open(pair[0]), false);
    if (pair[1] != -1) {
        char byte = 0;
        TEST_CHECK(read(pair[1], &byte, 1), 0);
    }
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);

    event_loop_test_close_fd_if_open(pair[0]);
    event_loop_test_close_fd_if_open(pair[1]);
    pthread_cond_destroy(&bootstrap.cond);
    pthread_mutex_destroy(&bootstrap.mutex);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
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

static struct window *focus_fallback_test_focused;
static struct window *focus_fallback_test_recent;
static int focus_fallback_test_focus_count;

static struct window *focus_fallback_test_find_focused(struct window_manager *wm)
{
    (void) wm;
    return focus_fallback_test_focused;
}

static struct window *focus_fallback_test_find_recent(struct window_manager *wm)
{
    (void) wm;
    return focus_fallback_test_recent && focus_fallback_test_recent->ref ? focus_fallback_test_recent : NULL;
}

static void focus_fallback_test_focus(struct window *window)
{
    if (window == focus_fallback_test_recent) ++focus_fallback_test_focus_count;
}

static void focus_fallback_test_request_follow_up(void)
{
    event_loop_request_window_focus_fallback();
}

static void focus_fallback_test_retry_during_rejection_return(void)
{
    event_loop_focus_fallback_rejection_test_hook = NULL;
    g_event_queue_test_fail_allocation = false;
    struct event event;
    while (event_queue_pop(&g_event_loop.queue, &event)) {
        event_loop_discard_event(event.type, event.context, event.param1, false);
    }
    event_loop_request_window_focus_fallback();
}

static void focus_fallback_test_open_queue(uint32_t capacity)
{
    g_event_loop = (struct event_loop) {0};
    event_queue_init(&g_event_loop.queue, capacity);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
}

static void focus_fallback_test_close_queue(void)
{
    event_loop_close_producer_admission(&g_event_loop);
    struct event event;
    while (event_queue_pop(&g_event_loop.queue, &event)) {
        event_loop_discard_event(event.type, event.context, event.param1, false);
    }
    event_queue_destroy(&g_event_loop.queue);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    g_event_loop = (struct event_loop) {0};
}

TEST_FUNC(window_focus_fallback_trigger_policy_and_behavior_are_deterministic,
{
    struct event_loop saved_event_loop = g_event_loop;
    enum window_focus_fallback saved_policy = g_window_manager.window_focus_fallback;
    __auto_type saved_focused = event_loop_focus_fallback_find_focused;
    __auto_type saved_recent = event_loop_focus_fallback_find_recent;
    __auto_type saved_focus = event_loop_focus_fallback_focus;
    __auto_type saved_hook = event_loop_focus_fallback_entry_test_hook;
    __auto_type saved_rejection_hook = event_loop_focus_fallback_rejection_test_hook;
    struct application focused_app = { .name = "focused" };
    struct application recent_app = { .name = "recent" };
    struct window focused = { .application = &focused_app, .ref = (AXUIElementRef)(intptr_t)1, .id = 41, .is_root = true, .rule_flags = WINDOW_RULE_MANAGED };
    struct window recent = { .application = &recent_app, .ref = (AXUIElementRef)(intptr_t)2, .id = 42, .is_root = true, .rule_flags = WINDOW_RULE_MANAGED };
    focused.id_ptr = &focused.id;
    recent.id_ptr = &recent.id;
    event_loop_focus_fallback_find_focused = focus_fallback_test_find_focused;
    event_loop_focus_fallback_find_recent = focus_fallback_test_find_recent;
    event_loop_focus_fallback_focus = focus_fallback_test_focus;
    event_loop_focus_fallback_entry_test_hook = NULL;
    event_loop_focus_fallback_rejection_test_hook = NULL;
    __atomic_store_n(&window_focus_fallback_reserved, 0, __ATOMIC_RELEASE);
    focus_fallback_test_open_queue(4);

    enum event_type triggers[] = { WINDOW_DESTROYED, WINDOW_MINIMIZED, APPLICATION_HIDDEN };
    g_window_manager.window_focus_fallback = WINDOW_FOCUS_FALLBACK_OFF;
    for (size_t i = 0; i < sizeof(triggers) / sizeof(*triggers); ++i) {
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(g_event_loop.queue.count, 0U);
    }

    g_window_manager.window_focus_fallback = WINDOW_FOCUS_FALLBACK_RECENT;
    focus_fallback_test_focus_count = 0;
    for (size_t i = 0; i < sizeof(triggers) / sizeof(*triggers); ++i) {
        struct event event;
        focus_fallback_test_focused = &focused;
        focus_fallback_test_recent = &recent;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        TEST_CHECK(focus_fallback_test_focus_count, (int) i);
        focused.ref = NULL;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        focused.ref = (AXUIElementRef)(intptr_t)1;
        TEST_CHECK(focus_fallback_test_focus_count, (int) i);

        focus_fallback_test_focused = NULL;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        TEST_CHECK(focus_fallback_test_focus_count, (int) i + 1);

        focus_fallback_test_recent = NULL;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        TEST_CHECK(focus_fallback_test_focus_count, (int) i + 1);

        focus_fallback_test_recent = &recent;
        recent.ref = NULL;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        recent.ref = (AXUIElementRef)(intptr_t)2;

        recent.id_ptr = NULL;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        recent.id_ptr = &recent.id;

        window_set_flag(&recent, WINDOW_MINIMIZE);
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        window_clear_flag(&recent, WINDOW_MINIMIZE);

        recent_app.is_hidden = true;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        recent_app.is_hidden = false;

        recent.rule_flags = 0;
        recent.is_root = false;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        recent.rule_flags = WINDOW_RULE_MANAGED;
        recent.is_root = true;
        TEST_CHECK(focus_fallback_test_focus_count, (int) i + 1);

        focused.flags = WINDOW_FULLSCREEN;
        focus_fallback_test_focused = &focused;
        event_loop_request_window_focus_fallback_for_event(triggers[i]);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
        focused.flags = 0;
        TEST_CHECK(focus_fallback_test_focus_count, (int) i + 1);
    }

    focus_fallback_test_close_queue();
    event_loop_focus_fallback_find_focused = saved_focused;
    event_loop_focus_fallback_find_recent = saved_recent;
    event_loop_focus_fallback_focus = saved_focus;
    event_loop_focus_fallback_entry_test_hook = saved_hook;
    event_loop_focus_fallback_rejection_test_hook = saved_rejection_hook;
    g_window_manager.window_focus_fallback = saved_policy;
    g_event_loop = saved_event_loop;
});

TEST_FUNC(window_focus_fallback_queue_reservation_coalesces_retries_and_clears,
{
    struct event_loop saved_event_loop = g_event_loop;
    enum window_focus_fallback saved_policy = g_window_manager.window_focus_fallback;
    __auto_type saved_focused = event_loop_focus_fallback_find_focused;
    __auto_type saved_recent = event_loop_focus_fallback_find_recent;
    __auto_type saved_focus = event_loop_focus_fallback_focus;
    __auto_type saved_hook = event_loop_focus_fallback_entry_test_hook;
    __auto_type saved_rejection_hook = event_loop_focus_fallback_rejection_test_hook;
    g_window_manager.window_focus_fallback = WINDOW_FOCUS_FALLBACK_RECENT;
    event_loop_focus_fallback_find_focused = focus_fallback_test_find_focused;
    event_loop_focus_fallback_find_recent = focus_fallback_test_find_recent;
    event_loop_focus_fallback_focus = focus_fallback_test_focus;
    focus_fallback_test_focused = NULL;
    focus_fallback_test_recent = NULL;
    event_loop_focus_fallback_entry_test_hook = NULL;
    event_loop_focus_fallback_rejection_test_hook = NULL;
    __atomic_store_n(&window_focus_fallback_reserved, 0, __ATOMIC_RELEASE);
    focus_fallback_test_open_queue(2);

    event_loop_request_window_focus_fallback_for_event(WINDOW_DESTROYED);
    event_loop_request_window_focus_fallback_for_event(WINDOW_MINIMIZED);
    event_loop_request_window_focus_fallback_for_event(APPLICATION_HIDDEN);
    TEST_CHECK(g_event_loop.queue.count, 1U);
    TEST_CHECK(__atomic_load_n(&window_focus_fallback_reserved, __ATOMIC_ACQUIRE), 1U);

    struct event event;
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, WINDOW_FOCUS_FALLBACK);
    event_loop_focus_fallback_entry_test_hook = focus_fallback_test_request_follow_up;
    EVENT_HANDLER_WINDOW_FOCUS_FALLBACK(event.context, event.param1);
    TEST_CHECK(g_event_loop.queue.count, 1U);
    TEST_CHECK(__atomic_load_n(&window_focus_fallback_reserved, __ATOMIC_ACQUIRE), 1U);
    event_loop_focus_fallback_entry_test_hook = NULL;
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    event_loop_discard_event(event.type, event.context, event.param1, false);
    TEST_CHECK(__atomic_load_n(&window_focus_fallback_reserved, __ATOMIC_ACQUIRE), 0U);

    struct event replaced = {0}; uint32_t capacity = 0; bool wake = false;
    TEST_CHECK(event_queue_push(&g_event_loop.queue, (struct event) { .type = WINDOW_MOVED }, false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
    TEST_CHECK(event_queue_push(&g_event_loop.queue, (struct event) { .type = WINDOW_RESIZED }, false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
    g_event_queue_test_fail_allocation = true;
    event_loop_request_window_focus_fallback();
    g_event_queue_test_fail_allocation = false;
    TEST_CHECK(__atomic_load_n(&window_focus_fallback_reserved, __ATOMIC_ACQUIRE), 0U);
    TEST_CHECK(g_event_loop.queue.count, 2U);
    while (event_queue_pop(&g_event_loop.queue, &event)) event_loop_discard_event(event.type, event.context, event.param1, false);
    event_loop_request_window_focus_fallback();
    TEST_CHECK(g_event_loop.queue.count, 1U);
    TEST_CHECK(__atomic_load_n(&window_focus_fallback_reserved, __ATOMIC_ACQUIRE), 1U);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    event_loop_discard_event(event.type, event.context, event.param1, false);

    TEST_CHECK(event_queue_push(&g_event_loop.queue, (struct event) { .type = WINDOW_MOVED }, false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
    TEST_CHECK(event_queue_push(&g_event_loop.queue, (struct event) { .type = WINDOW_RESIZED }, false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
    event_loop_focus_fallback_rejection_test_hook = focus_fallback_test_retry_during_rejection_return;
    g_event_queue_test_fail_allocation = true;
    event_loop_request_window_focus_fallback();
    g_event_queue_test_fail_allocation = false;
    TEST_CHECK(g_event_loop.queue.count, 1U);
    TEST_CHECK(__atomic_load_n(&window_focus_fallback_reserved, __ATOMIC_ACQUIRE), 1U);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, WINDOW_FOCUS_FALLBACK);
    event_loop_discard_event(event.type, event.context, event.param1, false);
    TEST_CHECK(__atomic_load_n(&window_focus_fallback_reserved, __ATOMIC_ACQUIRE), 0U);

    event_loop_request_window_focus_fallback();
    event_loop_cleanup(&g_event_loop);
    TEST_CHECK(__atomic_load_n(&window_focus_fallback_reserved, __ATOMIC_ACQUIRE), 0U);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);
    printf("                   focus fallback queue accepted=5 skipped=2 rejected=2 discarded=2 shutdown=1 followup=1 aba_retry=1 owned_contexts=0 owned_sockets=0\n");

    event_loop_focus_fallback_find_focused = saved_focused;
    event_loop_focus_fallback_find_recent = saved_recent;
    event_loop_focus_fallback_focus = saved_focus;
    event_loop_focus_fallback_entry_test_hook = saved_hook;
    event_loop_focus_fallback_rejection_test_hook = saved_rejection_hook;
    g_window_manager.window_focus_fallback = saved_policy;
    g_event_loop = saved_event_loop;
});

TEST_FUNC(window_focus_fallback_selector_matches_public_recent_helper,
{
    struct window_manager wm = {0};
    table_init(&wm.window, 4, hash_wm, compare_wm);
    table_init(&wm.managed_window, 4, hash_wm, compare_wm);
    struct application application = { .name = "recent" };
    struct window recent = { .application = &application, .ref = (AXUIElementRef)(intptr_t)1, .id = 73, .is_root = true, .rule_flags = WINDOW_RULE_MANAGED };
    recent.id_ptr = &recent.id;
    struct view view = {0};
    table_add(&wm.window, &recent.id, &recent);
    table_add(&wm.managed_window, &recent.id, &view);
    wm.last_window_id = recent.id;
    struct window *public_target = window_manager_find_recent_managed_window(&wm);
    struct window *native_target = event_loop_focus_fallback_recent_window(&wm);
    TEST_CHECK(public_target == &recent, true);
    TEST_CHECK(native_target == public_target, true);
    window_set_flag(&recent, WINDOW_MINIMIZE);
    TEST_CHECK(window_manager_find_recent_managed_window(&wm) == NULL, true);
    TEST_CHECK(event_loop_focus_fallback_recent_window(&wm) == NULL, true);
    table_remove(&wm.managed_window, &recent.id);
    table_remove(&wm.window, &recent.id);
    table_free(&wm.managed_window);
    table_free(&wm.window);
});

TEST_FUNC(declarative_scaffold_membership_add_remove_requests_one_reconcile,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct table saved_managed = g_window_manager.managed_window;
    bool saved_has_scaffold = declarative_installed.has_scaffold;
    uint64_t saved_generation = declarative_installed.generation;
    g_event_loop = (struct event_loop) {0};
    g_window_manager.managed_window = (struct table) {0};
    table_init(&g_window_manager.managed_window, 4, hash_wm, compare_wm);
    declarative_installed.has_scaffold = true;
    declarative_installed.generation = 1;
    __atomic_store_n(&declarative_reconcile_reserved, 0, __ATOMIC_RELEASE);

    TEST_CHECK(event_queue_init(&g_event_loop.queue, 4), true);
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai_sm_%d", getpid());
    g_event_loop.semaphore = sem_open(semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(semaphore_name);
    TEST_CHECK(g_event_loop.semaphore != SEM_FAILED, true);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);

    struct application application = { .name = "Mail" };
    struct window window = { .id = 4242, .application = &application };
    struct window_node root = { .window_list = { 4242 }, .window_order = { 4242 }, .window_count = 1 };
    struct view view = { .layout = VIEW_BSP, .root = &root };
    window_manager_add_managed_window(&g_window_manager, &window, &view);
    window_manager_remove_managed_window(&g_window_manager, window.id);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);
    struct event event = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, CONFIGURATION_RECONCILE);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);

    __atomic_store_n(&declarative_reconcile_reserved, 0, __ATOMIC_RELEASE);
    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    sem_close(g_event_loop.semaphore);
    event_queue_destroy(&g_event_loop.queue);
    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed;
    declarative_installed.has_scaffold = saved_has_scaffold;
    declarative_installed.generation = saved_generation;
    g_event_loop = saved_event_loop;
})
