static struct token message_test_token(char *text)
{
    return (struct token) { .text = text, .length = (int) strlen(text) };
}

static bool message_test_parse_rule_grid(char *grid)
{
    char buffer[256];
    int app_length = snprintf(buffer, sizeof(buffer), "app=.*");
    if (app_length <= 0 || app_length + 1 >= (int) sizeof(buffer)) return false;

    int grid_length = snprintf(buffer + app_length + 1, sizeof(buffer) - (size_t) app_length - 1, "grid=%s", grid);
    if (grid_length <= 0 || app_length + grid_length + 2 >= (int) sizeof(buffer)) return false;
    buffer[app_length + grid_length + 2] = '\0';

    char *message = buffer + app_length + 1;
    struct rule rule = {0};
    bool result = parse_rule(NULL, &message, &rule, message_test_token(buffer));
    rule_destroy(&rule);
    return result;
}

TEST_FUNC(message_utf8_ingress_validation,
{
    char malformed_label_bytes[] = { (char) 0xc0, (char) 0xaf, 0 };
    struct token malformed_label_token = {
        .text = malformed_label_bytes,
        .length = (int) sizeof(malformed_label_bytes) - 1
    };
    char *label = NULL;
    FILE *label_response = tmpfile();
    TEST_CHECK(label_response != NULL, true);
    if (label_response) {
        TEST_CHECK(parse_label(label_response, malformed_label_token, LABEL_SPACE, &label), false);
        TEST_CHECK(label == NULL, true);
        rewind(label_response);
        char response[512] = {0};
        size_t response_length = fread(response, 1, sizeof(response) - 1, label_response);
        fclose(label_response);
        TEST_CHECK(strstr(response, "label is not valid UTF-8") != NULL, true);
        TEST_CHECK(memchr(response, 0xc0, response_length) == NULL, true);
        TEST_CHECK(memchr(response, 0xaf, response_length) == NULL, true);
    }

    char valid_label_bytes[] = { (char) 0xe6, (char) 0xb5, (char) 0x8b,
                                 (char) 0xe8, (char) 0xaf, (char) 0x95, 0 };
    TEST_CHECK(parse_label(NULL, message_test_token(valid_label_bytes), LABEL_SPACE, &label), true);
    TEST_CHECK(label != NULL, true);
    if (label) {
        TEST_CHECK(strcmp(label, valid_label_bytes), 0);
        free(label);
        label = NULL;
    }

    char malformed_rule[] = { 'a', 'p', 'p', '=', 'x', (char) 0xc0, (char) 0xaf, 0, 0 };
    char *rule_message = malformed_rule + sizeof(malformed_rule) - 2;
    struct rule rule = {0};
    FILE *rule_response = tmpfile();
    TEST_CHECK(rule_response != NULL, true);
    if (rule_response) {
        TEST_CHECK(parse_rule(rule_response, &rule_message, &rule, message_test_token(malformed_rule)), false);
        rewind(rule_response);
        char response[512] = {0};
        size_t response_length = fread(response, 1, sizeof(response) - 1, rule_response);
        fclose(rule_response);
        TEST_CHECK(strstr(response, "value is not valid UTF-8") != NULL, true);
        TEST_CHECK(memchr(response, 0xc0, response_length) == NULL, true);
        TEST_CHECK(memchr(response, 0xaf, response_length) == NULL, true);
    }
    rule_destroy(&rule);

    char valid_rule[] = "label=\xe6\xb5\x8b\xe8\xaf\x95\0app=.*\0";
    rule_message = valid_rule + strlen(valid_rule) + 1;
    memset(&rule, 0, sizeof(rule));
    TEST_CHECK(parse_rule(NULL, &rule_message, &rule, message_test_token(valid_rule)), true);
    TEST_CHECK(rule.label != NULL, true);
    if (rule.label) TEST_CHECK(strcmp(rule.label, valid_label_bytes), 0);
    rule_destroy(&rule);

    struct signal *original_signal_buffer = g_signal_event[SIGNAL_APPLICATION_LAUNCHED];
    int original_signal_count = buf_len(original_signal_buffer);
    char malformed_signal[] = "--add\0event=application_launched\0action=x\xc0\xaf\0label=utf8-malformed-signal\0";
    FILE *signal_response = tmpfile();
    TEST_CHECK(signal_response != NULL, true);
    if (signal_response) {
        handle_domain_signal(signal_response, message_test_token("signal"), malformed_signal);
        rewind(signal_response);
        char response[512] = {0};
        size_t response_length = fread(response, 1, sizeof(response) - 1, signal_response);
        fclose(signal_response);
        TEST_CHECK(strstr(response, "value is not valid UTF-8") != NULL, true);
        TEST_CHECK(memchr(response, 0xc0, response_length) == NULL, true);
        TEST_CHECK(memchr(response, 0xaf, response_length) == NULL, true);
    }
    TEST_CHECK(buf_len(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]), original_signal_count);

    char valid_signal[] = "--add\0event=application_launched\0action=echo-\xe6\xb5\x8b\xe8\xaf\x95\0label=utf8-signal-\xe6\xb5\x8b\xe8\xaf\x95\0";
    char valid_signal_label[] = "utf8-signal-\xe6\xb5\x8b\xe8\xaf\x95";
    handle_domain_signal(NULL, message_test_token("signal"), valid_signal);
    TEST_CHECK(buf_len(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]), original_signal_count + 1);
    if (buf_len(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]) == original_signal_count + 1) {
        struct signal *signal = &g_signal_event[SIGNAL_APPLICATION_LAUNCHED][original_signal_count];
        TEST_CHECK(strcmp(signal->label, valid_signal_label), 0);
        TEST_CHECK(strcmp(signal->command, "echo-\xe6\xb5\x8b\xe8\xaf\x95"), 0);
    }
    TEST_CHECK(event_signal_remove(valid_signal_label), true);
    TEST_CHECK(buf_len(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]), original_signal_count);
    if (!original_signal_buffer) {
        buf_free(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]);
        g_signal_event[SIGNAL_APPLICATION_LAUNCHED] = NULL;
    }
});

TEST_FUNC(message_numeric_token_boundaries,
{
    struct token_value value = token_to_value(message_test_token("2147483647"));
    TEST_CHECK(value.type, TOKEN_TYPE_INT);
    TEST_CHECK(value.int_value, INT_MAX);

    value = token_to_value(message_test_token("2147483648"));
    TEST_CHECK(value.type, TOKEN_TYPE_UNKNOWN);

    value = token_to_value(message_test_token("-2.5"));
    TEST_CHECK(value.type, TOKEN_TYPE_FLOAT);
    TEST_CHECK(value.float_value == -2.5f, true);

    value = token_to_value(message_test_token("nan"));
    TEST_CHECK(value.type, TOKEN_TYPE_STRING);

    value = token_to_value(message_test_token("inf"));
    TEST_CHECK(value.type, TOKEN_TYPE_STRING);

    value = token_to_value(message_test_token("1e999"));
    TEST_CHECK(value.type, TOKEN_TYPE_STRING);
});

TEST_FUNC(message_hexadecimal_token_boundaries,
{
    uint32_t u32_value;
    TEST_CHECK(token_is_hexadecimal(message_test_token("0x"), &u32_value), false);
    TEST_CHECK(token_is_hexadecimal(message_test_token("0xG"), &u32_value), false);

    struct token_value value = token_to_value(message_test_token("0xFFFFFFFF"));
    TEST_CHECK(value.type, TOKEN_TYPE_U32);
    TEST_CHECK(value.u32_value, UINT32_MAX);

    value = token_to_value(message_test_token("0x100000000"));
    TEST_CHECK(value.type, TOKEN_TYPE_UNKNOWN);

    value = token_to_value(message_test_token("0x80FFAA00"));
    TEST_CHECK(value.type, TOKEN_TYPE_U32);
    TEST_CHECK(value.u32_value, 0x80FFAA00U);
});

TEST_FUNC(message_duration_values_must_be_finite_and_nonnegative,
{
    struct token_value value = token_to_value(message_test_token("0.25"));
    TEST_CHECK(token_value_is_nonnegative_float(value), true);

    value = token_to_value(message_test_token("-0.25"));
    TEST_CHECK(token_value_is_nonnegative_float(value), false);

    value = token_to_value(message_test_token("nan"));
    TEST_CHECK(token_value_is_nonnegative_float(value), false);

    TEST_CHECK(sa_opacity_duration_is_safe(2147483.5f), true);
    TEST_CHECK(sa_opacity_duration_is_safe(2147483.75f), false);
    TEST_CHECK(sa_opacity_duration_is_safe(-0.25f), false);
    TEST_CHECK(sa_opacity_duration_is_safe(INFINITY), false);
});

TEST_FUNC(message_window_grid_numeric_validation,
{
    unsigned r, c, x, y, w, h;
    TEST_CHECK(parse_window_grid_command("1:2:3:4:5:6", &r, &c, &x, &y, &w, &h), true);
    TEST_CHECK(r, 1U);
    TEST_CHECK(c, 2U);
    TEST_CHECK(x, 3U);
    TEST_CHECK(y, 4U);
    TEST_CHECK(w, 5U);
    TEST_CHECK(h, 6U);

    TEST_CHECK(parse_window_grid_command("0:2:3:4:5:6", &r, &c, &x, &y, &w, &h), false);
    TEST_CHECK(parse_window_grid_command("1:0:3:4:5:6", &r, &c, &x, &y, &w, &h), false);
    TEST_CHECK(parse_window_grid_command("-1:2:3:4:5:6", &r, &c, &x, &y, &w, &h), false);
    TEST_CHECK(parse_window_grid_command("1:2:3:4:5:6:7", &r, &c, &x, &y, &w, &h), false);
    TEST_CHECK(parse_window_grid_command("1:2::4:5:6", &r, &c, &x, &y, &w, &h), false);
    TEST_CHECK(parse_window_grid_command("4294967296:2:3:4:5:6", &r, &c, &x, &y, &w, &h), false);
});

TEST_FUNC(message_rule_grid_numeric_validation,
{
    TEST_CHECK(message_test_parse_rule_grid("1:2:3:4:5:6"), true);
    TEST_CHECK(message_test_parse_rule_grid("0:2:3:4:5:6"), false);
    TEST_CHECK(message_test_parse_rule_grid("1:0:3:4:5:6"), false);
    TEST_CHECK(message_test_parse_rule_grid("-1:2:3:4:5:6"), false);
    TEST_CHECK(message_test_parse_rule_grid("1:2:3:4:5:6:7"), false);
    TEST_CHECK(message_test_parse_rule_grid("1:2::4:5:6"), false);
    TEST_CHECK(message_test_parse_rule_grid("4294967296:2:3:4:5:6"), false);
});

TEST_FUNC(message_rule_opacity_numeric_validation,
{
    float opacity;
    TEST_CHECK(parse_rule_opacity("0", &opacity), true);
    TEST_CHECK(opacity == 0.0f, true);
    TEST_CHECK(parse_rule_opacity("0.5", &opacity), true);
    TEST_CHECK(opacity == 0.5f, true);
    TEST_CHECK(parse_rule_opacity("1", &opacity), true);
    TEST_CHECK(opacity == 1.0f, true);

    char *invalid[] = { "-0.1", "1.1", "nan", "inf", "1e-1000", "0.5junk", "0.5:1", "" };
    for (int i = 0; i < array_count(invalid); ++i) {
        TEST_CHECK(parse_rule_opacity(invalid[i], &opacity), false);
    }
});

TEST_FUNC(message_external_bar_parser_is_strict,
{
    enum external_bar_mode mode;
    int top, bottom;

    TEST_CHECK(parse_external_bar_command("main:2147483647:-2147483648", &mode, &top, &bottom), true);
    TEST_CHECK(mode, EXTERNAL_BAR_MAIN);
    TEST_CHECK(top, INT_MAX);
    TEST_CHECK(bottom, INT_MIN);
    TEST_CHECK(parse_external_bar_command("all:+12:-34", &mode, &top, &bottom), true);
    TEST_CHECK(mode, EXTERNAL_BAR_ALL);
    TEST_CHECK(parse_external_bar_command("off:0:0", &mode, &top, &bottom), true);
    TEST_CHECK(mode, EXTERNAL_BAR_OFF);

    char *invalid[] = {
        "unknown:1:2", "main:1", "main:1:2:3", "main:1:2junk",
        "main:2147483648:0", "main:-2147483649:0"
    };
    for (int i = 0; i < array_count(invalid); ++i) {
        TEST_CHECK(parse_external_bar_command(invalid[i], &mode, &top, &bottom), false);
    }
});

TEST_FUNC(message_space_padding_parser_is_strict,
{
    uint8_t type;
    int top, bottom, left, right;

    TEST_CHECK(parse_space_padding_command("abs:2147483647:-2147483648:+3:-4", &type, &top, &bottom, &left, &right), true);
    TEST_CHECK(type, TYPE_ABS);
    TEST_CHECK(top, INT_MAX);
    TEST_CHECK(bottom, INT_MIN);
    TEST_CHECK(left, 3);
    TEST_CHECK(right, -4);
    TEST_CHECK(parse_space_padding_command("rel:-1:2:-3:4", &type, &top, &bottom, &left, &right), true);
    TEST_CHECK(type, TYPE_REL);

    char *invalid[] = {
        "unknown:1:2:3:4", "abs:1:2:3", "abs:1:2:3:4:5", "abs:1:2:3:4junk",
        "abs:2147483648:2:3:4", "abs:-2147483649:2:3:4"
    };
    for (int i = 0; i < array_count(invalid); ++i) {
        TEST_CHECK(parse_space_padding_command(invalid[i], &type, &top, &bottom, &left, &right), false);
    }
});

TEST_FUNC(message_space_gap_parser_is_strict,
{
    uint8_t type;
    int gap;

    TEST_CHECK(parse_space_gap_command("abs:2147483647", &type, &gap), true);
    TEST_CHECK(type, TYPE_ABS);
    TEST_CHECK(gap, INT_MAX);
    TEST_CHECK(parse_space_gap_command("rel:-2147483648", &type, &gap), true);
    TEST_CHECK(type, TYPE_REL);
    TEST_CHECK(gap, INT_MIN);

    char *invalid[] = {
        "unknown:1", "abs", "abs:", "abs:1:2", "rel:1junk",
        "abs:2147483648", "rel:-2147483649"
    };
    for (int i = 0; i < array_count(invalid); ++i) {
        TEST_CHECK(parse_space_gap_command(invalid[i], &type, &gap), false);
    }
});

TEST_FUNC(message_window_float_commands_are_strict,
{
    struct {
        char *text;
        uint8_t type;
        float x;
        float y;
    } valid_move[] = {
        { "abs:-1.5:2.5", TYPE_ABS, -1.5f, 2.5f },
        { "rel:0.25:-0.75", TYPE_REL, 0.25f, -0.75f },
    };
    for (int i = 0; i < array_count(valid_move); ++i) {
        uint8_t type;
        float x, y;
        TEST_CHECK(parse_window_move_command(valid_move[i].text, &type, &x, &y), true);
        TEST_CHECK(type, valid_move[i].type);
        TEST_CHECK(x == valid_move[i].x, true);
        TEST_CHECK(y == valid_move[i].y, true);
    }

    struct {
        char *text;
        uint8_t handle;
    } valid_resize[] = {
        { "abs:-1.5:2.5", HANDLE_ABS },
        { "top_left:0.25:-0.75", HANDLE_TOP | HANDLE_LEFT },
    };
    for (int i = 0; i < array_count(valid_resize); ++i) {
        uint8_t handle;
        float w, h;
        TEST_CHECK(parse_window_resize_command(valid_resize[i].text, &handle, &w, &h), true);
        TEST_CHECK(handle, valid_resize[i].handle);
    }

    uint8_t type;
    float ratio;
    TEST_CHECK(parse_window_ratio_command("rel:-0.25", &type, &ratio), true);
    TEST_CHECK(type, TYPE_REL);
    TEST_CHECK(ratio == -0.25f, true);

    char *invalid_move[] = {
        "unknown:1:2", "abs:nan:2", "rel:1:inf", "abs:1e-1000:2",
        "abs:1", "abs:1:2:3", "abs:1:2junk"
    };
    for (int i = 0; i < array_count(invalid_move); ++i) {
        float x, y;
        TEST_CHECK(parse_window_move_command(invalid_move[i], &type, &x, &y), false);
    }

    char *invalid_resize[] = {
        "unknown:1:2", "top:nan:2", "abs:1:inf", "top:1e-1000:2",
        "left:1", "right:1:2:3", "bottom:1:2junk"
    };
    for (int i = 0; i < array_count(invalid_resize); ++i) {
        uint8_t handle;
        float w, h;
        TEST_CHECK(parse_window_resize_command(invalid_resize[i], &handle, &w, &h), false);
    }

    char *invalid_ratio[] = {
        "unknown:1", "abs:nan", "rel:inf", "abs:1e-1000",
        "abs:", "rel:1:2", "abs:0.5junk"
    };
    for (int i = 0; i < array_count(invalid_ratio); ++i) {
        TEST_CHECK(parse_window_ratio_command(invalid_ratio[i], &type, &ratio), false);
    }
});
