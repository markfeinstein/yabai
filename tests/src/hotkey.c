TEST_FUNC(hotkey_matcher_selects_exact_application_before_default,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":["
      "{\"key\":\"h\",\"modifiers\":[\"cmd\"],\"cases\":["
        "{\"application\":{\"bundle_id\":\"com.example.rex\",\"name\":\"Rex Beta\"},"
         "\"disposition\":\"remap\",\"action\":{\"type\":\"synthesize_key\",\"key\":\"h\"}},"
        "{\"disposition\":\"consume_only\"}]},"
      "{\"key\":\"j\",\"modifiers\":[\"ctrl\"],\"cases\":["
        "{\"application\":{\"name\":\"Rex Beta\"},\"disposition\":\"pass_through\"},"
        "{\"disposition\":\"consume\",\"action\":{\"type\":\"focus_window\",\"direction\":\"south\",\"robust\":true}}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "match.jsonc", &model, &error), true);
    if (model) {
        struct hotkey_match_result match;
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_H,
                                     HOTKEY_MODIFIER_COMMAND, "com.example.rex", "Rex Beta", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_REMAP);
        TEST_CHECK(match.repeat, true);
        TEST_CHECK(match.action_count, 1U);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_H,
                                     HOTKEY_MODIFIER_COMMAND, "com.apple.Safari", "Safari", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_CONSUME_ONLY);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_J,
                                     HOTKEY_MODIFIER_CONTROL, NULL, "Rex Beta", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_PASS_THROUGH);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_J,
                                     HOTKEY_MODIFIER_CONTROL, NULL, "Mail", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_CONSUME);
        TEST_CHECK(match.actions[0].data.focus_window.robust, true);
        declarative_config_destroy(model);
    }
})

TEST_FUNC(hotkey_matcher_distinguishes_sided_modifiers_and_accepts_fn,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"h\","
      "\"modifiers\":[\"left_cmd\",\"fn\"],\"cases\":[{\"disposition\":\"consume_only\"}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "sided.jsonc", &model, &error), true);
    if (model) {
        struct hotkey_match_result match;
        uint16_t left = HOTKEY_MODIFIER_COMMAND | HOTKEY_MODIFIER_LEFT_COMMAND | HOTKEY_MODIFIER_FN;
        uint16_t right = HOTKEY_MODIFIER_COMMAND | HOTKEY_MODIFIER_RIGHT_COMMAND | HOTKEY_MODIFIER_FN;
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT,
                                     kVK_ANSI_H, left, NULL, NULL, &match), true);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT,
                                     kVK_ANSI_H, right, NULL, NULL, &match), false);
        declarative_config_destroy(model);
    }
})

TEST_FUNC(hotkey_matcher_prefers_explicit_fn_then_normalizes_navigation_fallback,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":["
      "{\"key\":\"right\",\"modifiers\":[\"ctrl\"],\"cases\":[{\"disposition\":\"consume_only\"}]},"
      "{\"key\":\"right\",\"modifiers\":[\"ctrl\",\"fn\"],\"cases\":[{\"disposition\":\"pass_through\"}]},"
      "{\"key\":\"h\",\"modifiers\":[\"ctrl\"],\"cases\":[{\"disposition\":\"consume_only\"}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "implicit-fn.jsonc", &model, &error), true);
    if (model) {
        struct hotkey_match_result match;
        uint16_t ctrl_fn = HOTKEY_MODIFIER_CONTROL | HOTKEY_MODIFIER_FN;
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT,
                                     kVK_RightArrow, ctrl_fn, NULL, NULL, &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_PASS_THROUGH);

        // Removing the explicit Fn binding exposes the ordinary navigation
        // binding through the implicit-Fn fallback.
        model->keyboard.binding_count = 1;
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT,
                                     kVK_RightArrow, ctrl_fn, NULL, NULL, &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_CONSUME_ONLY);

        // Fn remains an exact modifier for non-navigation keys.
        model->keyboard.binding_count = 3;
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT,
                                     kVK_ANSI_H, ctrl_fn, NULL, NULL, &match), false);
        declarative_config_destroy(model);
    }
})

TEST_FUNC(hotkey_callback_transitions_modes_consumes_unbound_and_ignores_synthetic_events,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"r\",\"modifiers\":[\"alt\",\"ctrl\"],"
      "\"cases\":[{\"disposition\":\"consume\",\"transition\":\"resize\"}]}],"
      "\"modes\":[{\"name\":\"resize\",\"capture_unbound\":true,\"bindings\":["
      "{\"key\":\"escape\",\"cases\":[{\"disposition\":\"consume\",\"transition\":\"default\"}]}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "modes.jsonc", &model, &error), true);
    if (model) {
        hotkey_manager_install_config(&model->keyboard);
        CGEventRef repeated_enter = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, true);
        CGEventSetFlags(repeated_enter, kCGEventFlagMaskAlternate | kCGEventFlagMaskControl);
        CGEventSetIntegerValueField(repeated_enter, kCGKeyboardEventAutorepeat, 1);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, repeated_enter, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), HOTKEY_TRANSITION_DEFAULT);
        CFRelease(repeated_enter);

        CGEventRef enter = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, true);
        CGEventSetFlags(enter, kCGEventFlagMaskAlternate | kCGEventFlagMaskControl);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, enter, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        CFRelease(enter);

        hotkey_manager_install_config(&model->keyboard);
        TEST_CHECK(hotkey_test_active_mode(), HOTKEY_TRANSITION_DEFAULT);
        enter = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, true);
        CGEventSetFlags(enter, kCGEventFlagMaskAlternate | kCGEventFlagMaskControl);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, enter, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        CFRelease(enter);

        CGEventRef unbound = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_Z, true);
        CGEventSetFlags(unbound, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, unbound, NULL) == NULL, true);
        CFRelease(unbound);

        CGEventRef synthetic = CGEventCreateKeyboardEvent(NULL, kVK_Escape, true);
        CGEventSetFlags(synthetic, 0);
        CGEventSetIntegerValueField(synthetic, kCGEventSourceUserData, (int64_t)HOTKEY_SYNTHETIC_EVENT_TAG);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, synthetic, NULL) == synthetic, true);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        CFRelease(synthetic);

        CGEventRef leave = CGEventCreateKeyboardEvent(NULL, kVK_Escape, true);
        CGEventSetFlags(leave, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, leave, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), HOTKEY_TRANSITION_DEFAULT);
        CFRelease(leave);
        hotkey_manager_install_config(NULL);
        declarative_config_destroy(model);
    }
})

TEST_FUNC(hotkey_callback_preserves_key_delivery_across_mode_changes,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":["
      "{\"key\":\"a\",\"cases\":[{\"disposition\":\"pass_through\"}]},"
      "{\"key\":\"r\",\"cases\":[{\"disposition\":\"consume\",\"transition\":\"plain\"}]},"
      "{\"key\":\"c\",\"cases\":[{\"disposition\":\"consume\",\"transition\":\"capture\"}]}],"
      "\"modes\":["
      "{\"name\":\"plain\",\"bindings\":[{\"key\":\"escape\",\"cases\":[{\"disposition\":\"consume\",\"transition\":\"default\"}]}]},"
      "{\"name\":\"capture\",\"capture_unbound\":true,\"bindings\":[]} ]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "delivery.jsonc", &model, &error), true);
    if (model) {
        hotkey_manager_install_config(&model->keyboard);

        CGEventRef enter_down = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, true);
        CGEventRef enter_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, false);
        CGEventSetFlags(enter_down, 0);
        CGEventSetFlags(enter_up, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, enter_down, NULL) == NULL, true);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, enter_up, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        CFRelease(enter_down);
        CFRelease(enter_up);

        CGEventRef leave_down = CGEventCreateKeyboardEvent(NULL, kVK_Escape, true);
        CGEventRef leave_up = CGEventCreateKeyboardEvent(NULL, kVK_Escape, false);
        CGEventSetFlags(leave_down, 0);
        CGEventSetFlags(leave_up, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, leave_down, NULL) == NULL, true);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, leave_up, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), HOTKEY_TRANSITION_DEFAULT);
        CFRelease(leave_down);
        CFRelease(leave_up);

        CGEventRef pass_down = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_A, true);
        CGEventRef pass_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_A, false);
        CGEventRef capture_down = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_C, true);
        CGEventRef capture_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_C, false);
        CGEventSetFlags(pass_down, 0);
        CGEventSetFlags(pass_up, 0);
        CGEventSetFlags(capture_down, 0);
        CGEventSetFlags(capture_up, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, pass_down, NULL) == pass_down, true);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, capture_down, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), 1);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, pass_up, NULL) == pass_up, true);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, capture_up, NULL) == NULL, true);
        CFRelease(pass_down);
        CFRelease(pass_up);
        CFRelease(capture_down);
        CFRelease(capture_up);

        hotkey_manager_install_config(NULL);
        declarative_config_destroy(model);
    }
})

TEST_FUNC(hotkey_input_mode_timeout_returns_to_default,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"r\",\"cases\":[{"
      "\"disposition\":\"consume\",\"transition\":\"resize\"}]}],"
      "\"modes\":[{\"name\":\"resize\",\"capture_unbound\":true,\"timeout_ms\":40,\"bindings\":[]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "timeout.jsonc", &model, &error), true);
    if (model) {
        hotkey_manager_install_config(&model->keyboard);
        CGEventRef enter = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, true);
        CGEventSetFlags(enter, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, enter, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        CFRelease(enter);
        CGEventRef enter_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, false);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, enter_up, NULL) == NULL, true);
        CFRelease(enter_up);
        usleep(25000);
        CGEventRef activity = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_Z, true);
        CGEventSetFlags(activity, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, activity, NULL) == NULL, true);
        CFRelease(activity);
        usleep(25000);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        usleep(30000);
        TEST_CHECK(hotkey_test_active_mode(), HOTKEY_TRANSITION_DEFAULT);
        CGEventRef activity_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_Z, false);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, activity_up, NULL) == NULL, true);
        CFRelease(activity_up);
        hotkey_manager_install_config(NULL);
        declarative_config_destroy(model);
    }
})

static int hotkey_rejected_post_count;

static bool hotkey_test_reject_and_consume_action(void *context)
{
    ++hotkey_rejected_post_count;
    hotkey_action_job_destroy(context);
    return false;
}

TEST_FUNC(hotkey_callback_does_not_reclaim_a_rejected_consumed_job,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"j\",\"cases\":[{"
      "\"disposition\":\"consume\",\"action\":{\"type\":\"focus_window\",\"direction\":\"south\"}}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "post.jsonc", &model, &error), true);
    if (model) {
        hotkey_manager_install_config(&model->keyboard);
        hotkey_rejected_post_count = 0;
        hotkey_test_set_action_poster(hotkey_test_reject_and_consume_action);
        CGEventRef key = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_J, true);
        CGEventSetFlags(key, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, key, NULL) == NULL, true);
        TEST_CHECK(hotkey_rejected_post_count, 1);
        CFRelease(key);
        hotkey_test_set_action_poster(NULL);
        hotkey_manager_install_config(NULL);
        declarative_config_destroy(model);
    }
})

struct hotkey_lifecycle_serialization_test
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    const struct hotkey_config_model *config;
    bool first_reconcile_entered;
    bool release_first_reconcile;
    bool shutdown_state_published;
    bool observed[2];
    int observed_count;
};

static struct hotkey_lifecycle_serialization_test *hotkey_lifecycle_serialization_active;

static void hotkey_lifecycle_serialization_locked_hook(void)
{
    struct hotkey_lifecycle_serialization_test *test = hotkey_lifecycle_serialization_active;
    if (!test) return;
    pthread_mutex_lock(&test->mutex);
    if (!test->first_reconcile_entered) {
        test->first_reconcile_entered = true;
        pthread_cond_broadcast(&test->cond);
        while (!test->release_first_reconcile) pthread_cond_wait(&test->cond, &test->mutex);
    }
    pthread_mutex_unlock(&test->mutex);
}

static void hotkey_lifecycle_serialization_observer(bool enabled)
{
    struct hotkey_lifecycle_serialization_test *test = hotkey_lifecycle_serialization_active;
    if (test && test->observed_count < (int)array_count(test->observed)) {
        test->observed[test->observed_count++] = enabled;
    }
}

static void *hotkey_lifecycle_install_thread(void *context)
{
    struct hotkey_lifecycle_serialization_test *test = context;
    hotkey_manager_install_config(test->config);
    return NULL;
}

static void *hotkey_lifecycle_shutdown_thread(void *context)
{
    struct hotkey_lifecycle_serialization_test *test = context;
    pthread_mutex_lock(&hotkey_manager.lock);
    hotkey_manager.lifecycle_started = false;
    pthread_mutex_unlock(&hotkey_manager.lock);

    pthread_mutex_lock(&test->mutex);
    test->shutdown_state_published = true;
    pthread_cond_broadcast(&test->cond);
    pthread_mutex_unlock(&test->mutex);

    (void)hotkey_manager_reconcile_event_tap();
    return NULL;
}

TEST_FUNC(hotkey_lifecycle_reconcile_serializes_state_snapshot_with_resource_action,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"a\",\"cases\":[{\"disposition\":\"consume_only\"}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json),
                                               "lifecycle.jsonc", &model, &error), true);
    if (model) {
        struct hotkey_lifecycle_serialization_test test = {
            .mutex = PTHREAD_MUTEX_INITIALIZER,
            .cond = PTHREAD_COND_INITIALIZER,
            .config = &model->keyboard
        };
        pthread_mutex_lock(&hotkey_manager.lock);
        hotkey_manager.lifecycle_started = true;
        hotkey_manager.config = NULL;
        pthread_mutex_unlock(&hotkey_manager.lock);
        hotkey_lifecycle_serialization_active = &test;
        hotkey_test_reconcile_locked_hook = hotkey_lifecycle_serialization_locked_hook;
        hotkey_test_reconcile_observer = hotkey_lifecycle_serialization_observer;

        pthread_t install_thread;
        TEST_CHECK(pthread_create(&install_thread, NULL, hotkey_lifecycle_install_thread, &test), 0);
        pthread_mutex_lock(&test.mutex);
        while (!test.first_reconcile_entered) pthread_cond_wait(&test.cond, &test.mutex);
        pthread_mutex_unlock(&test.mutex);

        pthread_t shutdown_thread;
        TEST_CHECK(pthread_create(&shutdown_thread, NULL, hotkey_lifecycle_shutdown_thread, &test), 0);
        pthread_mutex_lock(&test.mutex);
        while (!test.shutdown_state_published) pthread_cond_wait(&test.cond, &test.mutex);
        test.release_first_reconcile = true;
        pthread_cond_broadcast(&test.cond);
        pthread_mutex_unlock(&test.mutex);

        pthread_join(install_thread, NULL);
        pthread_join(shutdown_thread, NULL);
        TEST_CHECK(test.observed_count, 2);
        TEST_CHECK(test.observed[0], true);
        TEST_CHECK(test.observed[1], false);

        hotkey_test_reconcile_locked_hook = NULL;
        hotkey_test_reconcile_observer = NULL;
        hotkey_lifecycle_serialization_active = NULL;
        pthread_mutex_lock(&hotkey_manager.lock);
        hotkey_manager.config = NULL;
        hotkey_manager.lifecycle_started = false;
        pthread_mutex_unlock(&hotkey_manager.lock);
        pthread_cond_destroy(&test.cond);
        pthread_mutex_destroy(&test.mutex);
        declarative_config_destroy(model);
    }
})

TEST_FUNC(hotkey_yabai_command_executes_in_process_and_propagates_failure,
{
    struct hotkey_action action = {.type = HOTKEY_ACTION_YABAI_COMMAND};
    static const char success[] = "query\0--keyboard-mode\0";
    memcpy(action.data.yabai_command.bytes, success, sizeof(success));
    action.data.yabai_command.length = sizeof(success);
    TEST_CHECK(hotkey_execute_yabai_command(&action), true);

    static const char failure[] = "unknown\0";
    memset(action.data.yabai_command.bytes, 0, sizeof(action.data.yabai_command.bytes));
    memcpy(action.data.yabai_command.bytes, failure, sizeof(failure));
    action.data.yabai_command.length = sizeof(failure);
    TEST_CHECK(hotkey_execute_yabai_command(&action), false);
})

struct hotkey_sequence_test_context { int count; enum hotkey_action_type types[4]; int fail_at; };
static bool hotkey_sequence_test_execute(const struct hotkey_action *action, void *context)
{
    struct hotkey_sequence_test_context *test = context;
    test->types[test->count++] = action->type;
    return test->count != test->fail_at;
}

TEST_FUNC(hotkey_action_sequences_preserve_order_and_stop_after_failure,
{
    struct hotkey_action actions[] = {
        {.type = HOTKEY_ACTION_MOVE_WINDOW_TO_SPACE},
        {.type = HOTKEY_ACTION_FOCUS_SPACE},
        {.type = HOTKEY_ACTION_BALANCE_SPACE}
    };
    struct hotkey_sequence_test_context context = {.fail_at = 2};
    hotkey_test_set_action_executor(hotkey_sequence_test_execute, &context);
    TEST_CHECK(hotkey_action_execute_sequence(actions, 3), false);
    TEST_CHECK(context.count, 2);
    TEST_CHECK(context.types[0], HOTKEY_ACTION_MOVE_WINDOW_TO_SPACE);
    TEST_CHECK(context.types[1], HOTKEY_ACTION_FOCUS_SPACE);
    context = (struct hotkey_sequence_test_context){0};
    hotkey_test_set_action_executor(hotkey_sequence_test_execute, &context);
    TEST_CHECK(hotkey_action_execute_sequence(actions, 3), true);
    TEST_CHECK(context.count, 3);
    hotkey_test_set_action_executor(NULL, NULL);
})
