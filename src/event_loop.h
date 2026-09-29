#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#define EVENT_HANDLER(event_type) void EVENT_HANDLER_##event_type(void *context, int param1)

#define EVENT_TYPE_LIST \
    EVENT_TYPE_ENTRY(APPLICATION_LAUNCHED) \
    EVENT_TYPE_ENTRY(APPLICATION_TERMINATED) \
    EVENT_TYPE_ENTRY(APPLICATION_FRONT_SWITCHED) \
    EVENT_TYPE_ENTRY(APPLICATION_VISIBLE) \
    EVENT_TYPE_ENTRY(APPLICATION_HIDDEN) \
    EVENT_TYPE_ENTRY(WINDOW_CREATED) \
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
    uint32_t head;
    uint32_t count;
};

enum event_queue_result
{
    EVENT_QUEUE_ADDED,
    EVENT_QUEUE_MERGED,
    EVENT_QUEUE_GREW,
    EVENT_QUEUE_FULL
};

static bool event_queue_init(struct event_queue *queue, uint32_t capacity);
static enum event_queue_result event_queue_push(struct event_queue *queue, struct event event, bool merge,
                                                struct event *replaced, uint32_t *capacity);
static bool event_queue_pop(struct event_queue *queue, struct event *event);

struct event_loop
{
    bool is_running;
    pthread_t thread;
    sem_t *semaphore;
    struct event_queue queue;
};

bool event_loop_begin(struct event_loop *event_loop);
void event_loop_post(struct event_loop *event_loop, enum event_type type, void *context, int param1);

#endif
