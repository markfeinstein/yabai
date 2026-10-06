#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#define EVENT_HANDLER(event_type) void EVENT_HANDLER_##event_type(void *context, int param1)

static inline bool event_loop_pending_reserve(volatile uint32_t *pending)
{
    uint32_t value = __atomic_load_n(pending, __ATOMIC_ACQUIRE);
    while (value != UINT32_MAX) {
        if (__atomic_compare_exchange_n(pending, &value, value + 1, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return true;
    }
    return false;
}

static inline bool event_loop_pending_consume(volatile uint32_t *pending)
{
    uint32_t value = __atomic_load_n(pending, __ATOMIC_ACQUIRE);
    while (value != 0) {
        if (__atomic_compare_exchange_n(pending, &value, value - 1, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return true;
    }
    return false;
}

#define EVENT_TYPE_LIST \
    EVENT_TYPE_ENTRY(APPLICATION_LAUNCHED) \
    EVENT_TYPE_ENTRY(APPLICATION_TERMINATED) \
    EVENT_TYPE_ENTRY(APPLICATION_FRONT_SWITCHED) \
    EVENT_TYPE_ENTRY(APPLICATION_FRONT_SPACE_RETRY) \
    EVENT_TYPE_ENTRY(APPLICATION_VISIBLE) \
    EVENT_TYPE_ENTRY(APPLICATION_HIDDEN) \
    EVENT_TYPE_ENTRY(WINDOW_CREATED) \
    EVENT_TYPE_ENTRY(WINDOW_CREATED_RETRY) \
    EVENT_TYPE_ENTRY(WINDOW_DESTROYED) \
    EVENT_TYPE_ENTRY(WINDOW_FOCUSED) \
    EVENT_TYPE_ENTRY(WINDOW_MOVED) \
    EVENT_TYPE_ENTRY(WINDOW_RESIZED) \
    EVENT_TYPE_ENTRY(WINDOW_MINIMIZED) \
    EVENT_TYPE_ENTRY(WINDOW_DEMINIMIZED) \
    EVENT_TYPE_ENTRY(WINDOW_TITLE_CHANGED) \
    EVENT_TYPE_ENTRY(SLS_WINDOW_ORDERED) \
    EVENT_TYPE_ENTRY(SLS_WINDOW_IS_VISIBLE) \
    EVENT_TYPE_ENTRY(SLS_WINDOW_IS_INVISIBLE) \
    EVENT_TYPE_ENTRY(SLS_WINDOW_DESTROYED) \
    EVENT_TYPE_ENTRY(SLS_SPACE_CREATED) \
    EVENT_TYPE_ENTRY(SLS_SPACE_DESTROYED) \
    EVENT_TYPE_ENTRY(SPACE_CHANGED) \
    EVENT_TYPE_ENTRY(DISPLAY_ADDED) \
    EVENT_TYPE_ENTRY(DISPLAY_REMOVED) \
    EVENT_TYPE_ENTRY(DISPLAY_MOVED) \
    EVENT_TYPE_ENTRY(DISPLAY_RESIZED) \
    EVENT_TYPE_ENTRY(DISPLAY_CHANGED) \
    EVENT_TYPE_ENTRY(MOUSE_DOWN) \
    EVENT_TYPE_ENTRY(MOUSE_UP) \
    EVENT_TYPE_ENTRY(MOUSE_DRAGGED) \
    EVENT_TYPE_ENTRY(MOUSE_MOVED) \
    EVENT_TYPE_ENTRY(NATIVE_RESIZE_FINALIZE) \
    EVENT_TYPE_ENTRY(NATIVE_RESIZE_CANCEL) \
    EVENT_TYPE_ENTRY(MISSION_CONTROL_SHOW_ALL_WINDOWS) \
    EVENT_TYPE_ENTRY(MISSION_CONTROL_SHOW_FRONT_WINDOWS) \
    EVENT_TYPE_ENTRY(MISSION_CONTROL_SHOW_DESKTOP) \
    EVENT_TYPE_ENTRY(MISSION_CONTROL_ENTER) \
    EVENT_TYPE_ENTRY(MISSION_CONTROL_CHECK_FOR_EXIT) \
    EVENT_TYPE_ENTRY(MISSION_CONTROL_EXIT) \
    EVENT_TYPE_ENTRY(DOCK_DID_RESTART) \
    EVENT_TYPE_ENTRY(MENU_OPENED) \
    EVENT_TYPE_ENTRY(MENU_CLOSED) \
    EVENT_TYPE_ENTRY(MENU_BAR_HIDDEN_CHANGED) \
    EVENT_TYPE_ENTRY(DOCK_DID_CHANGE_PREF) \
    EVENT_TYPE_ENTRY(SYSTEM_WOKE) \
    EVENT_TYPE_ENTRY(MENU_BAR_REFRESH) \
    EVENT_TYPE_ENTRY(MENU_BAR_ACTION) \
    EVENT_TYPE_ENTRY(CONFIGURATION_RECONCILE) \
    EVENT_TYPE_ENTRY(EVENT_LOOP_SYNC) \
    EVENT_TYPE_ENTRY(WINDOW_FOCUS_FALLBACK) \
    EVENT_TYPE_ENTRY(DAEMON_MESSAGE)

enum event_type
{
#define EVENT_TYPE_ENTRY(value) value,
    EVENT_TYPE_LIST
#undef EVENT_TYPE_ENTRY
};

struct event
{
    enum event_type type;
    int param1;
    void *context;
};

struct event_queue
{
    pthread_mutex_t lock;
    struct event *events;
    uint32_t capacity;
    uint32_t normal_capacity;
    uint32_t hard_capacity;
    uint32_t head;
    uint32_t count;
    bool wake_pending;
};

enum event_queue_result
{
    EVENT_QUEUE_ADDED,
    EVENT_QUEUE_MERGED,
    EVENT_QUEUE_GREW,
    EVENT_QUEUE_FULL
};

static bool event_queue_init(struct event_queue *queue, uint32_t capacity);
static void event_queue_destroy(struct event_queue *queue);
static enum event_queue_result event_queue_push(struct event_queue *queue, struct event event, bool merge,
                                                struct event *replaced, uint32_t *capacity, bool *post_wake);
static bool event_queue_pop(struct event_queue *queue, struct event *event);

struct event_loop
{
    bool is_running;
    bool producer_sync_initialized;
    uint64_t generation;
    volatile uint32_t producer_admission;
    volatile uint32_t shutdown_owner;
    pthread_mutex_t producer_lock;
    pthread_cond_t producer_cond;
    bool final_sync_pending;
    bool final_sync_completed;
    bool final_sync_result;
    bool (*final_sync_callback)(void *);
    void *final_sync_context;
    pthread_t thread;
    sem_t *semaphore;
    struct event_queue queue;
    volatile uint32_t topology_resync_pending;
    volatile uint64_t topology_resync_retry_scheduled;
};

typedef bool (*event_loop_bootstrap_callback)(void *context);
typedef bool (*event_loop_sync_callback)(void *context);

bool event_loop_begin(struct event_loop *event_loop);
bool event_loop_begin_bootstrap(struct event_loop *event_loop, event_loop_bootstrap_callback callback, void *context);
void event_loop_end(struct event_loop *event_loop);
bool event_loop_perform_sync(struct event_loop *event_loop, event_loop_sync_callback callback, void *context);
bool event_loop_is_worker_thread(struct event_loop *event_loop);
uint64_t event_loop_generation_snapshot(struct event_loop *event_loop);
bool event_loop_try_post(struct event_loop *event_loop, enum event_type type, void *context, int param1);
bool event_loop_try_post_generation(struct event_loop *event_loop, uint64_t generation,
                                    enum event_type type, void *context, int param1);
void event_loop_post(struct event_loop *event_loop, enum event_type type, void *context, int param1);
struct process;
bool event_loop_post_application_launched(struct process *process);
void event_loop_schedule_application_launch_retry(struct process *process);
void event_loop_window_inventory_did_change(void);
void event_loop_request_window_focus_fallback(void);
void event_loop_process_application_windows(struct window **window_list, int window_count);

#endif
