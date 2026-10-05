#ifndef DECLARATIVE_CONFIG_H
#define DECLARATIVE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DECLARATIVE_CONFIG_MAX_FILE_SIZE (1024u * 1024u)
#define DECLARATIVE_CONFIG_MAX_DEPTH 64u
#define DECLARATIVE_CONFIG_MAX_OBJECT_MEMBERS 256u
#define DECLARATIVE_CONFIG_MAX_SPACES 1024u
#define DECLARATIVE_CONFIG_MAX_MODES 64u
#define DECLARATIVE_CONFIG_MAX_RULES 1024u
#define DECLARATIVE_CONFIG_MAX_SCAFFOLD_SLOTS 3u
#define DECLARATIVE_CONFIG_MAX_KEY_BYTES 128u
#define DECLARATIVE_CONFIG_MAX_STRING_BYTES 1024u
#define DECLARATIVE_CONFIG_MAX_PATTERN_BYTES 4096u
#define DECLARATIVE_CONFIG_MAX_MODEL_BYTES (8u * 1024u * 1024u)

#define CONFIG_SETTING_LIST(X) \
    X(DEBUG_OUTPUT,          COMMAND_CONFIG_DEBUG_OUTPUT,          "debug_output",                BOOL,       GLOBAL, BOOL,             DEBUG_OUTPUT) \
    X(MFF,                   COMMAND_CONFIG_MFF,                   "mouse_follows_focus",         BOOL,       GLOBAL, BOOL,             MFF) \
    X(FFM,                   COMMAND_CONFIG_FFM,                   "focus_follows_mouse",         ENUM,       GLOBAL, FFM,              FFM) \
    X(DISPLAY_ORDER,         COMMAND_CONFIG_DISPLAY_ORDER,         "display_arrangement_order",   ENUM,       GLOBAL, DISPLAY_ORDER,    DISPLAY_ORDER) \
    X(WINDOW_ORIGIN,         COMMAND_CONFIG_WINDOW_ORIGIN,         "window_origin_display",       ENUM,       GLOBAL, WINDOW_ORIGIN,    WINDOW_ORIGIN) \
    X(WINDOW_FOCUS_FALLBACK, COMMAND_CONFIG_WINDOW_FOCUS_FALLBACK, "window_focus_fallback",       ENUM,       GLOBAL, FOCUS_FALLBACK,   WINDOW_FOCUS_FALLBACK) \
    X(WINDOW_PLACEMENT,      COMMAND_CONFIG_WINDOW_PLACEMENT,      "window_placement",            ENUM,       GLOBAL, WINDOW_PLACEMENT, WINDOW_PLACEMENT) \
    X(WINDOW_INSERT_POINT,   COMMAND_CONFIG_WINDOW_INSERT_POINT,   "window_insertion_point",       ENUM,       GLOBAL, INSERT_POINT,     INSERT_POINT) \
    X(WINDOW_ZOOM_PERSIST,   COMMAND_CONFIG_WINDOW_ZOOM_PERSIST,   "window_zoom_persist",          BOOL,       GLOBAL, BOOL,             WINDOW_ZOOM_PERSIST) \
    X(OPACITY,               COMMAND_CONFIG_OPACITY,               "window_opacity",               BOOL,       GLOBAL, BOOL,             OPACITY) \
    X(OPACITY_DURATION,      COMMAND_CONFIG_OPACITY_DURATION,      "window_opacity_duration",      FLOAT,      GLOBAL, OPACITY_DURATION, OPACITY_DURATION) \
    X(ANIMATION_DURATION,    COMMAND_CONFIG_ANIMATION_DURATION,    "window_animation_duration",    FLOAT,      GLOBAL, NONNEGATIVE,      ANIMATION_DURATION) \
    X(ANIMATION_EASING,      COMMAND_CONFIG_ANIMATION_EASING,      "window_animation_easing",      ENUM,       GLOBAL, EASING,           ANIMATION_EASING) \
    X(SHADOW,                COMMAND_CONFIG_SHADOW,                "window_shadow",                 ENUM,       GLOBAL, SHADOW,           SHADOW) \
    X(MENUBAR,               COMMAND_CONFIG_MENUBAR,               "menubar",                       ENUM,       GLOBAL, MENUBAR,          MENUBAR) \
    X(MENUBAR_STYLE,         COMMAND_CONFIG_MENUBAR_STYLE,         "menubar_style",                 ENUM,       GLOBAL, MENUBAR_STYLE,    MENUBAR_STYLE) \
    X(MENUBAR_SPACE_FILTER,  COMMAND_CONFIG_MENUBAR_SPACE_FILTER,  "menubar_space_filter",          ENUM,       GLOBAL, SPACE_FILTER,     SPACE_FILTER) \
    X(MENUBAR_OPACITY,       COMMAND_CONFIG_MENUBAR_OPACITY,       "menubar_opacity",               FLOAT,      GLOBAL, UNIT_FLOAT,       MENUBAR_OPACITY) \
    X(ACTIVE_WINDOW_OPACITY, COMMAND_CONFIG_ACTIVE_WINDOW_OPACITY, "active_window_opacity",         FLOAT,      GLOBAL, POSITIVE_UNIT,    ACTIVE_WINDOW_OPACITY) \
    X(NORMAL_WINDOW_OPACITY, COMMAND_CONFIG_NORMAL_WINDOW_OPACITY, "normal_window_opacity",         FLOAT,      GLOBAL, POSITIVE_UNIT,    NORMAL_WINDOW_OPACITY) \
    X(INSERT_FEEDBACK_COLOR, COMMAND_CONFIG_INSERT_FEEDBACK_COLOR, "insert_feedback_color",         UINT,       GLOBAL, NONZERO_U32,      INSERT_FEEDBACK_COLOR) \
    X(INSERT_FEEDBACK_RADIUS, COMMAND_CONFIG_INSERT_FEEDBACK_RADIUS, "insert_feedback_radius",       FLOAT,      GLOBAL, NONNEGATIVE,      INSERT_FEEDBACK_RADIUS) \
    X(INSERT_FEEDBACK_WIDTH, COMMAND_CONFIG_INSERT_FEEDBACK_WIDTH, "insert_feedback_width",          FLOAT,      GLOBAL, POSITIVE,         INSERT_FEEDBACK_WIDTH) \
    X(TOP_PADDING,           COMMAND_CONFIG_TOP_PADDING,           "top_padding",                   INT,        SPACE,  INT,              TOP_PADDING) \
    X(BOTTOM_PADDING,        COMMAND_CONFIG_BOTTOM_PADDING,        "bottom_padding",                INT,        SPACE,  INT,              BOTTOM_PADDING) \
    X(LEFT_PADDING,          COMMAND_CONFIG_LEFT_PADDING,          "left_padding",                  INT,        SPACE,  INT,              LEFT_PADDING) \
    X(RIGHT_PADDING,         COMMAND_CONFIG_RIGHT_PADDING,         "right_padding",                 INT,        SPACE,  INT,              RIGHT_PADDING) \
    X(LAYOUT,                COMMAND_CONFIG_LAYOUT,                "layout",                        ENUM,       SPACE,  LAYOUT,           LAYOUT) \
    X(WINDOW_GAP,            COMMAND_CONFIG_WINDOW_GAP,            "window_gap",                    INT,        SPACE,  INT,              WINDOW_GAP) \
    X(ACCORDION_PADDING,      COMMAND_CONFIG_ACCORDION_PADDING,      "accordion_padding",              INT,        SPACE,  NONNEGATIVE_INT,  ACCORDION_PADDING) \
    X(SPLIT_RATIO,           COMMAND_CONFIG_SPLIT_RATIO,           "split_ratio",                   FLOAT,      SPACE,  SPLIT_RATIO,      SPLIT_RATIO) \
    X(SPLIT_TYPE,            COMMAND_CONFIG_SPLIT_TYPE,            "split_type",                    ENUM,       SPACE,  SPLIT_TYPE,       SPLIT_TYPE) \
    X(AUTO_BALANCE,          COMMAND_CONFIG_AUTO_BALANCE,          "auto_balance",                  ENUM,       SPACE,  AUTO_BALANCE,     AUTO_BALANCE) \
    X(NATIVE_EDGE_RESIZE,    COMMAND_CONFIG_NATIVE_EDGE_RESIZE,    "native_edge_resize",            BOOL,       GLOBAL, BOOL,             NATIVE_EDGE_RESIZE) \
    X(MOUSE_MOD,             COMMAND_CONFIG_MOUSE_MOD,             "mouse_modifier",                ENUM,       GLOBAL, MOUSE_MOD,        MOUSE_MOD) \
    X(MOUSE_ACTION1,         COMMAND_CONFIG_MOUSE_ACTION1,         "mouse_action1",                 ENUM,       GLOBAL, MOUSE_ACTION,     MOUSE_ACTION1) \
    X(MOUSE_ACTION2,         COMMAND_CONFIG_MOUSE_ACTION2,         "mouse_action2",                 ENUM,       GLOBAL, MOUSE_ACTION,     MOUSE_ACTION2) \
    X(MOUSE_DROP_ACTION,     COMMAND_CONFIG_MOUSE_DROP_ACTION,     "mouse_drop_action",              ENUM,       GLOBAL, DROP_ACTION,      MOUSE_DROP_ACTION) \
    X(EXTERNAL_BAR,          COMMAND_CONFIG_EXTERNAL_BAR,          "external_bar",                  STRUCTURED, GLOBAL, EXTERNAL_BAR,     EXTERNAL_BAR) \
    X(SKIP_SPACE_ANIMATION,  COMMAND_CONFIG_SKIP_SPACE_ANIMATION,  "skip_window_focus_animation",   BOOL,       GLOBAL, BOOL,             SKIP_SPACE_ANIMATION)

#define RULE_FIELD_LIST(X) \
    X(LABEL,       ARGUMENT_RULE_KEY_LABEL,      "label",               "label",             "label",               METADATA) \
    X(ONE_SHOT,    ARGUMENT_RULE_ONE_SHOT,       "one_shot",            "--one-shot",        "one-shot",            FLAG) \
    X(APP,         ARGUMENT_RULE_KEY_APP,        "app",                 "app",               "app",                 MATCHER) \
    X(TITLE,       ARGUMENT_RULE_KEY_TITLE,      "title",               "title",             "title",               MATCHER) \
    X(ROLE,        ARGUMENT_RULE_KEY_ROLE,       "role",                "role",              "role",                MATCHER) \
    X(SUBROLE,     ARGUMENT_RULE_KEY_SUBROLE,    "subrole",             "subrole",           "subrole",             MATCHER) \
    X(DISPLAY,     ARGUMENT_RULE_KEY_DISPLAY,    "display",             "display",           "display",             SELECTOR) \
    X(SPACE,       ARGUMENT_RULE_KEY_SPACE,      "space",               "space",             "space",               SELECTOR) \
    X(FOLLOW,      DECLARATIVE_RULE_FOLLOW,      "follow",              NULL,                "follow_space",        SHARED_FLAG) \
    X(GRID,        ARGUMENT_RULE_KEY_GRID,       "grid",                "grid",              "grid",                EFFECT) \
    X(OPACITY,     ARGUMENT_RULE_KEY_OPACITY,    "opacity",             "opacity",           "opacity",             EFFECT) \
    X(MANAGE,      ARGUMENT_RULE_KEY_MANAGE,     "manage",              "manage",            "manage",              EFFECT) \
    X(STICKY,      ARGUMENT_RULE_KEY_STICKY,     "sticky",              "sticky",            "sticky",              EFFECT) \
    X(MFF,         ARGUMENT_RULE_KEY_MFF,        "mouse_follows_focus", "mouse_follows_focus", "mouse_follows_focus", EFFECT) \
    X(SUB_LAYER,   ARGUMENT_RULE_KEY_SUB_LAYER,  "sub_layer",           "sub-layer",         "sub-layer",           EFFECT) \
    X(FULLSCREEN,  ARGUMENT_RULE_KEY_FULLSCR,    "native_fullscreen",   "native-fullscreen", "native-fullscreen",   EFFECT) \
    X(SCRATCHPAD,  ARGUMENT_RULE_KEY_SCRATCHPAD, "scratchpad",          "scratchpad",        "scratchpad",           EFFECT)

enum declarative_value_kind { DECLARATIVE_VALUE_BOOL, DECLARATIVE_VALUE_ENUM, DECLARATIVE_VALUE_INT, DECLARATIVE_VALUE_UINT, DECLARATIVE_VALUE_FLOAT, DECLARATIVE_VALUE_STRUCTURED };
enum declarative_scope { DECLARATIVE_SCOPE_GLOBAL, DECLARATIVE_SCOPE_SPACE };
enum declarative_config_id {
#define X(id, compat, name, kind, scope, validator, setter) DECLARATIVE_CONFIG_##id,
    CONFIG_SETTING_LIST(X)
#undef X
    DECLARATIVE_CONFIG_COUNT
};
enum declarative_rule_field_id {
#define X(id, compat, json, cli, query, syntax) DECLARATIVE_RULE_FIELD_##id,
    RULE_FIELD_LIST(X)
#undef X
    DECLARATIVE_RULE_FIELD_COUNT
};
struct declarative_config_descriptor { const char *name; enum declarative_value_kind kind; enum declarative_scope scope; int validator_id; int setter_id; };
struct declarative_rule_field_descriptor { const char *json_name; const char *cli_name; const char *query_name; int syntax_id; };
extern const struct declarative_config_descriptor declarative_config_descriptors[DECLARATIVE_CONFIG_COUNT];
extern const struct declarative_rule_field_descriptor declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_COUNT];
int declarative_config_id_for_name(const char *name, size_t length);
int declarative_rule_field_for_json_name(const char *name, size_t length);
int declarative_rule_field_for_cli_name(const char *name, size_t length);

/* Generated compatibility names keep the imperative parser on the canonical inventory. */
#define X(id, compat, name, kind, scope, validator, setter) static char compat[] = name;
CONFIG_SETTING_LIST(X)
#undef X
#define X(id, compat, json, cli, query, syntax) static char *compat = (char *) cli;
RULE_FIELD_LIST(X)
#undef X

enum declarative_error_stage { DECLARATIVE_ERROR_NONE, DECLARATIVE_ERROR_PATH, DECLARATIVE_ERROR_OPEN, DECLARATIVE_ERROR_READ, DECLARATIVE_ERROR_FILE, DECLARATIVE_ERROR_ENCODING, DECLARATIVE_ERROR_JSONC, DECLARATIVE_ERROR_JSON, DECLARATIVE_ERROR_SCHEMA, DECLARATIVE_ERROR_REGEX, DECLARATIVE_ERROR_ALLOCATION, DECLARATIVE_ERROR_RUNTIME };
struct declarative_error { enum declarative_error_stage stage; size_t line; size_t column; char path[PATH_MAX * 4]; char json_path[256]; char message[160]; };
struct declarative_external_bar { int mode; int top_padding; int bottom_padding; };
struct declarative_value { bool present; union { bool boolean; int integer; uint32_t u32; float real; int enumeration; struct declarative_external_bar external_bar; }; };
struct declarative_matcher { char *pattern; bool exclude; };
struct declarative_space_policy { bool by_index; int index; char *uuid; struct declarative_value settings[DECLARATIVE_CONFIG_COUNT]; struct declarative_matcher scaffold[DECLARATIVE_CONFIG_MAX_SCAFFOLD_SLOTS]; size_t scaffold_count; bool scaffold_present; };
struct declarative_mode_model { char *name; struct declarative_space_policy *spaces; size_t space_count; };
struct declarative_rule_model { char *label; bool one_shot; bool matcher_present[4]; struct declarative_matcher matchers[4]; bool display_present, space_present, follow_present, follow; int display_index, space_index; bool grid_present; unsigned grid[6]; bool opacity_present; float opacity; bool manage_present, manage, sticky_present, sticky, mff_present, mff, layer_present; int layer; bool fullscreen_present, fullscreen; char *scratchpad; };
struct declarative_config_model { int version; struct declarative_value config[DECLARATIVE_CONFIG_COUNT]; struct declarative_space_policy *spaces; size_t space_count; struct declarative_mode_model *modes; size_t mode_count; char *active_mode; struct declarative_rule_model *rules; size_t rule_count; size_t allocated_bytes; void (*allocator_free)(void *); };

enum declarative_file_result { DECLARATIVE_FILE_OK, DECLARATIVE_FILE_ABSENT, DECLARATIVE_FILE_ERROR };
bool declarative_config_validate_path(const char *path, struct declarative_error *error);
enum declarative_file_result declarative_config_parse_optional_file(const char *path, struct declarative_config_model **model, struct declarative_error *error);
bool declarative_config_parse_file(const char *path, struct declarative_config_model **model, struct declarative_error *error);
bool declarative_config_parse_bytes(const uint8_t *bytes, size_t length, const char *public_path, struct declarative_config_model **model, struct declarative_error *error);
void declarative_config_destroy(struct declarative_config_model *model);

struct declarative_file_ops { int (*open_fn)(const char *, int); ssize_t (*read_fn)(int, void *, size_t); int (*fstat_fn)(int, struct stat *); int (*close_fn)(int); };
struct declarative_allocator_ops { void *(*calloc_fn)(size_t, size_t); void (*free_fn)(void *); };
#ifdef TESTS
void declarative_config_set_file_ops(const struct declarative_file_ops *ops);
void declarative_config_set_allocator_ops(const struct declarative_allocator_ops *ops);
size_t declarative_config_get_structural_traversal_count(void);
bool declarative_model_budget_charge(size_t *used, size_t amount);
bool declarative_jsonc_normalize(uint8_t *bytes, size_t length, struct declarative_error *error);
bool declarative_number_to_int(NSNumber *number, int minimum, int maximum, int *value);
bool declarative_number_to_uint(NSNumber *number, uint32_t minimum, uint32_t maximum, uint32_t *value);
bool declarative_number_to_float(NSNumber *number, bool zero_allowed, float *value);
#endif

#endif
