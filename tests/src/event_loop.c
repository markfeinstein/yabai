static uint32_t native_resize_shutdown_restored_window_id;
static int native_resize_shutdown_restore_count;

static void native_resize_shutdown_restore(uint32_t window_id)
{
    native_resize_shutdown_restored_window_id = window_id;
    ++native_resize_shutdown_restore_count;
}

static bool native_resize_shutdown_seed(void *context)
{
    uint32_t window_id = *(uint32_t *) context;
    g_mouse_state.native_resize.window_id = window_id;
    g_mouse_state.native_resize.generation = 41;
    g_mouse_state.native_resize.claimed = true;
    return true;
}

struct concurrent_event_loop_end_context
{
    struct event_loop *event_loop;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready;
    int admitted;
    int winners;
    int losers;
    bool go;
};

static struct concurrent_event_loop_end_context *concurrent_event_loop_end_active;

static void concurrent_event_loop_end_admitted(struct event_loop *event_loop)
{
    struct concurrent_event_loop_end_context *end = concurrent_event_loop_end_active;
    if (!end || end->event_loop != event_loop) return;
    pthread_mutex_lock(&end->mutex);
    ++end->admitted;
    pthread_cond_broadcast(&end->cond);
    while (end->admitted != 2) pthread_cond_wait(&end->cond, &end->mutex);
    pthread_mutex_unlock(&end->mutex);
}

static void concurrent_event_loop_end_owner(bool won)
{
    struct concurrent_event_loop_end_context *end = concurrent_event_loop_end_active;
    if (!end) return;
    pthread_mutex_lock(&end->mutex);
    if (won) ++end->winners; else ++end->losers;
    pthread_cond_broadcast(&end->cond);
    pthread_mutex_unlock(&end->mutex);
}

static void *concurrent_event_loop_end(void *context)
{
    struct concurrent_event_loop_end_context *end = context;
    pthread_mutex_lock(&end->mutex);
    ++end->ready;
    pthread_cond_broadcast(&end->cond);
    while (!end->go) pthread_cond_wait(&end->cond, &end->mutex);
    pthread_mutex_unlock(&end->mutex);
    event_loop_end(end->event_loop);
    return NULL;
}

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

TEST_FUNC(native_resize_shutdown_serializes_final_cancellation_and_restart_state,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct mouse_state saved_mouse_state = g_mouse_state;
    __auto_type saved_restore = event_loop_test_native_resize_restore;
    bool saved_fail_allocation = g_event_queue_test_fail_allocation;
    g_event_loop = (struct event_loop) {0};
    memset(&g_mouse_state.native_resize, 0, sizeof(g_mouse_state.native_resize));
    native_resize_shutdown_restored_window_id = 0;
    native_resize_shutdown_restore_count = 0;
    event_loop_test_native_resize_restore = native_resize_shutdown_restore;

    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    uint32_t window_id = 4242;
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, native_resize_shutdown_seed, &window_id), true);
    g_event_queue_test_fail_allocation = true;
    event_loop_end(&g_event_loop);
    g_event_queue_test_fail_allocation = false;
    TEST_CHECK(native_resize_shutdown_restored_window_id, window_id);
    TEST_CHECK(native_resize_shutdown_restore_count, 1);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    TEST_CHECK(g_mouse_state.native_resize.generation != 41U, true);

    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, NULL, NULL), true);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    event_loop_end(&g_event_loop);

    event_loop_test_native_resize_restore = saved_restore;
    g_event_queue_test_fail_allocation = saved_fail_allocation;
    g_mouse_state = saved_mouse_state;
    g_event_loop = saved_event_loop;
});

TEST_FUNC(event_loop_concurrent_shutdown_has_one_owner_and_one_final_cancel,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct mouse_state saved_mouse_state = g_mouse_state;
    __auto_type saved_restore = event_loop_test_native_resize_restore;
    __auto_type saved_admitted = event_loop_shutdown_admitted_test_hook;
    __auto_type saved_owner = event_loop_shutdown_owner_test_hook;
    g_event_loop = (struct event_loop) {0};
    memset(&g_mouse_state.native_resize, 0, sizeof(g_mouse_state.native_resize));
    native_resize_shutdown_restored_window_id = 0;
    native_resize_shutdown_restore_count = 0;
    event_loop_test_native_resize_restore = native_resize_shutdown_restore;
    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    uint32_t window_id = 4343;
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, native_resize_shutdown_seed, &window_id), true);

    struct concurrent_event_loop_end_context end = {
        .event_loop = &g_event_loop,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER
    };
    concurrent_event_loop_end_active = &end;
    event_loop_shutdown_admitted_test_hook = concurrent_event_loop_end_admitted;
    event_loop_shutdown_owner_test_hook = concurrent_event_loop_end_owner;
    pthread_t first, second;
    TEST_CHECK(pthread_create(&first, NULL, concurrent_event_loop_end, &end), 0);
    TEST_CHECK(pthread_create(&second, NULL, concurrent_event_loop_end, &end), 0);
    pthread_mutex_lock(&end.mutex);
    while (end.ready != 2) pthread_cond_wait(&end.cond, &end.mutex);
    end.go = true;
    pthread_cond_broadcast(&end.cond);
    pthread_mutex_unlock(&end.mutex);
    pthread_join(first, NULL);
    pthread_join(second, NULL);
    concurrent_event_loop_end_active = NULL;
    event_loop_shutdown_admitted_test_hook = saved_admitted;
    event_loop_shutdown_owner_test_hook = saved_owner;
    TEST_CHECK(end.admitted, 2);
    TEST_CHECK(end.winners, 1);
    TEST_CHECK(end.losers, 1);
    TEST_CHECK(__atomic_load_n(&g_event_loop.producer_admission, __ATOMIC_ACQUIRE), 0U);
    TEST_CHECK(native_resize_shutdown_restore_count, 1);
    TEST_CHECK(g_event_loop.semaphore == NULL, true);
    TEST_CHECK(g_event_loop.queue.events == NULL, true);

    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, NULL, NULL), true);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    event_loop_end(&g_event_loop);
    pthread_cond_destroy(&end.cond);
    pthread_mutex_destroy(&end.mutex);
    event_loop_test_native_resize_restore = saved_restore;
    g_mouse_state = saved_mouse_state;
    g_event_loop = saved_event_loop;
});

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

static int launch_path_create_count;
static int launch_path_destroy_count;
static int launch_path_observe_count;
static int launch_path_unobserve_count;
static int launch_path_remove_count;
static int launch_path_invalidate_count;
static int launch_path_observer_release_count;
static int launch_path_retry_count;
static int launch_path_signal_count;

static AXUIElementRef launch_path_create_element(pid_t pid)
{
    (void) pid;
    ++launch_path_create_count;
    return (AXUIElementRef) CFStringCreateCopy(NULL, CFSTR("launch-path-ref"));
}

static bool launch_path_not_hidden(struct application *application)
{
    (void) application;
    return false;
}

static void launch_path_connection(struct application *application)
{
    application->connection = 1;
}

static void launch_path_destroy(struct application *application)
{
    (void) application;
    ++launch_path_destroy_count;
}

static AXError launch_path_observer_create(pid_t pid, AXObserverCallback callback, AXObserverRef *observer)
{
    (void) pid; (void) callback;
    ++launch_path_observe_count;
    *observer = (AXObserverRef) CFRetain(CFSTR("launch-path-observer"));
    return kAXErrorSuccess;
}

static AXError launch_path_remove(AXObserverRef observer, AXUIElementRef element, CFStringRef notification)
{
    (void) observer; (void) element; (void) notification;
    ++launch_path_remove_count;
    return kAXErrorSuccess;
}

static void launch_path_invalidate(AXObserverRef observer)
{
    (void) observer;
    ++launch_path_invalidate_count;
}

static void launch_path_observer_release(AXObserverRef observer)
{
    ++launch_path_observer_release_count;
    CFRelease(observer);
}

static void launch_path_unobserve_source(AXObserverRef observer)
{
    (void) observer;
}

static bool launch_path_true(struct process *process)
{
    (void) process;
    return true;
}

static void launch_path_retry(struct process *process)
{
    (void) process;
    ++launch_path_retry_count;
}

static void launch_path_signal(enum signal_type type, void *context)
{
    (void) context;
    if (type == SIGNAL_APPLICATION_LAUNCHED) ++launch_path_signal_count;
}

static CFArrayRef launch_path_empty_window_list(struct application *application)
{
    (void) application;
    return CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
}

static bool launch_path_empty_inventory(struct application *application, uint32_t **list, int *count)
{
    (void) application;
    *list = NULL;
    *count = 0;
    return true;
}

static void launch_path_reset_counts(void)
{
    launch_path_create_count = launch_path_destroy_count = 0;
    launch_path_observe_count = launch_path_unobserve_count = 0;
    launch_path_remove_count = launch_path_invalidate_count = 0;
    launch_path_observer_release_count = launch_path_retry_count = 0;
    launch_path_signal_count = 0;
}

static void launch_path_cleanup_application(struct window_manager *wm, pid_t pid)
{
    struct application *application = window_manager_find_application(wm, pid);
    if (!application) return;
    window_manager_remove_application(wm, pid);
    application_unobserve(application);
    ++launch_path_unobserve_count;
    application_destroy(application);
}

TEST_FUNC(application_launch_and_startup_table_failures_retry_and_recover_through_callers,
{
    struct table saved_applications = g_window_manager.application;
    struct table saved_processes = g_process_manager.process;
    struct application **saved_refresh = g_window_manager.applications_to_refresh;
    __auto_type saved_ax_create = application_create_ax_element;
    __auto_type saved_hidden = application_create_is_hidden;
    __auto_type saved_connection = application_create_connection;
    __auto_type saved_destroy = application_destroy_observer;
    __auto_type saved_observer_create = application_observer_create;
    __auto_type saved_observer_add = application_observer_add_notification;
    __auto_type saved_observer_remove = application_observer_remove_notification;
    __auto_type saved_add_source = application_observer_add_run_loop_source;
    __auto_type saved_invalidate = application_observer_invalidate;
    __auto_type saved_observer_release = application_observer_release;
    __auto_type saved_finished = event_loop_test_application_finished;
    __auto_type saved_observable = event_loop_test_application_observable;
    __auto_type saved_retry = event_loop_test_application_retry_scheduler;
    __auto_type saved_signal = event_signal_test_push_observer;
    __auto_type saved_list = window_manager_test_application_window_list;
    __auto_type saved_inventory = window_manager_test_existing_application_window_list;
    __auto_type saved_startup_observable = window_manager_test_application_is_observable;

    g_window_manager.application = (struct table) {0};
    g_process_manager.process = (struct table) {0};
    g_window_manager.applications_to_refresh = NULL;
    application_create_ax_element = launch_path_create_element;
    application_create_is_hidden = launch_path_not_hidden;
    application_create_connection = launch_path_connection;
    application_destroy_observer = launch_path_destroy;
    application_observer_create = launch_path_observer_create;
    application_observer_add_notification = application_test_observer_add_notification;
    application_observer_remove_notification = launch_path_remove;
    application_observer_add_run_loop_source = launch_path_unobserve_source;
    application_observer_invalidate = launch_path_invalidate;
    application_observer_release = launch_path_observer_release;
    event_loop_test_application_finished = launch_path_true;
    event_loop_test_application_observable = launch_path_true;
    event_loop_test_application_retry_scheduler = launch_path_retry;
    event_signal_test_push_observer = launch_path_signal;
    window_manager_test_application_window_list = launch_path_empty_window_list;
    window_manager_test_existing_application_window_list = launch_path_empty_inventory;
    window_manager_test_application_is_observable = launch_path_true;
    application_test_reset(kAXErrorSuccess);

    struct process process = { .pid = 5151, .psn = { .highLongOfPSN = 51, .lowLongOfPSN = 52 },
                               .name = "launch-path", .ns_application = (void *) 1 };

    for (int rehash = 0; rehash <= 1; ++rehash) {
        table_init(&g_window_manager.application, rehash ? 2 : 4, hash_wm, compare_wm);
        struct application sentinel = { .pid = 6161 };
        if (rehash) table_add(&g_window_manager.application, &sentinel.pid, &sentinel);
        launch_path_reset_counts();
        application_test_reset(kAXErrorSuccess);
        process.launch_retries = 0;
        table_test_fail_allocation_after(rehash ? 2 : 0);
        event_loop_dispatch_event((struct event) { .type = APPLICATION_LAUNCHED, .context = &process });
        table_test_fail_allocation_after(-1);
        TEST_CHECK(window_manager_find_application(&g_window_manager, process.pid) == NULL, true);
        TEST_CHECK(g_window_manager.application.count, rehash ? 1 : 0);
        TEST_CHECK(launch_path_create_count, 1);
        TEST_CHECK(launch_path_destroy_count, 1);
        TEST_CHECK(launch_path_observe_count, 1);
        TEST_CHECK(launch_path_remove_count, 7);
        TEST_CHECK(launch_path_invalidate_count, 1);
        TEST_CHECK(launch_path_observer_release_count, 1);
        TEST_CHECK(launch_path_retry_count, 1);
        TEST_CHECK(launch_path_signal_count, 0);

        application_test_reset(kAXErrorSuccess);
        event_loop_dispatch_event((struct event) { .type = APPLICATION_LAUNCHED, .context = &process });
        TEST_CHECK(window_manager_find_application(&g_window_manager, process.pid) != NULL, true);
        TEST_CHECK(process.launch_retries, 0U);
        TEST_CHECK(launch_path_signal_count, 1);
        launch_path_cleanup_application(&g_window_manager, process.pid);
        TEST_CHECK(launch_path_create_count, 2);
        TEST_CHECK(launch_path_destroy_count, 2);
        TEST_CHECK(launch_path_observe_count, 2);
        TEST_CHECK(launch_path_remove_count, 14);
        TEST_CHECK(process.name, "launch-path");
        TEST_CHECK(process.ns_application, (void *) 1);
        table_free(&g_window_manager.application);
    }

    table_init(&g_process_manager.process, 4, hash_psn, compare_psn);
    table_add(&g_process_manager.process, &process.psn, &process);
    for (int rehash = 0; rehash <= 1; ++rehash) {
        table_init(&g_window_manager.application, rehash ? 2 : 4, hash_wm, compare_wm);
        struct application sentinel = { .pid = 6161 };
        if (rehash) table_add(&g_window_manager.application, &sentinel.pid, &sentinel);
        launch_path_reset_counts();
        application_test_reset(kAXErrorSuccess);
        process.launch_retries = 0;
        table_test_fail_allocation_after(rehash ? 2 : 0);
        window_manager_begin(&g_space_manager, &g_window_manager);
        table_test_fail_allocation_after(-1);
        TEST_CHECK(window_manager_find_application(&g_window_manager, process.pid) == NULL, true);
        TEST_CHECK(launch_path_destroy_count, 1);
        TEST_CHECK(launch_path_remove_count, 7);
        TEST_CHECK(launch_path_retry_count, 1);
        application_test_reset(kAXErrorSuccess);
        window_manager_begin(&g_space_manager, &g_window_manager);
        TEST_CHECK(window_manager_find_application(&g_window_manager, process.pid) != NULL, true);
        TEST_CHECK(process.launch_retries, 0U);
        launch_path_cleanup_application(&g_window_manager, process.pid);
        TEST_CHECK(launch_path_create_count, 2);
        TEST_CHECK(launch_path_destroy_count, 2);
        TEST_CHECK(launch_path_observe_count, 2);
        TEST_CHECK(launch_path_remove_count, 14);
        TEST_CHECK(launch_path_signal_count, 0);
        TEST_CHECK(process.name, "launch-path");
        TEST_CHECK(process.ns_application, (void *) 1);
        table_free(&g_window_manager.application);
    }

    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 1), true);
    char launch_semaphore_name[64];
    snprintf(launch_semaphore_name, sizeof(launch_semaphore_name), "/yabai_launch_retry_%d", getpid());
    g_event_loop.semaphore = sem_open(launch_semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(launch_semaphore_name);
    TEST_CHECK(g_event_loop.semaphore != SEM_FAILED, true);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    struct event replaced = {0};
    uint32_t capacity = 0;
    bool post_wake = false;
    TEST_CHECK(event_queue_push(&g_event_loop.queue, (struct event) { .type = WINDOW_MOVED },
                                false, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    process.launch_retries = 0;
    launch_path_retry_count = 0;
    g_event_queue_test_fail_allocation = true;
    TEST_CHECK(event_loop_post_application_launched(&process), false);
    g_event_queue_test_fail_allocation = false;
    TEST_CHECK(process.launch_retries, 1U);
    TEST_CHECK(launch_path_retry_count, 1);
    struct event queued;
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &queued), true);
    TEST_CHECK(event_loop_post_application_launched(&process), true);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &queued), true);
    TEST_CHECK(queued.type, APPLICATION_LAUNCHED);
    table_init(&g_window_manager.application, 4, hash_wm, compare_wm);
    application_test_reset(kAXErrorSuccess);
    event_loop_dispatch_event(queued);
    TEST_CHECK(window_manager_find_application(&g_window_manager, process.pid) != NULL, true);
    launch_path_cleanup_application(&g_window_manager, process.pid);
    table_free(&g_window_manager.application);
    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    event_queue_destroy(&g_event_loop.queue);
    if (g_event_loop.semaphore != SEM_FAILED) sem_close(g_event_loop.semaphore);
    g_event_loop = saved_event_loop;

    process.launch_retries = APPLICATION_LAUNCH_RETRY_LIMIT - 1;
    launch_path_retry_count = 0;
    event_loop_schedule_application_launch_retry(&process);
    event_loop_schedule_application_launch_retry(&process);
    TEST_CHECK(process.launch_retries, APPLICATION_LAUNCH_RETRY_LIMIT);
    TEST_CHECK(launch_path_retry_count, 1);

    table_free(&g_process_manager.process);
    buf_free(g_window_manager.applications_to_refresh);
    g_window_manager.application = saved_applications;
    g_process_manager.process = saved_processes;
    g_window_manager.applications_to_refresh = saved_refresh;
    application_create_ax_element = saved_ax_create;
    application_create_is_hidden = saved_hidden;
    application_create_connection = saved_connection;
    application_destroy_observer = saved_destroy;
    application_observer_create = saved_observer_create;
    application_observer_add_notification = saved_observer_add;
    application_observer_remove_notification = saved_observer_remove;
    application_observer_add_run_loop_source = saved_add_source;
    application_observer_invalidate = saved_invalidate;
    application_observer_release = saved_observer_release;
    event_loop_test_application_finished = saved_finished;
    event_loop_test_application_observable = saved_observable;
    event_loop_test_application_retry_scheduler = saved_retry;
    event_signal_test_push_observer = saved_signal;
    window_manager_test_application_window_list = saved_list;
    window_manager_test_existing_application_window_list = saved_inventory;
    window_manager_test_application_is_observable = saved_startup_observable;
})

static int ax_owner_observe_count;
static int ax_owner_unobserve_count;
static int ax_owner_retain_count;
static int ax_owner_retry_release_count;
static int ax_owner_ref_release_count;
static int ax_owner_signal_count;
static int ax_owner_post_count;
static int ax_owner_array_release_count;
static int ax_owner_list_count;
static CFIndex ax_owner_constructor_retain_count;
static bool ax_owner_observe_success;
static bool ax_owner_observe_retry;
static bool ax_owner_post_accept;
static struct window_created_retry *ax_owner_posted_retry;
static AXUIElementRef ax_owner_list_ref;

enum ax_owner_kind {
    AX_SUCCESS,
    AX_PERMANENT,
    AX_TRANSIENT,
    AX_PENDING,
    AX_ALLOC_FAIL,
    AX_POST_REJECT
};
static enum ax_owner_kind ax_owner_kind;
static bool ax_owner_constructor_path;

static uint32_t ax_owner_window_id(AXUIElementRef ref)
{
    (void) ref;
    if (ax_owner_constructor_path && ax_owner_kind == AX_PENDING) {
        struct rule pending = { .flags = RULE_ONE_SHOT };
        buf_push(g_window_manager.rules, pending);
        rule_one_shot_reassignment_pending = true;
        rule_test_fail_reservation_allocation_at(1);
    }
    return 7171;
}

static pid_t ax_owner_window_pid(AXUIElementRef ref)
{
    (void) ref;
    return 8181;
}

static bool ax_owner_observe(struct window *window, bool *retry)
{
    ++ax_owner_observe_count;
    ax_owner_constructor_retain_count = CFGetRetainCount(window->ref);
    if (retry) *retry = ax_owner_observe_retry;
    return ax_owner_observe_success;
}

static void ax_owner_unobserve(struct window *window)
{
    (void) window;
    ++ax_owner_unobserve_count;
}

static CFTypeRef ax_owner_retain(CFTypeRef ref)
{
    ++ax_owner_retain_count;
    return CFRetain(ref);
}

static void ax_owner_retry_release(CFTypeRef ref)
{
    ++ax_owner_retry_release_count;
    CFRelease(ref);
}

static void ax_owner_ref_release(CFTypeRef ref)
{
    ++ax_owner_ref_release_count;
    CFRelease(ref);
}

static bool ax_owner_post(struct window_created_retry *retry)
{
    ++ax_owner_post_count;
    if (ax_owner_post_accept) ax_owner_posted_retry = retry;
    return ax_owner_post_accept;
}

static void *ax_owner_no_allocate(size_t size)
{
    (void) size;
    return NULL;
}

static void ax_owner_signal(enum signal_type type, void *context)
{
    (void) context;
    if (type == SIGNAL_WINDOW_CREATED) ++ax_owner_signal_count;
}

static void ax_owner_array_release(CFAllocatorRef allocator, const void *value)
{
    (void) allocator;
    ++ax_owner_array_release_count;
    CFRelease(value);
}

static CFArrayRef ax_owner_window_list(struct application *application)
{
    (void) application;
    if (ax_owner_list_count++) return NULL;
    const void *values[] = { ax_owner_list_ref };
    CFArrayCallBacks callbacks = kCFTypeArrayCallBacks;
    callbacks.release = ax_owner_array_release;
    return CFArrayCreate(NULL, values, 1, &callbacks);
}

static void ax_owner_reset(void)
{
    ax_owner_observe_count = ax_owner_unobserve_count = 0;
    ax_owner_retain_count = ax_owner_retry_release_count = 0;
    ax_owner_ref_release_count = ax_owner_signal_count = 0;
    ax_owner_post_count = ax_owner_array_release_count = ax_owner_list_count = 0;
    ax_owner_constructor_retain_count = 0;
    ax_owner_observe_success = false;
    ax_owner_observe_retry = false;
    ax_owner_post_accept = true;
    ax_owner_posted_retry = NULL;
}

TEST_FUNC(window_created_event_ax_ownership_matrix_is_exact,
{
    struct table saved_applications = g_window_manager.application;
    struct table saved_windows = g_window_manager.window;
    struct table saved_managed = g_window_manager.managed_window;
    struct table saved_lost = g_window_manager.window_lost_focused_event;
    struct rule *saved_rules = g_window_manager.rules;
    struct application **saved_refresh = g_window_manager.applications_to_refresh;
    __auto_type saved_rule_window = window_manager_test_rule_window;
    __auto_type saved_id = window_manager_test_ax_window_id;
    __auto_type saved_pid = event_loop_test_ax_window_pid;
    __auto_type saved_observe = window_manager_test_window_observe;
    __auto_type saved_unobserve = window_manager_test_window_unobserve;
    __auto_type saved_retain = window_manager_test_retry_retain;
    __auto_type saved_retry_release = window_manager_test_retry_release;
    __auto_type saved_allocate = window_manager_test_retry_allocate;
    __auto_type saved_post = window_manager_test_retry_post;
    __auto_type saved_ref_release = window_test_ref_release;
    __auto_type saved_signal = event_signal_test_push_observer;
    __auto_type saved_list = window_manager_test_application_window_list;
    __auto_type saved_inventory = window_manager_test_existing_application_window_list;

    g_window_manager.application = (struct table) {0};
    g_window_manager.window = (struct table) {0};
    g_window_manager.managed_window = (struct table) {0};
    g_window_manager.window_lost_focused_event = (struct table) {0};
    g_window_manager.rules = NULL;
    g_window_manager.applications_to_refresh = NULL;
    table_init(&g_window_manager.application, 8, hash_wm, compare_wm);
    table_init(&g_window_manager.window, 8, hash_wm, compare_wm);
    table_init(&g_window_manager.managed_window, 8, hash_wm, compare_wm);
    table_init(&g_window_manager.window_lost_focused_event, 8, hash_wm, compare_wm);
    struct application application = { .pid = 8181, .psn = { .highLongOfPSN = 81, .lowLongOfPSN = 82 },
                                       .name = "ax-owner" };
    table_add(&g_window_manager.application, &application.pid, &application);
    window_manager_test_ax_window_id = ax_owner_window_id;
    event_loop_test_ax_window_pid = ax_owner_window_pid;
    window_manager_test_window_observe = ax_owner_observe;
    window_manager_test_window_unobserve = ax_owner_unobserve;
    window_manager_test_retry_retain = ax_owner_retain;
    window_manager_test_retry_release = ax_owner_retry_release;
    window_manager_test_retry_allocate = malloc;
    window_manager_test_retry_post = ax_owner_post;
    window_test_ref_release = ax_owner_ref_release;
    event_signal_test_push_observer = ax_owner_signal;

    ax_owner_constructor_path = false;
    for (int kind = AX_SUCCESS; kind <= AX_POST_REJECT; ++kind) {
        ax_owner_reset();
        struct window *window = legacy_allocation_calloc(1, sizeof(*window));
        window->flags = WINDOW_FLOAT;
        window_set_rule_flag(window, WINDOW_RULE_MANAGED);
        window_manager_test_rule_window = window;
        AXUIElementRef ref = (AXUIElementRef) CFStringCreateCopy(NULL, CFSTR("window-created-owner"));
        if (kind == AX_SUCCESS) ax_owner_observe_success = true;
        if (kind == AX_TRANSIENT || kind == AX_ALLOC_FAIL || kind == AX_POST_REJECT) ax_owner_observe_retry = true;
        if (kind == AX_POST_REJECT) ax_owner_post_accept = false;
        if (kind == AX_ALLOC_FAIL) window_manager_test_retry_allocate = ax_owner_no_allocate;
        if (kind == AX_PENDING) {
            struct rule pending = { .flags = RULE_ONE_SHOT };
            buf_push(g_window_manager.rules, pending);
            rule_one_shot_reassignment_pending = true;
            rule_test_fail_reservation_allocation_at(1);
        }

        event_loop_dispatch_event((struct event) { .type = WINDOW_CREATED, .context = ref });

        TEST_CHECK(ax_owner_observe_count, kind == AX_PENDING ? 0 : 1);
        TEST_CHECK(ax_owner_unobserve_count,
                   kind == AX_PERMANENT || kind == AX_TRANSIENT || kind == AX_ALLOC_FAIL || kind == AX_POST_REJECT ? 1 : 0);
        TEST_CHECK(g_window_manager.window.count, kind == AX_SUCCESS ? 1 : 0);
        TEST_CHECK(ax_owner_signal_count, kind == AX_SUCCESS ? 1 : 0);
        TEST_CHECK(ax_owner_retain_count,
                   kind == AX_TRANSIENT || kind == AX_POST_REJECT ? 2 : kind == AX_PENDING ? 1 : kind == AX_ALLOC_FAIL ? 1 : 0);
        TEST_CHECK(ax_owner_ref_release_count,
                   kind == AX_PERMANENT || kind == AX_TRANSIENT || kind == AX_ALLOC_FAIL || kind == AX_POST_REJECT ? 1 : 0);
        TEST_CHECK(ax_owner_retry_release_count,
                   kind == AX_TRANSIENT || kind == AX_PENDING ? 1 : kind == AX_ALLOC_FAIL ? 1 : kind == AX_POST_REJECT ? 2 : 0);
        TEST_CHECK(ax_owner_posted_retry != NULL, kind == AX_TRANSIENT || kind == AX_PENDING);

        if (kind == AX_SUCCESS) {
            TEST_CHECK(window_manager_find_window(&g_window_manager, 7171), window);
            window_manager_remove_window(&g_window_manager, 7171);
            window_destroy(window);
        } else if (ax_owner_posted_retry) {
            window_manager_destroy_window_created_retry(ax_owner_posted_retry);
            TEST_CHECK(ax_owner_retry_release_count, 2);
        }
        if (kind == AX_PENDING) {
            buf_free(g_window_manager.rules);
            g_window_manager.rules = NULL;
            rule_one_shot_reassignment_pending = false;
            rule_test_fail_reservation_allocation_at(0);
            legacy_allocation_free(window);
        }
        window_manager_test_retry_allocate = malloc;
        window_manager_test_rule_window = NULL;
    }

    ax_owner_constructor_path = true;
    window_manager_test_application_window_list = ax_owner_window_list;
    window_manager_test_existing_application_window_list = launch_path_empty_inventory;
    for (int kind = AX_SUCCESS; kind <= AX_POST_REJECT; ++kind) {
        ax_owner_reset();
        ax_owner_kind = kind;
        application.launch_refresh = false;
        size_t allocation_baseline = legacy_allocation_test_outstanding();
        struct window *window = legacy_allocation_calloc(1, sizeof(*window));
        window->flags = WINDOW_FLOAT;
        window_set_rule_flag(window, WINDOW_RULE_MANAGED);
        window_manager_test_rule_window = window;
        const UInt8 ref_bytes[] = { 0x41, 0x58 };
        ax_owner_list_ref = (AXUIElementRef) CFDataCreate(NULL, ref_bytes, sizeof(ref_bytes));
        TEST_CHECK((int) CFGetRetainCount(ax_owner_list_ref), 1);
        if (kind == AX_SUCCESS) ax_owner_observe_success = true;
        if (kind == AX_TRANSIENT || kind == AX_ALLOC_FAIL || kind == AX_POST_REJECT) ax_owner_observe_retry = true;
        if (kind == AX_POST_REJECT) ax_owner_post_accept = false;
        if (kind == AX_ALLOC_FAIL) window_manager_test_retry_allocate = ax_owner_no_allocate;

        int created_count = -1;
        struct window **created = window_manager_add_application_windows(&g_space_manager, &g_window_manager,
                                                                          &application, &created_count);

        bool accepted_retry = kind == AX_TRANSIENT || kind == AX_PENDING;
        bool destroyed_window = kind == AX_PERMANENT || kind == AX_TRANSIENT ||
                                kind == AX_ALLOC_FAIL || kind == AX_POST_REJECT;
        TEST_CHECK(ax_owner_array_release_count, 1);
        TEST_CHECK(ax_owner_list_count, 2);
        TEST_CHECK(ax_owner_observe_count, kind == AX_PENDING ? 0 : 1);
        TEST_CHECK(ax_owner_constructor_retain_count, kind == AX_PENDING ? 0 : 3);
        TEST_CHECK(ax_owner_unobserve_count, destroyed_window ? 1 : 0);
        TEST_CHECK(ax_owner_signal_count, 0);
        TEST_CHECK(ax_owner_retain_count,
                   kind == AX_TRANSIENT || kind == AX_POST_REJECT ? 2 :
                   kind == AX_PENDING || kind == AX_ALLOC_FAIL ? 1 : 0);
        TEST_CHECK(ax_owner_ref_release_count, destroyed_window ? 1 : 0);
        TEST_CHECK(ax_owner_retry_release_count,
                   kind == AX_TRANSIENT || kind == AX_PENDING || kind == AX_ALLOC_FAIL ? 1 :
                   kind == AX_POST_REJECT ? 2 : 0);
        TEST_CHECK(ax_owner_post_count,
                   kind == AX_TRANSIENT || kind == AX_PENDING || kind == AX_POST_REJECT ? 1 : 0);
        TEST_CHECK(ax_owner_posted_retry != NULL, accepted_retry);
        TEST_CHECK(g_window_manager.window.count, kind == AX_SUCCESS ? 1 : 0);
        TEST_CHECK(window_manager_find_window(&g_window_manager, 7171), kind == AX_SUCCESS ? window : NULL);
        TEST_CHECK(created_count, kind == AX_SUCCESS ? 1 : 0);
        TEST_CHECK(buf_len(created), kind == AX_SUCCESS ? 1 : 0);
        if (kind == AX_SUCCESS) TEST_CHECK(created[0], window);
        TEST_CHECK(legacy_allocation_test_outstanding(),
                   allocation_baseline + (kind == AX_SUCCESS || kind == AX_PENDING ? 1 : 0));
        TEST_CHECK((int) CFGetRetainCount(ax_owner_list_ref),
                   kind == AX_SUCCESS || accepted_retry ? 2 : 1);

        if (kind == AX_SUCCESS) {
            window_manager_remove_window(&g_window_manager, 7171);
            window_destroy(window);
            TEST_CHECK(ax_owner_ref_release_count, 1);
        } else if (accepted_retry) {
            struct window_created_retry *retry = ax_owner_posted_retry;
            TEST_CHECK(retry->window_ref, ax_owner_list_ref);
            ax_owner_posted_retry = NULL;
            window_manager_destroy_window_created_retry(retry);
            TEST_CHECK(ax_owner_retry_release_count, 2);
        }
        if (kind == AX_PENDING) {
            buf_free(g_window_manager.rules);
            g_window_manager.rules = NULL;
            rule_one_shot_reassignment_pending = false;
            rule_test_fail_reservation_allocation_at(0);
            legacy_allocation_free(window);
        }
        TEST_CHECK(ax_owner_posted_retry, NULL);
        TEST_CHECK(legacy_allocation_test_outstanding(), allocation_baseline);
        TEST_CHECK((int) CFGetRetainCount(ax_owner_list_ref), 1);
        CFRelease(ax_owner_list_ref);
        buf_free(created);
        window_manager_test_retry_allocate = malloc;
        window_manager_test_rule_window = NULL;
        buf_free(g_window_manager.applications_to_refresh);
        g_window_manager.applications_to_refresh = NULL;
    }
    ax_owner_constructor_path = false;

    table_free(&g_window_manager.application);
    table_free(&g_window_manager.window);
    table_free(&g_window_manager.managed_window);
    table_free(&g_window_manager.window_lost_focused_event);
    g_window_manager.application = saved_applications;
    g_window_manager.window = saved_windows;
    g_window_manager.managed_window = saved_managed;
    g_window_manager.window_lost_focused_event = saved_lost;
    g_window_manager.rules = saved_rules;
    g_window_manager.applications_to_refresh = saved_refresh;
    window_manager_test_rule_window = saved_rule_window;
    window_manager_test_ax_window_id = saved_id;
    event_loop_test_ax_window_pid = saved_pid;
    window_manager_test_window_observe = saved_observe;
    window_manager_test_window_unobserve = saved_unobserve;
    window_manager_test_retry_retain = saved_retain;
    window_manager_test_retry_release = saved_retry_release;
    window_manager_test_retry_allocate = saved_allocate;
    window_manager_test_retry_post = saved_post;
    window_test_ref_release = saved_ref_release;
    event_signal_test_push_observer = saved_signal;
    window_manager_test_application_window_list = saved_list;
    window_manager_test_existing_application_window_list = saved_inventory;
})

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
    return process_manager_publish_front_switch(&g_process_manager, psn);
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
    struct process *terminated_process = legacy_allocation_calloc(1, sizeof(struct process));
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
    struct process rejected_process = { .psn = psn_a, .pid = 101, .name = "rejected" };
    uint64_t rejected_generation = process_manager_publish_front_switch(&g_process_manager, rejected_process.psn);
    TEST_CHECK(event_loop_post_tracked_application_front_switched(&rejected_process, rejected_generation), false);
    TEST_CHECK(g_process_manager.front_switch_generation, generation_before_rejection + 1);
    TEST_CHECK(g_process_manager.front_switch_psn, process_manager_pack_psn(psn_a));
    event_loop_replay_lost_front_switched(&replay_process);
    g_event_queue_test_fail_allocation = false;
    TEST_CHECK(g_event_loop.queue.count, 1U);
    TEST_CHECK(window_manager_find_lost_front_switched_event(&g_window_manager, &psn_b), 0U);
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
    TEST_CHECK(event.context != NULL, true);
    TEST_CHECK(event.param1, 0);
    declarative_reload_test_clear_reconcile_reservation();
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

TEST_FUNC(declarative_reconcile_reservation_token_cancellation_is_generation_safe,
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    uint64_t old_token = ++declarative_reconcile_token_counter;
    declarative_reconcile_active_token = old_token;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    declarative_configuration_reconcile_cancel(old_token);

    pthread_mutex_lock(&declarative_reconcile_lock);
    uint64_t new_token = ++declarative_reconcile_token_counter;
    declarative_reconcile_active_token = new_token;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    declarative_configuration_reconcile_cancel(old_token);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(declarative_reload_test_reconcile_token(), new_token);

    /* Queue rejection/shutdown discard owns and releases only its token. */
    event_loop_discard_event(CONFIGURATION_RECONCILE, (void *) (uintptr_t) new_token, 0, false);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 0U);
})

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
    declarative_reload_test_clear_reconcile_reservation();

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
    TEST_CHECK(event.context != NULL, true);
    TEST_CHECK(event.param1, 0);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);
    declarative_reload_test_clear_reconcile_reservation();

    EVENT_HANDLER_SYSTEM_WOKE(NULL, 0);
    EVENT_HANDLER_SYSTEM_WOKE(NULL, 0);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, CONFIGURATION_RECONCILE);
    TEST_CHECK(event.context != NULL, true);
    TEST_CHECK(event.param1, 0);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);

    declarative_reload_test_clear_reconcile_reservation();
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
    /* Invalid installed arguments are rejected before reservation ownership is consumed. */
    TEST_CHECK(declarative_reconcile_entry_test_reserved, 0U);
    TEST_CHECK(declarative_reconcile_entry_test_queue_count, 0U);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 0U);
    declarative_reload_test_clear_reconcile_reservation();
    printf("                   phase5 reconcile queue accepted=1 skipped=2 rejected=1 preserved_invalid=1 owned_contexts=0 owned_sockets=0\n");
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
    uint64_t saved_installed_generation = declarative_installed.generation;
    uint64_t pre_shutdown_generation = g_event_loop.generation;
    pthread_mutex_lock(&declarative_reconcile_lock);
    uint64_t pre_shutdown_token = ++declarative_reconcile_token_counter;
    declarative_reconcile_active_token = pre_shutdown_token;
    declarative_reconcile_retry_waiting = true;
    pthread_mutex_unlock(&declarative_reconcile_lock);

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
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 0U);
    TEST_CHECK(declarative_reconcile_retry_ready(pre_shutdown_token, pre_shutdown_generation), false);

    /* A late pre-shutdown callback cannot post into or cancel a restarted loop. */
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, NULL, NULL), true);
    declarative_install_test_set_generation(1);
    declarative_configuration_request_reconcile();
    uint64_t restarted_token = declarative_reload_test_reconcile_token();
    TEST_CHECK(restarted_token != 0 && restarted_token != pre_shutdown_token, true);
    TEST_CHECK(declarative_reconcile_retry_ready(pre_shutdown_token, pre_shutdown_generation), false);
    TEST_CHECK(declarative_reload_test_reconcile_token(), restarted_token);
    event_loop_end(&g_event_loop);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 0U);
    declarative_installed.generation = saved_installed_generation;
    g_event_loop = saved_event_loop;
});

static void *event_loop_front_switch_test_allocation_failure(size_t size)
{
    (void) size;
    return NULL;
}

TEST_FUNC(event_loop_front_switch_helpers_preserve_monotonic_observation_on_delivery_failure,
{
    struct event_loop saved_event_loop = g_event_loop;
    pid_t saved_front_pid = g_process_manager.front_pid;
    uint64_t saved_generation = g_process_manager.front_switch_generation;
    uint64_t saved_front_psn = g_process_manager.front_switch_psn;
    __auto_type saved_alloc = event_loop_application_front_switched_alloc;
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
    uint64_t rejected_generation = process_manager_publish_front_switch(&g_process_manager, process.psn);
    TEST_CHECK(event_loop_post_tracked_application_front_switched(&process, rejected_generation), false);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE), 71ULL);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_psn, __ATOMIC_ACQUIRE), process_manager_pack_psn(process.psn));

    event_loop_application_front_switched_alloc = event_loop_front_switch_test_allocation_failure;
    ProcessSerialNumber allocation_failure_psn = { .highLongOfPSN = 9, .lowLongOfPSN = 10 };
    uint64_t allocation_failure_generation = process_manager_publish_front_switch(&g_process_manager, allocation_failure_psn);
    process.psn = allocation_failure_psn;
    TEST_CHECK(event_loop_post_tracked_application_front_switched(&process, allocation_failure_generation), false);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE), 72ULL);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_psn, __ATOMIC_ACQUIRE), process_manager_pack_psn(allocation_failure_psn));
    event_loop_application_front_switched_alloc = saved_alloc;

    g_process_manager.front_pid = saved_front_pid;
    __atomic_store_n(&g_process_manager.front_switch_generation, saved_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_process_manager.front_switch_psn, saved_front_psn, __ATOMIC_RELEASE);
    event_loop_application_front_switched_alloc = saved_alloc;
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

static int ordinary_application_front_test_update_calls;
static int ordinary_application_front_test_activated_signals;
static int ordinary_application_front_test_front_switched_signals;
static int ordinary_window_focus_test_receive_calls;
static int ordinary_window_focus_test_signal_calls;
static int ordinary_window_focus_test_menu_calls;
static bool ordinary_window_focus_test_frontmost;

static void ordinary_application_front_test_update(struct application *application)
{
    (void) application;
    ++ordinary_application_front_test_update_calls;
}

static void ordinary_application_front_test_signal(enum signal_type type, void *context)
{
    (void) context;
    if (type == SIGNAL_APPLICATION_ACTIVATED) ++ordinary_application_front_test_activated_signals;
    if (type == SIGNAL_APPLICATION_FRONT_SWITCHED) ++ordinary_application_front_test_front_switched_signals;
}

static bool ordinary_window_focus_test_is_frontmost(struct application *application)
{
    (void) application;
    return ordinary_window_focus_test_frontmost;
}

static void ordinary_window_focus_test_did_receive(struct window *window)
{
    (void) window;
    ++ordinary_window_focus_test_receive_calls;
}

static void ordinary_window_focus_test_push_signal(struct window *window)
{
    (void) window;
    ++ordinary_window_focus_test_signal_calls;
}

static void ordinary_window_focus_test_menu_dirty(void)
{
    ++ordinary_window_focus_test_menu_calls;
}

TEST_FUNC(ordinary_application_front_switch_no_refresh_preserves_bookkeeping_without_direct_animation_transition,
{
    __auto_type saved_update_focus = event_loop_update_application_front_focus;
    __auto_type saved_signal = event_loop_application_front_signal;
    struct table saved_application_table = g_window_manager.application;
    struct table saved_lost_front_table = g_window_manager.application_lost_front_switched_event;
    struct application **saved_refresh = g_window_manager.applications_to_refresh;
    pid_t saved_front_pid = g_process_manager.front_pid;
    pid_t saved_last_front_pid = g_process_manager.last_front_pid;
    ProcessSerialNumber saved_front_psn = g_process_manager.front_psn;
    ProcessSerialNumber saved_last_front_psn = g_process_manager.last_front_psn;
    uint64_t saved_generation = __atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE);
    uint64_t saved_tracked_psn = __atomic_load_n(&g_process_manager.front_switch_psn, __ATOMIC_ACQUIRE);
    bool saved_skip_window_focus_animation = g_space_manager.skip_window_focus_animation;
    struct application application = {
        .name = "passive-front",
        .pid = 42,
        .psn = { .highLongOfPSN = 1, .lowLongOfPSN = 2 }
    };

    g_window_manager.application = (struct table) {0};
    g_window_manager.application_lost_front_switched_event = (struct table) {0};
    // Exercise the ordinary no-refresh path. Explicit rule effects during window resolution remain intentional.
    g_window_manager.applications_to_refresh = NULL;
    TEST_CHECK(buf_len(g_window_manager.applications_to_refresh), 0);
    table_init(&g_window_manager.application, 4, hash_wm, compare_wm);
    table_init(&g_window_manager.application_lost_front_switched_event, 4, hash_psn, compare_psn);
    table_add(&g_window_manager.application, &application.pid, &application);
    event_loop_update_application_front_focus = ordinary_application_front_test_update;
    event_loop_application_front_signal = ordinary_application_front_test_signal;
    ordinary_application_front_test_update_calls = 0;
    ordinary_application_front_test_activated_signals = 0;
    ordinary_application_front_test_front_switched_signals = 0;
    g_process_manager.front_pid = 0;
    g_process_manager.last_front_pid = 0;
    g_process_manager.front_psn = (ProcessSerialNumber) {0};
    g_process_manager.last_front_psn = (ProcessSerialNumber) {0};

    g_space_manager.skip_window_focus_animation = true;
    uint64_t generation = process_manager_publish_front_switch(&g_process_manager, application.psn);
    struct application_front_switched_event *event = malloc(sizeof(*event));
    *event = (struct application_front_switched_event) {
        .psn = application.psn,
        .pid = application.pid,
        .generation = generation
    };
    EVENT_HANDLER_APPLICATION_FRONT_SWITCHED(event, 0);
    TEST_CHECK(ordinary_application_front_test_update_calls, 1);
    TEST_CHECK(ordinary_application_front_test_activated_signals, 1);
    TEST_CHECK(ordinary_application_front_test_front_switched_signals, 1);

    g_space_manager.skip_window_focus_animation = false;
    generation = process_manager_publish_front_switch(&g_process_manager, application.psn);
    event = malloc(sizeof(*event));
    *event = (struct application_front_switched_event) {
        .psn = application.psn,
        .pid = application.pid,
        .generation = generation
    };
    EVENT_HANDLER_APPLICATION_FRONT_SWITCHED(event, 0);
    TEST_CHECK(ordinary_application_front_test_update_calls, 2);
    TEST_CHECK(ordinary_application_front_test_activated_signals, 2);
    TEST_CHECK(ordinary_application_front_test_front_switched_signals, 2);

    table_free(&g_window_manager.application);
    table_free(&g_window_manager.application_lost_front_switched_event);
    g_window_manager.application = saved_application_table;
    g_window_manager.application_lost_front_switched_event = saved_lost_front_table;
    g_window_manager.applications_to_refresh = saved_refresh;
    event_loop_update_application_front_focus = saved_update_focus;
    event_loop_application_front_signal = saved_signal;
    g_process_manager.front_pid = saved_front_pid;
    g_process_manager.last_front_pid = saved_last_front_pid;
    g_process_manager.front_psn = saved_front_psn;
    g_process_manager.last_front_psn = saved_last_front_psn;
    __atomic_store_n(&g_process_manager.front_switch_generation, saved_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_process_manager.front_switch_psn, saved_tracked_psn, __ATOMIC_RELEASE);
    g_space_manager.skip_window_focus_animation = saved_skip_window_focus_animation;
});

TEST_FUNC(window_focus_preserves_bookkeeping_without_direct_animation_transition,
{
    __auto_type saved_is_frontmost = event_loop_window_focus_is_frontmost;
    __auto_type saved_did_receive = event_loop_window_focus_did_receive;
    __auto_type saved_push_signal = event_loop_window_focus_push_signal;
    __auto_type saved_menu_dirty = event_loop_window_focus_menu_dirty;
    struct table saved_window_table = g_window_manager.window;
    struct table saved_lost_focused_table = g_window_manager.window_lost_focused_event;
    bool saved_skip_window_focus_animation = g_space_manager.skip_window_focus_animation;
    uint32_t saved_pending_window_focus = __atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE);
    struct application application = { .name = "passive-focus" };
    struct window window = { .id = 58, .application = &application };
    window.id_ptr = &window.id;

    g_window_manager.window = (struct table) {0};
    g_window_manager.window_lost_focused_event = (struct table) {0};
    table_init(&g_window_manager.window, 4, hash_wm, compare_wm);
    table_init(&g_window_manager.window_lost_focused_event, 4, hash_wm, compare_wm);
    table_add(&g_window_manager.window, &window.id, &window);
    event_loop_window_focus_is_frontmost = ordinary_window_focus_test_is_frontmost;
    event_loop_window_focus_did_receive = ordinary_window_focus_test_did_receive;
    event_loop_window_focus_push_signal = ordinary_window_focus_test_push_signal;
    event_loop_window_focus_menu_dirty = ordinary_window_focus_test_menu_dirty;
    ordinary_window_focus_test_frontmost = true;
    ordinary_window_focus_test_receive_calls = 0;
    ordinary_window_focus_test_signal_calls = 0;
    ordinary_window_focus_test_menu_calls = 0;

    /* Compatibility config state does not change the direct WINDOW_FOCUSED behavior. */
    g_space_manager.skip_window_focus_animation = true;
    __atomic_store_n(&__pending_window_focus, 2, __ATOMIC_RELEASE);
    EVENT_HANDLER_WINDOW_FOCUSED((void *)(intptr_t) window.id, 1);
    TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 1U);
    TEST_CHECK(ordinary_window_focus_test_receive_calls, 1);
    TEST_CHECK(ordinary_window_focus_test_signal_calls, 1);
    TEST_CHECK(ordinary_window_focus_test_menu_calls, 1);

    /* A newer queued same-app event performs the same direct-animation-free bookkeeping. */
    EVENT_HANDLER_WINDOW_FOCUSED((void *)(intptr_t) window.id, 1);
    TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 0U);
    TEST_CHECK(ordinary_window_focus_test_receive_calls, 2);
    TEST_CHECK(ordinary_window_focus_test_signal_calls, 2);
    TEST_CHECK(ordinary_window_focus_test_menu_calls, 2);

    g_space_manager.skip_window_focus_animation = false;
    EVENT_HANDLER_WINDOW_FOCUSED((void *)(intptr_t) window.id, 0);
    TEST_CHECK(ordinary_window_focus_test_receive_calls, 3);
    TEST_CHECK(ordinary_window_focus_test_signal_calls, 3);
    TEST_CHECK(ordinary_window_focus_test_menu_calls, 3);

    window.flags |= WINDOW_MINIMIZE;
    EVENT_HANDLER_WINDOW_FOCUSED((void *)(intptr_t) window.id, 0);
    TEST_CHECK(ordinary_window_focus_test_receive_calls, 3);
    TEST_CHECK(window_manager_find_lost_focused_event(&g_window_manager, window.id), true);
    window.flags &= ~WINDOW_MINIMIZE;

    ordinary_window_focus_test_frontmost = false;
    EVENT_HANDLER_WINDOW_FOCUSED((void *)(intptr_t) window.id, 0);
    TEST_CHECK(ordinary_window_focus_test_receive_calls, 3);

    table_free(&g_window_manager.window);
    table_free(&g_window_manager.window_lost_focused_event);
    g_window_manager.window = saved_window_table;
    g_window_manager.window_lost_focused_event = saved_lost_focused_table;
    event_loop_window_focus_is_frontmost = saved_is_frontmost;
    event_loop_window_focus_did_receive = saved_did_receive;
    event_loop_window_focus_push_signal = saved_push_signal;
    event_loop_window_focus_menu_dirty = saved_menu_dirty;
    g_space_manager.skip_window_focus_animation = saved_skip_window_focus_animation;
    __atomic_store_n(&__pending_window_focus, saved_pending_window_focus, __ATOMIC_RELEASE);
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
    declarative_reload_test_clear_reconcile_reservation();

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
    struct window_node other_root = { .window_list = { 4242 }, .window_order = { 4242 }, .window_count = 1 };
    struct view view = { .layout = VIEW_BSP, .root = &root };
    struct view other_view = { .layout = VIEW_BSP, .root = &other_root };
    table_add(&g_window_manager.managed_window, &window.id, &view);
    TEST_CHECK(window_manager_add_managed_window(&g_window_manager, &window, &view), true);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 0U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 0U);
    TEST_CHECK(window_manager_add_managed_window(&g_window_manager, &window, &other_view), true);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);
    window_manager_remove_managed_window(&g_window_manager, window.id);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(event_loop_test_queue_count(&g_event_loop), 1U);
    struct event event = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event.type, CONFIGURATION_RECONCILE);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);

    declarative_reload_test_clear_reconcile_reservation();
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
