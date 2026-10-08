TEST_FUNC(message_loop_admission_caps_at_32_and_excess_gets_overload_wire,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    char directory[] = "/tmp/yabai-message-capacity.XXXXXX";
    char *root = mkdtemp(directory);
    TEST_CHECK(root != NULL, true);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/socket", root);
    TEST_CHECK(message_loop_begin(path), true);
    int clients[DAEMON_MESSAGE_ADMISSION_MAX + 1];
    for (uint32_t i = 0; i < DAEMON_MESSAGE_ADMISSION_MAX + 1; ++i) clients[i] = -1;
    struct sockaddr_un address;
    TEST_CHECK(socket_address_init(&address, path), true);
    for (uint32_t i = 0; i < DAEMON_MESSAGE_ADMISSION_MAX; ++i) {
        clients[i] = socket(AF_UNIX, SOCK_STREAM, 0);
        TEST_CHECK(clients[i] >= 0, true);
        TEST_CHECK(connect(clients[i], (struct sockaddr *) &address, sizeof(address)), 0);
    }
    TEST_CHECK(daemon_io_test_wait_for_admission(DAEMON_MESSAGE_ADMISSION_MAX, 1000), true);
    TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission) <= DAEMON_MESSAGE_ADMISSION_MAX, true);
    clients[DAEMON_MESSAGE_ADMISSION_MAX] = socket(AF_UNIX, SOCK_STREAM, 0);
    TEST_CHECK(connect(clients[DAEMON_MESSAGE_ADMISSION_MAX], (struct sockaddr *) &address, sizeof(address)), 0);
    char response[128] = {0};
    size_t count = 0;
    bool failed = false;
    TEST_CHECK(daemon_test_read_response(clients[DAEMON_MESSAGE_ADMISSION_MAX], response,
                                         sizeof(response), &count, &failed), true);
    TEST_CHECK(failed, true);
    TEST_CHECK(count > 1, true);
    TEST_CHECK(response[0], DAEMON_MESSAGE_FAILURE_MARKER);
    TEST_CHECK(strstr(response + 1, "too many pending messages") != NULL, true);
    for (uint32_t i = 0; i < DAEMON_MESSAGE_ADMISSION_MAX + 1; ++i) if (clients[i] >= 0) close(clients[i]);
    TEST_CHECK(message_loop_end(), true);
    TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 0U);
    rmdir(root);
    event_loop_end(&g_event_loop);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
});
