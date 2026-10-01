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
    free(event_loop.queue.events);
    pthread_mutex_destroy(&event_loop.queue.lock);
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
    free(event_loop.queue.events);
    pthread_mutex_destroy(&event_loop.queue.lock);
    return result;
}

static void event_queue_child_rejects_destructive_event(enum event_type type)
{
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;
    struct event first;
    struct event_loop event_loop = { 0 };

    if (!event_queue_init(&event_loop.queue, 1)) _exit(2);

    first.type = WINDOW_MOVED;
    first.param1 = 1;
    first.context = (void *)(intptr_t) 1;
    if (event_queue_push(&event_loop.queue, first, false, &replaced, &capacity, &post_wake) != EVENT_QUEUE_ADDED) _exit(3);

    g_event_queue_test_fail_allocation = true;
    event_loop_post(&event_loop, type, (void *)(intptr_t) 1, 0);
    _exit(EXIT_SUCCESS);
}

static bool event_queue_check_full_failstops_destructive_events(char *test_name)
{
    bool result = true;
    enum event_type events[] = { APPLICATION_TERMINATED, WINDOW_DESTROYED };

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

TEST_FUNC(event_queue_grows_past_512k_fifo_and_release_once, {
    result = event_queue_check_grows_past_512k_fifo_and_release_once(test_name);
});

TEST_FUNC(event_queue_wrapped_growth_preserves_fifo_and_release_once, {
    result = event_queue_check_wrapped_growth_preserves_fifo_and_release_once(test_name);
});

TEST_FUNC(event_queue_merge_releases_replaced_mouse_move_once, {
    result = event_queue_check_merge_releases_replaced_mouse_move_once(test_name);
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
