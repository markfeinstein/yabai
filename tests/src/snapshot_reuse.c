static bool snapshot_reuse_startup_benchmark_case(int window_count)
{
    enum { sample_count = 20 };
    int iterations_per_sample = window_count <= 50 ? 20 : 5;
    int total_iterations = sample_count * iterations_per_sample;
    uint64_t elapsed[sample_count];
    uint64_t checksum = 0;
    int created_windows = 0;

    window_server_snapshot_fake_begin(window_count);
    CFArrayRef ids = window_server_snapshot_fake_window_ids(window_count);
    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_VALID);
    window_create_test_begin();
    struct application application = {0};
    application.ref = AXUIElementCreateApplication(getpid());
    window_ax_snapshot_test_parent_value = application.ref;
    AXUIElementRef base_ref = AXUIElementCreateApplication(getpid());

    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int iteration = 0; iteration < iterations_per_sample; ++iteration) {
            struct window_server_snapshot *snapshot =
                window_server_snapshot_create(31, ids, window_count);
            if (!snapshot) continue;
            for (int i = 0; i < window_count; ++i) {
                uint32_t window_id = 1000u + (uint32_t) i;
                const struct window_server_window_record *record =
                    window_server_snapshot_find(snapshot, window_id);
                struct window *window = window_create_with_server_record(
                    &application, CFRetain(base_ref), window_id, record);
                if (!window) continue;
                ++created_windows;
                checksum += window->id;
                checksum += window->is_root ? 1 : 0;
                checksum += window_check_flag(window, WINDOW_SHADOW) ? 1 : 0;
                window_destroy(window);
            }
            window_server_snapshot_destroy(snapshot);
        }
        elapsed[sample] = (read_os_timer() - start) / (uint64_t) iterations_per_sample;
    }

    qsort(elapsed, sample_count, sizeof(elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    int p95_index = (sample_count * 95 + 99) / 100 - 1;
    long long avoided_targeted_calls = (long long) total_iterations * window_count * 2;
    size_t expected_peak = sizeof(struct window_server_snapshot_owner) +
                           (size_t) window_count * sizeof(struct window_server_window_record);
    printf("{\"case\":\"startup-snapshot-reuse\",\"windows\":%d,\"iterations\":%d,"
           "\"p50_ns\":%llu,\"p95_ns\":%llu,\"inventory_calls\":%d,"
           "\"snapshot_destroys\":%zu,\"parent_fallback_calls\":%d,"
           "\"tag_fallback_calls\":%d,\"avoided_targeted_calls\":%lld,"
           "\"peak_bytes\":%zu,\"retained_bytes\":%zu,\"checksum\":%llu}\n",
           window_count, total_iterations,
           (unsigned long long) elapsed[sample_count / 2],
           (unsigned long long) elapsed[p95_index],
           window_server_snapshot_fake.query_calls,
           window_server_snapshot_allocator.deallocation_calls / 2,
           window_create_test_parent_calls,
           window_create_test_shadow_calls,
           avoided_targeted_calls,
           window_server_snapshot_allocator.peak_bytes,
           window_server_snapshot_allocator.current_bytes,
           (unsigned long long) checksum);

    bool valid = created_windows == total_iterations * window_count &&
                 window_server_snapshot_fake.query_calls == total_iterations &&
                 window_server_snapshot_allocator.deallocation_calls == (size_t) total_iterations * 2 &&
                 window_create_test_parent_calls == 0 &&
                 window_create_test_shadow_calls == 0 &&
                 window_server_snapshot_allocator.peak_bytes == expected_peak &&
                 window_server_snapshot_allocator.current_bytes == 0 &&
                 window_server_snapshot_allocator.outstanding_allocations == 0 &&
                 legacy_allocation_test_outstanding() == 0;

    CFRelease(base_ref);
    CFRelease(application.ref);
    window_create_test_end();
    window_ax_snapshot_test_end();
    CFRelease(ids);
    window_server_snapshot_fake_end();
    return valid;
}

static uint32_t snapshot_reuse_menu_ids[200];
static CFArrayRef snapshot_reuse_menu_id_array;
static int snapshot_reuse_menu_candidate_count;
static int snapshot_reuse_menu_list_calls;

static uint32_t *snapshot_reuse_menu_window_list(
    uint64_t sid, int *count, bool include_minimized, bool *success,
    struct window_server_snapshot **snapshot_out)
{
    (void) sid;
    (void) include_minimized;
    ++snapshot_reuse_menu_list_calls;
    *count = snapshot_reuse_menu_candidate_count;
    *success = true;
    *snapshot_out = window_server_snapshot_create(
        32, snapshot_reuse_menu_id_array, snapshot_reuse_menu_candidate_count);
    return snapshot_reuse_menu_ids;
}

static bool snapshot_reuse_menu_benchmark_case(int display_count, int total_space_count,
                                                int window_count)
{
    enum { sample_count = 20, iterations_per_sample = 10 };
    int total_iterations = sample_count * iterations_per_sample;
    uint64_t elapsed[sample_count];
    uint64_t checksum = 0;
    bool valid = true;

    window_server_snapshot_fake_begin(window_count);
    for (int i = 0; i < window_count; ++i) {
        snapshot_reuse_menu_ids[i] = 1000u + (uint32_t) i;
        int record_index = window_count - i - 1;
        window_server_snapshot_fake.records[record_index].id = snapshot_reuse_menu_ids[i];
    }
    snapshot_reuse_menu_id_array = cfarray_of_cfnumbers(
        snapshot_reuse_menu_ids, sizeof(snapshot_reuse_menu_ids[0]), window_count,
        kCFNumberSInt32Type);
    snapshot_reuse_menu_candidate_count = window_count;
    snapshot_reuse_menu_list_calls = 0;

    struct table saved_windows = g_window_manager.window;
    g_window_manager.window = (struct table) {0};
    table_init(&g_window_manager.window, 512, hash_wm, compare_wm);
    struct application application = { .name = "Benchmark" };
    struct window windows[200] = {0};
    for (int i = 0; i < window_count; ++i) {
        windows[i].application = &application;
        windows[i].id = snapshot_reuse_menu_ids[i];
        windows[i].id_ptr = &windows[i].id;
        table_add(&g_window_manager.window, &windows[i].id, &windows[i]);
    }

    __auto_type saved_window_list = g_menu_bar_manager_native.space_window_list;
    g_menu_bar_manager_native.space_window_list = snapshot_reuse_menu_window_list;
    int spaces_per_display = total_space_count / display_count;

    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int iteration = 0; iteration < iterations_per_sample; ++iteration) {
            struct menu_bar_display_builder displays[2] = {0};
            struct menu_bar_space_input inputs[20] = {0};
            struct menu_bar_space_builder builders[20] = {0};
            int space_cursor = 0;
            for (int display_index = 0; display_index < display_count; ++display_index) {
                displays[display_index].input.spaces = inputs + space_cursor;
                displays[display_index].input.space_count = (size_t) spaces_per_display;
                displays[display_index].space_inputs = inputs + space_cursor;
                displays[display_index].space_builders = builders + space_cursor;
                for (int i = 0; i < spaces_per_display; ++i) {
                    inputs[space_cursor + i].space_id = 100u + (uint64_t) space_cursor + (uint64_t) i;
                    builders[space_cursor + i].input = inputs + space_cursor + i;
                }
                space_cursor += spaces_per_display;
            }

            if (!menu_bar_manager_collect_raw_untracked(displays, (size_t) display_count)) {
                valid = false;
            }
            for (int i = 0; i < total_space_count; ++i) {
                if (!builders[i].raw_query_succeeded ||
                    builders[i].raw_window_count != (size_t) window_count) valid = false;
                if (builders[i].raw_window_count) {
                    checksum += builders[i].raw_window_ids[0];
                    checksum += builders[i].raw_window_ids[builders[i].raw_window_count - 1];
                }
                free(builders[i].raw_window_ids);
                free(builders[i].windows);
            }
        }
        elapsed[sample] = (read_os_timer() - start) / iterations_per_sample;
    }

    qsort(elapsed, sample_count, sizeof(elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    int p95_index = (sample_count * 95 + 99) / 100 - 1;
    int expected_calls = total_iterations * total_space_count;
    size_t expected_peak = sizeof(struct window_server_snapshot_owner) +
                           (size_t) window_count * sizeof(struct window_server_window_record);
    printf("{\"case\":\"menu-snapshot-reuse\",\"displays\":%d,\"spaces\":%d,"
           "\"windows\":%d,\"iterations\":%d,\"p50_ns\":%llu,\"p95_ns\":%llu,"
           "\"inventory_calls\":%d,\"snapshot_destroys\":%zu,\"call_delta\":0,"
           "\"peak_bytes\":%zu,\"retained_bytes\":%zu,\"checksum\":%llu}\n",
           display_count, total_space_count, window_count, total_iterations,
           (unsigned long long) elapsed[sample_count / 2],
           (unsigned long long) elapsed[p95_index],
           snapshot_reuse_menu_list_calls,
           window_server_snapshot_allocator.deallocation_calls / 2,
           window_server_snapshot_allocator.peak_bytes,
           window_server_snapshot_allocator.current_bytes,
           (unsigned long long) checksum);

    valid = valid && snapshot_reuse_menu_list_calls == expected_calls &&
            window_server_snapshot_fake.query_calls == expected_calls &&
            window_server_snapshot_allocator.deallocation_calls == (size_t) expected_calls * 2 &&
            window_server_snapshot_allocator.peak_bytes == expected_peak &&
            window_server_snapshot_allocator.current_bytes == 0 &&
            window_server_snapshot_allocator.outstanding_allocations == 0;

    g_menu_bar_manager_native.space_window_list = saved_window_list;
    table_free(&g_window_manager.window);
    g_window_manager.window = saved_windows;
    CFRelease(snapshot_reuse_menu_id_array);
    snapshot_reuse_menu_id_array = NULL;
    window_server_snapshot_fake_end();
    return valid;
}

static bool snapshot_reuse_visibility_benchmark_case(int events_per_sample)
{
    enum { sample_count = 20 };
    uint64_t elapsed[sample_count];
    int total_events = sample_count * events_per_sample;

    window_server_snapshot_fake_begin(1);
    visibility_test_begin(1000);
    event_loop_debug_snapshot_create = window_server_snapshot_create;
    event_loop_debug_snapshot_destroy = window_server_snapshot_destroy;
    visibility_reset_counts();

    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    FILE *sink = fopen("/dev/null", "w");
    bool valid = saved_stdout != -1 && sink && dup2(fileno(sink), STDOUT_FILENO) != -1;
    for (int sample = 0; valid && sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int event = 0; event < events_per_sample; ++event) {
            event_loop_debug_window_visibility("BENCH", 1000, true, true);
        }
        elapsed[sample] = (read_os_timer() - start) / (uint64_t) events_per_sample;
    }
    fflush(stdout);
    if (saved_stdout != -1) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }
    if (sink) fclose(sink);

    qsort(elapsed, sample_count, sizeof(elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    int p95_index = (sample_count * 95 + 99) / 100 - 1;
    long long avoided_targeted_calls = (long long) total_events * 3;
    long long total_query_delta = -(long long) total_events * 2;
    size_t expected_peak = sizeof(struct window_server_snapshot_owner) +
                           sizeof(struct window_server_window_record);
    printf("{\"case\":\"visibility-snapshot-reuse\",\"events_per_sample\":%d,"
           "\"iterations\":%d,\"p50_ns\":%llu,\"p95_ns\":%llu,"
           "\"snapshot_queries\":%d,\"snapshot_destroys\":%zu,"
           "\"parent_fallback_calls\":%d,\"level_fallback_calls\":%d,"
           "\"tag_fallback_calls\":%d,\"avoided_targeted_calls\":%lld,"
           "\"total_query_delta\":%lld,\"peak_bytes\":%zu,"
           "\"retained_bytes\":%zu,\"checksum\":%d}\n",
           events_per_sample, total_events,
           (unsigned long long) elapsed[sample_count / 2],
           (unsigned long long) elapsed[p95_index],
           window_server_snapshot_fake.query_calls,
           window_server_snapshot_allocator.deallocation_calls / 2,
           visibility_parent_calls, visibility_level_calls, visibility_tags_calls,
           avoided_targeted_calls, total_query_delta,
           window_server_snapshot_allocator.peak_bytes,
           window_server_snapshot_allocator.current_bytes,
           visibility_space_calls + visibility_on_screen_calls + visibility_sub_level_calls);

    valid = valid && window_server_snapshot_fake.query_calls == total_events &&
            window_server_snapshot_allocator.deallocation_calls == (size_t) total_events * 2 &&
            visibility_parent_calls == 0 && visibility_level_calls == 0 && visibility_tags_calls == 0 &&
            visibility_snapshot_calls == 0 &&
            visibility_space_calls == total_events &&
            visibility_on_screen_calls == total_events &&
            visibility_sub_level_calls == total_events &&
            window_server_snapshot_allocator.peak_bytes == expected_peak &&
            window_server_snapshot_allocator.current_bytes == 0 &&
            window_server_snapshot_allocator.outstanding_allocations == 0;

    visibility_test_end();
    window_server_snapshot_fake_end();
    return valid;
}

static bool snapshot_reuse_benchmark_run(void)
{
    bool valid = true;
    valid &= snapshot_reuse_startup_benchmark_case(10);
    valid &= snapshot_reuse_startup_benchmark_case(50);
    valid &= snapshot_reuse_startup_benchmark_case(200);
    valid &= snapshot_reuse_menu_benchmark_case(1, 10, 50);
    valid &= snapshot_reuse_menu_benchmark_case(2, 20, 200);
    valid &= snapshot_reuse_visibility_benchmark_case(10);
    valid &= snapshot_reuse_visibility_benchmark_case(50);
    valid &= snapshot_reuse_visibility_benchmark_case(200);
    return valid;
}
