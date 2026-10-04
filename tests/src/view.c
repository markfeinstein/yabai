TEST_FUNC(bsp_split_areas_preserve_truncation_and_handle_extreme_geometry,
{
    struct area parent = { .x = 10.0f, .y = 20.0f, .w = 101.0f, .h = 81.0f };
    struct area view_left, view_right;
    struct window_node declarative_left = {0}, declarative_right = {0};
    struct window_node node = {
        .area = parent,
        .left = &declarative_left,
        .right = &declarative_right,
        .split = SPLIT_Y,
        .ratio = 0.3f
    };

    area_make_pair(SPLIT_Y, 7, 0.3f, &parent, &view_left, &view_right);
    declarative_space_split_areas(&node, 7);
    TEST_CHECK(view_left.w, 28.0f);
    TEST_CHECK(view_right.w, 65.0f);
    TEST_CHECK(view_right.x, 45.0f);
    TEST_CHECK(declarative_left.area.w, view_left.w);
    TEST_CHECK(declarative_right.area.w, view_right.w);
    TEST_CHECK(declarative_right.area.x, view_right.x);

    node.split = SPLIT_X;
    node.ratio = 0.3f;
    area_make_pair(SPLIT_X, 7, 0.3f, &parent, &view_left, &view_right);
    declarative_space_split_areas(&node, 7);
    TEST_CHECK(view_left.h, 22.0f);
    TEST_CHECK(view_right.h, 51.0f);
    TEST_CHECK(view_right.y, 49.0f);
    TEST_CHECK(declarative_left.area.h, view_left.h);
    TEST_CHECK(declarative_right.area.h, view_right.h);
    TEST_CHECK(declarative_right.area.y, view_right.y);

    const enum window_node_split splits[] = { SPLIT_Y, SPLIT_X };
    const int gaps[] = { INT_MIN, INT_MAX };
    const float ratios[] = { 0.9f, 0.1f };
    for (int i = 0; i < 2; ++i) {
        parent = (struct area) {
            .x = 100.0f,
            .y = 200.0f,
            .w = (float) (1920.0 - view_padding_sum(INT_MIN, INT_MIN)),
            .h = (float) (1080.0 - view_padding_sum(INT_MIN, INT_MIN))
        };
        node.area = parent;
        node.split = splits[i];
        node.ratio = ratios[i];

        area_make_pair(splits[i], gaps[i], ratios[i], &parent, &view_left, &view_right);
        declarative_space_split_areas(&node, gaps[i]);

        TEST_CHECK(isfinite(view_left.x) && isfinite(view_left.y) &&
                   isfinite(view_left.w) && isfinite(view_left.h), true);
        TEST_CHECK(isfinite(view_right.x) && isfinite(view_right.y) &&
                   isfinite(view_right.w) && isfinite(view_right.h), true);
        TEST_CHECK(declarative_left.area.x, view_left.x);
        TEST_CHECK(declarative_left.area.y, view_left.y);
        TEST_CHECK(declarative_left.area.w, view_left.w);
        TEST_CHECK(declarative_left.area.h, view_left.h);
        TEST_CHECK(declarative_right.area.x, view_right.x);
        TEST_CHECK(declarative_right.area.y, view_right.y);
        TEST_CHECK(declarative_right.area.w, view_right.w);
        TEST_CHECK(declarative_right.area.h, view_right.h);

        float available = (splits[i] == SPLIT_Y ? parent.w : parent.h) - (float) gaps[i];
        float expected_left = truncf(available * ratios[i]);
        float expected_right = truncf(available * (1.0f - ratios[i]));
        float expected_offset = truncf(available * ratios[i] + 0.5f) + (float) gaps[i];
        if (splits[i] == SPLIT_Y) {
            TEST_CHECK(view_left.w, expected_left);
            TEST_CHECK(view_right.w, expected_right);
            TEST_CHECK(view_right.x, parent.x + expected_offset);
        } else {
            TEST_CHECK(view_left.h, expected_left);
            TEST_CHECK(view_right.h, expected_right);
            TEST_CHECK(view_right.y, parent.y + expected_offset);
        }

        struct area repeated_left, repeated_right;
        area_make_pair(splits[i], gaps[i], ratios[i], &parent, &repeated_left, &repeated_right);
        TEST_CHECK(repeated_left.x, view_left.x);
        TEST_CHECK(repeated_left.y, view_left.y);
        TEST_CHECK(repeated_left.w, view_left.w);
        TEST_CHECK(repeated_left.h, view_left.h);
        TEST_CHECK(repeated_right.x, view_right.x);
        TEST_CHECK(repeated_right.y, view_right.y);
        TEST_CHECK(repeated_right.w, view_right.w);
        TEST_CHECK(repeated_right.h, view_right.h);
    }
});

static size_t insert_feedback_refresh_test_calls;
static struct window_node *insert_feedback_refresh_test_nodes[2];
static void insert_feedback_refresh_test_record(struct window_node *node)
{
    if (insert_feedback_refresh_test_calls < 2) insert_feedback_refresh_test_nodes[insert_feedback_refresh_test_calls] = node;
    ++insert_feedback_refresh_test_calls;
}

TEST_FUNC(insert_feedback_refresh_visits_each_active_node_once,
{
    struct table saved = g_window_manager.insert_feedback;
    memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback));
    table_init(&g_window_manager.insert_feedback, 4, hash_wm, compare_wm);
    struct window_node first = { .window_order = { 101 } };
    struct window_node second = { .window_order = { 202 } };
    table_add(&g_window_manager.insert_feedback, &first.window_order[0], &first);
    table_add(&g_window_manager.insert_feedback, &second.window_order[0], &second);
    insert_feedback_refresh_test_calls = 0;
    memset(insert_feedback_refresh_test_nodes, 0, sizeof(insert_feedback_refresh_test_nodes));
    insert_feedback_refresh_all_with(insert_feedback_refresh_test_record);
    TEST_CHECK(insert_feedback_refresh_test_calls, 2U);
    TEST_CHECK((insert_feedback_refresh_test_nodes[0] == &first && insert_feedback_refresh_test_nodes[1] == &second) ||
               (insert_feedback_refresh_test_nodes[0] == &second && insert_feedback_refresh_test_nodes[1] == &first), true);
    table_free(&g_window_manager.insert_feedback);
    g_window_manager.insert_feedback = saved;
});

TEST_FUNC(insert_feedback_geometry_clamps_configured_values_to_safe_rectangles,
{
    TEST_CHECK(INSERT_FEEDBACK_RADIUS_DEFAULT, 9.0f);
    TEST_CHECK(INSERT_FEEDBACK_WIDTH_DEFAULT, 2.0f);
    CGRect frame = CGRectMake(0.0, 0.0, 100.0, 40.0);
    struct insert_feedback_geometry defaults = insert_feedback_geometry_make(frame, INSERT_FEEDBACK_WIDTH_DEFAULT, INSERT_FEEDBACK_RADIUS_DEFAULT);
    TEST_CHECK(defaults.stroke_width, 2.0);
    TEST_CHECK(defaults.corner_radius_x, 9.0);
    TEST_CHECK(defaults.corner_radius_y, 9.0);
    TEST_CHECK(defaults.fill_radius_x, 8.0);
    TEST_CHECK(defaults.fill_radius_y, 8.0);
    TEST_CHECK(defaults.stroke_rect.size.width, 98.0);
    TEST_CHECK(defaults.stroke_rect.size.height, 38.0);
    TEST_CHECK(defaults.fill_rect.size.width, 96.0);
    TEST_CHECK(defaults.fill_rect.size.height, 36.0);

    struct insert_feedback_geometry clamped = insert_feedback_geometry_make(CGRectMake(0.0, 0.0, 8.0, 4.0), 100.0f, 100.0f);
    TEST_CHECK(clamped.stroke_width, 2.0);
    TEST_CHECK(clamped.stroke_rect.size.width, 6.0);
    TEST_CHECK(clamped.stroke_rect.size.height, 2.0);
    TEST_CHECK(clamped.fill_rect.size.width, 4.0);
    TEST_CHECK(clamped.fill_rect.size.height, 0.0);
    TEST_CHECK(clamped.corner_radius_x, 3.0);
    TEST_CHECK(clamped.corner_radius_y, 1.0);
    TEST_CHECK(clamped.fill_radius_x, 2.0);
    TEST_CHECK(clamped.fill_radius_y, 0.0);

    struct insert_feedback_geometry rounded = insert_feedback_geometry_make(CGRectMake(0.0, 0.0, 20.0, 20.0), 2.0f, 100.0f);
    TEST_CHECK(rounded.corner_radius_x, 9.0);
    TEST_CHECK(rounded.corner_radius_y, 9.0);
    TEST_CHECK(rounded.fill_radius_x, 8.0);
    TEST_CHECK(rounded.fill_radius_y, 8.0);
    CGPathRef fill_path = CGPathCreateWithRoundedRect(rounded.fill_rect, rounded.fill_radius_x, rounded.fill_radius_y, NULL);
    TEST_CHECK(CGPathContainsPoint(fill_path, NULL, CGPointMake(rounded.fill_rect.origin.x, rounded.fill_rect.origin.y), false), false);
    TEST_CHECK(CGPathContainsPoint(fill_path, NULL, CGPointMake(CGRectGetMidX(rounded.fill_rect), CGRectGetMidY(rounded.fill_rect)), false), true);
    CGPathRelease(fill_path);

    struct insert_feedback_geometry zero = insert_feedback_geometry_make(CGRectMake(3.0, 4.0, 0.0, 0.0), FLT_MAX, FLT_MAX);
    TEST_CHECK(zero.stroke_width, 0.0);
    TEST_CHECK(zero.stroke_rect.size.width, 0.0);
    TEST_CHECK(zero.stroke_rect.size.height, 0.0);
    TEST_CHECK(zero.fill_rect.size.width, 0.0);
    TEST_CHECK(zero.fill_rect.size.height, 0.0);
    TEST_CHECK(zero.corner_radius_x, 0.0);
    TEST_CHECK(zero.corner_radius_y, 0.0);
    TEST_CHECK(zero.fill_radius_x, 0.0);
    TEST_CHECK(zero.fill_radius_y, 0.0);

    struct insert_feedback_geometry negative = insert_feedback_geometry_make(CGRectMake(3.0, 4.0, -8.0, -4.0), 2.0f, 9.0f);
    TEST_CHECK(negative.stroke_rect.size.width, 0.0);
    TEST_CHECK(negative.stroke_rect.size.height, 0.0);
    TEST_CHECK(negative.fill_rect.size.width, 0.0);
    TEST_CHECK(negative.fill_rect.size.height, 0.0);
});

TEST_FUNC(view_find_min_depth_leaf_node_handles_complete_tree_boundary,
{
    struct window_node nodes[257] = {0};

    for (int i = 0; i < 128; ++i) {
        nodes[i].left = &nodes[2 * i + 1];
        nodes[i].right = &nodes[2 * i + 2];
        nodes[i].left->parent = &nodes[i];
        nodes[i].right->parent = &nodes[i];
    }

    TEST_CHECK(view_find_min_depth_leaf_node(NULL) == NULL, true);
    TEST_CHECK(view_find_min_depth_leaf_node(nodes) == &nodes[128], true);
});
