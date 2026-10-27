struct window_server_snapshot_fake_record
{
    uint32_t id;
    uint32_t parent_id;
    int level;
    uint64_t tags;
    uint64_t attributes;
    struct window_constraint_observation constraints;
    struct window_corner_geometry corner_geometry;
};

struct window_server_snapshot_fake_state
{
    struct window_server_snapshot_fake_record records[200];
    int record_count;
    int iterator_count;
    int advance_limit;
    int cursor;
    bool query_failure;
    bool iterator_failure;
    bool destroy_storage_on_release;
    int query_calls;
    int iterator_copy_calls;
    int iterator_count_calls;
    int iterator_advance_calls;
    int window_id_calls;
    int parent_id_calls;
    int level_calls;
    int tags_calls;
    int attributes_calls;
    int constraints_calls;
    bool constraints_available;
    int corner_geometry_calls;
    bool corner_geometry_available;
    int query_releases;
    int iterator_releases;
    int unknown_releases;
};

struct window_server_snapshot_fake_allocator
{
    size_t fail_ordinal;
    size_t allocation_attempts;
    size_t allocation_successes;
    size_t deallocation_calls;
    size_t outstanding_allocations;
    size_t current_bytes;
    size_t peak_bytes;
};

struct window_server_snapshot_fake_allocation
{
    max_align_t alignment;
    size_t size;
};

static struct window_server_snapshot_fake_state window_server_snapshot_fake;
static struct window_server_snapshot_fake_allocator window_server_snapshot_allocator;
static int window_server_snapshot_fake_query_token;
static int window_server_snapshot_fake_iterator_token;

static void *window_server_snapshot_fake_allocate(size_t count, size_t size)
{
    ++window_server_snapshot_allocator.allocation_attempts;
    if (window_server_snapshot_allocator.fail_ordinal ==
        window_server_snapshot_allocator.allocation_attempts) return NULL;
    if (count && size > SIZE_MAX / count) return NULL;
    size_t bytes = count * size;
    if (bytes > SIZE_MAX - sizeof(struct window_server_snapshot_fake_allocation)) return NULL;

    struct window_server_snapshot_fake_allocation *allocation =
        calloc(1, sizeof(*allocation) + bytes);
    if (!allocation) return NULL;
    allocation->size = bytes;
    ++window_server_snapshot_allocator.allocation_successes;
    ++window_server_snapshot_allocator.outstanding_allocations;
    window_server_snapshot_allocator.current_bytes += bytes;
    if (window_server_snapshot_allocator.current_bytes > window_server_snapshot_allocator.peak_bytes) {
        window_server_snapshot_allocator.peak_bytes = window_server_snapshot_allocator.current_bytes;
    }
    return allocation + 1;
}

static void window_server_snapshot_fake_deallocate(void *value)
{
    if (!value) return;
    struct window_server_snapshot_fake_allocation *allocation =
        (struct window_server_snapshot_fake_allocation *) value - 1;
    ++window_server_snapshot_allocator.deallocation_calls;
    --window_server_snapshot_allocator.outstanding_allocations;
    window_server_snapshot_allocator.current_bytes -= allocation->size;
    free(allocation);
}

static CFTypeRef window_server_snapshot_fake_query(int connection, CFArrayRef window_ids,
                                                   int expected_count)
{
    (void) connection;
    (void) window_ids;
    ++window_server_snapshot_fake.query_calls;
    window_server_snapshot_fake.cursor = 0;
    if (window_server_snapshot_fake.query_failure ||
        expected_count != window_server_snapshot_fake.record_count) return NULL;
    return (CFTypeRef) &window_server_snapshot_fake_query_token;
}

static CFTypeRef window_server_snapshot_fake_copy_iterator(CFTypeRef query)
{
    ++window_server_snapshot_fake.iterator_copy_calls;
    if (query != (CFTypeRef) &window_server_snapshot_fake_query_token ||
        window_server_snapshot_fake.iterator_failure) return NULL;
    return (CFTypeRef) &window_server_snapshot_fake_iterator_token;
}

static int window_server_snapshot_fake_iterator_count(CFTypeRef iterator)
{
    ++window_server_snapshot_fake.iterator_count_calls;
    return iterator == (CFTypeRef) &window_server_snapshot_fake_iterator_token ?
        window_server_snapshot_fake.iterator_count : -1;
}

static bool window_server_snapshot_fake_iterator_advance(CFTypeRef iterator)
{
    ++window_server_snapshot_fake.iterator_advance_calls;
    if (iterator != (CFTypeRef) &window_server_snapshot_fake_iterator_token) return false;
    if (window_server_snapshot_fake.cursor >= window_server_snapshot_fake.advance_limit) return false;
    ++window_server_snapshot_fake.cursor;
    return true;
}

static const struct window_server_snapshot_fake_record *window_server_snapshot_fake_current(void)
{
    int index = window_server_snapshot_fake.cursor - 1;
    if (index < 0 || index >= window_server_snapshot_fake.record_count) return NULL;
    return window_server_snapshot_fake.records + index;
}

static uint32_t window_server_snapshot_fake_window_id(CFTypeRef iterator)
{
    (void) iterator;
    ++window_server_snapshot_fake.window_id_calls;
    const struct window_server_snapshot_fake_record *record = window_server_snapshot_fake_current();
    return record ? record->id : 0;
}

static uint32_t window_server_snapshot_fake_parent_id(CFTypeRef iterator)
{
    (void) iterator;
    ++window_server_snapshot_fake.parent_id_calls;
    const struct window_server_snapshot_fake_record *record = window_server_snapshot_fake_current();
    return record ? record->parent_id : 0;
}

static int window_server_snapshot_fake_level(CFTypeRef iterator)
{
    (void) iterator;
    ++window_server_snapshot_fake.level_calls;
    const struct window_server_snapshot_fake_record *record = window_server_snapshot_fake_current();
    return record ? record->level : 0;
}

static uint64_t window_server_snapshot_fake_tags(CFTypeRef iterator)
{
    (void) iterator;
    ++window_server_snapshot_fake.tags_calls;
    const struct window_server_snapshot_fake_record *record = window_server_snapshot_fake_current();
    return record ? record->tags : 0;
}

static bool window_server_snapshot_fake_constraints(
    CFTypeRef iterator, struct window_constraint_observation *observation)
{
    (void) iterator;
    ++window_server_snapshot_fake.constraints_calls;
    if (!window_server_snapshot_fake.constraints_available || !observation) return false;
    const struct window_server_snapshot_fake_record *record = window_server_snapshot_fake_current();
    if (!record) return false;
    *observation = record->constraints;
    return true;
}

static uint64_t window_server_snapshot_fake_attributes(CFTypeRef iterator)
{
    (void) iterator;
    ++window_server_snapshot_fake.attributes_calls;
    const struct window_server_snapshot_fake_record *record = window_server_snapshot_fake_current();
    return record ? record->attributes : 0;
}

static bool window_server_snapshot_fake_corner_geometry(
    CFTypeRef iterator, uint32_t window_id, struct window_corner_geometry *geometry)
{
    (void) iterator;
    ++window_server_snapshot_fake.corner_geometry_calls;
    if (!window_server_snapshot_fake.corner_geometry_available || !geometry) return false;
    const struct window_server_snapshot_fake_record *record = window_server_snapshot_fake_current();
    if (!record || record->id != window_id ||
        !window_corner_geometry_is_complete(&record->corner_geometry)) return false;
    *geometry = record->corner_geometry;
    return true;
}

static void window_server_snapshot_fake_release(CFTypeRef value)
{
    if (value == (CFTypeRef) &window_server_snapshot_fake_iterator_token) {
        ++window_server_snapshot_fake.iterator_releases;
        if (window_server_snapshot_fake.destroy_storage_on_release) {
            memset(window_server_snapshot_fake.records, 0, sizeof(window_server_snapshot_fake.records));
        }
    } else if (value == (CFTypeRef) &window_server_snapshot_fake_query_token) {
        ++window_server_snapshot_fake.query_releases;
    } else {
        ++window_server_snapshot_fake.unknown_releases;
    }
}

static const struct window_server_snapshot_backend window_server_snapshot_fake_backend = {
    .query_windows = window_server_snapshot_fake_query,
    .copy_iterator = window_server_snapshot_fake_copy_iterator,
    .iterator_count = window_server_snapshot_fake_iterator_count,
    .iterator_advance = window_server_snapshot_fake_iterator_advance,
    .iterator_window_id = window_server_snapshot_fake_window_id,
    .iterator_parent_id = window_server_snapshot_fake_parent_id,
    .iterator_level = window_server_snapshot_fake_level,
    .iterator_tags = window_server_snapshot_fake_tags,
    .iterator_attributes = window_server_snapshot_fake_attributes,
    .iterator_constraints = window_server_snapshot_fake_constraints,
    .iterator_corner_geometry = window_server_snapshot_fake_corner_geometry,
    .release = window_server_snapshot_fake_release,
    .allocate = window_server_snapshot_fake_allocate,
    .deallocate = window_server_snapshot_fake_deallocate
};

static void window_server_snapshot_fake_begin(int count)
{
    memset(&window_server_snapshot_fake, 0, sizeof(window_server_snapshot_fake));
    memset(&window_server_snapshot_allocator, 0, sizeof(window_server_snapshot_allocator));
    window_server_snapshot_fake.record_count = count;
    window_server_snapshot_fake.iterator_count = count;
    window_server_snapshot_fake.advance_limit = count;
    for (int i = 0; i < count; ++i) {
        window_server_snapshot_fake.records[i] = (struct window_server_snapshot_fake_record) {
            .id = 1000u + (uint32_t) i,
            .parent_id = i ? 500u + (uint32_t) i : 0,
            .level = i - 2,
            .tags = UINT64_C(0x1000000000000000) | (uint64_t) i,
            .attributes = (uint64_t) (i % 4),
            .constraints = {
                .minimum = { 100 + i, 80 + i },
                .maximum = { 2000 + i, 1200 + i },
                .current = { 800 + i, 600 + i }
            },
            .corner_geometry = {
                .radius = { i, i + 1, i + 2, i + 3 },
                .valid_mask = WINDOW_CORNER_GEOMETRY_VALID_ALL,
                .count = WINDOW_CORNER_GEOMETRY_RADIUS_COUNT,
                .source = WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED
            }
        };
    }
    window_server_snapshot_test_backend = &window_server_snapshot_fake_backend;
}

static void window_server_snapshot_fake_end(void)
{
    window_server_snapshot_test_backend = NULL;
}

static CFArrayRef window_server_snapshot_fake_window_ids(int count)
{
    uint32_t ids[200];
    for (int i = 0; i < count; ++i) ids[i] = 1000u + (uint32_t) i;
    return cfarray_of_cfnumbers(ids, sizeof(ids[0]), count, kCFNumberSInt32Type);
}

TEST_FUNC(window_server_snapshot_copies_ordered_owned_records_and_empty_inventory,
{
    window_server_snapshot_fake_begin(3);
    window_server_snapshot_fake.destroy_storage_on_release = true;
    CFArrayRef ids = window_server_snapshot_fake_window_ids(3);
    struct window_server_snapshot *snapshot = window_server_snapshot_create(91, ids, 3);
    TEST_CHECK(snapshot != NULL, true);
    TEST_CHECK(snapshot->generation != 0, true);
    TEST_CHECK(snapshot->count, 3UL);
    TEST_CHECK(snapshot->records[0].id, 1000U);
    TEST_CHECK(snapshot->records[0].parent_id, 0U);
    TEST_CHECK(snapshot->records[0].level, -2);
    TEST_CHECK(snapshot->records[0].tags, UINT64_C(0x1000000000000000));
    TEST_CHECK(snapshot->records[0].attributes, 0ULL);
    TEST_CHECK(snapshot->records[1].id, 1001U);
    TEST_CHECK(snapshot->records[1].parent_id, 501U);
    TEST_CHECK(snapshot->records[2].id, 1002U);
    TEST_CHECK(snapshot->records[2].level, 0);
    TEST_CHECK(snapshot->records[2].valid_fields, (uint64_t) WINDOW_SERVER_WINDOW_FIELD_BASE);
    TEST_CHECK(window_server_snapshot_find(snapshot, 1001) == snapshot->records + 1, true);
    TEST_CHECK(window_server_snapshot_find(snapshot, 9999) == NULL, true);
    TEST_CHECK(window_server_snapshot_find(NULL, 1001) == NULL, true);
    TEST_CHECK(window_server_snapshot_fake.query_calls, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_copy_calls, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_count_calls, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_advance_calls, 4);
    TEST_CHECK(window_server_snapshot_fake.window_id_calls, 3);
    TEST_CHECK(window_server_snapshot_fake.parent_id_calls, 3);
    TEST_CHECK(window_server_snapshot_fake.level_calls, 3);
    TEST_CHECK(window_server_snapshot_fake.tags_calls, 3);
    TEST_CHECK(window_server_snapshot_fake.attributes_calls, 3);
    TEST_CHECK(window_server_snapshot_fake.iterator_releases, 1);
    TEST_CHECK(window_server_snapshot_fake.query_releases, 1);
    TEST_CHECK(window_server_snapshot_fake.unknown_releases, 0);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 2UL);
    CFRelease(ids);

    window_server_snapshot_fake_end();
    window_server_snapshot_destroy(snapshot);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    TEST_CHECK(window_server_snapshot_allocator.current_bytes, 0UL);
    TEST_CHECK(window_server_snapshot_allocator.deallocation_calls, 2UL);
    window_server_snapshot_destroy(NULL);

    window_server_snapshot_fake_begin(0);
    ids = window_server_snapshot_fake_window_ids(0);
    snapshot = window_server_snapshot_create(91, ids, 0);
    TEST_CHECK(snapshot != NULL, true);
    TEST_CHECK(snapshot->generation != 0, true);
    TEST_CHECK(snapshot->count, 0UL);
    TEST_CHECK(snapshot->records == NULL, true);
    TEST_CHECK(window_server_snapshot_fake.query_calls, 0);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 1UL);
    window_server_snapshot_destroy(snapshot);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();
});

TEST_FUNC(window_server_snapshot_valid_fields_distinguish_zero_from_unavailable,
{
    window_server_snapshot_fake_begin(1);
    memset(window_server_snapshot_fake.records, 0, sizeof(window_server_snapshot_fake.records));
    CFArrayRef ids = window_server_snapshot_fake_window_ids(1);
    struct window_server_snapshot *snapshot = window_server_snapshot_create(5, ids, 1);
    TEST_CHECK(snapshot != NULL, true);
    TEST_CHECK(snapshot->records[0].id, 0U);
    TEST_CHECK(snapshot->records[0].parent_id, 0U);
    TEST_CHECK(snapshot->records[0].level, 0);
    TEST_CHECK(snapshot->records[0].tags, 0ULL);
    TEST_CHECK(snapshot->records[0].attributes, 0ULL);
    TEST_CHECK(snapshot->records[0].valid_fields, (uint64_t) WINDOW_SERVER_WINDOW_FIELD_BASE);
    window_server_snapshot_destroy(snapshot);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(1);
    struct window_server_snapshot_backend base_only = window_server_snapshot_fake_backend;
    base_only.iterator_level = NULL;
    window_server_snapshot_test_backend = &base_only;
    ids = window_server_snapshot_fake_window_ids(1);
    snapshot = window_server_snapshot_create(5, ids, 1);
    TEST_CHECK(snapshot != NULL, true);
    TEST_CHECK(snapshot->records[0].level, 0);
    TEST_CHECK((snapshot->records[0].valid_fields & WINDOW_SERVER_WINDOW_FIELD_LEVEL) == 0, true);
    TEST_CHECK(snapshot->records[0].valid_fields,
               (uint64_t) (WINDOW_SERVER_WINDOW_FIELD_BASE & ~WINDOW_SERVER_WINDOW_FIELD_LEVEL));
    TEST_CHECK(window_server_snapshot_fake.level_calls, 0);
    window_server_snapshot_destroy(snapshot);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();
});

TEST_FUNC(window_server_snapshot_rejects_invalid_inputs_and_backend_failures,
{
    window_server_snapshot_fake_begin(2);
    CFArrayRef ids = window_server_snapshot_fake_window_ids(2);
    TEST_CHECK(window_server_snapshot_create(1, NULL, 2) == NULL, true);
    TEST_CHECK(window_server_snapshot_create(1, ids, -1) == NULL, true);
    TEST_CHECK(window_server_snapshot_create(1, ids, 1) == NULL, true);
    TEST_CHECK(window_server_snapshot_create(1, (CFArrayRef) CFSTR("not-an-array"), 2) == NULL, true);
    TEST_CHECK(window_server_snapshot_fake.query_calls, 0);
    TEST_CHECK(window_server_snapshot_allocator.allocation_attempts, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(2);
    window_server_snapshot_fake.query_failure = true;
    ids = window_server_snapshot_fake_window_ids(2);
    TEST_CHECK(window_server_snapshot_create(1, ids, 2) == NULL, true);
    TEST_CHECK(window_server_snapshot_fake.query_calls, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_copy_calls, 0);
    TEST_CHECK(window_server_snapshot_fake.query_releases, 0);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(2);
    window_server_snapshot_fake.iterator_failure = true;
    ids = window_server_snapshot_fake_window_ids(2);
    TEST_CHECK(window_server_snapshot_create(1, ids, 2) == NULL, true);
    TEST_CHECK(window_server_snapshot_fake.query_releases, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_releases, 0);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(2);
    window_server_snapshot_fake.iterator_count = 1;
    ids = window_server_snapshot_fake_window_ids(2);
    TEST_CHECK(window_server_snapshot_create(1, ids, 2) == NULL, true);
    TEST_CHECK(window_server_snapshot_fake.query_releases, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_releases, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_advance_calls, 0);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(2);
    window_server_snapshot_fake.advance_limit = 1;
    ids = window_server_snapshot_fake_window_ids(2);
    TEST_CHECK(window_server_snapshot_create(1, ids, 2) == NULL, true);
    TEST_CHECK(window_server_snapshot_fake.iterator_advance_calls, 2);
    TEST_CHECK(window_server_snapshot_fake.query_releases, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_releases, 1);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(2);
    window_server_snapshot_fake.advance_limit = 3;
    ids = window_server_snapshot_fake_window_ids(2);
    TEST_CHECK(window_server_snapshot_create(1, ids, 2) == NULL, true);
    TEST_CHECK(window_server_snapshot_fake.iterator_advance_calls, 3);
    TEST_CHECK(window_server_snapshot_fake.window_id_calls, 2);
    TEST_CHECK(window_server_snapshot_fake.query_releases, 1);
    TEST_CHECK(window_server_snapshot_fake.iterator_releases, 1);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();
});

TEST_FUNC(window_server_snapshot_allocation_failures_are_atomic_and_generation_is_owned,
{
    for (size_t fail_ordinal = 1; fail_ordinal <= 2; ++fail_ordinal) {
        window_server_snapshot_fake_begin(2);
        window_server_snapshot_allocator.fail_ordinal = fail_ordinal;
        CFArrayRef ids = window_server_snapshot_fake_window_ids(2);
        TEST_CHECK(window_server_snapshot_create(1, ids, 2) == NULL, true);
        TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
        TEST_CHECK(window_server_snapshot_allocator.current_bytes, 0UL);
        TEST_CHECK(window_server_snapshot_fake.query_calls, fail_ordinal == 1 ? 0 : 1);
        TEST_CHECK(window_server_snapshot_fake.query_releases, fail_ordinal == 1 ? 0 : 1);
        TEST_CHECK(window_server_snapshot_fake.iterator_releases, fail_ordinal == 1 ? 0 : 1);
        CFRelease(ids);
        window_server_snapshot_fake_end();
    }

    window_server_snapshot_fake_begin(1);
    CFArrayRef ids = window_server_snapshot_fake_window_ids(1);
    struct window_server_snapshot *first = window_server_snapshot_create(1, ids, 1);
    struct window_server_snapshot *second = window_server_snapshot_create(1, ids, 1);
    TEST_CHECK(first != NULL, true);
    TEST_CHECK(second != NULL, true);
    TEST_CHECK(second->generation > first->generation, true);
    TEST_CHECK(first->records[0].id, 1000U);
    TEST_CHECK(second->records[0].id, 1000U);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 4UL);
    window_server_snapshot_destroy(first);
    window_server_snapshot_destroy(second);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    TEST_CHECK(window_server_snapshot_allocator.current_bytes, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();
});

TEST_FUNC(window_server_snapshot_corner_geometry_is_optional_owned_pod,
{
    window_server_snapshot_fake_begin(2);
    window_server_snapshot_fake.corner_geometry_available = true;
    window_server_snapshot_fake.destroy_storage_on_release = true;
    CFArrayRef ids = window_server_snapshot_fake_window_ids(2);
    struct window_server_snapshot *snapshot = window_server_snapshot_create(1, ids, 2);
    TEST_CHECK(snapshot != NULL, true);
    if (snapshot) {
        TEST_CHECK(snapshot->records[0].valid_fields & WINDOW_SERVER_WINDOW_FIELD_CORNER_GEOMETRY,
                   WINDOW_SERVER_WINDOW_FIELD_CORNER_GEOMETRY);
        TEST_CHECK(snapshot->records[0].corner_geometry.radius[0], 0.0);
        TEST_CHECK(snapshot->records[0].corner_geometry.radius[3], 3.0);
        TEST_CHECK(snapshot->records[1].corner_geometry.radius[0], 1.0);
        TEST_CHECK(snapshot->records[1].corner_geometry.radius[3], 4.0);
        TEST_CHECK(window_corner_geometry_is_complete(&snapshot->records[1].corner_geometry), true);
    }
    TEST_CHECK(window_server_snapshot_fake.corner_geometry_calls, 2);
    window_server_snapshot_destroy(snapshot);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(1);
    window_server_snapshot_fake.corner_geometry_available = true;
    window_server_snapshot_fake.records[0].corner_geometry.valid_mask = 0;
    ids = window_server_snapshot_fake_window_ids(1);
    snapshot = window_server_snapshot_create(1, ids, 1);
    TEST_CHECK(snapshot != NULL, true);
    if (snapshot) {
        TEST_CHECK(snapshot->records[0].valid_fields & WINDOW_SERVER_WINDOW_FIELD_CORNER_GEOMETRY, 0ULL);
        TEST_CHECK(window_corner_geometry_is_complete(&snapshot->records[0].corner_geometry), false);
    }
    TEST_CHECK(window_server_snapshot_fake.corner_geometry_calls, 1);
    window_server_snapshot_destroy(snapshot);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(1);
    struct window_server_snapshot_backend without_corners = window_server_snapshot_fake_backend;
    without_corners.iterator_corner_geometry = NULL;
    window_server_snapshot_test_backend = &without_corners;
    ids = window_server_snapshot_fake_window_ids(1);
    snapshot = window_server_snapshot_create(1, ids, 1);
    TEST_CHECK(snapshot != NULL, true);
    if (snapshot) {
        TEST_CHECK(snapshot->records[0].valid_fields & WINDOW_SERVER_WINDOW_FIELD_CORNER_GEOMETRY, 0ULL);
    }
    TEST_CHECK(window_server_snapshot_fake.corner_geometry_calls, 0);
    window_server_snapshot_destroy(snapshot);
    CFRelease(ids);
    window_server_snapshot_fake_end();
})

TEST_FUNC(window_server_snapshot_constraints_are_optional_owned_pod,
{
    window_server_snapshot_fake_begin(2);
    window_server_snapshot_fake.constraints_available = true;
    window_server_snapshot_fake.records[1].constraints.maximum = CGSizeMake(50, 40);
    CFArrayRef ids = window_server_snapshot_fake_window_ids(2);
    struct window_server_snapshot *snapshot = window_server_snapshot_create(1, ids, 2);
    TEST_CHECK(snapshot != NULL, true);
    if (snapshot) {
        TEST_CHECK(snapshot->records[0].valid_fields & WINDOW_SERVER_WINDOW_FIELD_CONSTRAINTS,
                   WINDOW_SERVER_WINDOW_FIELD_CONSTRAINTS);
        TEST_CHECK(snapshot->records[0].constraints.minimum.width, 100.0);
        TEST_CHECK(snapshot->records[0].constraints.current.height, 600.0);
        TEST_CHECK(snapshot->records[1].constraints.maximum.width, 50.0);
        TEST_CHECK(snapshot->records[1].constraints.minimum.width, 101.0);
    }
    TEST_CHECK(window_server_snapshot_fake.constraints_calls, 2);
    window_server_snapshot_destroy(snapshot);
    TEST_CHECK(window_server_snapshot_allocator.outstanding_allocations, 0UL);
    CFRelease(ids);
    window_server_snapshot_fake_end();

    window_server_snapshot_fake_begin(1);
    struct window_server_snapshot_backend without_constraints = window_server_snapshot_fake_backend;
    without_constraints.iterator_constraints = NULL;
    window_server_snapshot_test_backend = &without_constraints;
    ids = window_server_snapshot_fake_window_ids(1);
    snapshot = window_server_snapshot_create(1, ids, 1);
    TEST_CHECK(snapshot != NULL, true);
    if (snapshot) {
        TEST_CHECK(snapshot->records[0].valid_fields & WINDOW_SERVER_WINDOW_FIELD_CONSTRAINTS, 0ULL);
    }
    TEST_CHECK(window_server_snapshot_fake.constraints_calls, 0);
    window_server_snapshot_destroy(snapshot);
    CFRelease(ids);
    window_server_snapshot_fake_end();
});

static int window_server_snapshot_benchmark_compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *) left;
    uint64_t b = *(const uint64_t *) right;
    return (a > b) - (a < b);
}

static bool window_server_snapshot_benchmark_case(int window_count, bool constraints_available)
{
    enum { sample_count = 20, iterations_per_sample = 200 };
    uint64_t elapsed[sample_count];
    uint64_t checksum = 0;
    int successes = 0;
    window_server_snapshot_fake_begin(window_count);
    window_server_snapshot_fake.constraints_available = constraints_available;
    CFArrayRef ids = window_server_snapshot_fake_window_ids(window_count);

    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int iteration = 0; iteration < iterations_per_sample; ++iteration) {
            struct window_server_snapshot *snapshot = window_server_snapshot_create(77, ids, window_count);
            if (snapshot) {
                ++successes;
                checksum += snapshot->records[0].id;
                checksum += snapshot->records[snapshot->count - 1].id;
            }
            window_server_snapshot_destroy(snapshot);
        }
        elapsed[sample] = (read_os_timer() - start) / iterations_per_sample;
    }

    qsort(elapsed, sample_count, sizeof(elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    int total = sample_count * iterations_per_sample;
    int p95_index = (sample_count * 95 + 99) / 100 - 1;
    int field_getter_calls = window_server_snapshot_fake.window_id_calls +
                             window_server_snapshot_fake.parent_id_calls +
                             window_server_snapshot_fake.level_calls +
                             window_server_snapshot_fake.tags_calls +
                             window_server_snapshot_fake.attributes_calls;
    printf("{\"case\":\"window-server-snapshot\",\"windows\":%d,\"iterations\":%d,"
           "\"constraints_available\":%s,\"p50_ns\":%llu,\"p95_ns\":%llu,\"query_calls\":%d,"
           "\"iterator_copy_calls\":%d,\"advance_calls\":%d,\"field_getter_calls\":%d,"
           "\"constraint_getter_calls\":%d,\"corner_geometry_getter_calls\":%d,"
           "\"peak_bytes\":%zu,\"retained_bytes\":%zu,\"checksum\":%llu}\n",
           window_count, total, constraints_available ? "true" : "false",
           (unsigned long long) elapsed[sample_count / 2],
           (unsigned long long) elapsed[p95_index],
           window_server_snapshot_fake.query_calls,
           window_server_snapshot_fake.iterator_copy_calls,
           window_server_snapshot_fake.iterator_advance_calls,
           field_getter_calls,
           window_server_snapshot_fake.constraints_calls,
           window_server_snapshot_fake.corner_geometry_calls,
           window_server_snapshot_allocator.peak_bytes,
           window_server_snapshot_allocator.current_bytes,
           (unsigned long long) checksum);

    size_t expected_peak = sizeof(struct window_server_snapshot_owner) +
                           (size_t) window_count * sizeof(struct window_server_window_record);
    bool valid = successes == total &&
                 window_server_snapshot_fake.query_calls == total &&
                 window_server_snapshot_fake.iterator_copy_calls == total &&
                 window_server_snapshot_fake.iterator_count_calls == total &&
                 window_server_snapshot_fake.iterator_advance_calls == total * (window_count + 1) &&
                 field_getter_calls == total * window_count * 5 &&
                 window_server_snapshot_fake.constraints_calls == total * window_count &&
                 window_server_snapshot_fake.corner_geometry_calls == total * window_count &&
                 window_server_snapshot_fake.query_releases == total &&
                 window_server_snapshot_fake.iterator_releases == total &&
                 window_server_snapshot_allocator.allocation_attempts == (size_t) total * 2 &&
                 window_server_snapshot_allocator.deallocation_calls == (size_t) total * 2 &&
                 window_server_snapshot_allocator.peak_bytes == expected_peak &&
                 window_server_snapshot_allocator.current_bytes == 0 &&
                 window_server_snapshot_allocator.outstanding_allocations == 0;

    CFRelease(ids);
    window_server_snapshot_fake_end();
    return valid;
}

static bool window_server_snapshot_benchmark_run(void)
{
    bool valid = true;
    valid &= window_server_snapshot_benchmark_case(10, false);
    valid &= window_server_snapshot_benchmark_case(50, false);
    valid &= window_server_snapshot_benchmark_case(200, false);
    valid &= window_server_snapshot_benchmark_case(10, true);
    valid &= window_server_snapshot_benchmark_case(50, true);
    valid &= window_server_snapshot_benchmark_case(200, true);
    return valid;
}
