#define DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, member, value) do { \
    __typeof__((view).member) saved = (view).member; (view).member = (value); \
    TEST_CHECK(declarative_space_view_before_matches(&(view), &(image)), false); (view).member = saved; \
} while (0)

TEST_FUNC(declarative_space_before_image_zero_topology_and_window_identity_guards,
{
    struct view view = { .layout = VIEW_BSP, .top_padding = 1, .bottom_padding = 2, .left_padding = 3,
                         .right_padding = 4, .window_gap = 5, .accordion_padding = 30,
                         .single_window_max_width = 1600, .single_window_min_width = 1200,
                         .single_window_width_ratio = 0.85f, .split_type = SPLIT_AUTO,
                         .auto_balance = SPLIT_X, .insertion_point = 17, .flags = VIEW_LAYOUT | VIEW_ENABLE_GAP };
    struct declarative_space_view_before_image image;
    memset(&image, 0xA5, sizeof(image)); /* Padding is deliberately poisoned and semantically irrelevant. */
    declarative_space_capture_view_before(&view, &image);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, layout, VIEW_STACK);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, top_padding, 11);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, bottom_padding, 12);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, left_padding, 13);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, right_padding, 14);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, window_gap, 15);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, accordion_padding, 31);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, single_window_max_width, 1700);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, single_window_min_width, 1100);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, single_window_width_ratio, 0.75f);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, split_type, SPLIT_X);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, auto_balance, SPLIT_Y);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, insertion_point, 18);
    DECLARATIVE_SPACE_MUTATE_BEFORE(view, image, flags, view.flags ^ VIEW_IS_DIRTY);
    TEST_CHECK(declarative_space_view_before_matches(&view, &image), true);

    struct table views = { 0 }, managed = { 0 };
    table_init(&views, 8, hash_wm, compare_wm); table_init(&managed, 8, hash_wm, compare_wm);
    struct declarative_space_table_contract vc = { hash_wm, compare_wm, sizeof(uint64_t) };
    struct declarative_space_table_contract mc = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_test_capture capture = { 0 };
    struct declarative_space_capture_ops ops = { declarative_space_test_query, declarative_space_test_find, declarative_space_test_eligible, &capture };
    struct declarative_space_defaults defaults = declarative_space_test_defaults();
    struct declarative_runtime_state runtime = { 0 };
    runtime.global[DECLARATIVE_CONFIG_LAYOUT].present = true;
    struct declarative_space_preparation preparation;
    TEST_CHECK(declarative_space_prepare(NULL, 0, NULL, 0, &views, &vc, &managed, &mc, NULL, &runtime,
                                         &defaults, DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD, &ops, &preparation), true);
    TEST_CHECK(preparation.global_ownership_before[DECLARATIVE_CONFIG_LAYOUT].present, true);
    TEST_CHECK(preparation.global_ownership_after[DECLARATIVE_CONFIG_LAYOUT].present, false);
    declarative_space_preparation_destroy(&preparation);
    TEST_CHECK(declarative_space_prepare(NULL, 0, NULL, 0, &views, &vc, &managed, &mc, NULL, &runtime,
                                         &defaults, (enum declarative_space_ownership_mode) 99, &ops, &preparation), false);
    TEST_CHECK(declarative_space_prepare(NULL, 0, NULL, 0, &views, &vc, &managed, &mc, NULL, &runtime,
                                         NULL, DECLARATIVE_SPACE_PRESERVE_RUNTIME, &ops, &preparation), false);
    struct declarative_space_target target = { .sid = 1 };
    capture.sids[0] = 1; capture.wids[0][0] = 5; capture.counts[0] = 1; capture.mismatch_window_id = true;
    struct declarative_space_inventory inventory;
    TEST_CHECK(declarative_space_capture_inventory(&target, 1, &ops, &inventory), false);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), 0UL);
    table_free(&managed); table_free(&views);
})

static TABLE_HASH_FUNC(declarative_space_other_hash) { return *(uint32_t *) key + 1; }

TEST_FUNC(declarative_space_heap_root_and_table_topology_negatives,
{
    struct window_node *n = calloc(3, sizeof(*n));
    declarative_space_test_init_leaf(&n[1], 1, 0); declarative_space_test_init_leaf(&n[2], 9, 0);
    n[0].left = &n[1]; n[0].right = &n[2]; n[0].split = SPLIT_X; n[1].parent = &n[0]; n[2].parent = &n[0];
    struct view view = { .sid = 71, .uuid = CFSTR("heap-71"), .layout = VIEW_BSP, .root = &n[0] };
    struct declarative_space_root_before_image root_image;
    TEST_CHECK(declarative_space_validate_root(&view, &root_image), true);
    n[1].zoom = &n[2]; TEST_CHECK(declarative_space_validate_root(&view, &root_image), false);
    n[1].zoom = &n[1]; TEST_CHECK(declarative_space_validate_root(&view, &root_image), false);
    n[1].zoom = &n[0]; TEST_CHECK(declarative_space_validate_root(&view, &root_image), true);
    n[1].zoom = NULL; n[0].zoom = &n[1]; TEST_CHECK(declarative_space_validate_root(&view, &root_image), false);
    n[0].zoom = NULL; n[0].split = SPLIT_AUTO; TEST_CHECK(declarative_space_validate_root(&view, &root_image), false);
    n[0].split = SPLIT_X; n[1].child = (enum window_node_child) 99; TEST_CHECK(declarative_space_validate_root(&view, &root_image), false);
    n[1].child = CHILD_NONE; n[1].insert_dir = 999; TEST_CHECK(declarative_space_validate_root(&view, &root_image), false);
    n[1].insert_dir = 0;

    struct table managed = { 0 }; table_init(&managed, 8, hash_wm, compare_wm);
    uint32_t one = 1, nine = 9; table_add(&managed, &one, &view); table_add(&managed, &nine, &view);
    struct declarative_space_target target = { &view, 71, 1, "heap-71", true };
    struct declarative_space_table_contract contract = { hash_wm, compare_wm, sizeof(uint32_t) };
    struct declarative_space_table_before_image before, changed;
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &contract, &root_image, &before), true);
    int slot = (int) (hash_wm(&one) % managed.capacity);
    struct bucket *first = managed.buckets[slot], *second = first->next;
    managed.buckets[slot] = second; first->next = NULL; second->next = first;
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &contract, &root_image, &changed), true);
    TEST_CHECK(changed.fingerprint == before.fingerprint, false);
    managed.buckets[slot] = first; first->next = second; second->next = NULL;
    void *old_key = first->key; first->key = malloc(sizeof(one)); memcpy(first->key, &one, sizeof(one));
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &contract, &root_image, &changed), true);
    TEST_CHECK(changed.fingerprint == before.fingerprint, false);
    free(first->key); first->key = old_key;

    uint32_t saved_second_key = *(uint32_t *) second->key;
    struct view other_owner = view;
    *(uint32_t *) second->key = *(uint32_t *) first->key; /* Duplicate physical key also omits one forest WID. */
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &contract, &root_image, &changed), false);
    second->value = &other_owner;
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &contract, &root_image, &changed), false);
    second->value = &view;
    *(uint32_t *) second->key = 7; /* Equal cardinality, but one forest WID is missing and one is extra. */
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &contract, &root_image, &changed), false);
    *(uint32_t *) second->key = saved_second_key;

    managed.buckets[slot] = first->next; first->next = NULL; managed.buckets[(slot + 1) % managed.capacity] = first;
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &contract, &root_image, &changed), false);
    managed.buckets[(slot + 1) % managed.capacity] = NULL; first->next = managed.buckets[slot]; managed.buckets[slot] = first;
    managed.hash = declarative_space_other_hash;
    TEST_CHECK(declarative_space_validate_forest_and_table(&target, 1, &managed, &contract, &root_image, &changed), false);
    managed.hash = hash_wm; table_free(&managed); free(n);
})
