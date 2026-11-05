#include "daemon_io.h"

enum daemon_io_state
{
    DAEMON_IO_STOPPED,
    DAEMON_IO_STARTING,
    DAEMON_IO_RUNNING,
    DAEMON_IO_STOPPING
};

enum daemon_io_worker_kind
{
    DAEMON_IO_NOT_WORKER,
    DAEMON_IO_READER_WORKER,
    DAEMON_IO_WRITER_WORKER
};

struct daemon_io_queue
{
    pthread_mutex_t mutex;
    pthread_cond_t available;
    void *items[DAEMON_IO_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t count;
    uint32_t reserved;
    bool open;
    bool mutex_initialized;
    bool cond_initialized;
};

static struct
{
    pthread_mutex_t lifecycle_mutex;
    pthread_cond_t jobs_drained;
    pthread_cond_t teardown_done;
    enum daemon_io_state state;
    struct daemon_io_queue readers;
    struct daemon_io_queue writers;
    pthread_t reader_threads[DAEMON_IO_READER_COUNT];
    pthread_t writer_threads[DAEMON_IO_WRITER_COUNT];
    uint32_t reader_count;
    uint32_t writer_count;
    uint32_t active_jobs;
    uint32_t enqueue_leases;
    uint64_t event_generation;
    bool running;
    bool teardown_attempt_active;
    pthread_t teardown_owner;
} g_daemon_io = {
    .lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER,
    .jobs_drained = PTHREAD_COND_INITIALIZER,
    .teardown_done = PTHREAD_COND_INITIALIZER,
    .state = DAEMON_IO_STOPPED
};

static __thread enum daemon_io_worker_kind daemon_io_worker_kind;

extern struct daemon_message_admission g_daemon_message_admission;
extern struct event_loop g_event_loop;

#ifdef TESTS
static void (*daemon_io_handle_message_test_hook)(FILE *, char *, size_t);
static void (*daemon_io_reader_worker_test_hook)(void);
static void (*daemon_io_writer_worker_test_hook)(void);
static void (*daemon_io_enqueue_lease_test_hook)(void);
static void (*daemon_io_after_publish_test_hook)(void);
static void (*daemon_io_stopping_test_hook)(void);
static void (*daemon_io_before_drain_test_hook)(void);
static int daemon_io_thread_create_fail_after = -1;
static int daemon_io_thread_create_attempt;
static int daemon_io_queue_init_fail_after = -1;
static int daemon_io_queue_init_attempt;
static bool daemon_io_fail_job_allocation;
static bool daemon_io_fail_request_allocation;
static bool daemon_io_fail_response_open;
static bool daemon_io_fail_response_finalize;
static uint32_t daemon_io_read_timeout_ms = DAEMON_IO_READ_TIMEOUT_MS;
static uint32_t daemon_io_write_timeout_ms = DAEMON_IO_WRITE_TIMEOUT_MS;
static uint32_t daemon_io_shutdown_timeout_ms = DAEMON_IO_READ_TIMEOUT_MS + DAEMON_IO_WRITE_TIMEOUT_MS + DAEMON_IO_SHUTDOWN_GRACE_MS;
static uint32_t daemon_io_test_finalizer_count;
static uint32_t daemon_io_test_request_free_count;
static uint32_t daemon_io_test_response_destroy_count;
static uint32_t daemon_io_test_close_count;
static uint32_t daemon_io_test_admission_release_count;
static uint32_t daemon_io_test_reservation_cancel_count;
static int daemon_io_test_clock_error;
static int daemon_io_test_timedwait_error;
#define DAEMON_IO_READ_DEADLINE_MS daemon_io_read_timeout_ms
#define DAEMON_IO_WRITE_DEADLINE_MS daemon_io_write_timeout_ms
#define DAEMON_IO_SHUTDOWN_TIMEOUT_MS daemon_io_shutdown_timeout_ms
#else
#define DAEMON_IO_READ_DEADLINE_MS DAEMON_IO_READ_TIMEOUT_MS
#define DAEMON_IO_WRITE_DEADLINE_MS DAEMON_IO_WRITE_TIMEOUT_MS
#define DAEMON_IO_SHUTDOWN_TIMEOUT_MS (DAEMON_IO_READ_TIMEOUT_MS + DAEMON_IO_WRITE_TIMEOUT_MS + DAEMON_IO_SHUTDOWN_GRACE_MS)
#endif

static int daemon_io_thread_create(pthread_t *thread, void *(*entry)(void *))
{
#ifdef TESTS
    if (daemon_io_thread_create_fail_after >= 0 &&
        daemon_io_thread_create_attempt++ == daemon_io_thread_create_fail_after) return EAGAIN;
#endif
    return pthread_create(thread, NULL, entry, NULL);
}

static bool daemon_io_queue_init(struct daemon_io_queue *queue)
{
    if (queue->mutex_initialized || queue->cond_initialized) return false;
    queue->head = 0;
    queue->count = 0;
    queue->reserved = 0;
    queue->open = false;
    memset(queue->items, 0, sizeof(queue->items));
#ifdef TESTS
    if (daemon_io_queue_init_fail_after >= 0 &&
        daemon_io_queue_init_attempt++ == daemon_io_queue_init_fail_after) return false;
#endif
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) return false;
    queue->mutex_initialized = true;
#ifdef TESTS
    if (daemon_io_queue_init_fail_after >= 0 &&
        daemon_io_queue_init_attempt++ == daemon_io_queue_init_fail_after) {
        pthread_mutex_destroy(&queue->mutex);
        queue->mutex_initialized = false;
        return false;
    }
#endif
    if (pthread_cond_init(&queue->available, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        queue->mutex_initialized = false;
        return false;
    }
    queue->cond_initialized = true;
    queue->open = true;
    return true;
}

static void daemon_io_queue_close(struct daemon_io_queue *queue)
{
    if (!queue->mutex_initialized) return;
    pthread_mutex_lock(&queue->mutex);
    queue->open = false;
    if (queue->cond_initialized) pthread_cond_broadcast(&queue->available);
    pthread_mutex_unlock(&queue->mutex);
}

static void daemon_io_queue_destroy(struct daemon_io_queue *queue)
{
    if (queue->cond_initialized) {
        pthread_cond_destroy(&queue->available);
        queue->cond_initialized = false;
    }
    if (queue->mutex_initialized) {
        pthread_mutex_destroy(&queue->mutex);
        queue->mutex_initialized = false;
    }
    queue->open = false;
    queue->head = queue->count = queue->reserved = 0;
    memset(queue->items, 0, sizeof(queue->items));
}

static bool daemon_io_queue_try_push(struct daemon_io_queue *queue, void *item)
{
    if (!queue->mutex_initialized) return false;
    bool result = false;
    pthread_mutex_lock(&queue->mutex);
    if (queue->open && queue->count + queue->reserved < DAEMON_IO_QUEUE_CAPACITY) {
        uint32_t tail = (queue->head + queue->count) % DAEMON_IO_QUEUE_CAPACITY;
        queue->items[tail] = item;
        ++queue->count;
        pthread_cond_signal(&queue->available);
        result = true;
    }
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

static void *daemon_io_queue_pop(struct daemon_io_queue *queue)
{
    if (!queue->mutex_initialized || !queue->cond_initialized) return NULL;
    pthread_mutex_lock(&queue->mutex);
    while (queue->open && queue->count == 0) pthread_cond_wait(&queue->available, &queue->mutex);
    void *item = NULL;
    if (queue->count) {
        item = queue->items[queue->head];
        queue->items[queue->head] = NULL;
        queue->head = (queue->head + 1) % DAEMON_IO_QUEUE_CAPACITY;
        --queue->count;
    }
    pthread_mutex_unlock(&queue->mutex);
    return item;
}

static bool daemon_io_queue_reserve(struct daemon_io_queue *queue)
{
    if (!queue->mutex_initialized) return false;
    bool result = false;
    pthread_mutex_lock(&queue->mutex);
    if (queue->open && queue->count + queue->reserved < DAEMON_IO_QUEUE_CAPACITY) {
        ++queue->reserved;
        result = true;
    }
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

static bool daemon_io_queue_submit_reserved(struct daemon_io_queue *queue, void *item)
{
    if (!queue->mutex_initialized) return false;
    bool result = false;
    pthread_mutex_lock(&queue->mutex);
    if (queue->reserved) {
        --queue->reserved;
        if (queue->open) {
            uint32_t tail = (queue->head + queue->count) % DAEMON_IO_QUEUE_CAPACITY;
            queue->items[tail] = item;
            ++queue->count;
            pthread_cond_signal(&queue->available);
            result = true;
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

static void daemon_io_queue_cancel_reservation(struct daemon_io_queue *queue)
{
    if (!queue->mutex_initialized) return;
    pthread_mutex_lock(&queue->mutex);
    if (queue->reserved) {
        --queue->reserved;
#ifdef TESTS
        __atomic_add_fetch(&daemon_io_test_reservation_cancel_count, 1, __ATOMIC_RELAXED);
#endif
    }
    pthread_mutex_unlock(&queue->mutex);
}

static void daemon_io_active_remove(void)
{
    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    if (g_daemon_io.active_jobs) --g_daemon_io.active_jobs;
    if (!g_daemon_io.active_jobs && !g_daemon_io.enqueue_leases) {
        pthread_cond_broadcast(&g_daemon_io.jobs_drained);
    }
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
}

static void daemon_io_enqueue_lease_release(void)
{
    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    if (g_daemon_io.enqueue_leases) --g_daemon_io.enqueue_leases;
    if (!g_daemon_io.active_jobs && !g_daemon_io.enqueue_leases) {
        pthread_cond_broadcast(&g_daemon_io.jobs_drained);
    }
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
}

void daemon_io_job_destroy(struct daemon_io_job *job)
{
    if (!job || __atomic_exchange_n(&job->finalized, 1, __ATOMIC_ACQ_REL)) return;
    job->owner = DAEMON_IO_OWNER_FINALIZED;
#ifdef TESTS
    __atomic_add_fetch(&daemon_io_test_finalizer_count, 1, __ATOMIC_RELAXED);
#endif
    if (job->writer_reserved) {
        daemon_io_queue_cancel_reservation(&g_daemon_io.writers);
        job->writer_reserved = false;
    }
    if (job->response_owned) {
        daemon_response_buffer_destroy(&job->response);
#ifdef TESTS
        __atomic_add_fetch(&daemon_io_test_response_destroy_count, 1, __ATOMIC_RELAXED);
#endif
    }
    if (job->request) {
        free(job->request);
#ifdef TESTS
        __atomic_add_fetch(&daemon_io_test_request_free_count, 1, __ATOMIC_RELAXED);
#endif
    }
    if (job->fd >= 0) {
        socket_close(job->fd);
#ifdef TESTS
        __atomic_add_fetch(&daemon_io_test_close_count, 1, __ATOMIC_RELAXED);
#endif
    }
    daemon_message_admission_release(&g_daemon_message_admission);
#ifdef TESTS
    __atomic_add_fetch(&daemon_io_test_admission_release_count, 1, __ATOMIC_RELAXED);
#endif
    free(job);
    daemon_io_active_remove();
}

static void daemon_io_fail_and_destroy(struct daemon_io_job *job, const char *bytes, size_t length)
{
    (void) daemon_response_write_frame_until(job->fd, bytes, length, true,
                                              socket_io_deadline_after_ms(DAEMON_IO_WRITE_DEADLINE_MS));
    daemon_io_job_destroy(job);
}

void daemon_io_reject(struct daemon_io_job *job)
{
    static char rejected[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                               'm','e','s','s','a','g','e',' ','w','a','s',' ','n','o','t',' ','d','i','s','p','a','t','c','h','e','d','.','\n' };
    if (!job) return;
    job->response.bytes = rejected;
    job->response.length = sizeof(rejected);
    job->owner = DAEMON_IO_OWNER_WRITER;
    job->writer_reserved = false;
    if (!daemon_io_queue_submit_reserved(&g_daemon_io.writers, job)) daemon_io_job_destroy(job);
}

static void *daemon_io_reader(void *context)
{
    (void) context;
    daemon_io_worker_kind = DAEMON_IO_READER_WORKER;
    static const char incomplete[] = { DAEMON_MESSAGE_FAILURE_MARKER, 'i','n','c','o','m','p','l','e','t','e',' ','m','e','s','s','a','g','e','.','\n' };
    static const char invalid_length[] = { DAEMON_MESSAGE_FAILURE_MARKER, 'i','n','v','a','l','i','d',' ','m','e','s','s','a','g','e',' ','l','e','n','g','t','h','.','\n' };
    static const char invalid_body[] = { DAEMON_MESSAGE_FAILURE_MARKER, 'i','n','v','a','l','i','d',' ','m','e','s','s','a','g','e',' ','b','o','d','y','.','\n' };

    for (;;) {
        struct daemon_io_job *job = daemon_io_queue_pop(&g_daemon_io.readers);
        if (!job) break;
#ifdef TESTS
        if (daemon_io_reader_worker_test_hook) daemon_io_reader_worker_test_hook();
#endif
        if (!__atomic_load_n(&g_daemon_io.running, __ATOMIC_ACQUIRE)) {
            daemon_io_job_destroy(job);
            continue;
        }
        int length = 0;
        uint64_t deadline = socket_io_deadline_after_ms(DAEMON_IO_READ_DEADLINE_MS);
        if (!socket_io_read_exact_until(job->fd, &length, sizeof(length), deadline)) {
            daemon_io_fail_and_destroy(job, incomplete, sizeof(incomplete));
            continue;
        }
        if (!daemon_message_length_is_valid(length)) {
            daemon_io_fail_and_destroy(job, invalid_length, sizeof(invalid_length));
            continue;
        }
#ifdef TESTS
        job->request = daemon_io_fail_request_allocation ? NULL : malloc((size_t) length);
#else
        job->request = malloc((size_t) length);
#endif
        if (!job->request || !socket_io_read_exact_until(job->fd, job->request, (size_t) length, deadline)) {
            daemon_io_fail_and_destroy(job, incomplete, sizeof(incomplete));
            continue;
        }
        job->request_length = (size_t) length;
        if (!daemon_message_body_is_valid(job->request, job->request_length)) {
            daemon_io_fail_and_destroy(job, invalid_body, sizeof(invalid_body));
            continue;
        }
        if (!daemon_io_queue_reserve(&g_daemon_io.writers)) {
            static const char unavailable[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                                'r','e','s','p','o','n','s','e',' ','w','o','r','k','e','r',' ','u','n','a','v','a','i','l','a','b','l','e','.','\n' };
            daemon_io_fail_and_destroy(job, unavailable, sizeof(unavailable));
            continue;
        }
        job->writer_reserved = true;
        job->owner = DAEMON_IO_OWNER_EVENT;
        /* Posting consumes the job whether it succeeds or rejects it. */
        (void) event_loop_try_post_generation(&g_event_loop, job->event_generation,
                                              DAEMON_MESSAGE, job, -1);
    }
    daemon_io_worker_kind = DAEMON_IO_NOT_WORKER;
    return NULL;
}

static void *daemon_io_writer(void *context)
{
    (void) context;
    daemon_io_worker_kind = DAEMON_IO_WRITER_WORKER;
    for (;;) {
        struct daemon_io_job *job = daemon_io_queue_pop(&g_daemon_io.writers);
        if (!job) break;
#ifdef TESTS
        if (daemon_io_writer_worker_test_hook) daemon_io_writer_worker_test_hook();
#endif
        if (job->response.bytes) {
            bool failed = job->response.length && job->response.bytes[0] == DAEMON_MESSAGE_FAILURE_MARKER;
            (void) daemon_response_write_frame_until(job->fd, job->response.bytes, job->response.length,
                                                      failed, socket_io_deadline_after_ms(DAEMON_IO_WRITE_DEADLINE_MS));
        }
        daemon_io_job_destroy(job);
    }
    daemon_io_worker_kind = DAEMON_IO_NOT_WORKER;
    return NULL;
}

bool daemon_io_enqueue_fd(int fd)
{
    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    if (g_daemon_io.state != DAEMON_IO_RUNNING) {
        pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
        return false;
    }
    ++g_daemon_io.enqueue_leases;
    uint64_t generation = g_daemon_io.event_generation;
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
#ifdef TESTS
    if (daemon_io_enqueue_lease_test_hook) daemon_io_enqueue_lease_test_hook();
#endif
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0) {
        daemon_io_enqueue_lease_release();
        return false;
    }
#endif
#ifdef TESTS
    struct daemon_io_job *job = daemon_io_fail_job_allocation ? NULL : calloc(1, sizeof(*job));
#else
    struct daemon_io_job *job = calloc(1, sizeof(*job));
#endif
    if (!job) {
        daemon_io_enqueue_lease_release();
        return false;
    }
    job->fd = fd;
    job->event_generation = generation;
    job->owner = DAEMON_IO_OWNER_READER;

    /* Convert the lease to job accounting before queue publication. A reader
     * can finalize immediately after push, so publication must never precede
     * this transfer. */
    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    ++g_daemon_io.active_jobs;
    --g_daemon_io.enqueue_leases;
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);

    if (!daemon_io_queue_try_push(&g_daemon_io.readers, job)) {
        free(job);
        daemon_io_active_remove();
        return false;
    }
#ifdef TESTS
    if (daemon_io_after_publish_test_hook) daemon_io_after_publish_test_hook();
#endif
    return true;
}

void daemon_io_execute(struct daemon_io_job *job)
{
    static char buffer_failure[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'f','a','i','l','e','d',' ','t','o',' ','b','u','f','f','e','r',' ','r','e','s','p','o','n','s','e','.','\n' };
    if (!job) return;
#ifdef TESTS
    bool opened = !daemon_io_fail_response_open && daemon_response_buffer_open(&job->response);
#else
    bool opened = daemon_response_buffer_open(&job->response);
#endif
    if (opened) {
        job->response_owned = true;
        debug_message(__FUNCTION__, job->request);
#ifdef TESTS
        if (daemon_io_handle_message_test_hook) daemon_io_handle_message_test_hook(job->response.stream, job->request, job->request_length);
        else
#endif
        handle_message(job->response.stream, job->request, job->request_length);
#ifdef TESTS
        bool finalized = !daemon_io_fail_response_finalize && daemon_response_buffer_finalize(&job->response);
#else
        bool finalized = daemon_response_buffer_finalize(&job->response);
#endif
        if (!finalized) {
            daemon_response_buffer_destroy(&job->response);
#ifdef TESTS
            __atomic_add_fetch(&daemon_io_test_response_destroy_count, 1, __ATOMIC_RELAXED);
#endif
            job->response_owned = false;
            job->response.bytes = buffer_failure;
            job->response.length = sizeof(buffer_failure);
        }
    } else {
        job->response.bytes = buffer_failure;
        job->response.length = sizeof(buffer_failure);
    }

    job->owner = DAEMON_IO_OWNER_WRITER;
    job->writer_reserved = false;
    if (!daemon_io_queue_submit_reserved(&g_daemon_io.writers, job)) daemon_io_job_destroy(job);
}

static void daemon_io_join_workers(void)
{
    for (uint32_t i = 0; i < g_daemon_io.reader_count; ++i) pthread_join(g_daemon_io.reader_threads[i], NULL);
    g_daemon_io.reader_count = 0;
    for (uint32_t i = 0; i < g_daemon_io.writer_count; ++i) pthread_join(g_daemon_io.writer_threads[i], NULL);
    g_daemon_io.writer_count = 0;
}

static void daemon_io_startup_rollback(void)
{
    __atomic_store_n(&g_daemon_io.running, false, __ATOMIC_RELEASE);
    daemon_io_queue_close(&g_daemon_io.readers);
    daemon_io_queue_close(&g_daemon_io.writers);
    daemon_io_join_workers();
    daemon_io_queue_destroy(&g_daemon_io.readers);
    daemon_io_queue_destroy(&g_daemon_io.writers);
    g_daemon_io.event_generation = 0;
}

bool daemon_io_begin(uint64_t event_generation)
{
    if (!event_generation || daemon_io_worker_kind != DAEMON_IO_NOT_WORKER || event_loop_is_worker_thread(&g_event_loop)) return false;
    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    if (g_daemon_io.state != DAEMON_IO_STOPPED) {
        pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
        return false;
    }
    g_daemon_io.state = DAEMON_IO_STARTING;
    g_daemon_io.event_generation = event_generation;
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);

#ifdef TESTS
    daemon_io_thread_create_attempt = 0;
    daemon_io_queue_init_attempt = 0;
#endif
    bool ok = daemon_io_queue_init(&g_daemon_io.readers) && daemon_io_queue_init(&g_daemon_io.writers);
    if (ok) {
        __atomic_store_n(&g_daemon_io.running, true, __ATOMIC_RELEASE);
        for (; g_daemon_io.reader_count < DAEMON_IO_READER_COUNT; ++g_daemon_io.reader_count) {
            if (daemon_io_thread_create(&g_daemon_io.reader_threads[g_daemon_io.reader_count], daemon_io_reader) != 0) { ok = false; break; }
        }
    }
    if (ok) {
        for (; g_daemon_io.writer_count < DAEMON_IO_WRITER_COUNT; ++g_daemon_io.writer_count) {
            if (daemon_io_thread_create(&g_daemon_io.writer_threads[g_daemon_io.writer_count], daemon_io_writer) != 0) { ok = false; break; }
        }
    }
    if (!ok) daemon_io_startup_rollback();

    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    g_daemon_io.state = ok ? DAEMON_IO_RUNNING : DAEMON_IO_STOPPED;
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
    return ok;
}

static int daemon_io_monotonic_now(struct timespec *now)
{
#ifdef TESTS
    if (daemon_io_test_clock_error) return daemon_io_test_clock_error;
#endif
    return clock_gettime(CLOCK_MONOTONIC, now) == 0 ? 0 : errno;
}

static int daemon_io_jobs_timedwait_relative(const struct timespec *relative)
{
#ifdef TESTS
    if (daemon_io_test_timedwait_error) return daemon_io_test_timedwait_error;
#endif
#ifdef __APPLE__
    return pthread_cond_timedwait_relative_np(&g_daemon_io.jobs_drained,
                                              &g_daemon_io.lifecycle_mutex, relative);
#else
    struct timespec absolute;
    if (clock_gettime(CLOCK_REALTIME, &absolute) != 0) return errno;
    absolute.tv_sec += relative->tv_sec;
    absolute.tv_nsec += relative->tv_nsec;
    if (absolute.tv_nsec >= 1000000000L) { ++absolute.tv_sec; absolute.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(&g_daemon_io.jobs_drained, &g_daemon_io.lifecycle_mutex, &absolute);
#endif
}

static void daemon_io_teardown_attempt_release_locked(void)
{
    g_daemon_io.teardown_attempt_active = false;
    memset(&g_daemon_io.teardown_owner, 0, sizeof(g_daemon_io.teardown_owner));
    pthread_cond_broadcast(&g_daemon_io.teardown_done);
}

static bool daemon_io_wait_for_drain_locked(uint32_t timeout_ms)
{
    struct timespec start;
    if (daemon_io_monotonic_now(&start) != 0) return false;
    uint64_t start_ns = (uint64_t) start.tv_sec * 1000000000ULL + (uint64_t) start.tv_nsec;
    uint64_t delta = (uint64_t) timeout_ms * 1000000ULL;
    uint64_t deadline = UINT64_MAX - start_ns < delta ? UINT64_MAX : start_ns + delta;

    while (g_daemon_io.active_jobs || g_daemon_io.enqueue_leases) {
        struct timespec now;
        if (daemon_io_monotonic_now(&now) != 0) return false;
        uint64_t now_ns = (uint64_t) now.tv_sec * 1000000000ULL + (uint64_t) now.tv_nsec;
        if (now_ns >= deadline) return false;
        uint64_t remaining = deadline - now_ns;
        struct timespec relative = {
            .tv_sec = (time_t) (remaining / 1000000000ULL),
            .tv_nsec = (long) (remaining % 1000000000ULL)
        };
        int wait_result = daemon_io_jobs_timedwait_relative(&relative);
        if (wait_result == ETIMEDOUT) return false;
        if (wait_result != 0) return false;
    }
    return true;
}

bool daemon_io_end(void)
{
    if (daemon_io_worker_kind != DAEMON_IO_NOT_WORKER || event_loop_is_worker_thread(&g_event_loop)) return false;

    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    if (g_daemon_io.state == DAEMON_IO_STOPPED) {
        pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
        return true;
    }
    if (g_daemon_io.state == DAEMON_IO_STARTING || g_daemon_io.teardown_attempt_active) {
        pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
        return false;
    }
    bool first_stop = g_daemon_io.state == DAEMON_IO_RUNNING;
    g_daemon_io.state = DAEMON_IO_STOPPING;
    g_daemon_io.teardown_attempt_active = true;
    g_daemon_io.teardown_owner = pthread_self();
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
#ifdef TESTS
    if (daemon_io_stopping_test_hook) daemon_io_stopping_test_hook();
#endif

    if (first_stop) {
        __atomic_store_n(&g_daemon_io.running, false, __ATOMIC_RELEASE);
        daemon_io_queue_close(&g_daemon_io.readers);
        for (uint32_t i = 0; i < g_daemon_io.reader_count; ++i) pthread_join(g_daemon_io.reader_threads[i], NULL);
        g_daemon_io.reader_count = 0;
    }

#ifdef TESTS
    if (daemon_io_before_drain_test_hook) daemon_io_before_drain_test_hook();
#endif
    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    if (!daemon_io_wait_for_drain_locked(DAEMON_IO_SHUTDOWN_TIMEOUT_MS)) {
        daemon_io_teardown_attempt_release_locked();
        pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
        return false;
    }
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);

    daemon_io_queue_close(&g_daemon_io.writers);
    for (uint32_t i = 0; i < g_daemon_io.writer_count; ++i) pthread_join(g_daemon_io.writer_threads[i], NULL);
    g_daemon_io.writer_count = 0;
    daemon_io_queue_destroy(&g_daemon_io.readers);
    daemon_io_queue_destroy(&g_daemon_io.writers);

    pthread_mutex_lock(&g_daemon_io.lifecycle_mutex);
    g_daemon_io.event_generation = 0;
    g_daemon_io.state = DAEMON_IO_STOPPED;
    daemon_io_teardown_attempt_release_locked();
    pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex);
    return true;
}

#ifdef TESTS
uint32_t daemon_io_test_reader_thread_count(void) { return g_daemon_io.reader_count; }
uint32_t daemon_io_test_writer_thread_count(void) { return g_daemon_io.writer_count; }
uint32_t daemon_io_test_active_jobs(void) { pthread_mutex_lock(&g_daemon_io.lifecycle_mutex); uint32_t n = g_daemon_io.active_jobs; pthread_mutex_unlock(&g_daemon_io.lifecycle_mutex); return n; }
uint32_t daemon_io_test_writer_reservations(void) { if (!g_daemon_io.writers.mutex_initialized) return 0; pthread_mutex_lock(&g_daemon_io.writers.mutex); uint32_t n = g_daemon_io.writers.reserved; pthread_mutex_unlock(&g_daemon_io.writers.mutex); return n; }
#endif
