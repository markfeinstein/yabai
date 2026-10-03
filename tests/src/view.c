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
