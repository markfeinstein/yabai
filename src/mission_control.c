extern struct event_loop g_event_loop;
extern enum mission_control_mode g_mission_control_mode;
extern volatile uint64_t __last_cmd_tab_time;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static CONNECTION_CALLBACK(connection_handler)
{
    if (type == 1204) {
        event_loop_post(&g_event_loop, MISSION_CONTROL_ENTER, NULL, 0);
    } else if (type == 1327) {
        uint64_t sid; memcpy(&sid, data, sizeof(uint64_t));
        event_loop_post(&g_event_loop, SLS_SPACE_CREATED, (void *) (intptr_t) sid , 0);
    } else if (type == 1328) {
        uint64_t sid; memcpy(&sid, data, sizeof(uint64_t));
        event_loop_post(&g_event_loop, SLS_SPACE_DESTROYED, (void *) (intptr_t) sid, 0);
    } else if (type == 808) {
        uint32_t wid; memcpy(&wid, data, sizeof(uint32_t));
        event_loop_post(&g_event_loop, SLS_WINDOW_ORDERED, (void *) (intptr_t) wid, 0);
    } else if (type == 815) {
        uint32_t wid; memcpy(&wid, data, sizeof(uint32_t));
        event_loop_post(&g_event_loop, SLS_WINDOW_IS_VISIBLE, (void *) (intptr_t) wid, 0);
    } else if (type == 816) {
        uint32_t wid; memcpy(&wid, data, sizeof(uint32_t));
        event_loop_post(&g_event_loop, SLS_WINDOW_IS_INVISIBLE, (void *) (intptr_t) wid, 0);
    } else if (type == 804) {
        uint32_t wid; memcpy(&wid, data, sizeof(uint32_t));
        event_loop_post(&g_event_loop, SLS_WINDOW_DESTROYED, (void *) (intptr_t) wid, 0);
    } else if (type == 1202) {
        __atomic_store_n(&__last_cmd_tab_time, read_os_timer(), __ATOMIC_RELEASE);
    }
}
#pragma clang diagnostic pop

enum mission_control_mode
{
    MISSION_CONTROL_MODE_INACTIVE           = 0,
    MISSION_CONTROL_MODE_SHOW               = 1,
    MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS   = 2,
    MISSION_CONTROL_MODE_SHOW_FRONT_WINDOWS = 3,
    MISSION_CONTROL_MODE_SHOW_DESKTOP       = 4
};

static const char *mission_control_mode_str[] = {
    [MISSION_CONTROL_MODE_INACTIVE]           = "inactive",
    [MISSION_CONTROL_MODE_SHOW]               = "show",
    [MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS]   = "show-all-windows",
    [MISSION_CONTROL_MODE_SHOW_FRONT_WINDOWS] = "show-front-windows",
    [MISSION_CONTROL_MODE_SHOW_DESKTOP]       = "show-desktop"
};

enum mission_control_notification_kind {
    MISSION_CONTROL_NOTIFICATION_SHOW_ALL,
    MISSION_CONTROL_NOTIFICATION_SHOW_FRONT,
    MISSION_CONTROL_NOTIFICATION_SHOW_DESKTOP,
    MISSION_CONTROL_NOTIFICATION_EXIT,
    MISSION_CONTROL_NOTIFICATION_COUNT
};

struct mission_control_event_context {
    uint64_t epoch;
    uint64_t token;
    uint64_t event_loop_generation;
    enum mission_control_notification_kind kind;
};

struct mission_control_ingress_item {
    struct mission_control_ingress_item *next;
    uint64_t epoch;
    uint64_t token;
    uint64_t event_loop_generation;
    unsigned retries;
    enum mission_control_notification_kind kind;
};

static struct mission_control_observer {
    AXUIElementRef ref;
    AXObserverRef observer_ref;
    bool is_observing;
    volatile uint64_t epoch;
    volatile uint64_t notification_token;
} g_mission_control_observer;

static volatile uint64_t g_mission_control_observer_epoch;
static volatile uint64_t g_mission_control_notification_token;
static volatile uint64_t g_mission_control_observe_retry_generation;
static volatile bool g_mission_control_observe_retry_pending;
static pthread_mutex_t g_mission_control_observer_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_mission_control_accepting;
static uint64_t g_mission_control_lifetime_generation;
static dispatch_queue_t g_mission_control_ingress_queue;
static struct mission_control_ingress_item *g_mission_control_ingress_head;
static struct mission_control_ingress_item *g_mission_control_ingress_tail;
static bool g_mission_control_ingress_retry_pending;

struct mission_control_fallback_entry {
    uint64_t sequence;
    uint64_t epoch;
    uint64_t event_loop_generation;
    enum mission_control_notification_kind kind;
};

#define MISSION_CONTROL_FALLBACK_CAPACITY 32
static struct mission_control_fallback_queue {
    struct mission_control_fallback_entry entries[MISSION_CONTROL_FALLBACK_CAPACITY];
    uint64_t next_sequence;
    uint32_t head;
    uint32_t count;
    unsigned retries;
    bool drain_scheduled;
} g_mission_control_fallback;

static uint64_t mission_control_next_nonzero(volatile uint64_t *value)
{
    uint64_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    for (;;) {
        uint64_t next = current + 1;
        if (!next) next = 1;
        if (__atomic_compare_exchange_n(value, &current, next, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return next;
    }
}

static CFStringRef mission_control_notifications[] = {
    CFSTR("AXExposeShowAllWindows"),
    CFSTR("AXExposeShowFrontWindows"),
    CFSTR("AXExposeShowDesktop"),
    CFSTR("AXExposeExit")
};

#ifdef TESTS
struct mission_control_observer_operations {
    pid_t (*dock_pid)(void);
    AXUIElementRef (*application_create)(pid_t pid);
    AXError (*observer_create)(pid_t pid, AXObserverCallback callback, AXObserverRef *observer);
    AXError (*notification_add)(AXObserverRef observer, AXUIElementRef element, CFStringRef notification, void *context);
    AXError (*notification_remove)(AXObserverRef observer, AXUIElementRef element, CFStringRef notification);
    CFRunLoopSourceRef (*observer_source)(AXObserverRef observer);
    CFRunLoopRef (*main_run_loop)(void);
    void (*run_loop_add_source)(CFRunLoopRef run_loop, CFRunLoopSourceRef source, CFRunLoopMode mode);
    void (*source_invalidate)(CFRunLoopSourceRef source);
    void (*release)(CFTypeRef ref);
};

static bool (*mission_control_test_post)(enum event_type, void *, int);
static void (*mission_control_test_ingress_submit)(struct mission_control_ingress_item *);
static void (*mission_control_test_ingress_retry)(double);
static void (*mission_control_test_observe_retry)(double);
static void (*mission_control_test_retry_before_lock)(void);
static void (*mission_control_test_ensure_before_lock)(void);
static bool mission_control_test_fail_ingress_allocation;
static bool mission_control_test_fail_event_allocation;
static void (*mission_control_test_fallback_schedule)(double);
static void (*mission_control_test_fallback_before_allocate)(void);
static int mission_control_test_ingress_allocated;
static int mission_control_test_ingress_freed;
static int mission_control_test_event_allocated;
static int mission_control_test_event_freed;

static struct mission_control_observer_operations g_mission_control_observer_operations = {
    workspace_get_dock_pid, AXUIElementCreateApplication, AXObserverCreate,
    AXObserverAddNotification, AXObserverRemoveNotification, AXObserverGetRunLoopSource,
    CFRunLoopGetMain, CFRunLoopAddSource, CFRunLoopSourceInvalidate, CFRelease
};
#define MC_OP(name) g_mission_control_observer_operations.name
#else
#define MC_OP_dock_pid workspace_get_dock_pid
#define MC_OP_application_create AXUIElementCreateApplication
#define MC_OP_observer_create AXObserverCreate
#define MC_OP_notification_add AXObserverAddNotification
#define MC_OP_notification_remove AXObserverRemoveNotification
#define MC_OP_observer_source AXObserverGetRunLoopSource
#define MC_OP_main_run_loop CFRunLoopGetMain
#define MC_OP_run_loop_add_source CFRunLoopAddSource
#define MC_OP_source_invalidate CFRunLoopSourceInvalidate
#define MC_OP_release CFRelease
#define MC_OP(name) MC_OP_##name
#endif

static dispatch_queue_t mission_control_ingress_queue(void)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        g_mission_control_ingress_queue = dispatch_queue_create("com.koekeishiya.yabai.mission-control", DISPATCH_QUEUE_SERIAL);
    });
    return g_mission_control_ingress_queue;
}

void mission_control_event_context_destroy(void *context);

static struct mission_control_ingress_item *mission_control_ingress_item_create(void)
{
#ifdef TESTS
    if (mission_control_test_fail_ingress_allocation) return NULL;
#endif
    struct mission_control_ingress_item *item = calloc(1, sizeof(*item));
#ifdef TESTS
    if (item) ++mission_control_test_ingress_allocated;
#endif
    return item;
}

static void mission_control_ingress_item_destroy(struct mission_control_ingress_item *item)
{
    if (!item) return;
#ifdef TESTS
    ++mission_control_test_ingress_freed;
#endif
    free(item);
}

static struct mission_control_event_context *mission_control_event_context_create(void)
{
#ifdef TESTS
    if (mission_control_test_fail_event_allocation) return NULL;
#endif
    struct mission_control_event_context *context = malloc(sizeof(*context));
#ifdef TESTS
    if (context) ++mission_control_test_event_allocated;
#endif
    return context;
}

static bool mission_control_post(enum event_type type, void *context, int param1)
{
#ifdef TESTS
    if (mission_control_test_post) return mission_control_test_post(type, context, param1);
#endif
    return event_loop_try_post_generation(&g_event_loop,
                                          ((struct mission_control_event_context *) context)->event_loop_generation,
                                          type, context, param1);
}

static enum event_type mission_control_event_type(enum mission_control_notification_kind kind)
{
    switch (kind) {
    case MISSION_CONTROL_NOTIFICATION_SHOW_ALL: return MISSION_CONTROL_SHOW_ALL_WINDOWS;
    case MISSION_CONTROL_NOTIFICATION_SHOW_FRONT: return MISSION_CONTROL_SHOW_FRONT_WINDOWS;
    case MISSION_CONTROL_NOTIFICATION_SHOW_DESKTOP: return MISSION_CONTROL_SHOW_DESKTOP;
    case MISSION_CONTROL_NOTIFICATION_EXIT: return MISSION_CONTROL_EXIT;
    default: return MISSION_CONTROL_EXIT;
    }
}

static void mission_control_ingress_process(void);

static void mission_control_ingress_retry_after(double delay)
{
#ifdef TESTS
    if (mission_control_test_ingress_retry) {
        mission_control_test_ingress_retry(delay);
        return;
    }
#endif
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (delay * NSEC_PER_SEC)),
                   mission_control_ingress_queue(), ^{
        g_mission_control_ingress_retry_pending = false;
        mission_control_ingress_process();
    });
}

static void mission_control_ingress_process(void)
{
    while (g_mission_control_ingress_head) {
        struct mission_control_ingress_item *item = g_mission_control_ingress_head;
        uint64_t epoch = __atomic_load_n(&g_mission_control_observer.epoch, __ATOMIC_ACQUIRE);
        uint64_t loop_generation = event_loop_generation_snapshot(&g_event_loop);
        if (!item->epoch || item->epoch != epoch ||
            (item->event_loop_generation && item->event_loop_generation != loop_generation) ||
            !__atomic_load_n(&g_event_loop.is_running, __ATOMIC_ACQUIRE)) {
            g_mission_control_ingress_head = item->next;
            if (!g_mission_control_ingress_head) g_mission_control_ingress_tail = NULL;
            mission_control_ingress_item_destroy(item);
            continue;
        }

        if (!item->token && item->kind != MISSION_CONTROL_NOTIFICATION_EXIT) {
            item->token = mission_control_next_nonzero(&g_mission_control_notification_token);
        } else if (item->kind == MISSION_CONTROL_NOTIFICATION_EXIT) {
            item->token = __atomic_load_n(&g_mission_control_observer.notification_token, __ATOMIC_ACQUIRE);
            if (!item->token) {
                g_mission_control_ingress_head = item->next;
                if (!g_mission_control_ingress_head) g_mission_control_ingress_tail = NULL;
                mission_control_ingress_item_destroy(item);
                continue;
            }
        }

        struct mission_control_event_context *context = mission_control_event_context_create();
        if (!context) goto retry;
        *context = (struct mission_control_event_context) {
            .epoch = item->epoch, .token = item->token,
            .event_loop_generation = item->event_loop_generation, .kind = item->kind
        };

        pthread_mutex_lock(&g_mission_control_observer_lock);
        if (item->epoch != __atomic_load_n(&g_mission_control_observer.epoch, __ATOMIC_ACQUIRE)) {
            pthread_mutex_unlock(&g_mission_control_observer_lock);
            mission_control_event_context_destroy(context);
            g_mission_control_ingress_head = item->next;
            if (!g_mission_control_ingress_head) g_mission_control_ingress_tail = NULL;
            mission_control_ingress_item_destroy(item);
            continue;
        }
        uint64_t previous = __atomic_load_n(&g_mission_control_observer.notification_token, __ATOMIC_ACQUIRE);
        if (item->kind != MISSION_CONTROL_NOTIFICATION_EXIT) {
            __atomic_store_n(&g_mission_control_observer.notification_token, item->token, __ATOMIC_RELEASE);
        }
        if (!mission_control_post(mission_control_event_type(item->kind), context, 1)) {
            if (item->kind != MISSION_CONTROL_NOTIFICATION_EXIT) {
                __atomic_store_n(&g_mission_control_observer.notification_token, previous, __ATOMIC_RELEASE);
            }
#ifdef TESTS
            if (mission_control_test_post) mission_control_event_context_destroy(context);
#endif
            pthread_mutex_unlock(&g_mission_control_observer_lock);
            goto retry;
        }
        pthread_mutex_unlock(&g_mission_control_observer_lock);

        g_mission_control_ingress_head = item->next;
        if (!g_mission_control_ingress_head) g_mission_control_ingress_tail = NULL;
        mission_control_ingress_item_destroy(item);
        continue;

retry:
        if (!g_mission_control_ingress_retry_pending) {
            g_mission_control_ingress_retry_pending = true;
            double delay = item->retries++ < 9 ? 0.1 : 1.0;
            mission_control_ingress_retry_after(delay);
        }
        return;
    }
}

static void mission_control_ingress_append(struct mission_control_ingress_item *item)
{
    item->next = NULL;
    if (g_mission_control_ingress_tail) g_mission_control_ingress_tail->next = item;
    else g_mission_control_ingress_head = item;
    g_mission_control_ingress_tail = item;
    mission_control_ingress_process();
}

static void mission_control_ingress_submit(struct mission_control_ingress_item *item)
{
#ifdef TESTS
    if (mission_control_test_ingress_submit) {
        mission_control_test_ingress_submit(item);
        return;
    }
    mission_control_ingress_append(item);
#else
    dispatch_async(mission_control_ingress_queue(), ^{ mission_control_ingress_append(item); });
#endif
}

static void mission_control_fallback_drain(void *context);

static void mission_control_fallback_schedule(double delay)
{
#ifdef TESTS
    if (mission_control_test_fallback_schedule) {
        mission_control_test_fallback_schedule(delay);
        return;
    }
#endif
    if (delay == 0.0) {
        dispatch_async_f(mission_control_ingress_queue(), NULL, mission_control_fallback_drain);
    } else {
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (delay * NSEC_PER_SEC)),
                         mission_control_ingress_queue(), NULL, mission_control_fallback_drain);
    }
}

static void mission_control_fallback_cancel_locked(void)
{
    g_mission_control_fallback.head = 0;
    g_mission_control_fallback.count = 0;
    g_mission_control_fallback.retries = 0;
    g_mission_control_fallback.drain_scheduled = false;
}

static void mission_control_fallback_pop_locked(void)
{
    if (!g_mission_control_fallback.count) return;
    g_mission_control_fallback.head = (g_mission_control_fallback.head + 1) % MISSION_CONTROL_FALLBACK_CAPACITY;
    --g_mission_control_fallback.count;
}

static void mission_control_fallback_append_locked(uint64_t epoch, uint64_t event_loop_generation,
                                                   enum mission_control_notification_kind kind)
{
    uint32_t tail = (g_mission_control_fallback.head + g_mission_control_fallback.count) %
                    MISSION_CONTROL_FALLBACK_CAPACITY;
    uint64_t sequence = ++g_mission_control_fallback.next_sequence;
    if (!sequence) sequence = ++g_mission_control_fallback.next_sequence;
    g_mission_control_fallback.entries[tail] = (struct mission_control_fallback_entry) {
        .sequence = sequence, .epoch = epoch, .event_loop_generation = event_loop_generation, .kind = kind
    };
    ++g_mission_control_fallback.count;
}

static void mission_control_fallback_drain(void *context)
{
    (void) context;
    for (;;) {
        pthread_mutex_lock(&g_mission_control_observer_lock);
        if (!g_mission_control_fallback.drain_scheduled || !g_mission_control_fallback.count) {
            g_mission_control_fallback.drain_scheduled = false;
            pthread_mutex_unlock(&g_mission_control_observer_lock);
            return;
        }
        struct mission_control_fallback_entry entry =
            g_mission_control_fallback.entries[g_mission_control_fallback.head];
        bool valid = g_mission_control_accepting &&
                     entry.epoch == __atomic_load_n(&g_mission_control_observer.epoch, __ATOMIC_ACQUIRE) &&
                     entry.event_loop_generation == g_mission_control_lifetime_generation &&
                     entry.event_loop_generation == event_loop_generation_snapshot(&g_event_loop);
        if (!valid) {
            mission_control_fallback_pop_locked();
            pthread_mutex_unlock(&g_mission_control_observer_lock);
            continue;
        }
#ifdef TESTS
        if (mission_control_test_fallback_before_allocate) mission_control_test_fallback_before_allocate();
#endif
        struct mission_control_ingress_item *item = mission_control_ingress_item_create();
        if (!item) {
            double delay = g_mission_control_fallback.retries++ < 9 ? 0.1 : 1.0;
            pthread_mutex_unlock(&g_mission_control_observer_lock);
            mission_control_fallback_schedule(delay);
            return;
        }
        item->epoch = entry.epoch;
        item->kind = entry.kind;
        item->event_loop_generation = entry.event_loop_generation;
        pthread_mutex_unlock(&g_mission_control_observer_lock);

        mission_control_ingress_append(item);

        pthread_mutex_lock(&g_mission_control_observer_lock);
        if (g_mission_control_fallback.count &&
            g_mission_control_fallback.entries[g_mission_control_fallback.head].sequence == entry.sequence) {
            mission_control_fallback_pop_locked();
            g_mission_control_fallback.retries = 0;
        }
        pthread_mutex_unlock(&g_mission_control_observer_lock);
    }
}

static void mission_control_fallback_record(uint64_t epoch, uint64_t event_loop_generation,
                                            enum mission_control_notification_kind kind)
{
    bool schedule = false;
    pthread_mutex_lock(&g_mission_control_observer_lock);
    if (!g_mission_control_accepting || epoch == 0 ||
        epoch != __atomic_load_n(&g_mission_control_observer.epoch, __ATOMIC_ACQUIRE) ||
        event_loop_generation != g_mission_control_lifetime_generation) {
        pthread_mutex_unlock(&g_mission_control_observer_lock);
        return;
    }

    bool overflow = g_mission_control_fallback.count == MISSION_CONTROL_FALLBACK_CAPACITY;
    uint32_t needed = overflow && kind != MISSION_CONTROL_NOTIFICATION_EXIT ? 2 : 1;
    while (g_mission_control_fallback.count > MISSION_CONTROL_FALLBACK_CAPACITY - needed) {
        mission_control_fallback_pop_locked();
    }
    mission_control_fallback_append_locked(epoch, event_loop_generation, kind);
    if (overflow && kind != MISSION_CONTROL_NOTIFICATION_EXIT) {
        // Dropping the oldest entry is bounded; a trailing exit forces one poll
        // confirmation after the newest mode so no potentially lost exit can strand it.
        mission_control_fallback_append_locked(epoch, event_loop_generation, MISSION_CONTROL_NOTIFICATION_EXIT);
    }
    if (!g_mission_control_fallback.drain_scheduled) {
        g_mission_control_fallback.drain_scheduled = true;
        schedule = true;
    }
    pthread_mutex_unlock(&g_mission_control_observer_lock);
    if (schedule) mission_control_fallback_schedule(0.0);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static OBSERVER_CALLBACK(mission_control_notification_handler)
{
    enum mission_control_notification_kind kind;
    if (CFEqual(notification, mission_control_notifications[0])) kind = MISSION_CONTROL_NOTIFICATION_SHOW_ALL;
    else if (CFEqual(notification, mission_control_notifications[1])) kind = MISSION_CONTROL_NOTIFICATION_SHOW_FRONT;
    else if (CFEqual(notification, mission_control_notifications[2])) kind = MISSION_CONTROL_NOTIFICATION_SHOW_DESKTOP;
    else if (CFEqual(notification, mission_control_notifications[3])) kind = MISSION_CONTROL_NOTIFICATION_EXIT;
    else return;

    uint64_t epoch = (uint64_t) (uintptr_t) context;
    uint64_t event_loop_generation = event_loop_generation_snapshot(&g_event_loop);
    struct mission_control_ingress_item *item = mission_control_ingress_item_create();
    if (!item) {
        mission_control_fallback_record(epoch, event_loop_generation, kind);
        return;
    }
    item->epoch = epoch;
    item->kind = kind;
    item->event_loop_generation = event_loop_generation;
    mission_control_ingress_submit(item);
}
#pragma clang diagnostic pop

static bool mission_control_observe_locked(void)
{
    if (!g_mission_control_accepting ||
        g_mission_control_lifetime_generation != event_loop_generation_snapshot(&g_event_loop)) return false;
    if (g_mission_control_observer.is_observing) return true;
    pid_t pid = MC_OP(dock_pid)();
    if (!pid) return false;
    AXUIElementRef ref = MC_OP(application_create)(pid);
    if (!ref) return false;

    AXObserverRef observer_ref = NULL;
    CFRunLoopSourceRef source = NULL;
    CFRunLoopRef run_loop = NULL;
    int registered_count = 0;
    uint64_t epoch = mission_control_next_nonzero(&g_mission_control_observer_epoch);
    AXError observer_result = MC_OP(observer_create)(pid, mission_control_notification_handler, &observer_ref);
    if (observer_result != kAXErrorSuccess || !observer_ref) goto fail;
    source = MC_OP(observer_source)(observer_ref);
    run_loop = MC_OP(main_run_loop)();
    if (!source || !run_loop) goto fail;
    for (int i = 0; i < array_count(mission_control_notifications); ++i) {
        AXError result = MC_OP(notification_add)(observer_ref, ref, mission_control_notifications[i], (void *) (uintptr_t) epoch);
        if (result != kAXErrorSuccess && result != kAXErrorNotificationAlreadyRegistered) goto fail;
        ++registered_count;
    }

    g_mission_control_observer.ref = ref;
    g_mission_control_observer.observer_ref = observer_ref;
    __atomic_store_n(&g_mission_control_observer.is_observing, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observer.notification_token, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observer.epoch, epoch, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observe_retry_pending, false, __ATOMIC_RELEASE);
    MC_OP(run_loop_add_source)(run_loop, source, kCFRunLoopDefaultMode);
    return true;

fail:
    for (int i = registered_count - 1; i >= 0; --i) {
        (void) MC_OP(notification_remove)(observer_ref, ref, mission_control_notifications[i]);
    }
    if (observer_ref) {
        if (!source) source = MC_OP(observer_source)(observer_ref);
        if (source) MC_OP(source_invalidate)(source);
        MC_OP(release)(observer_ref);
    }
    MC_OP(release)(ref);
    return false;
}

void mission_control_admission_open(uint64_t event_loop_generation)
{
    pthread_mutex_lock(&g_mission_control_observer_lock);
    g_mission_control_accepting = event_loop_generation != 0;
    g_mission_control_lifetime_generation = event_loop_generation;
    mission_control_fallback_cancel_locked();
    mission_control_next_nonzero(&g_mission_control_observe_retry_generation);
    __atomic_store_n(&g_mission_control_observe_retry_pending, false, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_mission_control_observer_lock);
}

bool mission_control_observe(void)
{
    pthread_mutex_lock(&g_mission_control_observer_lock);
    bool result = mission_control_observe_locked();
    pthread_mutex_unlock(&g_mission_control_observer_lock);
    return result;
}

bool mission_control_is_observing(void)
{
    return __atomic_load_n(&g_mission_control_observer.is_observing, __ATOMIC_ACQUIRE);
}

static void mission_control_observe_retry_attempt(uint64_t generation, unsigned attempt);

static void mission_control_observe_retry_after(uint64_t generation, unsigned attempt, double delay)
{
#ifdef TESTS
    if (mission_control_test_observe_retry) {
        mission_control_test_observe_retry(delay);
        return;
    }
#endif
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        mission_control_observe_retry_attempt(generation, attempt);
    });
}

static void mission_control_observe_retry_attempt(uint64_t generation, unsigned attempt)
{
#ifdef TESTS
    if (mission_control_test_retry_before_lock) mission_control_test_retry_before_lock();
#endif
    pthread_mutex_lock(&g_mission_control_observer_lock);
    if (!g_mission_control_accepting ||
        generation != __atomic_load_n(&g_mission_control_observe_retry_generation, __ATOMIC_ACQUIRE) ||
        g_mission_control_lifetime_generation != event_loop_generation_snapshot(&g_event_loop) ||
        !__atomic_load_n(&g_event_loop.is_running, __ATOMIC_ACQUIRE)) {
        pthread_mutex_unlock(&g_mission_control_observer_lock);
        return;
    }
    __atomic_store_n(&g_mission_control_observe_retry_pending, false, __ATOMIC_RELEASE);
    bool observed = mission_control_observe_locked();
    if (!observed && g_mission_control_accepting &&
        generation == __atomic_load_n(&g_mission_control_observe_retry_generation, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&g_mission_control_observe_retry_pending, true, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_mission_control_observer_lock);
    if (!observed) mission_control_observe_retry_after(generation, attempt + 1, attempt < 9 ? 0.1 : 1.0);
}

void mission_control_ensure_observed(void)
{
#ifdef TESTS
    if (mission_control_test_ensure_before_lock) mission_control_test_ensure_before_lock();
#endif
    pthread_mutex_lock(&g_mission_control_observer_lock);
    if (!g_mission_control_accepting ||
        g_mission_control_lifetime_generation != event_loop_generation_snapshot(&g_event_loop) ||
        !__atomic_load_n(&g_event_loop.is_running, __ATOMIC_ACQUIRE) ||
        g_mission_control_observer.is_observing) {
        pthread_mutex_unlock(&g_mission_control_observer_lock);
        return;
    }
    if (mission_control_observe_locked()) {
        pthread_mutex_unlock(&g_mission_control_observer_lock);
        return;
    }
    uint64_t generation = __atomic_load_n(&g_mission_control_observe_retry_generation, __ATOMIC_ACQUIRE);
    bool pending = __atomic_exchange_n(&g_mission_control_observe_retry_pending, true, __ATOMIC_ACQ_REL);
    pthread_mutex_unlock(&g_mission_control_observer_lock);
    if (!pending) mission_control_observe_retry_after(generation, 1, 0.1);
}

static void mission_control_unobserve_locked(void)
{
    mission_control_next_nonzero(&g_mission_control_observe_retry_generation);
    __atomic_store_n(&g_mission_control_observe_retry_pending, false, __ATOMIC_RELEASE);
    mission_control_fallback_cancel_locked();
    AXUIElementRef ref = g_mission_control_observer.ref;
    AXObserverRef observer_ref = g_mission_control_observer.observer_ref;
    __atomic_store_n(&g_mission_control_observer.epoch, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_observer.notification_token, 0, __ATOMIC_RELEASE);
    g_mission_control_observer.ref = NULL;
    g_mission_control_observer.observer_ref = NULL;
    __atomic_store_n(&g_mission_control_observer.is_observing, false, __ATOMIC_RELEASE);
    if (observer_ref && ref) {
        for (int i = 0; i < array_count(mission_control_notifications); ++i) {
            (void) MC_OP(notification_remove)(observer_ref, ref, mission_control_notifications[i]);
        }
    }
    if (observer_ref) {
        CFRunLoopSourceRef source = MC_OP(observer_source)(observer_ref);
        if (source) MC_OP(source_invalidate)(source);
        MC_OP(release)(observer_ref);
    }
    if (ref) MC_OP(release)(ref);
}

void mission_control_unobserve(void)
{
    pthread_mutex_lock(&g_mission_control_observer_lock);
    mission_control_unobserve_locked();
    pthread_mutex_unlock(&g_mission_control_observer_lock);
}

static void mission_control_ingress_cancel_all(void)
{
    while (g_mission_control_ingress_head) {
        struct mission_control_ingress_item *item = g_mission_control_ingress_head;
        g_mission_control_ingress_head = item->next;
        mission_control_ingress_item_destroy(item);
    }
    g_mission_control_ingress_tail = NULL;
    g_mission_control_ingress_retry_pending = false;
    pthread_mutex_lock(&g_mission_control_observer_lock);
    mission_control_fallback_cancel_locked();
    pthread_mutex_unlock(&g_mission_control_observer_lock);
}

void mission_control_shutdown(void)
{
    pthread_mutex_lock(&g_mission_control_observer_lock);
    g_mission_control_accepting = false;
    g_mission_control_lifetime_generation = 0;
    mission_control_unobserve_locked();
    pthread_mutex_unlock(&g_mission_control_observer_lock);
#ifdef TESTS
    mission_control_ingress_cancel_all();
#else
    dispatch_sync(mission_control_ingress_queue(), ^{ mission_control_ingress_cancel_all(); });
#endif
}

void mission_control_event_context_destroy(void *context)
{
    if (!context) return;
#ifdef TESTS
    ++mission_control_test_event_freed;
#endif
    free(context);
}

static inline bool mission_control_is_active(void)
{
    return g_mission_control_mode != MISSION_CONTROL_MODE_INACTIVE;
}
