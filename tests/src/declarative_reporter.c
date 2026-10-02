struct declarative_reporter_test_state
{
    int notifications;
    int logs;
    bool fail_notification;
    bool fail_log;
    bool throw_notification;
    bool throw_log;
    char title[32];
    char subtitle[32];
    char notification[DECLARATIVE_REPORT_PRIMARY_MAX + 1];
    char log[DECLARATIVE_REPORT_LOG_MAX + 1];
    char format[64];
};

static bool declarative_reporter_test_notification(const char *title, const char *subtitle,
                                                   const char *payload, void *context)
{
    struct declarative_reporter_test_state *state = context;
    ++state->notifications;
    if (state->throw_notification) @throw [NSException exceptionWithName:@"ReporterTest" reason:@"injected" userInfo:nil];
    snprintf(state->title, sizeof(state->title), "%s", title);
    snprintf(state->subtitle, sizeof(state->subtitle), "%s", subtitle);
    snprintf(state->notification, sizeof(state->notification), "%s", payload);
    return !state->fail_notification;
}

static bool declarative_reporter_test_log(const char *format, const char *payload, void *context)
{
    struct declarative_reporter_test_state *state = context;
    ++state->logs;
    if (state->throw_log) @throw [NSException exceptionWithName:@"ReporterTest" reason:@"injected" userInfo:nil];
    snprintf(state->format, sizeof(state->format), "%s", format);
    snprintf(state->log, sizeof(state->log), "%s", payload);
    return !state->fail_log;
}

static bool declarative_reporter_test_utf8_valid(const char *bytes, size_t length)
{
    return utf8_validate_bytes(bytes, length);
}

TEST_FUNC(declarative_reporter_bounds_redacts_and_escapes_structural_fields,
{
    char long_message[4096];
    memset(long_message, 'x', sizeof(long_message) - 1);
    long_message[sizeof(long_message) - 1] = '\0';
    char output[DECLARATIVE_REPORT_PRIMARY_MAX + 1];
    size_t length = declarative_reporter_test_render(output, sizeof(output), DECLARATIVE_REPORT_PRIMARY_MAX,
                                                     "schema", "/tmp/control\npath", "$.rules.secretCandidate",
                                                     12, 34, long_message, true);
    TEST_CHECK(length, (size_t) DECLARATIVE_REPORT_PRIMARY_MAX);
    TEST_CHECK(output[length - 1], '\n');
    TEST_CHECK(strstr(output, "\\x0A") != NULL, true);
    TEST_CHECK(strstr(output, "secretCandidate") == NULL, true);
    TEST_CHECK(strstr(output, "$.<unknown>") != NULL, true);
    TEST_CHECK(strstr(output, "...<truncated>\n") != NULL, true);
    TEST_CHECK(declarative_reporter_test_utf8_valid(output, length), true);

    memset(long_message, 0, sizeof(long_message));
    size_t cursor = 0;
    while (cursor + 4 < sizeof(long_message)) {
        memcpy(long_message + cursor, "😀", 4);
        cursor += 4;
    }
    length = declarative_reporter_test_render(output, sizeof(output), DECLARATIVE_REPORT_PRIMARY_MAX,
                                              "schema", "/tmp/config", "$.config.window_gap",
                                              1, 2, long_message, true);
    TEST_CHECK(length <= DECLARATIVE_REPORT_PRIMARY_MAX, true);
    TEST_CHECK(output[length - 1], '\n');
    TEST_CHECK(declarative_reporter_test_utf8_valid(output, length), true);
});

TEST_FUNC(declarative_reporter_isolates_sinks_and_uses_literal_bounded_payloads,
{
    struct declarative_reporter_test_state state = { .fail_notification = true, .fail_log = true };
    const struct declarative_reporter_test_sinks sinks = {
        .notification = declarative_reporter_test_notification,
        .unified_log = declarative_reporter_test_log,
        .context = &state
    };
    declarative_reporter_test_set_sinks(&sinks);

    char message[4096];
    memset(message, '%', sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    char *primary = NULL;
    size_t primary_length = 0;
    FILE *stream = open_memstream(&primary, &primary_length);
    declarative_report_runtime(stream, "runtime", "/tmp/config", "$.config.layout", 0, 0, message);
    fclose(stream);

    TEST_CHECK(primary_length, (size_t) DECLARATIVE_REPORT_PRIMARY_MAX);
    TEST_CHECK(primary[primary_length - 1], '\n');
    TEST_CHECK(state.notifications, 1);
    TEST_CHECK(state.logs, 1);
    TEST_CHECK(strcmp(state.title, "yabai"), 0);
    TEST_CHECK(strcmp(state.subtitle, "configuration"), 0);
    TEST_CHECK(strlen(state.notification), (size_t) DECLARATIVE_REPORT_PRIMARY_MAX);
    TEST_CHECK(strlen(state.log), (size_t) DECLARATIVE_REPORT_LOG_MAX);
    TEST_CHECK(strcmp(state.format, "%{public}s"), 0);
    TEST_CHECK(strstr(primary, "%") != NULL, true);
    free(primary);

    state.throw_notification = true;
    state.throw_log = true;
    primary = NULL;
    primary_length = 0;
    stream = open_memstream(&primary, &primary_length);
    declarative_report_runtime(stream, "runtime", "/tmp/config", "$", 0, 0, "exception isolation");
    fclose(stream);
    TEST_CHECK(primary_length > 0, true);
    TEST_CHECK(strstr(primary, "exception isolation") != NULL, true);
    free(primary);
    declarative_reporter_test_reset();
});

TEST_FUNC(declarative_reporter_client_marker_is_inside_the_primary_bound,
{
    struct declarative_reporter_test_state state = {0};
    const struct declarative_reporter_test_sinks sinks = {
        .notification = declarative_reporter_test_notification,
        .unified_log = declarative_reporter_test_log,
        .context = &state
    };
    declarative_reporter_test_set_sinks(&sinks);
    char message[4096];
    memset(message, 'z', sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    char *client = NULL;
    size_t client_length = 0;
    FILE *stream = open_memstream(&client, &client_length);
    declarative_report_runtime_client(stream, "runtime", "/tmp/config", "$", 0, 0, message);
    fclose(stream);
    TEST_CHECK(client_length, (size_t) DECLARATIVE_REPORT_PRIMARY_MAX);
    TEST_CHECK(client[0], FAILURE_MESSAGE[0]);
    TEST_CHECK(client[client_length - 1], '\n');
    TEST_CHECK(memchr(client + 1, FAILURE_MESSAGE[0], client_length - 1) == NULL, true);
    free(client);
    declarative_reporter_test_reset();
});

TEST_FUNC(declarative_stale_warning_acknowledges_after_sink_failure_and_rearms_by_generation,
{
    struct declarative_reporter_test_state state = { .fail_notification = true, .fail_log = true };
    const struct declarative_reporter_test_sinks sinks = {
        .notification = declarative_reporter_test_notification,
        .unified_log = declarative_reporter_test_log,
        .context = &state
    };
    declarative_reporter_test_set_sinks(&sinks);
    struct rule_warning_state warning = {0};
    struct rule rule = {
        .owner = RULE_OWNER_NATIVE, .generation = 7, .warning_state = &warning,
        .effects = { .did = 42 }, .display_uuid = "captured-display"
    };
    struct rule_effects effects = {0};
    enum rule_fold_result fold = rule_combine_rule_effects(&rule, &effects, NULL);
    TEST_CHECK(fold, RULE_FOLD_WARNING_NEWLY_REQUIRED);
    rule_report_fold_warning(&rule, fold);
    TEST_CHECK(warning.emitted_generation, 7ULL);
    TEST_CHECK(state.notifications, 1);
    TEST_CHECK(state.logs, 1);
    TEST_CHECK(strstr(state.notification, "native rule target is stale") != NULL, true);

    rule_report_fold_warning(&rule, RULE_FOLD_OK);
    TEST_CHECK(state.notifications, 1);
    rule.generation = 8;
    memset(&effects, 0, sizeof(effects));
    fold = rule_combine_rule_effects(&rule, &effects, NULL);
    TEST_CHECK(fold, RULE_FOLD_WARNING_NEWLY_REQUIRED);
    rule_report_fold_warning(&rule, fold);
    TEST_CHECK(warning.emitted_generation, 8ULL);
    TEST_CHECK(state.notifications, 2);
    TEST_CHECK(strstr(state.notification, "label") == NULL, true);
    TEST_CHECK(strstr(state.notification, "uuid") == NULL, true);
    declarative_reporter_test_reset();
});
