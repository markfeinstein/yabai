// A window its app orders out without minimizing or hiding it (an Electron app that
// hides its window on close) must leave the bsp tree, or it holds a tile nobody sees.
//
// The ids are made up: the window server reports such a window as ordered out and such
// a space as not visible, so these tests never move, resize or reorder a real window.
#define TEST_ORDERED_SID   0x7ffffff0ULL
#define TEST_ORDERED_WID_A 0x7ffffff1
#define TEST_ORDERED_WID_B 0x7ffffff2
#define TEST_ORDERED_EXTRA_WID_BASE UINT32_C(0x70000000)

TEST_FUNC(view_padding_sum_widens_before_addition,
{
    TEST_CHECK(view_padding_sum(INT_MAX, INT_MAX), 4294967294.0);
    TEST_CHECK(view_padding_sum(INT_MIN, INT_MIN), -4294967296.0);
});

TEST_FUNC(view_bsp_insertion_and_equalization_use_the_space_split_ratio,
{
    struct view view = { .layout = VIEW_BSP, .split_type = SPLIT_Y, .split_ratio = 0.6f };
    view.root = calloc(1, sizeof(*view.root));
    view.root->area = (struct area) { .w = 1000.0f, .h = 700.0f };
    struct window first = { .id = 101 }, second = { .id = 102 };
    TEST_CHECK(view_add_window_node(&view, &first) != NULL, true);
    TEST_CHECK(view_add_window_node(&view, &second) != NULL, true);
    TEST_CHECK(view.root->split, SPLIT_Y);
    TEST_CHECK(view.root->ratio, 0.6f);
    view.root->ratio = 0.4f;
    window_node_equalize(view.root, SPLIT_Y, view.split_ratio);
    TEST_CHECK(view.root->ratio, 0.6f);
    free(view.root->left);
    free(view.root->right);
    free(view.root);
});

static CGError test_ordered_query_failure(int connection, uint32_t wid, uint8_t *ordered_in)
{
    (void) connection;
    (void) wid;
    (void) ordered_in;
    return kCGErrorFailure;
}

static CGError test_ordered_query_out(int connection, uint32_t wid, uint8_t *ordered_in)
{
    (void) connection;
    (void) wid;
    *ordered_in = 0;
    return kCGErrorSuccess;
}

struct test_ordered
{
    struct application application;
    struct window a;
    struct window b;
    struct view *view;
};

static void test_ordered_window_init(struct window *window, struct application *application, uint32_t wid)
{
    memset(window, 0, sizeof(struct window));
    window->application = application;
    window->id = wid;
    window->id_ptr = &window->id;
    window->is_root = true;
    window_set_flag(window, WINDOW_MOVABLE);
    window_set_rule_flag(window, WINDOW_RULE_MANAGED);
}

// Two tiled windows on a bsp view of a space that is not visible, as the event loop leaves them.
static bool test_ordered_init(struct test_ordered *t)
{
    event_loop_window_is_ordered_in = test_ordered_query_out;
    static bool tables_initialized;
    if (!tables_initialized) {
        g_connection = SLSMainConnectionID();
        table_init(&g_window_manager.window, 150, hash_wm, compare_wm);
        table_init(&g_window_manager.managed_window, 150, hash_wm, compare_wm);
        table_init(&g_window_manager.insert_feedback, 150, hash_wm, compare_wm);
        table_init(&g_space_manager.view, 23, hash_view, compare_view);
        tables_initialized = true;
    }

    memset(t, 0, sizeof(*t));
    t->application.name = "test";

    // view_create() asks the window server about the space; a made-up space would come back as float.
    uint64_t sid = TEST_ORDERED_SID;
    t->view = calloc(1, sizeof(struct view));
    if (!t->view) return false;
    t->view->root = calloc(1, sizeof(struct window_node));
    if (!t->view->root) {
        free(t->view);
        t->view = NULL;
        return false;
    }
    t->view->sid = sid;
    t->view->layout = VIEW_BSP;
    t->view->split_ratio = 0.5f;
    table_remove(&g_space_manager.view, &sid);
    table_add(&g_space_manager.view, &sid, t->view);

    struct window *windows[] = { &t->a, &t->b };
    uint32_t wids[] = { TEST_ORDERED_WID_A, TEST_ORDERED_WID_B };
    for (int i = 0; i < 2; ++i) {
        test_ordered_window_init(windows[i], &t->application, wids[i]);
        window_manager_add_window(&g_window_manager, windows[i]);
        struct view *view = space_manager_tile_window_on_space(&g_space_manager, windows[i], sid);
        if (!view) {
            for (int j = 0; j <= i; ++j) {
                window_manager_remove_managed_window(&g_window_manager, windows[j]->id);
                window_manager_remove_window(&g_window_manager, windows[j]->id);
            }
            table_remove(&g_space_manager.view, &sid);
            view_destroy(t->view);
            free(t->view);
            t->view = NULL;
            return false;
        }
        window_manager_add_managed_window(&g_window_manager, windows[i], view);
    }

    return true;
}

static void test_ordered_free(struct test_ordered *t)
{
    struct window *windows[] = { &t->a, &t->b };
    for (int i = 0; i < 2; ++i) {
        window_manager_remove_managed_window(&g_window_manager, windows[i]->id);
        window_manager_remove_window(&g_window_manager, windows[i]->id);
    }

    table_remove(&g_space_manager.view, &t->view->sid);
    view_destroy(t->view);
    free(t->view);
    t->view = NULL;
}

TEST_FUNC(window_inventory_failure_preserves_and_empty_success_clears_membership,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;

    TEST_CHECK(window_manager_reconcile_windows_on_space(&g_window_manager, t.view, NULL, 0, false), false);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) != NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) != NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == t.view, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b) == t.view, true);

    TEST_CHECK(window_manager_reconcile_windows_on_space(&g_window_manager, t.view, NULL, 0, true), true);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) == NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b) == NULL, true);
    test_ordered_free(&t);
});

TEST_FUNC(layout_inventory_failure_is_transactional_and_empty_success_applies,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;

    t.view->scaffold_fingerprint = 99;
    t.view->scaffold_primary[0] = t.a.id;
    TEST_CHECK(space_manager_apply_layout_with_inventory(&g_window_manager, t.view, VIEW_STACK, NULL, 0, false), false);
    TEST_CHECK(t.view->layout, VIEW_BSP);
    TEST_CHECK(t.view->scaffold_fingerprint, 99ULL);
    TEST_CHECK(t.view->scaffold_primary[0], t.a.id);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) != NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) != NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == t.view, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b) == t.view, true);

    TEST_CHECK(space_manager_apply_layout_with_inventory(&g_window_manager, t.view, VIEW_STACK, NULL, 0, true), true);
    TEST_CHECK(t.view->layout, VIEW_STACK);
    TEST_CHECK(t.view->scaffold_fingerprint, 0ULL);
    TEST_CHECK(t.view->scaffold_primary[0], 0U);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) == NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b) == NULL, true);
    test_ordered_free(&t);
});

TEST_FUNC(scaffold_identity_is_invalidated_across_rapid_layout_conversions,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;

    uint32_t inventory[] = { t.a.id, t.b.id };
    t.view->scaffold_fingerprint = 1234;
    t.view->scaffold_primary[0] = t.a.id;
    t.view->scaffold_primary[1] = t.b.id;
    TEST_CHECK(space_manager_apply_layout_with_inventory(&g_window_manager, t.view, VIEW_STACK,
                                                         inventory, 2, true), true);
    TEST_CHECK(t.view->scaffold_fingerprint, 0ULL);
    TEST_CHECK(t.view->scaffold_primary[0], 0U);
    TEST_CHECK(t.view->scaffold_primary[1], 0U);
    TEST_CHECK(space_manager_apply_layout_with_inventory(&g_window_manager, t.view, VIEW_BSP,
                                                         inventory, 2, true), true);
    TEST_CHECK(t.view->scaffold_fingerprint, 0ULL);
    TEST_CHECK(t.view->scaffold_primary[0], 0U);
    TEST_CHECK(t.view->scaffold_primary[1], 0U);
    test_ordered_free(&t);
});

TEST_FUNC(scaffold_identity_is_invalidated_for_global_layout_batch,
{
    struct window_node roots[2] = { 0 };
    struct view views[2] = {
        { .layout = VIEW_BSP, .root = &roots[0], .scaffold_fingerprint = 101, .scaffold_primary = { 1, 2, 3 } },
        { .layout = VIEW_BSP, .root = &roots[1], .scaffold_fingerprint = 202, .scaffold_primary = { 4, 5, 6 } }
    };
    struct view *mutated[1] = { 0 };
    for (size_t i = 0; i < 2; ++i) {
        space_manager_commit_layout_with_inventory(&g_window_manager, &views[i], VIEW_STACK,
                                                   NULL, 0, mutated, 1);
        space_manager_commit_layout_with_inventory(&g_window_manager, &views[i], VIEW_BSP,
                                                   NULL, 0, mutated, 1);
        TEST_CHECK(views[i].scaffold_fingerprint, 0ULL);
        TEST_CHECK(views[i].scaffold_primary[0], 0U);
        TEST_CHECK(views[i].scaffold_primary[1], 0U);
        TEST_CHECK(views[i].scaffold_primary[2], 0U);
    }
});

TEST_FUNC(stack_layout_capacity_preflight_preserves_then_accepts_bounded_inventory,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;

    enum { window_count = NODE_MAX_WINDOW_COUNT + 1 };
    struct window *windows[window_count] = {0};
    uint32_t window_list[window_count] = {0};
    windows[0] = &t.a;
    windows[1] = &t.b;
    window_list[0] = t.a.id;
    window_list[1] = t.b.id;
    for (int i = 2; i < window_count; ++i) {
        windows[i] = calloc(1, sizeof(struct window));
        TEST_CHECK(windows[i] != NULL, true);
        if (!windows[i]) continue;
        test_ordered_window_init(windows[i], &t.application, TEST_ORDERED_EXTRA_WID_BASE + (uint32_t) i);
        window_list[i] = windows[i]->id;
        window_manager_add_window(&g_window_manager, windows[i]);
    }

    TEST_CHECK(space_manager_apply_layout_with_inventory(&g_window_manager, t.view, VIEW_STACK, window_list, window_count, true), false);
    TEST_CHECK(t.view->layout, VIEW_BSP);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) != NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) != NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == t.view, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b) == t.view, true);

    TEST_CHECK(space_manager_apply_layout_with_inventory(&g_window_manager, t.view, VIEW_STACK, window_list, NODE_MAX_WINDOW_COUNT, true), true);
    TEST_CHECK(t.view->layout, VIEW_STACK);
    TEST_CHECK(t.view->root->window_count, NODE_MAX_WINDOW_COUNT);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == t.view, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b) == t.view, true);

    for (int i = 2; i < window_count; ++i) {
        if (!windows[i]) continue;
        window_manager_remove_managed_window(&g_window_manager, windows[i]->id);
        window_manager_remove_window(&g_window_manager, windows[i]->id);
        free(windows[i]);
    }
    test_ordered_free(&t);
});

TEST_FUNC(window_ordered_query_failure_preserves_state,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;

    __auto_type saved_query = event_loop_window_is_ordered_in;
    event_loop_window_is_ordered_in = test_ordered_query_failure;
    EVENT_HANDLER_SLS_WINDOW_IS_INVISIBLE((void *)(intptr_t) t.a.id, 0);
    TEST_CHECK(window_check_flag(&t.a, WINDOW_ORDERED_OUT), false);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) != NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == t.view, true);

    event_loop_window_is_ordered_in = test_ordered_query_out;
    EVENT_HANDLER_SLS_WINDOW_IS_INVISIBLE((void *)(intptr_t) t.a.id, 0);
    TEST_CHECK(window_check_flag(&t.a, WINDOW_ORDERED_OUT), true);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);

    event_loop_window_is_ordered_in = test_ordered_query_failure;
    EVENT_HANDLER_SLS_WINDOW_IS_VISIBLE((void *)(intptr_t) t.a.id, 0);
    TEST_CHECK(window_check_flag(&t.a, WINDOW_ORDERED_OUT), true);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);

    event_loop_window_is_ordered_in = saved_query;
    test_ordered_free(&t);
});

TEST_FUNC(window_ordered_out_leaves_the_tree,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;
    TEST_CHECK(view_find_window_node(t.view, t.a.id) != NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) != NULL, true);

    EVENT_HANDLER_SLS_WINDOW_IS_INVISIBLE((void *)(intptr_t) t.a.id, 0);

    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) != NULL, true);
    test_ordered_free(&t);
});

// Space changes and rule re-evaluation re-tile every window that should be managed,
// so an ordered-out window must stop qualifying, as a minimized one does.
TEST_FUNC(window_ordered_out_stays_unmanaged,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;
    TEST_CHECK(window_manager_should_manage_window(&t.a), true);

    EVENT_HANDLER_SLS_WINDOW_IS_INVISIBLE((void *)(intptr_t) t.a.id, 0);

    TEST_CHECK(window_manager_should_manage_window(&t.a), false);
    TEST_CHECK(window_manager_should_manage_window(&t.b), true);
    test_ordered_free(&t);
});

TEST_FUNC(window_ordered_in_is_tiled_again,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;
    EVENT_HANDLER_SLS_WINDOW_IS_INVISIBLE((void *)(intptr_t) t.a.id, 0);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);

    // Ordered in on an inactive space: eligible again, tiled when its space becomes active.
    window_manager_window_did_order_in(&g_window_manager, &t.a, 0);
    TEST_CHECK(window_check_flag(&t.a, WINDOW_ORDERED_OUT), false);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);

    window_manager_window_did_order_in(&g_window_manager, &t.a, TEST_ORDERED_SID);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) != NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == t.view, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) != NULL, true);
    test_ordered_free(&t);
});

// The window server sends 808 when a window's z-order changes (e.g. its app comes to the front),
// not when it is ordered out: on macOS 27 hiding a window sends no 808 at all.
TEST_FUNC(window_reordered_stays_tiled,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;

    EVENT_HANDLER_SLS_WINDOW_ORDERED((void *)(intptr_t) t.a.id, 0);

    TEST_CHECK(view_find_window_node(t.view, t.a.id) != NULL, true);
    TEST_CHECK(window_check_flag(&t.a, WINDOW_ORDERED_OUT), false);
    test_ordered_free(&t);
});

// The events are queued: a visible event for a window that is ordered out again by now changes nothing.
TEST_FUNC(window_visible_event_while_still_ordered_out,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;
    EVENT_HANDLER_SLS_WINDOW_IS_INVISIBLE((void *)(intptr_t) t.a.id, 0);

    EVENT_HANDLER_SLS_WINDOW_IS_VISIBLE((void *)(intptr_t) t.a.id, 0);

    TEST_CHECK(window_check_flag(&t.a, WINDOW_ORDERED_OUT), true);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);
    test_ordered_free(&t);
});

// A floated window stays floating when it comes back.
TEST_FUNC(window_ordered_in_keeps_float,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;
    EVENT_HANDLER_SLS_WINDOW_IS_INVISIBLE((void *)(intptr_t) t.a.id, 0);
    window_set_flag(&t.a, WINDOW_FLOAT);

    window_manager_window_did_order_in(&g_window_manager, &t.a, TEST_ORDERED_SID);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);
    test_ordered_free(&t);
});

// A release newer than any listed must take the newest listed code paths, not the pre-Ventura ones.
TEST_FUNC(macos_version_newer_than_listed,
{
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;

    workspace_set_macos_version(27);
    TEST_CHECK(workspace_is_macos_tahoe(), true);
    TEST_CHECK(workspace_is_macos_sequoia(), false);

    workspace_set_macos_version(26);
    TEST_CHECK(workspace_is_macos_tahoe(), true);

    workspace_set_macos_version(15);
    TEST_CHECK(workspace_is_macos_sequoia(), true);
    TEST_CHECK(workspace_is_macos_tahoe(), false);

    workspace_set_macos_version(10);
    TEST_CHECK(workspace_is_macos_bigsur(), false);
    TEST_CHECK(workspace_is_macos_tahoe(), false);

    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
});
