static unsigned process_discovery_test_queue_calls;
static bool process_discovery_test_queue_accept;

static bool process_discovery_test_schedule(unsigned index, uint64_t token)
{
    (void) index;
    (void) token;
    ++process_discovery_test_queue_calls;
    return process_discovery_test_queue_accept;
}

static unsigned process_discovery_test_active_count(void)
{
    unsigned count = 0;
    for (unsigned i = 0; i < PROCESS_DISCOVERY_RETRY_CAPACITY; ++i) {
        if (process_discovery_retries[i].active) ++count;
    }
    return count;
}

static ProcessSerialNumber process_caller_test_psn;
static int process_caller_test_create_count;
static int process_caller_test_destroy_count;
static int process_caller_test_next_count;
static int process_caller_test_launch_retry_count;

static struct process *process_caller_test_create(ProcessSerialNumber psn, pid_t pid)
{
    ++process_caller_test_create_count;
    struct process *process = legacy_allocation_calloc(1, sizeof(*process));
    if (!process) return NULL;
    process->psn = psn;
    process->pid = pid;
    process->name = strdup("process-caller");
    return process;
}

static void process_caller_test_destroy(struct process *process)
{
    (void) process;
    ++process_caller_test_destroy_count;
}

static pid_t process_caller_test_pid(ProcessSerialNumber psn)
{
    (void) psn;
    return 9191;
}

static bool process_caller_test_not_debugged(pid_t pid)
{
    (void) pid;
    return false;
}

static OSStatus process_caller_test_event_psn(EventRef event, ProcessSerialNumber *psn)
{
    (void) event;
    *psn = process_caller_test_psn;
    return noErr;
}

static UInt32 process_caller_test_event_kind(EventRef event)
{
    (void) event;
    return kEventAppLaunched;
}

static UInt32 process_caller_test_front_switched_event_kind(EventRef event)
{
    (void) event;
    return kEventAppFrontSwitched;
}

static OSStatus process_caller_test_next(ProcessSerialNumber *psn)
{
    if (process_caller_test_next_count++) return procNotFound;
    *psn = process_caller_test_psn;
    return noErr;
}

static void process_caller_test_launch_retry(struct process *process)
{
    (void) process;
    ++process_caller_test_launch_retry_count;
}

static int process_caller_test_retry_index(void)
{
    for (unsigned i = 0; i < PROCESS_DISCOVERY_RETRY_CAPACITY; ++i) {
        if (process_discovery_retries[i].active &&
            psn_equals(&process_discovery_retries[i].psn, &process_caller_test_psn)) return (int) i;
    }
    return -1;
}

TEST_FUNC(process_table_failures_recover_through_launch_and_startup_callers,
{
    __auto_type saved_create = process_manager_test_create_process;
    __auto_type saved_pid = process_manager_test_pid_for_psn;
    __auto_type saved_debugged = process_manager_test_is_debugged;
    __auto_type saved_event_psn = process_manager_test_event_psn;
    __auto_type saved_event_kind = process_manager_test_event_kind;
    __auto_type saved_next = process_manager_test_next_process;
    __auto_type saved_destroy = process_manager_test_destroy_observer;
    __auto_type saved_schedule = process_manager_test_schedule_discovery_callback;
    __auto_type saved_launch_retry = event_loop_test_application_retry_scheduler;
    process_manager_test_create_process = process_caller_test_create;
    process_manager_test_pid_for_psn = process_caller_test_pid;
    process_manager_test_is_debugged = process_caller_test_not_debugged;
    process_manager_test_event_psn = process_caller_test_event_psn;
    process_manager_test_event_kind = process_caller_test_event_kind;
    process_manager_test_next_process = process_caller_test_next;
    process_manager_test_destroy_observer = process_caller_test_destroy;
    process_manager_test_schedule_discovery_callback = process_discovery_test_schedule;
    event_loop_test_application_retry_scheduler = process_caller_test_launch_retry;
    process_discovery_test_queue_accept = true;

    for (int caller = 0; caller < 2; ++caller) {
        for (int rehash = 0; rehash <= 1; ++rehash) {
            memset(process_discovery_retries, 0, sizeof(process_discovery_retries));
            struct process_manager manager = {0};
            table_init(&manager.process, rehash ? 2 : 4, hash_psn, compare_psn);
            struct process sentinel = { .psn = { .highLongOfPSN = 71, .lowLongOfPSN = 72 } };
            if (rehash) table_add(&manager.process, &sentinel.psn, &sentinel);
            process_caller_test_psn = (ProcessSerialNumber) {
                .highLongOfPSN = (UInt32) (100 + caller), .lowLongOfPSN = (UInt32) (200 + rehash)
            };
            process_caller_test_create_count = process_caller_test_destroy_count = 0;
            process_caller_test_next_count = process_caller_test_launch_retry_count = 0;
            process_discovery_test_queue_calls = 0;
            table_test_fail_allocation_after(rehash ? 2 : 0);
            if (caller == 0) {
                TEST_CHECK(process_handler(NULL, (EventRef) (uintptr_t) 1, &manager), noErr);
            } else {
                process_manager_add_running_processes(&manager);
            }
            table_test_fail_allocation_after(-1);
            TEST_CHECK(process_manager_find_process(&manager, &process_caller_test_psn) == NULL, true);
            TEST_CHECK(manager.process.count, rehash ? 1 : 0);
            TEST_CHECK(process_caller_test_create_count, 1);
            TEST_CHECK(process_caller_test_destroy_count, 1);
            TEST_CHECK(process_discovery_test_queue_calls, 1U);
            int retry_index = process_caller_test_retry_index();
            TEST_CHECK(retry_index >= 0, true);
            if (retry_index >= 0) {
                uint64_t token = process_discovery_retries[retry_index].token;
                process_manager_run_discovery_retry((unsigned) retry_index, token);
            }
            struct process *recovered = process_manager_find_process(&manager, &process_caller_test_psn);
            TEST_CHECK(recovered != NULL, true);
            TEST_CHECK(process_caller_test_create_count, 2);
            TEST_CHECK(process_caller_test_destroy_count, 1);
            TEST_CHECK(process_caller_test_retry_index(), -1);
            TEST_CHECK(process_caller_test_launch_retry_count, 1);
            if (recovered) {
                TEST_CHECK(strcmp(recovered->name, "process-caller"), 0);
                TEST_CHECK(recovered->ns_application, NULL);
                table_remove(&manager.process, &process_caller_test_psn);
                process_destroy(recovered);
            }
            TEST_CHECK(process_caller_test_destroy_count, 2);
            table_free(&manager.process);
        }
    }

    memset(process_discovery_retries, 0, sizeof(process_discovery_retries));
    process_manager_test_create_process = saved_create;
    process_manager_test_pid_for_psn = saved_pid;
    process_manager_test_is_debugged = saved_debugged;
    process_manager_test_event_psn = saved_event_psn;
    process_manager_test_event_kind = saved_event_kind;
    process_manager_test_next_process = saved_next;
    process_manager_test_destroy_observer = saved_destroy;
    process_manager_test_schedule_discovery_callback = saved_schedule;
    event_loop_test_application_retry_scheduler = saved_launch_retry;
});

TEST_FUNC(process_front_switch_observation_publishes_identity_before_missing_process_discovery,
{
    __auto_type saved_pid = process_manager_test_pid_for_psn;
    __auto_type saved_event_psn = process_manager_test_event_psn;
    __auto_type saved_event_kind = process_manager_test_event_kind;
    __auto_type saved_schedule = process_manager_test_schedule_discovery_callback;
    struct table saved_process_table = g_process_manager.process;
    uint64_t saved_generation = g_process_manager.front_switch_generation;
    uint64_t saved_front_switch_psn = g_process_manager.front_switch_psn;
    pid_t saved_front_pid = g_process_manager.front_pid;
    ProcessSerialNumber saved_front_psn = g_process_manager.front_psn;

    ProcessSerialNumber old_psn = { .highLongOfPSN = 1, .lowLongOfPSN = 2 };
    ProcessSerialNumber missing_psn = { .highLongOfPSN = 3, .lowLongOfPSN = 4 };

    g_process_manager.process = (struct table) {0};
    table_init(&g_process_manager.process, 4, hash_psn, compare_psn);
    g_process_manager.front_pid = 101;
    g_process_manager.front_psn = old_psn;
    __atomic_store_n(&g_process_manager.front_switch_generation, 20, __ATOMIC_RELEASE);
    __atomic_store_n(&g_process_manager.front_switch_psn, process_manager_pack_psn(old_psn), __ATOMIC_RELEASE);

    memset(process_discovery_retries, 0, sizeof(process_discovery_retries));
    process_caller_test_psn = missing_psn;
    process_discovery_test_queue_accept = true;
    process_discovery_test_queue_calls = 0;
    process_manager_test_pid_for_psn = process_caller_test_pid;
    process_manager_test_event_psn = process_caller_test_event_psn;
    process_manager_test_event_kind = process_caller_test_front_switched_event_kind;
    process_manager_test_schedule_discovery_callback = process_discovery_test_schedule;
    TEST_CHECK(process_handler(NULL, (EventRef) (uintptr_t) 1, &g_process_manager), noErr);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE), 21ULL);
    TEST_CHECK(__atomic_load_n(&g_process_manager.front_switch_psn, __ATOMIC_ACQUIRE), process_manager_pack_psn(missing_psn));
    TEST_CHECK(process_discovery_test_queue_calls, 1U);

    memset(process_discovery_retries, 0, sizeof(process_discovery_retries));
    table_free(&g_process_manager.process);
    g_process_manager.process = saved_process_table;
    g_process_manager.front_pid = saved_front_pid;
    g_process_manager.front_psn = saved_front_psn;
    __atomic_store_n(&g_process_manager.front_switch_generation, saved_generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_process_manager.front_switch_psn, saved_front_switch_psn, __ATOMIC_RELEASE);
    process_manager_test_pid_for_psn = saved_pid;
    process_manager_test_event_psn = saved_event_psn;
    process_manager_test_event_kind = saved_event_kind;
    process_manager_test_schedule_discovery_callback = saved_schedule;
});

TEST_FUNC(process_discovery_retry_generation_rejects_stale_callbacks,
{
    memset(process_discovery_retries, 0, sizeof(process_discovery_retries));
    struct process_manager manager = {0};
    ProcessSerialNumber old_psn = { .highLongOfPSN = 1, .lowLongOfPSN = 2 };
    ProcessSerialNumber new_psn = { .highLongOfPSN = 3, .lowLongOfPSN = 4 };
    struct process_discovery_retry *retry = &process_discovery_retries[0];

    retry->manager = &manager;
    retry->psn = old_psn;
    retry->pid = 101;
    retry->attempt = 2;
    retry->active = true;
    uint64_t old_token = process_manager_advance_discovery_token(retry);
    process_manager_cancel_discovery_retry(retry);

    retry->manager = &manager;
    retry->psn = new_psn;
    retry->pid = 101;
    retry->attempt = 0;
    retry->active = true;
    uint64_t new_token = process_manager_advance_discovery_token(retry);
    TEST_CHECK(new_token != old_token, true);

    process_manager_run_discovery_retry(0, old_token);
    TEST_CHECK(retry->active, true);
    TEST_CHECK(retry->attempt, 0U);
    TEST_CHECK(psn_equals(&retry->psn, &new_psn), true);
    TEST_CHECK(retry->token, new_token);

    process_manager_cancel_discovery_retry(retry);
    TEST_CHECK(retry->active, false);
    TEST_CHECK(retry->token != new_token, true);
    memset(process_discovery_retries, 0, sizeof(process_discovery_retries));
});

TEST_FUNC(process_discovery_retry_registry_deduplicates_bounds_and_recovers_rejected_slots,
{
    memset(process_discovery_retries, 0, sizeof(process_discovery_retries));
    struct process_manager manager = {0};
    process_manager_test_schedule_discovery_callback = process_discovery_test_schedule;
    process_discovery_test_queue_accept = true;
    process_discovery_test_queue_calls = 0;

    ProcessSerialNumber first = { .highLongOfPSN = 10, .lowLongOfPSN = 20 };
    process_manager_schedule_discovery_retry(&manager, first, 200, 0);
    process_manager_schedule_discovery_retry(&manager, first, 200, 0);
    TEST_CHECK(process_discovery_test_active_count(), 1U);
    TEST_CHECK(process_discovery_test_queue_calls, 1U);

    ProcessSerialNumber same_pid_other_psn = { .highLongOfPSN = 11, .lowLongOfPSN = 21 };
    process_manager_schedule_discovery_retry(&manager, same_pid_other_psn, 200, 0);
    TEST_CHECK(process_discovery_test_active_count(), 2U);

    for (unsigned i = 2; i < PROCESS_DISCOVERY_RETRY_CAPACITY; ++i) {
        ProcessSerialNumber psn = { .highLongOfPSN = 100, .lowLongOfPSN = (UInt32) (1000 + i) };
        process_manager_schedule_discovery_retry(&manager, psn, (pid_t) (300 + i), 0);
    }
    TEST_CHECK(process_discovery_test_active_count(), PROCESS_DISCOVERY_RETRY_CAPACITY);
    unsigned queued_at_capacity = process_discovery_test_queue_calls;
    process_manager_schedule_discovery_retry(&manager,
        (ProcessSerialNumber) { .highLongOfPSN = 999, .lowLongOfPSN = 999 }, 999, 0);
    TEST_CHECK(process_discovery_test_queue_calls, queued_at_capacity);

    process_manager_cancel_discovery_retry(&process_discovery_retries[0]);
    process_discovery_test_queue_accept = false;
    process_manager_schedule_discovery_retry(&manager,
        (ProcessSerialNumber) { .highLongOfPSN = 12, .lowLongOfPSN = 22 }, 202, 0);
    TEST_CHECK(process_discovery_test_active_count(), PROCESS_DISCOVERY_RETRY_CAPACITY - 1);

    process_discovery_test_queue_accept = true;
    process_manager_schedule_discovery_retry(&manager,
        (ProcessSerialNumber) { .highLongOfPSN = 13, .lowLongOfPSN = 23 }, 203, 0);
    TEST_CHECK(process_discovery_test_active_count(), PROCESS_DISCOVERY_RETRY_CAPACITY);

    process_manager_test_schedule_discovery_callback = NULL;
    memset(process_discovery_retries, 0, sizeof(process_discovery_retries));
});
