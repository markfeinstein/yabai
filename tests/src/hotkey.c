TEST_FUNC(hotkey_matcher_selects_application_and_window_conditions_before_default,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":["
      "{\"key\":\"h\",\"modifiers\":[\"cmd\"],\"cases\":["
        "{\"application\":{\"bundle_id\":\"com.example.rex\",\"name\":\"Rex Beta\"},"
         "\"window\":{\"title\":\"Main\"},\"disposition\":\"remap\","
         "\"action\":{\"type\":\"synthesize_key\",\"key\":\"h\"}},"
        "{\"disposition\":\"consume_only\"}]},"
      "{\"key\":\"j\",\"modifiers\":[\"ctrl\"],\"cases\":["
        "{\"application\":{\"name\":\"Rex Beta\"},\"window\":{\"title_prefix\":\"Settings - \"},"
         "\"disposition\":\"consume\",\"action\":{\"type\":\"focus_window\",\"direction\":\"south\",\"robust\":true}},"
        "{\"application\":{\"name\":\"Rex Beta\"},\"disposition\":\"pass_through\"},"
        "{\"disposition\":\"consume\",\"action\":{\"type\":\"focus_window\",\"direction\":\"south\",\"robust\":true}}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "match.jsonc", &model, &error), true);
    if (model) {
        struct hotkey_match_result match;
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_H,
                                     HOTKEY_MODIFIER_COMMAND, "com.example.rex", "Rex Beta", "Main", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_REMAP);
        TEST_CHECK(match.repeat, true);
        TEST_CHECK(match.action_count, 1U);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_H,
                                     HOTKEY_MODIFIER_COMMAND, "com.example.rex", "Rex Beta", "Other", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_CONSUME_ONLY);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_H,
                                     HOTKEY_MODIFIER_COMMAND, "com.apple.Safari", "Safari", NULL, &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_CONSUME_ONLY);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_J,
                                     HOTKEY_MODIFIER_CONTROL, NULL, "Rex Beta", "Settings - Server", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_CONSUME);
        TEST_CHECK(match.actions[0].data.focus_window.robust, true);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_J,
                                     HOTKEY_MODIFIER_CONTROL, NULL, "Rex Beta", "yabai - calm-reef", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_PASS_THROUGH);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT, kVK_ANSI_J,
                                     HOTKEY_MODIFIER_CONTROL, NULL, "Mail", "Inbox", &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_CONSUME);
        declarative_config_destroy(model);
    }
})

TEST_FUNC(hotkey_callback_matches_cached_focused_window_title,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"h\",\"modifiers\":[\"ctrl\"],\"cases\":["
      "{\"window\":{\"title_prefix\":\"Settings - \"},\"disposition\":\"consume_only\"},"
      "{\"disposition\":\"pass_through\"}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "window-title.jsonc", &model, &error), true);
    if (model) {
        hotkey_manager_install_config(&model->keyboard);
        hotkey_manager_update_focused_window_title("Settings - Server");
        CGEventRef settings = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_H, true);
        CGEventRef settings_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_H, false);
        CGEventSetFlags(settings, kCGEventFlagMaskControl);
        CGEventSetFlags(settings_up, kCGEventFlagMaskControl);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, settings, NULL) == NULL, true);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, settings_up, NULL) == NULL, true);
        CFRelease(settings);
        CFRelease(settings_up);

        hotkey_manager_update_focused_window_title("yabai - calm-reef");
        CGEventRef terminal = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_H, true);
        CGEventRef terminal_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_H, false);
        CGEventSetFlags(terminal, kCGEventFlagMaskControl);
        CGEventSetFlags(terminal_up, kCGEventFlagMaskControl);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, terminal, NULL) == terminal, true);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, terminal_up, NULL) == terminal_up, true);
        CFRelease(terminal);
        CFRelease(terminal_up);
        hotkey_manager_install_config(NULL);
        hotkey_manager_update_focused_window_title(NULL);
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
                                     kVK_ANSI_H, left, NULL, NULL, NULL, &match), true);
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT,
                                     kVK_ANSI_H, right, NULL, NULL, NULL, &match), false);
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
                                     kVK_RightArrow, ctrl_fn, NULL, NULL, NULL, &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_PASS_THROUGH);

        // Removing the explicit Fn binding exposes the ordinary navigation
        // binding through the implicit-Fn fallback.
        model->keyboard.binding_count = 1;
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT,
                                     kVK_RightArrow, ctrl_fn, NULL, NULL, NULL, &match), true);
        TEST_CHECK(match.disposition, HOTKEY_DISPOSITION_CONSUME_ONLY);

        // Fn remains an exact modifier for non-navigation keys.
        model->keyboard.binding_count = 3;
        TEST_CHECK(hotkey_test_match(&model->keyboard, HOTKEY_TRANSITION_DEFAULT,
                                     kVK_ANSI_H, ctrl_fn, NULL, NULL, NULL, &match), false);
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

static int hotkey_release_test_modifier_queries;

static bool hotkey_release_test_key_state(CGKeyCode keycode)
{
    (void) keycode;
    ++hotkey_release_test_modifier_queries;
    return false;
}

TEST_FUNC(hotkey_callback_reuses_tracked_key_delivery_without_modifier_queries,
{
    TEST_SAVE(hotkey_key_state);
    TEST_SAVE(hotkey_manager.delivery);
    hotkey_key_state = hotkey_release_test_key_state;
    struct hotkey_binding_case cases[] = {
        { .disposition = HOTKEY_DISPOSITION_CONSUME, .transition_mode = HOTKEY_TRANSITION_NONE },
        { .disposition = HOTKEY_DISPOSITION_PASS_THROUGH, .transition_mode = HOTKEY_TRANSITION_NONE }
    };
    struct hotkey_binding bindings[] = {
        { .keystroke = { kVK_ANSI_A, HOTKEY_MODIFIER_COMMAND }, .cases = &cases[0], .case_count = 1 },
        { .keystroke = { kVK_ANSI_S, HOTKEY_MODIFIER_COMMAND }, .cases = &cases[1], .case_count = 1 }
    };
    struct hotkey_config_model config = {
        .present = true, .enabled = true, .bindings = bindings, .binding_count = array_count(bindings)
    };

    for (size_t i = 0; i < array_count(bindings); ++i) {
        uint16_t keycode = bindings[i].keystroke.keycode;
        hotkey_manager.delivery[keycode] = HOTKEY_DELIVERY_UNTRACKED;
        hotkey_manager_install_config(&config);
        CGEventRef down = CGEventCreateKeyboardEvent(NULL, keycode, true);
        CGEventRef up = CGEventCreateKeyboardEvent(NULL, keycode, false);
        TEST_CHECK(down != NULL && up != NULL, true);
        if (!down || !up) {
            if (down) CFRelease(down);
            if (up) CFRelease(up);
            continue;
        }
        CGEventSetFlags(down, kCGEventFlagMaskCommand);
        CGEventSetFlags(up, kCGEventFlagMaskControl);
        CGEventRef expected_down = i == 0 ? NULL : down;
        CGEventRef expected_up = i == 0 ? NULL : up;
        hotkey_release_test_modifier_queries = 0;
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, down, NULL), expected_down);
        TEST_CHECK(hotkey_release_test_modifier_queries, 2);

        // Config and modifier changes cannot change an already tracked release.
        hotkey_manager_install_config(NULL);
        hotkey_release_test_modifier_queries = 0;
        CGEventRef synthetic_up = CGEventCreateKeyboardEvent(NULL, keycode, false);
        TEST_CHECK(synthetic_up != NULL, true);
        if (synthetic_up) {
            CGEventSetIntegerValueField(synthetic_up, kCGEventSourceUserData, (int64_t) HOTKEY_SYNTHETIC_EVENT_TAG);
            TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, synthetic_up, NULL), synthetic_up);
            TEST_CHECK(hotkey_manager.delivery[keycode] != HOTKEY_DELIVERY_UNTRACKED, true);
            CFRelease(synthetic_up);
        }
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, up, NULL), expected_up);
        TEST_CHECK(hotkey_release_test_modifier_queries, 0);
        TEST_CHECK(hotkey_manager.delivery[keycode], HOTKEY_DELIVERY_UNTRACKED);

        // An orphan release still uses the current binding, not the old decision.
        hotkey_release_test_modifier_queries = 0;
        hotkey_manager_install_config(&config);
        CGEventSetFlags(up, kCGEventFlagMaskCommand);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, up, NULL), expected_up);
        TEST_CHECK(hotkey_release_test_modifier_queries, 2);
        TEST_CHECK(hotkey_manager.delivery[keycode], HOTKEY_DELIVERY_UNTRACKED);
        CFRelease(down);
        CFRelease(up);
    }
    hotkey_manager_install_config(NULL);
});

static uint64_t hotkey_timeout_test_generations[4];
static uint32_t hotkey_timeout_test_durations[4];
static int hotkey_timeout_test_count;

static void hotkey_timeout_test_schedule(uint64_t generation, uint32_t timeout_ms)
{
    if (hotkey_timeout_test_count < (int)array_count(hotkey_timeout_test_generations)) {
        hotkey_timeout_test_generations[hotkey_timeout_test_count] = generation;
        hotkey_timeout_test_durations[hotkey_timeout_test_count] = timeout_ms;
        ++hotkey_timeout_test_count;
    }
}

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
        memset(hotkey_timeout_test_generations, 0, sizeof(hotkey_timeout_test_generations));
        memset(hotkey_timeout_test_durations, 0, sizeof(hotkey_timeout_test_durations));
        hotkey_timeout_test_count = 0;
        hotkey_test_mode_timeout_scheduler = hotkey_timeout_test_schedule;
        hotkey_manager_install_config(&model->keyboard);
        CGEventRef enter = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, true);
        CGEventSetFlags(enter, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, enter, NULL) == NULL, true);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        CFRelease(enter);
        CGEventRef enter_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_R, false);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, enter_up, NULL) == NULL, true);
        CFRelease(enter_up);
        TEST_CHECK(hotkey_timeout_test_count, 1);
        TEST_CHECK(hotkey_timeout_test_durations[0], 40U);
        CGEventRef activity = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_Z, true);
        CGEventSetFlags(activity, 0);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyDown, activity, NULL) == NULL, true);
        CFRelease(activity);
        TEST_CHECK(hotkey_timeout_test_count, 2);
        TEST_CHECK(hotkey_timeout_test_durations[1], 40U);
        TEST_CHECK(hotkey_timeout_test_generations[1] > hotkey_timeout_test_generations[0], true);
        hotkey_manager_fire_mode_timeout(hotkey_timeout_test_generations[0]);
        TEST_CHECK(hotkey_test_active_mode(), 0);
        hotkey_manager_fire_mode_timeout(hotkey_timeout_test_generations[1]);
        TEST_CHECK(hotkey_test_active_mode(), HOTKEY_TRANSITION_DEFAULT);
        CGEventRef activity_up = CGEventCreateKeyboardEvent(NULL, kVK_ANSI_Z, false);
        TEST_CHECK(hotkey_event_callback(NULL, kCGEventKeyUp, activity_up, NULL) == NULL, true);
        CFRelease(activity_up);
        hotkey_test_mode_timeout_scheduler = NULL;
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

TEST_FUNC(hotkey_occupied_space_selection_requires_a_real_target_and_wraps,
{
    uint64_t target = 0;
    struct hotkey_occupied_space occupied[] = {
        { .sid = 11, .index = 1 },
        { .sid = 33, .index = 3 },
        { .sid = 55, .index = 5 }
    };

    TEST_CHECK(hotkey_select_occupied_space(NULL, 0, 2,
                                            HOTKEY_OCCUPIED_SPACE_NEXT, &target), false);
    TEST_CHECK(hotkey_select_occupied_space(occupied, 3, 2,
                                            HOTKEY_OCCUPIED_SPACE_NEXT, &target), true);
    TEST_CHECK(target, 33ULL);
    TEST_CHECK(hotkey_select_occupied_space(occupied, 3, 4,
                                            HOTKEY_OCCUPIED_SPACE_PREVIOUS, &target), true);
    TEST_CHECK(target, 33ULL);
    TEST_CHECK(hotkey_select_occupied_space(occupied, 3, 5,
                                            HOTKEY_OCCUPIED_SPACE_NEXT, &target), true);
    TEST_CHECK(target, 11ULL);
    TEST_CHECK(hotkey_select_occupied_space(occupied, 3, 1,
                                            HOTKEY_OCCUPIED_SPACE_PREVIOUS, &target), true);
    TEST_CHECK(target, 55ULL);

    struct hotkey_occupied_space current_only[] = {{ .sid = 22, .index = 2 }};
    TEST_CHECK(hotkey_select_occupied_space(current_only, 1, 2,
                                            HOTKEY_OCCUPIED_SPACE_NEXT, &target), true);
    TEST_CHECK(target, 22ULL);
});

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
