static CFArrayRef window_corner_geometry_test_array(const double *values, CFIndex count)
{
    CFMutableArrayRef array = CFArrayCreateMutable(NULL, count, &kCFTypeArrayCallBacks);
    if (!array) return NULL;
    for (CFIndex i = 0; i < count; ++i) {
        CFNumberRef value = CFNumberCreate(NULL, kCFNumberDoubleType, values + i);
        if (!value) { CFRelease(array); return NULL; }
        CFArrayAppendValue(array, value);
        CFRelease(value);
    }
    return array;
}

TEST_FUNC(window_corner_geometry_rejects_nonarrays_counts_and_nonnumbers,
{
    struct window_corner_geometry geometry = {
        .radius = { 1, 2, 3, 4 }, .valid_mask = 0xff, .count = 4, .source = 9
    };
    TEST_CHECK(window_corner_geometry_normalize(NULL,
        WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED, &geometry), false);
    TEST_CHECK(window_corner_geometry_is_complete(&geometry), false);
    TEST_CHECK(window_corner_geometry_normalize(CFSTR("wrong"),
        WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED, &geometry), false);
    TEST_CHECK(window_corner_geometry_normalize(CFSTR("wrong"),
        WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED, NULL), false);

    const double values[] = { 1, 2, 3, 4, 5 };
    const CFIndex invalid_counts[] = { 0, 1, 3, 5 };
    for (size_t i = 0; i < array_count(invalid_counts); ++i) {
        CFArrayRef array = window_corner_geometry_test_array(values, invalid_counts[i]);
        TEST_CHECK(array != NULL, true);
        TEST_CHECK(window_corner_geometry_normalize(array,
            WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED, &geometry), false);
        TEST_CHECK(window_corner_geometry_is_complete(&geometry), false);
        if (array) CFRelease(array);
    }

    CFTypeRef mixed_values[] = { @1, @2, CFSTR("not-a-number"), @4 };
    CFArrayRef mixed = CFArrayCreate(NULL, mixed_values, 4, &kCFTypeArrayCallBacks);
    TEST_CHECK(mixed != NULL, true);
    TEST_CHECK(window_corner_geometry_normalize(mixed,
        WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED, &geometry), false);
    TEST_CHECK(window_corner_geometry_is_complete(&geometry), false);
    if (mixed) CFRelease(mixed);

    CFArrayRef valid = window_corner_geometry_test_array(values, 4);
    TEST_CHECK(valid != NULL, true);
    TEST_CHECK(window_corner_geometry_normalize(valid,
        WINDOW_CORNER_GEOMETRY_SOURCE_NONE, &geometry), false);
    TEST_CHECK(window_corner_geometry_is_complete(&geometry), false);
    if (valid) CFRelease(valid);
})

TEST_FUNC(window_corner_geometry_rejects_negative_nan_and_infinity,
{
    const double invalid[][4] = {
        { -1, 0, 0, 0 },
        { 0, NAN, 0, 0 },
        { 0, 0, INFINITY, 0 },
        { 0, 0, 0, -INFINITY }
    };
    for (size_t i = 0; i < array_count(invalid); ++i) {
        CFArrayRef array = window_corner_geometry_test_array(invalid[i], 4);
        struct window_corner_geometry geometry = { .valid_mask = 0xff };
        TEST_CHECK(array != NULL, true);
        TEST_CHECK(window_corner_geometry_normalize(array,
            WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED, &geometry), false);
        TEST_CHECK(window_corner_geometry_is_complete(&geometry), false);
        TEST_CHECK(geometry.valid_mask, 0U);
        if (array) CFRelease(array);
    }
})

TEST_FUNC(window_corner_geometry_preserves_zero_equal_and_asymmetric_source_order,
{
    const double cases[][4] = {
        { 0, 0, 0, 0 },
        { 16, 16, 16, 16 },
        { 1.25, 2.5, 3.75, 5.0 }
    };
    for (size_t i = 0; i < array_count(cases); ++i) {
        CFArrayRef array = window_corner_geometry_test_array(cases[i], 4);
        struct window_corner_geometry geometry = {0};
        TEST_CHECK(array != NULL, true);
        TEST_CHECK(window_corner_geometry_normalize(array,
            WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED, &geometry), true);
        TEST_CHECK(window_corner_geometry_is_complete(&geometry), true);
        TEST_CHECK(geometry.valid_mask, WINDOW_CORNER_GEOMETRY_VALID_ALL);
        TEST_CHECK(geometry.count, WINDOW_CORNER_GEOMETRY_RADIUS_COUNT);
        TEST_CHECK(geometry.source, WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED);
        for (size_t corner = 0; corner < WINDOW_CORNER_GEOMETRY_RADIUS_COUNT; ++corner) {
            TEST_CHECK(geometry.radius[corner], cases[i][corner]);
        }
        if (array) CFRelease(array);
    }

    struct window_corner_geometry invalid = {
        .radius = { 1, 2, 3, 4 },
        .valid_mask = WINDOW_CORNER_GEOMETRY_VALID_ALL,
        .count = WINDOW_CORNER_GEOMETRY_RADIUS_COUNT,
        .source = WINDOW_CORNER_GEOMETRY_SOURCE_NONE
    };
    TEST_CHECK(window_corner_geometry_is_complete(NULL), false);
    TEST_CHECK(window_corner_geometry_is_complete(&invalid), false);
})

TEST_FUNC(window_corner_geometry_capability_is_resolved_but_production_paths_are_disabled,
{
    struct window_corner_geometry_capability capability = window_corner_geometry_capability();
    TEST_CHECK(capability.collection_enabled, false);
    TEST_CHECK(capability.proxy_rendering_enabled, false);
    struct window_corner_geometry geometry = { .valid_mask = 0xff };
    TEST_CHECK(window_corner_geometry_collect_resolved(NULL, 1, &geometry), false);
    TEST_CHECK(window_corner_geometry_is_complete(&geometry), false);
    TEST_CHECK(window_corner_geometry_apply_to_proxy(1, 1, CGSizeMake(100, 100), &geometry), false);
})
