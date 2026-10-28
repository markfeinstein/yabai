static int active_window_highlight_refresh_calls;
static int active_window_highlight_hide_calls;

static void active_window_highlight_test_refresh_observer(struct window_manager *wm,
                                                           uint32_t target_wid,
                                                           const CGRect *target_frame)
{
    (void) wm;
    (void) target_wid;
    (void) target_frame;
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
    TEST_CHECK(geometry.frame.origin.x, 6.0);
    TEST_CHECK(geometry.frame.origin.y, 16.0);
    TEST_CHECK(geometry.frame.size.width, 108.0);
    TEST_CHECK(geometry.frame.size.height, 48.0);
    TEST_CHECK(geometry.stroke_width, 4.0);
    TEST_CHECK(geometry.stroke_rect.origin.x, 2.0);
    TEST_CHECK(geometry.stroke_rect.origin.y, 2.0);
    TEST_CHECK(geometry.stroke_rect.size.width, 104.0);
    TEST_CHECK(geometry.stroke_rect.size.height, 44.0);
    TEST_CHECK(geometry.corner_radius_x, 11.0);
    TEST_CHECK(geometry.corner_radius_y, 11.0);

    struct active_window_highlight_geometry clamped =
        active_window_highlight_geometry_make(CGRectMake(0.0, 0.0, 8.0, 4.0), 100.0f, 100.0f);
    TEST_CHECK(clamped.stroke_width, 2.0);
    TEST_CHECK(clamped.frame.size.width, 12.0);
    TEST_CHECK(clamped.frame.size.height, 8.0);
    TEST_CHECK(clamped.stroke_rect.size.width, 10.0);
    TEST_CHECK(clamped.stroke_rect.size.height, 6.0);
    TEST_CHECK(clamped.corner_radius_x, 5.0);
    TEST_CHECK(clamped.corner_radius_y, 3.0);

    struct active_window_highlight_geometry empty =
        active_window_highlight_geometry_make(CGRectMake(3.0, 4.0, -8.0, -4.0), 4.0f, 9.0f);
    TEST_CHECK(empty.stroke_width, 0.0);
    TEST_CHECK(empty.frame.size.width, 0.0);
    TEST_CHECK(empty.frame.size.height, 0.0);
    TEST_CHECK(empty.stroke_rect.size.width, 0.0);
    TEST_CHECK(empty.stroke_rect.size.height, 0.0);

    struct active_window_highlight state = { .ordered = true };
    active_window_highlight_invalidate_order(&state);
    TEST_CHECK(state.ordered, true);
    TEST_CHECK(state.needs_reorder, true);
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

TEST_FUNC(active_window_highlight_manager_routes_enable_refresh_and_hide,
{
    __auto_type saved_refresh = window_manager_test_active_window_highlight_refresh_observer;
    __auto_type saved_hide = window_manager_test_active_window_highlight_hide_observer;
    bool saved_enabled = g_window_manager.enable_active_window_highlight;
    active_window_highlight_refresh_calls = 0;
    active_window_highlight_hide_calls = 0;
    window_manager_test_active_window_highlight_refresh_observer = active_window_highlight_test_refresh_observer;
    window_manager_test_active_window_highlight_hide_observer = active_window_highlight_test_hide_observer;

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
    window_manager_test_active_window_highlight_hide_observer = saved_hide;
});
