static struct window_node *declarative_space_phase3b_find_wid(struct window_node *node, uint32_t wid)
{
    if (!node || (!node->left && !node->right)) {
        for (int i = 0; node && i < node->window_count; ++i) if (node->window_list[i] == wid) return node;
        return NULL;
    }
    struct window_node *found = declarative_space_phase3b_find_wid(node->left, wid);
    return found ? found : declarative_space_phase3b_find_wid(node->right, wid);
}

static void declarative_space_phase3b_init_leaf(struct window_node *node,
                                                 const uint32_t *membership,
                                                 const uint32_t *order,
                                                 int count)
{
    memset(node, 0, sizeof(*node));
    memcpy(node->window_list, membership, (size_t) count * sizeof(uint32_t));
    memcpy(node->window_order, order, (size_t) count * sizeof(uint32_t));
    node->window_count = count;
}

TEST_FUNC(declarative_space_phase3b_same_layout_reconciles_detached_and_preserves_structure,
{
    uint32_t left_membership[] = { 1, 2 }, left_order[] = { 2, 1 }, right_membership[] = { 3 };
    struct window_node root = { 0 }, left, right;
    declarative_space_phase3b_init_leaf(&left, left_membership, left_order, 2);
    declarative_space_phase3b_init_leaf(&right, right_membership, right_membership, 1);
    root.left = &left; root.right = &right; root.split = SPLIT_X; root.ratio = 0.37f;
    left.parent = &root; right.parent = &root; left.zoom = &root;
    uint64_t sid = 301;
    struct view view = { .sid = sid, .uuid = CFSTR("phase3b-301"), .layout = VIEW_BSP,
                         .root = &root, .insertion_point = 2, .split_type = SPLIT_Y };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view);
    for (uint32_t wid = 1; wid <= 3; ++wid) table_add(&managed, &wid, &view);
    struct declarative_space_target target = { &view, sid, 1, "phase3b-301", true };
    struct declarative_space_topology_entry topology = { sid, 1, "phase3b-301", true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { 1, 3, 4 } }, .counts = { 3 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.layout = VIEW_BSP;
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                         NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    struct declarative_space_prepared_target *change = &preparation.targets[0];
    TEST_CHECK(change->changed, true);
    TEST_CHECK(change->layout_changed, false);
    TEST_CHECK(change->membership_changed, true);
    TEST_CHECK(change->root_owned, true);
    TEST_CHECK(change->final_root != view.root, true);
    TEST_CHECK(change->final_root->split, SPLIT_X);
    TEST_CHECK(change->final_root->ratio, 0.5f);
    TEST_CHECK(change->final_insertion_point, 0U);
    TEST_CHECK(declarative_space_phase3b_find_wid(change->final_root, 1) != NULL, true);
    TEST_CHECK(declarative_space_phase3b_find_wid(change->final_root, 2) == NULL, true);
    TEST_CHECK(declarative_space_phase3b_find_wid(change->final_root, 3) != NULL, true);
    TEST_CHECK(declarative_space_phase3b_find_wid(change->final_root, 4) != NULL, true);
    TEST_CHECK(preparation.replacement_managed_window.count, 3);
    uint32_t four = 4;
    TEST_CHECK(table_find(&preparation.replacement_managed_window, &four), &view);
    TEST_CHECK(view.root, &root);
    TEST_CHECK(root.left, &left);
    TEST_CHECK(left.window_count, 2);

    int old_live_count = managed.count;
    struct window_node *detached = change->final_root;
    change->final_root = NULL; change->root_owned = false;
    declarative_space_destroy_detached_root(detached);
    TEST_CHECK(managed.count, old_live_count);
    TEST_CHECK(table_find(&managed, &four) == NULL, true);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);

    bool reached_success = false;
    for (size_t ordinal = 1; ordinal < 80 && !reached_success; ++ordinal) {
        declarative_space_test_fail_allocation_at(ordinal);
        bool success = declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                                  NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                                  &ops, &preparation);
        TEST_CHECK(view.root, &root);
        TEST_CHECK(root.left, &left);
        TEST_CHECK(managed.count, old_live_count);
        if (success) {
            reached_success = true;
            declarative_space_preparation_destroy(&preparation);
        }
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    }
    declarative_space_test_fail_allocation_at(0);
    TEST_CHECK(reached_success, true);
    table_free(&managed); table_free(&views);
})

TEST_FUNC(declarative_space_phase3b_zero_view_insertion_uses_focused_leaf,
{
    uint32_t one = 1, two = 2;
    struct window_node root = { 0 }, left, right;
    declarative_space_phase3b_init_leaf(&left, &one, &one, 1);
    declarative_space_phase3b_init_leaf(&right, &two, &two, 1);
    root.area = (struct area) { .w = 400, .h = 200 };
    root.left = &left; root.right = &right; root.split = SPLIT_Y; root.ratio = 0.5f;
    left.parent = right.parent = &root;
    left.area = (struct area) { .w = 200, .h = 200 };
    right.area = (struct area) { .x = 200, .w = 200, .h = 200 };

    uint64_t sid = 309;
    struct view view = { .sid = sid, .uuid = CFSTR("phase3b-309"), .layout = VIEW_BSP,
                         .root = &root, .insertion_point = 0, .split_type = SPLIT_Y };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view); table_add(&managed, &one, &view); table_add(&managed, &two, &view);
    struct declarative_space_target target = { &view, sid, 1, "phase3b-309", true };
    struct declarative_space_topology_entry topology = { sid, 1, "phase3b-309", true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { 1, 2, 3 } }, .counts = { 3 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.layout = VIEW_BSP;
    defaults.window_insertion_point = INSERT_FOCUSED;
    defaults.focused_window_id = two;
    defaults.window_placement = CHILD_FIRST;

    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                         NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    struct window_node *focused = declarative_space_phase3b_find_wid(preparation.targets[0].final_root, two);
    TEST_CHECK(focused != NULL, true);
    TEST_CHECK(focused ? focused->parent != NULL : false, true);
    TEST_CHECK(focused && focused->parent ? focused->parent->right : NULL, focused);
    TEST_CHECK(focused && focused->parent ? focused->parent->left->window_list[0] : 0U, 3U);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&views);
})

TEST_FUNC(declarative_space_phase3b_empty_leaf_collapse_matches_live_parent_semantics,
{
    uint32_t one = 1, two = 2;
    struct window_node root = { 0 }, left, right;
    declarative_space_phase3b_init_leaf(&left, &one, &one, 1);
    declarative_space_phase3b_init_leaf(&right, &two, &two, 1);
    root.area = (struct area) { .w = 300, .h = 200 };
    root.left = &left; root.right = &right; root.split = SPLIT_Y; root.ratio = 0.2f;
    left.parent = right.parent = &root;
    left.area = (struct area) { .w = 60, .h = 200 };
    left.split = SPLIT_AUTO; left.ratio = 0.7f; left.child = CHILD_SECOND;
    left.zoom = &root;
    uint64_t sid = 311;
    struct view view = { .sid = sid, .uuid = CFSTR("phase3b-311"), .layout = VIEW_BSP,
                         .root = &root, .insertion_point = one, .split_type = SPLIT_AUTO };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view); table_add(&managed, &one, &view); table_add(&managed, &two, &view);
    struct declarative_space_target target = { &view, sid, 1, "phase3b-311", true };
    struct declarative_space_topology_entry topology = { sid, 1, "phase3b-311", true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { 1, 3 } }, .counts = { 2 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.layout = VIEW_BSP; defaults.auto_balance = SPLIT_NONE;
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                         NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    struct window_node *final = preparation.targets[0].final_root;
    TEST_CHECK(final->area.w, 300.0f); TEST_CHECK(final->area.h, 200.0f);
    TEST_CHECK(final->split, SPLIT_Y); TEST_CHECK(final->ratio, 0.2f);
    TEST_CHECK(final->child, CHILD_NONE);
    TEST_CHECK(final->left->parent, final); TEST_CHECK(final->right->parent, final);
    TEST_CHECK(final->left->window_list[0], 1U); TEST_CHECK(final->right->window_list[0], 3U);
    TEST_CHECK(final->left->area.w, 60.0f); TEST_CHECK(final->right->area.w, 240.0f);
    TEST_CHECK(final->zoom, NULL); TEST_CHECK(final->feedback_window.id, 0U);
    TEST_CHECK(final->left->feedback_window.id, 0U);
    TEST_CHECK(root.area.w, 300.0f); TEST_CHECK(root.left, &left);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&views);
})

TEST_FUNC(declarative_space_phase3b_internal_collapse_reparents_and_remaps_zoom,
{
    uint32_t wids[] = { 1, 2, 3, 4 };
    struct window_node nodes[7] = { 0 };
    struct window_node *root = &nodes[0], *collapse = &nodes[1], *outer = &nodes[2];
    struct window_node *removed = &nodes[3], *survivor = &nodes[4];
    declarative_space_phase3b_init_leaf(removed, &wids[0], &wids[0], 1);
    declarative_space_phase3b_init_leaf(&nodes[5], &wids[1], &wids[1], 1);
    declarative_space_phase3b_init_leaf(&nodes[6], &wids[2], &wids[2], 1);
    declarative_space_phase3b_init_leaf(outer, &wids[3], &wids[3], 1);
    root->left = collapse; root->right = outer; root->split = SPLIT_X; root->ratio = 0.4f;
    collapse->parent = outer->parent = root; collapse->left = removed; collapse->right = survivor;
    collapse->split = SPLIT_Y; collapse->ratio = 0.3f; collapse->area = (struct area) { .w = 400, .h = 300 };
    removed->parent = survivor->parent = collapse;
    survivor->left = &nodes[5]; survivor->right = &nodes[6]; survivor->split = SPLIT_X; survivor->ratio = 0.8f;
    survivor->child = CHILD_FIRST;
    nodes[5].parent = nodes[6].parent = survivor; nodes[5].zoom = survivor; nodes[6].zoom = root;
    uint64_t sid = 312;
    struct view view = { .sid = sid, .uuid = CFSTR("phase3b-312"), .layout = VIEW_BSP, .root = root };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view);
    for (size_t i = 0; i < 4; ++i) table_add(&managed, &wids[i], &view);
    struct declarative_space_target target = { &view, sid, 1, "phase3b-312", true };
    struct declarative_space_topology_entry topology = { sid, 1, "phase3b-312", true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { 2, 3, 4 } }, .counts = { 3 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.layout = VIEW_BSP; defaults.auto_balance = SPLIT_NONE;
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                         NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    struct window_node *final = preparation.targets[0].final_root;
    struct window_node *parent = final->left;
    struct window_node *two_node = declarative_space_phase3b_find_wid(final, 2);
    struct window_node *three_node = declarative_space_phase3b_find_wid(final, 3);
    TEST_CHECK(parent->area.w, 400.0f); TEST_CHECK(parent->ratio, 0.3f);
    TEST_CHECK(parent->split, SPLIT_Y); TEST_CHECK(parent->child, CHILD_NONE);
    TEST_CHECK(parent->insert_dir, 0); TEST_CHECK(parent->feedback_window.id, 0U);
    TEST_CHECK(parent->left->parent, parent); TEST_CHECK(parent->right->parent, parent);
    TEST_CHECK(two_node->zoom, parent); TEST_CHECK(three_node->zoom, final);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&views);
})

TEST_FUNC(declarative_space_phase3b_auto_balance_uses_effective_axis_after_same_layout_split,
{
    const uint32_t modes[] = { SPLIT_NONE, SPLIT_X, SPLIT_Y, SPLIT_X | SPLIT_Y };
    for (size_t mode_index = 0; mode_index < 4; ++mode_index) {
        uint32_t wids[] = { 1, 2, 3 };
        struct window_node nodes[5] = { 0 };
        declarative_space_phase3b_init_leaf(&nodes[1], &wids[0], &wids[0], 1);
        declarative_space_phase3b_init_leaf(&nodes[3], &wids[1], &wids[1], 1);
        declarative_space_phase3b_init_leaf(&nodes[4], &wids[2], &wids[2], 1);
        nodes[0].left = &nodes[1]; nodes[0].right = &nodes[2]; nodes[0].split = SPLIT_Y; nodes[0].ratio = 0.61f;
        nodes[1].parent = nodes[2].parent = &nodes[0];
        nodes[2].left = &nodes[3]; nodes[2].right = &nodes[4]; nodes[2].split = SPLIT_Y; nodes[2].ratio = 0.62f;
        nodes[3].parent = nodes[4].parent = &nodes[2]; nodes[4].split = SPLIT_Y; nodes[4].ratio = 0.73f;
        uint64_t sid = 320 + mode_index;
        struct view view = { .sid = sid, .uuid = CFSTR("phase3b-balance"), .layout = VIEW_BSP,
                             .root = &nodes[0], .insertion_point = 3, .split_type = SPLIT_AUTO };
        struct table views = { 0 }, managed = { 0 };
        table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
        table_add(&views, &sid, &view);
        for (size_t i = 0; i < 3; ++i) table_add(&managed, &wids[i], &view);
        struct declarative_space_target target = { &view, sid, 1, "phase3b-balance", true };
        struct declarative_space_topology_entry topology = { sid, 1, "phase3b-balance", true };
        struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
        struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
        struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { 1, 2, 3, 4 } }, .counts = { 4 } };
        struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                      declarative_space_test_eligible, &capture };
        struct declarative_space_defaults defaults = declarative_space_test_defaults();
        defaults.layout = VIEW_BSP; defaults.auto_balance = modes[mode_index];
        struct declarative_space_preparation preparation;
        TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                             NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                             &ops, &preparation), true);
        struct window_node *final = preparation.targets[0].final_root;
        struct window_node *split_three = declarative_space_phase3b_find_wid(final, 3)->parent;
        bool balance_y = modes[mode_index] & SPLIT_Y;
        TEST_CHECK(final->ratio, balance_y ? 0.25f : 0.61f);
        TEST_CHECK(final->right->ratio, balance_y ? (1.0f / 3.0f) : 0.62f);
        TEST_CHECK(split_three->split, SPLIT_Y);
        TEST_CHECK(split_three->ratio, balance_y ? 0.5f : 0.73f);
        TEST_CHECK(nodes[0].ratio, 0.61f); TEST_CHECK(nodes[2].ratio, 0.62f);
        declarative_space_preparation_destroy(&preparation);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        table_free(&managed); table_free(&views);
    }

    struct window_node x_nodes[7] = { 0 };
    x_nodes[0].left = &x_nodes[1]; x_nodes[0].right = &x_nodes[2]; x_nodes[0].split = SPLIT_X; x_nodes[0].ratio = 0.61f;
    x_nodes[1].parent = x_nodes[2].parent = &x_nodes[0];
    x_nodes[2].left = &x_nodes[3]; x_nodes[2].right = &x_nodes[4]; x_nodes[2].split = SPLIT_X; x_nodes[2].ratio = 0.62f;
    x_nodes[3].parent = x_nodes[4].parent = &x_nodes[2];
    x_nodes[4].left = &x_nodes[5]; x_nodes[4].right = &x_nodes[6]; x_nodes[4].split = SPLIT_X; x_nodes[4].ratio = 0.73f;
    x_nodes[5].parent = x_nodes[6].parent = &x_nodes[4];
    (void) declarative_space_balance(&x_nodes[0], SPLIT_Y);
    TEST_CHECK(x_nodes[0].ratio, 0.61f); TEST_CHECK(x_nodes[2].ratio, 0.62f); TEST_CHECK(x_nodes[4].ratio, 0.73f);
    (void) declarative_space_balance(&x_nodes[0], SPLIT_X);
    TEST_CHECK(x_nodes[0].ratio, 0.25f); TEST_CHECK(x_nodes[2].ratio, 1.0f / 3.0f); TEST_CHECK(x_nodes[4].ratio, 0.5f);
})

TEST_FUNC(declarative_space_phase3b_converted_bsp_balances_each_constructed_split,
{
    uint32_t wids[] = { 1, 2, 3 };
    struct window_node root;
    declarative_space_phase3b_init_leaf(&root, wids, wids, 3);
    uint64_t sid = 330;
    struct view view = { .sid = sid, .uuid = CFSTR("phase3b-convert-balance"), .layout = VIEW_STACK, .root = &root };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view);
    for (size_t i = 0; i < 3; ++i) table_add(&managed, &wids[i], &view);
    struct declarative_space_target target = { &view, sid, 1, "phase3b-convert-balance", true };
    struct declarative_space_topology_entry topology = { sid, 1, "phase3b-convert-balance", true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { 1, 2, 3, 4 } }, .counts = { 4 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.layout = VIEW_BSP; defaults.split_type = SPLIT_Y; defaults.split_ratio = 0.3f;
    defaults.auto_balance = SPLIT_Y;
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                         NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    struct window_node *final = preparation.targets[0].final_root;
    TEST_CHECK(final->ratio, 0.5f);
    TEST_CHECK(final->left->ratio, 0.5f); TEST_CHECK(final->right->ratio, 0.5f);
    TEST_CHECK(root.window_count, 3);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&views);
})

TEST_FUNC(declarative_space_phase3b_cross_view_owner_table_and_scalar_only_reuse,
{
    uint32_t five = 5, six = 6;
    struct window_node roots[2];
    declarative_space_phase3b_init_leaf(&roots[0], &five, &five, 1);
    declarative_space_phase3b_init_leaf(&roots[1], &six, &six, 1);
    struct view live[2] = {
        { .sid = 401, .uuid = CFSTR("phase3b-401"), .layout = VIEW_STACK, .root = &roots[0] },
        { .sid = 402, .uuid = CFSTR("phase3b-402"), .layout = VIEW_STACK, .root = &roots[1] }
    };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &live[0].sid, &live[0]); table_add(&views, &live[1].sid, &live[1]);
    table_add(&managed, &five, &live[0]); table_add(&managed, &six, &live[1]);
    struct declarative_space_target targets[2] = {
        { &live[0], 401, 1, "phase3b-401", true }, { &live[1], 402, 2, "phase3b-402", true }
    };
    struct declarative_space_topology_entry topology[2] = {
        { 401, 1, "phase3b-401", true }, { 402, 2, "phase3b-402", true }
    };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { 401, 402 }, .wids = { { 6 }, { 5 } }, .counts = { 1, 1 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults(); defaults.layout = VIEW_STACK;
    struct declarative_config_model native = { 0 };
    native.config[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_value) { .present = true, .integer = 91 };
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(targets, 2, topology, 2, &views, &vc, &managed, &mc,
                                         &native, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    TEST_CHECK(preparation.targets[0].changed, true);
    TEST_CHECK(preparation.targets[1].changed, true);
    TEST_CHECK(preparation.targets[0].layout_changed, false);
    TEST_CHECK(preparation.targets[1].layout_changed, false);
    TEST_CHECK(table_find(&preparation.replacement_managed_window, &five), &live[1]);
    TEST_CHECK(table_find(&preparation.replacement_managed_window, &six), &live[0]);
    TEST_CHECK(declarative_space_phase3b_find_wid(preparation.targets[0].final_root, six) != NULL, true);
    TEST_CHECK(declarative_space_phase3b_find_wid(preparation.targets[1].final_root, five) != NULL, true);
    TEST_CHECK(live[0].root, &roots[0]); TEST_CHECK(live[1].root, &roots[1]);
    declarative_space_preparation_destroy(&preparation);

    capture.wids[0][0] = 5; capture.wids[1][0] = 6;
    TEST_CHECK(declarative_space_prepare(targets, 2, topology, 2, &views, &vc, &managed, &mc,
                                         &native, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    TEST_CHECK(preparation.targets[0].changed, false);
    TEST_CHECK(preparation.targets[1].changed, false);
    TEST_CHECK(preparation.targets[0].final_root, &roots[0]);
    TEST_CHECK(preparation.targets[1].final_root, &roots[1]);
    TEST_CHECK(preparation.targets[0].effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer, 91);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&views);
})

TEST_FUNC(declarative_space_phase3b_capacity_is_positive_and_overflow_checked,
{
    int capacity = 0;
    TEST_CHECK(declarative_space_table_capacity(0, &capacity), true);
    TEST_CHECK(capacity, 1);
    TEST_CHECK(declarative_space_table_capacity(1, &capacity), true);
    TEST_CHECK(capacity >= 2, true);
    TEST_CHECK(declarative_space_table_capacity(SIZE_MAX, &capacity), false);
})

TEST_FUNC(declarative_space_phase3b_bsp_split_matches_captured_live_semantics,
{
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.split_type = SPLIT_AUTO;
    defaults.split_ratio = 0.61f;
    defaults.window_placement = CHILD_SECOND;
    defaults.window_insertion_point = INSERT_LAST;
    defaults.window_zoom_persist = true;

    struct window_node *root = declarative_space_allocate(1, sizeof(*root));
    struct window_node *left = declarative_space_allocate(1, sizeof(*left));
    struct window_node *right = declarative_space_allocate(1, sizeof(*right));
    uint32_t one = 1, stack_members[] = { 2, 3 }, stack_order[] = { 3, 2 };
    declarative_space_phase3b_init_leaf(left, &one, &one, 1);
    declarative_space_phase3b_init_leaf(right, stack_members, stack_order, 2);
    root->left = left; root->right = right; root->split = SPLIT_Y; root->ratio = 0.5f;
    left->parent = right->parent = root;
    right->area = (struct area) { .w = 300, .h = 100 };
    right->split = SPLIT_AUTO; right->ratio = 0.3f; right->child = CHILD_FIRST; right->zoom = root;

    defaults.window_gap_enabled = true;
    defaults.window_gap = 10;
    TEST_CHECK(declarative_space_bsp_add(root, right, 4, SPLIT_AUTO, 10, &defaults), true);
    TEST_CHECK(right->split, SPLIT_Y);
    TEST_CHECK(right->ratio, 0.3f);
    TEST_CHECK(right->child, CHILD_FIRST);
    TEST_CHECK(right->left->window_list[0], 4U);
    TEST_CHECK(right->right->window_count, 2);
    TEST_CHECK(right->right->window_list[0], 2U);
    TEST_CHECK(right->right->window_order[0], 3U);
    TEST_CHECK(right->right->zoom, right);
    TEST_CHECK(right->left->area.w, 87.0f);
    TEST_CHECK(right->right->area.w, 203.0f);
    TEST_CHECK(right->right->area.x, 97.0f);
    TEST_CHECK(right->left->feedback_window.id, 0U);
    TEST_CHECK(right->right->feedback_window.id, 0U);

    struct window_node *old = right->right;
    old->split = SPLIT_NONE; old->ratio = 0.0f; old->child = CHILD_SECOND;
    old->area = (struct area) { .w = 80, .h = 240 };
    defaults.split_type = SPLIT_NONE;
    TEST_CHECK(declarative_space_bsp_add(root, old, 5, SPLIT_NONE, 10, &defaults), true);
    TEST_CHECK(old->split, SPLIT_X);
    TEST_CHECK(old->ratio, 0.61f);
    TEST_CHECK(old->left->window_count, 2);
    TEST_CHECK(old->right->window_list[0], 5U);
    TEST_CHECK(old->left->window_order[0], 3U);
    TEST_CHECK(old->left->area.h, 140.0f);
    TEST_CHECK(old->right->area.h, 89.0f);
    TEST_CHECK(old->right->area.y, 150.0f);

    declarative_space_destroy_detached_root(root);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})

TEST_FUNC(declarative_space_phase3b_dynamic_capture_supports_maximum_and_detects_limit_plus_one_duplicate,
{
    uint32_t generated[NODE_MAX_WINDOW_COUNT + 1];
    for (size_t i = 0; i < NODE_MAX_WINDOW_COUNT + 1; ++i) generated[i] = (uint32_t) i + 100;
    struct view view = { 0 };
    struct declarative_space_target target = { &view, 700, 1, "dynamic", true };
    struct declarative_space_test_capture capture = { .sids = { 700 }, .counts = { NODE_MAX_WINDOW_COUNT } };
    capture.dynamic_wids[0] = generated;
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_inventory inventory;
    TEST_CHECK(declarative_space_capture_inventory(&target, 1, &ops, &inventory), true);
    TEST_CHECK(inventory.count, (size_t) NODE_MAX_WINDOW_COUNT);
    TEST_CHECK(inventory.entries[NODE_MAX_WINDOW_COUNT - 1].wid, generated[NODE_MAX_WINDOW_COUNT - 1]);
    declarative_space_inventory_destroy(&inventory);
    capture.counts[0] = NODE_MAX_WINDOW_COUNT + 1;
    generated[NODE_MAX_WINDOW_COUNT] = generated[0];
    TEST_CHECK(declarative_space_capture_inventory(&target, 1, &ops, &inventory), false);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})

TEST_FUNC(declarative_space_phase3b_complete_pipeline_has_indexed_operation_bound,
{
    enum { view_count = 4, per_view = 1000, inventory_count = view_count * per_view };
    uint32_t generated[inventory_count];
    for (size_t i = 0; i < inventory_count; ++i) generated[i] = (uint32_t) i + 1;
    struct window_node roots[view_count] = { 0 };
    struct view views[view_count] = {
        { .sid = 801, .uuid = CFSTR("pipeline-801"), .layout = VIEW_FLOAT, .root = &roots[0] },
        { .sid = 802, .uuid = CFSTR("pipeline-802"), .layout = VIEW_FLOAT, .root = &roots[1] },
        { .sid = 803, .uuid = CFSTR("pipeline-803"), .layout = VIEW_FLOAT, .root = &roots[2] },
        { .sid = 804, .uuid = CFSTR("pipeline-804"), .layout = VIEW_FLOAT, .root = &roots[3] }
    };
    const char *uuids[view_count] = { "pipeline-801", "pipeline-802", "pipeline-803", "pipeline-804" };
    struct declarative_space_target targets[view_count];
    struct declarative_space_topology_entry topology[view_count];
    struct table view_table = { 0 }, managed = { 0 };
    table_init(&view_table, 8, hash_wm, compare_wm);
    table_init(&managed, 8, hash_wm, compare_wm);
    struct declarative_space_test_capture capture = { 0 };
    for (size_t i = 0; i < view_count; ++i) {
        targets[i] = (struct declarative_space_target) { &views[i], views[i].sid, (int) i + 1, uuids[i], true };
        topology[i] = (struct declarative_space_topology_entry) { views[i].sid, (int) i + 1, uuids[i], true };
        table_add(&view_table, &views[i].sid, &views[i]);
        capture.sids[i] = views[i].sid;
        capture.dynamic_wids[i] = generated + i * per_view;
        capture.counts[i] = per_view;
    }
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.layout = VIEW_FLOAT;
    struct declarative_space_preparation preparation;
    declarative_space_test_reset_index_operations();
    TEST_CHECK(declarative_space_prepare(targets, view_count, topology, view_count, &view_table, &vc,
                                         &managed, &mc, NULL, NULL, &defaults,
                                         DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preparation), true);
    size_t operations = declarative_space_test_index_operations();
    /* Complete preparation, including identity, inventory, owner, forest and replacement validation. */
    TEST_CHECK(operations <= 32UL * inventory_count * 12UL, true);
    TEST_CHECK(operations >= inventory_count, true);
    TEST_CHECK(preparation.inventory.count, (size_t) inventory_count);
    TEST_CHECK(preparation.final_owner_count, 0UL);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&view_table);
})

static struct window_node *declarative_space_phase3b_build_balanced_tree(struct window_node *nodes,
                                                                          size_t *used,
                                                                          uint32_t first,
                                                                          uint32_t count,
                                                                          struct window_node *parent)
{
    struct window_node *node = &nodes[(*used)++];
    node->parent = parent;
    if (count == 1) {
        declarative_space_phase3b_init_leaf(node, &first, &first, 1);
        node->parent = parent;
    } else {
        uint32_t left_count = count / 2;
        node->split = SPLIT_Y; node->ratio = 0.5f;
        node->left = declarative_space_phase3b_build_balanced_tree(nodes, used, first, left_count, node);
        node->right = declarative_space_phase3b_build_balanced_tree(nodes, used, first + left_count,
                                                                     count - left_count, node);
    }
    return node;
}

TEST_FUNC(declarative_space_phase3b_large_changed_layout_is_index_bounded,
{
    enum { count = 1000 };
    struct window_node *nodes = calloc(count * 2 - 1, sizeof(*nodes));
    size_t used = 0;
    struct window_node *root = declarative_space_phase3b_build_balanced_tree(nodes, &used, 1, count, NULL);
    uint64_t sid = 901;
    struct view view = { .sid = sid, .uuid = CFSTR("changed-901"), .layout = VIEW_BSP, .root = root };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 2048, hash_wm, compare_wm);
    table_add(&views, &sid, &view);
    for (uint32_t wid = 1; wid <= count; ++wid) table_add(&managed, &wid, &view);
    struct declarative_space_target target = { &view, sid, 1, "changed-901", true };
    struct declarative_space_topology_entry topology = { sid, 1, "changed-901", true };
    struct declarative_space_test_capture capture = { .sids = { sid }, .counts = { 0 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_defaults defaults = declarative_space_test_defaults(); defaults.layout = VIEW_BSP;
    struct declarative_space_policy policy = { .uuid = "changed-901" };
    policy.settings[DECLARATIVE_CONFIG_LAYOUT] = (struct declarative_value) { .present = true, .enumeration = VIEW_FLOAT };
    struct declarative_config_model native = { .spaces = &policy, .space_count = 1 };
    struct declarative_space_preparation preparation;
    declarative_space_test_reset_index_operations();
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                         &native, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    size_t operations = declarative_space_test_index_operations();
    TEST_CHECK(operations <= 256UL * count * 10UL, true);
    TEST_CHECK(preparation.targets[0].layout_changed, true);
    TEST_CHECK(preparation.targets[0].tree_index_count, (size_t) count);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&views); free(nodes);

    /* Exercise every sorted-index alias operation at 4x the window scale. */
    enum { alias_count = 4000, alias_nodes = alias_count * 2 - 1 };
    struct window_node *left_nodes = calloc(alias_nodes, sizeof(*left_nodes));
    struct window_node *right_nodes = calloc(alias_nodes, sizeof(*right_nodes));
    struct window_node **left_index = calloc(alias_nodes, sizeof(*left_index));
    struct window_node **right_index = calloc(alias_nodes, sizeof(*right_index));
    size_t left_used = 0, right_used = 0, left_indexed = 0, right_indexed = 0;
    struct window_node *left_root = declarative_space_phase3b_build_balanced_tree(left_nodes, &left_used, 1, alias_count, NULL);
    struct window_node *right_root = declarative_space_phase3b_build_balanced_tree(right_nodes, &right_used, 5001, alias_count, NULL);
    declarative_space_test_reset_index_operations();
    declarative_space_fill_node_index(left_root, left_index, &left_indexed);
    declarative_space_fill_node_index(right_root, right_index, &right_indexed);
    qsort(left_index, left_indexed, sizeof(*left_index), declarative_space_compare_node_pointer);
    qsort(right_index, right_indexed, sizeof(*right_index), declarative_space_compare_node_pointer);
    TEST_CHECK(declarative_space_sorted_node_indexes_disjoint(left_index, left_indexed,
                                                              right_index, right_indexed), true);
    size_t alias_operations = declarative_space_test_index_operations();
    TEST_CHECK(alias_operations <= 64UL * alias_nodes, true);
    TEST_CHECK(alias_operations >= 2UL * alias_nodes, true);
    free(right_index); free(left_index); free(right_nodes); free(left_nodes);
})

TEST_FUNC(declarative_space_phase3b_large_same_layout_reconciliation_is_index_bounded,
{
    enum { count = 1000 };
    struct window_node *nodes = calloc(count * 2 - 1, sizeof(*nodes));
    size_t used = 0;
    struct window_node *root = declarative_space_phase3b_build_balanced_tree(nodes, &used, 1, count, NULL);
    uint32_t generated[count];
    for (size_t i = 0; i < count; ++i) generated[i] = (uint32_t) i + 501;
    uint64_t sid = 902;
    struct view view = { .sid = sid, .uuid = CFSTR("same-902"), .layout = VIEW_BSP, .root = root };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 2048, hash_wm, compare_wm);
    table_add(&views, &sid, &view);
    for (uint32_t wid = 1; wid <= count; ++wid) table_add(&managed, &wid, &view);
    struct declarative_space_target target = { &view, sid, 1, "same-902", true };
    struct declarative_space_topology_entry topology = { sid, 1, "same-902", true };
    struct declarative_space_test_capture capture = { .sids = { sid }, .counts = { count } };
    capture.dynamic_wids[0] = generated;
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.layout = VIEW_BSP; defaults.window_insertion_point = INSERT_LAST;
    struct declarative_space_preparation preparation;
    declarative_space_test_reset_index_operations();
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                         NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                         &ops, &preparation), true);
    size_t operations = declarative_space_test_index_operations();
    TEST_CHECK(operations <= 256UL * count * 10UL, true);
    TEST_CHECK(preparation.targets[0].layout_changed, false);
    TEST_CHECK(preparation.targets[0].membership_changed, true);
    TEST_CHECK(preparation.targets[0].tree_index_count, 1500UL);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&views); free(nodes);
})

TEST_FUNC(declarative_space_phase3b_replacement_table_collision_matrix,
{
    enum { owner_count = 6 };
    struct view owners[2] = { { .layout = VIEW_STACK }, { .layout = VIEW_STACK } };
    struct window_node roots[2] = { 0 };
    uint32_t keys[owner_count] = { 1, 9, 17, 2, 10, 18 };
    for (size_t i = 0; i < 2; ++i) {
        owners[i].root = &roots[i];
        roots[i].window_count = 3;
        for (size_t j = 0; j < 3; ++j) {
            size_t index = i * 3 + j;
            roots[i].window_list[j] = roots[i].window_order[j] = keys[index];
        }
    }
    struct declarative_space_preparation preparation = { 0 };
    struct declarative_space_prepared_target targets[2] = {
        { .target = { .view = &owners[0] }, .final_root = &roots[0] },
        { .target = { .view = &owners[1] }, .final_root = &roots[1] }
    };
    struct declarative_space_final_owner final_owners[owner_count];
    for (size_t i = 0; i < owner_count; ++i) {
        final_owners[i] = (struct declarative_space_final_owner) { keys[i], NULL, &owners[i / 3] };
    }
    preparation.targets = targets;
    preparation.target_count = 2;
    preparation.final_owners = final_owners;
    preparation.final_owner_count = owner_count;
    struct declarative_space_table_contract contract = { hash_wm, compare_wm, sizeof(uint32_t) };
    TEST_CHECK(declarative_space_build_replacement_table(&preparation, &contract), true);
    TEST_CHECK(preparation.replacement_managed_window.hash, hash_wm);
    TEST_CHECK(preparation.replacement_managed_window.cmp, compare_wm);
    TEST_CHECK(preparation.replacement_managed_window.capacity, 8);
    TEST_CHECK(preparation.replacement_managed_window.count, owner_count);
    TEST_CHECK(preparation.replacement_managed_window.max_load, 0.75f);
    for (size_t i = 0; i < owner_count; ++i) {
        struct bucket *bucket = preparation.replacement_managed_window.buckets[keys[i] % 8];
        bool found_independent_key = false;
        while (bucket) {
            if (*(uint32_t *) bucket->key == keys[i]) {
                found_independent_key = bucket->key != &keys[i] && bucket->value == &owners[i / 3];
                break;
            }
            bucket = bucket->next;
        }
        TEST_CHECK(found_independent_key, true);
        TEST_CHECK(table_find(&preparation.replacement_managed_window, &keys[i]), &owners[i / 3]);
    }
    TEST_CHECK(declarative_space_validate_final_state(&preparation), true);

    /* Every retained and changed view is checked through the same all-view validator. */
    final_owners[0].view = &owners[1];
    TEST_CHECK(declarative_space_validate_final_state(&preparation), false);
    final_owners[0].view = &owners[0];
    roots[0].window_count = 2;
    TEST_CHECK(declarative_space_validate_final_state(&preparation), false);
    roots[0].window_count = 3;
    final_owners[5].wid = final_owners[4].wid;
    struct declarative_space_preparation duplicate = { .final_owners = final_owners, .final_owner_count = owner_count };
    TEST_CHECK(declarative_space_build_replacement_table(&duplicate, &contract), false);
    declarative_space_destroy_replacement_table(&duplicate.replacement_managed_window);
    final_owners[5].wid = keys[5];
    declarative_space_destroy_replacement_table(&preparation.replacement_managed_window);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})

TEST_FUNC(declarative_space_phase3b_detached_destructor_isolation_matrix,
{
    struct window_manager manager_before = g_window_manager;
    struct mouse_state mouse_before = g_mouse_state;
    struct table managed_before = g_window_manager.managed_window;
    struct table feedback_before = g_window_manager.insert_feedback;
    size_t platform_feedback_cleanup_calls = 0;
    const size_t node_counts[] = { 0, 1, 1, 1, 3, NODE_MAX_WINDOW_COUNT };
    const enum view_type layouts[] = { VIEW_BSP, VIEW_BSP, VIEW_STACK, VIEW_FLOAT, VIEW_BSP, VIEW_STACK };
    for (size_t case_index = 0; case_index < sizeof(node_counts) / sizeof(node_counts[0]); ++case_index) {
        struct window_node *root = NULL;
        if (node_counts[case_index]) {
            root = declarative_space_allocate(1, sizeof(*root));
            TEST_CHECK(root != NULL, true);
            root->window_count = layouts[case_index] == VIEW_BSP && node_counts[case_index] == 3 ? 0 :
                                 (int) node_counts[case_index];
            if (node_counts[case_index] == 3) {
                root->split = SPLIT_Y; root->ratio = 0.5f;
                root->left = declarative_space_allocate(1, sizeof(*root));
                root->right = declarative_space_allocate(1, sizeof(*root));
                TEST_CHECK(root->left != NULL && root->right != NULL, true);
                root->left->parent = root->right->parent = root;
                root->left->window_count = root->right->window_count = 1;
            }
        }
        declarative_space_destroy_detached_root(root);
        TEST_CHECK(memcmp(&g_window_manager, &manager_before, sizeof(manager_before)), 0);
        TEST_CHECK(memcmp(&g_mouse_state, &mouse_before, sizeof(mouse_before)), 0);
        TEST_CHECK(memcmp(&g_window_manager.managed_window, &managed_before, sizeof(managed_before)), 0);
        TEST_CHECK(memcmp(&g_window_manager.insert_feedback, &feedback_before, sizeof(feedback_before)), 0);
        TEST_CHECK(platform_feedback_cleanup_calls, 0UL);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    }
})

TEST_FUNC(declarative_space_phase3b_large_inventory_has_indexed_operation_bound,
{
    enum { inventory_count = 4000 };
    uint32_t generated[inventory_count];
    for (size_t i = 0; i < inventory_count; ++i) generated[i] = (uint32_t) i + 1;
    struct view view = { 0 };
    struct declarative_space_target target = { &view, 701, 1, "large-index", true };
    struct declarative_space_test_capture capture = { .sids = { 701 }, .counts = { inventory_count } };
    capture.dynamic_wids[0] = generated;
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &capture };
    struct declarative_space_inventory inventory;
    declarative_space_test_reset_index_operations();
    TEST_CHECK(declarative_space_capture_inventory(&target, 1, &ops, &inventory), true);
    size_t operations = declarative_space_test_index_operations();
    /* A comparison-count bound, rather than a timing assertion: 32*n*ceil(log2(n)). */
    TEST_CHECK(operations <= 32UL * inventory_count * 12UL, true);
    TEST_CHECK(operations >= inventory_count - 1, true);
    TEST_CHECK(inventory.wid_index[0].wid, 1U);
    TEST_CHECK(inventory.wid_index[inventory_count - 1].wid, (uint32_t) inventory_count);
    declarative_space_inventory_destroy(&inventory);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})

struct declarative_space_phase3b_matrix_capture
{
    uint64_t sids[2];
    const uint32_t *wids[2];
    size_t counts[2];
    struct window windows[2 * (NODE_MAX_WINDOW_COUNT + 1)];
    uint32_t window_ids[2 * (NODE_MAX_WINDOW_COUNT + 1)];
    size_t window_count;
    uint32_t missing_wid;
    uint32_t ineligible_wid;
    uint32_t mismatch_wid;
};

static bool declarative_space_phase3b_matrix_query(uint64_t sid, const uint32_t **wids,
                                                    size_t *count, void *context)
{
    struct declarative_space_phase3b_matrix_capture *capture = context;
    for (size_t i = 0; i < 2; ++i) {
        if (capture->sids[i] == sid) {
            *wids = capture->wids[i];
            *count = capture->counts[i];
            return true;
        }
    }
    return false;
}

static struct window *declarative_space_phase3b_matrix_find(uint32_t wid, void *context)
{
    struct declarative_space_phase3b_matrix_capture *capture = context;
    if (wid == capture->missing_wid) return NULL;
    for (size_t i = 0; i < capture->window_count; ++i) {
        if (capture->window_ids[i] == wid) {
            capture->windows[i].id = wid == capture->mismatch_wid ? wid + 1 : wid;
            return &capture->windows[i];
        }
    }
    return NULL;
}

static bool declarative_space_phase3b_matrix_eligible(struct window *window, void *context)
{
    struct declarative_space_phase3b_matrix_capture *capture = context;
    return window->id != capture->ineligible_wid;
}

static void declarative_space_phase3b_matrix_root(struct window_node nodes[3], enum view_type layout,
                                                   const uint32_t *wids, size_t count)
{
    memset(nodes, 0, sizeof(*nodes) * 3);
    if (layout == VIEW_FLOAT || !count) return;
    if (layout == VIEW_BSP && count > 1) {
        size_t left_count = count / 2, right_count = count - left_count;
        nodes[0].left = &nodes[1]; nodes[0].right = &nodes[2];
        nodes[0].split = SPLIT_X; nodes[0].ratio = 0.37f;
        nodes[0].area = (struct area) { .x = 1, .y = 2, .w = 800, .h = 600 };
        nodes[1].parent = nodes[2].parent = &nodes[0];
        declarative_space_phase3b_init_leaf(&nodes[1], wids, wids, (int) left_count);
        declarative_space_phase3b_init_leaf(&nodes[2], wids + left_count, wids + left_count, (int) right_count);
        nodes[1].parent = nodes[2].parent = &nodes[0];
        nodes[1].split = SPLIT_Y; nodes[1].ratio = 0.42f; nodes[1].child = CHILD_FIRST;
        nodes[2].split = SPLIT_AUTO; nodes[2].ratio = 0.58f; nodes[2].child = CHILD_SECOND;
        for (size_t i = 0; i < left_count; ++i) nodes[1].window_order[i] = wids[left_count - i - 1];
        for (size_t i = 0; i < right_count; ++i) nodes[2].window_order[i] = wids[count - i - 1];
        nodes[1].zoom = &nodes[0];
    } else {
        declarative_space_phase3b_init_leaf(&nodes[0], wids, wids, (int) count);
        nodes[0].split = SPLIT_Y; nodes[0].ratio = 0.42f; nodes[0].child = CHILD_FIRST;
        for (size_t i = 0; i < count && i < NODE_MAX_WINDOW_COUNT; ++i) {
            nodes[0].window_order[i] = wids[count - i - 1];
        }
    }
}

static void declarative_space_phase3b_matrix_flatten(struct window_node *node,
                                                       uint32_t *membership, uint32_t *order,
                                                       size_t *count)
{
    if (node->left) {
        declarative_space_phase3b_matrix_flatten(node->left, membership, order, count);
        declarative_space_phase3b_matrix_flatten(node->right, membership, order, count);
        return;
    }
    for (int i = 0; i < node->window_count; ++i) {
        membership[*count] = node->window_list[i];
        order[*count] = node->window_order[i];
        ++*count;
    }
}

static bool declarative_space_phase3b_matrix_contains(const uint32_t *items, size_t count, uint32_t wid)
{
    for (size_t i = 0; i < count; ++i) if (items[i] == wid) return true;
    return false;
}

TEST_FUNC(declarative_space_phase3b_generated_production_scenario_matrix,
{
    static const struct { const char *name; enum view_type from, to; } layouts[] = {
        { "same-bsp", VIEW_BSP, VIEW_BSP }, { "same-stack", VIEW_STACK, VIEW_STACK },
        { "bsp-to-stack", VIEW_BSP, VIEW_STACK }, { "stack-to-bsp", VIEW_STACK, VIEW_BSP },
        { "bsp-to-float", VIEW_BSP, VIEW_FLOAT }, { "stack-to-float", VIEW_STACK, VIEW_FLOAT },
        { "float-to-bsp", VIEW_FLOAT, VIEW_BSP }, { "float-to-stack", VIEW_FLOAT, VIEW_STACK },
        { "same-accordion-horizontal", VIEW_ACCORDION_HORIZONTAL, VIEW_ACCORDION_HORIZONTAL },
        { "same-accordion-vertical", VIEW_ACCORDION_VERTICAL, VIEW_ACCORDION_VERTICAL },
        { "bsp-to-accordion-horizontal", VIEW_BSP, VIEW_ACCORDION_HORIZONTAL },
        { "stack-to-accordion-vertical", VIEW_STACK, VIEW_ACCORDION_VERTICAL },
        { "float-to-accordion-horizontal", VIEW_FLOAT, VIEW_ACCORDION_HORIZONTAL },
        { "accordion-horizontal-to-bsp", VIEW_ACCORDION_HORIZONTAL, VIEW_BSP },
        { "accordion-vertical-to-stack", VIEW_ACCORDION_VERTICAL, VIEW_STACK },
        { "accordion-horizontal-to-vertical", VIEW_ACCORDION_HORIZONTAL, VIEW_ACCORDION_VERTICAL }
    };
    static const size_t cardinalities[] = { 0, 1, NODE_MAX_WINDOW_COUNT };
    static const char *row_names[] = {
        "same-bsp-zero", "same-bsp-one", "same-bsp-max",
        "same-stack-zero", "same-stack-one", "same-stack-max",
        "bsp-to-stack-zero", "bsp-to-stack-one", "bsp-to-stack-max",
        "stack-to-bsp-zero", "stack-to-bsp-one", "stack-to-bsp-max",
        "bsp-to-float-zero", "bsp-to-float-one", "bsp-to-float-max",
        "stack-to-float-zero", "stack-to-float-one", "stack-to-float-max",
        "float-to-bsp-zero", "float-to-bsp-one", "float-to-bsp-max",
        "float-to-stack-zero", "float-to-stack-one", "float-to-stack-max",
        "same-accordion-horizontal-zero", "same-accordion-horizontal-one", "same-accordion-horizontal-max",
        "same-accordion-vertical-zero", "same-accordion-vertical-one", "same-accordion-vertical-max",
        "bsp-to-accordion-horizontal-zero", "bsp-to-accordion-horizontal-one", "bsp-to-accordion-horizontal-max",
        "stack-to-accordion-vertical-zero", "stack-to-accordion-vertical-one", "stack-to-accordion-vertical-max",
        "float-to-accordion-horizontal-zero", "float-to-accordion-horizontal-one", "float-to-accordion-horizontal-max",
        "accordion-horizontal-to-bsp-zero", "accordion-horizontal-to-bsp-one", "accordion-horizontal-to-bsp-max",
        "accordion-vertical-to-stack-zero", "accordion-vertical-to-stack-one", "accordion-vertical-to-stack-max",
        "accordion-horizontal-to-vertical-zero", "accordion-horizontal-to-vertical-one", "accordion-horizontal-to-vertical-max",
        "root-replace-landscape", "stack-to-bsp-landscape", "float-to-bsp-landscape",
        "root-replace-portrait", "stack-to-bsp-portrait", "float-to-bsp-portrait",
        "auto-descendant-gap-disabled", "auto-descendant-gap-enabled",
        "same-stack-over-limit-input", "bsp-to-stack-over-limit-conversion",
        "float-to-stack-over-limit-capture", "non-user-forced-float", "unknown-sid",
        "cross-sid-duplicate", "missing-eligible-object", "ineligible-removal",
        "mismatched-wid-object", "malformed-unchanged-root", "duplicate-final-owner"
    };
    TEST_CHECK(sizeof(row_names) / sizeof(row_names[0]), 67UL);
    size_t rows_run = 0;
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };

    for (size_t layout_index = 0; layout_index < sizeof(layouts) / sizeof(layouts[0]); ++layout_index) {
        for (size_t cardinality_index = 0; cardinality_index < 3; ++cardinality_index) {
            const size_t count = cardinalities[cardinality_index];
            uint32_t source_wids[NODE_MAX_WINDOW_COUNT], capture_wids[NODE_MAX_WINDOW_COUNT];
            for (size_t i = 0; i < count; ++i) source_wids[i] = capture_wids[i] = 100 + (uint32_t) i;
            bool reconcile = layouts[layout_index].from == layouts[layout_index].to && count == NODE_MAX_WINDOW_COUNT;
            if (reconcile) capture_wids[count - 1] = 500;
            struct window_node nodes[3];
            size_t source_count = layouts[layout_index].from == VIEW_FLOAT ? 0 : count;
            declarative_space_phase3b_matrix_root(nodes, layouts[layout_index].from, source_wids, source_count);
            uint64_t sid = 1200 + rows_run;
            struct view view = { .sid = sid, .uuid = CFSTR("phase3b-matrix"),
                                 .layout = layouts[layout_index].from, .root = &nodes[0],
                                 .split_type = SPLIT_Y,
                                 .insertion_point = source_count ? source_wids[source_count - 1] : 0 };
            struct window_node source_before[3]; memcpy(source_before, nodes, sizeof(nodes));
            struct view view_before = view;
            struct table views = { 0 }, managed = { 0 };
            table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 64, hash_wm, compare_wm);
            table_add(&views, &sid, &view);
            for (size_t i = 0; i < source_count; ++i) table_add(&managed, &source_wids[i], &view);
            struct declarative_space_target target = { &view, sid, 1, "phase3b-matrix", true };
            struct declarative_space_topology_entry topology = { sid, 1, "phase3b-matrix", true };
            struct declarative_space_phase3b_matrix_capture capture = { .sids = { sid },
                .wids = { capture_wids }, .counts = { count } };
            for (size_t i = 0; i < count; ++i) {
                capture.window_ids[capture.window_count++] = capture_wids[i];
            }
            struct declarative_space_capture_ops ops = { declarative_space_phase3b_matrix_query,
                declarative_space_phase3b_matrix_find, declarative_space_phase3b_matrix_eligible, &capture };
            struct declarative_space_defaults defaults = declarative_space_test_defaults();
            defaults.layout = layouts[layout_index].to;
            struct declarative_space_preparation preparation;
            TEST_CHECK(strcmp(row_names[rows_run], row_names[layout_index * 3 + cardinality_index]), 0);
            TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                                 NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                                 &ops, &preparation), true);
            struct declarative_space_prepared_target *prepared = &preparation.targets[0];
            bool expected_changed = layouts[layout_index].from != layouts[layout_index].to || reconcile;
            TEST_CHECK(prepared->changed, expected_changed);
            TEST_CHECK(prepared->layout_changed, layouts[layout_index].from != layouts[layout_index].to);
            TEST_CHECK(prepared->membership_changed, reconcile ||
                       (layouts[layout_index].from == VIEW_FLOAT && count != 0) ||
                       (layouts[layout_index].to == VIEW_FLOAT && count != 0));
            TEST_CHECK(prepared->root_owned, expected_changed);
            TEST_CHECK(prepared->final_root == view.root, !expected_changed);
            TEST_CHECK(memcmp(nodes, source_before, sizeof(nodes)), 0);
            TEST_CHECK(memcmp(&view, &view_before, sizeof(view)), 0);
            enum view_type final_layout = layouts[layout_index].to;
            struct view validation = view; validation.layout = final_layout;
            validation.root = prepared->final_root; validation.insertion_point = prepared->final_insertion_point;
            struct declarative_space_root_before_image final_image;
            TEST_CHECK(declarative_space_validate_root(&validation, &final_image), true);
            size_t expected_final_count = final_layout == VIEW_FLOAT ? 0 : count;
            TEST_CHECK(final_image.window_count, expected_final_count);
            TEST_CHECK(preparation.final_owner_count, expected_final_count);
            TEST_CHECK(preparation.replacement_managed_window.count, (int) expected_final_count);
            uint32_t final_membership[NODE_MAX_WINDOW_COUNT] = { 0 };
            uint32_t final_order[NODE_MAX_WINDOW_COUNT] = { 0 };
            size_t final_count = 0;
            declarative_space_phase3b_matrix_flatten(prepared->final_root, final_membership, final_order, &final_count);
            TEST_CHECK(final_count, expected_final_count);
            for (size_t i = 0; i < expected_final_count; ++i) {
                TEST_CHECK(declarative_space_phase3b_matrix_contains(final_membership, final_count, capture_wids[i]), true);
                TEST_CHECK(declarative_space_phase3b_matrix_contains(final_order, final_count, capture_wids[i]), true);
                TEST_CHECK(preparation.final_owners[i].wid, capture_wids[i]);
                TEST_CHECK(preparation.final_owners[i].view, &view);
                TEST_CHECK(preparation.final_owners[i].window != NULL, true);
                TEST_CHECK(table_find(&preparation.replacement_managed_window, &capture_wids[i]), &view);
            }
            if (prepared->layout_changed) {
                uint32_t source_membership[NODE_MAX_WINDOW_COUNT] = { 0 };
                uint32_t source_order[NODE_MAX_WINDOW_COUNT] = { 0 };
                size_t flattened = 0;
                declarative_space_phase3b_matrix_flatten(&nodes[0], source_membership, source_order, &flattened);
                TEST_CHECK(prepared->flatten_count, source_count);
                TEST_CHECK(memcmp(prepared->flatten_membership, source_membership,
                                  source_count * sizeof(uint32_t)), 0);
                TEST_CHECK(memcmp(prepared->flatten_order, source_order,
                                  source_count * sizeof(uint32_t)), 0);
            }
            if (!expected_changed && layouts[layout_index].from != VIEW_FLOAT) {
                TEST_CHECK(prepared->final_root->split, source_before[0].split);
                TEST_CHECK(prepared->final_root->ratio, source_before[0].ratio);
                TEST_CHECK(memcmp(final_membership, source_wids, count * sizeof(uint32_t)), 0);
            }
            if (reconcile) {
                TEST_CHECK(prepared->final_insertion_point, 0U);
                TEST_CHECK(prepared->final_root->split, source_before[0].split);
                TEST_CHECK(prepared->final_root->ratio,
                           layouts[layout_index].from == VIEW_BSP ? 0.5f : source_before[0].ratio);
                if (layouts[layout_index].from == VIEW_BSP) {
                    struct window_node *zoomed = declarative_space_phase3b_find_wid(prepared->final_root,
                                                                                     source_wids[0]);
                    TEST_CHECK(zoomed->zoom == zoomed->parent || zoomed->zoom == prepared->final_root, true);
                }
                if (view_layout_is_single_root(layouts[layout_index].from)) TEST_CHECK(final_order[0], 500U);
            } else if (final_layout != VIEW_FLOAT && source_count) {
                TEST_CHECK(prepared->final_insertion_point, source_wids[source_count - 1]);
            } else {
                TEST_CHECK(prepared->final_insertion_point, 0U);
            }
            TEST_CHECK(declarative_space_validate_final_state(&preparation), true);
            declarative_space_preparation_destroy(&preparation);
            TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
            table_free(&managed); table_free(&views);
            ++rows_run;
        }
    }

    for (size_t geometry_case = 0; geometry_case < 6; ++geometry_case) {
        bool portrait = geometry_case >= 3;
        enum view_type from = geometry_case % 3 == 0 ? VIEW_BSP :
                              geometry_case % 3 == 1 ? VIEW_STACK : VIEW_FLOAT;
        uint32_t source_wids[2] = { 100, 101 }, capture_wids[2] = { 200, 201 };
        size_t source_count = from == VIEW_BSP ? 1 : from == VIEW_STACK ? 2 : 0;
        struct window_node nodes[3];
        declarative_space_phase3b_matrix_root(nodes, from, source_wids, source_count);
        nodes[0].area = (struct area) { .x = 11, .y = 22,
                                        .w = portrait ? 200 : 300,
                                        .h = portrait ? 300 : 200 };
        uint64_t sid = 1250 + geometry_case;
        struct view view = { .sid = sid, .uuid = CFSTR("phase3b-geometry"), .layout = from,
                             .root = &nodes[0], .split_type = SPLIT_AUTO,
                             .flags = VIEW_ENABLE_GAP };
        struct window_node source_before[3]; memcpy(source_before, nodes, sizeof(nodes));
        struct view view_before = view;
        struct table views = { 0 }, managed = { 0 };
        table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
        table_add(&views, &sid, &view);
        for (size_t i = 0; i < source_count; ++i) table_add(&managed, &source_wids[i], &view);
        struct declarative_space_target target = { &view, sid, 1, "phase3b-geometry", true };
        struct declarative_space_topology_entry topology = { sid, 1, "phase3b-geometry", true };
        struct declarative_space_phase3b_matrix_capture capture = { .sids = { sid },
            .wids = { capture_wids }, .counts = { 2 }, .window_ids = { 200, 201 }, .window_count = 2 };
        struct declarative_space_capture_ops ops = { declarative_space_phase3b_matrix_query,
            declarative_space_phase3b_matrix_find, declarative_space_phase3b_matrix_eligible, &capture };
        struct declarative_space_defaults defaults = declarative_space_test_defaults();
        defaults.layout = VIEW_BSP; defaults.split_type = SPLIT_AUTO; defaults.auto_balance = SPLIT_NONE;
        defaults.window_gap_enabled = true; defaults.window_gap = 10;
        struct declarative_space_preparation preparation;
        TEST_CHECK(strcmp(row_names[rows_run], row_names[48 + geometry_case]), 0);
        TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                             NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                             &ops, &preparation), true);
        struct window_node *final = preparation.targets[0].final_root;
        TEST_CHECK(final->area.x, 11.0f); TEST_CHECK(final->area.y, 22.0f);
        TEST_CHECK(final->area.w, portrait ? 200.0f : 300.0f);
        TEST_CHECK(final->area.h, portrait ? 300.0f : 200.0f);
        TEST_CHECK(final->split, portrait ? SPLIT_X : SPLIT_Y);
        TEST_CHECK(final->left->area.x, 11.0f); TEST_CHECK(final->left->area.y, 22.0f);
        TEST_CHECK(final->left->area.w, portrait ? 200.0f : 145.0f);
        TEST_CHECK(final->left->area.h, portrait ? 145.0f : 200.0f);
        TEST_CHECK(final->right->area.x, portrait ? 11.0f : 166.0f);
        TEST_CHECK(final->right->area.y, portrait ? 177.0f : 22.0f);
        TEST_CHECK(final->right->area.w, portrait ? 200.0f : 145.0f);
        TEST_CHECK(final->right->area.h, portrait ? 145.0f : 200.0f);
        TEST_CHECK(memcmp(nodes, source_before, sizeof(nodes)), 0);
        TEST_CHECK(memcmp(&view, &view_before, sizeof(view)), 0);
        declarative_space_preparation_destroy(&preparation);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        table_free(&managed); table_free(&views); ++rows_run;
    }

    for (size_t gap_case = 0; gap_case < 2; ++gap_case) {
        bool gap_enabled = gap_case != 0;
        uint32_t capture_wids[3] = { 300, 301, 302 };
        struct window_node root = { .area = { .x = 11, .y = 22, .w = 300, .h = 148 } };
        uint64_t sid = 1280 + gap_case;
        struct view view = { .sid = sid, .uuid = CFSTR("phase3b-auto-gap"), .layout = VIEW_FLOAT,
                             .root = &root, .split_type = SPLIT_AUTO,
                             .flags = gap_enabled ? VIEW_ENABLE_GAP : 0 };
        struct window_node root_before = root;
        struct view view_before = view;
        struct table views = { 0 }, managed = { 0 };
        table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
        table_add(&views, &sid, &view);
        struct declarative_space_target target = { &view, sid, 1, "phase3b-auto-gap", true };
        struct declarative_space_topology_entry topology = { sid, 1, "phase3b-auto-gap", true };
        struct declarative_space_phase3b_matrix_capture capture = { .sids = { sid },
            .wids = { capture_wids }, .counts = { 3 },
            .window_ids = { 300, 301, 302 }, .window_count = 3 };
        struct declarative_space_capture_ops ops = { declarative_space_phase3b_matrix_query,
            declarative_space_phase3b_matrix_find, declarative_space_phase3b_matrix_eligible, &capture };
        struct declarative_space_defaults defaults = declarative_space_test_defaults();
        defaults.layout = VIEW_BSP; defaults.split_type = SPLIT_AUTO; defaults.auto_balance = SPLIT_NONE;
        defaults.window_gap_enabled = true; defaults.window_gap = 10;
        struct declarative_space_preparation preparation;
        TEST_CHECK(strcmp(row_names[rows_run], row_names[54 + gap_case]), 0);
        TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                             NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                             &ops, &preparation), true);
        struct window_node *final = preparation.targets[0].final_root;
        TEST_CHECK(preparation.targets[0].effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer, 10);
        TEST_CHECK(final->split, SPLIT_Y);
        TEST_CHECK(final->left->area.w, gap_enabled ? 145.0f : 150.0f);
        TEST_CHECK(final->right->area.w, gap_enabled ? 145.0f : 150.0f);
        TEST_CHECK(final->left->split, gap_enabled ? SPLIT_X : SPLIT_Y);
        TEST_CHECK(memcmp(&root, &root_before, sizeof(root)), 0);
        TEST_CHECK(memcmp(&view, &view_before, sizeof(view)), 0);
        declarative_space_preparation_destroy(&preparation);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        table_free(&managed); table_free(&views); ++rows_run;
    }

    for (size_t over = 0; over < 3; ++over) {
        uint32_t wids[NODE_MAX_WINDOW_COUNT + 1];
        for (size_t i = 0; i < NODE_MAX_WINDOW_COUNT + 1; ++i) wids[i] = 600 + (uint32_t) i;
        enum view_type from = over == 0 ? VIEW_STACK : over == 1 ? VIEW_BSP : VIEW_FLOAT;
        struct window_node nodes[3];
        if (over == 0) {
            memset(nodes, 0, sizeof(nodes));
            nodes[0].window_count = NODE_MAX_WINDOW_COUNT + 1;
        } else {
            declarative_space_phase3b_matrix_root(nodes, from, wids,
                                                   from == VIEW_FLOAT ? 0 : NODE_MAX_WINDOW_COUNT + 1);
        }
        uint64_t sid = 1300 + over;
        struct view view = { .sid = sid, .uuid = CFSTR("phase3b-matrix"), .layout = from, .root = &nodes[0] };
        struct table views = { 0 }, managed = { 0 };
        table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 64, hash_wm, compare_wm);
        table_add(&views, &sid, &view);
        if (over != 0) for (size_t i = 0; from != VIEW_FLOAT && i < NODE_MAX_WINDOW_COUNT + 1; ++i) table_add(&managed, &wids[i], &view);
        struct declarative_space_target target = { &view, sid, 1, "phase3b-matrix", true };
        struct declarative_space_topology_entry topology = { sid, 1, "phase3b-matrix", true };
        struct declarative_space_phase3b_matrix_capture capture = { .sids = { sid }, .wids = { wids },
            .counts = { NODE_MAX_WINDOW_COUNT + 1 } };
        for (size_t i = 0; i < NODE_MAX_WINDOW_COUNT + 1; ++i) capture.window_ids[capture.window_count++] = wids[i];
        struct declarative_space_capture_ops ops = { declarative_space_phase3b_matrix_query,
            declarative_space_phase3b_matrix_find, declarative_space_phase3b_matrix_eligible, &capture };
        struct declarative_space_defaults defaults = declarative_space_test_defaults(); defaults.layout = VIEW_STACK;
        struct declarative_space_preparation preparation;
        TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
                                             NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                                             &ops, &preparation), false);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        table_free(&managed); table_free(&views); ++rows_run;
    }

    for (size_t invalid = 0; invalid < 8; ++invalid) {
        uint32_t a_wids[2] = { 800, 801 }, b_wids[1] = { 802 };
        struct window_node roots[2][3];
        declarative_space_phase3b_matrix_root(roots[0], VIEW_STACK, a_wids, invalid == 6 ? 2 : 1);
        declarative_space_phase3b_matrix_root(roots[1], VIEW_STACK, b_wids, 1);
        if (invalid == 6) roots[0][0].window_order[1] = roots[0][0].window_order[0];
        uint64_t sids[2] = { 1400 + invalid * 2, 1401 + invalid * 2 };
        struct view live[2] = {
            { .sid = sids[0], .uuid = CFSTR("phase3b-invalid-a"), .layout = VIEW_STACK, .root = &roots[0][0] },
            { .sid = sids[1], .uuid = CFSTR("phase3b-invalid-b"), .layout = VIEW_STACK, .root = &roots[1][0] }
        };
        size_t target_count = invalid == 2 ? 2 : 1;
        bool non_user = invalid == 0;
        struct declarative_space_target targets[2] = {
            { &live[0], sids[0], 1, "phase3b-invalid-a", !non_user },
            { &live[1], sids[1], 2, "phase3b-invalid-b", true }
        };
        struct declarative_space_topology_entry topology[2] = {
            { sids[0], 1, "phase3b-invalid-a", !non_user },
            { sids[1], 2, "phase3b-invalid-b", true }
        };
        struct table views = { 0 }, managed = { 0 };
        table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
        for (size_t i = 0; i < target_count; ++i) table_add(&views, &sids[i], &live[i]);
        table_add(&managed, &a_wids[0], &live[0]);
        if (invalid == 6) table_add(&managed, &a_wids[1], &live[0]);
        if (target_count == 2) table_add(&managed, &b_wids[0], &live[1]);
        struct declarative_space_phase3b_matrix_capture capture = { .sids = { sids[0], sids[1] },
            .wids = { a_wids, b_wids }, .counts = { 1, target_count == 2 ? 1 : 0 } };
        capture.window_ids[capture.window_count++] = a_wids[0];
        capture.window_ids[capture.window_count++] = a_wids[1];
        capture.window_ids[capture.window_count++] = b_wids[0];
        if (invalid == 1) capture.sids[0] = sids[0] + 999;
        if (invalid == 2) { b_wids[0] = a_wids[0]; capture.wids[1] = b_wids; }
        if (invalid == 3) capture.missing_wid = a_wids[0];
        if (invalid == 4) capture.ineligible_wid = a_wids[0];
        if (invalid == 5) capture.mismatch_wid = a_wids[0];
        if (invalid == 7) { capture.counts[0] = 2; a_wids[1] = a_wids[0]; }
        struct declarative_space_capture_ops ops = { declarative_space_phase3b_matrix_query,
            declarative_space_phase3b_matrix_find, declarative_space_phase3b_matrix_eligible, &capture };
        struct declarative_space_defaults defaults = declarative_space_test_defaults(); defaults.layout = VIEW_STACK;
        struct declarative_space_preparation preparation;
        bool expected_success = invalid == 0 || invalid == 4;
        TEST_CHECK(declarative_space_prepare(targets, target_count, topology, target_count, &views, &vc,
                                             &managed, &mc, NULL, NULL, &defaults,
                                             DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preparation), expected_success);
        if (expected_success) {
            TEST_CHECK(preparation.targets[0].changed, true);
            TEST_CHECK(preparation.targets[0].effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer,
                       invalid == 0 ? VIEW_FLOAT : VIEW_STACK);
            TEST_CHECK(preparation.targets[0].final_count, 0UL);
            TEST_CHECK(preparation.final_owner_count, 0UL);
            TEST_CHECK(preparation.removal_count, 1UL);
            declarative_space_preparation_destroy(&preparation);
        }
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        table_free(&managed); table_free(&views); ++rows_run;
    }
    TEST_CHECK(rows_run, sizeof(row_names) / sizeof(row_names[0]));
})

struct declarative_space_phase3b_e_case
{
    const char *name;
    enum view_type from[2];
    enum view_type to[2];
    size_t target_count;
    size_t count[2];
    bool replace_last;
    bool cross_move;
    bool collision;
    bool geometry;
    bool portrait;
    bool auto_descendant;
    bool gap_disabled;
};

struct declarative_space_phase3b_e_fixture
{
    struct window_node nodes[2][3];
    struct window_node nodes_before[2][3];
    struct view views[2];
    struct view views_before[2];
    struct declarative_space_target targets[2];
    struct declarative_space_topology_entry topology[2];
    struct table view_table;
    struct table managed;
    struct table view_table_before;
    struct table managed_before;
    struct declarative_space_phase3b_matrix_capture capture;
    struct declarative_space_capture_ops ops;
    struct declarative_runtime_state runtime;
    struct declarative_runtime_state runtime_before;
    struct declarative_space_root_before_image root_images_before[2];
    struct declarative_space_table_before_image table_image_before;
    struct window_manager window_manager_before;
    struct mouse_state mouse_before;
    struct declarative_space_defaults defaults;
    uint32_t source_wids[2][NODE_MAX_WINDOW_COUNT];
    uint32_t capture_wids[2][NODE_MAX_WINDOW_COUNT];
};

struct declarative_space_phase3b_e_result
{
    size_t allocation_count;
    size_t final_count[2];
    uint32_t membership[2][NODE_MAX_WINDOW_COUNT];
    uint32_t order[2][NODE_MAX_WINDOW_COUNT];
};

static void declarative_space_phase3b_e_fixture_init(struct declarative_space_phase3b_e_fixture *fixture,
                                                       const struct declarative_space_phase3b_e_case *scenario)
{
    memset(fixture, 0, sizeof(*fixture));
    table_init(&fixture->view_table, 8, hash_wm, compare_wm);
    table_init(&fixture->managed, 128, hash_wm, compare_wm);
    fixture->defaults = declarative_space_test_defaults();
    fixture->defaults.layout = scenario->to[0];
    fixture->runtime.global[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_runtime_cell) {
        .present = true, .value.integer = 23
    };
    for (size_t target = 0; target < scenario->target_count; ++target) {
        uint64_t sid = 2000 + target;
        for (size_t i = 0; i < scenario->count[target]; ++i) {
            uint32_t wid = scenario->collision ? (uint32_t) (1 + i * 128) :
                           (uint32_t) (1000 + target * 100 + i);
            fixture->source_wids[target][i] = wid;
            fixture->capture_wids[target][i] = wid;
        }
        if (scenario->replace_last && scenario->count[target]) {
            fixture->capture_wids[target][scenario->count[target] - 1] = (uint32_t) (5000 + target);
        }
        size_t source_count = scenario->geometry
                            ? scenario->from[target] == VIEW_BSP ? 1 :
                              scenario->from[target] == VIEW_STACK ? 2 : 0
                            : scenario->from[target] == VIEW_FLOAT ? 0 : scenario->count[target];
        if (scenario->geometry) {
            for (size_t i = 0; i < scenario->count[target]; ++i) {
                fixture->capture_wids[target][i] = (uint32_t) (5000 + target * 100 + i);
            }
        }
        declarative_space_phase3b_matrix_root(fixture->nodes[target], scenario->from[target],
                                               fixture->source_wids[target], source_count);
        if (scenario->geometry) {
            fixture->nodes[target][0].area = (struct area) { .x = 11, .y = 22,
                .w = scenario->portrait ? 200 : 300,
                .h = scenario->portrait ? 300 : scenario->auto_descendant ? 148 : 200 };
        }
        fixture->views[target] = (struct view) {
            .sid = sid, .uuid = target ? CFSTR("phase3b-e-b") : CFSTR("phase3b-e-a"),
            .layout = scenario->from[target], .root = &fixture->nodes[target][0],
            .split_type = SPLIT_Y,
            .insertion_point = source_count ? fixture->source_wids[target][source_count - 1] : 0,
            .flags = scenario->gap_disabled ? 0 : VIEW_ENABLE_GAP
        };
        fixture->views[target].runtime_ownership[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP] =
            (struct declarative_runtime_cell) { .present = true,
                .value.integer = scenario->geometry ? 10 : 23 };
        fixture->targets[target] = (struct declarative_space_target) {
            &fixture->views[target], sid, (int) target + 1,
            target ? "phase3b-e-b" : "phase3b-e-a", true
        };
        fixture->topology[target] = (struct declarative_space_topology_entry) {
            sid, (int) target + 1, target ? "phase3b-e-b" : "phase3b-e-a", true
        };
        table_add(&fixture->view_table, &fixture->views[target].sid, &fixture->views[target]);
        if (scenario->from[target] != VIEW_FLOAT) {
            for (size_t i = 0; i < source_count; ++i) {
                table_add(&fixture->managed, &fixture->source_wids[target][i], &fixture->views[target]);
            }
        }
        fixture->capture.sids[target] = sid;
        fixture->capture.wids[target] = fixture->capture_wids[target];
        fixture->capture.counts[target] = scenario->count[target];
    }
    if (scenario->cross_move) {
        fixture->capture_wids[0][0] = fixture->source_wids[1][0];
        fixture->capture_wids[1][0] = fixture->source_wids[0][0];
    }
    for (size_t target = 0; target < scenario->target_count; ++target) {
        for (size_t i = 0; i < scenario->count[target]; ++i) {
            fixture->capture.window_ids[fixture->capture.window_count++] = fixture->capture_wids[target][i];
        }
    }
    fixture->ops = (struct declarative_space_capture_ops) {
        declarative_space_phase3b_matrix_query, declarative_space_phase3b_matrix_find,
        declarative_space_phase3b_matrix_eligible, &fixture->capture
    };
    if (scenario->geometry) {
        fixture->defaults.split_type = SPLIT_AUTO;
        fixture->defaults.auto_balance = SPLIT_NONE;
        fixture->defaults.window_gap_enabled = true;
        fixture->defaults.window_gap = 10;
    }
    memcpy(fixture->nodes_before, fixture->nodes, sizeof(fixture->nodes));
    memcpy(fixture->views_before, fixture->views, sizeof(fixture->views));
    fixture->view_table_before = fixture->view_table;
    fixture->managed_before = fixture->managed;
    fixture->runtime_before = fixture->runtime;
    fixture->window_manager_before = g_window_manager;
    fixture->mouse_before = g_mouse_state;
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    (void) declarative_space_validate_forest_and_table(fixture->targets, scenario->target_count,
                                                        &fixture->managed, &mc,
                                                        fixture->root_images_before,
                                                        &fixture->table_image_before);
}

static void declarative_space_phase3b_e_fixture_destroy(struct declarative_space_phase3b_e_fixture *fixture)
{
    table_free(&fixture->managed);
    table_free(&fixture->view_table);
}

static bool declarative_space_phase3b_e_source_unchanged(struct declarative_space_phase3b_e_fixture *fixture,
                                                          size_t target_count)
{
    if (memcmp(fixture->nodes, fixture->nodes_before, sizeof(fixture->nodes)) != 0 ||
        memcmp(fixture->views, fixture->views_before, sizeof(fixture->views)) != 0 ||
        memcmp(&fixture->view_table, &fixture->view_table_before, sizeof(fixture->view_table)) != 0 ||
        memcmp(&fixture->managed, &fixture->managed_before, sizeof(fixture->managed)) != 0 ||
        memcmp(&fixture->runtime, &fixture->runtime_before, sizeof(fixture->runtime)) != 0 ||
        memcmp(&g_window_manager, &fixture->window_manager_before, sizeof(g_window_manager)) != 0 ||
        memcmp(&g_mouse_state, &fixture->mouse_before, sizeof(g_mouse_state)) != 0) return false;
    struct declarative_space_root_before_image roots[2];
    struct declarative_space_table_before_image table_image;
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    if (!declarative_space_validate_forest_and_table(fixture->targets, target_count, &fixture->managed,
                                                      &mc, roots, &table_image) ||
        table_image.buckets != fixture->table_image_before.buckets ||
        table_image.capacity != fixture->table_image_before.capacity ||
        table_image.count != fixture->table_image_before.count ||
        table_image.max_load != fixture->table_image_before.max_load ||
        table_image.hash != fixture->table_image_before.hash ||
        table_image.cmp != fixture->table_image_before.cmp ||
        table_image.fingerprint != fixture->table_image_before.fingerprint) return false;
    for (size_t i = 0; i < target_count; ++i) {
        if (roots[i].root != fixture->root_images_before[i].root ||
            roots[i].fingerprint != fixture->root_images_before[i].fingerprint ||
            roots[i].node_count != fixture->root_images_before[i].node_count ||
            roots[i].window_count != fixture->root_images_before[i].window_count) return false;
    }
    return true;
}

static bool declarative_space_phase3b_e_capture_result(const struct declarative_space_preparation *preparation,
                                                        struct declarative_space_phase3b_e_result *result)
{
    for (size_t i = 0; i < preparation->target_count; ++i) {
        struct view validation = *preparation->targets[i].target.view;
        validation.layout = declarative_space_final_layout(&preparation->targets[i]);
        validation.root = preparation->targets[i].final_root;
        validation.insertion_point = preparation->targets[i].final_insertion_point;
        struct declarative_space_root_before_image image;
        if (!declarative_space_validate_root(&validation, &image) || image.window_count > NODE_MAX_WINDOW_COUNT) return false;
        result->final_count[i] = image.window_count;
        size_t count = 0;
        declarative_space_phase3b_matrix_flatten(validation.root, result->membership[i], result->order[i], &count);
        if (count != image.window_count) return false;
    }
    if (!declarative_space_validate_final_state(preparation)) return false;
    for (size_t i = 0; i < preparation->final_owner_count; ++i) {
        const struct declarative_space_final_owner *owner = &preparation->final_owners[i];
        if (table_find((struct table *) &preparation->replacement_managed_window, (void *) &owner->wid) != owner->view) return false;
    }
    return true;
}

TEST_FUNC(declarative_space_phase3b_allocation_ordinal_matrix_e,
{
    static const struct declarative_space_phase3b_e_case scenarios[] = {
        { "same-bsp", { VIEW_BSP }, { VIEW_BSP }, 1, { 3 } },
        { "same-bsp-collapse", { VIEW_BSP }, { VIEW_BSP }, 1, { 2 }, true },
        { "same-stack", { VIEW_STACK }, { VIEW_STACK }, 1, { 3 } },
        { "bsp-to-stack", { VIEW_BSP }, { VIEW_STACK }, 1, { 3 } },
        { "stack-to-bsp", { VIEW_STACK }, { VIEW_BSP }, 1, { 3 } },
        { "bsp-to-float", { VIEW_BSP }, { VIEW_FLOAT }, 1, { 3 } },
        { "stack-to-float", { VIEW_STACK }, { VIEW_FLOAT }, 1, { 3 } },
        { "float-to-bsp", { VIEW_FLOAT }, { VIEW_BSP }, 1, { 3 } },
        { "float-to-stack", { VIEW_FLOAT }, { VIEW_STACK }, 1, { 3 } },
        { "same-accordion-horizontal", { VIEW_ACCORDION_HORIZONTAL }, { VIEW_ACCORDION_HORIZONTAL }, 1, { 3 } },
        { "same-accordion-vertical", { VIEW_ACCORDION_VERTICAL }, { VIEW_ACCORDION_VERTICAL }, 1, { 3 } },
        { "bsp-to-accordion-horizontal", { VIEW_BSP }, { VIEW_ACCORDION_HORIZONTAL }, 1, { 3 } },
        { "stack-to-accordion-vertical", { VIEW_STACK }, { VIEW_ACCORDION_VERTICAL }, 1, { 3 } },
        { "float-to-accordion-horizontal", { VIEW_FLOAT }, { VIEW_ACCORDION_HORIZONTAL }, 1, { 3 } },
        { "accordion-horizontal-to-bsp", { VIEW_ACCORDION_HORIZONTAL }, { VIEW_BSP }, 1, { 3 } },
        { "accordion-vertical-to-stack", { VIEW_ACCORDION_VERTICAL }, { VIEW_STACK }, 1, { 3 } },
        { "accordion-horizontal-to-vertical", { VIEW_ACCORDION_HORIZONTAL }, { VIEW_ACCORDION_VERTICAL }, 1, { 3 } },
        { "root-replace-landscape", { VIEW_BSP }, { VIEW_BSP }, 1, { 2 }, false, false, false, true, false },
        { "stack-to-bsp-landscape", { VIEW_STACK }, { VIEW_BSP }, 1, { 2 }, false, false, false, true, false },
        { "float-to-bsp-landscape", { VIEW_FLOAT }, { VIEW_BSP }, 1, { 2 }, false, false, false, true, false },
        { "root-replace-portrait", { VIEW_BSP }, { VIEW_BSP }, 1, { 2 }, false, false, false, true, true },
        { "stack-to-bsp-portrait", { VIEW_STACK }, { VIEW_BSP }, 1, { 2 }, false, false, false, true, true },
        { "float-to-bsp-portrait", { VIEW_FLOAT }, { VIEW_BSP }, 1, { 2 }, false, false, false, true, true },
        { "auto-gap-disabled", { VIEW_FLOAT }, { VIEW_BSP }, 1, { 3 }, false, false, false, true, false, true, true },
        { "auto-gap-enabled", { VIEW_FLOAT }, { VIEW_BSP }, 1, { 3 }, false, false, false, true, false, true, false },
        { "empty", { VIEW_BSP }, { VIEW_BSP }, 1, { 0 } },
        { "node-max-bsp", { VIEW_BSP }, { VIEW_BSP }, 1, { NODE_MAX_WINDOW_COUNT }, true },
        { "node-max-stack", { VIEW_STACK }, { VIEW_STACK }, 1, { NODE_MAX_WINDOW_COUNT }, true },
        { "collision-replacement-table", { VIEW_STACK }, { VIEW_STACK }, 1, { 6 }, false, false, true },
        { "multi-view-cross-move", { VIEW_STACK, VIEW_STACK }, { VIEW_STACK, VIEW_STACK }, 2, { 1, 1 }, false, true }
    };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    /* Matrix E allocation ledger (scenario order above): the pre-accordion
     * baseline is 874 exact failure ordinals. Eight accordion transition rows
     * add 274 measured ordinals for an exact total of 1148. */
    static const size_t expected_ordinals[] = {
        30, 39, 30, 36, 41, 25, 25, 34, 29,
        30, 30, 36, 36, 29, 41, 36, 36,
        37, 37, 30, 37, 37, 30, 34, 34, 12, 109, 104, 36, 48
    };
    size_t aggregate_failures = 0, aggregate_successes = 0;
    TEST_CHECK(sizeof(expected_ordinals) / sizeof(expected_ordinals[0]),
               sizeof(scenarios) / sizeof(scenarios[0]));

    for (size_t case_index = 0; case_index < sizeof(scenarios) / sizeof(scenarios[0]); ++case_index) {
        const struct declarative_space_phase3b_e_case *scenario = &scenarios[case_index];
        struct declarative_space_phase3b_e_fixture fixture;
        struct declarative_space_preparation preparation;
        struct declarative_space_phase3b_e_result baseline = { 0 }, terminal = { 0 };
        declarative_space_phase3b_e_fixture_init(&fixture, scenario);
        declarative_space_test_fail_allocation_at(0);
        bool success = declarative_space_prepare(fixture.targets, scenario->target_count,
            fixture.topology, scenario->target_count, &fixture.view_table, &vc, &fixture.managed, &mc,
            NULL, &fixture.runtime, &fixture.defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
            &fixture.ops, &preparation);
        TEST_CHECK(success, true);
        baseline.allocation_count = declarative_space_test_allocation_count();
        TEST_CHECK(baseline.allocation_count, expected_ordinals[case_index]);
        TEST_CHECK(declarative_space_phase3b_e_capture_result(&preparation, &baseline), true);
        if (scenario->geometry) {
            struct window_node *final = preparation.targets[0].final_root;
            float child_extent = scenario->gap_disabled ? 150.0f : 145.0f;
            TEST_CHECK(final->area.x, 11.0f); TEST_CHECK(final->area.y, 22.0f);
            TEST_CHECK(final->area.w, scenario->portrait ? 200.0f : 300.0f);
            TEST_CHECK(final->area.h, scenario->portrait ? 300.0f : scenario->auto_descendant ? 148.0f : 200.0f);
            TEST_CHECK(final->split, scenario->portrait ? SPLIT_X : SPLIT_Y);
            TEST_CHECK(final->left->area.w, scenario->portrait ? 200.0f : child_extent);
            TEST_CHECK(final->left->area.h, scenario->portrait ? 145.0f : scenario->auto_descendant ? 148.0f : 200.0f);
            TEST_CHECK(final->right->area.x, scenario->portrait ? 11.0f :
                       scenario->gap_disabled ? 161.0f : 166.0f);
            TEST_CHECK(final->right->area.y, scenario->portrait ? 177.0f : 22.0f);
            if (scenario->auto_descendant) {
                TEST_CHECK(final->left->split, scenario->gap_disabled ? SPLIT_Y : SPLIT_X);
            }
        }
        TEST_CHECK(declarative_space_phase3b_e_source_unchanged(&fixture, scenario->target_count), true);
        declarative_space_preparation_destroy(&preparation);
        declarative_space_preparation_destroy(&preparation);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        declarative_space_phase3b_e_fixture_destroy(&fixture);
        ++aggregate_successes;

        for (size_t ordinal = 1; ordinal <= baseline.allocation_count; ++ordinal) {
            declarative_space_phase3b_e_fixture_init(&fixture, scenario);
            declarative_space_test_fail_allocation_at(ordinal);
            success = declarative_space_prepare(fixture.targets, scenario->target_count,
                fixture.topology, scenario->target_count, &fixture.view_table, &vc, &fixture.managed, &mc,
                NULL, &fixture.runtime, &fixture.defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                &fixture.ops, &preparation);
            TEST_CHECK(success, false);
            TEST_CHECK(preparation.prepared, false);
            TEST_CHECK(preparation.targets == NULL, true);
            TEST_CHECK(preparation.target_count, 0UL);
            declarative_space_preparation_destroy(&preparation);
            declarative_space_preparation_destroy(&preparation);
            declarative_space_test_fail_allocation_at(0);
            TEST_CHECK(declarative_space_phase3b_e_source_unchanged(&fixture, scenario->target_count), true);
            TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
            declarative_space_phase3b_e_fixture_destroy(&fixture);
            ++aggregate_failures;
        }

        declarative_space_phase3b_e_fixture_init(&fixture, scenario);
        declarative_space_test_fail_allocation_at(baseline.allocation_count + 1);
        success = declarative_space_prepare(fixture.targets, scenario->target_count,
            fixture.topology, scenario->target_count, &fixture.view_table, &vc, &fixture.managed, &mc,
            NULL, &fixture.runtime, &fixture.defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME,
            &fixture.ops, &preparation);
        TEST_CHECK(success, true);
        TEST_CHECK(declarative_space_test_allocation_count(), baseline.allocation_count);
        declarative_space_test_fail_allocation_at(0);
        TEST_CHECK(declarative_space_phase3b_e_capture_result(&preparation, &terminal), true);
        if (scenario->geometry) {
            struct window_node *final = preparation.targets[0].final_root;
            TEST_CHECK(final->split, scenario->portrait ? SPLIT_X : SPLIT_Y);
            TEST_CHECK(final->area.w, scenario->portrait ? 200.0f : 300.0f);
            TEST_CHECK(final->area.h, scenario->portrait ? 300.0f : scenario->auto_descendant ? 148.0f : 200.0f);
            if (scenario->auto_descendant) {
                TEST_CHECK(final->left->split, scenario->gap_disabled ? SPLIT_Y : SPLIT_X);
            }
        }
        TEST_CHECK(memcmp(terminal.final_count, baseline.final_count, sizeof(baseline.final_count)), 0);
        TEST_CHECK(memcmp(terminal.membership, baseline.membership, sizeof(baseline.membership)), 0);
        TEST_CHECK(memcmp(terminal.order, baseline.order, sizeof(baseline.order)), 0);
        TEST_CHECK(declarative_space_phase3b_e_source_unchanged(&fixture, scenario->target_count), true);
        declarative_space_preparation_destroy(&preparation);
        declarative_space_preparation_destroy(&preparation);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        declarative_space_phase3b_e_fixture_destroy(&fixture);
        ++aggregate_successes;
        printf("                   matrix-e %-27s ordinals 1..%zu fail, %zu succeeds\n",
               scenario->name, baseline.allocation_count, baseline.allocation_count + 1);
    }
    declarative_space_test_fail_allocation_at(0);
    TEST_CHECK(aggregate_successes, 60UL);
    TEST_CHECK(aggregate_failures, 1148UL);
    printf("                   matrix-e aggregate failures=%zu successes=%zu\n",
           aggregate_failures, aggregate_successes);
})
