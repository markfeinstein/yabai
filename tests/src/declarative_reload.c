struct declarative_reload_fixture
{
    struct declarative_topology_fixture topology;
    struct declarative_topology_capture_ops topology_ops;
    const char *json;
    const char *path;
    char observed_installed_path[PATH_MAX];
    bool observed_installed_path_present;
    char observed_xdg[PATH_MAX];
    char observed_home[PATH_MAX];
    size_t select_calls;
    size_t topology_calls;
    size_t query_calls;
    size_t postcommit_calls;
    size_t postcommit_failures;
    size_t probe_calls;
    struct declarative_space_postcommit_ops postcommit_ops;
    uint32_t wids[4];
    struct window windows[4];
    size_t window_count;
    bool query_fail;
    bool postcommit_fail;
    bool probe_committed;
};

struct declarative_reload_saved_state
{
    struct event_loop event_loop;
    struct declarative_install_test_window_manager_state window_manager;
    struct space_manager space_manager;
    struct display_manager display_manager;
    struct mouse_state mouse_state;
    struct menu_bar_manager_state menu_manager;
    struct declarative_runtime_state runtime;
    bool verbose;
};

static bool declarative_reload_test_select(struct declarative_startup_state *state,
                                           const char *installed_path,
                                           const char *xdg_config_home,
                                           const char *home,
                                           struct declarative_error *error,
                                           void *context)
{
    struct declarative_reload_fixture *fixture = context;
    ++fixture->select_calls;
    fixture->observed_installed_path_present = installed_path != NULL;
    snprintf(fixture->observed_installed_path, sizeof(fixture->observed_installed_path), "%s", installed_path ? installed_path : "");
    snprintf(fixture->observed_xdg, sizeof(fixture->observed_xdg), "%s", xdg_config_home ? xdg_config_home : "");
    snprintf(fixture->observed_home, sizeof(fixture->observed_home), "%s", home ? home : "");
    memset(state, 0, sizeof(*state));
    if (!declarative_config_parse_bytes((const uint8_t *) fixture->json, strlen(fixture->json),
                                        fixture->path, &state->model, error)) return false;
    snprintf(state->path, sizeof(state->path), "%s", fixture->path);
    state->present = true;
    return true;
}

static bool declarative_reload_test_capture_topology(const struct declarative_topology_bounds_config *bounds,
                                                     struct declarative_topology *topology,
                                                     void *context)
{
    (void) bounds;
    struct declarative_reload_fixture *fixture = context;
    ++fixture->topology_calls;
    return declarative_topology_capture(&fixture->topology_ops, topology);
}

static bool declarative_reload_test_query_sid(uint64_t sid, const uint32_t **wids,
                                              size_t *count, void *context)
{
    (void) sid;
    struct declarative_reload_fixture *fixture = context;
    ++fixture->query_calls;
    if (fixture->query_fail) return false;
    if (sid == 11 && fixture->window_count) {
        *wids = fixture->wids;
        *count = fixture->window_count;
    } else {
        *wids = NULL;
        *count = 0;
    }
    return true;
}

static struct window *declarative_reload_test_find_window(uint32_t wid, void *context)
{
    struct declarative_reload_fixture *fixture = context;
    for (size_t i = 0; i < fixture->window_count; ++i) {
        if (fixture->wids[i] == wid) return &fixture->windows[i];
    }
    return NULL;
}

static bool declarative_reload_test_is_eligible(struct window *window, void *context)
{
    (void) context;
    return window != NULL;
}

static bool declarative_reload_test_postcommit_window(struct window *window, void *context)
{
    (void) window;
    struct declarative_reload_fixture *fixture = context;
    ++fixture->postcommit_calls;
    if (fixture->postcommit_fail) ++fixture->postcommit_failures;
    return !fixture->postcommit_fail;
}

static bool declarative_reload_test_postcommit_view(struct view *view, void *context)
{
    (void) view;
    struct declarative_reload_fixture *fixture = context;
    ++fixture->postcommit_calls;
    if (fixture->postcommit_fail) ++fixture->postcommit_failures;
    return !fixture->postcommit_fail;
}

static bool declarative_reload_test_postcommit_feedback(struct window_node *node, void *context)
{
    (void) node;
    struct declarative_reload_fixture *fixture = context;
    ++fixture->postcommit_calls;
    if (fixture->postcommit_fail) ++fixture->postcommit_failures;
    return !fixture->postcommit_fail;
}

static void declarative_reload_test_postcommit_probe(bool committed, void *context)
{
    struct declarative_reload_fixture *fixture = context;
    ++fixture->probe_calls;
    fixture->probe_committed = committed;
}

static struct declarative_reload_test_ops declarative_reload_test_ops(struct declarative_reload_fixture *fixture)
{
    fixture->postcommit_ops = (struct declarative_space_postcommit_ops) {
        .layer_normal = declarative_reload_test_postcommit_window,
        .layer_below = declarative_reload_test_postcommit_window,
        .purify = declarative_reload_test_postcommit_window,
        .update = declarative_reload_test_postcommit_view,
        .flush = declarative_reload_test_postcommit_view,
        .dispose_feedback = declarative_reload_test_postcommit_feedback,
        .context = fixture
    };
    return (struct declarative_reload_test_ops) {
        .select = declarative_reload_test_select,
        .capture_topology = declarative_reload_test_capture_topology,
        .query_sid = declarative_reload_test_query_sid,
        .find_window = declarative_reload_test_find_window,
        .is_eligible = declarative_reload_test_is_eligible,
        .postcommit_ops = &fixture->postcommit_ops,
        .postcommit_probe = declarative_reload_test_postcommit_probe,
        .context = fixture
    };
}

static void declarative_reload_test_save(struct declarative_reload_saved_state *saved)
{
    *saved = (struct declarative_reload_saved_state) {
        .event_loop = g_event_loop,
        .window_manager = declarative_install_test_window_manager_capture(&g_window_manager),
        .space_manager = g_space_manager,
        .display_manager = g_display_manager,
        .mouse_state = g_mouse_state,
        .menu_manager = g_menu_bar_manager,
        .runtime = g_declarative_runtime_state,
        .verbose = g_verbose
    };
}

static void declarative_reload_test_restore(const struct declarative_reload_saved_state *saved)
{
    g_verbose = saved->verbose;
    g_declarative_runtime_state = saved->runtime;
    g_menu_bar_manager = saved->menu_manager;
    g_mouse_state = saved->mouse_state;
    g_display_manager = saved->display_manager;
    g_space_manager = saved->space_manager;
    declarative_install_test_window_manager_restore(&g_window_manager, &saved->window_manager);
    g_event_loop = saved->event_loop;
}

static bool declarative_reload_test_setup(struct declarative_reload_fixture *fixture,
                                          struct declarative_reload_saved_state *saved)
{
    declarative_reload_test_save(saved);
    g_event_loop = (struct event_loop) {0};
    declarative_install_test_window_manager_clear_nonmutex(&g_window_manager);
    g_space_manager = (struct space_manager) {0};
    g_display_manager = (struct display_manager) {0};
    g_mouse_state = (struct mouse_state) {
        .modifier = MOUSE_MOD_FN, .action1 = MOUSE_MODE_MOVE,
        .action2 = MOUSE_MODE_RESIZE, .drop_action = MOUSE_MODE_SWAP
    };
    g_declarative_runtime_state = (struct declarative_runtime_state) {0};
    g_menu_bar_manager = (struct menu_bar_manager_state) { .initialized = true };
    g_menu_bar_manager.settings = (struct menu_bar_settings) {
        .enabled = MENU_BAR_DISABLED, .style = MENU_BAR_STYLE_COMPACT,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };
    menu_bar_manager_startup_staging_open();
    declarative_install_test_reset();

    uint64_t empty_uuid_sid = fixture->topology.empty_uuid_sid;
    declarative_topology_test_fixture_init(&fixture->topology, false);
    fixture->topology.empty_uuid_sid = empty_uuid_sid;
    fixture->topology_ops = declarative_topology_test_ops(&fixture->topology);
    struct declarative_startup_state startup = {
        .model = declarative_install_test_model(), .present = true
    };
    snprintf(startup.path, sizeof(startup.path), "%s", "/tmp/reload-initial.jsonc");
    struct declarative_install_test_context install = { .startup = &startup, .ops = &fixture->topology_ops };
    bool result = startup.model && event_loop_begin_bootstrap(&g_event_loop, declarative_install_test_bootstrap, &install);
    declarative_startup_destroy(&startup);
    return result;
}

static void declarative_reload_test_teardown(struct declarative_reload_fixture *fixture,
                                             const struct declarative_reload_saved_state *saved)
{
    declarative_reload_test_set_message_ops(NULL);
    declarative_reload_test_fail_stage(DECLARATIVE_RELOAD_TEST_STAGE_NONE);
    if (g_event_loop.semaphore) event_loop_end(&g_event_loop);
    declarative_install_test_reset();
    declarative_topology_test_fixture_destroy(&fixture->topology);
    menu_bar_manager_startup_staging_abort();
    declarative_reload_test_restore(saved);
}

struct declarative_reload_worker_context
{
    FILE *response;
    const struct declarative_reload_test_ops *ops;
    bool result;
};

static bool declarative_reload_test_worker(void *context)
{
    struct declarative_reload_worker_context *reload = context;
    reload->result = declarative_config_reload_with_test_ops(reload->response, reload->ops);
    return true;
}

static bool declarative_reload_test_message_worker(void *context)
{
    struct declarative_reload_worker_context *reload = context;
    char message[] = "reload\0";
    handle_domain_config(reload->response, message_test_token("config"), message);
    reload->result = true;
    return true;
}

static bool declarative_reconcile_test_worker(void *context)
{
    struct declarative_reload_worker_context *reconcile = context;
    reconcile->result = declarative_configuration_reconcile_with_test_ops(reconcile->ops);
    return true;
}

static bool declarative_reload_run(struct declarative_reload_test_ops *ops, FILE *response)
{
    struct declarative_reload_worker_context reload = { .response = response, .ops = ops };
    if (!event_loop_perform_sync(&g_event_loop, declarative_reload_test_worker, &reload)) return false;
    return reload.result;
}

static bool declarative_reconcile_run(struct declarative_reload_test_ops *ops)
{
    struct declarative_reload_worker_context reconcile = { .ops = ops };
    if (!event_loop_perform_sync(&g_event_loop, declarative_reconcile_test_worker, &reconcile)) return false;
    return reconcile.result;
}

static void declarative_reload_add_runtime_rule(const char *label)
{
    struct rule rule = { .label = strdup(label) };
    if (rule_add(&rule) != RULE_ADD_OK) rule_destroy(&rule);
}

static struct window_node *declarative_reload_find_wid(struct window_node *node, uint32_t wid)
{
    if (!node || (!node->left && !node->right)) {
        for (int i = 0; node && i < node->window_count; ++i) if (node->window_list[i] == wid) return node;
        return NULL;
    }
    struct window_node *found = declarative_reload_find_wid(node->left, wid);
    return found ? found : declarative_reload_find_wid(node->right, wid);
}

static void declarative_reload_set_inventory(struct declarative_reload_fixture *fixture, size_t count)
{
    fixture->window_count = count;
    for (size_t i = 0; i < count; ++i) {
        fixture->wids[i] = (uint32_t) i + 1;
        fixture->windows[i].id = fixture->wids[i];
    }
}

static bool declarative_reload_ownership_present(void)
{
    if (!g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_GAP].present ||
        !g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK].present ||
        g_window_manager.window_focus_fallback != WINDOW_FOCUS_FALLBACK_RECENT) return false;
    uint64_t sid = 11;
    struct view *view = table_find(&g_space_manager.view, &sid);
    return view && view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].present;
}

static void declarative_reload_set_ownership(void)
{
    g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_GAP] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_GLOBAL, .value.integer = 73
    };
    g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_GLOBAL, .value.integer = WINDOW_FOCUS_FALLBACK_RECENT
    };
    g_window_manager.window_focus_fallback = WINDOW_FOCUS_FALLBACK_RECENT;
    uint64_t sid = 11;
    struct view *view = table_find(&g_space_manager.view, &sid);
    if (view) view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP] = (struct declarative_runtime_cell) {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_SPACE, .value.integer = 74
    };
}

TEST_FUNC(declarative_reload_stage_failure_matrix_preserves_installed_state,
{
    static const char candidate_json[] =
        "{\"version\":1,\"config\":{\"layout\":\"accordion_horizontal\",\"accordion_padding\":31,\"window_gap\":17},"
        "\"rules\":[{\"label\":\"native-once\",\"one_shot\":true,"
        "\"match\":{\"app\":{\"pattern\":\"^App$\"}},\"effects\":{\"manage\":true}}]}";
    struct declarative_reload_fixture fixture = {
        .json = candidate_json, .path = "/tmp/reload-next.jsonc"
    };
    struct declarative_reload_saved_state saved;
    TEST_CHECK(declarative_reload_test_setup(&fixture, &saved), true);
    if (!g_event_loop.semaphore) return result;

    declarative_reload_add_runtime_rule("runtime-a");
    declarative_reload_add_runtime_rule("runtime-b");
    declarative_reload_set_ownership();
    uint64_t generation = declarative_installed.generation;
    struct declarative_config_model *model = declarative_installed.model;
    char *path = declarative_installed.path;
    struct rule *rules = g_window_manager.rules;
    struct bucket **managed_buckets = g_window_manager.managed_window.buckets;
    size_t ledger = declarative_install_test_outstanding_allocations();
    size_t topology_ledger = declarative_topology_test_outstanding_allocations();
    size_t rules_ledger = declarative_rules_test_outstanding_allocations();
    size_t space_ledger = declarative_space_test_outstanding_allocations();
    uint64_t sid = 11;
    struct view *view = table_find(&g_space_manager.view, &sid);
    struct window_node *root = view ? view->root : NULL;
    size_t stages = 0;

    struct declarative_reload_test_ops ops = declarative_reload_test_ops(&fixture);
    for (enum declarative_reload_test_stage stage = DECLARATIVE_RELOAD_TEST_STAGE_SELECT;
         stage < DECLARATIVE_RELOAD_TEST_STAGE_COUNT; ++stage) {
        declarative_reload_test_fail_stage(stage);
        char *diagnostic = NULL;
        size_t diagnostic_length = 0;
        FILE *response = stage == DECLARATIVE_RELOAD_TEST_STAGE_SELECT ? open_memstream(&diagnostic, &diagnostic_length) : NULL;
        TEST_CHECK(declarative_reload_run(&ops, response), false);
        if (response) {
            fclose(response);
            TEST_CHECK(diagnostic_length > 1, true);
            TEST_CHECK(diagnostic[0], FAILURE_MESSAGE[0]);
            TEST_CHECK(strstr(diagnostic + 1, "stage=runtime") != NULL, true);
            TEST_CHECK(strstr(diagnostic + 1, "configuration reload failed") == NULL, true);
            free(diagnostic);
        }
        TEST_CHECK(declarative_installed.generation, generation);
        TEST_CHECK(declarative_installed.model, model);
        TEST_CHECK(declarative_installed.path, path);
        TEST_CHECK(g_window_manager.rules, rules);
        TEST_CHECK(g_window_manager.managed_window.buckets, managed_buckets);
        TEST_CHECK(view ? view->root : NULL, root);
        TEST_CHECK(declarative_reload_ownership_present(), true);
        TEST_CHECK(declarative_install_test_outstanding_allocations(), ledger);
        ++stages;
    }

    declarative_reload_test_fail_stage(DECLARATIVE_RELOAD_TEST_STAGE_NONE);
    fixture.query_fail = true;
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.model, model);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(view ? view->root : NULL, root);
    TEST_CHECK(declarative_reload_ownership_present(), true);
    TEST_CHECK(declarative_install_test_outstanding_allocations(), ledger);
    fixture.query_fail = false;

    static const char collision_json[] =
        "{\"version\":1,\"rules\":[{\"label\":\"runtime-a\","
        "\"match\":{\"app\":{\"pattern\":\"^Collision$\"}}}]}";
    fixture.json = collision_json;
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    fixture.json = candidate_json;
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.model, model);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(declarative_reload_ownership_present(), true);

    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    fixture.topology.fail_copy_query = 1;
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    fixture.topology.fail_copy_query = 0;
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.model, model);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(declarative_reload_ownership_present(), true);

    size_t allocator_failures = 0;
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    declarative_topology_test_fail_allocation_at(1);
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    declarative_topology_test_reset_allocator();
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.model, model);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(declarative_reload_ownership_present(), true);
    ++allocator_failures;

    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    declarative_rules_test_fail_allocation_at(1);
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    declarative_rules_test_reset_allocator();
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(declarative_reload_ownership_present(), true);
    ++allocator_failures;

    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    declarative_install_test_fail_allocation_at(1);
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    declarative_install_test_fail_allocation_at(0);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(declarative_reload_ownership_present(), true);
    ++allocator_failures;

    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    declarative_space_test_fail_allocation_at(1);
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    declarative_space_test_fail_allocation_at(0);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(declarative_reload_ownership_present(), true);
    ++allocator_failures;

    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    declarative_space_test_set_admission(9001, false);
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    declarative_space_test_set_admission(9002, true);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.model, model);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(declarative_reload_ownership_present(), true);

    TEST_CHECK(declarative_install_test_outstanding_allocations(), ledger);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), topology_ledger);
    TEST_CHECK(declarative_rules_test_outstanding_allocations(), rules_ledger);
    TEST_CHECK(declarative_space_test_outstanding_allocations(), space_ledger);
    printf("                   phase5 reload failure stages=%zu topology_query_failures=1 inventory_failures=1 label_collisions=1 allocator_failures=%zu commit_guard_failures=1\n",
           stages, allocator_failures);
    TEST_CHECK(stages, (size_t) DECLARATIVE_RELOAD_TEST_STAGE_COUNT - 1);
    TEST_CHECK(allocator_failures, 4U);
    declarative_reload_test_teardown(&fixture, &saved);
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_reload_success_is_synchronous_atomic_and_rearms_rules,
{
    static const char candidate_json[] =
        "{\"version\":1,\"config\":{\"layout\":\"accordion_horizontal\",\"accordion_padding\":31,\"window_gap\":17},"
        "\"rules\":[{\"label\":\"native-once\",\"one_shot\":true,"
        "\"match\":{\"app\":{\"pattern\":\"^App$\"}},\"effects\":{\"manage\":true}}]}";
    struct declarative_reload_fixture fixture = {
        .json = candidate_json, .path = "/tmp/reload-next.jsonc", .postcommit_fail = true
    };
    struct declarative_reload_saved_state saved;
    TEST_CHECK(declarative_reload_test_setup(&fixture, &saved), true);
    if (!g_event_loop.semaphore) return result;

    declarative_reload_add_runtime_rule("runtime-a");
    declarative_reload_add_runtime_rule("runtime-b");
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_WINDOW_PLACEMENT,
                                                 &(struct declarative_value) { .present = true, .enumeration = CHILD_FIRST }, 0),
               DECLARATIVE_APPLY_OK);
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_WINDOW_INSERT_POINT,
                                                 &(struct declarative_value) { .present = true, .enumeration = INSERT_LAST }, 0),
               DECLARATIVE_APPLY_OK);
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_SPLIT_RATIO,
                                                 &(struct declarative_value) { .present = true, .real = 0.73f }, 0),
               DECLARATIVE_APPLY_OK);
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_WINDOW_ZOOM_PERSIST,
                                                 &(struct declarative_value) { .present = true, .boolean = false }, 0),
               DECLARATIVE_APPLY_OK);
    rule_set_flag(&g_window_manager.rules[0], RULE_ONE_SHOT_REMOVE);
    rule_cleanup_one_shots();
    TEST_CHECK(rule_native_prefix_length(), 0U);
    TEST_CHECK(buf_len(g_window_manager.rules), 2);
    declarative_reload_set_ownership();

    uint64_t old_generation = declarative_installed.generation;
    struct declarative_config_model *old_model = declarative_installed.model;
    char *old_path = declarative_installed.path;
    struct rule_fold_context *old_context = declarative_installed.fold_context;
    struct rule *old_rules = g_window_manager.rules;
    uint64_t sid = 11;
    struct view *old_view = table_find(&g_space_manager.view, &sid);
    struct window_node *old_root = old_view ? old_view->root : NULL;

    struct declarative_reload_test_ops ops = declarative_reload_test_ops(&fixture);
    declarative_reload_test_set_message_ops(&ops);
    FILE *response = tmpfile();
    TEST_CHECK(response != NULL, true);
    struct declarative_reload_worker_context reload = { .response = response, .ops = &ops };
    TEST_CHECK(event_loop_perform_sync(&g_event_loop, declarative_reload_test_message_worker, &reload), true);
    declarative_reload_test_set_message_ops(NULL);

    char output[128] = {0};
    if (response) {
        rewind(response);
        fread(output, 1, sizeof(output) - 1, response);
        fclose(response);
    }
    TEST_CHECK(strcmp(output, "configuration reloaded\n"), 0);
    TEST_CHECK(fixture.probe_calls, 1U);
    TEST_CHECK(fixture.probe_committed, true);
    TEST_CHECK(declarative_installed.generation, old_generation + 1);
    TEST_CHECK(declarative_installed.model != old_model, true);
    TEST_CHECK(declarative_installed.path != old_path, true);
    TEST_CHECK(strcmp(declarative_installed.path, "/tmp/reload-next.jsonc"), 0);
    TEST_CHECK(declarative_installed.fold_context != old_context, true);
    TEST_CHECK(g_window_manager.rules != old_rules, true);
    TEST_CHECK(rule_native_prefix_length(), 1U);
    TEST_CHECK(buf_len(g_window_manager.rules), 3);
    TEST_CHECK(strcmp(g_window_manager.rules[0].label, "native-once"), 0);
    TEST_CHECK(strcmp(g_window_manager.rules[1].label, "runtime-a"), 0);
    TEST_CHECK(strcmp(g_window_manager.rules[2].label, "runtime-b"), 0);
    TEST_CHECK(rule_check_flag(&g_window_manager.rules[0], RULE_ONE_SHOT), true);
    TEST_CHECK(rule_native_label_is_protected("native-once"), true);
    TEST_CHECK(declarative_reload_ownership_present(), false);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK].present, false);
    TEST_CHECK(g_window_manager.window_focus_fallback, WINDOW_FOCUS_FALLBACK_OFF);
    TEST_CHECK(g_space_manager.window_placement, CHILD_SECOND);
    TEST_CHECK(g_space_manager.window_insertion_point, INSERT_FOCUSED);
    TEST_CHECK(g_space_manager.split_ratio, 0.5f);
    TEST_CHECK(g_space_manager.window_zoom_persist, true);
    TEST_CHECK(fixture.postcommit_calls > 0, true);
    TEST_CHECK(fixture.postcommit_failures, fixture.postcommit_calls);
    struct view *new_view = table_find(&g_space_manager.view, &sid);
    TEST_CHECK(new_view != NULL, true);
    TEST_CHECK(new_view ? new_view->layout : VIEW_FLOAT, VIEW_ACCORDION_HORIZONTAL);
    TEST_CHECK(new_view ? new_view->accordion_padding : 0, 31);
    TEST_CHECK(new_view ? new_view->root != old_root : false, true);

    struct rule_effects effects = {0};
    TEST_CHECK(rule_combine_rule_effects(&g_window_manager.rules[0], &effects, NULL), RULE_FOLD_OK);
    TEST_CHECK(effects.manage, RULE_PROP_ON);
    TEST_CHECK(fixture.observed_installed_path_present, true);
    TEST_CHECK(strcmp(fixture.observed_installed_path, "/tmp/reload-initial.jsonc"), 0);

    declarative_reload_set_ownership();
    uint64_t fixed_generation = declarative_installed.generation;
    struct rule_fold_context *fixed_context = declarative_installed.fold_context;
    struct declarative_config_model *fixed_model = declarative_installed.model;
    char *fixed_path = declarative_installed.path;
    TEST_CHECK(declarative_reconcile_run(&ops), true);
    TEST_CHECK(declarative_reload_ownership_present(), true);
    TEST_CHECK(g_window_manager.window_focus_fallback, WINDOW_FOCUS_FALLBACK_RECENT);
    TEST_CHECK(declarative_installed.generation, fixed_generation);
    TEST_CHECK(declarative_installed.fold_context != fixed_context, true);
    TEST_CHECK(declarative_installed.model, fixed_model);
    TEST_CHECK(declarative_installed.path, fixed_path);
    TEST_CHECK(rule_native_generation(), fixed_generation);
    TEST_CHECK(strcmp(g_window_manager.rules[1].label, "runtime-a"), 0);
    TEST_CHECK(strcmp(g_window_manager.rules[2].label, "runtime-b"), 0);

    static const char exit_accordion_json[] =
        "{\"version\":1,\"config\":{\"layout\":\"bsp\"},\"rules\":[]}";
    fixture.json = exit_accordion_json;
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reload_run(&ops, NULL), true);
    new_view = table_find(&g_space_manager.view, &sid);
    TEST_CHECK(new_view ? new_view->layout : VIEW_FLOAT, VIEW_BSP);
    TEST_CHECK(new_view ? new_view->accordion_padding : 0, 30);

    static const char native_focus_json[] =
        "{\"version\":1,\"config\":{\"window_focus_fallback\":\"recent\"}}";
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK,
                                                 &(struct declarative_value) { .present = true, .enumeration = WINDOW_FOCUS_FALLBACK_OFF }, 0),
               DECLARATIVE_APPLY_OK);
    TEST_CHECK(g_window_manager.window_focus_fallback, WINDOW_FOCUS_FALLBACK_OFF);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK].present, true);
    fixture.json = native_focus_json;
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reload_run(&ops, NULL), true);
    TEST_CHECK(g_window_manager.window_focus_fallback, WINDOW_FOCUS_FALLBACK_RECENT);
    TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK].present, false);

    declarative_reload_test_teardown(&fixture, &saved);
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_reconcile_refreshes_stale_rule_context_without_retargeting,
{
    static const char candidate_json[] =
        "{\"version\":1,\"rules\":[{\"label\":\"fixed-target\","
        "\"match\":{\"app\":{\"pattern\":\"^App$\"}},"
        "\"effects\":{\"display\":{\"index\":2},\"space\":{\"index\":1},"
        "\"follow\":true,\"manage\":true}}]}";
    struct declarative_reload_fixture fixture = {
        .json = candidate_json, .path = "/tmp/reconcile-rule-context.jsonc"
    };
    struct declarative_reload_saved_state saved;
    TEST_CHECK(declarative_reload_test_setup(&fixture, &saved), true);
    if (!g_event_loop.semaphore) return result;

    struct declarative_reload_test_ops ops = declarative_reload_test_ops(&fixture);
    TEST_CHECK(declarative_reload_run(&ops, NULL), true);
    struct rule runtime = { .label = strdup("runtime-target") };
    runtime.effects.did = 2;
    TEST_CHECK(rule_add(&runtime), RULE_ADD_OK);

    uint64_t generation = declarative_installed.generation;
    struct rule *rules = g_window_manager.rules;
    struct rule *native = &rules[0];
    uint64_t fixed_sid = native->effects.sid;
    uint32_t fixed_did = native->effects.did;
    char fixed_space_uuid[64];
    snprintf(fixed_space_uuid, sizeof(fixed_space_uuid), "%s", native->space_uuid);
    int fixed_kind = native->expected_space_kind;
    struct rule_fold_context *context = declarative_installed.fold_context;
    uint64_t topology_fingerprint = declarative_installed.topology.fingerprint;

    /* A fold-context allocation failure cannot publish partial current topology. */
    uint64_t sid = 11;
    struct view *preserved_view = table_find(&g_space_manager.view, &sid);
    struct window_node *preserved_root = preserved_view ? preserved_view->root : NULL;
    declarative_rules_test_fail_allocation_at(1);
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reconcile_run(&ops), false);
    declarative_rules_test_reset_allocator();
    TEST_CHECK(declarative_installed.fold_context, context);
    TEST_CHECK(declarative_installed.topology.fingerprint, topology_fingerprint);
    TEST_CHECK(preserved_view ? preserved_view->root : NULL, preserved_root);

    /* Failures after complete topology/context preparation refresh rule
     * validation but preserve the Space forest and runtime ownership. */
    declarative_space_test_fail_allocation_at(1);
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reconcile_run(&ops), false);
    declarative_space_test_fail_allocation_at(0);
    TEST_CHECK(declarative_installed.fold_context != context, true);
    TEST_CHECK(declarative_installed.topology.fingerprint, topology_fingerprint);
    TEST_CHECK(preserved_view ? preserved_view->root : NULL, preserved_root);
    context = declarative_installed.fold_context;

    declarative_space_test_set_admission(9101, false);
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reconcile_run(&ops), false);
    declarative_space_test_set_admission(9102, true);
    TEST_CHECK(declarative_installed.fold_context != context, true);
    TEST_CHECK(declarative_installed.topology.fingerprint, topology_fingerprint);
    TEST_CHECK(preserved_view ? preserved_view->root : NULL, preserved_root);
    context = declarative_installed.fold_context;

    /* Reusing the numeric SID with another identity makes only move/follow stale. */
    fixture.topology.replacement_uuid_sid = 11;
    fixture.topology.replacement_uuid = "space-reused";
    if (preserved_view && preserved_view->uuid) CFRelease(preserved_view->uuid);
    if (preserved_view) preserved_view->uuid = CFStringCreateWithCString(NULL, "space-reused", kCFStringEncodingUTF8);
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reconcile_run(&ops), true);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.fold_context != context, true);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(native->effects.sid, fixed_sid);
    TEST_CHECK(native->effects.did, fixed_did);
    TEST_CHECK(strcmp(native->space_uuid, fixed_space_uuid), 0);
    TEST_CHECK(native->expected_space_kind, fixed_kind);

    struct rule_effects effects = {0};
    TEST_CHECK(rule_combine_rule_effects(native, &effects, NULL), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    TEST_CHECK(effects.sid, 0ULL);
    TEST_CHECK(effects.did, 0U);
    TEST_CHECK(rule_effects_check_flag(&effects, RULE_FOLLOW_SPACE), false);
    TEST_CHECK(effects.manage, RULE_PROP_ON);
    TEST_CHECK(rule_acknowledge_stale_warning(native), true);
    struct rule_effects repeated = {0};
    TEST_CHECK(rule_combine_rule_effects(native, &repeated, NULL), RULE_FOLD_OK);
    TEST_CHECK(repeated.manage, RULE_PROP_ON);
    TEST_CHECK(repeated.sid, 0ULL);
    TEST_CHECK(repeated.did, 0U);

    struct rule_effects layered = {0};
    TEST_CHECK(rule_combine_rule_effects(native, &layered, NULL), RULE_FOLD_OK);
    TEST_CHECK(rule_combine_rule_effects(&rules[1], &layered, NULL), RULE_FOLD_OK);
    TEST_CHECK(layered.did, 2U);
    TEST_CHECK(layered.sid, 0ULL);
    TEST_CHECK(layered.manage, RULE_PROP_ON);

    /* A kind change is stale under the same generation and does not rearm the warning. */
    context = declarative_installed.fold_context;
    fixture.topology.replacement_uuid_sid = 0;
    fixture.topology.replacement_uuid = NULL;
    fixture.topology.replacement_kind_sid = 11;
    fixture.topology.replacement_kind = 4;
    if (preserved_view && preserved_view->uuid) CFRelease(preserved_view->uuid);
    if (preserved_view) preserved_view->uuid = CFStringCreateWithCString(NULL, "space-11", kCFStringEncodingUTF8);
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reconcile_run(&ops), true);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.fold_context != context, true);
    TEST_CHECK(g_window_manager.rules, rules);
    struct rule_effects kind_changed = {0};
    TEST_CHECK(rule_combine_rule_effects(native, &kind_changed, NULL), RULE_FOLD_OK);
    TEST_CHECK(kind_changed.sid, 0ULL);
    TEST_CHECK(kind_changed.did, 0U);
    TEST_CHECK(kind_changed.manage, RULE_PROP_ON);

    /* Removing the captured target also refreshes validation without changing the rule. */
    uint64_t first[] = { 12 };
    uint64_t second[] = { 21 };
    for (int i = 0; i < 2; ++i) {
        CFRelease(fixture.topology.topology[i]);
        fixture.topology.topology[i] = declarative_topology_test_topology(CFSTR("display-a"), first, 1,
                                                                         CFSTR("display-b"), second, 1);
        fixture.topology.active_sid[i] = 12;
    }
    fixture.topology.replacement_kind_sid = 0;
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    uint64_t removed_sid = 11;
    struct view *removed = table_find(&g_space_manager.view, &removed_sid);
    TEST_CHECK(removed != NULL, true);
    table_remove(&g_space_manager.view, &removed_sid);
    if (removed) { view_destroy(removed); free(removed); }
    g_space_manager.current_space_id = 12;
    context = declarative_installed.fold_context;
    TEST_CHECK(declarative_reconcile_run(&ops), true);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.fold_context != context, true);
    TEST_CHECK(g_window_manager.rules, rules);
    TEST_CHECK(native->effects.sid, fixed_sid);
    TEST_CHECK(strcmp(native->space_uuid, fixed_space_uuid), 0);
    struct rule_effects removed_effects = {0};
    TEST_CHECK(rule_combine_rule_effects(native, &removed_effects, NULL), RULE_FOLD_OK);
    TEST_CHECK(removed_effects.sid, 0ULL);
    TEST_CHECK(removed_effects.did, 0U);
    TEST_CHECK(removed_effects.manage, RULE_PROP_ON);

    declarative_reload_test_teardown(&fixture, &saved);
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_reconcile_uuidless_tuple_drift_stales_rules_without_space_transfer,
{
    static const char candidate_json[] =
        "{\"version\":1,\"rules\":[{\"label\":\"uuidless-target\","
        "\"match\":{\"app\":{\"pattern\":\"^App$\"}},"
        "\"effects\":{\"space\":{\"index\":1},\"follow\":true,\"manage\":true}}]}";
    struct declarative_reload_fixture fixture = {
        .json = candidate_json, .path = "/tmp/reconcile-uuidless-drift.jsonc"
    };
    fixture.topology.empty_uuid_sid = 11;
    struct declarative_reload_saved_state saved;
    TEST_CHECK(declarative_reload_test_setup(&fixture, &saved), true);
    if (!g_event_loop.semaphore) return result;

    struct declarative_reload_test_ops ops = declarative_reload_test_ops(&fixture);
    TEST_CHECK(declarative_reload_run(&ops, NULL), true);
    uint64_t generation = declarative_installed.generation;
    struct rule *native = &g_window_manager.rules[0];
    TEST_CHECK(native->space_identity.has_uuid, false);
    TEST_CHECK(native->space_identity.sid, 11ULL);
    TEST_CHECK(native->space_identity.mission_control_ordinal, 0);

    uint64_t sid = 11;
    struct view *view = table_find(&g_space_manager.view, &sid);
    TEST_CHECK(view != NULL, true);
    struct window_node *root = view ? view->root : NULL;
    struct bucket **managed_buckets = g_window_manager.managed_window.buckets;
    int managed_count = g_window_manager.managed_window.count;
    struct declarative_runtime_cell ownership = {
        .present = true, .source = DECLARATIVE_SETTING_RUNTIME_SPACE,
        .value.integer = VIEW_STACK
    };
    if (view) view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT] = ownership;

    uint64_t reordered[] = { 12, 11 };
    uint64_t second[] = { 21 };
    for (int i = 0; i < 2; ++i) {
        CFRelease(fixture.topology.topology[i]);
        fixture.topology.topology[i] = declarative_topology_test_topology(
            CFSTR("display-a"), reordered, 2, CFSTR("display-b"), second, 1);
        fixture.topology.active_sid[i] = 12;
    }
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    struct rule_fold_context *context = declarative_installed.fold_context;
    uint64_t topology_fingerprint = declarative_installed.topology.fingerprint;

    declarative_rules_test_fail_allocation_at(1);
    TEST_CHECK(declarative_reconcile_run(&ops), false);
    declarative_rules_test_reset_allocator();
    TEST_CHECK(declarative_installed.fold_context, context);
    TEST_CHECK(declarative_installed.topology.fingerprint, topology_fingerprint);
    TEST_CHECK(view ? view->root : NULL, root);

    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    TEST_CHECK(declarative_reconcile_run(&ops), false);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(declarative_installed.fold_context != context, true);
    TEST_CHECK(declarative_installed.topology.fingerprint != topology_fingerprint, true);
    TEST_CHECK(view ? view->root : NULL, root);
    TEST_CHECK(g_window_manager.managed_window.buckets, managed_buckets);
    TEST_CHECK(g_window_manager.managed_window.count, managed_count);
    TEST_CHECK(view ? memcmp(&view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT],
                             &ownership, sizeof(ownership)) : -1, 0);
    TEST_CHECK(native->effects.sid, 11ULL);
    TEST_CHECK(native->space_identity.mission_control_ordinal, 0);

    struct rule_effects effects = {0};
    TEST_CHECK(rule_combine_rule_effects(native, &effects, NULL), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    TEST_CHECK(effects.sid, 0ULL);
    TEST_CHECK(rule_effects_check_flag(&effects, RULE_FOLLOW_SPACE), false);
    TEST_CHECK(effects.manage, RULE_PROP_ON);
    TEST_CHECK(rule_acknowledge_stale_warning(native), true);
    effects = (struct rule_effects) {0};
    TEST_CHECK(rule_combine_rule_effects(native, &effects, NULL), RULE_FOLD_OK);
    TEST_CHECK(effects.sid, 0ULL);
    TEST_CHECK(effects.manage, RULE_PROP_ON);

    uint64_t missing[] = { 12, 99 };
    for (int i = 0; i < 2; ++i) {
        CFRelease(fixture.topology.topology[i]);
        fixture.topology.topology[i] = declarative_topology_test_topology(
            CFSTR("display-a"), missing, 2, CFSTR("display-b"), second, 1);
        fixture.topology.active_sid[i] = 12;
    }
    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    context = declarative_installed.fold_context;
    int view_count = g_space_manager.view.count;
    struct bucket **view_buckets = g_space_manager.view.buckets;
    uint64_t missing_sid = 99;
    TEST_CHECK(declarative_reconcile_run(&ops), false);
    TEST_CHECK(declarative_installed.fold_context != context, true);
    TEST_CHECK(g_space_manager.view.count, view_count);
    TEST_CHECK(g_space_manager.view.buckets, view_buckets);
    TEST_CHECK(table_find(&g_space_manager.view, &missing_sid) == NULL, true);
    TEST_CHECK(view ? view->root : NULL, root);
    TEST_CHECK(g_window_manager.managed_window.buckets, managed_buckets);
    TEST_CHECK(g_window_manager.managed_window.count, managed_count);
    TEST_CHECK(view ? memcmp(&view->runtime_ownership[DECLARATIVE_RUNTIME_SPACE_LAYOUT],
                             &ownership, sizeof(ownership)) : -1, 0);

    fixture.topology.observation = 0;
    fixture.topology.query_count = 0;
    context = declarative_installed.fold_context;
    generation = declarative_installed.generation;
    TEST_CHECK(declarative_reload_run(&ops, NULL), false);
    TEST_CHECK(declarative_installed.fold_context, context);
    TEST_CHECK(declarative_installed.generation, generation);
    TEST_CHECK(g_space_manager.view.count, view_count);
    TEST_CHECK(g_space_manager.view.buckets, view_buckets);
    TEST_CHECK(table_find(&g_space_manager.view, &missing_sid) == NULL, true);
    TEST_CHECK(view ? view->root : NULL, root);

    declarative_reload_test_teardown(&fixture, &saved);
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
    TEST_CHECK(declarative_rules_test_outstanding_allocations(), 0U);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_reconcile_preserves_live_imperative_bsp_defaults_across_lifecycle_triggers,
{
    static const char candidate_json[] = "{\"version\":1,\"rules\":[]}";
    struct declarative_reload_fixture fixture = {
        .json = candidate_json, .path = "/tmp/reconcile-live-defaults.jsonc"
    };
    struct declarative_reload_saved_state saved;
    TEST_CHECK(declarative_reload_test_setup(&fixture, &saved), true);
    if (!g_event_loop.semaphore) return result;

    declarative_reload_set_inventory(&fixture, 3);
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_WINDOW_PLACEMENT,
                                                 &(struct declarative_value) { .present = true, .enumeration = CHILD_FIRST }, 0),
               DECLARATIVE_APPLY_OK);
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_WINDOW_INSERT_POINT,
                                                 &(struct declarative_value) { .present = true, .enumeration = INSERT_FOCUSED }, 0),
               DECLARATIVE_APPLY_OK);
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_SPLIT_RATIO,
                                                 &(struct declarative_value) { .present = true, .real = 0.73f }, 0),
               DECLARATIVE_APPLY_OK);
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_WINDOW_ZOOM_PERSIST,
                                                 &(struct declarative_value) { .present = true, .boolean = false }, 0),
               DECLARATIVE_APPLY_OK);
    g_window_manager.focused_window_id = 2;
    uint64_t sid = 11;
    g_space_manager.current_space_id = sid;
    struct view *view = table_find(&g_space_manager.view, &sid);
    TEST_CHECK(view != NULL, true);
    if (view) view_clear_flag(view, VIEW_ENABLE_GAP);

    struct declarative_space_defaults live = declarative_reconcile_view_defaults();
    TEST_CHECK(live.split_ratio, 0.73f);
    TEST_CHECK(live.window_placement, CHILD_FIRST);
    TEST_CHECK(live.window_insertion_point, INSERT_FOCUSED);
    TEST_CHECK(live.window_zoom_persist, false);
    TEST_CHECK(live.focused_window_id, 2U);
    TEST_CHECK(live.window_gap_enabled, false);

    struct declarative_reload_test_ops ops = declarative_reload_test_ops(&fixture);
    /* Dock restart and system wake both converge through this same reserved event-turn operation. */
    TEST_CHECK(declarative_reconcile_run(&ops), true);
    view = table_find(&g_space_manager.view, &sid);
    struct window_node *focused = view ? declarative_reload_find_wid(view->root, 2) : NULL;
    TEST_CHECK(focused != NULL, true);
    TEST_CHECK(focused && focused->parent ? focused->parent->right : NULL, focused);
    TEST_CHECK(focused && focused->parent ? focused->parent->left->window_list[0] : 0U, 3U);
    TEST_CHECK(focused && focused->parent ? focused->parent->ratio : 0.0f, 0.73f);
    if (focused) focused->zoom = focused->parent;

    declarative_reload_set_inventory(&fixture, 4);
    TEST_CHECK(declarative_reconcile_run(&ops), true);
    view = table_find(&g_space_manager.view, &sid);
    focused = view ? declarative_reload_find_wid(view->root, 2) : NULL;
    TEST_CHECK(focused != NULL, true);
    TEST_CHECK(focused && focused->parent ? focused->parent->right : NULL, focused);
    TEST_CHECK(focused && focused->parent ? focused->parent->left->window_list[0] : 0U, 4U);
    TEST_CHECK(focused ? focused->zoom : NULL, NULL);
    TEST_CHECK(g_space_manager.window_placement, CHILD_FIRST);
    TEST_CHECK(g_space_manager.window_insertion_point, INSERT_FOCUSED);
    TEST_CHECK(g_space_manager.split_ratio, 0.73f);
    TEST_CHECK(g_space_manager.window_zoom_persist, false);

    declarative_reload_test_teardown(&fixture, &saved);
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
})

TEST_FUNC(declarative_reload_generation_zero_rediscovers_then_pins_installed_path,
{
    static const char candidate_json[] =
        "{\"version\":1,\"config\":{\"window_gap\":13},\"rules\":[]}";
    struct declarative_reload_fixture fixture = {
        .json = candidate_json, .path = "/tmp/xdg/yabai/yabai.jsonc"
    };
    struct declarative_reload_saved_state saved;
    TEST_CHECK(declarative_reload_test_setup(&fixture, &saved), true);
    if (!g_event_loop.semaphore) return result;

    char old_cwd[PATH_MAX] = {0};
    TEST_CHECK(getcwd(old_cwd, sizeof(old_cwd)) != NULL, true);
    const char *old_xdg_value = getenv("XDG_CONFIG_HOME");
    const char *old_home_value = getenv("HOME");
    char *old_xdg = old_xdg_value ? strdup(old_xdg_value) : NULL;
    char *old_home = old_home_value ? strdup(old_home_value) : NULL;
    setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
    setenv("HOME", "/tmp/home-a", 1);

    declarative_install_test_set_generation(0);
    struct declarative_reload_test_ops ops = declarative_reload_test_ops(&fixture);
    TEST_CHECK(declarative_reload_run(&ops, NULL), true);
    TEST_CHECK(fixture.observed_installed_path_present, false);
    TEST_CHECK(strcmp(fixture.observed_xdg, "/tmp/xdg"), 0);
    TEST_CHECK(strcmp(fixture.observed_home, "/tmp/home-a"), 0);
    TEST_CHECK(declarative_installed.generation, 1ULL);
    TEST_CHECK(strcmp(declarative_installed.path, "/tmp/xdg/yabai/yabai.jsonc"), 0);

    fixture.path = "/tmp/xdg/yabai/yabai.jsonc";
    setenv("XDG_CONFIG_HOME", "/tmp/other-xdg", 1);
    setenv("HOME", "/tmp/home-b", 1);
    chdir("/tmp");
    TEST_CHECK(declarative_reload_run(&ops, NULL), true);
    TEST_CHECK(fixture.observed_installed_path_present, true);
    TEST_CHECK(strcmp(fixture.observed_installed_path, "/tmp/xdg/yabai/yabai.jsonc"), 0);
    TEST_CHECK(declarative_installed.generation, 2ULL);

    if (old_cwd[0]) chdir(old_cwd);
    if (old_xdg) { setenv("XDG_CONFIG_HOME", old_xdg, 1); free(old_xdg); }
    else unsetenv("XDG_CONFIG_HOME");
    if (old_home) { setenv("HOME", old_home, 1); free(old_home); }
    else unsetenv("HOME");
    declarative_reload_test_teardown(&fixture, &saved);
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
});
