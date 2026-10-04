#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#define kCPSAllWindows    0x100
#define kCPSUserGenerated 0x200
#define kCPSNoWindows     0x400

enum window_op_error
{
    WINDOW_OP_ERROR_SUCCESS,
    WINDOW_OP_ERROR_INVALID_SRC_VIEW,
    WINDOW_OP_ERROR_INVALID_SRC_NODE,
    WINDOW_OP_ERROR_INVALID_DST_VIEW,
    WINDOW_OP_ERROR_INVALID_DST_NODE,
    WINDOW_OP_ERROR_INVALID_OPERATION,
    WINDOW_OP_ERROR_SAME_WINDOW,
    WINDOW_OP_ERROR_CANT_MINIMIZE,
    WINDOW_OP_ERROR_ALREADY_MINIMIZED,
    WINDOW_OP_ERROR_MINIMIZE_FAILED,
    WINDOW_OP_ERROR_NOT_MINIMIZED,
    WINDOW_OP_ERROR_DEMINIMIZE_FAILED,
    WINDOW_OP_ERROR_MAX_STACK,
    WINDOW_OP_ERROR_SAME_STACK,
};

enum managed_window_result
{
    MANAGED_WINDOW_FAILED,
    MANAGED_WINDOW_UNMANAGED,
    MANAGED_WINDOW_SUCCESS
};

enum purify_mode
{
    PURIFY_DISABLED,
    PURIFY_MANAGED,
    PURIFY_ALWAYS
};

static const char *purify_mode_str[] =
{
    "on",
    "float",
    "off"
};

enum ffm_mode
{
    FFM_DISABLED,
    FFM_AUTOFOCUS,
    FFM_AUTORAISE
};

typedef uint32_t ffm_atomic_mode;
_Static_assert(FFM_AUTORAISE <= UINT32_MAX, "ffm mode must fit its atomic storage");

static const char *ffm_mode_str[] =
{
    "off",
    "autofocus",
    "autoraise"
};

enum window_origin_mode
{
    WINDOW_ORIGIN_DEFAULT,
    WINDOW_ORIGIN_FOCUSED,
    WINDOW_ORIGIN_CURSOR
};

static const char *window_origin_mode_str[] =
{
    "default",
    "focused",
    "cursor"
};

enum window_focus_fallback
{
    WINDOW_FOCUS_FALLBACK_OFF,
    WINDOW_FOCUS_FALLBACK_RECENT
};

static const char *window_focus_fallback_str[] =
{
    "off",
    "recent"
};

struct scratchpad
{
    char *label;
    struct window *window;
};

#define INSERT_FEEDBACK_RADIUS_DEFAULT 9.0f
#define INSERT_FEEDBACK_WIDTH_DEFAULT 2.0f

struct window_manager
{
    AXUIElementRef system_element;
    struct table application;
    struct table window;
    struct table managed_window;
    struct table window_lost_focused_event;
    struct table application_lost_front_switched_event;
    struct table window_animations_table;
    struct table insert_feedback;
    pthread_mutex_t window_animations_lock;
    struct rule *rules;
    struct application **applications_to_refresh;
    int application_refresh_cursor;
    uint32_t focused_window_id;
    ProcessSerialNumber focused_window_psn;
    uint32_t last_window_id;
    bool enable_mff;
    enum ffm_mode configured_ffm_mode;
    ffm_atomic_mode effective_ffm_mode;
    uint32_t ffm_menu_suspend_count;
    enum purify_mode purify_mode;
    enum window_origin_mode window_origin_mode;
    enum window_focus_fallback window_focus_fallback;
    bool enable_window_opacity;
    float menubar_opacity;
    float active_window_opacity;
    float normal_window_opacity;
    float window_opacity_duration;
    float window_animation_duration;
    int window_animation_easing;
    struct rgba_color insert_feedback_color;
    float insert_feedback_radius;
    float insert_feedback_width;
    struct scratchpad *scratchpad_window;
};

void window_manager_query_window_rules(FILE *rsp);
void window_manager_query_windows_for_spaces(FILE *rsp, uint64_t *space_list, int space_count, uint64_t flags);
void window_manager_query_windows_for_display(FILE *rsp, uint32_t did, uint64_t flags);
void window_manager_query_windows_for_displays(FILE *rsp, uint64_t flags);
bool window_manager_rule_matches_window(struct rule *rule, struct window *window, char *window_title, char *window_role, char *window_subrole);
void window_manager_apply_manage_rule_effects_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, struct rule_effects *effects);
void window_manager_apply_rule_effects_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, struct rule_effects *effects);
void window_manager_apply_manage_rules_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, char *window_title, char *window_role, char *window_subrole, bool one_shot_rules, pid_t one_shot_owner_pid);
void window_manager_apply_rules_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, char *window_title, char *window_role, char *window_subrole, bool one_shot_rules, pid_t one_shot_owner_pid);
void window_manager_center_mouse(struct window_manager *wm, struct window *window);
bool window_manager_is_window_eligible(struct window *window);
bool window_manager_should_manage_window(struct window *window);
void window_manager_tile_window(struct window_manager *wm, struct window *window);
void window_manager_move_window(struct window *window, float x, float y);
void window_manager_resize_window(struct window *window, float width, float height);
enum window_op_error window_manager_adjust_window_ratio(struct window_manager *wm, struct window *window, int action, float ratio);
void window_manager_animate_window(struct window_capture capture);
void window_manager_animate_window_list(struct window_capture *window_list, int window_count);
void window_manager_set_window_frame(struct window *window, float x, float y, float width, float height);
int window_manager_find_rank_of_window_in_list(uint32_t wid, uint32_t *window_list, int window_count);
struct window *window_manager_find_window_on_space_by_rank_filtering_window(struct window_manager *wm, uint64_t sid, int rank, uint32_t filter_wid);
struct window *window_manager_find_window_at_point_filtering_window(struct window_manager *wm, CGPoint point, uint32_t filter_wid);
struct window *window_manager_find_window_at_point(struct window_manager *wm, CGPoint point);
struct window *window_manager_find_window_below_cursor(struct window_manager *wm);
struct window *window_manager_find_closest_managed_window_in_direction(struct window_manager *wm, struct window *window, int direction);
struct window *window_manager_find_prev_managed_window(struct space_manager *sm, struct window_manager *wm, struct window *window);
struct window *window_manager_find_next_managed_window(struct space_manager *sm, struct window_manager *wm, struct window *window);
struct window *window_manager_find_first_managed_window(struct space_manager *sm, struct window_manager *wm);
struct window *window_manager_find_last_managed_window(struct space_manager *sm, struct window_manager *wm);
struct window *window_manager_find_recent_managed_window(struct window_manager *wm);
bool window_manager_window_is_focus_fallback_eligible(struct window *window);
struct window *window_manager_find_prev_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window);
struct window *window_manager_find_next_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window);
struct window *window_manager_find_first_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window);
struct window *window_manager_find_last_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window);
struct window *window_manager_find_recent_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window);
struct window *window_manager_find_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window, int index);
struct window *window_manager_find_largest_managed_window(struct space_manager *sm, struct window_manager *wm);
struct window *window_manager_find_smallest_managed_window(struct space_manager *sm, struct window_manager *wm);
struct window *window_manager_find_sibling_for_managed_window(struct window_manager *wm, struct window *window);
struct window *window_manager_find_first_nephew_for_managed_window(struct window_manager *wm, struct window *window);
struct window *window_manager_find_second_nephew_for_managed_window(struct window_manager *wm, struct window *window);
struct window *window_manager_find_uncle_for_managed_window(struct window_manager *wm, struct window *window);
struct window *window_manager_find_first_cousin_for_managed_window(struct window_manager *wm, struct window *window);
struct window *window_manager_find_second_cousin_for_managed_window(struct window_manager *wm, struct window *window);
void window_manager_focus_window_without_raise(ProcessSerialNumber *window_psn, uint32_t window_id);
void window_manager_focus_window_with_raise(ProcessSerialNumber *window_psn, uint32_t window_id, AXUIElementRef window_ref);
struct window *window_manager_focused_window(struct window_manager *wm);
struct application *window_manager_focused_application(struct window_manager *wm);
struct view *window_manager_find_managed_window(struct window_manager *wm, struct window *window);
void window_manager_remove_managed_window(struct window_manager *wm, uint32_t wid);
bool window_manager_add_managed_window(struct window_manager *wm, struct window *window, struct view *view);
bool window_manager_transfer_managed_window(struct window_manager *wm, struct window *window,
                                            struct view *src_view, struct view *dst_view,
                                            uint32_t insertion_point);
enum managed_window_result window_manager_tile_window_on_space(struct space_manager *sm, struct window_manager *wm,
                                                                struct window *window, uint64_t sid, uint32_t insertion_point,
                                                                bool flush);
void window_manager_window_did_order_out(struct window_manager *wm, struct window *window);
void window_manager_window_did_order_in(struct window_manager *wm, struct window *window, uint64_t sid);
uint64_t window_manager_find_lost_front_switched_event(struct window_manager *wm, ProcessSerialNumber *psn);
void window_manager_remove_lost_front_switched_event(struct window_manager *wm, ProcessSerialNumber *psn);
void window_manager_add_lost_front_switched_event(struct window_manager *wm, ProcessSerialNumber *psn, uint64_t generation);
bool window_manager_find_lost_focused_event(struct window_manager *wm, uint32_t window_id);
void window_manager_remove_lost_focused_event(struct window_manager *wm, uint32_t window_id);
void window_manager_add_lost_focused_event(struct window_manager *wm, uint32_t window_id);
uint32_t window_manager_ax_window_id(AXUIElementRef window_ref);
struct window *window_manager_find_window(struct window_manager *wm, uint32_t window_id);
void window_manager_remove_window(struct window_manager *wm, uint32_t window_id);
bool window_manager_add_window(struct window_manager *wm, struct window *window);
struct application *window_manager_find_application(struct window_manager *wm, pid_t pid);
void window_manager_remove_application(struct window_manager *wm, pid_t pid);
bool window_manager_add_application(struct window_manager *wm, struct application *application);
struct window **window_manager_find_application_windows(struct window_manager *wm, struct application *application, int *window_count);
enum window_op_error window_manager_move_window_relative(struct window_manager *wm, struct window *window, int type, float dx, float dy);
void window_manager_resize_window_relative_internal(struct window *window, CGRect frame, int direction, float dx, float dy, bool animate);
enum window_op_error window_manager_resize_window_relative(struct window_manager *wm, struct window *window, int direction, float dx, float dy, bool animate);
void window_manager_set_purify_mode(struct window_manager *wm, enum purify_mode mode);
void window_manager_set_menubar_opacity(struct window_manager *wm, float opacity);
void window_manager_set_active_window_opacity(struct window_manager *wm, float opacity);
void window_manager_set_normal_window_opacity(struct window_manager *wm, float opacity);
void window_manager_set_window_opacity_enabled(struct window_manager *wm, bool enabled);
bool window_manager_set_opacity(struct window_manager *wm, struct window *window, float opacity);
void window_manager_set_window_opacity(struct window_manager *wm, struct window *window, float opacity);
bool window_manager_set_focus_follows_mouse(struct window_manager *wm, enum ffm_mode mode);
enum ffm_mode window_manager_effective_ffm_mode(const struct window_manager *wm);
void window_manager_recompute_effective_ffm_mode(struct window_manager *wm);
void window_manager_ffm_menu_opened(struct window_manager *wm);
void window_manager_ffm_menu_closed(struct window_manager *wm);
enum window_op_error window_manager_set_window_insertion(struct space_manager *sm, struct window *window, int direction);
enum window_op_error window_manager_stack_window(struct space_manager *sm, struct window_manager *wm, struct window *a, struct window *b);
enum window_op_error window_manager_warp_window(struct space_manager *sm, struct window_manager *wm, struct window *a, struct window *b);
enum window_op_error window_manager_swap_window(struct space_manager *sm, struct window_manager *wm, struct window *a, struct window *b);
enum window_op_error window_manager_minimize_window(struct window *window);
enum window_op_error window_manager_deminimize_window(struct window *window);
bool window_manager_close_window(struct window *window);
void window_manager_send_window_to_space(struct space_manager *sm, struct window_manager *wm, struct window *window, uint64_t sid, bool moved_by_rule);
struct window_created_retry
{
    AXUIElementRef window_ref;
    ProcessSerialNumber application_psn;
    pid_t application_pid;
    bool one_shot_rules;
    pid_t one_shot_owner_pid;
};

struct window_created_retry *window_manager_create_window_created_retry(AXUIElementRef window_ref, struct application *application, bool one_shot_rules, pid_t one_shot_owner_pid);
void window_manager_destroy_window_created_retry(struct window_created_retry *retry);
void window_manager_schedule_window_created_retry(AXUIElementRef window_ref, struct application *application, bool one_shot_rules, pid_t one_shot_owner_pid);
struct application *window_manager_resolve_window_created_retry(struct window_manager *wm, struct window_created_retry *retry);
pid_t window_manager_window_created_retry_owner(struct application *application, struct window_created_retry *retry);
bool window_manager_window_created_retry_uses_one_shots(struct application *application, struct window_created_retry *retry);
struct window *window_manager_create_and_add_window(struct space_manager *sm, struct window_manager *wm, struct application *application, AXUIElementRef window_ref, uint32_t window_id, bool one_shot_rules, pid_t one_shot_owner_pid, bool *retry);
struct window **window_manager_add_application_windows(struct space_manager *sm, struct window_manager *wm, struct application *application, int *count);
bool window_manager_add_existing_application_windows(struct space_manager *sm, struct window_manager *wm, struct application *application, int refresh_index, struct window ***created_windows, int *created_count);
bool window_manager_remove_application_refresh(struct window_manager *wm, struct application *application);
void window_manager_begin_inactive_probe_pass(int budget);
int window_manager_inactive_probe_budget_remaining(void);
enum window_op_error window_manager_apply_grid(struct space_manager *sm, struct window_manager *wm, struct window *window, unsigned r, unsigned c, unsigned x, unsigned y, unsigned w, unsigned h);
void window_manager_purify_window(struct window_manager *wm, struct window *window);
void window_manager_make_window_floating(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_float, bool force);
void window_manager_make_window_sticky(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_sticky);
void window_manager_adjust_layer(struct window *window, int layer);
bool window_manager_set_window_layer(struct window *window, int layer);
void window_manager_toggle_window_shadow(struct window *window);
void window_manager_toggle_window_zoom_parent(struct window_manager *wm, struct window *window);
void window_manager_toggle_window_zoom_fullscreen(struct window_manager *wm, struct window *window);
void window_manager_toggle_window_windowed_fullscreen(struct window *window);
void window_manager_toggle_window_native_fullscreen(struct window *window);
void window_manager_toggle_window_expose(struct window *window);
void window_manager_toggle_window_pip(struct space_manager *sm, struct window *window);
bool window_manager_toggle_scratchpad_window_by_label(struct window_manager *wm, char *label);
bool window_manager_toggle_scratchpad_window(struct window_manager *wm, struct window *window, int forced_mode);
bool window_manager_set_scratchpad_for_window(struct window_manager *wm, struct window *window, char *label);
bool window_manager_remove_scratchpad_for_window(struct window_manager *wm, struct window *window, bool unfloat);
void window_manager_scratchpad_recover_windows(void);
void window_manager_wait_for_native_fullscreen_transition(struct window *window);
bool window_manager_reconcile_windows_on_space_with_storage(struct window_manager *wm, struct view *view, uint32_t *window_list, int window_count, bool inventory_succeeded, struct view **mutated_views, int mutated_capacity);
bool window_manager_reconcile_windows_on_space(struct window_manager *wm, struct view *view, uint32_t *window_list, int window_count, bool inventory_succeeded);
void window_manager_validate_and_check_for_windows_on_space(struct space_manager *sm, struct window_manager *wm, uint64_t sid);
void window_manager_correct_for_mission_control_changes(struct space_manager *sm, struct window_manager *wm);
bool window_manager_handle_display_add_and_remove(struct space_manager *sm, struct window_manager *wm, uint32_t did);
void window_manager_begin(struct space_manager *sm, struct window_manager *wm);
void window_manager_init(struct window_manager *wm);

#endif
