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
