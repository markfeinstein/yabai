#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#define PROCESS_EVENT_HANDLER(name) OSStatus name(EventHandlerCallRef ref, EventRef event, void *context)
typedef PROCESS_EVENT_HANDLER(process_event_handler);

struct process
{
    ProcessSerialNumber psn;
    pid_t pid;
    char *name;
    void *ns_application;
    int policy;
    uint8_t launch_retries;
    bool volatile terminated;
};

static inline bool process_is_terminated(struct process *process)
{
    return __atomic_load_n(&process->terminated, __ATOMIC_ACQUIRE);
}

static inline int process_policy_load(struct process *process)
{
    return __atomic_load_n(&process->policy, __ATOMIC_ACQUIRE);
}

static inline void process_policy_store(struct process *process, int policy)
{
    __atomic_store_n(&process->policy, policy, __ATOMIC_RELEASE);
}

static inline uint8_t process_launch_retries_load(struct process *process)
{
    return __atomic_load_n(&process->launch_retries, __ATOMIC_ACQUIRE);
}

static inline void process_launch_retries_reset(struct process *process)
{
    __atomic_store_n(&process->launch_retries, 0, __ATOMIC_RELEASE);
}

static inline bool process_launch_retry_reserve(struct process *process, uint8_t limit)
{
    uint8_t count = process_launch_retries_load(process);
    while (count < limit) {
        if (__atomic_compare_exchange_n(&process->launch_retries, &count, (uint8_t) (count + 1),
                                        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return true;
    }
    return false;
}

static inline bool process_launch_retries_reset_if_at_least(struct process *process, uint8_t limit)
{
    uint8_t count = process_launch_retries_load(process);
    while (count >= limit) {
        if (__atomic_compare_exchange_n(&process->launch_retries, &count, 0,
                                        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return true;
    }
    return false;
}

struct process_manager
{
    struct table process;
    EventTargetRef target;
    EventHandlerUPP handler;
    EventTypeSpec type[3];
    EventHandlerRef ref;
    pid_t front_pid;
    pid_t last_front_pid;
    ProcessSerialNumber front_psn;
    ProcessSerialNumber last_front_psn;
    EventTime switch_event_time;
    uint64_t volatile front_switch_generation;
    uint64_t volatile front_switch_psn;
    uint64_t volatile committed_front_switch_generation;
    ProcessSerialNumber finder_psn;
};

/* process_destroy accepts NULL and releases all resources owned by a process. */
void process_destroy(struct process *process);
struct process *process_manager_find_process(struct process_manager *pm, ProcessSerialNumber *psn);
bool process_manager_begin(struct process_manager *pm);

#endif
