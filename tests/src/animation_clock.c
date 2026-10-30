static int animation_clock_test_stage;
static int animation_clock_test_create_calls;
static int animation_clock_test_callback_calls;
static int animation_clock_test_start_calls;
static int animation_clock_test_stop_calls;
static int animation_clock_test_release_calls;
static int animation_clock_test_allocations;
static int animation_clock_test_deallocations;
static CVReturn animation_clock_test_stop_result;
static CVDisplayLinkOutputCallback animation_clock_test_callback;
static void *animation_clock_test_callback_context;
static int animation_clock_test_link_token;

static void *animation_clock_test_allocate(size_t count, size_t size)
{
    ++animation_clock_test_allocations;
    return calloc(count, size);
}

static void animation_clock_test_deallocate(void *value)
{
    if (value) ++animation_clock_test_deallocations;
    free(value);
}

static CVReturn animation_clock_test_create(CVDisplayLinkRef *link)
{
    ++animation_clock_test_create_calls;
    if (animation_clock_test_stage == 1) return kCVReturnError;
    *link = (CVDisplayLinkRef) &animation_clock_test_link_token;
    return kCVReturnSuccess;
}

static CVReturn animation_clock_test_set_callback(CVDisplayLinkRef link,
                                                   CVDisplayLinkOutputCallback callback,
                                                   void *context)
{
    (void) link;
    ++animation_clock_test_callback_calls;
    animation_clock_test_callback = callback;
    animation_clock_test_callback_context = context;
    return animation_clock_test_stage == 2 ? kCVReturnError : kCVReturnSuccess;
}

static CVReturn animation_clock_test_start(CVDisplayLinkRef link)
{
    (void) link;
    ++animation_clock_test_start_calls;
    return animation_clock_test_stage == 3 ? kCVReturnError : kCVReturnSuccess;
}

static CVReturn animation_clock_test_stop(CVDisplayLinkRef link)
{
    (void) link;
    ++animation_clock_test_stop_calls;
    return animation_clock_test_stop_result;
}

static void animation_clock_test_release(CVDisplayLinkRef link)
{
    (void) link;
    ++animation_clock_test_release_calls;
}

static double animation_clock_test_frequency(void)
{
    return 600.0;
}

static void animation_clock_test_begin(void)
{
    animation_clock_test_stage = 0;
    animation_clock_test_create_calls = 0;
    animation_clock_test_callback_calls = 0;
    animation_clock_test_start_calls = 0;
    animation_clock_test_stop_calls = 0;
    animation_clock_test_release_calls = 0;
    animation_clock_test_allocations = 0;
    animation_clock_test_deallocations = 0;
    animation_clock_test_stop_result = kCVReturnSuccess;
    animation_clock_test_callback = NULL;
    animation_clock_test_callback_context = NULL;
    animation_clock_cv_create = animation_clock_test_create;
    animation_clock_cv_set_callback = animation_clock_test_set_callback;
    animation_clock_cv_start = animation_clock_test_start;
    animation_clock_cv_stop = animation_clock_test_stop;
    animation_clock_cv_release = animation_clock_test_release;
    animation_clock_cv_frequency = animation_clock_test_frequency;
    animation_clock_allocate = animation_clock_test_allocate;
    animation_clock_deallocate = animation_clock_test_deallocate;
}

static void animation_clock_test_end(void)
{
    animation_clock_cv_create = CVDisplayLinkCreateWithActiveCGDisplays;
    animation_clock_cv_set_callback = CVDisplayLinkSetOutputCallback;
    animation_clock_cv_start = CVDisplayLinkStart;
    animation_clock_cv_stop = CVDisplayLinkStop;
    animation_clock_cv_release = CVDisplayLinkRelease;
    animation_clock_cv_frequency = CVGetHostClockFrequency;
    animation_clock_allocate = calloc;
    animation_clock_deallocate = free;
}

static void animation_clock_test_noop_tick(void *context, const struct animation_clock_tick *tick)
{
    (void) context;
    (void) tick;
}

TEST_FUNC(animation_clock_cv_setup_stop_and_destroy_states_are_exact,
{
    for (int stage = 1; stage <= 3; ++stage) {
        animation_clock_test_begin();
        animation_clock_test_stage = stage;
        struct animation_clock *clock = animation_clock_create(animation_clock_test_noop_tick, NULL);
        TEST_CHECK(clock != NULL, true);
        TEST_CHECK(animation_clock_start(clock), false);
        TEST_CHECK(animation_clock_test_create_calls, 1);
        TEST_CHECK(animation_clock_test_callback_calls, stage >= 2 ? 1 : 0);
        TEST_CHECK(animation_clock_test_start_calls, stage >= 3 ? 1 : 0);
        animation_clock_test_stop_result = stage == 3 ? kCVReturnDisplayLinkNotRunning : kCVReturnSuccess;
        TEST_CHECK(animation_clock_stop(clock), ANIMATION_CLOCK_STOP_QUIESCED);
        TEST_CHECK(animation_clock_test_stop_calls, stage == 3 ? 1 : 0);
        animation_clock_destroy(clock);
        TEST_CHECK(animation_clock_test_release_calls, stage >= 2 ? 1 : 0);
        TEST_CHECK(animation_clock_test_deallocations, 1);
        animation_clock_test_end();
    }

    animation_clock_test_begin();
    struct animation_clock *clock = animation_clock_create(animation_clock_test_noop_tick, NULL);
    TEST_CHECK(animation_clock_start(clock), true);
    animation_clock_test_stop_result = kCVReturnError;
    TEST_CHECK(animation_clock_stop(clock), ANIMATION_CLOCK_STOP_RETRY);
    TEST_CHECK(animation_clock_test_stop_calls, 1);
    animation_clock_destroy(clock);
    TEST_CHECK(animation_clock_test_release_calls, 0);
    TEST_CHECK(animation_clock_test_deallocations, 0);
    animation_clock_test_stop_result = kCVReturnSuccess;
    TEST_CHECK(animation_clock_stop(clock), ANIMATION_CLOCK_STOP_QUIESCED);
    TEST_CHECK(animation_clock_stop(clock), ANIMATION_CLOCK_STOP_QUIESCED);
    TEST_CHECK(animation_clock_test_stop_calls, 2);
    animation_clock_destroy(clock);
    TEST_CHECK(animation_clock_test_release_calls, 1);
    TEST_CHECK(animation_clock_test_deallocations, 1);
    animation_clock_test_end();
});

static int animation_clock_test_tick_calls;
static struct animation_clock_tick animation_clock_test_last_tick;
static pthread_t animation_clock_test_tick_thread;

static void animation_clock_test_record_tick(void *context, const struct animation_clock_tick *tick)
{
    (void) context;
    ++animation_clock_test_tick_calls;
    animation_clock_test_last_tick = *tick;
    animation_clock_test_tick_thread = pthread_self();
}

TEST_FUNC(animation_clock_cv_tick_preserves_origin_target_frequency_and_thread,
{
    animation_clock_test_begin();
    animation_clock_test_tick_calls = 0;
    struct animation_clock *clock = animation_clock_create(animation_clock_test_record_tick, NULL);
    TEST_CHECK(animation_clock_start(clock), true);
    TEST_CHECK(animation_clock_test_callback != NULL, true);
    pthread_t invoking_thread = pthread_self();
    CVTimeStamp now = { .hostTime = 100 };
    CVTimeStamp output = { .hostTime = 110 };
    CVOptionFlags output_flags = 0;
    CVDisplayLinkRef link = (CVDisplayLinkRef) &animation_clock_test_link_token;
    TEST_CHECK(animation_clock_test_callback(link, &now, &output, 0, &output_flags,
                                             animation_clock_test_callback_context), kCVReturnSuccess);
    TEST_CHECK(animation_clock_test_tick_calls, 1);
    TEST_CHECK(animation_clock_test_last_tick.elapsed_ticks, 10.0);
    TEST_CHECK(animation_clock_test_last_tick.ticks_per_second, 600.0);
    TEST_CHECK(pthread_equal(animation_clock_test_tick_thread, invoking_thread), true);

    now.hostTime = 999;
    output.hostTime = 130;
    animation_clock_test_callback(link, &now, &output, 0, &output_flags,
                                  animation_clock_test_callback_context);
    TEST_CHECK(animation_clock_test_tick_calls, 2);
    TEST_CHECK(animation_clock_test_last_tick.elapsed_ticks, 30.0);
    TEST_CHECK(animation_clock_stop(clock), ANIMATION_CLOCK_STOP_QUIESCED);
    animation_clock_destroy(clock);
    animation_clock_test_end();
});

static bool animation_clock_trace_case(double hz)
{
    const double duration = 0.35;
    const int frame_count = (int) ceil(duration * hz) + 3;
    double checksum_direct = 0.0;
    double checksum_clock = 0.0;
    int terminal_direct = -1;
    int terminal_clock = -1;
    for (int frame = 0; frame < frame_count; ++frame) {
        double elapsed = frame / hz;
        double direct = elapsed / duration;
        if (direct <= 0.0) direct = 0.0;
        if (direct >= 1.0) direct = 1.0;
        struct animation_clock_tick tick = {
            .elapsed_ticks = elapsed * 1000000.0,
            .ticks_per_second = 1000000.0
        };
        double abstracted = tick.elapsed_ticks / (duration * tick.ticks_per_second);
        if (abstracted <= 0.0) abstracted = 0.0;
        if (abstracted >= 1.0) abstracted = 1.0;
        checksum_direct += direct * (frame + 1);
        checksum_clock += abstracted * (frame + 1);
        if (terminal_direct == -1 && direct == 1.0) terminal_direct = frame;
        if (terminal_clock == -1 && abstracted == 1.0) terminal_clock = frame;
    }
    return terminal_direct == terminal_clock && fabs(checksum_direct - checksum_clock) < 0.000001;
}

TEST_FUNC(animation_clock_synthetic_refresh_traces_match_direct_formula,
{
    TEST_CHECK(animation_clock_trace_case(60.0), true);
    TEST_CHECK(animation_clock_trace_case(120.0), true);
    TEST_CHECK(animation_clock_trace_case(144.0), true);
});

static bool animation_clock_benchmark_run(void)
{
    enum { sample_count = 20, tick_count = 100000 };
    uint64_t direct_elapsed[sample_count] = {0};
    uint64_t clock_elapsed[sample_count] = {0};
    double direct_checksum = 0.0;
    double clock_checksum = 0.0;
    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int tick = 0; tick < tick_count; ++tick) {
            double t = ((double) tick * 1000.0) / (0.35 * 1000000.0);
            if (t > 1.0) t = 1.0;
            direct_checksum += t;
        }
        direct_elapsed[sample] = (read_os_timer() - start) / tick_count;

        start = read_os_timer();
        for (int tick = 0; tick < tick_count; ++tick) {
            struct animation_clock_tick value = {
                .elapsed_ticks = (double) tick * 1000.0,
                .ticks_per_second = 1000000.0
            };
            double t = value.elapsed_ticks / (0.35 * value.ticks_per_second);
            if (t > 1.0) t = 1.0;
            clock_checksum += t;
        }
        clock_elapsed[sample] = (read_os_timer() - start) / tick_count;
    }
    qsort(direct_elapsed, sample_count, sizeof(direct_elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    qsort(clock_elapsed, sample_count, sizeof(clock_elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    printf("{\"case\":\"animation-clock-adapter\",\"ticks\":%d,"
           "\"direct_median_ns_per_tick\":%llu,\"clock_median_ns_per_tick\":%llu,"
           "\"checksum_equal\":%s}\n",
           sample_count * tick_count,
           (unsigned long long) direct_elapsed[sample_count / 2],
           (unsigned long long) clock_elapsed[sample_count / 2],
           fabs(direct_checksum - clock_checksum) < 0.000001 ? "true" : "false");
    return fabs(direct_checksum - clock_checksum) < 0.000001;
}
