struct daemon_io_end_thread_context
{
    bool completed;
    bool result;
};

static void *daemon_io_test_end_thread(void *context)
{
    struct daemon_io_end_thread_context *end = context;
    end->result = daemon_io_end();
    __atomic_store_n(&end->completed, true, __ATOMIC_RELEASE);
    return NULL;
}

static pthread_mutex_t daemon_io_stop_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t daemon_io_stop_gate_cond = PTHREAD_COND_INITIALIZER;
static bool daemon_io_stop_gate_entered;
static bool daemon_io_stop_gate_release;

static void daemon_io_test_stop_gate(void)
{
    pthread_mutex_lock(&daemon_io_stop_gate_mutex);
    daemon_io_stop_gate_entered = true;
    pthread_cond_broadcast(&daemon_io_stop_gate_cond);
    while (!daemon_io_stop_gate_release) pthread_cond_wait(&daemon_io_stop_gate_cond, &daemon_io_stop_gate_mutex);
    pthread_mutex_unlock(&daemon_io_stop_gate_mutex);
}

TEST_FUNC(daemon_io_concurrent_end_has_single_teardown_owner,
{
    TEST_CHECK(daemon_io_begin(1), true);
    daemon_io_stop_gate_entered = false;
    daemon_io_stop_gate_release = false;
    daemon_io_stopping_test_hook = daemon_io_test_stop_gate;
    struct daemon_io_end_thread_context owner = {0};
    pthread_t owner_thread;
    TEST_CHECK(pthread_create(&owner_thread, NULL, daemon_io_test_end_thread, &owner), 0);
    pthread_mutex_lock(&daemon_io_stop_gate_mutex);
    while (!daemon_io_stop_gate_entered) pthread_cond_wait(&daemon_io_stop_gate_cond, &daemon_io_stop_gate_mutex);
    pthread_mutex_unlock(&daemon_io_stop_gate_mutex);
    TEST_CHECK(daemon_io_end(), false);
    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    TEST_CHECK(g_daemon_io.teardown_attempt_active, true);
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
    pthread_mutex_lock(&daemon_io_stop_gate_mutex);
    daemon_io_stop_gate_release = true;
    pthread_cond_broadcast(&daemon_io_stop_gate_cond);
    pthread_mutex_unlock(&daemon_io_stop_gate_mutex);
    pthread_join(owner_thread, NULL);
    TEST_CHECK(owner.result, true);
    TEST_CHECK(owner.completed, true);
    daemon_io_stopping_test_hook = NULL;
    TEST_CHECK(daemon_io_end(), true);
});

struct daemon_io_enqueue_thread_context
{
    int fd;
    bool completed;
    bool result;
};

static void *daemon_io_test_enqueue_thread(void *context)
{
    struct daemon_io_enqueue_thread_context *enqueue = context;
    enqueue->result = daemon_io_enqueue_fd(enqueue->fd);
    __atomic_store_n(&enqueue->completed, true, __ATOMIC_RELEASE);
    return NULL;
}

static pthread_mutex_t daemon_io_lease_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t daemon_io_lease_gate_cond = PTHREAD_COND_INITIALIZER;
static bool daemon_io_lease_gate_entered;
static bool daemon_io_lease_gate_release;

static void daemon_io_test_lease_gate(void)
{
    pthread_mutex_lock(&daemon_io_lease_gate_mutex);
    daemon_io_lease_gate_entered = true;
    pthread_cond_broadcast(&daemon_io_lease_gate_cond);
    while (!daemon_io_lease_gate_release) pthread_cond_wait(&daemon_io_lease_gate_cond, &daemon_io_lease_gate_mutex);
    pthread_mutex_unlock(&daemon_io_lease_gate_mutex);
}

static pthread_mutex_t daemon_io_drain_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t daemon_io_drain_gate_cond = PTHREAD_COND_INITIALIZER;
static bool daemon_io_drain_gate_entered;

static void daemon_io_test_before_drain(void)
{
    pthread_mutex_lock(&daemon_io_drain_gate_mutex);
    daemon_io_drain_gate_entered = true;
    pthread_cond_broadcast(&daemon_io_drain_gate_cond);
    pthread_mutex_unlock(&daemon_io_drain_gate_mutex);
}

static pthread_mutex_t daemon_io_publish_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t daemon_io_publish_gate_cond = PTHREAD_COND_INITIALIZER;
static bool daemon_io_publish_gate_entered;
static bool daemon_io_publish_gate_release;

static void daemon_io_test_after_publish_gate(void)
{
    pthread_mutex_lock(&daemon_io_publish_gate_mutex);
    daemon_io_publish_gate_entered = true;
    pthread_cond_broadcast(&daemon_io_publish_gate_cond);
    while (!daemon_io_publish_gate_release) pthread_cond_wait(&daemon_io_publish_gate_cond, &daemon_io_publish_gate_mutex);
    pthread_mutex_unlock(&daemon_io_publish_gate_mutex);
}

TEST_FUNC(daemon_io_immediate_reader_finalize_cannot_create_phantom_active_job,
{
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(daemon_io_begin(1), true);
    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    int invalid_length = 1;
    TEST_CHECK(write(pair[1], &invalid_length, sizeof(invalid_length)), (ssize_t) sizeof(invalid_length));
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    daemon_io_publish_gate_entered = false;
    daemon_io_publish_gate_release = false;
    daemon_io_after_publish_test_hook = daemon_io_test_after_publish_gate;
    struct daemon_io_enqueue_thread_context enqueue = {.fd = pair[0]};
    pthread_t enqueue_thread;
    TEST_CHECK(pthread_create(&enqueue_thread, NULL, daemon_io_test_enqueue_thread, &enqueue), 0);
    pthread_mutex_lock(&daemon_io_publish_gate_mutex);
    while (!daemon_io_publish_gate_entered) pthread_cond_wait(&daemon_io_publish_gate_cond, &daemon_io_publish_gate_mutex);
    pthread_mutex_unlock(&daemon_io_publish_gate_mutex);

    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    while (g_daemon_io.active_jobs || g_daemon_io.enqueue_leases) {
        pthread_cond_wait(&g_daemon_io.jobs_drained, &g_daemon_io.lifecycle_mutex);
    }
    TEST_CHECK(g_daemon_io.active_jobs, 0U);
    TEST_CHECK(g_daemon_io.enqueue_leases, 0U);
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
    TEST_CHECK(__atomic_load_n(&enqueue.completed, __ATOMIC_ACQUIRE), false);
    TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 0U);
    TEST_CHECK(daemon_io_test_writer_reservations(), 0U);

    pthread_mutex_lock(&daemon_io_publish_gate_mutex);
    daemon_io_publish_gate_release = true;
    pthread_cond_broadcast(&daemon_io_publish_gate_cond);
    pthread_mutex_unlock(&daemon_io_publish_gate_mutex);
    pthread_join(enqueue_thread, NULL);
    TEST_CHECK(enqueue.result, true);
    TEST_CHECK(daemon_io_test_active_jobs(), 0U);
    TEST_CHECK(daemon_io_end(), true);
    TEST_CHECK(daemon_io_begin(2), true);
    TEST_CHECK(daemon_io_end(), true);
    daemon_io_after_publish_test_hook = NULL;
    char response[64];
    TEST_CHECK(read(pair[1], response, sizeof(response)) > 1, true);
    close(pair[1]);
    g_daemon_message_admission = saved_admission;
});

TEST_FUNC(daemon_io_enqueue_lease_blocks_teardown_until_transfer_or_release,
{
    struct daemon_message_admission saved_admission = g_daemon_message_admission;
    g_daemon_message_admission = (struct daemon_message_admission) {0};
    TEST_CHECK(daemon_io_begin(1), true);
    int pair[2] = {-1, -1};
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    TEST_CHECK(daemon_message_admission_acquire(&g_daemon_message_admission), true);
    daemon_io_lease_gate_entered = false;
    daemon_io_lease_gate_release = false;
    daemon_io_enqueue_lease_test_hook = daemon_io_test_lease_gate;
    struct daemon_io_enqueue_thread_context enqueue = {.fd = pair[0]};
    pthread_t enqueue_thread;
    TEST_CHECK(pthread_create(&enqueue_thread, NULL, daemon_io_test_enqueue_thread, &enqueue), 0);
    pthread_mutex_lock(&daemon_io_lease_gate_mutex);
    while (!daemon_io_lease_gate_entered) pthread_cond_wait(&daemon_io_lease_gate_cond, &daemon_io_lease_gate_mutex);
    pthread_mutex_unlock(&daemon_io_lease_gate_mutex);
    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    TEST_CHECK(g_daemon_io.enqueue_leases, 1U);
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
    daemon_io_drain_gate_entered = false;
    daemon_io_before_drain_test_hook = daemon_io_test_before_drain;
    struct daemon_io_end_thread_context end = {0};
    pthread_t end_thread;
    TEST_CHECK(pthread_create(&end_thread, NULL, daemon_io_test_end_thread, &end), 0);
    pthread_mutex_lock(&daemon_io_drain_gate_mutex);
    while (!daemon_io_drain_gate_entered) pthread_cond_wait(&daemon_io_drain_gate_cond, &daemon_io_drain_gate_mutex);
    pthread_mutex_unlock(&daemon_io_drain_gate_mutex);
    TEST_CHECK(__atomic_load_n(&end.completed, __ATOMIC_ACQUIRE), false);
    pthread_mutex_lock(&daemon_io_lease_gate_mutex);
    daemon_io_lease_gate_release = true;
    pthread_cond_broadcast(&daemon_io_lease_gate_cond);
    pthread_mutex_unlock(&daemon_io_lease_gate_mutex);
    pthread_join(enqueue_thread, NULL);
    pthread_join(end_thread, NULL);
    TEST_CHECK(enqueue.result, false);
    TEST_CHECK(end.result, true);
    daemon_io_enqueue_lease_test_hook = NULL;
    daemon_io_before_drain_test_hook = NULL;
    socket_close(pair[0]);
    close(pair[1]);
    daemon_message_admission_release(&g_daemon_message_admission);
    TEST_CHECK(daemon_message_admission_count(&g_daemon_message_admission), 0U);
    g_daemon_message_admission = saved_admission;
});
