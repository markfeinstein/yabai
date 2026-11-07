#include "window_server_snapshot.h"

#include <stdbool.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
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
    bool (*iterator_constraints)(CFTypeRef iterator, struct window_constraint_observation *observation);
    bool (*iterator_corner_geometry)(CFTypeRef iterator, uint32_t window_id,
                                     struct window_corner_geometry *geometry);
    bool (*iterator_alpha)(CFTypeRef iterator, float *alpha);
    bool (*iterator_space_count)(CFTypeRef iterator, int *space_count);
    void (*release)(CFTypeRef value);
    void *(*allocate)(size_t count, size_t size);
    void (*deallocate)(void *value);
};

static void window_server_snapshot_release(CFTypeRef value)
{
    CFRelease(value);
}

typedef void (*window_server_snapshot_constraints_getter)(CFTypeRef iterator,
                                                           CGSize *minimum,
                                                           CGSize *maximum,
                                                           CGSize *current);

static window_server_snapshot_constraints_getter window_server_snapshot_constraints_impl;
static pthread_once_t window_server_snapshot_constraints_once = PTHREAD_ONCE_INIT;

static void window_server_snapshot_resolve_constraints(void)
{
    window_server_snapshot_constraints_impl = (window_server_snapshot_constraints_getter)
        dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetConstraints");
}

static bool window_server_snapshot_constraints(CFTypeRef iterator,
                                               struct window_constraint_observation *observation)
{
    pthread_once(&window_server_snapshot_constraints_once,
                 window_server_snapshot_resolve_constraints);
    if (!window_server_snapshot_constraints_impl || !observation) return false;
    *observation = (struct window_constraint_observation) {0};
    window_server_snapshot_constraints_impl(iterator, &observation->minimum,
                                            &observation->maximum, &observation->current);
    return true;
}

typedef float (*window_server_snapshot_alpha_getter)(CFTypeRef iterator);
typedef int (*window_server_snapshot_space_count_getter)(CFTypeRef iterator);

static window_server_snapshot_alpha_getter window_server_snapshot_alpha_impl;
static window_server_snapshot_space_count_getter window_server_snapshot_space_count_impl;
static pthread_once_t window_server_snapshot_optional_once = PTHREAD_ONCE_INIT;

// Optional iterator fields are resolved at runtime so a WindowServer without
// them leaves the fields invalid and callers keep their per-window queries.
static void window_server_snapshot_resolve_optional(void)
{
    window_server_snapshot_alpha_impl = (window_server_snapshot_alpha_getter)
        dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetAlpha");
    window_server_snapshot_space_count_impl = (window_server_snapshot_space_count_getter)
        dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetSpaceCount");
}

static bool window_server_snapshot_alpha(CFTypeRef iterator, float *alpha)
{
    pthread_once(&window_server_snapshot_optional_once, window_server_snapshot_resolve_optional);
    if (!window_server_snapshot_alpha_impl || !alpha) return false;
    *alpha = window_server_snapshot_alpha_impl(iterator);
    return true;
}

static bool window_server_snapshot_alpha_value_valid(float alpha)
{
    return isfinite(alpha) && alpha >= 0.0f && alpha <= 1.0f;
}

#define WINDOW_SERVER_SNAPSHOT_MAX_SPACE_COUNT 1024

static bool window_server_snapshot_space_count_value_valid(int space_count)
{
    /* Every WindowServer window belongs to at least one Space. Newer
     * WindowServer builds still export the optional getter but can return zero for every
     * record; treat that sentinel as unavailable so callers use the
     * authoritative SLSCopySpacesForWindows fallback. A large positive value is
     * likewise treated as an ABI mismatch rather than as sticky-window evidence. */
    return space_count > 0 && space_count <= WINDOW_SERVER_SNAPSHOT_MAX_SPACE_COUNT;
}

static bool window_server_snapshot_space_count(CFTypeRef iterator, int *space_count)
{
    pthread_once(&window_server_snapshot_optional_once, window_server_snapshot_resolve_optional);
    if (!window_server_snapshot_space_count_impl || !space_count) return false;
    *space_count = window_server_snapshot_space_count_impl(iterator);
    return window_server_snapshot_space_count_value_valid(*space_count);
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
    .iterator_constraints = window_server_snapshot_constraints,
    .iterator_corner_geometry = window_corner_geometry_collect_resolved,
    .iterator_alpha = window_server_snapshot_alpha,
    .iterator_space_count = window_server_snapshot_space_count,
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
    if (backend->iterator_constraints &&
        backend->iterator_constraints(iterator, &record->constraints)) {
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_CONSTRAINTS;
    }
    if ((record->valid_fields & WINDOW_SERVER_WINDOW_FIELD_ID) &&
        backend->iterator_corner_geometry &&
        backend->iterator_corner_geometry(iterator, record->id, &record->corner_geometry) &&
        window_corner_geometry_is_complete(&record->corner_geometry)) {
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_CORNER_GEOMETRY;
    }
    float alpha = 0.0f;
    if (backend->iterator_alpha && backend->iterator_alpha(iterator, &alpha) &&
        window_server_snapshot_alpha_value_valid(alpha)) {
        record->alpha = alpha;
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_ALPHA;
    }
    int space_count = 0;
    if (backend->iterator_space_count && backend->iterator_space_count(iterator, &space_count) &&
        window_server_snapshot_space_count_value_valid(space_count)) {
        record->space_count = space_count;
        record->valid_fields |= WINDOW_SERVER_WINDOW_FIELD_SPACE_COUNT;
    }
}

struct window_server_snapshot_owner
{
    void (*deallocate)(void *value);
    struct window_server_snapshot snapshot;
};

// Small snapshots are cheaper to scan. Larger ones are repeatedly searched by
// startup reconciliation and menu collection; index IDs without moving records.
#define WINDOW_SERVER_SNAPSHOT_INDEX_MIN_COUNT 128

struct window_server_snapshot_index_entry
{
    uint32_t id;
    uint32_t record_index;
};

_Static_assert(sizeof(struct window_server_window_record) %
               _Alignof(struct window_server_snapshot_index_entry) == 0,
               "snapshot records must align the trailing index");

static int window_server_snapshot_index_compare(const void *left, const void *right)
{
    const struct window_server_snapshot_index_entry *a = left;
    const struct window_server_snapshot_index_entry *b = right;
    if (a->id != b->id) return (a->id > b->id) - (a->id < b->id);
    // Match the linear scan's first-record behavior, including duplicate IDs.
    return (a->record_index > b->record_index) - (a->record_index < b->record_index);
}

static void window_server_snapshot_build_index(struct window_server_snapshot *snapshot,
                                               struct window_server_snapshot_index_entry *index)
{
    for (size_t i = 0; i < snapshot->count; ++i) {
        bool valid = snapshot->records[i].valid_fields & WINDOW_SERVER_WINDOW_FIELD_ID;
        // Invalid records sort after every real ID, including UINT32_MAX.
        // Real ordinals fit in uint32_t because creation accepts an int count.
        index[i] = (struct window_server_snapshot_index_entry) {
            .id = valid ? snapshot->records[i].id : UINT32_MAX,
            .record_index = valid ? (uint32_t) i : UINT32_MAX
        };
    }
    qsort(index, snapshot->count, sizeof(*index), window_server_snapshot_index_compare);
    snapshot->index = index;
}

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

    bool indexed = expected_count >= WINDOW_SERVER_SNAPSHOT_INDEX_MIN_COUNT;
    size_t entry_size = sizeof(struct window_server_window_record) +
                        (indexed ? sizeof(struct window_server_snapshot_index_entry) : 0);
    // The optional index shares the records' allocation and lifetime.
    struct window_server_window_record *records = backend->allocate((size_t) expected_count, entry_size);
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
    if (indexed) {
        window_server_snapshot_build_index(snapshot, (void *) (records + expected_count));
    }
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

const struct window_server_window_record *window_server_snapshot_find(const struct window_server_snapshot *snapshot,
                                                                      uint32_t window_id)
{
    if (!snapshot || !snapshot->records) return NULL;
    if (snapshot->index) {
        size_t low = 0, high = snapshot->count;
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            if (snapshot->index[middle].id < window_id) low = middle + 1;
            else high = middle;
        }
        if (low == snapshot->count || snapshot->index[low].id != window_id ||
            snapshot->index[low].record_index == UINT32_MAX) return NULL;
        return snapshot->records + snapshot->index[low].record_index;
    }
    for (size_t i = 0; i < snapshot->count; ++i) {
        const struct window_server_window_record *record = snapshot->records + i;
        if ((record->valid_fields & WINDOW_SERVER_WINDOW_FIELD_ID) && record->id == window_id) return record;
    }
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
