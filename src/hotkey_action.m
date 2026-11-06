extern struct window_manager g_window_manager;
extern struct space_manager g_space_manager;
extern int g_connection;
extern char **environ;

#ifdef TESTS
static bool (*hotkey_test_action_executor)(const struct hotkey_action *, void *);
static void *hotkey_test_action_context;
void hotkey_test_set_action_executor(bool (*executor)(const struct hotkey_action *, void *), void *context)
{
    hotkey_test_action_executor = executor;
    hotkey_test_action_context = context;
}
#endif

static int hotkey_window_direction(enum hotkey_direction direction)
{
    switch (direction) {
    case HOTKEY_DIRECTION_WEST: return DIR_WEST;
    case HOTKEY_DIRECTION_SOUTH: return DIR_SOUTH;
    case HOTKEY_DIRECTION_NORTH: return DIR_NORTH;
    case HOTKEY_DIRECTION_EAST: return DIR_EAST;
    }
    return 0;
}

static bool hotkey_focus_window_direction(enum hotkey_direction direction)
{
    struct window *window = window_manager_focused_window(&g_window_manager);
    if (!window) return false;
    struct window *target = window_manager_find_closest_managed_window_in_direction(
        &g_window_manager, window, hotkey_window_direction(direction));
    if (!target) return false;
    window_manager_focus_window_with_raise(&target->application->psn, target->id, target->ref);
    return true;
}

static bool hotkey_directional_candidate_is_better(double primary, double secondary, double distance2,
                                                    double best_primary, double best_secondary, double best_distance2)
{
    return primary < best_primary ||
           (primary == best_primary && (secondary < best_secondary ||
            (secondary == best_secondary && distance2 < best_distance2)));
}

static bool hotkey_robust_focus_window_direction(enum hotkey_direction direction)
{
    struct window *focused = window_manager_focused_window(&g_window_manager);
    if (!focused) return false;

    struct window *managed = window_manager_find_closest_managed_window_in_direction(
        &g_window_manager, focused, hotkey_window_direction(direction));
    if (managed) {
        window_manager_focus_window_with_raise(&managed->application->psn, managed->id, managed->ref);
        @autoreleasepool {
            NSRunningApplication *application =
                [NSRunningApplication runningApplicationWithProcessIdentifier:managed->application->pid];
            if (application) (void)[application activateWithOptions:0];
        }
        return true;
    }

    uint32_t before_id = focused->id;
    uint64_t sid = space_manager_active_space();
    int window_count = 0;
    uint32_t *window_list = sid ? space_window_list(sid, &window_count, false) : NULL;
    if (!window_list || window_count <= 0) return false;

    CGRect focused_frame = focused->frame;
    double focused_x = CGRectGetMidX(focused_frame);
    double focused_y = CGRectGetMidY(focused_frame);
    struct window *best = NULL;
    double best_primary = INFINITY;
    double best_secondary = INFINITY;
    double best_distance2 = INFINITY;
    for (int i = 0; i < window_count; ++i) {
        struct window *candidate = window_manager_find_window(&g_window_manager, window_list[i]);
        if (!candidate || candidate->id == before_id || window_check_flag(candidate, WINDOW_MINIMIZE)) continue;
        double dx = CGRectGetMidX(candidate->frame) - focused_x;
        double dy = CGRectGetMidY(candidate->frame) - focused_y;
        bool half_plane = (direction == HOTKEY_DIRECTION_WEST && dx < 0.0) ||
                          (direction == HOTKEY_DIRECTION_EAST && dx > 0.0) ||
                          (direction == HOTKEY_DIRECTION_NORTH && dy < 0.0) ||
                          (direction == HOTKEY_DIRECTION_SOUTH && dy > 0.0);
        if (!half_plane) continue;
        double primary = direction == HOTKEY_DIRECTION_WEST || direction == HOTKEY_DIRECTION_EAST ? fabs(dx) : fabs(dy);
        double secondary = direction == HOTKEY_DIRECTION_WEST || direction == HOTKEY_DIRECTION_EAST ? fabs(dy) : fabs(dx);
        double distance2 = dx * dx + dy * dy;
        if (!best || hotkey_directional_candidate_is_better(primary, secondary, distance2,
                                                             best_primary, best_secondary, best_distance2)) {
            best = candidate;
            best_primary = primary;
            best_secondary = secondary;
            best_distance2 = distance2;
        }
    }
    if (!best) return false;
    window_manager_focus_window_with_raise(&best->application->psn, best->id, best->ref);
    @autoreleasepool {
        NSRunningApplication *application =
            [NSRunningApplication runningApplicationWithProcessIdentifier:best->application->pid];
        if (application) (void)[application activateWithOptions:0];
    }
    return true;
}

static bool hotkey_warp_window_direction(enum hotkey_direction direction)
{
    struct window *window = window_manager_focused_window(&g_window_manager);
    if (!window) return false;
    struct window *target = window_manager_find_closest_managed_window_in_direction(
        &g_window_manager, window, hotkey_window_direction(direction));
    if (!target) return false;
    if (window_manager_warp_window(&g_space_manager, &g_window_manager, window, target) != WINDOW_OP_ERROR_SUCCESS)
        return false;
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_SPACE);
    return true;
}

static bool hotkey_resize_window(uint8_t handle, float dx, float dy)
{
    struct window *window = window_manager_focused_window(&g_window_manager);
    return window && window_manager_resize_window_relative(&g_window_manager, window, handle, dx, dy, true) ==
                     WINDOW_OP_ERROR_SUCCESS;
}

static bool hotkey_change_window_ratio(float delta)
{
    struct window *window = window_manager_focused_window(&g_window_manager);
    return window && window_manager_adjust_window_ratio(&g_window_manager, window, TYPE_REL, delta) ==
                     WINDOW_OP_ERROR_SUCCESS;
}

static bool hotkey_adaptive_resize(enum hotkey_adaptive_resize direction)
{
    switch (direction) {
    case HOTKEY_ADAPTIVE_RESIZE_SHRINK:
        return hotkey_change_window_ratio(-0.05f) || hotkey_resize_window(HANDLE_BOTTOM | HANDLE_RIGHT, -50.0f, -50.0f);
    case HOTKEY_ADAPTIVE_RESIZE_GROW:
        return hotkey_change_window_ratio(0.05f) || hotkey_resize_window(HANDLE_BOTTOM | HANDLE_RIGHT, 50.0f, 50.0f);
    case HOTKEY_ADAPTIVE_RESIZE_WEST:
        return hotkey_resize_window(HANDLE_LEFT, -50.0f, 0.0f) || hotkey_resize_window(HANDLE_RIGHT, -50.0f, 0.0f);
    case HOTKEY_ADAPTIVE_RESIZE_SOUTH:
        return hotkey_resize_window(HANDLE_BOTTOM, 0.0f, 50.0f) || hotkey_resize_window(HANDLE_TOP, 0.0f, 50.0f);
    case HOTKEY_ADAPTIVE_RESIZE_NORTH:
        return hotkey_resize_window(HANDLE_TOP, 0.0f, -50.0f) || hotkey_resize_window(HANDLE_BOTTOM, 0.0f, -50.0f);
    case HOTKEY_ADAPTIVE_RESIZE_EAST:
        return hotkey_resize_window(HANDLE_RIGHT, 50.0f, 0.0f) || hotkey_resize_window(HANDLE_LEFT, 50.0f, 0.0f);
    }
    return false;
}

static enum view_type hotkey_view_layout(enum hotkey_space_layout layout)
{
    switch (layout) {
    case HOTKEY_SPACE_LAYOUT_BSP: return VIEW_BSP;
    case HOTKEY_SPACE_LAYOUT_STACK: return VIEW_STACK;
    case HOTKEY_SPACE_LAYOUT_FLOAT: return VIEW_FLOAT;
    case HOTKEY_SPACE_LAYOUT_ACCORDION_HORIZONTAL: return VIEW_ACCORDION_HORIZONTAL;
    case HOTKEY_SPACE_LAYOUT_ACCORDION_VERTICAL: return VIEW_ACCORDION_VERTICAL;
    }
    return VIEW_DEFAULT;
}

static bool hotkey_focus_space(uint64_t sid)
{
    if (!sid) return false;
    enum space_op_error result = space_manager_focus_space(sid);
    if (result != SPACE_OP_ERROR_SUCCESS && result != SPACE_OP_ERROR_SAME_SPACE) return false;
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_FOCUS | MENU_BAR_DIRTY_SPACE);
    return true;
}

struct hotkey_occupied_space
{
    uint64_t sid;
    int index;
};

static bool hotkey_focus_occupied_space(enum hotkey_occupied_space_direction direction)
{
    uint64_t active_sid = space_manager_active_space();
    if (!active_sid) return false;
    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (!topology) return false;
    int space_count = 0;
    uint64_t *space_list = display_space_list_with_topology(
        space_display_id(active_sid), topology, &space_count);
    if (!space_list || space_count <= 0) {
        CFRelease(topology);
        return false;
    }
    struct hotkey_occupied_space *occupied = ts_alloc_list(struct hotkey_occupied_space, space_count);
    if (!occupied) {
        CFRelease(topology);
        return false;
    }
    int occupied_count = 0;
    for (int i = 0; i < space_count; ++i) {
        uint64_t sid = space_list[i];
        int window_count = 0;
        if (sid != active_sid) (void)space_window_list(sid, &window_count, true);
        if (sid != active_sid && window_count <= 0) continue;
        int index = managed_space_index(topology, sid);
        if (index <= 0) continue;
        occupied[occupied_count++] = (struct hotkey_occupied_space){sid, index};
    }
    if (!occupied_count) {
        CFRelease(topology);
        return false;
    }
    for (int i = 1; i < occupied_count; ++i) {
        struct hotkey_occupied_space value = occupied[i];
        int j = i;
        while (j > 0 && occupied[j - 1].index > value.index) {
            occupied[j] = occupied[j - 1];
            --j;
        }
        occupied[j] = value;
    }
    int active = -1;
    for (int i = 0; i < occupied_count; ++i) if (occupied[i].sid == active_sid) { active = i; break; }
    if (active < 0) {
        CFRelease(topology);
        return false;
    }
    int target = direction == HOTKEY_OCCUPIED_SPACE_PREVIOUS
               ? (active + occupied_count - 1) % occupied_count
               : (active + 1) % occupied_count;
    uint64_t target_sid = occupied[target].sid;
    CFRelease(topology);
    return hotkey_focus_space(target_sid);
}

static bool hotkey_move_window_to_space(int index)
{
    struct window *window = window_manager_focused_window(&g_window_manager);
    uint64_t sid = space_manager_mission_control_space(index);
    if (!window || !sid || space_is_fullscreen(sid)) return false;
    if (!window_manager_send_window_to_space(&g_space_manager, &g_window_manager, window, sid, false)) return false;
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_SPACE);
    return true;
}

static bool hotkey_close_other_windows(void)
{
    struct window *focused = window_manager_focused_window(&g_window_manager);
    uint64_t sid = space_manager_active_space();
    if (!focused || !sid) return false;
    int count = 0;
    uint32_t *window_list = space_window_list(sid, &count, false);
    if (!window_list && count) return false;
    bool success = true;
    for (int i = 0; i < count; ++i) {
        if (window_list[i] == focused->id) continue;
        struct window *window = window_manager_find_window(&g_window_manager, window_list[i]);
        if (!window || window_check_flag(window, WINDOW_MINIMIZE)) continue;
        if (!window_manager_close_window(window)) success = false;
    }
    return success;
}

static bool hotkey_cycle_space_layout(void)
{
    uint64_t sid = space_manager_active_space();
    struct view *view = sid ? space_manager_find_view(&g_space_manager, sid) : NULL;
    if (!view) return false;
    enum view_type next = VIEW_BSP;
    switch (view->layout) {
    case VIEW_BSP: next = VIEW_STACK; break;
    case VIEW_STACK: next = VIEW_ACCORDION_HORIZONTAL; break;
    case VIEW_ACCORDION_HORIZONTAL: next = VIEW_ACCORDION_VERTICAL; break;
    case VIEW_ACCORDION_VERTICAL:
    case VIEW_FLOAT:
    case VIEW_DEFAULT: next = VIEW_BSP; break;
    }
    return declarative_apply_runtime_layout_for_space(sid, next) == DECLARATIVE_APPLY_OK;
}

static bool hotkey_restart_service(const char *label)
{
    char target[HOTKEY_MAX_SERVICE_LABEL_BYTES + 32];
    if (snprintf(target, sizeof(target), "gui/%u/%s", getuid(), label) >= (int)sizeof(target)) return false;
    char *const arguments[] = {(char *)"/bin/launchctl", (char *)"kickstart", (char *)"-k", target, NULL};
    pid_t pid = 0;
    return posix_spawn(&pid, arguments[0], NULL, NULL, arguments, environ) == 0;
}

static bool hotkey_execute_action(const struct hotkey_action *action)
{
#ifdef TESTS
    if (hotkey_test_action_executor) return hotkey_test_action_executor(action, hotkey_test_action_context);
#endif
    uint64_t sid;
    struct window *window;
    switch (action->type) {
    case HOTKEY_ACTION_FOCUS_WINDOW:
        return action->data.focus_window.robust
             ? hotkey_robust_focus_window_direction(action->data.focus_window.direction)
             : hotkey_focus_window_direction(action->data.focus_window.direction);
    case HOTKEY_ACTION_WARP_WINDOW:
        return hotkey_warp_window_direction(action->data.warp_window.direction);
    case HOTKEY_ACTION_RESIZE_WINDOW:
        return hotkey_resize_window(action->data.resize_window.handle,
                                    action->data.resize_window.dx, action->data.resize_window.dy);
    case HOTKEY_ACTION_CHANGE_WINDOW_RATIO:
        return hotkey_change_window_ratio(action->data.change_window_ratio.delta);
    case HOTKEY_ACTION_ADAPTIVE_RESIZE:
        return hotkey_adaptive_resize(action->data.adaptive_resize.direction);
    case HOTKEY_ACTION_TOGGLE_FLOATING:
        window = window_manager_focused_window(&g_window_manager);
        if (!window) return false;
        window_manager_toggle_window_floating(&g_space_manager, &g_window_manager, window);
        return true;
    case HOTKEY_ACTION_TOGGLE_ZOOM_FULLSCREEN:
        window = window_manager_focused_window(&g_window_manager);
        if (!window) return false;
        window_manager_toggle_window_zoom_fullscreen(&g_window_manager, window);
        return true;
    case HOTKEY_ACTION_ROTATE_SPACE:
        sid = space_manager_active_space();
        return sid && space_manager_rotate_space(&g_space_manager, sid, action->data.rotate_space.degrees);
    case HOTKEY_ACTION_SET_SPACE_LAYOUT:
        sid = space_manager_active_space();
        return sid && declarative_apply_runtime_layout_for_space(
                          sid, hotkey_view_layout(action->data.set_space_layout.layout)) == DECLARATIVE_APPLY_OK;
    case HOTKEY_ACTION_BALANCE_SPACE:
        sid = space_manager_active_space();
        return sid && space_manager_balance_space(&g_space_manager, sid, SPLIT_X | SPLIT_Y);
    case HOTKEY_ACTION_FOCUS_SPACE:
        return hotkey_focus_space(space_manager_mission_control_space(action->data.space.index));
    case HOTKEY_ACTION_FOCUS_OCCUPIED_SPACE:
        return hotkey_focus_occupied_space(action->data.occupied_space.direction);
    case HOTKEY_ACTION_MOVE_WINDOW_TO_SPACE:
        return hotkey_move_window_to_space(action->data.space.index);
    case HOTKEY_ACTION_CLOSE_WINDOW:
        window = window_manager_focused_window(&g_window_manager);
        return window && window_manager_close_window(window);
    case HOTKEY_ACTION_CLOSE_OTHER_WINDOWS:
        return hotkey_close_other_windows();
    case HOTKEY_ACTION_CYCLE_SPACE_LAYOUT:
        return hotkey_cycle_space_layout();
    case HOTKEY_ACTION_RELOAD_CONFIGURATION:
        return declarative_config_reload(NULL);
    case HOTKEY_ACTION_RESTART_SERVICE:
        return hotkey_restart_service(action->data.restart_service.label);
    case HOTKEY_ACTION_SYNTHESIZE_KEY:
        return hotkey_post_synthetic_keystroke(action->data.synthesize_key);
    }
    return false;
}

bool hotkey_action_execute_sequence(const struct hotkey_action *actions, size_t count)
{
    if (!actions || !count || count > HOTKEY_MAX_ACTIONS) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!hotkey_execute_action(&actions[i])) {
            debug("hotkey action failed: type=%d index=%zu\n", actions[i].type, i);
            return false;
        }
    }
    return true;
}
