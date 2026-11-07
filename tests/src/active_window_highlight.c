static int active_window_highlight_refresh_calls;
static int active_window_highlight_hide_calls;
static uint32_t active_window_highlight_refresh_target_wid;
static CGRect active_window_highlight_refresh_target_frame;
static bool active_window_highlight_refresh_target_frame_valid;

static void active_window_highlight_test_refresh_observer(struct window_manager *wm,
                                                           uint32_t target_wid,
                                                           const CGRect *target_frame)
{
    (void) wm;
    active_window_highlight_refresh_target_wid = target_wid;
    active_window_highlight_refresh_target_frame_valid = target_frame != NULL;
    if (target_frame) active_window_highlight_refresh_target_frame = *target_frame;
    ++active_window_highlight_refresh_calls;
}

static void active_window_highlight_test_hide_observer(struct window_manager *wm)
{
    (void) wm;
    ++active_window_highlight_hide_calls;
}

TEST_FUNC(active_window_highlight_geometry_is_bounded_and_outset,
{
    TEST_CHECK(ACTIVE_WINDOW_HIGHLIGHT_COLOR_DEFAULT, 0xffd75f5fU);
    TEST_CHECK(ACTIVE_WINDOW_HIGHLIGHT_RADIUS_DEFAULT, 9.0f);
    TEST_CHECK(ACTIVE_WINDOW_HIGHLIGHT_WIDTH_DEFAULT, 4.0f);

    struct active_window_highlight_geometry geometry =
        active_window_highlight_geometry_make(CGRectMake(10.0, 20.0, 100.0, 40.0), 4.0f, 9.0f);
    TEST_CHECK(geometry.frame.origin.x, 8.0);
    TEST_CHECK(geometry.frame.origin.y, 18.0);
    TEST_CHECK(geometry.frame.size.width, 104.0);
    TEST_CHECK(geometry.frame.size.height, 44.0);
    TEST_CHECK(geometry.stroke_width, 4.0);
    TEST_CHECK(geometry.stroke_rect.origin.x, 2.0);
    TEST_CHECK(geometry.stroke_rect.origin.y, 2.0);
    TEST_CHECK(geometry.stroke_rect.size.width, 100.0);
    TEST_CHECK(geometry.stroke_rect.size.height, 40.0);
    TEST_CHECK(geometry.corner_radius_x, 11.0);
    TEST_CHECK(geometry.corner_radius_y, 11.0);

    struct active_window_highlight_geometry clamped =
        active_window_highlight_geometry_make(CGRectMake(0.0, 0.0, 8.0, 4.0), 100.0f, 100.0f);
    TEST_CHECK(clamped.stroke_width, 2.0);
    TEST_CHECK(clamped.frame.size.width, 10.0);
    TEST_CHECK(clamped.frame.size.height, 6.0);
    TEST_CHECK(clamped.stroke_rect.size.width, 8.0);
    TEST_CHECK(clamped.stroke_rect.size.height, 4.0);
    TEST_CHECK(clamped.corner_radius_x, 4.0);
    TEST_CHECK(clamped.corner_radius_y, 2.0);

    struct active_window_highlight_geometry empty =
        active_window_highlight_geometry_make(CGRectMake(3.0, 4.0, -8.0, -4.0), 4.0f, 9.0f);
    TEST_CHECK(empty.stroke_width, 0.0);
    TEST_CHECK(empty.frame.size.width, 0.0);
    TEST_CHECK(empty.frame.size.height, 0.0);
    TEST_CHECK(empty.stroke_rect.size.width, 0.0);
    TEST_CHECK(empty.stroke_rect.size.height, 0.0);

    struct active_window_highlight state = {
        .id = 7,
        .context = (CGContextRef) (uintptr_t) 1,
        .target_wid = 42,
        .ordered = true
    };
    TEST_CHECK(active_window_highlight_can_fast_update(&state, 42), true);
    TEST_CHECK(active_window_highlight_can_fast_update(&state, 99), false);
    active_window_highlight_invalidate_order(&state);
    TEST_CHECK(state.ordered, true);
    TEST_CHECK(state.needs_reorder, true);
    TEST_CHECK(active_window_highlight_can_fast_update(&state, 42), false);
});

TEST_FUNC(active_window_highlight_moves_without_redrawing_unchanged_geometry,
{
    struct active_window_highlight highlight = {
        .id = 7,
        .frame = CGRectMake(8.0, 18.0, 104.0, 44.0),
        .color = 0xff9ccfd8U,
        .width = 4.0f,
        .radius = 9.0f
    };
    struct active_window_highlight_geometry same =
        active_window_highlight_geometry_make(CGRectMake(10.0, 20.0, 100.0, 40.0), 4.0f, 9.0f);
    struct active_window_highlight_geometry moved =
        active_window_highlight_geometry_make(CGRectMake(10.0, 120.0, 100.0, 40.0), 4.0f, 9.0f);
    struct active_window_highlight_geometry resized =
        active_window_highlight_geometry_make(CGRectMake(10.0, 120.0, 120.0, 60.0), 4.0f, 9.0f);

    TEST_CHECK(active_window_highlight_needs_redraw(&highlight, &same,
                                                    highlight.color, highlight.width,
                                                    highlight.radius), false);
    TEST_CHECK(CGSizeEqualToSize(highlight.frame.size, moved.frame.size), true);
    TEST_CHECK(active_window_highlight_needs_redraw(&highlight, &moved,
                                                    highlight.color, highlight.width,
                                                    highlight.radius), false);
    TEST_CHECK(active_window_highlight_needs_redraw(&highlight, &resized,
                                                    highlight.color, highlight.width,
                                                    highlight.radius), true);
});

TEST_FUNC(active_window_highlight_pending_geometry_rejects_stale_server_frame,
{
    CGRect old_frame = CGRectMake(10.0, 20.0, 100.0, 50.0);
    CGRect new_frame = CGRectMake(40.0, 60.0, 300.0, 200.0);
    struct active_window_highlight highlight = {
        .pending_target_frame = new_frame,
        .pending_target_wid = 42,
        .pending_target_frame_valid = true
    };

    CGRect selected = window_manager_active_window_highlight_resolve_frame(&highlight, 42, old_frame);
    TEST_CHECK(CGRectEqualToRect(selected, new_frame), true);
    TEST_CHECK(highlight.pending_target_frame_valid, true);

    selected = window_manager_active_window_highlight_resolve_frame(&highlight, 42, new_frame);
    TEST_CHECK(CGRectEqualToRect(selected, new_frame), true);
    TEST_CHECK(highlight.pending_target_frame_valid, false);

    highlight.pending_target_frame = new_frame;
    highlight.pending_target_wid = 42;
    highlight.pending_target_frame_valid = true;
    selected = window_manager_active_window_highlight_resolve_frame(&highlight, 99, old_frame);
    TEST_CHECK(CGRectEqualToRect(selected, old_frame), true);
    TEST_CHECK(highlight.pending_target_frame_valid, false);
});

TEST_FUNC(active_window_highlight_window_events_use_cached_frame,
{
    TEST_SAVE(window_manager_test_active_window_highlight_refresh_observer);
    TEST_SAVE(g_window_manager.focused_window_id);
    TEST_SAVE(g_mouse_state.native_resize);
    window_manager_test_active_window_highlight_refresh_observer = active_window_highlight_test_refresh_observer;
    g_mouse_state.native_resize = (struct native_resize_state) {0};

    struct window window = {
        .id = 42,
        .frame = CGRectMake(40.0, 60.0, 300.0, 200.0)
    };
    g_window_manager.focused_window_id = window.id;
    active_window_highlight_refresh_calls = 0;
    active_window_highlight_refresh_target_wid = 0;
    active_window_highlight_refresh_target_frame = CGRectZero;
    active_window_highlight_refresh_target_frame_valid = false;

    window_event_refresh_active_window_highlight(&window);
    TEST_CHECK(active_window_highlight_refresh_calls, 1);
    TEST_CHECK(active_window_highlight_refresh_target_wid, window.id);
    TEST_CHECK(active_window_highlight_refresh_target_frame_valid, true);
    TEST_CHECK(CGRectEqualToRect(active_window_highlight_refresh_target_frame, window.frame), true);

    g_window_manager.focused_window_id = 99;
    window_event_refresh_active_window_highlight(&window);
    TEST_CHECK(active_window_highlight_refresh_calls, 1);

    TEST_RESTORE(g_mouse_state.native_resize);
    TEST_RESTORE(g_window_manager.focused_window_id);
    TEST_RESTORE(window_manager_test_active_window_highlight_refresh_observer);
});

TEST_FUNC(active_window_highlight_native_move_tracks_cursor_after_ax_confirmation,
{
    struct table saved_windows = g_window_manager.window;
    TEST_SAVE(g_mouse_state);
    TEST_SAVE(g_window_manager.focused_window_id);
    TEST_SAVE(g_mission_control_mode);
    TEST_SAVE(window_manager_test_active_window_highlight_refresh_observer);

    g_window_manager.window = (struct table) {0};
    table_init(&g_window_manager.window, 4, hash_wm, compare_wm);
    struct window window = {
        .id = 42,
        .frame = CGRectMake(100.0, 200.0, 300.0, 150.0)
    };
    table_add(&g_window_manager.window, &window.id, &window);
    g_window_manager.focused_window_id = window.id;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
    g_mouse_state = (struct mouse_state) { .active_button = MOUSE_BUTTON_NONE };
    window_manager_test_active_window_highlight_refresh_observer = active_window_highlight_test_refresh_observer;
    active_window_highlight_refresh_calls = 0;

    native_move_candidate_begin(&g_mouse_state, &window, kCGMouseButtonLeft,
                                CGPointMake(150.0, 220.0));
    TEST_CHECK(native_move_drag(&g_mouse_state, kCGMouseButtonLeft,
                                CGPointMake(160.0, 230.0)), false);
    TEST_CHECK(g_mouse_state.native_move.dragging, true);
    TEST_CHECK(g_mouse_state.native_move.claimed, false);
    TEST_CHECK(active_window_highlight_refresh_calls, 0);

    window.frame.origin = CGPointMake(110.0, 210.0);
    TEST_CHECK(native_move_observe(&g_mouse_state, &window), true);
    TEST_CHECK(g_mouse_state.native_move.claimed, true);
    TEST_CHECK(active_window_highlight_refresh_calls, 1);
    TEST_CHECK(CGRectEqualToRect(active_window_highlight_refresh_target_frame,
                                 CGRectMake(110.0, 210.0, 300.0, 150.0)), true);

    TEST_CHECK(native_move_drag(&g_mouse_state, kCGMouseButtonLeft,
                                CGPointMake(190.0, 260.0)), true);
    TEST_CHECK(active_window_highlight_refresh_calls, 2);
    TEST_CHECK(CGRectEqualToRect(active_window_highlight_refresh_target_frame,
                                 CGRectMake(140.0, 240.0, 300.0, 150.0)), true);
    TEST_CHECK(native_move_drag(&g_mouse_state, kCGMouseButtonRight,
                                CGPointMake(210.0, 280.0)), false);
    TEST_CHECK(active_window_highlight_refresh_calls, 2);

    window.frame.origin = CGPointMake(138.0, 238.0);
    native_move_end(&g_mouse_state, kCGMouseButtonLeft);
    TEST_CHECK(g_mouse_state.native_move.window_id, 0U);
    TEST_CHECK(active_window_highlight_refresh_calls, 3);
    TEST_CHECK(CGRectEqualToRect(active_window_highlight_refresh_target_frame,
                                 window.frame), true);

    native_move_candidate_begin(&g_mouse_state, &window, kCGMouseButtonLeft,
                                CGPointMake(180.0, 250.0));
    TEST_CHECK(native_move_drag(&g_mouse_state, kCGMouseButtonLeft,
                                CGPointMake(190.0, 260.0)), false);
    g_mouse_state.native_resize.window_id = window.id;
    TEST_CHECK(native_move_observe(&g_mouse_state, &window), false);
    TEST_CHECK(g_mouse_state.native_move.claimed, false);
    TEST_CHECK(active_window_highlight_refresh_calls, 3);

    table_free(&g_window_manager.window);
    g_window_manager.window = saved_windows;
    TEST_RESTORE(window_manager_test_active_window_highlight_refresh_observer);
    TEST_RESTORE(g_mission_control_mode);
    TEST_RESTORE(g_window_manager.focused_window_id);
    TEST_RESTORE(g_mouse_state);
});

TEST_FUNC(active_window_highlight_manager_routes_enable_refresh_and_hide,
{
    __auto_type saved_refresh = window_manager_test_active_window_highlight_refresh_observer;
    bool saved_enabled = g_window_manager.enable_active_window_highlight;
    active_window_highlight_refresh_calls = 0;
    active_window_highlight_hide_calls = 0;
    window_manager_test_active_window_highlight_refresh_observer = active_window_highlight_test_refresh_observer;
    TEST_OVERRIDE(window_manager_test_active_window_highlight_hide_observer, active_window_highlight_test_hide_observer);

    window_manager_set_active_window_highlight_enabled(&g_window_manager, true);
    TEST_CHECK(g_window_manager.enable_active_window_highlight, true);
    TEST_CHECK(active_window_highlight_refresh_calls, 1);
    EVENT_HANDLER_ACTIVE_WINDOW_HIGHLIGHT_REFRESH(NULL, 0);
    TEST_CHECK(active_window_highlight_refresh_calls, 2);
    window_manager_hide_active_window_highlight(&g_window_manager);
    TEST_CHECK(active_window_highlight_hide_calls, 1);
    window_manager_set_active_window_highlight_enabled(&g_window_manager, false);
    TEST_CHECK(g_window_manager.enable_active_window_highlight, false);
    TEST_CHECK(active_window_highlight_refresh_calls, 3);

    uint32_t saved_focused_window_id = g_window_manager.focused_window_id;
    window_manager_test_active_window_highlight_refresh_observer = NULL;
    window_manager_refresh_active_window_highlight(&g_window_manager);
    TEST_CHECK(active_window_highlight_hide_calls, 2);
    g_window_manager.enable_active_window_highlight = true;
    g_window_manager.focused_window_id = 0;
    window_manager_refresh_active_window_highlight(&g_window_manager);
    TEST_CHECK(active_window_highlight_hide_calls, 3);

    g_window_manager.focused_window_id = saved_focused_window_id;
    g_window_manager.enable_active_window_highlight = saved_enabled;
    window_manager_test_active_window_highlight_refresh_observer = saved_refresh;
    TEST_RESTORE(window_manager_test_active_window_highlight_hide_observer);
});

static int active_window_highlight_space_visible_calls;
static bool active_window_highlight_space_visible_result;
static bool active_window_highlight_test_space_visible(uint64_t sid)
{
    (void) sid;
    ++active_window_highlight_space_visible_calls;
    return active_window_highlight_space_visible_result;
}

TEST_FUNC(active_window_highlight_fast_path_reuses_space_visibility_until_topology_event,
{
    bool initialized_window = false;
    if (!g_window_manager.window.buckets) {
        table_init(&g_window_manager.window, 8, hash_wm, compare_wm);
        initialized_window = true;
    }
    bool initialized_animations = false;
    if (!g_window_manager.window_animations_table.buckets) {
        table_init(&g_window_manager.window_animations_table, 8, hash_wm, compare_wm);
        initialized_animations = true;
    }
    struct window_manager saved = g_window_manager;
    TEST_SAVE(window_manager_test_highlight_space_is_visible);
    TEST_SAVE(window_manager_test_active_window_highlight_refresh_observer);
    TEST_SAVE(window_manager_test_active_window_highlight_hide_observer);
    window_manager_test_active_window_highlight_refresh_observer = NULL;
    window_manager_test_active_window_highlight_hide_observer = active_window_highlight_test_hide_observer;
    window_manager_test_highlight_space_is_visible = active_window_highlight_test_space_visible;

    struct application application = {0};
    struct window window = { .id = 42, .application = &application, .is_root = true };
    window_set_rule_flag(&window, WINDOW_RULE_MANAGED);
    table_add(&g_window_manager.window, &window.id, &window);

    float width = 4.0f;
    float radius = 9.0f;
    CGRect target = CGRectMake(10.0, 20.0, 300.0, 200.0);
    struct active_window_highlight_geometry geometry = active_window_highlight_geometry_make(target, width, radius);
    g_window_manager.enable_active_window_highlight = true;
    g_window_manager.focused_window_id = window.id;
    g_window_manager.active_window_highlight_color.p = 0xff9ccfd8U;
    g_window_manager.active_window_highlight_width = width;
    g_window_manager.active_window_highlight_radius = radius;
    g_window_manager.active_window_highlight = (struct active_window_highlight) {
        .id = 7, .context = (CGContextRef) (uintptr_t) 1, .frame = geometry.frame, .target_wid = window.id,
        .color = 0xff9ccfd8U, .sid = 5, .width = width, .radius = radius, .ordered = true,
        .space_visible_cached = true
    };

    // Cached visibility: no Space query on repeated frame updates.
    active_window_highlight_space_visible_calls = 0;
    active_window_highlight_hide_calls = 0;
    for (int i = 0; i < 4; ++i) {
        window_manager_refresh_active_window_highlight_for_frame(&g_window_manager, window.id, target);
    }
    TEST_CHECK(active_window_highlight_space_visible_calls, 0);
    TEST_CHECK(active_window_highlight_hide_calls, 0);

    // Frame-only events keep the cache; any other event drops it.
    TEST_CHECK(event_loop_event_preserves_space_topology(WINDOW_MOVED), true);
    TEST_CHECK(event_loop_event_preserves_space_topology(WINDOW_RESIZED), true);
    TEST_CHECK(event_loop_event_preserves_space_topology(SPACE_CHANGED), false);
    TEST_CHECK(event_loop_event_preserves_space_topology(DISPLAY_CHANGED), false);
    TEST_CHECK(event_loop_event_preserves_space_topology(WINDOW_FOCUSED), false);
    TEST_CHECK(event_loop_event_preserves_space_topology(DAEMON_MESSAGE), false);
    TEST_CHECK(event_loop_event_preserves_space_topology(MOUSE_UP), false);
    TEST_CHECK(event_loop_event_preserves_space_topology(DIRECT_SPACE_SWIPE), false);
    window_manager_invalidate_active_window_highlight_space(&g_window_manager);
    TEST_CHECK(g_window_manager.active_window_highlight.space_visible_cached, false);

    // Without the cache the fast path queries once per update.
    active_window_highlight_space_visible_result = true;
    window_manager_refresh_active_window_highlight_for_frame(&g_window_manager, window.id, target);
    TEST_CHECK(active_window_highlight_space_visible_calls, 1);
    TEST_CHECK(active_window_highlight_hide_calls, 0);

    // Sticky highlights never query.
    g_window_manager.active_window_highlight.sticky = true;
    window_manager_refresh_active_window_highlight_for_frame(&g_window_manager, window.id, target);
    TEST_CHECK(active_window_highlight_space_visible_calls, 1);

    table_remove(&g_window_manager.window, &window.id);
    struct table window_table = g_window_manager.window;
    struct table animations_table = g_window_manager.window_animations_table;
    g_window_manager = saved;
    g_window_manager.window = window_table;
    g_window_manager.window_animations_table = animations_table;
    if (initialized_window) { table_free(&g_window_manager.window); memset(&g_window_manager.window, 0, sizeof(g_window_manager.window)); }
    if (initialized_animations) { table_free(&g_window_manager.window_animations_table); memset(&g_window_manager.window_animations_table, 0, sizeof(g_window_manager.window_animations_table)); }
});

TEST_FUNC(managed_space_visibility_resolves_from_topology_or_reports_unknown,
{
    NSArray *topology = @[
        @{ @"Display Identifier": @"one", @"Current Space": @{ @"id64": @2 },
           @"Spaces": @[ @{ @"id64": @1 }, @{ @"id64": @2 } ] },
        @{ @"Display Identifier": @"two", @"Current Space": @{ @"id64": @9 },
           @"Spaces": @[ @{ @"id64": @9 } ] },
        @{ @"Display Identifier": @"three", @"Spaces": @[ @{ @"id64": @11 } ] }
    ];
    bool visible = true;
    TEST_CHECK(managed_space_is_visible((CFArrayRef) topology, 1, &visible), true);
    TEST_CHECK(visible, false);
    TEST_CHECK(managed_space_is_visible((CFArrayRef) topology, 2, &visible), true);
    TEST_CHECK(visible, true);
    TEST_CHECK(managed_space_is_visible((CFArrayRef) topology, 9, &visible), true);
    TEST_CHECK(visible, true);
    TEST_CHECK(managed_space_is_visible((CFArrayRef) topology, 11, &visible), false);
    TEST_CHECK(managed_space_is_visible((CFArrayRef) topology, 77, &visible), false);
    TEST_CHECK(managed_space_is_visible((CFArrayRef) topology, 0, &visible), false);
    TEST_CHECK(managed_space_is_visible(NULL, 2, &visible), false);
    TEST_CHECK(managed_space_is_visible((CFTypeRef) CFSTR("bad"), 2, &visible), false);
});
