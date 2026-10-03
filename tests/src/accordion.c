static bool accordion_area_is_valid(struct area area, struct area bounds)
{
    return isfinite(area.x) && isfinite(area.y) && isfinite(area.w) && isfinite(area.h) &&
           area.w >= 0.0f && area.h >= 0.0f &&
           area.x >= bounds.x && area.y >= bounds.y &&
           area.x + area.w <= bounds.x + bounds.w + 0.001f &&
           area.y + area.h <= bounds.y + bounds.h + 0.001f;
}

TEST_FUNC(accordion_geometry_matrix_is_bounded_stable_and_focus_expanding,
{
    const enum view_type layouts[] = { VIEW_ACCORDION_HORIZONTAL, VIEW_ACCORDION_VERTICAL };
    const int counts[] = { 0, 1, 2, 5, NODE_MAX_WINDOW_COUNT };
    const int paddings[] = { 0, 24, 40, 1000 };
    size_t rows = 0;
    for (size_t layout_index = 0; layout_index < sizeof(layouts) / sizeof(*layouts); ++layout_index) {
        for (size_t count_index = 0; count_index < sizeof(counts) / sizeof(*counts); ++count_index) {
            int count = counts[count_index];
            for (size_t padding_index = 0; padding_index < sizeof(paddings) / sizeof(*paddings); ++padding_index) {
                struct window_node root = { .area = { 10.0f, 20.0f, 200.0f, 120.0f }, .window_count = count };
                struct view view = { .layout = layouts[layout_index], .root = &root,
                                     .accordion_padding = paddings[padding_index] };
                for (int i = 0; i < count; ++i) {
                    root.window_list[i] = root.window_order[i] = (uint32_t) (100 + i);
                }
                int focus_positions[] = { 0, count / 2, count > 0 ? count - 1 : 0 };
                for (size_t focus_case = 0; focus_case < sizeof(focus_positions) / sizeof(*focus_positions); ++focus_case) {
                    uint32_t order_before[NODE_MAX_WINDOW_COUNT] = { 0 };
                    memcpy(order_before, root.window_list, sizeof(order_before));
                    g_window_manager.focused_window_id = count ? root.window_list[focus_positions[focus_case]] : 0;
                    view_update_accordion(&view);
                    TEST_CHECK(root.has_window_areas, count > 0);
                    TEST_CHECK(memcmp(order_before, root.window_list, sizeof(order_before)), 0);
                    float cursor = layouts[layout_index] == VIEW_ACCORDION_HORIZONTAL ? root.area.x : root.area.y;
                    float axis = layouts[layout_index] == VIEW_ACCORDION_HORIZONTAL ? root.area.w : root.area.h;
                    float expected_strip = count > 1 ? fminf((float) paddings[padding_index], axis / (float) count) : 0.0f;
                    float expected_focus = count > 0 ? axis - expected_strip * (float) (count - 1) : 0.0f;
                    for (int i = 0; i < count; ++i) {
                        struct area area = root.window_area[i];
                        TEST_CHECK(accordion_area_is_valid(area, root.area), true);
                        float origin = layouts[layout_index] == VIEW_ACCORDION_HORIZONTAL ? area.x : area.y;
                        float length = layouts[layout_index] == VIEW_ACCORDION_HORIZONTAL ? area.w : area.h;
                        TEST_CHECK(fabsf(origin - cursor) < 0.001f, true);
                        float expected = i == focus_positions[focus_case] ? expected_focus : expected_strip;
                        TEST_CHECK(fabsf(length - expected) < 0.001f, true);
                        if (layouts[layout_index] == VIEW_ACCORDION_HORIZONTAL) {
                            TEST_CHECK(area.y, root.area.y); TEST_CHECK(area.h, root.area.h);
                        } else {
                            TEST_CHECK(area.x, root.area.x); TEST_CHECK(area.w, root.area.w);
                        }
                        cursor += length;
                    }
                    if (count > 0) {
                        TEST_CHECK(fabsf(cursor - ((layouts[layout_index] == VIEW_ACCORDION_HORIZONTAL ? root.area.x : root.area.y) + axis)) < 0.001f, true);
                    }
                    ++rows;
                }
                if (count) {
                    g_window_manager.focused_window_id = UINT32_MAX;
                    view_update_accordion(&view);
                    float first_length = layouts[layout_index] == VIEW_ACCORDION_HORIZONTAL ? root.window_area[0].w : root.window_area[0].h;
                    float axis = layouts[layout_index] == VIEW_ACCORDION_HORIZONTAL ? root.area.w : root.area.h;
                    float strip = count > 1 ? fminf((float) paddings[padding_index], axis / (float) count) : 0.0f;
                    TEST_CHECK(fabsf(first_length - (axis - strip * (float) (count - 1))) < 0.001f, true);
                }
            }
        }
    }
    TEST_CHECK(rows, 120UL);
})

struct accordion_focus_counts { int update, flush, visible; bool is_visible; };
static bool accordion_focus_visible(uint64_t sid, void *context)
{
    (void) sid;
    struct accordion_focus_counts *counts = context;
    ++counts->visible;
    return counts->is_visible;
}
static void accordion_focus_update(struct view *view, void *context)
{ (void) view; ++((struct accordion_focus_counts *) context)->update; }
static void accordion_focus_flush(struct window_node *root, void *context)
{ (void) root; ++((struct accordion_focus_counts *) context)->flush; }

TEST_FUNC(accordion_focus_change_updates_and_flushes_once_without_reordering,
{
    bool initialized_window = false, initialized_managed = false;
    if (!g_window_manager.window.buckets) {
        table_init(&g_window_manager.window, 8, hash_wm, compare_wm);
        initialized_window = true;
    }
    if (!g_window_manager.managed_window.buckets) {
        table_init(&g_window_manager.managed_window, 8, hash_wm, compare_wm);
        initialized_managed = true;
    }
    struct application application = { 0 };
    struct window window = { .id = 72, .application = &application };
    struct window_node root = { .window_count = 2, .window_list = { 71, 72 }, .window_order = { 71, 72 } };
    struct view view = { .sid = 9, .layout = VIEW_ACCORDION_HORIZONTAL, .root = &root, .accordion_padding = 30 };
    table_add(&g_window_manager.window, &window.id, &window);
    table_add(&g_window_manager.managed_window, &window.id, &view);
    struct accordion_focus_counts counts = { .is_visible = true };
    struct view_test_platform_ops platform = { accordion_focus_visible, accordion_focus_update,
                                               accordion_focus_flush, &counts, NULL, NULL };
    view_test_set_platform_ops(&platform);
    g_window_manager.focused_window_id = 0;
    g_mouse_state.ffm_window_id = window.id;
    window_did_receive_focus(&g_window_manager, &g_mouse_state, &window);
    TEST_CHECK(counts.update, 1); TEST_CHECK(counts.flush, 1); TEST_CHECK(counts.visible, 1);
    TEST_CHECK(root.window_list[0], 71U); TEST_CHECK(root.window_list[1], 72U);
    TEST_CHECK(root.window_order[0], 71U); TEST_CHECK(root.window_order[1], 72U);
    window_did_receive_focus(&g_window_manager, &g_mouse_state, &window);
    TEST_CHECK(counts.update, 1); TEST_CHECK(counts.flush, 1);
    view_test_set_platform_ops(NULL);
    table_remove(&g_window_manager.managed_window, &window.id);
    table_remove(&g_window_manager.window, &window.id);
    if (initialized_managed) { table_free(&g_window_manager.managed_window); memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window)); }
    if (initialized_window) { table_free(&g_window_manager.window); memset(&g_window_manager.window, 0, sizeof(g_window_manager.window)); }
    g_window_manager.focused_window_id = 0;
})

TEST_FUNC(accordion_batched_lifecycle_recomputes_before_visibility_and_deduplicates,
{
    struct window_node root = { .window_count = 2, .window_list = { 1, 2 }, .window_order = { 1, 2 } };
    struct view view = { .sid = 9, .layout = VIEW_ACCORDION_VERTICAL, .root = &root,
                         .accordion_padding = 30, .flags = VIEW_IS_DIRTY };
    struct view *views[] = { &view, &view };
    struct accordion_focus_counts counts = { .is_visible = false };
    struct view_test_platform_ops platform = { accordion_focus_visible, accordion_focus_update,
                                               accordion_focus_flush, &counts, NULL, NULL };
    view_test_set_platform_ops(&platform);
    event_loop_flush_batched_views(views, 2);
    TEST_CHECK(counts.update, 1); TEST_CHECK(counts.visible, 1); TEST_CHECK(counts.flush, 0);
    TEST_CHECK(view_is_dirty(&view), true);
    counts.is_visible = true;
    event_loop_flush_batched_views(views, 2);
    TEST_CHECK(counts.update, 2); TEST_CHECK(counts.visible, 2); TEST_CHECK(counts.flush, 1);
    TEST_CHECK(view_is_dirty(&view), false);
    view_test_set_platform_ops(NULL);
})

TEST_FUNC(accordion_swap_flush_defers_only_when_focus_notification_will_recompute,
{
    struct view horizontal = { .layout = VIEW_ACCORDION_HORIZONTAL };
    struct view vertical = { .layout = VIEW_ACCORDION_VERTICAL };
    struct view stack = { .layout = VIEW_STACK };
    TEST_CHECK(window_manager_defer_accordion_swap_flush(&horizontal, 10, 10, 11), true);
    TEST_CHECK(window_manager_defer_accordion_swap_flush(&vertical, 11, 10, 11), true);
    TEST_CHECK(window_manager_defer_accordion_swap_flush(&horizontal, 12, 10, 11), false);
    TEST_CHECK(window_manager_defer_accordion_swap_flush(&stack, 10, 10, 11), false);
})

TEST_FUNC(accordion_membership_insertion_removal_and_focus_preserve_order,
{
    struct window_node root = { .area = { 0, 0, 300, 200 }, .window_count = 3 };
    root.window_list[0] = root.window_order[0] = 1;
    root.window_list[1] = root.window_order[1] = 2;
    root.window_list[2] = root.window_order[2] = 3;
    struct view view = { .layout = VIEW_ACCORDION_HORIZONTAL, .root = &root, .accordion_padding = 30 };
    uint32_t expected[] = { 1, 2, 3 };
    for (int focus = 1; focus <= 3; ++focus) {
        g_window_manager.focused_window_id = (uint32_t) focus;
        view_update_accordion(&view);
        TEST_CHECK(memcmp(root.window_list, expected, sizeof(expected)), 0);
    }

    struct window remove = { .id = 2 };
    TEST_CHECK(view_remove_window_node(&view, &remove), NULL);
    TEST_CHECK(root.window_count, 2);
    TEST_CHECK(root.window_list[0], 1U);
    TEST_CHECK(root.window_list[1], 3U);

    struct window add = { .id = 4 };
    TEST_CHECK(view_add_window_node(&view, &add), &root);
    TEST_CHECK(root.window_count, 3);
    TEST_CHECK(root.window_list[0], 1U);
    TEST_CHECK(root.window_list[1], 4U);
    TEST_CHECK(root.window_list[2], 3U);
    g_window_manager.focused_window_id = 3;
    view_update_accordion(&view);
    TEST_CHECK(root.window_list[0], 1U);
    TEST_CHECK(root.window_list[1], 4U);
    TEST_CHECK(root.window_list[2], 3U);
})
