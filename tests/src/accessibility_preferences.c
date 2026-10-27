static bool accessibility_preferences_test_values[3];
static bool accessibility_preferences_test_observer_failure;
static int accessibility_preferences_test_observer_begins;
static int accessibility_preferences_test_observer_ends;
static int accessibility_preferences_test_invalidations;
static accessibility_preferences_callback accessibility_preferences_test_callback;
static void *accessibility_preferences_test_callback_context;
static struct accessibility_display_preferences accessibility_preferences_test_invalidated_snapshot;

static bool accessibility_preferences_test_reduce_motion(void)
{
    return accessibility_preferences_test_values[0];
}

static bool accessibility_preferences_test_reduce_transparency(void)
{
    return accessibility_preferences_test_values[1];
}

static bool accessibility_preferences_test_differentiate_without_color(void)
{
    return accessibility_preferences_test_values[2];
}

static bool accessibility_preferences_test_observer_begin(accessibility_preferences_callback callback,
                                                           void *context, void **token_out)
{
    ++accessibility_preferences_test_observer_begins;
    accessibility_preferences_test_callback = callback;
    accessibility_preferences_test_callback_context = context;
    *token_out = NULL;
    if (accessibility_preferences_test_observer_failure) return false;
    *token_out = (void *)(uintptr_t) 0x1234;
    return true;
}

static void accessibility_preferences_test_observer_end(void *token)
{
    if (token == (void *)(uintptr_t) 0x1234) ++accessibility_preferences_test_observer_ends;
}

static void accessibility_preferences_test_invalidate(void)
{
    ++accessibility_preferences_test_invalidations;
    accessibility_preferences_test_invalidated_snapshot = accessibility_preferences_snapshot();
}

static void accessibility_preferences_test_begin(void)
{
    accessibility_preferences_end();
    memset(accessibility_preferences_test_values, 0, sizeof(accessibility_preferences_test_values));
    accessibility_preferences_test_observer_failure = false;
    accessibility_preferences_test_observer_begins = 0;
    accessibility_preferences_test_observer_ends = 0;
    accessibility_preferences_test_invalidations = 0;
    accessibility_preferences_test_callback = NULL;
    accessibility_preferences_test_callback_context = NULL;
    accessibility_preferences_test_invalidated_snapshot = (struct accessibility_display_preferences) {0};
    accessibility_preferences_reader_reduce_motion = accessibility_preferences_test_reduce_motion;
    accessibility_preferences_reader_reduce_transparency = accessibility_preferences_test_reduce_transparency;
    accessibility_preferences_reader_differentiate_without_color =
        accessibility_preferences_test_differentiate_without_color;
    accessibility_preferences_observer_begin_impl = accessibility_preferences_test_observer_begin;
    accessibility_preferences_observer_end_impl = accessibility_preferences_test_observer_end;
    accessibility_preferences_invalidate_menu = accessibility_preferences_test_invalidate;
}

static void accessibility_preferences_test_end(void)
{
    accessibility_preferences_end();
    accessibility_preferences_reader_reduce_motion = accessibility_preferences_read_reduce_motion;
    accessibility_preferences_reader_reduce_transparency = accessibility_preferences_read_reduce_transparency;
    accessibility_preferences_reader_differentiate_without_color =
        accessibility_preferences_read_differentiate_without_color;
    accessibility_preferences_observer_begin_impl = accessibility_preferences_observer_begin_production;
    accessibility_preferences_observer_end_impl = accessibility_preferences_observer_end_production;
    accessibility_preferences_invalidate_menu = menu_bar_accessibility_preferences_changed;
}

TEST_FUNC(accessibility_preferences_publish_all_combinations_and_missing_readers,
{
    accessibility_preferences_test_begin();
    for (uint32_t mask = 0; mask < 8; ++mask) {
        accessibility_preferences_test_values[0] = (mask & 1) != 0;
        accessibility_preferences_test_values[1] = (mask & 2) != 0;
        accessibility_preferences_test_values[2] = (mask & 4) != 0;
        TEST_CHECK(accessibility_preferences_begin(), true);
        struct accessibility_display_preferences snapshot = accessibility_preferences_snapshot();
        TEST_CHECK(snapshot.reduce_motion, (mask & 1) != 0);
        TEST_CHECK(snapshot.reduce_transparency, (mask & 2) != 0);
        TEST_CHECK(snapshot.differentiate_without_color, (mask & 4) != 0);
        accessibility_preferences_end();
    }
    TEST_CHECK(accessibility_preferences_test_observer_begins, 8);
    TEST_CHECK(accessibility_preferences_test_observer_ends, 8);

    accessibility_preferences_test_values[0] = true;
    accessibility_preferences_test_values[1] = true;
    accessibility_preferences_test_values[2] = true;
    accessibility_preferences_reader_reduce_motion = NULL;
    TEST_CHECK(accessibility_preferences_begin(), true);
    struct accessibility_display_preferences snapshot = accessibility_preferences_snapshot();
    TEST_CHECK(snapshot.reduce_motion, false);
    TEST_CHECK(snapshot.reduce_transparency, true);
    TEST_CHECK(snapshot.differentiate_without_color, true);
    accessibility_preferences_test_end();
});

TEST_FUNC(accessibility_preferences_live_updates_are_coherent_and_teardown_is_final,
{
    accessibility_preferences_test_begin();
    accessibility_preferences_test_values[0] = true;
    TEST_CHECK(accessibility_preferences_begin(), true);
    TEST_CHECK(accessibility_preferences_test_callback != NULL, true);
    accessibility_preferences_callback callback = accessibility_preferences_test_callback;
    void *callback_context = accessibility_preferences_test_callback_context;

    accessibility_preferences_test_values[0] = false;
    accessibility_preferences_test_values[1] = true;
    accessibility_preferences_test_values[2] = true;
    callback(callback_context);
    TEST_CHECK(accessibility_preferences_test_invalidations, 1);
    TEST_CHECK(accessibility_preferences_test_invalidated_snapshot.reduce_motion, false);
    TEST_CHECK(accessibility_preferences_test_invalidated_snapshot.reduce_transparency, true);
    TEST_CHECK(accessibility_preferences_test_invalidated_snapshot.differentiate_without_color, true);

    callback(callback_context);
    TEST_CHECK(accessibility_preferences_test_invalidations, 2);

    TEST_CHECK(accessibility_preferences_begin(), true);
    TEST_CHECK(accessibility_preferences_test_observer_begins, 2);
    TEST_CHECK(accessibility_preferences_test_observer_ends, 1);
    callback = accessibility_preferences_test_callback;
    callback_context = accessibility_preferences_test_callback_context;
    accessibility_preferences_end();
    TEST_CHECK(accessibility_preferences_test_observer_ends, 2);
    int invalidations = accessibility_preferences_test_invalidations;
    accessibility_preferences_test_values[0] = true;
    accessibility_preferences_test_values[1] = false;
    accessibility_preferences_test_values[2] = false;
    callback(callback_context);
    TEST_CHECK(accessibility_preferences_test_invalidations, invalidations);
    struct accessibility_display_preferences snapshot = accessibility_preferences_snapshot();
    TEST_CHECK(snapshot.reduce_motion, false);
    TEST_CHECK(snapshot.reduce_transparency, true);
    TEST_CHECK(snapshot.differentiate_without_color, true);
    accessibility_preferences_test_end();
});

TEST_FUNC(accessibility_preferences_observer_failure_preserves_initial_snapshot,
{
    accessibility_preferences_test_begin();
    accessibility_preferences_test_values[0] = true;
    accessibility_preferences_test_values[1] = false;
    accessibility_preferences_test_values[2] = true;
    accessibility_preferences_test_observer_failure = true;
    TEST_CHECK(accessibility_preferences_begin(), false);
    struct accessibility_display_preferences snapshot = accessibility_preferences_snapshot();
    TEST_CHECK(snapshot.reduce_motion, true);
    TEST_CHECK(snapshot.reduce_transparency, false);
    TEST_CHECK(snapshot.differentiate_without_color, true);
    TEST_CHECK(accessibility_preferences_test_observer_begins, 1);
    TEST_CHECK(accessibility_preferences_test_observer_ends, 0);
    accessibility_preferences_test_end();
});
