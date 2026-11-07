struct token
{
    char *text;
    int length;
};

enum token_type
{
    TOKEN_TYPE_INVALID,
    TOKEN_TYPE_UNKNOWN,
    TOKEN_TYPE_INT,
    TOKEN_TYPE_FLOAT,
    TOKEN_TYPE_U32,
    TOKEN_TYPE_STRING
};

struct token_value
{
    struct token token;
    enum token_type type;

    union {
        int int_value;
        float float_value;
        uint32_t u32_value;
        char *string_value;
    };
};

static const int token_char_int_table[] =
{
    ['0'] = 0x0, ['1'] = 0x1,
    ['2'] = 0x2, ['3'] = 0x3,
    ['4'] = 0x4, ['5'] = 0x5,
    ['6'] = 0x6, ['7'] = 0x7,
    ['8'] = 0x8, ['9'] = 0x9,
    ['a'] = 0xA, ['A'] = 0xA,
    ['b'] = 0xB, ['B'] = 0xB,
    ['c'] = 0xC, ['C'] = 0xC,
    ['d'] = 0xD, ['D'] = 0xD,
    ['e'] = 0xE, ['E'] = 0xE,
    ['f'] = 0xF, ['F'] = 0xF,
};

static struct token get_token(char **message)
{
    struct token token;

    token.text = *message;
    while (**message) {
        ++(*message);
    }
    token.length = (int) (*message - token.text);

    if ((*message)[0] == '\0' && (*message)[1] != '\0') {
        ++(*message);
    } else {
        // NOTE(asmvik): don't go past the null-terminator
    }

    return token;
}

static bool token_prefix(struct token token, char *match)
{
    char *at = match;
    for (int i = 0; i < token.length; ++i, ++at) {
        if (*at == 0)             return true;
        if (token.text[i] != *at) return false;
    }
    return *at == 0;
}

static bool token_equals(struct token token, char *match)
{
    char *at = match;
    for (int i = 0; i < token.length; ++i, ++at) {
        if ((*at == 0) || (token.text[i] != *at)) {
            return false;
        }
    }
    return *at == 0;
}

static inline bool token_is_valid(struct token token)
{
    return token.text && token.length > 0;
}

static bool token_is_decimal(struct token token)
{
    int start = token.length > 0 && (token.text[0] == '-' || token.text[0] == '+');
    if (start == token.length) return false;

    for (int i = start; i < token.length; ++i) {
        char c = token.text[i];
        if (!(c >= '0' && c <= '9')) return false;
    }

    return token.length > 0;
}

static bool token_is_integer(struct token token, int *value)
{
    *value = 0;
    bool negative = token.length > 0 && token.text[0] == '-';
    int start = token.length > 0 && (negative || token.text[0] == '+');
    if (start == token.length) return false;

    int result = 0;
    for (int i = start; i < token.length; ++i) {
        char c = token.text[i];
        if (!(c >= '0' && c <= '9')) return false;

        int digit = token_char_int_table[(int)c];
        if (negative) {
            if (result < (INT_MIN + digit) / 10) return false;
            result = result * 10 - digit;
        } else {
            if (result > (INT_MAX - digit) / 10) return false;
            result = result * 10 + digit;
        }
    }

    *value = result;
    return true;
}

static bool token_is_positive_integer(struct token token, int *value)
{
    return token_is_integer(token, value) && *value >= 0 &&
           token.text[0] != '-' && token.text[0] != '+';
}

static bool token_has_hexadecimal_prefix(struct token token)
{
    return token.length >= 2 &&
           token.text[0] == '0' &&
          (token.text[1] == 'x' ||
           token.text[1] == 'X');
}

static bool token_is_prefixed_hexadecimal_digits(struct token token)
{
    if (!token_has_hexadecimal_prefix(token) || token.length <= 2) return false;

    for (int i = 2; i < token.length; ++i) {
        char c = token.text[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }

    return true;
}

static bool token_is_hexadecimal(struct token token, uint32_t *value)
{
    *value = 0;

    if (!token_is_prefixed_hexadecimal_digits(token)) return false;

    for (int i = 2; i < token.length; ++i) {
        uint32_t digit = (uint32_t)token_char_int_table[(int)token.text[i]];
        if (*value > (UINT32_MAX - digit) / 16) {
            *value = 0;
            return false;
        }

        *value = *value * 16 + digit;
    }

    return true;
}

static bool token_is_float(struct token token, float *value)
{
    char *end = NULL;
    errno = 0;
    float v = strtof(token.text, &end);

    if (!end || *end || errno == ERANGE || !__builtin_isfinite(v)) {
        *value = 0.0f;
        return false;
    } else {
        *value = v;
        return true;
    }
}

static struct token_value token_to_value(struct token token)
{
    struct token_value value = { .token = token, .type = TOKEN_TYPE_INVALID };

    if (token_is_valid(token)) {
        if (token_is_integer(token, &value.int_value)) {
            value.type = TOKEN_TYPE_INT;
        } else if (token_is_decimal(token)) {
            value.type = TOKEN_TYPE_UNKNOWN;
        } else if (token_is_hexadecimal(token, &value.u32_value)) {
            value.type = TOKEN_TYPE_U32;
        } else if (token_is_prefixed_hexadecimal_digits(token)) {
            value.type = TOKEN_TYPE_UNKNOWN;
        } else if (token_is_float(token, &value.float_value)) {
            value.type = TOKEN_TYPE_FLOAT;
        } else if ((value.string_value = token.text)) {
            value.type = TOKEN_TYPE_STRING;
        } else {
            value.type = TOKEN_TYPE_UNKNOWN;
        }
    }

    return value;
}

static inline void daemon_fail(FILE *rsp, char *fmt, ...)
{
    if (!rsp) return;

    va_list ap;
    va_start(ap, fmt);
    fprintf(rsp, FAILURE_MESSAGE);
    vfprintf(rsp, fmt, ap);
    va_end(ap);
}

static bool message_compile_pattern(FILE *rsp, regex_t *regex, const char *key, const char *value)
{
    switch (pattern_compile(regex, value)) {
    case PATTERN_COMPILE_OK:
        return true;
    case PATTERN_COMPILE_TOO_COMPLEX:
        daemon_fail(rsp, "regex pattern for key '%s' is too complex\n", key);
        return false;
    case PATTERN_COMPILE_INVALID:
        daemon_fail(rsp, "invalid regex pattern '%s' for key '%s'\n", value, key);
        return false;
    }
    return false;
}

static void parse_key_value_pair(char *token, char **key, char **value, bool *exclusion)
{
    *key = token;

    while (*token) {
        char fst = token[0];
        char snd = token[1];

        if (fst == '!' && snd == '=') {
            break;
        } else if (fst == '=') {
            break;
        }

        ++token;
    }

    int index = (token[0] == '!' && token[1] == '=') ? 2 : 1;
    char check = (index == 2) ? '!' : '=';

    if (*token != check) {
        *key = NULL;
        *value = NULL;
    } else if (token[index]) {
        *token = '\0';
        *value = token+index;
        *exclusion = index == 2;
    } else {
        *value = NULL;
    }
}

static uint8_t parse_value_type(char *type)
{
    if (string_equals(type, "abs")) {
        return TYPE_ABS;
    } else if (string_equals(type, "rel")) {
        return TYPE_REL;
    } else {
        return 0;
    }
}

static uint8_t parse_resize_handle(char *handle)
{
    if (string_equals(handle, "top")) {
        return HANDLE_TOP;
    } else if (string_equals(handle, "bottom")) {
        return HANDLE_BOTTOM;
    } else if (string_equals(handle, "left")) {
        return HANDLE_LEFT;
    } else if (string_equals(handle, "right")) {
        return HANDLE_RIGHT;
    } else if (string_equals(handle, "top_left")) {
        return HANDLE_TOP | HANDLE_LEFT;
    } else if (string_equals(handle, "top_right")) {
        return HANDLE_TOP | HANDLE_RIGHT;
    } else if (string_equals(handle, "bottom_left")) {
        return HANDLE_BOTTOM | HANDLE_LEFT;
    } else if (string_equals(handle, "bottom_right")) {
        return HANDLE_BOTTOM | HANDLE_RIGHT;
    } else if (string_equals(handle, "abs")) {
        return HANDLE_ABS;
    } else {
        return 0;
    }
}

static bool parse_unsigned_component(char **cursor, unsigned *value, char terminator)
{
    *value = 0;
    char *at = *cursor;
    if (!*at || *at == terminator) return false;

    while (*at && *at != terminator) {
        char c = *at;
        if (!(c >= '0' && c <= '9')) return false;

        unsigned digit = (unsigned) token_char_int_table[(int)c];
        if (*value > (UINT_MAX - digit) / 10) {
            *value = 0;
            return false;
        }

        *value = *value * 10 + digit;
        ++at;
    }

    if (terminator) {
        if (*at != terminator) return false;
        ++at;
    } else if (*at) {
        return false;
    }

    *cursor = at;
    return true;
}

static bool parse_window_grid_command(char *text, unsigned *r, unsigned *c, unsigned *x, unsigned *y, unsigned *w, unsigned *h)
{
    char *cursor = text;
    if (!parse_unsigned_component(&cursor, r, ':') ||
        !parse_unsigned_component(&cursor, c, ':') ||
        !parse_unsigned_component(&cursor, x, ':') ||
        !parse_unsigned_component(&cursor, y, ':') ||
        !parse_unsigned_component(&cursor, w, ':') ||
        !parse_unsigned_component(&cursor, h, '\0')) {
        return false;
    }

    return *r > 0 && *c > 0;
}

static bool parse_string_component(char **cursor, char *value, size_t capacity)
{
    char *separator = strchr(*cursor, ':');
    if (!separator) return false;

    size_t length = (size_t) (separator - *cursor);
    if (!length || length >= capacity) return false;

    memcpy(value, *cursor, length);
    value[length] = '\0';
    *cursor = separator + 1;
    return true;
}

static bool parse_window_discriminator(char **cursor, uint8_t (*parser)(char *), uint8_t *value)
{
    char discriminator[MAXLEN];
    if (!parse_string_component(cursor, discriminator, sizeof(discriminator))) return false;

    *value = parser(discriminator);
    return *value != 0;
}

static bool parse_signed_component(char **cursor, int *value, char terminator)
{
    char *end;
    errno = 0;
    long parsed = strtol(*cursor, &end, 10);
    if (end == *cursor || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) return false;

    if (terminator) {
        if (*end != terminator) return false;
        *cursor = end + 1;
    } else {
        if (*end) return false;
        *cursor = end;
    }

    *value = (int) parsed;
    return true;
}

static bool parse_float_component(char **cursor, float *value, char terminator)
{
    char *end;
    errno = 0;
    *value = strtof(*cursor, &end);
    if (end == *cursor || errno == ERANGE || !__builtin_isfinite(*value)) return false;

    if (terminator) {
        if (*end != terminator) return false;
        *cursor = end + 1;
    } else {
        if (*end) return false;
        *cursor = end;
    }

    return true;
}

static bool parse_external_bar_command(char *text, enum external_bar_mode *mode, int *top, int *bottom)
{
    char *cursor = text;
    char mode_name[MAXLEN];
    if (!parse_string_component(&cursor, mode_name, sizeof(mode_name)) ||
        !parse_signed_component(&cursor, top, ':') ||
        !parse_signed_component(&cursor, bottom, '\0')) {
        return false;
    }

    if (string_equals(mode_name, ARGUMENT_CONFIG_EXTERNAL_BAR_MAIN)) {
        *mode = EXTERNAL_BAR_MAIN;
    } else if (string_equals(mode_name, ARGUMENT_CONFIG_EXTERNAL_BAR_ALL)) {
        *mode = EXTERNAL_BAR_ALL;
    } else if (string_equals(mode_name, ARGUMENT_COMMON_VAL_OFF)) {
        *mode = EXTERNAL_BAR_OFF;
    } else {
        return false;
    }

    return true;
}

static bool parse_space_padding_command(char *text, uint8_t *type, int *top, int *bottom, int *left, int *right)
{
    char *cursor = text;
    return parse_window_discriminator(&cursor, parse_value_type, type) &&
           parse_signed_component(&cursor, top, ':') &&
           parse_signed_component(&cursor, bottom, ':') &&
           parse_signed_component(&cursor, left, ':') &&
           parse_signed_component(&cursor, right, '\0');
}

static bool parse_space_gap_command(char *text, uint8_t *type, int *gap)
{
    char *cursor = text;
    return parse_window_discriminator(&cursor, parse_value_type, type) &&
           parse_signed_component(&cursor, gap, '\0');
}

static bool parse_window_move_command(char *text, uint8_t *type, float *x, float *y)
{
    char *cursor = text;
    return parse_window_discriminator(&cursor, parse_value_type, type) &&
           parse_float_component(&cursor, x, ':') &&
           parse_float_component(&cursor, y, '\0');
}

static bool parse_window_resize_command(char *text, uint8_t *handle, float *w, float *h)
{
    char *cursor = text;
    return parse_window_discriminator(&cursor, parse_resize_handle, handle) &&
           parse_float_component(&cursor, w, ':') &&
           parse_float_component(&cursor, h, '\0');
}

static bool parse_window_ratio_command(char *text, uint8_t *type, float *ratio)
{
    char *cursor = text;
    return parse_window_discriminator(&cursor, parse_value_type, type) &&
           parse_float_component(&cursor, ratio, '\0');
}

static bool parse_rule_opacity(char *text, float *opacity)
{
    char *cursor = text;
    return parse_float_component(&cursor, opacity, '\0') && in_range_ii(*opacity, 0.0f, 1.0f);
}

static bool token_value_to_float(struct token_value value, float *result)
{
    if (value.type == TOKEN_TYPE_FLOAT) {
        *result = value.float_value;
        return true;
    }
    if (value.type == TOKEN_TYPE_INT) {
        *result = (float) value.int_value;
        return true;
    }
    return false;
}

static bool parse_window_opacity_token(struct token token, float *opacity)
{
    struct token_value value = token_to_value(token);
    return token_value_to_float(value, opacity) && in_range_ii(*opacity, 0.0f, 1.0f);
}

enum label_type
{
    LABEL_DISPLAY,
    LABEL_SPACE,
    LABEL_WINDOW
};

static char *reserved_display_identifiers[] =
{
    ARGUMENT_COMMON_SEL_NORTH,
    ARGUMENT_COMMON_SEL_EAST,
    ARGUMENT_COMMON_SEL_SOUTH,
    ARGUMENT_COMMON_SEL_WEST,
    ARGUMENT_COMMON_SEL_PREV,
    ARGUMENT_COMMON_SEL_NEXT,
    ARGUMENT_COMMON_SEL_FIRST,
    ARGUMENT_COMMON_SEL_LAST,
    ARGUMENT_COMMON_SEL_RECENT,
    ARGUMENT_COMMON_SEL_MOUSE
};

static char *reserved_space_identifiers[] =
{
    ARGUMENT_COMMON_SEL_PREV,
    ARGUMENT_COMMON_SEL_NEXT,
    ARGUMENT_COMMON_SEL_FIRST,
    ARGUMENT_COMMON_SEL_LAST,
    ARGUMENT_COMMON_SEL_RECENT,
    ARGUMENT_COMMON_SEL_MOUSE
};

static char *reserved_window_identifiers[] =
{
    ARGUMENT_WINDOW_TOGGLE_FLOAT,
    ARGUMENT_WINDOW_TOGGLE_STICKY,
    ARGUMENT_WINDOW_TOGGLE_SHADOW,
    ARGUMENT_WINDOW_TOGGLE_SPLIT,
    ARGUMENT_WINDOW_TOGGLE_PARENT,
    ARGUMENT_WINDOW_TOGGLE_FULLSC,
    ARGUMENT_WINDOW_TOGGLE_WINDOWED,
    ARGUMENT_WINDOW_TOGGLE_NATIVE,
    ARGUMENT_WINDOW_TOGGLE_EXPOSE,
    ARGUMENT_WINDOW_TOGGLE_PIP,
    ARGUMENT_WINDOW_SCRATCHPAD_RECOVER
};

static bool parse_label(FILE *rsp, struct token token, enum label_type type, char **label)
{
    if (token_is_valid(token) && !utf8_validate_bytes(token.text, (size_t) token.length)) {
        daemon_fail(rsp, "label is not valid UTF-8.\n");
        return false;
    }

    struct token_value value = token_to_value(token);

    if (value.type == TOKEN_TYPE_INVALID) {
        *label = NULL;
        return true;
    }

    if (value.type != TOKEN_TYPE_STRING) {
        daemon_fail(rsp, "'%.*s' cannot be used as a label.\n", token.length, token.text);
        return false;
    }

    switch (type) {
    default: break;
    case LABEL_DISPLAY: {
        for (int i = 0; i < array_count(reserved_display_identifiers); ++i) {
            if (token_equals(token, reserved_display_identifiers[i])) {
                daemon_fail(rsp, "'%.*s' is a reserved keyword and cannot be used as a label.\n", token.length, token.text);
                return false;
            }
        }
    } break;
    case LABEL_SPACE: {
        for (int i = 0; i < array_count(reserved_space_identifiers); ++i) {
            if (token_equals(token, reserved_space_identifiers[i])) {
                daemon_fail(rsp, "'%.*s' is a reserved keyword and cannot be used as a label.\n", token.length, token.text);
                return false;
            }
        }
    } break;
    case LABEL_WINDOW: {
        for (int i = 0; i < array_count(reserved_window_identifiers); ++i) {
            if (token_equals(token, reserved_window_identifiers[i])) {
                daemon_fail(rsp, "'%.*s' is a reserved keyword and cannot be used as a scratchpad.\n", token.length, token.text);
                return false;
            }
        }
    } break;
    }

    *label = malloc(token.length + 1);
    if (!(*label)) return false;

    memcpy(*label, token.text, token.length);
    (*label)[token.length] = '\0';

    return true;
}

struct properties
{
    struct token token;
    bool did_parse;
    bool did_error;
    uint64_t flags;
};

static inline bool parse_property(struct properties *properties, char *property, uint64_t *property_val, char **property_str, int property_count)
{
    for (int i = 0; i < property_count; ++i) {
        if (string_equals(property, property_str[i])) {
            properties->flags |= property_val[i];
            return true;
        }
    }

    return false;
}

static struct properties parse_properties(FILE *rsp, struct token token, uint64_t *property_val, char **property_str, int property_count)
{
    struct properties result = { .token = token, .did_error = false };

    if ((result.did_parse = token_is_valid(token) && !token_prefix(token, "--"))) {
        for (int i = 0, cursor = 0; i <= token.length; ++i) {
            if (i != token.length && token.text[i] != ',') continue;

            int length = i - cursor;
            if (length == 0) {
                daemon_fail(rsp, "property list contains an empty property.\n");
                result.did_error = true;
            } else {
                char separator = token.text[i];
                token.text[i] = '\0';
                if (!parse_property(&result, token.text + cursor, property_val, property_str, property_count)) {
                    daemon_fail(rsp, "'%.*s' is not a valid property.\n", length, token.text + cursor);
                    result.did_error = true;
                }
                token.text[i] = separator;
            }
            cursor = i + 1;
        }
    }

    return result;
}
