#include <syslog.h>

extern struct event_loop g_event_loop;
extern struct process_manager g_process_manager;
extern struct display_manager g_display_manager;
extern struct space_manager g_space_manager;
extern struct window_manager g_window_manager;
extern struct mouse_state g_mouse_state;
extern enum mission_control_mode g_mission_control_mode;
extern int g_connection;
extern void *g_workspace_context;
extern int g_layer_below_window_level;
volatile uint32_t __pending_window_focus;
static volatile uint32_t window_focus_fallback_reserved;
volatile bool __pending_gesture;
volatile uint64_t __last_gesture_time;
volatile uint64_t __last_cmd_tab_time;
static pthread_mutex_t g_process_manager_front_switch_lock = PTHREAD_MUTEX_INITIALIZER;

struct mission_control_session_state {
    uint64_t generation;
    uint64_t observer_epoch;
    uint64_t ax_token;
    uint64_t poll_token;
    unsigned inconclusive_samples;
    bool active;
    bool poll_pending;
};

struct mission_control_poll_context {
    uint64_t generation;
    uint64_t poll_token;
};

enum mission_control_sample_state {
    MISSION_CONTROL_SAMPLE_ACTIVE,
    MISSION_CONTROL_SAMPLE_INACTIVE,
    MISSION_CONTROL_SAMPLE_INCONCLUSIVE
};

static struct mission_control_session_state g_mission_control_session;
static uint64_t g_mission_control_generation;
static volatile uint64_t g_mission_control_poll_generation;
static volatile uint64_t g_mission_control_poll_token;

static uint64_t mission_control_next_generation(void)
{
    if (++g_mission_control_generation == 0) ++g_mission_control_generation;
    return g_mission_control_generation;
}

#ifdef TESTS
static CFArrayRef (*event_loop_test_window_list_copy)(CGWindowListOption, CGWindowID) = CGWindowListCopyWindowInfo;
static void (*event_loop_test_mission_control_schedule)(uint64_t, uint64_t, double);
static void (*event_loop_test_mission_control_signal)(enum signal_type, void *);
static void (*event_loop_test_mission_control_correct)(struct space_manager *, struct window_manager *);
static void (*event_loop_test_mission_control_menu_alpha)(int, int, float);
static void (*event_loop_test_mission_control_menu_dirty)(uint32_t);
static void (*event_loop_test_mission_control_reconcile)(void);
static int event_loop_test_mission_control_poll_allocated;
static int event_loop_test_mission_control_poll_freed;
#endif

static CFArrayRef mission_control_copy_window_list(void)
{
#ifdef TESTS
    return event_loop_test_window_list_copy(kCGWindowListOptionOnScreenOnly, 0);
#else
    return CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, 0);
#endif
}

static void mission_control_emit_signal(enum signal_type type, void *context)
{
#ifdef TESTS
    if (event_loop_test_mission_control_signal) { event_loop_test_mission_control_signal(type, context); return; }
#endif
    event_signal_push(type, context);
}

static void mission_control_correct(void)
{
#ifdef TESTS
    if (event_loop_test_mission_control_correct) { event_loop_test_mission_control_correct(&g_space_manager, &g_window_manager); return; }
#endif
    window_manager_correct_for_mission_control_changes(&g_space_manager, &g_window_manager);
}

static void mission_control_set_menu_alpha(float alpha)
{
#ifdef TESTS
    if (event_loop_test_mission_control_menu_alpha) { event_loop_test_mission_control_menu_alpha(0, 1, alpha); return; }
#endif
    SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, alpha);
}

static void mission_control_mark_menu_dirty(void)
{
    uint32_t flags = MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_SPACE | MENU_BAR_DIRTY_WINDOW;
#ifdef TESTS
    if (event_loop_test_mission_control_menu_dirty) { event_loop_test_mission_control_menu_dirty(flags); return; }
#endif
    menu_bar_manager_mark_dirty(flags);
}

static void mission_control_request_reconcile(void)
{
#ifdef TESTS
    if (event_loop_test_mission_control_reconcile) { event_loop_test_mission_control_reconcile(); return; }
#endif
    declarative_configuration_request_reconcile();
}

static struct mission_control_poll_context *mission_control_poll_context_create(void)
{
    struct mission_control_poll_context *context = malloc(sizeof(*context));
#ifdef TESTS
    if (context) ++event_loop_test_mission_control_poll_allocated;
#endif
    return context;
}

static void mission_control_poll_context_destroy(struct mission_control_poll_context *context)
{
    if (!context) return;
#ifdef TESTS
    ++event_loop_test_mission_control_poll_freed;
#endif
    free(context);
}

static void mission_control_poll_post(uint64_t generation, uint64_t poll_token,
                                      uint64_t loop_generation, unsigned retry)
{
    if (generation != __atomic_load_n(&g_mission_control_poll_generation, __ATOMIC_ACQUIRE) ||
        poll_token != __atomic_load_n(&g_mission_control_poll_token, __ATOMIC_ACQUIRE) ||
        loop_generation != event_loop_generation_snapshot(&g_event_loop) ||
        !__atomic_load_n(&g_event_loop.is_running, __ATOMIC_ACQUIRE)) return;

    struct mission_control_poll_context *context = mission_control_poll_context_create();
    if (context) *context = (struct mission_control_poll_context) { generation, poll_token };
    if (context && event_loop_try_post_generation(&g_event_loop, loop_generation,
                                                   MISSION_CONTROL_CHECK_FOR_EXIT, context, 0)) return;
    double delay = retry < 9 ? 0.1 : 1.0;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        mission_control_poll_post(generation, poll_token, loop_generation, retry + 1);
    });
}

static void mission_control_schedule_poll(double delay)
{
    if (!g_mission_control_session.active || g_mission_control_session.poll_pending) return;
    g_mission_control_session.poll_pending = true;
    if (++g_mission_control_session.poll_token == 0) ++g_mission_control_session.poll_token;
    uint64_t generation = g_mission_control_session.generation;
    uint64_t poll_token = g_mission_control_session.poll_token;
    __atomic_store_n(&g_mission_control_poll_generation, generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_poll_token, poll_token, __ATOMIC_RELEASE);
#ifdef TESTS
    if (event_loop_test_mission_control_schedule) {
        event_loop_test_mission_control_schedule(generation, poll_token, delay);
        return;
    }
#endif
    uint64_t loop_generation = event_loop_generation_snapshot(&g_event_loop);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        mission_control_poll_post(generation, poll_token, loop_generation, 0);
    });
}

static bool update_window_notifications_append(uint32_t **window_list, size_t *window_count, size_t *window_capacity, uint32_t wid)
{
    if (*window_count == *window_capacity) {
        size_t new_capacity = *window_capacity ? *window_capacity * 2 : 128;
        if (new_capacity <= *window_capacity || new_capacity > (SIZE_MAX / sizeof(uint32_t))) return false;

        uint32_t *new_window_list = realloc(*window_list, sizeof(uint32_t) * new_capacity);
        if (!new_window_list) return false;

        *window_list = new_window_list;
        *window_capacity = new_capacity;
    }

    (*window_list)[(*window_count)++] = wid;
    return true;
}

#ifdef TESTS
static int g_window_notification_rebuild_count;
static bool g_window_notification_rebuild_result = true;

static void event_loop_test_mouse_drag_move_window_direct(struct window *window, float x, float y)
{
    if (!scripting_addition_move_window(window->id, x, y)) {
        window_manager_move_window(window, x, y);
    }
}

static void (*event_loop_test_mouse_drag_move_window)(struct window *, float, float) = event_loop_test_mouse_drag_move_window_direct;

static void event_loop_test_mouse_drag_resize_window_direct(struct window *window, CGRect frame)
{
    AX_ENHANCED_UI_WORKAROUND(window->application->ref, {
        window_manager_move_window(window, frame.origin.x, frame.origin.y);
        window_manager_resize_window(window, frame.size.width, frame.size.height);
    });
}

static void (*event_loop_test_mouse_drag_resize_window)(struct window *, CGRect) = event_loop_test_mouse_drag_resize_window_direct;
static uint64_t (*event_loop_test_mouse_drag_time)(void) = read_os_timer;
static void (*event_loop_test_mouse_drop_try_adjust_bsp_grid)(struct window_manager *, struct view *, struct window *, struct mouse_window_info *) = mouse_drop_try_adjust_bsp_grid;
static bool (*event_loop_test_native_resize_space_visible)(uint64_t);
static void (*event_loop_test_native_resize_schedule)(uint32_t, uint64_t);
static void (*event_loop_test_native_resize_restore)(uint32_t);
static bool (*event_loop_test_native_resize_live_frame)(struct window *, CGRect *);
static bool (*event_loop_test_managed_window_correction_live_frame)(struct window *, CGRect *);
static bool (*event_loop_test_managed_window_correction_space_visible)(uint64_t);
static void (*event_loop_test_managed_window_correction_schedule)(struct event_loop *, uint64_t, uint64_t);
static void (*event_loop_test_managed_window_correction_flush)(struct window_capture *);
static bool (*event_loop_test_managed_window_correction_admit)(struct event_loop *);
static void (*event_loop_test_managed_window_correction_leave)(struct event_loop *);
static AXError (*event_loop_test_managed_window_correction_hint_pid)(AXUIElementRef, pid_t *);
static uint64_t (*event_loop_test_frame_recovery_time)(void);
#endif

#define MANAGED_WINDOW_CORRECTION_DELAY_MS 50u
#define FRAME_CORRECTION_RETRY_WRITE_COUNT 10u
#define FRAME_CORRECTION_HINT_REARM_LIMIT 1u
#define FRAME_RECOVERY_UNOBSERVE_RETRY_LIMIT 5u
#define FRAME_RECOVERY_CLEANUP_COOLDOWN_NS (5ULL * 1000000000ULL)
#define NATIVE_RESIZE_SETTLED_QUIESCENCE_MS 16u
#define NATIVE_RESIZE_TRAILING_GRACE_MS 100u
#define NATIVE_RESIZE_TIMEOUT_MS 10000u
#define NATIVE_RESIZE_EDGE_TOLERANCE 6.0

static bool native_resize_space_visible(uint64_t sid)
{
#ifdef TESTS
    if (event_loop_test_native_resize_space_visible) return event_loop_test_native_resize_space_visible(sid);
#endif
    return space_is_visible(sid);
}

/* Cumulative deadlines for a bounded lease: ten write/readback attempts followed
 * by one readback-only probe. The sparse tail covers transient constraints
 * without continuously polling or depending on unrelated input events. */
static const uint64_t frame_correction_retry_delay_ms[] = {
    50, 150, 350, 750, 1500, 3000, 6000, 12000, 24000, 30000, 30150
};
static volatile uint64_t managed_window_correction_token_counter;
static const char *window_frame_correction_source_str[] = {
    [WINDOW_FRAME_CORRECTION_INITIAL] = "initial",
    [WINDOW_FRAME_CORRECTION_EVENT_HINT] = "event-hint",
    [WINDOW_FRAME_CORRECTION_APPLICATION_VISIBLE] = "application-visible"
};

static void managed_window_correction_log(struct window *window, const char *event,
                                          uint8_t retry_writes, double elapsed_ms)
{
    const char *source = window_frame_correction_source_str[window->frame_correction_source];
    debug("frame_correction: %s %s %d source=%s retry_writes=%u elapsed_ms=%.2f\n",
          event, window->application->name, window->id, source,
          (unsigned int) retry_writes, elapsed_ms);
#ifndef TESTS
    char payload[256];
    snprintf(payload, sizeof(payload),
             "%s app=%s window=%u source=%s retry_writes=%u elapsed_ms=%.2f",
             event, window->application->name, window->id, source,
             (unsigned int) retry_writes, elapsed_ms);
    syslog(LOG_NOTICE, "frame_correction: %s", payload);
#endif
}

static bool managed_window_correction_space_visible(uint64_t sid)
{
#ifdef TESTS
    if (event_loop_test_managed_window_correction_space_visible) {
        return event_loop_test_managed_window_correction_space_visible(sid);
    }
#endif
    return space_is_visible(sid);
}

static void managed_window_correction_flush(struct window_node *node, struct window *window)
{
    struct area area = node->area;
    if (node->has_window_areas) {
        bool found = false;
        for (int i = 0; i < node->window_count; ++i) {
            if (node->window_list[i] != window->id) continue;
            area = node->window_area[i];
            found = true;
            break;
        }
        if (!found) return;
    } else if (node->zoom) {
        area = node->zoom->area;
    }

    struct window_capture capture = {
        .window = window,
        .x = area.x,
        .y = area.y,
        .w = area.w,
        .h = area.h
    };
#ifdef TESTS
    if (event_loop_test_managed_window_correction_flush) {
        event_loop_test_managed_window_correction_flush(&capture);
        return;
    }
#endif
    window_manager_animate_window(capture);
}

static bool event_loop_admit_producer(struct event_loop *event_loop);
static void event_loop_leave_producer(struct event_loop *event_loop);

static bool managed_window_correction_admit_producer(struct event_loop *event_loop)
{
#ifdef TESTS
    if (event_loop_test_managed_window_correction_admit) {
        return event_loop_test_managed_window_correction_admit(event_loop);
    }
#endif
    return event_loop_admit_producer(event_loop);
}

static void managed_window_correction_leave_producer(struct event_loop *event_loop)
{
#ifdef TESTS
    if (event_loop_test_managed_window_correction_leave) {
        event_loop_test_managed_window_correction_leave(event_loop);
        return;
    }
#endif
    event_loop_leave_producer(event_loop);
}

static void managed_window_correction_fire(struct event_loop *event_loop, uint64_t generation,
                                           uint64_t schedule_token)
{
    if (!__atomic_compare_exchange_n(&event_loop->managed_window_correction_scheduled, &schedule_token, 0, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    if (!managed_window_correction_admit_producer(event_loop)) return;
    if (__atomic_load_n(&event_loop->generation, __ATOMIC_ACQUIRE) == generation) {
        __atomic_store_n(&event_loop->managed_window_correction_pending, 1, __ATOMIC_RELEASE);
        if (event_loop->semaphore) sem_post(event_loop->semaphore);
    }
    managed_window_correction_leave_producer(event_loop);
}

/* Replaced timers remain queued on the dispatch queue, so each timer gets a
 * unique token that prevents an older callback from claiming newer work. */
static uint64_t managed_window_correction_next_token(void)
{
    uint64_t token = __atomic_add_fetch(&managed_window_correction_token_counter, 1, __ATOMIC_RELAXED);
    if (!token) token = __atomic_add_fetch(&managed_window_correction_token_counter, 1, __ATOMIC_RELAXED);
    return token;
}

static void managed_window_correction_schedule_after(uint64_t delay_ms, bool supersede)
{
    if (!managed_window_correction_admit_producer(&g_event_loop)) return;
    uint64_t generation = event_loop_generation_snapshot(&g_event_loop);
    if (!generation) generation = 1;
    uint64_t token = managed_window_correction_next_token();
    if (supersede) {
        __atomic_store_n(&g_event_loop.managed_window_correction_scheduled, token, __ATOMIC_RELEASE);
    } else {
        uint64_t expected = 0;
        if (!__atomic_compare_exchange_n(&g_event_loop.managed_window_correction_scheduled, &expected, token, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            managed_window_correction_leave_producer(&g_event_loop);
            return;
        }
    }
#ifdef TESTS
    if (event_loop_test_managed_window_correction_schedule) {
        event_loop_test_managed_window_correction_schedule(&g_event_loop, generation, delay_ms);
        managed_window_correction_leave_producer(&g_event_loop);
        return;
    }
#endif
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ms * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        managed_window_correction_fire(&g_event_loop, generation, token);
    });
    managed_window_correction_leave_producer(&g_event_loop);
}

static void managed_window_correction_schedule(void)
{
    managed_window_correction_schedule_after(MANAGED_WINDOW_CORRECTION_DELAY_MS, false);
}

static void managed_window_correction_schedule_application_hint(struct application *application)
{
    if (!application || !(application->notification & AX_APPLICATION_FRAME_RECOVERY)) return;
    application->frame_recovery_hint_pending = true;
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&g_event_loop.managed_window_correction_hint_pending, &expected, 1, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        managed_window_correction_schedule_after(MANAGED_WINDOW_CORRECTION_DELAY_MS, true);
    }
}

struct managed_window_correction_hint_context
{
    AXObserverRef observer;
    AXUIElementRef element;
};

static struct managed_window_correction_hint_context *managed_window_correction_hint_context_create(AXObserverRef observer,
                                                                                                      AXUIElementRef element)
{
    if (!observer || !element) return NULL;
    struct managed_window_correction_hint_context *context = malloc(sizeof(*context));
    if (!context) return NULL;
    context->observer = (AXObserverRef) CFRetain(observer);
    context->element = (AXUIElementRef) CFRetain(element);
    return context;
}

static void managed_window_correction_hint_context_destroy(struct managed_window_correction_hint_context *context)
{
    if (!context) return;
    CFRelease(context->observer);
    CFRelease(context->element);
    free(context);
}

static AXError managed_window_correction_hint_element_pid(AXUIElementRef element, pid_t *pid)
{
#ifdef TESTS
    if (event_loop_test_managed_window_correction_hint_pid) {
        return event_loop_test_managed_window_correction_hint_pid(element, pid);
    }
#endif
    return AXUIElementGetPid(element, pid);
}

static struct application *managed_window_correction_hint_application(struct managed_window_correction_hint_context *context)
{
    pid_t pid = 0;
    if (!context || managed_window_correction_hint_element_pid(context->element, &pid) != kAXErrorSuccess) return NULL;
    struct application *application = window_manager_find_application(&g_window_manager, pid);
    return application && application->observer_ref == context->observer ? application : NULL;
}

void event_loop_post_managed_window_correction_hint(AXObserverRef observer, AXUIElementRef element)
{
    struct managed_window_correction_hint_context *context =
        managed_window_correction_hint_context_create(observer, element);
    if (context) (void) event_loop_try_post(&g_event_loop, MANAGED_WINDOW_CORRECTION_HINT, context, 0);
}

static EVENT_HANDLER(MANAGED_WINDOW_CORRECTION_HINT)
{
    struct managed_window_correction_hint_context *hint = context;
    managed_window_correction_schedule_application_hint(managed_window_correction_hint_application(hint));
    managed_window_correction_hint_context_destroy(hint);
}

static void managed_window_correction_clear(struct window *window);
static void managed_window_correction_schedule_application(struct application *application);

static uint32_t native_resize_next_generation(struct native_resize_state *state)
{
    if (++state->generation == 0) ++state->generation;
    return state->generation;
}

static void native_resize_schedule(uint32_t generation, uint64_t delay_ms)
{
#ifdef TESTS
    if (event_loop_test_native_resize_schedule) {
        event_loop_test_native_resize_schedule(generation, delay_ms);
        return;
    }
#endif
    uint64_t loop_generation = event_loop_generation_snapshot(&g_event_loop);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ms * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        (void) event_loop_try_post_generation(&g_event_loop, loop_generation,
                                               NATIVE_RESIZE_FINALIZE,
                                               (void *) (uintptr_t) generation, 0);
    });
}

static void native_resize_clear(struct mouse_state *mouse_state)
{
    struct native_resize_state *state = &mouse_state->native_resize;
    native_resize_next_generation(state);
    state->window_id = 0;
    state->down_location = CGPointZero;
    state->observed_origin = CGPointZero;
    state->observed_size = CGSizeZero;
    state->initial_frame = CGRectZero;
    state->latest_frame = CGRectZero;
    state->edges = 0;
    state->released = false;
    state->claimed = false;
    state->provisional = false;
    state->pending_size = false;
    state->saw_resize = false;
}

static void native_resize_restore_window(uint32_t window_id)
{
    if (!window_id) return;
#ifdef TESTS
    if (event_loop_test_native_resize_restore) {
        event_loop_test_native_resize_restore(window_id);
        return;
    }
#endif

    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window || !__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) return;
    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    struct window_node *node = view ? view_find_window_node(view, window->id) : NULL;
    if (!node) return;
    if (native_resize_space_visible(view->sid)) window_node_flush(node);
    else view_set_flag(view, VIEW_IS_DIRTY);
}

static void native_resize_abort(struct mouse_state *mouse_state, bool restore)
{
    uint32_t window_id = mouse_state->native_resize.window_id;
    bool claimed = mouse_state->native_resize.claimed;
    native_resize_clear(mouse_state);
    if (restore && claimed) native_resize_restore_window(window_id);
}

static bool native_resize_window_is_eligible(struct window *window, struct view *view,
                                             struct window_node *node)
{
    return g_mouse_state.native_edge_resize &&
           g_mouse_state.current_action == MOUSE_MODE_NONE &&
           g_mission_control_mode == MISSION_CONTROL_MODE_INACTIVE &&
           window && view && node && !node->zoom && node->window_count == 1 &&
           view->layout == VIEW_BSP && native_resize_space_visible(view->sid) &&
           g_window_manager.focused_window_id == window->id &&
           window_check_flag(window, WINDOW_RESIZABLE) &&
           !window_check_flag(window, WINDOW_FULLSCREEN) &&
           !window_check_flag(window, WINDOW_MINIMIZE);
}

static CGRect native_resize_node_frame(struct window_node *node)
{
    return CGRectMake(node->area.x, node->area.y, node->area.w, node->area.h);
}

static uint8_t native_resize_edges_at_point(CGPoint point, CGRect frame)
{
    if (point.x < CGRectGetMinX(frame) - NATIVE_RESIZE_EDGE_TOLERANCE ||
        point.x > CGRectGetMaxX(frame) + NATIVE_RESIZE_EDGE_TOLERANCE ||
        point.y < CGRectGetMinY(frame) - NATIVE_RESIZE_EDGE_TOLERANCE ||
        point.y > CGRectGetMaxY(frame) + NATIVE_RESIZE_EDGE_TOLERANCE) return 0;

    uint8_t edges = 0;
    if (fabs(point.x - CGRectGetMinX(frame)) <= NATIVE_RESIZE_EDGE_TOLERANCE) edges |= HANDLE_LEFT;
    if (fabs(point.x - CGRectGetMaxX(frame)) <= NATIVE_RESIZE_EDGE_TOLERANCE) edges |= HANDLE_RIGHT;
    if (fabs(point.y - CGRectGetMinY(frame)) <= NATIVE_RESIZE_EDGE_TOLERANCE) edges |= HANDLE_TOP;
    if (fabs(point.y - CGRectGetMaxY(frame)) <= NATIVE_RESIZE_EDGE_TOLERANCE) edges |= HANDLE_BOTTOM;
    return edges;
}

static void native_resize_candidate_begin(CGEventRef event)
{
    if (CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber) != kCGMouseButtonLeft) return;
    native_resize_abort(&g_mouse_state, true);
    if (!g_mouse_state.native_edge_resize || g_mouse_state.current_action != MOUSE_MODE_NONE ||
        g_mission_control_mode != MISSION_CONTROL_MODE_INACTIVE) return;

    struct window *window = window_manager_find_window(&g_window_manager, g_window_manager.focused_window_id);
    struct view *view = window ? window_manager_find_managed_window(&g_window_manager, window) : NULL;
    struct window_node *node = view && window ? view_find_window_node(view, window->id) : NULL;
    if (!native_resize_window_is_eligible(window, view, node)) return;

    CGRect frame = native_resize_node_frame(node);
    CGPoint point = CGEventGetLocation(event);
    uint8_t edges = native_resize_edges_at_point(point, frame);
    if (!edges) return;

    struct native_resize_state *state = &g_mouse_state.native_resize;
    state->window_id = window->id;
    state->down_location = point;
    state->observed_origin = frame.origin;
    state->observed_size = frame.size;
    state->initial_frame = frame;
    state->latest_frame = frame;
    state->edges = edges;
    state->released = false;
    state->claimed = false;
    state->provisional = false;
    state->pending_size = false;
    state->saw_resize = false;
    uint32_t generation = native_resize_next_generation(state);
    native_resize_schedule(generation, NATIVE_RESIZE_TIMEOUT_MS);
}

static bool native_resize_baseline_matches(const struct native_resize_state *state,
                                           struct window_node *node)
{
    if (!node) return false;
    CGRect frame = native_resize_node_frame(node);
    return !AX_DIFF(CGRectGetMinX(state->initial_frame), CGRectGetMinX(frame)) &&
           !AX_DIFF(CGRectGetMinY(state->initial_frame), CGRectGetMinY(frame)) &&
           !AX_DIFF(CGRectGetWidth(state->initial_frame), CGRectGetWidth(frame)) &&
           !AX_DIFF(CGRectGetHeight(state->initial_frame), CGRectGetHeight(frame));
}

static uint8_t native_resize_changed_edges(const struct native_resize_state *state, CGRect frame)
{
    uint8_t changed = 0;
    if (AX_DIFF(CGRectGetMinX(state->initial_frame), CGRectGetMinX(frame))) changed |= HANDLE_LEFT;
    if (AX_DIFF(CGRectGetMaxX(state->initial_frame), CGRectGetMaxX(frame))) changed |= HANDLE_RIGHT;
    if (AX_DIFF(CGRectGetMinY(state->initial_frame), CGRectGetMinY(frame))) changed |= HANDLE_TOP;
    if (AX_DIFF(CGRectGetMaxY(state->initial_frame), CGRectGetMaxY(frame))) changed |= HANDLE_BOTTOM;
    return changed;
}

static bool native_resize_geometry_matches(const struct native_resize_state *state, CGRect frame)
{
    uint8_t changed = native_resize_changed_edges(state, frame);
    return (changed & state->edges) != 0 && (changed & ~state->edges) == 0;
}

static bool native_resize_frames_match(CGRect a, CGRect b)
{
    return !AX_DIFF(a.origin.x, b.origin.x) && !AX_DIFF(a.origin.y, b.origin.y) &&
           !AX_DIFF(a.size.width, b.size.width) && !AX_DIFF(a.size.height, b.size.height);
}

static bool native_resize_read_ax_frame(struct window *window, CGRect *frame)
{
    return window_ax_frame_with_status(window, frame);
}

static bool native_resize_read_live_frame(struct window *window, CGRect *frame)
{
#ifdef TESTS
    if (event_loop_test_native_resize_live_frame) {
        return event_loop_test_native_resize_live_frame(window, frame);
    }
#endif
    return native_resize_read_ax_frame(window, frame);
}

static bool native_resize_geometry_is_provisional(const struct native_resize_state *state, CGRect frame)
{
    uint8_t changed = native_resize_changed_edges(state, frame);
    uint8_t provisional = 0;
    if (state->edges & HANDLE_LEFT) provisional |= HANDLE_RIGHT;
    if (state->edges & HANDLE_TOP) provisional |= HANDLE_BOTTOM;
    uint8_t allowed = state->edges | provisional;
    return (changed & provisional) != 0 && (changed & ~allowed) == 0;
}

static bool native_resize_has_settled_geometry(const struct native_resize_state *state)
{
    return state->claimed && state->saw_resize && !state->provisional && !state->pending_size &&
           native_resize_geometry_matches(state, state->latest_frame);
}

static uint64_t native_resize_post_release_delay(const struct native_resize_state *state)
{
    return native_resize_has_settled_geometry(state) ? NATIVE_RESIZE_SETTLED_QUIESCENCE_MS
                                                      : NATIVE_RESIZE_TRAILING_GRACE_MS;
}

static bool native_resize_move_matches(const struct native_resize_state *state, CGPoint origin)
{
    bool moved_x = AX_DIFF(CGRectGetMinX(state->initial_frame), origin.x);
    bool moved_y = AX_DIFF(CGRectGetMinY(state->initial_frame), origin.y);
    if (!moved_x && !moved_y) return false;
    if (moved_x && !(state->edges & HANDLE_LEFT)) return false;
    if (moved_y && !(state->edges & HANDLE_TOP)) return false;
    return true;
}

static bool native_resize_observe(struct window *window, struct view *view,
                                  struct window_node *node, CGRect frame, bool resized)
{
    struct native_resize_state *state = &g_mouse_state.native_resize;
    if (!state->window_id || state->window_id != window->id) return false;
    if (!native_resize_window_is_eligible(window, view, node) ||
        !native_resize_baseline_matches(state, node)) {
        native_resize_abort(&g_mouse_state, false);
        return false;
    }

    bool settled = false;
    if (resized) {
        if (native_resize_geometry_matches(state, frame)) {
            state->observed_origin = frame.origin;
            state->observed_size = frame.size;
            state->latest_frame = frame;
            state->pending_size = false;
            settled = true;
        } else {
            if (!(state->edges & (HANDLE_LEFT | HANDLE_TOP))) {
                native_resize_abort(&g_mouse_state, false);
                return false;
            }
            state->observed_size = frame.size;
            state->pending_size = true;
            CGRect merged = { state->observed_origin, state->observed_size };
            if (native_resize_geometry_matches(state, merged)) {
                state->latest_frame = merged;
                state->pending_size = false;
                settled = true;
            } else if (!native_resize_geometry_is_provisional(state, frame) &&
                       !native_resize_geometry_is_provisional(state, merged)) {
                native_resize_abort(&g_mouse_state, false);
                return false;
            }
        }
        state->provisional = !settled;
        if (settled) state->saw_resize = true;
    } else {
        if (!native_resize_move_matches(state, frame.origin)) {
            native_resize_abort(&g_mouse_state, false);
            return false;
        }
        state->observed_origin = frame.origin;
        CGRect merged = { state->observed_origin, state->observed_size };
        if (state->pending_size && native_resize_geometry_matches(state, merged)) {
            state->latest_frame = merged;
            state->provisional = false;
            state->pending_size = false;
            state->saw_resize = true;
        } else {
            state->provisional = true;
        }
    }

    state->claimed = true;
    uint32_t generation = native_resize_next_generation(state);
    native_resize_schedule(generation, state->released ? native_resize_post_release_delay(state)
                                                        : NATIVE_RESIZE_TIMEOUT_MS);
    return true;
}

static bool native_resize_reconcile_live_frame(struct window *window, struct view *view,
                                                struct window_node *node, CGRect frame)
{
    struct native_resize_state *state = &g_mouse_state.native_resize;
    if (native_resize_geometry_matches(state, frame)) {
        return native_resize_observe(window, view, node, frame, true);
    }
    if (!native_resize_geometry_is_provisional(state, frame)) return false;

    bool origin_changed = AX_DIFF(state->observed_origin.x, frame.origin.x) ||
                          AX_DIFF(state->observed_origin.y, frame.origin.y);
    bool size_changed = AX_DIFF(state->observed_size.width, frame.size.width) ||
                        AX_DIFF(state->observed_size.height, frame.size.height);
    if (!origin_changed && !size_changed) return false;

    if (origin_changed && !native_resize_observe(window, view, node, frame, false)) return false;
    if (size_changed) {
        if (!g_mouse_state.native_resize.window_id) return false;
        if (!native_resize_observe(window, view, node, frame, true)) return false;
    }
    return true;
}

static void native_resize_mouse_up(int64_t button)
{
    struct native_resize_state *state = &g_mouse_state.native_resize;
    if (button != kCGMouseButtonLeft || !state->window_id) return;
    state->released = true;
    uint32_t generation = native_resize_next_generation(state);
    native_resize_schedule(generation, native_resize_post_release_delay(state));
}

static bool update_window_notifications(void)
{
#ifdef TESTS
    ++g_window_notification_rebuild_count;
    return g_window_notification_rebuild_result;
#endif

    size_t window_count = 0;
    size_t window_capacity = 0;
    uint32_t *window_list = NULL;
    bool success = true;

    if (workspace_is_macos_sequoia_or_newer()) {
        // NOTE(asmvik): Subscribe to all windows because of window_destroyed (and ordered) notifications
        table_for (struct window *window, g_window_manager.window, {
            if (success && !update_window_notifications_append(&window_list, &window_count, &window_capacity, window->id)) {
                success = false;
            }
        })
    } else {
        // NOTE(asmvik): Subscribe to windows that have a feedback_border because of window_ordered notifications
        table_for (struct window_node *node, g_window_manager.insert_feedback, {
            if (success && !update_window_notifications_append(&window_list, &window_count, &window_capacity, node->window_order[0])) {
                success = false;
            }
        })
    }

    if (success && !workspace_is_macos_sequoia_or_newer() &&
        g_window_manager.enable_active_window_highlight && g_window_manager.focused_window_id) {
        bool present = false;
        for (size_t i = 0; i < window_count; ++i) {
            if (window_list[i] == g_window_manager.focused_window_id) { present = true; break; }
        }
        if (!present) {
            success = update_window_notifications_append(&window_list, &window_count, &window_capacity,
                                                         g_window_manager.focused_window_id);
        }
    }

    if (success && window_count <= INT_MAX) {
        CGError request_result = SLSRequestNotificationsForWindows(g_connection, window_list, (int) window_count);
        success = request_result == kCGErrorSuccess;
        if (!success) {
            warn("%s: SLSRequestNotificationsForWindows rejected %zu windows with status %d\n",
                 __FUNCTION__, window_count, request_result);
        }
    } else {
        warn("%s: could not construct a bounded notification list for %zu windows\n", __FUNCTION__, window_count);
        success = false;
    }

    free(window_list);
    return success;
}

void event_loop_request_active_window_highlight_refresh(void)
{
    (void) event_loop_post(&g_event_loop, ACTIVE_WINDOW_HIGHLIGHT_REFRESH, NULL, 0);
}

void event_loop_active_window_highlight_target_did_change(void)
{
    if (!workspace_is_macos_sequoia_or_newer()) (void) update_window_notifications();
}

static bool window_focus_should_center_mouse(struct window *focused_window, struct window *window, uint32_t ffm_window_id)
{
    return window && ffm_window_id != window->id && window->is_eligible &&
           (!focused_window || focused_window->is_eligible);
}

static void window_did_receive_focus(struct window_manager *wm, struct mouse_state *ms, struct window *window)
{
    if (ms->native_resize.window_id && ms->native_resize.window_id != window->id) {
        native_resize_abort(ms, true);
    }
    bool focus_changed = wm->focused_window_id != window->id;
    struct window *focused_window = window_manager_find_window(wm, wm->focused_window_id);
    struct view *previous_view = focused_window ? window_manager_find_managed_window(wm, focused_window) : NULL;
    if (focused_window && focused_window != window && window_space(focused_window->id) == window_space(window->id)) {
        window_manager_set_window_opacity(wm, focused_window, g_window_manager.normal_window_opacity);
    }

    window_manager_set_window_opacity(wm, window, wm->active_window_opacity);

    if (wm->focused_window_id != window->id) {
        if (window_focus_should_center_mouse(focused_window, window, ms->ffm_window_id)) {
            window_manager_center_mouse(wm, window);
        }

        wm->last_window_id = wm->focused_window_id;
    }

    wm->focused_window_id = window->id;
    wm->focused_window_psn = window->application->psn;
    ms->ffm_window_id = 0;
    window_manager_refresh_active_window_highlight(wm);
    if (focus_changed) event_loop_active_window_highlight_target_did_change();

    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    if (focus_changed && previous_view && previous_view != view && view_layout_is_accordion(previous_view->layout)) {
        view_update(previous_view);
        view_flush(previous_view);
    }
    if (!view) return;

    if (focus_changed && view_layout_is_accordion(view->layout)) {
        view_update(view);
        view_flush(view);
        return;
    }

    struct window_node *node = view_find_window_node(view, window->id);
    if (node->window_count <= 1) return;

    window_node_make_window_order_head(node, window->id);
}

void event_loop_window_inventory_did_change(void)
{
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);

    if (workspace_is_macos_sequoia_or_newer()) {
        update_window_notifications();
    }
}

static struct window *event_loop_focus_fallback_focused_window(struct window_manager *wm)
{
    return window_manager_focused_window(wm);
}

static struct window *event_loop_focus_fallback_recent_window(struct window_manager *wm)
{
    return window_manager_find_recent_managed_window(wm);
}

static bool event_loop_focus_fallback_window_is_on_active_space(struct window *window)
{
    if (window_is_sticky(window->id)) return true;

    uint64_t active_sid = space_manager_active_space();
    uint64_t window_sid = window_space(window->id);
    return active_sid && window_sid && active_sid == window_sid;
}

static void event_loop_focus_fallback_focus_window(struct window *window)
{
    window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
}

#ifdef TESTS
static struct window *(*event_loop_focus_fallback_find_focused)(struct window_manager *) = event_loop_focus_fallback_focused_window;
static struct window *(*event_loop_focus_fallback_find_recent)(struct window_manager *) = event_loop_focus_fallback_recent_window;
static bool (*event_loop_focus_fallback_is_on_active_space)(struct window *) = event_loop_focus_fallback_window_is_on_active_space;
static void (*event_loop_focus_fallback_focus)(struct window *) = event_loop_focus_fallback_focus_window;
static void (*event_loop_focus_fallback_entry_test_hook)(void);
static void (*event_loop_focus_fallback_rejection_test_hook)(void);
#else
#define event_loop_focus_fallback_find_focused event_loop_focus_fallback_focused_window
#define event_loop_focus_fallback_find_recent event_loop_focus_fallback_recent_window
#define event_loop_focus_fallback_is_on_active_space event_loop_focus_fallback_window_is_on_active_space
#define event_loop_focus_fallback_focus event_loop_focus_fallback_focus_window
#endif

void event_loop_request_window_focus_fallback(void)
{
    if (g_window_manager.window_focus_fallback != WINDOW_FOCUS_FALLBACK_RECENT) return;
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&window_focus_fallback_reserved, &expected, 1, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    if (!event_loop_try_post(&g_event_loop, WINDOW_FOCUS_FALLBACK, NULL, 0)) {
#ifdef TESTS
        if (event_loop_focus_fallback_rejection_test_hook) event_loop_focus_fallback_rejection_test_hook();
#endif
    }
}

static void event_loop_request_window_focus_fallback_for_event(enum event_type type)
{
    if (type == WINDOW_DESTROYED || type == WINDOW_MINIMIZED || type == APPLICATION_HIDDEN) {
        event_loop_request_window_focus_fallback();
    }
}

static void application_window_collection_update_inventory(int previous_window_count, int window_count)
{
    if (previous_window_count != window_count) {
        event_loop_window_inventory_did_change();
    }
}

#define EVENT_LOOP_BATCHED_VIEW_LINEAR_LIMIT 32

#ifdef TESTS
static void *(*event_loop_batched_view_calloc)(size_t, size_t) = calloc;
static void (*event_loop_batched_view_update)(struct view *) = view_update;
static void (*event_loop_batched_view_flush)(struct view *) = view_flush;
#else
#define event_loop_batched_view_calloc calloc
#define event_loop_batched_view_update view_update
#define event_loop_batched_view_flush view_flush
#endif

static int event_loop_compact_unique_views_linear(struct view **views, int count)
{
    int unique_count = 0;
    for (int i = 0; i < count; ++i) {
        bool duplicate = false;
        for (int j = 0; j < unique_count; ++j) {
            if (views[j] == views[i]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) views[unique_count++] = views[i];
    }
    return unique_count;
}

static int event_loop_compact_unique_views(struct view **views, int count)
{
    if (count <= 1) return max(0, count);
    if (count <= EVENT_LOOP_BATCHED_VIEW_LINEAR_LIMIT) {
        return event_loop_compact_unique_views_linear(views, count);
    }

    size_t needed = (size_t) count * 2;
    size_t capacity = 64;
    while (capacity < needed) capacity *= 2;

    struct view **seen = event_loop_batched_view_calloc(capacity, sizeof(*seen));
    if (!seen) return event_loop_compact_unique_views_linear(views, count);

    int unique_count = 0;
    size_t mask = capacity - 1;
    for (int i = 0; i < count; ++i) {
        struct view *view = views[i];
        size_t slot = (((uintptr_t) view >> 4) * UINT64_C(11400714819323198485)) & mask;
        while (seen[slot] && seen[slot] != view) slot = (slot + 1) & mask;
        if (seen[slot]) continue;

        seen[slot] = view;
        views[unique_count++] = view;
    }

    free(seen);
    return unique_count;
}

static void event_loop_flush_batched_views(struct view **views, int count)
{
    count = event_loop_compact_unique_views(views, count);
    for (int i = 0; i < count; ++i) {
        struct view *view = views[i];
        if (!view_is_dirty(view)) continue;
        if (view_layout_is_accordion(view->layout)) event_loop_batched_view_update(view);
        event_loop_batched_view_flush(view);
    }
}

static bool event_loop_enqueue(struct event_loop *event_loop, enum event_type type, void *context, int param1);
static uint64_t process_manager_pack_psn(ProcessSerialNumber psn);

struct application_front_switched_event
{
    ProcessSerialNumber psn;
    pid_t pid;
    uint64_t generation;
    bool replay;
};

#ifdef TESTS
static void *(*event_loop_application_front_switched_alloc)(size_t) = malloc;
#define EVENT_LOOP_APPLICATION_FRONT_SWITCHED_ALLOC(size) event_loop_application_front_switched_alloc(size)
#else
#define EVENT_LOOP_APPLICATION_FRONT_SWITCHED_ALLOC(size) malloc(size)
#endif

static struct application_front_switched_event *event_loop_create_application_front_switched(struct process *process, uint64_t generation, bool replay)
{
    struct application_front_switched_event *event = EVENT_LOOP_APPLICATION_FRONT_SWITCHED_ALLOC(sizeof(struct application_front_switched_event));
    if (!event) return NULL;

    event->psn = process->psn;
    event->pid = process->pid;
    event->generation = generation;
    event->replay = replay;
    return event;
}

#ifdef TESTS
static bool (*event_loop_test_post_replayed_front_switched)(struct process *, uint64_t);
#endif

static bool event_loop_post_application_front_switched(struct process *process, uint64_t generation, bool replay)
{
    struct application_front_switched_event *event = event_loop_create_application_front_switched(process, generation, replay);
    if (!event) return false;
    return event_loop_try_post(&g_event_loop, APPLICATION_FRONT_SWITCHED, event, 0);
}

static bool event_loop_post_tracked_application_front_switched(struct process *process, uint64_t generation)
{
    if (event_loop_post_application_front_switched(process, generation, false)) return true;

    pthread_mutex_lock(&g_process_manager_front_switch_lock);
    uint64_t current_generation = __atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&process->terminated, __ATOMIC_RELAXED) && generation == current_generation) {
        window_manager_add_lost_front_switched_event(&g_window_manager, &process->psn, generation);
    }
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);
    event_loop_post_application_launched(process);
    return false;
}

static bool event_loop_replay_lost_front_switched(struct process *process)
{
    bool had_record = false;
    bool retry = false;
    pthread_mutex_lock(&g_process_manager_front_switch_lock);

    uint64_t generation = window_manager_find_lost_front_switched_event(&g_window_manager, &process->psn);
    if (!generation) goto out;
    had_record = true;

    uint64_t current_generation = __atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&process->terminated, __ATOMIC_RELAXED) || generation != current_generation) {
        window_manager_remove_lost_front_switched_event(&g_window_manager, &process->psn);
        goto out;
    }

#ifdef TESTS
    bool posted = event_loop_test_post_replayed_front_switched
                ? event_loop_test_post_replayed_front_switched(process, generation)
                : event_loop_post_application_front_switched(process, generation, true);
#else
    bool posted = event_loop_post_application_front_switched(process, generation, true);
#endif
    if (posted) window_manager_remove_lost_front_switched_event(&g_window_manager, &process->psn);
    else retry = true;

out:
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);
    if (retry) event_loop_post_application_launched(process);
    return had_record;
}

static void event_loop_converge_tracked_front_switched(struct process *process)
{
    if (event_loop_replay_lost_front_switched(process)) return;

    uint64_t identity = ((uint64_t) (uint32_t) process->psn.highLongOfPSN << 32) |
                        (uint32_t) process->psn.lowLongOfPSN;
    pthread_mutex_lock(&g_process_manager_front_switch_lock);
    uint64_t generation = __atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE);
    bool is_current = !process_is_terminated(process) &&
                      __atomic_load_n(&g_process_manager.front_switch_psn, __ATOMIC_ACQUIRE) == identity;
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);
    if (is_current) event_loop_post_tracked_application_front_switched(process, generation);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
void event_loop_process_application_windows(struct window **window_list, int window_count)
{
    uint32_t prev_window_id = g_window_manager.focused_window_id;

    uint64_t sid;
    bool default_origin = g_window_manager.window_origin_mode == WINDOW_ORIGIN_DEFAULT;

    if (!default_origin) {
        if (g_window_manager.window_origin_mode == WINDOW_ORIGIN_FOCUSED) {
            sid = g_space_manager.current_space_id;
        } else /* if (g_window_manager.window_origin_mode == WINDOW_ORIGIN_CURSOR) */ {
            sid = space_manager_cursor_space();
        }
    }

    int view_count = 0;
    struct view **view_list = ts_alloc_list(struct view *, window_count);

    for (int i = 0; i < window_count; ++i) {
        struct window *window = window_list[i];

        if (window_manager_should_manage_window(window) && !window_manager_find_managed_window(&g_window_manager, window)) {
            if (default_origin) sid = window_space(window->id);

            struct view *view = space_manager_find_view(&g_space_manager, sid);
            if (view && view->layout != VIEW_FLOAT) {
                // Batch tree changes, but reserve managed publication before mutation.
                enum managed_window_result managed_result = window_manager_tile_window_on_space(&g_space_manager,
                                                                                                  &g_window_manager,
                                                                                                  window, sid,
                                                                                                  prev_window_id, false);
                if (managed_result == MANAGED_WINDOW_FAILED) continue;
                view_set_flag(view, VIEW_IS_DIRTY);
                view_list[view_count++] = view;
                prev_window_id = window->id;
            }
        }

        if (window_manager_is_window_eligible(window)) {
            event_signal_push(SIGNAL_WINDOW_CREATED, window);
        }
    }

    //
    // @cleanup
    //
    // :AXBatching
    //
    // NOTE(asmvik): Flush previously batched operations if the view is marked as dirty.
    // This is necessary to make sure that we do not call the AX API for each modification to the tree.
    //

    event_loop_flush_batched_views(view_list, view_count);
}

#ifdef TESTS
static void *(*event_loop_test_create_running_application)(struct process *) = workspace_application_create_running_ns_application;
static bool (*event_loop_test_application_finished)(struct process *) = workspace_application_is_finished_launching;
static bool (*event_loop_test_application_observable)(struct process *) = workspace_application_is_observable;
static pid_t (*event_loop_test_ax_window_pid)(AXUIElementRef);
static bool (*event_loop_test_window_ref_is_window)(AXUIElementRef);
static struct application *(*event_loop_test_application_for_window_id)(uint32_t);
static void (*event_loop_test_adopt_created)(AXUIElementRef, struct application *);
#endif

static EVENT_HANDLER(APPLICATION_LAUNCHED)
{
    struct process *process = context;

    if (__atomic_load_n(&process->terminated, __ATOMIC_RELAXED)) {
        debug("%s: %s (%d) terminated during launch\n", __FUNCTION__, process->name, process->pid);
        window_manager_remove_lost_front_switched_event(&g_window_manager, &process->psn);
        return;
    }

    if (!__atomic_load_n(&process->ns_application, __ATOMIC_RELAXED)) {
        debug("%s: %s (%d) missing ns_application. fetching..\n", __FUNCTION__, process->name, process->pid);
#ifdef TESTS
        __atomic_store_n(&process->ns_application, event_loop_test_create_running_application(process), __ATOMIC_RELEASE);
#else
        __atomic_store_n(&process->ns_application, workspace_application_create_running_ns_application(process), __ATOMIC_RELEASE);
#endif

        if (!__atomic_load_n(&process->ns_application, __ATOMIC_RELAXED)) {
            debug("%s: %s (%d) unable to fetch ns_application..\n", __FUNCTION__, process->name, process->pid);

            event_loop_schedule_application_launch_retry(process);
            return;
        }
    }

#ifdef TESTS
    if (!event_loop_test_application_finished(process)) {
#else
    if (!workspace_application_is_finished_launching(process)) {
#endif
        debug("%s: %s (%d) is not finished launching, subscribing to finishedLaunching changes\n", __FUNCTION__, process->name, process->pid);
        workspace_application_observe_finished_launching(g_workspace_context, process);

        //
        // NOTE(asmvik): Do this again in case of race-conditions between the previous check and key-value observation subscription.
        // Not actually sure if this can happen in practice..
        //

#ifdef TESTS
        if (event_loop_test_application_finished(process)) {
#else
        if (workspace_application_is_finished_launching(process)) {
#endif
            @try {
                NSRunningApplication *application = __atomic_load_n(&process->ns_application, __ATOMIC_RELAXED);
                if (application && [application observationInfo]) {
                    [application removeObserver:g_workspace_context forKeyPath:@"finishedLaunching" context:process];
                }
            } @catch (NSException * __unused exception) {}
        } else { return; }
    }

#ifdef TESTS
    if (!event_loop_test_application_observable(process)) {
#else
    if (!workspace_application_is_observable(process)) {
#endif
        debug("%s: %s (%d) is not observable, subscribing to activationPolicy changes\n", __FUNCTION__, process->name, process->pid);
        workspace_application_observe_activation_policy(g_workspace_context, process);

        //
        // NOTE(asmvik): Do this again in case of race-conditions between the previous check and key-value observation subscription.
        // Not actually sure if this can happen in practice..
        //

#ifdef TESTS
        if (event_loop_test_application_observable(process)) {
#else
        if (workspace_application_is_observable(process)) {
#endif
            @try {
                NSRunningApplication *application = __atomic_load_n(&process->ns_application, __ATOMIC_RELAXED);
                if (application && [application observationInfo]) {
                    [application removeObserver:g_workspace_context forKeyPath:@"activationPolicy" context:process];
                }
            } @catch (NSException * __unused exception) {}
        } else { return; }
    }

    //
    // NOTE(asmvik): If we somehow receive a duplicate launched event due to the subscription-timing-mess above,
    // simply ignore the event..
    //

    struct application *application = window_manager_find_application(&g_window_manager, process->pid);
    if (application) {
        event_loop_converge_tracked_front_switched(process);
        return;
    } else {
        application = application_create(process);
    }
    if (!application) {
        event_loop_schedule_application_launch_retry(process);
        return;
    }

    if (!application_observe(application)) {
        bool ax_retry = application->ax_retry;

        application_unobserve(application);
        application_destroy(application);
        debug("%s: could not observe notifications for %s (%d) (%d)\n", __FUNCTION__, process->name, process->pid, ax_retry);

        if (ax_retry) event_loop_schedule_application_launch_retry(process);

        return;
    }

    debug("%s: %s (%d)\n", __FUNCTION__, process->name, process->pid);
    if (!window_manager_add_application(&g_window_manager, application)) {
        application_unobserve(application);
        application_destroy(application);
        event_loop_schedule_application_launch_retry(process);
        return;
    }
    event_loop_converge_tracked_front_switched(process);
    process_launch_retries_reset(process);
    event_signal_push(SIGNAL_APPLICATION_LAUNCHED, application);

    int previous_window_count = g_window_manager.window.count;
    int window_count;
    struct window **window_list = window_manager_add_application_windows(&g_space_manager, &g_window_manager, application, &window_count);

    //
    // NOTE: The AX API only returns windows on visible spaces. An application that restores its windows on
    // an inactive space while launching (e.g. relaunch after an update) would otherwise never have them tracked.
    //

    application_window_collection_update_inventory(previous_window_count, g_window_manager.window.count);
    event_loop_process_application_windows(window_list, window_count);
    buf_free(window_list);
}

static void event_loop_clear_window_identity(struct window_manager *wm, uint32_t window_id)
{
    window_manager_remove_lost_focused_event(wm, window_id);
    if (wm->focused_window_id == window_id) {
        wm->focused_window_id = 0;
        wm->focused_window_psn = (ProcessSerialNumber) {0};
        window_manager_hide_active_window_highlight(wm);
        event_loop_active_window_highlight_target_did_change();
    }
    if (wm->last_window_id == window_id) wm->last_window_id = 0;
}

static void event_loop_clear_terminated_front_identity(struct process *process)
{
    if (psn_equals(&g_process_manager.front_psn, &process->psn)) {
        g_process_manager.front_pid = 0;
        g_process_manager.front_psn = (ProcessSerialNumber) {0};
    }
    if (psn_equals(&g_process_manager.last_front_psn, &process->psn)) {
        g_process_manager.last_front_pid = 0;
        g_process_manager.last_front_psn = (ProcessSerialNumber) {0};
    }
}

static EVENT_HANDLER(APPLICATION_TERMINATED)
{
    struct process *process = context;
    bool destroyed_windows = false;
    window_manager_remove_lost_front_switched_event(&g_window_manager, &process->psn);

    struct application *application = window_manager_find_application(&g_window_manager, process->pid);

    if (!application || !psn_equals(&application->psn, &process->psn)) {
        debug("%s: %s (%d) (not observed or stale)\n", __FUNCTION__, process->name, process->pid);
        event_loop_clear_terminated_front_identity(process);
        goto out;
    }

    debug("%s: %s (%d)\n", __FUNCTION__, process->name, process->pid);
    event_signal_push(SIGNAL_APPLICATION_TERMINATED, application);
    event_loop_clear_terminated_front_identity(process);
    window_manager_ffm_application_terminated(&g_window_manager, application);
    window_manager_remove_application(&g_window_manager, application->pid);
    rule_cancel_one_shots(application);
    application->launch_refresh = false;

    window_manager_remove_application_refresh(&g_window_manager, application);

    int window_count;
    struct window **window_list = window_manager_find_application_windows(&g_window_manager, application, &window_count);
    destroyed_windows = window_count > 0;

    int view_count = 0;
    struct view **view_list = ts_alloc_list(struct view *, window_count);

    for (int i = 0; i < window_count; ++i) {
        struct window *window = window_list[i];

        if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, NULL)) {
            window->application = NULL;
            continue;
        }

        struct view *view = window_manager_find_managed_window(&g_window_manager, window);
        if (view) {

            //
            // @cleanup
            //
            // :AXBatching
            //
            // NOTE(asmvik): Batch all operations and mark the view as dirty so that we can perform a single flush,
            // making sure that each window is only moved and resized a single time, when the final layout has been computed.
            // This is necessary to make sure that we do not call the AX API for each modification to the tree.
            //

            view_remove_window_node(view, window);
            window_manager_remove_managed_window(&g_window_manager, window->id);

            view_set_flag(view, VIEW_IS_DIRTY);
            view_list[view_count++] = view;
        }

        if (g_mouse_state.window == window) g_mouse_state.window = NULL;
        if (g_mouse_state.native_resize.window_id == window->id) native_resize_clear(&g_mouse_state);
        if (g_mouse_state.ffm_window_id == window->id) g_mouse_state.ffm_window_id = 0;

        if (window->is_eligible) {
            event_signal_push(SIGNAL_WINDOW_DESTROYED, window);
        }
        event_loop_clear_window_identity(&g_window_manager, window->id);

        window_manager_remove_scratchpad_for_window(&g_window_manager, window, false);
        window_manager_remove_window(&g_window_manager, window->id);
        window_unobserve(window);
        window_destroy(window);
    }

    application_unobserve(application);
    application_destroy(application);
    application_window_collection_update_inventory(window_count, 0);

    //
    // @cleanup
    //
    // :AXBatching
    //
    // NOTE(asmvik): Flush previously batched operations if the view is marked as dirty.
    // This is necessary to make sure that we do not call the AX API for each modification to the tree.
    //

    event_loop_flush_batched_views(view_list, view_count);

out:
    if (destroyed_windows) event_loop_request_window_focus_fallback_for_event(WINDOW_DESTROYED);
    process_destroy(process);
}

static struct application *event_loop_find_processed_front_application(void)
{
    struct application *application = window_manager_find_application(&g_window_manager, g_process_manager.front_pid);
    if (!application || !psn_equals(&application->psn, &g_process_manager.front_psn)) return NULL;
    return application;
}

static void event_loop_handle_window_created(AXUIElementRef window_ref, struct application *application,
                                             bool one_shot_rules, pid_t one_shot_owner_pid);

static struct application *event_loop_application_for_window_id(uint32_t window_id)
{
#ifdef TESTS
    return event_loop_test_application_for_window_id
         ? event_loop_test_application_for_window_id(window_id)
         : NULL;
#else
    int connection = 0;
    pid_t pid = 0;
    if (SLSGetWindowOwner(g_connection, window_id, &connection) != kCGErrorSuccess ||
        SLSConnectionGetPID(connection, &pid) != kCGErrorSuccess || pid <= 0) return NULL;
    return window_manager_find_application(&g_window_manager, pid);
#endif
}

static bool event_loop_try_adopt_focused_window(struct application *application, uint32_t window_id)
{
    if (!application || !window_id || application->adopt_attempted_window_id == window_id) return false;
    AXUIElementRef window_ref = window_manager_copy_application_window_ref(application, window_id);
    if (!window_ref) return false;

    application->adopt_attempted_window_id = window_id;
    debug("%s: %s %d\n", __FUNCTION__, application->name, window_id);
#ifdef TESTS
    if (event_loop_test_adopt_created) {
        event_loop_test_adopt_created(window_ref, application);
        return true;
    }
#endif
    event_loop_handle_window_created(window_ref, application, true, 0);
    return true;
}

static void event_loop_application_front_update_focus(struct application *application)
{
    uint32_t focused_window_id = application_focused_window(application);
    if (!focused_window_id) {
        struct window *focused_window = window_manager_find_window(&g_window_manager, g_window_manager.focused_window_id);
        if (focused_window) {
            window_manager_set_window_opacity(&g_window_manager, focused_window, g_window_manager.normal_window_opacity);
        }

        g_window_manager.last_window_id = g_window_manager.focused_window_id;
        g_window_manager.focused_window_id = 0;
        g_window_manager.focused_window_psn = application->psn;
        g_mouse_state.ffm_window_id = 0;
        window_manager_hide_active_window_highlight(&g_window_manager);
        event_loop_active_window_highlight_target_did_change();
        return;
    }

    struct window *window = window_manager_find_window(&g_window_manager, focused_window_id);
    if (!window) {
        struct window *focused_window = window_manager_find_window(&g_window_manager, g_window_manager.focused_window_id);
        if (focused_window) {
            window_manager_set_window_opacity(&g_window_manager, focused_window, g_window_manager.normal_window_opacity);
        }

        window_manager_add_lost_focused_event(&g_window_manager, focused_window_id);
        window_manager_hide_active_window_highlight(&g_window_manager);
        (void) event_loop_try_adopt_focused_window(application, focused_window_id);
        return;
    }

    application->adopt_attempted_window_id = 0;
    (void) window_manager_refresh_window_ref(&g_window_manager, window);
    window_did_receive_focus(&g_window_manager, &g_mouse_state, window);
    event_signal_push(SIGNAL_WINDOW_FOCUSED, window);
}

static void event_loop_application_front_push_signal(enum signal_type type, void *context)
{
    event_signal_push(type, context);
}

#ifdef TESTS
static void (*event_loop_update_application_front_focus)(struct application *) = event_loop_application_front_update_focus;
static void (*event_loop_application_front_signal)(enum signal_type, void *) = event_loop_application_front_push_signal;
#else
#define event_loop_update_application_front_focus event_loop_application_front_update_focus
#define event_loop_application_front_signal event_loop_application_front_push_signal
#endif

static bool event_loop_commit_application_front_switched(struct application_front_switched_event *event, struct application **application, struct application **deactivated_application)
{
    *application = NULL;
    *deactivated_application = NULL;

    pthread_mutex_lock(&g_process_manager_front_switch_lock);
    uint64_t current_generation = __atomic_load_n(&g_process_manager.front_switch_generation, __ATOMIC_ACQUIRE);
    if (event->generation != current_generation ||
        event->generation == __atomic_load_n(&g_process_manager.committed_front_switch_generation, __ATOMIC_ACQUIRE)) goto fail;

    *application = window_manager_find_application(&g_window_manager, event->pid);
    if (!*application) {
        if (!event->replay) window_manager_add_lost_front_switched_event(&g_window_manager, &event->psn, event->generation);
        goto fail;
    }
    if (!psn_equals(&(*application)->psn, &event->psn)) goto fail;

    window_manager_remove_lost_front_switched_event(&g_window_manager, &event->psn);
    __atomic_store_n(&g_process_manager.committed_front_switch_generation, event->generation, __ATOMIC_RELEASE);
    *deactivated_application = event_loop_find_processed_front_application();
    g_process_manager.switch_event_time = GetCurrentEventTime();
    g_process_manager.last_front_pid = g_process_manager.front_pid;
    g_process_manager.last_front_psn = g_process_manager.front_psn;
    g_process_manager.front_pid = event->pid;
    g_process_manager.front_psn = event->psn;
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);
    return true;

fail:
    pthread_mutex_unlock(&g_process_manager_front_switch_lock);
    return false;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static EVENT_HANDLER(APPLICATION_FRONT_SWITCHED)
{
    struct application_front_switched_event event = *(struct application_front_switched_event *) context;
    free(context);

    struct application *application;
    struct application *deactivated_application;
    if (!event_loop_commit_application_front_switched(&event, &application, &deactivated_application)) return;
    native_resize_abort(&g_mouse_state, true);

    // Native macOS owns the ordinary activation transition; do not add an automatic PSN assignment or gesture here.
    // Window resolution below may still apply explicit user-authored rules, including follow and fullscreen effects.
    // Application destruction is serialized on this event-loop worker, so both captured pointers remain valid here.
    if (deactivated_application) event_loop_application_front_signal(SIGNAL_APPLICATION_DEACTIVATED, deactivated_application);

    debug("%s: %s (%d)\n", __FUNCTION__, application->name, event.pid);
    event_loop_application_front_signal(SIGNAL_APPLICATION_ACTIVATED, application);
    event_loop_application_front_signal(SIGNAL_APPLICATION_FRONT_SWITCHED, NULL);

    for (int i = 0; i < buf_len(g_window_manager.applications_to_refresh); ++i) {
        if (application == g_window_manager.applications_to_refresh[i]) {
            debug("%s: %s has windows that are not yet resolved\n", __FUNCTION__, application->name);
            int previous_window_count = g_window_manager.window.count;
            int window_count = 0;
            struct window **window_list = NULL;
            window_manager_begin_inactive_probe_pass(4096);
            window_manager_add_existing_application_windows(&g_space_manager, &g_window_manager, application, i, &window_list, &window_count);
            // Resolution controls retry state independently: a false result can still add windows.
            application_window_collection_update_inventory(previous_window_count, g_window_manager.window.count);
            event_loop_process_application_windows(window_list, window_count);
            buf_free(window_list);
            break;
        }
    }

    event_loop_update_application_front_focus(application);
}
#pragma clang diagnostic pop

static EVENT_HANDLER(APPLICATION_VISIBLE)
{
    struct application *application = window_manager_find_application(&g_window_manager, (pid_t)(intptr_t) context);
    if (!application) return;

    debug("%s: %s\n", __FUNCTION__, application->name);
    application->is_hidden = false;

    int window_count;
    struct window **window_list = window_manager_find_application_windows(&g_window_manager, application, &window_count);
    uint32_t prev_window_id = g_window_manager.last_window_id;

    int view_count = 0;
    struct view **view_list = ts_alloc_list(struct view *, window_count);

    for (int i = 0; i < window_count; ++i) {
        struct window *window = window_list[i];

        if (window_manager_should_manage_window(window) && !window_manager_find_managed_window(&g_window_manager, window)) {
            struct view *view = space_manager_find_view(&g_space_manager, window_space(window->id));
            if (!view || view->layout == VIEW_FLOAT) continue;

            //
            // @cleanup
            //
            // :AXBatching
            //
            // NOTE(asmvik): Batch all operations and mark the view as dirty so that we can perform a single flush,
            // making sure that each window is only moved and resized a single time, when the final layout has been computed.
            // This is necessary to make sure that we do not call the AX API for each modification to the tree.
            //

            enum managed_window_result managed_result = window_manager_tile_window_on_space(&g_space_manager,
                                                                                              &g_window_manager,
                                                                                              window, view->sid,
                                                                                              prev_window_id, false);
            if (managed_result == MANAGED_WINDOW_FAILED) continue;

            view_set_flag(view, VIEW_IS_DIRTY);
            view_list[view_count++] = view;

            prev_window_id = window->id;
        }
    }

    //
    // @cleanup
    //
    // :AXBatching
    //
    // NOTE(asmvik): Flush previously batched operations if the view is marked as dirty.
    // This is necessary to make sure that we do not call the AX API for each modification to the tree.
    //

    event_loop_flush_batched_views(view_list, view_count);
    managed_window_correction_schedule_application(application);

    window_manager_refresh_active_window_highlight(&g_window_manager);
    event_signal_push(SIGNAL_APPLICATION_VISIBLE, application);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
}

static EVENT_HANDLER(APPLICATION_HIDDEN)
{
    struct application *application = window_manager_find_application(&g_window_manager, (pid_t)(intptr_t) context);
    if (!application) {
        event_loop_request_window_focus_fallback_for_event(APPLICATION_HIDDEN);
        return;
    }

    debug("%s: %s\n", __FUNCTION__, application->name);
    struct window *native_resize_window = window_manager_find_window(
        &g_window_manager, g_mouse_state.native_resize.window_id);
    if (native_resize_window && native_resize_window->application == application) {
        native_resize_clear(&g_mouse_state);
    }
    application->is_hidden = true;
    window_manager_refresh_active_window_highlight(&g_window_manager);

    int window_count;
    struct window **window_list = window_manager_find_application_windows(&g_window_manager, application, &window_count);

    int view_count = 0;
    struct view **view_list = ts_alloc_list(struct view *, window_count);

    for (int i = 0; i < window_count; ++i) {
        struct window *window = window_list[i];

        struct view *view = window_manager_find_managed_window(&g_window_manager, window);
        if (view) {

            //
            // @cleanup
            //
            // :AXBatching
            //
            // NOTE(asmvik): Batch all operations and mark the view as dirty so that we can perform a single flush,
            // making sure that each window is only moved and resized a single time, when the final layout has been computed.
            // This is necessary to make sure that we do not call the AX API for each modification to the tree.
            //

            window_manager_adjust_layer(window, LAYER_NORMAL);
            view_remove_window_node(view, window);
            window_manager_remove_managed_window(&g_window_manager, window->id);
            window_manager_purify_window(&g_window_manager, window);

            view_set_flag(view, VIEW_IS_DIRTY);
            view_list[view_count++] = view;
        }
    }

    //
    // @cleanup
    //
    // :AXBatching
    //
    // NOTE(asmvik): Flush previously batched operations if the view is marked as dirty.
    // This is necessary to make sure that we do not call the AX API for each modification to the tree.
    //

    event_loop_flush_batched_views(view_list, view_count);

    event_signal_push(SIGNAL_APPLICATION_HIDDEN, application);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
    event_loop_request_window_focus_fallback_for_event(APPLICATION_HIDDEN);
}

static bool event_loop_window_ref_is_window(AXUIElementRef window_ref)
{
#ifdef TESTS
    if (event_loop_test_window_ref_is_window) return event_loop_test_window_ref_is_window(window_ref);
#endif
    CFTypeRef role = NULL;
    bool result = AXUIElementCopyAttributeValue(window_ref, kAXRoleAttribute, &role) == kAXErrorSuccess &&
                  role && CFEqual(role, kAXWindowRole);
    if (role) CFRelease(role);
    return result;
}

static void event_loop_handle_window_created(AXUIElementRef window_ref, struct application *application, bool one_shot_rules, pid_t one_shot_owner_pid)
{
    uint32_t window_id = window_manager_ax_window_id(window_ref);
    if (!window_id) { CFRelease(window_ref); return; }

    struct window *existing_window = window_manager_find_window(&g_window_manager, window_id);
    if (existing_window) {
        if (event_loop_window_ref_is_window(window_ref)) {
            (void) window_manager_replace_window_ref(&g_window_manager, existing_window, window_ref);
        } else {
            CFRelease(window_ref);
        }
        return;
    }

    if (!application) {
#ifdef TESTS
        pid_t window_pid = event_loop_test_ax_window_pid
                         ? event_loop_test_ax_window_pid(window_ref) : ax_window_pid(window_ref);
#else
        pid_t window_pid = ax_window_pid(window_ref);
#endif
        if (window_pid) application = window_manager_find_application(&g_window_manager, window_pid);
    }
    if (!application) { CFRelease(window_ref); return; }

    bool retry = false;
    struct window *window = window_manager_create_and_add_window(&g_space_manager, &g_window_manager, application, window_ref, window_id, one_shot_rules, one_shot_owner_pid, &retry);
    if (!window && retry) window_manager_schedule_window_created_retry(window_ref, application, one_shot_rules, one_shot_owner_pid);
    if (!window) return;
    application->adopt_attempted_window_id = 0;

    rule_cleanup_one_shots();

    if (window_manager_should_manage_window(window) && !window_manager_find_managed_window(&g_window_manager, window)) {
        uint64_t sid;

        if (g_window_manager.window_origin_mode == WINDOW_ORIGIN_DEFAULT) {
            sid = window_space(window->id);
        } else if (g_window_manager.window_origin_mode == WINDOW_ORIGIN_FOCUSED) {
            sid = g_space_manager.current_space_id;
        } else /* if (g_window_manager.window_origin_mode == WINDOW_ORIGIN_CURSOR) */ {
            sid = space_manager_cursor_space();
        }

        enum managed_window_result managed_result = window_manager_tile_window_on_space(&g_space_manager, &g_window_manager, window, sid, 0, true);
        if (managed_result == MANAGED_WINDOW_FAILED) return;
    }

    if (window_manager_is_window_eligible(window)) {
        event_signal_push(SIGNAL_WINDOW_CREATED, window);
    }

    event_loop_window_inventory_did_change();
}

static EVENT_HANDLER(WINDOW_CREATED)
{
    event_loop_handle_window_created(context, NULL, true, 0);
}

static EVENT_HANDLER(WINDOW_CREATED_RETRY)
{
    struct window_created_retry *retry = context;
    struct application *application = window_manager_resolve_window_created_retry(&g_window_manager, retry);
    AXUIElementRef window_ref = retry->window_ref;
    if (!application) {
        window_manager_destroy_window_created_retry(retry);
        return;
    }
    pid_t one_shot_owner_pid = window_manager_window_created_retry_owner(application, retry);
    bool one_shot_rules = window_manager_window_created_retry_uses_one_shots(application, retry);
    retry->window_ref = NULL;
    window_manager_destroy_window_created_retry(retry);
    event_loop_handle_window_created(window_ref, application, one_shot_rules, one_shot_owner_pid);
}

static EVENT_HANDLER(WINDOW_DESTROYED)
{
    struct window *window = context;
    if (!window || window->id == 0) {
        debug("%s: window has already been destroyed, ignoring event..\n", __FUNCTION__);
        event_loop_request_window_focus_fallback_for_event(WINDOW_DESTROYED);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application ? window->application->name : "<unknown>", window->id);
    managed_window_correction_clear(window);

    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    if (view) {
        space_manager_untile_window(view, window);
        window_manager_remove_managed_window(&g_window_manager, window->id);
    }

    if (g_mouse_state.window == window) g_mouse_state.window = NULL;
    if (g_mouse_state.native_resize.window_id == window->id) native_resize_clear(&g_mouse_state);
    if (g_mouse_state.ffm_window_id == window->id) g_mouse_state.ffm_window_id = 0;

    if (window->is_eligible) {
        event_signal_push(SIGNAL_WINDOW_DESTROYED, window);
    }
    event_loop_clear_window_identity(&g_window_manager, window->id);

    window_manager_remove_scratchpad_for_window(&g_window_manager, window, false);
    window_manager_remove_window(&g_window_manager, window->id);
    window_unobserve(window);
    window_destroy(window);

    event_loop_window_inventory_did_change();
    event_loop_request_window_focus_fallback_for_event(WINDOW_DESTROYED);
}

static void event_loop_consume_window_focus(int ownership)
{
    if (ownership == 1) (void) event_loop_pending_consume(&__pending_window_focus);
}

static bool event_loop_window_focus_application_is_frontmost(struct application *application)
{
    return application_is_frontmost(application);
}

static void event_loop_window_focus_receive(struct window *window)
{
    window_did_receive_focus(&g_window_manager, &g_mouse_state, window);
}

static void event_loop_window_focus_signal(struct window *window)
{
    event_signal_push(SIGNAL_WINDOW_FOCUSED, window);
}

static void event_loop_window_focus_mark_menu_dirty(void)
{
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_FOCUS);
}

#ifdef TESTS
static bool (*event_loop_window_focus_is_frontmost)(struct application *) = event_loop_window_focus_application_is_frontmost;
static void (*event_loop_window_focus_did_receive)(struct window *) = event_loop_window_focus_receive;
static void (*event_loop_window_focus_push_signal)(struct window *) = event_loop_window_focus_signal;
static void (*event_loop_window_focus_menu_dirty)(void) = event_loop_window_focus_mark_menu_dirty;
#else
#define event_loop_window_focus_is_frontmost event_loop_window_focus_application_is_frontmost
#define event_loop_window_focus_did_receive event_loop_window_focus_receive
#define event_loop_window_focus_push_signal event_loop_window_focus_signal
#define event_loop_window_focus_menu_dirty event_loop_window_focus_mark_menu_dirty
#endif

static EVENT_HANDLER(WINDOW_FOCUSED)
{
    event_loop_consume_window_focus(param1);
    uint32_t window_id = (uint32_t)(intptr_t) context;

    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) {
        window_manager_add_lost_focused_event(&g_window_manager, window_id);
        struct application *application = event_loop_application_for_window_id(window_id);
        if (application) (void) event_loop_try_adopt_focused_window(application, window_id);
        return;
    }

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    if (window_check_flag(window, WINDOW_MINIMIZE)) {
        window_manager_add_lost_focused_event(&g_window_manager, window->id);
        return;
    }

    if (!event_loop_window_focus_is_frontmost(window->application)) {
        return;
    }

    window->application->adopt_attempted_window_id = 0;
    (void) window_manager_refresh_window_ref(&g_window_manager, window);

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);

    event_loop_window_focus_did_receive(window);
    event_loop_window_focus_push_signal(window);
    event_loop_window_focus_menu_dirty();
}

static bool managed_window_frame_differs(struct window_node *node, uint32_t window_id, CGRect frame)
{
    struct area *area = &node->area;
    if (node->has_window_areas) {
        for (int i = 0; i < node->window_count; ++i) {
            if (node->window_list[i] == window_id) {
                area = &node->window_area[i];
                break;
            }
        }
    }

    bool differs = AX_DIFF(area->x, frame.origin.x) ||
                   AX_DIFF(area->y, frame.origin.y) ||
                   AX_DIFF(area->w, frame.size.width) ||
                   AX_DIFF(area->h, frame.size.height);
    if (node->has_window_areas || !node->zoom) return differs;

    return differs &&
           (AX_DIFF(node->zoom->area.x, frame.origin.x) ||
            AX_DIFF(node->zoom->area.y, frame.origin.y) ||
            AX_DIFF(node->zoom->area.w, frame.size.width) ||
            AX_DIFF(node->zoom->area.h, frame.size.height));
}

static bool managed_window_correction_live_frame(struct window *window, CGRect *frame)
{
#ifdef TESTS
    if (event_loop_test_managed_window_correction_live_frame) {
        return event_loop_test_managed_window_correction_live_frame(window, frame);
    }
    *frame = window->frame;
    return true;
#else
    return window_ax_frame_with_status(window, frame);
#endif
}

static uint64_t frame_recovery_time(void)
{
#ifdef TESTS
    if (event_loop_test_frame_recovery_time) return event_loop_test_frame_recovery_time();
#endif
    return read_os_timer();
}

static bool frame_recovery_cleanup_retry_ready(struct application *application)
{
    if (application->frame_recovery_unobserve_retries < FRAME_RECOVERY_UNOBSERVE_RETRY_LIMIT) return true;
    uint64_t now = frame_recovery_time();
    if (application->frame_recovery_cleanup_retry_time > now) return false;
    application->frame_recovery_unobserve_retries = 0;
    application->frame_recovery_cleanup_retry_time = 0;
    return true;
}

static void frame_recovery_record_cleanup_failure(struct application *application)
{
    if (application->frame_recovery_unobserve_retries < FRAME_RECOVERY_UNOBSERVE_RETRY_LIMIT) {
        ++application->frame_recovery_unobserve_retries;
    }
    if (application->frame_recovery_unobserve_retries < FRAME_RECOVERY_UNOBSERVE_RETRY_LIMIT) {
        managed_window_correction_schedule();
    } else {
        application->frame_recovery_cleanup_retry_time = frame_recovery_time() + FRAME_RECOVERY_CLEANUP_COOLDOWN_NS;
    }
}

static void managed_window_correction_start_retries(struct window *window,
                                                    enum window_frame_correction_source source)
{
    uint64_t now = frame_recovery_time();
    window->frame_correction_retry_active = true;
    window->frame_correction_retry_index = 0;
    window->frame_correction_source = source;
    window->frame_correction_started_at = now;
    window->frame_correction_retry_at = now + frame_correction_retry_delay_ms[0] * 1000000ULL;
    managed_window_correction_log(window, "start", 0, 0.0);
}

static void managed_window_correction_log_converged(struct window *window, uint64_t now)
{
    managed_window_correction_log(window, "converged", window->frame_correction_retry_index,
                                  os_timer_elapsed_milliseconds(window->frame_correction_started_at, now));
}

static void managed_window_correction_advance_retry(struct window *window, uint64_t now)
{
    if (window->frame_correction_retry_index < FRAME_CORRECTION_RETRY_WRITE_COUNT) {
        ++window->frame_correction_retry_index;
        window->frame_correction_retry_at = window->frame_correction_started_at +
            frame_correction_retry_delay_ms[window->frame_correction_retry_index] * 1000000ULL;
    } else {
        window->frame_correction_retry_active = false;
        managed_window_correction_log(window, "exhausted", window->frame_correction_retry_index,
                                      os_timer_elapsed_milliseconds(window->frame_correction_started_at, now));
    }
}

static void managed_window_correction_clear(struct window *window)
{
    if (!window->frame_correction_pending) return;
    window->frame_correction_pending = false;
    window->frame_correction_retry_active = false;
    window->frame_correction_retry_index = 0;
    window->frame_correction_hint_rearms = 0;
    window->frame_correction_started_at = 0;
    window->frame_correction_retry_at = 0;
    if (!window->application) return;

    int window_count = 0;
    struct window **window_list = window_manager_find_application_windows(&g_window_manager,
                                                                          window->application,
                                                                          &window_count);
    for (int i = 0; i < window_count; ++i) {
        if (window_list[i]->frame_correction_pending) return;
    }
    window->application->frame_recovery_hint_pending = false;
    window->application->frame_recovery_hint_active = false;
    if (!application_unobserve_frame_recovery(window->application)) {
        frame_recovery_record_cleanup_failure(window->application);
    }
}

void event_loop_cancel_managed_window_correction(struct window *window)
{
    if (window) managed_window_correction_clear(window);
}

static void managed_window_correction_update(struct window *window, struct view *view,
                                             struct window_node *node, bool differs, bool native_owned)
{
    if (!node || native_owned) {
        managed_window_correction_clear(window);
        return;
    }
    if (!differs) {
        if (window->frame_correction_pending) {
            managed_window_correction_log_converged(window, frame_recovery_time());
        }
        managed_window_correction_clear(window);
        return;
    }

    bool newly_pending = !window->frame_correction_pending;
    if (newly_pending) {
        window->frame_correction_pending = true;
        managed_window_correction_start_retries(window, WINDOW_FRAME_CORRECTION_INITIAL);
        window->application->frame_recovery_unobserve_retries = 0;
        window->application->frame_recovery_cleanup_retry_time = 0;
    }
    bool observes_recovery = application_observe_frame_recovery(window->application);
    if (newly_pending || !observes_recovery) {
        managed_window_correction_schedule_after(MANAGED_WINDOW_CORRECTION_DELAY_MS, newly_pending);
    }
    if (managed_window_correction_space_visible(view->sid)) managed_window_correction_flush(node, window);
    else view_set_flag(view, VIEW_IS_DIRTY);
}

static bool managed_window_correction_application_has_pending(struct application *application)
{
    int window_count = 0;
    struct window **window_list = window_manager_find_application_windows(&g_window_manager, application, &window_count);
    for (int i = 0; i < window_count; ++i) {
        if (window_list[i]->frame_correction_pending) return true;
    }
    return false;
}

static void managed_window_correction_schedule_application(struct application *application)
{
    int window_count = 0;
    bool has_pending = false;
    struct window **window_list = window_manager_find_application_windows(&g_window_manager, application, &window_count);
    for (int i = 0; i < window_count; ++i) {
        if (!window_list[i]->frame_correction_pending) continue;
        window_list[i]->frame_correction_hint_rearms = 0;
        managed_window_correction_start_retries(window_list[i], WINDOW_FRAME_CORRECTION_APPLICATION_VISIBLE);
        has_pending = true;
    }
    if (has_pending) {
        managed_window_correction_schedule_after(MANAGED_WINDOW_CORRECTION_DELAY_MS, true);
    } else if (frame_recovery_cleanup_retry_ready(application) &&
               !application_unobserve_frame_recovery(application)) {
        frame_recovery_record_cleanup_failure(application);
    }
}

static EVENT_HANDLER(WINDOW_MOVED)
{
    uint32_t window_id = (uint32_t)(intptr_t) context;
    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    if (window->application->is_hidden) {
        debug("%s: %d was moved while the application is hidden, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    CGPoint new_origin = window_ax_origin(window);
    if (CGPointEqualToPoint(new_origin, window->frame.origin)) {
        debug("%s:DEBOUNCED %s %d\n", __FUNCTION__, window->application->name, window->id);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);
    event_signal_push(SIGNAL_WINDOW_MOVED, window);
    bool windowed_fullscreen = CGRectEqualToRect(window->windowed_frame, window->frame);
    window->frame.origin = new_origin;
    if (window->id == g_window_manager.focused_window_id) {
        window_manager_refresh_active_window_highlight(&g_window_manager);
    }

    if (!windowed_fullscreen) {
        window_clear_flag(window, WINDOW_WINDOWED);

        if (!g_mouse_state.window || g_mouse_state.window != window) {
            struct view *view = window_manager_find_managed_window(&g_window_manager, window);
            struct window_node *node = view ? view_find_window_node(view, window->id) : NULL;
            bool differs = node &&
                (AX_DIFF(node->area.x, new_origin.x) || AX_DIFF(node->area.y, new_origin.y)) &&
                (!node->zoom || AX_DIFF(node->zoom->area.x, new_origin.x) ||
                               AX_DIFF(node->zoom->area.y, new_origin.y));
            bool native_owned = false;
            if (g_mouse_state.native_resize.window_id == window->id) {
                native_owned = native_resize_observe(window, view, node, window->frame, false);
            }
            managed_window_correction_update(window, view, node, differs, native_owned);
        } else {
            managed_window_correction_clear(window);
        }
    }
}

static bool window_resize_should_debounce(bool was_fullscreen, bool is_fullscreen,
                                          CGRect current_frame, CGRect new_frame)
{
    return was_fullscreen == is_fullscreen && CGRectEqualToRect(current_frame, new_frame);
}

static EVENT_HANDLER(WINDOW_RESIZED)
{
    uint32_t window_id = (uint32_t)(intptr_t) context;
    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    if (window->application->is_hidden) {
        debug("%s: %d was resized while the application is hidden, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    CGRect new_frame = window_ax_frame(window);
    bool was_fullscreen = window_check_flag(window, WINDOW_FULLSCREEN);
    bool is_fullscreen = window_is_fullscreen(window);
    if (window_resize_should_debounce(was_fullscreen, is_fullscreen, window->frame, new_frame)) {
        debug("%s:DEBOUNCED %s %d\n", __FUNCTION__, window->application->name, window->id);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);
    event_signal_push(SIGNAL_WINDOW_RESIZED, window);

    if (is_fullscreen) {
        window_set_flag(window, WINDOW_FULLSCREEN);
    } else {
        window_clear_flag(window, WINDOW_FULLSCREEN);
    }

    if (was_fullscreen != is_fullscreen) {
        window_constraints_invalidate(&window->constraints);
        if (window_ax_can_move(window)) {
            window_set_flag(window, WINDOW_MOVABLE);
        } else {
            window_clear_flag(window, WINDOW_MOVABLE);
        }

        if (window_ax_can_resize(window)) {
            window_set_flag(window, WINDOW_RESIZABLE);
        } else {
            window_clear_flag(window, WINDOW_RESIZABLE);
        }

        if (window->role) CFRelease(window->role);
        window->role = window_ax_role(window);

        if (window->subrole) CFRelease(window->subrole);
        window->subrole = window_ax_subrole(window);

        menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_SPACE);
    }

    bool windowed_fullscreen = CGRectEqualToRect(window->windowed_frame, window->frame);
    window->frame = new_frame;
    if (window->id == g_window_manager.focused_window_id) {
        window_manager_refresh_active_window_highlight_for_frame(&g_window_manager, window->id, window->frame);
    }

    if (was_fullscreen != is_fullscreen) {
        managed_window_correction_clear(window);
        if (g_mouse_state.native_resize.window_id == window->id) {
            native_resize_abort(&g_mouse_state, false);
        }
    }

    if (!was_fullscreen && is_fullscreen) {
        struct view *view = window_manager_find_managed_window(&g_window_manager, window);
        if (view) {
            space_manager_untile_window(view, window);
            window_manager_remove_managed_window(&g_window_manager, window->id);
            window_manager_purify_window(&g_window_manager, window);
        }
    } else if (was_fullscreen && !is_fullscreen) {
        if (!window_manager_wait_for_native_fullscreen_transition(window, false)) {
            menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_SPACE | MENU_BAR_DIRTY_WINDOW);
            declarative_configuration_request_reconcile();
            return;
        }

        if (window_ax_can_move(window)) window_set_flag(window, WINDOW_MOVABLE);
        else window_clear_flag(window, WINDOW_MOVABLE);
        if (window_ax_can_resize(window)) window_set_flag(window, WINDOW_RESIZABLE);
        else window_clear_flag(window, WINDOW_RESIZABLE);
        if (window->role) CFRelease(window->role);
        window->role = window_ax_role(window);
        if (window->subrole) CFRelease(window->subrole);
        window->subrole = window_ax_subrole(window);
        window->frame = window_ax_frame(window);

        if (window_manager_should_manage_window(window) && !window_manager_find_managed_window(&g_window_manager, window)) {
            enum managed_window_result managed_result = window_manager_tile_window_on_space(&g_space_manager, &g_window_manager,
                                                                                             window, window_space(window->id), 0, true);
            if (managed_result == MANAGED_WINDOW_FAILED) return;
        }
    } else if (!was_fullscreen == !is_fullscreen) {
        if (g_mouse_state.current_action == MOUSE_MODE_MOVE && g_mouse_state.window == window) {
            g_mouse_state.window_frame.size = g_mouse_state.window->frame.size;
        }

        if (!windowed_fullscreen) {
            window_clear_flag(window, WINDOW_WINDOWED);

            if (!g_mouse_state.window || g_mouse_state.window != window) {
                struct view *view = window_manager_find_managed_window(&g_window_manager, window);
                struct window_node *node = view ? view_find_window_node(view, window->id) : NULL;
                bool differs = node &&
                    (AX_DIFF(node->area.x, new_frame.origin.x)   ||
                     AX_DIFF(node->area.y, new_frame.origin.y)   ||
                     AX_DIFF(node->area.w, new_frame.size.width) ||
                     AX_DIFF(node->area.h, new_frame.size.height)) &&
                    (!node->zoom || AX_DIFF(node->zoom->area.x, new_frame.origin.x)   ||
                                   AX_DIFF(node->zoom->area.y, new_frame.origin.y)   ||
                                   AX_DIFF(node->zoom->area.w, new_frame.size.width) ||
                                   AX_DIFF(node->zoom->area.h, new_frame.size.height));
                bool native_owned = false;
                if (g_mouse_state.native_resize.window_id == window->id) {
                    native_owned = native_resize_observe(window, view, node, new_frame, true);
                }
                managed_window_correction_update(window, view, node, differs, native_owned);
            } else {
                managed_window_correction_clear(window);
            }
        }
    }
}

static void managed_window_correction_retry_window(struct window *window, bool hinted, uint64_t now)
{
    if (!window->frame_correction_pending ||
        !__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) return;
    if (g_mouse_state.window == window || g_mouse_state.native_resize.window_id == window->id) {
        managed_window_correction_clear(window);
        return;
    }
    if (window->application->is_hidden) {
        window->frame_correction_retry_active = false;
        return;
    }
    if (window_check_flag(window, WINDOW_FULLSCREEN) || window_check_flag(window, WINDOW_MINIMIZE)) {
        managed_window_correction_clear(window);
        return;
    }
    if (hinted && !window->frame_correction_retry_active &&
        window->frame_correction_hint_rearms < FRAME_CORRECTION_HINT_REARM_LIMIT) {
        ++window->frame_correction_hint_rearms;
        managed_window_correction_start_retries(window, WINDOW_FRAME_CORRECTION_EVENT_HINT);
    }
    if (!window->frame_correction_retry_active || window->frame_correction_retry_at > now) return;

    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    struct window_node *node = view ? view_find_window_node(view, window->id) : NULL;
    if (!node) {
        managed_window_correction_clear(window);
        return;
    }

    CGRect live_frame;
    if (!managed_window_correction_live_frame(window, &live_frame)) {
        bool observes_recovery = application_observe_frame_recovery(window->application);
        managed_window_correction_advance_retry(window, now);
        if (!observes_recovery) managed_window_correction_clear(window);
        return;
    }

    window->frame = live_frame;
    if (!managed_window_frame_differs(node, window->id, live_frame)) {
        managed_window_correction_log_converged(window, now);
        managed_window_correction_clear(window);
        return;
    }

    bool observes_recovery = application_observe_frame_recovery(window->application);
    if (!managed_window_correction_space_visible(view->sid)) {
        view_set_flag(view, VIEW_IS_DIRTY);
        window->frame_correction_retry_active = false;
    } else {
        if (window->frame_correction_retry_index < FRAME_CORRECTION_RETRY_WRITE_COUNT) {
            managed_window_correction_flush(node, window);
        }
        managed_window_correction_advance_retry(window, now);
    }
    if (!observes_recovery) managed_window_correction_clear(window);
}

static bool managed_window_correction_consume(struct event_loop *event_loop)
{
    if (!__atomic_exchange_n(&event_loop->managed_window_correction_pending, 0, __ATOMIC_ACQ_REL)) return false;

    bool hinted = __atomic_exchange_n(&event_loop->managed_window_correction_hint_pending, 0, __ATOMIC_ACQ_REL);
    uint64_t now = frame_recovery_time();
    table_for (struct application *application, g_window_manager.application, {
        application->frame_recovery_hint_active = hinted && application->frame_recovery_hint_pending;
        application->frame_recovery_hint_pending = false;
        if ((application->notification & AX_APPLICATION_FRAME_RECOVERY) &&
            !managed_window_correction_application_has_pending(application) &&
            frame_recovery_cleanup_retry_ready(application) &&
            !application_unobserve_frame_recovery(application)) {
            frame_recovery_record_cleanup_failure(application);
        }
    })
    uint64_t next_retry_at = 0;
    table_for (struct window *window, g_window_manager.window, {
        bool window_hint = window->application && window->application->frame_recovery_hint_active;
        managed_window_correction_retry_window(window, window_hint, now);
        if (window->frame_correction_pending && window->frame_correction_retry_active &&
            (!next_retry_at || window->frame_correction_retry_at < next_retry_at)) {
            next_retry_at = window->frame_correction_retry_at;
        }
    })
    table_for (struct application *application, g_window_manager.application, {
        application->frame_recovery_hint_active = false;
    })
    if (next_retry_at) {
        uint64_t delay_ns = next_retry_at > now ? next_retry_at - now : 0;
        uint64_t delay_ms = (delay_ns + NSEC_PER_MSEC - 1) / NSEC_PER_MSEC;
        managed_window_correction_schedule_after(delay_ms ? delay_ms : 1, false);
    }
    return true;
}

static EVENT_HANDLER(WINDOW_MINIMIZED)
{
    uint32_t window_id = (uint32_t)(intptr_t) context;
    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) {
        event_loop_request_window_focus_fallback_for_event(WINDOW_MINIMIZED);
        return;
    }

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window->id);
        event_loop_request_window_focus_fallback_for_event(WINDOW_MINIMIZED);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);
    if (g_mouse_state.native_resize.window_id == window->id) native_resize_clear(&g_mouse_state);
    managed_window_correction_clear(window);
    window_set_flag(window, WINDOW_MINIMIZE);
    window_constraints_invalidate(&window->constraints);
    if (window->id == g_window_manager.focused_window_id) {
        window_manager_refresh_active_window_highlight(&g_window_manager);
    }

    if (window_ax_can_move(window)) {
        window_set_flag(window, WINDOW_MOVABLE);
    } else {
        window_clear_flag(window, WINDOW_MOVABLE);
    }

    if (window_ax_can_resize(window)) {
        window_set_flag(window, WINDOW_RESIZABLE);
    } else {
        window_clear_flag(window, WINDOW_RESIZABLE);
    }

    if (window->role) CFRelease(window->role);
    window->role = window_ax_role(window);

    if (window->subrole) CFRelease(window->subrole);
    window->subrole = window_ax_subrole(window);

    if (window->id == g_window_manager.last_window_id) {
        g_window_manager.last_window_id = g_window_manager.focused_window_id;
    }

    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    if (view) {
        space_manager_untile_window(view, window);
        window_manager_remove_managed_window(&g_window_manager, window->id);
        window_manager_purify_window(&g_window_manager, window);
    }

    event_signal_push(SIGNAL_WINDOW_MINIMIZED, window);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
    event_loop_request_window_focus_fallback_for_event(WINDOW_MINIMIZED);
}

static EVENT_HANDLER(WINDOW_DEMINIMIZED)
{
    uint32_t window_id = (uint32_t)(intptr_t) context;
    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) {
        window_manager_remove_lost_focused_event(&g_window_manager, window_id);
        return;
    }

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window->id);
        window_manager_remove_lost_focused_event(&g_window_manager, window->id);
        return;
    }

    window_clear_flag(window, WINDOW_MINIMIZE);
    window_constraints_invalidate(&window->constraints);
    if (window->id == g_window_manager.focused_window_id) {
        window_manager_refresh_active_window_highlight(&g_window_manager);
    }

    if (window_ax_can_move(window)) {
        window_set_flag(window, WINDOW_MOVABLE);
    } else {
        window_clear_flag(window, WINDOW_MOVABLE);
    }

    if (window_ax_can_resize(window)) {
        window_set_flag(window, WINDOW_RESIZABLE);
    } else {
        window_clear_flag(window, WINDOW_RESIZABLE);
    }

    if (window->role) CFRelease(window->role);
    window->role = window_ax_role(window);

    if (window->subrole) CFRelease(window->subrole);
    window->subrole = window_ax_subrole(window);

    uint64_t sid = space_manager_active_space();
    if (space_manager_is_window_on_space(sid, window)) {
        debug("%s: window %s %d is deminimized on active space\n", __FUNCTION__, window->application->name, window->id);
        if (window_manager_should_manage_window(window) && !window_manager_find_managed_window(&g_window_manager, window)) {
            struct window *last_window = window_manager_find_window(&g_window_manager, g_window_manager.last_window_id);
            uint32_t insertion_point = last_window && last_window->application->pid != window->application->pid ? last_window->id : 0;
            enum managed_window_result managed_result = window_manager_tile_window_on_space(&g_space_manager, &g_window_manager,
                                                                                             window, sid, insertion_point, true);
            if (managed_result == MANAGED_WINDOW_FAILED) return;
        }
    } else {
        debug("%s: window %s %d is deminimized on inactive space\n", __FUNCTION__, window->application->name, window->id);
    }

    if (window_manager_find_lost_focused_event(&g_window_manager, window->id)) {
        event_loop_post(&g_event_loop, WINDOW_FOCUSED, (void *)(intptr_t) window->id, 0);
        window_manager_remove_lost_focused_event(&g_window_manager, window->id);
    }

    event_signal_push(SIGNAL_WINDOW_DEMINIMIZED, window);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
}

static EVENT_HANDLER(WINDOW_TITLE_CHANGED)
{
    uint32_t window_id = (uint32_t)(intptr_t) context;
    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);

    if (window->title) CFRelease(window->title);

    window->title = window_title(window);

    event_signal_push(SIGNAL_WINDOW_TITLE_CHANGED, window);
}

static EVENT_HANDLER(ACTIVE_WINDOW_HIGHLIGHT_REFRESH)
{
    (void) context;
    (void) param1;
    window_manager_refresh_active_window_highlight(&g_window_manager);
}

static EVENT_HANDLER(SLS_WINDOW_ORDERED)
{
    uint32_t wid = (uint64_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, wid);
    struct window_node *node = table_find(&g_window_manager.insert_feedback, &wid);
    if (node) SLSOrderWindow(g_connection, node->feedback_window.id, 1, node->window_order[0]);
    if (wid == g_window_manager.focused_window_id) {
        window_manager_reorder_active_window_highlight(&g_window_manager);
    }
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
}

#ifdef TESTS
static CGError (*event_loop_window_is_ordered_in)(int, uint32_t, uint8_t *) = SLSWindowIsOrderedIn;
static uint64_t *(*event_loop_debug_window_space_list)(uint32_t, int *, bool *) =
    window_space_list_with_status;
static bool (*event_loop_debug_window_is_on_screen)(uint32_t, bool *) = window_is_on_screen;
static bool (*event_loop_debug_window_parent)(uint32_t, uint32_t *) = window_parent_with_status;
static int (*event_loop_debug_window_level)(uint32_t) = window_level;
static int (*event_loop_debug_window_sub_level)(uint32_t) = window_sub_level;
static bool (*event_loop_debug_window_tags)(uint32_t, uint64_t *) = window_tags_with_status;
static struct window_server_snapshot *(*event_loop_debug_snapshot_create)(int, CFArrayRef, int) =
    window_server_snapshot_create;
static void (*event_loop_debug_snapshot_destroy)(struct window_server_snapshot *) =
    window_server_snapshot_destroy;
#else
#define event_loop_window_is_ordered_in SLSWindowIsOrderedIn
#define event_loop_debug_window_space_list window_space_list_with_status
#define event_loop_debug_window_is_on_screen window_is_on_screen
#define event_loop_debug_window_parent window_parent_with_status
#define event_loop_debug_window_level window_level
#define event_loop_debug_window_sub_level window_sub_level
#define event_loop_debug_window_tags window_tags_with_status
#define event_loop_debug_snapshot_create window_server_snapshot_create
#define event_loop_debug_snapshot_destroy window_server_snapshot_destroy
#endif

static void event_loop_debug_window_visibility(const char *event_name, uint32_t wid,
                                               bool ordered_known, bool ordered_in)
{
    if (!g_verbose) return;

    int space_count = 0;
    bool spaces_known = false;
    uint64_t *space_list = event_loop_debug_window_space_list(wid, &space_count, &spaces_known);
    bool on_screen_known = false;
    bool on_screen = event_loop_debug_window_is_on_screen(wid, &on_screen_known);

    CFArrayRef window_ids = cfarray_of_cfnumbers(&wid, sizeof(wid), 1, kCFNumberSInt32Type);
    struct window_server_snapshot *window_server_snapshot = window_ids
        ? event_loop_debug_snapshot_create(g_connection, window_ids, 1)
        : NULL;
    if (window_ids) CFRelease(window_ids);
    const struct window_server_window_record *server_record =
        window_server_snapshot_find(window_server_snapshot, wid);

    int level = server_record && (server_record->valid_fields & WINDOW_SERVER_WINDOW_FIELD_LEVEL)
              ? server_record->level
              : event_loop_debug_window_level(wid);
    uint32_t parent_wid = 0;
    bool parent_known = false;
    if (server_record && (server_record->valid_fields & WINDOW_SERVER_WINDOW_FIELD_PARENT_ID)) {
        parent_wid = server_record->parent_id;
        parent_known = true;
    } else {
        parent_known = event_loop_debug_window_parent(wid, &parent_wid);
    }
    uint64_t tags = 0;
    bool tags_known = false;
    if (server_record && (server_record->valid_fields & WINDOW_SERVER_WINDOW_FIELD_TAGS)) {
        tags = server_record->tags;
        tags_known = true;
    } else {
        tags_known = event_loop_debug_window_tags(wid, &tags);
    }

    fprintf(stdout,
            "%s: wid=%u ordered-in=%s on-screen=%s level=%d sub-level=%d parent=",
            event_name, wid,
            ordered_known ? json_bool(ordered_in) : "unknown",
            on_screen_known ? json_bool(on_screen) : "unknown",
            level, event_loop_debug_window_sub_level(wid));
    if (parent_known) fprintf(stdout, "%u", parent_wid); else fputs("unknown", stdout);
    fputs(" tags=", stdout);
    if (tags_known) fprintf(stdout, "0x%016llx", (unsigned long long) tags); else fputs("unknown", stdout);
    fprintf(stdout, " active-space=%llu spaces-known=%s spaces=[",
            (unsigned long long) space_manager_active_space(), json_bool(spaces_known));
    if (spaces_known) {
        for (int i = 0; i < space_count; ++i) {
            if (i) fputc(',', stdout);
            fprintf(stdout, "%llu", (unsigned long long) space_list[i]);
        }
    }
    fputs("]\n", stdout);
    event_loop_debug_snapshot_destroy(window_server_snapshot);
}

// NOTE: Apps that hide a window on close (e.g. Electron) order it out without minimizing it or
// hiding the app, so none of those events arrives to untile it; the window server's own visibility
// notifications do. The ordered-in state must be checked: switching spaces sends 816 for every window
// on the space being left although they stay ordered in (seen on macOS 27), and the events are queued,
// so the window may have been ordered back in (or out) by the time one is handled.
static EVENT_HANDLER(SLS_WINDOW_IS_INVISIBLE)
{
    uint32_t wid = (uint64_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, wid);

    struct window *window = window_manager_find_window(&g_window_manager, wid);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, wid);
        return;
    }

    uint8_t ordered_in = 0;
    CGError ordered_result = event_loop_window_is_ordered_in(g_connection, wid, &ordered_in);
    event_loop_debug_window_visibility("SLS_WINDOW_IS_INVISIBLE", wid,
                                       ordered_result == kCGErrorSuccess, ordered_in != 0);
    if (ordered_result != kCGErrorSuccess) return;
    if (ordered_in) return;

    window_manager_window_did_order_out(&g_window_manager, window);
    if (wid == g_window_manager.focused_window_id) {
        window_manager_refresh_active_window_highlight(&g_window_manager);
    }
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
}

static EVENT_HANDLER(SLS_WINDOW_IS_VISIBLE)
{
    uint32_t wid = (uint64_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, wid);

    struct window *window = window_manager_find_window(&g_window_manager, wid);
    if (!window || (!g_verbose && !window_check_flag(window, WINDOW_ORDERED_OUT))) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, wid);
        return;
    }

    uint8_t ordered_in = 0;
    CGError ordered_result = event_loop_window_is_ordered_in(g_connection, wid, &ordered_in);
    event_loop_debug_window_visibility("SLS_WINDOW_IS_VISIBLE", wid,
                                       ordered_result == kCGErrorSuccess, ordered_in != 0);
    if (ordered_result != kCGErrorSuccess) return;
    if (!ordered_in || !window_check_flag(window, WINDOW_ORDERED_OUT)) return;

    (void) window_manager_refresh_window_ref(&g_window_manager, window);
    uint64_t sid = space_manager_active_space();
    window_manager_window_did_order_in(&g_window_manager, window, space_manager_is_window_on_space(sid, window) ? sid : 0);
    if (wid == g_window_manager.focused_window_id) {
        window_manager_refresh_active_window_highlight(&g_window_manager);
    }
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
}

static EVENT_HANDLER(SLS_WINDOW_DESTROYED)
{
    uint32_t wid = (uint64_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, wid);

    struct window *window = window_manager_find_window(&g_window_manager, wid);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, wid);
        return;
    }

    EVENT_HANDLER_WINDOW_DESTROYED(window, 0);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
}

static void event_loop_reserve_topology_resync(struct event_loop *event_loop);
#ifdef TESTS
static int (*event_loop_test_space_type)(uint64_t);
static uint32_t (*event_loop_test_active_display_id)(void);
#endif

static void event_loop_topology_lifecycle_failed(void)
{
    event_loop_reserve_topology_resync(&g_event_loop);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_DISPLAY | MENU_BAR_DIRTY_SPACE);
    declarative_configuration_request_reconcile();
}

static EVENT_HANDLER(SLS_SPACE_CREATED)
{
    uint64_t sid = (uint64_t)(intptr_t) context;
#ifdef TESTS
    int type = event_loop_test_space_type ? event_loop_test_space_type(sid) : SLSSpaceGetType(g_connection, sid);
#else
    int type = SLSSpaceGetType(g_connection, sid);
#endif

    if (type == 0 || type == 4) {
        debug("%s: %lld, %d\n", __FUNCTION__, sid, type);
        if (!space_manager_find_view(&g_space_manager, sid)) {
            event_loop_topology_lifecycle_failed();
            return;
        }
        event_signal_push(SIGNAL_SPACE_CREATED, context);
        menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_SPACE);
        declarative_configuration_request_reconcile();
    }
}

static EVENT_HANDLER(SLS_SPACE_DESTROYED)
{
    uint64_t sid = (uint64_t)(intptr_t) context;
    struct view *view = table_find(&g_space_manager.view, &sid);
    if (view) {
        debug("%s: %lld\n", __FUNCTION__, sid);
        space_manager_remove_label_for_space(&g_space_manager, sid);
        table_remove(&g_space_manager.view, &sid);
        view_destroy(view);
        legacy_allocation_free(view);
        event_signal_push(SIGNAL_SPACE_DESTROYED, context);
        menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_SPACE);
    }
    declarative_configuration_request_reconcile();
}

static EVENT_HANDLER(SPACE_CHANGED)
{
    g_space_manager.last_space_id = g_space_manager.current_space_id;
    g_space_manager.current_space_id = space_manager_active_space();

    if (g_window_manager.menubar_opacity != 1.0f) {
        float alpha = space_is_fullscreen(g_space_manager.current_space_id) ? 1.0f : g_window_manager.menubar_opacity;
        SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, alpha);
    }

    debug("%s: %lld\n", __FUNCTION__, g_space_manager.current_space_id);
    struct view *view = space_manager_find_view(&g_space_manager, g_space_manager.current_space_id);

    if (space_manager_refresh_application_windows(&g_space_manager)) {
        event_loop_window_inventory_did_change();

        struct window *focused_window = window_manager_focused_window(&g_window_manager);
        if (focused_window && window_manager_find_lost_focused_event(&g_window_manager, focused_window->id)) {
            window_did_receive_focus(&g_window_manager, &g_mouse_state, focused_window);
            window_manager_remove_lost_focused_event(&g_window_manager, focused_window->id);
        }
    }

    if (view && !mission_control_is_active() && space_is_user(g_space_manager.current_space_id)) {
        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, g_space_manager.current_space_id);

        if (view_is_invalid(view)) {
            view_update(view);
        }

        if (view_is_dirty(view)) {
            window_node_flush(view->root);
            view_clear_flag(view, VIEW_IS_DIRTY);
        }
    }

    window_manager_refresh_active_window_highlight(&g_window_manager);
    event_signal_push(SIGNAL_SPACE_CHANGED, NULL);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_SPACE | MENU_BAR_DIRTY_FOCUS);
}

static bool event_loop_commit_display_change(struct display_manager *dm, struct space_manager *sm,
                                             uint32_t new_display_id, uint64_t new_space_id,
                                             uint32_t space_display_id)
{
    if (!new_display_id || !new_space_id || new_display_id != space_display_id) return false;

    dm->last_display_id = dm->current_display_id;
    dm->current_display_id = new_display_id;
    sm->last_space_id = sm->current_space_id;
    sm->current_space_id = new_space_id;
    return true;
}

static EVENT_HANDLER(DISPLAY_CHANGED)
{
#ifdef TESTS
    uint32_t new_did = event_loop_test_active_display_id
                     ? event_loop_test_active_display_id()
                     : display_manager_active_display_id();
#else
    uint32_t new_did = display_manager_active_display_id();
#endif
    /* Active-display notifications can accompany Space changes without a
     * display transition. Only a valid repeated ID is an inert duplicate. */
    if (new_did && g_display_manager.current_display_id == new_did) {
        debug("%s: newly activated display %d was already active (%d); ignoring duplicate event..\n",
              __FUNCTION__, g_display_manager.current_display_id, new_did);
        return;
    }

    uint64_t new_sid = display_space_id(new_did);
    uint32_t expected_display_id = space_display_id(new_sid);
    if (!event_loop_commit_display_change(&g_display_manager, &g_space_manager,
                                          new_did, new_sid, expected_display_id)) {
        debug("%s: %d %lld did not match %d! reserving topology recovery..\n", __FUNCTION__, new_did, new_sid, expected_display_id);
        event_loop_topology_lifecycle_failed();
        return;
    }

    if (g_window_manager.menubar_opacity != 1.0f) {
        float alpha = space_is_fullscreen(g_space_manager.current_space_id) ? 1.0f : g_window_manager.menubar_opacity;
        SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, alpha);
    }

    debug("%s: %d %lld\n", __FUNCTION__, g_display_manager.current_display_id, g_space_manager.current_space_id);
    struct view *view = space_manager_find_view(&g_space_manager, g_space_manager.current_space_id);

    if (space_manager_refresh_application_windows(&g_space_manager)) {
        event_loop_window_inventory_did_change();

        struct window *focused_window = window_manager_focused_window(&g_window_manager);
        if (focused_window && window_manager_find_lost_focused_event(&g_window_manager, focused_window->id)) {
            window_did_receive_focus(&g_window_manager, &g_mouse_state, focused_window);
            window_manager_remove_lost_focused_event(&g_window_manager, focused_window->id);
        }
    }

    if (view && !mission_control_is_active() && space_is_user(g_space_manager.current_space_id)) {
        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, g_space_manager.current_space_id);

        if (view_is_invalid(view)) {
            view_update(view);
        }

        if (view_is_dirty(view)) {
            window_node_flush(view->root);
            view_clear_flag(view, VIEW_IS_DIRTY);
        }
    }

    window_manager_refresh_active_window_highlight(&g_window_manager);
    event_signal_push(SIGNAL_DISPLAY_CHANGED, NULL);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_DISPLAY | MENU_BAR_DIRTY_FOCUS | MENU_BAR_DIRTY_SPACE);
}

static EVENT_HANDLER(DISPLAY_ADDED)
{
    uint32_t did = (uint32_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, did);
    if (!space_manager_handle_display_add(&g_space_manager, did) ||
        !window_manager_handle_display_add_and_remove(&g_space_manager, &g_window_manager, did)) {
        event_loop_topology_lifecycle_failed();
        return;
    }
    window_manager_invalidate_window_constraints(&g_window_manager);
    event_signal_push(SIGNAL_DISPLAY_ADDED, context);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_DISPLAY);
    declarative_configuration_request_reconcile();
}

static EVENT_HANDLER(DISPLAY_REMOVED)
{
    uint32_t did = (uint32_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, did);
    display_manager_remove_label_for_display(&g_display_manager, did);
    if (!window_manager_handle_display_add_and_remove(&g_space_manager, &g_window_manager,
                                                       display_manager_main_display_id())) {
        event_loop_topology_lifecycle_failed();
        return;
    }
    window_manager_invalidate_window_constraints(&g_window_manager);
    event_signal_push(SIGNAL_DISPLAY_REMOVED, context);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_DISPLAY);
    declarative_configuration_request_reconcile();
}

static EVENT_HANDLER(DISPLAY_MOVED)
{
    uint32_t did = (uint32_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, did);
    space_manager_mark_spaces_invalid(&g_space_manager);
    window_manager_invalidate_window_constraints(&g_window_manager);
    event_signal_push(SIGNAL_DISPLAY_MOVED, context);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_DISPLAY);
    declarative_configuration_request_reconcile();
}

static EVENT_HANDLER(DISPLAY_RESIZED)
{
    uint32_t did = (uint32_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, did);
    space_manager_mark_spaces_invalid_for_display(&g_space_manager, did);
    window_manager_invalidate_window_constraints(&g_window_manager);
    event_signal_push(SIGNAL_DISPLAY_RESIZED, context);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_DISPLAY);
    declarative_configuration_request_reconcile();
}

static bool mouse_drag_action_is_valid(enum mouse_mode action)
{
    return action == MOUSE_MODE_MOVE || action == MOUSE_MODE_RESIZE;
}

static bool mouse_drag_button_is_valid(int64_t button)
{
    return button == kCGMouseButtonLeft || button == kCGMouseButtonRight;
}

static bool mouse_drag_begin(struct mouse_state *mouse_state, enum mouse_mode action, int64_t button, struct window *window, CGPoint point)
{
    if (!mouse_drag_action_is_valid(action) || !mouse_drag_button_is_valid(button)) return false;

    mouse_state->window = window;
    mouse_state->window_frame = window->frame;
    mouse_state->down_location = point;
    mouse_state->resize_requested_location = point;
    mouse_state->resize_requested_frame = window->frame;
    mouse_state->resize_requested_time = read_os_timer();
    mouse_state->direction = 0;
    mouse_state->current_action = action;
    mouse_state->active_button = button;

    if (action == MOUSE_MODE_RESIZE) {
        CGPoint frame_mid = { CGRectGetMidX(mouse_state->window_frame), CGRectGetMidY(mouse_state->window_frame) };
        if (point.x < frame_mid.x) mouse_state->direction |= HANDLE_LEFT;
        if (point.y < frame_mid.y) mouse_state->direction |= HANDLE_TOP;
        if (point.x > frame_mid.x) mouse_state->direction |= HANDLE_RIGHT;
        if (point.y > frame_mid.y) mouse_state->direction |= HANDLE_BOTTOM;
    }

    return true;
}

static bool mouse_drag_event_is_owner(struct mouse_state *mouse_state, int64_t button)
{
    return mouse_state->active_button != MOUSE_BUTTON_NONE && mouse_state->active_button == button;
}

static bool mouse_drag_feedback_target_is_valid(struct window_node *source, struct window_node *target)
{
    return source && target && source != target;
}

static int mouse_drag_feedback_direction(struct view *view, int direction)
{
    return direction && view && view->layout == VIEW_STACK ? STACK : direction;
}

static void mouse_drag_clear_feedback(struct mouse_state *mouse_state)
{
    if (!mouse_state->feedback_node) return;
    mouse_state->feedback_node->insert_dir = 0;
    insert_feedback_destroy(mouse_state->feedback_node);
    mouse_state->feedback_node = NULL;
}

#define MOUSE_RESIZE_MINIMUM_DISTANCE 2.0
#define MOUSE_RESIZE_MINIMUM_DURATION_MS (1000.0 / 60.0)

static CGRect mouse_drag_resize_frame(struct mouse_state *mouse_state, CGPoint point)
{
    CGFloat dx = point.x - mouse_state->down_location.x;
    CGFloat dy = point.y - mouse_state->down_location.y;
    int x_mod = (mouse_state->direction & HANDLE_LEFT) ? -1 : (mouse_state->direction & HANDLE_RIGHT) ? 1 : 0;
    int y_mod = (mouse_state->direction & HANDLE_TOP) ? -1 : (mouse_state->direction & HANDLE_BOTTOM) ? 1 : 0;
    CGFloat width = max(1.0, mouse_state->window_frame.size.width + dx * x_mod);
    CGFloat height = max(1.0, mouse_state->window_frame.size.height + dy * y_mod);
    CGFloat x = (mouse_state->direction & HANDLE_LEFT) ? mouse_state->window_frame.origin.x + mouse_state->window_frame.size.width - width : mouse_state->window_frame.origin.x;
    CGFloat y = (mouse_state->direction & HANDLE_TOP) ? mouse_state->window_frame.origin.y + mouse_state->window_frame.size.height - height : mouse_state->window_frame.origin.y;
    return CGRectMake(x, y, width, height);
}

static bool mouse_drag_resize_window(struct mouse_state *mouse_state, CGPoint point, uint64_t event_time, bool force)
{
    CGRect frame = mouse_drag_resize_frame(mouse_state, point);
    if (CGRectEqualToRect(frame, mouse_state->resize_requested_frame)) return false;

    CGFloat dx = point.x - mouse_state->resize_requested_location.x;
    CGFloat dy = point.y - mouse_state->resize_requested_location.y;
    if (!force) {
        if (dx * dx + dy * dy < MOUSE_RESIZE_MINIMUM_DISTANCE * MOUSE_RESIZE_MINIMUM_DISTANCE) return false;
        if (!os_timer_mouse_resize_ready(mouse_state->resize_requested_time, event_time)) return false;
    }

#ifdef TESTS
    event_loop_test_mouse_drag_resize_window(mouse_state->window, frame);
#else
    CGFloat total_dx = point.x - mouse_state->down_location.x;
    CGFloat total_dy = point.y - mouse_state->down_location.y;
    window_manager_resize_window_relative_internal(mouse_state->window, mouse_state->window_frame, mouse_state->direction, total_dx, total_dy, false);
#endif
    mouse_state->resize_requested_location = point;
    mouse_state->resize_requested_frame = frame;
    mouse_state->resize_requested_time = event_time;
    return true;
}

static void mouse_drag_reset(struct mouse_state *mouse_state)
{
    mouse_drag_clear_feedback(mouse_state);

    mouse_state->window = NULL;
    mouse_state->current_action = MOUSE_MODE_NONE;
    mouse_state->active_button = MOUSE_BUTTON_NONE;
}

static bool mouse_drag_reset_if_owner(struct mouse_state *mouse_state, int64_t button)
{
    if (!mouse_drag_event_is_owner(mouse_state, button)) return false;
    mouse_drag_reset(mouse_state);
    return true;
}

static EVENT_HANDLER(NATIVE_RESIZE_FINALIZE)
{
    (void) param1;
    struct native_resize_state *state = &g_mouse_state.native_resize;
    if (!state->window_id || state->generation != (uint32_t) (uintptr_t) context) return;
    if (!state->released || !state->claimed) {
        native_resize_abort(&g_mouse_state, true);
        return;
    }

    struct window *window = window_manager_find_window(&g_window_manager, state->window_id);
    if (!window || !__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        native_resize_clear(&g_mouse_state);
        return;
    }
    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    struct window_node *node = view ? view_find_window_node(view, window->id) : NULL;
    if (!native_resize_window_is_eligible(window, view, node) ||
        !native_resize_baseline_matches(state, node)) {
        native_resize_abort(&g_mouse_state, true);
        return;
    }

    CGRect live_frame;
    if (!native_resize_read_live_frame(window, &live_frame)) {
        native_resize_abort(&g_mouse_state, true);
        return;
    }
    if (!native_resize_frames_match(live_frame, state->latest_frame)) {
        uint32_t window_id = state->window_id;
        if (native_resize_reconcile_live_frame(window, view, node, live_frame)) return;
        if (g_mouse_state.native_resize.window_id) native_resize_abort(&g_mouse_state, false);
        native_resize_restore_window(window_id);
        return;
    }
    if (!native_resize_has_settled_geometry(state)) {
        native_resize_abort(&g_mouse_state, true);
        return;
    }

    struct mouse_state geometry = { .window_frame = state->initial_frame };
    struct mouse_window_info info;
    mouse_window_info_populate_frame(&geometry, state->latest_frame, &info);
    native_resize_clear(&g_mouse_state);
    if (info.changed_position || info.changed_size) {
#ifdef TESTS
        event_loop_test_mouse_drop_try_adjust_bsp_grid(&g_window_manager, view, window, &info);
#else
        mouse_drop_try_adjust_bsp_grid(&g_window_manager, view, window, &info);
#endif
    } else {
        window_node_flush(node);
    }
}

static EVENT_HANDLER(NATIVE_RESIZE_CANCEL)
{
    (void) context;
    (void) param1;
    native_resize_abort(&g_mouse_state, true);
}

static EVENT_HANDLER(MOUSE_DOWN)
{
    enum mouse_mode action = (enum mouse_mode) param1;
    if (!mouse_drag_action_is_valid(action)) {
        native_resize_candidate_begin(context);
        goto out;
    }
    native_resize_abort(&g_mouse_state, true);
    if (mission_control_is_active())                      goto out;
    if (g_mouse_state.current_action != MOUSE_MODE_NONE)  goto out;

    int64_t button = CGEventGetIntegerValueField(context, kCGMouseEventButtonNumber);
    if (!mouse_drag_button_is_valid(button)) goto out;

    CGPoint point = CGEventGetLocation(context);
    debug("%s: %.2f, %.2f\n", __FUNCTION__, point.x, point.y);

    struct window *window = window_manager_find_window_at_point(&g_window_manager, point);
    if (!window || window_check_flag(window, WINDOW_FULLSCREEN)) goto out;

    mouse_drag_begin(&g_mouse_state, action, button, window, point);

out:
    CFRelease(context);
}

static EVENT_HANDLER(MOUSE_UP)
{
    int64_t button = CGEventGetIntegerValueField(context, kCGMouseEventButtonNumber);
    native_resize_mouse_up(button);
    if (!mouse_drag_event_is_owner(&g_mouse_state, button)) goto out;
    if (mission_control_is_active())                        goto reset;
    if (!g_mouse_state.window)                             goto reset;

    if (!__sync_bool_compare_and_swap(&g_mouse_state.window->id_ptr, &g_mouse_state.window->id, &g_mouse_state.window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, g_mouse_state.window->id);
        goto reset;
    }

    if (window_check_flag(g_mouse_state.window, WINDOW_FULLSCREEN)) {
        debug("%s: %d is transitioning into native-fullscreen mode, ignoring event..\n", __FUNCTION__, g_mouse_state.window->id);
        goto reset;
    }

    CGPoint point = CGEventGetLocation(context);
    debug("%s: %.2f, %.2f\n", __FUNCTION__, point.x, point.y);

    if (g_mouse_state.current_action == MOUSE_MODE_RESIZE) {
#ifdef TESTS
        mouse_drag_resize_window(&g_mouse_state, point, event_loop_test_mouse_drag_time(), true);
#else
        mouse_drag_resize_window(&g_mouse_state, point, read_os_timer(), true);
#endif
    }

    struct view *src_view = window_manager_find_managed_window(&g_window_manager, g_mouse_state.window);
    if (!src_view) goto reset;

    struct mouse_window_info info;
    if (g_mouse_state.current_action == MOUSE_MODE_RESIZE) {
        mouse_window_info_populate_frame(&g_mouse_state, g_mouse_state.resize_requested_frame, &info);
    } else {
        mouse_window_info_populate(&g_mouse_state, &info);
    }

    if (info.changed_position && !info.changed_size) {
        uint64_t cursor_sid = display_space_id(display_manager_point_display_id(point));
        struct view *dst_view = space_manager_find_view(&g_space_manager, cursor_sid);
        if (!dst_view) goto reset;

        struct window *window = window_manager_find_window_at_point_filtering_window(&g_window_manager, point, g_mouse_state.window->id);
        if (!window) window = window_manager_find_window_at_point(&g_window_manager, point);
        if (window == g_mouse_state.window) window = NULL;

        struct window_node *a_node = view_find_window_node(src_view, g_mouse_state.window->id);
        struct window_node *b_node = window ? view_find_window_node(dst_view, window->id) : NULL;

        if (mouse_drag_feedback_target_is_valid(a_node, b_node)) {
            if (g_mouse_state.feedback_node) {
                g_mouse_state.feedback_node->insert_dir = 0;
                insert_feedback_destroy(g_mouse_state.feedback_node);
                g_mouse_state.feedback_node = NULL;
            }

            enum mouse_drop_action drop_action = mouse_determine_drop_action(&g_mouse_state, a_node, window, point);
            switch (drop_action) {
            case MOUSE_DROP_ACTION_STACK: {
                mouse_drop_action_stack(&g_window_manager, src_view, g_mouse_state.window, dst_view, window);
            } break;
            case MOUSE_DROP_ACTION_SWAP: {
                (void) mouse_drop_action_swap(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window);
            } break;
            case MOUSE_DROP_ACTION_WARP_TOP: {
                mouse_drop_action_warp(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window, SPLIT_X, CHILD_FIRST);
            } break;
            case MOUSE_DROP_ACTION_WARP_RIGHT: {
                mouse_drop_action_warp(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window, SPLIT_Y, CHILD_SECOND);
            } break;
            case MOUSE_DROP_ACTION_WARP_BOTTOM: {
                mouse_drop_action_warp(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window, SPLIT_X, CHILD_SECOND);
            } break;
            case MOUSE_DROP_ACTION_WARP_LEFT: {
                mouse_drop_action_warp(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window, SPLIT_Y, CHILD_FIRST);
            } break;
            case MOUSE_DROP_ACTION_NONE: {
                /* silence compiler warning.. */
            } break;
            }
        } else if (a_node) {
            mouse_drop_no_target(&g_space_manager, &g_window_manager, src_view, dst_view, g_mouse_state.window, a_node);
        }
    } else if (info.changed_position || info.changed_size) {
#ifdef TESTS
        event_loop_test_mouse_drop_try_adjust_bsp_grid(&g_window_manager, src_view, g_mouse_state.window, &info);
#else
        mouse_drop_try_adjust_bsp_grid(&g_window_manager, src_view, g_mouse_state.window, &info);
#endif
    }

reset:
    mouse_drag_reset_if_owner(&g_mouse_state, button);
out:
    CFRelease(context);
}

static EVENT_HANDLER(MOUSE_DRAGGED)
{
    int64_t button = CGEventGetIntegerValueField(context, kCGMouseEventButtonNumber);
    if (!mouse_drag_event_is_owner(&g_mouse_state, button)) goto out;
    if (mission_control_is_active())                        goto abort;
    if (!g_mouse_state.window)                             goto abort;

    if (!__sync_bool_compare_and_swap(&g_mouse_state.window->id_ptr, &g_mouse_state.window->id, &g_mouse_state.window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, g_mouse_state.window->id);
        goto abort;
    }

    if (window_check_flag(g_mouse_state.window, WINDOW_FULLSCREEN)) {
        debug("%s: %d is transitioning into native-fullscreen mode, ignoring event..\n", __FUNCTION__, g_mouse_state.window->id);
        goto abort;
    }

    CGPoint point = CGEventGetLocation(context);
    debug("%s: %.2f, %.2f\n", __FUNCTION__, point.x, point.y);

    if (g_mouse_state.current_action == MOUSE_MODE_MOVE) {
        CGPoint new_point = { g_mouse_state.window_frame.origin.x + (point.x - g_mouse_state.down_location.x),
                              g_mouse_state.window_frame.origin.y + (point.y - g_mouse_state.down_location.y) };

        uint32_t did = display_manager_point_display_id(new_point);
        if (did) {
            CGRect bounds = display_bounds_constrained(did, false);
            if (new_point.y < bounds.origin.y) new_point.y = bounds.origin.y;
        }

#ifdef TESTS
        event_loop_test_mouse_drag_move_window(g_mouse_state.window, new_point.x, new_point.y);
#else
        if (!scripting_addition_move_window(g_mouse_state.window->id, new_point.x, new_point.y)) {
            window_manager_move_window(g_mouse_state.window, new_point.x, new_point.y);
        }
#endif
    } else if (g_mouse_state.current_action == MOUSE_MODE_RESIZE) {
#ifdef TESTS
        if (!mouse_drag_resize_window(&g_mouse_state, point, event_loop_test_mouse_drag_time(), false)) goto out;
#else
        if (!mouse_drag_resize_window(&g_mouse_state, point, read_os_timer(), false)) goto out;
#endif
    }

    struct view *src_view = window_manager_find_managed_window(&g_window_manager, g_mouse_state.window);
    if (!src_view) goto out;

    struct mouse_window_info info;
    if (g_mouse_state.current_action == MOUSE_MODE_RESIZE) {
        mouse_window_info_populate_frame(&g_mouse_state, g_mouse_state.resize_requested_frame, &info);
    } else {
        mouse_window_info_populate(&g_mouse_state, &info);
    }

    if (info.changed_position && !info.changed_size) {
        uint64_t cursor_sid = display_space_id(display_manager_point_display_id(point));
        struct view *dst_view = space_manager_find_view(&g_space_manager, cursor_sid);
        if (!dst_view) goto out;

        struct window *window = window_manager_find_window_at_point_filtering_window(&g_window_manager, point, g_mouse_state.window->id);
        if (!window) window = window_manager_find_window_at_point(&g_window_manager, point);
        if (window == g_mouse_state.window) window = NULL;

        struct window_node *a_node = view_find_window_node(src_view, g_mouse_state.window->id);
        struct window_node *b_node = window ? view_find_window_node(dst_view, window->id) : NULL;

        if (a_node && b_node && a_node != b_node) {
            if (g_mouse_state.feedback_node && g_mouse_state.feedback_node != b_node) {
                g_mouse_state.feedback_node->insert_dir = 0;
                insert_feedback_destroy(g_mouse_state.feedback_node);
            }

            int insert_dir = 0;
            enum mouse_drop_action drop_action = mouse_determine_drop_action(&g_mouse_state, a_node, window, point);
            switch (drop_action) {
            case MOUSE_DROP_ACTION_STACK: {
                insert_dir = STACK;
            } break;
            case MOUSE_DROP_ACTION_SWAP: {
                insert_dir = STACK;
            } break;
            case MOUSE_DROP_ACTION_WARP_TOP: {
                insert_dir = DIR_NORTH;
            } break;
            case MOUSE_DROP_ACTION_WARP_RIGHT: {
                insert_dir = DIR_EAST;
            } break;
            case MOUSE_DROP_ACTION_WARP_BOTTOM: {
                insert_dir = DIR_SOUTH;
            } break;
            case MOUSE_DROP_ACTION_WARP_LEFT: {
                insert_dir = DIR_WEST;
            } break;
            case MOUSE_DROP_ACTION_NONE: {
                mouse_drag_clear_feedback(&g_mouse_state);
                b_node->insert_dir = 0;
                insert_feedback_destroy(b_node);
            } break;
            }

            insert_dir = mouse_drag_feedback_direction(dst_view, insert_dir);
            if (insert_dir && b_node->insert_dir != insert_dir) {
                b_node->insert_dir = insert_dir;
                insert_feedback_show(b_node);
                g_mouse_state.feedback_node = b_node;
            }
        } else {
            mouse_drag_clear_feedback(&g_mouse_state);
        }
    }

    goto out;

abort:
    mouse_drag_reset_if_owner(&g_mouse_state, button);
out:
    CFRelease(context);
}

static EVENT_HANDLER(MOUSE_MOVED)
{
    enum ffm_mode ffm_mode = window_manager_effective_ffm_mode(&g_window_manager);
    if (ffm_mode == FFM_DISABLED)    goto out;
    if (mission_control_is_active()) goto out;
    if (g_mouse_state.ffm_window_id)               goto out;

    if (__atomic_load_n(&__pending_gesture, __ATOMIC_RELAXED)) goto out;
    uint64_t last_gesture_time = __atomic_load_n(&__last_gesture_time, __ATOMIC_RELAXED);
    if (os_timer_gesture_suppression_active(last_gesture_time, read_os_timer())) goto out;

    CGPoint point = CGEventGetLocation(context);
    struct window *window = window_manager_find_window_at_point(&g_window_manager, point);

    if (window) {
        if (window->id == g_window_manager.focused_window_id) goto out;
        if (!window_manager_is_window_eligible(window))       goto out;

        if (ffm_mode == FFM_AUTOFOCUS) {

            //
            // NOTE(asmvik): Look for a window with role AXSheet or AXDrawer
            // and forward focus to it because we are not allowed to focus the main
            // window in these cases.
            //

            CFArrayRef window_list = SLSCopyAssociatedWindows(g_connection, window->id);
            if (window_list) {
                int window_count = CFArrayGetCount(window_list);

                uint32_t child_wid;
                for (int i = 0; i < window_count; ++i) {
                    CFNumberGetValue(CFArrayGetValueAtIndex(window_list, i), kCFNumberSInt32Type, &child_wid);
                    struct window *child = window_manager_find_window(&g_window_manager, child_wid);
                    if (!child) continue;

                    CFTypeRef role = window_role(child);
                    if (!role) continue;

                    bool valid = CFEqual(role, kAXSheetRole) || CFEqual(role, kAXDrawerRole);

                    if (valid) {
                        window = child;
                        break;
                    }
                }

                CFRelease(window_list);
            }

            window_manager_focus_window_without_raise(&window->application->psn, window->id);
            g_mouse_state.ffm_window_id = window->id;
        } else if (ffm_mode == FFM_AUTORAISE) {

            //
            // NOTE(asmvik): If any **floating** window would be fully occluded by
            // autoraising the window below the cursor we do not actually perform the
            // focus change, as it is likely that the user is trying to reach for the
            // smaller window that sits on top of the window we would otherwise raise.
            //

            bool occludes_window = false;

            int window_count;
            uint32_t *window_list = space_window_list(g_space_manager.current_space_id, &window_count, false);

            if (window_list) {
                for (int i = 0; i < window_count; ++i) {
                    uint32_t wid = window_list[i];
                    if (wid == window->id) break;

                    struct window *sub_window = window_manager_find_window(&g_window_manager, wid);
                    if (!sub_window) continue;

                    if (!window_check_flag(sub_window, WINDOW_FLOAT))                     continue;
                    if (window_level(window->id) != window_level(sub_window->id))         continue;
                    if (window_sub_level(window->id) != window_sub_level(sub_window->id)) continue;

                    if (CGRectContainsRect(window->frame, sub_window->frame)) {
                        occludes_window = true;
                        break;
                    }
                }
            }

            if (!occludes_window) {
                window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
                g_mouse_state.ffm_window_id = window->id;
            }
        }
    } else {
        uint32_t cursor_did = display_manager_point_display_id(point);
        if (g_display_manager.current_display_id == cursor_did) goto out;

        CGRect bounds = display_bounds_constrained(cursor_did, false);
        if (!cgrect_contains_point(bounds, point)) goto out;

        uint32_t wid = display_manager_focus_display_with_window_at_point(point);
        if (!wid) display_manager_set_active_display_id(cursor_did);
        g_mouse_state.ffm_window_id = wid;
    }

out:
    CFRelease(context);
}

static void mission_control_begin_session(enum mission_control_mode mode, uint64_t observer_epoch,
                                          uint64_t ax_token, bool poll)
{
    native_resize_abort(&g_mouse_state, true);
    g_mission_control_session = (struct mission_control_session_state) {
        .generation = mission_control_next_generation(), .observer_epoch = observer_epoch,
        .ax_token = ax_token, .active = true
    };
    g_mission_control_mode = mode;
    __atomic_store_n(&g_mission_control_poll_generation, g_mission_control_session.generation, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mission_control_poll_token, 0, __ATOMIC_RELEASE);
    if (poll) mission_control_schedule_poll(0.1);
    window_manager_hide_active_window_highlight(&g_window_manager);
    mission_control_emit_signal(SIGNAL_MISSION_CONTROL_ENTER, (void *) (uintptr_t) mode);
}

static bool mission_control_accept_ax_context(struct mission_control_event_context *event,
                                              enum mission_control_notification_kind kind)
{
    return event && event->kind == kind && event->epoch && event->token &&
           event->epoch == __atomic_load_n(&g_mission_control_observer.epoch, __ATOMIC_ACQUIRE) &&
           event->token == __atomic_load_n(&g_mission_control_observer.notification_token, __ATOMIC_ACQUIRE);
}

static void mission_control_handle_show(void *context, enum mission_control_notification_kind kind,
                                        enum mission_control_mode mode)
{
    struct mission_control_event_context event = context ? *(struct mission_control_event_context *) context
                                                         : (struct mission_control_event_context) {0};
    mission_control_event_context_destroy(context);
    if (!mission_control_accept_ax_context(&event, kind)) return;
    mission_control_begin_session(mode, event.epoch, event.token, false);
}

static EVENT_HANDLER(MISSION_CONTROL_SHOW_ALL_WINDOWS)
{
    debug("%s:\n", __FUNCTION__);
    mission_control_handle_show(context, MISSION_CONTROL_NOTIFICATION_SHOW_ALL, MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS);
}

static EVENT_HANDLER(MISSION_CONTROL_SHOW_FRONT_WINDOWS)
{
    debug("%s:\n", __FUNCTION__);
    mission_control_handle_show(context, MISSION_CONTROL_NOTIFICATION_SHOW_FRONT, MISSION_CONTROL_MODE_SHOW_FRONT_WINDOWS);
}

static EVENT_HANDLER(MISSION_CONTROL_SHOW_DESKTOP)
{
    debug("%s:\n", __FUNCTION__);
    mission_control_handle_show(context, MISSION_CONTROL_NOTIFICATION_SHOW_DESKTOP, MISSION_CONTROL_MODE_SHOW_DESKTOP);
}

static EVENT_HANDLER(MISSION_CONTROL_ENTER)
{
    debug("%s:\n", __FUNCTION__);
    mission_control_begin_session(MISSION_CONTROL_MODE_SHOW, 0, 0, true);
}

static enum mission_control_sample_state mission_control_classify_sample(CFTypeRef sample)
{
    if (!sample || CFGetTypeID(sample) != CFArrayGetTypeID()) return MISSION_CONTROL_SAMPLE_INCONCLUSIVE;
    CFArrayRef window_list = sample;
    bool inconclusive = false;
    for (CFIndex i = 0; i < CFArrayGetCount(window_list); ++i) {
        CFTypeRef item = CFArrayGetValueAtIndex(window_list, i);
        if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID()) { inconclusive = true; continue; }
        CFDictionaryRef dictionary = item;
        CFTypeRef name = CFDictionaryGetValue(dictionary, kCGWindowName);
        if (name) {
            if (CFGetTypeID(name) != CFStringGetTypeID()) inconclusive = true;
            continue;
        }
        CFTypeRef owner = CFDictionaryGetValue(dictionary, kCGWindowOwnerName);
        CFTypeRef layer_ref = CFDictionaryGetValue(dictionary, kCGWindowLayer);
        if (!owner || !layer_ref || CFGetTypeID(owner) != CFStringGetTypeID() ||
            CFGetTypeID(layer_ref) != CFNumberGetTypeID()) { inconclusive = true; continue; }
        int64_t layer = 0;
        if (!CFNumberGetValue(layer_ref, kCFNumberSInt64Type, &layer)) { inconclusive = true; continue; }
        if (layer == 18 && CFEqual(CFSTR("Dock"), owner)) return MISSION_CONTROL_SAMPLE_ACTIVE;
    }
    return inconclusive ? MISSION_CONTROL_SAMPLE_INCONCLUSIVE : MISSION_CONTROL_SAMPLE_INACTIVE;
}

static void mission_control_finish_exit(uint64_t generation)
{
    if (!g_mission_control_session.active || generation != g_mission_control_session.generation) return;
    enum mission_control_mode mode = g_mission_control_mode;
    g_mission_control_session.active = false;
    g_mission_control_session.poll_pending = false;
    __atomic_store_n(&g_mission_control_poll_token, 0, __ATOMIC_RELEASE);
    g_mission_control_session.ax_token = 0;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
    if (g_window_manager.menubar_opacity != 1.0f) {
#ifdef TESTS
        float alpha = g_window_manager.menubar_opacity;
#else
        float alpha = space_is_fullscreen(g_space_manager.current_space_id) ? 1.0f : g_window_manager.menubar_opacity;
#endif
        mission_control_set_menu_alpha(alpha);
    }
    if (mode == MISSION_CONTROL_MODE_SHOW || mode == MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS) mission_control_correct();
    window_manager_refresh_active_window_highlight(&g_window_manager);
    mission_control_emit_signal(SIGNAL_MISSION_CONTROL_EXIT, (void *) (uintptr_t) mode);
    mission_control_mark_menu_dirty();
    mission_control_request_reconcile();
}

static EVENT_HANDLER(MISSION_CONTROL_CHECK_FOR_EXIT)
{
    struct mission_control_poll_context event = context ? *(struct mission_control_poll_context *) context
                                                        : (struct mission_control_poll_context) {0};
    mission_control_poll_context_destroy(context);
    if (!g_mission_control_session.active || !g_mission_control_session.poll_pending ||
        event.generation != g_mission_control_session.generation ||
        event.poll_token != g_mission_control_session.poll_token) return;
    g_mission_control_session.poll_pending = false;
    __atomic_store_n(&g_mission_control_poll_token, 0, __ATOMIC_RELEASE);
    CFArrayRef window_list = mission_control_copy_window_list();
    enum mission_control_sample_state sample = mission_control_classify_sample(window_list);
    if (window_list) CFRelease(window_list);
    if (sample == MISSION_CONTROL_SAMPLE_INACTIVE) {
        g_mission_control_session.inconclusive_samples = 0;
        mission_control_finish_exit(event.generation);
    } else if (sample == MISSION_CONTROL_SAMPLE_ACTIVE) {
        g_mission_control_session.inconclusive_samples = 0;
        mission_control_schedule_poll(0.1);
    } else {
        if (g_mission_control_session.inconclusive_samples != UINT_MAX) ++g_mission_control_session.inconclusive_samples;
        mission_control_schedule_poll(g_mission_control_session.inconclusive_samples < 10 ? 0.1 : 1.0);
    }
}

static EVENT_HANDLER(MISSION_CONTROL_EXIT)
{
    struct mission_control_event_context event = context ? *(struct mission_control_event_context *) context
                                                         : (struct mission_control_event_context) {0};
    mission_control_event_context_destroy(context);
    if (!mission_control_accept_ax_context(&event, MISSION_CONTROL_NOTIFICATION_EXIT)) return;
    if (!g_mission_control_session.active || event.epoch != g_mission_control_session.observer_epoch ||
        event.token != g_mission_control_session.ax_token) return;
    mission_control_schedule_poll(0.0);
}

static EVENT_HANDLER(DOCK_DID_RESTART)
{
    debug("%s:\n", __FUNCTION__);

    if (workspace_is_macos_monterey_or_newer()) {
        mission_control_unobserve();
        mission_control_ensure_observed();
    }
    mission_control_schedule_poll(0.1);

    event_signal_push(SIGNAL_DOCK_DID_RESTART, NULL);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_SPACE | MENU_BAR_DIRTY_WINDOW);
    declarative_configuration_request_reconcile();
}

static EVENT_HANDLER(MENU_OPENED)
{
    struct application *application = window_manager_find_application(&g_window_manager, (pid_t)(intptr_t) context);
    if (!application) return;
    debug("%s: %s (%d)\n", __FUNCTION__, application->name, application->pid);
    window_manager_ffm_application_menu_opened(&g_window_manager, application);
}

static EVENT_HANDLER(MENU_CLOSED)
{
    struct application *application = window_manager_find_application(&g_window_manager, (pid_t)(intptr_t) context);
    if (!application) return;
    debug("%s: %s (%d)\n", __FUNCTION__, application->name, application->pid);
    window_manager_ffm_application_menu_closed(&g_window_manager, application);
}

static EVENT_HANDLER(MENU_BAR_HIDDEN_CHANGED)
{
    debug("%s:\n", __FUNCTION__);
    space_manager_mark_spaces_invalid(&g_space_manager);
    event_signal_push(SIGNAL_MENU_BAR_HIDDEN_CHANGED, NULL);
}

static EVENT_HANDLER(DOCK_DID_CHANGE_PREF)
{
    debug("%s:\n", __FUNCTION__);
    space_manager_mark_spaces_invalid(&g_space_manager);
    event_signal_push(SIGNAL_DOCK_DID_CHANGE_PREF, NULL);
}

static EVENT_HANDLER(SYSTEM_WOKE)
{
    debug("%s:\n", __FUNCTION__);

    struct window *focused_window = window_manager_find_window(&g_window_manager, g_window_manager.focused_window_id);
    if (focused_window) {
        window_manager_set_window_opacity(&g_window_manager, focused_window, g_window_manager.active_window_opacity);
        window_manager_center_mouse(&g_window_manager, focused_window);
    }

    if (workspace_is_macos_monterey_or_newer()) {
        mission_control_ensure_observed();
    }
    window_manager_refresh_active_window_highlight(&g_window_manager);
    mission_control_schedule_poll(0.1);
    event_signal_push(SIGNAL_SYSTEM_WOKE, NULL);
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_SPACE | MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_FOCUS);
    declarative_configuration_request_reconcile();
}

static EVENT_HANDLER(MENU_BAR_REFRESH)
{
    (void) context;
    (void) param1;
    menu_bar_manager_refresh();
}

static EVENT_HANDLER(MENU_BAR_ACTION)
{
    (void) param1;
    enum menu_bar_tracking_transition transition = menu_bar_manager_handle_action(context);
    if (transition == MENU_BAR_TRACKING_TRANSITION_OPENED) {
        window_manager_ffm_menu_opened(&g_window_manager);
    } else if (transition == MENU_BAR_TRACKING_TRANSITION_CLOSED) {
        window_manager_ffm_menu_closed(&g_window_manager);
    }
}

static EVENT_HANDLER(CONFIGURATION_RECONCILE)
{
    (void) param1;
    (void) declarative_configuration_reconcile_event((uint64_t) (uintptr_t) context);
}

struct event_loop_sync
{
    event_loop_sync_callback callback;
    void *context;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool completed;
    bool result;
};

static void event_loop_sync_complete(struct event_loop_sync *sync, bool result)
{
    pthread_mutex_lock(&sync->mutex);
    sync->result = result;
    sync->completed = true;
    pthread_cond_signal(&sync->cond);
    pthread_mutex_unlock(&sync->mutex);
}

static EVENT_HANDLER(EVENT_LOOP_SYNC)
{
    (void) param1;
    struct event_loop_sync *sync = context;
    bool result = sync->callback ? sync->callback(sync->context) : true;
    event_loop_sync_complete(sync, result);
}

static EVENT_HANDLER(WINDOW_FOCUS_FALLBACK)
{
    __atomic_store_n(&window_focus_fallback_reserved, 0, __ATOMIC_RELEASE);
#ifdef TESTS
    if (event_loop_focus_fallback_entry_test_hook) event_loop_focus_fallback_entry_test_hook();
#endif
    if (g_window_manager.window_focus_fallback != WINDOW_FOCUS_FALLBACK_RECENT) return;

    struct window *focused = event_loop_focus_fallback_find_focused(&g_window_manager);
    if (window_manager_window_is_focus_fallback_eligible(focused)) return;

    struct window *recent = event_loop_focus_fallback_find_recent(&g_window_manager);
    if (!window_manager_window_is_focus_fallback_eligible(recent)) return;
    if (!event_loop_focus_fallback_is_on_active_space(recent)) return;
    event_loop_focus_fallback_focus(recent);
}

static EVENT_HANDLER(DAEMON_MESSAGE)
{
    (void) param1;
    TIME_FUNCTION;
    daemon_io_execute(context);
}
#pragma clang diagnostic pop

struct event_loop_bootstrap
{
    struct event_loop *event_loop;
    event_loop_bootstrap_callback callback;
    void *context;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool completed;
    bool result;
};

static __thread struct event_loop *event_loop_worker;

#ifdef TESTS
static void (*event_loop_topology_resync_test_hook)(struct event_loop *);
static bool event_loop_topology_resync_test_result = true;
static void (*event_loop_topology_retry_test_scheduler)(struct event_loop *, uint64_t);
static enum declarative_topology_result (*event_loop_topology_capture_test)(
    const struct declarative_topology_bounds_config *, struct declarative_topology *);
static void (*event_loop_topology_postcommit_hook)(void);
static int event_loop_topology_fail_allocation_at;
static int event_loop_topology_allocation_count;
static size_t event_loop_topology_outstanding_allocations;
#endif

struct event_loop_topology_space {
    uint32_t did;
    uint64_t sid;
    const char *uuid;
    CFStringRef published_uuid;
    struct declarative_space_identity identity;
    struct view *view;
    uint32_t old_did;
    uint64_t old_sid;
    bool created;
};

struct event_loop_topology_space_label {
    int index;
    uint64_t old_sid;
    uint64_t new_sid;
    bool survives;
};

struct event_loop_topology_display_label {
    int index;
    uint32_t old_did;
    uint32_t new_did;
    bool survives;
};

struct event_loop_topology_plan {
    struct declarative_topology topology;
    uint32_t *displays;
    int display_count;
    struct event_loop_topology_space *spaces;
    int space_count;
    uint32_t active_did;
    uint64_t active_sid;
    struct table views;
    struct table managed;
    struct view **stale_views;
    int stale_count;
    struct event_loop_topology_space_label *space_labels;
    int space_label_count;
    struct event_loop_topology_display_label *display_labels;
    int display_label_count;
    uint32_t mapped_last_did;
    uint64_t mapped_last_sid;
    uint32_t *removed_windows;
    int removed_count;
};

static void *event_loop_topology_allocate(size_t count, size_t size)
{
    if (!count || !size || size > SIZE_MAX / count) return NULL;
#ifdef TESTS
    ++event_loop_topology_allocation_count;
    if (event_loop_topology_fail_allocation_at == event_loop_topology_allocation_count) return NULL;
#endif
    void *value = calloc(count, size);
#ifdef TESTS
    if (value) ++event_loop_topology_outstanding_allocations;
#endif
    return value;
}

static void event_loop_topology_free(void *value)
{
    if (!value) return;
#ifdef TESTS
    assert(event_loop_topology_outstanding_allocations > 0);
    --event_loop_topology_outstanding_allocations;
#endif
    free(value);
}

static enum declarative_topology_result event_loop_topology_capture(
    const struct declarative_topology_bounds_config *bounds, struct declarative_topology *topology)
{
#ifdef TESTS
    if (event_loop_topology_capture_test) return event_loop_topology_capture_test(bounds, topology);
#endif
    return declarative_topology_capture_system_result(bounds, topology);
}

static CFStringRef event_loop_topology_cf_uuid(const char *uuid)
{
    if (!uuid) return NULL;
    return CFStringCreateWithCString(NULL, uuid, kCFStringEncodingUTF8);
}

static struct view *event_loop_topology_create_view(const struct declarative_topology_space *space,
                                                    const struct declarative_topology_display *display,
                                                    CFStringRef uuid)
{
    struct view *view = legacy_allocation_calloc(1, sizeof(*view));
    struct window_node *root = legacy_allocation_calloc(1, sizeof(*root));
    if (!view || !root) {
        legacy_allocation_free(root);
        legacy_allocation_free(view);
        return NULL;
    }
    view->sid = space->sid;
    view->uuid = CFRetain(uuid);
    view->declarative_identity = space->identity;
    view->root = root;
    view_set_flag(view, VIEW_ENABLE_PADDING);
    view_set_flag(view, VIEW_ENABLE_GAP);
    if (space->kind != 0) {
        view->layout = VIEW_FLOAT;
        return view;
    }
    view->layout = g_space_manager.layout;
    view->top_padding = g_space_manager.top_padding;
    view->bottom_padding = g_space_manager.bottom_padding;
    view->left_padding = g_space_manager.left_padding;
    view->right_padding = g_space_manager.right_padding;
    view->window_gap = g_space_manager.window_gap;
    view->accordion_padding = g_space_manager.accordion_padding;
    view->single_window_max_width = g_space_manager.single_window_max_width;
    view->single_window_min_width = g_space_manager.single_window_min_width;
    view->single_window_width_ratio = g_space_manager.single_window_width_ratio;
    view->split_ratio = g_space_manager.split_ratio;
    view->auto_balance = g_space_manager.auto_balance;
    view->split_type = g_space_manager.split_type;
    CGRect bounds = display->bounds;
    root->area = (struct area) {
        .x = (float) bounds.origin.x + view->left_padding,
        .y = (float) bounds.origin.y + view->top_padding,
        .w = (float) bounds.size.width - view_padding_sum(view->left_padding, view->right_padding),
        .h = (float) bounds.size.height - view_padding_sum(view->top_padding, view->bottom_padding)
    };
    if (view_layout_is_accordion(view->layout)) {
        float min_x = (float) bounds.origin.x, max_x = (float) CGRectGetMaxX(bounds);
        float min_y = (float) bounds.origin.y, max_y = (float) CGRectGetMaxY(bounds);
        float x = fminf(max_x, fmaxf(min_x, root->area.x));
        float y = fminf(max_y, fmaxf(min_y, root->area.y));
        float right = fminf(max_x, fmaxf(min_x, root->area.x + root->area.w));
        float bottom = fminf(max_y, fmaxf(min_y, root->area.y + root->area.h));
        root->area = (struct area) { x, y, fmaxf(0.0f, right - x), fmaxf(0.0f, bottom - y) };
    }
    if (!isfinite(root->area.x) || !isfinite(root->area.y) ||
        !isfinite(root->area.w) || !isfinite(root->area.h)) {
        view_destroy(view);
        legacy_allocation_free(view);
        return NULL;
    }
    view_set_flag(view, VIEW_IS_VALID);
    view_set_flag(view, VIEW_IS_DIRTY);
    return view;
}

static void event_loop_topology_plan_destroy(struct event_loop_topology_plan *plan, bool committed)
{
    if (!plan) return;
    if (plan->spaces && !committed) {
        for (int i = 0; i < plan->space_count; ++i) {
            if (plan->spaces[i].created && plan->spaces[i].view) {
                view_destroy(plan->spaces[i].view);
                legacy_allocation_free(plan->spaces[i].view);
            }
        }
    }
    if (plan->spaces) {
        for (int i = 0; i < plan->space_count; ++i) {
            if (plan->spaces[i].published_uuid) CFRelease(plan->spaces[i].published_uuid);
        }
    }
    table_free(&plan->views);
    table_free(&plan->managed);
    declarative_topology_destroy(&plan->topology);
    event_loop_topology_free(plan->displays);
    event_loop_topology_free(plan->spaces);
    event_loop_topology_free(plan->stale_views);
    event_loop_topology_free(plan->space_labels);
    event_loop_topology_free(plan->display_labels);
    event_loop_topology_free(plan->removed_windows);
    memset(plan, 0, sizeof(*plan));
}

static bool event_loop_event_requires_topology_resync(enum event_type type)
{
    switch (type) {
    case SLS_SPACE_CREATED:
    case SPACE_CHANGED:
    case DISPLAY_ADDED:
    case DISPLAY_MOVED:
    case DISPLAY_RESIZED:
    case DISPLAY_CHANGED:
    case DOCK_DID_RESTART:
    case MENU_BAR_HIDDEN_CHANGED:
    case DOCK_DID_CHANGE_PREF:
    case SYSTEM_WOKE:
        return true;
    default:
        return false;
    }
}

static void event_loop_reserve_topology_resync(struct event_loop *event_loop)
{
    uint32_t expected = 0;
    if (!event_loop || !__atomic_compare_exchange_n(&event_loop->topology_resync_pending, &expected, 1, false,
                                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    if (event_loop->semaphore) sem_post(event_loop->semaphore);
}

static enum declarative_topology_result event_loop_prepare_topology_plan(struct event_loop_topology_plan *plan)
{
    memset(plan, 0, sizeof(*plan));
#ifdef TESTS
    event_loop_topology_allocation_count = 0;
#endif
    struct declarative_topology confirmation = {0};
    struct declarative_topology_bounds_config bounds = {
        .external_bar_mode = g_display_manager.mode,
        .external_bar_top_padding = g_display_manager.top_padding,
        .external_bar_bottom_padding = g_display_manager.bottom_padding
    };
    enum declarative_topology_result first = event_loop_topology_capture(&bounds, &plan->topology);
    if (first != DECLARATIVE_TOPOLOGY_OK) return first;
    enum declarative_topology_result second = event_loop_topology_capture(&bounds, &confirmation);
    if (second != DECLARATIVE_TOPOLOGY_OK) {
        declarative_topology_destroy(&confirmation);
        return second;
    }
    if (!declarative_topology_equal(&plan->topology, &confirmation)) {
        declarative_topology_destroy(&confirmation);
        return DECLARATIVE_TOPOLOGY_SETTLING;
    }
    declarative_topology_destroy(&confirmation);

    if (plan->topology.display_count > INT_MAX || plan->topology.space_count > INT_MAX)
        return DECLARATIVE_TOPOLOGY_INVALID;
    plan->display_count = (int) plan->topology.display_count;
    plan->space_count = (int) plan->topology.space_count;
    plan->active_did = plan->topology.active_did;
    plan->active_sid = plan->topology.active_sid;
    plan->displays = event_loop_topology_allocate(plan->topology.display_count, sizeof(*plan->displays));
    plan->spaces = event_loop_topology_allocate(plan->topology.space_count, sizeof(*plan->spaces));
    if (!plan->displays || !plan->spaces) return DECLARATIVE_TOPOLOGY_RESOURCE;
    for (int i = 0; i < plan->display_count; ++i) plan->displays[i] = plan->topology.displays[i].did;
    for (int i = 0; i < plan->space_count; ++i) {
        const struct declarative_topology_space *canonical = &plan->topology.spaces[i];
        const struct declarative_topology_display *display =
            &plan->topology.displays[canonical->parent_display_index];
        plan->spaces[i] = (struct event_loop_topology_space) {
            .did = display->did, .sid = canonical->sid, .uuid = canonical->uuid,
            .published_uuid = event_loop_topology_cf_uuid(canonical->uuid),
            .identity = canonical->identity
        };
        if (!plan->spaces[i].published_uuid) return DECLARATIVE_TOPOLOGY_RESOURCE;
    }

    int table_capacity = plan->space_count > 11 ? plan->space_count * 2 + 1 : 23;
    if (!table_try_init(&plan->views, table_capacity, g_space_manager.view.hash, g_space_manager.view.cmp))
        return DECLARATIVE_TOPOLOGY_RESOURCE;
    plan->stale_views = event_loop_topology_allocate((size_t) g_space_manager.view.count, sizeof(*plan->stale_views));
    if (g_space_manager.view.count && !plan->stale_views) return DECLARATIVE_TOPOLOGY_RESOURCE;

    for (int i = 0; i < plan->space_count; ++i) {
        struct event_loop_topology_space *space = &plan->spaces[i];
        table_for (struct view *candidate, g_space_manager.view, {
            bool identified = declarative_space_identity_valid(&candidate->declarative_identity);
            bool matches = identified
                         ? declarative_space_identity_equal(&candidate->declarative_identity, &space->identity)
                         : candidate->sid == space->sid;
            if (matches) {
                if (space->view) return DECLARATIVE_TOPOLOGY_INVALID;
                space->view = candidate;
                space->old_sid = candidate->sid;
                if (identified) {
                    space->old_did = candidate->declarative_identity.parent_did;
                } else if (candidate->sid == g_space_manager.current_space_id) {
                    space->old_did = g_display_manager.current_display_id;
                } else if (candidate->sid == g_space_manager.last_space_id) {
                    space->old_did = g_display_manager.last_display_id;
                } else {
                    space->old_did = space->did;
                }
            }
        })
        if (!space->view) {
            const struct declarative_topology_space *canonical = &plan->topology.spaces[i];
            const struct declarative_topology_display *display =
                &plan->topology.displays[canonical->parent_display_index];
            space->view = event_loop_topology_create_view(canonical, display, space->published_uuid);
            if (!space->view) return DECLARATIVE_TOPOLOGY_RESOURCE;
            space->created = true;
        }
        if (!table_try_add(&plan->views, &space->sid, space->view)) return DECLARATIVE_TOPOLOGY_RESOURCE;
    }

    table_for (struct view *old_view, g_space_manager.view, {
        bool survives = false;
        for (int i = 0; i < plan->space_count; ++i) if (plan->spaces[i].view == old_view) survives = true;
        if (!survives) plan->stale_views[plan->stale_count++] = old_view;
    })
    plan->space_label_count = buf_len(g_space_manager.labels);
    plan->space_labels = event_loop_topology_allocate((size_t) plan->space_label_count, sizeof(*plan->space_labels));
    if (plan->space_label_count && !plan->space_labels) return DECLARATIVE_TOPOLOGY_RESOURCE;
    for (int i = 0; i < plan->space_label_count; ++i) {
        struct event_loop_topology_space_label *label = &plan->space_labels[i];
        label->index = i;
        label->old_sid = g_space_manager.labels[i].sid;
        for (int j = 0; j < plan->space_count; ++j) {
            if (plan->spaces[j].old_sid == label->old_sid && !plan->spaces[j].created) {
                label->survives = true;
                label->new_sid = plan->spaces[j].sid;
                break;
            }
        }
    }

    plan->display_label_count = buf_len(g_display_manager.labels);
    plan->display_labels = event_loop_topology_allocate((size_t) plan->display_label_count, sizeof(*plan->display_labels));
    if (plan->display_label_count && !plan->display_labels) return DECLARATIVE_TOPOLOGY_RESOURCE;
    for (int i = 0; i < plan->display_label_count; ++i) {
        struct event_loop_topology_display_label *label = &plan->display_labels[i];
        label->index = i;
        label->old_did = g_display_manager.labels[i].did;
        for (int j = 0; j < plan->space_count; ++j) {
            if (!plan->spaces[j].created && plan->spaces[j].old_did == label->old_did) {
                if (label->survives && label->new_did != plan->spaces[j].did) return DECLARATIVE_TOPOLOGY_INVALID;
                label->survives = true;
                label->new_did = plan->spaces[j].did;
            }
        }
        if (!label->survives) {
            for (int j = 0; j < plan->display_count; ++j) if (plan->displays[j] == label->old_did) {
                label->survives = true;
                label->new_did = label->old_did;
            }
        }
    }

    plan->mapped_last_sid = plan->active_sid;
    plan->mapped_last_did = plan->active_did;
    for (int i = 0; i < plan->space_count; ++i) {
        if (plan->spaces[i].created) continue;
        for (int j = 0; j < i; ++j) {
            if (!plan->spaces[j].created && plan->spaces[i].old_did &&
                plan->spaces[i].old_did == plan->spaces[j].old_did &&
                plan->spaces[i].did != plan->spaces[j].did) return DECLARATIVE_TOPOLOGY_INVALID;
        }
        if (plan->spaces[i].old_sid == g_space_manager.last_space_id) {
            plan->mapped_last_sid = plan->spaces[i].sid;
            plan->mapped_last_did = plan->spaces[i].did;
        } else if (plan->spaces[i].old_did == g_display_manager.last_display_id &&
                   plan->mapped_last_did == plan->active_did) {
            plan->mapped_last_did = plan->spaces[i].did;
        }
    }

    int managed_capacity = g_window_manager.managed_window.count > 11 ? g_window_manager.managed_window.count * 2 + 1 : 23;
    if (!table_try_init(&plan->managed, managed_capacity,
                        g_window_manager.managed_window.hash, g_window_manager.managed_window.cmp))
        return DECLARATIVE_TOPOLOGY_RESOURCE;
    plan->removed_windows = event_loop_topology_allocate((size_t) g_window_manager.managed_window.count,
                                                          sizeof(*plan->removed_windows));
    if (g_window_manager.managed_window.count && !plan->removed_windows) return DECLARATIVE_TOPOLOGY_RESOURCE;
    for (int bucket_index = 0; bucket_index < g_window_manager.managed_window.capacity; ++bucket_index) {
        for (struct bucket *bucket = g_window_manager.managed_window.buckets[bucket_index]; bucket; bucket = bucket->next) {
            if (!bucket->value) continue;
            struct view *replacement = NULL;
            for (int i = 0; i < plan->space_count; ++i) if (plan->spaces[i].view == bucket->value) replacement = plan->spaces[i].view;
            uint32_t wid = *(uint32_t *) bucket->key;
            if (replacement) {
                if (!table_try_add(&plan->managed, &wid, replacement)) return DECLARATIVE_TOPOLOGY_RESOURCE;
            } else {
                plan->removed_windows[plan->removed_count++] = wid;
            }
        }
    }
    return DECLARATIVE_TOPOLOGY_OK;
}

static bool event_loop_recover_topology(struct event_loop *event_loop)
{
#ifdef TESTS
    if (event_loop_topology_resync_test_hook) {
        event_loop_topology_resync_test_hook(event_loop);
        return event_loop_topology_resync_test_result;
    }
#endif
    if (event_loop != &g_event_loop) return false;

    struct event_loop_topology_plan plan;
    if (event_loop_prepare_topology_plan(&plan) != DECLARATIVE_TOPOLOGY_OK) {
        event_loop_topology_plan_destroy(&plan, false);
        return false;
    }

    struct table old_views = g_space_manager.view;
    struct table old_managed = g_window_manager.managed_window;
    g_space_manager.view = plan.views;
    g_window_manager.managed_window = plan.managed;
    plan.views = (struct table) {0};
    plan.managed = (struct table) {0};
    /* Publish every SID/table mapping before touching labels. The immutable
     * label plan below is therefore safe for swaps and longer SID cycles. */
    for (int i = 0; i < plan.space_count; ++i) {
        struct event_loop_topology_space *space = &plan.spaces[i];
        if (!space->created) {
            if (space->view->uuid) CFRelease(space->view->uuid);
            space->view->uuid = space->published_uuid;
            space->published_uuid = NULL;
            space->view->sid = space->sid;
        }
        space->view->declarative_identity = space->identity;
    }
    for (int i = plan.space_label_count - 1; i >= 0; --i) {
        struct event_loop_topology_space_label *label = &plan.space_labels[i];
        if (label->survives) {
            g_space_manager.labels[label->index].sid = label->new_sid;
        } else {
            free(g_space_manager.labels[label->index].label);
            buf_del(g_space_manager.labels, label->index);
        }
    }
    for (int i = plan.display_label_count - 1; i >= 0; --i) {
        struct event_loop_topology_display_label *label = &plan.display_labels[i];
        if (label->survives) {
            g_display_manager.labels[label->index].did = label->new_did;
        } else {
            free(g_display_manager.labels[label->index].label);
            buf_del(g_display_manager.labels, label->index);
        }
    }
    g_display_manager.last_display_id = plan.mapped_last_did;
    g_display_manager.current_display_id = plan.active_did;
    g_space_manager.last_space_id = plan.mapped_last_sid;
    g_space_manager.current_space_id = plan.active_sid;
    table_free(&old_views);
    table_free(&old_managed);

    for (int i = 0; i < plan.stale_count; ++i) {
        view_destroy(plan.stale_views[i]);
        legacy_allocation_free(plan.stale_views[i]);
    }
    for (int i = 0; i < plan.removed_count; ++i) {
        struct window *window = window_manager_find_window(&g_window_manager, plan.removed_windows[i]);
        if (window) {
            window_manager_adjust_layer(window, LAYER_NORMAL);
            window_manager_purify_window(&g_window_manager, window);
        }
    }

#ifdef TESTS
    if (event_loop_topology_postcommit_hook) {
        event_loop_topology_postcommit_hook();
    } else
#endif
    {
        float animation_duration = g_window_manager.window_animation_duration;
        g_window_manager.window_animation_duration = 0.0f;
        for (int i = 0; i < plan.space_count; ++i) {
            struct view *view = plan.spaces[i].view;
            if (plan.spaces[i].sid == plan.active_sid) {
                window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager,
                                                                        plan.spaces[i].sid);
            } else if (view && view->layout != VIEW_FLOAT) {
                view_clear_flag(view, VIEW_IS_VALID);
            }
        }
        g_window_manager.window_animation_duration = animation_duration;
        if (space_manager_refresh_application_windows(&g_space_manager)) event_loop_window_inventory_did_change();
        if (workspace_is_macos_monterey_or_newer()) mission_control_ensure_observed();
        mission_control_schedule_poll(0.1);
        menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_TOPOLOGY | MENU_BAR_DIRTY_DISPLAY |
                                    MENU_BAR_DIRTY_SPACE | MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_FOCUS);
        declarative_configuration_request_reconcile();
    }
    event_loop_topology_plan_destroy(&plan, true);
    return true;
}

static void event_loop_cancel_topology_retry(struct event_loop *event_loop)
{
    if (event_loop) __atomic_store_n(&event_loop->topology_resync_retry_scheduled, 0, __ATOMIC_RELEASE);
}

static void event_loop_fire_topology_retry(struct event_loop *event_loop, uint64_t generation)
{
    uint64_t token = generation;
    if (!__atomic_compare_exchange_n(&event_loop->topology_resync_retry_scheduled, &token, 0, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    uint64_t live_generation = __atomic_load_n(&event_loop->generation, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&event_loop->is_running, __ATOMIC_ACQUIRE) &&
        (live_generation == generation || (!live_generation && generation == 1)) &&
        __atomic_load_n(&event_loop->topology_resync_pending, __ATOMIC_ACQUIRE)) {
        sem_post(event_loop->semaphore);
    }
}

static void event_loop_schedule_topology_retry(struct event_loop *event_loop)
{
    if (!event_loop || !event_loop->semaphore ||
        !__atomic_load_n(&event_loop->is_running, __ATOMIC_ACQUIRE)) return;
    uint64_t generation = __atomic_load_n(&event_loop->generation, __ATOMIC_ACQUIRE);
    if (!generation) generation = 1;
    uint64_t expected = 0;
    if (!__atomic_compare_exchange_n(&event_loop->topology_resync_retry_scheduled, &expected, generation, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
#ifdef TESTS
    if (event_loop_topology_retry_test_scheduler) {
        event_loop_topology_retry_test_scheduler(event_loop, generation);
        return;
    }
#endif
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        event_loop_fire_topology_retry(event_loop, generation);
    });
}

static bool event_loop_consume_topology_resync(struct event_loop *event_loop)
{
    if (!event_loop || !__atomic_exchange_n(&event_loop->topology_resync_pending, 0, __ATOMIC_ACQ_REL)) return false;
    bool recovered = event_loop_recover_topology(event_loop);
    bool follow_up = __atomic_load_n(&event_loop->topology_resync_pending, __ATOMIC_ACQUIRE) != 0;
    if (!recovered) {
        if (!follow_up) {
            __atomic_store_n(&event_loop->topology_resync_pending, 1, __ATOMIC_RELEASE);
            event_loop_schedule_topology_retry(event_loop);
        }
        return false;
    }
    event_loop_cancel_topology_retry(event_loop);
    /* A trigger observed during recovery is deliberately handled by the next
     * semaphore pass, never by a busy-spin in this pass. */
    return !follow_up;
}

static void event_loop_dispatch_event(struct event event)
{
    switch (event.type) {
    case SPACE_CHANGED:
    case DISPLAY_ADDED:
    case DISPLAY_REMOVED:
    case DISPLAY_MOVED:
    case DISPLAY_RESIZED:
    case DISPLAY_CHANGED:
        native_resize_abort(&g_mouse_state, true);
        break;
    default:
        break;
    }
    switch (event.type) {
#define EVENT_TYPE_ENTRY(value) case value: EVENT_HANDLER_##value(event.context, event.param1); break;
        EVENT_TYPE_LIST
#undef EVENT_TYPE_ENTRY
    }
}

static void event_loop_execute_final_sync(struct event_loop *event_loop)
{
    pthread_mutex_lock(&event_loop->producer_lock);
    if (!event_loop->final_sync_pending) {
        pthread_mutex_unlock(&event_loop->producer_lock);
        return;
    }
    bool (*callback)(void *) = event_loop->final_sync_callback;
    void *context = event_loop->final_sync_context;
    event_loop->final_sync_pending = false;
    pthread_mutex_unlock(&event_loop->producer_lock);

    bool result = !callback || callback(context);

    pthread_mutex_lock(&event_loop->producer_lock);
    event_loop->final_sync_result = result;
    event_loop->final_sync_completed = true;
    pthread_cond_broadcast(&event_loop->producer_cond);
    pthread_mutex_unlock(&event_loop->producer_lock);
}

static void *event_loop_run(void *context)
{
    struct event event;
    struct event_loop_bootstrap *bootstrap = context;
    struct event_loop *event_loop = bootstrap->event_loop;
    event_loop_worker = event_loop;

    NSAutoreleasePool *bootstrap_pool = [[NSAutoreleasePool alloc] init];
    bool bootstrap_result = !bootstrap->callback || bootstrap->callback(bootstrap->context);
    [bootstrap_pool drain];

    pthread_mutex_lock(&bootstrap->mutex);
    bootstrap->result = bootstrap_result;
    bootstrap->completed = true;
    if (!bootstrap_result) __atomic_store_n(&event_loop->is_running, false, __ATOMIC_RELEASE);
    pthread_cond_signal(&bootstrap->cond);
    pthread_mutex_unlock(&bootstrap->mutex);

    while (__atomic_load_n(&event_loop->is_running, __ATOMIC_ACQUIRE)) {
        while (sem_wait(event_loop->semaphore) == -1 && errno == EINTR) {}
        if (!__atomic_load_n(&event_loop->is_running, __ATOMIC_ACQUIRE)) break;

        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        for (;;) {
            while (event_queue_pop(&event_loop->queue, &event)) {
                profile_begin();

                event_loop_dispatch_event(event);

                event_signal_flush();
                ts_reset();

                profile_end_and_print();
            }
            bool topology_work = event_loop_consume_topology_resync(event_loop);
            bool correction_work = managed_window_correction_consume(event_loop);
            if (!topology_work && !correction_work) break;
            event_signal_flush();
            ts_reset();
        }
        event_loop_execute_final_sync(event_loop);

        [pool drain];
    }

    event_loop_worker = NULL;
    return NULL;
}

static void event_loop_release_event_context(enum event_type type, void *context)
{
    if (type == WINDOW_FOCUS_FALLBACK) {
        __atomic_store_n(&window_focus_fallback_reserved, 0, __ATOMIC_RELEASE);
        return;
    }
    if (type == CONFIGURATION_RECONCILE) {
        declarative_configuration_reconcile_cancel((uint64_t) (uintptr_t) context);
        return;
    }
    if (!context) return;

    switch (type) {
    case APPLICATION_FRONT_SWITCHED: {
        free(context);
    } break;
    case MISSION_CONTROL_SHOW_ALL_WINDOWS:
    case MISSION_CONTROL_SHOW_FRONT_WINDOWS:
    case MISSION_CONTROL_SHOW_DESKTOP:
    case MISSION_CONTROL_EXIT: {
        mission_control_event_context_destroy(context);
    } break;
    case MISSION_CONTROL_CHECK_FOR_EXIT: {
        mission_control_poll_context_destroy(context);
    } break;
    case WINDOW_CREATED:
    case MOUSE_DOWN:
    case MOUSE_UP:
    case MOUSE_DRAGGED:
    case MOUSE_MOVED: {
        CFRelease(context);
    } break;
    case MANAGED_WINDOW_CORRECTION_HINT: {
        managed_window_correction_hint_context_destroy(context);
    } break;
    case WINDOW_CREATED_RETRY: {
        window_manager_destroy_window_created_retry(context);
    } break;
    case MENU_BAR_ACTION: {
        menu_bar_manager_release_action(context);
    } break;
    case EVENT_LOOP_SYNC: {
        event_loop_sync_complete(context, false);
    } break;
    default: {
    } break;
    }
}

static bool event_loop_rejected_event_is_fatal(enum event_type type)
{
    switch (type) {
    case APPLICATION_TERMINATED:
    case WINDOW_DESTROYED:
    case SLS_WINDOW_DESTROYED:
    case SLS_SPACE_DESTROYED:
    case DISPLAY_REMOVED: {
        return true;
    } break;
    default: {
        return false;
    } break;
    }
}

static void event_loop_discard_event(enum event_type type, void *context, int param1, bool rejected)
{
    if (rejected && event_loop_rejected_event_is_fatal(type)) {
        error("fatal error: event queue rejected destructive event %d\n", type);
    }

    switch (type) {
    case DAEMON_MESSAGE: {
        if (context) {
            daemon_io_reject(context);
        } else {
            /* Compatibility for events queued before the transport handoff. */
            daemon_message_admission_release(&g_daemon_message_admission);
            if (param1 >= 0) socket_close(param1);
        }
    } break;
    default: {
        event_loop_release_event_context(type, context);
    } break;
    }
}

#define EVENT_LOOP_ADMISSION_OPEN       (1u << 31)
#define EVENT_LOOP_ADMISSION_COUNT_MASK (~EVENT_LOOP_ADMISSION_OPEN)

#ifdef TESTS
static void (*event_loop_admit_producer_test_hook)(void);
static void (*event_loop_leave_producer_test_hook)(void);
static void (*event_loop_shutdown_admitted_test_hook)(struct event_loop *);
static void (*event_loop_shutdown_owner_test_hook)(bool);
#endif

static uint32_t event_loop_admission_count(uint32_t state)
{
    return state & EVENT_LOOP_ADMISSION_COUNT_MASK;
}

static bool event_loop_admission_is_open(uint32_t state)
{
    return (state & EVENT_LOOP_ADMISSION_OPEN) != 0;
}

static bool event_loop_admit_producer(struct event_loop *event_loop)
{
#ifdef TESTS
    if (event_loop_admit_producer_test_hook) event_loop_admit_producer_test_hook();
#endif

    if (!event_loop) return false;

    uint32_t state = __atomic_load_n(&event_loop->producer_admission, __ATOMIC_ACQUIRE);
    while (event_loop_admission_is_open(state)) {
        if (event_loop_admission_count(state) == EVENT_LOOP_ADMISSION_COUNT_MASK) return false;
        if (__atomic_compare_exchange_n(&event_loop->producer_admission, &state, state + 1, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return true;
    }

    return false;
}

static void event_loop_leave_producer(struct event_loop *event_loop)
{
    pthread_mutex_lock(&event_loop->producer_lock);
    uint32_t previous = __atomic_fetch_sub(&event_loop->producer_admission, 1, __ATOMIC_ACQ_REL);
    uint32_t current = previous - 1;
#ifdef TESTS
    if (event_loop_leave_producer_test_hook) event_loop_leave_producer_test_hook();
#endif
    if (!event_loop_admission_is_open(current)) pthread_cond_broadcast(&event_loop->producer_cond);
    pthread_mutex_unlock(&event_loop->producer_lock);
}

static void event_loop_wait_for_producer_count(struct event_loop *event_loop, uint32_t count)
{
    if (!event_loop->producer_sync_initialized) return;

    pthread_mutex_lock(&event_loop->producer_lock);
    while (event_loop_admission_count(__atomic_load_n(&event_loop->producer_admission, __ATOMIC_ACQUIRE)) > count) {
        pthread_cond_wait(&event_loop->producer_cond, &event_loop->producer_lock);
    }
    pthread_mutex_unlock(&event_loop->producer_lock);
}

static void event_loop_wait_for_producers_closed(struct event_loop *event_loop)
{
    event_loop_wait_for_producer_count(event_loop, 0);
}

static void event_loop_close_producer_admission(struct event_loop *event_loop)
{
    __atomic_fetch_and(&event_loop->producer_admission, EVENT_LOOP_ADMISSION_COUNT_MASK, __ATOMIC_ACQ_REL);
    event_loop_wait_for_producers_closed(event_loop);
}

static void event_loop_close_producer_admission_after_admitted_self(struct event_loop *event_loop)
{
    __atomic_fetch_and(&event_loop->producer_admission, EVENT_LOOP_ADMISSION_COUNT_MASK, __ATOMIC_ACQ_REL);
    event_loop_leave_producer(event_loop);
    event_loop_wait_for_producers_closed(event_loop);
}

static bool event_loop_enqueue(struct event_loop *event_loop, enum event_type type, void *context, int param1)
{
    struct event replaced;
    uint32_t capacity = 0;
    bool post_wake = false;
    struct event event = { .type = type, .param1 = param1, .context = context };
    bool merge = type == MOUSE_DRAGGED || type == MOUSE_MOVED || type == MENU_BAR_REFRESH;
    enum event_queue_result result = event_queue_push(&event_loop->queue, event, merge, &replaced, &capacity, &post_wake);

    if (result == EVENT_QUEUE_MERGED) {
        event_loop_release_event_context(replaced.type, replaced.context);
    } else if (result == EVENT_QUEUE_GREW) {
        warn("%s: the event loop is falling behind, its queue now holds %u events\n", __FUNCTION__, capacity);
    } else if (result == EVENT_QUEUE_FULL) {
        warn("%s: could not grow the event queue, event %d dropped\n", __FUNCTION__, type);
        if (event_loop_event_requires_topology_resync(type)) event_loop_reserve_topology_resync(event_loop);
        event_loop_discard_event(type, context, param1, true);
        return false;
    }

    if (post_wake && event_loop->semaphore) sem_post(event_loop->semaphore);
    return true;
}

bool event_loop_try_post_generation(struct event_loop *event_loop, uint64_t generation,
                                    enum event_type type, void *context, int param1)
{
    if (!event_loop_admit_producer(event_loop)) {
        event_loop_discard_event(type, context, param1, true);
        return false;
    }
    if (__atomic_load_n(&event_loop->generation, __ATOMIC_ACQUIRE) != generation) {
        event_loop_discard_event(type, context, param1, true);
        event_loop_leave_producer(event_loop);
        return false;
    }
    bool result = event_loop_enqueue(event_loop, type, context, param1);
    event_loop_leave_producer(event_loop);
    return result;
}

bool event_loop_try_post(struct event_loop *event_loop, enum event_type type, void *context, int param1)
{
    if (!event_loop_admit_producer(event_loop)) {
        event_loop_discard_event(type, context, param1, true);
        return false;
    }

    bool result = event_loop_enqueue(event_loop, type, context, param1);
    event_loop_leave_producer(event_loop);
    return result;
}

void event_loop_post(struct event_loop *event_loop, enum event_type type, void *context, int param1)
{
    (void) event_loop_try_post(event_loop, type, context, param1);
}

#define APPLICATION_LAUNCH_RETRY_LIMIT 5

#ifdef TESTS
static void (*event_loop_test_application_retry_scheduler)(struct process *);
#endif

void event_loop_schedule_application_launch_retry(struct process *process)
{
    if (!process) return;
    if (!process_launch_retry_reserve(process, APPLICATION_LAUNCH_RETRY_LIMIT)) {
        debug("%s: exhausted application launch retry for pid %d; a later front/process event may restart recovery\n",
              __FUNCTION__, process->pid);
        return;
    }
#ifdef TESTS
    if (event_loop_test_application_retry_scheduler) {
        event_loop_test_application_retry_scheduler(process);
        return;
    }
#endif
    __block ProcessSerialNumber psn = process->psn;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1f * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        struct process *current = process_manager_find_process(&g_process_manager, &psn);
        if (current && psn_equals(&current->psn, &psn)) event_loop_post_application_launched(current);
    });
}

bool event_loop_post_application_launched(struct process *process)
{
    if (!process) return false;
    if (event_loop_try_post(&g_event_loop, APPLICATION_LAUNCHED, process, 0)) return true;
    event_loop_schedule_application_launch_retry(process);
    return false;
}

#ifdef TESTS
static int (*event_loop_thread_create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *) = pthread_create;
#else
#define event_loop_thread_create pthread_create
#endif

static void event_loop_cleanup(struct event_loop *event_loop)
{
    __atomic_store_n(&event_loop->managed_window_correction_pending, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&event_loop->managed_window_correction_hint_pending, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&event_loop->managed_window_correction_scheduled, 0, __ATOMIC_RELEASE);
    event_loop_close_producer_admission(event_loop);

    if (event_loop->queue.events) {
        struct event event;
        while (event_queue_pop(&event_loop->queue, &event)) {
            event_loop_discard_event(event.type, event.context, event.param1, false);
        }
        event_queue_destroy(&event_loop->queue);
    }
    if (event_loop->semaphore) sem_close(event_loop->semaphore);
    event_loop->semaphore = NULL;
    if (event_loop->producer_sync_initialized) {
        pthread_cond_destroy(&event_loop->producer_cond);
        pthread_mutex_destroy(&event_loop->producer_lock);
    }
    memset(&event_loop->thread, 0, sizeof(event_loop->thread));
    event_loop->final_sync_pending = false;
    event_loop->final_sync_completed = false;
    event_loop->final_sync_result = false;
    event_loop->final_sync_callback = NULL;
    event_loop->final_sync_context = NULL;
    event_loop->producer_sync_initialized = false;
    __atomic_store_n(&event_loop->producer_admission, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&event_loop->shutdown_owner, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&event_loop->topology_resync_pending, 0, __ATOMIC_RELEASE);
    event_loop_cancel_topology_retry(event_loop);
}

static volatile uint64_t event_loop_generation_counter;

bool event_loop_begin_bootstrap(struct event_loop *event_loop, event_loop_bootstrap_callback callback, void *context)
{
    /* Cleanup leaves producer admission closed; publish the new generation
     * atomically so generation-bound transports can safely reject stale work
     * while this lifetime is being initialized. */
    __atomic_store_n(&event_loop->producer_admission, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&event_loop->shutdown_owner, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&event_loop->is_running, false, __ATOMIC_RELEASE);
    event_loop->producer_sync_initialized = false;
    event_loop->semaphore = NULL;
    event_loop->queue = (struct event_queue) {0};
    memset(&event_loop->thread, 0, sizeof(event_loop->thread));
    uint64_t generation = __atomic_add_fetch(&event_loop_generation_counter, 1, __ATOMIC_RELAXED);
    if (!generation) generation = __atomic_add_fetch(&event_loop_generation_counter, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&event_loop->generation, generation, __ATOMIC_RELEASE);
    if (!event_queue_init(&event_loop->queue, EVENT_QUEUE_CAPACITY)) return false;

    event_loop->semaphore = sem_open("yabai_event_loop_semaphore", O_CREAT, 0600, 0);
    sem_unlink("yabai_event_loop_semaphore");
    if (event_loop->semaphore == SEM_FAILED) {
        event_loop->semaphore = NULL;
        event_queue_destroy(&event_loop->queue);
        return false;
    }

    if (pthread_mutex_init(&event_loop->producer_lock, NULL) != 0) {
        event_loop_cleanup(event_loop);
        return false;
    }
    if (pthread_cond_init(&event_loop->producer_cond, NULL) != 0) {
        pthread_mutex_destroy(&event_loop->producer_lock);
        event_loop_cleanup(event_loop);
        return false;
    }
    event_loop->producer_sync_initialized = true;
    __atomic_store_n(&event_loop->producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);

    struct event_loop_bootstrap bootstrap = {
        .event_loop = event_loop,
        .callback = callback,
        .context = context,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER
    };

    __atomic_store_n(&event_loop->is_running, true, __ATOMIC_RELEASE);
    if (event_loop_thread_create(&event_loop->thread, NULL, &event_loop_run, &bootstrap) != 0) {
        __atomic_store_n(&event_loop->is_running, false, __ATOMIC_RELEASE);
        event_loop_close_producer_admission(event_loop);
        pthread_cond_destroy(&bootstrap.cond);
        pthread_mutex_destroy(&bootstrap.mutex);
        event_loop_cleanup(event_loop);
        return false;
    }

    pthread_mutex_lock(&bootstrap.mutex);
    while (!bootstrap.completed) pthread_cond_wait(&bootstrap.cond, &bootstrap.mutex);
    bool result = bootstrap.result;
    pthread_mutex_unlock(&bootstrap.mutex);
    pthread_cond_destroy(&bootstrap.cond);
    pthread_mutex_destroy(&bootstrap.mutex);

    if (!result) {
        event_loop_close_producer_admission(event_loop);
        pthread_join(event_loop->thread, NULL);
        event_loop_cleanup(event_loop);
    } else if (event_loop == &g_event_loop) {
        mission_control_admission_open(generation);
        if (event_loop_admit_producer(event_loop)) {
            __atomic_store_n(&event_loop->managed_window_correction_pending, 1, __ATOMIC_RELEASE);
            __atomic_store_n(&event_loop->managed_window_correction_hint_pending, 1, __ATOMIC_RELEASE);
            if (event_loop->semaphore) sem_post(event_loop->semaphore);
            event_loop_leave_producer(event_loop);
        }
    }
    return result;
}

bool event_loop_begin(struct event_loop *event_loop)
{
    return event_loop_begin_bootstrap(event_loop, NULL, NULL);
}

bool event_loop_is_worker_thread(struct event_loop *event_loop)
{
    return event_loop && event_loop_worker == event_loop;
}

uint64_t event_loop_generation_snapshot(struct event_loop *event_loop)
{
    return event_loop ? __atomic_load_n(&event_loop->generation, __ATOMIC_ACQUIRE) : 0;
}

bool event_loop_perform_sync(struct event_loop *event_loop, event_loop_sync_callback callback, void *context)
{
    struct event_loop_sync sync = {
        .callback = callback,
        .context = context,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER
    };

    if (!event_loop_admit_producer(event_loop)) {
        pthread_cond_destroy(&sync.cond);
        pthread_mutex_destroy(&sync.mutex);
        return false;
    }

    if (event_loop_is_worker_thread(event_loop)) {
        event_loop_leave_producer(event_loop);
        pthread_cond_destroy(&sync.cond);
        pthread_mutex_destroy(&sync.mutex);
        return false;
    }

    (void) event_loop_enqueue(event_loop, EVENT_LOOP_SYNC, &sync, 0);
    event_loop_leave_producer(event_loop);

    pthread_mutex_lock(&sync.mutex);
    while (!sync.completed) pthread_cond_wait(&sync.cond, &sync.mutex);
    bool result = sync.result;
    pthread_mutex_unlock(&sync.mutex);

    pthread_cond_destroy(&sync.cond);
    pthread_mutex_destroy(&sync.mutex);
    return result;
}

static bool event_loop_cancel_native_resize(void *context)
{
    (void) context;
    native_resize_abort(&g_mouse_state, true);
    return true;
}

static bool event_loop_close_with_final_sync(struct event_loop *event_loop,
                                             bool (*callback)(void *), void *context)
{
    __atomic_fetch_and(&event_loop->producer_admission, EVENT_LOOP_ADMISSION_COUNT_MASK, __ATOMIC_ACQ_REL);
    event_loop_wait_for_producer_count(event_loop, 1);

    pthread_mutex_lock(&event_loop->producer_lock);
    event_loop->final_sync_callback = callback;
    event_loop->final_sync_context = context;
    event_loop->final_sync_result = false;
    event_loop->final_sync_completed = false;
    event_loop->final_sync_pending = true;
    pthread_mutex_unlock(&event_loop->producer_lock);

    event_loop_leave_producer(event_loop);
    event_loop_wait_for_producers_closed(event_loop);
    sem_post(event_loop->semaphore);

    pthread_mutex_lock(&event_loop->producer_lock);
    while (!event_loop->final_sync_completed) {
        pthread_cond_wait(&event_loop->producer_cond, &event_loop->producer_lock);
    }
    bool result = event_loop->final_sync_result;
    event_loop->final_sync_callback = NULL;
    event_loop->final_sync_context = NULL;
    pthread_mutex_unlock(&event_loop->producer_lock);
    return result;
}

void event_loop_end(struct event_loop *event_loop)
{
    if (!event_loop_admit_producer(event_loop)) return;
    if (event_loop_is_worker_thread(event_loop)) {
        event_loop_leave_producer(event_loop);
        warn("%s: ignoring event-loop shutdown requested from the event-loop thread\n", __FUNCTION__);
        return;
    }
#ifdef TESTS
    if (event_loop_shutdown_admitted_test_hook) event_loop_shutdown_admitted_test_hook(event_loop);
#endif

    uint32_t expected_owner = 0;
    if (!__atomic_compare_exchange_n(&event_loop->shutdown_owner, &expected_owner, 1, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
#ifdef TESTS
        if (event_loop_shutdown_owner_test_hook) event_loop_shutdown_owner_test_hook(false);
#endif
        event_loop_leave_producer(event_loop);
        return;
    }
#ifdef TESTS
    if (event_loop_shutdown_owner_test_hook) event_loop_shutdown_owner_test_hook(true);
#endif

    if (event_loop == &g_event_loop) {
        declarative_configuration_reconcile_shutdown();
        mission_control_shutdown();
    }
    bool running = __atomic_load_n(&event_loop->is_running, __ATOMIC_ACQUIRE);
    if (event_loop == &g_event_loop && running) {
        (void) event_loop_close_with_final_sync(event_loop, event_loop_cancel_native_resize, NULL);
    } else {
        event_loop_close_producer_admission_after_admitted_self(event_loop);
    }
    __atomic_store_n(&event_loop->is_running, false, __ATOMIC_RELEASE);
    sem_post(event_loop->semaphore);
    pthread_join(event_loop->thread, NULL);
    if (event_loop == &g_event_loop) {
        g_mission_control_session = (struct mission_control_session_state) {0};
        g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
        __atomic_store_n(&g_mission_control_poll_generation, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_mission_control_poll_token, 0, __ATOMIC_RELEASE);
    }
    event_loop_cleanup(event_loop);
}
