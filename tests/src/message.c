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

TEST_FUNC(message_direct_float_components_reject_non_finite,
{
    float x, y, ratio;
    char type[MAXLEN];
    TEST_CHECK(sscanf("abs:-1.5:2.5", ARGUMENT_WINDOW_MOVE, type, &x, &y) == 3 && floats_are_finite(x, y), true);
    TEST_CHECK(sscanf("abs:nan:2.5", ARGUMENT_WINDOW_MOVE, type, &x, &y) == 3 && floats_are_finite(x, y), false);
    TEST_CHECK(sscanf("abs:1.5:inf", ARGUMENT_WINDOW_RESIZE, type, &x, &y) == 3 && floats_are_finite(x, y), false);
    TEST_CHECK(sscanf("rel:0.25", ARGUMENT_WINDOW_RATIO, type, &ratio) == 2 && __builtin_isfinite(ratio), true);
    TEST_CHECK(sscanf("rel:nan", ARGUMENT_WINDOW_RATIO, type, &ratio) == 2 && __builtin_isfinite(ratio), false);
});
