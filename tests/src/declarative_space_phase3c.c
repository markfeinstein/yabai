struct declarative_space_phase3c_ops_context
{
    size_t normal, below, purify, update, flush, feedback;
    bool fail;
    uint64_t change_admission_to;
};

static bool declarative_space_phase3c_normal(struct window *window, void *opaque)
{ (void) window; ++((struct declarative_space_phase3c_ops_context *) opaque)->normal; return !((struct declarative_space_phase3c_ops_context *) opaque)->fail; }
static bool declarative_space_phase3c_below(struct window *window, void *opaque)
{ (void) window; ++((struct declarative_space_phase3c_ops_context *) opaque)->below; return !((struct declarative_space_phase3c_ops_context *) opaque)->fail; }
static bool declarative_space_phase3c_purify(struct window *window, void *opaque)
{ (void) window; ++((struct declarative_space_phase3c_ops_context *) opaque)->purify; return !((struct declarative_space_phase3c_ops_context *) opaque)->fail; }
static bool declarative_space_phase3c_update(struct view *view, void *opaque)
{
    (void) view;
    struct declarative_space_phase3c_ops_context *context = opaque;
    ++context->update;
    if (context->change_admission_to) declarative_space_test_set_admission(context->change_admission_to, true);
    return !context->fail;
}
static bool declarative_space_phase3c_flush(struct view *view, void *opaque)
{ (void) view; ++((struct declarative_space_phase3c_ops_context *) opaque)->flush; return !((struct declarative_space_phase3c_ops_context *) opaque)->fail; }
static bool declarative_space_phase3c_feedback(struct window_node *node, void *opaque)
{ (void) node; ++((struct declarative_space_phase3c_ops_context *) opaque)->feedback; return !((struct declarative_space_phase3c_ops_context *) opaque)->fail; }

static void declarative_space_phase3c_free_live_root(struct window_node *node)
{
    if (!node) return;
    declarative_space_phase3c_free_live_root(node->left);
    declarative_space_phase3c_free_live_root(node->right);
    free(node);
}

TEST_FUNC(declarative_space_phase3c_publication_postcommit_and_retirement_are_ordered,
{
    declarative_space_test_fail_allocation_at(0);
    declarative_space_test_set_admission(41, true);
    uint64_t sid = 3001;
    uint32_t old_wid = 10, new_wid = 11;
    struct window_node *old_root = calloc(1, sizeof(*old_root));
    old_root->window_list[0] = old_root->window_order[0] = old_wid; old_root->window_count = 1;
    old_root->insert_dir = STACK; old_root->feedback_window.id = 99;
    old_root->feedback_window.context = (CGContextRef) (uintptr_t) 1;
    struct view view = { .sid = sid, .uuid = CFSTR("phase3c"), .layout = VIEW_STACK, .root = old_root,
                         .flags = VIEW_IS_VALID, .window_gap = 4 };
    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view); table_add(&managed, &old_wid, &view);
    g_window_manager.managed_window = managed;
    memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback));
    table_init(&g_window_manager.insert_feedback, 8, hash_wm, compare_wm);
    table_add(&g_window_manager.insert_feedback, &old_wid, old_root);
    g_mouse_state.feedback_node = old_root;
    struct declarative_space_target target = { &view, sid, 1, "phase3c", true };
    struct declarative_space_topology_entry topology = { sid, 1, "phase3c", true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { new_wid } }, .counts = { 1 } };
    struct application application = { .name = "Mail" };
    capture.windows[new_wid].application = &application;
    struct declarative_space_capture_ops capture_ops = { declarative_space_test_query, declarative_space_test_find,
                                                          declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_space_policy policy = { .by_index = true, .index = 1, .scaffold_count = 1 };
    policy.scaffold[0].pattern = "^Mail$";
    struct declarative_config_model native = { .spaces = &policy, .space_count = 1 };
    native.config[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_value) { .present = true, .integer = 17 };
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc,
                                         &g_window_manager.managed_window, &mc, &native, NULL, &defaults,
                                         DECLARATIVE_SPACE_PRESERVE_RUNTIME, &capture_ops, &preparation), true);
    struct declarative_space_transition transition;
    TEST_CHECK(declarative_space_transition_begin(&transition, &preparation), true);
    TEST_CHECK(declarative_space_retire_old_roots(&transition, NULL), false);
    TEST_CHECK(declarative_space_transition_commit(&transition), true);
    TEST_CHECK(view.root != old_root, true);
    TEST_CHECK(view.window_gap, 17);
    TEST_CHECK(view.scaffold_fingerprint != 0, true);
    TEST_CHECK(view.scaffold_primary[0], new_wid);
    TEST_CHECK(view.scaffold_primary[1], 0U);
    TEST_CHECK(view.scaffold_primary[2], 0U);
    TEST_CHECK(table_find(&g_window_manager.managed_window, &new_wid), &view);
    TEST_CHECK(table_find(&g_window_manager.managed_window, &old_wid) == NULL, true);
    TEST_CHECK(view_is_invalid(&view), true); TEST_CHECK(view_is_dirty(&view), true);
    TEST_CHECK(declarative_space_test_commit_forbidden_calls(), 0UL);
    TEST_CHECK(declarative_space_transition_commit(&transition), false);

    struct declarative_space_phase3c_ops_context context = { .fail = true, .change_admission_to = 42 };
    struct declarative_space_postcommit_ops post = {
        declarative_space_phase3c_normal, declarative_space_phase3c_below,
        declarative_space_phase3c_purify, declarative_space_phase3c_update,
        declarative_space_phase3c_flush, declarative_space_phase3c_feedback, &context
    };
    TEST_CHECK(declarative_space_transition_postcommit(&transition, &post), true);
    TEST_CHECK(context.normal, 1UL);
    TEST_CHECK(context.below, 1UL); TEST_CHECK(context.purify, 2UL);
    TEST_CHECK(context.update, 1UL); TEST_CHECK(context.flush, 1UL);
    TEST_CHECK(transition.diagnostics.failed, 6UL);
    TEST_CHECK(table_find(&g_window_manager.managed_window, &new_wid), &view);
    TEST_CHECK(view_is_invalid(&view), true); TEST_CHECK(view_is_dirty(&view), true);
    TEST_CHECK(declarative_space_retire_old_roots(&transition, &post), false);
    TEST_CHECK(declarative_space_dispose_retired_table(&transition), true);
    TEST_CHECK(transition.retired_managed_window.buckets == NULL, true);
    TEST_CHECK(transition.retired_managed_window.capacity, 0);
    TEST_CHECK(declarative_space_retire_old_roots(&transition, &post), true);
    TEST_CHECK(context.feedback, 1UL);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &old_wid) == NULL, true);
    TEST_CHECK(table_find(&g_window_manager.managed_window, &new_wid), &view);
    TEST_CHECK(declarative_space_retire_old_roots(&transition, &post), false);
    declarative_space_transition_destroy(&transition);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);

    declarative_space_phase3c_free_live_root(view.root);
    table_free(&g_window_manager.managed_window); memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window));
    table_free(&g_window_manager.insert_feedback); memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback));
    table_free(&views);
})

TEST_FUNC(declarative_space_phase3c_stale_scalar_and_event_turn_reject_before_publication,
{
    declarative_space_test_fail_allocation_at(0);
    declarative_space_test_set_admission(51, true);
    uint64_t sid = 3002; uint32_t wid = 20;
    struct window_node *root = calloc(1, sizeof(*root)); root->window_count = 1; root->window_list[0] = root->window_order[0] = wid;
    struct view view = { .sid = sid, .uuid = CFSTR("phase3c-stale"), .layout = VIEW_STACK, .root = root };
    struct table views = { 0 }, managed = { 0 }; table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&views, &sid, &view); table_add(&managed, &wid, &view); g_window_manager.managed_window = managed;
    struct declarative_space_target target = { &view, sid, 1, "phase3c-stale", true };
    struct declarative_space_topology_entry topology = { sid, 1, "phase3c-stale", true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) }, mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { .sids = { sid }, .wids = { { wid } }, .counts = { 1 } };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find, declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults(); defaults.layout = VIEW_STACK;
    struct declarative_space_preparation preparation; struct declarative_space_transition transition;
    TEST_CHECK(declarative_space_prepare(&target, 1, &topology, 1, &views, &vc, &g_window_manager.managed_window, &mc,
                                         NULL, NULL, &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preparation), true);
    TEST_CHECK(declarative_space_transition_begin(&transition, &preparation), true);
    view.top_padding++;
    declarative_space_test_set_assertion_observer(NULL, NULL);
    TEST_CHECK(declarative_space_transition_commit(&transition), false);
    TEST_CHECK(declarative_space_test_assertion_count(), 1UL);
    TEST_CHECK(view.root, root); TEST_CHECK(g_window_manager.managed_window.buckets, managed.buckets);
    view.top_padding--;
    declarative_space_test_set_admission(52, true);
    TEST_CHECK(declarative_space_transition_commit(&transition), false);
    TEST_CHECK(declarative_space_test_assertion_count(), 2UL);
    declarative_space_test_set_admission(51, true);
    declarative_space_transition_destroy(&transition);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    free(root); table_free(&g_window_manager.managed_window); memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window)); table_free(&views);
})

TEST_FUNC(declarative_space_phase3c_rejects_shared_empty_roots_across_views,
{
    declarative_space_test_fail_allocation_at(0);
    struct window_node *shared = calloc(1, sizeof(*shared));
    struct view first = { .sid = 3101, .layout = VIEW_STACK, .root = shared };
    struct view second = { .sid = 3102, .layout = VIEW_STACK, .root = shared };
    struct declarative_space_target targets[2] = {
        { &first, 3101, 1, "first", true }, { &second, 3102, 2, "second", true }
    };
    struct table managed = { 0 };
    table_init(&managed, 8, hash_wm, compare_wm);
    struct declarative_space_table_contract contract = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_root_before_image roots[2];
    struct declarative_space_table_before_image table_image;
    TEST_CHECK(declarative_space_validate_forest_and_table(targets, 2, &managed, &contract,
                                                           roots, &table_image), false);
    first.layout = second.layout = VIEW_FLOAT;
    TEST_CHECK(declarative_space_validate_forest_and_table(targets, 2, &managed, &contract,
                                                           roots, &table_image), false);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed);
    free(shared);
})

struct declarative_space_phase3c_fixture
{
    uint64_t sid;
    uint32_t wid;
    struct window_node *root;
    struct view view;
    struct table views;
    struct declarative_runtime_state runtime;
    struct declarative_space_test_capture capture;
    struct declarative_space_preparation preparation;
    struct declarative_space_transition transition;
};

static unsigned long declarative_space_phase3c_alt_hash(void *key)
{ return hash_wm(key) + 1; }
static int declarative_space_phase3c_alt_cmp(void *a, void *b)
{ return compare_wm(a, b); }

static bool declarative_space_phase3c_fixture_begin(struct declarative_space_phase3c_fixture *fixture,
                                                     uint64_t sid, uint32_t wid)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->sid = sid; fixture->wid = wid;
    fixture->root = calloc(1, sizeof(*fixture->root));
    fixture->root->window_count = 1;
    fixture->root->window_list[0] = fixture->root->window_order[0] = wid;
    fixture->root->ratio = 0.5f;
    fixture->root->area = (struct area) { 1, 2, 300, 200 };
    fixture->view = (struct view) { .uuid = CFSTR("phase3c-matrix"), .sid = sid, .layout = VIEW_STACK, .root = fixture->root,
        .top_padding = 1, .bottom_padding = 2, .left_padding = 3, .right_padding = 4,
        .window_gap = 5, .split_type = SPLIT_AUTO, .auto_balance = SPLIT_X | SPLIT_Y,
        .insertion_point = wid, .flags = VIEW_ENABLE_PADDING | VIEW_ENABLE_GAP | VIEW_IS_VALID };
    table_init(&fixture->views, 8, hash_wm, compare_wm);
    table_init(&g_window_manager.managed_window, 8, hash_wm, compare_wm);
    table_add(&fixture->views, &fixture->sid, &fixture->view);
    table_add(&g_window_manager.managed_window, &fixture->wid, &fixture->view);
    fixture->capture.sids[0] = sid; fixture->capture.wids[0][0] = wid; fixture->capture.counts[0] = 1;
    struct declarative_space_target target = { &fixture->view, sid, 1, "phase3c-matrix", true };
    struct declarative_space_topology_entry topology = { sid, 1, "phase3c-matrix", true };
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find,
                                                  declarative_space_test_eligible, &fixture->capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults(); defaults.layout = VIEW_STACK;
    if (!declarative_space_prepare(&target, 1, &topology, 1, &fixture->views, &vc,
                                   &g_window_manager.managed_window, &mc, NULL, &fixture->runtime,
                                   &defaults, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops,
                                   &fixture->preparation)) return false;
    return declarative_space_transition_begin(&fixture->transition, &fixture->preparation);
}

static void declarative_space_phase3c_fixture_end(struct declarative_space_phase3c_fixture *fixture)
{
    declarative_space_transition_destroy(&fixture->transition);
    declarative_space_phase3c_free_live_root(fixture->view.root);
    if (g_window_manager.managed_window.buckets) table_free(&g_window_manager.managed_window);
    memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window));
    table_free(&fixture->views);
}

static void declarative_space_phase3c_observe(void *context)
{ ++*(size_t *) context; }

enum declarative_space_phase3c_stale_kind
{
    PHASE3C_ROOT_POINTER,
    PHASE3C_ROOT_WINDOW_COUNT, PHASE3C_ROOT_PARENT, PHASE3C_ROOT_LEFT, PHASE3C_ROOT_RIGHT,
    PHASE3C_ROOT_ZOOM, PHASE3C_ROOT_SPLIT, PHASE3C_ROOT_CHILD, PHASE3C_ROOT_INSERT,
    PHASE3C_ROOT_FEEDBACK_ID, PHASE3C_ROOT_FEEDBACK_CONTEXT, PHASE3C_ROOT_RATIO,
    PHASE3C_ROOT_AREA_X, PHASE3C_ROOT_AREA_Y, PHASE3C_ROOT_AREA_W, PHASE3C_ROOT_AREA_H,
    PHASE3C_ROOT_LIST, PHASE3C_ROOT_ORDER,
    PHASE3C_TABLE_BUCKETS, PHASE3C_TABLE_BUCKET_HEAD, PHASE3C_TABLE_KEY_POINTER,
    PHASE3C_TABLE_KEY_CONTENT, PHASE3C_TABLE_VALUE, PHASE3C_TABLE_NEXT,
    PHASE3C_TABLE_COUNT, PHASE3C_TABLE_CAPACITY, PHASE3C_TABLE_MAX_LOAD,
    PHASE3C_TABLE_HASH, PHASE3C_TABLE_CMP,
    PHASE3C_SCALAR_LAYOUT, PHASE3C_SCALAR_TOP, PHASE3C_SCALAR_BOTTOM, PHASE3C_SCALAR_LEFT,
    PHASE3C_SCALAR_RIGHT, PHASE3C_SCALAR_GAP, PHASE3C_SCALAR_SINGLE_WINDOW_MAX_WIDTH,
    PHASE3C_SCALAR_SINGLE_WINDOW_MIN_WIDTH,
    PHASE3C_SCALAR_SINGLE_WINDOW_WIDTH_RATIO,
    PHASE3C_SCALAR_RATIO, PHASE3C_SCALAR_SPLIT,
    PHASE3C_SCALAR_BALANCE,
    PHASE3C_INSERTION, PHASE3C_SCAFFOLD_FINGERPRINT, PHASE3C_SCAFFOLD_PRIMARY, PHASE3C_COMPLETE_FLAGS,
    PHASE3C_ROOT_POINTER_NULL, PHASE3C_ROOT_WINDOW_COUNT_NEGATIVE,
    PHASE3C_ROOT_PARENT_SELF, PHASE3C_ROOT_LEFT_SELF, PHASE3C_ROOT_ZOOM_SELF,
    PHASE3C_TABLE_BUCKETS_NULL, PHASE3C_TABLE_BUCKET_HEAD_NULL, PHASE3C_TABLE_BUCKET_SLOT,
    PHASE3C_TABLE_KEY_CONTENT_ZERO, PHASE3C_TABLE_VALUE_ALTERNATE,
    PHASE3C_SCALAR_FLAGS_PADDING, PHASE3C_INSERTION_ZERO,
    PHASE3C_GLOBAL_LAYOUT_SOURCE, PHASE3C_GLOBAL_GAP_VALUE,
    PHASE3C_VIEW_LAYOUT_SOURCE, PHASE3C_VIEW_GAP_VALUE, PHASE3C_LEGACY_VALID_FLAG,
    PHASE3C_GLOBAL_FIRST,
    PHASE3C_GLOBAL_LAST = PHASE3C_GLOBAL_FIRST + DECLARATIVE_CONFIG_COUNT - 1,
    PHASE3C_VIEW_FIRST,
    PHASE3C_VIEW_LAST = PHASE3C_VIEW_FIRST + DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT - 1,
    PHASE3C_LEGACY_FIRST,
    PHASE3C_LEGACY_LAST = PHASE3C_LEGACY_FIRST + DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT - 1,
    PHASE3C_STALE_CASE_COUNT
};

_Static_assert(PHASE3C_STALE_CASE_COUNT == 132, "Phase 3C stale matrix must contain exactly 132 cases");

TEST_FUNC(declarative_space_phase3c_generated_stale_before_image_matrix,
{
    static const uint64_t legacy_flags[DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT] = {
        VIEW_LAYOUT, VIEW_TOP_PADDING, VIEW_BOTTOM_PADDING, VIEW_LEFT_PADDING,
        VIEW_RIGHT_PADDING, VIEW_WINDOW_GAP, VIEW_ACCORDION_PADDING, VIEW_SINGLE_WINDOW_MAX_WIDTH,
        VIEW_SINGLE_WINDOW_MIN_WIDTH,
        VIEW_SINGLE_WINDOW_WIDTH_RATIO, VIEW_SPLIT_RATIO,
        VIEW_SPLIT_TYPE, VIEW_AUTO_BALANCE
    };
    size_t cases_run = 0;
    for (int kind = 0; kind < PHASE3C_STALE_CASE_COUNT; ++kind) {
        declarative_space_test_fail_allocation_at(0);
        declarative_space_test_set_admission(7000 + kind, true);
        struct declarative_space_phase3c_fixture f;
        bool began = declarative_space_phase3c_fixture_begin(&f, 4000 + kind, 100 + kind);
        if (!began) fprintf(stderr, "phase3c stale fixture failed: %d\n", kind);
        TEST_CHECK(began, true);
        if (!began) break;
        struct view original_view = f.view;
        struct table original_table = g_window_manager.managed_window;
        struct window_node original_root = *f.root;
        struct bucket *bucket = NULL;
        int bucket_slot = -1;
        for (int i = 0; i < original_table.capacity && !bucket; ++i) {
            bucket = original_table.buckets[i];
            if (bucket) bucket_slot = i;
        }
        struct bucket original_bucket = *bucket;
        struct window_node *extra_node = calloc(1, sizeof(*extra_node));
        struct bucket *extra_bucket = calloc(1, sizeof(*extra_bucket));
        uint32_t *extra_key = malloc(sizeof(*extra_key)); *extra_key = f.wid + 10000;
        extra_bucket->key = extra_key; extra_bucket->value = &f.view;
        struct bucket **extra_buckets = calloc((size_t) original_table.capacity, sizeof(*extra_buckets));

        if (kind == PHASE3C_ROOT_POINTER) f.view.root = extra_node;
        else if (kind == PHASE3C_ROOT_WINDOW_COUNT) f.root->window_count = 0;
        else if (kind == PHASE3C_ROOT_PARENT) f.root->parent = extra_node;
        else if (kind == PHASE3C_ROOT_LEFT) f.root->left = extra_node;
        else if (kind == PHASE3C_ROOT_RIGHT) f.root->right = extra_node;
        else if (kind == PHASE3C_ROOT_ZOOM) f.root->zoom = extra_node;
        else if (kind == PHASE3C_ROOT_SPLIT) f.root->split = SPLIT_X;
        else if (kind == PHASE3C_ROOT_CHILD) f.root->child = CHILD_FIRST;
        else if (kind == PHASE3C_ROOT_INSERT) f.root->insert_dir = DIR_NORTH;
        else if (kind == PHASE3C_ROOT_FEEDBACK_ID) f.root->feedback_window.id = 9;
        else if (kind == PHASE3C_ROOT_FEEDBACK_CONTEXT) f.root->feedback_window.context = (CGContextRef) (uintptr_t) 1;
        else if (kind == PHASE3C_ROOT_RATIO) f.root->ratio = 0.75f;
        else if (kind == PHASE3C_ROOT_AREA_X) f.root->area.x++;
        else if (kind == PHASE3C_ROOT_AREA_Y) f.root->area.y++;
        else if (kind == PHASE3C_ROOT_AREA_W) f.root->area.w++;
        else if (kind == PHASE3C_ROOT_AREA_H) f.root->area.h++;
        else if (kind == PHASE3C_ROOT_LIST) f.root->window_list[0]++;
        else if (kind == PHASE3C_ROOT_ORDER) f.root->window_order[0]++;
        else if (kind == PHASE3C_TABLE_BUCKETS) g_window_manager.managed_window.buckets = extra_buckets;
        else if (kind == PHASE3C_TABLE_BUCKET_HEAD) {
            int slot = (int) (original_table.hash(&f.wid) % (unsigned long) original_table.capacity);
            original_table.buckets[slot] = extra_bucket;
        } else if (kind == PHASE3C_TABLE_KEY_POINTER) bucket->key = extra_key;
        else if (kind == PHASE3C_TABLE_KEY_CONTENT) (*(uint32_t *) bucket->key)++;
        else if (kind == PHASE3C_TABLE_VALUE) bucket->value = NULL;
        else if (kind == PHASE3C_TABLE_NEXT) bucket->next = extra_bucket;
        else if (kind == PHASE3C_TABLE_COUNT) g_window_manager.managed_window.count++;
        else if (kind == PHASE3C_TABLE_CAPACITY) g_window_manager.managed_window.capacity--;
        else if (kind == PHASE3C_TABLE_MAX_LOAD) g_window_manager.managed_window.max_load = 0.5f;
        else if (kind == PHASE3C_TABLE_HASH) g_window_manager.managed_window.hash = declarative_space_phase3c_alt_hash;
        else if (kind == PHASE3C_TABLE_CMP) g_window_manager.managed_window.cmp = declarative_space_phase3c_alt_cmp;
        else if (kind == PHASE3C_SCALAR_LAYOUT) f.view.layout = VIEW_FLOAT;
        else if (kind == PHASE3C_SCALAR_TOP) f.view.top_padding++;
        else if (kind == PHASE3C_SCALAR_BOTTOM) f.view.bottom_padding++;
        else if (kind == PHASE3C_SCALAR_LEFT) f.view.left_padding++;
        else if (kind == PHASE3C_SCALAR_RIGHT) f.view.right_padding++;
        else if (kind == PHASE3C_SCALAR_GAP) f.view.window_gap++;
        else if (kind == PHASE3C_SCALAR_SINGLE_WINDOW_MAX_WIDTH) f.view.single_window_max_width++;
        else if (kind == PHASE3C_SCALAR_SINGLE_WINDOW_MIN_WIDTH) f.view.single_window_min_width++;
        else if (kind == PHASE3C_SCALAR_SINGLE_WINDOW_WIDTH_RATIO) f.view.single_window_width_ratio = 0.85f;
        else if (kind == PHASE3C_SCALAR_RATIO) f.view.split_ratio = 0.6f;
        else if (kind == PHASE3C_SCALAR_SPLIT) f.view.split_type = SPLIT_X;
        else if (kind == PHASE3C_SCALAR_BALANCE) f.view.auto_balance = SPLIT_NONE;
        else if (kind == PHASE3C_INSERTION) f.view.insertion_point++;
        else if (kind == PHASE3C_SCAFFOLD_FINGERPRINT) f.view.scaffold_fingerprint++;
        else if (kind == PHASE3C_SCAFFOLD_PRIMARY) f.view.scaffold_primary[0]++;
        else if (kind == PHASE3C_COMPLETE_FLAGS) f.view.flags ^= VIEW_IS_DIRTY;
        else if (kind == PHASE3C_ROOT_POINTER_NULL) f.view.root = NULL;
        else if (kind == PHASE3C_ROOT_WINDOW_COUNT_NEGATIVE) f.root->window_count = -1;
        else if (kind == PHASE3C_ROOT_PARENT_SELF) f.root->parent = f.root;
        else if (kind == PHASE3C_ROOT_LEFT_SELF) f.root->left = f.root;
        else if (kind == PHASE3C_ROOT_ZOOM_SELF) f.root->zoom = f.root;
        else if (kind == PHASE3C_TABLE_BUCKETS_NULL) g_window_manager.managed_window.buckets = NULL;
        else if (kind == PHASE3C_TABLE_BUCKET_HEAD_NULL) original_table.buckets[bucket_slot] = NULL;
        else if (kind == PHASE3C_TABLE_BUCKET_SLOT) {
            int alternate_slot = (bucket_slot + 1) % original_table.capacity;
            original_table.buckets[bucket_slot] = NULL;
            original_table.buckets[alternate_slot] = bucket;
        } else if (kind == PHASE3C_TABLE_KEY_CONTENT_ZERO) *(uint32_t *) bucket->key = 0;
        else if (kind == PHASE3C_TABLE_VALUE_ALTERNATE) bucket->value = extra_node;
        else if (kind == PHASE3C_SCALAR_FLAGS_PADDING) f.view.flags ^= VIEW_ENABLE_PADDING;
        else if (kind == PHASE3C_INSERTION_ZERO) f.view.insertion_point = 0;
        else if (kind == PHASE3C_GLOBAL_LAYOUT_SOURCE)
            f.runtime.global[DECLARATIVE_CONFIG_LAYOUT].source = DECLARATIVE_SETTING_RUNTIME_GLOBAL;
        else if (kind == PHASE3C_GLOBAL_GAP_VALUE)
            f.runtime.global[DECLARATIVE_CONFIG_WINDOW_GAP].value.integer = 1;
        else if (kind == PHASE3C_VIEW_LAYOUT_SOURCE)
            f.view.runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT].source = DECLARATIVE_SETTING_RUNTIME_SPACE;
        else if (kind == PHASE3C_VIEW_GAP_VALUE)
            f.view.runtime_ownership[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer = 1;
        else if (kind == PHASE3C_LEGACY_VALID_FLAG) f.view.flags ^= VIEW_IS_VALID;
        else if (kind >= PHASE3C_GLOBAL_FIRST && kind <= PHASE3C_GLOBAL_LAST)
            f.runtime.global[kind - PHASE3C_GLOBAL_FIRST].present = true;
        else if (kind >= PHASE3C_VIEW_FIRST && kind <= PHASE3C_VIEW_LAST)
            f.view.runtime_ownership[kind - PHASE3C_VIEW_FIRST].present = true;
        else f.view.flags ^= legacy_flags[kind - PHASE3C_LEGACY_FIRST];

        struct window_node *live_root_after_mutation = f.view.root;
        struct bucket **live_buckets_after_mutation = g_window_manager.managed_window.buckets;
        size_t observed = 0;
        declarative_space_test_set_assertion_observer(declarative_space_phase3c_observe, &observed);
        bool rejected = !declarative_space_transition_commit(&f.transition);
        if (!rejected) fprintf(stderr, "phase3c stale case unexpectedly committed: %d\n", kind);
        TEST_CHECK(rejected, true);
        TEST_CHECK(observed, 1UL); TEST_CHECK(declarative_space_test_assertion_count(), 1UL);
        TEST_CHECK(f.transition.state, DECLARATIVE_SPACE_TRANSITION_PREPARED);
        TEST_CHECK(f.transition.retired_managed_window.buckets == NULL, true);
        TEST_CHECK(f.view.root, live_root_after_mutation);
        TEST_CHECK(g_window_manager.managed_window.buckets, live_buckets_after_mutation);

        f.view = original_view; *f.root = original_root;
        if (kind == PHASE3C_TABLE_BUCKET_SLOT) {
            int alternate_slot = (bucket_slot + 1) % original_table.capacity;
            original_table.buckets[alternate_slot] = NULL;
        }
        original_table.buckets[bucket_slot] = bucket;
        g_window_manager.managed_window = original_table; *bucket = original_bucket;
        memcpy(bucket->key, &f.wid, sizeof(f.wid));
        declarative_space_test_set_assertion_observer(NULL, NULL);
        free(extra_buckets); free(extra_bucket); free(extra_key); free(extra_node);
        declarative_space_phase3c_fixture_end(&f);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        ++cases_run;
    }
    TEST_CHECK(cases_run, 132UL);
})

struct declarative_space_phase3c_outcome_context
{
    unsigned failures;
    size_t normal, below, purify, update, flush, feedback;
};
enum { PHASE3C_FAIL_NORMAL = 1, PHASE3C_FAIL_BELOW = 2, PHASE3C_FAIL_PURIFY = 4,
       PHASE3C_FAIL_UPDATE = 8, PHASE3C_FAIL_FLUSH = 16, PHASE3C_FAIL_FEEDBACK = 32 };
static bool declarative_space_phase3c_outcome_normal(struct window *w, void *p)
{ (void) w; struct declarative_space_phase3c_outcome_context *c=p; ++c->normal; return !(c->failures & PHASE3C_FAIL_NORMAL); }
static bool declarative_space_phase3c_outcome_below(struct window *w, void *p)
{ (void) w; struct declarative_space_phase3c_outcome_context *c=p; ++c->below; return !(c->failures & PHASE3C_FAIL_BELOW); }
static bool declarative_space_phase3c_outcome_purify(struct window *w, void *p)
{ (void) w; struct declarative_space_phase3c_outcome_context *c=p; ++c->purify; return !(c->failures & PHASE3C_FAIL_PURIFY); }
static bool declarative_space_phase3c_outcome_update(struct view *v, void *p)
{ (void) v; struct declarative_space_phase3c_outcome_context *c=p; ++c->update; return !(c->failures & PHASE3C_FAIL_UPDATE); }
static bool declarative_space_phase3c_outcome_flush(struct view *v, void *p)
{ struct declarative_space_phase3c_outcome_context *c=p; ++c->flush; if (c->failures & PHASE3C_FAIL_FLUSH) return false; view_clear_flag(v, VIEW_IS_DIRTY); return true; }
static bool declarative_space_phase3c_outcome_feedback(struct window_node *n, void *p)
{ (void) n; struct declarative_space_phase3c_outcome_context *c=p; ++c->feedback; return !(c->failures & PHASE3C_FAIL_FEEDBACK); }

TEST_FUNC(declarative_space_phase3c_generated_postcommit_outcome_matrix,
{
    static const unsigned outcomes[] = { 0, PHASE3C_FAIL_NORMAL, PHASE3C_FAIL_BELOW, PHASE3C_FAIL_PURIFY,
        PHASE3C_FAIL_UPDATE, PHASE3C_FAIL_FLUSH, PHASE3C_FAIL_FEEDBACK, 63 };
    size_t cases_run = 0;
    for (size_t i = 0; i < sizeof(outcomes)/sizeof(outcomes[0]); ++i) {
        declarative_space_test_set_admission(8000 + i, true);
        uint64_t sid = 6000 + i; uint32_t old_wid = 500 + i, new_wid = 700 + i;
        struct window_node *old_root = calloc(1, sizeof(*old_root)); old_root->window_count = 1;
        old_root->window_list[0] = old_root->window_order[0] = old_wid;
        old_root->insert_dir = STACK;
        old_root->feedback_window.id = 1; old_root->feedback_window.context = (CGContextRef)(uintptr_t)1;
        struct view view = { .uuid=CFSTR("outcome"), .sid=sid, .layout=VIEW_STACK, .root=old_root, .flags=VIEW_IS_VALID };
        struct table views={0}; table_init(&views,8,hash_wm,compare_wm); table_init(&g_window_manager.managed_window,8,hash_wm,compare_wm);
        table_add(&views,&sid,&view); table_add(&g_window_manager.managed_window,&old_wid,&view);
        memset(&g_window_manager.insert_feedback,0,sizeof(g_window_manager.insert_feedback)); table_init(&g_window_manager.insert_feedback,8,hash_wm,compare_wm);
        table_add(&g_window_manager.insert_feedback,&old_wid,old_root); g_mouse_state.feedback_node=old_root;
        struct declarative_space_target target={&view,sid,1,"outcome",true}; struct declarative_space_topology_entry topology={sid,1,"outcome",true};
        struct declarative_space_table_contract vc={hash_wm,compare_wm,sizeof(uint64_t)}, mc={hash_wm,compare_wm,sizeof(uint32_t)};
        struct declarative_space_test_capture capture={.sids={sid},.wids={{new_wid}},.counts={1}};
        struct declarative_space_capture_ops cops={declarative_space_test_query,declarative_space_test_find,declarative_space_test_eligible,&capture};
        struct declarative_space_defaults defaults=declarative_space_test_defaults(); defaults.layout=VIEW_STACK;
        struct declarative_space_preparation prep; struct declarative_space_transition transition;
        TEST_CHECK(declarative_space_prepare(&target,1,&topology,1,&views,&vc,&g_window_manager.managed_window,&mc,NULL,NULL,&defaults,DECLARATIVE_SPACE_PRESERVE_RUNTIME,&cops,&prep),true);
        TEST_CHECK(declarative_space_transition_begin(&transition,&prep),true); TEST_CHECK(declarative_space_transition_commit(&transition),true);
        struct declarative_space_phase3c_outcome_context context={.failures=outcomes[i]};
        struct declarative_space_postcommit_ops pops={declarative_space_phase3c_outcome_normal,declarative_space_phase3c_outcome_below,
            declarative_space_phase3c_outcome_purify,declarative_space_phase3c_outcome_update,declarative_space_phase3c_outcome_flush,
            declarative_space_phase3c_outcome_feedback,&context};
        TEST_CHECK(declarative_space_transition_postcommit(&transition,&pops),true);
        TEST_CHECK(declarative_space_dispose_retired_table(&transition),true); TEST_CHECK(declarative_space_retire_old_roots(&transition,&pops),true);
        TEST_CHECK(context.normal,1UL); TEST_CHECK(context.below,1UL); TEST_CHECK(context.purify,2UL);
        TEST_CHECK(context.update,1UL); TEST_CHECK(context.flush,1UL); TEST_CHECK(context.feedback,1UL);
        TEST_CHECK(transition.diagnostics.attempted,7UL);
        size_t expected_failed = !!(outcomes[i]&1)+!!(outcomes[i]&2)+2*!!(outcomes[i]&4)+!!(outcomes[i]&8)+!!(outcomes[i]&16)+!!(outcomes[i]&32);
        TEST_CHECK(transition.diagnostics.failed,expected_failed);
        TEST_CHECK(transition.diagnostics.layer_normal_failed,(size_t)!!(outcomes[i]&1));
        TEST_CHECK(transition.diagnostics.layer_below_failed,(size_t)!!(outcomes[i]&2));
        TEST_CHECK(transition.diagnostics.purify_failed,(size_t)(2*!!(outcomes[i]&4)));
        TEST_CHECK(transition.diagnostics.update_failed,(size_t)!!(outcomes[i]&8));
        TEST_CHECK(transition.diagnostics.flush_failed,(size_t)!!(outcomes[i]&16));
        TEST_CHECK(transition.diagnostics.feedback_failed,(size_t)!!(outcomes[i]&32));
        bool clean = !(outcomes[i] & (PHASE3C_FAIL_UPDATE|PHASE3C_FAIL_FLUSH));
        TEST_CHECK(view_is_invalid(&view),!clean); TEST_CHECK(view_is_dirty(&view),!clean);
        TEST_CHECK(table_find(&g_window_manager.managed_window,&new_wid),&view);
        TEST_CHECK(g_mouse_state.feedback_node==NULL,true); TEST_CHECK(table_find(&g_window_manager.insert_feedback,&old_wid)==NULL,true);
        declarative_space_transition_destroy(&transition); declarative_space_phase3c_free_live_root(view.root);
        table_free(&g_window_manager.managed_window); memset(&g_window_manager.managed_window,0,sizeof(g_window_manager.managed_window));
        table_free(&g_window_manager.insert_feedback); memset(&g_window_manager.insert_feedback,0,sizeof(g_window_manager.insert_feedback)); table_free(&views);
        TEST_CHECK(declarative_space_test_outstanding_allocations(),0UL); ++cases_run;
    }
    TEST_CHECK(cases_run,8UL);
})

TEST_FUNC(declarative_space_phase3c_ownership_modes_zero_one_multi_view_matrix,
{
    size_t cases_run = 0;
    for (size_t target_count = 0; target_count <= 2; ++target_count) {
        for (int clear = 0; clear <= 1; ++clear) {
            declarative_space_test_set_admission(8500 + cases_run, true);
            struct view live[2] = { 0 };
            struct declarative_space_target targets[2] = { 0 };
            struct declarative_space_topology_entry topology[2] = { 0 };
            struct table views = { 0 };
            table_init(&views, 8, hash_wm, compare_wm);
            table_init(&g_window_manager.managed_window, 8, hash_wm, compare_wm);
            struct declarative_runtime_state runtime = { 0 }, runtime_before;
            runtime.global[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_runtime_cell) {
                .present = true, .source = DECLARATIVE_SETTING_RUNTIME_GLOBAL, .value.integer = 44
            };
            runtime_before = runtime;
            struct declarative_space_test_capture capture = { 0 };
            for (size_t i = 0; i < target_count; ++i) {
                uint64_t sid = 6500 + cases_run * 10 + i;
                live[i] = (struct view) { .uuid = i ? CFSTR("ownership-b") : CFSTR("ownership-a"),
                    .sid = sid, .root = calloc(1, sizeof(struct window_node)), .layout = VIEW_FLOAT, .top_padding = 1, .bottom_padding = 2,
                    .left_padding = 3, .right_padding = 4, .window_gap = 44,
                    .split_type = SPLIT_AUTO, .auto_balance = SPLIT_X | SPLIT_Y,
                    .flags = VIEW_LAYOUT | VIEW_TOP_PADDING | VIEW_ENABLE_PADDING | VIEW_ENABLE_GAP | VIEW_IS_VALID };
                live[i].runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT] = (struct declarative_runtime_cell) {
                    .present = true, .source = DECLARATIVE_SETTING_RUNTIME_SPACE, .value.integer = VIEW_FLOAT
                };
                live[i].runtime_ownership[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING] = (struct declarative_runtime_cell) {
                    .present = true, .source = DECLARATIVE_SETTING_RUNTIME_SPACE, .value.integer = 1
                };
                const char *uuid = i ? "ownership-b" : "ownership-a";
                targets[i] = (struct declarative_space_target) { &live[i], sid, (int)i + 1, uuid, true };
                topology[i] = (struct declarative_space_topology_entry) { sid, (int)i + 1, uuid, true };
                table_add(&views, &live[i].sid, &live[i]);
                capture.sids[i] = sid;
            }
            struct view before[2]; memcpy(before, live, sizeof(live));
            struct declarative_space_capture_ops cops = { declarative_space_test_query, declarative_space_test_find,
                                                           declarative_space_test_eligible, &capture };
            struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
            struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
            struct declarative_space_defaults defaults = declarative_space_test_defaults(); defaults.layout = VIEW_FLOAT;
            struct declarative_space_preparation prep; struct declarative_space_transition transition;
            bool prepared = declarative_space_prepare(targets, target_count, topology, target_count, &views, &vc,
                &g_window_manager.managed_window, &mc, NULL, &runtime, &defaults,
                clear ? DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD : DECLARATIVE_SPACE_PRESERVE_RUNTIME,
                &cops, &prep);
            if (!prepared) fprintf(stderr, "phase3c ownership prepare failed: %zu/%d\n", target_count, clear);
            TEST_CHECK(prepared, true);
            if (!prepared) { table_free(&g_window_manager.managed_window); memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window)); table_free(&views); continue; }
            TEST_CHECK(declarative_space_transition_begin(&transition, &prep), true);
            TEST_CHECK(declarative_space_transition_commit(&transition), true);
            if (clear) {
                struct declarative_runtime_state empty = { 0 };
                TEST_CHECK(memcmp(&runtime, &empty, sizeof(runtime)), 0);
            } else TEST_CHECK(memcmp(&runtime, &runtime_before, sizeof(runtime)), 0);
            for (size_t i = 0; i < target_count; ++i) {
                if (clear) {
                    struct declarative_runtime_cell empty[DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT] = { 0 };
                    TEST_CHECK(memcmp(live[i].runtime_ownership, empty, sizeof(empty)), 0);
                    TEST_CHECK((live[i].flags & (VIEW_LAYOUT | VIEW_TOP_PADDING)) == 0, true);
                } else {
                    TEST_CHECK(memcmp(live[i].runtime_ownership, before[i].runtime_ownership,
                                      sizeof(live[i].runtime_ownership)), 0);
                    TEST_CHECK((live[i].flags & (VIEW_LAYOUT | VIEW_TOP_PADDING)) ==
                               (before[i].flags & (VIEW_LAYOUT | VIEW_TOP_PADDING)), true);
                }
                TEST_CHECK((live[i].flags & (VIEW_ENABLE_PADDING | VIEW_ENABLE_GAP)) ==
                           (VIEW_ENABLE_PADDING | VIEW_ENABLE_GAP), true);
                TEST_CHECK(live[i].root, before[i].root); TEST_CHECK(live[i].layout, VIEW_FLOAT);
                TEST_CHECK(live[i].top_padding, 1); TEST_CHECK(live[i].bottom_padding, 2);
                TEST_CHECK(live[i].left_padding, 3); TEST_CHECK(live[i].right_padding, 4);
                TEST_CHECK(live[i].window_gap, clear ? 5 : 44);
            }
            TEST_CHECK(g_window_manager.managed_window.count, 0);
            declarative_space_transition_destroy(&transition);
            for (size_t i = 0; i < target_count; ++i) free(live[i].root);
            table_free(&g_window_manager.managed_window); memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window));
            table_free(&views); TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL); ++cases_run;
        }
    }
    TEST_CHECK(cases_run, 6UL);
})

TEST_FUNC(declarative_space_phase3c_lifecycle_rejects_every_repeated_transition,
{
    declarative_space_test_set_admission(9001,true);
    struct declarative_space_phase3c_fixture f; TEST_CHECK(declarative_space_phase3c_fixture_begin(&f,7001,901),true);
    TEST_CHECK(declarative_space_transition_postcommit(&f.transition,NULL),false);
    TEST_CHECK(declarative_space_dispose_retired_table(&f.transition),false);
    TEST_CHECK(declarative_space_retire_old_roots(&f.transition,NULL),false);
    TEST_CHECK(declarative_space_transition_commit(&f.transition),true);
    TEST_CHECK(declarative_space_transition_commit(&f.transition),false);
    TEST_CHECK(declarative_space_dispose_retired_table(&f.transition),false);
    TEST_CHECK(declarative_space_retire_old_roots(&f.transition,NULL),false);
    struct declarative_space_phase3c_outcome_context context={0};
    struct declarative_space_postcommit_ops ops={declarative_space_phase3c_outcome_normal,declarative_space_phase3c_outcome_below,
        declarative_space_phase3c_outcome_purify,declarative_space_phase3c_outcome_update,declarative_space_phase3c_outcome_flush,
        declarative_space_phase3c_outcome_feedback,&context};
    TEST_CHECK(declarative_space_transition_postcommit(&f.transition,&ops),true);
    TEST_CHECK(declarative_space_transition_postcommit(&f.transition,&ops),false);
    TEST_CHECK(declarative_space_transition_commit(&f.transition),false);
    TEST_CHECK(declarative_space_retire_old_roots(&f.transition,&ops),false);
    TEST_CHECK(declarative_space_dispose_retired_table(&f.transition),true);
    TEST_CHECK(declarative_space_dispose_retired_table(&f.transition),false);
    TEST_CHECK(declarative_space_retire_old_roots(&f.transition,&ops),true);
    TEST_CHECK(declarative_space_retire_old_roots(&f.transition,&ops),false);
    TEST_CHECK(declarative_space_transition_commit(&f.transition),false);
    TEST_CHECK(declarative_space_transition_postcommit(&f.transition,&ops),false);
    TEST_CHECK(declarative_space_dispose_retired_table(&f.transition),false);
    declarative_space_phase3c_fixture_end(&f);
    TEST_CHECK(declarative_space_test_outstanding_allocations(),0UL);
})

struct declarative_space_phase3c_view_platform_context
{
    bool visible;
    size_t visibility, update, flush;
};

static bool declarative_space_phase3c_test_visible(uint64_t sid, void *opaque)
{
    (void) sid;
    struct declarative_space_phase3c_view_platform_context *context = opaque;
    ++context->visibility;
    return context->visible;
}

static void declarative_space_phase3c_test_update(struct view *view, void *opaque)
{
    (void) view;
    ++((struct declarative_space_phase3c_view_platform_context *) opaque)->update;
}

static void declarative_space_phase3c_test_flush(struct window_node *root, void *opaque)
{
    (void) root;
    ++((struct declarative_space_phase3c_view_platform_context *) opaque)->flush;
}

TEST_FUNC(declarative_space_phase3c_default_view_flush_visible_and_hidden,
{
    size_t cases_run = 0;
    for (int visible = 0; visible <= 1; ++visible) {
        declarative_space_test_set_admission(9100 + visible, true);
        struct declarative_space_phase3c_fixture f;
        TEST_CHECK(declarative_space_phase3c_fixture_begin(&f, 7100 + visible, 920 + visible), true);
        f.transition.preparation.targets[0].effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].value.integer = 19;
        TEST_CHECK(declarative_space_transition_commit(&f.transition), true);
        struct area area_before = f.view.root->area;
        struct declarative_space_phase3c_view_platform_context platform = { .visible = visible };
        struct view_test_platform_ops platform_ops = {
            declarative_space_phase3c_test_visible,
            declarative_space_phase3c_test_update,
            declarative_space_phase3c_test_flush,
            &platform
        };
        view_test_set_platform_ops(&platform_ops);
        struct declarative_space_phase3c_outcome_context calls = { 0 };
        struct declarative_space_postcommit_ops ops = {
            declarative_space_phase3c_outcome_normal,
            declarative_space_phase3c_outcome_below,
            declarative_space_phase3c_outcome_purify,
            NULL, NULL,
            declarative_space_phase3c_outcome_feedback,
            &calls
        };
        TEST_CHECK(declarative_space_transition_postcommit(&f.transition, &ops), true);
        view_test_set_platform_ops(NULL);
        TEST_CHECK(platform.update, 1UL);
        TEST_CHECK(platform.visibility, 1UL);
        TEST_CHECK(platform.flush, visible ? 1UL : 0UL);
        TEST_CHECK(memcmp(&f.view.root->area, &area_before, sizeof(area_before)), 0);
        TEST_CHECK(f.transition.diagnostics.update_failed, 0UL);
        TEST_CHECK(f.transition.diagnostics.flush_failed, 0UL);
        TEST_CHECK(f.transition.diagnostics.flush_deferred, visible ? 0UL : 1UL);
        TEST_CHECK(view_is_invalid(&f.view), !visible);
        TEST_CHECK(view_is_dirty(&f.view), !visible);
        TEST_CHECK(declarative_space_dispose_retired_table(&f.transition), true);
        TEST_CHECK(declarative_space_retire_old_roots(&f.transition, &ops), true);
        declarative_space_phase3c_fixture_end(&f);
        TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
        ++cases_run;
    }
    view_test_set_platform_ops(NULL);
    TEST_CHECK(cases_run, 2UL);
})

TEST_FUNC(declarative_space_phase3c_aliasing_and_retirement_guards,
{
    size_t cases_run = 0;
    struct window_node *shared = calloc(1, sizeof(*shared));
    shared->window_count = 1; shared->window_list[0] = shared->window_order[0] = 1001;
    struct view shared_views[2] = {
        { .sid = 7201, .layout = VIEW_STACK, .root = shared },
        { .sid = 7202, .layout = VIEW_STACK, .root = shared }
    };
    struct declarative_space_target shared_targets[2] = {
        { &shared_views[0], 7201, 1, "shared-a", true },
        { &shared_views[1], 7202, 2, "shared-b", true }
    };
    struct table managed = { 0 }; table_init(&managed, 8, hash_wm, compare_wm);
    table_add(&managed, &shared->window_list[0], &shared_views[0]);
    struct declarative_space_table_contract contract = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_root_before_image images[2]; struct declarative_space_table_before_image table_image;
    TEST_CHECK(declarative_space_validate_forest_and_table(shared_targets, 2, &managed, &contract, images, &table_image), false);
    table_free(&managed); free(shared); ++cases_run;

    struct window_node roots[4] = { 0 };
    struct declarative_space_prepared_target prepared_targets[2] = { 0 };
    struct window_node *old_index[] = { &roots[0] };
    struct window_node *final_index[] = { &roots[0], &roots[1] };
    struct declarative_space_preparation preparation = {
        .targets = prepared_targets, .target_count = 2,
        .old_node_index = old_index, .old_node_index_count = 1,
        .final_node_index = final_index, .final_node_index_count = 2
    };
    prepared_targets[0].changed = true; prepared_targets[0].root_before.root = &roots[0]; prepared_targets[0].final_root = &roots[1];
    prepared_targets[1].changed = false; prepared_targets[1].root_before.root = &roots[2]; prepared_targets[1].final_root = &roots[0];
    TEST_CHECK(declarative_space_prepared_roots_disjoint(&preparation), false); ++cases_run;
    final_index[0] = final_index[1] = &roots[2];
    TEST_CHECK(declarative_space_prepared_roots_disjoint(&preparation), false); ++cases_run;
    final_index[0] = final_index[1] = &roots[1];
    TEST_CHECK(declarative_space_prepared_roots_disjoint(&preparation), false); ++cases_run;

    struct declarative_space_phase3c_outcome_context calls = { 0 };
    struct declarative_space_postcommit_ops retire_ops = { .dispose_feedback = declarative_space_phase3c_outcome_feedback, .context = &calls };
    struct declarative_space_prepared_target unchanged_target = { 0 };
    struct window_node *unchanged = calloc(1, sizeof(*unchanged));
    unchanged_target.root_before.root = unchanged; unchanged_target.final_root = unchanged;
    struct declarative_space_transition unchanged_transition = { .preparation = { .targets = &unchanged_target, .target_count = 1 },
        .retired_table_disposed = true, .state = DECLARATIVE_SPACE_TRANSITION_POSTCOMMIT_ATTEMPTED };
    TEST_CHECK(declarative_space_retire_old_roots(&unchanged_transition, &retire_ops), true);
    TEST_CHECK(unchanged_target.root_before.root, unchanged); TEST_CHECK(calls.feedback, 0UL);
    free(unchanged); ++cases_run;

    memset(&calls, 0, sizeof(calls));
    struct declarative_space_prepared_target changed_target = { .changed = true };
    struct window_node *old = calloc(1, sizeof(*old)), *final = calloc(1, sizeof(*final));
    old->feedback_window.id = 88; old->feedback_window.context = (CGContextRef)(uintptr_t)1; old->insert_dir = STACK;
    old->window_count = 1; old->window_list[0] = old->window_order[0] = 88;
    struct view old_view = { .layout = VIEW_STACK, .root = old };
    TEST_CHECK(declarative_space_validate_root(&old_view, &changed_target.root_before), true);
    changed_target.final_root = final;
    memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback)); table_init(&g_window_manager.insert_feedback, 8, hash_wm, compare_wm);
    table_add(&g_window_manager.insert_feedback, &old->window_order[0], old); g_mouse_state.feedback_node = old;
    struct window_node *changed_final_index[] = { final };
    struct declarative_space_transition changed_transition = { .preparation = {
        .targets = &changed_target, .target_count = 1,
        .final_node_index = changed_final_index, .final_node_index_count = 1 },
        .retired_table_disposed = true, .state = DECLARATIVE_SPACE_TRANSITION_POSTCOMMIT_ATTEMPTED };
    TEST_CHECK(declarative_space_retire_old_roots(&changed_transition, &retire_ops), true);
    TEST_CHECK(calls.feedback, 1UL); TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    uint32_t retired_wid = 88;
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &retired_wid) == NULL, true);
    TEST_CHECK(declarative_space_retire_old_roots(&changed_transition, &retire_ops), false);
    TEST_CHECK(calls.feedback, 1UL); table_free(&g_window_manager.insert_feedback); memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback)); free(final); ++cases_run;

    memset(&calls, 0, sizeof(calls));
    struct window_node *alias_old = calloc(1, sizeof(*alias_old));
    struct window_node *alias_final = calloc(1, sizeof(*alias_final));
    alias_old->left = alias_final; alias_final->parent = alias_old;
    alias_old->feedback_window.id = 99; alias_old->feedback_window.context = (CGContextRef)(uintptr_t)1; alias_old->window_order[0] = 99;
    struct declarative_space_prepared_target alias_target = { .changed = true, .root_before.root = alias_old, .final_root = alias_final };
    struct window_node *alias_final_index[] = { alias_final };
    struct declarative_space_transition alias_transition = { .preparation = {
        .targets = &alias_target, .target_count = 1,
        .final_node_index = alias_final_index, .final_node_index_count = 1 },
        .retired_table_disposed = true, .state = DECLARATIVE_SPACE_TRANSITION_POSTCOMMIT_ATTEMPTED };
    table_init(&g_window_manager.insert_feedback, 8, hash_wm, compare_wm); table_add(&g_window_manager.insert_feedback, &alias_old->window_order[0], alias_old);
    g_mouse_state.feedback_node = alias_old; size_t observed = 0;
    declarative_space_test_set_assertion_observer(declarative_space_phase3c_observe, &observed);
    TEST_CHECK(declarative_space_retire_old_roots(&alias_transition, &retire_ops), false);
    TEST_CHECK(observed, 1UL); TEST_CHECK(calls.feedback, 0UL);
    TEST_CHECK(g_mouse_state.feedback_node, alias_old);
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &alias_old->window_order[0]), alias_old);
    TEST_CHECK(alias_old->feedback_window.id, 99U); TEST_CHECK(alias_final->parent, alias_old);
    declarative_space_test_set_assertion_observer(NULL, NULL);
    table_free(&g_window_manager.insert_feedback); memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback));
    g_mouse_state.feedback_node = NULL; free(alias_final); free(alias_old); ++cases_run;
    TEST_CHECK(cases_run, 7UL);
})

struct declarative_space_phase3c_thread_context
{
    struct declarative_space_transition *transition;
    bool committed;
};

static void *declarative_space_phase3c_foreign_commit(void *opaque)
{
    struct declarative_space_phase3c_thread_context *context = opaque;
    context->committed = declarative_space_transition_commit(context->transition);
    return NULL;
}

TEST_FUNC(declarative_space_phase3c_foreign_thread_precommit_is_rejected,
{
    declarative_space_test_set_admission(9301, true);
    struct declarative_space_phase3c_fixture f;
    TEST_CHECK(declarative_space_phase3c_fixture_begin(&f, 7301, 1030), true);
    struct window_node *root_before = f.view.root; struct bucket **table_before = g_window_manager.managed_window.buckets;
    size_t observed = 0; declarative_space_test_set_assertion_observer(declarative_space_phase3c_observe, &observed);
    struct declarative_space_phase3c_thread_context context = { .transition = &f.transition };
    pthread_t thread; int created = pthread_create(&thread, NULL, declarative_space_phase3c_foreign_commit, &context);
    TEST_CHECK(created, 0);
    if (!created) TEST_CHECK(pthread_join(thread, NULL), 0);
    TEST_CHECK(context.committed, false); TEST_CHECK(observed, 1UL);
    TEST_CHECK(declarative_space_test_assertion_count(), 1UL);
    TEST_CHECK(f.transition.state, DECLARATIVE_SPACE_TRANSITION_PREPARED);
    TEST_CHECK(f.view.root, root_before); TEST_CHECK(g_window_manager.managed_window.buckets, table_before);
    TEST_CHECK(f.transition.retired_managed_window.buckets == NULL, true);
    TEST_CHECK(f.transition.preparation.replacement_managed_window.buckets != NULL, true);
    declarative_space_test_set_assertion_observer(NULL, NULL);
    declarative_space_phase3c_fixture_end(&f);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
})
