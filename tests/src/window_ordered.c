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

static struct window_server_window_record visibility_snapshot_record;
static struct window_server_snapshot visibility_snapshot;
static bool visibility_snapshot_fails;
static bool visibility_expect_order;
static bool visibility_order_valid;
static int visibility_sequence;
static int visibility_ordered_calls;
static int visibility_snapshot_calls;
static int visibility_snapshot_destroys;
static int visibility_space_calls;
static int visibility_on_screen_calls;
static int visibility_parent_calls;
static int visibility_level_calls;
static int visibility_sub_level_calls;
static int visibility_tags_calls;

static CGError visibility_ordered_query(int connection, uint32_t wid, uint8_t *ordered_in)
{
    (void) connection;
    (void) wid;
    ++visibility_ordered_calls;
    visibility_sequence = 1;
    *ordered_in = 0;
    return kCGErrorSuccess;
}

static struct window_server_snapshot *visibility_snapshot_create(int connection, CFArrayRef window_ids,
                                                                  int expected_count)
{
    (void) connection;
    ++visibility_snapshot_calls;
    if (visibility_expect_order) {
        visibility_order_valid = visibility_sequence == 1;
        visibility_sequence = 2;
    }
    if (visibility_snapshot_fails || !window_ids || expected_count != 1) return NULL;
    return &visibility_snapshot;
}

static void visibility_snapshot_destroy(struct window_server_snapshot *snapshot)
{
    if (snapshot) ++visibility_snapshot_destroys;
}

static uint64_t *visibility_space_list(uint32_t wid, int *count, bool *success)
{
    static uint64_t spaces[] = { 42, 84 };
    (void) wid;
    ++visibility_space_calls;
    *count = 2;
    *success = true;
    return spaces;
}

static bool visibility_on_screen(uint32_t wid, bool *known)
{
    (void) wid;
    ++visibility_on_screen_calls;
    *known = true;
    return true;
}

static bool visibility_parent(uint32_t wid, uint32_t *parent_wid)
{
    (void) wid;
    ++visibility_parent_calls;
    *parent_wid = 700;
    return true;
}

static int visibility_level(uint32_t wid)
{
    (void) wid;
    ++visibility_level_calls;
    return 6;
}

static int visibility_sub_level(uint32_t wid)
{
    (void) wid;
    ++visibility_sub_level_calls;
    return 2;
}

static bool visibility_tags(uint32_t wid, uint64_t *tags)
{
    (void) wid;
    ++visibility_tags_calls;
    *tags = UINT64_C(0x55);
    return true;
}

static void visibility_reset_counts(void)
{
    visibility_order_valid = true;
    visibility_sequence = 0;
    visibility_ordered_calls = 0;
    visibility_snapshot_calls = 0;
    visibility_snapshot_destroys = 0;
    visibility_space_calls = 0;
    visibility_on_screen_calls = 0;
    visibility_parent_calls = 0;
    visibility_level_calls = 0;
    visibility_sub_level_calls = 0;
    visibility_tags_calls = 0;
}

static void visibility_test_begin(uint32_t wid)
{
    visibility_snapshot_record = (struct window_server_window_record) {
        .id = wid,
        .parent_id = 0,
        .level = 0,
        .tags = 0,
        .valid_fields = WINDOW_SERVER_WINDOW_FIELD_ID |
                        WINDOW_SERVER_WINDOW_FIELD_PARENT_ID |
                        WINDOW_SERVER_WINDOW_FIELD_LEVEL |
                        WINDOW_SERVER_WINDOW_FIELD_TAGS
    };
    visibility_snapshot = (struct window_server_snapshot) {
        .generation = 1,
        .records = &visibility_snapshot_record,
        .count = 1
    };
    visibility_snapshot_fails = false;
    visibility_expect_order = false;
    visibility_reset_counts();
    event_loop_debug_window_space_list = visibility_space_list;
    event_loop_debug_window_is_on_screen = visibility_on_screen;
    event_loop_debug_window_parent = visibility_parent;
    event_loop_debug_window_level = visibility_level;
    event_loop_debug_window_sub_level = visibility_sub_level;
    event_loop_debug_window_tags = visibility_tags;
    event_loop_debug_snapshot_create = visibility_snapshot_create;
    event_loop_debug_snapshot_destroy = visibility_snapshot_destroy;
    space_manager_test_active_space_id = 99;
    g_verbose = true;
}

static void visibility_test_end(void)
{
    event_loop_debug_window_space_list = window_space_list_with_status;
    event_loop_debug_window_is_on_screen = window_is_on_screen;
    event_loop_debug_window_parent = window_parent_with_status;
    event_loop_debug_window_level = window_level;
    event_loop_debug_window_sub_level = window_sub_level;
    event_loop_debug_window_tags = window_tags_with_status;
    event_loop_debug_snapshot_create = window_server_snapshot_create;
    event_loop_debug_snapshot_destroy = window_server_snapshot_destroy;
    event_loop_window_is_ordered_in = SLSWindowIsOrderedIn;
    space_manager_test_active_space_id = 0;
    g_verbose = false;
}

static char *visibility_capture_line(const char *event_name, uint32_t wid,
                                     bool ordered_known, bool ordered_in)
{
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    FILE *capture = tmpfile();
    if (saved_stdout == -1 || !capture || dup2(fileno(capture), STDOUT_FILENO) == -1) {
        if (saved_stdout != -1) close(saved_stdout);
        if (capture) fclose(capture);
        return NULL;
    }

    event_loop_debug_window_visibility(event_name, wid, ordered_known, ordered_in);
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    if (fseek(capture, 0, SEEK_END) != 0) {
        fclose(capture);
        return NULL;
    }
    long length = ftell(capture);
    if (length < 0 || fseek(capture, 0, SEEK_SET) != 0) {
        fclose(capture);
        return NULL;
    }
    char *line = calloc((size_t) length + 1, 1);
    if (line && fread(line, 1, (size_t) length, capture) != (size_t) length) {
        free(line);
        line = NULL;
    }
    fclose(capture);
    return line;
}

static uint32_t test_ordered_layout_inventory[2];
static bool test_ordered_layout_inventory_fails;
static bool test_ordered_layout_user_space(uint64_t sid) { (void) sid; return true; }
static uint32_t *test_ordered_layout_window_list(uint64_t *sids, int sid_count, int cid,
                                                 int *count, bool mode, bool *succeeded)
{
    (void) sids; (void) sid_count; (void) cid; (void) mode;
    *count = test_ordered_layout_inventory_fails ? 0 : 2;
    *succeeded = !test_ordered_layout_inventory_fails;
    return test_ordered_layout_inventory;
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
    t->view->split_type = SPLIT_AUTO;
    t->view->root->area = (struct area) { .w = 1000.0f, .h = 700.0f };
    t->view->split_ratio = 0.5f;
    table_remove(&g_space_manager.view, &sid);
    table_add(&g_space_manager.view, &sid, t->view);

    struct window *windows[] = { &t->a, &t->b };
    uint32_t wids[] = { TEST_ORDERED_WID_A, TEST_ORDERED_WID_B };
    memcpy(test_ordered_layout_inventory, wids, sizeof(wids));
    space_manager_test_layout_window_list = test_ordered_layout_window_list;
    space_manager_test_layout_space_is_user = test_ordered_layout_user_space;
    test_ordered_layout_inventory_fails = false;
    for (int i = 0; i < 2; ++i) {
        test_ordered_window_init(windows[i], &t->application, wids[i]);
        window_manager_add_window(&g_window_manager, windows[i]);
        enum managed_window_result managed_result = window_manager_tile_window_on_space(&g_space_manager,
                                                                                         &g_window_manager,
                                                                                         windows[i], sid, 0, true);
        if (managed_result == MANAGED_WINDOW_FAILED) {
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
    space_manager_test_layout_window_list = NULL;
    space_manager_test_layout_space_is_user = NULL;
}

TEST_FUNC(window_visibility_diagnostics_reuse_snapshot_with_independent_fallbacks,
{
    visibility_test_begin(77);
    char *line = visibility_capture_line("VISIBILITY", 77, true, true);
    TEST_CHECK(line != NULL, true);
    if (line) {
        TEST_CHECK(strcmp(line,
            "VISIBILITY: wid=77 ordered-in=true on-screen=true level=0 sub-level=2 parent=0 "
            "tags=0x0000000000000000 active-space=99 spaces-known=true spaces=[42,84]\n"), 0);
    }
    free(line);
    TEST_CHECK(visibility_snapshot_calls, 1);
    TEST_CHECK(visibility_snapshot_destroys, 1);
    TEST_CHECK(visibility_parent_calls, 0);
    TEST_CHECK(visibility_level_calls, 0);
    TEST_CHECK(visibility_tags_calls, 0);

    visibility_reset_counts();
    visibility_snapshot_record.valid_fields &= ~WINDOW_SERVER_WINDOW_FIELD_PARENT_ID;
    line = visibility_capture_line("VISIBILITY", 77, true, true);
    free(line);
    TEST_CHECK(visibility_parent_calls, 1);
    TEST_CHECK(visibility_level_calls, 0);
    TEST_CHECK(visibility_tags_calls, 0);
    TEST_CHECK(visibility_snapshot_destroys, 1);

    visibility_reset_counts();
    visibility_snapshot_record.valid_fields |= WINDOW_SERVER_WINDOW_FIELD_PARENT_ID;
    visibility_snapshot_record.valid_fields &= ~WINDOW_SERVER_WINDOW_FIELD_LEVEL;
    line = visibility_capture_line("VISIBILITY", 77, true, true);
    free(line);
    TEST_CHECK(visibility_parent_calls, 0);
    TEST_CHECK(visibility_level_calls, 1);
    TEST_CHECK(visibility_tags_calls, 0);
    TEST_CHECK(visibility_snapshot_destroys, 1);

    visibility_reset_counts();
    visibility_snapshot_record.valid_fields |= WINDOW_SERVER_WINDOW_FIELD_LEVEL;
    visibility_snapshot_record.valid_fields &= ~WINDOW_SERVER_WINDOW_FIELD_TAGS;
    line = visibility_capture_line("VISIBILITY", 77, true, true);
    free(line);
    TEST_CHECK(visibility_parent_calls, 0);
    TEST_CHECK(visibility_level_calls, 0);
    TEST_CHECK(visibility_tags_calls, 1);
    TEST_CHECK(visibility_snapshot_destroys, 1);

    visibility_reset_counts();
    visibility_snapshot_record.valid_fields |= WINDOW_SERVER_WINDOW_FIELD_TAGS;
    visibility_snapshot_fails = true;
    line = visibility_capture_line("VISIBILITY", 77, false, false);
    TEST_CHECK(line != NULL, true);
    if (line) {
        TEST_CHECK(strcmp(line,
            "VISIBILITY: wid=77 ordered-in=unknown on-screen=true level=6 sub-level=2 parent=700 "
            "tags=0x0000000000000055 active-space=99 spaces-known=true spaces=[42,84]\n"), 0);
    }
    free(line);
    TEST_CHECK(visibility_parent_calls, 1);
    TEST_CHECK(visibility_level_calls, 1);
    TEST_CHECK(visibility_tags_calls, 1);
    TEST_CHECK(visibility_snapshot_calls, 1);
    TEST_CHECK(visibility_snapshot_destroys, 0);

    visibility_reset_counts();
    g_verbose = false;
    line = visibility_capture_line("VISIBILITY", 77, true, true);
    TEST_CHECK(line != NULL, true);
    if (line) TEST_CHECK(line[0], '\0');
    free(line);
    TEST_CHECK(visibility_snapshot_calls, 0);
    TEST_CHECK(visibility_space_calls, 0);
    TEST_CHECK(visibility_on_screen_calls, 0);
    TEST_CHECK(visibility_parent_calls, 0);
    TEST_CHECK(visibility_level_calls, 0);
    TEST_CHECK(visibility_sub_level_calls, 0);
    TEST_CHECK(visibility_tags_calls, 0);
    visibility_test_end();
});

TEST_FUNC(window_visibility_handlers_query_order_before_snapshot_diagnostics,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;

    visibility_test_begin(t.a.id);
    visibility_expect_order = true;
    event_loop_window_is_ordered_in = visibility_ordered_query;

    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    FILE *capture = tmpfile();
    TEST_CHECK(saved_stdout != -1, true);
    TEST_CHECK(capture != NULL, true);
    if (saved_stdout != -1 && capture && dup2(fileno(capture), STDOUT_FILENO) != -1) {
        EVENT_HANDLER_SLS_WINDOW_IS_INVISIBLE((void *)(intptr_t) t.a.id, 0);
        fflush(stdout);
        dup2(saved_stdout, STDOUT_FILENO);
    }
    if (saved_stdout != -1) close(saved_stdout);
    if (capture) fclose(capture);

    TEST_CHECK(visibility_ordered_calls, 1);
    TEST_CHECK(visibility_snapshot_calls, 1);
    TEST_CHECK(visibility_snapshot_destroys, 1);
    TEST_CHECK(visibility_order_valid, true);
    TEST_CHECK(visibility_sequence, 2);
    TEST_CHECK(window_check_flag(&t.a, WINDOW_ORDERED_OUT), true);

    visibility_test_end();
    test_ordered_free(&t);
});

TEST_FUNC(imperative_layout_transaction_exhausts_preparation_faults_without_publication,
{
    struct test_ordered success;
    TEST_CHECK(test_ordered_init(&success), true);
    uint64_t unrelated_sid = TEST_ORDERED_SID - 1;
    struct view *unrelated = calloc(1, sizeof(*unrelated));
    unrelated->sid = unrelated_sid;
    unrelated->layout = VIEW_BSP;
    unrelated->split_ratio = 0.7f;
    unrelated->window_gap = 17;
    unrelated->flags = VIEW_IS_DIRTY | VIEW_WINDOW_GAP;
    unrelated->scaffold_fingerprint = 777;
    unrelated->scaffold_primary[0] = 999;
    unrelated->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].present = true;
    unrelated->root = calloc(1, sizeof(*unrelated->root));
    struct view unrelated_before = *unrelated;
    table_add(&g_space_manager.view, &unrelated_sid, unrelated);
    declarative_space_test_fail_allocation_at(0);
    TEST_CHECK(space_manager_set_layout_for_view(&g_space_manager, success.view, VIEW_STACK), true);
    TEST_CHECK(declarative_space_test_allocation_count() > 0, true);
    TEST_CHECK(success.view->layout, VIEW_STACK);
    TEST_CHECK(success.view->root->window_count, 2);
    TEST_CHECK(unrelated->root, unrelated_before.root);
    TEST_CHECK(unrelated->layout, unrelated_before.layout);
    TEST_CHECK(unrelated->flags, unrelated_before.flags);
    TEST_CHECK(unrelated->window_gap, unrelated_before.window_gap);
    TEST_CHECK(unrelated->split_ratio, unrelated_before.split_ratio);
    TEST_CHECK(unrelated->scaffold_fingerprint, unrelated_before.scaffold_fingerprint);
    TEST_CHECK(memcmp(unrelated->runtime_ownership, unrelated_before.runtime_ownership,
                      sizeof(unrelated->runtime_ownership)), 0);
    table_remove(&g_space_manager.view, &unrelated_sid);
    view_destroy(unrelated);
    free(unrelated);
    test_ordered_free(&success);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0U);
    TEST_CHECK(space_manager_layout_outstanding, 0U);

    for (size_t ordinal = 1; ordinal <= 3; ++ordinal) {
        struct test_ordered t;
        TEST_CHECK(test_ordered_init(&t), true);
        struct window_node *root_before = t.view->root;
        struct bucket **buckets_before = g_window_manager.managed_window.buckets;
        space_manager_layout_fail_ordinal = ordinal;
        TEST_CHECK(space_manager_set_layout_for_view(&g_space_manager, t.view, VIEW_STACK), false);
        TEST_CHECK(t.view->root, root_before);
        TEST_CHECK(t.view->layout, VIEW_BSP);
        TEST_CHECK(g_window_manager.managed_window.buckets, buckets_before);
        TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a), t.view);
        TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b), t.view);
        space_manager_layout_fail_ordinal = 0;
        test_ordered_free(&t);
        TEST_CHECK(space_manager_layout_outstanding, 0U);
    }

    struct test_ordered allocation_probe;
    TEST_CHECK(test_ordered_init(&allocation_probe), true);
    declarative_space_test_fail_allocation_at(0);
    TEST_CHECK(space_manager_set_layout_for_view(&g_space_manager, allocation_probe.view, VIEW_STACK), true);
    size_t allocation_count = declarative_space_test_allocation_count();
    test_ordered_free(&allocation_probe);
    TEST_CHECK(allocation_count > 0, true);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0U);

    for (size_t ordinal = 1; ordinal <= allocation_count; ++ordinal) {
        struct test_ordered t;
        TEST_CHECK(test_ordered_init(&t), true);
        struct window_node *root_before = t.view->root;
        struct declarative_space_root_before_image image_before;
        TEST_CHECK(declarative_space_validate_root(t.view, &image_before), true);
        struct bucket **buckets_before = g_window_manager.managed_window.buckets;
        int count_before = g_window_manager.managed_window.count;
        uint64_t flags_before = t.view->flags;
        struct declarative_runtime_cell ownership_before[DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT];
        memcpy(ownership_before, t.view->runtime_ownership, sizeof(ownership_before));
        declarative_space_test_fail_allocation_at(ordinal);
        TEST_CHECK(space_manager_set_layout_for_view(&g_space_manager, t.view, VIEW_STACK), false);
        struct declarative_space_root_before_image image_after;
        TEST_CHECK(declarative_space_validate_root(t.view, &image_after), true);
        TEST_CHECK(t.view->root, root_before);
        TEST_CHECK(image_after.fingerprint, image_before.fingerprint);
        TEST_CHECK(t.view->layout, VIEW_BSP);
        TEST_CHECK(t.view->flags, flags_before);
        TEST_CHECK(memcmp(t.view->runtime_ownership, ownership_before, sizeof(ownership_before)), 0);
        TEST_CHECK(g_window_manager.managed_window.buckets, buckets_before);
        TEST_CHECK(g_window_manager.managed_window.count, count_before);
        TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a), t.view);
        TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b), t.view);
        declarative_space_test_fail_allocation_at(0);
        test_ordered_free(&t);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0U);
    }
    declarative_space_test_fail_allocation_at(0);
    space_manager_layout_fail_ordinal = 0;
})

TEST_FUNC(imperative_layout_transaction_synthesizes_identity_for_empty_uuid,
{
    struct test_ordered t;
    bool initialized = test_ordered_init(&t);
    TEST_CHECK(initialized, true);
    if (!initialized) return result;

    t.view->uuid = CFStringCreateCopy(NULL, CFSTR(""));
    TEST_CHECK(t.view->uuid != NULL, true);
    if (!t.view->uuid) {
        test_ordered_free(&t);
        return result;
    }
    TEST_CHECK(CFStringGetLength(t.view->uuid), 0L);

    TEST_CHECK(space_manager_set_layout_for_view(&g_space_manager, t.view, VIEW_STACK), true);
    TEST_CHECK(t.view->layout, VIEW_STACK);
    TEST_CHECK(t.view->root->window_count, 2);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a), t.view);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b), t.view);

    test_ordered_free(&t);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0U);
    TEST_CHECK(space_manager_layout_outstanding, 0U);
})

TEST_FUNC(imperative_float_layout_uses_no_external_inventory_and_removes_mappings,
{
    struct test_ordered t;
    TEST_CHECK(test_ordered_init(&t), true);
    test_ordered_layout_inventory_fails = true;
    TEST_CHECK(space_manager_set_layout_for_view(&g_space_manager, t.view, VIEW_FLOAT), true);
    TEST_CHECK(t.view->layout, VIEW_FLOAT);
    TEST_CHECK(t.view->root != NULL, true);
    TEST_CHECK(t.view->root->window_count, 0);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a), NULL);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b), NULL);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0U);
    TEST_CHECK(space_manager_layout_outstanding, 0U);
    test_ordered_layout_inventory_fails = false;
    test_ordered_free(&t);
})

TEST_FUNC(imperative_global_layout_commits_detached_roots_and_runtime_ownership,
{
    struct test_ordered t;
    TEST_CHECK(test_ordered_init(&t), true);
    declarative_runtime_reset_operation_ops();
    t.view->scaffold_fingerprint = 91;
    t.view->scaffold_primary[0] = t.a.id;
    struct declarative_value stack = { .present = true, .enumeration = VIEW_STACK };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_LAYOUT, &stack, 0), DECLARATIVE_APPLY_OK);
    TEST_CHECK(g_space_manager.layout, VIEW_STACK);
    TEST_CHECK(t.view->layout, VIEW_STACK);
    TEST_CHECK(t.view->root->window_count, 2);
    TEST_CHECK(t.view->scaffold_fingerprint, 0ULL);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_LAYOUT].present, true);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_LAYOUT].value.integer, VIEW_STACK);

    test_ordered_layout_inventory_fails = true;
    struct declarative_value floating = { .present = true, .enumeration = VIEW_FLOAT };
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_LAYOUT, &floating, 0), DECLARATIVE_APPLY_OK);
    TEST_CHECK(g_space_manager.layout, VIEW_FLOAT);
    TEST_CHECK(t.view->layout, VIEW_FLOAT);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.a), NULL);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &t.b), NULL);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_LAYOUT].value.integer, VIEW_FLOAT);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0U);
    TEST_CHECK(space_manager_layout_outstanding, 0U);
    test_ordered_layout_inventory_fails = false;
    test_ordered_free(&t);
})

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

TEST_FUNC(macos_version_classifies_named_and_future_releases,
{
    long saved_macos_version = workspace_macos_major_version();

    workspace_set_macos_version(27);
    TEST_CHECK(workspace_macos_major_version(), 27L);
    TEST_CHECK(workspace_is_macos_golden_gate(), true);
    TEST_CHECK(workspace_is_macos_tahoe(), false);
    TEST_CHECK(workspace_is_macos_future(), false);
    TEST_CHECK(workspace_is_macos_tahoe_or_newer(), true);

    workspace_set_macos_version(26);
    TEST_CHECK(workspace_is_macos_tahoe(), true);
    TEST_CHECK(workspace_is_macos_golden_gate(), false);
    TEST_CHECK(workspace_is_macos_future(), false);

    workspace_set_macos_version(28);
    TEST_CHECK(workspace_macos_major_version(), 28L);
    TEST_CHECK(workspace_is_macos_future(), true);
    TEST_CHECK(workspace_is_macos_golden_gate(), false);
    TEST_CHECK(workspace_is_macos_tahoe(), false);
    TEST_CHECK(workspace_is_macos_tahoe_or_newer(), true);
    TEST_CHECK(workspace_is_macos_sequoia_or_newer(), true);

    workspace_set_macos_version(15);
    TEST_CHECK(workspace_is_macos_sequoia(), true);
    TEST_CHECK(workspace_is_macos_tahoe(), false);
    TEST_CHECK(workspace_is_macos_golden_gate(), false);
    TEST_CHECK(workspace_is_macos_future(), false);
    TEST_CHECK(workspace_is_macos_sequoia_or_newer(), true);
    TEST_CHECK(workspace_is_macos_tahoe_or_newer(), false);

    workspace_set_macos_version(10);
    TEST_CHECK(workspace_is_macos_bigsur(), false);
    TEST_CHECK(workspace_is_macos_future(), false);
    TEST_CHECK(workspace_is_macos_monterey_or_newer(), false);

    workspace_set_macos_version(saved_macos_version);
});
