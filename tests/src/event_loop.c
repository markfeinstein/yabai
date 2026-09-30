TEST_FUNC(update_window_notifications_append_grows_past_fixed_boundary,
{
    uint32_t *window_list = NULL;
    size_t window_count = 0;
    size_t window_capacity = 0;
    const size_t fixed_boundary = 1024;

    for (size_t i = 0; i < fixed_boundary + 3; ++i) {
        TEST_CHECK(update_window_notifications_append(&window_list, &window_count, &window_capacity, (uint32_t) (1000 + i)), true);
    }

    TEST_CHECK(window_count, fixed_boundary + 3);
    TEST_CHECK(window_capacity >= window_count, true);
    TEST_CHECK(window_list[0], 1000u);
    TEST_CHECK(window_list[fixed_boundary + 2], (uint32_t) (1000 + fixed_boundary + 2));

    free(window_list);
});
