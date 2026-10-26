extern mach_port_t g_bs_port;
extern uint8_t *g_event_bytes;
extern struct event_loop g_event_loop;
extern void *g_workspace_context;
extern struct process_manager g_process_manager;
extern struct mouse_state g_mouse_state;
extern double g_cv_host_clock_frequency;

#ifdef TESTS
static struct window *window_manager_test_rule_window;
#endif

static AXError window_manager_set_attribute_value(AXUIElementRef element, CFStringRef attribute, CFTypeRef value);

static TABLE_HASH_FUNC(hash_wm)
{
    return *(uint32_t *) key;
}

static TABLE_COMPARE_FUNC(compare_wm)
{
    return *(uint32_t *) key_a == *(uint32_t *) key_b;
}

bool window_manager_is_window_eligible(struct window *window)
{
    bool result = window->is_root && (window_is_real(window) || window_check_rule_flag(window, WINDOW_RULE_MANAGED));
    return result;
}

void window_manager_query_window_rules(FILE *rsp)
{
    TIME_FUNCTION;

    fprintf(rsp, "[");
    for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
        struct rule *rule = &g_window_manager.rules[i];
        rule_serialize(rsp, rule, i);
        if (i < buf_len(g_window_manager.rules) - 1) fprintf(rsp, ",");
    }
    fprintf(rsp, "]\n");
}

#ifdef TESTS
static uint32_t *(*window_manager_query_window_list)(uint64_t *, int, int, int *, bool, bool *) =
    space_window_list_for_connection_with_status;
static CFArrayRef (*window_manager_query_managed_display_spaces)(int) = SLSCopyManagedDisplaySpaces;
static CFArrayRef (*window_manager_query_ordered_display_uuids)(void) = display_manager_copy_ordered_display_uuids;
static uint64_t *(*window_manager_query_display_space_list)(uint32_t, CFArrayRef, int *) =
    display_space_list_with_topology;
#else
#define window_manager_query_window_list space_window_list_for_connection_with_status
#define window_manager_query_managed_display_spaces SLSCopyManagedDisplaySpaces
#define window_manager_query_ordered_display_uuids display_manager_copy_ordered_display_uuids
#define window_manager_query_display_space_list display_space_list_with_topology
#endif

static bool window_manager_query_serializes_space(uint64_t flags)
{
    return !flags || (flags & WINDOW_PROPERTY_SPACE);
}

static bool window_manager_query_serializes_display(uint64_t flags)
{
    return !flags || (flags & WINDOW_PROPERTY_DISPLAY);
}

static bool window_manager_query_needs_topology(uint64_t flags)
{
    return window_manager_query_serializes_space(flags) || window_manager_query_serializes_display(flags);
}

static bool window_manager_query_windows_for_spaces_with_topology(FILE *rsp,
                                                                  uint64_t *space_list,
                                                                  int space_count,
                                                                  uint64_t flags,
                                                                  CFArrayRef topology,
                                                                  bool has_topology_snapshot,
                                                                  CFArrayRef ordered_display_uuids,
                                                                  bool has_ordered_display_uuids_snapshot)
{
    int window_count = 0;
    bool inventory_succeeded = false;
    uint32_t *window_list = window_manager_query_window_list(space_list, space_count, 0, &window_count,
                                                             true, &inventory_succeeded);
    if (!inventory_succeeded) return false;

    bool owns_topology = false;
    if (!has_topology_snapshot && window_manager_query_needs_topology(flags)) {
        topology = window_manager_query_managed_display_spaces(g_connection);
        has_topology_snapshot = true;
        owns_topology = topology != NULL;
    }

    bool owns_ordered_display_uuids = false;
    if (!has_ordered_display_uuids_snapshot && window_manager_query_serializes_display(flags)) {
        ordered_display_uuids = window_manager_query_ordered_display_uuids();
        has_ordered_display_uuids_snapshot = true;
        owns_ordered_display_uuids = ordered_display_uuids != NULL;
    }

    struct window_serialization_context context = {
        .managed_display_spaces = topology,
        .ordered_display_uuids = ordered_display_uuids,
        .has_managed_display_spaces_snapshot = has_topology_snapshot,
        .has_ordered_display_uuids_snapshot = has_ordered_display_uuids_snapshot
    };

    fprintf(rsp, "[");
    for (int i = 0; i < window_count; ++i) {
        struct window *window = window_manager_find_window(&g_window_manager, window_list[i]);
        if (window) {
            window_serialize_with_context(rsp, window, flags, &context);
        } else {
            window_nonax_serialize_with_context(rsp, window_list[i], flags, &context);
        }
        if (i < window_count - 1) fprintf(rsp, ",");
    }
    fprintf(rsp, "]\n");
    if (owns_ordered_display_uuids) CFRelease(ordered_display_uuids);
    if (owns_topology) CFRelease(topology);
    return true;
}

bool window_manager_query_windows_for_spaces(FILE *rsp, uint64_t *space_list, int space_count, uint64_t flags)
{
    TIME_FUNCTION;
    return window_manager_query_windows_for_spaces_with_topology(rsp, space_list, space_count,
                                                                  flags, NULL, false, NULL, false);
}

bool window_manager_query_windows_for_display(FILE *rsp, uint32_t did, uint64_t flags)
{
    TIME_FUNCTION;

    CFArrayRef topology = window_manager_query_managed_display_spaces(g_connection);
    if (!topology) return false;

    int space_count = 0;
    uint64_t *space_list = window_manager_query_display_space_list(did, topology, &space_count);
    bool result = space_list && window_manager_query_windows_for_spaces_with_topology(
                                      rsp, space_list, space_count, flags,
                                      topology, true, NULL, false);
    CFRelease(topology);
    return result;
}

bool window_manager_query_windows_for_displays(FILE *rsp, uint64_t flags)
{
    TIME_FUNCTION;

    int display_count = 0;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return false;

    CFArrayRef topology = window_manager_query_managed_display_spaces(g_connection);
    if (!topology) return false;

    int space_count = 0;
    uint64_t *space_list = NULL;

    for (int i = 0; i < display_count; ++i) {
        int count;
        uint64_t *list = window_manager_query_display_space_list(display_list[i], topology, &count);
        if (!list) {
            CFRelease(topology);
            return false;
        }

        //
        // NOTE(asmvik): display_space_list_with_topology(..) uses a linear allocator,
        // and so we only need to track the beginning of the first list along
        // with the total number of spaces that have been allocated.
        //

        if (!space_list) space_list = list;
        space_count += count;
    }

    bool result = window_manager_query_windows_for_spaces_with_topology(
        rsp, space_list, space_count, flags, topology, true, NULL, false);
    CFRelease(topology);
    return result;
}

bool window_manager_rule_matches_window(struct rule *rule, struct window *window, char *window_title, char *window_role, char *window_subrole)
{
    int regex_match_app = rule_check_flag(rule, RULE_APP_EXCLUDE) ? REGEX_MATCH_YES : REGEX_MATCH_NO;
    if (regex_match(rule_check_flag(rule, RULE_APP_VALID), &rule->app_regex, window->application->name) == regex_match_app) return false;

    int regex_match_title = rule_check_flag(rule, RULE_TITLE_EXCLUDE) ? REGEX_MATCH_YES : REGEX_MATCH_NO;
    if (regex_match(rule_check_flag(rule, RULE_TITLE_VALID), &rule->title_regex, window_title) == regex_match_title) return false;

    int regex_match_role = rule_check_flag(rule, RULE_ROLE_EXCLUDE) ? REGEX_MATCH_YES : REGEX_MATCH_NO;
    if (regex_match(rule_check_flag(rule, RULE_ROLE_VALID), &rule->role_regex, window_role) == regex_match_role) return false;

    int regex_match_subrole = rule_check_flag(rule, RULE_SUBROLE_EXCLUDE) ? REGEX_MATCH_YES : REGEX_MATCH_NO;
    if (regex_match(rule_check_flag(rule, RULE_SUBROLE_VALID), &rule->subrole_regex, window_subrole) == regex_match_subrole) return false;

    return true;
}

void window_manager_apply_manage_rule_effects_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, struct rule_effects *effects)
{
    if (effects->manage == RULE_PROP_ON) {
        window_set_rule_flag(window, WINDOW_RULE_MANAGED);
#ifdef TESTS
        if (window == window_manager_test_rule_window) return;
#endif
        window_manager_make_window_floating(sm, wm, window, false, true);
    } else if (effects->manage == RULE_PROP_OFF) {
        window_clear_rule_flag(window, WINDOW_RULE_MANAGED);
        window_manager_make_window_floating(sm, wm, window, true, true);
    }
}

#ifdef TESTS
static char *(*window_manager_rule_scratchpad_copy)(char *) = string_copy;
#else
#define window_manager_rule_scratchpad_copy string_copy
#endif

void window_manager_apply_rule_effects_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, struct rule_effects *effects)
{
    char *scratchpad = NULL;
    if (effects->scratchpad) {
        scratchpad = window_manager_rule_scratchpad_copy(effects->scratchpad);
        if (!scratchpad) return;
    }

    if (effects->sid || effects->did) {
        if (!window_is_fullscreen(window) && !space_is_fullscreen(window_space(window->id))) {
            uint64_t sid = effects->sid ? effects->sid : display_space_id(effects->did);
            window_manager_send_window_to_space(sm, wm, window, sid, true);
            if (rule_effects_check_flag(effects, RULE_FOLLOW_SPACE) || effects->fullscreen == RULE_PROP_ON) {
                space_manager_focus_space(sid);
            }
        }
    }

    if (effects->sticky == RULE_PROP_ON) {
        window_manager_make_window_sticky(sm, wm, window, true);
    } else if (effects->sticky == RULE_PROP_OFF) {
        window_manager_make_window_sticky(sm, wm, window, false);
    }

    if (effects->mff == RULE_PROP_ON) {
        window_set_rule_flag(window, WINDOW_RULE_MFF);
        window_set_rule_flag(window, WINDOW_RULE_MFF_VALUE);
    } else if (effects->mff == RULE_PROP_OFF) {
        window_set_rule_flag(window, WINDOW_RULE_MFF);
        window_clear_rule_flag(window, WINDOW_RULE_MFF_VALUE);
    }

    if (rule_effects_check_flag(effects, RULE_LAYER)) {
        window_manager_set_window_layer(window, effects->layer);
    }

    if (rule_effects_check_flag(effects, RULE_OPACITY) && in_range_ii(effects->opacity, 0.0f, 1.0f)) {
        window->opacity = effects->opacity;
        window_manager_set_opacity(wm, window, effects->opacity);
    }

    if (effects->fullscreen == RULE_PROP_ON) {
        AXUIElementSetAttributeValue(window->ref, kAXFullscreenAttribute, kCFBooleanTrue);
        window_set_rule_flag(window, WINDOW_RULE_FULLSCREEN);
    }

    if (scratchpad && !window_manager_set_scratchpad_for_window(wm, window, scratchpad)) {
        free(scratchpad);
    }

    if (effects->grid[0] != 0 && effects->grid[1] != 0) {
        window_manager_apply_grid(sm, wm, window, effects->grid[0], effects->grid[1], effects->grid[2], effects->grid[3], effects->grid[4], effects->grid[5]);
    }
}

static bool window_manager_rule_is_available(struct rule *rule, struct application *application,
                                             bool one_shot_rules, pid_t one_shot_owner_pid,
                                             uint32_t window_id)
{
    if (!rule_check_flag(rule, RULE_ONE_SHOT)) return true;
    if (rule_one_shot_reassignment_is_pending()) return false;
    if (!one_shot_rules || rule->one_shot_owner_pid != one_shot_owner_pid ||
        (rule->one_shot_window_id && rule->one_shot_window_id != window_id)) return false;
    if (!rule->one_shot_owner_pid) return one_shot_owner_pid == 0;
    return application && rule->one_shot_owner_pid == application->pid &&
           rule->one_shot_owner_psn_high == (uint32_t) application->psn.highLongOfPSN &&
           rule->one_shot_owner_psn_low == (uint32_t) application->psn.lowLongOfPSN;
}

void window_manager_apply_manage_rules_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, char *window_title, char *window_role, char *window_subrole, bool one_shot_rules, pid_t one_shot_owner_pid)
{
    bool match = false;
    struct rule_effects effects = {0};

    for (int i = 0; i < buf_len(wm->rules); ++i) {
        if (window_manager_rule_is_available(&wm->rules[i], window->application,
                                             one_shot_rules, one_shot_owner_pid, window->id)) {
            if (window_manager_rule_matches_window(&wm->rules[i], window, window_title, window_role, window_subrole)) {
                if (wm->rules[i].effects.manage == RULE_PROP_ON) {
                    if (!rule_check_flag(&wm->rules[i], RULE_ROLE_VALID)    && !string_equals(window_role   , "AXWindow"))         continue;
                    if (!rule_check_flag(&wm->rules[i], RULE_SUBROLE_VALID) && !string_equals(window_subrole, "AXStandardWindow")) continue;
                }

                enum rule_fold_result fold = rule_combine_rule_effects(&wm->rules[i], &effects, NULL);
                if (fold == RULE_FOLD_RESOURCE) continue;
                match = true;
                rule_report_fold_warning(&wm->rules[i], fold);

                if (rule_check_flag(&wm->rules[i], RULE_ONE_SHOT)) {
                    rule_set_flag(&wm->rules[i], RULE_ONE_SHOT_REMOVE);
                    wm->rules[i].one_shot_window_id = window->id;
                }
            }
        }
    }

    if (match) window_manager_apply_manage_rule_effects_to_window(sm, wm, window, &effects);
    free(effects.scratchpad);
}

void window_manager_apply_rules_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, char *window_title, char *window_role, char *window_subrole, bool one_shot_rules, pid_t one_shot_owner_pid)
{
    bool match = false;
    struct rule_effects effects = {0};

    for (int i = 0; i < buf_len(wm->rules); ++i) {
        if (window_manager_rule_is_available(&wm->rules[i], window->application,
                                             one_shot_rules, one_shot_owner_pid, window->id)) {
            if (window_manager_rule_matches_window(&wm->rules[i], window, window_title, window_role, window_subrole)) {
                if (!window_check_rule_flag(window, WINDOW_RULE_MANAGED)) {
                    if (!rule_check_flag(&wm->rules[i], RULE_ROLE_VALID)    && !string_equals(window_role   , "AXWindow"))         continue;
                    if (!rule_check_flag(&wm->rules[i], RULE_SUBROLE_VALID) && !string_equals(window_subrole, "AXStandardWindow")) continue;
                }

                enum rule_fold_result fold = rule_combine_rule_effects(&wm->rules[i], &effects, NULL);
                if (fold == RULE_FOLD_RESOURCE) continue;
                match = true;
                rule_report_fold_warning(&wm->rules[i], fold);

                if (rule_check_flag(&wm->rules[i], RULE_ONE_SHOT)) {
                    rule_set_flag(&wm->rules[i], RULE_ONE_SHOT_REMOVE);
                    wm->rules[i].one_shot_window_id = window->id;
                }
            }
        }
    }

    if (match) window_manager_apply_rule_effects_to_window(sm, wm, window, &effects);
    free(effects.scratchpad);
}

#ifdef TESTS
static void (*window_manager_test_effective_ffm_load_observer)(enum ffm_mode);
#endif

enum ffm_mode window_manager_effective_ffm_mode(const struct window_manager *wm)
{
    enum ffm_mode mode = (enum ffm_mode) __atomic_load_n(&wm->effective_ffm_mode, __ATOMIC_ACQUIRE);
#ifdef TESTS
    if (window_manager_test_effective_ffm_load_observer) window_manager_test_effective_ffm_load_observer(mode);
#endif
    return mode;
}

void window_manager_recompute_effective_ffm_mode(struct window_manager *wm)
{
    enum ffm_mode mode = wm->ffm_menu_suspend_count ? FFM_DISABLED : wm->configured_ffm_mode;
    __atomic_store_n(&wm->effective_ffm_mode, (ffm_atomic_mode) mode, __ATOMIC_RELEASE);
}

bool window_manager_set_focus_follows_mouse(struct window_manager *wm, enum ffm_mode mode)
{
    wm->configured_ffm_mode = mode;
    window_manager_recompute_effective_ffm_mode(wm);
    return true;
}

void window_manager_ffm_menu_opened(struct window_manager *wm)
{
    if (wm->ffm_anonymous_menu_suspend_count == UINT32_MAX || wm->ffm_menu_suspend_count == UINT32_MAX) return;
    ++wm->ffm_anonymous_menu_suspend_count;
    ++wm->ffm_menu_suspend_count;
    window_manager_recompute_effective_ffm_mode(wm);
}

void window_manager_ffm_menu_closed(struct window_manager *wm)
{
    if (wm->ffm_anonymous_menu_suspend_count == 0) return;
    --wm->ffm_anonymous_menu_suspend_count;
    if (wm->ffm_menu_suspend_count != 0) --wm->ffm_menu_suspend_count;
    window_manager_recompute_effective_ffm_mode(wm);
}

void window_manager_ffm_application_menu_opened(struct window_manager *wm, struct application *application)
{
    if (!application || application->ffm_menu_suspend_count == UINT32_MAX || wm->ffm_menu_suspend_count == UINT32_MAX) return;
    ++application->ffm_menu_suspend_count;
    ++wm->ffm_menu_suspend_count;
    window_manager_recompute_effective_ffm_mode(wm);
}

void window_manager_ffm_application_menu_closed(struct window_manager *wm, struct application *application)
{
    if (!application || application->ffm_menu_suspend_count == 0) return;
    --application->ffm_menu_suspend_count;
    if (wm->ffm_menu_suspend_count != 0) --wm->ffm_menu_suspend_count;
    window_manager_recompute_effective_ffm_mode(wm);
}

void window_manager_ffm_application_terminated(struct window_manager *wm, struct application *application)
{
    if (!application || application->ffm_menu_suspend_count == 0) return;
    uint32_t released = application->ffm_menu_suspend_count;
    application->ffm_menu_suspend_count = 0;
    wm->ffm_menu_suspend_count = released >= wm->ffm_menu_suspend_count
                               ? 0
                               : wm->ffm_menu_suspend_count - released;
    window_manager_recompute_effective_ffm_mode(wm);
}

void window_manager_set_window_opacity_enabled(struct window_manager *wm, bool enabled)
{
    wm->enable_window_opacity = enabled;
    table_for (struct window *window, wm->window, {
        if (window_manager_is_window_eligible(window)) {
            window_manager_set_opacity(wm, window, enabled ? window->opacity : 1.0f);
        }
    })
}

void window_manager_center_mouse(struct window_manager *wm, struct window *window)
{
    if (window_check_rule_flag(window, WINDOW_RULE_MFF)) {
        if (!window_check_rule_flag(window, WINDOW_RULE_MFF_VALUE)) {
            return;
        }
    } else {
        if (!wm->enable_mff) {
            return;
        }
    }

    CGPoint cursor;
    SLSGetCurrentCursorLocation(g_connection, &cursor);
    if (CGRectContainsPoint(window->frame, cursor)) return;

    uint32_t did = window_display_id(window->id);
    if (!did) return;

    CGPoint center = {
        window->frame.origin.x + window->frame.size.width / 2,
        window->frame.origin.y + window->frame.size.height / 2
    };

    CGRect bounds = CGDisplayBounds(did);
    if (!CGRectContainsPoint(bounds, center)) return;

    CGWarpMouseCursorPosition(center);
}

bool window_manager_should_manage_window(struct window *window)
{
    if (!window->is_root)                              return false;
    if (window_check_flag(window, WINDOW_FLOAT))       return false;
    if (window_is_sticky(window->id))                  return false;
    if (window_check_flag(window, WINDOW_MINIMIZE))    return false;
    if (window_check_flag(window, WINDOW_ORDERED_OUT)) return false;
    if (window->application->is_hidden)                return false;

    // Invisible helper windows can otherwise reserve an empty BSP slot. Keep
    // explicit manage=on rules authoritative for applications that intentionally
    // publish their usable frame later.
    if ((window->frame.size.width <= 0.0f || window->frame.size.height <= 0.0f) &&
        !window_check_rule_flag(window, WINDOW_RULE_MANAGED)) return false;

    return (window_is_standard(window) && window_level_is_standard(window) && window_can_move(window)) || window_check_rule_flag(window, WINDOW_RULE_MANAGED);
}

struct view *window_manager_find_managed_window(struct window_manager *wm, struct window *window)
{
    return table_find(&wm->managed_window, &window->id);
}

void window_manager_remove_managed_window(struct window_manager *wm, uint32_t wid)
{
    bool present = table_find(&wm->managed_window, &wid) != NULL;
    if (present && wm == &g_window_manager) {
        event_loop_cancel_managed_window_correction(window_manager_find_window(wm, wid));
    }
    table_remove(&wm->managed_window, &wid);
    if (present && wm == &g_window_manager && declarative_installed_has_scaffold()) {
        declarative_configuration_request_reconcile();
    }
}

bool window_manager_add_managed_window(struct window_manager *wm, struct window *window, struct view *view)
{
    if (!window || !view || view->layout == VIEW_FLOAT) return false;
    if (!view_find_window_node(view, window->id)) return false;

    struct view *old_view = table_find(&wm->managed_window, &window->id);
    if (!old_view || !table_replace(&wm->managed_window, &window->id, view)) return false;
    window_manager_purify_window(wm, window);
    if (old_view != view && wm == &g_window_manager && declarative_installed_has_scaffold()) {
        declarative_configuration_request_reconcile();
    }
    return true;
}

static bool window_manager_transfer_managed_window_internal(struct window_manager *wm, struct window *window,
                                                             struct view *src_view, struct view *dst_view,
                                                             uint32_t insertion_point, bool flush)
{
    if (!wm || !window || !src_view || !dst_view || src_view == dst_view) return false;
    if (src_view->layout == VIEW_FLOAT || dst_view->layout == VIEW_FLOAT) return false;
    if (window_manager_find_managed_window(wm, window) != src_view) return false;
    if (!view_find_window_node(src_view, window->id) || view_find_window_node(dst_view, window->id)) return false;

    /* Reserve every destination resource before changing the source tree. */
    struct view_insertion_plan plan;
    if (!view_prepare_window_node_insertion(dst_view, window, insertion_point, &plan)) return false;

    view_remove_window_node(src_view, window);
    struct window_node *dst_node = view_commit_window_node_insertion(dst_view, window, insertion_point, &plan);
    view_abort_window_node_insertion(&plan);
    if (!dst_node) {
        /* Preparation and the checks above make this an invariant, not a recoverable allocation path. */
        abort();
    }

    if (!window_manager_add_managed_window(wm, window, dst_view)) {
        abort();
    }
    window_manager_adjust_layer(window, LAYER_BELOW);

    if (flush) {
        if (view_layout_is_accordion(src_view->layout)) view_update(src_view);
        if (view_space_is_visible(src_view->sid)) view_flush(src_view);
        else view_set_flag(src_view, VIEW_IS_DIRTY);
        space_manager_flush_tiled_window(dst_view, window);
    }
    return true;
}

bool window_manager_transfer_managed_window(struct window_manager *wm, struct window *window,
                                            struct view *src_view, struct view *dst_view,
                                            uint32_t insertion_point)
{
    return window_manager_transfer_managed_window_internal(wm, window, src_view, dst_view,
                                                           insertion_point, true);
}

enum managed_window_result window_manager_tile_window_on_space(struct space_manager *sm, struct window_manager *wm,
                                                                struct window *window, uint64_t sid, uint32_t insertion_point,
                                                                bool flush)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view) return MANAGED_WINDOW_FAILED;
    if (view->layout == VIEW_FLOAT) return MANAGED_WINDOW_UNMANAGED;
    if (table_find(&wm->managed_window, &window->id)) return MANAGED_WINDOW_FAILED;
    if (!table_try_add(&wm->managed_window, &window->id, view)) return MANAGED_WINDOW_FAILED;

    struct view *tiled = space_manager_tile_window_on_space_with_insertion_point(sm, window, sid, insertion_point);
    if (!tiled || !view_find_window_node(view, window->id) ||
        !table_replace(&wm->managed_window, &window->id, view)) {
        table_remove(&wm->managed_window, &window->id);
        return MANAGED_WINDOW_FAILED;
    }

    window_manager_purify_window(wm, window);
    if (wm == &g_window_manager && declarative_installed_has_scaffold()) {
        declarative_configuration_request_reconcile();
    }
    if (flush) space_manager_flush_tiled_window(view, window);
    return MANAGED_WINDOW_SUCCESS;
}

void window_manager_window_did_order_out(struct window_manager *wm, struct window *window)
{
    window_set_flag(window, WINDOW_ORDERED_OUT);

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) {
        space_manager_untile_window(view, window);
        window_manager_remove_managed_window(wm, window->id);
        window_manager_purify_window(wm, window);
    }
}

// NOTE: sid is the active space when the window is on it, and 0 otherwise; a window
// ordered in on an inactive space is tiled when that space becomes active.
void window_manager_window_did_order_in(struct window_manager *wm, struct window *window, uint64_t sid)
{
    window_clear_flag(window, WINDOW_ORDERED_OUT);

    if (!sid) return;
    if (!window_manager_should_manage_window(window)) return;
    if (window_manager_find_managed_window(wm, window)) return;

    struct window *last_window = window_manager_find_window(wm, wm->last_window_id);
    uint32_t insertion_point = last_window && last_window->application->pid != window->application->pid ? last_window->id : 0;
    enum managed_window_result managed_result = window_manager_tile_window_on_space(&g_space_manager, wm, window, sid, insertion_point, true);
    if (managed_result == MANAGED_WINDOW_FAILED) return;
}

enum window_op_error window_manager_adjust_window_ratio(struct window_manager *wm, struct window *window, int type, float ratio)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    switch (type) {
    case TYPE_REL: {
        node->parent->ratio = clampf_range(node->parent->ratio + ratio, 0.1f, 0.9f);
    } break;
    case TYPE_ABS: {
        node->parent->ratio = clampf_range(ratio, 0.1f, 0.9f);
    } break;
    }

    window_node_update(view, node->parent);

    if (space_is_visible(view->sid)) {
        window_node_flush(node->parent);
    } else {
        view_set_flag(view, VIEW_IS_DIRTY);
    }

    return WINDOW_OP_ERROR_SUCCESS;
}

enum window_op_error window_manager_move_window_relative(struct window_manager *wm, struct window *window, int type, float dx, float dy)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    if (type == TYPE_REL) {
        dx += window->frame.origin.x;
        dy += window->frame.origin.y;
    }

    window_manager_animate_window((struct window_capture) { .window = window, .x = dx, .y = dy, .w = window->frame.size.width, .h = window->frame.size.height });
    return WINDOW_OP_ERROR_SUCCESS;
}

void window_manager_resize_window_relative_internal(struct window *window, CGRect frame, int direction, float dx, float dy, bool animate)
{
    TIME_FUNCTION;

    int x_mod = (direction & HANDLE_LEFT) ? -1 : (direction & HANDLE_RIGHT)  ? 1 : 0;
    int y_mod = (direction & HANDLE_TOP)  ? -1 : (direction & HANDLE_BOTTOM) ? 1 : 0;

    float fw = max(1, frame.size.width  + dx * x_mod);
    float fh = max(1, frame.size.height + dy * y_mod);
    float fx = (direction & HANDLE_LEFT) ? frame.origin.x + frame.size.width  - fw : frame.origin.x;
    float fy = (direction & HANDLE_TOP)  ? frame.origin.y + frame.size.height - fh : frame.origin.y;

    if (animate) {
        window_manager_animate_window((struct window_capture) { .window = window, .x = fx, .y = fy, .w = fw, .h = fh });
    } else {
        AX_ENHANCED_UI_WORKAROUND(window->application->ref, {
            window_manager_move_window(window, fx, fy);
            window_manager_resize_window(window, fw, fh);
        });
    }
}

enum window_op_error window_manager_resize_window_relative(struct window_manager *wm, struct window *window, int direction, float dx, float dy, bool animate)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) {
        if (direction == HANDLE_ABS) return WINDOW_OP_ERROR_INVALID_OPERATION;

        struct window_node *node = view_find_window_node(view, window->id);
        if (!node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

        struct window_node *x_fence = NULL;
        struct window_node *y_fence = NULL;

        if (direction & HANDLE_TOP)    x_fence = window_node_fence(node, DIR_NORTH);
        if (direction & HANDLE_BOTTOM) x_fence = window_node_fence(node, DIR_SOUTH);
        if (direction & HANDLE_LEFT)   y_fence = window_node_fence(node, DIR_WEST);
        if (direction & HANDLE_RIGHT)  y_fence = window_node_fence(node, DIR_EAST);
        if (!x_fence && !y_fence)      return WINDOW_OP_ERROR_INVALID_DST_NODE;

        if (y_fence) {
            float sr = y_fence->ratio + (float) dx / (float) y_fence->area.w;
            y_fence->ratio = clampf_range(sr, 0.1f, 0.9f);
        }

        if (x_fence) {
            float sr = x_fence->ratio + (float) dy / (float) x_fence->area.h;
            x_fence->ratio = clampf_range(sr, 0.1f, 0.9f);
        }

        view_update(view);
        view_flush(view);
    } else {
        if (direction == HANDLE_ABS) {
            if (animate) {
                window_manager_animate_window((struct window_capture) { .window = window, .x = window->frame.origin.x, .y = window->frame.origin.y, .w = dx, .h = dy });
            } else {
                AX_ENHANCED_UI_WORKAROUND(window->application->ref, { window_manager_resize_window(window, dx, dy); });
            }
        } else {
            window_manager_resize_window_relative_internal(window, window_ax_frame(window), direction, dx, dy, animate);
        }
    }

    return WINDOW_OP_ERROR_SUCCESS;
}

static AXError window_manager_move_window_internal(struct window *window, float x, float y)
{
    CGPoint position = CGPointMake(x, y);
    CFTypeRef position_ref = AXValueCreate(kAXValueTypeCGPoint, (void *) &position);
    if (!position_ref) return kAXErrorFailure;

    AXError result = window_manager_set_attribute_value(window->ref, kAXPositionAttribute, position_ref);
    CFRelease(position_ref);
    return result;
}

void window_manager_move_window(struct window *window, float x, float y)
{
    if (window_manager_move_window_internal(window, x, y) == kAXErrorInvalidUIElement &&
        window_manager_refresh_window_ref(&g_window_manager, window)) {
        (void) window_manager_move_window_internal(window, x, y);
    }
}

static AXError window_manager_resize_window_internal(struct window *window, float width, float height)
{
    CGSize size = CGSizeMake(width, height);
    CFTypeRef size_ref = AXValueCreate(kAXValueTypeCGSize, (void *) &size);
    if (!size_ref) return kAXErrorFailure;

    AXError result = window_manager_set_attribute_value(window->ref, kAXSizeAttribute, size_ref);
    CFRelease(size_ref);
    return result;
}

void window_manager_resize_window(struct window *window, float width, float height)
{
    if (window_manager_resize_window_internal(window, width, height) == kAXErrorInvalidUIElement &&
        window_manager_refresh_window_ref(&g_window_manager, window)) {
        (void) window_manager_resize_window_internal(window, width, height);
    }
}

struct jankyborders_animation_message
{
    uint32_t event;
    uint32_t count;
    uint32_t proxy_wid[512];
    uint32_t real_wid[512];
};

#ifdef TESTS
static int window_manager_pack_jankyborders_batch(struct window_animation *animation_list, int animation_count,
                                                   int start, uint32_t event, bool skip,
                                                   struct jankyborders_animation_message *message)
{
    *message = (struct jankyborders_animation_message) { .event = event };
    int i = start;
    while (i < animation_count && message->count < array_count(message->proxy_wid)) {
        if (!skip || !__atomic_load_n(&animation_list[i].skip, __ATOMIC_RELAXED)) {
            message->proxy_wid[message->count] = animation_list[i].proxy.id;
            message->real_wid[message->count] = animation_list[i].wid;
            ++message->count;
        }
        ++i;
    }
    return i;
}
#endif

#ifdef TESTS
static kern_return_t (*window_manager_jankyborders_send)(mach_port_t, void *, uint32_t) = mach_send;
#else
#define window_manager_jankyborders_send mach_send
#endif

#ifdef TESTS
static bool window_manager_send_jankyborders_batches(mach_port_t port, struct window_animation *animation_list,
                                                       int animation_count, uint32_t event, bool skip)
{
    int cursor = 0;
    while (cursor < animation_count) {
        struct jankyborders_animation_message message;
        int next = window_manager_pack_jankyborders_batch(animation_list, animation_count, cursor, event, skip, &message);
        if (message.count && window_manager_jankyborders_send(port, &message, sizeof(message)) != KERN_SUCCESS) return false;
        cursor = next;
    }
    return true;
}
#endif

static uint64_t window_manager_retry_delay_ms(uint8_t attempt)
{
    uint8_t shift = attempt < 6 ? attempt : 6;
    return 100ULL << shift;
}

struct jankyborders_desired_entry { uint32_t wid; uint64_t generation; };
static struct {
    pthread_mutex_t lock;
    struct jankyborders_desired_entry *entries;
    int count;
    int capacity;
    uint64_t next_generation;
} g_jankyborders_desired = { .lock = PTHREAD_MUTEX_INITIALIZER };

struct jankyborders_record
{
    uint32_t proxy_wid;
    uint32_t real_wid;
};

struct jankyborders_reconciliation
{
    struct jankyborders_record *records;
    int count;
    uint32_t event;
    uint64_t generation;
    uint8_t attempt;
};

static int window_manager_jankyborders_generation_index_locked(uint32_t wid)
{
    for (int i = 0; i < g_jankyborders_desired.count; ++i) {
        if (g_jankyborders_desired.entries[i].wid == wid) return i;
    }
    return -1;
}

#ifdef TESTS
static bool window_manager_jankyborders_generation_matches(uint32_t wid, uint64_t generation)
{
    pthread_mutex_lock(&g_jankyborders_desired.lock);
    int index = window_manager_jankyborders_generation_index_locked(wid);
    bool matches = index >= 0 && g_jankyborders_desired.entries[index].generation == generation;
    pthread_mutex_unlock(&g_jankyborders_desired.lock);
    return matches;
}
#endif

static void window_manager_jankyborders_complete_stop_locked(struct jankyborders_reconciliation *state)
{
    if (state->event != 1326) return;
    for (int i = 0; i < state->count; ++i) {
        int index = window_manager_jankyborders_generation_index_locked(state->records[i].real_wid);
        if (index >= 0 && g_jankyborders_desired.entries[index].generation == state->generation) {
            g_jankyborders_desired.entries[index] = g_jankyborders_desired.entries[--g_jankyborders_desired.count];
        }
    }
}

static void window_manager_jankyborders_complete_stop(struct jankyborders_reconciliation *state)
{
    pthread_mutex_lock(&g_jankyborders_desired.lock);
    window_manager_jankyborders_complete_stop_locked(state);
    pthread_mutex_unlock(&g_jankyborders_desired.lock);
}

static struct jankyborders_reconciliation *window_manager_copy_jankyborders_state(struct window_animation *animation_list,
                                                                                  int animation_count,
                                                                                  uint32_t event, bool skip)
{
    int count = 0;
    for (int i = 0; i < animation_count; ++i) {
        if (!skip || !__atomic_load_n(&animation_list[i].skip, __ATOMIC_ACQUIRE)) ++count;
    }

    struct jankyborders_reconciliation *state = calloc(1, sizeof(*state));
    if (!state) goto fail;
    state->records = count ? calloc((size_t) count, sizeof(*state->records)) : NULL;
    if (count && !state->records) goto fail;
    state->count = count;
    state->event = event;
    for (int i = 0, out = 0; i < animation_count; ++i) {
        if (skip && __atomic_load_n(&animation_list[i].skip, __ATOMIC_ACQUIRE)) continue;
        state->records[out++] = (struct jankyborders_record) {
            .proxy_wid = animation_list[i].proxy.id,
            .real_wid = animation_list[i].wid
        };
    }

    pthread_mutex_lock(&g_jankyborders_desired.lock);
    int required = g_jankyborders_desired.count + count;
    if (required > g_jankyborders_desired.capacity) {
        int capacity = g_jankyborders_desired.capacity ? g_jankyborders_desired.capacity : 16;
        while (capacity < required) capacity *= 2;
        void *entries = realloc(g_jankyborders_desired.entries, (size_t) capacity * sizeof(*g_jankyborders_desired.entries));
        if (!entries) {
            pthread_mutex_unlock(&g_jankyborders_desired.lock);
            goto fail;
        }
        g_jankyborders_desired.entries = entries;
        g_jankyborders_desired.capacity = capacity;
    }
    if (++g_jankyborders_desired.next_generation == 0) ++g_jankyborders_desired.next_generation;
    state->generation = g_jankyborders_desired.next_generation;
    for (int i = 0; i < count; ++i) {
        int index = window_manager_jankyborders_generation_index_locked(state->records[i].real_wid);
        if (index < 0) index = g_jankyborders_desired.count++;
        g_jankyborders_desired.entries[index] = (struct jankyborders_desired_entry) {
            .wid = state->records[i].real_wid,
            .generation = state->generation
        };
    }
    pthread_mutex_unlock(&g_jankyborders_desired.lock);
    return state;

fail:
    pthread_mutex_lock(&g_jankyborders_desired.lock);
    if (++g_jankyborders_desired.next_generation == 0) ++g_jankyborders_desired.next_generation;
    for (int i = 0; i < animation_count; ++i) {
        if (skip && __atomic_load_n(&animation_list[i].skip, __ATOMIC_ACQUIRE)) continue;
        int index = window_manager_jankyborders_generation_index_locked(animation_list[i].wid);
        if (index < 0) continue;
        if (event == 1326) {
            g_jankyborders_desired.entries[index] = g_jankyborders_desired.entries[--g_jankyborders_desired.count];
        } else {
            g_jankyborders_desired.entries[index].generation = g_jankyborders_desired.next_generation;
        }
    }
    pthread_mutex_unlock(&g_jankyborders_desired.lock);
    warn("%s: unable to retain cosmetic border reconciliation state\n", __FUNCTION__);
    if (state) { free(state->records); free(state); }
    return NULL;
}

static bool window_manager_send_jankyborders_state(mach_port_t port, struct jankyborders_reconciliation *state)
{
    int capacity = (state->count + 511) / 512;
    struct jankyborders_animation_message *messages = capacity ? calloc((size_t) capacity, sizeof(*messages)) : NULL;
    if (capacity && !messages) return false;
    int message_count = 0;

    pthread_mutex_lock(&g_jankyborders_desired.lock);
    for (int i = 0; i < state->count; ++i) {
        int index = window_manager_jankyborders_generation_index_locked(state->records[i].real_wid);
        if (index < 0 || g_jankyborders_desired.entries[index].generation != state->generation) continue;
        if (!message_count || messages[message_count - 1].count == 512) {
            messages[message_count++].event = state->event;
        }
        struct jankyborders_animation_message *message = &messages[message_count - 1];
        message->proxy_wid[message->count] = state->records[i].proxy_wid;
        message->real_wid[message->count] = state->records[i].real_wid;
        ++message->count;
    }

    bool success = true;
    for (int i = 0; i < message_count; ++i) {
        if (window_manager_jankyborders_send(port, &messages[i], sizeof(messages[i])) != KERN_SUCCESS) {
            success = false;
            break;
        }
    }
    if (success) window_manager_jankyborders_complete_stop_locked(state);
    pthread_mutex_unlock(&g_jankyborders_desired.lock);
    free(messages);
    return success;
}

static void window_manager_free_jankyborders_state(struct jankyborders_reconciliation *state)
{
    if (!state) return;
    free(state->records);
    free(state);
}

static void window_manager_jankyborders_retry(struct jankyborders_reconciliation *state);
static void window_manager_jankyborders_dispatch_retry(struct jankyborders_reconciliation *state, uint64_t delay_ms)
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ms * NSEC_PER_MSEC),
                   dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        window_manager_jankyborders_retry(state);
    });
}
#ifdef TESTS
static void (*window_manager_jankyborders_retry_scheduler)(struct jankyborders_reconciliation *, uint64_t) = window_manager_jankyborders_dispatch_retry;
#else
#define window_manager_jankyborders_retry_scheduler window_manager_jankyborders_dispatch_retry
#endif

static void window_manager_jankyborders_schedule_retry(struct jankyborders_reconciliation *state)
{
    uint64_t delay = window_manager_retry_delay_ms(state->attempt);
    if (state->attempt != UINT8_MAX) ++state->attempt;
    window_manager_jankyborders_retry_scheduler(state, delay);
}

static void window_manager_jankyborders_retry(struct jankyborders_reconciliation *state)
{
    mach_port_t port;
    if (!g_bs_port || bootstrap_look_up(g_bs_port, "git.felix.jbevent", &port) != KERN_SUCCESS) {
        window_manager_jankyborders_schedule_retry(state);
        return;
    }
    bool success = window_manager_send_jankyborders_state(port, state);
    mach_port_deallocate(mach_task_self(), port);
    if (success) window_manager_free_jankyborders_state(state);
    else window_manager_jankyborders_schedule_retry(state);
}

static bool window_manager_reconcile_jankyborders_state(mach_port_t port, struct jankyborders_reconciliation *state)
{
    if (!state) return false;
    if (window_manager_send_jankyborders_state(port, state) || window_manager_send_jankyborders_state(port, state)) {
        window_manager_free_jankyborders_state(state);
        return true;
    }
    window_manager_jankyborders_schedule_retry(state);
    return false;
}

static bool window_manager_send_prepared_jankyborders(struct jankyborders_reconciliation *state, bool wait)
{
    if (!state) return false;
    mach_port_t port;
    if (!g_bs_port || bootstrap_look_up(g_bs_port, "git.felix.jbevent", &port) != KERN_SUCCESS) {
        window_manager_jankyborders_complete_stop(state);
        window_manager_free_jankyborders_state(state);
        return true;
    }
    bool success = window_manager_reconcile_jankyborders_state(port, state);
    mach_port_deallocate(mach_task_self(), port);
    if (success && wait) usleep(20000);
    return success;
}

static void window_manager_destroy_window_proxy(int animation_connection, struct window_proxy *proxy);

#ifdef TESTS
static CGError (*window_manager_proxy_new_region)(CGRect *, CFTypeRef *) = CGSNewRegionWithRect;
static CFTypeRef (*window_manager_proxy_empty_region)(void) = CGRegionCreateEmptyRegion;
static CGError (*window_manager_proxy_new_window)(int, int, CFTypeRef, CFTypeRef, int, uint64_t *, float, float, int, uint32_t *, void *) = SLSNewWindowWithOpaqueShapeAndContext;
static CGContextRef (*window_manager_proxy_new_context)(int, uint32_t, CFDictionaryRef) = SLWindowContextCreate;
static CGError (*window_manager_proxy_release_window)(int, uint32_t) = SLSReleaseWindow;
static CFArrayRef (*window_manager_proxy_capture)(int, uint32_t *, int, uint32_t) = SLSHWCaptureWindowList;
#else
#define window_manager_proxy_new_region CGSNewRegionWithRect
#define window_manager_proxy_empty_region CGRegionCreateEmptyRegion
#define window_manager_proxy_new_window SLSNewWindowWithOpaqueShapeAndContext
#define window_manager_proxy_new_context SLWindowContextCreate
#define window_manager_proxy_release_window SLSReleaseWindow
#define window_manager_proxy_capture SLSHWCaptureWindowList
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
    // Shadow suppression is cosmetic; keep the animation proxy usable if it fails.
    (void) sls_window_disable_shadow(proxy->id);
    SLSSetWindowOpacity(animation_connection, proxy->id, 0);
    SLSSetWindowResolution(animation_connection, proxy->id, 2.0f);
    SLSSetWindowAlpha(animation_connection, proxy->id, alpha);
    SLSSetWindowLevel(animation_connection, proxy->id, proxy->level);
    SLSSetWindowSubLevel(animation_connection, proxy->id, proxy->sub_level);

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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static CVReturn window_manager_animate_window_list_thread_proc(CVDisplayLinkRef link, const CVTimeStamp *now, const CVTimeStamp *output_time, CVOptionFlags flags, CVOptionFlags *flags_out, void *data)
{
    struct window_animation_context *context = data;
    int animation_count = context->animation_count;

    uint64_t current_clock = output_time->hostTime;
    if (!context->animation_clock) context->animation_clock = now->hostTime;

    double t = (double)(current_clock - context->animation_clock) / (double)(context->animation_duration * g_cv_host_clock_frequency);
    if (t <= 0.0) t = 0.0f;
    if (t >= 1.0) t = 1.0f;

    float mt;
    switch (context->animation_easing) {
#define ANIMATION_EASING_TYPE_ENTRY(value) case value##_type: mt = value(t); break;
        ANIMATION_EASING_TYPE_LIST
#undef ANIMATION_EASING_TYPE_ENTRY
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
        goto out;
    }

    bool claimed = window_manager_animation_claim_completion(context);
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);
    if (claimed) window_manager_animation_teardown_scheduler(context);

out:
    return kCVReturnSuccess;
}
#pragma clang diagnostic pop

#ifdef TESTS
static void *(*window_manager_animation_allocate)(size_t) = malloc;
static CGError (*window_manager_animation_new_connection)(int, int *) = SLSNewConnection;
static CVReturn (*window_manager_animation_display_link_create)(CVDisplayLinkRef *) = CVDisplayLinkCreateWithActiveCGDisplays;
static CVReturn (*window_manager_animation_display_link_set_callback)(CVDisplayLinkRef, CVDisplayLinkOutputCallback, void *) = CVDisplayLinkSetOutputCallback;
static CVReturn (*window_manager_animation_display_link_start)(CVDisplayLinkRef) = CVDisplayLinkStart;
static CVReturn (*window_manager_animation_display_link_stop)(CVDisplayLinkRef) = CVDisplayLinkStop;
static void (*window_manager_animation_display_link_release)(CVDisplayLinkRef) = CVDisplayLinkRelease;
static CGError (*window_manager_animation_release_connection)(int) = SLSReleaseConnection;
static CGError (*window_manager_animation_disable_update)(int) = SLSDisableUpdate;
static CGError (*window_manager_animation_reenable_update)(int) = SLSReenableUpdate;
static void (*window_manager_animation_set_frame)(struct window *, float, float, float, float) = window_manager_set_window_frame;
#else
#define window_manager_animation_allocate malloc
#define window_manager_animation_new_connection SLSNewConnection
#define window_manager_animation_display_link_create CVDisplayLinkCreateWithActiveCGDisplays
#define window_manager_animation_display_link_set_callback CVDisplayLinkSetOutputCallback
#define window_manager_animation_display_link_start CVDisplayLinkStart
#define window_manager_animation_display_link_stop CVDisplayLinkStop
#define window_manager_animation_display_link_release CVDisplayLinkRelease
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

static bool window_manager_animation_start_display_link(struct window_animation_context *context)
{
    if (window_manager_animation_display_link_create(&context->display_link) != kCVReturnSuccess || !context->display_link) return false;
    if (window_manager_animation_display_link_set_callback(context->display_link, window_manager_animate_window_list_thread_proc, context) != kCVReturnSuccess) return false;
    context->display_link_may_be_running = true;
    return window_manager_animation_display_link_start(context->display_link) == kCVReturnSuccess;
}

static void window_manager_animation_release_context(struct window_animation_context *context, bool update_disabled)
{
    if (update_disabled) (void) window_manager_animation_reenable_update(context->animation_connection);
    if (context->display_link) window_manager_animation_display_link_release(context->display_link);
    if (context->animation_connection) (void) window_manager_animation_release_connection(context->animation_connection);
    free(context->animation_list);
    free(context);
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
    if (context->display_link_may_be_running) {
        CVReturn stop_result = window_manager_animation_display_link_stop(context->display_link);
        if (stop_result != kCVReturnSuccess && stop_result != kCVReturnDisplayLinkNotRunning) {
            window_manager_animation_retry(context);
            return;
        }
        context->display_link_may_be_running = false;
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
    struct jankyborders_reconciliation *border_state =
        window_manager_copy_jankyborders_state(context->animation_list, context->animation_count, 1326, true);

    for (int i = 0; i < context->animation_count; ++i) {
        struct window_animation *animation = &context->animation_list[i];
        if (table_find(&g_window_manager.window_animations_table, &animation->wid) == animation) {
            table_remove(&g_window_manager.window_animations_table, &animation->wid);
            window_manager_destroy_window_proxy(context->animation_connection, &animation->proxy);
        }
    }
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);
    (void) window_manager_send_prepared_jankyborders(border_state, true);
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
    for (int i = 0; i < context->animation_count; ++i) {
        struct window_animation *animation = &context->animation_list[i];
        window_manager_animation_set_frame(animation->window, animation->x, animation->y, animation->w, animation->h);
    }
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

    pthread_t *threads = window_manager_animation_allocate((size_t) window_count * sizeof(pthread_t));
    if (!threads) goto cleanup;
    struct jankyborders_reconciliation **replacement_border_states = calloc((size_t) window_count * 2,
                                                                            sizeof(*replacement_border_states));
    if (!replacement_border_states) warn("%s: unable to retain replacement border state\n", __FUNCTION__);

    int thread_count = 0;
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

        bool add_to_table = true;
        struct window_animation *existing_animation = table_find(&g_window_manager.window_animations_table, &context->animation_list[i].wid);
        if (existing_animation) {
            context->animation_list[i].proxy.frame = (CGRect) { { existing_animation->proxy.tx, existing_animation->proxy.ty },
                                                                { existing_animation->proxy.tw, existing_animation->proxy.th } };
            context->animation_list[i].proxy.tx = existing_animation->proxy.tx;
            context->animation_list[i].proxy.ty = existing_animation->proxy.ty;
            context->animation_list[i].proxy.tw = existing_animation->proxy.tw;
            context->animation_list[i].proxy.th = existing_animation->proxy.th;
            context->animation_list[i].proxy.level = existing_animation->proxy.level;
            context->animation_list[i].proxy.sub_level = existing_animation->proxy.sub_level;
            context->animation_list[i].proxy.image = existing_animation->proxy.image ?
                                                     (CGImageRef) CFRetain(existing_animation->proxy.image) : NULL;

            float alpha = 1.0f;
            SLSGetWindowAlpha(context->animation_connection, context->animation_list[i].wid, &alpha);
            if (!window_manager_create_window_proxy(context->animation_connection, alpha, &context->animation_list[i].proxy)) {
                __atomic_store_n(&context->animation_list[i].skip, true, __ATOMIC_RELEASE);
                add_to_table = false;
            } else {
                __atomic_store_n(&existing_animation->skip, true, __ATOMIC_RELEASE);
                if (replacement_border_states) {
                    replacement_border_states[i * 2] = window_manager_copy_jankyborders_state(existing_animation, 1, 1326, false);
                    replacement_border_states[i * 2 + 1] = window_manager_copy_jankyborders_state(&context->animation_list[i], 1, 1325, true);
                }

                CFTypeRef transaction = SLSTransactionCreate(context->animation_connection);
                if (transaction) {
                    SLSTransactionOrderWindowGroup(transaction, context->animation_list[i].proxy.id, 1, context->animation_list[i].wid);
                    SLSTransactionSetWindowSystemAlpha(transaction, existing_animation->proxy.id, 0);
                    SLSTransactionCommit(transaction, 0);
                    CFRelease(transaction);
                }
                table_remove(&g_window_manager.window_animations_table, &context->animation_list[i].wid);
                window_manager_destroy_window_proxy(existing_animation->cid, &existing_animation->proxy);
            }
        } else {
            pthread_t thread;
            if (pthread_create(&thread, NULL, &window_manager_build_window_proxy_thread_proc, &context->animation_list[i]) == 0) {
                threads[thread_count++] = thread;
            } else {
                window_manager_build_window_proxy_thread_proc(&context->animation_list[i]);
            }
        }
        if (add_to_table) table_add(&g_window_manager.window_animations_table, &context->animation_list[i].wid, &context->animation_list[i]);
    }
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);

    if (replacement_border_states) {
        for (int i = 0; i < window_count * 2; ++i) {
            if (replacement_border_states[i]) (void) window_manager_send_prepared_jankyborders(replacement_border_states[i], false);
        }
        free(replacement_border_states);
    }

    for (int i = 0; i < thread_count; ++i) pthread_join(threads[i], NULL);
    free(threads);

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
    struct jankyborders_reconciliation *start_border_state =
        window_manager_copy_jankyborders_state(context->animation_list, context->animation_count, 1325, true);
    pthread_mutex_unlock(&g_window_manager.window_animations_lock);
    (void) window_manager_send_prepared_jankyborders(start_border_state, false);

    (void) window_manager_animation_reenable_update(context->animation_connection);
    update_disabled = false;
    if (!window_manager_animation_start_display_link(context)) goto cleanup_ready;
    return;

cleanup_ready:
    window_manager_animation_setup_cleanup(context, update_disabled);
    return;
cleanup:
    window_manager_animation_setup_cleanup(context, false);
    goto set_final_frames;
set_final_frames:
    for (int i = 0; i < window_count; ++i) {
        window_manager_animation_set_frame(window_list[i].window, window_list[i].x, window_list[i].y,
                                           window_list[i].w, window_list[i].h);
    }
}
#ifdef TESTS
static void (*window_manager_test_animate_observer)(struct window_capture *, int);
#endif

void window_manager_animate_window_list(struct window_capture *window_list, int window_count)
{
    TIME_FUNCTION;
#ifdef TESTS
    if (window_manager_test_animate_observer) {
        window_manager_test_animate_observer(window_list, window_count);
        return;
    }
#endif

    if (g_window_manager.window_animation_duration) {
        window_manager_animate_window_list_async(window_list, window_count);
    } else {
        for (int i = 0; i < window_count; ++i) {
            window_manager_set_window_frame(window_list[i].window, window_list[i].x, window_list[i].y, window_list[i].w, window_list[i].h);
        }
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

    if (g_window_manager.window_animation_duration) {
        window_manager_animate_window_list_async(&capture, 1);
    } else {
        window_manager_set_window_frame(capture.window, capture.x, capture.y, capture.w, capture.h);
    }
}

static AXError window_manager_set_window_frame_internal(struct window *window, float x, float y, float width, float height)
{
    //
    // NOTE(asmvik): Attempting to check the window frame cache to prevent unnecessary movement and resize calls to the AX API
    // is not reliable because it is possible to perform operations that should be applied, at a higher rate than the AX API events
    // are received, causing our cache to become out of date and incorrectly guard against some changes that **should** be applied.
    // This causes the window layout to **not** be modified the way we expect.
    //
    // A possible solution is to use the faster CG window notifications, as they are **a lot** more responsive, and can be used to
    // track changes to the window frame in real-time without delay.
    //

    AXError result = kAXErrorSuccess;
    AX_ENHANCED_UI_WORKAROUND(window->application->ref, {
        CGPoint position = CGPointMake(x, y);
        CFTypeRef position_ref = AXValueCreate(kAXValueTypeCGPoint, (void *) &position);

        CGSize size = CGSizeMake(width, height);
        CFTypeRef size_ref = AXValueCreate(kAXValueTypeCGSize, (void *) &size);

        // NOTE(asmvik): Due to macOS constraints (visible screen-area), we might need to resize the window *before* moving it.
        if (size_ref) {
            AXError error = window_manager_set_attribute_value(window->ref, kAXSizeAttribute, size_ref);
            if (result == kAXErrorSuccess && error != kAXErrorSuccess) result = error;
        }

        if (position_ref) {
            AXError error = window_manager_set_attribute_value(window->ref, kAXPositionAttribute, position_ref);
            if (result == kAXErrorSuccess && error != kAXErrorSuccess) result = error;
            CFRelease(position_ref);
        }

        // NOTE(asmvik): Due to macOS constraints (visible screen-area), we might need to resize the window *after* moving it.
        if (size_ref) {
            AXError error = window_manager_set_attribute_value(window->ref, kAXSizeAttribute, size_ref);
            if (result == kAXErrorSuccess && error != kAXErrorSuccess) result = error;
            CFRelease(size_ref);
        }
    });
    return result;
}

void window_manager_set_window_frame(struct window *window, float x, float y, float width, float height)
{
    if (window_manager_set_window_frame_internal(window, x, y, width, height) == kAXErrorInvalidUIElement &&
        window_manager_refresh_window_ref(&g_window_manager, window)) {
        (void) window_manager_set_window_frame_internal(window, x, y, width, height);
    }
}

void window_manager_set_purify_mode(struct window_manager *wm, enum purify_mode mode)
{
    wm->purify_mode = mode;
    table_for (struct window *window, wm->window, {
        if (window_manager_is_window_eligible(window)) {
            window_manager_purify_window(wm, window);
        }
    })
}

bool window_manager_set_opacity(struct window_manager *wm, struct window *window, float opacity)
{
    if (opacity == 0.0f) {
        if (wm->enable_window_opacity) {
            opacity = window->id == wm->focused_window_id ? wm->active_window_opacity : wm->normal_window_opacity;
        } else {
            opacity = 1.0f;
        }
    }

    return scripting_addition_set_opacity(window->id, opacity, wm->window_opacity_duration);
}

void window_manager_set_window_opacity(struct window_manager *wm, struct window *window, float opacity)
{
    if (!wm->enable_window_opacity)                 return;
    if (!window_manager_is_window_eligible(window)) return;
    if (window->opacity != 0.0f)                    return;

    window_manager_set_opacity(wm, window, opacity);
}

void window_manager_set_menubar_opacity(struct window_manager *wm, float opacity)
{
    wm->menubar_opacity = opacity;
    SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, opacity);
}

void window_manager_set_active_window_opacity(struct window_manager *wm, float opacity)
{
    wm->active_window_opacity = opacity;
    struct window *window = window_manager_focused_window(wm);
    if (window) window_manager_set_window_opacity(wm, window, wm->active_window_opacity);
}

void window_manager_set_normal_window_opacity(struct window_manager *wm, float opacity)
{
    wm->normal_window_opacity = opacity;
    table_for (struct window *window, wm->window, {
        if (window->id == wm->focused_window_id) continue;
        if (window_manager_is_window_eligible(window)) {
            window_manager_set_window_opacity(wm, window, wm->normal_window_opacity);
        }
    })
}

#ifdef TESTS
static void (*window_manager_test_layer_observer)(uint32_t wid, int layer);
#endif

void window_manager_adjust_layer(struct window *window, int layer)
{
    if (window->layer != LAYER_AUTO) return;

#ifdef TESTS
    if (window_manager_test_layer_observer) window_manager_test_layer_observer(window->id, layer);
#endif
    scripting_addition_set_layer(window->id, layer);
}

bool window_manager_set_window_layer(struct window *window, int layer)
{
    int parent_layer = layer;
    int child_layer = layer;

    if (layer == LAYER_AUTO) {
        parent_layer = window_manager_find_managed_window(&g_window_manager, window) ? LAYER_BELOW : LAYER_NORMAL;
        child_layer = LAYER_NORMAL;
    }

    window->layer = layer;
    bool result = scripting_addition_set_layer(window->id, parent_layer);
    if (!result) return false;

    CFArrayRef window_list = SLSCopyAssociatedWindows(g_connection, window->id);
    if (!window_list) return result;

    int window_count = CFArrayGetCount(window_list);
    if (!window_count) {
        CFRelease(window_list);
        return result;
    }

    CFTypeRef query = SLSWindowQueryWindows(g_connection, window_list, window_count);
    if (!query) {
        CFRelease(window_list);
        return result;
    }

    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (!iterator) {
        CFRelease(query);
        CFRelease(window_list);
        return result;
    }

    int relation_count = 0;
    uint32_t parent_list[window_count];
    uint32_t child_list[window_count];

    while (relation_count < window_count && SLSWindowIteratorAdvance(iterator)) {
        parent_list[relation_count] = SLSWindowIteratorGetParentID(iterator);
        child_list[relation_count] = SLSWindowIteratorGetWindowID(iterator);
        ++relation_count;
    }

    int check_count = 1;
    uint32_t check_list[window_count + 1];
    check_list[0] = window->id;

    for (int i = 0; i < check_count; ++i) {
        for (int j = 0; j < relation_count; ++j) {
            if (parent_list[j] != check_list[i]) continue;
            scripting_addition_set_layer(child_list[j], child_layer);
            if (check_count <= window_count) check_list[check_count++] = child_list[j];
        }
    }

    CFRelease(iterator);
    CFRelease(query);
    CFRelease(window_list);

    return result;
}

void window_manager_purify_window(struct window_manager *wm, struct window *window)
{
    int value;

    if (wm->purify_mode == PURIFY_DISABLED) {
        value = 1;
    } else if (wm->purify_mode == PURIFY_MANAGED) {
        value = window_manager_find_managed_window(wm, window) ? 0 : 1;
    } else /*if (wm->purify_mode == PURIFY_ALWAYS) */ {
        value = 0;
    }

    if (scripting_addition_set_shadow(window->id, value)) {
        if (value) {
            window_set_flag(window, WINDOW_SHADOW);
        } else {
            window_clear_flag(window, WINDOW_SHADOW);
        }
    }
}

int window_manager_find_rank_of_window_in_list(uint32_t wid, uint32_t *window_list, int window_count)
{
    for (int i = 0, rank = 0; i < window_count; ++i) {
        if (window_list[i] == wid) {
            return rank;
        } else {
            ++rank;
        }
    }

    return INT_MAX;
}

struct window *window_manager_find_window_on_space_by_rank_filtering_window(struct window_manager *wm, uint64_t sid, int rank, uint32_t filter_wid)
{
    int count;
    uint32_t *window_list = space_window_list(sid, &count, false);
    if (!window_list) return NULL;

    struct window *result = NULL;
    for (int i = 0, j = 0; i < count; ++i) {
        if (window_list[i] == filter_wid) continue;

        struct window *window = window_manager_find_window(wm, window_list[i]);
        if (!window) continue;

        if (++j == rank) {
            result = window;
            break;
        }
    }

    return result;
}

static inline bool window_manager_window_connection_is_jankyborders(int window_cid)
{
    static char process_name[PROC_PIDPATHINFO_MAXSIZE];

    pid_t window_pid = 0;
    process_name[0] = '\0';
    if (SLSConnectionGetPID(window_cid, &window_pid) != kCGErrorSuccess || window_pid <= 0) return false;
    proc_name(window_pid, process_name, sizeof(process_name));

    return strcmp(process_name, "borders") == 0;
}

struct window *window_manager_find_window_at_point_filtering_window(struct window_manager *wm, CGPoint point, uint32_t filter_wid)
{
    CGPoint window_point = CGPointZero;
    uint32_t window_id = 0;
    int window_cid = 0;

    if (SLSFindWindowAndOwner(g_connection, filter_wid, -1, 0, &point, &window_point, &window_id, &window_cid) != noErr) return NULL;
    if (g_connection == window_cid &&
        SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid) != noErr) return NULL;

    if (window_manager_window_connection_is_jankyborders(window_cid)) {
        if (SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid) != noErr) return NULL;
        if (g_connection == window_cid &&
            SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid) != noErr) return NULL;
    }

    return window_manager_find_window(wm, window_id);
}

struct window *window_manager_find_window_at_point(struct window_manager *wm, CGPoint point)
{
    CGPoint window_point = CGPointZero;
    uint32_t window_id = 0;
    int window_cid = 0;

    if (SLSFindWindowAndOwner(g_connection, 0, 1, 0, &point, &window_point, &window_id, &window_cid) != noErr) return NULL;
    if (g_connection == window_cid &&
        SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid) != noErr) return NULL;

    if (window_manager_window_connection_is_jankyborders(window_cid)) {
        if (SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid) != noErr) return NULL;
        if (g_connection == window_cid &&
            SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid) != noErr) return NULL;
    }

    return window_manager_find_window(wm, window_id);
}

struct window *window_manager_find_window_below_cursor(struct window_manager *wm)
{
    CGPoint cursor;
    SLSGetCurrentCursorLocation(g_connection, &cursor);
    return window_manager_find_window_at_point(wm, cursor);
}

struct window *window_manager_find_closest_managed_window_in_direction(struct window_manager *wm, struct window *window, int direction)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    struct window_node *closest = view_find_window_node_in_direction(view, node, direction);
    if (!closest) return NULL;

    return window_manager_find_window(wm, closest->window_order[0]);
}

struct window *window_manager_find_prev_managed_window(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    struct window_node *prev = window_node_find_prev_leaf(node);
    if (!prev) return NULL;

    return window_manager_find_window(wm, prev->window_order[0]);
}

struct window *window_manager_find_next_managed_window(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    struct window_node *next = window_node_find_next_leaf(node);
    if (!next) return NULL;

    return window_manager_find_window(wm, next->window_order[0]);
}

struct window *window_manager_find_first_managed_window(struct space_manager *sm, struct window_manager *wm)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *first = window_node_find_first_leaf(view->root);
    if (!first) return NULL;

    return window_manager_find_window(wm, first->window_order[0]);
}

struct window *window_manager_find_last_managed_window(struct space_manager *sm, struct window_manager *wm)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *last = window_node_find_last_leaf(view->root);
    if (!last) return NULL;

    return window_manager_find_window(wm, last->window_order[0]);
}

bool window_manager_window_is_focus_fallback_eligible(struct window *window)
{
    if (!window || !window->application || !window->id) return false;
    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) return false;
    if (!window_manager_is_window_eligible(window)) return false;
    if (window_check_flag(window, WINDOW_MINIMIZE) || window_check_flag(window, WINDOW_ORDERED_OUT)) return false;
    return !window->application->is_hidden;
}

struct window *window_manager_find_recent_managed_window(struct window_manager *wm)
{
    struct window *window = window_manager_find_window(wm, wm->last_window_id);
    if (!window_manager_window_is_focus_fallback_eligible(window) || !window->ref) return NULL;
    return window_manager_find_managed_window(wm, window) ? window : NULL;
}

struct window *window_manager_find_prev_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    for (int i = 1; i < node->window_count; ++i) {
        if (node->window_list[i] == window->id) {
            return window_manager_find_window(wm, node->window_list[i-1]);
        }
    }

    return NULL;
}

struct window *window_manager_find_next_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    for (int i = 0; i < node->window_count - 1; ++i) {
        if (node->window_list[i] == window->id) {
            return window_manager_find_window(wm, node->window_list[i+1]);
        }
    }

    return NULL;
}

struct window *window_manager_find_first_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    return node->window_count > 1 ? window_manager_find_window(wm, node->window_list[0]) : NULL;
}

struct window *window_manager_find_last_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    return node->window_count > 1 ? window_manager_find_window(wm, node->window_list[node->window_count-1]) : NULL;
}

struct window *window_manager_find_recent_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    return node->window_count > 1 ? window_manager_find_window(wm, node->window_order[1]) : NULL;
}

struct window *window_manager_find_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window, int index)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    return node->window_count > 1 && in_range_ii(index, 1, node->window_count) ? window_manager_find_window(wm, node->window_list[index-1]) : NULL;
}

struct window *window_manager_find_largest_managed_window(struct space_manager *sm, struct window_manager *wm)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    uint32_t best_id = 0;
    double best_area = 0.0;
    bool found = false;

    for (struct window_node *node = window_node_find_first_leaf(view->root); node != NULL; node = window_node_find_next_leaf(node)) {
        double width = node->area.w > 0.0f ? node->area.w : 0.0;
        double height = node->area.h > 0.0f ? node->area.h : 0.0;
        double area = width * height;
        if (!found || area > best_area) {
            best_id   = node->window_order[0];
            best_area = area;
            found = true;
        }
    }

    return best_id ? window_manager_find_window(wm, best_id) : NULL;
}

struct window *window_manager_find_smallest_managed_window(struct space_manager *sm, struct window_manager *wm)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    uint32_t best_id = 0;
    double best_area = 0.0;
    bool found = false;

    for (struct window_node *node = window_node_find_first_leaf(view->root); node != NULL; node = window_node_find_next_leaf(node)) {
        double width = node->area.w > 0.0f ? node->area.w : 0.0;
        double height = node->area.h > 0.0f ? node->area.h : 0.0;
        double area = width * height;
        if (!found || area <= best_area) {
            best_id   = node->window_order[0];
            best_area = area;
            found = true;
        }
    }

    return best_id ? window_manager_find_window(wm, best_id) : NULL;
}

struct window *window_manager_find_sibling_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *sibling_node = window_node_is_left_child(node) ? node->parent->right : node->parent->left;
    if (!window_node_is_leaf(sibling_node)) return NULL;

    return window_manager_find_window(wm, sibling_node->window_order[0]);
}

struct window *window_manager_find_first_nephew_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *sibling_node = window_node_is_left_child(node) ? node->parent->right : node->parent->left;
    if (window_node_is_leaf(sibling_node) || !window_node_is_leaf(sibling_node->left)) return NULL;

    return window_manager_find_window(wm, sibling_node->left->window_order[0]);
}

struct window *window_manager_find_second_nephew_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *sibling_node = window_node_is_left_child(node) ? node->parent->right : node->parent->left;
    if (window_node_is_leaf(sibling_node) || !window_node_is_leaf(sibling_node->right)) return NULL;

    return window_manager_find_window(wm, sibling_node->right->window_order[0]);
}

struct window *window_manager_find_uncle_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *grandparent = node->parent->parent;
    if (!grandparent) return NULL;

    struct window_node *uncle_node = window_node_is_left_child(node->parent) ? grandparent->right : grandparent->left;
    if (!window_node_is_leaf(uncle_node)) return NULL;

    return window_manager_find_window(wm, uncle_node->window_order[0]);
}

struct window *window_manager_find_first_cousin_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *grandparent = node->parent->parent;
    if (!grandparent) return NULL;

    struct window_node *uncle_node = window_node_is_left_child(node->parent) ? grandparent->right : grandparent->left;
    if (window_node_is_leaf(uncle_node) || !window_node_is_leaf(uncle_node->left)) return NULL;

    return window_manager_find_window(wm, uncle_node->left->window_order[0]);
}

struct window *window_manager_find_second_cousin_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *grandparent = node->parent->parent;
    if (!grandparent) return NULL;

    struct window_node *uncle_node = window_node_is_left_child(node->parent) ? grandparent->right : grandparent->left;
    if (window_node_is_leaf(uncle_node) || !window_node_is_leaf(uncle_node->right)) return NULL;

    return window_manager_find_window(wm, uncle_node->right->window_order[0]);
}

static void window_manager_make_key_window(ProcessSerialNumber *window_psn, uint32_t window_id)
{
    //
    // :SynthesizedEvent
    //
    // NOTE(asmvik): These events will be picked up by an event-tap
    // registered at the "Annotated Session" location; specifying that an
    // event-tap is placed at the point where session events have been
    // annotated to flow to an application.
    //

    memset(g_event_bytes, 0, 0xf8);
    g_event_bytes[0x04] = 0xf8;
    g_event_bytes[0x3a] = 0x10;
    memcpy(g_event_bytes + 0x3c, &window_id, sizeof(uint32_t));
    memset(g_event_bytes + 0x20, 0xff, 0x10);

    g_event_bytes[0x08] = 0x01;
    SLPSPostEventRecordTo(window_psn, g_event_bytes);

    g_event_bytes[0x08] = 0x02;
    SLPSPostEventRecordTo(window_psn, g_event_bytes);
}

void window_manager_focus_window_without_raise(ProcessSerialNumber *window_psn, uint32_t window_id)
{
    TIME_FUNCTION;

    if (psn_equals(window_psn, &g_window_manager.focused_window_psn)) {
        memset(g_event_bytes, 0, 0xf8);
        g_event_bytes[0x04] = 0xf8;
        g_event_bytes[0x08] = 0x0d;

        g_event_bytes[0x8a] = 0x02;
        memcpy(g_event_bytes + 0x3c, &g_window_manager.focused_window_id, sizeof(uint32_t));
        SLPSPostEventRecordTo(&g_window_manager.focused_window_psn, g_event_bytes);

        //
        // @hack
        // Artificially delay the activation by 40ms. This is necessary
        // because some applications appear to be confused if both of
        // the events appear instantaneously.
        //

        usleep(40000);

        g_event_bytes[0x8a] = 0x01;
        memcpy(g_event_bytes + 0x3c, &window_id, sizeof(uint32_t));
        SLPSPostEventRecordTo(window_psn, g_event_bytes);
    }

    _SLPSSetFrontProcessWithOptions(window_psn, window_id, kCPSUserGenerated);
    window_manager_make_key_window(window_psn, window_id);
}

void window_manager_focus_window_with_raise(ProcessSerialNumber *window_psn, uint32_t window_id, AXUIElementRef window_ref)
{
    TIME_FUNCTION;

#if 1
    _SLPSSetFrontProcessWithOptions(window_psn, window_id, kCPSUserGenerated);
    window_manager_make_key_window(window_psn, window_id);
    AXUIElementPerformAction(window_ref, kAXRaiseAction);
#else
    scripting_addition_focus_window(window_id);
#endif
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
struct application *window_manager_focused_application(struct window_manager *wm)
{
    TIME_FUNCTION;

    ProcessSerialNumber psn = {0};
    _SLPSGetFrontProcess(&psn);

    pid_t pid;
    GetProcessPID(&psn, &pid);

    return window_manager_find_application(wm, pid);
}

struct window *window_manager_focused_window(struct window_manager *wm)
{
    TIME_FUNCTION;

    struct application *application = window_manager_focused_application(wm);
    if (!application) return NULL;

    uint32_t window_id = application_focused_window(application);
    return window_manager_find_window(wm, window_id);
}
#pragma clang diagnostic pop

uint64_t window_manager_find_lost_front_switched_event(struct window_manager *wm, ProcessSerialNumber *psn)
{
    return (uint64_t)(uintptr_t) table_find(&wm->application_lost_front_switched_event, psn);
}

void window_manager_remove_lost_front_switched_event(struct window_manager *wm, ProcessSerialNumber *psn)
{
    table_remove(&wm->application_lost_front_switched_event, psn);
}

bool window_manager_add_lost_front_switched_event(struct window_manager *wm, ProcessSerialNumber *psn, uint64_t generation)
{
    table_remove(&wm->application_lost_front_switched_event, psn);
    return table_try_add(&wm->application_lost_front_switched_event, psn, (void *)(uintptr_t) generation);
}

bool window_manager_find_lost_focused_event(struct window_manager *wm, uint32_t window_id)
{
    return table_find(&wm->window_lost_focused_event, &window_id) != NULL;
}

void window_manager_remove_lost_focused_event(struct window_manager *wm, uint32_t window_id)
{
    table_remove(&wm->window_lost_focused_event, &window_id);
}

void window_manager_add_lost_focused_event(struct window_manager *wm, uint32_t window_id)
{
    table_add(&wm->window_lost_focused_event, &window_id, (void *)(intptr_t) 1);
}

struct window *window_manager_find_window(struct window_manager *wm, uint32_t window_id)
{
    return table_find(&wm->window, &window_id);
}

void window_manager_remove_window(struct window_manager *wm, uint32_t window_id)
{
    table_remove(&wm->window, &window_id);
}

bool window_manager_add_window(struct window_manager *wm, struct window *window)
{
    return window && table_try_add(&wm->window, &window->id, window);
}

struct application *window_manager_find_application(struct window_manager *wm, pid_t pid)
{
    return table_find(&wm->application, &pid);
}

void window_manager_remove_application(struct window_manager *wm, pid_t pid)
{
    table_remove(&wm->application, &pid);
}

bool window_manager_add_application(struct window_manager *wm, struct application *application)
{
    return application && table_try_add(&wm->application, &application->pid, application);
}

struct window **window_manager_find_application_windows(struct window_manager *wm, struct application *application, int *window_count)
{
    *window_count = 0;
    struct window **window_list = ts_alloc_list(struct window *, wm->window.count);

    table_for (struct window *window, wm->window, {
        if (window->application == application) {
            window_list[(*window_count)++] = window;
        }
    })

    return window_list;
}

#ifdef TESTS
static CFTypeRef (*window_manager_test_retry_retain)(CFTypeRef);
static void (*window_manager_test_retry_release)(CFTypeRef);
static void *(*window_manager_test_retry_allocate)(size_t) = malloc;
static bool (*window_manager_test_retry_post)(struct window_created_retry *);
static unsigned window_manager_test_retry_destroy_count;
static CFArrayRef (*window_manager_test_application_window_list)(struct application *);
static uint32_t (*window_manager_test_ax_window_id)(AXUIElementRef);
static bool (*window_manager_test_window_observe)(struct window *, bool *);
static void (*window_manager_test_window_unobserve)(struct window *);
static AXError (*window_manager_test_set_attribute_value)(AXUIElementRef, CFStringRef, CFTypeRef);
#endif

uint32_t window_manager_ax_window_id(AXUIElementRef window_ref)
{
#ifdef TESTS
    if (window_manager_test_ax_window_id) return window_manager_test_ax_window_id(window_ref);
#endif
    return ax_window_id(window_ref);
}

static AXError window_manager_set_attribute_value(AXUIElementRef element, CFStringRef attribute, CFTypeRef value)
{
#ifdef TESTS
    if (window_manager_test_set_attribute_value) return window_manager_test_set_attribute_value(element, attribute, value);
#endif
    return AXUIElementSetAttributeValue(element, attribute, value);
}

AXUIElementRef window_manager_copy_application_window_ref(struct application *application, uint32_t window_id)
{
    if (!application || !window_id) return NULL;
#ifdef TESTS
    CFArrayRef window_list = window_manager_test_application_window_list
                           ? window_manager_test_application_window_list(application)
                           : application_window_list(application);
#else
    CFArrayRef window_list = application_window_list(application);
#endif
    if (!window_list) return NULL;

    AXUIElementRef result = NULL;
    CFIndex count = CFArrayGetCount(window_list);
    for (CFIndex i = 0; i < count; ++i) {
        AXUIElementRef candidate = (AXUIElementRef) CFArrayGetValueAtIndex(window_list, i);
        if (window_manager_ax_window_id(candidate) == window_id) {
            result = (AXUIElementRef) CFRetain(candidate);
            break;
        }
    }
    CFRelease(window_list);
    return result;
}

bool window_manager_replace_window_ref(struct window_manager *wm, struct window *window, AXUIElementRef window_ref)
{
    if (!wm || !window || !window_ref) {
        if (window_ref) CFRelease(window_ref);
        return false;
    }
    if (window->ref == window_ref || (window->ref && CFEqual(window->ref, window_ref))) {
        CFRelease(window_ref);
        return false;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application ? window->application->name : "<unknown>", window->id);
#ifdef TESTS
    if (window_manager_test_window_unobserve) window_manager_test_window_unobserve(window); else window_unobserve(window);
#else
    window_unobserve(window);
#endif
    if (window->ref) CFRelease(window->ref);
    window->ref = window_ref;

    bool retry = false;
#ifdef TESTS
    bool observed = window_manager_test_window_observe
                  ? window_manager_test_window_observe(window, &retry)
                  : window_observe(window, &retry);
#else
    bool observed = window_observe(window, &retry);
#endif
    if (!observed) {
        debug("%s: could not observe every notification for %s %d (retry=%d)\n",
              __FUNCTION__, window->application ? window->application->name : "<unknown>", window->id, retry);
    }
    if (window->application && window->application->adopt_attempted_window_id == window->id) {
        window->application->adopt_attempted_window_id = 0;
    }

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) space_manager_flush_tiled_window(view, window);
    return true;
}

bool window_manager_refresh_window_ref(struct window_manager *wm, struct window *window)
{
    if (!wm || !window || !window->application) return false;
    AXUIElementRef fresh_ref = window_manager_copy_application_window_ref(window->application, window->id);
    return fresh_ref ? window_manager_replace_window_ref(wm, window, fresh_ref) : false;
}

struct window_created_retry *window_manager_create_window_created_retry(AXUIElementRef window_ref, struct application *application, bool one_shot_rules, pid_t one_shot_owner_pid)
{
#ifdef TESTS
    struct window_created_retry *retry = window_manager_test_retry_allocate(sizeof(*retry));
#else
    struct window_created_retry *retry = malloc(sizeof(*retry));
#endif
    if (!retry) return NULL;
#ifdef TESTS
    AXUIElementRef retained_ref = window_manager_test_retry_retain ? (AXUIElementRef) window_manager_test_retry_retain(window_ref) : CFRetain(window_ref);
#else
    AXUIElementRef retained_ref = CFRetain(window_ref);
#endif
    *retry = (struct window_created_retry) {
        .window_ref = retained_ref,
        .application_psn = application->psn,
        .application_pid = application->pid,
        .one_shot_rules = one_shot_rules,
        .one_shot_owner_pid = one_shot_owner_pid
    };
    return retry;
}

void window_manager_destroy_window_created_retry(struct window_created_retry *retry)
{
    if (!retry) return;
#ifdef TESTS
    ++window_manager_test_retry_destroy_count;
#endif
    if (retry->window_ref) {
#ifdef TESTS
        if (window_manager_test_retry_release) window_manager_test_retry_release(retry->window_ref); else CFRelease(retry->window_ref);
#else
        CFRelease(retry->window_ref);
#endif
    }
    free(retry);
}

void window_manager_schedule_window_created_retry(AXUIElementRef window_ref, struct application *application, bool one_shot_rules, pid_t one_shot_owner_pid)
{
    struct window_created_retry *retry = window_manager_create_window_created_retry(window_ref, application, one_shot_rules, one_shot_owner_pid);
#ifdef TESTS
    if (window_manager_test_retry_release) window_manager_test_retry_release(window_ref); else CFRelease(window_ref);
#else
    CFRelease(window_ref);
#endif
    if (!retry) return;
#ifdef TESTS
    if (window_manager_test_retry_post) {
        if (!window_manager_test_retry_post(retry)) window_manager_destroy_window_created_retry(retry);
        return;
    }
#endif
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1f * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        /* event_loop_try_post consumes the event context on every outcome. */
        (void) event_loop_try_post(&g_event_loop, WINDOW_CREATED_RETRY, retry, 0);
    });
}

struct application *window_manager_resolve_window_created_retry(struct window_manager *wm, struct window_created_retry *retry)
{
    struct application *application = window_manager_find_application(wm, retry->application_pid);
    if (!application || !psn_equals(&application->psn, &retry->application_psn)) return NULL;
    return application;
}

pid_t window_manager_window_created_retry_owner(struct application *application, struct window_created_retry *retry)
{
    return application->launch_refresh && retry->one_shot_owner_pid == application->pid &&
           psn_equals(&retry->application_psn, &application->psn)
         ? retry->one_shot_owner_pid : 0;
}

bool window_manager_window_created_retry_uses_one_shots(struct application *application, struct window_created_retry *retry)
{
    return retry->one_shot_owner_pid ? window_manager_window_created_retry_owner(application, retry) != 0 : retry->one_shot_rules;
}

struct window *window_manager_create_and_add_window(struct space_manager *sm, struct window_manager *wm, struct application *application, AXUIElementRef window_ref, uint32_t window_id, bool one_shot_rules, pid_t one_shot_owner_pid, bool *retry)
{
    if (retry) *retry = false;
    if (one_shot_rules && !rule_retry_pending_one_shots()) {
        if (retry) {
            *retry = true;
        } else {
#ifdef TESTS
            if (window_manager_test_retry_release) window_manager_test_retry_release(window_ref); else CFRelease(window_ref);
#else
            CFRelease(window_ref);
#endif
        }
        return NULL;
    }
#ifdef TESTS
    bool test_rule_window = window_manager_test_rule_window != NULL;
    struct window *window = test_rule_window ? window_manager_test_rule_window
                                             : window_create(application, window_ref, window_id);
    if (!window) return NULL;
    if (test_rule_window) {
        window->application = application;
        window->ref = window_ref;
        window->id = window_id;
        window->id_ptr = &window->id;
        window->is_root = true;
    }
    char *window_title = test_rule_window ? "Retry" : window_title_ts(window);
    char *window_role = test_rule_window ? "AXWindow" : window_role_ts(window);
    char *window_subrole = test_rule_window ? "AXStandardWindow" : window_subrole_ts(window);
#else
    struct window *window = window_create(application, window_ref, window_id);
    if (!window) return NULL;
    char *window_title = window_title_ts(window);
    char *window_role = window_role_ts(window);
    char *window_subrole = window_subrole_ts(window);
#endif
    debug("%s:%d %s - %s (%s:%s:%d)\n", __FUNCTION__, window->id, window->application->name, window_title, window_role, window_subrole, window->is_root);

#ifdef TESTS
    if (!test_rule_window && window_is_unknown(window)) {
#else
    if (window_is_unknown(window)) {
#endif
        debug("%s: ignoring AXUnknown window %s %d\n", __FUNCTION__, window->application->name, window->id);
        window_manager_remove_lost_focused_event(wm, window->id);
        window_destroy(window);
        return NULL;
    }

    //
    // NOTE(asmvik): Attempt to track **all** windows.
    //

#ifdef TESTS
    bool observed = test_rule_window
                  ? (window_manager_test_window_observe
                     ? window_manager_test_window_observe(window, retry) : true)
                  : window_observe(window, retry);
    if (!observed) {
#else
    if (!window_observe(window, retry)) {
#endif
        debug("%s: could not observe %s %d\n", __FUNCTION__, window->application->name, window->id);
        window_manager_remove_lost_focused_event(wm, window->id);
#ifdef TESTS
        if (window_manager_test_window_unobserve) window_manager_test_window_unobserve(window);
        else window_unobserve(window);
#else
        window_unobserve(window);
#endif
        if (retry && *retry) {
#ifdef TESTS
            if (window_manager_test_retry_retain) window_manager_test_retry_retain(window_ref); else CFRetain(window_ref);
#else
            CFRetain(window_ref);
#endif
        }
        window_destroy(window);
        return NULL;
    }

    if (!window_manager_add_window(wm, window)) {
        window_manager_remove_lost_focused_event(wm, window->id);
#ifdef TESTS
        if (window_manager_test_window_unobserve) window_manager_test_window_unobserve(window);
        else window_unobserve(window);
#else
        window_unobserve(window);
#endif
        window_destroy(window);
        return NULL;
    }

#ifdef TESTS
    if (!test_rule_window && window_manager_find_lost_focused_event(wm, window->id)) {
#else
    if (window_manager_find_lost_focused_event(wm, window->id)) {
#endif
        event_loop_post(&g_event_loop, WINDOW_FOCUSED, (void *)(intptr_t) window->id, 0);
        window_manager_remove_lost_focused_event(wm, window->id);
    }

    //
    // NOTE(asmvik): However, only **root windows** are eligible for management.
    //

    if (window->is_root) {

        //
        // NOTE(asmvik): A lot of windows misreport their accessibility role, so we allow the user
        // to specify rules to make sure that we do in fact manage these windows properly.
        //
        // This part of the rule must be applied at this stage (prior to other rule properties), and if
        // no such rule matches this window, it will be ignored if it does not have a role of kAXWindowRole.
        //

        window_manager_apply_manage_rules_to_window(sm, wm, window, window_title, window_role, window_subrole, one_shot_rules, one_shot_owner_pid);

#ifdef TESTS
        if (test_rule_window) {
            window->is_eligible = true;
            window_manager_apply_rules_to_window(sm, wm, window, window_title, window_role, window_subrole,
                                                 one_shot_rules, one_shot_owner_pid);
            goto out;
        }
#endif
        if (window_manager_is_window_eligible(window)) {
            window->is_eligible = true;
            window_manager_apply_rules_to_window(sm, wm, window, window_title, window_role, window_subrole, one_shot_rules, one_shot_owner_pid);
            window_manager_purify_window(wm, window);
            window_manager_set_window_opacity(wm, window, wm->normal_window_opacity);

            if (application->is_hidden)                              goto out;
            if (window_check_flag(window, WINDOW_MINIMIZE))          goto out;
            if (window_check_flag(window, WINDOW_FULLSCREEN))        goto out;
            if (window_check_rule_flag(window, WINDOW_RULE_MANAGED)) goto out;

            if (window_check_rule_flag(window, WINDOW_RULE_FULLSCREEN)) {
                window_clear_rule_flag(window, WINDOW_RULE_FULLSCREEN);
                goto out;
            }

            if (window_is_sticky(window->id) ||
                !window_can_move(window) ||
                !window_is_standard(window) ||
                !window_level_is_standard(window) ||
                (!window_can_resize(window) && window_is_undersized(window))) {
                window_set_flag(window, WINDOW_FLOAT);
            }
        } else {
            debug("%s ignoring incorrectly marked window %s %d\n", __FUNCTION__, window->application->name, window->id);
            window_set_flag(window, WINDOW_FLOAT);

            //
            // NOTE(asmvik): Print window information when debug_output is enabled.
            // Useful for identifying and creating rules if this window should in fact be managed.
            //

            if (g_verbose) {
                fprintf(stdout, "window info: \n");
                window_serialize(stdout, window, 0);
                fprintf(stdout, "\n");
            }
        }
    } else {
        debug("%s ignoring child window %s %d\n", __FUNCTION__, window->application->name, window->id);
        window_set_flag(window, WINDOW_FLOAT);

        //
        // NOTE(asmvik): Print window information when debug_output is enabled.
        //

        if (g_verbose) {
            fprintf(stdout, "window info: \n");
            window_serialize(stdout, window, 0);
            fprintf(stdout, "\n");
        }
    }

out:
    if (one_shot_owner_pid) rule_cleanup_consumed_one_shots(application);
    return window;
}

static void window_manager_enqueue_application_refresh(struct window_manager *wm,
                                                       struct application *application);

struct window **window_manager_add_application_windows(struct space_manager *sm, struct window_manager *wm, struct application *application, int *count)
{
    *count = 0;
    struct window **list = NULL;
    application->launch_refresh = true;
    if (!rule_retry_pending_one_shots() || !rule_reserve_one_shots(application)) {
        window_manager_enqueue_application_refresh(wm, application);
        return NULL;
    }
#ifdef TESTS
    CFArrayRef window_list = window_manager_test_application_window_list
                           ? window_manager_test_application_window_list(application)
                           : application_window_list(application);
#else
    CFArrayRef window_list = application_window_list(application);
#endif

    if (window_list) {
        int window_count = CFArrayGetCount(window_list);
        for (int i = 0; i < window_count; ++i) {
            AXUIElementRef window_ref = CFArrayGetValueAtIndex(window_list, i);
            uint32_t window_id = window_manager_ax_window_id(window_ref);
            if (!window_id || window_manager_find_window(wm, window_id)) continue;

            bool retry = false;
            struct window *window = window_manager_create_and_add_window(sm, wm, application, CFRetain(window_ref), window_id, true, application->pid, &retry);
            if (window) {
                buf_push(list, window);
            } else if (retry) {
                window_manager_schedule_window_created_retry(window_ref, application, true, application->pid);
            }
        }
        CFRelease(window_list);
    }

    window_manager_add_existing_application_windows(sm, wm, application, -1, &list, count);
    *count = buf_len(list);
    return list;
}

#ifdef TESTS
static bool (*window_manager_test_existing_application_window_list)(struct application *, uint32_t **, int *);
static AXUIElementRef (*window_manager_test_remote_token)(CFDataRef);
static AXError (*window_manager_test_copy_role)(AXUIElementRef, CFTypeRef *);
static bool (*window_manager_test_role_is_window)(CFTypeRef);
static uint32_t (*window_manager_test_ax_window_id)(AXUIElementRef);
static void (*window_manager_test_cf_release)(CFTypeRef);
static struct window *(*window_manager_test_create_window)(struct space_manager *, struct window_manager *, struct application *, AXUIElementRef, uint32_t, bool, pid_t);
#endif

static bool window_manager_existing_application_window_list_with_status(struct application *application, uint32_t **window_list, int *window_count)
{
#ifdef TESTS
    if (window_manager_test_existing_application_window_list) {
        return window_manager_test_existing_application_window_list(application, window_list, window_count);
    }
#endif
    *window_list = NULL;
    *window_count = 0;
    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return false;

    int space_count = 0;
    uint64_t *space_list = NULL;

    for (int i = 0; i < display_count; ++i) {
        int count;
        uint64_t *list = display_space_list(display_list[i], &count);
        if (!list) return false;

        //
        // NOTE(asmvik): display_space_list(..) uses a linear allocator,
        // and so we only need to track the beginning of the first list along
        // with the total number of windows that have been allocated.
        //

        if (!space_list) space_list = list;
        space_count += count;
    }

    if (!space_list) return display_count == 0;
    bool inventory_succeeded = false;
    *window_list = space_window_list_for_connection_with_status(space_list, space_count, application ? application->connection : 0,
                                                                 window_count, true, &inventory_succeeded);
    return inventory_succeeded;
}

static bool window_manager_window_list_is_resolved(struct window_manager *wm, uint32_t *window_list, int window_count)
{
    for (int i = 0; i < window_count; ++i) {
        if (!window_manager_find_window(wm, window_list[i])) return false;
    }
    return true;
}

static void window_manager_enqueue_application_refresh(struct window_manager *wm, struct application *application)
{
    for (int i = 0; i < buf_len(wm->applications_to_refresh); ++i) {
        if (wm->applications_to_refresh[i] == application) return;
    }
    buf_push(wm->applications_to_refresh, application);
}

bool window_manager_remove_application_refresh(struct window_manager *wm, struct application *application)
{
    for (int i = 0; i < buf_len(wm->applications_to_refresh); ++i) {
        if (wm->applications_to_refresh[i] == application) {
            int old_count = buf_len(wm->applications_to_refresh);
            int cursor = old_count ? wm->application_refresh_cursor % old_count : 0;
            if (cursor < 0) cursor += old_count;
            int old_final = old_count - 1;
            buf_del(wm->applications_to_refresh, i);
            int count = buf_len(wm->applications_to_refresh);
            if (!count) {
                wm->application_refresh_cursor = 0;
            } else if (cursor == i || cursor == old_final) {
                wm->application_refresh_cursor = i % count;
            } else {
                wm->application_refresh_cursor = cursor % count;
            }
            return true;
        }
    }
    return false;
}

static int window_manager_inactive_probe_budget;

void window_manager_begin_inactive_probe_pass(int budget)
{
    window_manager_inactive_probe_budget = max(0, budget);
}

int window_manager_inactive_probe_budget_remaining(void)
{
    return window_manager_inactive_probe_budget;
}

bool window_manager_add_existing_application_windows(struct space_manager *sm, struct window_manager *wm, struct application *application, int refresh_index, struct window ***created_windows, int *created_count)
{
    if (refresh_index == -1) window_manager_begin_inactive_probe_pass(4096);
    bool result = false;
    if (created_count) *created_count = 0;
    if (application->launch_refresh && !rule_retry_pending_one_shots()) {
        window_manager_enqueue_application_refresh(wm, application);
        return false;
    }
    bool one_shot_rules = application->launch_refresh;
    pid_t one_shot_owner_pid = one_shot_rules ? application->pid : 0;

    int global_window_count;
    uint32_t *global_window_list;
    if (!window_manager_existing_application_window_list_with_status(application, &global_window_list, &global_window_count)) {
        window_manager_enqueue_application_refresh(wm, application);
        return false;
    }

#ifdef TESTS
    CFArrayRef window_list_ref = window_manager_test_application_window_list ? window_manager_test_application_window_list(application) : application_window_list(application);
#else
    CFArrayRef window_list_ref = application_window_list(application);
#endif
    int window_count = window_list_ref ? CFArrayGetCount(window_list_ref) : 0;

    for (int i = 0; i < window_count; ++i) {
        AXUIElementRef window_ref = CFArrayGetValueAtIndex(window_list_ref, i);
        uint32_t window_id = window_manager_ax_window_id(window_ref);

        //
        // @cleanup
        //
        // :Workaround
        //
        // NOTE(asmvik): The AX API appears to always include a single element for Finder that returns an empty window id.
        // This is likely the desktop window. Other similar cases should be ignored.
        //

        if (!window_id) continue;

        if (!window_manager_find_window(wm, window_id)) {
            struct window *window = window_manager_create_and_add_window(sm, wm, application, CFRetain(window_ref), window_id, one_shot_rules, one_shot_owner_pid, NULL);
            if (window && created_windows) buf_push(*created_windows, window);
        }
    }

    if (!window_manager_window_list_is_resolved(wm, global_window_list, global_window_count)) {
            bool missing_window = false;
            uint32_t *app_window_list = NULL;

            int probe_role_count = 0;
            int probe_window_count = 0;
            uint64_t probe_last_window_element_id = 0;

            for (int i = 0; i < global_window_count; ++i) {
                struct window *window = window_manager_find_window(wm, global_window_list[i]);
                if (!window) {
                    missing_window = true;
                    ts_buf_push(app_window_list, global_window_list[i]);
                }
            }

            if (missing_window) {
                debug("%s: %s has %d windows that are not yet resolved, attempting workaround\n", __FUNCTION__, application->name, ts_buf_len(app_window_list));

                //
                // NOTE(asmvik): MacOS API does not return AXUIElementRef of windows on inactive spaces.
                // However, we can just brute-force the element_id and create the AXUIElementRef ourselves.
                //
                // :Attribution
                // https://github.com/decodism
                // https://github.com/lwouis/alt-tab-macos/issues/1324#issuecomment-2631035482
                //

                CFMutableDataRef data_ref = CFDataCreateMutable(NULL, 0x14);
                if (!data_ref) goto probe_complete;
                CFDataIncreaseLength(data_ref, 0x14);

                uint8_t *data = CFDataGetMutableBytePtr(data_ref);
                *(uint32_t *) (data + 0x0) = application->pid;
                *(uint32_t *) (data + 0x8) = 0x636f636f;

                enum { INACTIVE_WINDOW_PROBE_LIMIT = 0x7fff };
                while (window_manager_inactive_probe_budget > 0) {
                    --window_manager_inactive_probe_budget;
                    int app_window_list_len = ts_buf_len(app_window_list);
                    if (app_window_list_len == 0) break;

                    uint64_t element_id = application->inactive_probe_cursor;
                    application->inactive_probe_cursor = (application->inactive_probe_cursor + 1) % INACTIVE_WINDOW_PROBE_LIMIT;
                    memcpy(data+0xc, &element_id, sizeof(uint64_t));
#ifdef TESTS
                    AXUIElementRef element_ref = window_manager_test_remote_token ? window_manager_test_remote_token(data_ref) : _AXUIElementCreateWithRemoteToken(data_ref);
#else
                    AXUIElementRef element_ref = _AXUIElementCreateWithRemoteToken(data_ref);
#endif
                    if (!element_ref) continue;
                    bool element_consumed = false;

                    CFTypeRef role = NULL;
#ifdef TESTS
                    if (window_manager_test_copy_role) window_manager_test_copy_role(element_ref, &role);
                    else AXUIElementCopyAttributeValue(element_ref, kAXRoleAttribute, &role);
#else
                    AXUIElementCopyAttributeValue(element_ref, kAXRoleAttribute, &role);
#endif

                    if (role) {
                        ++probe_role_count;
#ifdef TESTS
                        bool role_is_window = window_manager_test_role_is_window ? window_manager_test_role_is_window(role) : CFEqual(role, kAXWindowRole);
#else
                        bool role_is_window = CFEqual(role, kAXWindowRole);
#endif
                        if (role_is_window) {
#ifdef TESTS
                            uint32_t element_wid = window_manager_test_ax_window_id ? window_manager_test_ax_window_id(element_ref) : ax_window_id(element_ref);
#else
                            uint32_t element_wid = ax_window_id(element_ref);
#endif
                            bool matched = false;

                            ++probe_window_count;
                            probe_last_window_element_id = element_id;
                            debug("%s: probe %s element_id=%llu wid=%d\n", __FUNCTION__, application->name, element_id, element_wid);

                            int match_index = -1;
                            if (element_wid != 0) {
                                for (int i = 0; i < app_window_list_len; ++i) {
                                    if (app_window_list[i] == element_wid) {
                                        matched = true;
                                        match_index = i;
                                        break;
                                    }
                                }
                            }

                            if (matched) {
                                element_consumed = true;
#ifdef TESTS
                                struct window *window = window_manager_test_create_window ?
                                    window_manager_test_create_window(sm, wm, application, element_ref, element_wid, one_shot_rules, one_shot_owner_pid) :
                                    window_manager_create_and_add_window(sm, wm, application, element_ref, element_wid, one_shot_rules, one_shot_owner_pid, NULL);
#else
                                struct window *window = window_manager_create_and_add_window(sm, wm, application, element_ref, element_wid, one_shot_rules, one_shot_owner_pid, NULL);
#endif
                                if (window) {
                                    if (created_windows) buf_push(*created_windows, window);
                                    ts_buf_del(app_window_list, match_index);
                                }
                            }
                        }

#ifdef TESTS
                        if (window_manager_test_cf_release) window_manager_test_cf_release(role); else CFRelease(role);
#else
                        CFRelease(role);
#endif
                    }
                    if (!element_consumed) {
#ifdef TESTS
                        if (window_manager_test_cf_release) window_manager_test_cf_release(element_ref); else CFRelease(element_ref);
#else
                        CFRelease(element_ref);
#endif
                    }
                }

                CFRelease(data_ref);
            }

probe_complete:
            if (ts_buf_len(app_window_list) > 0) {
                debug("%s: workaround failed to resolve all windows for %s (probed roles=%d windows=%d last_window_element_id=%llu unresolved=%d)\n",
                      __FUNCTION__, application->name, probe_role_count, probe_window_count, probe_last_window_element_id, ts_buf_len(app_window_list));
                for (int i = 0; i < ts_buf_len(app_window_list); ++i) {
                    debug("%s:   unresolved wid=%d for %s\n", __FUNCTION__, app_window_list[i], application->name);
                }
                window_manager_enqueue_application_refresh(wm, application);
            } else {
                debug("%s: workaround resolved all windows for %s\n", __FUNCTION__, application->name);
            }
    }

    bool fully_resolved = window_manager_window_list_is_resolved(wm, global_window_list, global_window_count);
    bool finalization_ok = !fully_resolved || !application->launch_refresh ||
                           rule_finalize_one_shots(application);
    if (fully_resolved && finalization_ok && refresh_index != -1 &&
        refresh_index < buf_len(wm->applications_to_refresh) &&
        wm->applications_to_refresh[refresh_index] == application) {
        debug("%s: all windows for %s are now resolved\n", __FUNCTION__, application->name);
        result = window_manager_remove_application_refresh(wm, application);
    }

    if (window_list_ref) CFRelease(window_list_ref);
    if (created_count && created_windows) *created_count = buf_len(*created_windows);
    if (fully_resolved && finalization_ok) {
        application->inactive_probe_cursor = 0;
        application->launch_refresh = false;
    } else if (fully_resolved) {
        window_manager_enqueue_application_refresh(wm, application);
    }

    return result || (refresh_index == -1 && fully_resolved && finalization_ok);
}

enum window_op_error window_manager_set_window_insertion(struct space_manager *sm, struct window *window, int direction)
{
    TIME_FUNCTION;

    uint64_t sid = window_space(window->id);
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    if (view->insertion_point && view->insertion_point != window->id) {
        struct window_node *insert_node = view_find_window_node(view, view->insertion_point);
        if (insert_node) {
            insert_feedback_destroy(insert_node);
            insert_node->split = SPLIT_NONE;
            insert_node->child = CHILD_NONE;
            insert_node->insert_dir = 0;
        }
    }

    if (direction == node->insert_dir) {
        insert_feedback_destroy(node);
        node->split = SPLIT_NONE;
        node->child = CHILD_NONE;
        node->insert_dir = 0;
        view->insertion_point = 0;
        return WINDOW_OP_ERROR_SUCCESS;
    }

    if (direction == DIR_NORTH) {
        node->split = SPLIT_X;
        node->child = CHILD_FIRST;
    } else if (direction == DIR_EAST) {
        node->split = SPLIT_Y;
        node->child = CHILD_SECOND;
    } else if (direction == DIR_SOUTH) {
        node->split = SPLIT_X;
        node->child = CHILD_SECOND;
    } else if (direction == DIR_WEST) {
        node->split = SPLIT_Y;
        node->child = CHILD_FIRST;
    }

    node->insert_dir = direction;
    view->insertion_point = node->window_order[0];
    insert_feedback_show(node);

    return WINDOW_OP_ERROR_SUCCESS;
}

#ifdef TESTS
static bool (*window_manager_test_order_window)(uint32_t, int, uint32_t);
#endif

enum window_op_error window_manager_stack_window(struct space_manager *sm, struct window_manager *wm, struct window *a, struct window *b)
{
    TIME_FUNCTION;

    if (a->id == b->id) return WINDOW_OP_ERROR_SAME_WINDOW;

    struct view *a_view = window_manager_find_managed_window(wm, a);
    if (!a_view) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    struct window_node *a_node = view_find_window_node(a_view, a->id);
    if (!a_node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;
    if (a_node->window_count >= NODE_MAX_WINDOW_COUNT) return WINDOW_OP_ERROR_MAX_STACK;

    struct view *b_view = window_manager_find_managed_window(wm, b);
    if (!b_view && window_check_flag(b, WINDOW_FLOAT) && !window_manager_is_window_eligible(b)) {
        return WINDOW_OP_ERROR_INVALID_SRC_NODE;
    }
    bool reserved_mapping = !b_view;
    if (reserved_mapping && !table_try_add(&wm->managed_window, &b->id, a_view)) return WINDOW_OP_ERROR_INVALID_OPERATION;
    if (b_view) {
        space_manager_untile_window(b_view, b);
        window_manager_purify_window(wm, b);
    } else if (window_check_flag(b, WINDOW_FLOAT)) {
        window_clear_flag(b, WINDOW_FLOAT);
        if (window_check_flag(b, WINDOW_STICKY)) window_manager_make_window_sticky(sm, wm, b, false);
    }

    a_node = view_find_window_node(a_view, a->id);
    if (!a_node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;
    if (a_node->window_count >= NODE_MAX_WINDOW_COUNT) return WINDOW_OP_ERROR_MAX_STACK;
    if (!view_stack_window_node(a_node, b)) return WINDOW_OP_ERROR_MAX_STACK;

    if (!window_manager_add_managed_window(wm, b, a_view)) {
        if (reserved_mapping) table_remove(&wm->managed_window, &b->id);
        return WINDOW_OP_ERROR_INVALID_OPERATION;
    }
    if (reserved_mapping && wm == &g_window_manager && declarative_installed_has_scaffold()) {
        declarative_configuration_request_reconcile();
    }
    window_manager_adjust_layer(b, LAYER_BELOW);
#ifdef TESTS
    if (window_manager_test_order_window) {
        window_manager_test_order_window(b->id, 1, a_node->window_order[1]);
    } else
#endif
    scripting_addition_order_window(b->id, 1, a_node->window_order[1]);

    if (view_layout_is_accordion(a_view->layout)) {
        view_update(a_view);
        view_flush(a_view);
        return WINDOW_OP_ERROR_SUCCESS;
    }
    struct area area = a_node->zoom ? a_node->zoom->area : a_node->area;
    window_manager_animate_window((struct window_capture) { b, area.x, area.y, area.w, area.h });
    return WINDOW_OP_ERROR_SUCCESS;
}

enum window_op_error window_manager_warp_window(struct space_manager *sm, struct window_manager *wm, struct window *a, struct window *b)
{
    TIME_FUNCTION;

    if (a->id == b->id) return WINDOW_OP_ERROR_SAME_WINDOW;

    uint64_t a_sid = window_space(a->id);
    struct view *a_view = space_manager_find_view(sm, a_sid);
    if (!a_view || a_view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    uint64_t b_sid = window_space(b->id);
    struct view *b_view = space_manager_find_view(sm, b_sid);
    if (!b_view || b_view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_DST_VIEW;

    struct window_node *a_node = view_find_window_node(a_view, a->id);
    if (!a_node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;
    if (window_manager_find_managed_window(wm, a) != a_view) return WINDOW_OP_ERROR_INVALID_OPERATION;

    struct window_node *b_node = view_find_window_node(b_view, b->id);
    if (!b_node) return WINDOW_OP_ERROR_INVALID_DST_NODE;

    if (a_node == b_node) return WINDOW_OP_ERROR_SAME_STACK;
    if (b_node->insert_dir == STACK && b_node->window_count >= NODE_MAX_WINDOW_COUNT) return WINDOW_OP_ERROR_MAX_STACK;

    if (a_node->parent && b_node->parent &&
        a_node->parent == b_node->parent &&
        a_node->window_count == 1) {
        if (window_node_contains_window(b_node, b_view->insertion_point)) {
            struct view_insertion_plan plan;
            if (!view_prepare_window_node_insertion(b_view, a, b->id, &plan)) return WINDOW_OP_ERROR_INVALID_OPERATION;
            b_node->parent->split = b_node->split;
            b_node->parent->child = b_node->child;

            view_remove_window_node(a_view, a);
            struct window_node *a_node_add = view_commit_window_node_insertion(b_view, a, b->id, &plan);
            view_abort_window_node_insertion(&plan);
            if (!a_node_add || !window_manager_add_managed_window(wm, a, b_view)) return WINDOW_OP_ERROR_INVALID_OPERATION;

            struct window_capture *window_list = NULL;
            window_node_capture_windows(a_node_add, &window_list);
            window_manager_animate_window_list(window_list, ts_buf_len(window_list));
        } else {
            if (window_node_contains_window(a_node, a_view->insertion_point)) {
                a_view->insertion_point = b->id;
            }

            window_node_swap_window_list(a_node, b_node);

            struct window_capture *window_list = NULL;
            window_node_capture_windows(a_node, &window_list);
            window_node_capture_windows(b_node, &window_list);
            window_manager_animate_window_list(window_list, ts_buf_len(window_list));
        }
    } else {
        struct view_insertion_plan plan;
        if (!view_prepare_window_node_insertion(b_view, a, b->id, &plan)) return WINDOW_OP_ERROR_INVALID_OPERATION;
        if (a_view->sid == b_view->sid) {

            //
            // :NaturalWarp
            //
            // NOTE(asmvik): Precalculate both target areas and select the one that has the closest distance to the source area.
            // This allows the warp to feel more natural in terms of where the window is placed on screen, however, this is only utilized
            // for warp operations where both operands belong to the same space. There may be a better system to handle this if/when multiple
            // monitors should be supported.
            //

            struct area cf, cs;
            area_make_pair(window_node_get_split(b_view, b_node), window_node_get_gap(b_view), window_node_get_ratio(b_view, b_node), &b_node->area, &cf, &cs);

            CGPoint ca = { (int)(0.5f + a_node->area.x + a_node->area.w / 2.0f), (int)(0.5f + a_node->area.y + a_node->area.h / 2.0f) };
            float dcf = powf((ca.x - (int)(0.5f + cf.x + cf.w / 2.0f)), 2.0f) + powf((ca.y - (int)(0.5f + cf.y + cf.h / 2.0f)), 2.0f);
            float dcs = powf((ca.x - (int)(0.5f + cs.x + cs.w / 2.0f)), 2.0f) + powf((ca.y - (int)(0.5f + cs.y + cs.h / 2.0f)), 2.0f);

            if (dcf < dcs) {
                b_node->child = CHILD_FIRST;
            } else if (dcf > dcs) {
                b_node->child = CHILD_SECOND;
            } else {
                b_node->child = window_node_is_left_child(a_node) ? CHILD_FIRST : CHILD_SECOND;
            }

            struct window_node *a_node_rm = view_remove_window_node(a_view, a);
            struct window_node *a_node_add = view_commit_window_node_insertion(b_view, a, b->id, &plan);
            view_abort_window_node_insertion(&plan);
            if (!a_node_add) return WINDOW_OP_ERROR_INVALID_OPERATION;

            struct window_capture *window_list = NULL;
            if (a_node_rm) {
                window_node_capture_windows(a_node_rm, &window_list);
            }

            if (a_node_rm != a_node_add && a_node_rm != a_node_add->parent) {
                window_node_capture_windows(a_node_add, &window_list);
            }

            window_manager_animate_window_list(window_list, ts_buf_len(window_list));
        } else {
            if (!space_manager_move_window_to_space(b_view->sid, a)) {
                view_abort_window_node_insertion(&plan);
                return WINDOW_OP_ERROR_SPACE_MOVE_FAILED;
            }
            if (wm->focused_window_id == a->id) {
                struct window *next = window_manager_find_window_on_space_by_rank_filtering_window(wm, a_view->sid, 1, a->id);
                if (next) {
                    window_manager_focus_window_with_raise(&next->application->psn, next->id, next->ref);
                } else {
                    _SLPSSetFrontProcessWithOptions(&g_process_manager.finder_psn, 0, kCPSNoWindows);
                }
            }

            //
            // :NaturalWarp
            //
            // TODO(asmvik): Warp operations with operands that belong to different monitors does not yet implement a heuristic to select
            // the target area that feels the most natural in terms of where the window is placed on screen. Is it possible to do better when
            // warping between spaces that belong to the same monitor as well??
            //

            struct window_node *changed = view_remove_window_node(a_view, a);
            struct window_node *added = view_commit_window_node_insertion(b_view, a, b->id, &plan);
            view_abort_window_node_insertion(&plan);
            if (!added || !window_manager_add_managed_window(wm, a, b_view)) {
                (void) space_manager_move_window_to_space(a_view->sid, a);
                return WINDOW_OP_ERROR_INVALID_OPERATION;
            }
            window_manager_adjust_layer(a, LAYER_BELOW);
            if (changed) {
                if (view_space_is_visible(a_view->sid)) window_node_flush(changed);
                else view_set_flag(a_view, VIEW_IS_DIRTY);
            }
            if (view_space_is_visible(b_view->sid)) window_node_flush(added);
            else view_set_flag(b_view, VIEW_IS_DIRTY);
        }
    }

    return WINDOW_OP_ERROR_SUCCESS;
}

static void window_manager_remap_node_windows(struct window_manager *wm, struct window_node *node, struct view *view)
{
    for (int i = 0; i < node->window_count; ++i) {
        struct window *window = window_manager_find_window(wm, node->window_list[i]);
        (void) window_manager_add_managed_window(wm, window, view);
    }
}

bool window_manager_move_node_windows_between_spaces(struct window_manager *wm,
                                                      struct window_node *a_node, uint64_t a_sid,
                                                      struct window_node *b_node, uint64_t b_sid)
{
    if (!wm || !a_node || !b_node || !a_sid || !b_sid) return false;
    if (a_sid == b_sid) return true;

    struct window *a_windows[NODE_MAX_WINDOW_COUNT] = {0};
    struct window *b_windows[NODE_MAX_WINDOW_COUNT] = {0};
    for (int i = 0; i < a_node->window_count; ++i) {
        a_windows[i] = window_manager_find_window(wm, a_node->window_list[i]);
        if (!a_windows[i]) return false;
    }
    for (int i = 0; i < b_node->window_count; ++i) {
        b_windows[i] = window_manager_find_window(wm, b_node->window_list[i]);
        if (!b_windows[i]) return false;
    }

    int moved_a = 0;
    int moved_b = 0;
    for (; moved_a < a_node->window_count; ++moved_a) {
        if (!space_manager_move_window_to_space(b_sid, a_windows[moved_a])) goto rollback;
    }
    for (; moved_b < b_node->window_count; ++moved_b) {
        if (!space_manager_move_window_to_space(a_sid, b_windows[moved_b])) goto rollback;
    }
    return true;

rollback:;
    bool rollback_ok = true;
    while (moved_b > 0) rollback_ok &= space_manager_move_window_to_space(b_sid, b_windows[--moved_b]);
    while (moved_a > 0) rollback_ok &= space_manager_move_window_to_space(a_sid, a_windows[--moved_a]);
    if (!rollback_ok) {
        warn("%s: a Space-move rollback was incomplete; scheduling topology reconciliation\n", __FUNCTION__);
        declarative_configuration_request_reconcile();
    }
    return false;
}

static void window_manager_update_swap_insertion_points(struct view *a_view, struct window_node *a_node, uint32_t a_window_id, struct view *b_view, struct window_node *b_node, uint32_t b_window_id)
{
    if (a_view == b_view) {
        if (window_node_contains_window(a_node, a_view->insertion_point)) {
            a_view->insertion_point = b_window_id;
        } else if (window_node_contains_window(b_node, b_view->insertion_point)) {
            b_view->insertion_point = a_window_id;
        }
    } else {
        if (window_node_contains_window(a_node, a_view->insertion_point)) a_view->insertion_point = b_window_id;
        if (window_node_contains_window(b_node, b_view->insertion_point)) b_view->insertion_point = a_window_id;
    }
}

static void window_manager_swap_node_window_lists_and_remap(struct window_manager *wm, struct window_node *a_node, struct view *a_view, struct window_node *b_node, struct view *b_view)
{
    window_node_swap_window_list(a_node, b_node);

    if (a_view->sid != b_view->sid) {
        window_manager_remap_node_windows(wm, a_node, a_view);
        window_manager_remap_node_windows(wm, b_node, b_view);
    }
}

static bool window_manager_defer_accordion_swap_flush(struct view *view, uint32_t focused_window_id,
                                                        uint32_t a_window_id, uint32_t b_window_id)
{
    return view_layout_is_accordion(view->layout) &&
           (focused_window_id == a_window_id || focused_window_id == b_window_id);
}

enum window_op_error window_manager_swap_window(struct space_manager *sm, struct window_manager *wm, struct window *a, struct window *b)
{
    TIME_FUNCTION;

    if (a->id == b->id) return WINDOW_OP_ERROR_SAME_WINDOW;

    uint64_t a_sid = window_space(a->id);
    struct view *a_view = space_manager_find_view(sm, a_sid);

    uint64_t b_sid = window_space(b->id);
    struct view *b_view = space_manager_find_view(sm, b_sid);
    if (!a_view || !b_view) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    struct window_node *a_node = view_find_window_node(a_view, a->id);
    if (!a_node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    struct window_node *b_node = view_find_window_node(b_view, b->id);
    if (!b_node) return WINDOW_OP_ERROR_INVALID_DST_NODE;

    if (a_node == b_node) {
        int a_list_index = 0;
        int a_order_index = 0;

        int b_list_index = 0;
        int b_order_index = 0;

        for (int i = 0; i < a_node->window_count; ++i) {
            if (a_node->window_list[i] == a->id) {
                a_list_index = i;
            } else if (a_node->window_list[i] == b->id) {
                b_list_index = i;
            }

            if (a_node->window_order[i] == a->id) {
                a_order_index = i;
            } else if (a_node->window_order[i] == b->id) {
                b_order_index = i;
            }
        }

        a_node->window_list[a_list_index] = b->id;
        a_node->window_order[a_order_index] = b->id;

        a_node->window_list[b_list_index] = a->id;
        a_node->window_order[b_order_index] = a->id;

        bool defer_accordion_flush = window_manager_defer_accordion_swap_flush(a_view,
            wm->focused_window_id, a->id, b->id);
        if (a->id == wm->focused_window_id) {
            window_manager_focus_window_with_raise(&b->application->psn, b->id, b->ref);
        } else if (b->id == wm->focused_window_id) {
            window_manager_focus_window_with_raise(&a->application->psn, a->id, a->ref);
        }

        if (view_layout_is_accordion(a_view->layout) && !defer_accordion_flush) {
            view_update(a_view);
            view_flush(a_view);
        }
        return WINDOW_OP_ERROR_SUCCESS;
    }

    if (a_view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;
    if (b_view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_DST_VIEW;

    if (a_view->sid != b_view->sid &&
        !window_manager_move_node_windows_between_spaces(wm, a_node, a_view->sid, b_node, b_view->sid)) {
        return WINDOW_OP_ERROR_SPACE_MOVE_FAILED;
    }

    window_manager_update_swap_insertion_points(a_view, a_node, a->id, b_view, b_node, b->id);

    bool a_visible = space_is_visible(a_view->sid);
    bool b_visible = space_is_visible(b_view->sid);

    if (a_view->sid != b_view->sid) {
        if (a_visible && !b_visible && a->id == wm->focused_window_id) {
            window_manager_focus_window_with_raise(&b->application->psn, b->id, b->ref);
        } else if (b_visible && !a_visible && b->id == wm->focused_window_id) {
            window_manager_focus_window_with_raise(&a->application->psn, a->id, a->ref);
        }
    }

    window_manager_swap_node_window_lists_and_remap(wm, a_node, a_view, b_node, b_view);

    struct window_capture *window_list = NULL;

    if (a_visible) {
        window_node_capture_windows(a_node, &window_list);
    } else {
        view_set_flag(a_view, VIEW_IS_DIRTY);
    }

    if (b_visible) {
        window_node_capture_windows(b_node, &window_list);
    } else {
        view_set_flag(b_view, VIEW_IS_DIRTY);
    }

    window_manager_animate_window_list(window_list, ts_buf_len(window_list));
    return WINDOW_OP_ERROR_SUCCESS;
}

enum window_op_error window_manager_minimize_window(struct window *window)
{
    TIME_FUNCTION;

    if (!window_can_minimize(window)) return WINDOW_OP_ERROR_CANT_MINIMIZE;
    if (window_check_flag(window, WINDOW_MINIMIZE)) return WINDOW_OP_ERROR_ALREADY_MINIMIZED;

    AXError result = AXUIElementSetAttributeValue(window->ref, kAXMinimizedAttribute, kCFBooleanTrue);
    return result == kAXErrorSuccess ? WINDOW_OP_ERROR_SUCCESS : WINDOW_OP_ERROR_MINIMIZE_FAILED;
}

enum window_op_error window_manager_deminimize_window(struct window *window)
{
    TIME_FUNCTION;

    if (!window_check_flag(window, WINDOW_MINIMIZE)) return WINDOW_OP_ERROR_NOT_MINIMIZED;

    AXError result = AXUIElementSetAttributeValue(window->ref, kAXMinimizedAttribute, kCFBooleanFalse);
    return result == kAXErrorSuccess ? WINDOW_OP_ERROR_SUCCESS : WINDOW_OP_ERROR_DEMINIMIZE_FAILED;
}

bool window_manager_close_window(struct window *window)
{
    TIME_FUNCTION;

    CFTypeRef button = NULL;
    AXUIElementCopyAttributeValue(window->ref, kAXCloseButtonAttribute, &button);
    if (!button) return false;

    AXUIElementPerformAction(button, kAXPressAction);
    CFRelease(button);

    return true;
}

bool window_manager_send_window_to_space(struct space_manager *sm, struct window_manager *wm, struct window *window, uint64_t dst_sid, bool moved_by_rule)
{
    TIME_FUNCTION;

    uint64_t src_sid = window_space(window->id);
    if (src_sid == dst_sid) return true;
    struct view *src_view = window_manager_find_managed_window(wm, window);
    struct view *dst_view = space_manager_find_view(sm, dst_sid);
    if (src_view && !dst_view) return false;
    if (!space_manager_move_window_to_space(dst_sid, window)) return false;

    if (src_view) {
        if (dst_view->layout == VIEW_FLOAT) {
            space_manager_untile_window(src_view, window);
            window_manager_remove_managed_window(wm, window->id);
            window_manager_purify_window(wm, window);
        } else {
            if (!window_manager_transfer_managed_window(wm, window, src_view, dst_view, 0)) {
                bool rolled_back = space_manager_move_window_to_space(src_sid, window);
                if (!rolled_back) declarative_configuration_request_reconcile();
                return false;
            }
        }
    } else if (window_manager_should_manage_window(window)) {
        if (window_manager_tile_window_on_space(sm, wm, window, dst_sid, 0, true) == MANAGED_WINDOW_FAILED) {
            bool rolled_back = space_manager_move_window_to_space(src_sid, window);
            if (!rolled_back) declarative_configuration_request_reconcile();
            return false;
        }
    }

    if (space_is_visible(src_sid) && (moved_by_rule || wm->focused_window_id == window->id)) {
        struct window *next = window_manager_find_window_on_space_by_rank_filtering_window(wm, src_sid, 1, window->id);
        if (next) {
            window_manager_focus_window_with_raise(&next->application->psn, next->id, next->ref);
        } else {
            _SLPSSetFrontProcessWithOptions(&g_process_manager.finder_psn, 0, kCPSNoWindows);
        }
    }
    SLSSpaceSetFrontPSN(g_connection, dst_sid, window->application->psn);
    return true;
}

enum window_op_error window_manager_apply_grid(struct space_manager *sm, struct window_manager *wm, struct window *window, unsigned r, unsigned c, unsigned x, unsigned y, unsigned w, unsigned h)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    uint32_t did = window_display_id(window->id);
    if (!did) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    if (x >=   c) x = c - 1;
    if (y >=   r) y = r - 1;
    if (w <=   0) w = 1;
    if (h <=   0) h = 1;
    if (w >  c-x) w = c - x;
    if (h >  r-y) h = r - y;

    CGRect bounds = display_bounds_constrained(did, false);
    struct view *dview = space_manager_find_view(sm, display_space_id(did));

    if (dview) {
        if (view_check_flag(dview, VIEW_ENABLE_PADDING)) {
            bounds.origin.x    += dview->left_padding;
            bounds.size.width  -= view_padding_sum(dview->left_padding, dview->right_padding);
            bounds.origin.y    += dview->top_padding;
            bounds.size.height -= view_padding_sum(dview->top_padding, dview->bottom_padding);
        }

        if (view_check_flag(dview, VIEW_ENABLE_GAP)) {
            int gap = window_node_get_gap(dview);

            if (x > 0) {
                bounds.origin.x   += gap;
                bounds.size.width -= gap;
            }

            if (y > 0) {
                bounds.origin.y    += gap;
                bounds.size.height -= gap;
            }

            if (c > x+w) bounds.size.width  -= gap;
            if (r > y+h) bounds.size.height -= gap;
        }
    }

    bounds.size.width = fmax(0.0, bounds.size.width);
    bounds.size.height = fmax(0.0, bounds.size.height);
    float cw = bounds.size.width / c;
    float ch = bounds.size.height / r;
    float fx = bounds.origin.x + bounds.size.width  - cw * (c - x);
    float fy = bounds.origin.y + bounds.size.height - ch * (r - y);
    float fw = cw * w;
    float fh = ch * h;

    window_manager_animate_window((struct window_capture) { .window = window, .x = fx, .y = fy, .w = fw, .h = fh });
    return WINDOW_OP_ERROR_SUCCESS;
}

void window_manager_make_window_floating(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_float, bool force)
{
    TIME_FUNCTION;

    if (!window_manager_is_window_eligible(window)) return;

    if (!force) {
        if (!window_is_standard(window) || !window_level_is_standard(window) || !window_can_move(window)) {
            if (!window_check_rule_flag(window, WINDOW_RULE_MANAGED)) {
                return;
            }
        }
    }

    if (should_float) {
        struct view *view = window_manager_find_managed_window(wm, window);
        if (view) {
            space_manager_untile_window(view, window);
            window_manager_remove_managed_window(wm, window->id);
            window_manager_purify_window(wm, window);
        }
        window_set_flag(window, WINDOW_FLOAT);
    } else {
        window_clear_flag(window, WINDOW_FLOAT);

        if (!window_check_flag(window, WINDOW_STICKY)) {
            if ((window_manager_should_manage_window(window)) && (!window_manager_find_managed_window(wm, window))) {
                if (window_manager_tile_window_on_space(sm, wm, window, space_manager_active_space(), 0, true) == MANAGED_WINDOW_FAILED) return;
            }
        }
    }
}

void window_manager_make_window_sticky(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_sticky)
{
    TIME_FUNCTION;

    if (!window_manager_is_window_eligible(window)) return;

    if (should_sticky) {
        if (scripting_addition_set_sticky(window->id, true)) {
            struct view *view = window_manager_find_managed_window(wm, window);
            if (view) {
                space_manager_untile_window(view, window);
                window_manager_remove_managed_window(wm, window->id);
                window_manager_purify_window(wm, window);
            }
            window_set_flag(window, WINDOW_STICKY);
        }
    } else {
        if (scripting_addition_set_sticky(window->id, false)) {
            window_clear_flag(window, WINDOW_STICKY);

            if (!window_check_flag(window, WINDOW_FLOAT)) {
                if ((window_manager_should_manage_window(window)) && (!window_manager_find_managed_window(wm, window))) {
                    if (window_manager_tile_window_on_space(sm, wm, window, space_manager_active_space(), 0, true) == MANAGED_WINDOW_FAILED) return;
                }
            }
        }
    }
}

void window_manager_toggle_window_shadow(struct window *window)
{
    TIME_FUNCTION;

    bool shadow = !window_check_flag(window, WINDOW_SHADOW);
    if (scripting_addition_set_shadow(window->id, shadow)) {
        if (shadow) {
            window_set_flag(window, WINDOW_SHADOW);
        } else {
            window_clear_flag(window, WINDOW_SHADOW);
        }
    }
}

#define NATIVE_FULLSCREEN_WAIT_ATTEMPTS 100
#define NATIVE_FULLSCREEN_WAIT_INTERVAL_US 100000

bool window_manager_wait_for_native_fullscreen_transition(struct window *window, bool fullscreen)
{
    TIME_FUNCTION;

    if (workspace_is_macos_monterey_or_newer()) {
        for (int attempt = 0; attempt < NATIVE_FULLSCREEN_WAIT_ATTEMPTS; ++attempt) {
            uint64_t sid = window_space(window->id);
            if (sid && space_is_user(sid) != fullscreen) return true;
            usleep(NATIVE_FULLSCREEN_WAIT_INTERVAL_US);
        }
        return false;
    }

    uint32_t did = window_display_id(window->id);
    if (!did) return false;
    for (int attempt = 0; attempt < NATIVE_FULLSCREEN_WAIT_ATTEMPTS; ++attempt) {
        uint64_t sid = window_space(window->id);
        bool reached_state = sid && space_is_user(sid) != fullscreen;
        if (reached_state && !display_manager_display_is_animating(did)) return true;
        usleep(NATIVE_FULLSCREEN_WAIT_INTERVAL_US);
    }
    return false;
}

bool window_manager_toggle_window_native_fullscreen(struct window *window)
{
    TIME_FUNCTION;

    uint64_t sid = window_space(window->id);
    if (!sid) return false;

    //
    // NOTE(asmvik): The window must become the focused window
    // before we can change its fullscreen attribute. We focus the
    // window and spin lock until a potential space animation has finished.
    //

    window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
    bool focused = false;
    for (int attempt = 0; attempt < NATIVE_FULLSCREEN_WAIT_ATTEMPTS; ++attempt) {
        if (sid == space_manager_active_space()) {
            focused = true;
            break;
        }
        usleep(NATIVE_FULLSCREEN_WAIT_INTERVAL_US);
    }
    if (!focused) return false;

    bool fullscreen = !window_is_fullscreen(window);
    AXError result = AXUIElementSetAttributeValue(window->ref, kAXFullscreenAttribute,
                                                   fullscreen ? kCFBooleanTrue : kCFBooleanFalse);
    if (result != kAXErrorSuccess) return false;

    //
    // NOTE(asmvik): We toggled the fullscreen attribute and must
    // now spin lock until the post-exit space animation has finished.
    //

    return window_manager_wait_for_native_fullscreen_transition(window, fullscreen);
}

static struct window_node *window_node_toggle_zoom_parent(struct window_node *node)
{
    if (!node->parent) return NULL;

    if (node->zoom == node->parent) {
        node->zoom = NULL;
        return node->parent;
    }

    node->parent->left->zoom = NULL;
    node->parent->right->zoom = NULL;
    node->zoom = node->parent;
    return node->parent;
}

void window_manager_toggle_window_zoom_parent(struct window_manager *wm, struct window *window)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view || view->layout != VIEW_BSP) return;

    struct window_node *node = view_find_window_node(view, window->id);
    assert(node);

    struct window_node *flush_node = window_node_toggle_zoom_parent(node);
    if (!flush_node) return;

    if (space_is_visible(view->sid)) {
        window_node_flush(flush_node);
    } else {
        view_set_flag(view, VIEW_IS_DIRTY);
    }
}

void window_manager_toggle_window_zoom_fullscreen(struct window_manager *wm, struct window *window)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view || view->layout != VIEW_BSP) return;

    struct window_node *node = view_find_window_node(view, window->id);
    assert(node);

    if (node == view->root) {
        if (!view_toggle_single_window_zoom(view)) return;
        if (space_is_visible(view->sid)) view_flush(view);
        else view_set_flag(view, VIEW_IS_DIRTY);
        return;
    }

    if (node->zoom == view->root) {
        node->zoom = NULL;
        if (space_is_visible(view->sid)) {
            window_node_flush(view->root);
        } else {
            view_set_flag(view, VIEW_IS_DIRTY);
        }
    } else {
        node->zoom = view->root;
        if (space_is_visible(view->sid)) {
            window_node_flush(node);
        } else {
            view_set_flag(view, VIEW_IS_DIRTY);
        }
    }
}

void window_manager_toggle_window_windowed_fullscreen(struct window *window)
{
    TIME_FUNCTION;

    uint32_t did = window_display_id(window->id);
    if (!did) return;

    if (window_check_flag(window, WINDOW_WINDOWED)) {
        window_clear_flag(window, WINDOW_WINDOWED);
        window_manager_animate_window((struct window_capture) { .window = window, .x = window->windowed_frame.origin.x , .y = window->windowed_frame.origin.y, .w = window->windowed_frame.size.width, .h = window->windowed_frame.size.height });
    } else {
        window_set_flag(window, WINDOW_WINDOWED);
        window->windowed_frame = window->frame;
        CGRect bounds = display_bounds_constrained(did, false);
        window_manager_animate_window((struct window_capture) { .window = window, .x = bounds.origin.x, .y = bounds.origin.y, .w = bounds.size.width, .h = bounds.size.height });
    }
}

void window_manager_toggle_window_expose(struct window *window)
{
    TIME_FUNCTION;

    window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
    CoreDockSendNotification(CFSTR("com.apple.expose.front.awake"), 0);
}

void window_manager_toggle_window_pip(struct space_manager *sm, struct window *window)
{
    TIME_FUNCTION;

    uint32_t did = window_display_id(window->id);
    if (!did) return;

    uint64_t sid = display_space_id(did);
    struct view *dview = space_manager_find_view(sm, sid);

    CGRect bounds = display_bounds_constrained(did, false);
    if (dview && view_check_flag(dview, VIEW_ENABLE_PADDING)) {
        bounds.origin.x    += dview->left_padding;
        bounds.size.width  -= view_padding_sum(dview->left_padding, dview->right_padding);
        bounds.origin.y    += dview->top_padding;
        bounds.size.height -= view_padding_sum(dview->top_padding, dview->bottom_padding);
    }

    bounds.size.width = fmax(0.0, bounds.size.width);
    bounds.size.height = fmax(0.0, bounds.size.height);
    scripting_addition_scale_window(window->id, bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
}

static inline struct window *window_manager_find_scratchpad_window(struct window_manager *wm, char *label)
{
    for (int i = 0; i < buf_len(wm->scratchpad_window); ++i) {
        if (string_equals(wm->scratchpad_window[i].label, label)) {
            return wm->scratchpad_window[i].window;
        }
    }

    return NULL;
}

bool window_manager_toggle_scratchpad_window_by_label(struct window_manager *wm, char *label)
{
    struct window *window = window_manager_find_scratchpad_window(wm, label);
    return window ? window_manager_toggle_scratchpad_window(wm, window, 0) : false;
}

#ifdef TESTS
static CGError (*window_manager_window_is_ordered_in)(int, uint32_t, uint8_t *) = SLSWindowIsOrderedIn;
#else
#define window_manager_window_is_ordered_in SLSWindowIsOrderedIn
#endif

static bool window_manager_scratchpad_ordered_state(struct window *window, int forced_mode, uint8_t *ordered_in)
{
    return forced_mode != 0 ||
           window_manager_window_is_ordered_in(g_connection, window->id, ordered_in) == kCGErrorSuccess;
}

bool window_manager_toggle_scratchpad_window(struct window_manager *wm, struct window *window, int forced_mode)
{
    TIME_FUNCTION;

    uint64_t sid = space_manager_active_space();
    if (!sid) return false;

    // TODO(asmvik): Both functions use the same underlying API and could be combined in a single function to reduce redundant work.
    bool visible_space = window_space(window->id) == sid || window_is_sticky(window->id);

    uint8_t ordered_in = 0;
    if (!window_manager_scratchpad_ordered_state(window, forced_mode, &ordered_in)) return false;

    switch (forced_mode) {
    case 0: goto mode_0;
    case 1: goto mode_1;
    case 2: goto mode_2;
    case 3: goto mode_3;
    }

mode_0:;
    if (visible_space && ordered_in) {
mode_1:;
        struct window *next = window_manager_find_window_on_space_by_rank_filtering_window(wm, sid, 1, window->id);
        if (next) {
            window_manager_focus_window_with_raise(&next->application->psn, next->id, next->ref);
        } else {
            _SLPSSetFrontProcessWithOptions(&g_process_manager.finder_psn, 0, kCPSNoWindows);
        }
        scripting_addition_order_window(window->id, 0, 0);
    } else if (visible_space && !ordered_in) {
mode_2:;
        scripting_addition_order_window(window->id, 1, 0);
        window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
    } else {
mode_3:;
        space_manager_move_window_to_space(sid, window);
        scripting_addition_order_window(window->id, 1, 0);
        window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
    }

    return true;
}

bool window_manager_set_scratchpad_for_window(struct window_manager *wm, struct window *window, char *label)
{
    if (!label) return false;
    struct window *existing_window = window_manager_find_scratchpad_window(wm, label);
    if (existing_window) return false;

    window_manager_remove_scratchpad_for_window(wm, window, false);
    buf_push(wm->scratchpad_window, ((struct scratchpad) {
        .label = label,
        .window = window
    }));
    window->scratchpad = label;
    window_manager_make_window_floating(&g_space_manager, wm, window, true, false);

    return true;
}

bool window_manager_remove_scratchpad_for_window(struct window_manager *wm, struct window *window, bool unfloat)
{
    for (int i = 0; i < buf_len(wm->scratchpad_window); ++i) {
        if (wm->scratchpad_window[i].window == window) {
            window->scratchpad = NULL;

            free(wm->scratchpad_window[i].label);
            buf_del(wm->scratchpad_window, i);

            if (unfloat) {
                window_manager_toggle_scratchpad_window(wm, window, 3);
                window_manager_make_window_floating(&g_space_manager, wm, window, false, false);
            }

            return true;
        }
    }

    return false;
}

#ifdef TESTS
static bool (*window_manager_scratchpad_existing_application_window_list)(struct application *, uint32_t **, int *) = window_manager_existing_application_window_list_with_status;
static bool (*window_manager_scratchpad_order_window_in)(uint32_t *, int) = scripting_addition_order_window_in;
static bool (*window_manager_scratchpad_refresh_application_windows)(struct space_manager *) = space_manager_refresh_application_windows;
#else
#define window_manager_scratchpad_existing_application_window_list window_manager_existing_application_window_list_with_status
#define window_manager_scratchpad_order_window_in scripting_addition_order_window_in
#define window_manager_scratchpad_refresh_application_windows space_manager_refresh_application_windows
#endif

bool window_manager_scratchpad_recover_windows(void)
{
    int window_count = 0;
    uint32_t *window_list = NULL;
    if (!window_manager_scratchpad_existing_application_window_list(NULL, &window_list, &window_count)) return false;
    if (!window_count) return true;
    if (!window_list) return false;

    for (int offset = 0; offset < window_count; offset += 1022) {
        int batch_count = min(1022, window_count - offset);
        if (!window_manager_scratchpad_order_window_in(window_list + offset, batch_count)) return false;
    }
    if (!window_manager_scratchpad_refresh_application_windows(&g_space_manager)) return false;
    event_loop_window_inventory_did_change();
    return true;
}

static void window_manager_record_mutated_view(struct view **views, int *count, int capacity, struct view *view)
{
    for (int i = 0; i < *count; ++i) {
        if (views[i] == view) return;
    }
    if (*count < capacity) views[(*count)++] = view;
}

static void window_manager_validate_windows_on_space(struct window_manager *wm, struct view *view, uint32_t *window_list, int window_count, struct view **mutated_views, int *mutated_count, int mutated_capacity)
{
    int view_window_count;
    uint32_t *view_window_list = view_find_window_list(view, &view_window_count);

    for (int i = 0; i < view_window_count; ++i) {
        bool found = false;

        for (int j = 0; j < window_count; ++j) {
            if (view_window_list[i] == window_list[j]) {
                found = true;
                break;
            }
        }

        if (!found) {
            struct window *window = window_manager_find_window(wm, view_window_list[i]);
            if (!window) continue;

            // A visible-window inventory can transiently omit a window while it
            // exits native fullscreen. Preserve the existing split while the
            // window remains eligible and still belongs to this Space.
            if (!window_check_flag(window, WINDOW_FULLSCREEN) &&
                window_manager_should_manage_window(window) &&
                window_space(window->id) == view->sid) continue;

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
            window_manager_adjust_layer(window, LAYER_NORMAL);
            window_manager_remove_managed_window(wm, window->id);
            window_manager_purify_window(wm, window);

            view_set_flag(view, VIEW_IS_DIRTY);
            window_manager_record_mutated_view(mutated_views, mutated_count, mutated_capacity, view);
        }
    }
}

static bool window_manager_check_for_windows_on_space(struct window_manager *wm, struct view *view, uint32_t *window_list, int window_count, struct view **mutated_views, int *mutated_count, int mutated_capacity)
{
    for (int i = 0; i < window_count; ++i) {
        struct window *window = window_manager_find_window(wm, window_list[i]);
        if (!window || !window_manager_should_manage_window(window)) continue;

        struct view *existing_view = window_manager_find_managed_window(wm, window);
        if (existing_view && existing_view->layout != VIEW_FLOAT && existing_view != view) {
            // A title-bar or modifier drag appears in the destination Space's
            // inventory before mouse-up. Leave ownership with the gesture until
            // its drop handler commits the transfer.
            if (window == g_mouse_state.window) continue;
            if (!window_manager_transfer_managed_window_internal(wm, window, existing_view, view, 0, false)) {
                return false;
            }
            view_set_flag(existing_view, VIEW_IS_DIRTY);
            window_manager_record_mutated_view(mutated_views, mutated_count, mutated_capacity, existing_view);
        }

        if (!existing_view) {

            //
            // @cleanup
            //
            // :AXBatching
            //
            // NOTE(asmvik): Batch all operations and mark the view as dirty so that we can perform a single flush,
            // making sure that each window is only moved and resized a single time, when the final layout has been computed.
            // This is necessary to make sure that we do not call the AX API for each modification to the tree.
            //

            enum managed_window_result managed_result = window_manager_tile_window_on_space(&g_space_manager, wm,
                                                                                             window, view->sid, 0, false);
            if (managed_result == MANAGED_WINDOW_FAILED) return false;
            view_set_flag(view, VIEW_IS_DIRTY);
            window_manager_record_mutated_view(mutated_views, mutated_count, mutated_capacity, view);
        }
    }
    return true;
}

static void window_manager_finalize_mutated_views(struct view **mutated_views, int mutated_count)
{
    for (int i = 0; i < mutated_count; ++i) {
        struct view *mutated_view = mutated_views[i];
        if (view_layout_is_accordion(mutated_view->layout)) view_update(mutated_view);
        if (space_is_visible(mutated_view->sid) && view_is_dirty(mutated_view)) {
            window_node_flush(mutated_view->root);
            view_clear_flag(mutated_view, VIEW_IS_DIRTY);
        }
    }
}

bool window_manager_reconcile_windows_on_space_with_storage(struct window_manager *wm, struct view *view, uint32_t *window_list, int window_count, bool inventory_succeeded, struct view **mutated_views, int mutated_capacity)
{
    if (!inventory_succeeded || window_count < 0 || (window_count && !window_list) ||
        !mutated_views || mutated_capacity < 1 || window_count > mutated_capacity - 1) return false;

    int mutated_count = 0;
    window_manager_record_mutated_view(mutated_views, &mutated_count, mutated_capacity, view);
    window_manager_validate_windows_on_space(wm, view, window_list, window_count, mutated_views, &mutated_count, mutated_capacity);
    if (!window_manager_check_for_windows_on_space(wm, view, window_list, window_count,
                                                   mutated_views, &mutated_count, mutated_capacity)) {
        window_manager_finalize_mutated_views(mutated_views, mutated_count);
        return false;
    }

    window_manager_finalize_mutated_views(mutated_views, mutated_count);
    return true;
}

bool window_manager_reconcile_windows_on_space(struct window_manager *wm, struct view *view, uint32_t *window_list, int window_count, bool inventory_succeeded)
{
    if (!inventory_succeeded || window_count < 0 || window_count == INT_MAX) return false;
    int capacity = window_count + 1;
    struct view **mutated_views = calloc((size_t) capacity, sizeof(struct view *));
    if (!mutated_views) return false;
    bool result = window_manager_reconcile_windows_on_space_with_storage(wm, view, window_list, window_count, true, mutated_views, capacity);
    free(mutated_views);
    return result;
}

void window_manager_validate_and_check_for_windows_on_space(struct space_manager *sm, struct window_manager *wm, uint64_t sid)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (!view || view->layout == VIEW_FLOAT) return;

    int window_count = 0;
    bool inventory_succeeded = false;
    uint32_t *window_list = space_window_list_for_connection_with_status(&sid, 1, 0, &window_count, false, &inventory_succeeded);
    window_manager_reconcile_windows_on_space(wm, view, window_list, window_count, inventory_succeeded);
}

void window_manager_correct_for_mission_control_changes(struct space_manager *sm, struct window_manager *wm)
{
    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return;

    float animation_duration = wm->window_animation_duration;
    wm->window_animation_duration = 0.0f;

    for (int i = 0; i < display_count; ++i) {
        uint32_t did = display_list[i];

        int space_count;
        uint64_t *space_list = display_space_list(did, &space_count);
        if (!space_list) continue;

        uint64_t sid = display_space_id(did);
        for (int j = 0; j < space_count; ++j) {
            if (space_list[j] == sid) {
                window_manager_validate_and_check_for_windows_on_space(sm, wm, sid);
            } else {
                space_manager_mark_view_invalid(sm, space_list[j]);
            }
        }
    }

    wm->window_animation_duration = animation_duration;
}

bool window_manager_handle_display_add_and_remove(struct space_manager *sm, struct window_manager *wm, uint32_t did)
{
    int space_count;
    uint64_t *space_list = display_space_list(did, &space_count);
    if (!space_list || space_count < 0) return false;

    for (int i = 0; i < space_count; ++i) {
        if (space_is_user(space_list[i])) {
            int window_count;
            bool inventory_succeeded = false;
            uint32_t *window_list = space_window_list_for_connection_with_status(&space_list[i], 1, 0,
                                                                                  &window_count, false,
                                                                                  &inventory_succeeded);
            if (!inventory_succeeded || window_count < 0 || window_count >= INT_MAX) return false;
            struct view *view = space_manager_find_view(sm, space_list[i]);
            if (!view) return false;
            if (view->layout != VIEW_FLOAT) {
                int mutated_count = 0;
                int capacity = window_count + 1;
                struct view **mutated_views = calloc((size_t) capacity, sizeof(struct view *));
                if (!mutated_views) return false;
                window_manager_check_for_windows_on_space(wm, view, window_list, window_count, mutated_views, &mutated_count, capacity);
                window_manager_finalize_mutated_views(mutated_views, mutated_count);
                free(mutated_views);
            }
            break;
        }
    }

    uint64_t sid = display_space_id(did);
    for (int i = 0; i < space_count; ++i) {
        struct view *view = space_manager_find_view(sm, space_list[i]);
        if (!view) return false;
        if (space_list[i] == sid) {
            if (view->layout != VIEW_FLOAT) {
                view_update(view);
                view_flush(view);
            }
        } else if (view->layout != VIEW_FLOAT) {
            view_clear_flag(view, VIEW_IS_VALID);
        }
    }
    return sid != 0;
}

void window_manager_init(struct window_manager *wm)
{
    wm->system_element = AXUIElementCreateSystemWide();
    AXUIElementSetMessagingTimeout(wm->system_element, 1.0);

    wm->configured_ffm_mode = FFM_DISABLED;
    wm->ffm_menu_suspend_count = 0;
    wm->ffm_anonymous_menu_suspend_count = 0;
    window_manager_recompute_effective_ffm_mode(wm);
    wm->purify_mode = PURIFY_DISABLED;
    wm->window_origin_mode = WINDOW_ORIGIN_DEFAULT;
    wm->window_focus_fallback = WINDOW_FOCUS_FALLBACK_OFF;
    wm->enable_mff = false;
    wm->enable_window_opacity = false;
    wm->menubar_opacity = 1.0f;
    wm->active_window_opacity = 1.0f;
    wm->normal_window_opacity = 1.0f;
    wm->window_opacity_duration = 0.0f;
    wm->window_animation_duration = 0.0f;
    wm->window_animation_easing = ease_out_circ_type;
    wm->insert_feedback_color = rgba_color_from_hex(0xffd75f5f);
    wm->insert_feedback_radius = INSERT_FEEDBACK_RADIUS_DEFAULT;
    wm->insert_feedback_width = INSERT_FEEDBACK_WIDTH_DEFAULT;

    table_init(&wm->application, 150, hash_wm, compare_wm);
    table_init(&wm->window, 150, hash_wm, compare_wm);
    table_init(&wm->managed_window, 150, hash_wm, compare_wm);
    table_init(&wm->window_lost_focused_event, 150, hash_wm, compare_wm);
    table_init(&wm->application_lost_front_switched_event, 150, hash_psn, compare_psn);
    table_init(&wm->window_animations_table, 150, hash_wm, compare_wm);
    table_init(&wm->insert_feedback, 150, hash_wm, compare_wm);
    pthread_mutex_init(&wm->window_animations_lock, NULL);
}

#ifdef TESTS
static bool (*window_manager_test_application_is_observable)(struct process *) = workspace_application_is_observable;
#endif

void window_manager_begin(struct space_manager *sm, struct window_manager *wm)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    table_for (struct process *process, g_process_manager.process, {
#ifdef TESTS
        if (window_manager_test_application_is_observable(process)) {
#else
        if (workspace_application_is_observable(process)) {
#endif
            struct application *application = application_create(process);
            if (!application) {
                event_loop_schedule_application_launch_retry(process);
                continue;
            }

            if (application_observe(application)) {
                if (window_manager_add_application(wm, application)) {
                    process_launch_retries_reset(process);
                    window_manager_add_existing_application_windows(sm, wm, application, -1, NULL, NULL);
                } else {
                    application_unobserve(application);
                    application_destroy(application);
                    event_loop_schedule_application_launch_retry(process);
                }
            } else {
                bool ax_retry = application->ax_retry;

                application_unobserve(application);
                application_destroy(application);
                debug("%s: could not observe notifications for %s (%d) (%d)\n", __FUNCTION__, process->name, process->pid, ax_retry);

                //
                // NOTE(hankun): The accessibility API reports kAXErrorCannotComplete for applications that
                // are busy or have not settled yet. This is common when yabai is (re)started right after the
                // system wakes from sleep, and previously caused such applications to be dropped permanently.
                // Retry through the regular application-launched path instead, matching the behaviour of
                // EVENT_HANDLER(APPLICATION_LAUNCHED).
                //

                if (ax_retry) event_loop_schedule_application_launch_retry(process);
            }
        } else {
            debug("%s: %s (%d) is not observable, subscribing to activationPolicy changes\n", __FUNCTION__, process->name, process->pid);
            workspace_application_observe_activation_policy(g_workspace_context, process);
        }
    })
    [pool drain];

    struct window *window = window_manager_focused_window(wm);
    if (window) {
        wm->last_window_id = window->id;
        wm->focused_window_id = window->id;
        wm->focused_window_psn = window->application->psn;
        window_manager_set_window_opacity(wm, window, wm->active_window_opacity);
    }
}
