extern struct event_loop g_event_loop;
extern enum mission_control_mode g_mission_control_mode;
extern volatile uint64_t __last_cmd_tab_time;
extern int g_connection;
extern struct space_manager g_space_manager;
extern struct window_manager g_window_manager;

struct mission_control_session_state {
    uint64_t generation;
    uint64_t observer_epoch;
    uint64_t ax_token;
    uint64_t poll_token;
    unsigned inconclusive_samples;
    bool active;
    bool poll_pending;
};

struct mission_control_poll_context {
    uint64_t generation;
    uint64_t poll_token;
};

enum mission_control_sample_state {
    MISSION_CONTROL_SAMPLE_ACTIVE,
    MISSION_CONTROL_SAMPLE_INACTIVE,
    MISSION_CONTROL_SAMPLE_INCONCLUSIVE
};

static struct mission_control_session_state g_mission_control_session;
static uint64_t g_mission_control_generation;
static volatile uint64_t g_mission_control_poll_generation;
static volatile uint64_t g_mission_control_poll_token;

#define MISSION_CONTROL_BACKDROP_LEVEL 19
#define MISSION_CONTROL_MAX_BACKDROPS 16
#define MISSION_CONTROL_MAX_DISPLAYS 32

static struct mission_control_backdrop_state {
    uint32_t window_ids[MISSION_CONTROL_MAX_BACKDROPS];
    uint32_t count;
} g_mission_control_backdrops;

void mission_control_backdrop_prune(void);

static uint64_t mission_control_next_generation(void)
{
    if (++g_mission_control_generation == 0) ++g_mission_control_generation;
    return g_mission_control_generation;
}

#ifdef TESTS
static CFArrayRef (*event_loop_test_window_list_copy)(CGWindowListOption, CGWindowID) = CGWindowListCopyWindowInfo;
static void (*event_loop_test_mission_control_schedule)(uint64_t, uint64_t, double);
static void (*event_loop_test_mission_control_signal)(enum signal_type, void *);
static void (*event_loop_test_mission_control_correct)(struct space_manager *, struct window_manager *);
static void (*event_loop_test_mission_control_menu_alpha)(int, int, float);
static void (*event_loop_test_mission_control_menu_dirty)(uint32_t);
static void (*event_loop_test_mission_control_reconcile)(void);
static bool (*event_loop_test_mission_control_is_backdrop)(uint32_t);
static bool (*event_loop_test_mission_control_window_exists)(uint32_t);
static int event_loop_test_mission_control_poll_allocated;
static int event_loop_test_mission_control_poll_freed;
#endif

static CFArrayRef mission_control_copy_window_list(void)
{
#ifdef TESTS
    return event_loop_test_window_list_copy(kCGWindowListOptionOnScreenOnly, 0);
#else
    return CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, 0);
#endif
}

static void mission_control_emit_signal(enum signal_type type, void *context)
{
#ifdef TESTS
    if (event_loop_test_mission_control_signal) { event_loop_test_mission_control_signal(type, context); return; }
#endif
    event_signal_push(type, context);
}

static void mission_control_correct(void)
{
#ifdef TESTS
    if (event_loop_test_mission_control_correct) { event_loop_test_mission_control_correct(&g_space_manager, &g_window_manager); return; }
#endif
    window_manager_correct_for_mission_control_changes(&g_space_manager, &g_window_manager);
}

static void mission_control_set_menu_alpha(float alpha)
{
#ifdef TESTS
    if (event_loop_test_mission_control_menu_alpha) { event_loop_test_mission_control_menu_alpha(0, 1, alpha); return; }
#endif
    SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, alpha);
}

static void mission_control_mark_menu_dirty(void)
{
    uint32_t flags = MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_SPACE | MENU_BAR_DIRTY_WINDOW;
#ifdef TESTS
    if (event_loop_test_mission_control_menu_dirty) { event_loop_test_mission_control_menu_dirty(flags); return; }
#endif
    menu_bar_manager_mark_dirty(flags);
}

static void mission_control_request_reconcile(void)
{
#ifdef TESTS
    if (event_loop_test_mission_control_reconcile) { event_loop_test_mission_control_reconcile(); return; }
#endif
    declarative_configuration_request_reconcile();
}

static bool mission_control_backdrop_matches(const char *owner, int level, CGRect bounds,
                                              const CGRect *display_bounds, uint32_t display_count)
{
    if (!owner || strcmp(owner, "WindowManager") != 0 || level != MISSION_CONTROL_BACKDROP_LEVEL ||
        !display_bounds || !display_count) return false;
    for (uint32_t i = 0; i < display_count; ++i) {
        if (CGRectEqualToRect(bounds, display_bounds[i])) return true;
    }
    return false;
}

static bool mission_control_window_exists(uint32_t window_id)
{
#ifdef TESTS
    if (event_loop_test_mission_control_window_exists) {
        return event_loop_test_mission_control_window_exists(window_id);
    }
#endif
    int window_connection = 0;
    return window_id &&
           SLSGetWindowOwner(g_connection, window_id, &window_connection) == kCGErrorSuccess &&
           window_connection != 0;
}

static bool mission_control_window_is_backdrop(uint32_t window_id)
{
#ifdef TESTS
    if (event_loop_test_mission_control_is_backdrop) {
        return event_loop_test_mission_control_is_backdrop(window_id);
    }
#endif
    int level = 0;
    int window_connection = 0;
    pid_t pid = 0;
    CGRect bounds;
    char process_name[PROC_PIDPATHINFO_MAXSIZE] = {0};
    if (!window_id || SLSGetWindowLevel(g_connection, window_id, &level) != kCGErrorSuccess ||
        SLSGetWindowOwner(g_connection, window_id, &window_connection) != kCGErrorSuccess ||
        !window_connection ||
        SLSConnectionGetPID(window_connection, &pid) != kCGErrorSuccess || pid <= 0 ||
        proc_name(pid, process_name, sizeof(process_name)) <= 0 ||
        SLSGetWindowBounds(g_connection, window_id, &bounds) != kCGErrorSuccess) return false;

    CGDirectDisplayID displays[MISSION_CONTROL_MAX_DISPLAYS];
    uint32_t display_count = 0;
    if (CGGetActiveDisplayList(array_count(displays), displays, &display_count) != kCGErrorSuccess ||
        !display_count) return false;

    CGRect display_bounds[MISSION_CONTROL_MAX_DISPLAYS];
    for (uint32_t i = 0; i < display_count; ++i) display_bounds[i] = CGDisplayBounds(displays[i]);
    return mission_control_backdrop_matches(process_name, level, bounds, display_bounds, display_count);
}

static int mission_control_backdrop_find(uint32_t window_id)
{
    for (uint32_t i = 0; i < g_mission_control_backdrops.count; ++i) {
        if (g_mission_control_backdrops.window_ids[i] == window_id) return (int) i;
    }
    return -1;
}

static struct mission_control_poll_context *mission_control_poll_context_create(void)
{
    struct mission_control_poll_context *context = malloc(sizeof(*context));
#ifdef TESTS
    if (context) ++event_loop_test_mission_control_poll_allocated;
#endif
    return context;
}

void mission_control_poll_context_destroy(struct mission_control_poll_context *context)
{
    if (!context) return;
#ifdef TESTS
    ++event_loop_test_mission_control_poll_freed;
#endif
    free(context);
}

static void mission_control_poll_post(uint64_t generation, uint64_t poll_token,
                                      uint64_t loop_generation, unsigned retry)
{
    if (generation != __atomic_load_n(&g_mission_control_poll_generation, __ATOMIC_ACQUIRE) ||
        poll_token != __atomic_load_n(&g_mission_control_poll_token, __ATOMIC_ACQUIRE) ||
        loop_generation != event_loop_generation_snapshot(&g_event_loop) ||
        !__atomic_load_n(&g_event_loop.is_running, __ATOMIC_ACQUIRE)) return;

    struct mission_control_poll_context *context = mission_control_poll_context_create();
    if (context) *context = (struct mission_control_poll_context) { generation, poll_token };
    if (context && event_loop_try_post_generation(&g_event_loop, loop_generation,
                                                   MISSION_CONTROL_CHECK_FOR_EXIT, context, 0)) return;
    double delay = retry < 9 ? 0.1 : 1.0;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        mission_control_poll_post(generation, poll_token, loop_generation, retry + 1);
    });
}

void mission_control_schedule_poll(double delay)
{
    if (!g_mission_control_session.active || g_mission_control_session.poll_pending) return;
    g_mission_control_session.poll_pending = true;
    if (++g_mission_control_session.poll_token == 0) ++g_mission_control_session.poll_token;
    uint64_t generation = g_mission_control_session.generation;
    uint64_t poll_token = g_mission_control_session.poll_token;
    __atomic_store_n(&g_mission_control_poll_generation, generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_poll_token, poll_token, __ATOMIC_RELEASE);
#ifdef TESTS
    if (event_loop_test_mission_control_schedule) {
        event_loop_test_mission_control_schedule(generation, poll_token, delay);
        return;
    }
#endif
    uint64_t loop_generation = event_loop_generation_snapshot(&g_event_loop);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        mission_control_poll_post(generation, poll_token, loop_generation, 0);
    });
}

static bool mission_control_connection_window_id(const void *data, size_t data_length, uint32_t *wid)
{
    if (!data || !wid || data_length < sizeof(uint64_t) + sizeof(uint32_t)) return false;
    memcpy(wid, (const uint8_t *) data + sizeof(uint64_t), sizeof(*wid));
    return *wid != 0;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static __attribute__((unused)) CONNECTION_CALLBACK(connection_handler)
{
    if (type == 1204) {
        event_loop_post(&g_event_loop, MISSION_CONTROL_ENTER, NULL, 0);
    } else if (type == 1325 || type == 1326) {
        uint32_t wid;
        if (mission_control_connection_window_id(data, data_length, &wid)) {
            enum event_type event = type == 1325 ? MISSION_CONTROL_BACKDROP_CREATED
                                                 : MISSION_CONTROL_BACKDROP_DESTROYED;
            event_loop_post(&g_event_loop, event, (void *) (uintptr_t) wid, 0);
        }
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

void mission_control_session_reset(void)
{
    g_mission_control_session = (struct mission_control_session_state) {0};
    g_mission_control_backdrops = (struct mission_control_backdrop_state) {0};
    __atomic_store_n(&g_mission_control_poll_generation, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_poll_token, 0, __ATOMIC_RELEASE);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static void mission_control_begin_session(enum mission_control_mode mode, uint64_t observer_epoch,
                                          uint64_t ax_token, bool poll)
{
    event_loop_abort_native_resize(true);
    g_mission_control_session = (struct mission_control_session_state) {
        .generation = mission_control_next_generation(), .observer_epoch = observer_epoch,
        .ax_token = ax_token, .active = true
    };
    g_mission_control_mode = mode;
    __atomic_store_n(&g_mission_control_poll_generation, g_mission_control_session.generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_poll_token, 0, __ATOMIC_RELEASE);
    if (poll) mission_control_schedule_poll(0.1);
    window_manager_hide_active_window_highlight(&g_window_manager);
    mission_control_emit_signal(SIGNAL_MISSION_CONTROL_ENTER, (void *) (uintptr_t) mode);
}

static bool mission_control_accept_ax_context(struct mission_control_event_context *event,
                                              enum mission_control_notification_kind kind)
{
    return event && event->kind == kind && event->epoch && event->token &&
           event->epoch == __atomic_load_n(&g_mission_control_observer.epoch, __ATOMIC_ACQUIRE) &&
           event->token == __atomic_load_n(&g_mission_control_observer.notification_token, __ATOMIC_ACQUIRE);
}

static void mission_control_handle_show(void *context, enum mission_control_notification_kind kind,
                                        enum mission_control_mode mode)
{
    struct mission_control_event_context event = context ? *(struct mission_control_event_context *) context
                                                         : (struct mission_control_event_context) {0};
    mission_control_event_context_destroy(context);
    if (!mission_control_accept_ax_context(&event, kind)) return;
    mission_control_begin_session(mode, event.epoch, event.token, false);
}

static EVENT_HANDLER(MISSION_CONTROL_SHOW_ALL_WINDOWS)
{
    debug("%s:\n", __FUNCTION__);
    mission_control_handle_show(context, MISSION_CONTROL_NOTIFICATION_SHOW_ALL, MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS);
}

static EVENT_HANDLER(MISSION_CONTROL_SHOW_FRONT_WINDOWS)
{
    debug("%s:\n", __FUNCTION__);
    mission_control_handle_show(context, MISSION_CONTROL_NOTIFICATION_SHOW_FRONT, MISSION_CONTROL_MODE_SHOW_FRONT_WINDOWS);
}

static EVENT_HANDLER(MISSION_CONTROL_SHOW_DESKTOP)
{
    debug("%s:\n", __FUNCTION__);
    mission_control_handle_show(context, MISSION_CONTROL_NOTIFICATION_SHOW_DESKTOP, MISSION_CONTROL_MODE_SHOW_DESKTOP);
}

static EVENT_HANDLER(MISSION_CONTROL_ENTER)
{
    debug("%s:\n", __FUNCTION__);
    mission_control_begin_session(MISSION_CONTROL_MODE_SHOW, 0, 0, true);
}

static enum mission_control_sample_state mission_control_classify_sample(CFTypeRef sample)
{
    if (!sample || CFGetTypeID(sample) != CFArrayGetTypeID()) return MISSION_CONTROL_SAMPLE_INCONCLUSIVE;
    CFArrayRef window_list = sample;
    bool inconclusive = false;
    for (CFIndex i = 0; i < CFArrayGetCount(window_list); ++i) {
        CFTypeRef item = CFArrayGetValueAtIndex(window_list, i);
        if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID()) { inconclusive = true; continue; }
        CFDictionaryRef dictionary = item;
        CFTypeRef name = CFDictionaryGetValue(dictionary, kCGWindowName);
        if (name) {
            if (CFGetTypeID(name) != CFStringGetTypeID()) inconclusive = true;
            continue;
        }
        CFTypeRef owner = CFDictionaryGetValue(dictionary, kCGWindowOwnerName);
        CFTypeRef layer_ref = CFDictionaryGetValue(dictionary, kCGWindowLayer);
        if (!owner || !layer_ref || CFGetTypeID(owner) != CFStringGetTypeID() ||
            CFGetTypeID(layer_ref) != CFNumberGetTypeID()) { inconclusive = true; continue; }
        int64_t layer = 0;
        if (!CFNumberGetValue(layer_ref, kCFNumberSInt64Type, &layer)) { inconclusive = true; continue; }
        if (layer == 18 && CFEqual(CFSTR("Dock"), owner)) return MISSION_CONTROL_SAMPLE_ACTIVE;
    }
    return inconclusive ? MISSION_CONTROL_SAMPLE_INCONCLUSIVE : MISSION_CONTROL_SAMPLE_INACTIVE;
}

static void mission_control_finish_exit(uint64_t generation)
{
    if (!g_mission_control_session.active || generation != g_mission_control_session.generation) return;
    enum mission_control_mode mode = g_mission_control_mode;
    g_mission_control_session.active = false;
    g_mission_control_session.poll_pending = false;
    __atomic_store_n(&g_mission_control_poll_token, 0, __ATOMIC_RELEASE);
    g_mission_control_session.ax_token = 0;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
    if (g_window_manager.menubar_opacity != 1.0f) {
#ifdef TESTS
        float alpha = g_window_manager.menubar_opacity;
#else
        float alpha = space_is_fullscreen(g_space_manager.current_space_id) ? 1.0f : g_window_manager.menubar_opacity;
#endif
        mission_control_set_menu_alpha(alpha);
    }
    if (mode == MISSION_CONTROL_MODE_SHOW || mode == MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS) mission_control_correct();
    window_manager_refresh_active_window_highlight(&g_window_manager);
    mission_control_emit_signal(SIGNAL_MISSION_CONTROL_EXIT, (void *) (uintptr_t) mode);
    mission_control_mark_menu_dirty();
    mission_control_request_reconcile();
}

void mission_control_backdrop_prune(void)
{
    if (!g_mission_control_backdrops.count) return;

    uint32_t count = 0;
    for (uint32_t i = 0; i < g_mission_control_backdrops.count; ++i) {
        uint32_t window_id = g_mission_control_backdrops.window_ids[i];
        if (mission_control_window_exists(window_id)) {
            g_mission_control_backdrops.window_ids[count++] = window_id;
        }
    }
    g_mission_control_backdrops.count = count;
    if (!count && g_mission_control_session.active) {
        mission_control_finish_exit(g_mission_control_session.generation);
    }
}

static EVENT_HANDLER(MISSION_CONTROL_BACKDROP_CREATED)
{
    uint32_t window_id = (uint32_t) (uintptr_t) context;
    mission_control_backdrop_prune();
    if (!window_id || mission_control_backdrop_find(window_id) >= 0 ||
        g_mission_control_backdrops.count == MISSION_CONTROL_MAX_BACKDROPS ||
        !mission_control_window_is_backdrop(window_id)) return;

    g_mission_control_backdrops.window_ids[g_mission_control_backdrops.count++] = window_id;
    if (!g_mission_control_session.active) {
        mission_control_begin_session(MISSION_CONTROL_MODE_SHOW, 0, 0, false);
    } else {
        window_manager_hide_active_window_highlight(&g_window_manager);
    }
}

static EVENT_HANDLER(MISSION_CONTROL_BACKDROP_DESTROYED)
{
    uint32_t window_id = (uint32_t) (uintptr_t) context;
    if (mission_control_backdrop_find(window_id) >= 0) mission_control_backdrop_prune();
}

static EVENT_HANDLER(MISSION_CONTROL_CHECK_FOR_EXIT)
{
    struct mission_control_poll_context event = context ? *(struct mission_control_poll_context *) context
                                                        : (struct mission_control_poll_context) {0};
    mission_control_poll_context_destroy(context);
    if (!g_mission_control_session.active || !g_mission_control_session.poll_pending ||
        event.generation != g_mission_control_session.generation ||
        event.poll_token != g_mission_control_session.poll_token) return;
    g_mission_control_session.poll_pending = false;
    __atomic_store_n(&g_mission_control_poll_token, 0, __ATOMIC_RELEASE);
    CFArrayRef window_list = mission_control_copy_window_list();
    enum mission_control_sample_state sample = mission_control_classify_sample(window_list);
    if (window_list) CFRelease(window_list);
    if (sample == MISSION_CONTROL_SAMPLE_INACTIVE) {
        g_mission_control_session.inconclusive_samples = 0;
        mission_control_finish_exit(event.generation);
    } else if (sample == MISSION_CONTROL_SAMPLE_ACTIVE) {
        g_mission_control_session.inconclusive_samples = 0;
        mission_control_schedule_poll(0.1);
    } else {
        if (g_mission_control_session.inconclusive_samples != UINT_MAX) ++g_mission_control_session.inconclusive_samples;
        mission_control_schedule_poll(g_mission_control_session.inconclusive_samples < 10 ? 0.1 : 1.0);
    }
}

static EVENT_HANDLER(MISSION_CONTROL_EXIT)
{
    struct mission_control_event_context event = context ? *(struct mission_control_event_context *) context
                                                         : (struct mission_control_event_context) {0};
    mission_control_event_context_destroy(context);
    if (!mission_control_accept_ax_context(&event, MISSION_CONTROL_NOTIFICATION_EXIT)) return;
    if (!g_mission_control_session.active || event.epoch != g_mission_control_session.observer_epoch ||
        event.token != g_mission_control_session.ax_token) return;
    mission_control_schedule_poll(0.0);
}
#pragma clang diagnostic pop

static inline bool mission_control_is_active(void)
{
    return g_mission_control_mode != MISSION_CONTROL_MODE_INACTIVE;
}
