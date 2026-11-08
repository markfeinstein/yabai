extern int g_connection;

struct active_window_highlight_geometry active_window_highlight_geometry_make(CGRect target_frame,
                                                                                float configured_width,
                                                                                float configured_radius)
{
    target_frame.size.width = fmax(0.0, target_frame.size.width);
    target_frame.size.height = fmax(0.0, target_frame.size.height);
    CGFloat minimum_dimension = fmin(target_frame.size.width, target_frame.size.height);
    CGFloat stroke_width = fmin((CGFloat) configured_width, 0.5 * minimum_dimension);
    CGFloat stroke_outset = 0.5 * stroke_width;
    CGRect stroke_frame = CGRectInset(target_frame, -stroke_outset, -stroke_outset);
    // The overlay uses a 1x backing surface. Keep its WindowServer shape on whole
    // points so odd stroke widths do not leave an unpainted top/right backing row.
    CGRect frame = CGRectIntegral(stroke_frame);
    CGRect bounds = CGRectMake(0.0, 0.0, frame.size.width, frame.size.height);
    CGRect stroke_rect = CGRectMake(target_frame.origin.x - frame.origin.x,
                                    target_frame.origin.y - frame.origin.y,
                                    target_frame.size.width, target_frame.size.height);
    CGFloat radius = fmax(0.0, (CGFloat) configured_radius) + stroke_outset;

    return (struct active_window_highlight_geometry) {
        .frame = frame,
        .bounds = bounds,
        .stroke_rect = stroke_rect,
        .stroke_width = stroke_width,
        .corner_radius_x = cgrect_clamp_x_radius(stroke_rect, radius),
        .corner_radius_y = cgrect_clamp_y_radius(stroke_rect, radius)
    };
}

static void active_window_highlight_release_window(struct active_window_highlight *highlight)
{
    if (highlight->context) CGContextRelease(highlight->context);
    if (highlight->id) SLSReleaseWindow(g_connection, highlight->id);
    highlight->id = 0;
    highlight->context = NULL;
    highlight->ordered = false;
    highlight->needs_reorder = false;
}

void active_window_highlight_hide(struct active_window_highlight *highlight)
{
    if (!highlight || !highlight->id || !highlight->ordered) return;
    if (SLSOrderWindow(g_connection, highlight->id, 0, 0) != kCGErrorSuccess) {
        active_window_highlight_release_window(highlight);
        return;
    }
    highlight->ordered = false;
    highlight->needs_reorder = false;
}

void active_window_highlight_invalidate_order(struct active_window_highlight *highlight)
{
    if (highlight) highlight->needs_reorder = true;
}

bool active_window_highlight_can_fast_update(const struct active_window_highlight *highlight,
                                             uint32_t target_wid)
{
    return highlight && highlight->id && highlight->context && highlight->ordered &&
           !highlight->needs_reorder && highlight->target_wid == target_wid;
}

void active_window_highlight_destroy(struct active_window_highlight *highlight)
{
    if (!highlight) return;
    active_window_highlight_hide(highlight);
    active_window_highlight_release_window(highlight);
    memset(highlight, 0, sizeof(*highlight));
}

static bool active_window_highlight_create(struct active_window_highlight *highlight, CFTypeRef frame_region)
{
    uint64_t tags = WINDOW_SERVER_TAG_FLOATING | WINDOW_SERVER_TAG_IGNORE_FOR_EVENTS;
    CFTypeRef empty_region = CGRegionCreateEmptyRegion();
    if (!empty_region) return false;

    CGError result = SLSNewWindowWithOpaqueShapeAndContext(g_connection, 2, frame_region, empty_region,
                                                           13, &tags, 0, 0, 64, &highlight->id, NULL);
    CFRelease(empty_region);
    if (result != kCGErrorSuccess || !highlight->id) {
        highlight->id = 0;
        return false;
    }

    (void) sls_window_disable_shadow(highlight->id);
    if (SLSSetWindowResolution(g_connection, highlight->id, 1.0f) != kCGErrorSuccess ||
        SLSSetWindowOpacity(g_connection, highlight->id, false) != kCGErrorSuccess) {
        active_window_highlight_release_window(highlight);
        return false;
    }

    highlight->context = SLWindowContextCreate(g_connection, highlight->id, 0);
    if (!highlight->context) {
        active_window_highlight_release_window(highlight);
        return false;
    }

    CGContextSetInterpolationQuality(highlight->context, kCGInterpolationNone);
    CGContextSetShouldAntialias(highlight->context, true);
    return true;
}

static bool active_window_highlight_assign_space(struct active_window_highlight *highlight,
                                                  uint64_t sid, bool sticky)
{
    uint64_t sticky_tag = WINDOW_SERVER_TAG_ON_ALL_WORKSPACES;
    if (sticky) {
        return SLSSetWindowTags(g_connection, highlight->id, &sticky_tag, 64) == kCGErrorSuccess;
    }

    if (!sid || SLSClearWindowTags(g_connection, highlight->id, &sticky_tag, 64) != kCGErrorSuccess) return false;
    CFArrayRef window_list = cfarray_of_cfnumbers(&highlight->id, sizeof(highlight->id), 1,
                                                  kCFNumberSInt32Type);
    if (!window_list) return false;
    SLSMoveWindowsToManagedSpace(g_connection, window_list, sid);
    CFRelease(window_list);
    return true;
}

static bool active_window_highlight_needs_redraw(const struct active_window_highlight *highlight,
                                                  const struct active_window_highlight_geometry *geometry,
                                                  uint32_t color, float width, float radius)
{
    return !highlight->id || !CGSizeEqualToSize(highlight->frame.size, geometry->frame.size) ||
           highlight->color != color || highlight->width != width || highlight->radius != radius;
}

static bool active_window_highlight_draw(struct active_window_highlight *highlight,
                                         const struct active_window_highlight_geometry *geometry,
                                         uint32_t color)
{
    struct rgba_color rgba = rgba_color_from_hex(color);
    CGPathRef path = CGPathCreateWithRoundedRect(geometry->stroke_rect,
                                                 geometry->corner_radius_x,
                                                 geometry->corner_radius_y, NULL);
    if (!path) return false;
    CGContextClearRect(highlight->context, geometry->bounds);
    CGContextSetLineWidth(highlight->context, geometry->stroke_width);
    CGContextSetRGBStrokeColor(highlight->context, rgba.r, rgba.g, rgba.b, rgba.a);
    CGContextAddPath(highlight->context, path);
    CGContextStrokePath(highlight->context);
    CGContextFlush(highlight->context);
    CGPathRelease(path);
    return true;
}

bool active_window_highlight_show(struct active_window_highlight *highlight,
                                  uint32_t target_wid, CGRect target_frame,
                                  int level, int sub_level, uint64_t sid, bool sticky,
                                  uint32_t color, float width, float radius)
{
    if (!highlight || !target_wid || !color || !isfinite(width) || width <= 0.0f ||
        !isfinite(radius) || radius < 0.0f || !isfinite(target_frame.origin.x) ||
        !isfinite(target_frame.origin.y) || !isfinite(target_frame.size.width) ||
        !isfinite(target_frame.size.height) || target_frame.size.width <= 0.0f ||
        target_frame.size.height <= 0.0f) {
        if (highlight) active_window_highlight_hide(highlight);
        return false;
    }

    struct active_window_highlight_geometry geometry =
        active_window_highlight_geometry_make(target_frame, width, radius);
    if (geometry.stroke_width <= 0.0 || geometry.stroke_rect.size.width <= 0.0 ||
        geometry.stroke_rect.size.height <= 0.0) {
        active_window_highlight_hide(highlight);
        return false;
    }

    bool create = highlight->id == 0;
    bool target_changed = highlight->target_wid != target_wid;
    bool origin_changed = !CGPointEqualToPoint(highlight->frame.origin, geometry.frame.origin);
    bool size_changed = !CGSizeEqualToSize(highlight->frame.size, geometry.frame.size);
    bool frame_changed = origin_changed || size_changed;
    bool style_changed = highlight->color != color || highlight->width != width || highlight->radius != radius;
    bool redraw = active_window_highlight_needs_redraw(highlight, &geometry, color, width, radius);
    bool level_changed = highlight->level != level || highlight->sub_level != sub_level;
    bool space_changed = highlight->sticky != sticky || (!sticky && highlight->sid != sid);
    bool order_changed = highlight->needs_reorder || !highlight->ordered || target_changed || level_changed;
    if (!create && !frame_changed && !style_changed && !level_changed && !space_changed && !order_changed) {
        return true;
    }

    CFTypeRef frame_region = NULL;
    if ((create || size_changed) &&
        (CGSNewRegionWithRect(&geometry.frame, &frame_region) != kCGErrorSuccess || !frame_region)) {
        active_window_highlight_hide(highlight);
        return false;
    }

    bool update_disabled = SLSDisableUpdate(g_connection) == kCGErrorSuccess;
    bool success = true;
    if (create) success = active_window_highlight_create(highlight, frame_region);
    if (success && (create || size_changed) &&
        SLSSetWindowShape(g_connection, highlight->id, 0.0f, 0.0f, frame_region) != kCGErrorSuccess) {
        success = false;
    }
    if (success && !create && origin_changed && !size_changed) {
        CGPoint origin = geometry.frame.origin;
        if (SLSMoveWindow(g_connection, highlight->id, &origin) != kCGErrorSuccess) success = false;
    }
    if (success && (create || space_changed) &&
        !active_window_highlight_assign_space(highlight, sid, sticky)) success = false;
    if (success && (create || highlight->level != level) &&
        SLSSetWindowLevel(g_connection, highlight->id, level) != kCGErrorSuccess) success = false;
    if (success && (create || highlight->sub_level != sub_level) &&
        SLSSetWindowSubLevel(g_connection, highlight->id, sub_level) != kCGErrorSuccess) success = false;
    // Resize through the WindowServer shape, but move the existing overlay directly.
    // Shape updates can discard context contents; direct moves preserve the drawing
    // and avoid a clear/redraw cycle for every pointer sample during a drag.
    if (success && redraw && !active_window_highlight_draw(highlight, &geometry, color)) success = false;
    if (success && (create || order_changed) &&
        SLSOrderWindow(g_connection, highlight->id, 1, target_wid) != kCGErrorSuccess) success = false;

    if (update_disabled) (void) SLSReenableUpdate(g_connection);
    if (frame_region) CFRelease(frame_region);

    if (!success) {
        active_window_highlight_destroy(highlight);
        return false;
    }

    highlight->frame = geometry.frame;
    highlight->target_wid = target_wid;
    highlight->color = color;
    highlight->sid = sid;
    highlight->level = level;
    highlight->sub_level = sub_level;
    highlight->width = width;
    highlight->radius = radius;
    highlight->sticky = sticky;
    highlight->ordered = true;
    highlight->needs_reorder = false;
    return true;
}
