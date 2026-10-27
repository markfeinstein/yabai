static struct window_server_hit_test_request hit_test_last_request;
static int hit_test_legacy_calls;
static int hit_test_geometry_calls;
static CGError hit_test_legacy_status;
static CGError hit_test_geometry_status;
static uint32_t hit_test_legacy_id;
static uint32_t hit_test_geometry_id;
static int hit_test_legacy_cid;
static int hit_test_geometry_cid;
static CGPoint hit_test_legacy_point;
static CGPoint hit_test_geometry_point;

static OSStatus hit_test_legacy_fake(int cid, int filter, int flags, int reserved,
                                     CGPoint *screen, CGPoint *window,
                                     uint32_t *wid, int *wcid)
{
    ++hit_test_legacy_calls;
    hit_test_last_request = (struct window_server_hit_test_request) {
        cid, (uint32_t)filter, flags, reserved, *screen
    };
    *window = hit_test_legacy_point; *wid = hit_test_legacy_id; *wcid = hit_test_legacy_cid;
    return hit_test_legacy_status;
}

static CGError hit_test_geometry_fake(int32_t cid, uint32_t filter, int32_t flags,
                                      int32_t reserved, const CGPoint *screen,
                                      CGPoint *window, uint32_t *wid, int32_t *wcid)
{
    ++hit_test_geometry_calls;
    hit_test_last_request = (struct window_server_hit_test_request) {
        cid, filter, flags, reserved, *screen
    };
    *window = hit_test_geometry_point; *wid = hit_test_geometry_id; *wcid = hit_test_geometry_cid;
    return hit_test_geometry_status;
}

static void hit_test_begin(void)
{
    hit_test_legacy_calls = hit_test_geometry_calls = 0;
    hit_test_legacy_status = hit_test_geometry_status = kCGErrorSuccess;
    hit_test_legacy_id = hit_test_geometry_id = 42;
    hit_test_legacy_cid = hit_test_geometry_cid = 7;
    hit_test_legacy_point = hit_test_geometry_point = CGPointMake(3, 4);
    window_server_hit_test_legacy_impl = hit_test_legacy_fake;
    window_server_hit_test_geometry_test_impl = hit_test_geometry_fake;
    window_server_hit_test_geometry_test_available = true;
}

static void hit_test_end(void)
{
    window_server_hit_test_legacy_impl = SLSFindWindowAndOwner;
    window_server_hit_test_geometry_test_impl = NULL;
    window_server_hit_test_geometry_test_available = false;
}

TEST_FUNC(window_server_hit_test_comparison_is_optional_and_legacy_authoritative,
{
    hit_test_begin();
    struct window_server_hit_test_request request = {
        .connection = 9, .filter_window_id = 88, .flags = -1, .reserved = 0,
        .screen_point = CGPointMake(100, 200)
    };
    struct window_server_hit_test_comparison comparison;
    window_server_hit_test_compare(&request, &comparison);
    TEST_CHECK(comparison.legacy.window_id, 42U);
    TEST_CHECK(comparison.geometry.window_id, 42U);
    TEST_CHECK(comparison.same_window, true);
    TEST_CHECK(comparison.same_owner, true);
    TEST_CHECK(comparison.same_window_point, true);
    TEST_CHECK(hit_test_last_request.connection, 9);
    TEST_CHECK(hit_test_last_request.filter_window_id, 88U);
    TEST_CHECK(hit_test_last_request.flags, -1);
    TEST_CHECK(hit_test_last_request.reserved, 0);
    TEST_CHECK(CGPointEqualToPoint(hit_test_last_request.screen_point, CGPointMake(100, 200)), true);

    hit_test_geometry_id = 43; hit_test_geometry_cid = 8; hit_test_geometry_point = CGPointMake(9, 10);
    window_server_hit_test_compare(&request, &comparison);
    TEST_CHECK(comparison.legacy.window_id, 42U);
    TEST_CHECK(comparison.same_window, false);
    TEST_CHECK(comparison.same_owner, false);
    TEST_CHECK(comparison.same_window_point, false);

    hit_test_geometry_status = kCGErrorFailure;
    window_server_hit_test_compare(&request, &comparison);
    TEST_CHECK(comparison.legacy.window_id, 42U);
    TEST_CHECK(comparison.geometry.window_id, 0U);
    TEST_CHECK(comparison.geometry.window_connection, 0);

    window_server_hit_test_geometry_test_available = false;
    int geometry_calls = hit_test_geometry_calls;
    window_server_hit_test_compare(&request, &comparison);
    TEST_CHECK(comparison.geometry_available, false);
    TEST_CHECK(comparison.geometry.invoked, false);
    TEST_CHECK(comparison.legacy.window_id, 42U);
    TEST_CHECK(hit_test_geometry_calls, geometry_calls);

    hit_test_legacy_status = kCGErrorFailure;
    struct window_server_hit_test_result legacy;
    window_server_hit_test_legacy(&request, &legacy);
    TEST_CHECK(legacy.invoked, true);
    TEST_CHECK(legacy.window_id, 0U);
    TEST_CHECK(legacy.window_connection, 0);
    TEST_CHECK(CGPointEqualToPoint(legacy.window_point, CGPointZero), true);
    hit_test_end();
});

static int hit_test_sequence_index;
static uint32_t hit_test_sequence_ids[4];
static int hit_test_sequence_cids[4];
static uint32_t hit_test_sequence_filters[4];
static int hit_test_sequence_flags[4];

static OSStatus hit_test_sequence_fake(int cid, int filter, int flags, int reserved,
                                       CGPoint *screen, CGPoint *window,
                                       uint32_t *wid, int *wcid)
{
    (void)cid; (void)reserved; (void)screen; *window = CGPointZero;
    int index = hit_test_sequence_index++;
    hit_test_sequence_filters[index] = (uint32_t)filter;
    hit_test_sequence_flags[index] = flags;
    *wid = hit_test_sequence_ids[index]; *wcid = hit_test_sequence_cids[index];
    return noErr;
}

static bool hit_test_janky_fake(int cid) { return cid == 77; }

TEST_FUNC(window_manager_hit_test_preserves_proxy_and_border_skip_sequence,
{
    __auto_type saved_legacy = window_server_hit_test_legacy_impl;
    __auto_type saved_janky = window_manager_test_connection_is_jankyborders;
    window_server_hit_test_legacy_impl = hit_test_sequence_fake;
    window_manager_test_connection_is_jankyborders = hit_test_janky_fake;
    struct window_manager wm = {0}; table_init(&wm.window, 4, hash_wm, compare_wm);
    struct window target = { .id = 30 }; table_add(&wm.window, &target.id, &target);
    int saved_connection = g_connection; g_connection = 55;

    hit_test_sequence_index = 0;
    hit_test_sequence_ids[0] = 10; hit_test_sequence_cids[0] = 55;
    hit_test_sequence_ids[1] = 30; hit_test_sequence_cids[1] = 99;
    TEST_CHECK(window_manager_find_window_at_point(&wm, CGPointMake(1, 2)), &target);
    TEST_CHECK(hit_test_sequence_index, 2);
    TEST_CHECK(hit_test_sequence_filters[0], 0U); TEST_CHECK(hit_test_sequence_flags[0], 1);
    TEST_CHECK(hit_test_sequence_filters[1], 10U); TEST_CHECK(hit_test_sequence_flags[1], -1);

    hit_test_sequence_index = 0;
    hit_test_sequence_ids[0] = 11; hit_test_sequence_cids[0] = 77;
    hit_test_sequence_ids[1] = 12; hit_test_sequence_cids[1] = 55;
    hit_test_sequence_ids[2] = 30; hit_test_sequence_cids[2] = 99;
    TEST_CHECK(window_manager_find_window_at_point_filtering_window(&wm, CGPointMake(1, 2), 5), &target);
    TEST_CHECK(hit_test_sequence_index, 3);
    TEST_CHECK(hit_test_sequence_filters[0], 5U); TEST_CHECK(hit_test_sequence_flags[0], -1);
    TEST_CHECK(hit_test_sequence_filters[1], 11U); TEST_CHECK(hit_test_sequence_flags[1], -1);
    TEST_CHECK(hit_test_sequence_filters[2], 12U); TEST_CHECK(hit_test_sequence_flags[2], -1);

    g_connection = saved_connection; table_free(&wm.window);
    window_server_hit_test_legacy_impl = saved_legacy;
    window_manager_test_connection_is_jankyborders = saved_janky;
});

static OSStatus hit_test_thread_legacy(int cid, int filter, int flags, int reserved,
                                        CGPoint *screen, CGPoint *window,
                                        uint32_t *wid, int *wcid)
{
    (void)cid; (void)filter; (void)flags; (void)reserved; (void)screen;
    *window = CGPointMake(1, 2); *wid = 42; *wcid = 7; return noErr;
}

static CGError hit_test_thread_geometry(int32_t cid, uint32_t filter, int32_t flags,
                                        int32_t reserved, const CGPoint *screen,
                                        CGPoint *window, uint32_t *wid, int32_t *wcid)
{
    (void)cid; (void)filter; (void)flags; (void)reserved; (void)screen;
    *window = CGPointMake(1, 2); *wid = 42; *wcid = 7; return kCGErrorSuccess;
}

static void *hit_test_thread(void *context)
{
    struct window_server_hit_test_request *request = context;
    for (int i = 0; i < 1000; ++i) {
        struct window_server_hit_test_comparison comparison;
        window_server_hit_test_compare(request, &comparison);
    }
    return NULL;
}

TEST_FUNC(window_server_hit_test_concurrent_comparison_is_race_free,
{
    hit_test_begin();
    window_server_hit_test_legacy_impl = hit_test_thread_legacy;
    window_server_hit_test_geometry_test_impl = hit_test_thread_geometry;
    struct window_server_hit_test_request request = { .connection = 1, .flags = 1 };
    pthread_t threads[4];
    for (int i = 0; i < 4; ++i) TEST_CHECK(pthread_create(&threads[i], NULL, hit_test_thread, &request), 0);
    for (int i = 0; i < 4; ++i) pthread_join(threads[i], NULL);
    hit_test_end();
});

static bool window_server_hit_test_benchmark_run(void)
{
    enum { sample_count = 20, iterations = 100000 };
    hit_test_begin();
    uint64_t elapsed[sample_count] = {0}; uint64_t checksum = 0;
    struct window_server_hit_test_request request = { .connection = 1, .flags = 1 };
    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int i = 0; i < iterations; ++i) {
            struct window_server_hit_test_comparison comparison;
            window_server_hit_test_compare(&request, &comparison);
            checksum += comparison.legacy.window_id + comparison.geometry.window_id;
        }
        elapsed[sample] = (read_os_timer() - start) / iterations;
    }
    qsort(elapsed, sample_count, sizeof(elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    int p95=(sample_count*95+99)/100-1;
    printf("{\"case\":\"window-server-hit-test-wrapper\",\"iterations\":%d,"
           "\"p50_ns\":%llu,\"p95_ns\":%llu,\"allocations\":0,\"checksum\":%llu}\n",
           sample_count*iterations,(unsigned long long)elapsed[sample_count/2],
           (unsigned long long)elapsed[p95],(unsigned long long)checksum);
    hit_test_end(); return checksum != 0;
}
