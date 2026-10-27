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

static AXUIElementRef stale_ref_test_old;
static AXUIElementRef stale_ref_test_fresh;
static uint32_t stale_ref_test_window_id;
static int stale_ref_test_set_calls;
static int stale_ref_test_observe_calls;
static int stale_ref_test_unobserve_calls;

static CFArrayRef stale_ref_test_window_list(struct application *application)
{
    (void) application;
    if (!stale_ref_test_fresh) return NULL;
    const void *values[] = { stale_ref_test_fresh };
    return CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
}

static uint32_t stale_ref_test_id(AXUIElementRef ref)
{
    (void) ref;
    return stale_ref_test_window_id;
}

static AXError stale_ref_test_set_attribute(AXUIElementRef element, CFStringRef attribute, CFTypeRef value)
{
    (void) attribute;
    (void) value;
    ++stale_ref_test_set_calls;
    return element == stale_ref_test_old ? kAXErrorInvalidUIElement : kAXErrorSuccess;
}

static bool stale_ref_test_observe(struct window *window, bool *retry)
{
    (void) window;
    if (retry) *retry = false;
    ++stale_ref_test_observe_calls;
    return true;
}

static void stale_ref_test_unobserve(struct window *window)
{
    (void) window;
    ++stale_ref_test_unobserve_calls;
}

TEST_FUNC(stale_ax_reference_refreshes_and_retries_once,
{
    __auto_type saved_list = window_manager_test_application_window_list;
    __auto_type saved_id = window_manager_test_ax_window_id;
    __auto_type saved_set = window_manager_test_set_attribute_value;
    __auto_type saved_observe = window_manager_test_window_observe;
    __auto_type saved_unobserve = window_manager_test_window_unobserve;
    struct table saved_managed = g_window_manager.managed_window;
    g_window_manager.managed_window = (struct table) {0};
    table_init(&g_window_manager.managed_window, 4, hash_wm, compare_wm);

    struct application application = { .name = "stale-ref" };
    stale_ref_test_old = (AXUIElementRef) CFStringCreateCopy(NULL, CFSTR("old-ref"));
    stale_ref_test_fresh = (AXUIElementRef) CFStringCreateCopy(NULL, CFSTR("fresh-ref"));
    stale_ref_test_window_id = 4242;
    stale_ref_test_set_calls = 0;
    stale_ref_test_observe_calls = 0;
    stale_ref_test_unobserve_calls = 0;
    struct window window = {
        .application = &application,
        .ref = stale_ref_test_old,
        .id = stale_ref_test_window_id,
        .constraints = {
            .generation = 7,
            .observed_at = 9,
            .validity = WINDOW_CONSTRAINT_MINIMUM_VALID | WINDOW_CONSTRAINT_MAXIMUM_VALID |
                        WINDOW_CONSTRAINT_LIMITS_ORDERED,
            .source = WINDOW_CONSTRAINT_SOURCE_ITERATOR
        }
    };

    window_manager_test_application_window_list = stale_ref_test_window_list;
    window_manager_test_ax_window_id = stale_ref_test_id;
    window_manager_test_set_attribute_value = stale_ref_test_set_attribute;
    window_manager_test_window_observe = stale_ref_test_observe;
    window_manager_test_window_unobserve = stale_ref_test_unobserve;

    window_manager_move_window(&window, 100.0f, 200.0f);
    TEST_CHECK(stale_ref_test_set_calls, 2);
    TEST_CHECK(stale_ref_test_unobserve_calls, 1);
    TEST_CHECK(stale_ref_test_observe_calls, 1);
    TEST_CHECK(window.ref == stale_ref_test_fresh, true);
    TEST_CHECK(window.constraints.validity, 0);
    TEST_CHECK(window.constraints.observed_at, 0ULL);
    TEST_CHECK(window.constraints.generation > 7, true);
    TEST_CHECK(window_manager_refresh_window_ref(&g_window_manager, &window), false);

    CFRelease(window.ref);
    CFRelease(stale_ref_test_fresh);
    stale_ref_test_old = NULL;
    stale_ref_test_fresh = NULL;
    window_manager_test_application_window_list = saved_list;
    window_manager_test_ax_window_id = saved_id;
    window_manager_test_set_attribute_value = saved_set;
    window_manager_test_window_observe = saved_observe;
    window_manager_test_window_unobserve = saved_unobserve;
    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed;
});

TEST_FUNC(window_manager_invalidates_all_constraint_caches,
{
    struct window_manager wm = {0};
    table_init(&wm.window, 4, hash_wm, compare_wm);
    struct window first = { .id = 1, .constraints = { .generation = 1, .validity = WINDOW_CONSTRAINT_CURRENT_VALID } };
    struct window second = { .id = 2, .constraints = { .generation = 2, .validity = WINDOW_CONSTRAINT_CURRENT_VALID } };
    table_add(&wm.window, &first.id, &first);
    table_add(&wm.window, &second.id, &second);
    window_manager_invalidate_window_constraints(&wm);
    TEST_CHECK(first.constraints.validity, 0);
    TEST_CHECK(second.constraints.validity, 0);
    TEST_CHECK(first.constraints.generation > 1, true);
    TEST_CHECK(second.constraints.generation > 2, true);
    table_free(&wm.window);
});

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

static char *window_manager_test_string_copy_failure(char *value)
{
    (void) value;
    return NULL;
}

TEST_FUNC(rule_scratchpad_allocation_failure_preserves_fold_and_one_shot,
{
    __auto_type saved_copy = rule_effects_string_copy;
    rule_effects_string_copy = window_manager_test_string_copy_failure;

    struct rule_effects effects = { .manage = RULE_PROP_ON, .scratchpad = "next" };
    struct rule_effects aggregate = { .manage = RULE_PROP_OFF, .scratchpad = string_copy("prior") };
    char *prior = aggregate.scratchpad;
    struct rule rule = {
        .effects = effects, .flags = RULE_ONE_SHOT, .owner = RULE_OWNER_RUNTIME
    };
    TEST_CHECK(rule_combine_rule_effects(&rule, &aggregate, NULL), RULE_FOLD_RESOURCE);
    TEST_CHECK(aggregate.manage, RULE_PROP_OFF);
    TEST_CHECK(aggregate.scratchpad, prior);
    TEST_CHECK(strcmp(aggregate.scratchpad, "prior"), 0);

    struct window_manager wm = {0};
    struct application application = { .name = "App" };
    struct window window = { .id = 42, .is_root = true, .application = &application };
    buf_push(wm.rules, rule);
    window_manager_apply_manage_rules_to_window(&g_space_manager, &wm, &window,
                                                 "title", "AXWindow", "AXStandardWindow", true, 0);
    TEST_CHECK(rule_check_flag(&wm.rules[0], RULE_ONE_SHOT_REMOVE), false);
    window_manager_apply_rules_to_window(&g_space_manager, &wm, &window,
                                          "title", "AXWindow", "AXStandardWindow", true, 0);
    TEST_CHECK(rule_check_flag(&wm.rules[0], RULE_ONE_SHOT_REMOVE), false);

    wm.rules[0].owner = RULE_OWNER_NATIVE;
    window_manager_apply_rules_to_window(&g_space_manager, &wm, &window,
                                          "title", "AXWindow", "AXStandardWindow", true, 0);
    TEST_CHECK(rule_check_flag(&wm.rules[0], RULE_ONE_SHOT_REMOVE), false);

    free(prior);
    buf_free(wm.rules);
    rule_effects_string_copy = saved_copy;
});

TEST_FUNC(rule_scratchpad_application_copy_failure_is_atomic,
{
    __auto_type saved_copy = window_manager_rule_scratchpad_copy;
    struct window_manager wm = {0};
    struct window window = { .id = 51, .scratchpad = NULL };
    char *label = string_copy("prior");
    buf_push(wm.scratchpad_window, ((struct scratchpad) { .label = label, .window = &window }));
    window.scratchpad = label;
    struct rule_effects effects = { .scratchpad = "next", .mff = RULE_PROP_ON };

    window_manager_rule_scratchpad_copy = window_manager_test_string_copy_failure;
    window_manager_apply_rule_effects_to_window(&g_space_manager, &wm, &window, &effects);
    TEST_CHECK(window.scratchpad, label);
    TEST_CHECK(buf_len(wm.scratchpad_window), 1);
    TEST_CHECK(wm.scratchpad_window[0].label, label);
    TEST_CHECK(window_check_rule_flag(&window, WINDOW_RULE_MFF), false);
    TEST_CHECK(window_manager_set_scratchpad_for_window(&wm, &window, NULL), false);
    TEST_CHECK(window.scratchpad, label);
    TEST_CHECK(buf_len(wm.scratchpad_window), 1);

    window_manager_rule_scratchpad_copy = saved_copy;
    free(label);
    buf_free(wm.scratchpad_window);
});

TEST_FUNC(window_manager_checked_publications_reject_bucket_failures,
{
    struct window_manager wm = {0};
    table_init(&wm.application, 1, hash_wm, compare_wm);
    table_init(&wm.window, 1, hash_wm, compare_wm);
    struct application application = { .pid = 91 };
    struct window window = { .id = 92 };

    table_test_fail_allocation_after(0);
    TEST_CHECK(window_manager_add_application(&wm, &application), false);
    TEST_CHECK(table_find(&wm.application, &application.pid) == NULL, true);
    table_test_fail_allocation_after(0);
    TEST_CHECK(window_manager_add_window(&wm, &window), false);
    TEST_CHECK(table_find(&wm.window, &window.id) == NULL, true);
    table_test_fail_allocation_after(-1);

    TEST_CHECK(window_manager_add_application(&wm, &application), true);
    TEST_CHECK(window_manager_add_window(&wm, &window), true);
    TEST_CHECK(table_find(&wm.application, &application.pid), &application);
    TEST_CHECK(table_find(&wm.window, &window.id), &window);
    table_free(&wm.application);
    table_free(&wm.window);
});

TEST_FUNC(window_management_rejects_zero_area_helpers_but_preserves_explicit_rules,
{
    struct application application = { .name = "helper" };
    struct window window = {
        .application = &application,
        .id = UINT32_C(0x7fffff80),
        .is_root = true,
        .frame = { { 10.0f, 20.0f }, { 0.0f, 100.0f } }
    };
    window_set_flag(&window, WINDOW_MOVABLE);

    TEST_CHECK(window_manager_should_manage_window(&window), false);
    window.frame.size.width = 100.0f;
    window.frame.size.height = 0.0f;
    TEST_CHECK(window_manager_should_manage_window(&window), false);

    window_set_rule_flag(&window, WINDOW_RULE_MANAGED);
    TEST_CHECK(window_manager_should_manage_window(&window), true);
    window_set_flag(&window, WINDOW_ORDERED_OUT);
    TEST_CHECK(window_manager_should_manage_window(&window), false);
});

TEST_FUNC(window_selector_area_comparison_preserves_fractional_and_zero_geometry,
{
    struct space_manager sm = {0};
    struct window_manager wm = {0};
    space_manager_test_active_space_id = 91;
    table_init(&sm.view, 4, hash_view, compare_view);
    table_init(&wm.window, 4, hash_wm, compare_wm);

    struct window first_window = { .id = 41 };
    struct window second_window = { .id = 42 };
    struct window_node first = {
        .window_list = {41}, .window_order = {41}, .window_count = 1,
        .area = {0, 0, 0.5f, 0.5f}
    };
    struct window_node second = {
        .window_list = {42}, .window_order = {42}, .window_count = 1,
        .area = {0, 0, 0.75f, 0.75f}
    };
    struct window_node root = { .left = &first, .right = &second };
    first.parent = &root;
    second.parent = &root;
    struct view view = { .sid = 91, .layout = VIEW_BSP, .root = &root };

    table_add(&sm.view, &view.sid, &view);
    table_add(&wm.window, &first_window.id, &first_window);
    table_add(&wm.window, &second_window.id, &second_window);

    TEST_CHECK(window_manager_find_largest_managed_window(&sm, &wm), &second_window);
    TEST_CHECK(window_manager_find_smallest_managed_window(&sm, &wm), &first_window);

    first.area.w = first.area.h = 0.0f;
    second.area.w = second.area.h = 0.0f;
    TEST_CHECK(window_manager_find_largest_managed_window(&sm, &wm), &first_window);
    TEST_CHECK(window_manager_find_smallest_managed_window(&sm, &wm), &second_window);

    first.area.w = first.area.h = 1000000.5f;
    second.area.w = second.area.h = 999999.5f;
    TEST_CHECK(window_manager_find_largest_managed_window(&sm, &wm), &first_window);
    TEST_CHECK(window_manager_find_smallest_managed_window(&sm, &wm), &second_window);

    table_free(&wm.window);
    table_free(&sm.view);
    space_manager_test_active_space_id = 0;
});

static int space_move_pair_test_calls;
static bool space_move_pair_test_results[3];
static bool space_move_pair_test_send(uint64_t sid, uint64_t after_sid, bool focus)
{
    (void) sid;
    (void) after_sid;
    (void) focus;
    return space_move_pair_test_results[space_move_pair_test_calls++];
}

TEST_FUNC(space_move_pair_stops_rolls_back_and_reports_partial_commit,
{
    space_manager_test_move_space_after_space = space_move_pair_test_send;
    bool partial_commit = false;

    space_move_pair_test_calls = 0;
    space_move_pair_test_results[0] = false;
    TEST_CHECK(space_manager_move_space_pair(1, 2, false, 3, 4, true, 5, &partial_commit), false);
    TEST_CHECK(space_move_pair_test_calls, 1);
    TEST_CHECK(partial_commit, false);

    space_move_pair_test_calls = 0;
    space_move_pair_test_results[0] = true;
    space_move_pair_test_results[1] = false;
    space_move_pair_test_results[2] = true;
    TEST_CHECK(space_manager_move_space_pair(1, 2, false, 3, 4, true, 5, &partial_commit), false);
    TEST_CHECK(space_move_pair_test_calls, 3);
    TEST_CHECK(partial_commit, false);

    space_move_pair_test_calls = 0;
    space_move_pair_test_results[0] = true;
    space_move_pair_test_results[1] = false;
    space_move_pair_test_results[2] = false;
    TEST_CHECK(space_manager_move_space_pair(1, 2, false, 3, 4, true, 5, &partial_commit), false);
    TEST_CHECK(space_move_pair_test_calls, 3);
    TEST_CHECK(partial_commit, true);

    space_move_pair_test_calls = 0;
    space_move_pair_test_results[0] = true;
    space_move_pair_test_results[1] = true;
    TEST_CHECK(space_manager_move_space_pair(1, 2, false, 3, 4, true, 5, &partial_commit), true);
    TEST_CHECK(space_move_pair_test_calls, 2);
    TEST_CHECK(partial_commit, false);

    space_manager_test_move_space_after_space = NULL;
});

static int window_move_transport_order[4];
static int window_move_transport_count;
static bool window_move_transport_result[4];

static bool window_move_transport_sa(uint64_t sid, uint32_t *windows, int count)
{
    (void) sid; (void) windows; (void) count;
    window_move_transport_order[window_move_transport_count] = 1;
    return window_move_transport_result[window_move_transport_count++];
}

static bool window_move_transport_bridge(uint64_t sid, uint32_t *windows, int count)
{
    (void) sid; (void) windows; (void) count;
    window_move_transport_order[window_move_transport_count] = 2;
    return window_move_transport_result[window_move_transport_count++];
}

static bool window_move_transport_fallback(uint64_t sid, uint32_t *windows, int count)
{
    (void) sid; (void) windows; (void) count;
    window_move_transport_order[window_move_transport_count] = 3;
    return window_move_transport_result[window_move_transport_count++];
}

TEST_FUNC(window_space_move_prefers_observable_sa_then_verified_fallbacks,
{
    struct space_manager_window_move_ops ops = {
        .scripting_addition = window_move_transport_sa,
        .bridged = window_move_transport_bridge,
        .fallback = window_move_transport_fallback
    };
    uint32_t windows[] = {41, 42};

    memset(window_move_transport_result, 0, sizeof(window_move_transport_result));
    window_move_transport_count = 0;
    window_move_transport_result[0] = true;
    TEST_CHECK(space_manager_move_window_list_with_ops(91, windows, 2, &ops), true);
    TEST_CHECK(window_move_transport_count, 1);
    TEST_CHECK(window_move_transport_order[0], 1);

    memset(window_move_transport_result, 0, sizeof(window_move_transport_result));
    window_move_transport_count = 0;
    window_move_transport_result[1] = true;
    TEST_CHECK(space_manager_move_window_list_with_ops(91, windows, 2, &ops), true);
    TEST_CHECK(window_move_transport_count, 2);
    TEST_CHECK(window_move_transport_order[0], 1);
    TEST_CHECK(window_move_transport_order[1], 2);

    memset(window_move_transport_result, 0, sizeof(window_move_transport_result));
    window_move_transport_count = 0;
    window_move_transport_result[2] = true;
    TEST_CHECK(space_manager_move_window_list_with_ops(91, windows, 2, &ops), true);
    TEST_CHECK(window_move_transport_count, 3);
    TEST_CHECK(window_move_transport_order[2], 3);

    memset(window_move_transport_result, 0, sizeof(window_move_transport_result));
    window_move_transport_count = 0;
    TEST_CHECK(space_manager_move_window_list_with_ops(91, windows, 2, &ops), false);
    TEST_CHECK(window_move_transport_count, 3);
    TEST_CHECK(space_manager_move_window_list_with_ops(0, windows, 2, &ops), false);
    TEST_CHECK(space_manager_move_window_list_with_ops(91, NULL, 2, &ops), false);
    TEST_CHECK(space_manager_move_window_list_with_ops(91, NULL, 0, &ops), true);
});

static bool node_space_move_results[8];
static uint64_t node_space_move_sids[8];
static uint32_t node_space_move_windows[8];
static int node_space_move_calls;

static bool node_space_move_test(uint64_t sid, struct window *window)
{
    int call = node_space_move_calls++;
    node_space_move_sids[call] = sid;
    node_space_move_windows[call] = window->id;
    return node_space_move_results[call];
}

TEST_FUNC(cross_space_node_move_rolls_back_before_tree_mutation,
{
    __auto_type saved_move = space_manager_test_move_window_to_space;
    struct window_manager wm = {0};
    table_init(&wm.window, 8, hash_wm, compare_wm);
    struct window first = { .id = 11 }, second = { .id = 21 };
    table_add(&wm.window, &first.id, &first);
    table_add(&wm.window, &second.id, &second);
    struct window_node source = { .window_list = {11}, .window_order = {11}, .window_count = 1 };
    struct window_node destination = { .window_list = {21}, .window_order = {21}, .window_count = 1 };
    space_manager_test_move_window_to_space = node_space_move_test;

    memset(node_space_move_results, 0, sizeof(node_space_move_results));
    node_space_move_calls = 0;
    TEST_CHECK(window_manager_move_node_windows_between_spaces(&wm, &source, 101, &destination, 202), false);
    TEST_CHECK(node_space_move_calls, 1);
    TEST_CHECK(source.window_list[0], 11U);
    TEST_CHECK(destination.window_list[0], 21U);

    memset(node_space_move_results, 0, sizeof(node_space_move_results));
    node_space_move_results[0] = true;
    node_space_move_results[1] = false;
    node_space_move_results[2] = true;
    node_space_move_calls = 0;
    TEST_CHECK(window_manager_move_node_windows_between_spaces(&wm, &source, 101, &destination, 202), false);
    TEST_CHECK(node_space_move_calls, 3);
    TEST_CHECK(node_space_move_sids[0], 202ULL);
    TEST_CHECK(node_space_move_sids[1], 101ULL);
    TEST_CHECK(node_space_move_sids[2], 101ULL);
    TEST_CHECK(node_space_move_windows[2], 11U);

    memset(node_space_move_results, 0, sizeof(node_space_move_results));
    node_space_move_results[0] = node_space_move_results[1] = true;
    node_space_move_calls = 0;
    TEST_CHECK(window_manager_move_node_windows_between_spaces(&wm, &source, 101, &destination, 202), true);
    TEST_CHECK(node_space_move_calls, 2);

    space_manager_test_move_window_to_space = saved_move;
    table_free(&wm.window);
});

static uint64_t window_manager_test_warp_space(uint32_t wid)
{
    return wid < 20 ? 1 : 2;
}

static int space_swap_test_inventory_calls;
static int space_swap_test_inventory_failure;

static uint32_t *space_swap_test_inventory(uint64_t *space_list, int space_count, int cid,
                                           int *count, bool include_minimized, bool *success)
{
    (void) space_list;
    (void) space_count;
    (void) cid;
    (void) include_minimized;
    static uint32_t partial_window_list[] = { 42 };
    ++space_swap_test_inventory_calls;
    if (space_swap_test_inventory_calls == space_swap_test_inventory_failure) {
        *count = 1;
        *success = false;
        return partial_window_list;
    }
    *count = 0;
    *success = true;
    return NULL;
}

static bool space_swap_test_space_is_visible(uint64_t sid, void *context)
{
    (void) sid;
    (void) context;
    return false;
}

static void space_swap_test_update(struct view *view, void *context)
{
    (void) view;
    (void) context;
}

TEST_FUNC(cross_display_space_swap_is_transactional,
{
    struct table saved_views = g_space_manager.view;
    float saved_animation_duration = g_window_manager.window_animation_duration;
    struct view_test_platform_ops ops = {
        .space_is_visible = space_swap_test_space_is_visible,
        .update_windows = space_swap_test_update
    };
    view_test_set_platform_ops(&ops);
    space_manager_test_swap_window_list = space_swap_test_inventory;

    for (int failed_inventory = 1; failed_inventory <= 2; ++failed_inventory) {
        memset(&g_space_manager.view, 0, sizeof(g_space_manager.view));
        table_init(&g_space_manager.view, 4, hash_view, compare_view);
        struct window_node a_root = {0};
        struct window_node b_root = {0};
        struct view a_view = { .sid = 101, .layout = VIEW_BSP, .root = &a_root };
        struct view b_view = { .sid = 202, .layout = VIEW_BSP, .root = &b_root };
        table_add(&g_space_manager.view, &a_view.sid, &a_view);
        table_add(&g_space_manager.view, &b_view.sid, &b_view);
        g_window_manager.window_animation_duration = 0.25f;
        space_swap_test_inventory_calls = 0;
        space_swap_test_inventory_failure = failed_inventory;

        TEST_CHECK(space_manager_swap_space_with_space_on_display(1, 101, 2, 202), SPACE_OP_ERROR_WINDOW_SERVER);
        TEST_CHECK(space_swap_test_inventory_calls, 2);
        TEST_CHECK(a_view.sid, 101ULL);
        TEST_CHECK(b_view.sid, 202ULL);
        TEST_CHECK(table_find(&g_space_manager.view, &a_view.sid), &a_view);
        TEST_CHECK(table_find(&g_space_manager.view, &b_view.sid), &b_view);
        TEST_CHECK(g_window_manager.window_animation_duration, 0.25f);
        table_free(&g_space_manager.view);
    }

    memset(&g_space_manager.view, 0, sizeof(g_space_manager.view));
    table_init(&g_space_manager.view, 4, hash_view, compare_view);
    struct window_node a_root = {0};
    struct window_node b_root = {0};
    struct view a_view = { .sid = 101, .layout = VIEW_BSP, .root = &a_root };
    struct view b_view = { .sid = 202, .layout = VIEW_BSP, .root = &b_root };
    uint64_t a_sid = a_view.sid;
    uint64_t b_sid = b_view.sid;
    table_add(&g_space_manager.view, &a_sid, &a_view);
    table_add(&g_space_manager.view, &b_sid, &b_view);
    g_window_manager.window_animation_duration = 0.25f;
    space_swap_test_inventory_calls = 0;
    space_swap_test_inventory_failure = 0;
    table_test_fail_allocation_after(0);

    TEST_CHECK(space_manager_swap_space_with_space_on_display(1, a_sid, 2, b_sid), SPACE_OP_ERROR_SUCCESS);
    TEST_CHECK(space_swap_test_inventory_calls, 2);
    TEST_CHECK(a_view.sid, b_sid);
    TEST_CHECK(b_view.sid, a_sid);
    TEST_CHECK(table_find(&g_space_manager.view, &a_sid), &b_view);
    TEST_CHECK(table_find(&g_space_manager.view, &b_sid), &a_view);
    TEST_CHECK(g_window_manager.window_animation_duration, 0.25f);

    table_test_fail_allocation_after(-1);
    table_free(&g_space_manager.view);
    space_manager_test_swap_window_list = NULL;
    view_test_set_platform_ops(NULL);
    g_space_manager.view = saved_views;
    g_window_manager.window_animation_duration = saved_animation_duration;
});

static bool window_manager_animation_test_reduce_motion;
static int window_manager_animation_test_async_calls;
static int window_manager_animation_test_async_windows;
static int window_manager_animation_test_direct_calls;

static struct accessibility_display_preferences window_manager_animation_test_preferences(void)
{
    return (struct accessibility_display_preferences) {
        .reduce_motion = window_manager_animation_test_reduce_motion
    };
}

static void window_manager_animation_test_async(struct window_capture *captures, int count)
{
    if (captures && count > 0) {
        ++window_manager_animation_test_async_calls;
        window_manager_animation_test_async_windows += count;
    }
}

static void window_manager_animation_test_direct(struct window *window, float x, float y,
                                                 float width, float height)
{
    (void) window;
    (void) x;
    (void) y;
    (void) width;
    (void) height;
    ++window_manager_animation_test_direct_calls;
}

TEST_FUNC(window_animation_policy_honors_reduce_motion_without_mutating_configuration,
{
    __auto_type saved_preferences = window_manager_animation_preferences;
    __auto_type saved_async = window_manager_animation_async_route;
    __auto_type saved_direct = window_manager_animation_direct_route;
    float saved_duration = g_window_manager.window_animation_duration;
    window_manager_animation_preferences = window_manager_animation_test_preferences;
    window_manager_animation_async_route = window_manager_animation_test_async;
    window_manager_animation_direct_route = window_manager_animation_test_direct;

    struct window first = { .id = 1 };
    struct window second = { .id = 2 };
    struct window_capture captures[] = {
        { .window = &first, .x = 1, .y = 2, .w = 3, .h = 4 },
        { .window = &second, .x = 5, .y = 6, .w = 7, .h = 8 }
    };

    window_manager_animation_test_reduce_motion = false;
    window_manager_animation_test_async_calls = 0;
    window_manager_animation_test_async_windows = 0;
    window_manager_animation_test_direct_calls = 0;
    g_window_manager.window_animation_duration = 0.0f;
    window_manager_animate_window_list(captures, 2);
    TEST_CHECK(window_manager_animation_test_async_calls, 0);
    TEST_CHECK(window_manager_animation_test_direct_calls, 2);

    window_manager_animation_test_direct_calls = 0;
    g_window_manager.window_animation_duration = 0.25f;
    window_manager_animate_window_list(captures, 2);
    TEST_CHECK(window_manager_animation_test_async_calls, 1);
    TEST_CHECK(window_manager_animation_test_async_windows, 2);
    TEST_CHECK(window_manager_animation_test_direct_calls, 0);

    window_manager_animation_test_reduce_motion = true;
    window_manager_animate_window_list(captures, 2);
    TEST_CHECK(window_manager_animation_test_async_calls, 1);
    TEST_CHECK(window_manager_animation_test_direct_calls, 2);
    TEST_CHECK(g_window_manager.window_animation_duration, 0.25f);

    window_manager_animation_test_reduce_motion = false;
    window_manager_animate_window(captures[0]);
    TEST_CHECK(window_manager_animation_test_async_calls, 2);
    TEST_CHECK(window_manager_animation_test_async_windows, 3);
    TEST_CHECK(window_manager_animation_test_direct_calls, 2);

    window_manager_animation_test_reduce_motion = true;
    window_manager_animate_window(captures[0]);
    TEST_CHECK(window_manager_animation_test_async_calls, 2);
    TEST_CHECK(window_manager_animation_test_direct_calls, 3);
    TEST_CHECK(g_window_manager.window_animation_duration, 0.25f);

    g_window_manager.window_animation_duration = saved_duration;
    window_manager_animation_preferences = saved_preferences;
    window_manager_animation_async_route = saved_async;
    window_manager_animation_direct_route = saved_direct;
});

TEST_FUNC(managed_tiling_reserves_publication_before_tree_mutation,
{
    struct space_manager sm = {0};
    struct window_manager wm = {0};
    table_init(&sm.view, 4, hash_view, compare_view);
    table_init(&wm.managed_window, 1, hash_wm, compare_wm);
    wm.managed_window.max_load = 0.75f;
    struct window_node root = {
        .window_list = {41}, .window_order = {41}, .window_count = 1,
        .ratio = 0.37f, .split = SPLIT_X, .child = CHILD_FIRST,
        .insert_dir = DIR_EAST, .area = {3,4,500,300}
    };
    struct view view = { .sid = 91, .layout = VIEW_BSP, .root = &root,
                         .insertion_point = 41, .auto_balance = SPLIT_Y };
    struct window window = { .id = 42 };
    table_add(&sm.view, &view.sid, &view);
    struct window_node root_before = root;
    struct view view_before = view;
    for (int allowed = 0; allowed <= 2; allowed += 2) {
        table_test_fail_allocation_after(allowed);
        TEST_CHECK(window_manager_tile_window_on_space(&sm, &wm, &window, view.sid, 41, true), MANAGED_WINDOW_FAILED);
        TEST_CHECK(memcmp(&root, &root_before, sizeof(root)), 0);
        TEST_CHECK(memcmp(&view, &view_before, sizeof(view)), 0);
        TEST_CHECK(table_find(&wm.managed_window, &window.id) == NULL, true);
    }
    table_test_fail_allocation_after(-1);

    view.layout = VIEW_FLOAT;
    TEST_CHECK(window_manager_tile_window_on_space(&sm, &wm, &window, view.sid, 0, true), MANAGED_WINDOW_UNMANAGED);
    TEST_CHECK(table_find(&wm.managed_window, &window.id) == NULL, true);
    TEST_CHECK(memcmp(&root, &root_before, sizeof(root)), 0);
    table_free(&wm.managed_window); table_free(&sm.view);
});

TEST_FUNC(window_reconciliation_prepares_destination_before_detaching_source,
{
    struct window *saved_dragged_window = g_mouse_state.window;
    struct window_manager wm = { .purify_mode = PURIFY_DISABLED };
    table_init(&wm.window, 4, hash_wm, compare_wm);
    table_init(&wm.managed_window, 4, hash_wm, compare_wm);

    struct application application = {0};
    struct window window = {
        .application = &application, .id = 42, .is_root = true,
        .rule_flags = WINDOW_RULE_MANAGED, .layer = LAYER_NORMAL
    };
    struct window_node src_root = {
        .window_list = {42}, .window_order = {42}, .window_count = 1,
        .ratio = 0.37f, .split = SPLIT_X, .child = CHILD_FIRST,
        .area = {3, 4, 500, 300}
    };
    struct window_node dst_root = {
        .window_list = {77}, .window_order = {77}, .window_count = 1,
        .ratio = 0.63f, .split = SPLIT_Y, .child = CHILD_SECOND,
        .area = {8, 9, 700, 400}
    };
    struct view src = { .sid = 101, .layout = VIEW_BSP, .root = &src_root, .insertion_point = 42 };
    struct view dst = { .sid = 202, .layout = VIEW_BSP, .root = &dst_root, .insertion_point = 77 };
    table_add(&wm.window, &window.id, &window);
    table_add(&wm.managed_window, &window.id, &src);

    struct window_node src_before = src_root, dst_before = dst_root;
    struct view src_view_before = src, dst_view_before = dst;
    uint32_t window_list[] = {42};
    struct view *mutated_views[2];
    struct view_test_platform_ops ops = {
        .space_is_visible = space_swap_test_space_is_visible,
        .update_windows = space_swap_test_update
    };
    view_test_set_platform_ops(&ops);

    g_mouse_state.window = &window;
    TEST_CHECK(window_manager_reconcile_windows_on_space_with_storage(&wm, &dst, window_list, 1, true,
                                                                      mutated_views, 2), true);
    TEST_CHECK(view_find_window_node(&src, window.id) != NULL, true);
    TEST_CHECK(view_find_window_node(&dst, window.id) == NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&wm, &window) == &src, true);
    g_mouse_state.window = NULL;

    for (size_t ordinal = 1; ordinal <= 2; ++ordinal) {
        legacy_allocation_test_fail_at(ordinal);
        TEST_CHECK(window_manager_reconcile_windows_on_space_with_storage(&wm, &dst, window_list, 1, true,
                                                                          mutated_views, 2), false);
        TEST_CHECK(memcmp(&src_root, &src_before, sizeof(src_root)), 0);
        TEST_CHECK(memcmp(&dst_root, &dst_before, sizeof(dst_root)), 0);
        TEST_CHECK(memcmp(&src, &src_view_before, sizeof(src)), 0);
        TEST_CHECK(memcmp(&dst, &dst_view_before, sizeof(dst)), 0);
        TEST_CHECK(window_manager_find_managed_window(&wm, &window) == &src, true);
        TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    }

    legacy_allocation_test_reset();
    TEST_CHECK(window_manager_reconcile_windows_on_space_with_storage(&wm, &dst, window_list, 1, true,
                                                                      mutated_views, 2), true);
    TEST_CHECK(view_find_window_node(&src, window.id) == NULL, true);
    TEST_CHECK(view_find_window_node(&dst, window.id) != NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&wm, &window) == &dst, true);
    view_remove_window_node(&dst, &window);
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);

    view_test_set_platform_ops(NULL);
    table_free(&wm.managed_window);
    table_free(&wm.window);
    g_mouse_state.window = saved_dragged_window;
});

static uint64_t transient_fullscreen_test_space(uint32_t wid)
{
    (void) wid;
    return 9001;
}

TEST_FUNC(window_reconciliation_preserves_transient_fullscreen_exit_membership,
{
    __auto_type saved_space = window_test_space_for_id;
    struct window_manager wm = { .purify_mode = PURIFY_DISABLED };
    table_init(&wm.window, 4, hash_wm, compare_wm);
    table_init(&wm.managed_window, 4, hash_wm, compare_wm);
    struct application application = { .name = "fullscreen-exit" };
    struct window window = {
        .application = &application,
        .id = 91,
        .is_root = true,
        .frame = { { 10.0f, 20.0f }, { 800.0f, 600.0f } }
    };
    window_set_flag(&window, WINDOW_MOVABLE);
    window_set_rule_flag(&window, WINDOW_RULE_MANAGED);
    struct window_node root = {
        .window_list = {91}, .window_order = {91}, .window_count = 1,
        .area = {10.0f, 20.0f, 800.0f, 600.0f}
    };
    struct view view = { .sid = 9001, .layout = VIEW_BSP, .root = &root };
    struct view *mutated_views[1];
    table_add(&wm.window, &window.id, &window);
    table_add(&wm.managed_window, &window.id, &view);
    window_test_space_for_id = transient_fullscreen_test_space;

    TEST_CHECK(window_manager_reconcile_windows_on_space_with_storage(&wm, &view, NULL, 0, true,
                                                                      mutated_views, 1), true);
    TEST_CHECK(view_find_window_node(&view, window.id), &root);
    TEST_CHECK(window_manager_find_managed_window(&wm, &window), &view);
    TEST_CHECK(view_is_dirty(&view), false);

    window_test_space_for_id = saved_space;
    table_free(&wm.managed_window);
    table_free(&wm.window);
});

TEST_FUNC(mouse_no_target_cross_space_transfer_prepares_before_source_mutation,
{
    struct space_manager sm = {0};
    struct window_manager wm = { .purify_mode = PURIFY_DISABLED };
    table_init(&wm.managed_window, 4, hash_wm, compare_wm);

    struct window window = { .id = 42 };
    struct window_node src_root = {
        .window_list = {42}, .window_order = {42}, .window_count = 1,
        .ratio = 0.37f, .split = SPLIT_X, .child = CHILD_FIRST,
        .area = {3, 4, 500, 300}
    };
    struct window_node dst_root = {
        .window_list = {77}, .window_order = {77}, .window_count = 1,
        .ratio = 0.63f, .split = SPLIT_Y, .child = CHILD_SECOND,
        .area = {8, 9, 700, 400}
    };
    struct view src = { .sid = 101, .layout = VIEW_BSP, .root = &src_root, .insertion_point = 42 };
    struct view dst = { .sid = 202, .layout = VIEW_BSP, .root = &dst_root, .insertion_point = 77 };
    table_add(&wm.managed_window, &window.id, &src);

    struct window_node src_before = src_root, dst_before = dst_root;
    struct view src_view_before = src, dst_view_before = dst;
    for (size_t ordinal = 1; ordinal <= 2; ++ordinal) {
        legacy_allocation_test_fail_at(ordinal);
        mouse_drop_no_target(&sm, &wm, &src, &dst, &window, &src_root);
        TEST_CHECK(memcmp(&src_root, &src_before, sizeof(src_root)), 0);
        TEST_CHECK(memcmp(&dst_root, &dst_before, sizeof(dst_root)), 0);
        TEST_CHECK(memcmp(&src, &src_view_before, sizeof(src)), 0);
        TEST_CHECK(memcmp(&dst, &dst_view_before, sizeof(dst)), 0);
        TEST_CHECK(window_manager_find_managed_window(&wm, &window) == &src, true);
        TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    }

    legacy_allocation_test_reset();
    mouse_drop_no_target(&sm, &wm, &src, &dst, &window, &src_root);
    TEST_CHECK(view_find_window_node(&src, window.id) == NULL, true);
    TEST_CHECK(view_find_window_node(&dst, window.id) != NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&wm, &window) == &dst, true);
    view_remove_window_node(&dst, &window);
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    table_free(&wm.managed_window);
});

TEST_FUNC(window_manager_warp_preparation_failure_preserves_trees,
{
    struct space_manager sm = {0};
    struct window_manager wm = {0};
    table_init(&sm.view, 4, hash_view, compare_view);
    table_init(&wm.managed_window, 4, hash_wm, compare_wm);
    window_test_space_for_id = window_manager_test_warp_space;

    struct window a = { .id = 11 };
    struct window b = { .id = 12 };
    struct window_node root = { .split = SPLIT_Y, .ratio = 0.5f, .area = { 1, 2, 300, 200 } };
    struct window_node left = { .parent = &root, .window_list = {11}, .window_order = {11}, .window_count = 1 };
    struct window_node right = { .parent = &root, .window_list = {12}, .window_order = {12}, .window_count = 1 };
    root.left = &left; root.right = &right;
    struct view view = { .sid = 1, .layout = VIEW_BSP, .root = &root, .insertion_point = 12 };
    table_add(&sm.view, &view.sid, &view);
    table_add(&wm.managed_window, &a.id, &view);
    struct window_node root_before = root, left_before = left, right_before = right;
    struct view view_before = view;
    for (size_t ordinal = 1; ordinal <= 2; ++ordinal) {
        legacy_allocation_test_fail_at(ordinal);
        TEST_CHECK(window_manager_warp_window(&sm, &wm, &a, &b), WINDOW_OP_ERROR_INVALID_OPERATION);
        TEST_CHECK(memcmp(&root, &root_before, sizeof(root)), 0);
        TEST_CHECK(memcmp(&left, &left_before, sizeof(left)), 0);
        TEST_CHECK(memcmp(&right, &right_before, sizeof(right)), 0);
        TEST_CHECK(memcmp(&view, &view_before, sizeof(view)), 0);
        TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    }

    table_remove(&sm.view, &view.sid);
    table_remove(&wm.managed_window, &a.id);
    b.id = 21;
    struct window_node a_root = { .window_list = {11}, .window_order = {11}, .window_count = 1, .area = {1,2,100,100} };
    struct window_node b_root = { .window_list = {21}, .window_order = {21}, .window_count = 1, .area = {3,4,100,100} };
    struct view a_view = { .sid = 1, .layout = VIEW_BSP, .root = &a_root };
    struct view b_view = { .sid = 2, .layout = VIEW_BSP, .root = &b_root };
    table_add(&sm.view, &a_view.sid, &a_view); table_add(&sm.view, &b_view.sid, &b_view);
    table_add(&wm.managed_window, &a.id, &a_view);
    struct window_node a_before = a_root, b_before = b_root;
    for (size_t ordinal = 1; ordinal <= 2; ++ordinal) {
        legacy_allocation_test_fail_at(ordinal);
        TEST_CHECK(window_manager_warp_window(&sm, &wm, &a, &b), WINDOW_OP_ERROR_INVALID_OPERATION);
        TEST_CHECK(memcmp(&a_root, &a_before, sizeof(a_root)), 0);
        TEST_CHECK(memcmp(&b_root, &b_before, sizeof(b_root)), 0);
        TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    }
    legacy_allocation_test_reset();
    window_test_space_for_id = NULL;
    table_free(&wm.managed_window); table_free(&sm.view);
});

static int transfer_test_layer_calls;
static uint32_t transfer_test_layer_wid;
static int transfer_test_layer_value;
static int transfer_test_update_calls;
static int transfer_test_flush_calls;
static int transfer_test_src_flush_calls;
static int transfer_test_dst_flush_calls;
static int transfer_test_node_flush_calls;
static bool transfer_test_visible;
static struct window_node *transfer_test_src_root;
static struct window_node *transfer_test_dst_root;

static void transfer_test_observe_layer(uint32_t wid, int layer)
{
    ++transfer_test_layer_calls;
    transfer_test_layer_wid = wid;
    transfer_test_layer_value = layer;
}

static bool transfer_test_space_visible(uint64_t sid, void *context)
{
    (void) sid; (void) context;
    return transfer_test_visible;
}

static void transfer_test_update(struct view *view, void *context)
{
    (void) view; (void) context;
    ++transfer_test_update_calls;
}

static void transfer_test_flush(struct window_node *root, void *context)
{
    (void) context;
    ++transfer_test_flush_calls;
    if (root == transfer_test_src_root) ++transfer_test_src_flush_calls;
    if (root == transfer_test_dst_root) ++transfer_test_dst_flush_calls;
}

static void transfer_test_observe_node_flush(struct window_node *node, void *context)
{
    (void) context;
    ++transfer_test_node_flush_calls;
    if (node == transfer_test_src_root) ++transfer_test_src_flush_calls;
    if (node == transfer_test_dst_root) ++transfer_test_dst_flush_calls;
}

TEST_FUNC(single_root_transfer_capacity_is_atomic_and_max_minus_one_commits,
{
    const enum view_type layouts[] = {
        VIEW_STACK, VIEW_ACCORDION_HORIZONTAL, VIEW_ACCORDION_VERTICAL
    };
    struct table saved_managed = g_window_manager.managed_window;
    enum purify_mode saved_purify = g_window_manager.purify_mode;
    uint64_t saved_generation = declarative_installed.generation;
    bool saved_scaffold = declarative_installed.has_scaffold;
    __auto_type saved_layer_observer = window_manager_test_layer_observer;
    struct view_test_platform_ops ops = {
        .space_is_visible = transfer_test_space_visible,
        .update_windows = transfer_test_update,
        .flush_windows = transfer_test_flush,
        .node_flush_observer = transfer_test_observe_node_flush
    };
    view_test_set_platform_ops(&ops);
    memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window));
    table_init(&g_window_manager.managed_window, 64, hash_wm, compare_wm);
    g_window_manager.purify_mode = PURIFY_DISABLED;
    declarative_installed.generation = 1;
    declarative_installed.has_scaffold = true;
    window_manager_test_layer_observer = transfer_test_observe_layer;

    for (size_t layout_index = 0; layout_index < array_count(layouts); ++layout_index) {
        for (int visible = 0; visible <= 1; ++visible) {
            transfer_test_visible = visible != 0;
            struct window window = { .id = (uint32_t) (500 + layout_index * 2 + visible), .layer = LAYER_AUTO };
        struct window_node src_root = {
            .window_list = { window.id }, .window_order = { window.id }, .window_count = 1,
            .ratio = 0.37f, .split = SPLIT_X, .child = CHILD_FIRST,
            .area = { 3, 4, 500, 300 }
        };
        struct window_node dst_root = {
            .window_count = NODE_MAX_WINDOW_COUNT,
            .ratio = 0.63f, .split = SPLIT_Y, .child = CHILD_SECOND,
            .area = { 8, 9, 700, 400 }, .has_window_areas = true
        };
        for (int i = 0; i < NODE_MAX_WINDOW_COUNT; ++i) {
            dst_root.window_list[i] = (uint32_t) (1000 + layout_index * 100 + i);
            dst_root.window_order[i] = dst_root.window_list[NODE_MAX_WINDOW_COUNT - 1 - i];
            dst_root.window_area[i] = (struct area) { i, i + 1, i + 2, i + 3 };
        }
        struct view src = { .sid = 101 + layout_index, .layout = VIEW_BSP, .root = &src_root,
                            .insertion_point = window.id, .window_gap = 7 };
        struct view dst = { .sid = 201 + layout_index, .layout = layouts[layout_index], .root = &dst_root,
                            .insertion_point = dst_root.window_list[0], .window_gap = 11 };
        table_add(&g_window_manager.managed_window, &window.id, &src);
        transfer_test_src_root = &src_root;
        transfer_test_dst_root = &dst_root;
        struct window_node src_before = src_root, dst_before = dst_root;
        struct view src_view_before = src, dst_view_before = dst;
        int layer_before = window.layer;
        uint32_t src_flags_before = src.flags, dst_flags_before = dst.flags;
        transfer_test_layer_calls = transfer_test_update_calls = transfer_test_flush_calls = 0;
        transfer_test_src_flush_calls = transfer_test_dst_flush_calls = 0;
        transfer_test_node_flush_calls = 0;
        declarative_reconcile_test_reservation_count = 0;
        declarative_configuration_reconcile_shutdown();

        TEST_CHECK(window_manager_transfer_managed_window(&g_window_manager, &window, &src, &dst, 0), false);
        TEST_CHECK(memcmp(&src_root, &src_before, sizeof(src_root)), 0);
        TEST_CHECK(memcmp(&dst_root, &dst_before, sizeof(dst_root)), 0);
        TEST_CHECK(memcmp(&src, &src_view_before, sizeof(src)), 0);
        TEST_CHECK(memcmp(&dst, &dst_view_before, sizeof(dst)), 0);
        TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &window), &src);
        TEST_CHECK(window.layer, layer_before);
        TEST_CHECK(src.flags, src_flags_before);
        TEST_CHECK(dst.flags, dst_flags_before);
        TEST_CHECK(transfer_test_layer_calls, 0);
        TEST_CHECK(transfer_test_update_calls, 0);
        TEST_CHECK(transfer_test_flush_calls, 0);
        TEST_CHECK(transfer_test_node_flush_calls, 0);
        TEST_CHECK(view_is_dirty(&src), false);
        TEST_CHECK(view_is_dirty(&dst), false);
        TEST_CHECK(declarative_reconcile_test_reservation_count, 0ULL);

        dst_root.window_count = NODE_MAX_WINDOW_COUNT - 1;
        dst_before = dst_root;
        transfer_test_layer_calls = transfer_test_update_calls = transfer_test_flush_calls = 0;
        transfer_test_src_flush_calls = transfer_test_dst_flush_calls = 0;
        transfer_test_node_flush_calls = 0;
        declarative_reconcile_test_reservation_count = 0;
        declarative_configuration_reconcile_shutdown();
        TEST_CHECK(window_manager_transfer_managed_window(&g_window_manager, &window, &src, &dst, 0), true);
        TEST_CHECK(view_find_window_node(&src, window.id) == NULL, true);
        TEST_CHECK(view_find_window_node(&dst, window.id), &dst_root);
        TEST_CHECK(dst_root.window_count, NODE_MAX_WINDOW_COUNT);
        TEST_CHECK(dst_root.window_list[NODE_MAX_WINDOW_COUNT - 1], window.id);
        TEST_CHECK(dst_root.window_order[0], window.id);
        TEST_CHECK(memcmp(dst_root.window_list, dst_before.window_list,
                          sizeof(uint32_t) * (NODE_MAX_WINDOW_COUNT - 1)), 0);
        TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &window), &dst);
        TEST_CHECK(transfer_test_layer_calls, 1);
        TEST_CHECK(transfer_test_layer_wid, window.id);
        TEST_CHECK(transfer_test_layer_value, LAYER_BELOW);
        int accordion = view_layout_is_accordion(dst.layout) ? 1 : 0;
        TEST_CHECK(transfer_test_update_calls, 1 + accordion);
        TEST_CHECK(transfer_test_flush_calls, visible ? 1 + accordion : 0);
        TEST_CHECK(transfer_test_node_flush_calls, visible && !accordion ? 1 : 0);
        TEST_CHECK(transfer_test_src_flush_calls, visible ? 1 : 0);
        TEST_CHECK(transfer_test_dst_flush_calls, visible ? 1 : 0);
        TEST_CHECK(view_is_dirty(&src), !visible);
        TEST_CHECK(view_is_dirty(&dst), !visible);
        TEST_CHECK(declarative_reconcile_test_reservation_count, 1ULL);
        table_remove(&g_window_manager.managed_window, &window.id);
        declarative_configuration_reconcile_shutdown();
        }
    }

    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed;
    g_window_manager.purify_mode = saved_purify;
    declarative_installed.generation = saved_generation;
    declarative_installed.has_scaffold = saved_scaffold;
    window_manager_test_layer_observer = saved_layer_observer;
    view_test_set_platform_ops(NULL);
});

static int warp_success_animation_calls;
static int warp_success_animation_windows;
static int warp_success_move_calls;
static uint64_t warp_success_move_sid;

static void warp_success_animate(struct window_capture *list, int count)
{
    (void) list;
    ++warp_success_animation_calls;
    warp_success_animation_windows += count;
}

static bool warp_success_move(uint64_t sid, struct window *window)
{
    (void) window;
    ++warp_success_move_calls;
    warp_success_move_sid = sid;
    return true;
}

static bool warp_success_feedback_show(struct window_node *node, void *context)
{
    (void) node; (void) context;
    return true;
}

static bool warp_success_feedback_destroy(struct window_node *node, void *context)
{
    (void) context;
    if (node->feedback_window.id) {
        table_remove(&g_window_manager.insert_feedback, &node->window_order[0]);
        node->feedback_window = (struct feedback_window) {0};
    }
    return true;
}

static bool warp_success_order(uint32_t wid, int order, uint32_t relative)
{
    (void) wid; (void) order; (void) relative;
    return true;
}

static struct window_node *warp_success_leaf(uint32_t wid, struct area area)
{
    struct window_node *node = legacy_allocation_calloc(1, sizeof(*node));
    node->window_list[0] = node->window_order[0] = wid;
    node->window_count = 1;
    node->area = area;
    node->ratio = 0.5f;
    return node;
}

TEST_FUNC(window_manager_warp_success_state_and_reconcile_matrix,
{
    struct table saved_views = g_space_manager.view;
    struct table saved_windows = g_window_manager.window;
    struct table saved_managed = g_window_manager.managed_window;
    struct table saved_feedback = g_window_manager.insert_feedback;
    struct event_loop saved_event_loop = g_event_loop;
    enum purify_mode saved_purify = g_window_manager.purify_mode;
    enum window_node_child saved_placement = g_space_manager.window_placement;
    uint64_t saved_generation = declarative_installed.generation;
    bool saved_scaffold = declarative_installed.has_scaffold;
    __auto_type saved_animation = window_manager_test_animate_observer;
    __auto_type saved_move = space_manager_test_move_window_to_space;
    __auto_type saved_layer = window_manager_test_layer_observer;
    __auto_type saved_order = window_manager_test_order_window;
    struct view_test_platform_ops ops = {
        .space_is_visible = transfer_test_space_visible,
        .update_windows = transfer_test_update,
        .flush_windows = transfer_test_flush,
        .node_flush_observer = transfer_test_observe_node_flush,
        .feedback_show_observer = warp_success_feedback_show,
        .feedback_destroy_observer = warp_success_feedback_destroy
    };

    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 16), true);
    char reconcile_semaphore_name[64];
    snprintf(reconcile_semaphore_name, sizeof(reconcile_semaphore_name), "/yabai_warp_reconcile_%d", getpid());
    g_event_loop.semaphore = sem_open(reconcile_semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(reconcile_semaphore_name);
    TEST_CHECK(g_event_loop.semaphore != SEM_FAILED, true);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);

    g_space_manager.view = (struct table) {0};
    g_window_manager.window = (struct table) {0};
    g_window_manager.managed_window = (struct table) {0};
    g_window_manager.insert_feedback = (struct table) {0};
    table_init(&g_space_manager.view, 8, hash_view, compare_view);
    table_init(&g_window_manager.window, 16, hash_wm, compare_wm);
    table_init(&g_window_manager.managed_window, 16, hash_wm, compare_wm);
    table_init(&g_window_manager.insert_feedback, 8, hash_wm, compare_wm);
    g_window_manager.purify_mode = PURIFY_DISABLED;
    g_space_manager.window_placement = CHILD_FIRST;
    declarative_installed.generation = 1;
    declarative_installed.has_scaffold = true;
    window_manager_test_animate_observer = warp_success_animate;
    space_manager_test_move_window_to_space = warp_success_move;
    window_manager_test_layer_observer = transfer_test_observe_layer;
    window_manager_test_order_window = warp_success_order;
    window_test_space_for_id = window_manager_test_warp_space;
    transfer_test_visible = true;
    view_test_set_platform_ops(&ops);

    struct application application = { .pid = 99 };
    struct window windows[] = {
        { .id = 11, .application = &application, .layer = LAYER_AUTO },
        { .id = 12, .application = &application, .layer = LAYER_AUTO },
        { .id = 13, .application = &application, .layer = LAYER_AUTO },
        { .id = 21, .application = &application, .layer = LAYER_AUTO },
        { .id = 22, .application = &application, .layer = LAYER_AUTO }
    };
    for (size_t i = 0; i < array_count(windows); ++i) {
        table_add(&g_window_manager.window, &windows[i].id, &windows[i]);
    }

    /* Stack reconcile matrix: same-view zero, cross-view one, and validation failure zero. */
    struct window_node *stack_root = legacy_allocation_calloc(1, sizeof(*stack_root));
    struct window_node *stack_a = warp_success_leaf(11, (struct area) { 0, 0, 400, 300 });
    struct window_node *stack_b = warp_success_leaf(12, (struct area) { 400, 0, 400, 300 });
    *stack_root = (struct window_node) { .left = stack_a, .right = stack_b,
        .split = SPLIT_Y, .ratio = 0.5f, .area = { 0, 0, 800, 300 } };
    stack_a->parent = stack_b->parent = stack_root;
    struct view stack_view = { .sid = 1, .layout = VIEW_BSP, .root = stack_root };
    table_add(&g_window_manager.managed_window, &windows[0].id, &stack_view);
    table_add(&g_window_manager.managed_window, &windows[1].id, &stack_view);
    declarative_reconcile_test_reservation_count = 0;
    declarative_configuration_reconcile_shutdown();
    TEST_CHECK(window_manager_stack_window(&g_space_manager, &g_window_manager, &windows[0], &windows[0]), WINDOW_OP_ERROR_SAME_WINDOW);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 0ULL);
    TEST_CHECK(window_manager_stack_window(&g_space_manager, &g_window_manager, &windows[0], &windows[1]), WINDOW_OP_ERROR_SUCCESS);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 0ULL);
    table_remove(&g_window_manager.managed_window, &windows[0].id);
    table_remove(&g_window_manager.managed_window, &windows[1].id);
    window_node_destroy(stack_root);

    struct window_node *stack_cross_a = warp_success_leaf(11, (struct area) { 0, 0, 400, 300 });
    struct window_node *stack_cross_b = warp_success_leaf(21, (struct area) { 400, 0, 400, 300 });
    struct view stack_cross_a_view = { .sid = 1, .layout = VIEW_BSP, .root = stack_cross_a };
    struct view stack_cross_b_view = { .sid = 2, .layout = VIEW_BSP, .root = stack_cross_b };
    table_add(&g_window_manager.managed_window, &windows[0].id, &stack_cross_a_view);
    table_add(&g_window_manager.managed_window, &windows[3].id, &stack_cross_b_view);
    declarative_reconcile_test_reservation_count = 0;
    declarative_configuration_reconcile_shutdown();
    TEST_CHECK(window_manager_stack_window(&g_space_manager, &g_window_manager, &windows[0], &windows[3]), WINDOW_OP_ERROR_SUCCESS);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 1ULL);
    table_remove(&g_window_manager.managed_window, &windows[0].id);
    table_remove(&g_window_manager.managed_window, &windows[3].id);
    window_node_destroy(stack_cross_a);
    window_node_destroy(stack_cross_b);
    declarative_configuration_reconcile_shutdown();

    /* Sibling collapse: the surviving sibling is promoted and the source is stacked into it. */
    struct window_node *sibling_root = legacy_allocation_calloc(1, sizeof(*sibling_root));
    struct window_node *sibling_a = warp_success_leaf(11, (struct area) { 0, 0, 400, 300 });
    struct window_node *sibling_b = warp_success_leaf(12, (struct area) { 400, 0, 400, 300 });
    *sibling_root = (struct window_node) { .left = sibling_a, .right = sibling_b,
        .split = SPLIT_Y, .child = CHILD_FIRST, .ratio = 0.37f, .area = { 0, 0, 800, 300 } };
    sibling_a->parent = sibling_b->parent = sibling_root;
    sibling_b->split = SPLIT_X;
    sibling_b->child = CHILD_SECOND;
    sibling_b->insert_dir = STACK;
    sibling_b->feedback_window = (struct feedback_window) { .id = 909, .context = (CGContextRef) (uintptr_t) 1 };
    g_mouse_state.feedback_node = sibling_b;
    table_add(&g_window_manager.insert_feedback, &sibling_b->window_order[0], sibling_b);
    struct view sibling_view = { .sid = 1, .layout = VIEW_BSP, .root = sibling_root, .insertion_point = 12 };
    table_add(&g_space_manager.view, &sibling_view.sid, &sibling_view);
    table_add(&g_window_manager.managed_window, &windows[0].id, &sibling_view);
    table_add(&g_window_manager.managed_window, &windows[1].id, &sibling_view);
    warp_success_animation_calls = warp_success_animation_windows = 0;
    transfer_test_layer_calls = transfer_test_update_calls = transfer_test_flush_calls = 0;
    transfer_test_node_flush_calls = warp_success_move_calls = 0;
    declarative_reconcile_test_reservation_count = 0;
    declarative_configuration_reconcile_shutdown();
    TEST_CHECK(window_manager_warp_window(&g_space_manager, &g_window_manager, &windows[0], &windows[1]), WINDOW_OP_ERROR_SUCCESS);
    TEST_CHECK(sibling_root->left == NULL && sibling_root->right == NULL, true);
    TEST_CHECK(sibling_root->window_count, 2);
    TEST_CHECK(sibling_root->window_list[0], 12U);
    TEST_CHECK(sibling_root->window_list[1], 11U);
    TEST_CHECK(sibling_root->window_order[0], 11U);
    TEST_CHECK(sibling_root->window_order[1], 12U);
    TEST_CHECK(sibling_root->split, SPLIT_X);
    TEST_CHECK(sibling_root->child, CHILD_SECOND);
    TEST_CHECK(sibling_root->ratio == 0.37f, true);
    TEST_CHECK(memcmp(&sibling_root->area, &(struct area) { 0, 0, 800, 300 }, sizeof(struct area)), 0);
    TEST_CHECK(sibling_root->zoom == NULL, true);
    TEST_CHECK(sibling_root->feedback_window.id, 0U);
    TEST_CHECK(sibling_root->feedback_window.context, NULL);
    TEST_CHECK(g_mouse_state.feedback_node, NULL);
    uint32_t old_feedback_key = 12, new_feedback_key = 11;
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &old_feedback_key) == NULL, true);
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &new_feedback_key) == NULL, true);
    TEST_CHECK(sibling_view.insertion_point, 12U);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &windows[0]), &sibling_view);
    TEST_CHECK(warp_success_animation_calls, 1);
    TEST_CHECK(warp_success_animation_windows, 2);
    TEST_CHECK(transfer_test_node_flush_calls, 0);
    TEST_CHECK(view_is_dirty(&sibling_view), false);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 0ULL);
    table_remove(&g_space_manager.view, &sibling_view.sid);
    table_remove(&g_window_manager.managed_window, &windows[0].id);
    table_remove(&g_window_manager.managed_window, &windows[1].id);
    window_node_destroy(sibling_root);

    /* General same-Space commit preserves membership and remains reconcile-free. */
    struct window_node *general_root = legacy_allocation_calloc(1, sizeof(*general_root));
    struct window_node *general_a = warp_success_leaf(11, (struct area) { 0, 0, 300, 300 });
    struct window_node *general_branch = legacy_allocation_calloc(1, sizeof(*general_branch));
    struct window_node *general_b = warp_success_leaf(12, (struct area) { 300, 0, 250, 300 });
    struct window_node *general_c = warp_success_leaf(13, (struct area) { 550, 0, 250, 300 });
    *general_root = (struct window_node) { .left = general_a, .right = general_branch,
        .split = SPLIT_Y, .child = CHILD_FIRST, .ratio = 0.4f, .area = { 0, 0, 800, 300 } };
    *general_branch = (struct window_node) { .parent = general_root, .left = general_b, .right = general_c,
        .split = SPLIT_Y, .child = CHILD_SECOND, .ratio = 0.5f, .area = { 300, 0, 500, 300 } };
    general_a->parent = general_root;
    general_b->parent = general_c->parent = general_branch;
    struct view general_view = { .sid = 1, .layout = VIEW_BSP, .root = general_root, .insertion_point = 13,
                                 .window_gap = 0, .split_type = SPLIT_Y };
    table_add(&g_space_manager.view, &general_view.sid, &general_view);
    for (int i = 0; i < 3; ++i) table_add(&g_window_manager.managed_window, &windows[i].id, &general_view);
    warp_success_animation_calls = warp_success_animation_windows = 0;
    declarative_reconcile_test_reservation_count = 0;
    declarative_configuration_reconcile_shutdown();
    TEST_CHECK(window_manager_warp_window(&g_space_manager, &g_window_manager, &windows[0], &windows[1]), WINDOW_OP_ERROR_SUCCESS);
    int general_count = 0;
    uint32_t *general_list = view_find_window_list(&general_view, &general_count);
    TEST_CHECK(general_count, 3);
    TEST_CHECK(view_find_window_node(&general_view, 11) != NULL, true);
    TEST_CHECK(view_find_window_node(&general_view, 12) != NULL, true);
    TEST_CHECK(view_find_window_node(&general_view, 13) != NULL, true);
    TEST_CHECK(general_view.insertion_point, 13U);
    TEST_CHECK(general_root->split, SPLIT_Y);
    TEST_CHECK(general_root->child, CHILD_FIRST);
    TEST_CHECK(general_root->ratio == 0.5f, true);
    TEST_CHECK(memcmp(&general_root->area, &(struct area) { 0, 0, 800, 300 }, sizeof(struct area)), 0);
    TEST_CHECK(general_root->left != NULL && general_root->right != NULL, true);
    TEST_CHECK(general_root->left->parent, general_root);
    TEST_CHECK(general_root->right->parent, general_root);
    TEST_CHECK(general_root->left->split, SPLIT_Y);
    TEST_CHECK(general_root->left->ratio == 0.5f, true);
    TEST_CHECK(general_root->left->left != NULL && general_root->left->right != NULL, true);
    TEST_CHECK(general_root->left->left->window_count, 1);
    TEST_CHECK(general_root->left->right->window_count, 1);
    TEST_CHECK(general_root->right->window_count, 1);
    TEST_CHECK(general_root->right->window_list[0], 13U);
    TEST_CHECK(general_root->right->window_order[0], 13U);
    TEST_CHECK(general_root->zoom == NULL, true);
    TEST_CHECK(general_root->left->zoom == NULL, true);
    TEST_CHECK(general_root->feedback_window.id, 0U);
    TEST_CHECK(general_root->left->feedback_window.id, 0U);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &windows[0]), &general_view);
    TEST_CHECK(warp_success_animation_calls, 1);
    TEST_CHECK(warp_success_animation_windows > 0, true);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 0ULL);
    (void) general_list;
    table_remove(&g_space_manager.view, &general_view.sid);
    for (int i = 0; i < 3; ++i) table_remove(&g_window_manager.managed_window, &windows[i].id);
    window_node_destroy(general_root);

    /* Cross-Space commit remaps once, moves/layers once, and flushes only the visible destination. */
    struct window_node *cross_a = warp_success_leaf(11, (struct area) { 0, 0, 400, 300 });
    struct window_node *cross_b = warp_success_leaf(21, (struct area) { 400, 0, 400, 300 });
    struct view cross_a_view = { .sid = 1, .layout = VIEW_BSP, .root = cross_a, .insertion_point = 11 };
    struct view cross_b_view = { .sid = 2, .layout = VIEW_BSP, .root = cross_b, .insertion_point = 21,
                                 .split_type = SPLIT_Y };
    table_add(&g_space_manager.view, &cross_a_view.sid, &cross_a_view);
    table_add(&g_space_manager.view, &cross_b_view.sid, &cross_b_view);
    table_add(&g_window_manager.managed_window, &windows[0].id, &cross_a_view);
    table_add(&g_window_manager.managed_window, &windows[3].id, &cross_b_view);
    transfer_test_src_root = cross_a;
    transfer_test_dst_root = cross_b;
    transfer_test_layer_calls = transfer_test_update_calls = transfer_test_flush_calls = 0;
    transfer_test_src_flush_calls = transfer_test_dst_flush_calls = transfer_test_node_flush_calls = 0;
    warp_success_move_calls = 0;
    declarative_reconcile_test_reservation_count = 0;
    declarative_configuration_reconcile_shutdown();
    TEST_CHECK(window_manager_warp_window(&g_space_manager, &g_window_manager, &windows[0], &windows[3]), WINDOW_OP_ERROR_SUCCESS);
    TEST_CHECK(cross_a->window_count, 0);
    TEST_CHECK(cross_b->window_count, 0);
    TEST_CHECK(cross_b->left != NULL && cross_b->right != NULL, true);
    TEST_CHECK(view_find_window_node(&cross_b_view, 11) != NULL, true);
    TEST_CHECK(view_find_window_node(&cross_b_view, 21) != NULL, true);
    TEST_CHECK(cross_b->split, SPLIT_Y);
    TEST_CHECK(cross_b->ratio == 0.5f, true);
    TEST_CHECK(memcmp(&cross_b->area, &(struct area) { 400, 0, 400, 300 }, sizeof(struct area)), 0);
    TEST_CHECK(cross_b->left->window_count, 1);
    TEST_CHECK(cross_b->left->window_list[0], 11U);
    TEST_CHECK(cross_b->left->window_order[0], 11U);
    TEST_CHECK(cross_b->right->window_count, 1);
    TEST_CHECK(cross_b->right->window_list[0], 21U);
    TEST_CHECK(cross_b->right->window_order[0], 21U);
    TEST_CHECK(memcmp(&cross_b->left->area, &(struct area) { 400, 0, 200, 300 }, sizeof(struct area)), 0);
    TEST_CHECK(memcmp(&cross_b->right->area, &(struct area) { 600, 0, 200, 300 }, sizeof(struct area)), 0);
    TEST_CHECK(cross_b->zoom == NULL, true);
    TEST_CHECK(cross_b->feedback_window.id, 0U);
    TEST_CHECK(cross_a_view.insertion_point, 0U);
    TEST_CHECK(cross_b_view.insertion_point, 21U);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &windows[0]), &cross_b_view);
    TEST_CHECK(windows[0].layer, LAYER_AUTO);
    TEST_CHECK(transfer_test_layer_calls, 1);
    TEST_CHECK(transfer_test_layer_value, LAYER_BELOW);
    TEST_CHECK(warp_success_move_calls, 1);
    TEST_CHECK(warp_success_move_sid, 2ULL);
    TEST_CHECK(transfer_test_node_flush_calls, 1);
    TEST_CHECK(view_is_dirty(&cross_a_view), true);
    TEST_CHECK(view_is_dirty(&cross_b_view), false);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 1ULL);
    table_remove(&g_space_manager.view, &cross_a_view.sid);
    table_remove(&g_space_manager.view, &cross_b_view.sid);
    table_remove(&g_window_manager.managed_window, &windows[0].id);
    table_remove(&g_window_manager.managed_window, &windows[3].id);
    window_node_destroy(cross_a);
    window_node_destroy(cross_b);
    declarative_configuration_reconcile_shutdown();

    /* A failed swap reserves nothing; a stacked cross-Space swap coalesces to one reconcile. */
    struct window_node *swap_a = warp_success_leaf(11, (struct area) { 0, 0, 400, 300 });
    swap_a->window_list[1] = swap_a->window_order[1] = 12;
    swap_a->window_count = 2;
    struct window_node *swap_b = warp_success_leaf(21, (struct area) { 400, 0, 400, 300 });
    swap_b->window_list[1] = swap_b->window_order[1] = 22;
    swap_b->window_count = 2;
    struct view swap_a_view = { .sid = 1, .layout = VIEW_BSP, .root = swap_a, .insertion_point = 12 };
    struct view swap_b_view = { .sid = 2, .layout = VIEW_BSP, .root = swap_b, .insertion_point = 22 };
    table_add(&g_space_manager.view, &swap_a_view.sid, &swap_a_view);
    table_add(&g_space_manager.view, &swap_b_view.sid, &swap_b_view);
    table_add(&g_window_manager.managed_window, &windows[0].id, &swap_a_view);
    table_add(&g_window_manager.managed_window, &windows[1].id, &swap_a_view);
    table_add(&g_window_manager.managed_window, &windows[3].id, &swap_b_view);
    table_add(&g_window_manager.managed_window, &windows[4].id, &swap_b_view);
    declarative_reconcile_test_reservation_count = 0;
    declarative_configuration_reconcile_shutdown();
    TEST_CHECK(window_manager_swap_window(&g_space_manager, &g_window_manager, &windows[0], &windows[0]), WINDOW_OP_ERROR_SAME_WINDOW);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 0ULL);
    TEST_CHECK(window_manager_swap_window(&g_space_manager, &g_window_manager, &windows[0], &windows[1]), WINDOW_OP_ERROR_SUCCESS);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 0ULL);
    TEST_CHECK(window_manager_swap_window(&g_space_manager, &g_window_manager, &windows[0], &windows[3]), WINDOW_OP_ERROR_SUCCESS);
    TEST_CHECK(declarative_reconcile_test_reservation_count, 1ULL);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &windows[0]), &swap_b_view);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &windows[3]), &swap_a_view);
    declarative_configuration_reconcile_shutdown();
    table_remove(&g_space_manager.view, &swap_a_view.sid);
    table_remove(&g_space_manager.view, &swap_b_view.sid);
    for (size_t i = 0; i < array_count(windows); ++i) table_remove(&g_window_manager.managed_window, &windows[i].id);
    window_node_destroy(swap_a);
    window_node_destroy(swap_b);
    declarative_configuration_reconcile_shutdown();

    for (size_t i = 0; i < array_count(windows); ++i) table_remove(&g_window_manager.window, &windows[i].id);
    table_free(&g_space_manager.view);
    table_free(&g_window_manager.window);
    table_free(&g_window_manager.managed_window);
    table_free(&g_window_manager.insert_feedback);
    g_space_manager.view = saved_views;
    g_window_manager.window = saved_windows;
    g_window_manager.managed_window = saved_managed;
    g_window_manager.insert_feedback = saved_feedback;
    g_window_manager.purify_mode = saved_purify;
    g_space_manager.window_placement = saved_placement;
    declarative_installed.generation = saved_generation;
    declarative_installed.has_scaffold = saved_scaffold;
    window_manager_test_animate_observer = saved_animation;
    space_manager_test_move_window_to_space = saved_move;
    window_manager_test_layer_observer = saved_layer;
    window_manager_test_order_window = saved_order;
    window_test_space_for_id = NULL;
    view_test_set_platform_ops(NULL);
    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    event_queue_destroy(&g_event_loop.queue);
    if (g_event_loop.semaphore != SEM_FAILED) sem_close(g_event_loop.semaphore);
    g_event_loop = saved_event_loop;
})

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
