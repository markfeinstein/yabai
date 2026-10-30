TEST_FUNC(window_constraints_normalize_validity_and_decoration_without_policy,
{
    struct window_constraints constraints = {0};
    struct window_constraint_observation observation = {
        .minimum = { 320, 240 }, .maximum = { 1920, 1080 }, .current = { 800, 600 }
    };
    CGSize ax = { 800, 624 };
    window_constraints_update(&constraints, &observation,
                              WINDOW_CONSTRAINT_SOURCE_ITERATOR, &ax, 1234);
    TEST_CHECK(window_constraints_has_limits(&constraints), true);
    TEST_CHECK(window_constraints_has_current(&constraints), true);
    TEST_CHECK(window_constraints_has_ax_corroboration(&constraints), true);
    TEST_CHECK(constraints.validity & WINDOW_CONSTRAINT_CURRENT_IN_RANGE,
               WINDOW_CONSTRAINT_CURRENT_IN_RANGE);
    TEST_CHECK(constraints.decoration_delta.width, 0.0);
    TEST_CHECK(constraints.decoration_delta.height, 24.0);
    TEST_CHECK(constraints.source, WINDOW_CONSTRAINT_SOURCE_ITERATOR);
    TEST_CHECK(constraints.observed_at, 1234ULL);
    uint64_t generation = constraints.generation;

    ax = CGSizeMake(854, 654);
    window_constraints_update(&constraints, &observation,
                              WINDOW_CONSTRAINT_SOURCE_PACKAGES, &ax, 5678);
    TEST_CHECK(constraints.generation > generation, true);
    TEST_CHECK(constraints.decoration_delta.width, 54.0);
    TEST_CHECK(constraints.decoration_delta.height, 54.0);
    TEST_CHECK(constraints.source, WINDOW_CONSTRAINT_SOURCE_PACKAGES);

    generation = constraints.generation;
    window_constraints_invalidate(&constraints);
    TEST_CHECK(constraints.generation > generation, true);
    TEST_CHECK(constraints.validity, 0);
    TEST_CHECK(constraints.source, WINDOW_CONSTRAINT_SOURCE_NONE);
    TEST_CHECK(constraints.observed_at, 0ULL);
    TEST_CHECK(window_constraints_has_limits(&constraints), false);
});

TEST_FUNC(window_constraints_reject_invalid_contradictory_and_out_of_range_fields,
{
    struct window_constraint_observation cases[] = {
        { .minimum = {0, 1}, .maximum = {10, 10}, .current = {5, 5} },
        { .minimum = {-1, 1}, .maximum = {10, 10}, .current = {5, 5} },
        { .minimum = {NAN, 1}, .maximum = {10, 10}, .current = {5, 5} },
        { .minimum = {1, 1}, .maximum = {INFINITY, 10}, .current = {5, 5} },
        { .minimum = {20, 1}, .maximum = {10, 10}, .current = {15, 5} },
        { .minimum = {1, 1}, .maximum = {10, 10}, .current = {0.4, 5} },
        { .minimum = {1, 1}, .maximum = {10, 10}, .current = {10.6, 5} }
    };
    for (size_t i = 0; i < array_count(cases); ++i) {
        struct window_constraints constraints = {0};
        window_constraints_update(&constraints, cases + i,
                                  WINDOW_CONSTRAINT_SOURCE_ITERATOR, NULL, 1);
        if (i < 4) TEST_CHECK(window_constraints_has_limits(&constraints), false);
        if (i == 4) TEST_CHECK(constraints.validity & WINDOW_CONSTRAINT_LIMITS_ORDERED, 0);
        if (i >= 5) TEST_CHECK(constraints.validity & WINDOW_CONSTRAINT_CURRENT_IN_RANGE, 0);
        TEST_CHECK(window_constraints_has_ax_corroboration(&constraints), false);
    }
});

TEST_FUNC(window_constraints_tolerance_and_absent_ax_are_explicit,
{
    struct window_constraint_observation observation = {
        .minimum = {100, 100}, .maximum = {200, 200}, .current = {99.5, 200.5}
    };
    struct window_constraints constraints = {0};
    window_constraints_update(&constraints, &observation,
                              WINDOW_CONSTRAINT_SOURCE_ITERATOR, NULL, 9);
    TEST_CHECK(constraints.validity & WINDOW_CONSTRAINT_CURRENT_IN_RANGE,
               WINDOW_CONSTRAINT_CURRENT_IN_RANGE);
    TEST_CHECK(constraints.validity & WINDOW_CONSTRAINT_AX_SIZE_VALID, 0);
    TEST_CHECK(constraints.validity & WINDOW_CONSTRAINT_DECORATION_DELTA, 0);
});

static int window_constraints_benchmark_compare(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *) left;
    uint64_t b = *(const uint64_t *) right;
    return (a > b) - (a < b);
}

static bool window_constraints_benchmark_case(size_t record_count)
{
    enum { sample_count = 20 };
    int iterations = record_count <= 100 ? 1000 : 200;
    struct window_constraint_observation observations[1000];
    struct window_constraints constraints[1000];
    for (size_t i = 0; i < record_count; ++i) {
        observations[i] = (struct window_constraint_observation) {
            .minimum = { 100 + (double)(i % 5), 80 + (double)(i % 7) },
            .maximum = { 2000, 1200 }, .current = { 800, 600 }
        };
        if (i % 11 == 0) observations[i].minimum.width = NAN;
        if (i % 13 == 0) observations[i].maximum.height = 0;
    }
    uint64_t elapsed[sample_count] = {0}; uint64_t checksum = 0;
    CGSize ax = { 800, 624 };
    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            for (size_t i = 0; i < record_count; ++i) {
                window_constraints_update(constraints + i, observations + i,
                                          WINDOW_CONSTRAINT_SOURCE_ITERATOR,
                                          (i % 3) ? &ax : NULL, 1);
                checksum += constraints[i].validity + constraints[i].generation;
            }
        }
        elapsed[sample] = (read_os_timer() - start) /
                          ((uint64_t)iterations * (uint64_t)record_count);
    }
    qsort(elapsed, sample_count, sizeof(elapsed[0]), window_constraints_benchmark_compare);
    int p95 = (sample_count * 95 + 99) / 100 - 1;
    printf("{\"case\":\"window-constraints\",\"records\":%zu,\"iterations\":%d,"
           "\"p50_ns_per_record\":%llu,\"p95_ns_per_record\":%llu,"
           "\"allocations\":0,\"checksum\":%llu}\n",
           record_count, sample_count * iterations,
           (unsigned long long)elapsed[sample_count / 2],
           (unsigned long long)elapsed[p95], (unsigned long long)checksum);
    return checksum != 0;
}

static bool window_constraints_benchmark_run(void)
{
    bool small = window_constraints_benchmark_case(10);
    bool medium = window_constraints_benchmark_case(100);
    bool large = window_constraints_benchmark_case(1000);
    return small && medium && large;
}
