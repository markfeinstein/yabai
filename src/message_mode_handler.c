static char *mode_json_snapshot(const struct declarative_mode_snapshot *snapshot, bool list)
{
    size_t capacity = list ? sizeof("{\"modes\":[],\"active\":null}\n") : sizeof("null\n");
    if (list) {
        for (size_t i = 0; i < snapshot->count; ++i) {
            size_t item = mode_json_string_size(snapshot->names[i]);
            if (!item || capacity > SIZE_MAX - item - (i != 0)) return NULL;
            capacity += item + (i != 0);
        }
    }
    if (snapshot->active_name) {
        size_t active = mode_json_string_size(snapshot->active_name);
        if (!active || capacity > SIZE_MAX - active) return NULL;
        capacity += active;
    }
    char *output = malloc(capacity);
    if (!output) return NULL;
    char *cursor = output;
    if (list) {
        memcpy(cursor, "{\"modes\":[", 10); cursor += 10;
        for (size_t i = 0; i < snapshot->count; ++i) {
            if (i) *cursor++ = ',';
            cursor = mode_json_write_string(cursor, snapshot->names[i]);
        }
        memcpy(cursor, "],\"active\":", 11); cursor += 11;
    }
    if (snapshot->active_name) cursor = mode_json_write_string(cursor, snapshot->active_name);
    else { memcpy(cursor, "null", 4); cursor += 4; }
    if (list) *cursor++ = '}';
    *cursor++ = '\n';
    *cursor = '\0';
    return output;
}

static bool mode_wire_is_exact(const char *wire, size_t length)
{
    if (!daemon_message_body_is_valid(wire, length)) return false;
    struct token tokens[4] = {0};
    size_t count = 0, offset = 0;
    while (offset < length - 1) {
        size_t remaining = length - 1 - offset;
        const char *end = memchr(wire + offset, '\0', remaining);
        if (!end || end == wire + offset || count == array_count(tokens)) return false;
        tokens[count++] = (struct token) { .text = (char *) wire + offset,
                                          .length = (int) (end - wire - offset) };
        offset = (size_t) (end - wire) + 1;
    }
    if (offset != length - 1 || count < 2 || !token_equals(tokens[0], DOMAIN_MODE)) return false;
    if (token_equals(tokens[1], COMMAND_MODE_ACTIVATE)) return count == 3;
    if (token_equals(tokens[1], COMMAND_MODE_DEACTIVATE) ||
        token_equals(tokens[1], COMMAND_MODE_QUERY) ||
        token_equals(tokens[1], COMMAND_MODE_LIST)) return count == 2;
    return count == 2;
}

static void mode_fail_runtime(FILE *rsp, const char *operation,
                              const struct declarative_modes_error *error)
{
    switch (error->code) {
    case DECLARATIVE_MODES_ERROR_EXPLICIT_MODE_NOT_FOUND:
        daemon_fail(rsp, "mode is not declared; choose a name from mode --list.\n");
        break;
    case DECLARATIVE_MODES_ERROR_RESOURCE:
        daemon_fail(rsp, "could not %s mode because a runtime resource is unavailable; retry later.\n",
                    operation);
        break;
    case DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED:
        daemon_fail(rsp, "could not %s mode; retry after asynchronous reconciliation settles.\n",
                    operation);
        break;
    case DECLARATIVE_MODES_ERROR_UNAVAILABLE:
        daemon_fail(rsp, "could not %s mode because native mode runtime is unavailable.\n",
                    operation);
        break;
    case DECLARATIVE_MODES_ERROR_INVALID_MODEL:
    case DECLARATIVE_MODES_ERROR_ACTIVE_MODE_NOT_FOUND:
    case DECLARATIVE_MODES_ERROR_TERMINAL:
    case DECLARATIVE_MODES_ERROR_NONE:
    default:
        daemon_fail(rsp, "could not %s mode because a terminal runtime invariant failed.\n",
                    operation);
        break;
    }
}

static void handle_domain_mode(FILE *rsp, struct token domain, char *message)
{
    struct token command = get_token(&message);
    struct token argument = get_token(&message);
    struct token trailing = get_token(&message);
    if (token_equals(command, COMMAND_MODE_ACTIVATE)) {
        if (!token_is_valid(argument) || token_is_valid(trailing)) {
            daemon_fail(rsp, "mode --activate requires exactly one name.\n");
            return;
        }
        if (!utf8_validate_bytes(argument.text, (size_t) argument.length)) {
            daemon_fail(rsp, "mode name is not valid UTF-8.\n");
            return;
        }
        struct declarative_modes_error error = {0};
        if (!message_mode_activate(argument.text, &error)) mode_fail_runtime(rsp, "activate", &error);
    } else if (token_equals(command, COMMAND_MODE_DEACTIVATE) ||
               token_equals(command, COMMAND_MODE_QUERY) ||
               token_equals(command, COMMAND_MODE_LIST)) {
        if (token_is_valid(argument)) {
            daemon_fail(rsp, "command '%.*s' for domain '%.*s' accepts no arguments.\n",
                        command.length, command.text, domain.length, domain.text);
            return;
        }
        if (token_equals(command, COMMAND_MODE_DEACTIVATE)) {
            struct declarative_modes_error error = {0};
            if (!message_mode_deactivate(&error)) mode_fail_runtime(rsp, "deactivate", &error);
            return;
        }
        struct declarative_mode_snapshot snapshot = {0};
        if (!message_mode_snapshot_create(&snapshot)) {
            daemon_fail(rsp, "could not snapshot installed modes.\n");
            return;
        }
        char *json = mode_json_snapshot(&snapshot, token_equals(command, COMMAND_MODE_LIST));
        message_mode_snapshot_destroy(&snapshot);
        if (!json) {
            daemon_fail(rsp, "could not encode installed modes.\n");
            return;
        }
        fputs(json, rsp);
        free(json);
    } else {
        daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n",
                    command.length, command.text, domain.length, domain.text);
    }
}
