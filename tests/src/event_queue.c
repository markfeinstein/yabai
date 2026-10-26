struct event_queue_test_context
{
    int index;
    int releases;
};

static struct event_queue event_queue_test_queue;

static void event_queue_test_destroy(void)
{
    if (event_queue_test_queue.events) {
        free(event_queue_test_queue.events);
        pthread_mutex_destroy(&event_queue_test_queue.lock);
        event_queue_test_queue = (struct event_queue) { 0 };
    }
}

static bool event_queue_test_reset(uint32_t capacity)
{
    event_queue_test_destroy();
    return event_queue_init(&event_queue_test_queue, capacity);
}

static void event_queue_test_release(struct event_queue_test_context *context)
{
    ++context->releases;
}

static void event_queue_test_open_loop(struct event_loop *event_loop)
{
    pthread_mutex_init(&event_loop->producer_lock, NULL);
    pthread_cond_init(&event_loop->producer_cond, NULL);
    event_loop->producer_sync_initialized = true;
    __atomic_store_n(&event_loop->producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
}

static void event_queue_test_close_loop(struct event_loop *event_loop)
{
    event_loop_close_producer_admission(event_loop);
    pthread_cond_destroy(&event_loop->producer_cond);
    pthread_mutex_destroy(&event_loop->producer_lock);
    event_loop->producer_sync_initialized = false;
    __atomic_store_n(&event_loop->producer_admission, 0, __ATOMIC_RELEASE);
}

static bool event_queue_check_grows_past_512k_fifo_and_release_once(char *test_name)
{
    bool result = true;
    const int old_pool_events = (int) (KILOBYTES(512) / sizeof(struct event));
    const int event_count = old_pool_events + 257;
    bool crossed_old_boundary = false;
    struct event_queue_test_context *contexts = calloc(event_count, sizeof(struct event_queue_test_context));

    if (!contexts) return false;
    if (!event_queue_test_reset(16)) {
        free(contexts);
        return false;
    }

    for (int i = 0; i < event_count; ++i) {
        uint32_t capacity = 0;
        bool post_wake = false;
        struct event replaced = { 0 };
        struct event event;
        contexts[i].index = i;
        event.type = WINDOW_CREATED;
        event.param1 = i;
        event.context = &contexts[i];
        enum event_queue_result queue_result = event_queue_push(&event_queue_test_queue, event, false, &replaced, &capacity, &post_wake);

        TEST_CHECK(queue_result != EVENT_QUEUE_FULL, true);
        if (queue_result == EVENT_QUEUE_GREW) {
            TEST_CHECK(capacity >= 32, true);
            TEST_CHECK((capacity & (capacity - 1)) == 0, true);
        }

        if (event_queue_test_queue.count * sizeof(struct event) > KILOBYTES(512)) {
            crossed_old_boundary = true;
        }
    }

    TEST_CHECK(crossed_old_boundary, true);

    for (int i = 0; i < event_count; ++i) {
        struct event event = { 0 };
        TEST_CHECK(event_queue_pop(&event_queue_test_queue, &event), true);
        TEST_CHECK(event.type, WINDOW_CREATED);
        TEST_CHECK(event.param1, i);
        TEST_CHECK(((struct event_queue_test_context *) event.context)->index, i);
        event_queue_test_release(event.context);
    }

    struct event event = { 0 };
    TEST_CHECK(event_queue_pop(&event_queue_test_queue, &event), false);

    for (int i = 0; i < event_count; ++i) {
        TEST_CHECK(contexts[i].releases, 1);
    }

    event_queue_test_destroy();
    free(contexts);
    return result;
}

static bool event_queue_check_wrapped_growth_preserves_fifo_and_release_once(char *test_name)
{
    bool result = true;
    struct event_queue_test_context contexts[7] = { 0 };
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;

    if (!event_queue_test_reset(4)) return false;

    for (int i = 0; i < 4; ++i) {
        contexts[i].index = i + 1;
        struct event event = { .type = WINDOW_CREATED, .param1 = i + 1, .context = &contexts[i] };
        TEST_CHECK(event_queue_push(&event_queue_test_queue, event, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    }

    for (int expected = 1; expected <= 2; ++expected) {
        struct event event = { 0 };
        TEST_CHECK(event_queue_pop(&event_queue_test_queue, &event), true);
        TEST_CHECK(event.param1, expected);
        event_queue_test_release(event.context);
    }

    for (int i = 4; i < 7; ++i) {
        contexts[i].index = i + 1;
        struct event event = { .type = WINDOW_CREATED, .param1 = i + 1, .context = &contexts[i] };
        enum event_queue_result queue_result = event_queue_push(&event_queue_test_queue, event, false, &replaced, &capacity, &post_wake);
        TEST_CHECK(queue_result, i == 6 ? EVENT_QUEUE_GREW : EVENT_QUEUE_ADDED);
    }
    TEST_CHECK(capacity, 8U);

    for (int expected = 3; expected <= 7; ++expected) {
        struct event event = { 0 };
        TEST_CHECK(event_queue_pop(&event_queue_test_queue, &event), true);
        TEST_CHECK(event.type, WINDOW_CREATED);
        TEST_CHECK(event.param1, expected);
        TEST_CHECK(((struct event_queue_test_context *) event.context)->index, expected);
        event_queue_test_release(event.context);
    }

    struct event event = { 0 };
    TEST_CHECK(event_queue_pop(&event_queue_test_queue, &event), false);
    for (int i = 0; i < 7; ++i) {
        TEST_CHECK(contexts[i].releases, 1);
    }

    event_queue_test_destroy();
    return result;
}

static bool event_queue_check_merge_releases_replaced_mouse_move_once(char *test_name)
{
    bool result = true;
    struct event replaced = { 0 };
    struct event_queue_test_context contexts[2] = { 0 };
    struct event first;
    struct event second;
    uint32_t capacity = 0;
    bool post_wake = false;

    contexts[0].index = 0;
    contexts[1].index = 1;

    if (!event_queue_test_reset(4)) return false;

    first.type = MOUSE_MOVED;
    first.param1 = 0;
    first.context = &contexts[0];
    second.type = MOUSE_MOVED;
    second.param1 = 1;
    second.context = &contexts[1];

    TEST_CHECK(event_queue_push(&event_queue_test_queue, first, true, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    TEST_CHECK(event_queue_push(&event_queue_test_queue, second, true, &replaced, &capacity, &post_wake), EVENT_QUEUE_MERGED);
    TEST_CHECK(((struct event_queue_test_context *) replaced.context)->index, 0);
    event_queue_test_release(replaced.context);

    struct event event = { 0 };
    TEST_CHECK(event_queue_pop(&event_queue_test_queue, &event), true);
    TEST_CHECK(event.type, MOUSE_MOVED);
    TEST_CHECK(event.param1, 1);
    event_queue_test_release(event.context);
    TEST_CHECK(event_queue_pop(&event_queue_test_queue, &event), false);

    TEST_CHECK(contexts[0].releases, 1);
    TEST_CHECK(contexts[1].releases, 1);

    event_queue_test_destroy();
    return result;
}

static bool event_queue_check_adjacent_mouse_drags_coalesce_and_release_replaced_context(char *test_name)
{
    bool result = true;
    struct event_loop event_loop = { 0 };
    CFMutableArrayRef first = CFArrayCreateMutable(NULL, 0, NULL);
    CFMutableArrayRef second = CFArrayCreateMutable(NULL, 0, NULL);

    if (!first || !second) {
        if (first) CFRelease(first);
        if (second) CFRelease(second);
        return false;
    }

    CFRetain(first);
    CFRetain(second);
    if (!event_queue_init(&event_loop.queue, 4)) {
        CFRelease(first);
        CFRelease(first);
        CFRelease(second);
        CFRelease(second);
        return false;
    }
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai_drag_merge_%d", getpid());
    sem_t *semaphore = sem_open(semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(semaphore_name);
    if (semaphore == SEM_FAILED) {
        event_queue_destroy(&event_loop.queue);
        CFRelease(first);
        CFRelease(first);
        CFRelease(second);
        CFRelease(second);
        return false;
    }
    event_loop.semaphore = semaphore;
    event_queue_test_open_loop(&event_loop);

    event_loop_post(&event_loop, MOUSE_DRAGGED, first, 1);
    TEST_CHECK((int) CFGetRetainCount(first), 2);
    event_loop_post(&event_loop, MOUSE_DRAGGED, second, 1);
    TEST_CHECK(event_loop.queue.count, 1U);
    TEST_CHECK((int) CFGetRetainCount(first), 1);
    TEST_CHECK((int) CFGetRetainCount(second), 2);

    struct event event = { 0 };
    TEST_CHECK(event_queue_pop(&event_loop.queue, &event), true);
    TEST_CHECK(event.type, MOUSE_DRAGGED);
    TEST_CHECK(event.param1, 1);
    TEST_CHECK(event.context == second, true);
    event_loop_release_event_context(event.type, event.context);
    TEST_CHECK((int) CFGetRetainCount(second), 1);
    bool popped_extra = event_queue_pop(&event_loop.queue, &event);
    TEST_CHECK(popped_extra, false);
    if (popped_extra) event_loop_release_event_context(event.type, event.context);

    event_queue_test_close_loop(&event_loop);
    sem_close(semaphore);
    event_queue_destroy(&event_loop.queue);
    CFRelease(first);
    CFRelease(second);
    return result;
}

static bool event_queue_check_different_mouse_button_drags_preserve_fifo(char *test_name)
{
    bool result = true;
    struct event_queue_test_context contexts[2] = { 0 };
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;

    if (!event_queue_test_reset(4)) return false;

    for (int i = 0; i < 2; ++i) {
        contexts[i].index = i;
        struct event event = { .type = MOUSE_DRAGGED, .param1 = i, .context = &contexts[i] };
        TEST_CHECK(event_queue_push(&event_queue_test_queue, event, true, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    }
    TEST_CHECK(event_queue_test_queue.count, 2U);

    for (int i = 0; i < 2; ++i) {
        struct event event = { 0 };
        TEST_CHECK(event_queue_pop(&event_queue_test_queue, &event), true);
        TEST_CHECK(event.type, MOUSE_DRAGGED);
        TEST_CHECK(event.param1, i);
        TEST_CHECK(event.context == &contexts[i], true);
        event_queue_test_release(event.context);
    }
    TEST_CHECK(contexts[0].releases, 1);
    TEST_CHECK(contexts[1].releases, 1);

    event_queue_test_destroy();
    return result;
}

static bool event_queue_check_mouse_up_boundary_preserves_drag_fifo(char *test_name)
{
    bool result = true;
    struct event_loop event_loop = { 0 };
    CFMutableArrayRef contexts[] = {
        CFArrayCreateMutable(NULL, 0, NULL),
        CFArrayCreateMutable(NULL, 0, NULL),
        CFArrayCreateMutable(NULL, 0, NULL),
    };
    enum event_type types[] = { MOUSE_DRAGGED, MOUSE_UP, MOUSE_DRAGGED };

    for (int i = 0; i < array_count(contexts); ++i) {
        if (!contexts[i]) {
            for (int j = 0; j < array_count(contexts); ++j) {
                if (contexts[j]) CFRelease(contexts[j]);
            }
            return false;
        }
    }
    for (int i = 0; i < array_count(contexts); ++i) {
        CFRetain(contexts[i]);
    }

    if (!event_queue_init(&event_loop.queue, 4)) {
        for (int i = 0; i < array_count(contexts); ++i) {
            CFRelease(contexts[i]);
            CFRelease(contexts[i]);
        }
        return false;
    }
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai_drag_bound_%d", getpid());
    sem_t *semaphore = sem_open(semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(semaphore_name);
    if (semaphore == SEM_FAILED) {
        event_queue_destroy(&event_loop.queue);
        for (int i = 0; i < array_count(contexts); ++i) {
            CFRelease(contexts[i]);
            CFRelease(contexts[i]);
        }
        return false;
    }
    event_loop.semaphore = semaphore;
    event_queue_test_open_loop(&event_loop);

    for (int i = 0; i < array_count(contexts); ++i) {
        event_loop_post(&event_loop, types[i], contexts[i], i + 1);
    }
    TEST_CHECK(event_loop.queue.count, 3U);

    for (int i = 0; i < array_count(contexts); ++i) {
        struct event event = { 0 };
        TEST_CHECK(event_queue_pop(&event_loop.queue, &event), true);
        TEST_CHECK(event.type, types[i]);
        TEST_CHECK(event.param1, i + 1);
        TEST_CHECK(event.context == contexts[i], true);
        event_loop_release_event_context(event.type, event.context);
        TEST_CHECK((int) CFGetRetainCount(contexts[i]), 1);
    }

    struct event event = { 0 };
    TEST_CHECK(event_queue_pop(&event_loop.queue, &event), false);

    event_queue_test_close_loop(&event_loop);
    sem_close(semaphore);
    event_queue_destroy(&event_loop.queue);
    for (int i = 0; i < array_count(contexts); ++i) {
        CFRelease(contexts[i]);
    }
    return result;
}

static bool event_queue_check_full_releases_owned_context_once(char *test_name)
{
    bool result = true;
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;
    struct event first;
    struct event_loop event_loop = { 0 };
    CFMutableArrayRef owned_context = CFArrayCreateMutable(NULL, 0, NULL);

    if (!owned_context) return false;
    CFRetain(owned_context);

    if (!event_queue_init(&event_loop.queue, 1)) {
        CFRelease(owned_context);
        CFRelease(owned_context);
        return false;
    }
    event_queue_test_open_loop(&event_loop);

    first.type = WINDOW_MOVED;
    first.param1 = 1;
    first.context = (void *)(intptr_t) 1;
    TEST_CHECK(event_queue_push(&event_loop.queue, first, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);

    TEST_CHECK((int) CFGetRetainCount(owned_context), 2);
    g_event_queue_test_fail_allocation = true;
    event_loop_post(&event_loop, WINDOW_CREATED, owned_context, 0);
    TEST_CHECK((int) CFGetRetainCount(owned_context), 1);
    TEST_CHECK(event_loop.queue.count, 1);

    event_loop_post(&event_loop, WINDOW_MOVED, first.context, 0);
    g_event_queue_test_fail_allocation = false;
    TEST_CHECK(event_loop.queue.count, 1);

    CFRelease(owned_context);
    event_queue_test_close_loop(&event_loop);
    event_queue_destroy(&event_loop.queue);
    return result;
}

static bool event_queue_fd_is_open(int fd)
{
    return fcntl(fd, F_GETFD) != -1;
}

static void event_queue_close_fd_if_open(int fd)
{
    if (event_queue_fd_is_open(fd)) close(fd);
}

static bool event_queue_check_full_closes_daemon_message_fd_once(char *test_name)
{
    bool result = true;
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;
    struct event first;
    struct event_loop event_loop = { 0 };
    int daemon_pair[2] = { -1, -1 };
    int scalar_pair[2] = { -1, -1 };

    if (!event_queue_init(&event_loop.queue, 1)) return false;
    event_queue_test_open_loop(&event_loop);

    first.type = WINDOW_MOVED;
    first.param1 = 1;
    first.context = (void *)(intptr_t) 1;
    TEST_CHECK(event_queue_push(&event_loop.queue, first, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);

    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, daemon_pair), 0);
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, scalar_pair), 0);

    if (daemon_pair[0] != -1 && scalar_pair[0] != -1) {
        TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 0);
        TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
        TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 1);
        g_event_queue_test_fail_allocation = true;
        event_loop_post(&event_loop, DAEMON_MESSAGE, NULL, daemon_pair[0]);
        TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 0);
        TEST_CHECK(event_queue_fd_is_open(daemon_pair[0]), false);
        TEST_CHECK(event_queue_fd_is_open(daemon_pair[1]), true);
        TEST_CHECK(event_loop.queue.count, 1);

        event_loop_post(&event_loop, WINDOW_MOVED, first.context, scalar_pair[0]);
        g_event_queue_test_fail_allocation = false;
        TEST_CHECK(event_queue_fd_is_open(scalar_pair[0]), true);
        TEST_CHECK(event_queue_fd_is_open(scalar_pair[1]), true);
        TEST_CHECK(event_loop.queue.count, 1);
    }

    g_event_queue_test_fail_allocation = false;
    event_queue_close_fd_if_open(daemon_pair[0]);
    event_queue_close_fd_if_open(daemon_pair[1]);
    event_queue_close_fd_if_open(scalar_pair[0]);
    event_queue_close_fd_if_open(scalar_pair[1]);
    event_queue_test_close_loop(&event_loop);
    event_queue_destroy(&event_loop.queue);
    return result;
}

static void event_queue_child_rejects_destructive_event(enum event_type type)
{
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;
    struct event first;
    struct event_loop event_loop = { 0 };

    if (!event_queue_init(&event_loop.queue, 2)) _exit(2);
    event_queue_test_open_loop(&event_loop);
    event_loop.queue.normal_capacity = 1;
    event_loop.queue.hard_capacity = 2;

    first.type = WINDOW_MOVED;
    first.param1 = 1;
    first.context = (void *)(intptr_t) 1;
    if (event_queue_push(&event_loop.queue, first, false, &replaced, &capacity, &post_wake) != EVENT_QUEUE_ADDED) _exit(3);
    first.type = WINDOW_DESTROYED;
    if (event_queue_push(&event_loop.queue, first, false, &replaced, &capacity, &post_wake) != EVENT_QUEUE_ADDED) _exit(4);

    event_loop_post(&event_loop, type, (void *)(intptr_t) 1, 0);
    _exit(EXIT_SUCCESS);
}

static bool event_queue_check_full_failstops_destructive_events(char *test_name)
{
    bool result = true;
    enum event_type events[] = {
        APPLICATION_TERMINATED, WINDOW_DESTROYED, SLS_WINDOW_DESTROYED,
        SLS_SPACE_DESTROYED, DISPLAY_REMOVED
    };

    for (int i = 0; i < array_count(events); ++i) {
        int status = 0;
        pid_t pid = fork();

        TEST_CHECK(pid != -1, true);
        if (pid == -1) continue;

        if (pid == 0) {
            event_queue_child_rejects_destructive_event(events[i]);
        }

        TEST_CHECK(waitpid(pid, &status, 0), pid);
        TEST_CHECK(WIFEXITED(status), true);
        if (WIFEXITED(status)) {
            TEST_CHECK(WEXITSTATUS(status) != EXIT_SUCCESS, true);
        }
    }

    return result;
}

static int event_queue_retry_retain_count;
static int event_queue_retry_release_count;

static CFTypeRef event_queue_retry_retain(CFTypeRef value)
{
    ++event_queue_retry_retain_count;
    return CFRetain(value);
}

static void event_queue_retry_release(CFTypeRef value)
{
    ++event_queue_retry_release_count;
    CFRelease(value);
}

TEST_FUNC(window_created_retry_rejection_consumes_context_and_ax_reference_once, {
    __auto_type saved_retain = window_manager_test_retry_retain;
    __auto_type saved_release = window_manager_test_retry_release;
    window_manager_test_retry_retain = event_queue_retry_retain;
    window_manager_test_retry_release = event_queue_retry_release;
    struct application application = { .pid = 42, .psn = { .highLongOfPSN = 4, .lowLongOfPSN = 2 } };

    for (int full = 0; full <= 1; ++full) {
        struct event_loop event_loop = {0};
        TEST_CHECK(event_queue_init(&event_loop.queue, 1), true);
        event_queue_test_open_loop(&event_loop);
        event_loop.queue.normal_capacity = 1;
        event_loop.queue.hard_capacity = 1;
        if (full) {
            struct event replaced = {0};
            uint32_t capacity = 0;
            bool wake = false;
            TEST_CHECK(event_queue_push(&event_loop.queue, (struct event) { .type = WINDOW_MOVED },
                                        false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
        } else {
            event_loop_close_producer_admission(&event_loop);
        }

        event_queue_retry_retain_count = 0;
        event_queue_retry_release_count = 0;
        unsigned destroys = window_manager_test_retry_destroy_count;
        AXUIElementRef ref = (AXUIElementRef) CFStringCreateCopy(NULL, CFSTR("queue-retry-owner"));
        struct window_created_retry *retry = window_manager_create_window_created_retry(ref, &application, true, application.pid, false);
        TEST_CHECK(retry != NULL, true);
        TEST_CHECK(event_loop_try_post(&event_loop, WINDOW_CREATED_RETRY, retry, 0), false);
        TEST_CHECK(event_queue_retry_retain_count, 1);
        TEST_CHECK(event_queue_retry_release_count, 1);
        TEST_CHECK(window_manager_test_retry_destroy_count, destroys + 1);
        TEST_CHECK(event_loop.queue.count, full ? 1U : 0U);
        TEST_CHECK(event_loop_admission_count(__atomic_load_n(&event_loop.producer_admission, __ATOMIC_ACQUIRE)), 0U);
        CFRelease(ref);

        event_queue_test_close_loop(&event_loop);
        event_queue_destroy(&event_loop.queue);
    }

    window_manager_test_retry_retain = saved_retain;
    window_manager_test_retry_release = saved_release;
});

TEST_FUNC(event_queue_normal_and_hard_cap_reserve_is_bounded, {
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;
    TEST_CHECK(event_queue_test_reset(2), true);
    event_queue_test_queue.normal_capacity = 2;
    event_queue_test_queue.hard_capacity = 3;

    struct event normal = { .type = WINDOW_MOVED };
    TEST_CHECK(event_queue_push(&event_queue_test_queue, normal, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    TEST_CHECK(post_wake, true);
    TEST_CHECK(event_queue_push(&event_queue_test_queue, normal, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    TEST_CHECK(post_wake, false);
    TEST_CHECK(event_queue_push(&event_queue_test_queue, normal, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_FULL);
    TEST_CHECK(event_queue_test_queue.capacity, 2U);

    struct event critical = { .type = WINDOW_DESTROYED };
    TEST_CHECK(event_queue_push(&event_queue_test_queue, critical, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_GREW);
    TEST_CHECK(event_queue_test_queue.capacity, 3U);
    TEST_CHECK(event_queue_test_queue.count, 3U);
    TEST_CHECK(event_queue_push(&event_queue_test_queue, critical, false, &replaced, &capacity, &post_wake), EVENT_QUEUE_FULL);
    TEST_CHECK(event_queue_test_queue.capacity, 3U);

    struct event popped;
    while (event_queue_pop(&event_queue_test_queue, &popped)) {}
    TEST_CHECK(event_queue_pop(&event_queue_test_queue, &popped), false);
    event_queue_test_destroy();
});

static volatile int event_queue_topology_resync_count;
static bool event_queue_topology_resync_rearm;
static int event_queue_topology_retry_schedule_count;
static uint64_t event_queue_topology_retry_generation;

static void event_queue_test_topology_retry_scheduler(struct event_loop *event_loop, uint64_t generation)
{
    (void) event_loop;
    ++event_queue_topology_retry_schedule_count;
    event_queue_topology_retry_generation = generation;
}

static void event_queue_test_topology_resync(struct event_loop *event_loop)
{
    __atomic_add_fetch(&event_queue_topology_resync_count, 1, __ATOMIC_RELAXED);
    if (event_queue_topology_resync_rearm) {
        event_queue_topology_resync_rearm = false;
        event_loop_reserve_topology_resync(event_loop);
    }
}

TEST_FUNC(event_loop_topology_retry_chain_is_single_and_generation_bound, {
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai-topology-test-%d", getpid());
    sem_unlink(semaphore_name);
    struct event_loop event_loop = {
        .is_running = true,
        .generation = 77,
        .semaphore = sem_open(semaphore_name, O_CREAT | O_EXCL, 0600, 0)
    };
    sem_unlink(semaphore_name);
    TEST_CHECK(event_loop.semaphore != SEM_FAILED, true);
    event_queue_topology_retry_schedule_count = 0;
    event_queue_topology_retry_generation = 0;
    __atomic_store_n(&event_queue_topology_resync_count, 0, __ATOMIC_RELAXED);
    event_queue_topology_resync_rearm = false;
    event_loop_topology_resync_test_hook = event_queue_test_topology_resync;
    event_loop_topology_resync_test_result = false;
    event_loop_topology_retry_test_scheduler = event_queue_test_topology_retry_scheduler;

    __atomic_store_n(&event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
    TEST_CHECK(event_loop_consume_topology_resync(&event_loop), false);
    TEST_CHECK(event_queue_topology_resync_count, 1);
    TEST_CHECK(event_queue_topology_retry_schedule_count, 1);
    TEST_CHECK(event_queue_topology_retry_generation, 77ULL);
    TEST_CHECK(event_loop.topology_resync_retry_scheduled, 77ULL);

    event_loop_reserve_topology_resync(&event_loop);
    TEST_CHECK(sem_trywait(event_loop.semaphore), -1);
    TEST_CHECK(errno, EAGAIN);
    TEST_CHECK(event_loop_consume_topology_resync(&event_loop), false);
    TEST_CHECK(event_queue_topology_resync_count, 2);
    TEST_CHECK(event_queue_topology_retry_schedule_count, 1);
    TEST_CHECK(event_loop.topology_resync_retry_scheduled, 77ULL);

    event_loop_fire_topology_retry(&event_loop, 76);
    TEST_CHECK(event_loop.topology_resync_retry_scheduled, 77ULL);
    event_loop_fire_topology_retry(&event_loop, 77);
    TEST_CHECK(event_loop.topology_resync_retry_scheduled, 0ULL);
    TEST_CHECK(sem_trywait(event_loop.semaphore), 0);

    event_queue_topology_resync_rearm = true;
    TEST_CHECK(event_loop_consume_topology_resync(&event_loop), false);
    TEST_CHECK(event_queue_topology_resync_count, 3);
    TEST_CHECK(event_queue_topology_retry_schedule_count, 1);
    TEST_CHECK(event_loop.topology_resync_retry_scheduled, 0ULL);
    TEST_CHECK(sem_trywait(event_loop.semaphore), 0);

    event_loop_topology_resync_test_result = true;
    TEST_CHECK(event_loop_consume_topology_resync(&event_loop), true);
    TEST_CHECK(event_queue_topology_resync_count, 4);
    TEST_CHECK(event_loop.topology_resync_pending, 0U);
    TEST_CHECK(event_loop.topology_resync_retry_scheduled, 0ULL);

    event_loop_topology_resync_test_hook = NULL;
    event_loop_topology_resync_test_result = true;
    event_loop_topology_retry_test_scheduler = NULL;
    event_loop.is_running = false;
    sem_close(event_loop.semaphore);
});

TEST_FUNC(event_queue_all_destructive_events_use_hard_reserve, {
    enum event_type critical_types[] = {
        APPLICATION_TERMINATED, WINDOW_DESTROYED, SLS_WINDOW_DESTROYED,
        SLS_SPACE_DESTROYED, DISPLAY_REMOVED
    };
    for (int type_index = 0; type_index < array_count(critical_types); ++type_index) {
        struct event replaced = {0};
        uint32_t capacity = 0;
        bool wake = false;
        TEST_CHECK(event_queue_test_reset(2), true);
        event_queue_test_queue.normal_capacity = 2;
        event_queue_test_queue.hard_capacity = 3;
        TEST_CHECK(event_queue_push(&event_queue_test_queue, (struct event) { .type = WINDOW_MOVED },
                                    false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
        TEST_CHECK(event_queue_push(&event_queue_test_queue, (struct event) { .type = WINDOW_RESIZED },
                                    false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
        TEST_CHECK(event_queue_push(&event_queue_test_queue, (struct event) { .type = critical_types[type_index] },
                                    false, &replaced, &capacity, &wake), EVENT_QUEUE_GREW);
        TEST_CHECK(event_queue_test_queue.count, 3U);
        TEST_CHECK(event_queue_test_queue.capacity, 3U);
    }
    event_queue_test_destroy();
});

TEST_FUNC(event_queue_topology_rejections_coalesce_and_rearm_without_admission, {
    struct event_loop event_loop = {0};
    TEST_CHECK(event_queue_init(&event_loop.queue, 2), true);
    event_loop.queue.normal_capacity = 2;
    event_loop.queue.hard_capacity = 3;
    event_queue_test_open_loop(&event_loop);

    struct event replaced = {0};
    uint32_t capacity = 0;
    bool wake = false;
    TEST_CHECK(event_queue_push(&event_loop.queue, (struct event) { .type = WINDOW_MOVED },
                                false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
    TEST_CHECK(event_queue_push(&event_loop.queue, (struct event) { .type = WINDOW_RESIZED },
                                false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
    uint32_t bounded_capacity = event_loop.queue.capacity;

    TEST_CHECK(event_loop_try_post(&event_loop, SLS_SPACE_CREATED, (void *)(uintptr_t) 41, 0), false);
    TEST_CHECK(event_loop_try_post(&event_loop, DISPLAY_CHANGED, NULL, 0), false);
    TEST_CHECK(event_loop_try_post(&event_loop, SYSTEM_WOKE, NULL, 0), false);
    TEST_CHECK(__atomic_load_n(&event_loop.topology_resync_pending, __ATOMIC_ACQUIRE), 1U);
    TEST_CHECK(event_loop.queue.capacity, bounded_capacity);
    TEST_CHECK(event_loop.queue.count, 2U);

    struct event event;
    while (event_queue_pop(&event_loop.queue, &event)) {}
    __atomic_store_n(&event_queue_topology_resync_count, 0, __ATOMIC_RELAXED);
    event_queue_topology_resync_rearm = true;
    event_loop_topology_resync_test_hook = event_queue_test_topology_resync;
    TEST_CHECK(event_loop_consume_topology_resync(&event_loop), false);
    TEST_CHECK(__atomic_load_n(&event_queue_topology_resync_count, __ATOMIC_RELAXED), 1);
    TEST_CHECK(__atomic_load_n(&event_loop.topology_resync_pending, __ATOMIC_ACQUIRE), 1U);
    TEST_CHECK(event_loop_consume_topology_resync(&event_loop), true);
    TEST_CHECK(__atomic_load_n(&event_queue_topology_resync_count, __ATOMIC_RELAXED), 2);
    TEST_CHECK(event_loop_consume_topology_resync(&event_loop), false);
    TEST_CHECK(event_loop.queue.capacity, bounded_capacity);
    event_loop_topology_resync_test_hook = NULL;

    event_queue_test_close_loop(&event_loop);
    event_queue_destroy(&event_loop.queue);

    event_loop = (struct event_loop) {0};
    __atomic_store_n(&event_queue_topology_resync_count, 0, __ATOMIC_RELAXED);
    event_queue_topology_resync_rearm = true;
    event_loop_topology_resync_test_hook = event_queue_test_topology_resync;
    TEST_CHECK(event_loop_begin(&event_loop), true);
    event_loop_reserve_topology_resync(&event_loop);
    for (int elapsed = 0;
         elapsed < 1000 && __atomic_load_n(&event_queue_topology_resync_count, __ATOMIC_RELAXED) < 2;
         ++elapsed) usleep(1000);
    TEST_CHECK(__atomic_load_n(&event_queue_topology_resync_count, __ATOMIC_RELAXED), 2);
    event_loop_end(&event_loop);
    event_loop_topology_resync_test_hook = NULL;
});

static uint64_t topology_fixture_spaces[10] = { 20, 30 };
static int topology_fixture_space_count = 2;
static int topology_fixture_cycle;
static int topology_fixture_postcommit_count;
static bool topology_fixture_fail_capture;
static int topology_fixture_destroy_count;
static enum declarative_topology_result topology_fixture_capture_result = DECLARATIVE_TOPOLOGY_OK;
static int topology_fixture_capture_failure_call;
static int topology_fixture_capture_count;
static int topology_fixture_capture_mismatch;

static const char *topology_fixture_uuid_string(uint64_t sid)
{
    if (topology_fixture_cycle == 2) {
        if (sid == 10) return "space-B";
        if (sid == 20) return "space-A";
    } else if (topology_fixture_cycle == 3) {
        if (sid == 10) return "space-C";
        if (sid == 20) return "space-A";
        if (sid == 30) return "space-B";
    }
    switch (sid) {
    case 10: case 20: return "space-A";
    case 30: return "space-C";
    case 98: return "space-D";
    case 99: return "space-B";
    default: return "";
    }
}

static char *topology_fixture_owned_string(const char *value)
{
    size_t length = strlen(value);
    char *copy = declarative_topology_calloc(length + 1, 1);
    if (copy) memcpy(copy, value, length + 1);
    return copy;
}

static struct declarative_space_identity topology_fixture_identity(uint64_t sid, const char *uuid,
                                                                    int ordinal)
{
    struct declarative_space_identity identity = {
        .has_uuid = uuid[0] != '\0', .parent_did = 7, .parent_display_ordinal = 0,
        .sid = sid, .kind = sid == 30 ? 1 : 0, .mission_control_ordinal = ordinal
    };
    memcpy(identity.uuid, uuid, strlen(uuid) + 1);
    memcpy(identity.parent_display_uuid, "display-7", sizeof("display-7"));
    identity.fingerprint = declarative_space_identity_fingerprint(&identity);
    return identity;
}

static enum declarative_topology_result topology_fixture_capture(
    const struct declarative_topology_bounds_config *bounds, struct declarative_topology *out)
{
    int capture = ++topology_fixture_capture_count;
    memset(out, 0, sizeof(*out));
    if (!bounds || bounds->external_bar_mode != (int) g_display_manager.mode ||
        bounds->external_bar_top_padding != g_display_manager.top_padding ||
        bounds->external_bar_bottom_padding != g_display_manager.bottom_padding)
        return DECLARATIVE_TOPOLOGY_INVALID;
    if (topology_fixture_fail_capture) return DECLARATIVE_TOPOLOGY_INVALID;
    if (topology_fixture_capture_result != DECLARATIVE_TOPOLOGY_OK &&
        (!topology_fixture_capture_failure_call || topology_fixture_capture_failure_call == capture))
        return topology_fixture_capture_result;
    out->display_count = 1;
    out->space_count = (size_t) topology_fixture_space_count;
    if (topology_fixture_capture_mismatch == 1 && capture == 1) --out->space_count;
    out->active_did = 7;
    uint64_t stable_active_sid = topology_fixture_space_count == 9 ? topology_fixture_spaces[0] : 20;
    out->active_sid = topology_fixture_capture_mismatch == 3 && capture == 2
                    ? topology_fixture_spaces[out->space_count - 1] : stable_active_sid;
    out->displays = declarative_topology_calloc(1, sizeof(*out->displays));
    out->spaces = declarative_topology_calloc(out->space_count, sizeof(*out->spaces));
    if (!out->displays || !out->spaces) {
        declarative_topology_destroy(out);
        return DECLARATIVE_TOPOLOGY_RESOURCE;
    }
    out->displays[0] = (struct declarative_topology_display) {
        .did = 7, .uuid = topology_fixture_owned_string("display-7"),
        .bounds = CGRectMake(topology_fixture_capture_mismatch == 2 && capture == 2 ? 100 : 0,
                             0, 1000, 800), .managed_ordinal = 0
    };
    if (!out->displays[0].uuid) {
        declarative_topology_destroy(out);
        return DECLARATIVE_TOPOLOGY_RESOURCE;
    }
    for (size_t i = 0; i < out->space_count; ++i) {
        uint64_t sid = topology_fixture_spaces[i];
        const char *uuid = topology_fixture_uuid_string(sid);
        out->spaces[i] = (struct declarative_topology_space) {
            .sid = sid, .uuid = topology_fixture_owned_string(uuid), .kind = sid == 30 ? 1 : 0,
            .mission_control_ordinal = (int) i, .parent_display_index = 0,
            .identity = topology_fixture_identity(sid, uuid, (int) i)
        };
        if (!out->spaces[i].uuid) {
            declarative_topology_destroy(out);
            return DECLARATIVE_TOPOLOGY_RESOURCE;
        }
    }
    out->fingerprint = declarative_topology_fingerprint(out);
    return DECLARATIVE_TOPOLOGY_OK;
}

static CFStringRef topology_fixture_uuid(uint64_t sid)
{
    if (topology_fixture_fail_capture && sid == 30) return NULL;
    if (topology_fixture_cycle == 2) {
        if (sid == 10) return CFStringCreateCopy(NULL, CFSTR("space-B"));
        if (sid == 20) return CFStringCreateCopy(NULL, CFSTR("space-A"));
    } else if (topology_fixture_cycle == 3) {
        if (sid == 10) return CFStringCreateCopy(NULL, CFSTR("space-C"));
        if (sid == 20) return CFStringCreateCopy(NULL, CFSTR("space-A"));
        if (sid == 30) return CFStringCreateCopy(NULL, CFSTR("space-B"));
    }
    switch (sid) {
    case 10: case 20: return CFStringCreateCopy(NULL, CFSTR("space-A"));
    case 30: return CFStringCreateCopy(NULL, CFSTR("space-C"));
    case 98: return CFStringCreateCopy(NULL, CFSTR("space-D"));
    case 99: return CFStringCreateCopy(NULL, CFSTR("space-B"));
    default: return NULL;
    }
}
static bool topology_fixture_user(uint64_t sid) { return sid != 30; }
static CFStringRef topology_fixture_view_uuid(uint64_t sid, void *context) { (void) context; return topology_fixture_uuid(sid); }
static bool topology_fixture_view_user(uint64_t sid, void *context) { (void) context; return topology_fixture_user(sid); }
static bool topology_fixture_feedback_destroy(struct window_node *node, void *context)
{
    (void) node; (void) context;
    ++topology_fixture_destroy_count;
    return true;
}
static void topology_fixture_postcommit(void) { ++topology_fixture_postcommit_count; }
static int topology_fixture_retry_schedules;
static void topology_fixture_retry_scheduler(struct event_loop *event_loop, uint64_t generation)
{
    (void) event_loop; (void) generation;
    ++topology_fixture_retry_schedules;
}
static int topology_fixture_space_type(uint64_t sid) { (void) sid; return 0; }

static struct view *topology_fixture_view(uint64_t sid)
{
    struct view *view = legacy_allocation_calloc(1, sizeof(*view));
    if (!view) return NULL;
    view->root = legacy_allocation_calloc(1, sizeof(*view->root));
    view->sid = sid;
    view->uuid = topology_fixture_uuid(sid);
    const char *uuid = topology_fixture_uuid_string(sid);
    view->declarative_identity = topology_fixture_identity(sid, uuid, (int) (sid == 10 ? 0 : 1));
    view->layout = VIEW_FLOAT;
    return view;
}

#define CHECK_TOPOLOGY_FAILURE_UNCHANGED() do { \
    TEST_CHECK(&g_space_manager.view, original_view_table); \
    TEST_CHECK(g_space_manager.view.buckets, original_buckets); \
    TEST_CHECK(g_window_manager.managed_window.buckets, managed_buckets); \
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 10), remapped); \
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 99), stale_labeled); \
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 98), stale_unlabeled); \
    TEST_CHECK(table_find(&g_window_manager.managed_window, &preserved_wid), remapped); \
    TEST_CHECK(table_find(&g_window_manager.managed_window, &removed_wid), stale_labeled); \
    TEST_CHECK(memcmp(remapped, &remapped_before, sizeof(remapped_before)), 0); \
    TEST_CHECK(memcmp(stale_labeled, &stale_labeled_before, sizeof(stale_labeled_before)), 0); \
    TEST_CHECK(memcmp(stale_unlabeled, &stale_unlabeled_before, sizeof(stale_unlabeled_before)), 0); \
    TEST_CHECK(memcmp(g_space_manager.labels, space_labels_before, sizeof(space_labels_before)), 0); \
    TEST_CHECK(memcmp(g_display_manager.labels, display_labels_before, sizeof(display_labels_before)), 0); \
    TEST_CHECK(g_space_manager.current_space_id, 10ULL); \
    TEST_CHECK(g_space_manager.last_space_id, 99ULL); \
    TEST_CHECK(g_display_manager.current_display_id, 7U); \
    TEST_CHECK(g_display_manager.last_display_id, 8U); \
    TEST_CHECK(__atomic_load_n(&g_event_loop.topology_resync_pending, __ATOMIC_ACQUIRE), 1U); \
    TEST_CHECK(topology_fixture_retry_schedules, 1); \
    TEST_CHECK(topology_fixture_postcommit_count, 0); \
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), capture_outstanding); \
    TEST_CHECK(event_loop_topology_outstanding_allocations, 0U); \
} while (0)

TEST_FUNC(event_loop_topology_recovery_is_transactional_complete_and_retryable, {
    struct space_manager saved_space = g_space_manager;
    struct window_manager saved_window = g_window_manager;
    struct display_manager saved_display = g_display_manager;
    struct event_loop saved_loop = g_event_loop;
    struct view_test_platform_ops platform = {
        .copy_space_uuid = topology_fixture_view_uuid,
        .space_is_user = topology_fixture_view_user,
        .feedback_destroy_observer = topology_fixture_feedback_destroy
    };
    view_test_set_platform_ops(&platform);
    g_space_manager = (struct space_manager) { .layout = VIEW_FLOAT };
    g_window_manager = (struct window_manager) { .purify_mode = PURIFY_DISABLED };
    g_display_manager = (struct display_manager) { .current_display_id = 7, .last_display_id = 8 };
    g_space_manager.current_space_id = 10;
    g_space_manager.last_space_id = 99;
    g_event_loop = (struct event_loop) { .semaphore = (sem_t *) (uintptr_t) 1 };
    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 42, __ATOMIC_RELEASE);
    table_init(&g_space_manager.view, 23, hash_view, compare_view);
    table_init(&g_window_manager.managed_window, 23, hash_wm, compare_wm);
    table_init(&g_window_manager.window, 23, hash_wm, compare_wm);
    struct view *remapped = topology_fixture_view(10);
    struct view *stale_labeled = topology_fixture_view(99);
    struct view *stale_unlabeled = topology_fixture_view(98);
    TEST_CHECK(remapped && stale_labeled && stale_unlabeled, true);
    table_add(&g_space_manager.view, &remapped->sid, remapped);
    table_add(&g_space_manager.view, &stale_labeled->sid, stale_labeled);
    table_add(&g_space_manager.view, &stale_unlabeled->sid, stale_unlabeled);
    uint32_t preserved_wid = 1001, removed_wid = 1002;
    table_add(&g_window_manager.managed_window, &preserved_wid, remapped);
    table_add(&g_window_manager.managed_window, &removed_wid, stale_labeled);
    space_manager_set_label_for_space(&g_space_manager, 10, strdup("kept"));
    space_manager_set_label_for_space(&g_space_manager, 99, strdup("stale"));
    display_manager_set_label_for_display(&g_display_manager, 7, strdup("kept-display"));
    display_manager_set_label_for_display(&g_display_manager, 8, strdup("stale-display"));

    event_loop_topology_capture_test = topology_fixture_capture;
    event_loop_topology_postcommit_hook = topology_fixture_postcommit;
    event_loop_topology_retry_test_scheduler = topology_fixture_retry_scheduler;
    topology_fixture_postcommit_count = 0;
    topology_fixture_destroy_count = 0;

    struct table *original_view_table = &g_space_manager.view;
    struct bucket **original_buckets = g_space_manager.view.buckets;
    topology_fixture_fail_capture = true;
    __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
    TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), false);
    TEST_CHECK(g_space_manager.view.buckets, original_buckets);
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 10), remapped);
    TEST_CHECK(__atomic_load_n(&g_event_loop.topology_resync_pending, __ATOMIC_ACQUIRE), 1U);
    TEST_CHECK(topology_fixture_postcommit_count, 0);

    topology_fixture_fail_capture = false;
    size_t capture_outstanding = declarative_topology_test_outstanding_allocations();
    declarative_topology_test_reset_allocator();
    legacy_allocation_test_reset();
    event_loop_topology_fail_allocation_at = 0;
    struct event_loop_topology_plan baseline_plan;
    TEST_CHECK(event_loop_prepare_topology_plan(&baseline_plan), DECLARATIVE_TOPOLOGY_OK);
    size_t capture_allocations = declarative_topology_test_allocation_count();
    int plan_allocations = event_loop_topology_allocation_count;
    size_t view_allocations = legacy_allocation_test_count();
    event_loop_topology_plan_destroy(&baseline_plan, false);
    TEST_CHECK(capture_allocations, 10U);
    TEST_CHECK(plan_allocations, 6);
    TEST_CHECK(view_allocations, 2U);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), capture_outstanding);
    TEST_CHECK(event_loop_topology_outstanding_allocations, 0U);

    struct bucket **managed_buckets = g_window_manager.managed_window.buckets;
    struct view remapped_before = *remapped;
    struct view stale_labeled_before = *stale_labeled;
    struct view stale_unlabeled_before = *stale_unlabeled;
    struct space_label space_labels_before[2] = { g_space_manager.labels[0], g_space_manager.labels[1] };
    struct display_label display_labels_before[2] = { g_display_manager.labels[0], g_display_manager.labels[1] };
    for (size_t ordinal = 1; ordinal <= capture_allocations; ++ordinal) {
        declarative_topology_test_fail_allocation_at(ordinal);
        event_loop_topology_fail_allocation_at = 0;
        topology_fixture_retry_schedules = 0;
        __atomic_store_n(&g_event_loop.topology_resync_retry_scheduled, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
        TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), false);
        CHECK_TOPOLOGY_FAILURE_UNCHANGED();
    }
    declarative_topology_test_reset_allocator();

    for (int ordinal = 1; ordinal <= plan_allocations; ++ordinal) {
        event_loop_topology_fail_allocation_at = ordinal;
        topology_fixture_retry_schedules = 0;
        __atomic_store_n(&g_event_loop.topology_resync_retry_scheduled, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
        TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), false);
        CHECK_TOPOLOGY_FAILURE_UNCHANGED();
    }
    event_loop_topology_fail_allocation_at = 0;

    for (size_t ordinal = 1; ordinal <= view_allocations; ++ordinal) {
        legacy_allocation_test_fail_at(ordinal);
        topology_fixture_retry_schedules = 0;
        __atomic_store_n(&g_event_loop.topology_resync_retry_scheduled, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
        TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), false);
        legacy_allocation_test_reset();
        CHECK_TOPOLOGY_FAILURE_UNCHANGED();
    }

    for (int successful = 0; successful < 8; ++successful) {
        table_test_fail_allocation_after(successful);
        topology_fixture_retry_schedules = 0;
        __atomic_store_n(&g_event_loop.topology_resync_retry_scheduled, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
        TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), false);
        table_test_fail_allocation_after(-1);
        CHECK_TOPOLOGY_FAILURE_UNCHANGED();
    }
    table_test_fail_allocation_after(8);
    TEST_CHECK(event_loop_prepare_topology_plan(&baseline_plan), DECLARATIVE_TOPOLOGY_OK);
    table_test_fail_allocation_after(-1);
    event_loop_topology_plan_destroy(&baseline_plan, false);
    TEST_CHECK(event_loop_topology_outstanding_allocations, 0U);
    TEST_CHECK(topology_fixture_destroy_count, 10);
    int destroy_count_before_commit = topology_fixture_destroy_count;

    event_loop_topology_fail_allocation_at = 0;
    TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), true);
    TEST_CHECK(g_space_manager.view.count, 2);
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 20), remapped);
    TEST_CHECK(remapped->sid, 20ULL);
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 30) != NULL, true);
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 98), NULL);
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 99), NULL);
    TEST_CHECK(table_find(&g_window_manager.managed_window, &preserved_wid), remapped);
    TEST_CHECK(table_find(&g_window_manager.managed_window, &removed_wid), NULL);
    struct space_label *kept_label = space_manager_get_label_for_space(&g_space_manager, 20);
    TEST_CHECK(kept_label != NULL, true);
    TEST_CHECK(kept_label && strcmp(kept_label->label, "kept") == 0, true);
    TEST_CHECK(space_manager_get_label_for_space(&g_space_manager, 99), NULL);
    TEST_CHECK(g_display_manager.current_display_id, 7U);
    TEST_CHECK(g_display_manager.last_display_id, 7U);
    TEST_CHECK(g_space_manager.current_space_id, 20ULL);
    TEST_CHECK(g_space_manager.last_space_id, 20ULL);
    TEST_CHECK(display_manager_get_label_for_display(&g_display_manager, 7) != NULL, true);
    TEST_CHECK(display_manager_get_label_for_display(&g_display_manager, 8), NULL);
    TEST_CHECK(topology_fixture_postcommit_count, 1);
    TEST_CHECK(topology_fixture_destroy_count, destroy_count_before_commit + 2);

    table_for (struct view *view, g_space_manager.view, { view_destroy(view); legacy_allocation_free(view); })
    table_free(&g_space_manager.view);
    table_free(&g_window_manager.managed_window);
    table_free(&g_window_manager.window);
    while (buf_len(g_space_manager.labels)) space_manager_remove_label_for_space(&g_space_manager, g_space_manager.labels[0].sid);
    while (buf_len(g_display_manager.labels)) display_manager_remove_label_for_display(&g_display_manager, g_display_manager.labels[0].did);
    buf_free(g_space_manager.labels);
    buf_free(g_display_manager.labels);
    event_loop_topology_postcommit_hook = NULL;
    event_loop_topology_capture_test = NULL;
    event_loop_topology_retry_test_scheduler = NULL;
    event_loop_topology_fail_allocation_at = 0;
    topology_fixture_fail_capture = false;
    topology_fixture_cycle = 0;
    topology_fixture_space_count = 2;
    view_test_set_platform_ops(NULL);
    g_space_manager = saved_space;
    g_window_manager = saved_window;
    g_display_manager = saved_display;
    g_event_loop = saved_loop;
    TEST_CHECK(topology_fixture_destroy_count, destroy_count_before_commit + 4);
});
#undef CHECK_TOPOLOGY_FAILURE_UNCHANGED

TEST_FUNC(event_loop_topology_recovery_preserves_identity_through_sid_cycles, {
    struct space_manager saved_space = g_space_manager;
    struct window_manager saved_window = g_window_manager;
    struct display_manager saved_display = g_display_manager;
    struct event_loop saved_loop = g_event_loop;
    struct view_test_platform_ops platform = {
        .copy_space_uuid = topology_fixture_view_uuid,
        .space_is_user = topology_fixture_view_user,
        .feedback_destroy_observer = topology_fixture_feedback_destroy
    };
    view_test_set_platform_ops(&platform);
    event_loop_topology_capture_test = topology_fixture_capture;
    event_loop_topology_postcommit_hook = topology_fixture_postcommit;

    for (int cycle = 2; cycle <= 3; ++cycle) {
        topology_fixture_cycle = 0;
        topology_fixture_space_count = cycle;
        topology_fixture_spaces[0] = 10;
        topology_fixture_spaces[1] = 20;
        topology_fixture_spaces[2] = 30;
        g_space_manager = (struct space_manager) {
            .layout = VIEW_FLOAT, .current_space_id = 10, .last_space_id = 20
        };
        g_window_manager = (struct window_manager) { .purify_mode = PURIFY_DISABLED };
        g_display_manager = (struct display_manager) { .current_display_id = 7, .last_display_id = 7 };
        g_event_loop = (struct event_loop) {0};
        table_init(&g_space_manager.view, 23, hash_view, compare_view);
        table_init(&g_window_manager.managed_window, 23, hash_wm, compare_wm);
        table_init(&g_window_manager.window, 23, hash_wm, compare_wm);

        struct view *identity_a = topology_fixture_view(10);
        struct view *identity_b = topology_fixture_view(20);
        struct view *identity_c = cycle == 3 ? topology_fixture_view(30) : NULL;
        TEST_CHECK(identity_a && identity_b && (cycle == 2 || identity_c), true);
        identity_a->declarative_identity = topology_fixture_identity(10, "space-A", 0);
        if (identity_b) {
            if (identity_b->uuid) CFRelease(identity_b->uuid);
            identity_b->uuid = CFStringCreateCopy(NULL, CFSTR("space-B"));
            identity_b->declarative_identity = topology_fixture_identity(20, "space-B", 1);
        }
        if (identity_c) identity_c->declarative_identity = topology_fixture_identity(30, "space-C", 2);
        table_add(&g_space_manager.view, &identity_a->sid, identity_a);
        table_add(&g_space_manager.view, &identity_b->sid, identity_b);
        if (identity_c) table_add(&g_space_manager.view, &identity_c->sid, identity_c);
        space_manager_set_label_for_space(&g_space_manager, 10, strdup("label-A"));
        space_manager_set_label_for_space(&g_space_manager, 20, strdup("label-B"));
        if (cycle == 3) space_manager_set_label_for_space(&g_space_manager, 30, strdup("label-C"));
        display_manager_set_label_for_display(&g_display_manager, 7, strdup("display-identity"));

        topology_fixture_cycle = cycle;
        __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
        TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), true);
        TEST_CHECK(space_manager_lookup_view(&g_space_manager, 20), identity_a);
        TEST_CHECK(space_manager_lookup_view(&g_space_manager, cycle == 2 ? 10 : 30), identity_b);
        if (cycle == 3) TEST_CHECK(space_manager_lookup_view(&g_space_manager, 10), identity_c);
        struct space_label *label_a = space_manager_get_label_for_space(&g_space_manager, 20);
        struct space_label *label_b = space_manager_get_label_for_space(&g_space_manager, cycle == 2 ? 10 : 30);
        TEST_CHECK(label_a && strcmp(label_a->label, "label-A") == 0, true);
        TEST_CHECK(label_b && strcmp(label_b->label, "label-B") == 0, true);
        if (cycle == 3) {
            struct space_label *label_c = space_manager_get_label_for_space(&g_space_manager, 10);
            TEST_CHECK(label_c && strcmp(label_c->label, "label-C") == 0, true);
        }
        struct display_label *display_label = display_manager_get_label_for_display(&g_display_manager, 7);
        TEST_CHECK(display_label && strcmp(display_label->label, "display-identity") == 0, true);
        TEST_CHECK(g_display_manager.current_display_id, 7U);
        TEST_CHECK(g_display_manager.last_display_id, 7U);
        TEST_CHECK(g_space_manager.current_space_id, 20ULL);
        TEST_CHECK(g_space_manager.last_space_id, cycle == 2 ? 10ULL : 30ULL);

        table_for (struct view *view, g_space_manager.view, { view_destroy(view); legacy_allocation_free(view); })
        table_free(&g_space_manager.view);
        table_free(&g_window_manager.managed_window);
        table_free(&g_window_manager.window);
        while (buf_len(g_space_manager.labels)) space_manager_remove_label_for_space(&g_space_manager, g_space_manager.labels[0].sid);
        while (buf_len(g_display_manager.labels)) display_manager_remove_label_for_display(&g_display_manager, g_display_manager.labels[0].did);
        buf_free(g_space_manager.labels);
        buf_free(g_display_manager.labels);
    }

    topology_fixture_cycle = 0;
    topology_fixture_space_count = 2;
    topology_fixture_spaces[0] = 20;
    topology_fixture_spaces[1] = 30;
    event_loop_topology_postcommit_hook = NULL;
    event_loop_topology_capture_test = NULL;
    view_test_set_platform_ops(NULL);
    g_space_manager = saved_space;
    g_window_manager = saved_window;
    g_display_manager = saved_display;
    g_event_loop = saved_loop;
});

TEST_FUNC(event_loop_topology_double_capture_uuidless_stability_failures_and_ownership, {
    struct space_manager saved_space = g_space_manager;
    struct window_manager saved_window = g_window_manager;
    struct display_manager saved_display = g_display_manager;
    struct event_loop saved_loop = g_event_loop;
    struct view_test_platform_ops platform = {
        .copy_space_uuid = topology_fixture_view_uuid,
        .space_is_user = topology_fixture_view_user,
        .feedback_destroy_observer = topology_fixture_feedback_destroy
    };
    view_test_set_platform_ops(&platform);
    g_space_manager = (struct space_manager) { .layout = VIEW_FLOAT, .current_space_id = 101, .last_space_id = 109 };
    g_window_manager = (struct window_manager) { .purify_mode = PURIFY_DISABLED };
    g_display_manager = (struct display_manager) {
        .current_display_id = 7, .last_display_id = 7,
        .mode = EXTERNAL_BAR_ALL, .top_padding = 17, .bottom_padding = 9
    };
    g_event_loop = (struct event_loop) { .semaphore = (sem_t *) (uintptr_t) 1 };
    __atomic_store_n(&g_event_loop.is_running, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.generation, 42, __ATOMIC_RELEASE);
    table_init(&g_space_manager.view, 23, hash_view, compare_view);
    table_init(&g_window_manager.managed_window, 23, hash_wm, compare_wm);
    table_init(&g_window_manager.window, 23, hash_wm, compare_wm);
    topology_fixture_cycle = 0;
    topology_fixture_space_count = 9;
    for (int i = 0; i < 9; ++i) topology_fixture_spaces[i] = (uint64_t) (101 + i);
    struct view *views[9];
    for (int i = 0; i < 9; ++i) {
        views[i] = topology_fixture_view(topology_fixture_spaces[i]);
        TEST_CHECK(views[i] != NULL, true);
        views[i]->declarative_identity = (struct declarative_space_identity) {0};
        table_add(&g_space_manager.view, &views[i]->sid, views[i]);
    }
    uint32_t wid = 7001;
    table_add(&g_window_manager.managed_window, &wid, views[4]);
    space_manager_set_label_for_space(&g_space_manager, 105, strdup("middle"));
    display_manager_set_label_for_display(&g_display_manager, 7, strdup("only-display"));
    event_loop_topology_capture_test = topology_fixture_capture;
    event_loop_topology_postcommit_hook = topology_fixture_postcommit;
    event_loop_topology_retry_test_scheduler = topology_fixture_retry_scheduler;
    topology_fixture_postcommit_count = 0;
    topology_fixture_capture_result = DECLARATIVE_TOPOLOGY_OK;
    topology_fixture_capture_failure_call = 0;
    size_t outstanding_before = declarative_topology_test_outstanding_allocations();

    struct bucket **view_buckets = g_space_manager.view.buckets;
    struct bucket **managed_buckets = g_window_manager.managed_window.buckets;
    struct view view_before = *views[4];
    struct space_label space_label_before = g_space_manager.labels[0];
    struct display_label display_label_before = g_display_manager.labels[0];
    for (int mismatch = 1; mismatch <= 3; ++mismatch) {
        topology_fixture_capture_count = 0;
        topology_fixture_capture_mismatch = mismatch;
        topology_fixture_retry_schedules = 0;
        __atomic_store_n(&g_event_loop.topology_resync_retry_scheduled, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
        TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), false);
        event_loop_reserve_topology_resync(&g_event_loop);
        TEST_CHECK(topology_fixture_capture_count, 2);
        TEST_CHECK(topology_fixture_retry_schedules, 1);
        TEST_CHECK(g_space_manager.view.buckets, view_buckets);
        TEST_CHECK(g_window_manager.managed_window.buckets, managed_buckets);
        TEST_CHECK(memcmp(views[4], &view_before, sizeof(view_before)), 0);
        TEST_CHECK(memcmp(&g_space_manager.labels[0], &space_label_before, sizeof(space_label_before)), 0);
        TEST_CHECK(memcmp(&g_display_manager.labels[0], &display_label_before, sizeof(display_label_before)), 0);
        TEST_CHECK(g_space_manager.current_space_id, 101ULL);
        TEST_CHECK(g_space_manager.last_space_id, 109ULL);
        TEST_CHECK(g_display_manager.current_display_id, 7U);
        TEST_CHECK(g_display_manager.last_display_id, 7U);
        TEST_CHECK(declarative_topology_test_outstanding_allocations(), outstanding_before);
    }

    const enum declarative_topology_result failures[] = {
        DECLARATIVE_TOPOLOGY_RESOURCE, DECLARATIVE_TOPOLOGY_SETTLING
    };
    topology_fixture_capture_mismatch = 0;
    for (size_t i = 0; i < array_count(failures); ++i) {
        topology_fixture_capture_result = failures[i];
        topology_fixture_capture_failure_call = 2;
        topology_fixture_capture_count = topology_fixture_retry_schedules = 0;
        __atomic_store_n(&g_event_loop.topology_resync_retry_scheduled, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
        TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), false);
        TEST_CHECK(topology_fixture_capture_count, 2);
        TEST_CHECK(topology_fixture_retry_schedules, 1);
        TEST_CHECK(g_space_manager.view.buckets, view_buckets);
        TEST_CHECK(declarative_topology_test_outstanding_allocations(), outstanding_before);
    }

    topology_fixture_capture_result = DECLARATIVE_TOPOLOGY_OK;
    topology_fixture_capture_failure_call = 0;
    topology_fixture_capture_count = topology_fixture_retry_schedules = 0;
    topology_fixture_capture_mismatch = 0;
    __atomic_store_n(&g_event_loop.topology_resync_retry_scheduled, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_event_loop.topology_resync_pending, 1, __ATOMIC_RELEASE);
    TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), true);
    TEST_CHECK(topology_fixture_capture_count, 2);
    TEST_CHECK(topology_fixture_retry_schedules, 0);
    TEST_CHECK(g_space_manager.view.count, 9);
    TEST_CHECK(table_find(&g_window_manager.managed_window, &wid), views[4]);
    struct space_label *label = space_manager_get_label_for_space(&g_space_manager, 105);
    TEST_CHECK(label && strcmp(label->label, "middle") == 0, true);
    TEST_CHECK(display_manager_get_label_for_display(&g_display_manager, 7) != NULL, true);
    for (int i = 0; i < 9; ++i) {
        TEST_CHECK(space_manager_lookup_view(&g_space_manager, topology_fixture_spaces[i]), views[i]);
        TEST_CHECK(declarative_space_identity_valid(&views[i]->declarative_identity), true);
        TEST_CHECK(views[i]->declarative_identity.has_uuid, false);
    }
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), outstanding_before);

    table_for (struct view *view, g_space_manager.view, { view_destroy(view); legacy_allocation_free(view); })
    table_free(&g_space_manager.view);
    table_free(&g_window_manager.managed_window);
    table_free(&g_window_manager.window);
    while (buf_len(g_space_manager.labels)) space_manager_remove_label_for_space(&g_space_manager, g_space_manager.labels[0].sid);
    while (buf_len(g_display_manager.labels)) display_manager_remove_label_for_display(&g_display_manager, g_display_manager.labels[0].did);
    buf_free(g_space_manager.labels);
    buf_free(g_display_manager.labels);
    event_loop_topology_capture_test = NULL;
    event_loop_topology_postcommit_hook = NULL;
    event_loop_topology_retry_test_scheduler = NULL;
    topology_fixture_capture_result = DECLARATIVE_TOPOLOGY_OK;
    topology_fixture_capture_failure_call = 0;
    topology_fixture_capture_mismatch = 0;
    topology_fixture_capture_count = 0;
    topology_fixture_space_count = 2;
    topology_fixture_spaces[0] = 20;
    topology_fixture_spaces[1] = 30;
    view_test_set_platform_ops(NULL);
    g_space_manager = saved_space;
    g_window_manager = saved_window;
    g_display_manager = saved_display;
    g_event_loop = saved_loop;
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), outstanding_before);
});

TEST_FUNC(event_loop_accepted_space_creation_failure_reserves_and_recovers_topology, {
    struct space_manager saved_space = g_space_manager;
    struct window_manager saved_window = g_window_manager;
    struct display_manager saved_display = g_display_manager;
    struct event_loop saved_loop = g_event_loop;
    struct view_test_platform_ops platform = {
        .copy_space_uuid = topology_fixture_view_uuid,
        .space_is_user = topology_fixture_view_user,
        .feedback_destroy_observer = topology_fixture_feedback_destroy
    };
    view_test_set_platform_ops(&platform);
    g_space_manager = (struct space_manager) { .layout = VIEW_FLOAT };
    g_window_manager = (struct window_manager) { .purify_mode = PURIFY_DISABLED };
    g_display_manager = (struct display_manager) { .current_display_id = 7, .last_display_id = 7 };
    g_event_loop = (struct event_loop) {0};
    table_init(&g_space_manager.view, 23, hash_view, compare_view);
    table_init(&g_window_manager.managed_window, 23, hash_wm, compare_wm);
    table_init(&g_window_manager.window, 23, hash_wm, compare_wm);
    event_loop_test_space_type = topology_fixture_space_type;

    table_test_fail_allocation_after(0);
    EVENT_HANDLER_SLS_SPACE_CREATED((void *)(uintptr_t) 20, 0);
    table_test_fail_allocation_after(-1);
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 20), NULL);
    TEST_CHECK(g_event_loop.topology_resync_pending, 1U);

    topology_fixture_cycle = 0;
    topology_fixture_space_count = 2;
    topology_fixture_spaces[0] = 20;
    topology_fixture_spaces[1] = 30;
    event_loop_topology_capture_test = topology_fixture_capture;
    event_loop_topology_postcommit_hook = topology_fixture_postcommit;
    TEST_CHECK(event_loop_consume_topology_resync(&g_event_loop), true);
    TEST_CHECK(space_manager_lookup_view(&g_space_manager, 20) != NULL, true);
    TEST_CHECK(g_space_manager.current_space_id, 20ULL);
    TEST_CHECK(g_display_manager.current_display_id, 7U);

    table_for (struct view *view, g_space_manager.view, { view_destroy(view); legacy_allocation_free(view); })
    table_free(&g_space_manager.view);
    table_free(&g_window_manager.managed_window);
    table_free(&g_window_manager.window);
    event_loop_test_space_type = NULL;
    event_loop_topology_postcommit_hook = NULL;
    event_loop_topology_capture_test = NULL;
    view_test_set_platform_ops(NULL);
    g_space_manager = saved_space;
    g_window_manager = saved_window;
    g_display_manager = saved_display;
    g_event_loop = saved_loop;
});

TEST_FUNC(event_queue_grows_past_512k_fifo_and_release_once, {
    result = event_queue_check_grows_past_512k_fifo_and_release_once(test_name);
});

TEST_FUNC(event_queue_wrapped_growth_preserves_fifo_and_release_once, {
    result = event_queue_check_wrapped_growth_preserves_fifo_and_release_once(test_name);
});

TEST_FUNC(event_queue_merge_releases_replaced_mouse_move_once, {
    result = event_queue_check_merge_releases_replaced_mouse_move_once(test_name);
});

TEST_FUNC(event_queue_adjacent_mouse_drags_coalesce_and_release_replaced_context, {
    result = event_queue_check_adjacent_mouse_drags_coalesce_and_release_replaced_context(test_name);
});

TEST_FUNC(event_queue_different_mouse_button_drags_preserve_fifo, {
    result = event_queue_check_different_mouse_button_drags_preserve_fifo(test_name);
});

TEST_FUNC(event_queue_mouse_up_boundary_preserves_drag_fifo, {
    result = event_queue_check_mouse_up_boundary_preserves_drag_fifo(test_name);
});

TEST_FUNC(event_queue_full_releases_owned_context_once, {
    result = event_queue_check_full_releases_owned_context_once(test_name);
});

TEST_FUNC(event_queue_full_closes_daemon_message_fd_once, {
    result = event_queue_check_full_closes_daemon_message_fd_once(test_name);
});

TEST_FUNC(event_queue_full_failstops_destructive_events, {
    result = event_queue_check_full_failstops_destructive_events(test_name);
});

TEST_FUNC(daemon_message_admission_bounds_and_reuses_slots, {
    struct daemon_message_admission admission = { 0 };

    daemon_message_admission_release(&admission);
    TEST_CHECK(daemon_message_admission_count(&admission), 0);

    for (uint32_t i = 0; i < DAEMON_MESSAGE_ADMISSION_MAX; ++i) {
        TEST_CHECK(daemon_message_admission_acquire(&admission), true);
    }
    TEST_CHECK(daemon_message_admission_count(&admission), DAEMON_MESSAGE_ADMISSION_MAX);
    TEST_CHECK(daemon_message_admission_acquire(&admission), false);

    daemon_message_admission_release(&admission);
    TEST_CHECK(daemon_message_admission_count(&admission), DAEMON_MESSAGE_ADMISSION_MAX - 1);
    TEST_CHECK(daemon_message_admission_acquire(&admission), true);
    TEST_CHECK(daemon_message_admission_count(&admission), DAEMON_MESSAGE_ADMISSION_MAX);
});
