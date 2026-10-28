#ifndef DECLARATIVE_RUNTIME_H
#define DECLARATIVE_RUNTIME_H

#include "declarative_runtime_types.h"
#include "declarative_config.h"

struct space_manager;
struct view;

struct declarative_runtime_state
{
    struct declarative_runtime_cell global[DECLARATIVE_CONFIG_COUNT];
};

struct declarative_prepared_settings
{
    struct declarative_value values[DECLARATIVE_CONFIG_COUNT];
};

enum declarative_apply_result
{
    DECLARATIVE_APPLY_OK,
    DECLARATIVE_APPLY_INVALID_VALUE,
    DECLARATIVE_APPLY_INVALID_SCOPE,
    DECLARATIVE_APPLY_MISSING_SPACE,
    DECLARATIVE_APPLY_NON_USER_SPACE,
    DECLARATIVE_APPLY_LAYOUT_FAILED,
    DECLARATIVE_APPLY_SIP_DENIED,
    DECLARATIVE_APPLY_SCREEN_CAPTURE_DENIED
};

struct declarative_animation_permission_ops
{
    bool (*sip_friendly)(void);
    bool (*screen_capture_preflight)(void);
    void (*screen_capture_request)(void);
};

struct declarative_runtime_operation_ops
{
    bool (*space_is_user)(uint64_t);
    bool (*set_focus_follows_mouse)(void *, int);
    bool (*set_layout_for_all_spaces)(void *, int);
    bool (*set_layout_for_view)(void *, void *, int);
    void (*set_single_window_width_ratio_for_all_spaces)(void *, float);
    void (*set_split_ratio_for_all_spaces)(void *, float);
    void (*set_split_type_for_all_spaces)(void *, int);
    void (*set_auto_balance_for_all_spaces)(void *, uint32_t);
    void (*update_and_flush_space)(void *);
    void (*set_global_space_geometry)(void *, enum declarative_config_id, int);
    void (*mark_menu_bar_dirty)(uint32_t);
    void (*set_window_opacity_enabled)(void *, bool);
    void (*set_menubar_opacity)(void *, float);
    void (*set_active_window_opacity)(void *, float);
    void (*set_normal_window_opacity)(void *, float);
    void (*set_active_window_highlight_enabled)(void *, bool);
    void (*refresh_active_window_highlight)(void *);
    void (*refresh_insert_feedback)(void);
    void (*set_shadow)(void *, int);
    void (*set_menu_enabled)(int);
    void (*set_menu_interface)(int);
    void (*set_menu_thumbnails)(int);
    void (*set_menu_style)(int);
    void (*set_menu_space_filter)(int);
    void (*set_mouse_modifier)(void *, int);
    void (*set_mouse_action1)(void *, int);
    void (*set_mouse_action2)(void *, int);
    void (*set_mouse_drop_action)(void *, int);
    void (*invalidate_spaces)(void *);
};

extern struct declarative_runtime_state g_declarative_runtime_state;

int declarative_space_field_for_id(enum declarative_config_id id);
void declarative_runtime_reset_ownership(void);
enum declarative_apply_result declarative_apply_runtime_setting(enum declarative_config_id id, const struct declarative_value *value, uint64_t sid);
enum declarative_apply_result declarative_apply_runtime_setting_for_view(struct space_manager *manager, struct view *view, uint64_t expected_sid, enum declarative_config_id id, const struct declarative_value *value);
enum declarative_apply_result declarative_apply_runtime_layout_for_space(uint64_t sid, enum view_type layout);
enum declarative_apply_result declarative_apply_runtime_layout_for_view(struct space_manager *manager, struct view *view, uint64_t expected_sid, enum view_type layout);
bool declarative_prepare_setting(struct declarative_prepared_settings *prepared, enum declarative_config_id id, const struct declarative_value *value, enum declarative_apply_result *result);
void declarative_runtime_request_animation_permission(void);
#ifdef TESTS
void declarative_runtime_set_animation_permission_ops(const struct declarative_animation_permission_ops *ops);
void declarative_runtime_reset_animation_permission_ops(void);
void declarative_runtime_set_operation_ops(const struct declarative_runtime_operation_ops *ops);
void declarative_runtime_reset_operation_ops(void);
#endif

#endif
