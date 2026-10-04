
static void daemon_io_test_reset_counters(void)
{
    __atomic_store_n(&daemon_io_test_finalizer_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&daemon_io_test_request_free_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&daemon_io_test_response_destroy_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&daemon_io_test_close_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&daemon_io_test_admission_release_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&daemon_io_test_reservation_cancel_count, 0, __ATOMIC_RELAXED);
}

TEST_FUNC(daemon_io_post_rejection_consumes_owned_job_once,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    daemon_io_test_reset_counters();
    TEST_CHECK(daemon_io_begin(77), true);
    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    int length = 3;
    char body[] = {'x', '\0', '\0'};
    TEST_CHECK(write(pair[1], &length, sizeof(length)), (ssize_t) sizeof(length));
    TEST_CHECK(write(pair[1], body, sizeof(body)), (ssize_t) sizeof(body));
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    TEST_CHECK(daemon_io_test_active_jobs(), 0U);
    TEST_CHECK(daemon_io_test_writer_reservations(), 0U);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_finalizer_count, __ATOMIC_RELAXED), 1U);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_request_free_count, __ATOMIC_RELAXED), 1U);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_close_count, __ATOMIC_RELAXED), 1U);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_admission_release_count, __ATOMIC_RELAXED), 1U);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_reservation_cancel_count, __ATOMIC_RELAXED), 1U);
    char byte;
    TEST_CHECK(read(pair[1], &byte, 1), 0);
    close(pair[1]);
    TEST_CHECK(daemon_io_end(), true);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
});

TEST_FUNC(daemon_io_all_startup_failures_are_idempotent_and_restartable,
{
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    for (int failure = 0; failure < 4; ++failure) {
        daemon_io_queue_init_fail_after = failure;
        daemon_io_queue_init_attempt = 0;
        TEST_CHECK(daemon_io_begin(1), false);
        TEST_CHECK(daemon_io_end(), true);
        TEST_CHECK(daemon_io_end(), true);
    }
    daemon_io_queue_init_fail_after = -1;
    for (int failure = 0; failure < DAEMON_IO_READER_COUNT + DAEMON_IO_WRITER_COUNT; ++failure) {
        daemon_io_thread_create_fail_after = failure;
        daemon_io_thread_create_attempt = 0;
        TEST_CHECK(daemon_io_begin(1), false);
        TEST_CHECK(daemon_io_end(), true);
    }
    daemon_io_thread_create_fail_after = -1;
    TEST_CHECK(daemon_io_begin(1), true);
    TEST_CHECK(daemon_io_end(), true);
    g_daemon_message_admission = saved_admission;
});

static bool daemon_io_test_end_from_handler_result;
static void daemon_io_test_end_from_handler(FILE *response, char *request, size_t length)
{
    (void) response; (void) request; (void) length;
    daemon_io_test_end_from_handler_result = daemon_io_end();
}

static bool daemon_io_test_end_from_reader_result;
static bool daemon_io_test_end_from_writer_result;
static void daemon_io_test_end_from_reader(void)
{
    daemon_io_test_end_from_reader_result = daemon_io_end();
}
static void daemon_io_test_end_from_writer(void)
{
    daemon_io_test_end_from_writer_result = daemon_io_end();
}

TEST_FUNC(daemon_io_rejects_shutdown_from_event_and_transport_workers,
{
    struct event_loop saved_event_loop;
    struct daemon_message_admission saved_admission;
    if (!daemon_io_test_start(test_name, &saved_event_loop, &saved_admission)) return false;
    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    daemon_io_test_end_from_reader_result = true;
    daemon_io_test_end_from_handler_result = true;
    daemon_io_test_end_from_writer_result = true;
    daemon_io_reader_worker_test_hook = daemon_io_test_end_from_reader;
    daemon_io_writer_worker_test_hook = daemon_io_test_end_from_writer;
    daemon_io_handle_message_test_hook = daemon_io_test_end_from_handler;
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    int length = 3;
    char body[] = {'x', '\0', '\0'};
    write(pair[1], &length, sizeof(length));
    write(pair[1], body, sizeof(body));
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    TEST_CHECK(daemon_io_test_end_from_reader_result, false);
    TEST_CHECK(daemon_io_test_end_from_handler_result, false);
    TEST_CHECK(daemon_io_test_end_from_writer_result, false);
    daemon_io_reader_worker_test_hook = NULL;
    daemon_io_writer_worker_test_hook = NULL;
    close(pair[1]);
    daemon_io_test_stop(saved_event_loop, saved_admission);
});

TEST_FUNC(daemon_io_open_peer_read_and_writer_stalls_observe_injected_deadlines,
{
    struct event_loop saved_event_loop;
    struct daemon_message_admission saved_admission;
    if (!daemon_io_test_start(test_name, &saved_event_loop, &saved_admission)) return false;
    daemon_io_read_timeout_ms = 20;
    daemon_io_write_timeout_ms = 20;
    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    int length = 64;
    write(pair[1], &length, sizeof(length));
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 500), true);
    close(pair[1]);

    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    int send_buffer = 1024;
    setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
    daemon_io_test_response_size = 4U * 1024U * 1024U;
    daemon_io_handle_message_test_hook = daemon_io_test_handler;
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    length = 3;
    char body[] = {'x', '\0', '\0'};
    write(pair[1], &length, sizeof(length));
    write(pair[1], body, sizeof(body));
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    close(pair[1]);
    daemon_io_read_timeout_ms = DAEMON_IO_READ_TIMEOUT_MS;
    daemon_io_write_timeout_ms = DAEMON_IO_WRITE_TIMEOUT_MS;
    daemon_io_test_stop(saved_event_loop, saved_admission);
});

TEST_FUNC(daemon_io_response_failures_skip_or_preserve_handler_and_static_fallback,
{
    struct event_loop saved_event_loop;
    struct daemon_message_admission saved_admission;
    if (!daemon_io_test_start(test_name, &saved_event_loop, &saved_admission)) return false;
    for (int failure = 0; failure < 3; ++failure) {
        int pair[2] = {-1, -1};
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
        daemon_io_test_request_valid = false;
        daemon_io_test_response_size = 0;
        daemon_io_handle_message_test_hook = daemon_io_test_handler;
        g_daemon_response_test_fail_allocate = failure == 0;
        g_daemon_response_test_fail_fmemopen = failure == 1;
        daemon_io_fail_response_finalize = failure == 2;
        TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
        TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
        int length = 3;
        char body[] = {'x', '\0', '\0'};
        write(pair[1], &length, sizeof(length));
        write(pair[1], body, sizeof(body));
        char response[64] = {0};
        ssize_t count = read(pair[1], response, sizeof(response));
        TEST_CHECK(count > 1, true);
        TEST_CHECK(response[0], DAEMON_MESSAGE_FAILURE_MARKER);
        TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
        TEST_CHECK(__atomic_load_n(&daemon_io_test_request_valid, __ATOMIC_ACQUIRE), failure == 2);
        close(pair[1]);
    }
    g_daemon_response_test_fail_allocate = false;
    g_daemon_response_test_fail_fmemopen = false;
    daemon_io_fail_response_finalize = false;
    daemon_io_test_stop(saved_event_loop, saved_admission);
});
