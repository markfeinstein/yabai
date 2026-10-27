#include "window_server_snapshot.h"

#include <stdbool.h>
#include <stdlib.h>

struct window_server_snapshot_backend
{
    CFTypeRef (*query_windows)(int connection, CFArrayRef window_ids, int expected_count);
    CFTypeRef (*copy_iterator)(CFTypeRef query);
    int (*iterator_count)(CFTypeRef iterator);
    bool (*iterator_advance)(CFTypeRef iterator);
    uint32_t (*iterator_window_id)(CFTypeRef iterator);
    uint32_t (*iterator_parent_id)(CFTypeRef iterator);
    int (*iterator_level)(CFTypeRef iterator);
    uint64_t (*iterator_tags)(CFTypeRef iterator);
    uint64_t (*iterator_attributes)(CFTypeRef iterator);
    void (*release)(CFTypeRef value);
    void *(*allocate)(size_t count, size_t size);
    void (*deallocate)(void *value);
};

static void window_server_snapshot_release(CFTypeRef value)
{
    CFRelease(value);
}

static const struct window_server_snapshot_backend window_server_snapshot_production_backend = {
    .query_windows = SLSWindowQueryWindows,
    .copy_iterator = SLSWindowQueryResultCopyWindows,
    .iterator_count = SLSWindowIteratorGetCount,
    .iterator_advance = SLSWindowIteratorAdvance,
    .iterator_window_id = SLSWindowIteratorGetWindowID,
    .iterator_parent_id = SLSWindowIteratorGetParentID,
    .iterator_level = SLSWindowIteratorGetLevel,
    .iterator_tags = SLSWindowIteratorGetTags,
    .iterator_attributes = SLSWindowIteratorGetAttributes,
    .release = window_server_snapshot_release,
    .allocate = calloc,
    .deallocate = free
};

#ifdef TESTS
static const struct window_server_snapshot_backend *window_server_snapshot_test_backend;
#endif

static const struct window_server_snapshot_backend *window_server_snapshot_backend(void)
{
#ifdef TESTS
    if (window_server_snapshot_test_backend) return window_server_snapshot_test_backend;
#endif
    return &window_server_snapshot_production_backend;
}

static bool window_server_snapshot_backend_is_valid(const struct window_server_snapshot_backend *backend)
{
    return backend && backend->query_windows && backend->copy_iterator &&
           backend->iterator_count && backend->iterator_advance &&
           backend->release && backend->allocate && backend->deallocate;
}

static void window_server_snapshot_copy_record(const struct window_server_snapshot_backend *backend,
                                               CFTypeRef iterator,
                                               struct window_server_window_record *record)
{
    if (backend->iterator_window_id) {
        record->id = backend->iterator_window_id(iterator);
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_ID;
    }
    if (backend->iterator_parent_id) {
        record->parent_id = backend->iterator_parent_id(iterator);
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_PARENT_ID;
    }
    if (backend->iterator_level) {
        record->level = backend->iterator_level(iterator);
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_LEVEL;
    }
    if (backend->iterator_tags) {
        record->tags = backend->iterator_tags(iterator);
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_TAGS;
    }
    if (backend->iterator_attributes) {
        record->attributes = backend->iterator_attributes(iterator);
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_ATTRIBUTES;
    }
}

struct window_server_snapshot_owner
{
    void (*deallocate)(void *value);
    struct window_server_snapshot snapshot;
};

static uint64_t window_server_snapshot_generation;

struct window_server_snapshot *window_server_snapshot_create(int connection,
                                                              CFArrayRef window_ids,
                                                              int expected_count)
{
    if (expected_count < 0) return NULL;
    if (window_ids) {
        if (CFGetTypeID(window_ids) != CFArrayGetTypeID() ||
            CFArrayGetCount(window_ids) != expected_count) return NULL;
    } else if (expected_count != 0) {
        return NULL;
    }

    const struct window_server_snapshot_backend *backend = window_server_snapshot_backend();
    if (!window_server_snapshot_backend_is_valid(backend)) return NULL;

    struct window_server_snapshot_owner *owner = backend->allocate(1, sizeof(*owner));
    if (!owner) return NULL;
    owner->deallocate = backend->deallocate;
    struct window_server_snapshot *snapshot = &owner->snapshot;

    if (expected_count == 0) {
        snapshot->generation = __atomic_add_fetch(&window_server_snapshot_generation, 1, __ATOMIC_RELAXED);
        return snapshot;
    }

    CFTypeRef query = backend->query_windows(connection, window_ids, expected_count);
    if (!query) goto fail;

    CFTypeRef iterator = backend->copy_iterator(query);
    if (!iterator) goto fail_query;

    if (backend->iterator_count(iterator) != expected_count) goto fail_iterator;

    struct window_server_window_record *records = backend->allocate((size_t) expected_count, sizeof(*records));
    if (!records) goto fail_iterator;

    int advanced_count = 0;
    while (backend->iterator_advance(iterator)) {
        if (advanced_count >= expected_count) goto fail_records;
        window_server_snapshot_copy_record(backend, iterator, records + advanced_count);
        ++advanced_count;
    }
    if (advanced_count != expected_count) goto fail_records;

    backend->release(iterator);
    backend->release(query);
    snapshot->generation = __atomic_add_fetch(&window_server_snapshot_generation, 1, __ATOMIC_RELAXED);
    snapshot->records = records;
    snapshot->count = (size_t) expected_count;
    return snapshot;

fail_records:
    backend->deallocate(records);
fail_iterator:
    backend->release(iterator);
fail_query:
    backend->release(query);
fail:
    backend->deallocate(owner);
    return NULL;
}

void window_server_snapshot_destroy(struct window_server_snapshot *snapshot)
{
    if (!snapshot) return;
    struct window_server_snapshot_owner *owner = (struct window_server_snapshot_owner *)
        ((char *) snapshot - offsetof(struct window_server_snapshot_owner, snapshot));
    void (*deallocate)(void *) = owner->deallocate;
    deallocate((void *) snapshot->records);
    deallocate(owner);
}
