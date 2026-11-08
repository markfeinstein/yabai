/* Managed window frame correction.
 *
 * After yabai writes a managed window's frame, some applications settle at a
 * different size or position. This module keeps a bounded retry lease per
 * window: it rewrites the layout frame on a sparse schedule, observes
 * application frame-recovery hints, and gives up after the last deadline.
 * Work is coalesced into one dispatch timer and consumed on the event loop.
 *
 * Included into the event_loop.c translation unit; it uses that file's
 * constants, test seams, and producer admission. */

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
    (void) param1;
    struct managed_window_correction_hint_context *hint = context;
    managed_window_correction_schedule_application_hint(managed_window_correction_hint_application(hint));
    managed_window_correction_hint_context_destroy(hint);
}

static void managed_window_correction_clear(struct window *window);
static void managed_window_correction_schedule_application(struct application *application);

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
