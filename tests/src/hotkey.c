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
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, unbound, NULL) == NULL, true);
        CFRelease(unbound);

        CGEventRef synthetic = CGEventCreateKeyboardEvent(NULL, kVK_Escape, true);
        CGEventSetIntegerValueField(synthetic, kCGEventSourceUserData, (int64_t)HOTKEY_SYNTHETIC_EVENT_TAG);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, synthetic, NULL) == synthetic, true);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        CFRelease(synthetic);

        CGEventRef leave = CGEventCreateKeyboardEvent(NULL, kVK_Escape, true);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, leave, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), HOTKEY_TRANSITION_DEFAULT);
        CFRelease(leave);
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
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, key, NULL) == NULL, true);
        TEST_CHECK(hotkey_rejected_post_count, 1);
        CFRelease(key);
        hotkey_test_set_action_poster(NULL);
        hotkey_manager_install_config(NULL);
        declarative_config_destroy(model);
    }
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
