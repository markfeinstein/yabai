static int message_loop_accept_test_errors[] = { EINTR, ECONNABORTED, EMFILE, ENFILE, EBADF };
static uint32_t message_loop_accept_test_index;
static uint32_t message_loop_accept_test_backoffs;

static int message_loop_accept_test_script(int listener, struct sockaddr *address, socklen_t *length)
{
    (void) listener; (void) address; (void) length;
    uint32_t index = __atomic_fetch_add(&message_loop_accept_test_index, 1, __ATOMIC_RELAXED);
    if (index >= array_count(message_loop_accept_test_errors)) index = array_count(message_loop_accept_test_errors) - 1;
    errno = message_loop_accept_test_errors[index];
    return -1;
}

static int message_loop_accept_test_backoff(const struct timespec *delay, struct timespec *remaining)
{
    (void) delay; (void) remaining;
    __atomic_add_fetch(&message_loop_accept_test_backoffs, 1, __ATOMIC_RELAXED);
    return 0;
}

TEST_FUNC(message_loop_accept_errors_retry_backoff_fail_and_recover,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    char directory[] = "/tmp/yabai-message-accept.XXXXXX";
    char *root = mkdtemp(directory);
    TEST_CHECK(root != NULL, true);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/socket", root);
    message_loop_accept_test_index = 0;
    message_loop_accept_test_backoffs = 0;
    message_loop_accept_call = message_loop_accept_test_script;
    message_loop_backoff_call = message_loop_accept_test_backoff;
    TEST_CHECK(message_loop_begin(path), true);
    for (uint32_t elapsed = 0;
         elapsed < 1000 && __atomic_load_n(&g_message_loop.state, __ATOMIC_ACQUIRE) != MESSAGE_LOOP_FAILED;
         ++elapsed) usleep(1000);
    TEST_CHECK(__atomic_load_n(&g_message_loop.state, __ATOMIC_ACQUIRE), MESSAGE_LOOP_FAILED);
    TEST_CHECK(__atomic_load_n(&message_loop_accept_test_index, __ATOMIC_RELAXED), 5U);
    TEST_CHECK(__atomic_load_n(&message_loop_accept_test_backoffs, __ATOMIC_RELAXED), 2U);
    TEST_CHECK(message_loop_end(), true);
    message_loop_accept_call = accept;
    message_loop_backoff_call = nanosleep;
    TEST_CHECK(message_loop_begin(path), true);
    TEST_CHECK(message_loop_end(), true);
    TEST_CHECK(access(path, F_OK), -1);
    rmdir(root);
    event_loop_end(&g_event_loop);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
});
