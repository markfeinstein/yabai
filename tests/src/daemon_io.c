static bool daemon_io_test_sync(void *context)
{
    __atomic_store_n((bool *) context, true, __ATOMIC_RELEASE);
    return true;
}

static bool daemon_io_test_wait_for_admission(uint32_t expected, uint32_t timeout_ms)
{
    for (uint32_t elapsed = 0; elapsed < timeout_ms; ++elapsed) {
        if (daemon_message_admission_count(&g_daemon_message_admission) == expected) return true;
        usleep(1000);
    }
    return false;
}

static pthread_t daemon_io_test_handler_thread;
static bool daemon_io_test_request_valid;
static size_t daemon_io_test_response_size;

static void daemon_io_test_handler(FILE *response, char *request, size_t length)
{
    daemon_io_test_handler_thread = pthread_self();
    __atomic_store_n(&daemon_io_test_request_valid,
                     length == 3 && request[0] == 'x' && request[1] == '\0' && request[2] == '\0',
                     __ATOMIC_RELEASE);
    char block[4096];
    memset(block, 'r', sizeof(block));
    for (size_t written = 0; written < daemon_io_test_response_size; written += sizeof(block)) {
        size_t remaining = daemon_io_test_response_size - written;
        fwrite(block, 1, remaining < sizeof(block) ? remaining : sizeof(block), response);
    }
}

static bool daemon_io_test_start(char *test_name, struct event_loop *saved_event_loop,
                                 struct daemon_message_admission *saved_admission)
{
    bool result = true;
    *saved_event_loop = g_event_loop;
    *saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    TEST_CHECK(daemon_io_begin(event_loop_generation_snapshot(&g_event_loop)), true);
    TEST_CHECK(daemon_io_test_reader_thread_count(), (uint32_t) DAEMON_IO_READER_COUNT);
    TEST_CHECK(daemon_io_test_writer_thread_count(), (uint32_t) DAEMON_IO_WRITER_COUNT);
    return result;
}

static void daemon_io_test_stop(struct event_loop saved_event_loop,
                                struct daemon_message_admission saved_admission)
{
    daemon_io_handle_message_test_hook = NULL;
    (void) daemon_io_end();
    event_loop_end(&g_event_loop);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
}

TEST_FUNC(daemon_io_partial_worker_initialization_rolls_back,
{
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    daemon_io_thread_create_attempt = 0;
    daemon_io_thread_create_fail_after = 2;
    TEST_CHECK(daemon_io_begin(1), false);
    TEST_CHECK(daemon_io_test_reader_thread_count(), 0U);
    TEST_CHECK(daemon_io_test_writer_thread_count(), 0U);
    TEST_CHECK(g_daemon_io.readers.mutex_initialized, false);
    TEST_CHECK(g_daemon_io.writers.mutex_initialized, false);
    daemon_io_thread_create_fail_after = -1;
    daemon_io_thread_create_attempt = 0;
    g_daemon_message_admission = saved_admission;
});

TEST_FUNC(daemon_io_partial_body_never_blocks_event_worker,
{
    struct event_loop saved_event_loop;
    struct daemon_message_admission saved_admission;
    if (!daemon_io_test_start(test_name, &saved_event_loop, &saved_admission)) return false;

    int pair[2] = { -1, -1 };
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    if (pair[0] >= 0) {
        TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
        TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
        int length = 64;
        TEST_CHECK(write(pair[1], &length, sizeof(length)), (ssize_t) sizeof(length));

        bool sentinel = false;
        uint64_t begin = socket_io_monotonic_ns();
        TEST_CHECK(event_loop_perform_sync(&g_event_loop, daemon_io_test_sync, &sentinel), true);
        TEST_CHECK(__atomic_load_n(&sentinel, __ATOMIC_ACQUIRE), true);
        TEST_CHECK((socket_io_monotonic_ns() - begin) / 1000000ULL < 250, true);
        TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 1U);

        close(pair[1]);
        pair[1] = -1;
        TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    }
    if (pair[1] >= 0) close(pair[1]);
    daemon_io_test_stop(saved_event_loop, saved_admission);
});

TEST_FUNC(daemon_io_nonreading_peer_never_blocks_event_worker_and_handler_is_event_thread,
{
    struct event_loop saved_event_loop;
    struct daemon_message_admission saved_admission;
    if (!daemon_io_test_start(test_name, &saved_event_loop, &saved_admission)) return false;

    int pair[2] = { -1, -1 };
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    if (pair[0] >= 0) {
        int send_buffer = 1024;
        setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
        daemon_io_test_handler_thread = (pthread_t) {0};
        daemon_io_test_request_valid = false;
        daemon_io_test_response_size = 4U * 1024U * 1024U;
        daemon_io_handle_message_test_hook = daemon_io_test_handler;

        TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
        TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
        char frame[sizeof(int) + 3];
        int length = 3;
        memcpy(frame, &length, sizeof(length));
        frame[sizeof(int)] = 'x';
        frame[sizeof(int) + 1] = '\0';
        frame[sizeof(int) + 2] = '\0';
        TEST_CHECK(write(pair[1], frame, sizeof(frame)), (ssize_t) sizeof(frame));

        for (uint32_t elapsed = 0;
             elapsed < 1000 && !__atomic_load_n(&daemon_io_test_request_valid, __ATOMIC_ACQUIRE);
             ++elapsed) usleep(1000);
        TEST_CHECK(__atomic_load_n(&daemon_io_test_request_valid, __ATOMIC_ACQUIRE), true);

        bool sentinel = false;
        TEST_CHECK(event_loop_perform_sync(&g_event_loop, daemon_io_test_sync, &sentinel), true);
        TEST_CHECK(sentinel, true);
        TEST_CHECK(pthread_equal(daemon_io_test_handler_thread, g_event_loop.thread), 1);
        TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 1U);

        close(pair[1]);
        pair[1] = -1;
        TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    }
    if (pair[1] >= 0) close(pair[1]);
    daemon_io_test_stop(saved_event_loop, saved_admission);
});

TEST_FUNC(daemon_io_invalid_frame_is_rejected_before_handler,
{
    struct event_loop saved_event_loop;
    struct daemon_message_admission saved_admission;
    if (!daemon_io_test_start(test_name, &saved_event_loop, &saved_admission)) return false;

    int pair[2] = { -1, -1 };
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    if (pair[0] >= 0) {
        daemon_io_test_request_valid = false;
        daemon_io_handle_message_test_hook = daemon_io_test_handler;
        TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
        TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
        int invalid = 1;
        TEST_CHECK(write(pair[1], &invalid, sizeof(invalid)), (ssize_t) sizeof(invalid));
        char response[64];
        TEST_CHECK(read(pair[1], response, sizeof(response)) > 0, true);
        TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
        TEST_CHECK(__atomic_load_n(&daemon_io_test_request_valid, __ATOMIC_ACQUIRE), false);
    }
    close(pair[1]);
    daemon_io_test_stop(saved_event_loop, saved_admission);
});
