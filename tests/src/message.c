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

struct message_reload_test_context
{
    FILE *response;
};

static size_t message_space_lifecycle_create_calls;
static size_t message_space_lifecycle_destroy_calls;
static uint64_t message_space_lifecycle_active(void) { return 11; }
static enum space_op_error message_space_lifecycle_create(uint64_t sid)
{
    ++message_space_lifecycle_create_calls;
    return sid == 11 ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_MISSING_SRC;
}
static enum space_op_error message_space_lifecycle_destroy(uint64_t sid)
{
    ++message_space_lifecycle_destroy_calls;
    return sid == 11 ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_MISSING_SRC;
}

static bool message_test_reload_on_worker(void *context)
{
    struct message_reload_test_context *test = context;
    char exact[] = "reload\0";
    handle_domain_config(test->response, message_test_token("config"), exact);
    return true;
}

TEST_FUNC(declarative_reload_grammar_rejects_scoped_trailing_and_mixed_before_mutation,
{
    bool saved_verbose = g_verbose;
    int saved_window_gap = g_space_manager.window_gap;
    enum ffm_mode saved_ffm = g_window_manager.configured_ffm_mode;
    struct rule *saved_rules = g_window_manager.rules;
    struct bucket **saved_views = g_space_manager.view.buckets;
    struct bucket **saved_managed = g_window_manager.managed_window.buckets;
    struct declarative_runtime_cell saved_runtime =
        g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_GAP];
    struct menu_bar_settings saved_menu = menu_bar_manager_settings();
    int saved_mouse_modifier = g_mouse_state.modifier;
    uint64_t saved_generation = declarative_installed_generation();
    g_verbose = false;
    char scoped[] = "--space\0focused\0reload\0";
    char trailing[] = "reload\0trailing\0";
    char mixed_before[] = "debug_output\0on\0reload\0";
    char mixed_after[] = "reload\0debug_output\0on\0";
    char *invalid_messages[] = { scoped, trailing, mixed_before, mixed_after };

    for (size_t i = 0; i < array_count(invalid_messages); ++i) {
        FILE *response = tmpfile();
        TEST_CHECK(response != NULL, true);
        if (!response) continue;
        handle_domain_config(response, message_test_token("config"), invalid_messages[i]);
        rewind(response);
        char output[256] = {0};
        fread(output, 1, sizeof(output) - 1, response);
        fclose(response);
        TEST_CHECK(strstr(output, "must be unscoped") != NULL, true);
        TEST_CHECK(g_verbose, false);
        TEST_CHECK(g_space_manager.window_gap, saved_window_gap);
        TEST_CHECK(g_window_manager.configured_ffm_mode, saved_ffm);
        TEST_CHECK(g_window_manager.rules, saved_rules);
        TEST_CHECK(g_space_manager.view.buckets, saved_views);
        TEST_CHECK(g_window_manager.managed_window.buckets, saved_managed);
        TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_GAP].present,
                   saved_runtime.present);
        TEST_CHECK(g_declarative_runtime_state.global[DECLARATIVE_CONFIG_WINDOW_GAP].source,
                   saved_runtime.source);
        TEST_CHECK(menu_bar_manager_settings().enabled, saved_menu.enabled);
        TEST_CHECK(menu_bar_manager_settings().style, saved_menu.style);
        TEST_CHECK(g_mouse_state.modifier, saved_mouse_modifier);
        TEST_CHECK(declarative_installed_generation(), saved_generation);
    }

    struct event_loop saved_event_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    declarative_install_test_set_generation(UINT64_MAX);
    FILE *exact_response = tmpfile();
    TEST_CHECK(exact_response != NULL, true);
    if (exact_response) {
        struct message_reload_test_context exact = { .response = exact_response };
        TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, message_test_reload_on_worker, &exact), true);
        event_loop_end(&g_event_loop);
        rewind(exact_response);
        unsigned char marker = 0;
        TEST_CHECK(fread(&marker, 1, 1, exact_response), 1UL);
        TEST_CHECK(marker, (unsigned char) FAILURE_MESSAGE[0]);
        fclose(exact_response);
    }
    TEST_CHECK(declarative_installed_generation(), UINT64_MAX);
    declarative_install_test_set_generation(0);
    g_event_loop = saved_event_loop;
    g_verbose = saved_verbose;
});

TEST_FUNC(message_space_create_destroy_success_requests_one_reconcile,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct menu_bar_manager_state saved_menu_manager = g_menu_bar_manager;
    __auto_type saved_active = message_space_active;
    __auto_type saved_add = message_space_add;
    __auto_type saved_destroy = message_space_destroy;
    g_event_loop = (struct event_loop) {0};
    g_menu_bar_manager = (struct menu_bar_manager_state) {0};
    declarative_install_test_set_generation(1);
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 4), true);
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai_message_lifecycle_%d", getpid());
    g_event_loop.semaphore = sem_open(semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(semaphore_name);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    message_space_active = message_space_lifecycle_active;
    message_space_add = message_space_lifecycle_create;
    message_space_destroy = message_space_lifecycle_destroy;
    message_space_lifecycle_create_calls = 0;
    message_space_lifecycle_destroy_calls = 0;

    char create[] = "--create\0";
    char destroy[] = "--destroy\0";
    handle_domain_space(NULL, message_test_token("space"), create);
    handle_domain_space(NULL, message_test_token("space"), destroy);
    TEST_CHECK(message_space_lifecycle_create_calls, 1U);
    TEST_CHECK(message_space_lifecycle_destroy_calls, 1U);
    TEST_CHECK(declarative_reload_test_reconcile_reservation(), 1U);
    TEST_CHECK(g_event_loop.queue.count, 1U);

    struct event event = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    declarative_reload_test_clear_reconcile_reservation();
    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    sem_close(g_event_loop.semaphore);
    event_queue_destroy(&g_event_loop.queue);
    message_space_active = saved_active;
    message_space_add = saved_add;
    message_space_destroy = saved_destroy;
    declarative_install_test_set_generation(0);
    g_menu_bar_manager = saved_menu_manager;
    g_event_loop = saved_event_loop;
});

TEST_FUNC(message_numeric_token_boundaries,
{
    struct token_value value = token_to_value(message_test_token("2147483647"));
    TEST_CHECK(value.type, TOKEN_TYPE_INT);
    TEST_CHECK(value.int_value, INT_MAX);

    value = token_to_value(message_test_token("-2147483648"));
    TEST_CHECK(value.type, TOKEN_TYPE_INT);
    TEST_CHECK(value.int_value, INT_MIN);

    value = token_to_value(message_test_token("2147483648"));
    TEST_CHECK(value.type, TOKEN_TYPE_UNKNOWN);
    value = token_to_value(message_test_token("-2147483649"));
    TEST_CHECK(value.type, TOKEN_TYPE_UNKNOWN);

    value = token_to_value(message_test_token("-1"));
    TEST_CHECK(value.type, TOKEN_TYPE_INT);
    TEST_CHECK(value.int_value, -1);

    float real;
    value = token_to_value(message_test_token("1"));
    TEST_CHECK(token_value_to_float(value, &real), true);
    TEST_CHECK(real == 1.0f, true);

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

TEST_FUNC(event_signal_pool_drops_records_when_full_without_overwriting,
{
    enum signal_type type = SIGNAL_DOCK_DID_RESTART;
    struct memory_pool saved_storage = g_signal_storage;
    struct signal *saved_signals = g_signal_event[type];
    size_t record_size = sizeof(struct event_signal);
    uint64_t sentinel_value = 0xfeedfacecafebeefULL;
    uint8_t *storage = calloc(1, record_size + sizeof(sentinel_value));
    TEST_CHECK(storage != NULL, true);
    if (storage) {
        memcpy(storage + record_size, &sentinel_value, sizeof(sentinel_value));
        g_signal_storage = (struct memory_pool) { .memory = storage, .size = record_size, .used = 0 };
        g_signal_event[type] = NULL;
        buf_push(g_signal_event[type], ((struct signal) {0}));

        event_signal_push(type, NULL);
        TEST_CHECK(g_signal_storage.used, record_size);
        TEST_CHECK(((struct event_signal *) storage)->type, type);

        event_signal_push(type, NULL);
        TEST_CHECK(g_signal_storage.used, record_size);
        uint64_t sentinel_after = 0;
        memcpy(&sentinel_after, storage + record_size, sizeof(sentinel_after));
        TEST_CHECK(sentinel_after, sentinel_value);

        buf_free(g_signal_event[type]);
        g_signal_event[type] = saved_signals;
        g_signal_storage = saved_storage;
        free(storage);
    }
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
    float saved_duration = g_window_manager.window_opacity_duration;
    struct declarative_runtime_cell saved_ownership =
        g_declarative_runtime_state.global[DECLARATIVE_CONFIG_OPACITY_DURATION];
    const char *invalid[] = { "-0.25", "nan", "inf", "2147483.75", "2147484" };

    for (int i = 0; i < array_count(invalid); ++i) {
        size_t value_length = strlen(invalid[i]);
        char message[sizeof("window_opacity_duration") + value_length + 2];
        memset(message, 0, sizeof(message));
        memcpy(message, "window_opacity_duration", sizeof("window_opacity_duration"));
        memcpy(message + sizeof("window_opacity_duration"), invalid[i], value_length);
        FILE *rsp = tmpfile();
        TEST_CHECK(rsp != NULL, true);
        if (rsp) {
            handle_domain_config(rsp, message_test_token("config"), message);
            rewind(rsp);
            char output[256] = {0};
            fread(output, 1, sizeof(output) - 1, rsp);
            fclose(rsp);
            char expected[256];
            snprintf(expected, sizeof(expected), FAILURE_MESSAGE "unknown value '%s' given to command 'window_opacity_duration' for domain 'config'\n", invalid[i]);
            TEST_CHECK(strcmp(output, expected), 0);
        }
        TEST_CHECK(g_window_manager.window_opacity_duration == saved_duration, true);
        TEST_CHECK(memcmp(&g_declarative_runtime_state.global[DECLARATIVE_CONFIG_OPACITY_DURATION],
                          &saved_ownership, sizeof(saved_ownership)), 0);
    }

    TEST_CHECK(sa_opacity_duration_is_safe(2147483.5f), true);
    TEST_CHECK(sa_opacity_duration_is_safe(2147483.75f), false);
    g_window_manager.window_opacity_duration = saved_duration;
    g_declarative_runtime_state.global[DECLARATIVE_CONFIG_OPACITY_DURATION] = saved_ownership;
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

static size_t message_mode_test_activate_calls;
static size_t message_mode_test_deactivate_calls;
static enum declarative_modes_error_code message_mode_test_error;
static bool message_mode_test_fail;
static bool message_mode_test_name_ok;
static bool message_mode_test_activate(const char *name, struct declarative_modes_error *error)
{
    ++message_mode_test_activate_calls;
    message_mode_test_name_ok = strcmp(name, "work") == 0;
    if (message_mode_test_fail) { error->code = message_mode_test_error; return false; }
    return true;
}
static bool message_mode_test_deactivate(struct declarative_modes_error *error)
{
    ++message_mode_test_deactivate_calls;
    if (message_mode_test_fail) { error->code = message_mode_test_error; return false; }
    return true;
}
static bool message_mode_test_snapshot(struct declarative_mode_snapshot *snapshot)
{
    static char *names[] = { "plain", "quo\"te\\line\n", "模式" };
    *snapshot = (struct declarative_mode_snapshot) { .names = names, .count = 3, .active_name = names[1] };
    return true;
}
static void message_mode_test_snapshot_destroy(struct declarative_mode_snapshot *snapshot) { (void) snapshot; }

static void message_mode_test_run(char *message, size_t message_length,
                                  char *output, size_t capacity)
{
    FILE *response = tmpfile();
    if (!response) { if (capacity) output[0] = '\0'; return; }
    handle_message(response, message, message_length);
    rewind(response);
    size_t count = fread(output, 1, capacity - 1, response);
    output[count] = '\0';
    fclose(response);
}

#define MESSAGE_MODE_RUN(frame) message_mode_test_run((frame), sizeof(frame), output, sizeof(output))
#define MESSAGE_MODE_REJECT(frame) do { MESSAGE_MODE_RUN(frame); TEST_CHECK((unsigned char) output[0], (unsigned char) FAILURE_MESSAGE[0]); } while (0)
TEST_FUNC(message_mode_domain_is_exclusive_strict_and_json_safe,
{
    __auto_type saved_activate = message_mode_activate;
    __auto_type saved_deactivate = message_mode_deactivate;
    __auto_type saved_snapshot = message_mode_snapshot_create;
    __auto_type saved_destroy = message_mode_snapshot_destroy;
    message_mode_activate = message_mode_test_activate;
    message_mode_deactivate = message_mode_test_deactivate;
    message_mode_snapshot_create = message_mode_test_snapshot;
    message_mode_snapshot_destroy = message_mode_test_snapshot_destroy;
    message_mode_test_activate_calls = 0;
    message_mode_test_deactivate_calls = 0;
    message_mode_test_error = DECLARATIVE_MODES_ERROR_NONE;
    message_mode_test_fail = false;
    message_mode_test_name_ok = false;
    char output[512];

    char activate[] = "mode\0--activate\0work\0";
    MESSAGE_MODE_RUN(activate);
    TEST_CHECK(output[0], '\0');
    TEST_CHECK(message_mode_test_activate_calls, 1UL);
    TEST_CHECK(message_mode_test_name_ok, true);

    char invalid_missing[] = "mode\0--activate\0";
    char invalid_trailing[] = "mode\0--activate\0work\0extra\0";
    char invalid_combined[] = "mode\0--activate\0work\0--query\0";
    char invalid_deactivate[] = "mode\0--deactivate\0extra\0";
    char invalid_query[] = "mode\0--query\0extra\0";
    char invalid_list[] = "mode\0--list\0extra\0";
    char invalid_empty[] = "mode\0";
    char activate_hidden[] = "mode\0--activate\0work\0\0--query\0";
    char deactivate_hidden[] = "mode\0--deactivate\0\0ignored\0";
    char query_hidden[] = "mode\0--query\0\0ignored\0";
    char list_hidden[] = "mode\0--list\0\0ignored\0";
    MESSAGE_MODE_REJECT(invalid_missing); MESSAGE_MODE_REJECT(invalid_trailing); MESSAGE_MODE_REJECT(invalid_combined);
    MESSAGE_MODE_REJECT(invalid_deactivate); MESSAGE_MODE_REJECT(invalid_query); MESSAGE_MODE_REJECT(invalid_list);
    MESSAGE_MODE_REJECT(invalid_empty); MESSAGE_MODE_REJECT(activate_hidden); MESSAGE_MODE_REJECT(deactivate_hidden);
    MESSAGE_MODE_REJECT(query_hidden); MESSAGE_MODE_REJECT(list_hidden);
    TEST_CHECK(message_mode_test_activate_calls, 1UL);
    TEST_CHECK(message_mode_test_deactivate_calls, 0UL);

    char deactivate[] = "mode\0--deactivate\0";
    MESSAGE_MODE_RUN(deactivate);
    TEST_CHECK(output[0], '\0');
    TEST_CHECK(message_mode_test_deactivate_calls, 1UL);

    char query[] = "mode\0--query\0";
    MESSAGE_MODE_RUN(query);
    TEST_CHECK(strcmp(output, "\"quo\\\"te\\\\line\\u000a\"\n"), 0);
    char list[] = "mode\0--list\0";
    MESSAGE_MODE_RUN(list);
    TEST_CHECK(strcmp(output, "{\"modes\":[\"plain\",\"quo\\\"te\\\\line\\u000a\",\"模式\"],\"active\":\"quo\\\"te\\\\line\\u000a\"}\n"), 0);

    struct declarative_mode_snapshot null_snapshot = { .names = (char *[]) { "plain" }, .count = 1 };
    char *null_json = mode_json_snapshot(&null_snapshot, true);
    TEST_CHECK(null_json != NULL, true);
    if (null_json) { TEST_CHECK(strcmp(null_json, "{\"modes\":[\"plain\"],\"active\":null}\n"), 0); free(null_json); }

    struct error_case { enum declarative_modes_error_code code; const char *text; bool retry; } errors[] = {
        { DECLARATIVE_MODES_ERROR_EXPLICIT_MODE_NOT_FOUND, "not declared", false },
        { DECLARATIVE_MODES_ERROR_INVALID_MODEL, "terminal runtime invariant", false },
        { DECLARATIVE_MODES_ERROR_ACTIVE_MODE_NOT_FOUND, "terminal runtime invariant", false },
        { DECLARATIVE_MODES_ERROR_TERMINAL, "terminal runtime invariant", false },
        { DECLARATIVE_MODES_ERROR_NONE, "terminal runtime invariant", false },
        { DECLARATIVE_MODES_ERROR_RESOURCE, "runtime resource is unavailable", true },
        { DECLARATIVE_MODES_ERROR_UNAVAILABLE, "mode runtime is unavailable", false },
        { DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED, "asynchronous reconciliation settles", true }
    };
    message_mode_test_fail = true;
    for (size_t i = 0; i < array_count(errors); ++i) {
        message_mode_test_error = errors[i].code;
        char failure[] = "mode\0--activate\0work\0";
        MESSAGE_MODE_RUN(failure);
        TEST_CHECK(strstr(output, errors[i].text) != NULL, true);
        TEST_CHECK(strstr(output, "retry") != NULL, errors[i].retry);
    }
    TEST_CHECK(message_mode_test_activate_calls, 1UL + array_count(errors));
    message_mode_test_error = DECLARATIVE_MODES_ERROR_RESOURCE;
    char deactivate_failure[] = "mode\0--deactivate\0";
    MESSAGE_MODE_RUN(deactivate_failure);
    TEST_CHECK(strstr(output, "runtime resource is unavailable") != NULL, true);
    TEST_CHECK(message_mode_test_deactivate_calls, 2UL);
    message_mode_activate = saved_activate;
    message_mode_deactivate = saved_deactivate;
    message_mode_snapshot_create = saved_snapshot;
    message_mode_snapshot_destroy = saved_destroy;
});
#undef MESSAGE_MODE_REJECT
#undef MESSAGE_MODE_RUN
