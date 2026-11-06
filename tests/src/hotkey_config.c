TEST_FUNC(hotkey_config_parses_app_cases_modes_sequences_and_raw_keycodes,
{
    const char json[] =
        "{\"version\":3,\"keyboard\":{\"enabled\":true,"
        "\"bindings\":["
          "{\"key\":\"h\",\"modifiers\":[\"cmd\"],\"cases\":["
            "{\"application\":{\"name\":\"Rex Beta\"},\"disposition\":\"remap\","
             "\"action\":{\"type\":\"synthesize_key\",\"key\":\"h\",\"modifiers\":[\"ctrl\",\"alt\",\"cmd\"]}},"
            "{\"disposition\":\"consume_only\"}]},"
          "{\"keycode\":\"0x12\",\"modifiers\":[\"ctrl\"],\"repeat\":false,\"cases\":["
            "{\"disposition\":\"consume\",\"action\":["
              "{\"type\":\"move_window_to_space\",\"index\":1},"
              "{\"type\":\"focus_space\",\"index\":1}]}]},"
          "{\"key\":\"r\",\"modifiers\":[\"alt\",\"ctrl\"],\"cases\":["
            "{\"disposition\":\"consume\",\"transition\":\"resize\"}]}],"
        "\"modes\":[{\"name\":\"resize\",\"capture_unbound\":true,\"bindings\":["
          "{\"key\":\"escape\",\"cases\":[{\"disposition\":\"consume\",\"transition\":\"default\"}]},"
          "{\"key\":\"h\",\"cases\":[{\"disposition\":\"consume\","
            "\"action\":{\"type\":\"adaptive_resize\",\"direction\":\"west\"}}]}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "hotkeys.jsonc", &model, &error), true);
    TEST_CHECK(model != NULL, true);
    if (model) {
        TEST_CHECK(model->version, 3);
        TEST_CHECK(model->keyboard.present, true);
        TEST_CHECK(model->keyboard.enabled, true);
        TEST_CHECK(model->keyboard.binding_count, 3U);
        TEST_CHECK(model->keyboard.mode_count, 1U);
        TEST_CHECK(model->keyboard.bindings[0].keystroke.keycode, kVK_ANSI_H);
        TEST_CHECK(model->keyboard.bindings[0].cases[0].disposition, HOTKEY_DISPOSITION_REMAP);
        TEST_CHECK(model->keyboard.bindings[0].cases[0].actions[0].data.synthesize_key.modifiers,
                   HOTKEY_MODIFIER_CONTROL | HOTKEY_MODIFIER_OPTION | HOTKEY_MODIFIER_COMMAND);
        TEST_CHECK(model->keyboard.bindings[1].keystroke.keycode, 0x12);
        TEST_CHECK(model->keyboard.bindings[1].repeat, false);
        TEST_CHECK(model->keyboard.bindings[1].cases[0].action_count, 2U);
        TEST_CHECK(model->keyboard.bindings[1].cases[0].actions[0].type, HOTKEY_ACTION_MOVE_WINDOW_TO_SPACE);
        TEST_CHECK(model->keyboard.bindings[2].cases[0].transition_mode, 0);
        TEST_CHECK(model->keyboard.modes[0].capture_unbound, true);
        TEST_CHECK(model->keyboard.modes[0].bindings[0].cases[0].transition_mode, HOTKEY_TRANSITION_DEFAULT);
        declarative_config_destroy(model);
    }
})

TEST_FUNC(hotkey_config_rejects_versions_conflicts_conditions_actions_and_modes,
{
    struct path_case { const char *json; const char *path; } cases[] = {
        {"{\"version\":2,\"keyboard\":{}}", "$.keyboard"},
        {"{\"version\":3,\"keyboard\":{\"bindings\":["
         "{\"key\":\"h\",\"cases\":[{\"disposition\":\"consume_only\"}]},"
         "{\"keycode\":4,\"cases\":[{\"disposition\":\"consume_only\"}]}]}}",
         "$.keyboard.bindings[1]"},
        {"{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"h\",\"cases\":["
         "{\"disposition\":\"consume_only\"},{\"application\":{\"name\":\"Rex Beta\"},"
         "\"disposition\":\"pass_through\"}]}]}}",
         "$.keyboard.bindings[0].cases[0]"},
        {"{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"h\",\"cases\":["
         "{\"disposition\":\"consume\",\"action\":{\"type\":\"unknown\"}}]}]}}",
         "$.keyboard.bindings[0].cases[0].action.type"},
        {"{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"h\",\"cases\":["
         "{\"disposition\":\"consume\",\"transition\":\"missing\"}]}]}}",
         "$.keyboard.bindings[0].cases[0].transition"},
        {"{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"not-a-key\",\"cases\":["
         "{\"disposition\":\"consume_only\"}]}]}}",
         "$.keyboard.bindings[0].key"},
        {"{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"h\",\"cases\":["
         "{\"disposition\":\"remap\",\"action\":{\"type\":\"focus_window\",\"direction\":\"west\"}}]}]}}",
         "$.keyboard.bindings[0].cases[0]"}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        struct declarative_config_model *model = NULL;
        struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)cases[i].json, strlen(cases[i].json),
                                                  "invalid-hotkeys.jsonc", &model, &error), false);
        TEST_CHECK(model == NULL, true);
        TEST_CHECK(error.stage, DECLARATIVE_ERROR_SCHEMA);
        TEST_CHECK(strcmp(error.json_path, cases[i].path), 0);
    }
})

TEST_FUNC(hotkey_config_parses_all_native_action_primitives,
{
    const char json[] =
      "{\"version\":3,\"keyboard\":{\"bindings\":[{\"key\":\"f\",\"cases\":[{"
      "\"disposition\":\"consume\",\"action\":["
      "{\"type\":\"focus_window\",\"direction\":\"west\",\"robust\":true},"
      "{\"type\":\"warp_window\",\"direction\":\"east\"},"
      "{\"type\":\"resize_window\",\"handle\":\"bottom_right\",\"dx\":50,\"dy\":50},"
      "{\"type\":\"change_window_ratio\",\"delta\":0.05},"
      "{\"type\":\"toggle_floating\"},"
      "{\"type\":\"toggle_zoom_fullscreen\"},"
      "{\"type\":\"rotate_space\",\"degrees\":90},"
      "{\"type\":\"set_space_layout\",\"layout\":\"bsp\"},"
      "{\"type\":\"balance_space\"},"
      "{\"type\":\"focus_occupied_space\",\"direction\":\"next\"},"
      "{\"type\":\"close_window\"},"
      "{\"type\":\"close_other_windows\"},"
      "{\"type\":\"cycle_space_layout\"},"
      "{\"type\":\"reload_configuration\"},"
      "{\"type\":\"restart_service\",\"label\":\"org.nixos.yabai\"}]}]}]}}";
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "actions.jsonc", &model, &error), true);
    if (model) {
        TEST_CHECK(model->keyboard.bindings[0].cases[0].action_count, 15U);
        TEST_CHECK(model->keyboard.bindings[0].cases[0].actions[14].type, HOTKEY_ACTION_RESTART_SERVICE);
        TEST_CHECK(strcmp(model->keyboard.bindings[0].cases[0].actions[14].data.restart_service.label,
                          "org.nixos.yabai"), 0);
        declarative_config_destroy(model);
    }
})
