static pthread_mutex_t daemon_io_generation_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t daemon_io_generation_gate_cond = PTHREAD_COND_INITIALIZER;
static bool daemon_io_generation_gate_entered;
static bool daemon_io_generation_gate_release;

static void daemon_io_generation_reader_gate(void)
{
    pthread_mutex_lock(&daemon_io_generation_gate_mutex);
    daemon_io_generation_gate_entered = true;
    pthread_cond_broadcast(&daemon_io_generation_gate_cond);
    while (!daemon_io_generation_gate_release) pthread_cond_wait(&daemon_io_generation_gate_cond, &daemon_io_generation_gate_mutex);
    pthread_mutex_unlock(&daemon_io_generation_gate_mutex);
}

TEST_FUNC(daemon_io_stale_generation_is_discarded_after_event_loop_restart,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    uint64_t first_generation = event_loop_generation_snapshot(&g_event_loop);
    TEST_CHECK(daemon_io_begin(first_generation), true);
    daemon_io_generation_gate_entered = false;
    daemon_io_generation_gate_release = false;
    daemon_io_reader_worker_test_hook = daemon_io_generation_reader_gate;
    daemon_io_test_request_valid = false;
    daemon_io_handle_message_test_hook = daemon_io_test_handler;
    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    int length = 3;
    char body[] = {'x', '\0', '\0'};
    write(pair[1], &length, sizeof(length));
    write(pair[1], body, sizeof(body));
    pthread_mutex_lock(&daemon_io_generation_gate_mutex);
    while (!daemon_io_generation_gate_entered) pthread_cond_wait(&daemon_io_generation_gate_cond, &daemon_io_generation_gate_mutex);
    pthread_mutex_unlock(&daemon_io_generation_gate_mutex);
    event_loop_end(&g_event_loop);
    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    TEST_CHECK(event_loop_generation_snapshot(&g_event_loop) != first_generation, true);
    pthread_mutex_lock(&daemon_io_generation_gate_mutex);
    daemon_io_generation_gate_release = true;
    pthread_cond_broadcast(&daemon_io_generation_gate_cond);
    pthread_mutex_unlock(&daemon_io_generation_gate_mutex);
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    TEST_CHECK(__atomic_load_n(&daemon_io_test_request_valid, __ATOMIC_ACQUIRE), false);
    daemon_io_reader_worker_test_hook = NULL;
    daemon_io_handle_message_test_hook = NULL;
    close(pair[1]);
    TEST_CHECK(daemon_io_end(), true);
    event_loop_end(&g_event_loop);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
});

TEST_FUNC(daemon_io_queue_growth_rejection_finalizes_once,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 1), true);
    TEST_CHECK(pthread_mutex_init(&g_event_loop.producer_lock, NULL), 0);
    TEST_CHECK(pthread_cond_init(&g_event_loop.producer_cond, NULL), 0);
    g_event_loop.producer_sync_initialized = true;
    g_event_loop.generation = 91;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    struct event replaced = {0};
    uint32_t capacity = 0;
    bool wake = false;
    struct event blocker = {.type = WINDOW_MOVED};
    TEST_CHECK(event_queue_push(&g_event_loop.queue, blocker, false, &replaced, &capacity, &wake), EVENT_QUEUE_ADDED);
    g_event_queue_test_fail_allocation = true;
    daemon_io_test_reset_counters();
    TEST_CHECK(daemon_io_begin(91), true);
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
    TEST_CHECK(__atomic_load_n(&daemon_io_test_reservation_cancel_count, __ATOMIC_RELAXED), 1U);
    close(pair[1]);
    TEST_CHECK(daemon_io_end(), true);
    g_event_queue_test_fail_allocation = false;
    event_loop_cleanup(&g_event_loop);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
});

static uint32_t daemon_io_test_event_queue_count(struct event_loop *event_loop)
{
    pthread_mutex_lock(&event_loop->queue.lock);
    uint32_t count = event_loop->queue.count;
    pthread_mutex_unlock(&event_loop->queue.lock);
    return count;
}

TEST_FUNC(daemon_io_shutdown_times_out_without_event_progress_then_recovers_after_drain,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 1), true);
    TEST_CHECK(pthread_mutex_init(&g_event_loop.producer_lock, NULL), 0);
    TEST_CHECK(pthread_cond_init(&g_event_loop.producer_cond, NULL), 0);
    g_event_loop.producer_sync_initialized = true;
    g_event_loop.generation = 101;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    TEST_CHECK(daemon_io_begin(101), true);
    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    TEST_CHECK(daemon_io_enqueue_fd(pair[0]), true);
    int length = 3;
    char body[] = {'x', '\0', '\0'};
    write(pair[1], &length, sizeof(length));
    write(pair[1], body, sizeof(body));
    for (uint32_t elapsed = 0; elapsed < 1000 && daemon_io_test_event_queue_count(&g_event_loop) == 0; ++elapsed) usleep(1000);
    TEST_CHECK(daemon_io_test_event_queue_count(&g_event_loop), 1U);
    daemon_io_shutdown_timeout_ms = 1000;
    uint64_t before = socket_io_monotonic_ns();
    daemon_io_test_clock_error = EIO;
    TEST_CHECK(daemon_io_end(), false);
    TEST_CHECK((socket_io_monotonic_ns() - before) / 1000000ULL < 100, true);
    daemon_io_test_clock_error = 0;
    daemon_io_test_timedwait_error = EINVAL;
    before = socket_io_monotonic_ns();
    TEST_CHECK(daemon_io_end(), false);
    TEST_CHECK((socket_io_monotonic_ns() - before) / 1000000ULL < 100, true);
    daemon_io_test_timedwait_error = 0;
    daemon_io_shutdown_timeout_ms = 20;
    before = socket_io_monotonic_ns();
    TEST_CHECK(daemon_io_end(), false);
    TEST_CHECK((socket_io_monotonic_ns() - before) / 1000000ULL < 200, true);
    TEST_CHECK(daemon_io_test_active_jobs(), 1U);
    event_loop_cleanup(&g_event_loop);
    TEST_CHECK(daemon_io_test_wait_for_admission(0, 1000), true);
    TEST_CHECK(daemon_io_end(), true);
    daemon_io_shutdown_timeout_ms = DAEMON_IO_READ_TIMEOUT_MS + DAEMON_IO_WRITE_TIMEOUT_MS + DAEMON_IO_SHUTDOWN_GRACE_MS;
    close(pair[1]);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
});

static int message_loop_test_fail_thread(pthread_t *thread, const pthread_attr_t *attr,
                                         void *(*entry)(void *), void *context)
{
    (void) thread; (void) attr; (void) entry; (void) context;
    return EAGAIN;
}

TEST_FUNC(message_loop_lifecycle_unblocks_accept_and_recovers_from_thread_failure,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_event_loop = (struct event_loop) {0};
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(event_loop_begin(&g_event_loop), true);
    char directory[] = "/tmp/yabai-message-loop.XXXXXX";
    char *root = mkdtemp(directory);
    TEST_CHECK(root != NULL, true);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/socket", root);
    message_loop_thread_create = message_loop_test_fail_thread;
    TEST_CHECK(message_loop_begin(path), false);
    TEST_CHECK(message_loop_end(), true);
    message_loop_thread_create = pthread_create;
    TEST_CHECK(message_loop_begin(path), true);
    TEST_CHECK(message_loop_end(), true);
    TEST_CHECK(message_loop_end(), true);
    TEST_CHECK(access(path, F_OK), -1);
    rmdir(root);
    event_loop_end(&g_event_loop);
    g_daemon_message_admission = saved_admission;
    g_event_loop = saved_event_loop;
});
