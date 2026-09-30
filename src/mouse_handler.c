extern struct event_loop g_event_loop;
extern volatile bool __pending_gesture;
extern volatile uint64_t __last_gesture_time;

static inline uint8_t mouse_mod_from_cgflags(CGEventFlags cgflags)
{
    uint8_t flags = 0;

    if ((cgflags & kCGEventFlagMaskAlternate)   == kCGEventFlagMaskAlternate)   flags |= MOUSE_MOD_ALT;
    if ((cgflags & kCGEventFlagMaskShift)       == kCGEventFlagMaskShift)       flags |= MOUSE_MOD_SHIFT;
    if ((cgflags & kCGEventFlagMaskCommand)     == kCGEventFlagMaskCommand)     flags |= MOUSE_MOD_CMD;
    if ((cgflags & kCGEventFlagMaskControl)     == kCGEventFlagMaskControl)     flags |= MOUSE_MOD_CTRL;
    if ((cgflags & kCGEventFlagMaskSecondaryFn) == kCGEventFlagMaskSecondaryFn) flags |= MOUSE_MOD_FN;

    return flags;
}

enum mouse_mode mouse_action_for_event(CGEventType type, CGEventFlags flags, uint8_t modifier, enum mouse_mode action1, enum mouse_mode action2)
{
    if (mouse_mod_from_cgflags(flags) != modifier) return MOUSE_MODE_NONE;

    if (type == kCGEventLeftMouseDown)  return action1;
    if (type == kCGEventRightMouseDown) return action2;
    return MOUSE_MODE_NONE;
}

struct mouse_click_state_up_result
{
    bool consume;
    bool repost;
};

static struct mouse_click_state *mouse_click_state_for_event(struct mouse_state *mouse_state, CGEventType type)
{
    switch (type) {
    case kCGEventLeftMouseDown:
    case kCGEventLeftMouseUp:
    case kCGEventLeftMouseDragged:
        return &mouse_state->left_click;
    case kCGEventRightMouseDown:
    case kCGEventRightMouseUp:
    case kCGEventRightMouseDragged:
        return &mouse_state->right_click;
    default:
        return NULL;
    }
}

static void mouse_click_state_mouse_down(struct mouse_click_state *click_state, bool consume)
{
    click_state->drag_detected = false;
    click_state->consume = consume;
}

static void mouse_click_state_mouse_dragged(struct mouse_click_state *click_state)
{
    if (click_state->consume) click_state->drag_detected = true;
}

static struct mouse_click_state_up_result mouse_click_state_mouse_up(struct mouse_click_state *click_state)
{
    return (struct mouse_click_state_up_result) {
        .consume = click_state->consume,
        .repost = click_state->consume && !click_state->drag_detected
    };
}

static void mouse_click_state_clear(struct mouse_click_state *click_state)
{
    click_state->down_event = NULL;
    click_state->consume = false;
    click_state->drag_detected = false;
}

static void mouse_click_state_release(struct mouse_click_state *click_state)
{
    if (click_state->down_event) CFRelease(click_state->down_event);
    mouse_click_state_clear(click_state);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static MOUSE_HANDLER(mouse_handler)
{
    struct mouse_state *mouse_state = context;

    switch (type) {
    case kCGEventTapDisabledByTimeout:
    case kCGEventTapDisabledByUserInput: {
        CFMachPortRef handle = mouse_state->handle;
        if (handle) CGEventTapEnable(handle, true);
    } break;
    case kCGEventLeftMouseDown:
    case kCGEventRightMouseDown: {
        struct mouse_click_state *click_state = mouse_click_state_for_event(mouse_state, type);
        enum mouse_mode action = mouse_action_for_event(type, CGEventGetFlags(event), mouse_state->modifier, mouse_state->action1, mouse_state->action2);
        mouse_click_state_release(click_state);
        mouse_click_state_mouse_down(click_state, action != MOUSE_MODE_NONE);
        event_loop_post(&g_event_loop, MOUSE_DOWN, (void *) CFRetain(event), action);

        if (click_state->consume) {
            click_state->down_event = (CGEventRef) CFRetain(event);
            return NULL;
        }
    } break;
    case kCGEventLeftMouseUp:
    case kCGEventRightMouseUp: {
        struct mouse_click_state *click_state = mouse_click_state_for_event(mouse_state, type);
        event_loop_post(&g_event_loop, MOUSE_UP, (void *) CFRetain(event), 0);

        struct mouse_click_state_up_result up_result = mouse_click_state_mouse_up(click_state);
        if (up_result.consume) {
            if (up_result.repost) {
                CGEventTapPostEvent(proxy, click_state->down_event);
                CGEventTapPostEvent(proxy, event);
            }

            mouse_click_state_release(click_state);
            return NULL;
        }
    } break;
    case kCGEventLeftMouseDragged:
    case kCGEventRightMouseDragged: {
        struct mouse_click_state *click_state = mouse_click_state_for_event(mouse_state, type);
        mouse_click_state_mouse_dragged(click_state);
        event_loop_post(&g_event_loop, MOUSE_DRAGGED, (void *) CFRetain(event), 0);
    } break;
    case kCGEventMouseMoved: {
        uint8_t mod = mouse_mod_from_cgflags(CGEventGetFlags(event));
        if (mod == mouse_state->modifier) return event;

        event_loop_post(&g_event_loop, MOUSE_MOVED, (void *) CFRetain(event), mod);
    } break;
    case /* kCGSEventDockControl */ 30: {
        int type = CGEventGetIntegerValueField(event, /* kCGEventGestureHIDType */ 110);
        if (type == /* kIOHIDEventTypeDockSwipe */ 23) {
            int motion = CGEventGetIntegerValueField(event, /* kCGEventGestureSwipeMotion */ 123);
            if (motion == /* kCGGestureMotionHorizontal */ 1) {
                int phase = CGEventGetIntegerValueField(event, /* kCGEventGesturePhase */ 132);
                if (phase == /* kCGSGesturePhaseBegan */ 1) {
                    __atomic_store_n(&__pending_gesture, true, __ATOMIC_RELEASE);
                } else if (phase == /* kCGSGesturePhaseEnded */ 4 || phase == /* kCGSGesturePhaseCancelled */ 8) {
                    __atomic_store_n(&__pending_gesture, false, __ATOMIC_RELEASE);
                    __atomic_store_n(&__last_gesture_time, read_os_timer(), __ATOMIC_RELEASE);
                }
            }
        }
    } break;
    }

    return event;
}
#pragma clang diagnostic pop

void mouse_window_info_populate(struct mouse_state *ms, struct mouse_window_info *info)
{
    CGRect frame = ms->window->frame;

    info->dx = frame.origin.x    - ms->window_frame.origin.x;
    info->dy = frame.origin.y    - ms->window_frame.origin.y;
    info->dw = frame.size.width  - ms->window_frame.size.width;
    info->dh = frame.size.height - ms->window_frame.size.height;

    info->changed_x = info->dx != 0.0f;
    info->changed_y = info->dy != 0.0f;
    info->changed_w = info->dw != 0.0f;
    info->changed_h = info->dh != 0.0f;

    info->changed_position = info->changed_x || info->changed_y;
    info->changed_size     = info->changed_w || info->changed_h;
}

enum mouse_drop_action mouse_determine_drop_action(struct mouse_state *ms, struct window_node *src_node, struct window *dst_window, CGPoint point)
{
    CGRect  f    = dst_window->frame;
    CGPoint wp   = { point.x - f.origin.x, point.y - f.origin.y };
    CGRect  c    = {{ 0.25f * f.size.width, 0.25f * f.size.height }, { 0.50f * f.size.width, 0.50f * f.size.height }};
    CGPoint t[3] = {{ 0.0f, 0.0f}, { 0.5f * f.size.width, 0.5f * f.size.height }, { f.size.width, 0.0f }};
    CGPoint r[3] = {{ f.size.width, 0.0f }, { 0.5f * f.size.width, 0.5f * f.size.height }, { f.size.width, f.size.height }};
    CGPoint b[3] = {{ f.size.width, f.size.height }, { 0.5f * f.size.width, 0.5f * f.size.height }, { 0.0f, f.size.height }};
    CGPoint l[3] = {{ 0.0f, f.size.height }, { 0.5f * f.size.width, 0.5f * f.size.height }, { 0.0f, 0.0f }};

    if ((CGRectContainsPoint(c, wp)) && (src_node->window_count == 1)) {
        return ms->drop_action == MOUSE_MODE_STACK ? MOUSE_DROP_ACTION_STACK : MOUSE_DROP_ACTION_SWAP;
    } else if (triangle_contains_point(t, wp)) {
        return MOUSE_DROP_ACTION_WARP_TOP;
    } else if (triangle_contains_point(r, wp)) {
        return MOUSE_DROP_ACTION_WARP_RIGHT;
    } else if (triangle_contains_point(b, wp)) {
        return MOUSE_DROP_ACTION_WARP_BOTTOM;
    } else if (triangle_contains_point(l, wp)) {
        return MOUSE_DROP_ACTION_WARP_LEFT;
    }

    return MOUSE_DROP_ACTION_NONE;
}

static uint32_t mouse_drop_action_restore_insertion_point(struct window_node *src_node, struct window *src_window)
{
    for (int i = 0; i < src_node->window_count; ++i) {
        if (src_node->window_order[i] != src_window->id) {
            return src_node->window_order[i];
        }
    }

    if (!src_node->parent) return 0;

    struct window_node *sibling = window_node_is_right_child(src_node)
                               ? src_node->parent->left
                               : src_node->parent->right;

    if (!sibling || sibling->window_count == 0) return 0;
    return sibling->window_order[0];
}

static struct window_node *mouse_drop_action_restore_window(struct view *src_view, struct window *src_window, uint32_t insertion_point)
{
    struct window_node *node = view_add_window_node_with_insertion_point(src_view, src_window, insertion_point);
    if (!node && insertion_point) {
        node = view_add_window_node_with_insertion_point(src_view, src_window, 0);
    }

    return node;
}

static bool mouse_drop_action_warp_capacity_available(struct view *dst_view, struct window *dst_window)
{
    struct window_node *dst_node = view_find_window_node(dst_view, dst_window->id);
    if (!dst_node) return false;

    if (dst_view->layout == VIEW_STACK && dst_view->root->window_count >= NODE_MAX_WINDOW_COUNT) {
        return false;
    }

    if (dst_view->layout == VIEW_BSP && dst_node->insert_dir == STACK && dst_node->window_count >= NODE_MAX_WINDOW_COUNT) {
        return false;
    }

    return true;
}

static __attribute__((noreturn)) void mouse_drop_action_fail_rollback(char *operation)
{
    error("fatal error: mouse-drop rollback failed for %s\n", operation);
    __builtin_unreachable();
}

void mouse_drop_action_stack(struct window_manager *wm, struct view *src_view, struct window *src_window, struct view *dst_view, struct window *dst_window)
{
    struct window_node *dst_node = view_find_window_node(dst_view, dst_window->id);
    if (!dst_node || dst_node->window_count >= NODE_MAX_WINDOW_COUNT) return;

    struct window_node *src_node = view_find_window_node(src_view, src_window->id);
    if (!src_node) return;

    uint32_t src_restore_insertion_point = mouse_drop_action_restore_insertion_point(src_node, src_window);

    space_manager_untile_window(src_view, src_window);

    dst_node = view_find_window_node(dst_view, dst_window->id);
    if (!dst_node || !view_stack_window_node(dst_node, src_window)) {
        if (!mouse_drop_action_restore_window(src_view, src_window, src_restore_insertion_point)) {
            mouse_drop_action_fail_rollback("stack");
        }

        window_manager_adjust_layer(src_window, LAYER_BELOW);
        return;
    }

    window_manager_remove_managed_window(wm, src_window->id);
    window_manager_add_managed_window(wm, src_window, dst_view);
    window_manager_adjust_layer(src_window, LAYER_BELOW);
    scripting_addition_order_window(src_window->id, 1, dst_node->window_order[1]);

    if (dst_node->zoom) {
        window_manager_animate_window((struct window_capture) { src_window, dst_node->zoom->area.x, dst_node->zoom->area.y, dst_node->zoom->area.w, dst_node->zoom->area.h });
    } else {
        window_manager_animate_window((struct window_capture) { src_window, dst_node->area.x, dst_node->area.y, dst_node->area.w, dst_node->area.h });
    }
}

void mouse_drop_action_swap(struct window_manager *wm, struct view *src_view, struct window_node *src_node, struct window *src_window, struct view *dst_view, struct window_node *dst_node, struct window *dst_window)
{
    if (window_node_contains_window(src_node, src_view->insertion_point)) {
        src_view->insertion_point = dst_window->id;
    } else if (window_node_contains_window(dst_node, dst_view->insertion_point)) {
        dst_view->insertion_point = src_window->id;
    }

    window_node_swap_window_list(src_node, dst_node);

    if (src_view->sid != dst_view->sid) {
        for (int i = 0; i < src_node->window_count; ++i) {
            window_manager_remove_managed_window(wm, src_node->window_list[i]);
            window_manager_add_managed_window(wm, window_manager_find_window(wm, src_node->window_list[i]), src_view);
        }

        for (int i = 0; i < dst_node->window_count; ++i) {
            window_manager_remove_managed_window(wm, dst_node->window_list[i]);
            window_manager_add_managed_window(wm, window_manager_find_window(wm, dst_node->window_list[i]), dst_view);
        }
    }

    struct window_capture *window_list = NULL;
    window_node_capture_windows(src_node, &window_list);
    window_node_capture_windows(dst_node, &window_list);
    window_manager_animate_window_list(window_list, ts_buf_len(window_list));
}

void mouse_drop_action_warp(struct window_manager *wm, struct view *src_view, struct window_node *src_node, struct window *src_window, struct view *dst_view, struct window_node *dst_node, struct window *dst_window, enum window_node_split split, enum window_node_child child)
{
    if (!mouse_drop_action_warp_capacity_available(dst_view, dst_window)) return;

    src_node = view_find_window_node(src_view, src_window->id);
    dst_node = view_find_window_node(dst_view, dst_window->id);
    if (!src_node || !dst_node) return;

    bool sibling_collapse = (src_node->parent && dst_node->parent) &&
                            (src_node->parent == dst_node->parent) &&
                            (src_node->window_count == 1);

    if (sibling_collapse && dst_node->parent->split == split) {
        mouse_drop_action_swap(wm, src_view, src_node, src_window, dst_view, dst_node, dst_window);
        return;
    }

    uint32_t src_restore_insertion_point = mouse_drop_action_restore_insertion_point(src_node, src_window);

    struct window_node *src_node_rm = view_remove_window_node(src_view, src_window);
    window_manager_remove_managed_window(wm, src_window->id);
    window_manager_purify_window(wm, src_window);

    dst_node = view_find_window_node(dst_view, dst_window->id);
    if (!dst_node) {
        if (!mouse_drop_action_restore_window(src_view, src_window, src_restore_insertion_point)) {
            mouse_drop_action_fail_rollback("warp destination re-resolve");
        }

        window_manager_add_managed_window(wm, src_window, src_view);
        return;
    }

    enum window_node_split dst_prev_split = dst_node->split;
    enum window_node_child dst_prev_child = dst_node->child;

    dst_node->split = split;
    dst_node->child = child;

    struct window_node *src_node_add = view_add_window_node_with_insertion_point(dst_view, src_window, dst_window->id);
    if (!src_node_add) {
        dst_node->split = dst_prev_split;
        dst_node->child = dst_prev_child;

        if (!mouse_drop_action_restore_window(src_view, src_window, src_restore_insertion_point)) {
            mouse_drop_action_fail_rollback("warp insertion");
        }

        window_manager_add_managed_window(wm, src_window, src_view);
        return;
    }

    window_manager_add_managed_window(wm, src_window, dst_view);

    struct window_capture *window_list = NULL;

    if (src_node_rm) {
        window_node_capture_windows(src_node_rm, &window_list);
    }

    if (src_node_rm != src_node_add && src_node_rm != src_node_add->parent) {
        window_node_capture_windows(src_node_add, &window_list);
    }

    window_manager_animate_window_list(window_list, ts_buf_len(window_list));
}

void mouse_drop_no_target(struct space_manager *sm, struct window_manager *wm, struct view *src_view, struct view *dst_view, struct window *window, struct window_node *node)
{
    if (src_view->sid == dst_view->sid) {
        window_node_flush(node);
    } else {
        space_manager_untile_window(src_view, window);
        window_manager_remove_managed_window(wm, window->id);
        window_manager_purify_window(wm, window);

        struct view *view = space_manager_tile_window_on_space(sm, window, dst_view->sid);
        window_manager_add_managed_window(wm, window, view);
    }
}

void mouse_drop_try_adjust_bsp_grid(struct window_manager *wm, struct view *view, struct window *window, struct mouse_window_info *info)
{
    bool success = true;

    if (view->layout != VIEW_BSP) {
        success = false;
        goto end;
    }

    if (info->changed_position) {
        uint8_t direction = 0;
        if (info->changed_x) direction |= HANDLE_LEFT;
        if (info->changed_y) direction |= HANDLE_TOP;
        if (window_manager_resize_window_relative(wm, window, direction, info->dx, info->dy, true) == WINDOW_OP_ERROR_INVALID_DST_NODE) {
            success = false;
        }
    }

    if (info->changed_size) {
        uint8_t direction = 0;
        if (info->changed_w && !info->changed_x) direction |= HANDLE_RIGHT;
        if (info->changed_h && !info->changed_y) direction |= HANDLE_BOTTOM;
        if (window_manager_resize_window_relative(wm, window, direction, info->dw, info->dh, true) == WINDOW_OP_ERROR_INVALID_DST_NODE) {
            success = false;
        }
    }

end:
    if (!success) {
        struct window_node *node = view_find_window_node(view, window->id);
        if (node) window_node_flush(node);
    }
}

static void mouse_state_set_modifier_on_main_thread(struct mouse_state *mouse_state, uint8_t modifier)
{
    mouse_state->modifier = modifier;
}

void mouse_state_set_modifier(struct mouse_state *mouse_state, uint8_t modifier)
{
    if (pthread_main_np()) {
        mouse_state_set_modifier_on_main_thread(mouse_state, modifier);
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            mouse_state_set_modifier_on_main_thread(mouse_state, modifier);
        });
    }
}

static void mouse_state_set_action1_on_main_thread(struct mouse_state *mouse_state, enum mouse_mode action)
{
    mouse_state->action1 = action;
}

void mouse_state_set_action1(struct mouse_state *mouse_state, enum mouse_mode action)
{
    if (pthread_main_np()) {
        mouse_state_set_action1_on_main_thread(mouse_state, action);
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            mouse_state_set_action1_on_main_thread(mouse_state, action);
        });
    }
}

static void mouse_state_set_action2_on_main_thread(struct mouse_state *mouse_state, enum mouse_mode action)
{
    mouse_state->action2 = action;
}

void mouse_state_set_action2(struct mouse_state *mouse_state, enum mouse_mode action)
{
    if (pthread_main_np()) {
        mouse_state_set_action2_on_main_thread(mouse_state, action);
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            mouse_state_set_action2_on_main_thread(mouse_state, action);
        });
    }
}

void mouse_state_init(struct mouse_state *state)
{
    mouse_state_set_modifier(state, MOUSE_MOD_FN);
    mouse_state_set_action1(state, MOUSE_MODE_MOVE);
    mouse_state_set_action2(state, MOUSE_MODE_RESIZE);
    state->drop_action = MOUSE_MODE_SWAP;
    state->active_button = MOUSE_BUTTON_NONE;
}

struct mouse_handler_resources
{
    CFMachPortRef handle;
    CFRunLoopSourceRef runloop_source;
};

static void mouse_handler_release_uninstalled_resources(struct mouse_handler_resources resources)
{
    if (resources.runloop_source) {
        CFRunLoopSourceInvalidate(resources.runloop_source);
        CFRelease(resources.runloop_source);
    }

    if (resources.handle) {
        CGEventTapEnable(resources.handle, false);
        CFMachPortInvalidate(resources.handle);
        CFRelease(resources.handle);
    }
}

static bool mouse_handler_create_resources(struct mouse_state *mouse_state, uint32_t mask, struct mouse_handler_resources *resources)
{
    resources->handle = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, mask, mouse_handler, mouse_state);
    if (!resources->handle) return false;

    if (!CGEventTapIsEnabled(resources->handle)) {
        mouse_handler_release_uninstalled_resources(*resources);
        *resources = (struct mouse_handler_resources) {0};
        return false;
    }
    CGEventTapEnable(resources->handle, false);

    resources->runloop_source = CFMachPortCreateRunLoopSource(NULL, resources->handle, 0);
    if (!resources->runloop_source) {
        mouse_handler_release_uninstalled_resources(*resources);
        *resources = (struct mouse_handler_resources) {0};
        return false;
    }

    return true;
}

static void mouse_handler_remove_installed_resources(struct mouse_handler_resources resources)
{
    if (resources.handle) CGEventTapEnable(resources.handle, false);
    if (resources.runloop_source) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), resources.runloop_source, kCFRunLoopCommonModes);
        CFRunLoopSourceInvalidate(resources.runloop_source);
    }
    if (resources.handle) CFMachPortInvalidate(resources.handle);

    if (resources.runloop_source) CFRelease(resources.runloop_source);
    if (resources.handle) CFRelease(resources.handle);
}

static void mouse_handler_install_resources(struct mouse_state *mouse_state, struct mouse_handler_resources resources)
{
    mouse_state->handle = resources.handle;
    mouse_state->runloop_source = resources.runloop_source;
    CFRunLoopAddSource(CFRunLoopGetMain(), resources.runloop_source, kCFRunLoopCommonModes);
    CGEventTapEnable(resources.handle, true);
}

static bool mouse_handler_begin_on_main_thread(struct mouse_state *mouse_state, uint32_t mask)
{
    if (mouse_state->handle) return true;

    struct mouse_handler_resources resources = {0};
    if (!mouse_handler_create_resources(mouse_state, mask, &resources)) return false;

    mouse_handler_install_resources(mouse_state, resources);
    return true;
}

bool mouse_handler_begin(struct mouse_state *mouse_state, uint32_t mask)
{
    if (pthread_main_np()) return mouse_handler_begin_on_main_thread(mouse_state, mask);

    __block bool result;
    dispatch_sync(dispatch_get_main_queue(), ^{
        result = mouse_handler_begin_on_main_thread(mouse_state, mask);
    });
    return result;
}

static bool mouse_handler_reconfigure_on_main_thread(struct mouse_state *mouse_state, uint32_t mask)
{
    struct mouse_handler_resources replacement = {0};
    if (!mouse_handler_create_resources(mouse_state, mask, &replacement)) return false;

    struct mouse_handler_resources previous = {
        .handle = mouse_state->handle,
        .runloop_source = mouse_state->runloop_source
    };

    mouse_handler_remove_installed_resources(previous);
    mouse_handler_install_resources(mouse_state, replacement);
    return true;
}

bool mouse_handler_reconfigure(struct mouse_state *mouse_state, uint32_t mask)
{
    if (pthread_main_np()) return mouse_handler_reconfigure_on_main_thread(mouse_state, mask);

    __block bool result;
    dispatch_sync(dispatch_get_main_queue(), ^{
        result = mouse_handler_reconfigure_on_main_thread(mouse_state, mask);
    });
    return result;
}

static void mouse_handler_end_on_main_thread(struct mouse_state *mouse_state)
{
    struct mouse_handler_resources resources = {
        .handle = mouse_state->handle,
        .runloop_source = mouse_state->runloop_source
    };

    mouse_state->handle = NULL;
    mouse_state->runloop_source = NULL;
    mouse_handler_remove_installed_resources(resources);

    mouse_click_state_release(&mouse_state->left_click);
    mouse_click_state_release(&mouse_state->right_click);
}

void mouse_handler_end(struct mouse_state *mouse_state)
{
    if (pthread_main_np()) {
        mouse_handler_end_on_main_thread(mouse_state);
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            mouse_handler_end_on_main_thread(mouse_state);
        });
    }
}
