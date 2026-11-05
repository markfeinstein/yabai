static struct declarative_space_defaults declarative_space_test_defaults(void)
{
    return (struct declarative_space_defaults) {
        .layout = VIEW_BSP, .top_padding = 1, .bottom_padding = 2,
        .left_padding = 3, .right_padding = 4, .window_gap = 5,
        .single_window_width_ratio = 1.0f,
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

TEST_FUNC(declarative_space_index_policy_is_dormant_until_target_appears,
{
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_space_policy policy = { .by_index = true, .index = 9 };
    policy.settings[DECLARATIVE_CONFIG_LAYOUT] = (struct declarative_value) {
        .present = true, .enumeration = VIEW_FLOAT
    };
    struct declarative_config_model native = { .spaces = &policy, .space_count = 1 };
    struct declarative_space_effective effective;
    for (int index = 1; index <= 8; ++index) {
        struct view view = { .sid = (uint64_t) index, .layout = VIEW_BSP };
        struct declarative_space_target target = {
            .view = &view, .sid = (uint64_t) index, .index = index,
            .uuid = "current", .is_user_space = true
        };
        TEST_CHECK(declarative_space_resolve_policy(&native, NULL, &defaults, &target,
                                                    DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
        TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer, VIEW_BSP);
        TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].source, DECLARATIVE_SETTING_BUILTIN);
    }
    struct view future = { .sid = 9, .layout = VIEW_BSP };
    struct declarative_space_target target9 = {
        .view = &future, .sid = 9, .index = 9, .uuid = "future", .is_user_space = true
    };
    TEST_CHECK(declarative_space_resolve_policy(&native, NULL, &defaults, &target9,
                                                DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer, VIEW_FLOAT);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].source, DECLARATIVE_SETTING_NATIVE_SPACE);
})

TEST_FUNC(declarative_split_ratio_resolver_preserves_all_precedence_layers_and_mode_overlay,
{
    struct view view = { .sid = 92, .split_ratio = 0.5f };
    struct declarative_space_target target = { .view = &view, .sid = 92, .index = 2, .uuid = "ratio-space", .is_user_space = true };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_config_model native = { 0 };
    native.config[DECLARATIVE_CONFIG_SPLIT_RATIO] = (struct declarative_value) { .present = true, .real = 0.55f };
    struct declarative_space_effective effective;
    TEST_CHECK(declarative_space_resolve_effective_policy(&native, NULL, 0, NULL, &defaults, &target,
                                                          DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real, 0.55f);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].source, DECLARATIVE_SETTING_NATIVE_GLOBAL);

    struct declarative_space_policy policies[2] = { 0 };
    policies[0].by_index = true; policies[0].index = 2;
    policies[0].settings[DECLARATIVE_CONFIG_SPLIT_RATIO] = (struct declarative_value) { .present = true, .real = 0.6f };
    policies[1].uuid = "ratio-space";
    policies[1].settings[DECLARATIVE_CONFIG_SPLIT_RATIO] = (struct declarative_value) { .present = true, .real = 0.65f };
    struct declarative_runtime_state runtime = { 0 };
    runtime.global[DECLARATIVE_CONFIG_SPLIT_RATIO] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_GLOBAL, .value.real = 0.7f
    };
    view.runtime_ownership[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_SPACE, .value.real = 0.75f
    };
    TEST_CHECK(declarative_space_resolve_effective_policy(&native, policies, 2, &runtime, &defaults, &target,
                                                          DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real, 0.75f);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].source, DECLARATIVE_SETTING_RUNTIME_SPACE);
    memset(&view.runtime_ownership, 0, sizeof(view.runtime_ownership));
    TEST_CHECK(declarative_space_resolve_effective_policy(&native, policies, 2, &runtime, &defaults, &target,
                                                          DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real, 0.7f);
    TEST_CHECK(declarative_space_resolve_effective_policy(&native, policies, 2, &runtime, &defaults, &target,
                                                          DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real, 0.65f);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].source, DECLARATIVE_SETTING_NATIVE_SPACE);
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
    TEST_CHECK(declarative_space_capture_inventory_result(targets, 2, &ops, &inventory),
               DECLARATIVE_SPACE_APPLY_TRANSIENT);
    TEST_CHECK(capture.queries[0], 1);
    TEST_CHECK(capture.queries[1], 0);
    TEST_CHECK(capture.finds, 3);
    TEST_CHECK(capture.eligibility, 2);
    TEST_CHECK(inventory.entries == NULL, true);
    ops.allow_unresolved_windows = true;
    TEST_CHECK(declarative_space_capture_inventory_result(targets, 2, &ops, &inventory),
               DECLARATIVE_SPACE_APPLY_OK);
    TEST_CHECK(inventory.count, 3UL);
    TEST_CHECK(declarative_space_inventory_find(&inventory, 99), NULL);
    declarative_space_inventory_destroy(&inventory);
    ops.allow_unresolved_windows = false;
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

    legacy_allocation_free(root.left);
    legacy_allocation_free(root.right);
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

static bool declarative_scaffold_test_case(const uint32_t *order, size_t count,
                                            bool ratio_present, float split_ratio)
{
    struct application applications[3] = { 0 };
    applications[0].name = "Mail"; applications[1].name = "Calendar"; applications[2].name = "Messages";
    struct window_node root = { .area = { .w = 1200.0f, .h = 800.0f } };
    struct view view = { .sid = 200, .uuid = CFSTR("space-2"), .layout = VIEW_BSP,
                         .split_ratio = 0.5f, .root = &root };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    uint64_t sid = 200; table_add(&views, &sid, &view);
    struct declarative_space_target target = { .view = &view, .sid = 200, .index = 2, .uuid = "space-2", .is_user_space = true };
    struct declarative_space_topology_entry topology = { .sid = 200, .index = 2, .uuid = "space-2", .is_user_space = true };
    struct declarative_space_table_contract view_contract = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract managed_contract = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { 200 }, .counts = { count } };
    bool present[3] = { false, false, false };
    for (size_t i = 0; i < count; ++i) {
        uint32_t wid = order[i]; capture.wids[0][i] = wid;
        int slot = wid == 10 ? 0 : wid == 20 ? 1 : 2; present[slot] = true;
        capture.windows[wid].application = &applications[slot];
    }
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find, declarative_space_test_eligible, &capture };
    struct declarative_space_policy policy = { .by_index = true, .index = 2, .scaffold_count = 3 };
    policy.scaffold[0].pattern = "^Mail$"; policy.scaffold[1].pattern = "^Calendar$"; policy.scaffold[2].pattern = "^Messages$";
    if (ratio_present) {
        policy.settings[DECLARATIVE_CONFIG_SPLIT_RATIO] = (struct declarative_value) {
            .present = true, .real = split_ratio
        };
    }
    struct declarative_config_model native = { .spaces = &policy, .space_count = 1 };
    struct declarative_runtime_state runtime = { 0 }; struct declarative_space_defaults defaults = declarative_space_test_defaults();
    defaults.auto_balance = SPLIT_NONE; defaults.focused_window_id = 0;
    struct declarative_space_preparation preparation; bool result = false;
    if (!declarative_space_prepare(&target, 1, &topology, 1, &views, &view_contract, &managed, &managed_contract,
                                   &native, &runtime, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preparation)) goto out;
    struct declarative_space_prepared_target *prepared = &preparation.targets[0];
    uint32_t expected[3] = { 0 }; size_t expected_count = 0;
    for (int slot = 0; slot < 3; ++slot) if (present[slot]) expected[expected_count++] = (uint32_t) ((slot + 1) * 10);
    if (!prepared->scaffold_changed || !prepared->final_scaffold_fingerprint) goto destroy;
    for (int slot = 0; slot < 3; ++slot) if (prepared->final_scaffold_primary[slot] != (present[slot] ? (uint32_t) ((slot + 1) * 10) : 0)) goto destroy;
    struct window_node *final = prepared->final_root;
    if (expected_count == 1) {
        if (final->left || final->window_count != 1 || final->window_list[0] != expected[0]) goto destroy;
    } else if (expected_count == 2) {
        float expected_ratio = ratio_present ? split_ratio : 0.5f;
        if (!final->left || final->split != SPLIT_Y || final->ratio != expected_ratio ||
            final->left->window_list[0] != expected[0] || final->right->window_list[0] != expected[1] ||
            final->left->area.w != truncf(1200.0f * expected_ratio) ||
            final->right->area.w != truncf(1200.0f * (1.0f - expected_ratio))) goto destroy;
    } else if (expected_count == 3) {
        float expected_ratio = ratio_present ? split_ratio : 0.5f;
        if (!final->left || final->split != SPLIT_Y || final->ratio != expected_ratio || final->left->window_list[0] != 10 ||
            !final->right->left || final->right->split != SPLIT_X || final->right->ratio != 0.5f ||
            final->right->left->window_list[0] != 20 || final->right->right->window_list[0] != 30 ||
            final->left->area.w != truncf(1200.0f * expected_ratio) ||
            final->right->area.w != truncf(1200.0f * (1.0f - expected_ratio)) ||
            final->right->left->area.h != 400.0f || final->right->right->area.h != 400.0f) goto destroy;
    }
    result = true;
destroy:
    declarative_space_preparation_destroy(&preparation);
out:
    table_free(&managed); table_free(&views);
    return result && declarative_space_test_outstanding_allocations() == 0;
}

TEST_FUNC(declarative_mode_policy_order_scaffold_inheritance_clear_and_stable_hash,
{
    struct window_node root = {0};
    struct view view = { .sid = 9, .uuid = CFSTR("mode-space"), .layout = VIEW_BSP, .root = &root };
    struct declarative_space_target target = {
        .view = &view, .sid = 9, .index = 1, .uuid = "mode-space", .is_user_space = true
    };
    struct declarative_space_policy policies[3] = {0};
    policies[0].by_index = true; policies[0].index = 1;
    policies[0].settings[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_value) { .present = true, .integer = 4 };
    policies[0].scaffold_present = true; policies[0].scaffold_count = 1;
    policies[0].scaffold[0].pattern = "^Base$";
    policies[1].by_index = true; policies[1].index = 1;
    policies[1].settings[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_value) { .present = true, .integer = 8 };
    policies[2].by_index = true; policies[2].index = 1; policies[2].scaffold_present = true;
    struct declarative_config_model native = {0};
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_space_effective effective = {0};
    TEST_CHECK(declarative_space_resolve_effective_policy(&native, policies, 2, NULL, &defaults, &target,
                                                          DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer, 8);
    TEST_CHECK(declarative_space_scaffold_policy(policies, 2, &target), &policies[0]);
    TEST_CHECK(declarative_space_scaffold_policy(policies, 3, &target), &policies[2]);
    struct declarative_space_policy copy = policies[0];
    TEST_CHECK(declarative_space_scaffold_hash(7, &policies[0]), declarative_space_scaffold_hash(7, &copy));
    TEST_CHECK(declarative_space_scaffold_hash(7, &policies[0]) != declarative_space_scaffold_hash(8, &copy), true);
})

TEST_FUNC(declarative_scaffold_compacts_subsets_and_ignores_opening_order,
{
    uint32_t all[] = { 10, 20, 30 };
    TEST_CHECK(declarative_scaffold_test_case(NULL, 0, false, 0.5f), true);
    for (unsigned mask = 1; mask < 8; ++mask) {
        uint32_t order[3]; size_t count = 0;
        for (int slot = 0; slot < 3; ++slot) if (mask & (1u << slot)) order[count++] = all[slot];
        TEST_CHECK(declarative_scaffold_test_case(order, count, false, 0.5f), true);
    }
    uint32_t permutations[6][3] = { {10,20,30}, {10,30,20}, {20,10,30}, {20,30,10}, {30,10,20}, {30,20,10} };
    for (size_t i = 0; i < 6; ++i) TEST_CHECK(declarative_scaffold_test_case(permutations[i], 3, false, 0.5f), true);
})

TEST_FUNC(declarative_scaffold_uses_effective_ratio_only_for_its_root_split,
{
    uint32_t two[] = { 10, 20 };
    uint32_t three[] = { 10, 20, 30 };
    TEST_CHECK(declarative_scaffold_test_case(two, 2, true, 0.6f), true);
    TEST_CHECK(declarative_scaffold_test_case(three, 3, true, 0.6f), true);
})

static bool declarative_scaffold_subtree_contains(struct window_node *node, uint32_t wid)
{
    if (!node) return false;
    if (!node->left) return window_node_contains_window(node, wid);
    return declarative_scaffold_subtree_contains(node->left, wid) ||
           declarative_scaffold_subtree_contains(node->right, wid);
}

TEST_FUNC(declarative_scaffold_ratio_change_rebuilds_existing_canonical_root,
{
    struct application apps[2] = { { .name = "Mail" }, { .name = "Calendar" } };
    struct window_node root = { .area = { .w = 1000.0f, .h = 700.0f }, .split = SPLIT_Y, .ratio = 0.5f };
    struct window_node left = { .parent = &root, .window_list = { 10 }, .window_order = { 10 }, .window_count = 1 };
    struct window_node right = { .parent = &root, .window_list = { 20 }, .window_order = { 20 }, .window_count = 1 };
    root.left = &left; root.right = &right;
    struct declarative_space_policy policy = { .by_index = true, .index = 2, .scaffold_count = 2 };
    policy.scaffold[0].pattern = "^Mail$"; policy.scaffold[1].pattern = "^Calendar$";
    policy.settings[DECLARATIVE_CONFIG_SPLIT_RATIO] = (struct declarative_value) { .present = true, .real = 0.6f };
    struct declarative_config_model native = { .spaces = &policy, .space_count = 1 };
    struct view view = { .sid = 202, .uuid = CFSTR("ratio-rebuild"), .layout = VIEW_BSP,
        .split_ratio = 0.5f, .root = &root,
        .scaffold_fingerprint = declarative_space_scaffold_hash(8, &policy),
        .scaffold_primary = { 10, 20, 0 } };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    uint64_t sid = view.sid; table_add(&views, &sid, &view);
    struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { 10, 20 } }, .counts = { 2 } };
    capture.windows[10].id = 10; capture.windows[20].id = 20;
    capture.windows[10].application = &apps[0]; capture.windows[20].application = &apps[1];
    table_add(&managed, &capture.windows[10].id, &view); table_add(&managed, &capture.windows[20].id, &view);
    struct declarative_space_target target = { .view = &view, .sid = sid, .index = 2, .uuid = "ratio-rebuild", .is_user_space = true };
    struct declarative_space_topology_entry topology = { .sid = sid, .index = 2, .uuid = "ratio-rebuild", .is_user_space = true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
        declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_runtime_state runtime = { 0 };
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare_effective(&target, 1, &topology, 1, &views, &vc, &managed, &mc,
        &native, native.spaces, native.space_count, 8, &runtime, &defaults,
        DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, false, VIEW_DEFAULT, &preparation), true);
    TEST_CHECK(preparation.targets[0].scaffold_changed, true);
    TEST_CHECK(preparation.targets[0].changed, true);
    TEST_CHECK(preparation.targets[0].final_root != &root, true);
    TEST_CHECK(preparation.targets[0].final_root->ratio, 0.6f);
    declarative_space_preparation_destroy(&preparation);
    table_free(&managed); table_free(&views);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})

TEST_FUNC(declarative_scaffold_primary_selection_and_overflow_stability,
{
    struct application apps[2] = { 0 }; apps[0].name = "Mail"; apps[1].name = "Safari";
    struct window windows[3] = { { .id = 10, .application = &apps[0] }, { .id = 11, .application = &apps[0] }, { .id = 40, .application = &apps[1] } };
    uint32_t final_wids[] = { 11, 10, 40 };
    struct declarative_space_inventory_entry inventory_entries[] = {
        { .wid = 11, .sid = 2, .window = &windows[1], .eligible = true },
        { .wid = 10, .sid = 2, .window = &windows[0], .eligible = true },
        { .wid = 40, .sid = 2, .window = &windows[2], .eligible = true }
    };
    struct declarative_space_policy policy = { .by_index = true, .index = 2, .scaffold_count = 2 };
    policy.scaffold[0].pattern = "^Mail$"; policy.scaffold[1].pattern = ".*";
    struct declarative_config_model native = { .spaces = &policy, .space_count = 1 };
    struct declarative_space_prepared_target target = { .target = { .index = 2, .uuid = "s", .is_user_space = true },
        .final_wids = final_wids, .final_count = 3 };
    target.effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer = VIEW_BSP;
    struct declarative_space_final_owner owners[] = {
        { .wid = 10, .window = &windows[0] }, { .wid = 11, .window = &windows[1] }, { .wid = 40, .window = &windows[2] }
    };
    struct declarative_space_preparation preparation = { .targets = &target, .target_count = 1,
        .inventory = { .entries = inventory_entries, .count = 3 }, .final_owners = owners, .final_owner_count = 3 };
    TEST_CHECK(declarative_space_prepare_scaffolds(&preparation, native.spaces, native.space_count, 0), true);
    TEST_CHECK(target.final_scaffold_primary[0], 10U);
    TEST_CHECK(target.final_scaffold_primary[1], 40U);

    struct window_node root = { 0 }, left, right;
    declarative_space_test_init_leaf(&left, 10, 0); declarative_space_test_init_leaf(&right, 40, 0);
    root.left = &left; root.right = &right; root.split = SPLIT_Y; root.ratio = 0.5f; root.area = (struct area) { .w = 1200, .h = 800 };
    left.parent = &root; right.parent = &root;
    struct view view = { .sid = 2, .uuid = CFSTR("s"), .layout = VIEW_BSP, .split_ratio = 0.5f, .root = &root,
        .scaffold_fingerprint = declarative_space_scaffold_hash(0, &policy), .scaffold_primary = { 10, 40, 0 } };
    struct table views = { 0 }, managed = { 0 }; table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    uint64_t sid = 2; table_add(&views, &sid, &view); table_add(&managed, &windows[0].id, &view); table_add(&managed, &windows[2].id, &view);
    struct declarative_space_target public_target = { .view = &view, .sid = 2, .index = 2, .uuid = "s", .is_user_space = true };
    struct declarative_space_topology_entry topology = { .sid = 2, .index = 2, .uuid = "s", .is_user_space = true };
    struct declarative_space_table_contract view_contract = { hash_wm, compare_wm, sizeof(uint64_t) }, managed_contract = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { 2 }, .wids = { { 10, 40, 11 } }, .counts = { 3 } };
    capture.windows[10].application = &apps[0]; capture.windows[11].application = &apps[0]; capture.windows[40].application = &apps[1];
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find, declarative_space_test_eligible, &capture };
    struct declarative_runtime_state runtime = { 0 }; struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_space_preparation full;
    TEST_CHECK(declarative_space_prepare(&public_target, 1, &topology, 1, &views, &view_contract, &managed, &managed_contract,
        &native, &runtime, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &full), true);
    TEST_CHECK(full.targets[0].changed, true);
    TEST_CHECK(full.targets[0].scaffold_changed, false);
    TEST_CHECK(full.targets[0].final_root != &root, true);
    struct window_node *final = full.targets[0].final_root;
    TEST_CHECK(final->split, SPLIT_Y);
    TEST_CHECK(declarative_scaffold_subtree_contains(final->left, 10), true);
    TEST_CHECK(declarative_scaffold_subtree_contains(final->right, 40), true);
    for (uint32_t wid = 10; wid <= 40; wid += wid == 10 ? 1 : 29) {
        int occurrences = 0;
        for (struct window_node *leaf = window_node_find_first_leaf(final); leaf; leaf = window_node_find_next_leaf(leaf)) {
            for (int i = 0; i < leaf->window_count; ++i) if (leaf->window_list[i] == wid) ++occurrences;
        }
        TEST_CHECK(occurrences, 1);
    }
    declarative_space_preparation_destroy(&full); table_free(&managed); table_free(&views);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})
