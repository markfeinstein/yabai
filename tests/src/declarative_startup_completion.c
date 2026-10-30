struct declarative_startup_completion_workspace_version
{
    long major;
};

static struct declarative_startup_completion_workspace_version
    declarative_startup_completion_test_capture_workspace_version(void)
{
    return (struct declarative_startup_completion_workspace_version) {
        .major = workspace_macos_major_version()
    };
}

static void declarative_startup_completion_test_restore_workspace_version(
    struct declarative_startup_completion_workspace_version version)
{
    workspace_set_macos_version(version.major);
}

struct declarative_startup_completion_test_context
{
    char order[16];
    size_t order_count;
    bool notification_result;
    bool menu_result;
    bool socket_result;
    bool expect_native;
    bool native_seen_before_inventory;
    bool native_effect_applied;
    bool delayed_fold_used_installed_generation;
    bool exercise_runtime_override;
    bool runtime_rule_appended_after_native;
    bool runtime_effect_overrode_native;
    unsigned inventory_count;
    unsigned notification_count;
    unsigned menu_count;
    unsigned socket_count;
    unsigned legacy_count;
};

static void declarative_startup_completion_test_record(struct declarative_startup_completion_test_context *test, char step)
{
    if (test->order_count + 1 < sizeof(test->order)) {
        test->order[test->order_count++] = step;
        test->order[test->order_count] = '\0';
    }
}

static void declarative_startup_completion_test_inventory(void *context)
{
    struct declarative_startup_completion_test_context *test = context;
    declarative_startup_completion_test_record(test, 'I');
    ++test->inventory_count;
    if (!test->expect_native) {
        test->native_seen_before_inventory = rule_native_generation() == 0 &&
                                             rule_native_prefix_length() == 0 &&
                                             buf_len(g_window_manager.rules) == 0;
        return;
    }

    test->native_seen_before_inventory = rule_native_generation() == 1 &&
                                         rule_native_prefix_length() == 1 &&
                                         buf_len(g_window_manager.rules) == 1 &&
                                         g_window_manager.rules[0].owner == RULE_OWNER_NATIVE;

    struct rule_effects effects = {0};
    enum rule_fold_result result = rule_combine_rule_effects(&g_window_manager.rules[0], &effects, NULL);
    test->native_effect_applied = result == RULE_FOLD_OK && effects.manage == RULE_PROP_ON;
    free(effects.scratchpad);

    effects = (struct rule_effects) {0};
    result = rule_combine_rule_effects(&g_window_manager.rules[0], &effects, NULL);
    test->delayed_fold_used_installed_generation = result == RULE_FOLD_OK && effects.manage == RULE_PROP_ON;
    free(effects.scratchpad);
}

static bool declarative_startup_completion_test_notifications(void *context)
{
    struct declarative_startup_completion_test_context *test = context;
    declarative_startup_completion_test_record(test, 'N');
    ++test->notification_count;
    g_window_notification_rebuild_result = test->notification_result;
    return update_window_notifications();
}

static bool declarative_startup_completion_test_menu(void *context)
{
    struct declarative_startup_completion_test_context *test = context;
    declarative_startup_completion_test_record(test, 'M');
    ++test->menu_count;
    return test->menu_result;
}

static bool declarative_startup_completion_test_socket(void *context)
{
    struct declarative_startup_completion_test_context *test = context;
    declarative_startup_completion_test_record(test, 'S');
    ++test->socket_count;
    return test->socket_result;
}

static void declarative_startup_completion_test_legacy(void *context)
{
    struct declarative_startup_completion_test_context *test = context;
    declarative_startup_completion_test_record(test, 'L');
    ++test->legacy_count;

    if (test->expect_native && test->exercise_runtime_override) {
        struct rule runtime = {
            .owner = RULE_OWNER_RUNTIME,
            .effects = { .manage = RULE_PROP_OFF }
        };
        buf_push(g_window_manager.rules, runtime);
        test->runtime_rule_appended_after_native = buf_len(g_window_manager.rules) == 2 &&
                                                   g_window_manager.rules[0].owner == RULE_OWNER_NATIVE &&
                                                   g_window_manager.rules[1].owner == RULE_OWNER_RUNTIME;

        struct rule_effects effects = {0};
        rule_combine_rule_effects(&g_window_manager.rules[0], &effects, NULL);
        rule_combine_rule_effects(&g_window_manager.rules[1], &effects, NULL);
        test->runtime_effect_overrode_native = effects.manage == RULE_PROP_OFF;
        free(effects.scratchpad);
    }
}

static struct declarative_startup_completion_ops
    declarative_startup_completion_test_ops(struct declarative_startup_completion_test_context *test,
                                            bool notifications_required)
{
    workspace_set_macos_version(notifications_required ? 15 : 14);
    declarative_startup_production_test_overrides = (struct declarative_startup_completion_ops) {
        .inventory = declarative_startup_completion_test_inventory,
        .rebuild_notifications = declarative_startup_completion_test_notifications,
        .seal_menu_staging = declarative_startup_completion_test_menu,
        .start_socket = declarative_startup_completion_test_socket,
        .execute_legacy_config = declarative_startup_completion_test_legacy,
        .context = test
    };
    return declarative_startup_production_completion_ops();
}

static void declarative_startup_completion_test_clear_overrides(void)
{
    declarative_startup_production_test_overrides = (struct declarative_startup_completion_ops) {0};
}

static void declarative_startup_completion_test_reset_rules(struct rule *saved_rules)
{
    if (g_window_manager.rules != saved_rules) {
        buf_free(g_window_manager.rules);
        g_window_manager.rules = saved_rules;
    }
    rule_native_generation_clear();
}

TEST_FUNC(declarative_startup_completion_orders_inventory_notifications_menu_socket_and_legacy,
{
    struct declarative_startup_completion_workspace_version saved_version =
        declarative_startup_completion_test_capture_workspace_version();
    struct rule *saved_rules = g_window_manager.rules;
    g_window_manager.rules = NULL;
    struct rule native = {
        .owner = RULE_OWNER_NATIVE,
        .generation = 1,
        .effects = { .manage = RULE_PROP_ON }
    };
    buf_push(g_window_manager.rules, native);
    rule_native_generation_install(1, 1, NULL, 0, NULL);
    g_window_notification_rebuild_count = 0;
    g_window_notification_rebuild_result = true;

    struct declarative_startup_completion_test_context test = {
        .notification_result = true,
        .menu_result = true,
        .socket_result = true,
        .expect_native = true,
        .exercise_runtime_override = true
    };
    struct declarative_startup_completion_ops ops = declarative_startup_completion_test_ops(&test, true);
    TEST_CHECK(declarative_startup_complete(&ops), DECLARATIVE_STARTUP_COMPLETION_OK);
    TEST_CHECK(strcmp(test.order, "INMSL"), 0);
    TEST_CHECK(test.inventory_count, 1U);
    TEST_CHECK(test.notification_count, 1U);
    TEST_CHECK(g_window_notification_rebuild_count, 1);
    TEST_CHECK(test.menu_count, 1U);
    TEST_CHECK(test.socket_count, 1U);
    TEST_CHECK(test.legacy_count, 1U);
    TEST_CHECK(test.native_seen_before_inventory, true);
    TEST_CHECK(test.native_effect_applied, true);
    TEST_CHECK(test.delayed_fold_used_installed_generation, true);
    TEST_CHECK(test.runtime_rule_appended_after_native, true);
    TEST_CHECK(test.runtime_effect_overrode_native, true);

    declarative_startup_completion_test_clear_overrides();
    declarative_startup_completion_test_reset_rules(saved_rules);
    g_window_notification_rebuild_result = true;
    declarative_startup_completion_test_restore_workspace_version(saved_version);
});

TEST_FUNC(declarative_startup_production_ops_observe_bootstrap_install_before_inventory,
{
    struct declarative_startup_completion_workspace_version saved_version =
        declarative_startup_completion_test_capture_workspace_version();
    struct event_loop saved_event_loop = g_event_loop;
    struct declarative_install_test_window_manager_state saved_window_manager =
        declarative_install_test_window_manager_capture(&g_window_manager);
    struct space_manager saved_space_manager = g_space_manager;
    struct display_manager saved_display_manager = g_display_manager;
    struct mouse_state saved_mouse_state = g_mouse_state;
    bool saved_verbose = g_verbose;
    struct menu_bar_manager_state saved_menu_manager = g_menu_bar_manager;

    g_event_loop = (struct event_loop) {0};
    declarative_install_test_window_manager_clear_nonmutex(&g_window_manager);
    g_space_manager = (struct space_manager) {0};
    g_display_manager = (struct display_manager) {0};
    g_mouse_state = (struct mouse_state) {
        .modifier = MOUSE_MOD_FN,
        .action1 = MOUSE_MODE_MOVE,
        .action2 = MOUSE_MODE_RESIZE,
        .drop_action = MOUSE_MODE_SWAP
    };
    g_menu_bar_manager = (struct menu_bar_manager_state) { .initialized = true };
    g_menu_bar_manager.settings = (struct menu_bar_settings) {
        .enabled = MENU_BAR_DISABLED,
        .style = MENU_BAR_STYLE_COMPACT,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };
    menu_bar_manager_startup_staging_open();
    declarative_install_test_reset();

    struct declarative_topology_fixture fixture;
    declarative_topology_test_fixture_init(&fixture, false);
    struct declarative_topology_capture_ops topology_ops = declarative_topology_test_ops(&fixture);
    struct declarative_startup_state startup = {
        .model = declarative_install_test_model(),
        .present = true
    };
    snprintf(startup.path, sizeof(startup.path), "%s", "/tmp/startup-completion.jsonc");
    TEST_CHECK(startup.model != NULL, true);

    struct declarative_install_test_context bootstrap = {
        .startup = &startup,
        .ops = &topology_ops
    };
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, declarative_install_test_bootstrap, &bootstrap), true);
    TEST_CHECK(declarative_installed_generation(), 1ULL);
    TEST_CHECK(declarative_installed_model() != NULL, true);
    TEST_CHECK(strcmp(declarative_installed_path(), "/tmp/startup-completion.jsonc"), 0);

    struct declarative_startup_completion_test_context test = {
        .notification_result = true,
        .menu_result = true,
        .socket_result = true,
        .expect_native = true
    };
    struct declarative_startup_completion_ops completion =
        declarative_startup_completion_test_ops(&test, true);
    TEST_CHECK(test.order_count, 0U);
    TEST_CHECK(test.legacy_count, 0U);
    TEST_CHECK(declarative_startup_complete(&completion), DECLARATIVE_STARTUP_COMPLETION_OK);
    TEST_CHECK(strcmp(test.order, "INMSL"), 0);
    TEST_CHECK(test.native_seen_before_inventory, true);
    TEST_CHECK(test.native_effect_applied, true);
    TEST_CHECK(test.delayed_fold_used_installed_generation, true);
    TEST_CHECK(test.socket_count, 1U);
    TEST_CHECK(test.legacy_count, 1U);

    declarative_startup_completion_test_clear_overrides();
    event_loop_end(&g_event_loop);
    declarative_install_test_reset();
    declarative_startup_destroy(&startup);
    declarative_topology_test_fixture_destroy(&fixture);
    menu_bar_manager_startup_staging_abort();
    g_menu_bar_manager = saved_menu_manager;
    g_verbose = saved_verbose;
    g_mouse_state = saved_mouse_state;
    g_display_manager = saved_display_manager;
    g_space_manager = saved_space_manager;
    declarative_install_test_window_manager_restore(&g_window_manager, &saved_window_manager);
    g_event_loop = saved_event_loop;
    g_window_notification_rebuild_result = true;
    declarative_startup_completion_test_restore_workspace_version(saved_version);
    TEST_CHECK(declarative_install_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_startup_completion_failure_cutoffs_are_exact,
{
    struct declarative_startup_completion_workspace_version saved_version =
        declarative_startup_completion_test_capture_workspace_version();
    struct rule *saved_rules = g_window_manager.rules;
    g_window_manager.rules = NULL;
    struct rule native = {
        .owner = RULE_OWNER_NATIVE,
        .generation = 1,
        .effects = { .manage = RULE_PROP_ON }
    };
    buf_push(g_window_manager.rules, native);
    rule_native_generation_install(1, 1, NULL, 0, NULL);

    struct declarative_startup_completion_test_context notification = {
        .notification_result = false, .menu_result = true, .socket_result = true, .expect_native = true
    };
    struct declarative_startup_completion_ops ops = declarative_startup_completion_test_ops(&notification, true);
    TEST_CHECK(declarative_startup_complete(&ops), DECLARATIVE_STARTUP_COMPLETION_NOTIFICATION_FAILED);
    TEST_CHECK(strcmp(notification.order, "IN"), 0);
    TEST_CHECK(notification.socket_count, 0U);
    TEST_CHECK(notification.legacy_count, 0U);

    struct declarative_startup_completion_test_context menu = {
        .notification_result = true, .menu_result = false, .socket_result = true, .expect_native = true
    };
    ops = declarative_startup_completion_test_ops(&menu, true);
    TEST_CHECK(declarative_startup_complete(&ops), DECLARATIVE_STARTUP_COMPLETION_MENU_FAILED);
    TEST_CHECK(strcmp(menu.order, "INM"), 0);
    TEST_CHECK(menu.socket_count, 0U);
    TEST_CHECK(menu.legacy_count, 0U);

    struct declarative_startup_completion_test_context socket = {
        .notification_result = true, .menu_result = true, .socket_result = false, .expect_native = true
    };
    ops = declarative_startup_completion_test_ops(&socket, true);
    TEST_CHECK(declarative_startup_complete(&ops), DECLARATIVE_STARTUP_COMPLETION_SOCKET_FAILED);
    TEST_CHECK(strcmp(socket.order, "INMS"), 0);
    TEST_CHECK(socket.legacy_count, 0U);

    buf_free(g_window_manager.rules);
    g_window_manager.rules = NULL;
    rule_native_generation_clear();
    struct declarative_startup_completion_test_context absent_native = {
        .notification_result = true, .menu_result = true, .socket_result = true, .expect_native = false
    };
    ops = declarative_startup_completion_test_ops(&absent_native, false);
    TEST_CHECK(absent_native.order_count, 0U);
    TEST_CHECK(absent_native.legacy_count, 0U);
    TEST_CHECK(declarative_startup_complete(&ops), DECLARATIVE_STARTUP_COMPLETION_OK);
    TEST_CHECK(strcmp(absent_native.order, "IMSL"), 0);
    TEST_CHECK(absent_native.notification_count, 0U);
    TEST_CHECK(absent_native.native_seen_before_inventory, true);
    TEST_CHECK(absent_native.socket_count, 1U);
    TEST_CHECK(absent_native.legacy_count, 1U);

    declarative_startup_completion_test_clear_overrides();
    declarative_startup_completion_test_reset_rules(saved_rules);
    g_window_notification_rebuild_result = true;
    declarative_startup_completion_test_restore_workspace_version(saved_version);
});
