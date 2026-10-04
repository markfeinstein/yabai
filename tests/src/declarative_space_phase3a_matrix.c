
static const enum declarative_config_id declarative_space_test_field_ids[DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT] = {
    DECLARATIVE_CONFIG_LAYOUT, DECLARATIVE_CONFIG_TOP_PADDING, DECLARATIVE_CONFIG_BOTTOM_PADDING,
    DECLARATIVE_CONFIG_LEFT_PADDING, DECLARATIVE_CONFIG_RIGHT_PADDING, DECLARATIVE_CONFIG_WINDOW_GAP,
    DECLARATIVE_CONFIG_ACCORDION_PADDING, DECLARATIVE_CONFIG_SPLIT_RATIO,
    DECLARATIVE_CONFIG_SPLIT_TYPE, DECLARATIVE_CONFIG_AUTO_BALANCE
};

static const uint64_t declarative_space_test_legacy_flags[DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT] = {
    VIEW_LAYOUT, VIEW_TOP_PADDING, VIEW_BOTTOM_PADDING, VIEW_LEFT_PADDING,
    VIEW_RIGHT_PADDING, VIEW_WINDOW_GAP, VIEW_ACCORDION_PADDING, VIEW_SPLIT_RATIO,
    VIEW_SPLIT_TYPE, VIEW_AUTO_BALANCE
};

static void declarative_space_test_set_native_value(struct declarative_value *value, int scalar)
{
    memset(value, 0, sizeof(*value));
    value->present = true;
    value->integer = scalar;
}

static void declarative_space_test_set_runtime_cell(struct declarative_runtime_cell *cell,
                                                     enum declarative_setting_source source,
                                                     int scalar)
{
    memset(cell, 0, sizeof(*cell));
    cell->present = true;
    cell->source = source;
    cell->value.integer = scalar;
}

static int declarative_space_test_layer_value(int field, int layer)
{
    return 1000 + field * 100 + layer * 7;
}

static enum declarative_setting_source declarative_space_test_layer_source(int layer)
{
    static const enum declarative_setting_source sources[] = {
        DECLARATIVE_SETTING_BUILTIN, DECLARATIVE_SETTING_NATIVE_GLOBAL,
        DECLARATIVE_SETTING_NATIVE_SPACE, DECLARATIVE_SETTING_RUNTIME_GLOBAL,
        DECLARATIVE_SETTING_RUNTIME_SPACE
    };
    return sources[layer];
}

TEST_FUNC(declarative_space_generated_precedence_matrix_covers_all_fields_and_layer_pairs,
{
    struct view view = { .sid = 901 };
    struct declarative_space_target target = { .view = &view, .sid = 901, .index = 9, .uuid = "matrix-901", .is_user_space = true };
    for (int field = 0; field < DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT; ++field) {
        enum declarative_config_id id = declarative_space_test_field_ids[field];
        for (int lower = 0; lower < 5; ++lower) {
            for (int upper = lower + 1; upper < 5; ++upper) {
                struct declarative_space_defaults defaults = { 0 };
                struct declarative_config_model native = { 0 };
                struct declarative_space_policy policy = { 0 };
                struct declarative_runtime_state runtime = { 0 };
                native.spaces = &policy; native.space_count = 1;
                if ((field + lower + upper) & 1) policy.uuid = "matrix-901";
                else { policy.by_index = true; policy.index = 9; }
                int values[5];
                for (int layer = 0; layer < 5; ++layer) values[layer] = declarative_space_test_layer_value(field, layer);
                switch (field) {
                case DECLARATIVE_RUNTIME_SPACE_LAYOUT: defaults.layout = values[0]; break;
                case DECLARATIVE_RUNTIME_SPACE_TOP_PADDING: defaults.top_padding = values[0]; break;
                case DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING: defaults.bottom_padding = values[0]; break;
                case DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING: defaults.left_padding = values[0]; break;
                case DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING: defaults.right_padding = values[0]; break;
                case DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP: defaults.window_gap = values[0]; break;
                case DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING: defaults.accordion_padding = values[0]; break;
                case DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO: defaults.split_ratio = (float) values[0] / 10000.0f; break;
                case DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE: defaults.split_type = values[0]; break;
                case DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE: defaults.auto_balance = (uint32_t) values[0]; break;
                }
                int layers[2] = { lower, upper };
                for (int i = 0; i < 2; ++i) {
                    int layer = layers[i];
                    if (layer == 1) declarative_space_test_set_native_value(&native.config[id], values[layer]);
                    if (layer == 2) declarative_space_test_set_native_value(&policy.settings[id], values[layer]);
                    if (layer == 3) declarative_space_test_set_runtime_cell(&runtime.global[id], DECLARATIVE_SETTING_RUNTIME_GLOBAL, values[layer]);
                    if (layer == 4) declarative_space_test_set_runtime_cell(&view.runtime_ownership[field], DECLARATIVE_SETTING_RUNTIME_SPACE, values[layer]);
                    if (id == DECLARATIVE_CONFIG_SPLIT_RATIO) {
                        if (layer == 1) native.config[id].real = (float) values[layer] / 10000.0f;
                        if (layer == 2) policy.settings[id].real = (float) values[layer] / 10000.0f;
                        if (layer == 3) runtime.global[id].value.real = (float) values[layer] / 10000.0f;
                        if (layer == 4) view.runtime_ownership[field].value.real = (float) values[layer] / 10000.0f;
                    }
                }
                struct declarative_space_effective effective;
                TEST_CHECK(declarative_space_resolve_policy(&native, &runtime, &defaults, &target,
                                                             DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
                TEST_CHECK(effective.fields[field].source, declarative_space_test_layer_source(upper));
                if (field == DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO) {
                    TEST_CHECK(effective.fields[field].value.real, (float) values[upper] / 10000.0f);
                } else {
                    TEST_CHECK(effective.fields[field].value.integer, values[upper]);
                }
                int absent = (field + 1) % DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT;
                TEST_CHECK(effective.fields[absent].source, DECLARATIVE_SETTING_BUILTIN);
                memset(view.runtime_ownership, 0, sizeof(view.runtime_ownership));
            }
        }
    }
})

TEST_FUNC(declarative_space_generated_full_chain_and_native_overlap_are_fieldwise,
{
    struct view view = { .sid = 902 };
    struct declarative_space_target target = { .view = &view, .sid = 902, .index = 12, .uuid = "chain-902", .is_user_space = true };
    struct declarative_space_defaults defaults = { 0 };
    struct declarative_runtime_state runtime = { 0 };
    struct declarative_space_policy policies[2] = { 0 };
    policies[0].by_index = true; policies[0].index = 12;
    policies[1].uuid = "chain-902";
    struct declarative_config_model native = { .spaces = policies, .space_count = 2 };
    for (int field = 0; field < DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT; ++field) {
        enum declarative_config_id id = declarative_space_test_field_ids[field];
        int base = declarative_space_test_layer_value(field, 0);
        switch (field) {
        case DECLARATIVE_RUNTIME_SPACE_LAYOUT: defaults.layout = base; break;
        case DECLARATIVE_RUNTIME_SPACE_TOP_PADDING: defaults.top_padding = base; break;
        case DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING: defaults.bottom_padding = base; break;
        case DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING: defaults.left_padding = base; break;
        case DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING: defaults.right_padding = base; break;
        case DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP: defaults.window_gap = base; break;
        case DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING: defaults.accordion_padding = base; break;
        case DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO: defaults.split_ratio = (float) base / 10000.0f; break;
        case DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE: defaults.split_type = base; break;
        case DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE: defaults.auto_balance = (uint32_t) base; break;
        }
        declarative_space_test_set_native_value(&native.config[id], declarative_space_test_layer_value(field, 1));
        struct declarative_space_policy *owner = &policies[field & 1];
        declarative_space_test_set_native_value(&owner->settings[id], declarative_space_test_layer_value(field, 2));
        declarative_space_test_set_runtime_cell(&runtime.global[id], DECLARATIVE_SETTING_RUNTIME_GLOBAL,
                                                declarative_space_test_layer_value(field, 3));
        declarative_space_test_set_runtime_cell(&view.runtime_ownership[field], DECLARATIVE_SETTING_RUNTIME_SPACE,
                                                declarative_space_test_layer_value(field, 4));
        if (id == DECLARATIVE_CONFIG_SPLIT_RATIO) {
            native.config[id].real = (float) declarative_space_test_layer_value(field, 1) / 10000.0f;
            owner->settings[id].real = (float) declarative_space_test_layer_value(field, 2) / 10000.0f;
            runtime.global[id].value.real = (float) declarative_space_test_layer_value(field, 3) / 10000.0f;
            view.runtime_ownership[field].value.real = (float) declarative_space_test_layer_value(field, 4) / 10000.0f;
        }
    }
    /* A later UUID policy overlaps an index policy and wins only the overlapping field. */
    declarative_space_test_set_native_value(&policies[0].settings[DECLARATIVE_CONFIG_TOP_PADDING], 2201);
    declarative_space_test_set_native_value(&policies[1].settings[DECLARATIVE_CONFIG_TOP_PADDING], 2202);
    for (int winner = 4; winner >= 0; --winner) {
        struct declarative_space_effective effective;
        TEST_CHECK(declarative_space_resolve_policy(&native, &runtime, &defaults, &target,
                                                     DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective), true);
        for (int field = 0; field < DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT; ++field) {
            int expected = declarative_space_test_layer_value(field, winner);
            if (winner == 2 && field == DECLARATIVE_RUNTIME_SPACE_TOP_PADDING) expected = 2202;
            TEST_CHECK(effective.fields[field].source, declarative_space_test_layer_source(winner));
            if (field == DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO) {
                TEST_CHECK(effective.fields[field].value.real, (float) expected / 10000.0f);
            } else {
                TEST_CHECK(effective.fields[field].value.integer, expected);
            }
        }
        if (winner == 4) memset(view.runtime_ownership, 0, sizeof(view.runtime_ownership));
        else if (winner == 3) memset(&runtime, 0, sizeof(runtime));
        else if (winner == 2) { native.spaces = NULL; native.space_count = 0; }
        else if (winner == 1) memset(native.config, 0, sizeof(native.config));
    }
    view.layout = VIEW_STACK; view.top_padding = 31; view.bottom_padding = 32; view.left_padding = 33;
    view.right_padding = 34; view.window_gap = 35; view.accordion_padding = 36; view.split_ratio = 0.6f;
    view.split_type = SPLIT_Y; view.auto_balance = SPLIT_X;
    target.is_user_space = false;
    struct declarative_space_effective effective;
    TEST_CHECK(declarative_space_resolve_policy(&native, &runtime, &defaults, &target,
                                                 DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD, &effective), true);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer, VIEW_FLOAT);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].value.integer, 31);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING].value.integer, 32);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING].value.integer, 33);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING].value.integer, 34);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer, 35);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING].value.integer, 36);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real, 0.6f);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].value.integer, SPLIT_Y);
    TEST_CHECK(effective.fields[DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE].value.u32, SPLIT_X);
})

TEST_FUNC(declarative_space_generated_ownership_matrix_preserves_clears_and_detects_each_mutation,
{
    uint64_t sid = 903;
    struct window_node root;
    declarative_space_test_init_leaf(&root, 9, 0);
    struct view view = { .sid = 903, .uuid = CFSTR("ownership-903"), .layout = VIEW_STACK, .root = &root,
                         .flags = VIEW_ENABLE_PADDING | VIEW_ENABLE_GAP | VIEW_IS_DIRTY };
    struct declarative_runtime_state runtime = { 0 };
    for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) {
        declarative_space_test_set_runtime_cell(&runtime.global[id], DECLARATIVE_SETTING_RUNTIME_GLOBAL, 3000 + id);
    }
    for (int field = 0; field < DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT; ++field) {
        declarative_space_test_set_runtime_cell(&view.runtime_ownership[field], DECLARATIVE_SETTING_RUNTIME_SPACE, 4000 + field);
        view.flags |= declarative_space_test_legacy_flags[field];
    }
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view); uint32_t nine = 9; table_add(&managed, &nine, &view);
    struct declarative_space_target target = { .view = &view, .sid = sid, .index = 13, .uuid = "ownership-903", .is_user_space = true };
    struct declarative_space_topology_entry topology = { .sid = sid, .index = 13, .uuid = "ownership-903", .is_user_space = true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { 903 }, .wids = { { 9 } }, .counts = { 1 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find, declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_space_preparation preserve, clear;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc, NULL, &runtime,
                                         &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preserve), true);
    TEST_CHECK(memcmp(preserve.global_ownership_before, preserve.global_ownership_after,
                      sizeof(preserve.global_ownership_before)), 0);
    TEST_CHECK(memcmp(&preserve.targets[0].ownership_before, &preserve.targets[0].ownership_after,
                      sizeof(preserve.targets[0].ownership_before)), 0);
    TEST_CHECK(preserve.targets[0].target.view == &view, true);
    TEST_CHECK(preserve.targets[0].target.uuid == target.uuid, false);
    TEST_CHECK(strcmp(preserve.targets[0].target.uuid, target.uuid), 0);
    TEST_CHECK(declarative_space_global_ownership_before_matches(&runtime, preserve.global_ownership_before), true);
    TEST_CHECK(declarative_space_view_ownership_before_matches(&view, &preserve.targets[0].ownership_before), true);
    for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) {
        runtime.global[id].value.integer ^= 0x55;
        TEST_CHECK(declarative_space_global_ownership_before_matches(&runtime, preserve.global_ownership_before), false);
        runtime.global[id] = preserve.global_ownership_before[id];
    }
    for (int field = 0; field < DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT; ++field) {
        view.runtime_ownership[field].value.integer ^= 0x55;
        TEST_CHECK(declarative_space_view_ownership_before_matches(&view, &preserve.targets[0].ownership_before), false);
        view.runtime_ownership[field] = preserve.targets[0].ownership_before.view[field];
        view.flags ^= declarative_space_test_legacy_flags[field];
        TEST_CHECK(declarative_space_view_ownership_before_matches(&view, &preserve.targets[0].ownership_before), false);
        view.flags ^= declarative_space_test_legacy_flags[field];
    }
    uint64_t operational[] = { VIEW_ENABLE_PADDING, VIEW_ENABLE_GAP };
    for (size_t i = 0; i < sizeof(operational) / sizeof(operational[0]); ++i) {
        view.flags ^= operational[i];
        TEST_CHECK(declarative_space_view_ownership_before_matches(&view, &preserve.targets[0].ownership_before), false);
        view.flags ^= operational[i];
    }
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &managed, &mc, NULL, &runtime,
                                         &defaults, DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD, &ops, &clear), true);
    for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) TEST_CHECK(clear.global_ownership_after[id].present, false);
    for (int field = 0; field < DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT; ++field) {
        TEST_CHECK(clear.targets[0].ownership_after.view[field].present, false);
        TEST_CHECK((clear.targets[0].ownership_after.legacy_flags & declarative_space_test_legacy_flags[field]) != 0, false);
    }
    TEST_CHECK((clear.targets[0].ownership_after.legacy_flags & VIEW_ENABLE_PADDING) != 0, true);
    TEST_CHECK((clear.targets[0].ownership_after.legacy_flags & VIEW_ENABLE_GAP) != 0, true);
    TEST_CHECK((clear.targets[0].ownership_after.legacy_flags & VIEW_IS_DIRTY) != 0, true);
    declarative_space_preparation_destroy(&clear);
    declarative_space_preparation_destroy(&preserve);

    struct table empty_views = { 0 }, empty_managed = { 0 };
    table_init(&empty_views, 8, hash_wm, compare_wm); table_init(&empty_managed, 8, hash_wm, compare_wm);
    TEST_CHECK(declarative_space_prepare(NULL, 0, NULL, 0, &empty_views, &vc, &empty_managed, &mc, NULL, &runtime,
                                         &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preserve), true);
    TEST_CHECK(declarative_space_global_ownership_before_matches(&runtime, preserve.global_ownership_after), true);
    declarative_space_preparation_destroy(&preserve);
    TEST_CHECK(declarative_space_prepare(NULL, 0, NULL, 0, &empty_views, &vc, &empty_managed, &mc, NULL, &runtime,
                                         &defaults, DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD, &ops, &clear), true);
    for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) TEST_CHECK(clear.global_ownership_after[id].present, false);
    declarative_space_preparation_destroy(&clear);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&empty_managed); table_free(&empty_views); table_free(&managed); table_free(&views);
})

TEST_FUNC(declarative_space_generated_multi_target_allocator_matrix_is_transactional,
{
    enum { target_count = 3 };
    uint64_t sids[target_count] = { 910, 911, 912 };
    uint32_t wids[target_count][2] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };
    struct window_node roots[target_count];
    struct view views_live[target_count];
    memset(views_live, 0, sizeof(views_live));
    const CFStringRef cfuuids[target_count] = { CFSTR("alloc-910"), CFSTR("alloc-911"), CFSTR("alloc-912") };
    const char *uuids[target_count] = { "alloc-910", "alloc-911", "alloc-912" };
    struct declarative_space_target targets[target_count];
    struct declarative_space_topology_entry topology[target_count];
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 16, hash_wm, compare_wm);
    for (int i = 0; i < target_count; ++i) {
        declarative_space_test_init_leaf(&roots[i], wids[i][0], wids[i][1]);
        views_live[i] = (struct view) { .sid = sids[i], .uuid = cfuuids[i], .layout = VIEW_STACK, .root = &roots[i],
                                        .top_padding = 50 + i, .flags = VIEW_TOP_PADDING | VIEW_ENABLE_PADDING };
        declarative_space_test_set_runtime_cell(&views_live[i].runtime_ownership[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING],
                                                DECLARATIVE_SETTING_RUNTIME_SPACE, 50 + i);
        targets[i] = (struct declarative_space_target) { &views_live[i], sids[i], 20 + i, uuids[i], true };
        topology[i] = (struct declarative_space_topology_entry) { sids[i], 20 + i, uuids[i], true };
        table_add(&views, &sids[i], &views_live[i]);
        table_add(&managed, &wids[i][0], &views_live[i]); table_add(&managed, &wids[i][1], &views_live[i]);
    }
    struct declarative_space_test_capture capture = {
        .sids = { 910, 911, 912 }, .wids = { { 1, 2 }, { 3, 4 }, { 5, 6 } }, .counts = { 2, 2, 2 }
    };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find, declarative_space_test_eligible, &capture };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_runtime_state runtime = { 0 };
    declarative_space_test_set_runtime_cell(&runtime.global[DECLARATIVE_CONFIG_WINDOW_GAP],
                                            DECLARATIVE_SETTING_RUNTIME_GLOBAL, 77);
    struct window_node roots_before[target_count]; memcpy(roots_before, roots, sizeof(roots));
    struct view views_before[target_count]; memcpy(views_before, views_live, sizeof(views_live));
    struct table views_before_table = views, managed_before_table = managed;

    size_t capture_terminal = 0;
    for (size_t ordinal = 1; ordinal < 16; ++ordinal) {
        struct declarative_space_inventory inventory;
        declarative_space_test_fail_allocation_at(ordinal);
        bool success = declarative_space_capture_inventory(targets, target_count, &ops, &inventory);
        if (success) {
            capture_terminal = ordinal;
            TEST_CHECK(inventory.count, 6UL);
            declarative_space_inventory_destroy(&inventory);
            break;
        }
        TEST_CHECK(inventory.entries == NULL, true);
        TEST_CHECK(inventory.count, 0UL);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    }
    TEST_CHECK(capture_terminal, 3UL);

    declarative_space_test_fail_allocation_at(0);
    for (int i = 0; i < target_count; ++i) {
        TEST_CHECK(views_live[i].sid, targets[i].sid);
        TEST_CHECK(CFEqual(views_live[i].uuid, cfuuids[i]), true);
        TEST_CHECK(table_find(&views, &targets[i].sid) == &views_live[i], true);
    }
    TEST_CHECK(declarative_space_validate_targets(targets, target_count, topology, target_count, &views, &vc), true);
    struct declarative_space_root_before_image root_images[target_count];
    struct declarative_space_table_before_image table_image;
    TEST_CHECK(declarative_space_validate_forest_and_table(targets, target_count, &managed, &mc,
                                                           root_images, &table_image), true);
    struct declarative_space_preparation preflight;
    TEST_CHECK(declarative_space_prepare(targets, target_count, topology, target_count, &views, &vc,
                                         &managed, &mc, NULL, &runtime, &defaults,
                                         DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preflight), true);
    declarative_space_preparation_destroy(&preflight);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);

    size_t prepare_terminal = 0;
    for (size_t ordinal = 1; ordinal < 96; ++ordinal) {
        struct declarative_space_preparation preparation;
        declarative_space_test_fail_allocation_at(ordinal);
        bool success = declarative_space_prepare(targets, target_count, topology, target_count, &views, &vc,
                                                  &managed, &mc, NULL, &runtime, &defaults,
                                                  DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preparation);
        TEST_CHECK(memcmp(roots, roots_before, sizeof(roots)), 0);
        TEST_CHECK(memcmp(views_live, views_before, sizeof(views_live)), 0);
        TEST_CHECK(memcmp(&views, &views_before_table, sizeof(views)), 0);
        TEST_CHECK(memcmp(&managed, &managed_before_table, sizeof(managed)), 0);
        declarative_space_test_fail_allocation_at(0);
        struct declarative_space_root_before_image roots_after[target_count];
        struct declarative_space_table_before_image table_after;
        TEST_CHECK(declarative_space_validate_forest_and_table(targets, target_count, &managed, &mc,
                                                               roots_after, &table_after), true);
        TEST_CHECK(table_after.fingerprint, table_image.fingerprint);
        for (int i = 0; i < target_count; ++i) {
            TEST_CHECK(roots_after[i].fingerprint, root_images[i].fingerprint);
        }
        if (success) {
            prepare_terminal = ordinal;
            TEST_CHECK(preparation.target_count, (size_t) target_count);
            TEST_CHECK(preparation.inventory.count, 6UL);
            declarative_space_preparation_destroy(&preparation);
            break;
        }
        TEST_CHECK(preparation.targets == NULL, true);
        TEST_CHECK(preparation.inventory.entries == NULL, true);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    }
    /* Phase 3B extends preparation with detached roots and a checked replacement table. */
    TEST_CHECK(prepare_terminal > 16UL && prepare_terminal < 96UL, true);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    declarative_space_test_fail_allocation_at(0);
    table_free(&managed); table_free(&views);
})
