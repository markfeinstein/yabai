static uint32_t launch_test_inventory[8];
static int launch_test_inventory_count;
static bool launch_test_inventory_success;
static int launch_test_probe_count;
static int launch_test_probe_limit;
static struct window launch_test_windows[8];

static bool launch_test_inventory_op(struct application *application, uint32_t **window_list, int *window_count)
{
    (void) application;
    *window_list = launch_test_inventory;
    *window_count = launch_test_inventory_count;
    return launch_test_inventory_success;
}

static CFArrayRef launch_test_ax_list_op(struct application *application)
{
    (void) application;
    return NULL;
}

static int launch_test_release_count;
static int launch_test_retain_count;
static uint64_t launch_test_last_element_id;

static CFTypeRef launch_test_retry_retain_op(CFTypeRef value)
{
    ++launch_test_retain_count;
    return value;
}

static void launch_test_retry_release_op(CFTypeRef value)
{
    (void) value;
    ++launch_test_release_count;
}

static AXUIElementRef launch_test_remote_token_op(CFDataRef data_ref)
{
    uint8_t *data = CFDataGetMutableBytePtr((CFMutableDataRef) data_ref);
    memcpy(&launch_test_last_element_id, data + 0xc, sizeof(launch_test_last_element_id));
    ++launch_test_probe_count;
    bool wanted = false;
    for (int i = 0; i < launch_test_inventory_count; ++i) {
        if (launch_test_inventory[i] == launch_test_last_element_id) { wanted = true; break; }
    }
    if (!wanted) return NULL;
    return (AXUIElementRef) (uintptr_t) (launch_test_last_element_id + 1);
}

static AXError launch_test_copy_role_op(AXUIElementRef element_ref, CFTypeRef *role)
{
    (void) element_ref;
    *role = (CFTypeRef) (uintptr_t) 1;
    return kAXErrorSuccess;
}

static bool launch_test_role_is_window_op(CFTypeRef role)
{
    return role != NULL;
}

static uint32_t launch_test_ax_window_id_op(AXUIElementRef element_ref)
{
    return (uint32_t) ((uintptr_t) element_ref - 1);
}

static void launch_test_cf_release_op(CFTypeRef value)
{
    (void) value;
    ++launch_test_release_count;
}

static struct window *launch_test_create_window_op(struct space_manager *sm, struct window_manager *wm, struct application *application,
                                                    AXUIElementRef element_ref, uint32_t window_id, bool one_shot_rules, pid_t one_shot_owner_pid)
{
    (void) sm;
    (void) element_ref;
    (void) one_shot_rules;
    (void) one_shot_owner_pid;
    if (launch_test_probe_limit <= 0) return NULL;
    --launch_test_probe_limit;
    struct window *window = &launch_test_windows[window_id % array_count(launch_test_windows)];
    *window = (struct window) { .id = window_id, .application = application };
    table_add(&wm->window, &window->id, window);
    return window;
}

static void launch_test_begin(struct window_manager *wm)
{
    if (!g_temp_storage.memory) ts_init(MEGABYTES(1));
    memset(wm, 0, sizeof(*wm));
    table_init(&wm->window, 16, hash_wm, compare_wm);
    launch_test_inventory_count = 0;
    launch_test_inventory_success = true;
    launch_test_probe_count = 0;
    launch_test_release_count = 0;
    launch_test_retain_count = 0;
    launch_test_probe_limit = 8;
    memset(launch_test_windows, 0, sizeof(launch_test_windows));
    window_manager_test_existing_application_window_list = launch_test_inventory_op;
    window_manager_test_application_window_list = launch_test_ax_list_op;
    window_manager_test_remote_token = launch_test_remote_token_op;
    window_manager_test_copy_role = launch_test_copy_role_op;
    window_manager_test_role_is_window = launch_test_role_is_window_op;
    window_manager_test_ax_window_id = launch_test_ax_window_id_op;
    window_manager_test_cf_release = launch_test_cf_release_op;
    window_manager_test_create_window = launch_test_create_window_op;
    window_manager_test_retry_retain = launch_test_retry_retain_op;
    window_manager_test_retry_release = launch_test_retry_release_op;
}

static void launch_test_end(struct window_manager *wm)
{
    window_manager_test_existing_application_window_list = NULL;
    window_manager_test_application_window_list = NULL;
    window_manager_test_remote_token = NULL;
    window_manager_test_copy_role = NULL;
    window_manager_test_role_is_window = NULL;
    window_manager_test_ax_window_id = NULL;
    window_manager_test_cf_release = NULL;
    window_manager_test_create_window = NULL;
    window_manager_test_retry_retain = NULL;
    window_manager_test_retry_release = NULL;
    buf_free(wm->applications_to_refresh);
    buf_free(wm->rules);
    table_free(&wm->window);
}

static struct rule launch_test_app_rule(const char *pattern)
{
    struct rule rule = { .flags = RULE_ONE_SHOT | RULE_APP_VALID };
    regcomp(&rule.app_regex, pattern, REG_EXTENDED | REG_NOSUB);
    return rule;
}

TEST_FUNC(inactive_space_launch_partial_inventory_reprobes_and_removes_real_queue_entry,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    struct application application = { .pid = 41, .name = "Partial", .launch_refresh = true };
    launch_test_inventory_count = 3;
    launch_test_inventory[0] = 1;
    launch_test_inventory[1] = 2;
    launch_test_inventory[2] = 4097;
    launch_test_probe_limit = 2;

    struct window **created = NULL;
    int created_count = 0;
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &wm, &application, -1, &created, &created_count), false);
    TEST_CHECK(created_count, 2);
    TEST_CHECK(buf_len(wm.applications_to_refresh), 1);
    TEST_CHECK(application.launch_refresh, true);
    buf_free(created);

    launch_test_probe_limit = 8;
    created = NULL;
    created_count = 0;
    window_manager_begin_inactive_probe_pass(4096);
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &wm, &application, 0, &created, &created_count), true);
    TEST_CHECK(launch_test_probe_count > 0, true);
    TEST_CHECK(launch_test_release_count > 0, true);
    TEST_CHECK(created_count, 1);
    TEST_CHECK(buf_len(wm.applications_to_refresh), 0);
    TEST_CHECK(application.launch_refresh, false);
    buf_free(created);
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_inventory_failure_is_deduplicated_and_retried,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    struct application application = { .pid = 42, .name = "Retry", .launch_refresh = true };
    launch_test_inventory_success = false;
    window_manager_add_existing_application_windows(&g_space_manager, &wm, &application, -1, NULL, NULL);
    window_manager_add_existing_application_windows(&g_space_manager, &wm, &application, -1, NULL, NULL);
    TEST_CHECK(buf_len(wm.applications_to_refresh), 1);
    launch_test_inventory_success = true;
    launch_test_inventory_count = 0;
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &wm, &application, 0, NULL, NULL), true);
    TEST_CHECK(buf_len(wm.applications_to_refresh), 0);
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_overlapping_reservations_are_application_owned,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    buf_push(wm.rules, launch_test_app_rule("^Alpha$"));
    buf_push(wm.rules, launch_test_app_rule("^Beta$"));
    struct window_manager saved = g_window_manager;
    g_window_manager.rules = wm.rules;
    struct application alpha = { .pid = 51, .name = "Alpha" };
    struct application beta = { .pid = 52, .name = "Beta" };
    rule_reserve_one_shots(&alpha);
    rule_reserve_one_shots(&beta);
    TEST_CHECK(wm.rules[0].one_shot_owner_pid, 51);
    TEST_CHECK(wm.rules[1].one_shot_owner_pid, 52);
    TEST_CHECK(window_manager_rule_is_available(&wm.rules[0], &alpha, true, 51, 1), true);
    TEST_CHECK(window_manager_rule_is_available(&wm.rules[0], &beta, true, 52, 1), false);
    rule_cancel_one_shots(&alpha);
    rule_cancel_one_shots(&beta);
    g_window_manager = saved;
    rule_destroy(&wm.rules[0]);
    rule_destroy(&wm.rules[1]);
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_reserved_one_shot_is_available_to_only_one_window,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    buf_push(wm.rules, launch_test_app_rule("^Owner$"));
    struct window_manager saved = g_window_manager;
    g_window_manager.rules = wm.rules;
    struct application owner = { .pid = 60, .name = "Owner" };
    rule_reserve_one_shots(&owner);
    TEST_CHECK(window_manager_rule_is_available(&wm.rules[0], &owner, true, 60, 100), true);
    wm.rules[0].one_shot_window_id = 100;
    rule_set_flag(&wm.rules[0], RULE_ONE_SHOT_REMOVE);
    TEST_CHECK(window_manager_rule_is_available(&wm.rules[0], &owner, true, 60, 100), true);
    TEST_CHECK(window_manager_rule_is_available(&wm.rules[0], &owner, true, 60, 101), false);
    rule_cleanup_consumed_one_shots(&owner);
    TEST_CHECK(buf_len(wm.rules), 0);
    g_window_manager = saved;
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_reserved_rule_ignores_ordinary_window_created_cleanup,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    buf_push(wm.rules, launch_test_app_rule("^Owner$"));
    struct window_manager saved = g_window_manager;
    g_window_manager.rules = wm.rules;
    struct application owner = { .pid = 61, .name = "Owner" };
    rule_reserve_one_shots(&owner);
    rule_set_flag(&wm.rules[0], RULE_ONE_SHOT_REMOVE);
    TEST_CHECK(window_manager_rule_is_available(&wm.rules[0], &owner, true, 0, 1), false);
    rule_cleanup_one_shots();
    TEST_CHECK(buf_len(wm.rules), 1);
    rule_cancel_one_shots(&owner);
    TEST_CHECK(rule_check_flag(&wm.rules[0], RULE_ONE_SHOT_REMOVE), false);
    g_window_manager = saved;
    rule_destroy(&wm.rules[0]);
    launch_test_end(&wm);
})

static void launch_test_reset_refresh_queue(struct window_manager *wm,
                                            struct application *a, struct application *b,
                                            struct application *c, struct application *d, int cursor)
{
    buf_free(wm->applications_to_refresh);
    wm->applications_to_refresh = NULL;
    buf_push(wm->applications_to_refresh, a);
    buf_push(wm->applications_to_refresh, b);
    buf_push(wm->applications_to_refresh, c);
    buf_push(wm->applications_to_refresh, d);
    wm->application_refresh_cursor = cursor;
}

TEST_FUNC(inactive_space_launch_refresh_cursor_tracks_swap_deleted_identity,
{
    struct application a = { .pid = 610, .name = "A" };
    struct application b = { .pid = 611, .name = "B" };
    struct application c = { .pid = 612, .name = "C" };
    struct application d = { .pid = 613, .name = "D" };
    struct window_manager wm = {0};
    launch_test_reset_refresh_queue(&wm, &a, &b, &c, &d, 2);
    TEST_CHECK(window_manager_remove_application_refresh(&wm, &b), true);
    TEST_CHECK(wm.applications_to_refresh[wm.application_refresh_cursor], &c);
    launch_test_reset_refresh_queue(&wm, &a, &b, &c, &d, 1);
    TEST_CHECK(window_manager_remove_application_refresh(&wm, &b), true);
    TEST_CHECK(wm.applications_to_refresh[wm.application_refresh_cursor], &d);
    launch_test_reset_refresh_queue(&wm, &a, &b, &c, &d, 1);
    TEST_CHECK(window_manager_remove_application_refresh(&wm, &c), true);
    TEST_CHECK(wm.applications_to_refresh[wm.application_refresh_cursor], &b);
    launch_test_reset_refresh_queue(&wm, &a, &b, &c, &d, 3);
    TEST_CHECK(window_manager_remove_application_refresh(&wm, &b), true);
    TEST_CHECK(wm.applications_to_refresh[wm.application_refresh_cursor], &d);
    launch_test_reset_refresh_queue(&wm, &a, &b, &c, &d, 1);
    TEST_CHECK(window_manager_remove_application_refresh(&wm, &d), true);
    TEST_CHECK(wm.applications_to_refresh[wm.application_refresh_cursor], &b);
    launch_test_reset_refresh_queue(&wm, &a, &b, &c, &d, 3);
    TEST_CHECK(window_manager_remove_application_refresh(&wm, &d), true);
    TEST_CHECK(wm.applications_to_refresh[wm.application_refresh_cursor], &a);
    launch_test_reset_refresh_queue(&wm, &a, &b, &c, &d, 5);
    TEST_CHECK(window_manager_remove_application_refresh(&wm, &b), true);
    TEST_CHECK(wm.applications_to_refresh[wm.application_refresh_cursor], &d);
    buf_free(wm.applications_to_refresh);

    launch_test_begin(&wm);
    table_init(&wm.application, 4, hash_wm, compare_wm);
    struct window_manager saved = g_window_manager;
    g_window_manager = wm;
    a.launch_refresh = true; b.launch_refresh = true; c.launch_refresh = true;
    window_manager_add_application(&g_window_manager, &a);
    window_manager_add_application(&g_window_manager, &b);
    window_manager_add_application(&g_window_manager, &c);
    buf_push(g_window_manager.applications_to_refresh, &a);
    buf_push(g_window_manager.applications_to_refresh, &b);
    buf_push(g_window_manager.applications_to_refresh, &c);
    g_window_manager.application_refresh_cursor = 2;
    struct rule title_only = { .flags = RULE_ONE_SHOT | RULE_TITLE_VALID };
    TEST_CHECK(regcomp(&title_only.title_regex, "^Next$", REG_EXTENDED | REG_NOSUB), 0);
    buf_push(g_window_manager.rules, title_only);
    TEST_CHECK(rule_reserve_one_shots(&b), true);
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &g_window_manager,
                                                               &b, 1, NULL, NULL), true);
    TEST_CHECK(buf_len(g_window_manager.applications_to_refresh), 2);
    TEST_CHECK(g_window_manager.applications_to_refresh[g_window_manager.application_refresh_cursor], &c);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, c.pid);
    window_manager_remove_application(&g_window_manager, a.pid);
    window_manager_remove_application(&g_window_manager, b.pid);
    window_manager_remove_application(&g_window_manager, c.pid);
    table_free(&g_window_manager.application);
    wm = g_window_manager;
    g_window_manager = saved;
    rule_destroy(&wm.rules[0]);
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_global_probe_budget_round_robins_queue,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    struct window_manager saved = g_window_manager;
    g_window_manager = wm;
    struct application first = { .pid = 91, .name = "First" };
    struct application second = { .pid = 92, .name = "Second" };
    launch_test_inventory_count = 1;
    launch_test_inventory[0] = 5000;
    buf_push(g_window_manager.applications_to_refresh, &first);
    buf_push(g_window_manager.applications_to_refresh, &second);
    space_manager_refresh_application_windows(&g_space_manager);
    TEST_CHECK(first.inactive_probe_cursor, 4096);
    TEST_CHECK(second.inactive_probe_cursor, 0);
    TEST_CHECK(g_window_manager.application_refresh_cursor, 1);
    space_manager_refresh_application_windows(&g_space_manager);
    TEST_CHECK(second.inactive_probe_cursor, 4096);
    wm = g_window_manager;
    g_window_manager = saved;
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_retry_survives_finalization_and_rejects_stale_pid_reuse,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    table_init(&wm.application, 4, hash_wm, compare_wm);
    struct application application = { .pid = 81, .psn = { .highLongOfPSN = 1, .lowLongOfPSN = 2 }, .name = "Retry", .launch_refresh = true };
    table_add(&wm.application, &application.pid, &application);
    struct window_created_retry *owned_retry = window_manager_create_window_created_retry((AXUIElementRef)(uintptr_t) 99, &application, true, 81);
    TEST_CHECK(launch_test_retain_count, 1);
    struct window_created_retry retry = *owned_retry;
    TEST_CHECK(window_manager_resolve_window_created_retry(&wm, &retry) == &application, true);
    TEST_CHECK(window_manager_window_created_retry_owner(&application, &retry), 81);
    TEST_CHECK(window_manager_window_created_retry_uses_one_shots(&application, &retry), true);
    retry.application_psn.lowLongOfPSN = 3;
    TEST_CHECK(window_manager_resolve_window_created_retry(&wm, &retry) == NULL, true);
    retry.application_psn = application.psn;
    application.launch_refresh = false;
    TEST_CHECK(window_manager_resolve_window_created_retry(&wm, &retry) == &application, true);
    TEST_CHECK(window_manager_window_created_retry_owner(&application, &retry), 0);
    TEST_CHECK(window_manager_window_created_retry_uses_one_shots(&application, &retry), false);
    window_manager_destroy_window_created_retry(owned_retry);
    TEST_CHECK(launch_test_release_count, 1);
    table_free(&wm.application);
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_window_creation_defers_all_rule_effects_until_retry,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    table_init(&wm.application, 4, hash_wm, compare_wm);
    struct window_manager saved = g_window_manager;
    g_window_manager = wm;
    struct application terminated = { .pid = 570, .psn = { 3, 1 }, .name = "Old", .launch_refresh = true };
    struct application pending = { .pid = 571, .psn = { 3, 2 }, .name = "New", .launch_refresh = true };
    window_manager_add_application(&g_window_manager, &terminated);
    window_manager_add_application(&g_window_manager, &pending);
    buf_push(g_window_manager.applications_to_refresh, &pending);

    struct rule rule = {
        .flags = RULE_ONE_SHOT | RULE_APP_VALID,
        .effects = { .manage = RULE_PROP_ON, .mff = RULE_PROP_ON }
    };
    TEST_CHECK(regcomp(&rule.app_regex, ".*", REG_EXTENDED | REG_NOSUB), 0);
    buf_push(g_window_manager.rules, rule);
    TEST_CHECK(rule_reserve_one_shots(&terminated), true);
    window_manager_remove_application(&g_window_manager, terminated.pid);
    rule_test_fail_reservation_match_at(1, REG_ESPACE);
    TEST_CHECK(rule_cancel_one_shots(&terminated), false);
    rule_test_fail_reservation_match_at(1, REG_ESPACE);

    struct window test_window = {0};
    window_manager_test_rule_window = &test_window;
    bool retry = false;
    AXUIElementRef owned_ref = (AXUIElementRef) (uintptr_t) 99;
    TEST_CHECK(window_manager_create_and_add_window(&g_space_manager, &g_window_manager,
                                                    &pending, owned_ref, 900, true,
                                                    pending.pid, &retry), NULL);
    TEST_CHECK(retry, true);
    TEST_CHECK(g_window_manager.window.count, 0);
    TEST_CHECK(window_check_rule_flag(&test_window, WINDOW_RULE_MANAGED), false);
    TEST_CHECK(window_check_rule_flag(&test_window, WINDOW_RULE_MFF), false);
    TEST_CHECK(g_window_manager.rules[0].one_shot_window_id, 0U);

    rule_test_set_reservation_match_result(-1);
    retry = false;
    TEST_CHECK(window_manager_create_and_add_window(&g_space_manager, &g_window_manager,
                                                    &pending, owned_ref, 900, true,
                                                    pending.pid, &retry), &test_window);
    TEST_CHECK(retry, false);
    TEST_CHECK(g_window_manager.window.count, 1);
    TEST_CHECK(window_check_rule_flag(&test_window, WINDOW_RULE_MANAGED), true);
    TEST_CHECK(window_check_rule_flag(&test_window, WINDOW_RULE_MFF), true);
    TEST_CHECK(window_check_rule_flag(&test_window, WINDOW_RULE_MFF_VALUE), true);
    TEST_CHECK(buf_len(g_window_manager.rules), 0);

    window_manager_remove_window(&g_window_manager, test_window.id);
    test_window.ref = NULL;
    window_manager_test_rule_window = NULL;
    window_manager_remove_application(&g_window_manager, pending.pid);
    table_free(&g_window_manager.application);
    wm = g_window_manager;
    g_window_manager = saved;
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_unreserved_one_shots_preserve_ordinary_semantics,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    struct window_manager saved = g_window_manager;
    g_window_manager = wm;
    struct application running = { .pid = 580, .psn = { 1, 8 }, .name = "Running" };

    struct rule runtime = { .flags = RULE_ONE_SHOT | RULE_TITLE_VALID };
    TEST_CHECK(regcomp(&runtime.title_regex, "^Runtime$", REG_EXTENDED | REG_NOSUB), 0);
    TEST_CHECK(rule_add(&runtime), RULE_ADD_OK);
    struct window first = { .id = 800, .application = &running };
    window_manager_apply_rules_to_window(&g_space_manager, &g_window_manager, &first,
                                         "Runtime", "AXWindow", "AXStandardWindow", true, 0);
    TEST_CHECK(g_window_manager.rules[0].one_shot_window_id, first.id);
    rule_cleanup_one_shots();
    TEST_CHECK(buf_len(g_window_manager.rules), 0);

    struct application launcher = { .pid = 581, .psn = { 1, 9 }, .name = "Launcher" };
    struct rule native = { .flags = RULE_ONE_SHOT | RULE_TITLE_VALID };
    TEST_CHECK(regcomp(&native.title_regex, "^Native$", REG_EXTENDED | REG_NOSUB), 0);
    buf_push(g_window_manager.rules, native);
    TEST_CHECK(rule_reserve_one_shots(&launcher), true);
    TEST_CHECK(rule_finalize_one_shots(&launcher), true);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, 0);
    struct window nonmatch = { .id = 801, .application = &running };
    window_manager_apply_rules_to_window(&g_space_manager, &g_window_manager, &nonmatch,
                                         "Other", "AXWindow", "AXStandardWindow", true, 0);
    TEST_CHECK(g_window_manager.rules[0].one_shot_window_id, 0U);
    struct window match = { .id = 802, .application = &running };
    window_manager_apply_rules_to_window(&g_space_manager, &g_window_manager, &match,
                                         "Native", "AXWindow", "AXStandardWindow", true, 0);
    TEST_CHECK(g_window_manager.rules[0].one_shot_window_id, match.id);
    rule_cleanup_one_shots();
    TEST_CHECK(buf_len(g_window_manager.rules), 0);

    wm = g_window_manager;
    g_window_manager = saved;
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_initial_reservation_failures_are_atomic_and_retried,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    table_init(&wm.application, 8, hash_wm, compare_wm);
    struct window_manager saved = g_window_manager;
    g_window_manager = wm;
    buf_push(g_window_manager.rules, launch_test_app_rule("^Pending$"));
    buf_push(g_window_manager.rules, launch_test_app_rule("^Pending$"));
    struct application pending = { .pid = 590, .psn = { 2, 1 }, .name = "Pending" };
    window_manager_add_application(&g_window_manager, &pending);

    rule_test_fail_reservation_match_at(2, REG_ESPACE);
    int created_count = -1;
    TEST_CHECK(window_manager_add_application_windows(&g_space_manager, &g_window_manager,
                                                      &pending, &created_count), NULL);
    TEST_CHECK(created_count, 0);
    TEST_CHECK(pending.launch_refresh, true);
    TEST_CHECK(buf_len(g_window_manager.applications_to_refresh), 1);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, 0);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, 0);
    TEST_CHECK(rule_one_shot_reassignment_is_pending(), true);

    rule_test_set_reservation_match_result(-1);
    launch_test_inventory_success = false;
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &g_window_manager,
                                                               &pending, 0, NULL, NULL), false);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, pending.pid);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, pending.pid);
    TEST_CHECK(rule_one_shot_reassignment_is_pending(), false);

    TEST_CHECK(rule_cancel_one_shots(&pending), true);
    window_manager_remove_application_refresh(&g_window_manager, &pending);
    pending.launch_refresh = false;
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, 0);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, 0);

    rule_test_fail_reservation_allocation_at(1);
    pending.launch_refresh = false;
    TEST_CHECK(window_manager_add_application_windows(&g_space_manager, &g_window_manager,
                                                      &pending, &created_count), NULL);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, 0);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, 0);
    TEST_CHECK(rule_one_shot_reassignment_is_pending(), true);
    rule_test_fail_reservation_allocation_at(0);
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &g_window_manager,
                                                               &pending, 0, NULL, NULL), false);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, pending.pid);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, pending.pid);

    TEST_CHECK(rule_cancel_one_shots(&pending), true);
    window_manager_remove_application(&g_window_manager, pending.pid);
    table_free(&g_window_manager.application);
    wm = g_window_manager;
    g_window_manager = saved;
    rule_destroy(&wm.rules[0]);
    rule_destroy(&wm.rules[1]);
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_one_shot_handoff_is_cursor_ordered_and_psn_stable,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    table_init(&wm.application, 8, hash_wm, compare_wm);
    struct window_manager saved = g_window_manager;
    g_window_manager = wm;

    struct application stale = { .pid = 600, .name = "Stale", .launch_refresh = true };
    struct application first = { .pid = 601, .psn = { 1, 11 }, .name = "First", .launch_refresh = true };
    struct application second = { .pid = 602, .psn = { 1, 12 }, .name = "Second", .launch_refresh = true };
    window_manager_add_application(&g_window_manager, &first);
    window_manager_add_application(&g_window_manager, &second);
    buf_push(g_window_manager.applications_to_refresh, &stale);
    buf_push(g_window_manager.applications_to_refresh, &first);
    buf_push(g_window_manager.applications_to_refresh, &first);
    buf_push(g_window_manager.applications_to_refresh, &second);
    g_window_manager.application_refresh_cursor = 1;

    for (int i = 0; i < 2; ++i) {
        struct rule rule = { .flags = RULE_ONE_SHOT | RULE_APP_VALID | RULE_TITLE_VALID };
        TEST_CHECK(regcomp(&rule.app_regex, ".*", REG_EXTENDED | REG_NOSUB), 0);
        TEST_CHECK(regcomp(&rule.title_regex, "^Wanted$", REG_EXTENDED | REG_NOSUB), 0);
        buf_push(g_window_manager.rules, rule);
    }
    TEST_CHECK(rule_reserve_one_shots(&first), true);
    rule_test_fail_reservation_allocation_at(1);
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &g_window_manager,
                                                               &first, 1, NULL, NULL), false);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, first.pid);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, first.pid);
    TEST_CHECK(rule_one_shot_reassignment_is_pending(), true);
    rule_test_fail_reservation_allocation_at(0);
    rule_test_fail_reservation_match_at(2, REG_ESPACE);
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &g_window_manager,
                                                               &first, 1, NULL, NULL), false);
    TEST_CHECK(first.launch_refresh, true);
    TEST_CHECK(buf_len(g_window_manager.applications_to_refresh), 4);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, first.pid);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, first.pid);
    TEST_CHECK(rule_one_shot_reassignment_is_pending(), true);

    rule_test_set_reservation_match_result(-1);
    TEST_CHECK(window_manager_add_existing_application_windows(&g_space_manager, &g_window_manager,
                                                               &first, 1, NULL, NULL), true);
    TEST_CHECK(first.launch_refresh, false);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, second.pid);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, second.pid);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_psn_low, (uint32_t) second.psn.lowLongOfPSN);

    struct application reused = { .pid = second.pid, .psn = { 9, 99 }, .name = "Reused", .launch_refresh = true };
    TEST_CHECK(window_manager_rule_is_available(&g_window_manager.rules[0], &reused,
                                                true, reused.pid, 700), false);
    struct window matching = { .id = 700, .application = &second };
    window_manager_apply_rules_to_window(&g_space_manager, &g_window_manager, &matching,
                                         "Wanted", "AXWindow", "AXStandardWindow", true, second.pid);
    TEST_CHECK(g_window_manager.rules[0].one_shot_window_id, matching.id);
    TEST_CHECK(g_window_manager.rules[1].one_shot_window_id, matching.id);
    rule_cleanup_consumed_one_shots(&second);
    TEST_CHECK(buf_len(g_window_manager.rules), 0);

    for (int i = 0; i < 2; ++i) {
        struct rule rule = { .flags = RULE_ONE_SHOT | RULE_APP_VALID | RULE_TITLE_VALID };
        TEST_CHECK(regcomp(&rule.app_regex, ".*", REG_EXTENDED | REG_NOSUB), 0);
        TEST_CHECK(regcomp(&rule.title_regex, "^Wanted$", REG_EXTENDED | REG_NOSUB), 0);
        buf_push(g_window_manager.rules, rule);
    }
    first.launch_refresh = true;
    TEST_CHECK(rule_reserve_one_shots(&first), true);
    window_manager_remove_application(&g_window_manager, first.pid);
    rule_test_fail_reservation_match_at(2, REG_ESPACE);
    TEST_CHECK(rule_cancel_one_shots(&first), false);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, first.pid);
    TEST_CHECK(g_window_manager.rules[1].one_shot_owner_pid, first.pid);
    TEST_CHECK(rule_one_shot_reassignment_is_pending(), true);

    rule_test_fail_reservation_match_at(2, REG_ESPACE);
    struct window ordinary = { .id = 701, .application = &reused };
    window_manager_apply_rules_to_window(&g_space_manager, &g_window_manager, &ordinary,
                                         "Wanted", "AXWindow", "AXStandardWindow", true, 0);
    TEST_CHECK(g_window_manager.rules[0].one_shot_window_id, 0U);
    TEST_CHECK(g_window_manager.rules[1].one_shot_window_id, 0U);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, first.pid);

    rule_test_set_reservation_match_result(-1);
    TEST_CHECK(rule_retry_pending_one_shots(), true);
    matching.id = 702;
    window_manager_apply_rules_to_window(&g_space_manager, &g_window_manager, &matching,
                                         "Wanted", "AXWindow", "AXStandardWindow", true, second.pid);
    TEST_CHECK(g_window_manager.rules[0].one_shot_owner_pid, second.pid);
    TEST_CHECK(g_window_manager.rules[0].one_shot_window_id, matching.id);
    rule_cleanup_consumed_one_shots(&second);
    TEST_CHECK(buf_len(g_window_manager.rules), 0);

    window_manager_remove_application(&g_window_manager, second.pid);
    table_free(&g_window_manager.application);
    wm = g_window_manager;
    g_window_manager = saved;
    launch_test_end(&wm);
})

TEST_FUNC(inactive_space_launch_termination_cancels_reservation_and_real_queue_entry,
{
    struct window_manager wm;
    launch_test_begin(&wm);
    buf_push(wm.rules, launch_test_app_rule("^Terminating$"));
    struct window_manager saved = g_window_manager;
    g_window_manager.rules = wm.rules;
    struct application application = { .pid = 71, .name = "Terminating", .launch_refresh = true };
    rule_reserve_one_shots(&application);
    launch_test_inventory_success = false;
    window_manager_add_existing_application_windows(&g_space_manager, &wm, &application, -1, NULL, NULL);
    TEST_CHECK(buf_len(wm.applications_to_refresh), 1);
    rule_set_flag(&wm.rules[0], RULE_ONE_SHOT_REMOVE);
    wm.rules[0].one_shot_window_id = 900;
    rule_cancel_one_shots(&application);
    TEST_CHECK(window_manager_remove_application_refresh(&wm, &application), true);
    application.launch_refresh = false;
    TEST_CHECK(buf_len(wm.applications_to_refresh), 0);
    TEST_CHECK(buf_len(wm.rules), 0);
    g_window_manager = saved;
    launch_test_end(&wm);
})
