#include <os/log.h>

static const char declarative_report_log_format[] = "%{public}s";
static NSString *const declarative_report_notification_identifier = @"com.asmvik.yabai.configuration";

#ifdef TESTS
static const struct declarative_reporter_test_sinks *declarative_reporter_test_sinks;
void declarative_reporter_test_set_sinks(const struct declarative_reporter_test_sinks *sinks)
{
    declarative_reporter_test_sinks = sinks;
}
void declarative_reporter_test_reset(void) { declarative_reporter_test_sinks = NULL; }
#endif

struct declarative_report_builder
{
    char *bytes;
    size_t capacity;
    size_t length;
};

static void declarative_report_append_bytes(struct declarative_report_builder *builder,
                                            const char *bytes, size_t length)
{
    if (!builder || !builder->capacity || !bytes) return;
    size_t available = builder->capacity - 1 - builder->length;
    if (length > available) length = available;
    memcpy(builder->bytes + builder->length, bytes, length);
    builder->length += length;
    builder->bytes[builder->length] = '\0';
}

static void declarative_report_append(struct declarative_report_builder *builder, const char *text)
{
    if (text) declarative_report_append_bytes(builder, text, strlen(text));
}

static void declarative_report_append_number(struct declarative_report_builder *builder, size_t value)
{
    char number[32];
    int length = snprintf(number, sizeof(number), "%zu", value);
    if (length > 0) declarative_report_append_bytes(builder, number, (size_t) length);
}

static bool declarative_report_path_token_known(const char *token, size_t length)
{
    static const char *known[] = {
        "version", "config", "spaces", "modes", "active_mode", "selected_mode", "rules",
        "name", "scaffold", "match", "settings", "effects", "label", "one_shot",
        "app", "title", "role", "subrole", "pattern", "exclude",
        "display", "space", "follow", "grid", "opacity", "manage", "sticky",
        "mouse_follows_focus", "sub_layer", "native_fullscreen", "scratchpad", "index",
        "uuid", "external_bar", "mode", "top_padding", "bottom_padding", "left_padding",
        "right_padding", "window_gap", "unknown", "duplicate"
    };
    if ((length == 9 && !memcmp(token, "<unknown>", length)) ||
        (length == 11 && !memcmp(token, "<duplicate>", length))) return true;
    for (size_t i = 0; i < sizeof(known) / sizeof(*known); ++i) {
        if (strlen(known[i]) == length && !memcmp(known[i], token, length)) return true;
    }
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        const char *name = declarative_config_descriptors[i].name;
        if (strlen(name) == length && !memcmp(name, token, length)) return true;
    }
    for (int i = 0; i < DECLARATIVE_RULE_FIELD_COUNT; ++i) {
        const char *name = declarative_rule_field_descriptors[i].json_name;
        if (strlen(name) == length && !memcmp(name, token, length)) return true;
    }
    return false;
}

static const char *declarative_report_sanitized_json_path(const char *path)
{
    if (!path || path[0] != '$') return "$.<unknown>";
    const char *cursor = path + 1;
    while (*cursor) {
        if (*cursor == '.') {
            const char *token = ++cursor;
            if (*cursor == '<') {
                while (*cursor && *cursor != '>') ++cursor;
                if (*cursor == '>') ++cursor;
            } else {
                while ((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
                       (*cursor >= '0' && *cursor <= '9') || *cursor == '_') ++cursor;
            }
            if (cursor == token || !declarative_report_path_token_known(token, (size_t)(cursor - token))) return "$.<unknown>";
        } else if (*cursor == '[') {
            ++cursor;
            const char *digits = cursor;
            while (*cursor >= '0' && *cursor <= '9') ++cursor;
            if (cursor == digits || *cursor != ']') return "$.<unknown>";
            ++cursor;
        } else {
            return "$.<unknown>";
        }
    }
    return path;
}

static void declarative_report_append_escaped_path(struct declarative_report_builder *builder, const char *path)
{
    if (!path || !path[0]) {
        declarative_report_append(builder, "<none>");
        return;
    }
    for (const unsigned char *cursor = (const unsigned char *) path; *cursor; ++cursor) {
        unsigned char value = *cursor;
        if (value == '\\') {
            declarative_report_append(builder, "\\\\");
        } else if (value < 0x20 || value == 0x7f) {
            char escaped[5];
            snprintf(escaped, sizeof(escaped), "\\x%02X", value);
            declarative_report_append(builder, escaped);
        } else {
            declarative_report_append_bytes(builder, (const char *) cursor, 1);
        }
    }
}

static size_t declarative_report_utf8_prefix(const char *bytes, size_t length)
{
    while (length && !utf8_validate_bytes(bytes, length)) --length;
    return length;
}

static size_t declarative_report_limit(char *output, size_t length, size_t limit, bool newline)
{
    static const char suffix[] = "...<truncated>";
    size_t newline_bytes = newline ? 1 : 0;
    if (!limit) return 0;
    length = declarative_report_utf8_prefix(output, length);
    if (length + newline_bytes <= limit) {
        if (newline) output[length++] = '\n';
        output[length] = '\0';
        return length;
    }
    size_t suffix_length = sizeof(suffix) - 1;
    size_t text_limit = limit - newline_bytes;
    if (text_limit <= suffix_length) {
        memcpy(output, suffix, text_limit);
        length = text_limit;
    } else {
        size_t prefix_limit = text_limit - suffix_length;
        size_t prefix = declarative_report_utf8_prefix(output, prefix_limit);
        memcpy(output + prefix, suffix, suffix_length);
        length = prefix + suffix_length;
    }
    if (newline) output[length++] = '\n';
    output[length] = '\0';
    return length;
}

static size_t declarative_report_render(char *output, size_t capacity, size_t limit,
                                        const char *stage, const char *public_path,
                                        const char *json_path, size_t line, size_t column,
                                        const char *message, bool newline)
{
    if (!output || capacity <= limit || !limit) return 0;
    struct declarative_report_builder builder = { .bytes = output, .capacity = capacity };
    output[0] = '\0';
    declarative_report_append(&builder, "yabai: configuration error: stage=");
    declarative_report_append(&builder, stage && stage[0] ? stage : "runtime");
    declarative_report_append(&builder, " path=");
    declarative_report_append_escaped_path(&builder, public_path);
    declarative_report_append(&builder, " location=");
    declarative_report_append(&builder, declarative_report_sanitized_json_path(json_path));
    declarative_report_append(&builder, " line=");
    declarative_report_append_number(&builder, line);
    declarative_report_append(&builder, " column=");
    declarative_report_append_number(&builder, column);
    declarative_report_append(&builder, " message=");
    declarative_report_append(&builder, message && message[0] ? message : "configuration operation failed");
    return declarative_report_limit(output, builder.length, limit, newline);
}

static bool declarative_report_notification(const char *payload)
{
#ifdef TESTS
    if (declarative_reporter_test_sinks && declarative_reporter_test_sinks->notification) {
        @try {
            return declarative_reporter_test_sinks->notification(
                [declarative_report_notification_identifier UTF8String], "yabai", "configuration", payload,
                declarative_reporter_test_sinks->context);
        } @catch (NSException *exception) {
            (void) exception;
            return false;
        }
    }
    return true;
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    @try {
        @autoreleasepool {
            NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
            notification.identifier = declarative_report_notification_identifier;
            notification.title = @"yabai";
            notification.subtitle = @"configuration";
            notification.informativeText = [NSString stringWithUTF8String:payload];
            [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
        }
        return true;
    } @catch (NSException *exception) {
        (void) exception;
        return false;
    }
#pragma clang diagnostic pop
#endif
}

static bool declarative_report_unified_log(const char *payload)
{
#ifdef TESTS
    if (declarative_reporter_test_sinks && declarative_reporter_test_sinks->unified_log) {
        @try {
            return declarative_reporter_test_sinks->unified_log(declarative_report_log_format, payload,
                                                               declarative_reporter_test_sinks->context);
        } @catch (NSException *exception) {
            (void) exception;
            return false;
        }
    }
    return true;
#else
    static os_log_t configuration_log;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        configuration_log = os_log_create("com.asmvik.yabai", "configuration");
    });
    if (!configuration_log) return false;
    os_log_with_type(configuration_log, OS_LOG_TYPE_ERROR, "%{public}s", payload);
    return true;
#endif
}

static void declarative_report_runtime_internal(FILE *primary, bool client,
                                                const char *stage, const char *public_path,
                                                const char *json_path, size_t line, size_t column,
                                                const char *message)
{
    char primary_payload[DECLARATIVE_REPORT_PRIMARY_MAX + 1];
    char notification_payload[DECLARATIVE_REPORT_PRIMARY_MAX + 1];
    char log_payload[DECLARATIVE_REPORT_LOG_MAX + 1];
    size_t primary_limit = client ? DECLARATIVE_REPORT_PRIMARY_MAX - 1 : DECLARATIVE_REPORT_PRIMARY_MAX;
    size_t primary_length = declarative_report_render(primary_payload, sizeof(primary_payload),
                                                      primary_limit, stage, public_path,
                                                      json_path, line, column, message, true);
    (void) declarative_report_render(notification_payload, sizeof(notification_payload),
                                     DECLARATIVE_REPORT_PRIMARY_MAX, stage, public_path,
                                     json_path, line, column, message, false);
    (void) declarative_report_render(log_payload, sizeof(log_payload), DECLARATIVE_REPORT_LOG_MAX,
                                     stage, public_path, json_path, line, column, message, false);
    if (primary) {
        if (client) (void) fwrite(FAILURE_MESSAGE, 1, 1, primary);
        (void) fwrite(primary_payload, 1, primary_length, primary);
    }
    (void) declarative_report_notification(notification_payload);
    (void) declarative_report_unified_log(log_payload);
}

void declarative_report_runtime(FILE *primary, const char *stage, const char *public_path,
                                const char *json_path, size_t line, size_t column,
                                const char *message)
{
    declarative_report_runtime_internal(primary, false, stage, public_path, json_path, line, column, message);
}

void declarative_report_runtime_client(FILE *primary, const char *stage, const char *public_path,
                                       const char *json_path, size_t line, size_t column,
                                       const char *message)
{
    declarative_report_runtime_internal(primary, true, stage, public_path, json_path, line, column, message);
}

static void declarative_report_error_internal(FILE *primary, bool client, const struct declarative_error *error)
{
    declarative_report_runtime_internal(primary, client,
                                        declarative_error_stage_name(error ? error->stage : DECLARATIVE_ERROR_NONE),
                                        error && error->path[0] ? error->path : NULL,
                                        error && error->json_path[0] ? error->json_path : "$",
                                        error ? error->line : 0, error ? error->column : 0,
                                        error && error->message[0] ? error->message : "invalid invocation");
}

void declarative_report_error(FILE *primary, const struct declarative_error *error)
{
    declarative_report_error_internal(primary, false, error);
}

void declarative_report_error_client(FILE *primary, const struct declarative_error *error)
{
    declarative_report_error_internal(primary, true, error);
}

void declarative_report_stale_rule(void)
{
    declarative_report_runtime(stderr, "rule", declarative_installed_path(), "$.rules.effects",
                               0, 0, "native rule target is unavailable or stale; move and follow effects were skipped");
}

#ifdef TESTS
size_t declarative_reporter_test_render(char *output, size_t capacity, size_t limit,
                                        const char *stage, const char *public_path,
                                        const char *json_path, size_t line, size_t column,
                                        const char *message, bool newline)
{
    return declarative_report_render(output, capacity, limit, stage, public_path,
                                     json_path, line, column, message, newline);
}
#endif
