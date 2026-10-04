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

static bool lifecycle_test_space_is_user(uint64_t sid, void *context)
{
    (void) sid; (void) context;
    return false;
}

static CFStringRef lifecycle_test_copy_space_uuid(uint64_t sid, void *context)
{
    (void) sid; (void) context;
    return CFRetain(CFSTR("lifecycle-test"));
}

TEST_FUNC(legacy_view_and_split_allocations_are_transactional,
{
    struct view_test_platform_ops ops = {
        .space_is_user = lifecycle_test_space_is_user,
        .copy_space_uuid = lifecycle_test_copy_space_uuid
    };
    view_test_set_platform_ops(&ops);

    for (size_t ordinal = 1; ordinal <= 2; ++ordinal) {
        legacy_allocation_test_fail_at(ordinal);
        TEST_CHECK(view_create(91) == NULL, true);
        TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    }
    legacy_allocation_test_reset();
    struct view *created = view_create(91);
    TEST_CHECK(created != NULL && created->root != NULL, true);
    view_destroy(created);
    legacy_allocation_free(created);
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);

    struct window_node leaf = {
        .area = { 1, 2, 300, 200 }, .window_list = { 17, 18 },
        .window_order = { 18, 17 }, .window_count = 2, .ratio = 0.63f,
        .split = SPLIT_Y, .child = CHILD_SECOND, .insert_dir = DIR_EAST
    };
    struct view view = { .root = &leaf, .window_gap = 9, .split_type = SPLIT_X, .insertion_point = 17 };
    struct window window = { .id = 44 };
    struct window_node before = leaf;
    for (size_t ordinal = 1; ordinal <= 2; ++ordinal) {
        legacy_allocation_test_fail_at(ordinal);
        TEST_CHECK(window_node_split(&view, &leaf, &window), false);
        TEST_CHECK(memcmp(&leaf, &before, sizeof(leaf)), 0);
        TEST_CHECK(view.insertion_point, 17U);
        TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    }
    legacy_allocation_test_reset();
    TEST_CHECK(window_node_split(&view, &leaf, &window), true);
    TEST_CHECK(leaf.left != NULL && leaf.right != NULL, true);
    TEST_CHECK(leaf.left->parent == &leaf && leaf.right->parent == &leaf, true);
    legacy_allocation_free(leaf.left);
    legacy_allocation_free(leaf.right);
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);

    void *tracked = legacy_allocation_calloc(1, 8);
    void *raw = calloc(1, 8);
    TEST_CHECK(legacy_allocation_test_outstanding(), 1UL);
    legacy_allocation_free(raw);
    TEST_CHECK(legacy_allocation_test_outstanding(), 1UL);
    legacy_allocation_free(tracked);
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    view_test_set_platform_ops(NULL);
});

TEST_FUNC(view_insertion_preparation_enforces_single_root_capacity_and_layout,
{
    const enum view_type single_root_layouts[] = {
        VIEW_STACK, VIEW_ACCORDION_HORIZONTAL, VIEW_ACCORDION_VERTICAL
    };
    struct window window = { .id = 99 };

    for (size_t i = 0; i < array_count(single_root_layouts); ++i) {
        struct window_node root = {0};
        struct view view = { .layout = single_root_layouts[i], .root = &root };
        struct view_insertion_plan plan = {0};

        root.window_count = NODE_MAX_WINDOW_COUNT - 1;
        TEST_CHECK(view_prepare_window_node_insertion(&view, &window, 0, &plan), true);
        TEST_CHECK(plan.left == NULL && plan.right == NULL, true);
        TEST_CHECK(view_commit_window_node_insertion(&view, &window, 0, &plan), &root);
        TEST_CHECK(root.window_count, NODE_MAX_WINDOW_COUNT);

        struct window_node full_before = root;
        TEST_CHECK(view_prepare_window_node_insertion(&view, &window, 0, &plan), false);
        TEST_CHECK(memcmp(&root, &full_before, sizeof(root)), 0);
        TEST_CHECK(plan.left == NULL && plan.right == NULL, true);
    }

    const enum view_type unsupported_layouts[] = { VIEW_FLOAT, VIEW_DEFAULT };
    for (size_t i = 0; i < array_count(unsupported_layouts); ++i) {
        struct window_node root = {0};
        struct view view = { .layout = unsupported_layouts[i], .root = &root };
        struct view_insertion_plan plan = {0};
        TEST_CHECK(view_prepare_window_node_insertion(&view, &window, 0, &plan), false);
        TEST_CHECK(plan.left == NULL && plan.right == NULL, true);
    }
});

static uint32_t *lifecycle_test_no_active_displays(int *count)
{
    *count = 0;
    return NULL;
}

static uint32_t *lifecycle_test_one_active_display(int *count)
{
    static uint32_t display = 1;
    *count = 1;
    return &display;
}

static uint64_t *lifecycle_test_one_space(uint32_t did, int *count)
{
    static uint64_t sid = 91;
    (void) did;
    *count = 1;
    return &sid;
}

static uint32_t *lifecycle_test_two_active_displays(int *count)
{
    static uint32_t displays[] = { 1, 2 };
    *count = 2;
    return displays;
}

static uint64_t *lifecycle_test_second_space_failure(uint32_t did, int *count)
{
    static uint64_t sid = 91;
    if (did == 2) return NULL;
    *count = 1;
    return &sid;
}

TEST_FUNC(space_manager_begin_reports_and_cleans_initialization_failures,
{
    struct view_test_platform_ops view_ops = {
        .space_is_user = lifecycle_test_space_is_user,
        .copy_space_uuid = lifecycle_test_copy_space_uuid
    };
    view_test_set_platform_ops(&view_ops);
    struct space_manager manager = {0};
    table_test_fail_allocation_after(0);
    TEST_CHECK(space_manager_begin(&manager), false);
    TEST_CHECK(manager.did_begin, false);
    TEST_CHECK(manager.view.buckets == NULL, true);
    table_test_fail_allocation_after(-1);

    space_manager_test_active_display_list = lifecycle_test_no_active_displays;
    TEST_CHECK(space_manager_begin(&manager), false);
    TEST_CHECK(manager.did_begin, false);
    TEST_CHECK(manager.view.buckets == NULL, true);

    space_manager_test_active_display_list = lifecycle_test_one_active_display;
    space_manager_test_display_space_list = lifecycle_test_one_space;
    legacy_allocation_test_fail_at(1);
    TEST_CHECK(space_manager_begin(&manager), false);
    TEST_CHECK(manager.did_begin, false);
    TEST_CHECK(manager.view.buckets == NULL, true);
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    legacy_allocation_test_reset();

    space_manager_test_active_display_list = lifecycle_test_two_active_displays;
    space_manager_test_display_space_list = lifecycle_test_second_space_failure;
    TEST_CHECK(space_manager_begin(&manager), false);
    TEST_CHECK(manager.did_begin, false);
    TEST_CHECK(manager.view.buckets == NULL, true);
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);

    space_manager_test_active_display_list = NULL;
    space_manager_test_display_space_list = NULL;
    view_test_set_platform_ops(NULL);
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
