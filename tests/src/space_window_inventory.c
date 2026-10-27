struct space_window_inventory_test_state
{
    CFTypeRef candidate_result;
    CFIndex candidate_count_override;
    bool snapshot_available;
    bool destroy_scribbles_records;
    struct window_server_window_record records[1000];
    struct window_server_snapshot snapshot;
    int candidate_calls;
    int snapshot_calls;
    int destroy_calls;
    int candidate_connection;
    uint32_t candidate_owner;
    uint32_t candidate_options;
    uint64_t candidate_set_tags;
    uint64_t candidate_clear_tags;
    CFIndex candidate_space_count;
    uint64_t candidate_spaces[4];
    int snapshot_connection;
    CFArrayRef snapshot_candidates;
    int snapshot_expected_count;
};

struct space_window_inventory_test_fixture
{
    struct table windows;
    int connection;
    CFArrayRef (*copy_candidates)(int, uint32_t, CFArrayRef, uint32_t, uint64_t *, uint64_t *);
    CFIndex (*candidate_count)(CFArrayRef);
    struct window_server_snapshot *(*capture_snapshot)(int, CFArrayRef, int);
    void (*destroy_snapshot)(struct window_server_snapshot *);
};

static struct space_window_inventory_test_state space_window_inventory_test;

static CFArrayRef space_window_inventory_copy_candidates(int connection, uint32_t owner,
                                                         CFArrayRef spaces, uint32_t options,
                                                         uint64_t *set_tags, uint64_t *clear_tags)
{
    ++space_window_inventory_test.candidate_calls;
    space_window_inventory_test.candidate_connection = connection;
    space_window_inventory_test.candidate_owner = owner;
    space_window_inventory_test.candidate_options = options;
    space_window_inventory_test.candidate_set_tags = set_tags ? *set_tags : UINT64_MAX;
    space_window_inventory_test.candidate_clear_tags = clear_tags ? *clear_tags : UINT64_MAX;
    space_window_inventory_test.candidate_space_count = CFArrayGetCount(spaces);
    for (CFIndex i = 0; i < space_window_inventory_test.candidate_space_count && i < 4; ++i) {
        CFNumberGetValue(CFArrayGetValueAtIndex(spaces, i), kCFNumberSInt64Type,
                         space_window_inventory_test.candidate_spaces + i);
    }
    return space_window_inventory_test.candidate_result ?
        (CFArrayRef) CFRetain(space_window_inventory_test.candidate_result) : NULL;
}

static CFIndex space_window_inventory_candidate_count(CFArrayRef candidates)
{
    return space_window_inventory_test.candidate_count_override >= 0 ?
        space_window_inventory_test.candidate_count_override : CFArrayGetCount(candidates);
}

static struct window_server_snapshot *space_window_inventory_capture_snapshot(int connection,
                                                                               CFArrayRef candidates,
                                                                               int expected_count)
{
    ++space_window_inventory_test.snapshot_calls;
    space_window_inventory_test.snapshot_connection = connection;
    space_window_inventory_test.snapshot_candidates = candidates;
    space_window_inventory_test.snapshot_expected_count = expected_count;
    return space_window_inventory_test.snapshot_available ? &space_window_inventory_test.snapshot : NULL;
}

static void space_window_inventory_destroy_snapshot(struct window_server_snapshot *snapshot)
{
    if (snapshot == &space_window_inventory_test.snapshot) {
        ++space_window_inventory_test.destroy_calls;
        if (space_window_inventory_test.destroy_scribbles_records) {
            memset(space_window_inventory_test.records, 0, sizeof(space_window_inventory_test.records));
        }
    }
}

static void space_window_inventory_test_begin(struct space_window_inventory_test_fixture *fixture)
{
    fixture->windows = g_window_manager.window;
    fixture->connection = g_connection;
    fixture->copy_candidates = space_window_copy_candidates;
    fixture->candidate_count = space_window_candidate_count;
    fixture->capture_snapshot = space_window_capture_snapshot;
    fixture->destroy_snapshot = space_window_destroy_snapshot;

    table_init(&g_window_manager.window, 64, hash_wm, compare_wm);
    g_connection = 313;
    memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
    space_window_inventory_test.candidate_count_override = -1;
    space_window_inventory_test.snapshot_available = true;
    space_window_inventory_test.snapshot.records = space_window_inventory_test.records;
    space_window_copy_candidates = space_window_inventory_copy_candidates;
    space_window_candidate_count = space_window_inventory_candidate_count;
    space_window_capture_snapshot = space_window_inventory_capture_snapshot;
    space_window_destroy_snapshot = space_window_inventory_destroy_snapshot;
    test_temp_storage_ensure(KILOBYTES(64));
    ts_reset();
}

static void space_window_inventory_test_end(struct space_window_inventory_test_fixture *fixture)
{
    space_window_copy_candidates = fixture->copy_candidates;
    space_window_candidate_count = fixture->candidate_count;
    space_window_capture_snapshot = fixture->capture_snapshot;
    space_window_destroy_snapshot = fixture->destroy_snapshot;
    g_connection = fixture->connection;
    table_free(&g_window_manager.window);
    g_window_manager.window = fixture->windows;
    memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
}

static CFArrayRef space_window_inventory_candidates(const struct window_server_window_record *records,
                                                    int count)
{
    uint32_t ids[1000];
    for (int i = 0; i < count; ++i) ids[i] = records[i].id;
    return cfarray_of_cfnumbers(ids, sizeof(ids[0]), count, kCFNumberSInt32Type);
}

static void space_window_inventory_set_snapshot(struct window_server_window_record *records, int count)
{
    memcpy(space_window_inventory_test.records, records, sizeof(*records) * (size_t) count);
    space_window_inventory_test.snapshot = (struct window_server_snapshot) {
        .generation = 1,
        .records = space_window_inventory_test.records,
        .count = (size_t) count
    };
}

static bool space_window_inventory_legacy_eligible(const struct window_server_window_record *record,
                                                   struct window *window, bool include_minimized)
{
    if (include_minimized) {
        if (window) return true;
        if (record->parent_id == 0 &&
            (record->level == 0 || record->level == 3 || record->level == 8)) {
            if (((record->attributes & 0x2) || (record->tags & 0x400000000000000)) &&
                ((record->tags & 0x1) || ((record->tags & 0x2) && (record->tags & 0x80000000)))) {
                return true;
            }
            if ((record->attributes == 0x0 || record->attributes == 0x1) &&
                ((record->tags & 0x1000000000000000) || (record->tags & 0x300000000000000)) &&
                ((record->tags & 0x1) || ((record->tags & 0x2) && (record->tags & 0x80000000)))) {
                return true;
            }
        }
    } else {
        if (window && !window_check_flag(window, WINDOW_MINIMIZE)) return true;
        if (record->parent_id == 0 &&
            (record->level == 0 || record->level == 3 || record->level == 8) &&
            ((record->attributes & 0x2) || (record->tags & 0x400000000000000)) &&
            ((record->tags & 0x1) || ((record->tags & 0x2) && (record->tags & 0x80000000)))) {
            return true;
        }
    }
    return false;
}

TEST_FUNC(space_window_inventory_snapshot_predicate_preserves_options_order_and_ownership,
{
    struct space_window_inventory_test_fixture fixture;
    space_window_inventory_test_begin(&fixture);
    struct window known[] = {
        { .id = 10 },
        { .id = 11, .flags = WINDOW_MINIMIZE }
    };
    table_add(&g_window_manager.window, &known[0].id, &known[0]);
    table_add(&g_window_manager.window, &known[1].id, &known[1]);

    const uint64_t valid = WINDOW_SERVER_WINDOW_FIELD_BASE;
    struct window_server_window_record records[] = {
        { .id = 10, .valid_fields = valid },
        { .id = 11, .level = 0, .tags = 0x1, .attributes = 0x2, .valid_fields = valid },
        { .id = 20, .level = 0, .tags = 0x1, .attributes = 0x2, .valid_fields = valid },
        { .id = 21, .level = 3, .tags = 0x400000000000001, .valid_fields = valid },
        { .id = 22, .level = 8, .tags = 0x80000002, .attributes = 0x2, .valid_fields = valid },
        { .id = 23, .parent_id = 9, .level = 0, .tags = 0x1, .attributes = 0x2, .valid_fields = valid },
        { .id = 24, .level = 2, .tags = 0x1, .attributes = 0x2, .valid_fields = valid },
        { .id = 25, .level = 0, .valid_fields = valid },
        { .id = 26, .level = 0, .tags = 0x1000000000000001, .attributes = 0x1, .valid_fields = valid },
        { .id = 27, .level = 0, .valid_fields = valid }
    };
    space_window_inventory_set_snapshot(records, (int) array_count(records));
    CFArrayRef candidates = space_window_inventory_candidates(records, (int) array_count(records));
    space_window_inventory_test.candidate_result = candidates;
    uint64_t spaces[] = { 501, 502 };
    int count = -1;
    bool success = false;
    uint32_t *output = space_window_list_for_connection_with_status(spaces, 2, 44, &count, false, &success);
    uint32_t expected_without_minimized[] = { 10, 11, 20, 21, 22 };
    TEST_CHECK(success, true);
    TEST_CHECK(count, (int) array_count(expected_without_minimized));
    TEST_CHECK(memcmp(output, expected_without_minimized, sizeof(expected_without_minimized)), 0);
    TEST_CHECK(space_window_inventory_test.candidate_calls, 1);
    TEST_CHECK(space_window_inventory_test.candidate_connection, 313);
    TEST_CHECK(space_window_inventory_test.candidate_owner, 44U);
    TEST_CHECK(space_window_inventory_test.candidate_options, 0x2U);
    TEST_CHECK(space_window_inventory_test.candidate_set_tags, 0ULL);
    TEST_CHECK(space_window_inventory_test.candidate_clear_tags, 0ULL);
    TEST_CHECK(space_window_inventory_test.candidate_space_count, 2L);
    TEST_CHECK(space_window_inventory_test.candidate_spaces[0], 501ULL);
    TEST_CHECK(space_window_inventory_test.candidate_spaces[1], 502ULL);
    TEST_CHECK(space_window_inventory_test.snapshot_calls, 1);
    TEST_CHECK(space_window_inventory_test.snapshot_connection, 313);
    TEST_CHECK(space_window_inventory_test.snapshot_candidates == candidates, true);
    TEST_CHECK(space_window_inventory_test.snapshot_expected_count, (int) array_count(records));
    TEST_CHECK(space_window_inventory_test.destroy_calls, 1);

    ts_reset();
    space_window_inventory_test.candidate_calls = 0;
    space_window_inventory_test.snapshot_calls = 0;
    space_window_inventory_test.destroy_calls = 0;
    space_window_inventory_set_snapshot(records, (int) array_count(records));
    output = space_window_list_for_connection_with_status(spaces, 2, 44, &count, true, &success);
    uint32_t expected_with_minimized[] = { 10, 11, 20, 21, 22, 26 };
    TEST_CHECK(success, true);
    TEST_CHECK(count, (int) array_count(expected_with_minimized));
    TEST_CHECK(memcmp(output, expected_with_minimized, sizeof(expected_with_minimized)), 0);
    TEST_CHECK(space_window_inventory_test.candidate_options, 0x7U);
    TEST_CHECK(space_window_inventory_test.snapshot_calls, 1);
    TEST_CHECK(space_window_inventory_test.destroy_calls, 1);

    CFRelease(candidates);
    space_window_inventory_test_end(&fixture);
});

TEST_FUNC(space_window_inventory_preserves_duplicates_empty_success_and_independent_output,
{
    struct space_window_inventory_test_fixture fixture;
    space_window_inventory_test_begin(&fixture);
    const uint64_t valid = WINDOW_SERVER_WINDOW_FIELD_BASE;
    struct window_server_window_record records[] = {
        { .id = 30, .level = 0, .tags = 0x1, .attributes = 0x2, .valid_fields = valid },
        { .id = 30, .level = 3, .tags = 0x400000000000001, .valid_fields = valid },
        { .id = 31, .level = 0, .valid_fields = valid },
        { .id = 32, .level = 8, .tags = 0x80000002, .attributes = 0x2, .valid_fields = valid }
    };
    space_window_inventory_set_snapshot(records, (int) array_count(records));
    CFArrayRef candidates = space_window_inventory_candidates(records, (int) array_count(records));
    space_window_inventory_test.candidate_result = candidates;
    space_window_inventory_test.destroy_scribbles_records = true;
    uint64_t sid = 99;
    int count = 0;
    bool success = false;
    uint32_t *output = space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success);
    uint32_t expected[] = { 30, 30, 32 };
    TEST_CHECK(success, true);
    TEST_CHECK(count, 3);
    TEST_CHECK(memcmp(output, expected, sizeof(expected)), 0);
    TEST_CHECK(space_window_inventory_test.destroy_calls, 1);
    TEST_CHECK(space_window_inventory_test.records[0].id, 0U);
    CFRelease(candidates);

    ts_reset();
    memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
    space_window_inventory_test.candidate_count_override = -1;
    space_window_inventory_test.snapshot_available = true;
    candidates = space_window_inventory_candidates(NULL, 0);
    space_window_inventory_test.candidate_result = candidates;
    count = 55;
    success = false;
    output = space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success);
    TEST_CHECK(output == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(success, true);
    TEST_CHECK(space_window_inventory_test.snapshot_calls, 0);
    TEST_CHECK(space_window_inventory_test.destroy_calls, 0);
    count = 55;
    output = space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, NULL);
    TEST_CHECK(output == NULL, true);
    TEST_CHECK(count, 0);
    CFRelease(candidates);

    space_window_inventory_test_end(&fixture);
});

TEST_FUNC(space_window_inventory_fails_closed_for_candidates_snapshots_and_required_fields,
{
    struct space_window_inventory_test_fixture fixture;
    space_window_inventory_test_begin(&fixture);
    uint64_t sid = 77;
    int count = 9;
    bool success = true;

    space_window_inventory_test.candidate_result = NULL;
    TEST_CHECK(space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(success, false);
    TEST_CHECK(space_window_inventory_test.snapshot_calls, 0);

    memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
    space_window_inventory_test.candidate_count_override = -1;
    space_window_inventory_test.snapshot_available = true;
    space_window_inventory_test.candidate_result = CFSTR("wrong-type");
    count = 9;
    success = true;
    TEST_CHECK(space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(success, false);
    TEST_CHECK(space_window_inventory_test.snapshot_calls, 0);

    const uint64_t valid = WINDOW_SERVER_WINDOW_FIELD_BASE;
    struct window_server_window_record records[] = {
        { .id = 40, .level = 0, .tags = 0x1, .attributes = 0x2, .valid_fields = valid },
        { .id = 41, .level = 3, .tags = 0x400000000000001, .valid_fields = valid }
    };
    CFArrayRef candidates = space_window_inventory_candidates(records, 2);

    memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
    space_window_inventory_test.candidate_count_override = (CFIndex) INT_MAX + 1;
    space_window_inventory_test.snapshot_available = true;
    space_window_inventory_test.candidate_result = candidates;
    count = 9;
    success = true;
    TEST_CHECK(space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(success, false);
    TEST_CHECK(space_window_inventory_test.snapshot_calls, 0);

    memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
    space_window_inventory_test.candidate_count_override = -1;
    space_window_inventory_test.candidate_result = candidates;
    space_window_inventory_test.snapshot_available = false;
    count = 9;
    success = true;
    TEST_CHECK(space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(success, false);
    TEST_CHECK(space_window_inventory_test.snapshot_calls, 1);
    TEST_CHECK(space_window_inventory_test.destroy_calls, 0);

    for (int count_delta = -1; count_delta <= 1; count_delta += 2) {
        memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
        space_window_inventory_test.candidate_count_override = -1;
        space_window_inventory_test.candidate_result = candidates;
        space_window_inventory_test.snapshot_available = true;
        space_window_inventory_set_snapshot(records, 2);
        space_window_inventory_test.snapshot.count = (size_t) (2 + count_delta);
        count = 9;
        success = true;
        TEST_CHECK(space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success) == NULL, true);
        TEST_CHECK(count, 0);
        TEST_CHECK(success, false);
        TEST_CHECK(space_window_inventory_test.destroy_calls, 1);
    }

    const uint64_t fields[] = {
        WINDOW_SERVER_WINDOW_FIELD_ID,
        WINDOW_SERVER_WINDOW_FIELD_PARENT_ID,
        WINDOW_SERVER_WINDOW_FIELD_LEVEL,
        WINDOW_SERVER_WINDOW_FIELD_TAGS,
        WINDOW_SERVER_WINDOW_FIELD_ATTRIBUTES
    };
    for (size_t i = 0; i < array_count(fields); ++i) {
        memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
        space_window_inventory_test.candidate_count_override = -1;
        space_window_inventory_test.candidate_result = candidates;
        space_window_inventory_test.snapshot_available = true;
        space_window_inventory_set_snapshot(records, 2);
        space_window_inventory_test.records[1].valid_fields &= ~fields[i];
        ts_reset();
        count = 9;
        success = true;
        TEST_CHECK(space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success) == NULL, true);
        TEST_CHECK(count, 0);
        TEST_CHECK(success, false);
        TEST_CHECK(space_window_inventory_test.destroy_calls, 1);
    }

    memset(&space_window_inventory_test, 0, sizeof(space_window_inventory_test));
    space_window_inventory_test.candidate_count_override = -1;
    space_window_inventory_test.candidate_result = candidates;
    space_window_inventory_test.snapshot_available = true;
    space_window_inventory_set_snapshot(records, 2);
    space_window_inventory_test.records[0].valid_fields |= UINT64_C(1) << 63;
    ts_reset();
    count = 9;
    success = false;
    uint32_t *output = space_window_list_for_connection_with_status(&sid, 1, 0, &count, false, &success);
    TEST_CHECK(success, true);
    TEST_CHECK(count, 2);
    TEST_CHECK(output[0], 40U);
    TEST_CHECK(output[1], 41U);
    TEST_CHECK(space_window_inventory_test.destroy_calls, 1);

    CFRelease(candidates);
    space_window_inventory_test_end(&fixture);
});

static uint32_t space_window_inventory_lcg(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void space_window_inventory_generate(struct window_server_window_record *records,
                                            struct window *windows, bool *known, int count)
{
    uint32_t state = 0x71a5b39u;
    for (int i = 0; i < count; ++i) {
        uint32_t bits = space_window_inventory_lcg(&state);
        records[i] = (struct window_server_window_record) {
            .id = 2000u + (uint32_t) i,
            .parent_id = (bits & 3u) ? (uint32_t) (bits >> 8) : 0,
            .level = (int) ((bits >> 4) % 10),
            .tags = ((uint64_t) space_window_inventory_lcg(&state) << 32) |
                    space_window_inventory_lcg(&state),
            .attributes = space_window_inventory_lcg(&state) & 3u,
            .valid_fields = WINDOW_SERVER_WINDOW_FIELD_BASE
        };
        known[i] = (i % 7) == 0;
        windows[i] = (struct window) {
            .id = records[i].id,
            .flags = (i % 14) == 0 ? WINDOW_MINIMIZE : 0
        };
    }
}

TEST_FUNC(space_window_inventory_fixed_seed_matches_frozen_legacy_filter,
{
    struct space_window_inventory_test_fixture fixture;
    space_window_inventory_test_begin(&fixture);
    enum { record_count = 256 };
    struct window_server_window_record records[record_count];
    struct window windows[record_count];
    bool known[record_count];
    space_window_inventory_generate(records, windows, known, record_count);
    for (int i = 0; i < record_count; ++i) {
        if (known[i]) table_add(&g_window_manager.window, &windows[i].id, &windows[i]);
    }
    space_window_inventory_set_snapshot(records, record_count);
    CFArrayRef candidates = space_window_inventory_candidates(records, record_count);
    space_window_inventory_test.candidate_result = candidates;
    uint64_t sid = 123;

    for (int mode = 0; mode < 2; ++mode) {
        uint32_t expected[record_count];
        int expected_count = 0;
        for (int i = 0; i < record_count; ++i) {
            struct window *window = known[i] ? windows + i : NULL;
            if (space_window_inventory_legacy_eligible(records + i, window, mode != 0)) {
                expected[expected_count++] = records[i].id;
            }
        }

        ts_reset();
        space_window_inventory_set_snapshot(records, record_count);
        int count = -1;
        bool success = false;
        uint32_t *output = space_window_list_for_connection_with_status(&sid, 1, 0, &count,
                                                                         mode != 0, &success);
        TEST_CHECK(success, true);
        TEST_CHECK(count, expected_count);
        TEST_CHECK(memcmp(output, expected, sizeof(*expected) * (size_t) expected_count), 0);
    }

    CFRelease(candidates);
    space_window_inventory_test_end(&fixture);
});

static int space_window_inventory_benchmark_compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *) left;
    uint64_t b = *(const uint64_t *) right;
    return (a > b) - (a < b);
}

static bool space_window_inventory_benchmark_case(int record_count, bool include_minimized)
{
    enum { sample_count = 20, iterations_per_sample = 500 };
    struct window_server_window_record records[1000];
    struct window windows[1000];
    bool known[1000];
    uint32_t legacy_output[1000];
    uint32_t snapshot_output[1000];
    uint64_t legacy_elapsed[sample_count];
    uint64_t snapshot_elapsed[sample_count];
    uint64_t checksum = 0;
    space_window_inventory_generate(records, windows, known, record_count);

    int legacy_count = 0;
    int snapshot_count = 0;
    for (int i = 0; i < record_count; ++i) {
        struct window *window = known[i] ? windows + i : NULL;
        if (space_window_inventory_legacy_eligible(records + i, window, include_minimized)) {
            legacy_output[legacy_count++] = records[i].id;
        }
        if (space_window_record_is_eligible(records + i, window, include_minimized)) {
            snapshot_output[snapshot_count++] = records[i].id;
        }
    }
    bool equal = legacy_count == snapshot_count &&
                 memcmp(legacy_output, snapshot_output, sizeof(*legacy_output) * (size_t) legacy_count) == 0;

    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int iteration = 0; iteration < iterations_per_sample; ++iteration) {
            int count = 0;
            for (int i = 0; i < record_count; ++i) {
                struct window *window = known[i] ? windows + i : NULL;
                if (space_window_inventory_legacy_eligible(records + i, window, include_minimized)) {
                    legacy_output[count++] = records[i].id;
                }
            }
            checksum += (uint64_t) count;
        }
        legacy_elapsed[sample] = (read_os_timer() - start) /
                                 ((uint64_t) iterations_per_sample * (uint64_t) record_count);

        start = read_os_timer();
        for (int iteration = 0; iteration < iterations_per_sample; ++iteration) {
            int count = 0;
            for (int i = 0; i < record_count; ++i) {
                struct window *window = known[i] ? windows + i : NULL;
                if (space_window_record_is_eligible(records + i, window, include_minimized)) {
                    snapshot_output[count++] = records[i].id;
                }
            }
            checksum += (uint64_t) count;
        }
        snapshot_elapsed[sample] = (read_os_timer() - start) /
                                   ((uint64_t) iterations_per_sample * (uint64_t) record_count);
    }

    qsort(legacy_elapsed, sample_count, sizeof(legacy_elapsed[0]), space_window_inventory_benchmark_compare_u64);
    qsort(snapshot_elapsed, sample_count, sizeof(snapshot_elapsed[0]), space_window_inventory_benchmark_compare_u64);
    int p95_index = (sample_count * 95 + 99) / 100 - 1;
    printf("{\"case\":\"space-window-filter\",\"windows\":%d,\"include_minimized\":%s,"
           "\"legacy_p50_ns_per_record\":%llu,\"legacy_p95_ns_per_record\":%llu,"
           "\"snapshot_p50_ns_per_record\":%llu,\"snapshot_p95_ns_per_record\":%llu,"
           "\"scratch_allocations\":1,\"scratch_bytes\":%zu,\"output_equal\":%s,"
           "\"checksum\":%llu}\n",
           record_count, include_minimized ? "true" : "false",
           (unsigned long long) legacy_elapsed[sample_count / 2],
           (unsigned long long) legacy_elapsed[p95_index],
           (unsigned long long) snapshot_elapsed[sample_count / 2],
           (unsigned long long) snapshot_elapsed[p95_index],
           sizeof(uint32_t) * (size_t) record_count,
           equal ? "true" : "false",
           (unsigned long long) checksum);
    return equal && checksum != 0;
}

static bool space_window_inventory_benchmark_run(void)
{
    bool valid = true;
    const int counts[] = { 10, 100, 1000 };
    for (size_t i = 0; i < array_count(counts); ++i) {
        valid &= space_window_inventory_benchmark_case(counts[i], false);
        valid &= space_window_inventory_benchmark_case(counts[i], true);
    }
    return valid;
}
