extern struct event_loop g_event_loop;
extern void *g_workspace_context;

static TABLE_HASH_FUNC(hash_psn)
{
    return ((ProcessSerialNumber *) key)->lowLongOfPSN;
}

static TABLE_COMPARE_FUNC(compare_psn)
{
    return psn_equals(key_a, key_b);
}

static uint64_t process_manager_pack_psn(ProcessSerialNumber psn)
{
    return ((uint64_t)(uint32_t) psn.highLongOfPSN << 32) | (uint32_t) psn.lowLongOfPSN;
}

static uint64_t process_manager_advance_front_switch_generation(struct process_manager *pm)
{
    uint64_t generation = __atomic_add_fetch(&pm->front_switch_generation, 1, __ATOMIC_ACQ_REL);
    if (!generation) generation = __atomic_add_fetch(&pm->front_switch_generation, 1, __ATOMIC_ACQ_REL);
    return generation;
}

static uint64_t process_manager_publish_tracked_front_switch_locked(struct process_manager *pm, ProcessSerialNumber psn)
{
    uint64_t generation = process_manager_advance_front_switch_generation(pm);
    __atomic_store_n(&pm->front_switch_psn, process_manager_pack_psn(psn), __ATOMIC_RELEASE);
    return generation;
}

static void process_manager_invalidate_tracked_front_switch_if_matches(struct process_manager *pm, ProcessSerialNumber psn)
{
    uint64_t identity = process_manager_pack_psn(psn);

    pthread_mutex_lock(&g_process_manager_front_switch_lock);
    if (__atomic_load_n(&pm->front_switch_psn, __ATOMIC_ACQUIRE) == identity) {
        process_manager_advance_front_switch_generation(pm);
        __atomic_store_n(&pm->front_switch_psn, 0, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);
}

#ifdef TESTS
static char *(*process_create_cfstring_copy)(CFStringRef) = cfstring_copy;
#define PROCESS_CREATE_CFSTRING_COPY(value) process_create_cfstring_copy(value)
#else
#define PROCESS_CREATE_CFSTRING_COPY(value) cfstring_copy(value)
#endif

static const char *process_name_blacklist[] =
{
    "Übersicht",
    "Slack Helper (Plugin)",
    "Google Chrome Helper (Plugin)",
    "qlmanage",
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static inline pid_t process_pid_for_psn(ProcessSerialNumber psn)
{
    pid_t pid = 0;
    GetProcessPID(&psn, &pid);
    return pid;
}

static struct process *process_create(ProcessSerialNumber psn, pid_t pid)
{
    ProcessInfoRec process_info = { .processInfoLength = sizeof(ProcessInfoRec) };
    GetProcessInformation(&psn, &process_info);

    CFStringRef process_name_ref = NULL;
    CopyProcessName(&psn, &process_name_ref);

    if (!process_name_ref) {
        debug("%s: could not retrieve process name! ignoring..\n", __FUNCTION__);
        return NULL;
    }

    char *process_name = PROCESS_CREATE_CFSTRING_COPY(process_name_ref);
    CFRelease(process_name_ref);
    if (!process_name) {
        debug("%s: could not copy process name! ignoring..\n", __FUNCTION__);
        return NULL;
    }

    if (process_info.processType == 'XPC!') {
        debug("%s: xpc service '%s' detected! ignoring..\n", __FUNCTION__, process_name);
        free(process_name);
        return NULL;
    }

    for (int i = 0; i < array_count(process_name_blacklist); ++i) {
        if (string_equals(process_name, process_name_blacklist[i])) {
            debug("%s: %s is blacklisted! ignoring..\n", __FUNCTION__, process_name);
            free(process_name);
            return NULL;
        }
    }

    struct process *process = legacy_allocation_calloc(1, sizeof(struct process));
    if (!process) {
        free(process_name);
        return NULL;
    }
    process->psn = psn;
    process->pid = pid;
    process->name = process_name;
    __atomic_store_n(&process->terminated, false, __ATOMIC_RELEASE);
    __atomic_store_n(&process->ns_application, workspace_application_create_running_ns_application(process), __ATOMIC_RELEASE);
    return process;
}

#define PROCESS_DISCOVERY_RETRY_LIMIT 5
#define PROCESS_DISCOVERY_RETRY_CAPACITY 32

static bool process_is_being_debugged(pid_t pid);

struct process_discovery_retry
{
    struct process_manager *manager;
    ProcessSerialNumber psn;
    pid_t pid;
    unsigned attempt;
    uint64_t token;
    bool active;
};

static struct process_discovery_retry process_discovery_retries[PROCESS_DISCOVERY_RETRY_CAPACITY];

#ifdef TESTS
static bool (*process_manager_test_schedule_discovery_callback)(unsigned, uint64_t);
static struct process *(*process_manager_test_create_process)(ProcessSerialNumber, pid_t) = process_create;
static pid_t (*process_manager_test_pid_for_psn)(ProcessSerialNumber) = process_pid_for_psn;
static bool (*process_manager_test_is_debugged)(pid_t) = process_is_being_debugged;
static OSStatus (*process_manager_test_event_psn)(EventRef, ProcessSerialNumber *);
static UInt32 (*process_manager_test_event_kind)(EventRef);
static OSStatus (*process_manager_test_next_process)(ProcessSerialNumber *);
static void (*process_manager_test_destroy_observer)(struct process *);
#endif

static void process_manager_run_discovery_retry(unsigned index, uint64_t token);

static bool process_manager_queue_discovery_retry(unsigned index, uint64_t token)
{
#ifdef TESTS
    if (process_manager_test_schedule_discovery_callback) {
        return process_manager_test_schedule_discovery_callback(index, token);
    }
#endif
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1f * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        process_manager_run_discovery_retry(index, token);
    });
    return true;
}

static uint64_t process_manager_advance_discovery_token(struct process_discovery_retry *retry)
{
    if (!++retry->token) ++retry->token;
    return retry->token;
}

static void process_manager_cancel_discovery_retry(struct process_discovery_retry *retry)
{
    retry->active = false;
    process_manager_advance_discovery_token(retry);
}

static void process_manager_run_discovery_retry(unsigned index, uint64_t token)
{
    struct process_discovery_retry *retry = &process_discovery_retries[index];
    if (!retry->active || retry->token != token) return;
    ProcessSerialNumber psn = retry->psn;
#ifdef TESTS
    pid_t current_pid = process_manager_test_pid_for_psn(psn);
#else
    pid_t current_pid = process_pid_for_psn(psn);
#endif
    if (process_manager_find_process(retry->manager, &psn) || current_pid != retry->pid) {
        process_manager_cancel_discovery_retry(retry);
        return;
    }

#ifdef TESTS
    struct process *process = process_manager_test_create_process(psn, retry->pid);
#else
    struct process *process = process_create(psn, retry->pid);
#endif
    if (process && table_try_add(&retry->manager->process, &process->psn, process)) {
        process_manager_cancel_discovery_retry(retry);
        event_loop_post_application_launched(process);
        return;
    }
    if (process) process_destroy(process);
    if (++retry->attempt >= PROCESS_DISCOVERY_RETRY_LIMIT) {
        debug("%s: exhausted process discovery retry for pid %d; a later process/front event may restart recovery\n",
              __FUNCTION__, retry->pid);
        process_manager_cancel_discovery_retry(retry);
        return;
    }
    if (!process_manager_queue_discovery_retry(index, token)) {
        process_manager_cancel_discovery_retry(retry);
    }
}

static void process_manager_schedule_discovery_retry(struct process_manager *pm, ProcessSerialNumber psn,
                                                     pid_t pid, unsigned attempt)
{
    if (!pm || !pid) return;
    for (unsigned i = 0; i < PROCESS_DISCOVERY_RETRY_CAPACITY; ++i) {
        if (process_discovery_retries[i].active && psn_equals(&process_discovery_retries[i].psn, &psn)) return;
    }
    for (unsigned i = 0; i < PROCESS_DISCOVERY_RETRY_CAPACITY; ++i) {
        struct process_discovery_retry *retry = &process_discovery_retries[i];
        if (retry->active) continue;
        uint64_t token = process_manager_advance_discovery_token(retry);
        retry->manager = pm;
        retry->psn = psn;
        retry->pid = pid;
        retry->attempt = attempt;
        retry->active = true;
        if (!process_manager_queue_discovery_retry(i, token)) {
            process_manager_cancel_discovery_retry(retry);
        }
        return;
    }
    debug("%s: process discovery retry capacity exhausted for pid %d\n", __FUNCTION__, pid);
}

static bool process_is_being_debugged(pid_t pid)
{
    struct kinfo_proc info;
    info.kp_proc.p_flag = 0;

    size_t size = sizeof(info);
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };

    sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    return ((info.kp_proc.p_flag & P_TRACED) != 0);
}

uint64_t process_manager_active_space_for_psn(int connection)
{
    uint64_t sid = 0;

    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return sid;

    int space_count = 0;
    uint64_t *space_list = NULL;

    for (int i = 0; i < display_count; ++i) {
        int count;
        uint64_t *list = display_space_list(display_list[i], &count);
        if (!list) continue;

        //
        // NOTE(asmvik): display_space_list(..) uses a linear allocator,
        // and so we only need to track the beginning of the first list along
        // with the total number of windows that have been allocated.
        //

        if (!space_list) space_list = list;
        space_count += count;
    }

    if (!space_list || !space_count) return sid;

    uint64_t set_tags = 0;
    uint64_t clear_tags = 0;
    uint32_t options = 0x2;

    CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list, sizeof(uint64_t), space_count, kCFNumberSInt64Type);
    if (!space_list_ref) return sid;

    CFArrayRef window_list_ref = SLSCopyWindowsWithOptionsAndTags(g_connection, connection, space_list_ref, options, &set_tags, &clear_tags);
    if (!window_list_ref) goto err;

    int count = CFArrayGetCount(window_list_ref);
    if (!count) goto out;

    CFTypeRef query = SLSWindowQueryWindows(g_connection, window_list_ref, count);
    if (!query) goto out;

    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (!iterator) goto query_err;

    while (SLSWindowIteratorAdvance(iterator)) {
        uint64_t tags = SLSWindowIteratorGetTags(iterator);
        uint64_t attributes = SLSWindowIteratorGetAttributes(iterator);
        uint32_t parent_wid = SLSWindowIteratorGetParentID(iterator);
        uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
        int level = SLSWindowIteratorGetLevel(iterator);

        if (parent_wid == 0) {
            if (level == 0 || level == 3 || level == 8) {
                if (((attributes & 0x2) || (tags & 0x400000000000000)) && (((tags & 0x1)) || ((tags & 0x2) && (tags & 0x80000000)))) {
                    sid = window_space(wid);
                    break;
                }
            }
        }
    }

    CFRelease(iterator);
query_err:
    CFRelease(query);
out:
    CFRelease(window_list_ref);
err:
    CFRelease(space_list_ref);
    return sid;
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static PROCESS_EVENT_HANDLER(process_handler)
{
    struct process_manager *pm = context;

    ProcessSerialNumber psn;
#ifdef TESTS
    OSStatus event_status = process_manager_test_event_psn
                         ? process_manager_test_event_psn(event, &psn)
                         : GetEventParameter(event, kEventParamProcessID, typeProcessSerialNumber,
                                             NULL, sizeof(psn), NULL, &psn);
#else
    OSStatus event_status = GetEventParameter(event, kEventParamProcessID, typeProcessSerialNumber,
                                              NULL, sizeof(psn), NULL, &psn);
#endif
    if (event_status != noErr) return -1;

#ifdef TESTS
    UInt32 event_kind = process_manager_test_event_kind ? process_manager_test_event_kind(event) : GetEventKind(event);
#else
    UInt32 event_kind = GetEventKind(event);
#endif
    switch (event_kind) {
    case kEventAppLaunched: {
        if (process_manager_find_process(pm, &psn)) {

            //
            // NOTE(asmvik): Some garbage applications (e.g Steam) are reported twice with the same PID and PSN for some hecking reason.
            // It is by definition NOT possible for two processes to exist at the same time with the same PID and PSN.
            // If we detect such a scenario we simply discard the dupe notification..
            //

            return noErr;
        }

#ifdef TESTS
        pid_t pid = process_manager_test_pid_for_psn(psn);
        if (process_manager_test_is_debugged(pid)) {
#else
        pid_t pid = process_pid_for_psn(psn);
        if (process_is_being_debugged(pid)) {
#endif
            debug("%s: process with pid %d is running under a debugger! ignoring..\n", __FUNCTION__, pid);
            return noErr;
        }

#ifdef TESTS
        struct process *process = process_manager_test_create_process(psn, pid);
#else
        struct process *process = process_create(psn, pid);
#endif
        if (!process) {
            process_manager_schedule_discovery_retry(pm, psn, pid, 0);
            return noErr;
        }

        if (!table_try_add(&pm->process, &process->psn, process)) {
            process_destroy(process);
            process_manager_schedule_discovery_retry(pm, psn, pid, 0);
            return noErr;
        }
        event_loop_post_application_launched(process);
    } break;
    case kEventAppTerminated: {
        for (unsigned i = 0; i < PROCESS_DISCOVERY_RETRY_CAPACITY; ++i) {
            if (process_discovery_retries[i].active && psn_equals(&process_discovery_retries[i].psn, &psn)) {
                process_manager_cancel_discovery_retry(&process_discovery_retries[i]);
            }
        }
        struct process *process = process_manager_find_process(pm, &psn);
        if (!process) return noErr;

        process_manager_invalidate_tracked_front_switch_if_matches(pm, psn);
        __atomic_store_n(&process->terminated, true, __ATOMIC_RELEASE);
        table_remove(&pm->process, &psn);
        workspace_application_unobserve(g_workspace_context, process);
        __asm__ __volatile__ ("" ::: "memory");

        event_loop_post(&g_event_loop, APPLICATION_TERMINATED, process, 0);
    } break;
    case kEventAppFrontSwitched: {
        struct process *process = process_manager_find_process(pm, &psn);
        if (!process) {
            pid_t pid = process_pid_for_psn(psn);
            if (pid) process_manager_schedule_discovery_retry(pm, psn, pid, 0);
            return noErr;
        }

        if (process->launch_retries >= 5) {
            process->launch_retries = 0;
            event_loop_post_application_launched(process);
        }
        event_loop_post_tracked_application_front_switched(process);
    } break;
    }

    return noErr;
}
#pragma clang diagnostic pop

static void process_manager_add_running_processes(struct process_manager *pm)
{
    ProcessSerialNumber psn = { kNoProcess, kNoProcess };
#ifdef TESTS
    while ((process_manager_test_next_process ? process_manager_test_next_process(&psn) : GetNextProcess(&psn)) == noErr) {
        pid_t pid = process_manager_test_pid_for_psn(psn);
        if (process_manager_test_is_debugged(pid)) {
#else
    while (GetNextProcess(&psn) == noErr) {
        pid_t pid = process_pid_for_psn(psn);
        if (process_is_being_debugged(pid)) {
#endif
            debug("%s: process with pid %d is running under a debugger! ignoring..\n", __FUNCTION__, pid);
            continue;
        }

#ifdef TESTS
        struct process *process = process_manager_test_create_process(psn, pid);
#else
        struct process *process = process_create(psn, pid);
#endif
        if (!process) {
            process_manager_schedule_discovery_retry(pm, psn, pid, 0);
            continue;
        }

        if (string_equals(process->name, "Finder")) {
            debug("%s: %s (%d) was found! caching psn..\n", __FUNCTION__, process->name, process->pid);
            pm->finder_psn = psn;
        }

        if (!table_try_add(&pm->process, &process->psn, process)) {
            process_destroy(process);
            process_manager_schedule_discovery_retry(pm, psn, pid, 0);
        }
    }
}

bool process_manager_begin(struct process_manager *pm)
{
    pm->target = GetApplicationEventTarget();
    pm->handler = NewEventHandlerUPP(process_handler);
    pm->type[0].eventClass = kEventClassApplication;
    pm->type[0].eventKind  = kEventAppLaunched;
    pm->type[1].eventClass = kEventClassApplication;
    pm->type[1].eventKind  = kEventAppTerminated;
    pm->type[2].eventClass = kEventClassApplication;
    pm->type[2].eventKind  = kEventAppFrontSwitched;
    if (!table_try_init(&pm->process, 125, hash_psn, compare_psn)) return false;
    __atomic_store_n(&pm->front_switch_generation, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&pm->front_switch_psn, 0, __ATOMIC_RELEASE);

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    process_manager_add_running_processes(pm);
    [pool drain];

    ProcessSerialNumber front_psn;
    _SLPSGetFrontProcess(&front_psn);
    GetProcessPID(&front_psn, &pm->front_pid);
    pm->front_psn = front_psn;
    pm->last_front_pid = pm->front_pid;
    pm->last_front_psn = front_psn;
    pm->switch_event_time = GetCurrentEventTime();
    return InstallEventHandler(pm->target, pm->handler, 3, pm->type, pm, &pm->ref) == noErr;
}
#pragma clang diagnostic pop

struct process *process_manager_find_process(struct process_manager *pm, ProcessSerialNumber *psn)
{
    return table_find(&pm->process, psn);
}

void process_destroy(struct process *process)
{
    if (!process) return;
#ifdef TESTS
    if (process_manager_test_destroy_observer) process_manager_test_destroy_observer(process);
#endif
    workspace_application_destroy_running_ns_application(g_workspace_context, process);
    free(process->name);
    legacy_allocation_free(process);
}
