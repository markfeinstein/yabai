extern int g_connection;
extern struct display_manager g_display_manager;
extern struct space_manager g_space_manager;
extern struct window_manager g_window_manager;
extern struct mouse_state g_mouse_state;

#ifdef TESTS
static struct view_test_platform_ops view_test_platform_ops;
#endif

struct insert_feedback_geometry insert_feedback_geometry_make(CGRect frame, float configured_width, float configured_radius)
{
    frame.size.width = fmax(0.0, frame.size.width);
    frame.size.height = fmax(0.0, frame.size.height);
    CGFloat minimum_dimension = fmin(frame.size.width, frame.size.height);
    CGFloat stroke_width = fmin((CGFloat) configured_width, 0.5 * minimum_dimension);
    CGFloat radius_inset = 0.5 * stroke_width;
    CGRect stroke_rect = CGRectInset(frame, radius_inset, radius_inset);
    CGRect fill_rect = CGRectInset(stroke_rect, radius_inset, radius_inset);
    CGFloat corner_radius_x = cgrect_clamp_x_radius(stroke_rect, configured_radius);
    CGFloat corner_radius_y = cgrect_clamp_y_radius(stroke_rect, configured_radius);
    return (struct insert_feedback_geometry) {
        .stroke_width = stroke_width,
        .corner_radius_x = corner_radius_x,
        .corner_radius_y = corner_radius_y,
        .fill_radius_x = cgrect_clamp_x_radius(fill_rect, fmax(0.0, corner_radius_x - radius_inset)),
        .fill_radius_y = cgrect_clamp_y_radius(fill_rect, fmax(0.0, corner_radius_y - radius_inset)),
        .stroke_rect = stroke_rect,
        .fill_rect = fill_rect
    };
}

#ifdef TESTS
void insert_feedback_refresh_all_with(void (*refresh)(struct window_node *))
#else
static void insert_feedback_refresh_all_with(void (*refresh)(struct window_node *))
#endif
{
    table_for (struct window_node *node, g_window_manager.insert_feedback, {
        refresh(node);
    });
}

static void insert_feedback_refresh_node(struct window_node *node)
{
    if (node->feedback_window.id &&
        (node->insert_dir == DIR_NORTH || node->insert_dir == DIR_EAST ||
         node->insert_dir == DIR_SOUTH || node->insert_dir == DIR_WEST ||
         node->insert_dir == STACK)) {
        insert_feedback_show(node);
    }
}

void insert_feedback_refresh_all(void)
{
    insert_feedback_refresh_all_with(insert_feedback_refresh_node);
}

void insert_feedback_show(struct window_node *node)
{
#ifdef TESTS
    if (view_test_platform_ops.feedback_show_observer &&
        view_test_platform_ops.feedback_show_observer(node, view_test_platform_ops.context)) return;
#endif
    if (node->insert_dir != DIR_NORTH &&
        node->insert_dir != DIR_EAST &&
        node->insert_dir != DIR_SOUTH &&
        node->insert_dir != DIR_WEST &&
        node->insert_dir != STACK) {
        insert_feedback_destroy(node);
        return;
    }

    CFTypeRef frame_region;
    CGRect frame = {{node->area.x, node->area.y},{node->area.w, node->area.h}};
    CGSNewRegionWithRect(&frame, &frame_region);
    frame.origin.x = 0; frame.origin.y = 0;

    if (!node->feedback_window.id) {
        uint64_t tags = (1ULL << 1) | (1ULL << 9);
        CFTypeRef empty_region = CGRegionCreateEmptyRegion();
        SLSNewWindowWithOpaqueShapeAndContext(g_connection, 2, frame_region, empty_region, 13, &tags, 0, 0, 64, &node->feedback_window.id, NULL);
        CFRelease(empty_region);

        // Shadow suppression is cosmetic; keep insertion feedback usable if it fails.
        (void) sls_window_disable_shadow(node->feedback_window.id);
        SLSSetWindowResolution(g_connection, node->feedback_window.id, 1.0f);
        SLSSetWindowOpacity(g_connection, node->feedback_window.id, 0);
        SLSSetWindowLevel(g_connection, node->feedback_window.id, window_level(node->window_order[0]));
        SLSSetWindowSubLevel(g_connection, node->feedback_window.id, window_sub_level(node->window_order[0]));
        node->feedback_window.context = SLWindowContextCreate(g_connection, node->feedback_window.id, 0);
        SLSDisableUpdate(g_connection);
        CGContextClearRect(node->feedback_window.context, frame);
        CGContextFlush(node->feedback_window.context);
        SLSReenableUpdate(g_connection);
        SLSOrderWindow(g_connection, node->feedback_window.id, 1, node->window_order[0]);
        table_add(&g_window_manager.insert_feedback, &node->window_order[0], node);
        if (!workspace_is_macos_sequoia() && !workspace_is_macos_tahoe()) {
            update_window_notifications();
        }
    }

    struct insert_feedback_geometry geometry = insert_feedback_geometry_make(frame,
                                                                              g_window_manager.insert_feedback_width,
                                                                              g_window_manager.insert_feedback_radius);
    CGFloat width = geometry.stroke_width;
    CGFloat clip_x, clip_y, clip_w, clip_h;
    CGFloat midx = CGRectGetMidX(frame);
    CGFloat midy = CGRectGetMidY(frame);

    switch (node->insert_dir) {
    case DIR_NORTH: {
        clip_x = -0.5f * width;
        clip_y = midy - 0.5f * width;
        clip_w = width;
        clip_h = width;
    } break;
    case DIR_EAST: {
        clip_x = midx - 0.5f * width;
        clip_y = -0.5f * width;
        clip_w = width;
        clip_h = width;
    } break;
    case DIR_SOUTH: {
        clip_x = -0.5f * width;
        clip_y = -0.5f * width;
        clip_w = width;
        clip_h = -midy + width;
    } break;
    case DIR_WEST: {
        clip_x = -0.5f * width;
        clip_y = -0.5f * width;
        clip_w = -midx + width;
        clip_h = width;
    } break;
    case STACK: {
        clip_x = -0.5f * width;
        clip_y = -0.5f * width;
        clip_w = width;
        clip_h = width;
    } break;
    }

    CGRect rect = geometry.stroke_rect;
    CGRect fill = geometry.fill_rect;
    CGRect clip = { { rect.origin.x + clip_x, rect.origin.y + clip_y }, { rect.size.width + clip_w, rect.size.height + clip_h } };
    CGPathRef path = CGPathCreateWithRoundedRect(rect, geometry.corner_radius_x, geometry.corner_radius_y, NULL);
    CGPathRef fill_path = CGPathCreateWithRoundedRect(fill, geometry.fill_radius_x, geometry.fill_radius_y, NULL);

    CGContextSetLineWidth(node->feedback_window.context, width);
    CGContextSetRGBFillColor(node->feedback_window.context,
                             g_window_manager.insert_feedback_color.r,
                             g_window_manager.insert_feedback_color.g,
                             g_window_manager.insert_feedback_color.b,
                             g_window_manager.insert_feedback_color.a*0.25f);
    CGContextSetRGBStrokeColor(node->feedback_window.context,
                               g_window_manager.insert_feedback_color.r,
                               g_window_manager.insert_feedback_color.g,
                               g_window_manager.insert_feedback_color.b,
                               g_window_manager.insert_feedback_color.a);
    SLSDisableUpdate(g_connection);
    SLSSetWindowShape(g_connection, node->feedback_window.id, 0.0f, 0.0f, frame_region);
    CGContextClearRect(node->feedback_window.context, frame);
    CGContextClipToRect(node->feedback_window.context, clip);
    CGContextAddPath(node->feedback_window.context, fill_path);
    CGContextFillPath(node->feedback_window.context);
    CGContextAddPath(node->feedback_window.context, path);
    CGContextStrokePath(node->feedback_window.context);
    CGContextResetClip(node->feedback_window.context);
    CGContextFlush(node->feedback_window.context);
    SLSReenableUpdate(g_connection);
    CGPathRelease(fill_path);
    CGPathRelease(path);
    CFRelease(frame_region);
}

void insert_feedback_destroy(struct window_node *node)
{
    if (g_mouse_state.feedback_node == node) {
        g_mouse_state.feedback_node = NULL;
    }
#ifdef TESTS
    if (view_test_platform_ops.feedback_destroy_observer &&
        view_test_platform_ops.feedback_destroy_observer(node, view_test_platform_ops.context)) return;
#endif

    if (node->feedback_window.id) {
        table_remove(&g_window_manager.insert_feedback, &node->window_order[0]);

        if (!workspace_is_macos_sequoia() && !workspace_is_macos_tahoe()) {
            update_window_notifications();
        }

        SLSOrderWindow(g_connection, node->feedback_window.id, 0, 0);
        CGContextRelease(node->feedback_window.context);
        SLSReleaseWindow(g_connection, node->feedback_window.id);
        memset(&node->feedback_window, 0, sizeof(struct feedback_window));
    }
}

void insert_feedback_rekey(struct window_node *node, uint32_t prev_window_id)
{
    if (node->feedback_window.id && prev_window_id != node->window_order[0]) {
        table_remove(&g_window_manager.insert_feedback, &prev_window_id);
        table_add(&g_window_manager.insert_feedback, &node->window_order[0], node);

        if (!workspace_is_macos_sequoia() && !workspace_is_macos_tahoe()) {
            update_window_notifications();
        }
    }
}

static void insert_feedback_adopt(struct window_node *parent, struct window_node *child)
{
    if (child->feedback_window.id) {
        table_remove(&g_window_manager.insert_feedback, &child->window_order[0]);
        parent->feedback_window = child->feedback_window;
        memset(&child->feedback_window, 0, sizeof(struct feedback_window));
        table_add(&g_window_manager.insert_feedback, &parent->window_order[0], parent);

        if (!workspace_is_macos_sequoia() && !workspace_is_macos_tahoe()) {
            update_window_notifications();
        }
    }

    if (g_mouse_state.feedback_node == child) {
        g_mouse_state.feedback_node = parent;
    }
}

static inline struct area area_from_cgrect(CGRect rect)
{
    return (struct area) { rect.origin.x, rect.origin.y, rect.size.width, rect.size.height };
}

static inline CGPoint area_max_point(struct area area)
{
    return (CGPoint) { area.x + area.w - 1, area.y + area.h - 1 };
}

static inline enum window_node_child window_node_get_child(struct window_node *node)
{
    return node->child != CHILD_NONE ? node->child : g_space_manager.window_placement;
}

static inline enum window_node_split window_node_get_split(struct view *view, struct window_node *node)
{
    if (node->split != SPLIT_NONE) return node->split;

    if (view->split_type != SPLIT_NONE) {
        if (view->split_type != SPLIT_AUTO) {
            return view->split_type;
        }
    } else if (g_space_manager.split_type != SPLIT_AUTO) {
        return g_space_manager.split_type;
    }

    return node->area.w >= node->area.h ? SPLIT_Y : SPLIT_X;
}

static inline float window_node_get_ratio(struct view *view, struct window_node *node)
{
    return in_range_ii(node->ratio, 0.1f, 0.9f) ? node->ratio : view->split_ratio;
}

static inline int window_node_get_gap(struct view *view)
{
    return view_check_flag(view, VIEW_ENABLE_GAP) ? view->window_gap : 0;
}

static void area_make_pair(enum window_node_split split, int gap, float ratio, struct area *parent_area, struct area *left_area, struct area *right_area)
{
    area_split_pair(split, gap, ratio, parent_area, left_area, right_area);
}

static void area_make_pair_for_node(struct view *view, struct window_node *node)
{
    enum window_node_split split = window_node_get_split(view, node);
    float ratio = window_node_get_ratio(view, node);
    int gap     = window_node_get_gap(view);

    area_make_pair(split, gap, ratio, &node->area, &node->left->area, &node->right->area);

    node->split = split;
    node->ratio = ratio;
}

static inline bool window_node_is_occupied(struct window_node *node)
{
    return node->window_count != 0;
}

static inline bool window_node_is_intermediate(struct window_node *node)
{
    return node->parent != NULL;
}

static inline bool window_node_is_leaf(struct window_node *node)
{
    return node->left == NULL && node->right == NULL;
}

static bool view_has_single_managed_window(struct view *view)
{
    return view && view->layout != VIEW_FLOAT && view->root &&
           window_node_is_leaf(view->root) && view->root->window_count == 1;
}

static bool view_single_window_width_enabled(struct view *view)
{
    if (!view) return false;
    bool proportional = view->single_window_width_ratio > 0.0f &&
                        view->single_window_width_ratio < 1.0f &&
                        isfinite(view->single_window_width_ratio);
    return proportional || view->single_window_max_width > 0;
}

void view_apply_single_window_width(struct view *view)
{
    if (!view_has_single_managed_window(view) || !view_single_window_width_enabled(view)) return;

    struct area *area = &view->root->area;
    double available = area->w;
    double ratio = view->single_window_width_ratio > 0.0f &&
                   view->single_window_width_ratio <= 1.0f &&
                   isfinite(view->single_window_width_ratio)
                 ? view->single_window_width_ratio : 1.0;
    double requested = available * ratio;
    if (view->single_window_max_width > 0) {
        requested = fmin(requested, (double) view->single_window_max_width);
    }
    if (!isfinite(available) || available <= requested) return;

    double centered_x = (double) area->x + (available - requested) * 0.5;
    if (!isfinite(centered_x) || centered_x > FLT_MAX || centered_x < -FLT_MAX) return;
    area->x = (float) centered_x;
    area->w = (float) requested;
}

static bool view_reconcile_single_window_geometry(struct view *view, bool was_single)
{
    if (!view_single_window_width_enabled(view)) return false;
    if (was_single == view_has_single_managed_window(view)) return false;
    view_update(view);
    return true;
}

static inline bool window_node_is_left_child(struct window_node *node)
{
    return node->parent && node->parent->left == node;
}

static inline bool window_node_is_right_child(struct window_node *node)
{
    return node->parent && node->parent->right == node;
}

static void window_node_equalize(struct window_node *node, uint32_t axis_flag, float split_ratio)
{
    if (node->left)  window_node_equalize(node->left, axis_flag, split_ratio);
    if (node->right) window_node_equalize(node->right, axis_flag, split_ratio);

    if ((axis_flag & SPLIT_Y) && node->split == SPLIT_Y) {
        node->ratio = split_ratio;
    }

    if ((axis_flag & SPLIT_X) && node->split == SPLIT_X) {
        node->ratio = split_ratio;
    }
}

static inline struct balance_node balance_node_add(struct balance_node a, struct balance_node b)
{
    return (struct balance_node) { a.y_count + b.y_count, a.x_count + b.x_count, };
}

static struct balance_node window_node_balance(struct window_node *node, uint32_t axis_flag)
{
    if (window_node_is_leaf(node)) {
        return (struct balance_node) {
            node->parent ? node->parent->split == SPLIT_Y : 0,
            node->parent ? node->parent->split == SPLIT_X : 0
        };
    }

    struct balance_node left_leafs  = window_node_balance(node->left, axis_flag);
    struct balance_node right_leafs = window_node_balance(node->right, axis_flag);
    struct balance_node total_leafs = balance_node_add(left_leafs, right_leafs);

    if (axis_flag & SPLIT_Y) {
        if (node->split == SPLIT_Y) {
            node->ratio = (float) left_leafs.y_count / total_leafs.y_count;
            --total_leafs.y_count;
        }
    }

    if (axis_flag & SPLIT_X) {
        if (node->split == SPLIT_X) {
            node->ratio = (float) left_leafs.x_count / total_leafs.x_count;
            --total_leafs.x_count;
        }
    }

    if (node->parent) {
        total_leafs.y_count += node->parent->split == SPLIT_Y;
        total_leafs.x_count += node->parent->split == SPLIT_X;
    }

    return total_leafs;
}

static bool window_node_split_prepared(struct view *view, struct window_node *node, struct window *window,
                                       struct window_node *left, struct window_node *right)
{
    if (!view || !node || !window || !left || !right) return false;

    struct window_node *zoom = !g_space_manager.window_zoom_persist
                             ? NULL
                             : !node->zoom
                             ? NULL
                             : node->zoom == node->parent
                             ? node
                             : view->root;

    if (window_node_get_child(node) == CHILD_SECOND) {
        memcpy(left->window_list, node->window_list, sizeof(uint32_t) * node->window_count);
        memcpy(left->window_order, node->window_order, sizeof(uint32_t) * node->window_count);
        left->window_count = node->window_count;
        left->zoom = zoom;

        right->window_list[0] = window->id;
        right->window_order[0] = window->id;
        right->window_count = 1;
    } else {
        memcpy(right->window_list, node->window_list, sizeof(uint32_t) * node->window_count);
        memcpy(right->window_order, node->window_order, sizeof(uint32_t) * node->window_count);
        right->window_count = node->window_count;
        right->zoom = zoom;

        left->window_list[0] = window->id;
        left->window_order[0] = window->id;
        left->window_count = 1;
    }

    left->parent  = node;
    right->parent = node;

    node->window_count = 0;
    node->left  = left;
    node->right = right;
    node->zoom  = NULL;

    area_make_pair_for_node(view, node);
    return true;
}

bool window_node_split(struct view *view, struct window_node *node, struct window *window)
{
    if (!view || !node || !window) return false;
    struct window_node *left = legacy_allocation_calloc(1, sizeof(struct window_node));
    if (!left) return false;
    struct window_node *right = legacy_allocation_calloc(1, sizeof(struct window_node));
    if (!right) {
        legacy_allocation_free(left);
        return false;
    }
    return window_node_split_prepared(view, node, window, left, right);
}

void window_node_update(struct view *view, struct window_node *node)
{
    if (window_node_is_leaf(node)) {
        if (node->insert_dir) insert_feedback_show(node);
    } else {
        area_make_pair_for_node(view, node);
        window_node_update(view, node->left);
        window_node_update(view, node->right);
    }
}

static void window_node_destroy(struct window_node *node)
{
    if (node->left)  window_node_destroy(node->left);
    if (node->right) window_node_destroy(node->right);

    for (int i = 0; i < node->window_count; ++i) {
        window_manager_remove_managed_window(&g_window_manager, node->window_list[i]);
    }

    insert_feedback_destroy(node);
    legacy_allocation_free(node);
}

static void window_node_clear_zoom(struct window_node *node)
{
    node->zoom = NULL;

    if (!window_node_is_leaf(node)) {
        window_node_clear_zoom(node->left);
        window_node_clear_zoom(node->right);
    }
}

void window_node_capture_windows(struct window_node *node, struct window_capture **window_list)
{
    if (window_node_is_leaf(node)) {
        for (int i = 0; i < node->window_count; ++i) {
            struct window *window = window_manager_find_window(&g_window_manager, node->window_list[i]);
            if (window) {
                struct area area = node->has_window_areas ? node->window_area[i]
                                 : node->zoom ? node->zoom->area : node->area;
                ts_buf_push(*window_list, ((struct window_capture) { .window = window, .x = area.x, .y = area.y, .w = area.w, .h = area.h }));
            }
        }
    } else {
        window_node_capture_windows(node->left, window_list);
        window_node_capture_windows(node->right, window_list);
    }
}

void window_node_flush(struct window_node *node)
{
#ifdef TESTS
    if (view_test_platform_ops.node_flush_observer) {
        view_test_platform_ops.node_flush_observer(node, view_test_platform_ops.context);
    }
#endif
    struct window_capture *window_list = NULL;
    window_node_capture_windows(node, &window_list);
    if (window_list) window_manager_animate_window_list(window_list, ts_buf_len(window_list));
}

bool window_node_contains_window(struct window_node *node, uint32_t window_id)
{
    for (int i = 0; i < node->window_count; ++i) {
        if (node->window_list[i] == window_id) return true;
    }

    return false;
}

int window_node_index_of_window(struct window_node *node, uint32_t window_id)
{
    for (int i = 0; i < node->window_count; ++i) {
        if (node->window_list[i] == window_id) return i;
    }

    return 0;
}

bool window_node_make_window_order_head(struct window_node *node, uint32_t window_id)
{
    uint32_t prev_order_head = node->window_order[0];

    for (int i = 0; i < node->window_count; ++i) {
        if (node->window_order[i] != window_id) continue;

        memmove(node->window_order + 1, node->window_order, sizeof(uint32_t) * i);
        node->window_order[0] = window_id;
        insert_feedback_rekey(node, prev_order_head);
        return true;
    }

    return false;
}

void window_node_swap_window_list(struct window_node *a_node, struct window_node *b_node)
{
    uint32_t a_prev_order_head = a_node->window_order[0];
    uint32_t b_prev_order_head = b_node->window_order[0];
    uint32_t tmp_window_list[NODE_MAX_WINDOW_COUNT];
    uint32_t tmp_window_order[NODE_MAX_WINDOW_COUNT];
    uint32_t tmp_window_count;

    memcpy(tmp_window_list, a_node->window_list, sizeof(uint32_t) * a_node->window_count);
    memcpy(tmp_window_order, a_node->window_order, sizeof(uint32_t) * a_node->window_count);
    tmp_window_count = a_node->window_count;

    memcpy(a_node->window_list, b_node->window_list, sizeof(uint32_t) * b_node->window_count);
    memcpy(a_node->window_order, b_node->window_order, sizeof(uint32_t) * b_node->window_count);
    a_node->window_count = b_node->window_count;

    memcpy(b_node->window_list, tmp_window_list, sizeof(uint32_t) * tmp_window_count);
    memcpy(b_node->window_order, tmp_window_order, sizeof(uint32_t) * tmp_window_count);
    b_node->window_count = tmp_window_count;

    a_node->zoom = NULL;
    b_node->zoom = NULL;

    if (a_node->feedback_window.id || b_node->feedback_window.id) {
        if (a_node->feedback_window.id) table_remove(&g_window_manager.insert_feedback, &a_prev_order_head);
        if (b_node->feedback_window.id) table_remove(&g_window_manager.insert_feedback, &b_prev_order_head);
        if (a_node->feedback_window.id) table_add(&g_window_manager.insert_feedback, &a_node->window_order[0], a_node);
        if (b_node->feedback_window.id) table_add(&g_window_manager.insert_feedback, &b_node->window_order[0], b_node);

        if (!workspace_is_macos_sequoia() && !workspace_is_macos_tahoe()) {
            update_window_notifications();
        }
    }
}

struct window_node *window_node_find_first_leaf(struct window_node *root)
{
    struct window_node *node = root;
    while (!window_node_is_leaf(node)) {
        node = node->left;
    }
    return node;
}

struct window_node *window_node_find_last_leaf(struct window_node *root)
{
    struct window_node *node = root;
    while (!window_node_is_leaf(node)) {
        node = node->right;
    }
    return node;
}

struct window_node *window_node_find_prev_leaf(struct window_node *node)
{
    if (!node->parent) return NULL;

    if (window_node_is_left_child(node)) {
        return window_node_find_prev_leaf(node->parent);
    }

    if (window_node_is_leaf(node->parent->left)) {
        return node->parent->left;
    }

    return window_node_find_last_leaf(node->parent->left->right);
}

struct window_node *window_node_find_next_leaf(struct window_node *node)
{
    if (!node->parent) return NULL;

    if (window_node_is_right_child(node)) {
        return window_node_find_next_leaf(node->parent);
    }

    if (window_node_is_leaf(node->parent->right)) {
        return node->parent->right;
    }

    return window_node_find_first_leaf(node->parent->right->left);
}

void window_node_rotate(struct window_node *node, int degrees)
{
    if ((degrees ==  90 && node->split == SPLIT_Y) ||
        (degrees == 270 && node->split == SPLIT_X) ||
        (degrees == 180)) {
        struct window_node *temp = node->left;
        node->left  = node->right;
        node->right = temp;
        node->ratio = 1 - node->ratio;
    }

    if (degrees != 180) {
        if (node->split == SPLIT_X) {
            node->split = SPLIT_Y;
        } else if (node->split == SPLIT_Y) {
            node->split = SPLIT_X;
        }
    }

    if (!window_node_is_leaf(node)) {
        window_node_rotate(node->left, degrees);
        window_node_rotate(node->right, degrees);
    }
}

struct window_node *window_node_mirror(struct window_node *node, enum window_node_split axis)
{
    if (!window_node_is_leaf(node)) {
        struct window_node *left = window_node_mirror(node->left, axis);
        struct window_node *right = window_node_mirror(node->right, axis);

        if (node->split == axis) {
            node->left = right;
            node->right = left;
        }
    }

    return node;
}

struct window_node *window_node_fence(struct window_node *node, int dir)
{
    if (!node) return NULL;

    for (struct window_node *parent = node->parent; parent; parent = parent->parent) {
        if ((dir == DIR_NORTH && parent->split == SPLIT_X && parent->area.y < node->area.y) ||
            (dir == DIR_WEST  && parent->split == SPLIT_Y && parent->area.x < node->area.x) ||
            (dir == DIR_SOUTH && parent->split == SPLIT_X && (parent->area.y + parent->area.h) > (node->area.y + node->area.h)) ||
            (dir == DIR_EAST  && parent->split == SPLIT_Y && (parent->area.x + parent->area.w) > (node->area.x + node->area.w))) {
                return parent;
        }
    }

    return NULL;
}

static void view_find_min_depth_leaf_node_recursive(struct window_node *node, size_t depth, struct window_node **leaf, size_t *leaf_depth)
{
    if (!node || depth >= *leaf_depth) return;

    if (window_node_is_leaf(node)) {
        *leaf = node;
        *leaf_depth = depth;
        return;
    }

    view_find_min_depth_leaf_node_recursive(node->left, depth + 1, leaf, leaf_depth);
    view_find_min_depth_leaf_node_recursive(node->right, depth + 1, leaf, leaf_depth);
}

struct window_node *view_find_min_depth_leaf_node(struct window_node *node)
{
    struct window_node *leaf = NULL;
    size_t leaf_depth = SIZE_MAX;

    view_find_min_depth_leaf_node_recursive(node, 0, &leaf, &leaf_depth);
    return leaf;
}

static inline bool area_is_in_direction(struct area *r1, CGPoint r1_max, struct area *r2, CGPoint r2_max, int direction)
{
    if (direction == DIR_NORTH && r1_max.y <= r2->y) return false;
    if (direction == DIR_EAST  && r2_max.x <= r1->x) return false;
    if (direction == DIR_SOUTH && r2_max.y <= r1->y) return false;
    if (direction == DIR_WEST  && r1_max.x <= r2->x) return false;

    if (direction == DIR_NORTH || direction == DIR_SOUTH) {
        return ((r2_max.x >  r1->x && r2_max.x <= r1_max.x) ||
                (r2->x    <  r1->x && r2_max.x >  r1_max.x) ||
                (r2->x    >= r1->x && r2->x    <  r1_max.x));
    }

    if (direction == DIR_EAST || direction == DIR_WEST) {
        return ((r2_max.y >  r1->y && r2_max.y <= r1_max.y) ||
                (r2->y    <  r1->y && r2_max.y >  r1_max.y) ||
                (r2->y    >= r1->y && r2->y    <  r1_max.y));
    }

    return false;
}

static inline int area_distance_in_direction(struct area *r1, CGPoint r1_max, struct area *r2, CGPoint r2_max, int direction)
{
    switch (direction) {
    case DIR_NORTH: {
        return r2_max.y > r1->y ? r2_max.y - r1->y : r1->y - r2_max.y;
    } break;
    case DIR_EAST: {
        return r2->x < r1_max.x ? r1_max.x - r2->x : r2->x - r1_max.x;
    } break;
    case DIR_SOUTH: {
        return r2->y < r1_max.y ? r1_max.y - r2->y : r2->y - r1_max.y;
    } break;
    case DIR_WEST: {
        return r2_max.x > r1->x ? r2_max.x - r1->x : r1->x - r2_max.x;
    } break;
    }

    return INT_MAX;
}

struct window_node *view_find_window_node_in_direction(struct view *view, struct window_node *source, int direction)
{
    int window_count;
    uint32_t *window_list = space_window_list(view->sid, &window_count, false);
    if (!window_list) return NULL;

    int best_distance = INT_MAX;
    int best_rank = INT_MAX;
    struct window_node *best_node = NULL;
    CGPoint source_area_max = area_max_point(source->area);

    for (struct window_node *target = window_node_find_first_leaf(view->root); target; target = window_node_find_next_leaf(target)) {
        if (source == target) continue;

        CGPoint target_area_max = area_max_point(target->area);
        if (area_is_in_direction(&source->area, source_area_max, &target->area, target_area_max, direction)) {
            int distance = area_distance_in_direction(&source->area, source_area_max, &target->area, target_area_max, direction);
            int rank = window_manager_find_rank_of_window_in_list(target->window_order[0], window_list, window_count);
            if ((distance < best_distance) || (distance == best_distance && rank < best_rank)) {
                best_node = target;
                best_distance = distance;
                best_rank = rank;
            }
        }
    }

    return best_node;
}

struct window_node *view_find_window_node(struct view *view, uint32_t window_id)
{
    for (struct window_node *node = window_node_find_first_leaf(view->root); node; node = window_node_find_next_leaf(node)) {
        if (window_node_contains_window(node, window_id)) return node;
    }

    return NULL;
}

struct window_node *view_remove_window_node(struct view *view, struct window *window)
{
    bool was_single = view_has_single_managed_window(view);
    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    if (node->window_count > 1) {
        uint32_t prev_order_head = node->window_order[0];
        bool removed_entry = false;
        bool removed_order = false;

        for (int i = 0; i < node->window_count; ++i) {
            if (!removed_entry && node->window_list[i] == window->id) {
                memmove(node->window_list + i, node->window_list + i + 1, sizeof(uint32_t) * (node->window_count - i - 1));
                removed_entry = true;
            }

            if (!removed_order && node->window_order[i] == window->id) {
                memmove(node->window_order + i, node->window_order + i + 1, sizeof(uint32_t) * (node->window_count - i - 1));
                removed_order = true;
            }
        }

        assert(removed_entry);
        assert(removed_order);
        --node->window_count;
        insert_feedback_rekey(node, prev_order_head);

        if (view->insertion_point == window->id) {
            view->insertion_point = node->window_order[0];
        }

        if (view_reconcile_single_window_geometry(view, was_single)) return view->root;
        return NULL;
    }

    if (node == view->root) {
        view->insertion_point = 0;
        insert_feedback_destroy(node);
        memset(node, 0, sizeof(struct window_node));
        view_update(view);
        return NULL;
    }

    struct window_node *parent = node->parent;
    struct window_node *child  = window_node_is_right_child(node)
                               ? parent->left
                               : parent->right;


    memcpy(parent->window_list, child->window_list, sizeof(uint32_t) * child->window_count);
    memcpy(parent->window_order, child->window_order, sizeof(uint32_t) * child->window_count);
    parent->window_count = child->window_count;

    parent->left      = NULL;
    parent->right     = NULL;
    parent->zoom      = !g_space_manager.window_zoom_persist
                      ? NULL
                      : !child->zoom
                      ? NULL
                      : child->zoom == parent
                      ? parent->parent
                      : view->root;

    if (child->insert_dir) {
        insert_feedback_adopt(parent, child);
        parent->insert_dir      = child->insert_dir;
        parent->split           = child->split;
        parent->child           = child->child;
        insert_feedback_show(parent);
    }

    if (window_node_is_intermediate(child) && !window_node_is_leaf(child)) {
        parent->split         = child->split;
        parent->ratio         = child->ratio;
        parent->left          = child->left;
        parent->left->parent  = parent;
        parent->left->zoom    = !g_space_manager.window_zoom_persist
                              ? NULL
                              : !child->left->zoom
                              ? NULL
                              : child->left->zoom == child
                              ? parent
                              : view->root;

        parent->right         = child->right;
        parent->right->parent = parent;
        parent->right->zoom   = !g_space_manager.window_zoom_persist
                              ? NULL
                              : !child->right->zoom
                              ? NULL
                              : child->right->zoom == child
                              ? parent
                              : view->root;

        if (!g_space_manager.window_zoom_persist) {
            window_node_clear_zoom(parent);
        }

        window_node_update(view, parent);
    }

    insert_feedback_destroy(node);
    legacy_allocation_free(child);
    legacy_allocation_free(node);

    bool was_dirty = view_is_dirty(view);
    if (view->auto_balance != SPLIT_NONE) {
        window_node_balance(view->root, view->auto_balance);
    }

    // A cached root area can predate a change to usable display bounds. Refresh
    // the complete tree even without auto-balance so surviving windows do not
    // retain stale Dock, menu-bar, or external-bar geometry. view_update keeps
    // the user's split ratios intact.
    view_update(view);
    if (!was_dirty) view_clear_flag(view, VIEW_IS_DIRTY);
    return view->root;
}

bool view_stack_window_node(struct window_node *node, struct window *window)
{
    if (node->window_count >= NODE_MAX_WINDOW_COUNT) return false;

    uint32_t prev_order_head = node->window_order[0];
    int insert_index = node->window_count;

    for (int i = 0; i < node->window_count; ++i) {
        if (node->window_list[i] == node->window_order[0]) {
            insert_index = i+1;
            break;
        }
    }

    if (insert_index < node->window_count) {
        memmove(node->window_list + insert_index + 1, node->window_list + insert_index, sizeof(uint32_t) * (node->window_count - insert_index));
    }

    node->window_list[insert_index] = window->id;
    memmove(node->window_order + 1, node->window_order, sizeof(uint32_t) * node->window_count);
    node->window_order[0] = window->id;
    ++node->window_count;
    insert_feedback_rekey(node, prev_order_head);

    return true;
}

void view_abort_window_node_insertion(struct view_insertion_plan *plan)
{
    if (!plan) return;
    legacy_allocation_free(plan->left);
    legacy_allocation_free(plan->right);
    *plan = (struct view_insertion_plan) {0};
}

bool view_prepare_window_node_insertion(struct view *view, struct window *window, uint32_t insertion_point, struct view_insertion_plan *plan)
{
    if (!view || !view->root || !window || !plan) return false;
    *plan = (struct view_insertion_plan) {0};

    bool empty_root = !window_node_is_occupied(view->root) && window_node_is_leaf(view->root);
    if (view_layout_is_single_root(view->layout)) {
        return empty_root || view->root->window_count < NODE_MAX_WINDOW_COUNT;
    }
    if (view->layout != VIEW_BSP) return false;
    if (empty_root) return true;

    uint32_t target = insertion_point ? insertion_point : view->insertion_point;
    struct window_node *leaf = target ? view_find_window_node(view, target) : NULL;
    if (leaf && leaf->insert_dir == STACK) return leaf->window_count < NODE_MAX_WINDOW_COUNT;

    plan->left = legacy_allocation_calloc(1, sizeof(struct window_node));
    if (!plan->left) return false;
    plan->right = legacy_allocation_calloc(1, sizeof(struct window_node));
    if (!plan->right) {
        view_abort_window_node_insertion(plan);
        return false;
    }
    return true;
}

struct window_node *view_commit_window_node_insertion(struct view *view, struct window *window, uint32_t insertion_point, struct view_insertion_plan *plan)
{
    if (!view || !window || !plan) return NULL;
    bool was_single = view_has_single_managed_window(view);
    if (!window_node_is_occupied(view->root) &&
        window_node_is_leaf(view->root)) {
        view->root->window_list[0] = window->id;
        view->root->window_order[0] = window->id;
        view->root->window_count = 1;
        view_reconcile_single_window_geometry(view, was_single);
        return view->root;
    } else if (view->layout == VIEW_BSP) {
        uint32_t prev_insertion_point = 0;
        struct window_node *leaf = NULL;

        if (insertion_point) {
            prev_insertion_point = view->insertion_point;
            view->insertion_point = insertion_point;
        }

        if (view->insertion_point) {
            leaf = view_find_window_node(view, view->insertion_point);
            view->insertion_point = prev_insertion_point;

            if (leaf) {
                bool do_stack = leaf->insert_dir == STACK;

                if (do_stack) {
                    if (!view_stack_window_node(leaf, window)) return NULL;

                    leaf->insert_dir = 0;
                    insert_feedback_destroy(leaf);
                    view_reconcile_single_window_geometry(view, was_single);
                    return leaf;
                }

            }
        }

        if (!leaf) {
            if (g_space_manager.window_insertion_point == INSERT_FOCUSED) {
                leaf = view_find_window_node(view, g_window_manager.focused_window_id);
            } else if (g_space_manager.window_insertion_point == INSERT_FIRST) {
                leaf = window_node_find_first_leaf(view->root);
            } else if (g_space_manager.window_insertion_point == INSERT_LAST) {
                leaf = window_node_find_last_leaf(view->root);
            }

            if (!leaf) leaf = view_find_min_depth_leaf_node(view->root);
        }

        if (!plan->left || !plan->right) return NULL;
        struct window_node *left = plan->left;
        struct window_node *right = plan->right;
        plan->left = plan->right = NULL;
        if (!window_node_split_prepared(view, leaf, window, left, right)) {
            legacy_allocation_free(left);
            legacy_allocation_free(right);
            return NULL;
        }
        if (leaf->insert_dir) {
            leaf->insert_dir = 0;
            insert_feedback_destroy(leaf);
        }

        if (view->auto_balance != SPLIT_NONE) {
            window_node_balance(view->root, view->auto_balance);
            view_update(view);
            return view->root;
        }

        view_reconcile_single_window_geometry(view, was_single);
        return leaf;
    } else if (view_layout_is_single_root(view->layout)) {
        if (!view_stack_window_node(view->root, window)) return NULL;
        view_reconcile_single_window_geometry(view, was_single);
        return view->root;
    }

    return NULL;
}

struct window_node *view_add_window_node_with_insertion_point(struct view *view, struct window *window, uint32_t insertion_point)
{
    struct view_insertion_plan plan;
    if (!view_prepare_window_node_insertion(view, window, insertion_point, &plan)) return NULL;
    struct window_node *node = view_commit_window_node_insertion(view, window, insertion_point, &plan);
    view_abort_window_node_insertion(&plan);
    return node;
}

struct window_node *view_add_window_node(struct view *view, struct window *window)
{
    return view_add_window_node_with_insertion_point(view, window, 0);
}

uint32_t *view_find_window_list(struct view *view, int *window_count)
{
    *window_count = 0;

    int capacity = 13;
    uint32_t *window_list = ts_alloc_list(uint32_t, capacity);

    for (struct window_node *node = window_node_find_first_leaf(view->root); node; node = window_node_find_next_leaf(node)) {
        while (*window_count + node->window_count > capacity) {
            window_list = ts_expand(window_list, sizeof(uint32_t) * capacity, sizeof(uint32_t) * capacity);
            capacity *= 2;
        }

        for (int i = 0; i < node->window_count; ++i) {
            window_list[(*window_count)++] = node->window_list[i];
        }
    }

    return window_list;
}

bool view_is_invalid(struct view *view)
{
    return !view_check_flag(view, VIEW_IS_VALID);
}

bool view_is_dirty(struct view *view)
{
    return view_check_flag(view, VIEW_IS_DIRTY);
}

#ifdef TESTS
void view_test_set_platform_ops(const struct view_test_platform_ops *ops)
{
    view_test_platform_ops = ops ? *ops : (struct view_test_platform_ops) { 0 };
}
#endif

bool view_space_is_visible(uint64_t sid)
{
#ifdef TESTS
    if (view_test_platform_ops.space_is_visible) {
        return view_test_platform_ops.space_is_visible(sid, view_test_platform_ops.context);
    }
#endif
    return space_is_visible(sid);
}

void view_flush(struct view *view)
{
    if (view_space_is_visible(view->sid)) {
#ifdef TESTS
        if (view_test_platform_ops.flush_windows) {
            view_test_platform_ops.flush_windows(view->root, view_test_platform_ops.context);
        } else
#endif
        window_node_flush(view->root);
        view_clear_flag(view, VIEW_IS_DIRTY);
    } else {
        view_set_flag(view, VIEW_IS_DIRTY);
    }
}

void view_serialize(FILE *rsp, struct view *view, uint64_t flags)
{
    TIME_FUNCTION;

    if (flags == 0x0) flags |= ~flags;

    bool did_output = false;
    fprintf(rsp, "{\n");

    if (flags & SPACE_PROPERTY_ID) {
        fprintf(rsp, "\t\"id\":%lld", view->sid);
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_UUID) {
        if (did_output) fprintf(rsp, ",\n");

        char *uuid = ts_cfstring_copy(view->uuid);
        fprintf(rsp, "\t\"uuid\":\"%s\"", uuid ? uuid : "<unknown>");
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_INDEX) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"index\":%d", space_manager_mission_control_index(view->sid));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_LABEL) {
        if (did_output) fprintf(rsp, ",\n");

        struct space_label *space_label = space_manager_get_label_for_space(&g_space_manager, view->sid);
        char *label = space_label ? space_label->label : NULL;
        char *escaped_label = label ? ts_string_escape(label) : NULL;
        fprintf(rsp, "\t\"label\":\"%s\"", escaped_label ? escaped_label : label ? label : "");
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_TYPE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"type\":\"%s\"", view_type_str[view->layout]);
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_DISPLAY) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"display\":%d", display_manager_display_id_arrangement(space_display_id(view->sid)));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_WINDOWS) {
        if (did_output) fprintf(rsp, ",\n");

        int window_count = 0;
        uint32_t *window_list = space_window_list(view->sid, &window_count, true);

        fprintf(rsp, "\t\"windows\":[");
        for (int i = 0; i < window_count; ++i) {
            if (i < window_count - 1) {
                fprintf(rsp, "%d, ", window_list[i]);
            } else {
                fprintf(rsp, "%d", window_list[i]);
            }
        }
        fprintf(rsp, "]");
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_FIRST_WINDOW) {
        if (did_output) fprintf(rsp, ",\n");

        struct window_node *first_leaf = window_node_find_first_leaf(view->root);
        fprintf(rsp, "\t\"first-window\":%d", first_leaf ? first_leaf->window_order[0] : 0);
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_LAST_WINDOW) {
        if (did_output) fprintf(rsp, ",\n");

        struct window_node *last_leaf = window_node_find_last_leaf(view->root);
        fprintf(rsp, "\t\"last-window\":%d", last_leaf ? last_leaf->window_order[0] : 0);
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_HAS_FOCUS) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-focus\":%s", json_bool(view->sid == g_space_manager.current_space_id));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_IS_VISIBLE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-visible\":%s", json_bool(space_is_visible(view->sid)));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_IS_FULLSCREEN) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-native-fullscreen\":%s", json_bool(space_is_fullscreen(view->sid)));
    }

    fprintf(rsp, "\n}");
}

static void window_node_clear_window_areas(struct window_node *node)
{
    node->has_window_areas = false;
    if (node->left) window_node_clear_window_areas(node->left);
    if (node->right) window_node_clear_window_areas(node->right);
}

static void view_update_accordion(struct view *view)
{
    struct window_node *root = view->root;
    int count = root->window_count;
    root->has_window_areas = count > 0;
    if (count <= 0) return;

    bool horizontal = view->layout == VIEW_ACCORDION_HORIZONTAL;
    double axis = horizontal ? root->area.w : root->area.h;
    if (!isfinite(axis) || axis < 0.0) axis = 0.0;

    if (count == 1) {
        root->window_area[0] = root->area;
        if (horizontal) {
            root->window_area[0].w = (float) axis;
        } else {
            root->window_area[0].h = (float) axis;
        }
        return;
    }
    double request = view->accordion_padding;
    if (!isfinite(request) || request < 0.0) request = 0.0;
    double strip = fmin(request, axis / (double) count);
    double computed_length = axis - strip * (double) (count - 1);
    float frame_length = (float) fmax(0.0, computed_length);
    if (!isfinite(frame_length) || frame_length < 0.0f) frame_length = 0.0f;

    double origin = horizontal ? root->area.x : root->area.y;
    double max_offset = fmax(0.0, axis - (double) frame_length);
    double max_origin = origin + max_offset;
    for (int i = 0; i < count; ++i) {
        double local_offset = fmin(strip * (double) i, max_offset);
        double computed_origin = origin + local_offset;
        float frame_origin = computed_origin > FLT_MAX ? FLT_MAX :
                             computed_origin < -FLT_MAX ? -FLT_MAX : (float) computed_origin;
        if ((double) frame_origin > max_origin) frame_origin = nextafterf(frame_origin, -INFINITY);
        if ((double) frame_origin < origin) frame_origin = nextafterf(frame_origin, INFINITY);

        struct area area = root->area;
        if (horizontal) {
            area.x = frame_origin;
            area.w = frame_length;
        } else {
            area.y = frame_origin;
            area.h = frame_length;
        }
        root->window_area[i] = area;
    }
}

void view_update(struct view *view)
{
#ifdef TESTS
    if (view_test_platform_ops.update_windows) {
        view_test_platform_ops.update_windows(view, view_test_platform_ops.context);
    } else {
#endif
        uint32_t did = space_display_id(view->sid);
        CGRect frame = display_bounds_constrained(did, false);
        view->root->area = area_from_cgrect(frame);

        if (view_check_flag(view, VIEW_ENABLE_PADDING)) {
            view->root->area.x += view->left_padding;
            view->root->area.w -= view_padding_sum(view->left_padding, view->right_padding);
            view->root->area.y += view->top_padding;
            view->root->area.h -= view_padding_sum(view->top_padding, view->bottom_padding);
        }

        view->root->area.w = fmaxf(0.0f, view->root->area.w);
        view->root->area.h = fmaxf(0.0f, view->root->area.h);

        if (view_layout_is_accordion(view->layout)) {
            float min_x = frame.origin.x, max_x = CGRectGetMaxX(frame);
            float min_y = frame.origin.y, max_y = CGRectGetMaxY(frame);
            float x = fminf(max_x, fmaxf(min_x, view->root->area.x));
            float y = fminf(max_y, fmaxf(min_y, view->root->area.y));
            float right = fminf(max_x, fmaxf(min_x, view->root->area.x + view->root->area.w));
            float bottom = fminf(max_y, fmaxf(min_y, view->root->area.y + view->root->area.h));
            view->root->area = (struct area) { x, y, fmaxf(0.0f, right - x), fmaxf(0.0f, bottom - y) };
        }

        view_apply_single_window_width(view);

        window_node_clear_window_areas(view->root);
        if (view_layout_is_accordion(view->layout)) {
            view_update_accordion(view);
        } else {
            window_node_update(view, view->root);
        }
#ifdef TESTS
    }
#endif
    view_set_flag(view, VIEW_IS_VALID);
    view_set_flag(view, VIEW_IS_DIRTY);
}

struct view *view_create(uint64_t sid)
{
    struct view *view = legacy_allocation_calloc(1, sizeof(struct view));
    if (!view) return NULL;

    view->root = legacy_allocation_calloc(1, sizeof(struct window_node));
    if (!view->root) {
        legacy_allocation_free(view);
        return NULL;
    }

    view->sid = sid;
#ifdef TESTS
    view->uuid = view_test_platform_ops.copy_space_uuid
        ? view_test_platform_ops.copy_space_uuid(sid, view_test_platform_ops.context)
        : SLSSpaceCopyName(g_connection, sid);
#else
    view->uuid = SLSSpaceCopyName(g_connection, sid);
#endif

    view_set_flag(view, VIEW_ENABLE_PADDING);
    view_set_flag(view, VIEW_ENABLE_GAP);

#ifdef TESTS
    bool is_user_space = view_test_platform_ops.space_is_user
        ? view_test_platform_ops.space_is_user(sid, view_test_platform_ops.context)
        : space_is_user(view->sid);
#else
    bool is_user_space = space_is_user(view->sid);
#endif
    if (is_user_space) {
        if (!view_check_flag(view, VIEW_LAYOUT))         view->layout         = g_space_manager.layout;
        if (!view_check_flag(view, VIEW_TOP_PADDING))    view->top_padding    = g_space_manager.top_padding;
        if (!view_check_flag(view, VIEW_BOTTOM_PADDING)) view->bottom_padding = g_space_manager.bottom_padding;
        if (!view_check_flag(view, VIEW_LEFT_PADDING))   view->left_padding   = g_space_manager.left_padding;
        if (!view_check_flag(view, VIEW_RIGHT_PADDING))  view->right_padding  = g_space_manager.right_padding;
        if (!view_check_flag(view, VIEW_WINDOW_GAP))     view->window_gap     = g_space_manager.window_gap;
        if (!view_check_flag(view, VIEW_ACCORDION_PADDING)) view->accordion_padding = g_space_manager.accordion_padding;
        if (!view_check_flag(view, VIEW_SINGLE_WINDOW_MAX_WIDTH)) view->single_window_max_width = g_space_manager.single_window_max_width;
        if (!view_check_flag(view, VIEW_SINGLE_WINDOW_WIDTH_RATIO)) view->single_window_width_ratio = g_space_manager.single_window_width_ratio;
        if (!view_check_flag(view, VIEW_AUTO_BALANCE))   view->auto_balance   = g_space_manager.auto_balance;
        if (!view_check_flag(view, VIEW_SPLIT_RATIO))    view->split_ratio    = g_space_manager.split_ratio;
        if (!view_check_flag(view, VIEW_SPLIT_TYPE))     view->split_type     = g_space_manager.split_type;
        view_update(view);
    } else {
        view->layout = VIEW_FLOAT;
    }

    return view;
}

void view_clear(struct view *view)
{
    if (view->root) {
        if (view->root->left)  window_node_destroy(view->root->left);
        if (view->root->right) window_node_destroy(view->root->right);

        for (int i = 0; i < view->root->window_count; ++i) {
            window_manager_remove_managed_window(&g_window_manager, view->root->window_list[i]);
        }

        insert_feedback_destroy(view->root);
        memset(view->root, 0, sizeof(struct window_node));
        view_update(view);
    }
}

void view_destroy(struct view *view)
{
    if (view->root) {
        window_node_destroy(view->root);
    }

    if (view->uuid) {
        CFRelease(view->uuid);
    }
}
