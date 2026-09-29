// The event loop queue stores waiting events, oldest at `head`.
// Producers on any thread append under a lock, and the event loop pops from
// the same ring. A full ring grows instead of overwriting unread events,
// because queued contexts are owned by their eventual handlers.

#define EVENT_QUEUE_CAPACITY 4096

#ifndef EVENT_QUEUE_ALLOCATE
#define EVENT_QUEUE_ALLOCATE(size) malloc(size)
#endif

static bool event_queue_init(struct event_queue *queue, uint32_t capacity)
{
    *queue = (struct event_queue) { .capacity = capacity };
    queue->events = EVENT_QUEUE_ALLOCATE(capacity * sizeof(struct event));

    return queue->events && pthread_mutex_init(&queue->lock, NULL) == 0;
}

static uint32_t event_queue_slot(struct event_queue *queue, uint32_t offset)
{
    uint32_t slot = queue->head + offset;
    return slot < queue->capacity ? slot : slot - queue->capacity;
}

static bool event_queue_grow(struct event_queue *queue)
{
    if (queue->capacity > UINT32_MAX / 2) return false;

    uint32_t capacity = queue->capacity * 2;
    struct event *events = EVENT_QUEUE_ALLOCATE(capacity * sizeof(struct event));
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
                                                struct event *replaced, uint32_t *capacity)
{
    enum event_queue_result result = EVENT_QUEUE_ADDED;

    pthread_mutex_lock(&queue->lock);

    if (merge && queue->count) {
        struct event *last = &queue->events[event_queue_slot(queue, queue->count - 1)];
        if (last->type == event.type) {
            *replaced = *last;
            *last = event;
            result = EVENT_QUEUE_MERGED;
            goto out;
        }
    }

    if (queue->count == queue->capacity) {
        if (!event_queue_grow(queue)) {
            result = EVENT_QUEUE_FULL;
            goto out;
        }

        *capacity = queue->capacity;
        result = EVENT_QUEUE_GREW;
    }

    queue->events[event_queue_slot(queue, queue->count)] = event;
    ++queue->count;

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
    }

    pthread_mutex_unlock(&queue->lock);
    return popped;
}
