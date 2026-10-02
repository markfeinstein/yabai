static struct declarative_space_defaults declarative_space_test_defaults(void)
{
    return (struct declarative_space_defaults) {
        .layout = VIEW_BSP, .top_padding = 1, .bottom_padding = 2,
        .left_padding = 3, .right_padding = 4, .window_gap = 5,
        .split_type = SPLIT_AUTO, .auto_balance = SPLIT_X | SPLIT_Y,
        .split_ratio = 0.5f, .window_placement = CHILD_SECOND,
        .window_insertion_point = INSERT_FOCUSED, .window_zoom_persist = true
    };
}

TEST_FUNC(declarative_space_resolver_is_fieldwise_and_mode_explicit,
{
    struct view view = { .sid = 91, .layout = VIEW_STACK, .top_padding = 70 };
    struct declarative_space_target target = { .view = &view, .sid = 91, .index = 2, .uuid = "space-a", .is_user_space = true };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_space_policy policies[3] = { 0 };
    policies[0].by_index = true;
    policies[0].index = 2;
    policies[0].settings[DECLARATIVE_CONFIG_TOP_PADDING] = (struct declarative_value) { .present = true, .integer = 20 };
    policies[0].settings[DECLARATIVE_CONFIG_LEFT_PADDING] = (struct declarative_value) { .present = true, .integer = 21 };
    policies[1].uuid = "space-a";
    policies[1].settings[DECLARATIVE_CONFIG_TOP_PADDING] = (struct declarative_value) { .present = true, .integer = 30 };
    policies[1].settings[DECLARATIVE_CONFIG_LAYOUT] = (struct declarative_value) { .present = true, .enumeration = VIEW_STACK };
    policies[2].uuid = "not-a-match";
    policies[2].settings[DECLARATIVE_CONFIG_LAYOUT] = (struct declarative_value) { .present = true, .enumeration = VIEW_FLOAT };
    struct declarative_config_model native = { .spaces = policies, .space_count = 3 };
    native.config[DECLARATIVE_CONFIG_TOP_PADDING] = (struct declarative_value) { .present = true, .integer = 10 };
    native.config[DECLARATIVE_CONFIG_BOTTOM_PADDING] = (struct declarative_value) { .present = true, .integer = 11 };
    struct declarative_runtime_state runtime = { 0 };
    runtime.global[DECLARATIVE_CONFIG_TOP_PADDING] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_GLOBAL, .value.integer = 40
    };
    runtime.global[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_GLOBAL, .value.integer = 41
    };
    view.runtime_ownership[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_SPACE, .value.integer = 50
    };
    struct declarative_space_effective effective;
    TEST_CHECK(declarative_space_resolve_policy(&native, &runtime, &defaults, &target, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].value.integer, 50);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].source, DECLARATIVE_SETTING_RUNTIME_SPACE);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING].value.integer, 11);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING].value.integer, 21);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer, 41);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer, VIEW_STACK);
    TEST_CHECK(declarative_space_resolve_policy(&native, &runtime, &defaults, &target, DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].value.integer, 30);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].source, DECLARATIVE_SETTING_NATIVE_SPACE);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer, 5);
})

TEST_FUNC(declarative_space_resolver_retains_view_ownership_and_preserves_non_user_scalars,
{
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_runtime_state runtime = { 0 };
    struct view view = { .sid = 777, .layout = VIEW_BSP, .top_padding = 81, .bottom_padding = 82, .left_padding = 83,
                         .right_padding = 84, .window_gap = 85, .split_type = SPLIT_X, .auto_balance = SPLIT_Y };
    view.runtime_ownership[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_SPACE, .value.integer = SPLIT_Y
    };
    struct declarative_space_target target = { .view = &view, .sid = 777, .index = 1, .uuid = "remapped", .is_user_space = true };
    struct declarative_space_effective effective;
    TEST_CHECK(declarative_space_resolve_policy(NULL, &runtime, &defaults, &target, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].value.integer, SPLIT_Y);
    target.is_user_space = false;
    TEST_CHECK(declarative_space_resolve_policy(NULL, &runtime, &defaults, &target, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer, VIEW_FLOAT);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].value.integer, 81);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer, 85);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].value.integer, SPLIT_X);
})

struct declarative_space_test_capture
{
    uint64_t sids[4];
    uint32_t wids[4][4];
    const uint32_t *dynamic_wids[4];
    size_t counts[4];
    int queries[4];
    int finds;
    int eligibility;
    struct window windows[4096];
    bool mismatch_window_id;
    uint32_t missing_window_id;
};

static bool declarative_space_test_query(uint64_t sid, const uint32_t **wids, size_t *count, void *context)
{
    struct declarative_space_test_capture *capture = context;
    for (int i = 0; i < 4; ++i) if (capture->sids[i] == sid) {
        ++capture->queries[i];
        *wids = capture->dynamic_wids[i] ? capture->dynamic_wids[i] : capture->wids[i];
        *count = capture->counts[i];
        return true;
    }
    return false;
}

static struct window *declarative_space_test_find(uint32_t wid, void *context)
{
    struct declarative_space_test_capture *capture = context;
    ++capture->finds;
    if (wid == capture->missing_window_id || wid >= 4096) return NULL;
    capture->windows[wid].id = capture->mismatch_window_id ? wid + 1 : wid;
    return &capture->windows[wid];
}

static bool declarative_space_test_eligible(struct window *window, void *context)
{
    struct declarative_space_test_capture *capture = context;
    ++capture->eligibility;
    return window->id != 7;
}

TEST_FUNC(declarative_space_capture_is_per_sid_once_owned_and_rejects_duplicates,
{
    struct view views[2] = { 0 };
    struct declarative_space_target targets[2] = {
        { .view = &views[0], .sid = 1, .index = 1, .uuid = "a", .is_user_space = true },
        { .view = &views[1], .sid = 2, .index = 2, .uuid = "b", .is_user_space = true }
    };
    struct declarative_space_test_capture capture = { .sids = { 1, 2 }, .wids = { { 5, 7, 99 }, { 8 } },
                                                      .counts = { 3, 1 }, .missing_window_id = 99 };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find, declarative_space_test_eligible, &capture };
    struct declarative_space_inventory inventory;
    TEST_CHECK(declarative_space_capture_inventory(targets, 2, &ops, &inventory), false);
    TEST_CHECK(capture.queries[0], 1);
    TEST_CHECK(capture.queries[1], 0);
    TEST_CHECK(capture.finds, 3);
    TEST_CHECK(capture.eligibility, 2);
    TEST_CHECK(inventory.entries == NULL, true);
    capture.wids[0][2] = 6;
    TEST_CHECK(declarative_space_capture_inventory(targets, 2, &ops, &inventory), true);
    TEST_CHECK(inventory.count, 4UL);
    TEST_CHECK(inventory.entries[1].eligible, false);
    declarative_space_inventory_destroy(&inventory);
    capture.wids[1][0] = 5;
    TEST_CHECK(declarative_space_capture_inventory(targets, 2, &ops, &inventory), false);
    TEST_CHECK(inventory.entries == NULL, true);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})

static void declarative_space_test_init_leaf(struct window_node *node, uint32_t a, uint32_t b)
{
    memset(node, 0, sizeof(*node));
    if (a) { node->window_list[node->window_count] = a; node->window_order[node->window_count++] = a; }
    if (b) { node->window_list[node->window_count] = b; node->window_order[node->window_count++] = b; }
}

TEST_FUNC(declarative_space_root_validation_rejects_shape_order_parent_and_zoom_corruption,
{
    struct view view = { .layout = VIEW_BSP };
    struct window_node root, left, right, foreign;
    declarative_space_test_init_leaf(&left, 1, 2);
    declarative_space_test_init_leaf(&right, 3, 0);
    memset(&root, 0, sizeof(root));
    root.left = &left; root.right = &right; root.split = SPLIT_X; left.parent = &root; right.parent = &root;
    view.root = &root;
    struct declarative_space_root_before_image image, changed;
    TEST_CHECK(declarative_space_validate_root(&view, &image), true);
    root.area.x = 12.0f;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), true);
    TEST_CHECK(changed.fingerprint == image.fingerprint, false);
    root.area.x = 0;
    left.window_order[1] = 1;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    left.window_order[1] = 2;
    right.parent = NULL;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    right.parent = &root;
    root.right = NULL;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    root.right = &right;
    root.window_count = 1;
    root.window_list[0] = root.window_order[0] = 9;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    root.window_count = 0;
    left.window_count = 0;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    declarative_space_test_init_leaf(&left, 1, 2);
    left.parent = &root;
    right.window_list[0] = right.window_order[0] = 2;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    right.window_list[0] = right.window_order[0] = 3;
    left.zoom = &foreign;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    left.zoom = &root;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), true);
    view.layout = VIEW_STACK;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    view.layout = VIEW_FLOAT;
    struct window_node empty = { 0 };
    view.root = &empty;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), true);
    empty.window_count = NODE_MAX_WINDOW_COUNT + 1;
    TEST_CHECK(declarative_space_validate_root(&view, &changed), false);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})

TEST_FUNC(declarative_space_root_validation_accepts_retained_split_child_and_rejects_invalid_domain,
{
    struct window_node root;
    declarative_space_test_init_leaf(&root, 1, 0);
    root.split = SPLIT_X;
    root.child = CHILD_FIRST;
    root.insert_dir = DIR_NORTH;
    root.ratio = 0.5f;
    root.area = (struct area) { .w = 100.0f, .h = 100.0f };
    struct view view = { .layout = VIEW_BSP, .root = &root };
    struct window inserted = { .id = 2 };

    /* The insertion path consumes insert_dir before splitting but retains child. */
    root.insert_dir = 0;
    window_node_split(&view, &root, &inserted);

    struct declarative_space_root_before_image image;
    TEST_CHECK(root.child, CHILD_FIRST);
    TEST_CHECK(root.left->window_list[0], 2U);
    TEST_CHECK(root.right->window_list[0], 1U);
    TEST_CHECK(declarative_space_validate_root(&view, &image), true);
    root.child = CHILD_SECOND;
    TEST_CHECK(declarative_space_validate_root(&view, &image), true);

    root.child = (enum window_node_child) -1;
    TEST_CHECK(declarative_space_validate_root(&view, &image), false);
    root.child = (enum window_node_child) (CHILD_FIRST + 1);
    TEST_CHECK(declarative_space_validate_root(&view, &image), false);

    free(root.left);
    free(root.right);
})

TEST_FUNC(declarative_space_preparation_classifies_removals_and_owns_clear_images_without_live_writes,
{
    uint64_t sid = 55;
    struct window_node root;
    declarative_space_test_init_leaf(&root, 7, 0);
    struct view view = { .sid = 55, .uuid = CFSTR("uuid-55"), .layout = VIEW_STACK, .root = &root,
                         .flags = VIEW_LAYOUT | VIEW_TOP_PADDING | VIEW_ENABLE_PADDING };
    view.runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_SPACE, .value.integer = VIEW_STACK
    };
    struct declarative_runtime_state runtime = { 0 };
    runtime.global[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_GLOBAL, .value.integer = 17
    };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm);
    table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view);
    uint32_t seven = 7;
    table_add(&managed, &seven, &view);
    struct declarative_space_target target = { .view = &view, .sid = 55, .index = 1, .uuid = "uuid-55", .is_user_space = true };
    struct declarative_space_topology_entry topology = { .sid = 55, .index = 1, .uuid = "uuid-55", .is_user_space = true };
    struct declarative_space_table_contract view_contract = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract managed_contract = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { 55, 0 }, .wids = { { 7 } }, .counts = { 1, 0 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find, declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct view before_view = view;
    struct table before_views = views, before_managed = managed;
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &view_contract,
                                         &managed, &managed_contract, NULL, &runtime, &defaults,
                                         DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD, &ops, &preparation), true);
    TEST_CHECK(preparation.removal_count, 1UL);
    TEST_CHECK(preparation.removal_wids[0], 7U);
    TEST_CHECK(preparation.global_ownership_before[DECLARATIVE_CONFIG_WINDOW_GAP].present, true);
    TEST_CHECK(preparation.global_ownership_after[DECLARATIVE_CONFIG_WINDOW_GAP].present, false);
    TEST_CHECK(preparation.targets[0].ownership_before.view[DECLARATIVE_RUNTIME_SPACE_LAYOUT].present, true);
    TEST_CHECK(preparation.targets[0].ownership_after.view[DECLARATIVE_RUNTIME_SPACE_LAYOUT].present, false);
    TEST_CHECK((preparation.targets[0].ownership_after.legacy_flags & VIEW_LAYOUT) != 0, false);
    TEST_CHECK((preparation.targets[0].ownership_after.legacy_flags & VIEW_ENABLE_PADDING) != 0, true);
    TEST_CHECK(memcmp(&view, &before_view, sizeof(view)), 0);
    TEST_CHECK(memcmp(&views, &before_views, sizeof(views)), 0);
    TEST_CHECK(memcmp(&managed, &before_managed, sizeof(managed)), 0);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);

    bool reached_success = false;
    for (size_t ordinal = 1; ordinal < 40 && !reached_success; ++ordinal) {
        declarative_space_test_fail_allocation_at(ordinal);
        bool prepared = declarative_space_prepare(&target, 1, &topology, 1, &views, &view_contract,
                                                  &managed, &managed_contract, NULL, &runtime, &defaults,
                                                  DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preparation);
        TEST_CHECK(memcmp(&view, &before_view, sizeof(view)), 0);
        TEST_CHECK(memcmp(&views, &before_views, sizeof(views)), 0);
        TEST_CHECK(memcmp(&managed, &before_managed, sizeof(managed)), 0);
        if (prepared) {
            reached_success = true;
            declarative_space_preparation_destroy(&preparation);
        }
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    }
    TEST_CHECK(reached_success, true);
    declarative_space_test_fail_allocation_at(0);
    table_free(&managed); table_free(&views);
})

TEST_FUNC(declarative_space_target_and_old_table_validation_is_bijective,
{
    uint64_t sid = 44;
    struct view view = { .sid = 44, .uuid = CFSTR("uuid-44"), .layout = VIEW_STACK };
    struct window_node root;
    declarative_space_test_init_leaf(&root, 10, 11);
    view.root = &root;
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm);
    table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view);
    uint32_t ten = 10, eleven = 11;
    table_add(&managed, &ten, &view);
    table_add(&managed, &eleven, &view);
    struct declarative_space_target target = { .view = &view, .sid = 44, .index = 1, .uuid = "uuid-44", .is_user_space = true };
    struct declarative_space_topology_entry topology = { .sid = 44, .index = 1, .uuid = "uuid-44", .is_user_space = true };
    struct declarative_space_table_contract view_contract = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract managed_contract = { hash_wm, compare_wm, sizeof(uint32_t) };
    TEST_CHECK(declarative_space_validate_targets(&target, 1, &topology, 1, &views, &view_contract), true);
    struct declarative_space_root_before_image image;
    struct declarative_space_table_before_image table_image;
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &managed_contract, &image, &table_image), true);
    TEST_CHECK(table_image.count, 2);
    TEST_CHECK(table_image.buckets == managed.buckets, true);
    uint64_t fingerprint = table_image.fingerprint;
    for (int i = 0; i < managed.capacity; ++i) if (managed.buckets[i]) { managed.buckets[i]->value = (void *) 1; break; }
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &managed_contract, &image, &table_image), false);
    for (int i = 0; i < managed.capacity; ++i) if (managed.buckets[i] && managed.buckets[i]->value == (void *) 1) { managed.buckets[i]->value = &view; break; }
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &managed_contract, &image, &table_image), true);
    TEST_CHECK(table_image.fingerprint, fingerprint);
    target.index = 0;
    TEST_CHECK(declarative_space_validate_targets(&target, 1, &topology, 1, &views, &view_contract), false);
    target.index = 1; target.is_user_space = false;
    TEST_CHECK(declarative_space_validate_targets(&target, 1, &topology, 1, &views, &view_contract), false);
    target.is_user_space = true; topology.index = 2;
    TEST_CHECK(declarative_space_validate_targets(&target, 1, &topology, 1, &views, &view_contract), false);
    topology.index = 1;
    TEST_CHECK(declarative_space_validate_targets(&target, 1, NULL, 0, &views, &view_contract), false);
    struct declarative_space_topology_entry extra[2] = { topology, { 99, 2, "extra", false } };
    TEST_CHECK(declarative_space_validate_targets(&target, 1, extra, 2, &views, &view_contract), false);
    table_free(&managed); table_free(&views);
})
