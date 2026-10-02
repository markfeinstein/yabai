static CGError window_manager_test_ordered_result;
static uint8_t window_manager_test_ordered_value;
static int window_manager_test_ordered_calls;

static CGError window_manager_test_window_is_ordered_in(int connection, uint32_t wid, uint8_t *ordered_in)
{
    (void) connection;
    (void) wid;
    ++window_manager_test_ordered_calls;
    *ordered_in = window_manager_test_ordered_value;
    return window_manager_test_ordered_result;
}

TEST_FUNC(scratchpad_ordered_query_failure_preserves_automatic_mode,
{
    __auto_type saved_query = window_manager_window_is_ordered_in;
    window_manager_window_is_ordered_in = window_manager_test_window_is_ordered_in;
    struct window window = { .id = 42 };
    uint8_t ordered_in = 0;

    window_manager_test_ordered_result = kCGErrorFailure;
    window_manager_test_ordered_value = 1;
    window_manager_test_ordered_calls = 0;
    TEST_CHECK(window_manager_scratchpad_ordered_state(&window, 0, &ordered_in), false);
    TEST_CHECK(window_manager_test_ordered_calls, 1);

    window_manager_test_ordered_calls = 0;
    TEST_CHECK(window_manager_scratchpad_ordered_state(&window, 1, &ordered_in), true);
    TEST_CHECK(window_manager_test_ordered_calls, 0);

    window_manager_test_ordered_result = kCGErrorSuccess;
    window_manager_test_ordered_calls = 0;
    TEST_CHECK(window_manager_scratchpad_ordered_state(&window, 0, &ordered_in), true);
    TEST_CHECK(ordered_in, 1);
    TEST_CHECK(window_manager_test_ordered_calls, 1);

    window_manager_window_is_ordered_in = saved_query;
});

TEST_FUNC(window_inventory_compares_membership_not_cardinality,
{
    struct window_manager window_manager = {0};
    table_init(&window_manager.window, 4, hash_wm, compare_wm);
    struct window windows[2] = { { .id = 11 }, { .id = 22 } };
    table_add(&window_manager.window, &windows[0].id, &windows[0]);
    table_add(&window_manager.window, &windows[1].id, &windows[1]);

    uint32_t same_membership[] = { 22, 11 };
    uint32_t equal_count_different_membership[] = { 11, 33 };
    TEST_CHECK(window_manager_window_list_is_resolved(&window_manager, same_membership, 2), true);
    TEST_CHECK(window_manager_window_list_is_resolved(&window_manager, equal_count_different_membership, 2), false);
    TEST_CHECK(window_manager_window_list_is_resolved(&window_manager, NULL, 0), true);

    table_free(&window_manager.window);
});

TEST_FUNC(window_manager_remaps_both_stacked_nodes_after_cross_space_swap,
{
    struct window_manager window_manager = {0};
    table_init(&window_manager.window, 8, hash_wm, compare_wm);
    table_init(&window_manager.managed_window, 8, hash_wm, compare_wm);
    window_manager.purify_mode = PURIFY_DISABLED;

    struct window windows[5] = {
        { .id = 11 }, { .id = 12 }, { .id = 21 }, { .id = 22 }, { .id = 23 }
    };
    for (int i = 0; i < 5; ++i) table_add(&window_manager.window, &windows[i].id, &windows[i]);

    struct window_node a_node = {
        .window_list = { 11, 12 },
        .window_order = { 11, 12 },
        .window_count = 2
    };
    struct window_node b_node = {
        .window_list = { 21, 22, 23 },
        .window_order = { 21, 22, 23 },
        .window_count = 3
    };
    struct view a_view = { .sid = 1, .layout = VIEW_BSP, .root = &a_node, .insertion_point = 12 };
    struct view b_view = { .sid = 2, .layout = VIEW_BSP, .root = &b_node, .insertion_point = 22 };

    for (int i = 0; i < a_node.window_count; ++i) table_add(&window_manager.managed_window, &a_node.window_list[i], &a_view);
    for (int i = 0; i < b_node.window_count; ++i) table_add(&window_manager.managed_window, &b_node.window_list[i], &b_view);
    for (int i = 0; i < 5; ++i) window_manager_remove_managed_window(&window_manager, windows[i].id);

    window_manager_update_swap_insertion_points(&a_view, &a_node, 11, &b_view, &b_node, 21);
    window_manager_swap_node_window_lists_and_remap(&window_manager, &a_node, &a_view, &b_node, &b_view);

    TEST_CHECK(a_view.insertion_point, 21U);
    TEST_CHECK(b_view.insertion_point, 11U);
    TEST_CHECK(window_manager.managed_window.count, 5);
    TEST_CHECK(window_manager_find_managed_window(&window_manager, &windows[0]) == &b_view, true);
    TEST_CHECK(window_manager_find_managed_window(&window_manager, &windows[1]) == &b_view, true);
    TEST_CHECK(window_manager_find_managed_window(&window_manager, &windows[2]) == &a_view, true);
    TEST_CHECK(window_manager_find_managed_window(&window_manager, &windows[3]) == &a_view, true);
    TEST_CHECK(window_manager_find_managed_window(&window_manager, &windows[4]) == &a_view, true);
    TEST_CHECK(view_find_window_node(&a_view, 21) == &a_node, true);
    TEST_CHECK(view_find_window_node(&b_view, 11) == &b_node, true);

    table_free(&window_manager.managed_window);
    table_free(&window_manager.window);
});
