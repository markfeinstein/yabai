#ifndef HOTKEY_H
#define HOTKEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define HOTKEY_MAX_BINDINGS 512u
#define HOTKEY_MAX_INPUT_MODES 32u
#define HOTKEY_MAX_CASES 16u
#define HOTKEY_MAX_ACTIONS 16u
#define HOTKEY_MAX_SERVICE_LABEL_BYTES 127u
#define HOTKEY_MAX_COMMAND_BYTES 1024u

#define HOTKEY_TRANSITION_NONE    (-2)
#define HOTKEY_TRANSITION_DEFAULT (-1)

#define HOTKEY_MODIFIER_COMMAND       (1u << 0)
#define HOTKEY_MODIFIER_CONTROL       (1u << 1)
#define HOTKEY_MODIFIER_OPTION        (1u << 2)
#define HOTKEY_MODIFIER_SHIFT         (1u << 3)
#define HOTKEY_MODIFIER_FN            (1u << 4)
#define HOTKEY_MODIFIER_LEFT_COMMAND  (1u << 5)
#define HOTKEY_MODIFIER_RIGHT_COMMAND (1u << 6)
#define HOTKEY_MODIFIER_LEFT_CONTROL  (1u << 7)
#define HOTKEY_MODIFIER_RIGHT_CONTROL (1u << 8)
#define HOTKEY_MODIFIER_LEFT_OPTION   (1u << 9)
#define HOTKEY_MODIFIER_RIGHT_OPTION  (1u << 10)
#define HOTKEY_MODIFIER_LEFT_SHIFT    (1u << 11)
#define HOTKEY_MODIFIER_RIGHT_SHIFT   (1u << 12)
#define HOTKEY_MODIFIER_PRIMARY_MASK  (HOTKEY_MODIFIER_COMMAND | HOTKEY_MODIFIER_CONTROL | \
                                       HOTKEY_MODIFIER_OPTION | HOTKEY_MODIFIER_SHIFT | \
                                       HOTKEY_MODIFIER_FN)
#define HOTKEY_MODIFIER_HYPER_MASK    (HOTKEY_MODIFIER_COMMAND | HOTKEY_MODIFIER_CONTROL | \
                                       HOTKEY_MODIFIER_OPTION | HOTKEY_MODIFIER_SHIFT)
#define HOTKEY_MODIFIER_SIDED_MASK    (HOTKEY_MODIFIER_LEFT_COMMAND | HOTKEY_MODIFIER_RIGHT_COMMAND | \
                                       HOTKEY_MODIFIER_LEFT_CONTROL | HOTKEY_MODIFIER_RIGHT_CONTROL | \
                                       HOTKEY_MODIFIER_LEFT_OPTION | HOTKEY_MODIFIER_RIGHT_OPTION | \
                                       HOTKEY_MODIFIER_LEFT_SHIFT | HOTKEY_MODIFIER_RIGHT_SHIFT)

enum hotkey_disposition
{
    HOTKEY_DISPOSITION_CONSUME,
    HOTKEY_DISPOSITION_CONSUME_ONLY,
    HOTKEY_DISPOSITION_PASS_THROUGH,
    HOTKEY_DISPOSITION_REMAP
};

enum hotkey_direction
{
    HOTKEY_DIRECTION_WEST,
    HOTKEY_DIRECTION_SOUTH,
    HOTKEY_DIRECTION_NORTH,
    HOTKEY_DIRECTION_EAST
};

enum hotkey_action_type
{
    HOTKEY_ACTION_FOCUS_WINDOW,
    HOTKEY_ACTION_WARP_WINDOW,
    HOTKEY_ACTION_RESIZE_WINDOW,
    HOTKEY_ACTION_CHANGE_WINDOW_RATIO,
    HOTKEY_ACTION_ADAPTIVE_RESIZE,
    HOTKEY_ACTION_TOGGLE_FLOATING,
    HOTKEY_ACTION_TOGGLE_ZOOM_FULLSCREEN,
    HOTKEY_ACTION_ROTATE_SPACE,
    HOTKEY_ACTION_SET_SPACE_LAYOUT,
    HOTKEY_ACTION_BALANCE_SPACE,
    HOTKEY_ACTION_FOCUS_SPACE,
    HOTKEY_ACTION_FOCUS_OCCUPIED_SPACE,
    HOTKEY_ACTION_MOVE_WINDOW_TO_SPACE,
    HOTKEY_ACTION_CLOSE_WINDOW,
    HOTKEY_ACTION_CLOSE_OTHER_WINDOWS,
    HOTKEY_ACTION_CYCLE_SPACE_LAYOUT,
    HOTKEY_ACTION_RELOAD_CONFIGURATION,
    HOTKEY_ACTION_RESTART_SERVICE,
    HOTKEY_ACTION_YABAI_COMMAND,
    HOTKEY_ACTION_SYNTHESIZE_KEY
};

enum hotkey_adaptive_resize
{
    HOTKEY_ADAPTIVE_RESIZE_SHRINK,
    HOTKEY_ADAPTIVE_RESIZE_GROW,
    HOTKEY_ADAPTIVE_RESIZE_WEST,
    HOTKEY_ADAPTIVE_RESIZE_SOUTH,
    HOTKEY_ADAPTIVE_RESIZE_NORTH,
    HOTKEY_ADAPTIVE_RESIZE_EAST
};

enum hotkey_space_layout
{
    HOTKEY_SPACE_LAYOUT_BSP,
    HOTKEY_SPACE_LAYOUT_STACK,
    HOTKEY_SPACE_LAYOUT_FLOAT,
    HOTKEY_SPACE_LAYOUT_ACCORDION_HORIZONTAL,
    HOTKEY_SPACE_LAYOUT_ACCORDION_VERTICAL
};

enum hotkey_occupied_space_direction
{
    HOTKEY_OCCUPIED_SPACE_PREVIOUS,
    HOTKEY_OCCUPIED_SPACE_NEXT
};

struct hotkey_keystroke
{
    uint16_t keycode;
    uint16_t modifiers;
};

struct hotkey_action
{
    enum hotkey_action_type type;
    union {
        struct { enum hotkey_direction direction; bool robust; } focus_window;
        struct { enum hotkey_direction direction; } warp_window;
        struct { uint8_t handle; float dx; float dy; } resize_window;
        struct { float delta; } change_window_ratio;
        struct { enum hotkey_adaptive_resize direction; } adaptive_resize;
        struct { int degrees; } rotate_space;
        struct { enum hotkey_space_layout layout; } set_space_layout;
        struct { int index; } space;
        struct { enum hotkey_occupied_space_direction direction; } occupied_space;
        struct { char label[HOTKEY_MAX_SERVICE_LABEL_BYTES + 1]; } restart_service;
        struct { uint16_t length; char bytes[HOTKEY_MAX_COMMAND_BYTES]; } yabai_command;
        struct hotkey_keystroke synthesize_key;
    } data;
};

struct hotkey_application_condition
{
    char *bundle_id;
    char *name;
};

struct hotkey_window_condition
{
    char *title;
    char *title_prefix;
};

struct hotkey_binding_case
{
    struct hotkey_application_condition application;
    struct hotkey_window_condition window;
    enum hotkey_disposition disposition;
    struct hotkey_action *actions;
    size_t action_count;
    char *transition_name;
    int transition_mode;
};

struct hotkey_binding
{
    struct hotkey_keystroke keystroke;
    bool repeat;
    struct hotkey_binding_case *cases;
    size_t case_count;
};

struct hotkey_input_mode
{
    char *name;
    bool capture_unbound;
    uint32_t timeout_ms;
    struct hotkey_binding *bindings;
    size_t binding_count;
};

struct hotkey_config_model
{
    bool present;
    bool enabled;
    struct hotkey_binding *bindings;
    size_t binding_count;
    struct hotkey_input_mode *modes;
    size_t mode_count;
};

struct hotkey_match_result
{
    bool matched;
    bool capture_unbound;
    bool repeat;
    enum hotkey_disposition disposition;
    const struct hotkey_action *actions;
    size_t action_count;
    int transition_mode;
};

void hotkey_manager_install_config(const struct hotkey_config_model *config);
bool hotkey_manager_begin(void);
void hotkey_manager_end(void);
void hotkey_manager_update_frontmost_application(pid_t pid, const char *name);
void hotkey_manager_update_focused_window_title(const char *title);
bool hotkey_post_synthetic_keystroke(struct hotkey_keystroke keystroke);
void hotkey_action_job_destroy(void *context);
bool hotkey_action_execute_sequence(const struct hotkey_action *actions, size_t count);
bool hotkey_manager_copy_active_mode(char *buffer, size_t capacity);

#ifdef TESTS
bool hotkey_test_match(const struct hotkey_config_model *config, int active_mode,
                       uint16_t keycode, uint16_t modifiers,
                       const char *bundle_id, const char *name, const char *window_title,
                       struct hotkey_match_result *result);
int hotkey_test_active_mode(void);
void hotkey_test_set_action_executor(bool (*executor)(const struct hotkey_action *, void *), void *context);
void hotkey_test_set_action_poster(bool (*poster)(void *));
#endif

#endif
