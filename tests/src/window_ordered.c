// A window its app orders out without minimizing or hiding it (an Electron app that
// hides its window on close) must leave the bsp tree, or it holds a tile nobody sees.
//
// The ids are made up: the window server reports such a window as ordered out and such
// a space as not visible, so these tests never move, resize or reorder a real window.
#define TEST_ORDERED_SID   0x7ffffff0ULL
#define TEST_ORDERED_WID_A 0x7ffffff1
#define TEST_ORDERED_WID_B 0x7ffffff2
#define TEST_ORDERED_EXTRA_WID_BASE UINT32_C(0x70000000)

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

    TEST_CHECK(space_manager_apply_layout_with_inventory(&g_window_manager, t.view, VIEW_STACK, NULL, 0, false), false);
    TEST_CHECK(t.view->layout, VIEW_BSP);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) != NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) != NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == t.view, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b) == t.view, true);

    TEST_CHECK(space_manager_apply_layout_with_inventory(&g_window_manager, t.view, VIEW_STACK, NULL, 0, true), true);
    TEST_CHECK(t.view->layout, VIEW_STACK);
    TEST_CHECK(view_find_window_node(t.view, t.a.id) == NULL, true);
    TEST_CHECK(view_find_window_node(t.view, t.b.id) == NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a) == NULL, true);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b) == NULL, true);
    test_ordered_free(&t);
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
