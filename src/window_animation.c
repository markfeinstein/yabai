#include "window_animation.h"

extern struct window_manager g_window_manager;

static void window_manager_refresh_active_window_highlight_for_captures(struct window_capture *window_list, int window_count);

static uint64_t window_manager_retry_delay_ms(uint8_t attempt)
{
    uint8_t shift = attempt < 6 ? attempt : 6;
    return 100ULL << shift;
}

static void window_manager_destroy_window_proxy(int animation_connection, struct window_proxy *proxy);

#ifdef TESTS
static CGError (*window_manager_proxy_new_region)(CGRect *, CFTypeRef *) = CGSNewRegionWithRect;
static CFTypeRef (*window_manager_proxy_empty_region)(void) = CGRegionCreateEmptyRegion;
static CGError (*window_manager_proxy_new_window)(int, int, CFTypeRef, CFTypeRef, int, uint64_t *, float, float, int, uint32_t *, void *) = SLSNewWindowWithOpaqueShapeAndContext;
static CGContextRef (*window_manager_proxy_new_context)(int, uint32_t, CFDictionaryRef) = SLWindowContextCreate;
static CGError (*window_manager_proxy_release_window)(int, uint32_t) = SLSReleaseWindow;
static CFArrayRef (*window_manager_proxy_capture)(int, uint32_t *, int, uint32_t) = SLSHWCaptureWindowList;
static bool (*window_manager_proxy_disable_shadow)(uint32_t) = sls_window_disable_shadow;
static CGError (*window_manager_proxy_set_opacity)(int, uint32_t, bool) = SLSSetWindowOpacity;
static CGError (*window_manager_proxy_set_resolution)(int, uint32_t, double) = SLSSetWindowResolution;
static CGError (*window_manager_proxy_set_alpha)(int, uint32_t, float) = SLSSetWindowAlpha;
static CGError (*window_manager_proxy_set_level)(int, uint32_t, int) = SLSSetWindowLevel;
static CGError (*window_manager_proxy_set_sub_level)(int, uint32_t, int) = SLSSetWindowSubLevel;
#else
#define window_manager_proxy_new_region CGSNewRegionWithRect
#define window_manager_proxy_empty_region CGRegionCreateEmptyRegion
#define window_manager_proxy_new_window SLSNewWindowWithOpaqueShapeAndContext
#define window_manager_proxy_new_context SLWindowContextCreate
#define window_manager_proxy_release_window SLSReleaseWindow
#define window_manager_proxy_capture SLSHWCaptureWindowList
#define window_manager_proxy_disable_shadow sls_window_disable_shadow
#define window_manager_proxy_set_opacity SLSSetWindowOpacity
#define window_manager_proxy_set_resolution SLSSetWindowResolution
#define window_manager_proxy_set_alpha SLSSetWindowAlpha
#define window_manager_proxy_set_level SLSSetWindowLevel
#define window_manager_proxy_set_sub_level SLSSetWindowSubLevel
#endif

static bool window_manager_create_window_proxy(int animation_connection, float alpha, struct window_proxy *proxy)
{
    if (!proxy->image || CFGetTypeID(proxy->image) != CGImageGetTypeID()) return false;

    CFTypeRef frame_region = NULL;
    CFTypeRef empty_region = NULL;
    if (window_manager_proxy_new_region(&proxy->frame, &frame_region) != kCGErrorSuccess || !frame_region) goto fail;
    empty_region = window_manager_proxy_empty_region();
    if (!empty_region) goto fail;

    uint64_t tags = 1ULL << 46;
    if (window_manager_proxy_new_window(animation_connection, 2, frame_region, empty_region, 13|(1 << 18),
                                        &tags, 0, 0, 64, &proxy->id, NULL) != kCGErrorSuccess || !proxy->id) goto fail;
    proxy->context = window_manager_proxy_new_context(animation_connection, proxy->id, 0);
    if (!proxy->context) goto fail;
    // Corner rendering is cosmetic and proxy-only. A disabled or failing backend
    // must preserve the existing rectangular animation proxy.
    (void) window_corner_geometry_apply_to_proxy(animation_connection, proxy->id,
                                                 proxy->frame.size, &proxy->corner_geometry);
    // Shadow suppression is cosmetic; keep the animation proxy usable if it fails.
    (void) window_manager_proxy_disable_shadow(proxy->id);
    window_manager_proxy_set_opacity(animation_connection, proxy->id, 0);
    window_manager_proxy_set_resolution(animation_connection, proxy->id, 2.0f);
    window_manager_proxy_set_alpha(animation_connection, proxy->id, alpha);
    window_manager_proxy_set_level(animation_connection, proxy->id, proxy->level);
    window_manager_proxy_set_sub_level(animation_connection, proxy->id, proxy->sub_level);

    CGRect frame = { {0, 0}, proxy->frame.size };
    CGContextClearRect(proxy->context, frame);
    CGContextDrawImage(proxy->context, frame, proxy->image);
    CGContextFlush(proxy->context);
    CFRelease(frame_region);
    CFRelease(empty_region);
    return true;

fail:
    if (frame_region) CFRelease(frame_region);
    if (empty_region) CFRelease(empty_region);
    window_manager_destroy_window_proxy(animation_connection, proxy);
    return false;
}

static void window_manager_destroy_window_proxy(int animation_connection, struct window_proxy *proxy)
{
    if (!proxy) return;
    if (proxy->image) {
        CFRelease(proxy->image);
        proxy->image = NULL;
    }

    if (proxy->context) {
        CGContextRelease(proxy->context);
        proxy->context = NULL;
    }

    if (proxy->id) {
        (void) window_manager_proxy_release_window(animation_connection, proxy->id);
        proxy->id = 0;
    }
}

static CGImageRef window_manager_proxy_image_from_capture(CFArrayRef image_array, float alpha)
{
    if (!image_array || CFArrayGetCount(image_array) == 0) return NULL;
    CFTypeRef image = CFArrayGetValueAtIndex(image_array, 0);
    if (!image || CFGetTypeID(image) != CGImageGetTypeID()) return NULL;
    return alpha == 1.0f ? (CGImageRef) CFRetain(image) : cgimage_restore_alpha((CGImageRef) image);
}

static void window_manager_prepare_replacement_proxy(struct window_proxy *replacement,
                                                     const struct window_proxy *existing)
{
    replacement->frame = (CGRect) { { existing->tx, existing->ty }, { existing->tw, existing->th } };
    replacement->tx = existing->tx;
    replacement->ty = existing->ty;
    replacement->tw = existing->tw;
    replacement->th = existing->th;
    replacement->level = existing->level;
    replacement->sub_level = existing->sub_level;
    replacement->corner_geometry = existing->corner_geometry;
    replacement->image = existing->image ? (CGImageRef) CFRetain(existing->image) : NULL;
}

static void *window_manager_build_window_proxy_thread_proc(void *data)
{
    struct window_animation *animation = data;

    float alpha = 1.0f;
    SLSGetWindowAlpha(animation->cid, animation->wid, &alpha);
    animation->proxy.level = window_level(animation->wid);
    animation->proxy.sub_level = window_sub_level(animation->wid);
    if (SLSGetWindowBounds(animation->cid, animation->wid, &animation->proxy.frame) != kCGErrorSuccess) {
        __atomic_store_n(&animation->skip, true, __ATOMIC_RELEASE);
        return NULL;
    }
    animation->proxy.tx = animation->proxy.frame.origin.x;
    animation->proxy.ty = animation->proxy.frame.origin.y;
    animation->proxy.tw = animation->proxy.frame.size.width;
    animation->proxy.th = animation->proxy.frame.size.height;

    CFArrayRef image_array = window_manager_proxy_capture(animation->cid, &animation->wid, 1, (1 << 11) | (1 << 8));
    animation->proxy.image = window_manager_proxy_image_from_capture(image_array, alpha);
    if (image_array) CFRelease(image_array);

    if (!window_manager_create_window_proxy(animation->cid, alpha, &animation->proxy)) {
        __atomic_store_n(&animation->skip, true, __ATOMIC_RELEASE);
    }
    return NULL;
}

static void window_manager_animation_schedule_teardown(struct window_animation_context *context);

#ifdef TESTS
static CFTypeRef (*window_manager_animation_transaction_create)(int) = SLSTransactionCreate;
static void (*window_manager_animation_teardown_scheduler)(struct window_animation_context *) = window_manager_animation_schedule_teardown;
#else
#define window_manager_animation_transaction_create SLSTransactionCreate
#define window_manager_animation_teardown_scheduler window_manager_animation_schedule_teardown
#endif

static bool window_manager_animation_claim_completion(struct window_animation_context *context)
{
    return !__atomic_exchange_n(&context->completion_claimed, true, __ATOMIC_ACQ_REL);
}

static void window_manager_animation_tick(void *data, const struct animation_clock_tick *tick)
{
    struct window_animation_context *context = data;
    int animation_count = context->animation_count;

    double t = tick->elapsed_ticks / (double)(context->animation_duration * tick->ticks_per_second);
    if (t <= 0.0) t = 0.0f;
    if (t >= 1.0) t = 1.0f;

    float mt = (float) t;
    switch (context->animation_easing) {
#define ANIMATION_EASING_TYPE_ENTRY(value) case value##_type: mt = value(t); break;
        ANIMATION_EASING_TYPE_LIST
#undef ANIMATION_EASING_TYPE_ENTRY
    default: break;
    }

    pthread_mutex_lock(&g_window_manager.window_animations_lock);

    CFTypeRef transaction = window_manager_animation_transaction_create(context->animation_connection);
    if (transaction) {
        for (int i = 0; i < animation_count; ++i) {
            if (__atomic_load_n(&context->animation_list[i].skip, __ATOMIC_RELAXED)) continue;

            context->animation_list[i].proxy.tx = lerp(context->animation_list[i].proxy.frame.origin.x,    mt, context->animation_list[i].x);
            context->animation_list[i].proxy.ty = lerp(context->animation_list[i].proxy.frame.origin.y,    mt, context->animation_list[i].y);
            context->animation_list[i].proxy.tw = lerp(context->animation_list[i].proxy.frame.size.width,  mt, context->animation_list[i].w);
            context->animation_list[i].proxy.th = lerp(context->animation_list[i].proxy.frame.size.height, mt, context->animation_list[i].h);

            CGAffineTransform transform = CGAffineTransformMakeTranslation(-context->animation_list[i].proxy.tx, -context->animation_list[i].proxy.ty);
            CGAffineTransform scale = CGAffineTransformMakeScale(context->animation_list[i].proxy.frame.size.width / context->animation_list[i].proxy.tw, context->animation_list[i].proxy.frame.size.height / context->animation_list[i].proxy.th);
            SLSTransactionSetWindowTransform(transaction, context->animation_list[i].proxy.id, 0, 0, CGAffineTransformConcat(transform, scale));

            float alpha = 0.0f;
            SLSGetWindowAlpha(context->animation_connection, context->animation_list[i].wid, &alpha);
            if (alpha != 0.0f) SLSTransactionSetWindowAlpha(transaction, context->animation_list[i].proxy.id, alpha);
        }
        SLSTransactionCommit(transaction, 0);
        CFRelease(transaction);
    }

    if (t != 1.0f) {
        pthread_mutex_unlock(&g_window_manager.window_animations_lock);
        return;
    }

    bool claimed = window_manager_animation_claim_completion(context);
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);
    if (claimed) window_manager_animation_teardown_scheduler(context);
}

#ifdef TESTS
static void *(*window_manager_animation_allocate)(size_t) = malloc;
static CGError (*window_manager_animation_new_connection)(int, int *) = SLSNewConnection;
static struct animation_clock *(*window_manager_animation_clock_create)(uint32_t, animation_clock_tick_fn, void *) =
    animation_clock_create_for_display;
static uint32_t (*window_manager_animation_display_for_point)(CGPoint) = display_manager_point_display_id;
static bool (*window_manager_animation_clock_start)(struct animation_clock *) = animation_clock_start;
static enum animation_clock_stop_result (*window_manager_animation_clock_stop)(struct animation_clock *) =
    animation_clock_stop;
static void (*window_manager_animation_clock_destroy)(struct animation_clock *) = animation_clock_destroy;
static CGError (*window_manager_animation_release_connection)(int) = SLSReleaseConnection;
static CGError (*window_manager_animation_disable_update)(int) = SLSDisableUpdate;
static CGError (*window_manager_animation_reenable_update)(int) = SLSReenableUpdate;
static void (*window_manager_animation_set_frame)(struct window *, float, float, float, float) = window_manager_set_window_frame;
#else
#define window_manager_animation_allocate malloc
#define window_manager_animation_new_connection SLSNewConnection
#define window_manager_animation_clock_create animation_clock_create_for_display
#define window_manager_animation_display_for_point display_manager_point_display_id
#define window_manager_animation_clock_start animation_clock_start
#define window_manager_animation_clock_stop animation_clock_stop
#define window_manager_animation_clock_destroy animation_clock_destroy
#define window_manager_animation_release_connection SLSReleaseConnection
#define window_manager_animation_disable_update SLSDisableUpdate
#define window_manager_animation_reenable_update SLSReenableUpdate
#define window_manager_animation_set_frame window_manager_set_window_frame
#endif

static struct window_animation_context *window_manager_animation_create_context(int window_count)
{
    struct window_animation_context *context = window_manager_animation_allocate(sizeof(*context));
    if (!context) return NULL;
    *context = (struct window_animation_context) { .animation_count = window_count };

    if (window_manager_animation_new_connection(0, &context->animation_connection) != kCGErrorSuccess ||
        !context->animation_connection) {
        free(context);
        return NULL;
    }

    context->animation_list = window_manager_animation_allocate((size_t) window_count * sizeof(struct window_animation));
    if (!context->animation_list) {
        (void) window_manager_animation_release_connection(context->animation_connection);
        free(context);
        return NULL;
    }
    memset(context->animation_list, 0, (size_t) window_count * sizeof(struct window_animation));
    return context;
}

static bool window_manager_animation_start_clock(struct window_animation_context *context)
{
    uint32_t did = 0;
    if (context->animation_list && context->animation_count > 0) {
        struct window_animation *first = &context->animation_list[0];
        CGPoint target_center = CGPointMake(first->x + first->w / 2.0f,
                                            first->y + first->h / 2.0f);
        did = window_manager_animation_display_for_point(target_center);
    }
    context->clock = window_manager_animation_clock_create(did, window_manager_animation_tick, context);
    return context->clock && window_manager_animation_clock_start(context->clock);
}

static void window_manager_animation_release_context(struct window_animation_context *context, bool update_disabled)
{
    if (update_disabled) (void) window_manager_animation_reenable_update(context->animation_connection);
    if (context->clock) window_manager_animation_clock_destroy(context->clock);
    if (context->animation_connection) (void) window_manager_animation_release_connection(context->animation_connection);
    free(context->animation_list);
    free(context);
    event_loop_request_active_window_highlight_refresh();
}

static void window_manager_animation_destroy_context(struct window_animation_context *context, bool update_disabled)
{
    pthread_mutex_lock(&g_window_manager.window_animations_lock);
    for (int i = 0; context->animation_list && i < context->animation_count; ++i) {
        struct window_animation *animation = &context->animation_list[i];
        if (table_find(&g_window_manager.window_animations_table, &animation->wid) == animation) {
            table_remove(&g_window_manager.window_animations_table, &animation->wid);
            window_manager_destroy_window_proxy(context->animation_connection, &animation->proxy);
        }
    }
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);
    window_manager_animation_release_context(context, update_disabled);
}

static void window_manager_animation_teardown_attempt(void *data);
static void window_manager_animation_dispatch_retry(struct window_animation_context *context, uint64_t delay_ms)
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ms * NSEC_PER_MSEC),
                   dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        window_manager_animation_teardown_attempt(context);
    });
}

#ifdef TESTS
static void (*window_manager_animation_retry_scheduler)(struct window_animation_context *, uint64_t) = window_manager_animation_dispatch_retry;
#else
#define window_manager_animation_retry_scheduler window_manager_animation_dispatch_retry
#endif

static void window_manager_animation_retry(struct window_animation_context *context)
{
    uint64_t delay = window_manager_retry_delay_ms(context->teardown_attempts);
    if (context->teardown_attempts != UINT8_MAX) ++context->teardown_attempts;
    window_manager_animation_retry_scheduler(context, delay);
}

static void window_manager_animation_teardown_attempt(void *data)
{
    struct window_animation_context *context = data;
    if (window_manager_animation_clock_stop(context->clock) == ANIMATION_CLOCK_STOP_RETRY) {
        window_manager_animation_retry(context);
        return;
    }

    (void) window_manager_animation_disable_update(context->animation_connection);
    pthread_mutex_lock(&g_window_manager.window_animations_lock);

    for (int i = 0; i < context->animation_count; ++i) {
        struct window_animation *animation = &context->animation_list[i];
        if (table_find(&g_window_manager.window_animations_table, &animation->wid) != animation) {
            __atomic_store_n(&animation->skip, true, __ATOMIC_RELEASE);
        }
    }

    if (context->proxies_may_be_installed &&
        !scripting_addition_swap_window_proxy_out(context->animation_list, context->animation_count)) {
        pthread_mutex_unlock(&g_window_manager.window_animations_lock);
        (void) window_manager_animation_reenable_update(context->animation_connection);
        window_manager_animation_retry(context);
        return;
    }
    context->proxies_may_be_installed = false;

    for (int i = 0; i < context->animation_count; ++i) {
        struct window_animation *animation = &context->animation_list[i];
        if (table_find(&g_window_manager.window_animations_table, &animation->wid) == animation) {
            table_remove(&g_window_manager.window_animations_table, &animation->wid);
            window_manager_destroy_window_proxy(context->animation_connection, &animation->proxy);
        }
    }
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);
    window_manager_animation_release_context(context, true);
}

static void window_manager_animation_schedule_teardown(struct window_animation_context *context)
{
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        window_manager_animation_teardown_attempt(context);
    });
}
static void window_manager_animation_setup_cleanup(struct window_animation_context *context, bool update_disabled)
{
    if (!context) return;
    if (update_disabled && context->animation_connection) (void) window_manager_animation_reenable_update(context->animation_connection);

    if (context->proxies_may_be_installed) {
        if (window_manager_animation_claim_completion(context)) {
            window_manager_animation_schedule_teardown(context);
        }
    } else {
        window_manager_animation_destroy_context(context, false);
    }
}
static void window_manager_animation_apply_final_frames(struct window_animation_context *context)
{
    struct window_capture *captures = NULL;
#ifdef TESTS
    if (window_manager_animation_set_frame == window_manager_set_window_frame)
#endif
    captures = malloc((size_t) context->animation_count * sizeof(struct window_capture));

    if (!captures) {
        for (int i = 0; i < context->animation_count; ++i) {
            struct window_animation *animation = &context->animation_list[i];
            window_manager_animation_set_frame(animation->window, animation->x, animation->y, animation->w, animation->h);
        }
        return;
    }

    for (int i = 0; i < context->animation_count; ++i) {
        struct window_animation *animation = &context->animation_list[i];
        captures[i] = (struct window_capture) {
            .window = animation->window, .x = animation->x, .y = animation->y, .w = animation->w, .h = animation->h
        };
    }
    window_manager_set_window_frames(captures, context->animation_count);
    free(captures);
}

static void window_manager_animation_prune_failed_locked(struct window_animation_context *context)
{
    for (int i = 0; i < context->animation_count; ++i) {
        struct window_animation *animation = &context->animation_list[i];
        if (__atomic_load_n(&animation->skip, __ATOMIC_ACQUIRE) &&
            table_find(&g_window_manager.window_animations_table, &animation->wid) == animation) {
            table_remove(&g_window_manager.window_animations_table, &animation->wid);
            window_manager_destroy_window_proxy(context->animation_connection, &animation->proxy);
        }
    }
}

static bool window_manager_animation_publish_new_locked(struct window_animation *animation)
{
    if (table_try_add(&g_window_manager.window_animations_table, &animation->wid, animation)) return true;
    __atomic_store_n(&animation->skip, true, __ATOMIC_RELEASE);
    return false;
}

static bool window_manager_animation_can_proxy(int window_count)
{
    return window_count > 0 && window_count <= SA_SWAP_PROXY_ACTIVE_RECORD_LIMIT;
}

void window_manager_animate_window_list_async(struct window_capture *window_list, int window_count)
{
    if (!window_list || window_count <= 0) return;
    if (!window_manager_animation_can_proxy(window_count)) goto set_final_frames;

    struct window_animation_context *context = window_manager_animation_create_context(window_count);
    if (!context) goto set_final_frames;
    context->animation_duration = g_window_manager.window_animation_duration;
    context->animation_easing = g_window_manager.window_animation_easing;

    struct window_animation **build_jobs = window_manager_animation_allocate(
        (size_t) window_count * sizeof(*build_jobs));
    if (!build_jobs) goto cleanup;

    int build_job_count = 0;
    bool update_disabled = true;

    (void) window_manager_animation_disable_update(context->animation_connection);
    pthread_mutex_lock(&g_window_manager.window_animations_lock);
    for (int i = 0; i < window_count; ++i) {
        context->animation_list[i].window = window_list[i].window;
        context->animation_list[i].wid = window_list[i].window->id;
        context->animation_list[i].x = window_list[i].x;
        context->animation_list[i].y = window_list[i].y;
        context->animation_list[i].w = window_list[i].w;
        context->animation_list[i].h = window_list[i].h;
        context->animation_list[i].cid = context->animation_connection;

        struct window_animation *animation = &context->animation_list[i];
        struct window_animation *existing_animation = table_find(&g_window_manager.window_animations_table,
                                                                  &animation->wid);
        if (existing_animation) {
            window_manager_prepare_replacement_proxy(&animation->proxy, &existing_animation->proxy);

            float alpha = 1.0f;
            SLSGetWindowAlpha(context->animation_connection, animation->wid, &alpha);
            if (!window_manager_create_window_proxy(context->animation_connection, alpha, &animation->proxy)) {
                __atomic_store_n(&animation->skip, true, __ATOMIC_RELEASE);
            } else if (!table_replace(&g_window_manager.window_animations_table, &animation->wid, animation)) {
                window_manager_destroy_window_proxy(context->animation_connection, &animation->proxy);
                __atomic_store_n(&animation->skip, true, __ATOMIC_RELEASE);
            } else {
                __atomic_store_n(&existing_animation->skip, true, __ATOMIC_RELEASE);

                CFTypeRef transaction = SLSTransactionCreate(context->animation_connection);
                if (transaction) {
                    SLSTransactionOrderWindowGroup(transaction, animation->proxy.id, 1, animation->wid);
                    SLSTransactionSetWindowSystemAlpha(transaction, existing_animation->proxy.id, 0);
                    SLSTransactionCommit(transaction, 0);
                    CFRelease(transaction);
                }
                window_manager_destroy_window_proxy(existing_animation->cid, &existing_animation->proxy);
            }
        } else if (window_manager_animation_publish_new_locked(animation)) {
            build_jobs[build_job_count++] = animation;
        }
    }
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);

    if (build_job_count > 0) {
        dispatch_apply((size_t) build_job_count,
                       dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
                       ^(size_t index) {
            window_manager_build_window_proxy_thread_proc(build_jobs[index]);
        });
    }
    free(build_jobs);

    pthread_mutex_lock(&g_window_manager.window_animations_lock);
    window_manager_animation_prune_failed_locked(context);
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);

    window_manager_animation_apply_final_frames(context);

    context->proxies_may_be_installed = true;
    if (!scripting_addition_swap_window_proxy_in(context->animation_list, context->animation_count)) goto cleanup_ready;
    pthread_mutex_lock(&g_window_manager.window_animations_lock);
    for (int i = 0; i < context->animation_count; ++i) {
        struct window_animation *animation = &context->animation_list[i];
        if (table_find(&g_window_manager.window_animations_table, &animation->wid) != animation) {
            __atomic_store_n(&animation->skip, true, __ATOMIC_RELEASE);
        }
    }
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);

    (void) window_manager_animation_reenable_update(context->animation_connection);
    update_disabled = false;
    if (!window_manager_animation_start_clock(context)) goto cleanup_ready;
    return;

cleanup_ready:
    window_manager_animation_setup_cleanup(context, update_disabled);
    return;
cleanup:
    window_manager_animation_setup_cleanup(context, false);
    goto set_final_frames;
set_final_frames:
#ifdef TESTS
    if (window_manager_animation_set_frame != window_manager_set_window_frame) {
        for (int i = 0; i < window_count; ++i) {
            window_manager_animation_set_frame(window_list[i].window, window_list[i].x, window_list[i].y,
                                               window_list[i].w, window_list[i].h);
        }
    } else
#endif
    window_manager_set_window_frames(window_list, window_count);
    window_manager_refresh_active_window_highlight_for_captures(window_list, window_count);
}
#ifdef TESTS
static void (*window_manager_test_animate_observer)(struct window_capture *, int);
static struct accessibility_display_preferences (*window_manager_animation_preferences)(void) =
    accessibility_preferences_snapshot;
static void (*window_manager_animation_async_route)(struct window_capture *, int) =
    window_manager_animate_window_list_async;
static void (*window_manager_animation_direct_route)(struct window *, float, float, float, float) =
    window_manager_set_window_frame;
#else
#define window_manager_animation_preferences accessibility_preferences_snapshot
#define window_manager_animation_async_route window_manager_animate_window_list_async
#define window_manager_animation_direct_route window_manager_set_window_frame
#endif

static bool window_manager_should_animate(float configured_duration)
{
    struct accessibility_display_preferences preferences = window_manager_animation_preferences();
    return configured_duration > 0.0f && !preferences.reduce_motion;
}

static struct window_capture *window_manager_find_window_capture(struct window_capture *window_list,
                                                                  int window_count,
                                                                  uint32_t window_id)
{
    if (!window_id) return NULL;
    for (int i = 0; i < window_count; ++i) {
        if (window_list[i].window && window_list[i].window->id == window_id) return &window_list[i];
    }
    return NULL;
}

static void window_manager_refresh_active_window_highlight_for_captures(struct window_capture *window_list,
                                                                        int window_count)
{
    struct window_capture *capture = window_manager_find_window_capture(window_list, window_count,
                                                                         g_window_manager.focused_window_id);
    if (capture) {
        CGRect frame = CGRectMake(capture->x, capture->y, capture->w, capture->h);
        window_manager_refresh_active_window_highlight_for_frame(&g_window_manager, capture->window->id, frame);
    } else {
        window_manager_refresh_active_window_highlight(&g_window_manager);
    }
}

void window_manager_animate_window_list(struct window_capture *window_list, int window_count)
{
    TIME_FUNCTION;
#ifdef TESTS
    if (window_manager_test_animate_observer) {
        window_manager_test_animate_observer(window_list, window_count);
        return;
    }
#endif

    if (window_manager_should_animate(g_window_manager.window_animation_duration)) {
        if (window_manager_find_window_capture(window_list, window_count,
                                               g_window_manager.focused_window_id)) {
            window_manager_hide_active_window_highlight(&g_window_manager);
        }
        window_manager_animation_async_route(window_list, window_count);
    } else {
#ifdef TESTS
        if (window_manager_animation_direct_route != window_manager_set_window_frame) {
            for (int i = 0; i < window_count; ++i) {
                window_manager_animation_direct_route(window_list[i].window, window_list[i].x,
                                                       window_list[i].y, window_list[i].w,
                                                       window_list[i].h);
            }
        } else
#endif
        window_manager_set_window_frames(window_list, window_count);
        window_manager_refresh_active_window_highlight_for_captures(window_list, window_count);
    }
}

void window_manager_animate_window(struct window_capture capture)
{
    TIME_FUNCTION;
#ifdef TESTS
    if (window_manager_test_animate_observer) {
        window_manager_test_animate_observer(&capture, 1);
        return;
    }
#endif

    if (window_manager_should_animate(g_window_manager.window_animation_duration)) {
        if (capture.window && capture.window->id == g_window_manager.focused_window_id) {
            window_manager_hide_active_window_highlight(&g_window_manager);
        }
        window_manager_animation_async_route(&capture, 1);
    } else {
        window_manager_animation_direct_route(capture.window, capture.x, capture.y,
                                              capture.w, capture.h);
        window_manager_refresh_active_window_highlight_for_captures(&capture, 1);
    }
}
