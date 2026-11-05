static void daemon_io_test_close_writer_in_handler(FILE *response, char *request, size_t length)
{
    (void) response; (void) request; (void) length;
    daemon_io_queue_close(&g_daemon_io.writers);
}

TEST_FUNC(daemon_io_allocation_reserve_and_submit_failures_finalize_cleanly,
{
    struct event_loop saved_event_loop;
    struct daemon_message_admission saved_admission;
    if (!daemon_io_test_start(test_name, &saved_event_loop, &saved_admission)) return false;

    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    daemon_io_fail_job_allocation = true;
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), false);
    socket_close(pair[0]);
    daemon_message_admission_release(&g_daemon_message_admission);
    close(pair[1]);
    daemon_io_fail_job_allocation = false;
    TEST_CHECK(daemon_io_test_active_jobs(), 0U);

    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    daemon_io_fail_request_allocation = true;
    daemon_io_test_request_valid = false;
    daemon_io_handle_message_test_hook = daemon_io_test_handler;
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    int length = 3;
    char body[] = {'x', '\0', '\0'};
    write(pair[1], &length, sizeof(length));
    write(pair[1], body, sizeof(body));
    char response[64];
    TEST_CHECK(read(pair[1], response, sizeof(response)) > 1, true);
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_request_valid, __ATOMIC_ACQUIRE), false);
    close(pair[1]);
    daemon_io_fail_request_allocation = false;

    /* Closing the writer queue forces reserve failure before mutation. */
    daemon_io_queue_close(&g_daemon_io.writers);
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    write(pair[1], &length, sizeof(length));
    write(pair[1], body, sizeof(body));
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    close(pair[1]);
    daemon_io_handle_message_test_hook = NULL;
    daemon_io_test_stop(saved_event_loop, saved_admission);
});

TEST_FUNC(daemon_io_writer_submit_failure_finalizes_static_fallback_once,
{
    struct event_loop saved_event_loop;
    struct daemon_message_admission saved_admission;
    if (!daemon_io_test_start(test_name, &saved_event_loop, &saved_admission)) return false;
    daemon_io_test_reset_counters();
    daemon_io_fail_response_open = true;
    daemon_io_handle_message_test_hook = daemon_io_test_close_writer_in_handler;
    /* Open failure skips the hook, so use finalize failure to reach it. */
    daemon_io_fail_response_open = false;
    daemon_io_fail_response_finalize = true;
    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    int length = 3;
    char body[] = {'x', '\0', '\0'};
    write(pair[1], &length, sizeof(length));
    write(pair[1], body, sizeof(body));
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_finalizer_count, __ATOMIC_RELAXED), 1U);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_close_count, __ATOMIC_RELAXED), 1U);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_admission_release_count, __ATOMIC_RELAXED), 1U);
    close(pair[1]);
    daemon_io_fail_response_finalize = false;
    daemon_io_handle_message_test_hook = NULL;
    daemon_io_test_stop(saved_event_loop, saved_admission);
});
