extern struct window_manager g_window_manager;
extern int g_connection;

#ifdef TESTS
static uint32_t *(*space_manager_test_active_display_list)(int *count);
static uint64_t *(*space_manager_test_display_space_list)(uint32_t did, int *count);
static uint64_t *(*space_manager_test_query_window_space_list)(uint32_t wid, int *count, bool *success) =
    window_space_list_with_status;
static uint32_t *(*space_manager_test_layout_window_list)(uint64_t *, int, int, int *, bool, bool *);
static uint32_t *(*space_manager_test_swap_window_list)(uint64_t *, int, int, int *, bool, bool *);
static bool (*space_manager_test_layout_space_is_user)(uint64_t);
static size_t space_manager_test_lookup_count;
static size_t space_manager_layout_allocation_ordinal;
static size_t space_manager_layout_fail_ordinal;
static size_t space_manager_layout_outstanding;
#endif

static void *space_manager_layout_allocate(size_t count, size_t size)
{
    if (count && size > SIZE_MAX / count) return NULL;
#ifdef TESTS
    ++space_manager_layout_allocation_ordinal;
    if (space_manager_layout_fail_ordinal &&
        space_manager_layout_allocation_ordinal == space_manager_layout_fail_ordinal) return NULL;
#endif
    void *result = calloc(count, size);
#ifdef TESTS
    if (result) ++space_manager_layout_outstanding;
#endif
    return result;
}

static void space_manager_layout_free(void *pointer)
{
    if (!pointer) return;
    free(pointer);
#ifdef TESTS
    --space_manager_layout_outstanding;
#endif
}

static TABLE_HASH_FUNC(hash_view)
{
    return *(uint64_t *) key;
}

static TABLE_COMPARE_FUNC(compare_view)
{
    return *(uint64_t *) key_a == *(uint64_t *) key_b;
}

bool space_manager_query_space(FILE *rsp, uint64_t sid, uint64_t flags)
{
    TIME_FUNCTION;

    struct view *view = space_manager_query_view(&g_space_manager, sid);
    if (!view) return false;

    view_serialize(rsp, view, flags);
    fprintf(rsp, "\n");
    return true;
}

bool space_manager_query_spaces_for_window(FILE *rsp, struct window *window, uint64_t flags)
{
    TIME_FUNCTION;

    int space_count;
    bool query_succeeded = false;
#ifdef TESTS
    uint64_t *space_list = space_manager_test_query_window_space_list(window->id, &space_count, &query_succeeded);
#else
    uint64_t *space_list = window_space_list_with_status(window->id, &space_count, &query_succeeded);
#endif
    if (!query_succeeded) return false;

    fprintf(rsp, "[");
    bool first = true;
    for (int i = 0; i < space_count; ++i) {
        struct view *view = space_manager_query_view(&g_space_manager, space_list[i]);
        if (!view) continue;

        if (!first) fprintf(rsp, ",");
        view_serialize(rsp, view, flags);
        first = false;
    }
    fprintf(rsp, "]\n");

    return true;
}

bool space_manager_query_spaces_for_display(FILE *rsp, uint32_t did, uint64_t flags)
{
    TIME_FUNCTION;

    int space_count;
    uint64_t *space_list = display_space_list(did, &space_count);
    if (!space_list) return false;

    fprintf(rsp, "[");
    bool first = true;
    for (int i = 0; i < space_count; ++i) {
        struct view *view = space_manager_query_view(&g_space_manager, space_list[i]);
        if (!view) continue;

        if (!first) fprintf(rsp, ",");
        view_serialize(rsp, view, flags);
        first = false;
    }
    fprintf(rsp, "]\n");

    return true;
}

bool space_manager_query_spaces_for_displays(FILE *rsp, uint64_t flags)
{
    TIME_FUNCTION;

    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return false;

    int total_space_count = 0;
    uint64_t *all_spaces = NULL;
    for (int i = 0; i < display_count; ++i) {
        int space_count;
        uint64_t *space_list = display_space_list(display_list[i], &space_count);
        if (!space_list) return false;
        if (!all_spaces) all_spaces = space_list;
        total_space_count += space_count;
    }

    fprintf(rsp, "[");
    bool first = true;
    for (int i = 0; i < total_space_count; ++i) {
        struct view *view = space_manager_query_view(&g_space_manager, all_spaces[i]);
        if (!view) continue;

        if (!first) fprintf(rsp, ",");
        view_serialize(rsp, view, flags);
        first = false;
    }
    fprintf(rsp, "]\n");

    return true;
}

struct view *space_manager_lookup_view(struct space_manager *sm, uint64_t sid)
{
#ifdef TESTS
    ++space_manager_test_lookup_count;
#endif
    if (!sm || !sid) return NULL;
    return table_find(&sm->view, &sid);
}

struct view *space_manager_query_view(struct space_manager *sm, uint64_t sid)
{
    return space_manager_lookup_view(sm, sid);
}

struct view *space_manager_find_view(struct space_manager *sm, uint64_t sid)
{
    struct view *view = space_manager_lookup_view(sm, sid);
    if (!view) {
        view = view_create(sid);
        if (!view) return NULL;
        if (!table_try_add(&sm->view, &sid, view)) {
            view_destroy(view);
            legacy_allocation_free(view);
            return NULL;
        }
    }
    return view;
}

void space_manager_refresh_view(struct space_manager *sm, uint64_t sid)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout == VIEW_FLOAT) return;

    view_update(view);
    view_flush(view);
}

void space_manager_mark_view_invalid(struct space_manager *sm,  uint64_t sid)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout == VIEW_FLOAT) return;

    view_clear_flag(view, VIEW_IS_VALID);
}

void space_manager_untile_window(struct view *view, struct window *window)
{
    if (!view || view->layout == VIEW_FLOAT) return;

    window_manager_adjust_layer(window, LAYER_NORMAL);
    struct window_node *node = view_remove_window_node(view, window);
    if (view_layout_is_accordion(view->layout)) {
        view_update(view);
        view_flush(view);
        return;
    }
    if (!node) return;

    if (space_is_visible(view->sid)) {
        window_node_flush(node);
    } else {
        view_set_flag(view, VIEW_IS_DIRTY);
    }
}

struct space_label *space_manager_get_label_for_space(struct space_manager *sm, uint64_t sid)
{
    for (int i = 0; i < buf_len(sm->labels); ++i) {
        struct space_label *space_label = &sm->labels[i];
        if (space_label->sid == sid) {
            return space_label;
        }
    }

    return NULL;
}

struct space_label *space_manager_get_space_for_label(struct space_manager *sm, char *label)
{
    for (int i = 0; i < buf_len(sm->labels); ++i) {
        struct space_label *space_label = &sm->labels[i];
        if (string_equals(label, space_label->label)) {
            return space_label;
        }
    }

    return NULL;
}

bool space_manager_remove_label_for_space(struct space_manager *sm, uint64_t sid)
{
    for (int i = 0; i < buf_len(sm->labels); ++i) {
        struct space_label *space_label = &sm->labels[i];
        if (space_label->sid == sid) {
            free(space_label->label);
            buf_del(sm->labels, i);
            return true;
        }
    }

    return false;
}

void space_manager_set_label_for_space(struct space_manager *sm, uint64_t sid, char *label)
{
    space_manager_remove_label_for_space(sm, sid);

    for (int i = 0; i < buf_len(sm->labels); ++i) {
        struct space_label *space_label = &sm->labels[i];
        if (string_equals(space_label->label, label)) {
            free(space_label->label);
            buf_del(sm->labels, i);
            break;
        }
    }

    buf_push(sm->labels, ((struct space_label) {
        .sid   = sid,
        .label = label
    }));
}

static bool space_manager_layout_inventory_is_valid(struct window_manager *wm, enum view_type layout, uint32_t *window_list, int window_count, bool inventory_succeeded)
{
    if (layout == VIEW_FLOAT) return true;
    if (!inventory_succeeded || window_count < 0 || window_count == INT_MAX || (window_count && !window_list)) return false;
    if (view_layout_is_single_root(layout)) {
        int manageable_count = 0;
        for (int i = 0; i < window_count; ++i) {
            struct window *window = window_manager_find_window(wm, window_list[i]);
            if (window && window_manager_should_manage_window(window) && ++manageable_count > NODE_MAX_WINDOW_COUNT) return false;
        }
    }
    return true;
}

static void space_manager_commit_layout_with_inventory(struct window_manager *wm, struct view *view, enum view_type layout, uint32_t *window_list, int window_count, struct view **mutated_views, int mutated_capacity)
{
    if (view->scaffold_fingerprint || view->scaffold_primary[0] ||
        view->scaffold_primary[1] || view->scaffold_primary[2]) {
        view->scaffold_fingerprint = 0;
        memset(view->scaffold_primary, 0, sizeof(view->scaffold_primary));
    }
    view->layout = layout;
    view_clear(view);
    if (layout != VIEW_FLOAT) {
        window_manager_reconcile_windows_on_space_with_storage(wm, view, window_list, window_count, true, mutated_views, mutated_capacity);
    }
}

bool space_manager_apply_layout_with_inventory(struct window_manager *wm, struct view *view, enum view_type layout, uint32_t *window_list, int window_count, bool inventory_succeeded)
{
    if (!space_manager_layout_inventory_is_valid(wm, layout, window_list, window_count, inventory_succeeded)) return false;
    if (layout == VIEW_FLOAT) {
        space_manager_commit_layout_with_inventory(wm, view, layout, NULL, 0, NULL, 0);
        return true;
    }

    int capacity = window_count + 1;
    struct view **mutated_views = calloc((size_t) capacity, sizeof(struct view *));
    if (!mutated_views) return false;
    space_manager_commit_layout_with_inventory(wm, view, layout, window_list, window_count, mutated_views, capacity);
    free(mutated_views);
    return true;
}

static void space_manager_layout_synthetic_uuid(char *buffer, size_t capacity, uint64_t sid)
{
    snprintf(buffer, capacity, "imperative-space-%016llx", (unsigned long long) sid);
}

struct space_manager_layout_capture {
    const struct declarative_space_target *targets;
    size_t count;
    enum view_type layout;
};

static enum declarative_space_query_result space_manager_layout_query_inventory(
    uint64_t sid, const uint32_t **wids, size_t *count, void *opaque)
{
    struct space_manager_layout_capture *capture = opaque;
    const struct declarative_space_target *target = NULL;
    for (size_t i = 0; i < capture->count; ++i) {
        if (capture->targets[i].sid == sid) { target = &capture->targets[i]; break; }
    }
    if (!target) return DECLARATIVE_SPACE_QUERY_TERMINAL;
    int window_count = 0;
    uint32_t *window_list = NULL;
    if (target->imperative_layout_target) {
        if (capture->layout != VIEW_FLOAT) {
            bool succeeded = false;
#ifdef TESTS
            uint32_t *(*inventory)(uint64_t *, int, int, int *, bool, bool *) =
                space_manager_test_layout_window_list ? space_manager_test_layout_window_list
                                                      : space_window_list_for_connection_with_status;
            window_list = inventory(&sid, 1, 0, &window_count, false, &succeeded);
#else
            window_list = space_window_list_for_connection_with_status(&sid, 1, 0, &window_count,
                                                                        false, &succeeded);
#endif
            if (!succeeded || window_count < 0 || (window_count && !window_list)) {
                return DECLARATIVE_SPACE_QUERY_TERMINAL;
            }
        }
    } else {
        window_list = view_find_window_list(target->view, &window_count);
        if (!window_list || window_count < 0) return DECLARATIVE_SPACE_QUERY_RESOURCE;
    }
    *wids = window_list;
    *count = (size_t) window_count;
    return DECLARATIVE_SPACE_QUERY_OK;
}

static bool space_manager_layout_query_inventory_bool(uint64_t sid, const uint32_t **wids,
                                                       size_t *count, void *context)
{
    return space_manager_layout_query_inventory(sid, wids, count, context) == DECLARATIVE_SPACE_QUERY_OK;
}

static struct window *space_manager_layout_find_window(uint32_t wid, void *context)
{
    (void) context;
    return window_manager_find_window(&g_window_manager, wid);
}

static bool space_manager_layout_window_is_eligible(struct window *window, void *context)
{
    (void) context;
    return window_manager_should_manage_window(window);
}

struct space_manager_layout_publication {
    struct space_manager *manager;
    enum view_type layout;
};

static void space_manager_layout_publish(void *opaque)
{
    struct space_manager_layout_publication *publication = opaque;
    publication->manager->layout = publication->layout;
}

static bool space_manager_apply_layout_transaction(struct space_manager *sm, struct view *selected,
                                                   enum view_type layout, bool global)
{
    if (!sm || layout < VIEW_BSP || layout > VIEW_ACCORDION_VERTICAL) return false;
    size_t count = (size_t) sm->view.count;
    if (!count) {
        if (global) sm->layout = layout;
        return true;
    }
#ifdef TESTS
    space_manager_layout_allocation_ordinal = 0;
#endif
    struct declarative_space_target *targets = space_manager_layout_allocate(count, sizeof(*targets));
    struct declarative_space_topology_entry *topology = space_manager_layout_allocate(count, sizeof(*topology));
    char (*uuids)[DECLARATIVE_SPACE_IDENTITY_STRING_CAPACITY] =
        space_manager_layout_allocate(count, sizeof(*uuids));
    if (!targets || !topology || !uuids) {
        space_manager_layout_free(targets);
        space_manager_layout_free(topology);
        space_manager_layout_free(uuids);
        return false;
    }
    bool valid = true;
    size_t at = 0;
    table_for (struct view *view, sm->view, {
        if (!valid) continue;
#ifdef TESTS
        bool is_user_space = space_manager_test_layout_space_is_user
                           ? space_manager_test_layout_space_is_user(view->sid)
                           : declarative_space_identity_valid(&view->declarative_identity)
                           ? view->declarative_identity.kind == 0
                           : space_is_user(view->sid);
#else
        bool is_user_space = space_is_user(view->sid);
#endif
        if (view->uuid) {
            if (!CFStringGetCString(view->uuid, uuids[at], sizeof(uuids[at]), kCFStringEncodingUTF8) ||
                !uuids[at][0]) {
                valid = false;
                continue;
            }
        } else {
            space_manager_layout_synthetic_uuid(uuids[at], sizeof(uuids[at]), view->sid);
        }
        struct declarative_space_identity identity = ((struct declarative_space_identity) {
            .has_uuid = true, .sid = view->sid, .kind = is_user_space ? 0 : 2,
            .mission_control_ordinal = (int) at
        });
        memcpy(identity.uuid, uuids[at], strlen(uuids[at]) + 1);
        identity.fingerprint = declarative_space_identity_fingerprint(&identity);
        bool affected = global ? (!view_check_flag(view, VIEW_LAYOUT) && is_user_space)
                               : view == selected;
        targets[at] = ((struct declarative_space_target) {
            .view = view, .sid = view->sid, .index = identity.mission_control_ordinal + 1,
            .uuid = uuids[at], .is_user_space = identity.kind == 0, .identity = identity,
            .imperative_layout_target = affected
        });
        topology[at] = ((struct declarative_space_topology_entry) {
            .sid = view->sid, .index = identity.mission_control_ordinal + 1,
            .uuid = uuids[at], .is_user_space = identity.kind == 0, .identity = identity
        });
        ++at;
    })
    if (!valid || at != count) {
        space_manager_layout_free(targets);
        space_manager_layout_free(topology);
        space_manager_layout_free(uuids);
        return false;
    }
    struct declarative_space_defaults defaults = {
        .layout = sm->layout >= VIEW_BSP ? sm->layout : VIEW_BSP,
        .top_padding = sm->top_padding, .bottom_padding = sm->bottom_padding,
        .left_padding = sm->left_padding, .right_padding = sm->right_padding,
        .window_gap = sm->window_gap, .accordion_padding = sm->accordion_padding,
        .window_gap_enabled = true, .split_type = sm->split_type,
        .auto_balance = sm->auto_balance,
        .split_ratio = sm->split_ratio >= 0.1f && sm->split_ratio <= 0.9f ? sm->split_ratio : 0.5f,
        .window_placement = sm->window_placement == CHILD_FIRST ? CHILD_FIRST : CHILD_SECOND,
        .window_insertion_point = sm->window_insertion_point,
        .focused_window_id = g_window_manager.focused_window_id,
        .window_zoom_persist = sm->window_zoom_persist
    };
    struct declarative_space_table_contract view_contract = {
        .hash = sm->view.hash, .cmp = sm->view.cmp, .key_size = sizeof(uint64_t)
    };
    struct declarative_space_table_contract managed_contract = {
        .hash = g_window_manager.managed_window.hash, .cmp = g_window_manager.managed_window.cmp,
        .key_size = sizeof(uint32_t)
    };
    struct space_manager_layout_capture context = { .targets = targets, .count = count, .layout = layout };
    struct declarative_space_capture_ops capture = {
        .query_sid = space_manager_layout_query_inventory_bool,
        .query_sid_result = space_manager_layout_query_inventory,
        .find_window = space_manager_layout_find_window,
        .is_eligible = space_manager_layout_window_is_eligible,
        .context = &context
    };
    struct space_manager_layout_publication publication = { .manager = sm, .layout = layout };
    enum declarative_space_apply_result result = declarative_space_apply_imperative_layout(
        targets, count, topology, &sm->view, &view_contract, &managed_contract,
        &defaults, layout, &capture, global ? space_manager_layout_publish : NULL,
        global ? &publication : NULL);
    space_manager_layout_free(targets);
    space_manager_layout_free(topology);
    space_manager_layout_free(uuids);
    return result == DECLARATIVE_SPACE_APPLY_OK;
}

bool space_manager_set_layout_for_view(struct space_manager *sm, struct view *view, enum view_type layout)
{
    if (!sm || !view || table_find(&sm->view, &view->sid) != view) return false;
    return space_manager_apply_layout_transaction(sm, view, layout, false);
}

bool space_manager_set_layout_for_space(struct space_manager *sm, uint64_t sid, enum view_type layout)
{
    struct view *view = space_manager_lookup_view(sm, sid);
    return view && space_manager_set_layout_for_view(sm, view, layout);
}

bool space_manager_set_gap_for_space(struct space_manager *sm, uint64_t sid, int type, int gap)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout == VIEW_FLOAT) return false;

    if (type == TYPE_ABS) {
        view->window_gap = gap;
    } else if (type == TYPE_REL) {
        view->window_gap = add_and_clamp_to_zero(view->window_gap, gap);
    }

    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_toggle_gap_for_space(struct space_manager *sm, uint64_t sid)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout == VIEW_FLOAT) return false;

    if (view_check_flag(view, VIEW_ENABLE_GAP)) {
        view_clear_flag(view, VIEW_ENABLE_GAP);
    } else {
        view_set_flag(view, VIEW_ENABLE_GAP);
    }

    view_update(view);
    view_flush(view);

    return true;
}

void space_manager_toggle_mission_control(uint64_t sid)
{
    space_manager_focus_space(sid);
    CoreDockSendNotification(CFSTR("com.apple.expose.awake"), 0);
}

void space_manager_toggle_show_desktop(uint64_t sid)
{
    space_manager_focus_space(sid);
    CoreDockSendNotification(CFSTR("com.apple.showdesktop.awake"), 0);
}

bool space_manager_set_layout_for_all_spaces(struct space_manager *sm, enum view_type layout)
{
    return space_manager_apply_layout_transaction(sm, NULL, layout, true);
}

void space_manager_set_window_gap_for_all_spaces(struct space_manager *sm, int window_gap)
{
    sm->window_gap = window_gap;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_WINDOW_GAP)) {
            view->window_gap = window_gap;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_accordion_padding_for_all_spaces(struct space_manager *sm, int accordion_padding)
{
    sm->accordion_padding = accordion_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_ACCORDION_PADDING)) {
            view->accordion_padding = accordion_padding;
            if (view_layout_is_accordion(view->layout)) {
                view_update(view);
                view_flush(view);
            }
        }
    })
}

void space_manager_set_top_padding_for_all_spaces(struct space_manager *sm, int top_padding)
{
    sm->top_padding = top_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_TOP_PADDING)) {
            view->top_padding = top_padding;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_bottom_padding_for_all_spaces(struct space_manager *sm, int bottom_padding)
{
    sm->bottom_padding = bottom_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_BOTTOM_PADDING)) {
            view->bottom_padding = bottom_padding;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_left_padding_for_all_spaces(struct space_manager *sm, int left_padding)
{
    sm->left_padding = left_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_LEFT_PADDING)) {
            view->left_padding = left_padding;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_right_padding_for_all_spaces(struct space_manager *sm, int right_padding)
{
    sm->right_padding = right_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_RIGHT_PADDING)) {
            view->right_padding = right_padding;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_split_ratio_for_all_spaces(struct space_manager *sm, float split_ratio)
{
    sm->split_ratio = split_ratio;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_SPLIT_RATIO)) {
            view->split_ratio = split_ratio;
            if (view->scaffold_fingerprint) view->scaffold_fingerprint = 0;
        }
    })
}

void space_manager_set_split_type_for_all_spaces(struct space_manager *sm, enum window_node_split split_type)
{
    sm->split_type = split_type;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_SPLIT_TYPE)) {
            view->split_type = split_type;
        }
    })
}

void space_manager_set_auto_balance_for_all_spaces(struct space_manager *sm, uint32_t auto_balance)
{
    sm->auto_balance = auto_balance;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_AUTO_BALANCE)) {
            view->auto_balance = auto_balance;
        }
    })
}

bool space_manager_set_padding_for_space(struct space_manager *sm, uint64_t sid, int type, int top, int bottom, int left, int right)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout == VIEW_FLOAT) return false;

    if (type == TYPE_ABS) {
        view->top_padding    = top;
        view->bottom_padding = bottom;
        view->left_padding   = left;
        view->right_padding  = right;
    } else if (type == TYPE_REL) {
        view->top_padding    = add_and_clamp_to_zero(view->top_padding, top);
        view->bottom_padding = add_and_clamp_to_zero(view->bottom_padding, bottom);
        view->left_padding   = add_and_clamp_to_zero(view->left_padding, left);
        view->right_padding  = add_and_clamp_to_zero(view->right_padding, right);
    }

    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_toggle_padding_for_space(struct space_manager *sm, uint64_t sid)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout == VIEW_FLOAT) return false;

    if (view_check_flag(view, VIEW_ENABLE_PADDING)) {
        view_clear_flag(view, VIEW_ENABLE_PADDING);
    } else {
        view_set_flag(view, VIEW_ENABLE_PADDING);
    }

    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_rotate_space(struct space_manager *sm, uint64_t sid, int degrees)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout != VIEW_BSP) return false;

    window_node_rotate(view->root, degrees);
    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_mirror_space(struct space_manager *sm, uint64_t sid, enum window_node_split axis)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout != VIEW_BSP) return false;

    window_node_mirror(view->root, axis);
    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_equalize_space(struct space_manager *sm, uint64_t sid, uint32_t axis_flag)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout != VIEW_BSP) return false;

    window_node_equalize(view->root, axis_flag, view->split_ratio);
    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_balance_space(struct space_manager *sm, uint64_t sid, uint32_t axis_flag)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout != VIEW_BSP) return false;

    window_node_balance(view->root, axis_flag);
    view_update(view);
    view_flush(view);

    return true;
}

struct view *space_manager_tile_window_on_space_with_insertion_point(struct space_manager *sm, struct window *window, uint64_t sid, uint32_t insertion_point)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout == VIEW_FLOAT) return view;

    window_manager_adjust_layer(window, LAYER_BELOW);
    struct window_node *node = view_add_window_node_with_insertion_point(view, window, insertion_point);
    if (!node) {
        window_manager_adjust_layer(window, LAYER_NORMAL);
        return NULL;
    }

    return view;
}

struct view *space_manager_tile_window_on_space(struct space_manager *sm, struct window *window, uint64_t sid)
{
    return space_manager_tile_window_on_space_with_insertion_point(sm, window, sid, 0);
}

void space_manager_flush_tiled_window(struct view *view, struct window *window)
{
    if (!view || !window || view->layout == VIEW_FLOAT) return;
    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return;
    if (view_layout_is_accordion(view->layout)) {
        view_update(view);
        view_flush(view);
    } else if (view_space_is_visible(view->sid)) {
        window_node_flush(node);
    } else {
        view_set_flag(view, VIEW_IS_DIRTY);
    }
}

void space_manager_toggle_window_split(struct space_manager *sm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, window_space(window->id));
    if (!view || view->layout != VIEW_BSP) return;

    struct window_node *node = view_find_window_node(view, window->id);
    if (node && window_node_is_intermediate(node)) {
        node->parent->split = node->parent->split == SPLIT_Y ? SPLIT_X : SPLIT_Y;

        if (view->auto_balance != SPLIT_NONE) {
            window_node_balance(view->root, view->auto_balance);
            view_update(view);
            view_flush(view);
        } else {
            window_node_update(view, node->parent);
            if (space_is_visible(view->sid)) {
                window_node_flush(node->parent);
            } else {
                view_set_flag(view, VIEW_IS_DIRTY);
            }
        }
    }
}

int space_manager_mission_control_index(uint64_t sid)
{
    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (!topology) return 0;
    int result = managed_space_index(topology, sid);
    CFRelease(topology);
    return result;
}

uint64_t space_manager_mission_control_space(int desktop_id)
{
    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (!topology) return 0;
    uint64_t result = managed_space_select(topology, desktop_id);
    CFRelease(topology);
    return result;
}

uint64_t space_manager_cursor_space(void)
{
    uint32_t did = display_manager_cursor_display_id();
    return display_space_id(did);
}

uint64_t space_manager_prev_space(uint64_t sid)
{
    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (!topology) return 0;
    uint64_t result = managed_space_previous(topology, sid);
    CFRelease(topology);
    return result;
}

uint64_t space_manager_next_space(uint64_t sid)
{
    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (!topology) return 0;
    uint64_t result = managed_space_next(topology, sid);
    CFRelease(topology);
    return result;
}

uint64_t space_manager_first_space(void)
{
    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (!topology) return 0;
    uint64_t result = managed_space_first(topology);
    CFRelease(topology);
    return result;
}

uint64_t space_manager_last_space(void)
{
    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (!topology) return 0;
    uint64_t result = managed_space_last(topology);
    CFRelease(topology);
    return result;
}

uint64_t space_manager_active_space(void)
{
    uint32_t did = 0;
    struct window *window = window_manager_focused_window(&g_window_manager);

    if (window) did = window_display_id(window->id);
    if (!did)   did = display_manager_active_display_id();
    if (!did)   return 0;

    return display_space_id(did);
}

void space_manager_move_window_list_to_space(uint64_t sid, uint32_t *window_list, int window_count)
{
    if (SLSPerformAsynchronousBridgedWindowManagementOperation) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(window_list, sizeof(uint32_t), window_count, kCFNumberSInt32Type);
        Class cls = objc_getClass("SLSBridgedMoveWindowsToManagedSpaceOperation");
        SEL sel = sel_registerName("initWithWindows:spaceID:");
        id operation = ((id (*)(id, SEL, id, uint64_t))objc_msgSend)([cls alloc], sel, (__bridge id)window_list_ref, sid);
        SLSPerformAsynchronousBridgedWindowManagementOperation(operation);
        [operation release];
        CFRelease(window_list_ref);
    } else if (!workspace_use_macos_space_workaround()) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(window_list, sizeof(uint32_t), window_count, kCFNumberSInt32Type);
        SLSMoveWindowsToManagedSpace(g_connection, window_list_ref, sid);
        CFRelease(window_list_ref);
    } else if (!scripting_addition_move_window_list_to_space(sid, window_list, window_count)) {
        SLSSpaceSetCompatID(g_connection, sid, 0x79616265);
        SLSSetWindowListWorkspace(g_connection, window_list, window_count, 0x79616265);
        SLSSpaceSetCompatID(g_connection, sid, 0x0);
    }
}

#ifdef TESTS
static void (*space_manager_test_move_window_to_space)(uint64_t, struct window *);
#endif

void space_manager_move_window_to_space(uint64_t sid, struct window *window)
{
#ifdef TESTS
    if (space_manager_test_move_window_to_space) {
        space_manager_test_move_window_to_space(sid, window);
        return;
    }
#endif
    if (SLSPerformAsynchronousBridgedWindowManagementOperation) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(&window->id, sizeof(uint32_t), 1, kCFNumberSInt32Type);
        Class cls = objc_getClass("SLSBridgedMoveWindowsToManagedSpaceOperation");
        SEL sel = sel_registerName("initWithWindows:spaceID:");
        id operation = ((id (*)(id, SEL, id, uint64_t))objc_msgSend)([cls alloc], sel, (__bridge id)window_list_ref, sid);
        SLSPerformAsynchronousBridgedWindowManagementOperation(operation);
        [operation release];
        CFRelease(window_list_ref);
    } else if (!workspace_use_macos_space_workaround()) {
        CFArrayRef window_list_ref = cfarray_of_cfnumbers(&window->id, sizeof(uint32_t), 1, kCFNumberSInt32Type);
        SLSMoveWindowsToManagedSpace(g_connection, window_list_ref, sid);
        CFRelease(window_list_ref);
    } else if (!scripting_addition_move_window_to_space(sid, window->id)) {
        SLSSpaceSetCompatID(g_connection, sid, 0x79616265);
        SLSSetWindowListWorkspace(g_connection, &window->id, 1, 0x79616265);
        SLSSpaceSetCompatID(g_connection, sid, 0x0);
    }
}

static inline uint64_t space_manager_find_first_user_space_for_display(uint32_t did)
{
    int count;
    uint64_t *space_list = display_space_list(did, &count);
    if (!space_list) return 0;

    for (int i = 0; i < count; ++i) {
        uint64_t sid = space_list[i];

        if (space_is_user(sid)) {
            return sid;
        }
    }

    return 0;
}

static inline bool space_manager_is_space_last_user_space(uint64_t sid)
{
    bool result = true;

    int count;
    uint64_t *space_list = display_space_list(space_display_id(sid), &count);
    if (!space_list) return true;

    for (int i = 0; i < count; ++i) {
        uint64_t c_sid = space_list[i];
        if (sid == c_sid) continue;

        if (space_is_user(c_sid)) {
            result = false;
            break;
        }
    }

    return result;
}

static void space_manager_swap_view_identity(struct view *a_view, struct view *b_view)
{
    CFStringRef uuid = a_view->uuid;
    a_view->uuid = b_view->uuid;
    b_view->uuid = uuid;

    struct declarative_space_identity identity = a_view->declarative_identity;
    a_view->declarative_identity = b_view->declarative_identity;
    b_view->declarative_identity = identity;
}

static enum space_op_error space_manager_swap_space_with_space_on_display(uint32_t a_did, uint64_t a_sid, uint32_t b_did, uint64_t b_sid)
{
    if (display_manager_display_is_animating(a_did)) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;
    if (display_manager_display_is_animating(b_did)) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    int a_window_list_count = 0;
    bool a_inventory_succeeded = false;
#ifdef TESTS
    uint32_t *(*inventory)(uint64_t *, int, int, int *, bool, bool *) =
        space_manager_test_swap_window_list ? space_manager_test_swap_window_list
                                            : space_window_list_for_connection_with_status;
    uint32_t *a_window_list = inventory(&a_sid, 1, 0, &a_window_list_count, true, &a_inventory_succeeded);
#else
    uint32_t *a_window_list = space_window_list_for_connection_with_status(&a_sid, 1, 0,
                                                                           &a_window_list_count, true,
                                                                           &a_inventory_succeeded);
#endif

    int b_window_list_count = 0;
    bool b_inventory_succeeded = false;
#ifdef TESTS
    uint32_t *b_window_list = inventory(&b_sid, 1, 0, &b_window_list_count, true, &b_inventory_succeeded);
#else
    uint32_t *b_window_list = space_window_list_for_connection_with_status(&b_sid, 1, 0,
                                                                           &b_window_list_count, true,
                                                                           &b_inventory_succeeded);
#endif

    if (!a_inventory_succeeded || !b_inventory_succeeded ||
        a_window_list_count < 0 || b_window_list_count < 0 ||
        (a_window_list_count && !a_window_list) ||
        (b_window_list_count && !b_window_list)) {
        return SPACE_OP_ERROR_WINDOW_SERVER;
    }

    struct view *a_view = table_find(&g_space_manager.view, &a_sid);
    if (!a_view) return SPACE_OP_ERROR_MISSING_SRC;
    struct view *b_view = table_find(&g_space_manager.view, &b_sid);
    if (!b_view) return SPACE_OP_ERROR_MISSING_DST;

    float window_animation_duration = g_window_manager.window_animation_duration;
    g_window_manager.window_animation_duration = 0.0f;
    __asm__ __volatile__ ("" ::: "memory");

    /* Both keys were resolved above; replacement cannot allocate or fail recoverably. */
    if (!table_replace(&g_space_manager.view, &a_sid, b_view) ||
        !table_replace(&g_space_manager.view, &b_sid, a_view)) {
        abort();
    }

    a_view->sid = b_sid;
    b_view->sid = a_sid;
    space_manager_swap_view_identity(a_view, b_view);

    if (a_window_list_count) {
        space_manager_move_window_list_to_space(b_sid, a_window_list, a_window_list_count);
    }

    if (b_window_list_count) {
        space_manager_move_window_list_to_space(a_sid, b_window_list, b_window_list_count);
    }

    for (int i = 0; i < buf_len(g_space_manager.labels); ++i) {
        struct space_label *label = &g_space_manager.labels[i];
        if      (label->sid == a_sid) label->sid = b_sid;
        else if (label->sid == b_sid) label->sid = a_sid;
    }

    view_update(a_view);
    view_update(b_view);

    view_flush(a_view);
    view_flush(b_view);

    __asm__ __volatile__ ("" ::: "memory");
    g_window_manager.window_animation_duration = window_animation_duration;
    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error space_manager_swap_space_with_space(uint64_t acting_sid, uint64_t selector_sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    uint32_t acting_did = space_display_id(acting_sid);
    uint32_t selector_did = space_display_id(selector_sid);

    if (acting_sid == selector_sid) return SPACE_OP_ERROR_SAME_SPACE;
    if (acting_did != selector_did) return space_manager_swap_space_with_space_on_display(acting_did, acting_sid, selector_did, selector_sid);

    bool is_animating = display_manager_display_is_animating(acting_did);
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    uint64_t acting_prev_sid = space_manager_prev_space(acting_sid);
    uint64_t selector_prev_sid = space_manager_prev_space(selector_sid);

    uint32_t acting_prev_did = acting_prev_sid ? space_display_id(acting_prev_sid) : 0;
    uint32_t selector_prev_did = selector_prev_sid ? space_display_id(selector_prev_sid) : 0;

    bool acting_sid_is_first = !acting_prev_sid || acting_prev_did != acting_did;
    bool selector_sid_is_first = !selector_prev_sid || selector_prev_did != selector_did;

    int acting_mci = space_manager_mission_control_index(acting_sid);
    int selector_mci = space_manager_mission_control_index(selector_sid);
    bool success = true;

    if (acting_sid_is_first && !selector_sid_is_first && selector_mci - acting_mci == 1) {
        success = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
    } else if (!acting_sid_is_first && selector_sid_is_first && acting_mci - selector_mci == 1) {
        success = scripting_addition_move_space_after_space(selector_sid, acting_sid, selector_sid == space_manager_active_space());
    } else if (acting_sid_is_first && !selector_sid_is_first) {
        success  = scripting_addition_move_space_after_space(selector_sid, acting_sid, false);
        success &= scripting_addition_move_space_after_space(acting_sid, selector_prev_sid, acting_sid == space_manager_active_space());
    } else if (!acting_sid_is_first && selector_sid_is_first) {
        success  = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
        success &= scripting_addition_move_space_after_space(selector_sid, acting_prev_sid, false);
    } else if (!acting_sid_is_first && !selector_sid_is_first) {
        if (acting_mci > selector_mci) {
            success  = scripting_addition_move_space_after_space(selector_sid, acting_sid, false);
            success &= scripting_addition_move_space_after_space(acting_sid, selector_prev_sid, acting_sid == space_manager_active_space());
        } else {
            success  = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
            success &= scripting_addition_move_space_after_space(selector_sid, acting_prev_sid, false);
        }
    }

    return success ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

enum space_op_error space_manager_move_space_to_space(uint64_t acting_sid, uint64_t selector_sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    uint32_t acting_did = space_display_id(acting_sid);
    uint32_t selector_did = space_display_id(selector_sid);

    if (acting_sid == selector_sid) return SPACE_OP_ERROR_SAME_SPACE;
    if (acting_did != selector_did) return SPACE_OP_ERROR_SAME_DISPLAY;

    bool is_animating = display_manager_display_is_animating(acting_did);
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    uint64_t acting_prev_sid = space_manager_prev_space(acting_sid);
    uint64_t selector_prev_sid = space_manager_prev_space(selector_sid);

    uint32_t acting_prev_did = acting_prev_sid ? space_display_id(acting_prev_sid) : 0;
    uint32_t selector_prev_did = selector_prev_sid ? space_display_id(selector_prev_sid) : 0;

    bool acting_sid_is_first = !acting_prev_sid || acting_prev_did != acting_did;
    bool selector_sid_is_first = !selector_prev_sid || selector_prev_did != selector_did;
    bool success = true;

    if (acting_sid_is_first && !selector_sid_is_first) {
        success = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
    } else if (!acting_sid_is_first && selector_sid_is_first) {
        success  = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
        success &= scripting_addition_move_space_after_space(selector_sid, acting_sid, false);
    } else if (!acting_sid_is_first && !selector_sid_is_first) {
        if (space_manager_mission_control_index(acting_sid) > space_manager_mission_control_index(selector_sid)) {
            success = scripting_addition_move_space_after_space(acting_sid, selector_prev_sid, acting_sid == space_manager_active_space());
        } else {
            success = scripting_addition_move_space_after_space(acting_sid, selector_sid, acting_sid == space_manager_active_space());
        }
    }

    return success ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

enum space_op_error space_manager_move_space_to_display(struct space_manager *sm, uint64_t sid, uint32_t did)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;
    if (!sid)     return SPACE_OP_ERROR_MISSING_SRC;

    uint32_t s_did = space_display_id(sid);
    if (s_did == did) return SPACE_OP_ERROR_INVALID_DST;

    bool is_src_animating = display_manager_display_is_animating(s_did);
    if (is_src_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    bool last_space = space_manager_is_space_last_user_space(sid);
    if (last_space) return SPACE_OP_ERROR_INVALID_SRC;

    bool is_dst_animating = display_manager_display_is_animating(did);
    if (is_dst_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    uint64_t d_sid = display_space_id(did);
    if (!d_sid) return SPACE_OP_ERROR_MISSING_DST;

    bool focus_space = sid == space_manager_active_space();

    if (scripting_addition_move_space_to_display(sid, d_sid,  focus_space ? space_manager_prev_space(sid) : 0, focus_space ? 1 : 0)) {
        space_manager_mark_view_invalid(sm, sid);
        if (focus_space) {
            space_manager_focus_space(sid);
        }
        return SPACE_OP_ERROR_SUCCESS;
    }

    return SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool space_manager_focus_space_using_gesture(uint32_t new_did, uint64_t new_sid)
{
    int cur_index = space_manager_mission_control_index(display_space_id(new_did));
    int new_index = space_manager_mission_control_index(new_sid);
    if (!cur_index || !new_index) return false;

    int count = abs(new_index - cur_index);
    if (count == 0) {
        display_manager_focus_display(new_did, new_sid);
        return true;
    }

    CGPoint point = display_center(new_did);
    uint32_t cur_did = display_manager_cursor_display_id();

    bool focus_display = cur_did != new_did;
    if (focus_display) CGWarpMouseCursorPosition(point);

    if (space_gesture_requires_payload()) {
        if (!space_gesture_perform(new_index > cur_index ? 1 : -1, count)) return false;
        goto focused;
    }

    //
    // NOTE(asmvik): MacOS does not have an API that allows for space activation.
    // However, we can synthesize a sequence of high velocity gestures to skip the
    // animation instead.
    //
    // :Attribution
    // https://github.com/jurplel/InstantSpaceSwitcher
    // https://github.com/thenickdude/wacom-driver-fix/blob/bdfda9a788934c88d09d31ea6a42664b9ba1471e/Readme.md
    // Technique first observed in practice, and reverse-engineered from, BetterTouchTool.
    //

    CGEventRef event_dock_control = CGEventCreate(NULL);
    if (!event_dock_control) return false;

    float sign = (new_index - cur_index) > 0 ? 1.0 : -1.0;
    CGEventSetIntegerValueField(event_dock_control, /* kCGSEventTypeField            */  55, /* kCGSEventDockControl       */ 30);
    CGEventSetIntegerValueField(event_dock_control, /* kCGEventGestureHIDType        */ 110, /* kIOHIDEventTypeDockSwipe   */ 23);
    CGEventSetIntegerValueField(event_dock_control, /* kCGEventGestureSwipeMotion    */ 123, /* kCGGestureMotionHorizontal */  1);
    CGEventSetDoubleValueField(event_dock_control,  /* kCGEventGestureSwipeProgress  */ 124, sign);
    CGEventSetDoubleValueField(event_dock_control,  /* kCGEventGestureSwipeVelocityX */ 129, sign * 9999.0);

    for (int i = 0; i < count; ++i) {
        CGEventSetIntegerValueField(event_dock_control, /* kCGEventGesturePhase */ 132, /* kCGSGesturePhaseBegan */ 1);
        CGEventPost(kCGSessionEventTap, event_dock_control);
        CGEventSetIntegerValueField(event_dock_control, /* kCGEventGesturePhase */ 132, /* kCGSGesturePhaseEnded */ 4);
        CGEventPost(kCGSessionEventTap, event_dock_control);
    }
    CFRelease(event_dock_control);

focused:
    if (focus_display) {
        display_manager_set_active_display_id(new_did);
        if (space_manager_active_space() != new_sid) {
            CGPostMouseEvent(point, false, 1, true);
            CGPostMouseEvent(point, false, 1, false);
        }
    }

    return true;
}
#pragma clang diagnostic pop

enum space_op_error space_manager_focus_space(uint64_t sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    uint64_t cur_sid = space_manager_active_space();
    if (cur_sid == sid) return SPACE_OP_ERROR_SAME_SPACE;

    uint32_t cur_did = space_display_id(cur_sid);
    uint32_t new_did = space_display_id(sid);
    bool focus_display = cur_did != new_did;

    bool is_animating = display_manager_display_is_animating(new_did);
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    if (scripting_addition_focus_space(sid)) {
        if (focus_display) {
            display_manager_focus_display(new_did, sid);
        }
    } else {
        if (!space_manager_focus_space_using_gesture(new_did, sid)) return SPACE_OP_ERROR_GESTURE;
    }

    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error space_manager_switch_space(uint64_t sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    uint64_t cur_sid = space_manager_active_space();
    if (cur_sid == sid) return SPACE_OP_ERROR_SAME_SPACE;

    uint32_t cur_did = space_display_id(cur_sid);
    uint32_t did     = space_display_id(sid);

    bool is_src_animating = display_manager_display_is_animating(cur_did);
    if (is_src_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    bool is_dst_animating = display_manager_display_is_animating(did);
    if (is_dst_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    if (cur_did != did) {
        enum space_op_error result = space_manager_swap_space_with_space_on_display(cur_did, cur_sid, did, sid);
        if (result != SPACE_OP_ERROR_SUCCESS) return result;
        display_manager_focus_display(cur_did, cur_sid);
        return SPACE_OP_ERROR_SUCCESS;
    }

    return scripting_addition_focus_space(sid) ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

enum space_op_error space_manager_destroy_space(uint64_t sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;

    if (!sid) return SPACE_OP_ERROR_MISSING_SRC;
    if (!space_is_user(sid)) return SPACE_OP_ERROR_INVALID_TYPE;
    if (space_manager_is_space_last_user_space(sid)) return SPACE_OP_ERROR_INVALID_SRC;

    uint32_t did = space_display_id(sid);
    uint64_t first_sid = space_manager_find_first_user_space_for_display(did);

    bool is_animating = display_manager_display_is_animating(did);
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    bool success = scripting_addition_destroy_space(sid);
    if (!success) return SPACE_OP_ERROR_SCRIPTING_ADDITION;

    if (first_sid) {
        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, first_sid);
    }

    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error space_manager_add_space(uint64_t sid)
{
    bool is_in_mc = mission_control_is_active();
    if (is_in_mc) return SPACE_OP_ERROR_IN_MISSION_CONTROL;
    if (!sid)     return SPACE_OP_ERROR_MISSING_SRC;

    bool is_animating = display_manager_display_is_animating(space_display_id(sid));
    if (is_animating) return SPACE_OP_ERROR_DISPLAY_IS_ANIMATING;

    return scripting_addition_create_space(sid) ? SPACE_OP_ERROR_SUCCESS : SPACE_OP_ERROR_SCRIPTING_ADDITION;
}

void space_manager_assign_process_to_space(pid_t pid, uint64_t sid)
{
    SLSProcessAssignToSpace(g_connection, pid, sid);
}

void space_manager_assign_process_to_all_spaces(pid_t pid)
{
    SLSProcessAssignToAllSpaces(g_connection, pid);
}

bool space_manager_is_window_on_active_space(struct window *window)
{
    uint64_t sid = space_manager_active_space();
    bool result = space_manager_is_window_on_space(sid, window);
    return result;
}

bool space_manager_is_window_on_space(uint64_t sid, struct window *window)
{
    int space_count;
    uint64_t *space_list = window_space_list(window->id, &space_count);
    if (!space_list) return false;

    for (int i = 0; i < space_count; ++i) {
        if (sid == space_list[i]) {
            return true;
        }
    }

    return false;
}

void space_manager_mark_spaces_invalid_for_display(struct space_manager *sm, uint32_t did)
{
    int space_count;
    uint64_t *space_list = display_space_list(did, &space_count);
    if (!space_list) return;

    uint64_t sid = display_space_id(did);
    for (int i = 0; i < space_count; ++i) {
        if (space_list[i] == sid) {
            space_manager_refresh_view(sm, sid);
        } else {
            space_manager_mark_view_invalid(sm, space_list[i]);
        }
    }
}

void space_manager_mark_spaces_invalid(struct space_manager *sm)
{
    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return;

    for (int i = 0; i < display_count; ++i) {
        space_manager_mark_spaces_invalid_for_display(sm, display_list[i]);
    }
}

bool space_manager_refresh_application_windows(struct space_manager *sm)
{
    int refresh_count = buf_len(g_window_manager.applications_to_refresh);
    if (!refresh_count) return false;
    int window_count = g_window_manager.window.count;
    int visited = 0;
    g_window_manager.application_refresh_cursor %= refresh_count;
    window_manager_begin_inactive_probe_pass(4096);
    while (refresh_count > 0 && visited < refresh_count && window_manager_inactive_probe_budget_remaining() > 0) {
        int index = g_window_manager.application_refresh_cursor;
        struct application *application = g_window_manager.applications_to_refresh[index];
        debug("%s: %s has windows that are not yet resolved\n", __FUNCTION__, application->name);
        int created_count = 0;
        struct window **created_windows = NULL;
        bool result = window_manager_add_existing_application_windows(sm, &g_window_manager, application, index, &created_windows, &created_count);
        event_loop_process_application_windows(created_windows, created_count);
        buf_free(created_windows);
        if (result) {
            refresh_count = buf_len(g_window_manager.applications_to_refresh);
        } else {
            g_window_manager.application_refresh_cursor = (index + 1) % refresh_count;
            ++visited;
        }
    }
    return window_count != g_window_manager.window.count;
}

static bool space_manager_uuid_can_remap(CFStringRef uuid)
{
    return cf_is_type(uuid, CFStringGetTypeID()) && CFStringGetLength(uuid) > 0;
}

bool space_manager_handle_display_add(struct space_manager *sm, uint32_t did)
{
    struct display_add_remap {
        struct view *view;
        struct space_label *label;
        uint64_t old_sid;
        uint64_t new_sid;
        CFStringRef uuid;
    };
    int space_count;
    uint64_t *space_list = display_space_list(did, &space_count);
    if (!space_list || space_count < 0) return false;
    struct display_add_remap *remaps = calloc((size_t) space_count, sizeof(*remaps));
    if (space_count && !remaps) return false;

    bool result = false;
    struct table new_views = {0};
    for (int i = 0; i < space_count; ++i) {
        remaps[i].new_sid = space_list[i];
        remaps[i].uuid = SLSSpaceCopyName(g_connection, space_list[i]);
        if (!space_manager_uuid_can_remap(remaps[i].uuid)) goto out;
        table_for (struct view *view, sm->view, {
            if (!remaps[i].view && space_manager_uuid_can_remap(view->uuid) && CFEqual(view->uuid, remaps[i].uuid)) {
                for (int j = 0; j < i; ++j) if (remaps[j].view == view) goto out;
                remaps[i].view = view;
                remaps[i].old_sid = view->sid;
                for (int label = 0; label < buf_len(sm->labels); ++label) {
                    if (sm->labels[label].sid == view->sid) remaps[i].label = &sm->labels[label];
                }
            }
        })
    }

    if (!table_try_init(&new_views, sm->view.capacity, sm->view.hash, sm->view.cmp)) goto out;
    table_for (struct view *view, sm->view, {
        uint64_t sid = view->sid;
        for (int i = 0; i < space_count; ++i) if (remaps[i].view == view) sid = remaps[i].new_sid;
        if (!table_try_add(&new_views, &sid, view)) goto out;
    })
    if (new_views.count != sm->view.count) goto out;
    uint64_t active_sid = space_manager_active_space();
    if (!active_sid) goto out;

    struct table old_views = sm->view;
    sm->view = new_views;
    new_views = (struct table) {0};
    for (int i = 0; i < space_count; ++i) {
        if (!remaps[i].view) continue;
        if (remaps[i].view->uuid) CFRelease(remaps[i].view->uuid);
        remaps[i].view->uuid = remaps[i].uuid;
        remaps[i].uuid = NULL;
        remaps[i].view->sid = remaps[i].new_sid;
    }
    for (int i = 0; i < space_count; ++i) {
        if (remaps[i].label) remaps[i].label->sid = remaps[i].new_sid;
    }
    table_free(&old_views);
    sm->current_space_id = active_sid;
    sm->last_space_id = active_sid;
    result = true;

out:
    table_free(&new_views);
    for (int i = 0; i < space_count; ++i) if (remaps[i].uuid) CFRelease(remaps[i].uuid);
    free(remaps);
    return result;
}

bool space_manager_begin(struct space_manager *sm)
{
    sm->layout = VIEW_FLOAT;
    sm->accordion_padding = 30;
    sm->split_ratio = 0.5f;
    sm->auto_balance = SPLIT_NONE;
    sm->split_type = SPLIT_AUTO;
    sm->window_placement = CHILD_SECOND;
    sm->window_insertion_point = INSERT_FOCUSED;
    sm->window_zoom_persist = true;
    sm->labels = NULL;
    sm->skip_window_focus_animation = false;
    if (!table_try_init(&sm->view, 23, hash_view, compare_view)) return false;

    int display_count;
#ifdef TESTS
    uint32_t *display_list = space_manager_test_active_display_list
                           ? space_manager_test_active_display_list(&display_count)
                           : display_manager_active_display_list(&display_count);
#else
    uint32_t *display_list = display_manager_active_display_list(&display_count);
#endif
    if (!display_list) goto fail;

    for (int i = 0; i < display_count; ++i) {
        int space_count;
#ifdef TESTS
        uint64_t *space_list = space_manager_test_display_space_list
                             ? space_manager_test_display_space_list(display_list[i], &space_count)
                             : display_space_list(display_list[i], &space_count);
#else
        uint64_t *space_list = display_space_list(display_list[i], &space_count);
#endif
        if (!space_list) goto fail;

        for (int j = 0; j < space_count; ++j) {
            struct view *view = view_create(space_list[j]);
            if (!view) goto fail;
            if (!table_try_add(&sm->view, &space_list[j], view)) {
                view_destroy(view);
                legacy_allocation_free(view);
                goto fail;
            }
        }
    }

    sm->current_space_id = space_manager_active_space();
    sm->last_space_id = sm->current_space_id;
    sm->did_begin = true;
    return true;

fail:
    table_for (struct view *view, sm->view, {
        view_destroy(view);
        legacy_allocation_free(view);
    })
    table_free(&sm->view);
    sm->did_begin = false;
    return false;
}
