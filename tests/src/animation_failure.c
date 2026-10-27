static CFTypeRef animation_test_null_transaction(int cid)
{
    (void) cid;
    return NULL;
}

static int animation_test_terminal_schedule_count;
static void animation_test_terminal_schedule(struct window_animation_context *context)
{
    (void) context;
    ++animation_test_terminal_schedule_count;
}

TEST_FUNC(animation_null_frame_transaction_still_claims_terminal_completion,
{
    __auto_type saved_create = window_manager_animation_transaction_create;
    __auto_type saved_scheduler = window_manager_animation_teardown_scheduler;
    window_manager_animation_transaction_create = animation_test_null_transaction;
    window_manager_animation_teardown_scheduler = animation_test_terminal_schedule;
    animation_test_terminal_schedule_count = 0;

    struct window_animation animation = {
        .wid = 1,
        .x = 100,
        .y = 200,
        .w = 300,
        .h = 400,
        .proxy = { .tx = 11, .ty = 22, .tw = 33, .th = 44, .frame = CGRectMake(1, 2, 3, 4) }
    };
    struct window_animation_context context = {
        .animation_duration = 1.0f,
        .animation_easing = 0,
        .animation_list = &animation,
        .animation_count = 1
    };
    struct animation_clock_tick nonterminal = { .elapsed_ticks = 0.0, .ticks_per_second = 1.0 };
    struct animation_clock_tick terminal = { .elapsed_ticks = 1.0, .ticks_per_second = 1.0 };
    window_manager_animation_tick(&context, &nonterminal);
    TEST_CHECK(context.completion_claimed, false);
    TEST_CHECK(animation_test_terminal_schedule_count, 0);
    TEST_CHECK(animation.proxy.tx, 11.0f);
    TEST_CHECK(animation.proxy.ty, 22.0f);
    TEST_CHECK(animation.proxy.tw, 33.0f);
    TEST_CHECK(animation.proxy.th, 44.0f);

    window_manager_animation_tick(&context, &terminal);
    TEST_CHECK(context.completion_claimed, true);
    TEST_CHECK(animation_test_terminal_schedule_count, 1);
    TEST_CHECK(animation.proxy.tx, 11.0f);
    TEST_CHECK(animation.proxy.ty, 22.0f);
    TEST_CHECK(animation.proxy.tw, 33.0f);
    TEST_CHECK(animation.proxy.th, 44.0f);

    window_manager_animation_tick(&context, &terminal);
    TEST_CHECK(animation_test_terminal_schedule_count, 1);

    window_manager_animation_transaction_create = saved_create;
    window_manager_animation_teardown_scheduler = saved_scheduler;
});

static bool animation_test_send_result;
static bool animation_test_swap_sender(struct sa_payload_builder *builder)
{
    (void) builder;
    return animation_test_send_result;
}

static CGError animation_test_region_fail(CGRect *rect, CFTypeRef *region)
{
    (void) rect; (void) region;
    return kCGErrorFailure;
}
static CGError animation_test_region_ok(CGRect *rect, CFTypeRef *region)
{
    (void) rect;
    *region = CFRetain(kCFBooleanTrue);
    return kCGErrorSuccess;
}
static CFTypeRef animation_test_empty_region_fail(void) { return NULL; }
static CFTypeRef animation_test_empty_region_ok(void) { return CFRetain(kCFBooleanTrue); }
static CGError animation_test_window_fail(int cid, int type, CFTypeRef region, CFTypeRef shape, int options,
                                          uint64_t *tags, float x, float y, int tag_size, uint32_t *wid, void *context)
{
    (void) cid; (void) type; (void) region; (void) shape; (void) options; (void) tags;
    (void) x; (void) y; (void) tag_size; (void) wid; (void) context;
    return kCGErrorFailure;
}
static CGError animation_test_window_ok(int cid, int type, CFTypeRef region, CFTypeRef shape, int options,
                                        uint64_t *tags, float x, float y, int tag_size, uint32_t *wid, void *context)
{
    (void) cid; (void) type; (void) region; (void) shape; (void) options; (void) tags;
    (void) x; (void) y; (void) tag_size; (void) context;
    *wid = 99;
    return kCGErrorSuccess;
}
static CGContextRef animation_test_context_fail(int cid, uint32_t wid, CFDictionaryRef options)
{
    (void) cid; (void) wid; (void) options;
    return NULL;
}
static int animation_test_release_window_count;
static CGError animation_test_release_window(int cid, uint32_t wid)
{
    (void) cid; (void) wid;
    ++animation_test_release_window_count;
    return kCGErrorSuccess;
}
static CGContextRef animation_test_context_ok(int cid, uint32_t wid, CFDictionaryRef options)
{
    (void) cid; (void) wid; (void) options;
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    if (!color_space) return NULL;
    CGContextRef context = CGBitmapContextCreate(NULL, 2, 2, 8, 8, color_space,
                                                  kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(color_space);
    return context;
}
static bool animation_test_disable_shadow(uint32_t wid) { (void) wid; return true; }
static CGError animation_test_set_opacity(int cid, uint32_t wid, bool opaque)
{ (void) cid; (void) wid; (void) opaque; return kCGErrorSuccess; }
static CGError animation_test_set_resolution(int cid, uint32_t wid, double resolution)
{ (void) cid; (void) wid; (void) resolution; return kCGErrorSuccess; }
static CGError animation_test_set_alpha(int cid, uint32_t wid, float alpha)
{ (void) cid; (void) wid; (void) alpha; return kCGErrorSuccess; }
static CGError animation_test_set_level(int cid, uint32_t wid, int level)
{ (void) cid; (void) wid; (void) level; return kCGErrorSuccess; }

static int animation_test_corner_render_count;
static int animation_test_corner_render_connection;
static uint32_t animation_test_corner_render_window_id;
static double animation_test_corner_render_radius[WINDOW_CORNER_GEOMETRY_RADIUS_COUNT];
static bool animation_test_corner_render_result;
static bool animation_test_corner_renderer(
    int connection, uint32_t proxy_window_id,
    const double radius[WINDOW_CORNER_GEOMETRY_RADIUS_COUNT])
{
    ++animation_test_corner_render_count;
    animation_test_corner_render_connection = connection;
    animation_test_corner_render_window_id = proxy_window_id;
    memcpy(animation_test_corner_render_radius, radius,
           sizeof(animation_test_corner_render_radius));
    return animation_test_corner_render_result;
}

TEST_FUNC(animation_proxy_creation_faults_are_non_active_and_unwound,
{
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(NULL, 1, 1, 8, 4, color_space,
                                                 kCGImageAlphaPremultipliedLast);
    CGImageRef image = bitmap ? CGBitmapContextCreateImage(bitmap) : NULL;
    TEST_CHECK(image != NULL, true);

    __auto_type saved_region = window_manager_proxy_new_region;
    __auto_type saved_empty = window_manager_proxy_empty_region;
    __auto_type saved_window = window_manager_proxy_new_window;
    __auto_type saved_context = window_manager_proxy_new_context;
    __auto_type saved_release = window_manager_proxy_release_window;
    __auto_type saved_shadow = window_manager_proxy_disable_shadow;
    __auto_type saved_opacity = window_manager_proxy_set_opacity;
    __auto_type saved_resolution = window_manager_proxy_set_resolution;
    __auto_type saved_alpha = window_manager_proxy_set_alpha;
    __auto_type saved_level = window_manager_proxy_set_level;
    __auto_type saved_sub_level = window_manager_proxy_set_sub_level;
    __auto_type saved_corner_renderer = window_corner_geometry_proxy_renderer;
    window_manager_proxy_release_window = animation_test_release_window;

    if (image) {
        struct window_proxy proxy = { .image = (CGImageRef) CFRetain(image), .frame = CGRectMake(0, 0, 1, 1) };
        window_manager_proxy_new_region = animation_test_region_fail;
        TEST_CHECK(window_manager_create_window_proxy(1, 1.0f, &proxy), false);
        TEST_CHECK(proxy.image == NULL, true);

        proxy = (struct window_proxy) { .image = (CGImageRef) CFRetain(image), .frame = CGRectMake(0, 0, 1, 1) };
        window_manager_proxy_new_region = animation_test_region_ok;
        window_manager_proxy_empty_region = animation_test_empty_region_fail;
        TEST_CHECK(window_manager_create_window_proxy(1, 1.0f, &proxy), false);
        TEST_CHECK(proxy.image == NULL, true);

        proxy = (struct window_proxy) { .image = (CGImageRef) CFRetain(image), .frame = CGRectMake(0, 0, 1, 1) };
        window_manager_proxy_empty_region = animation_test_empty_region_ok;
        window_manager_proxy_new_window = animation_test_window_fail;
        TEST_CHECK(window_manager_create_window_proxy(1, 1.0f, &proxy), false);
        TEST_CHECK(proxy.id, 0U);

        proxy = (struct window_proxy) { .image = (CGImageRef) CFRetain(image), .frame = CGRectMake(0, 0, 1, 1) };
        window_manager_proxy_new_window = animation_test_window_ok;
        window_manager_proxy_new_context = animation_test_context_fail;
        animation_test_release_window_count = 0;
        TEST_CHECK(window_manager_create_window_proxy(1, 1.0f, &proxy), false);
        TEST_CHECK(proxy.id, 0U);
        TEST_CHECK(animation_test_release_window_count, 1);

        window_manager_proxy_new_context = animation_test_context_ok;
        window_manager_proxy_disable_shadow = animation_test_disable_shadow;
        window_manager_proxy_set_opacity = animation_test_set_opacity;
        window_manager_proxy_set_resolution = animation_test_set_resolution;
        window_manager_proxy_set_alpha = animation_test_set_alpha;
        window_manager_proxy_set_level = animation_test_set_level;
        window_manager_proxy_set_sub_level = animation_test_set_level;
        window_corner_geometry_proxy_renderer = animation_test_corner_renderer;
        animation_test_corner_render_count = 0;
        animation_test_corner_render_result = false;
        proxy = (struct window_proxy) {
            .image = (CGImageRef) CFRetain(image),
            .frame = CGRectMake(0, 0, 100, 40),
            .corner_geometry = {
                .radius = { 100, 10, 30, 5 },
                .valid_mask = WINDOW_CORNER_GEOMETRY_VALID_ALL,
                .count = WINDOW_CORNER_GEOMETRY_RADIUS_COUNT,
                .source = WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED
            }
        };
        TEST_CHECK(window_manager_create_window_proxy(7, 1.0f, &proxy), true);
        TEST_CHECK(animation_test_corner_render_count, 1);
        TEST_CHECK(animation_test_corner_render_connection, 7);
        TEST_CHECK(animation_test_corner_render_window_id, 99U);
        TEST_CHECK(animation_test_corner_render_radius[0], 20.0);
        TEST_CHECK(animation_test_corner_render_radius[1], 10.0);
        TEST_CHECK(animation_test_corner_render_radius[2], 20.0);
        TEST_CHECK(animation_test_corner_render_radius[3], 5.0);
        TEST_CHECK(proxy.id, 99U);
        window_manager_destroy_window_proxy(7, &proxy);

        animation_test_corner_render_count = 0;
        proxy = (struct window_proxy) {
            .image = (CGImageRef) CFRetain(image), .frame = CGRectMake(0, 0, 100, 40),
            .corner_geometry = { .radius = { 1, 2, 3, 4 } }
        };
        TEST_CHECK(window_manager_create_window_proxy(7, 1.0f, &proxy), true);
        TEST_CHECK(animation_test_corner_render_count, 0);
        window_manager_destroy_window_proxy(7, &proxy);

        window_corner_geometry_proxy_renderer = NULL;
        proxy = (struct window_proxy) {
            .image = (CGImageRef) CFRetain(image),
            .frame = CGRectMake(0, 0, 100, 40),
            .corner_geometry = {
                .radius = { 1, 2, 3, 4 },
                .valid_mask = WINDOW_CORNER_GEOMETRY_VALID_ALL,
                .count = WINDOW_CORNER_GEOMETRY_RADIUS_COUNT,
                .source = WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED
            }
        };
        TEST_CHECK(window_manager_create_window_proxy(7, 1.0f, &proxy), true);
        TEST_CHECK(animation_test_corner_render_count, 0);
        window_manager_destroy_window_proxy(7, &proxy);

        NSArray *empty = @[];
        NSArray *wrong = @[ @1 ];
        TEST_CHECK(window_manager_proxy_image_from_capture((CFArrayRef) empty, 1.0f) == NULL, true);
        TEST_CHECK(window_manager_proxy_image_from_capture((CFArrayRef) wrong, 1.0f) == NULL, true);
    }

    window_manager_proxy_new_region = saved_region;
    window_manager_proxy_empty_region = saved_empty;
    window_manager_proxy_new_window = saved_window;
    window_manager_proxy_new_context = saved_context;
    window_manager_proxy_release_window = saved_release;
    window_manager_proxy_disable_shadow = saved_shadow;
    window_manager_proxy_set_opacity = saved_opacity;
    window_manager_proxy_set_resolution = saved_resolution;
    window_manager_proxy_set_alpha = saved_alpha;
    window_manager_proxy_set_level = saved_level;
    window_manager_proxy_set_sub_level = saved_sub_level;
    window_corner_geometry_proxy_renderer = saved_corner_renderer;
    if (image) CGImageRelease(image);
    if (bitmap) CGContextRelease(bitmap);
    if (color_space) CGColorSpaceRelease(color_space);
});

TEST_FUNC(animation_replacement_proxy_preserves_corner_geometry,
{
    struct window_proxy existing = {
        .tx = 10, .ty = 20, .tw = 300, .th = 200,
        .level = 4, .sub_level = 2,
        .corner_geometry = {
            .radius = { 1, 2, 3, 4 },
            .valid_mask = WINDOW_CORNER_GEOMETRY_VALID_ALL,
            .count = WINDOW_CORNER_GEOMETRY_RADIUS_COUNT,
            .source = WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED
        }
    };
    struct window_proxy replacement = {0};
    window_manager_prepare_replacement_proxy(&replacement, &existing);
    TEST_CHECK(CGRectEqualToRect(replacement.frame, CGRectMake(10, 20, 300, 200)), true);
    TEST_CHECK(replacement.tx, 10.0f);
    TEST_CHECK(replacement.ty, 20.0f);
    TEST_CHECK(replacement.tw, 300.0f);
    TEST_CHECK(replacement.th, 200.0f);
    TEST_CHECK(replacement.level, 4);
    TEST_CHECK(replacement.sub_level, 2);
    TEST_CHECK(window_corner_geometry_is_complete(&replacement.corner_geometry), true);
    for (size_t i = 0; i < WINDOW_CORNER_GEOMETRY_RADIUS_COUNT; ++i) {
        TEST_CHECK(replacement.corner_geometry.radius[i], existing.corner_geometry.radius[i]);
    }
})

TEST_FUNC(animation_alpha_restoration_handles_partial_simd_tail,
{
    const size_t widths[] = { 1, 3, 5 };
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        size_t width = widths[i];
        CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
        CGContextRef bitmap = color_space
                            ? CGBitmapContextCreate(NULL, width, 1, 8, width * 4, color_space,
                                                   kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast)
                            : NULL;
        if (!bitmap) {
            result = false;
            if (color_space) CGColorSpaceRelease(color_space);
            continue;
        }

        CGContextSetRGBFillColor(bitmap, 0.25, 0.5, 0.75, 0.5);
        CGContextFillRect(bitmap, CGRectMake(0, 0, width, 1));
        if (width == 5) {
            UInt8 *pixels = CGBitmapContextGetData(bitmap);
            for (size_t pixel = 0; pixel < width; ++pixel) {
                pixels[pixel * 4 + 0] = 3;
                pixels[pixel * 4 + 1] = 0;
                pixels[pixel * 4 + 2] = 0;
                pixels[pixel * 4 + 3] = 10;
            }
        }
        CGImageRef image = CGBitmapContextCreateImage(bitmap);
        CGImageRef restored = image ? cgimage_restore_alpha(image) : NULL;
        TEST_CHECK(restored != NULL, true);

        CFDataRef restored_data = restored
                                ? CGDataProviderCopyData(CGImageGetDataProvider(restored))
                                : NULL;
        TEST_CHECK(restored_data != NULL, true);
        if (restored_data && CFDataGetLength(restored_data) >= (CFIndex) (width * 4)) {
            const UInt8 *bytes = CFDataGetBytePtr(restored_data);
            TEST_CHECK(bytes[3], 255);
            if (width == 5) TEST_CHECK(bytes[0], 77);
            for (size_t pixel = 1; pixel < width; ++pixel) {
                TEST_CHECK(memcmp(bytes, bytes + pixel * 4, 4), 0);
                TEST_CHECK(bytes[pixel * 4 + 3], 255);
                if (width == 5) TEST_CHECK(bytes[pixel * 4], 77);
            }
        }
        if (restored_data) CFRelease(restored_data);
        if (restored) CGImageRelease(restored);
        if (image) CGImageRelease(image);
        CGContextRelease(bitmap);
        CGColorSpaceRelease(color_space);
    }
});

static int animation_test_stop_count;
static int animation_test_link_release_count;
static CVReturn animation_test_stop_result;
static bool animation_test_clock_quiesced;
static enum animation_clock_stop_result animation_test_clock_stop(struct animation_clock *clock);
static void animation_test_clock_destroy(struct animation_clock *clock);
static CGError animation_test_update(int cid);

TEST_FUNC(animation_display_link_setup_stages_propagate_failures,
{
    __auto_type saved_disable = window_manager_animation_disable_update;
    __auto_type saved_reenable = window_manager_animation_reenable_update;
    window_manager_animation_disable_update = animation_test_update;
    window_manager_animation_reenable_update = animation_test_update;

    for (int stage = 1; stage <= 3; ++stage) {
        animation_clock_test_begin();
        animation_clock_test_stage = stage;
        animation_clock_test_stop_result = stage == 3 ? kCVReturnDisplayLinkNotRunning
                                                       : kCVReturnSuccess;
        struct window_animation_context *context = calloc(1, sizeof(*context));
        TEST_CHECK(window_manager_animation_start_clock(context), false);
        TEST_CHECK(animation_clock_test_create_calls, 1);
        TEST_CHECK(animation_clock_test_callback_calls, stage >= 2 ? 1 : 0);
        TEST_CHECK(animation_clock_test_start_calls, stage >= 3 ? 1 : 0);
        window_manager_animation_teardown_attempt(context);
        TEST_CHECK(animation_clock_test_stop_calls, stage >= 3 ? 1 : 0);
        TEST_CHECK(animation_clock_test_release_calls, stage >= 2 ? 1 : 0);
        TEST_CHECK(animation_clock_test_deallocations, 1);
        animation_clock_test_end();
    }

    animation_clock_test_begin();
    struct window_animation_context context = {0};
    TEST_CHECK(window_manager_animation_start_clock(&context), true);
    TEST_CHECK(context.clock != NULL, true);
    TEST_CHECK(animation_clock_stop(context.clock), ANIMATION_CLOCK_STOP_QUIESCED);
    animation_clock_destroy(context.clock);
    context.clock = NULL;
    animation_clock_test_end();

    window_manager_animation_disable_update = saved_disable;
    window_manager_animation_reenable_update = saved_reenable;
});

static int animation_test_connection_release_count;
static void animation_test_clock_destroy(struct animation_clock *clock) { (void) clock; ++animation_test_link_release_count; }
static CGError animation_test_connection_release(int cid) { (void) cid; ++animation_test_connection_release_count; return kCGErrorSuccess; }
static int animation_test_allocation_call;
static int animation_test_allocation_fail_call;
static void *animation_test_allocate(size_t size)
{
    ++animation_test_allocation_call;
    return animation_test_allocation_call == animation_test_allocation_fail_call ? NULL : malloc(size);
}
static bool animation_test_connection_fail;
static CGError animation_test_new_connection(int unused, int *cid)
{
    (void) unused;
    if (animation_test_connection_fail) return kCGErrorFailure;
    *cid = 7;
    return kCGErrorSuccess;
}
static CGError animation_test_update(int cid) { (void) cid; return kCGErrorSuccess; }

static int animation_test_set_frame_count;
static CGRect animation_test_last_frame;
static void animation_test_set_frame(struct window *window, float x, float y, float w, float h)
{
    (void) window;
    ++animation_test_set_frame_count;
    animation_test_last_frame = CGRectMake(x, y, w, h);
}

TEST_FUNC(animation_final_frames_and_failed_owner_pruning,
{
    __auto_type saved_set_frame = window_manager_animation_set_frame;
    window_manager_animation_set_frame = animation_test_set_frame;
    struct window_animation animation = { .wid = 7, .x = 1, .y = 2, .w = 3, .h = 4, .skip = true };
    struct window_animation_context context = { .animation_list = &animation, .animation_count = 1 };
    animation_test_set_frame_count = 0;
    window_manager_animation_apply_final_frames(&context);
    TEST_CHECK(animation_test_set_frame_count, 1);
    TEST_CHECK(CGRectEqualToRect(animation_test_last_frame, CGRectMake(1, 2, 3, 4)), true);
    window_manager_animation_set_frame = saved_set_frame;

    struct table saved_table = g_window_manager.window_animations_table;
    g_window_manager.window_animations_table = (struct table) { 0 };
    table_init(&g_window_manager.window_animations_table, 4, hash_wm, compare_wm);
    table_add(&g_window_manager.window_animations_table, &animation.wid, &animation);
    window_manager_animation_prune_failed_locked(&context);
    TEST_CHECK(table_find(&g_window_manager.window_animations_table, &animation.wid) == NULL, true);
    table_free(&g_window_manager.window_animations_table);
    g_window_manager.window_animations_table = saved_table;
});

TEST_FUNC(animation_context_setup_allocation_and_connection_failures_cleanup,
{
    __auto_type saved_allocate = window_manager_animation_allocate;
    __auto_type saved_connection = window_manager_animation_new_connection;
    __auto_type saved_release = window_manager_animation_release_connection;
    window_manager_animation_allocate = animation_test_allocate;
    window_manager_animation_new_connection = animation_test_new_connection;
    window_manager_animation_release_connection = animation_test_connection_release;

    animation_test_allocation_call = 0;
    animation_test_allocation_fail_call = 1;
    TEST_CHECK(window_manager_animation_create_context(1) == NULL, true);

    animation_test_allocation_call = 0;
    animation_test_allocation_fail_call = 0;
    animation_test_connection_fail = true;
    TEST_CHECK(window_manager_animation_create_context(1) == NULL, true);

    animation_test_allocation_call = 0;
    animation_test_allocation_fail_call = 2;
    animation_test_connection_fail = false;
    animation_test_connection_release_count = 0;
    TEST_CHECK(window_manager_animation_create_context(1) == NULL, true);
    TEST_CHECK(animation_test_connection_release_count, 1);

    animation_test_allocation_call = 0;
    animation_test_allocation_fail_call = 0;
    struct window_animation_context *context = window_manager_animation_create_context(1);
    TEST_CHECK(context != NULL, true);
    if (context) {
        (void) window_manager_animation_release_connection(context->animation_connection);
        free(context->animation_list);
        free(context);
    }

    window_manager_animation_allocate = saved_allocate;
    window_manager_animation_new_connection = saved_connection;
    window_manager_animation_release_connection = saved_release;
});

static struct window_animation_context *animation_test_scheduled_context;
static uint64_t animation_test_scheduled_delay;
static void animation_test_retry_schedule(struct window_animation_context *context, uint64_t delay)
{
    animation_test_scheduled_context = context;
    animation_test_scheduled_delay = delay;
}
static enum animation_clock_stop_result animation_test_clock_stop(struct animation_clock *clock)
{
    (void) clock;
    if (animation_test_clock_quiesced) return ANIMATION_CLOCK_STOP_QUIESCED;
    ++animation_test_stop_count;
    if (animation_test_stop_result == kCVReturnSuccess ||
        animation_test_stop_result == kCVReturnDisplayLinkNotRunning) {
        animation_test_clock_quiesced = true;
        return ANIMATION_CLOCK_STOP_QUIESCED;
    }
    return ANIMATION_CLOCK_STOP_RETRY;
}

TEST_FUNC(animation_teardown_has_single_non_callback_owner,
{
    struct window_animation_context claim = { 0 };
    TEST_CHECK(window_manager_animation_claim_completion(&claim), true);
    TEST_CHECK(window_manager_animation_claim_completion(&claim), false);
    TEST_CHECK(window_manager_retry_delay_ms(0), 100ULL);
    TEST_CHECK(window_manager_retry_delay_ms(6), 6400ULL);
    TEST_CHECK(window_manager_retry_delay_ms(UINT8_MAX), 6400ULL);

    __auto_type saved_stop = window_manager_animation_clock_stop;
    __auto_type saved_link_release = window_manager_animation_clock_destroy;
    __auto_type saved_connection_release = window_manager_animation_release_connection;
    __auto_type saved_disable = window_manager_animation_disable_update;
    __auto_type saved_reenable = window_manager_animation_reenable_update;
    __auto_type saved_scheduler = window_manager_animation_retry_scheduler;
    window_manager_animation_clock_stop = animation_test_clock_stop;
    window_manager_animation_clock_destroy = animation_test_clock_destroy;
    window_manager_animation_release_connection = animation_test_connection_release;
    window_manager_animation_disable_update = animation_test_update;
    window_manager_animation_reenable_update = animation_test_update;
    window_manager_animation_retry_scheduler = animation_test_retry_schedule;
    struct window_animation_context saturated = { .teardown_attempts = UINT8_MAX };
    window_manager_animation_retry(&saturated);
    TEST_CHECK(saturated.teardown_attempts, UINT8_MAX);
    TEST_CHECK(animation_test_scheduled_delay, 6400ULL);
    animation_test_stop_count = 0;
    animation_test_clock_quiesced = false;
    animation_test_link_release_count = 0;
    animation_test_connection_release_count = 0;
    animation_test_scheduled_context = NULL;

    struct window_animation_context *context = calloc(1, sizeof(*context));
    context->clock = (struct animation_clock *) (intptr_t) 1;
    context->animation_connection = 7;
    animation_test_stop_result = kCVReturnError;
    window_manager_animation_teardown_attempt(context);
    TEST_CHECK(animation_test_stop_count, 1);
    TEST_CHECK(animation_test_link_release_count, 0);
    TEST_CHECK(animation_test_connection_release_count, 0);
    TEST_CHECK(animation_test_scheduled_context == context, true);
    TEST_CHECK(animation_test_scheduled_delay, 100ULL);

    animation_test_stop_result = kCVReturnSuccess;
    window_manager_animation_teardown_attempt(context);
    TEST_CHECK(animation_test_stop_count, 2);
    TEST_CHECK(animation_test_link_release_count, 1);
    TEST_CHECK(animation_test_connection_release_count, 1);

    window_manager_animation_clock_stop = saved_stop;
    window_manager_animation_clock_destroy = saved_link_release;
    window_manager_animation_release_connection = saved_connection_release;
    window_manager_animation_disable_update = saved_disable;
    window_manager_animation_reenable_update = saved_reenable;
    window_manager_animation_retry_scheduler = saved_scheduler;
});

static int animation_test_teardown_order[8];
static int animation_test_teardown_order_count;
static bool animation_test_teardown_stop_retry;

static enum animation_clock_stop_result animation_test_order_stop(struct animation_clock *clock)
{
    (void) clock;
    animation_test_teardown_order[animation_test_teardown_order_count++] = 1;
    return animation_test_teardown_stop_retry ? ANIMATION_CLOCK_STOP_RETRY
                                              : ANIMATION_CLOCK_STOP_QUIESCED;
}

static CGError animation_test_order_disable(int cid)
{
    (void) cid;
    animation_test_teardown_order[animation_test_teardown_order_count++] = 2;
    return kCGErrorSuccess;
}

static CGError animation_test_order_reenable(int cid)
{
    (void) cid;
    animation_test_teardown_order[animation_test_teardown_order_count++] = 3;
    return kCGErrorSuccess;
}

static void animation_test_order_destroy(struct animation_clock *clock)
{
    (void) clock;
    animation_test_teardown_order[animation_test_teardown_order_count++] = 4;
}

static CGError animation_test_order_connection_release(int cid)
{
    (void) cid;
    animation_test_teardown_order[animation_test_teardown_order_count++] = 5;
    return kCGErrorSuccess;
}

static void animation_test_order_retry(struct window_animation_context *context, uint64_t delay)
{
    (void) context;
    (void) delay;
    animation_test_teardown_order[animation_test_teardown_order_count++] = 6;
}

TEST_FUNC(animation_teardown_order_stops_before_updates_and_releases_clock_before_connection,
{
    __auto_type saved_stop = window_manager_animation_clock_stop;
    __auto_type saved_destroy = window_manager_animation_clock_destroy;
    __auto_type saved_disable = window_manager_animation_disable_update;
    __auto_type saved_reenable = window_manager_animation_reenable_update;
    __auto_type saved_release = window_manager_animation_release_connection;
    __auto_type saved_retry = window_manager_animation_retry_scheduler;
    window_manager_animation_clock_stop = animation_test_order_stop;
    window_manager_animation_clock_destroy = animation_test_order_destroy;
    window_manager_animation_disable_update = animation_test_order_disable;
    window_manager_animation_reenable_update = animation_test_order_reenable;
    window_manager_animation_release_connection = animation_test_order_connection_release;
    window_manager_animation_retry_scheduler = animation_test_order_retry;

    animation_test_teardown_order_count = 0;
    animation_test_teardown_stop_retry = false;
    struct window_animation_context *context = calloc(1, sizeof(*context));
    context->clock = (struct animation_clock *)(intptr_t) 1;
    context->animation_connection = 7;
    window_manager_animation_teardown_attempt(context);
    TEST_CHECK(animation_test_teardown_order_count, 5);
    for (int i = 0; i < 5; ++i) TEST_CHECK(animation_test_teardown_order[i], i + 1);

    animation_test_teardown_order_count = 0;
    animation_test_teardown_stop_retry = true;
    context = calloc(1, sizeof(*context));
    context->clock = (struct animation_clock *)(intptr_t) 1;
    context->animation_connection = 7;
    window_manager_animation_teardown_attempt(context);
    TEST_CHECK(animation_test_teardown_order_count, 2);
    TEST_CHECK(animation_test_teardown_order[0], 1);
    TEST_CHECK(animation_test_teardown_order[1], 6);
    free(context);

    window_manager_animation_clock_stop = saved_stop;
    window_manager_animation_clock_destroy = saved_destroy;
    window_manager_animation_disable_update = saved_disable;
    window_manager_animation_reenable_update = saved_reenable;
    window_manager_animation_release_connection = saved_release;
    window_manager_animation_retry_scheduler = saved_retry;
});

TEST_FUNC(animation_out_failure_retains_resources_until_confirmed,
{
    struct table saved_table = g_window_manager.window_animations_table;
    g_window_manager.window_animations_table = (struct table) { 0 };
    table_init(&g_window_manager.window_animations_table, 4, hash_wm, compare_wm);

    struct window_animation_context *context = calloc(1, sizeof(*context));
    context->animation_list = calloc(1, sizeof(*context->animation_list));
    context->animation_count = 1;
    context->animation_connection = 7;
    context->clock = (struct animation_clock *) (intptr_t) 1;
    context->proxies_may_be_installed = true;
    context->animation_list[0].wid = 77;
    context->animation_list[0].proxy.id = 88;
    table_add(&g_window_manager.window_animations_table, &context->animation_list[0].wid, &context->animation_list[0]);

    __auto_type saved_sender = scripting_addition_swap_send_payload;
    __auto_type saved_scheduler = window_manager_animation_retry_scheduler;
    __auto_type saved_release_window = window_manager_proxy_release_window;
    __auto_type saved_connection_release = window_manager_animation_release_connection;
    __auto_type saved_disable = window_manager_animation_disable_update;
    __auto_type saved_reenable = window_manager_animation_reenable_update;
    __auto_type saved_stop = window_manager_animation_clock_stop;
    __auto_type saved_link_release = window_manager_animation_clock_destroy;
    scripting_addition_swap_send_payload = animation_test_swap_sender;
    window_manager_animation_retry_scheduler = animation_test_retry_schedule;
    window_manager_proxy_release_window = animation_test_release_window;
    window_manager_animation_release_connection = animation_test_connection_release;
    window_manager_animation_disable_update = animation_test_update;
    window_manager_animation_reenable_update = animation_test_update;
    window_manager_animation_clock_stop = animation_test_clock_stop;
    window_manager_animation_clock_destroy = animation_test_clock_destroy;

    animation_test_stop_result = kCVReturnSuccess;
    animation_test_stop_count = 0;
    animation_test_clock_quiesced = false;
    animation_test_send_result = false;
    animation_test_release_window_count = 0;
    animation_test_connection_release_count = 0;
    animation_test_scheduled_context = NULL;
    window_manager_animation_teardown_attempt(context);
    TEST_CHECK(animation_test_scheduled_context == context, true);
    TEST_CHECK(context->proxies_may_be_installed, true);
    TEST_CHECK(context->animation_list[0].proxy.id, 88U);
    TEST_CHECK(animation_test_release_window_count, 0);
    TEST_CHECK(animation_test_connection_release_count, 0);
    TEST_CHECK(animation_test_stop_count, 1);

    animation_test_send_result = true;
    window_manager_animation_teardown_attempt(context);
    TEST_CHECK(animation_test_release_window_count, 1);
    TEST_CHECK(animation_test_connection_release_count, 1);
    TEST_CHECK(animation_test_stop_count, 1);

    scripting_addition_swap_send_payload = saved_sender;
    window_manager_animation_retry_scheduler = saved_scheduler;
    window_manager_proxy_release_window = saved_release_window;
    window_manager_animation_release_connection = saved_connection_release;
    window_manager_animation_disable_update = saved_disable;
    window_manager_animation_reenable_update = saved_reenable;
    window_manager_animation_clock_stop = saved_stop;
    window_manager_animation_clock_destroy = saved_link_release;
    table_free(&g_window_manager.window_animations_table);
    g_window_manager.window_animations_table = saved_table;
});

TEST_FUNC(animation_teardown_skips_replaced_ownership,
{
    struct table saved_table = g_window_manager.window_animations_table;
    g_window_manager.window_animations_table = (struct table) { 0 };
    table_init(&g_window_manager.window_animations_table, 4, hash_wm, compare_wm);
    struct window_animation replacement = { .wid = 77, .proxy = { .id = 99 } };
    table_add(&g_window_manager.window_animations_table, &replacement.wid, &replacement);

    struct window_animation_context *context = calloc(1, sizeof(*context));
    context->animation_list = calloc(1, sizeof(*context->animation_list));
    context->animation_count = 1;
    context->animation_connection = 7;
    context->proxies_may_be_installed = true;
    context->animation_list[0].wid = 77;
    context->animation_list[0].proxy.id = 88;

    __auto_type saved_sender = scripting_addition_swap_send_payload;
    __auto_type saved_release_window = window_manager_proxy_release_window;
    __auto_type saved_connection_release = window_manager_animation_release_connection;
    __auto_type saved_disable = window_manager_animation_disable_update;
    __auto_type saved_reenable = window_manager_animation_reenable_update;
    scripting_addition_swap_send_payload = animation_test_swap_sender;
    window_manager_proxy_release_window = animation_test_release_window;
    window_manager_animation_release_connection = animation_test_connection_release;
    window_manager_animation_disable_update = animation_test_update;
    window_manager_animation_reenable_update = animation_test_update;
    animation_test_send_result = true;
    animation_test_release_window_count = 0;
    window_manager_animation_teardown_attempt(context);
    TEST_CHECK(animation_test_release_window_count, 0);
    TEST_CHECK(table_find(&g_window_manager.window_animations_table, &replacement.wid) == &replacement, true);

    scripting_addition_swap_send_payload = saved_sender;
    window_manager_proxy_release_window = saved_release_window;
    window_manager_animation_release_connection = saved_connection_release;
    window_manager_animation_disable_update = saved_disable;
    window_manager_animation_reenable_update = saved_reenable;
    table_free(&g_window_manager.window_animations_table);
    g_window_manager.window_animations_table = saved_table;
});
