// The event loop queue stores waiting events, oldest at `head`.
// Producers on any thread append under a lock, and the event loop pops from
// the same ring. A full ring grows instead of overwriting unread events,
// because queued contexts are owned by their eventual handlers.

#define EVENT_QUEUE_CAPACITY        4096
#define EVENT_QUEUE_NORMAL_CAPACITY 65536
#define EVENT_QUEUE_HARD_CAPACITY   69632

#ifndef EVENT_QUEUE_ALLOCATE
#define EVENT_QUEUE_ALLOCATE(size) malloc(size)
#endif

static bool event_queue_init(struct event_queue *queue, uint32_t capacity)
{
    *queue = (struct event_queue) { 0 };
    if (capacity == 0 || capacity > EVENT_QUEUE_HARD_CAPACITY) return false;
    struct event *events = EVENT_QUEUE_ALLOCATE((size_t) capacity * sizeof(struct event));
    if (!events) return false;

    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        free(events);
        *queue = (struct event_queue) { 0 };
        return false;
    }

    queue->events = events;
    queue->capacity = capacity;
    queue->normal_capacity = capacity > EVENT_QUEUE_NORMAL_CAPACITY ? capacity : EVENT_QUEUE_NORMAL_CAPACITY;
    queue->hard_capacity = capacity > EVENT_QUEUE_HARD_CAPACITY ? capacity : EVENT_QUEUE_HARD_CAPACITY;
    return true;
}

static void event_queue_destroy(struct event_queue *queue)
{
    pthread_mutex_destroy(&queue->lock);
    free(queue->events);
    *queue = (struct event_queue) { 0 };
}

static uint32_t event_queue_slot(struct event_queue *queue, uint32_t offset)
{
    uint32_t slot = queue->head + offset;
    return slot < queue->capacity ? slot : slot - queue->capacity;
}

static bool event_queue_grow(struct event_queue *queue, uint32_t limit)
{
    if (queue->capacity >= limit || queue->capacity > UINT32_MAX / 2) return false;

    uint32_t capacity = queue->capacity * 2;
    if (capacity > limit) capacity = limit;
    struct event *events = EVENT_QUEUE_ALLOCATE((size_t) capacity * sizeof(struct event));
    if (!events) return false;

    for (uint32_t i = 0; i < queue->count; ++i) {
        events[i] = queue->events[event_queue_slot(queue, i)];
    }

    free(queue->events);
    queue->events = events;
    queue->capacity = capacity;
    queue->head = 0;

    return true;
}

static enum event_queue_result event_queue_push(struct event_queue *queue, struct event event, bool merge,
                                                struct event *replaced, uint32_t *capacity, bool *post_wake)
{
    enum event_queue_result result = EVENT_QUEUE_ADDED;

    *post_wake = false;
    pthread_mutex_lock(&queue->lock);

    if (merge && queue->count) {
        struct event *last = &queue->events[event_queue_slot(queue, queue->count - 1)];
        bool same_stream = last->type == event.type &&
                           (event.type != MOUSE_DRAGGED || last->param1 == event.param1);
        if (same_stream) {
            *replaced = *last;
            *last = event;
            result = EVENT_QUEUE_MERGED;
            goto out;
        }
    }

    bool critical = event.type == APPLICATION_TERMINATED ||
                    event.type == WINDOW_DESTROYED ||
                    event.type == SLS_WINDOW_DESTROYED ||
                    event.type == SLS_SPACE_DESTROYED ||
                    event.type == DISPLAY_REMOVED;
    uint32_t limit = critical ? queue->hard_capacity : queue->normal_capacity;
    if (queue->count >= limit) {
        result = EVENT_QUEUE_FULL;
        goto out;
    }

    if (queue->count == queue->capacity) {
        if (!event_queue_grow(queue, limit)) {
            result = EVENT_QUEUE_FULL;
            goto out;
        }

        *capacity = queue->capacity;
        result = EVENT_QUEUE_GREW;
    }

    queue->events[event_queue_slot(queue, queue->count)] = event;
    ++queue->count;
    if (!queue->wake_pending) {
        queue->wake_pending = true;
        *post_wake = true;
    }

out:
    pthread_mutex_unlock(&queue->lock);
    return result;
}

static bool event_queue_pop(struct event_queue *queue, struct event *event)
{
    pthread_mutex_lock(&queue->lock);

    bool popped = queue->count > 0;
    if (popped) {
        *event = queue->events[queue->head];
        queue->head = event_queue_slot(queue, 1);
        --queue->count;
    } else {
        queue->wake_pending = false;
    }

    pthread_mutex_unlock(&queue->lock);
    return popped;
}
