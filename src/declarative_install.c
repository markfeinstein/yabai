extern struct event_loop g_event_loop;
extern struct window_manager g_window_manager;
extern struct space_manager g_space_manager;
extern struct display_manager g_display_manager;
extern struct mouse_state g_mouse_state;
extern bool g_verbose;

struct declarative_installed_state
{
    uint64_t generation;
    struct declarative_config_model *model;
    char *path;
    struct declarative_topology topology;
    struct rule_fold_context *fold_context;
    struct declarative_prepared_rules prepared_rules;
    char **labels;
    size_t label_count;
    char *selected_mode;
    bool has_scaffold;
};

static struct declarative_installed_state declarative_installed;

static bool declarative_policies_have_scaffold(const struct declarative_space_policy *policies,
                                               size_t policy_count)
{
    for (size_t i = 0; i < policy_count; ++i) if (policies[i].scaffold_count) return true;
    return false;
}

#ifdef TESTS
static size_t declarative_install_allocation_ordinal;
static size_t declarative_install_fail_ordinal;
static size_t declarative_install_outstanding;
static enum declarative_reload_test_stage declarative_reload_fail_stage;
static const struct declarative_reload_test_ops *declarative_reload_message_test_ops;
static void (*declarative_reload_reconcile_entry_test_hook)(void);
static void (*declarative_reload_prepublication_test_hook)(void);
void declarative_reload_test_set_message_ops(const struct declarative_reload_test_ops *ops)
{ declarative_reload_message_test_ops = ops; }
void declarative_reload_test_set_reconcile_entry_hook(void (*hook)(void))
{ declarative_reload_reconcile_entry_test_hook = hook; }
void declarative_reload_test_set_prepublication_hook(void (*hook)(void))
{ declarative_reload_prepublication_test_hook = hook; }
void declarative_reload_test_fail_stage(enum declarative_reload_test_stage stage)
{ declarative_reload_fail_stage = stage; }
static bool declarative_reload_stage_failed(enum declarative_reload_test_stage stage)
{ return declarative_reload_fail_stage == stage; }
void declarative_install_test_fail_allocation_at(size_t ordinal)
{ declarative_install_allocation_ordinal = 0; declarative_install_fail_ordinal = ordinal; }
size_t declarative_install_test_allocation_count(void) { return declarative_install_allocation_ordinal; }
size_t declarative_install_test_outstanding_allocations(void) { return declarative_install_outstanding; }
static bool declarative_install_should_fail(void)
{ return ++declarative_install_allocation_ordinal == declarative_install_fail_ordinal; }
#else
static bool declarative_install_should_fail(void) { return false; }
#define declarative_reload_stage_failed(stage) false
#endif

static void *declarative_install_calloc(size_t count, size_t size)
{
    if (count && size > SIZE_MAX / count) return NULL;
    if (declarative_install_should_fail()) return NULL;
    void *value = calloc(count, size);
#ifdef TESTS
    if (value) ++declarative_install_outstanding;
#endif
    return value;
}

static void declarative_install_free(void *value)
{
    if (!value) return;
#ifdef TESTS
    assert(declarative_install_outstanding > 0);
    --declarative_install_outstanding;
#endif
    free(value);
}

static char *declarative_install_strdup(const char *value)
{
    if (!value) return NULL;
    size_t length = strlen(value);
    char *copy = declarative_install_calloc(length + 1, 1);
    if (copy) memcpy(copy, value, length + 1);
    return copy;
}

struct declarative_startup_bundle
{
    struct window_manager window_manager;
    struct space_manager space_manager;
    struct display_manager display_manager;
    struct mouse_state mouse_state;
    struct menu_bar_settings menu_settings;
    bool verbose;
    struct declarative_prepared_rules rules;
    struct rule *rule_buffer;
    size_t rule_buffer_owned_count;
    struct rule_fold_context *fold_context;
    char **labels;
    size_t label_count;
    char *path;
    char *selected_mode;
    struct declarative_space_composition composition;
    bool window_manager_initialized;
};

#ifdef TESTS
static int (*declarative_install_mutex_init)(pthread_mutex_t *, const pthread_mutexattr_t *) = pthread_mutex_init;
static int (*declarative_install_mutex_destroy)(pthread_mutex_t *) = pthread_mutex_destroy;
#else
#define declarative_install_mutex_init pthread_mutex_init
#define declarative_install_mutex_destroy pthread_mutex_destroy
#endif

static void declarative_install_destroy_window_manager(struct window_manager *wm)
{
    if (!wm) return;
    if (wm->system_element) CFRelease(wm->system_element);
    if (wm->application.buckets) table_free(&wm->application);
    if (wm->window.buckets) table_free(&wm->window);
    if (wm->managed_window.buckets) table_free(&wm->managed_window);
    if (wm->window_lost_focused_event.buckets) table_free(&wm->window_lost_focused_event);
    if (wm->application_lost_front_switched_event.buckets) table_free(&wm->application_lost_front_switched_event);
    if (wm->window_animations_table.buckets) table_free(&wm->window_animations_table);
    if (wm->insert_feedback.buckets) table_free(&wm->insert_feedback);
    memset(wm, 0, sizeof(*wm));
}

static bool declarative_install_table_init(struct table *table, int capacity,
                                           table_hash_func *hash, table_compare_func *cmp)
{
    if (capacity <= 0 || declarative_install_should_fail()) return false;
    *table = (struct table) { .capacity = capacity, .max_load = 0.75f, .hash = hash, .cmp = cmp };
    table->buckets = calloc((size_t) capacity, sizeof(*table->buckets));
    return table->buckets != NULL;
}

static bool declarative_install_window_manager_init(struct window_manager *wm)
{
    memset(wm, 0, sizeof(*wm));
    wm->system_element = AXUIElementCreateSystemWide();
    if (!wm->system_element) return false;
    AXUIElementSetMessagingTimeout(wm->system_element, 1.0);
    wm->configured_ffm_mode = FFM_DISABLED;
    window_manager_recompute_effective_ffm_mode(wm);
    wm->purify_mode = PURIFY_DISABLED;
    wm->window_origin_mode = WINDOW_ORIGIN_DEFAULT;
    wm->window_focus_fallback = WINDOW_FOCUS_FALLBACK_OFF;
    wm->menubar_opacity = 1.0f;
    wm->active_window_opacity = 1.0f;
    wm->normal_window_opacity = 1.0f;
    wm->window_animation_easing = ease_out_circ_type;
    wm->insert_feedback_color = rgba_color_from_hex(0xffd75f5f);
    wm->insert_feedback_radius = INSERT_FEEDBACK_RADIUS_DEFAULT;
    wm->insert_feedback_width = INSERT_FEEDBACK_WIDTH_DEFAULT;
    if (!declarative_install_table_init(&wm->application, 150, hash_wm, compare_wm) ||
        !declarative_install_table_init(&wm->window, 150, hash_wm, compare_wm) ||
        !declarative_install_table_init(&wm->managed_window, 150, hash_wm, compare_wm) ||
        !declarative_install_table_init(&wm->window_lost_focused_event, 150, hash_wm, compare_wm) ||
        !declarative_install_table_init(&wm->application_lost_front_switched_event, 150, hash_psn, compare_psn) ||
        !declarative_install_table_init(&wm->window_animations_table, 150, hash_wm, compare_wm) ||
        !declarative_install_table_init(&wm->insert_feedback, 150, hash_wm, compare_wm)) {
        if (wm->system_element) CFRelease(wm->system_element);
        if (wm->application.buckets) table_free(&wm->application);
        if (wm->window.buckets) table_free(&wm->window);
        if (wm->managed_window.buckets) table_free(&wm->managed_window);
        if (wm->window_lost_focused_event.buckets) table_free(&wm->window_lost_focused_event);
        if (wm->application_lost_front_switched_event.buckets) table_free(&wm->application_lost_front_switched_event);
        if (wm->window_animations_table.buckets) table_free(&wm->window_animations_table);
        if (wm->insert_feedback.buckets) table_free(&wm->insert_feedback);
        memset(wm, 0, sizeof(*wm));
        return false;
    }
    return true;
}

static void declarative_startup_bundle_destroy(struct declarative_startup_bundle *bundle)
{
    if (!bundle) return;
    if (bundle->rule_buffer) {
        for (size_t i = 0; i < bundle->rule_buffer_owned_count; ++i) rule_destroy(&bundle->rule_buffer[i]);
        free(buf__hdr(bundle->rule_buffer));
    }
    declarative_prepared_rules_destroy(&bundle->rules);
    declarative_rule_fold_context_destroy(bundle->fold_context);
    if (bundle->labels) {
        for (size_t i = 0; i < bundle->label_count; ++i) declarative_install_free(bundle->labels[i]);
    }
    declarative_install_free(bundle->labels);
    declarative_install_free(bundle->path);
    declarative_install_free(bundle->selected_mode);
    declarative_modes_composition_destroy(&bundle->composition);
    if (bundle->window_manager_initialized) declarative_install_destroy_window_manager(&bundle->window_manager);
    memset(bundle, 0, sizeof(*bundle));
}

static void declarative_install_space_defaults(struct space_manager *sm)
{
    *sm = (struct space_manager) {
        .layout = VIEW_FLOAT,
        .accordion_padding = 30,
        .single_window_width_ratio = 1.0f,
        .split_ratio = 0.5f,
        .split_type = SPLIT_AUTO,
        .window_placement = CHILD_SECOND,
        .window_insertion_point = INSERT_FOCUSED,
        .window_zoom_persist = true
    };
}

static bool declarative_install_prepare_labels(const struct declarative_config_model *model,
                                               struct declarative_startup_bundle *bundle)
{
    for (size_t i = 0; i < model->rule_count; ++i) if (model->rules[i].label) ++bundle->label_count;
    if (!bundle->label_count) return true;
    bundle->labels = declarative_install_calloc(bundle->label_count, sizeof(*bundle->labels));
    if (!bundle->labels) return false;
    size_t position = 0;
    for (size_t i = 0; i < model->rule_count; ++i) {
        if (!model->rules[i].label) continue;
        bundle->labels[position] = declarative_install_strdup(model->rules[i].label);
        if (!bundle->labels[position++]) return false;
    }
    return true;
}

static bool declarative_install_prepare_rule_buffer(struct declarative_startup_bundle *bundle)
{
    size_t native_count = bundle->rules.count;
    size_t runtime_start = rule_native_prefix_length();
    size_t current_count = (size_t) buf_len(g_window_manager.rules);
    if (runtime_start > current_count) return false;
    size_t runtime_count = current_count - runtime_start;
    if (native_count + runtime_count > INT_MAX - 1) return false;
    int capacity = (int) (native_count + runtime_count + 1);
    size_t bytes = offsetof(struct buf_hdr, buf) + (size_t) capacity * sizeof(struct rule);
    if (declarative_install_should_fail()) return false;
    struct buf_hdr *header = calloc(1, bytes);
    if (!header) return false;
    header->cap = capacity;
    header->len = (int) (native_count + runtime_count);
    bundle->rule_buffer = (struct rule *) header->buf;
    bundle->rule_buffer_owned_count = native_count;
    for (size_t i = 0; i < native_count; ++i) {
        if (!bundle->rules.live_present[i]) return false;
        bundle->rule_buffer[i] = bundle->rules.live[i];
        memset(&bundle->rules.live[i], 0, sizeof(bundle->rules.live[i]));
        bundle->rules.live_present[i] = false;
    }
    for (size_t i = 0; i < runtime_count; ++i) bundle->rule_buffer[native_count + i] = g_window_manager.rules[runtime_start + i];
    return true;
}

static void declarative_install_apply_candidate_value(struct declarative_startup_bundle *bundle,
                                                       enum declarative_config_id id,
                                                       const struct declarative_value *value)
{
    struct window_manager *wm = &bundle->window_manager;
    struct space_manager *sm = &bundle->space_manager;
    struct display_manager *dm = &bundle->display_manager;
    struct mouse_state *mouse = &bundle->mouse_state;
    switch (id) {
    case DECLARATIVE_CONFIG_DEBUG_OUTPUT: bundle->verbose = value->boolean; break;
    case DECLARATIVE_CONFIG_MFF: wm->enable_mff = value->boolean; break;
    case DECLARATIVE_CONFIG_FFM: wm->configured_ffm_mode = value->enumeration; window_manager_recompute_effective_ffm_mode(wm); break;
    case DECLARATIVE_CONFIG_DISPLAY_ORDER: dm->order = value->enumeration; break;
    case DECLARATIVE_CONFIG_WINDOW_ORIGIN: wm->window_origin_mode = value->enumeration; break;
    case DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK: wm->window_focus_fallback = value->enumeration; break;
    case DECLARATIVE_CONFIG_WINDOW_PLACEMENT: sm->window_placement = value->enumeration; break;
    case DECLARATIVE_CONFIG_WINDOW_INSERT_POINT: sm->window_insertion_point = value->enumeration; break;
    case DECLARATIVE_CONFIG_WINDOW_ZOOM_PERSIST: sm->window_zoom_persist = value->boolean; break;
    case DECLARATIVE_CONFIG_SKIP_SPACE_ANIMATION: sm->skip_window_focus_animation = value->boolean; break;
    case DECLARATIVE_CONFIG_OPACITY: wm->enable_window_opacity = value->boolean; break;
    case DECLARATIVE_CONFIG_OPACITY_DURATION: wm->window_opacity_duration = value->real; break;
    case DECLARATIVE_CONFIG_ANIMATION_DURATION: wm->window_animation_duration = value->real; break;
    case DECLARATIVE_CONFIG_ANIMATION_EASING: wm->window_animation_easing = value->enumeration; break;
    case DECLARATIVE_CONFIG_SHADOW: wm->purify_mode = value->enumeration; break;
    case DECLARATIVE_CONFIG_MENUBAR: bundle->menu_settings.enabled = value->enumeration; break;
    case DECLARATIVE_CONFIG_MENUBAR_INTERFACE: bundle->menu_settings.interface = value->enumeration; break;
    case DECLARATIVE_CONFIG_MENUBAR_THUMBNAILS: bundle->menu_settings.thumbnails = value->enumeration; break;
    case DECLARATIVE_CONFIG_MENUBAR_STYLE: bundle->menu_settings.style = value->enumeration; break;
    case DECLARATIVE_CONFIG_MENUBAR_SPACE_FILTER: bundle->menu_settings.space_filter = value->enumeration; break;
    case DECLARATIVE_CONFIG_MENUBAR_OPACITY: wm->menubar_opacity = value->real; break;
    case DECLARATIVE_CONFIG_ACTIVE_WINDOW_OPACITY: wm->active_window_opacity = value->real; break;
    case DECLARATIVE_CONFIG_NORMAL_WINDOW_OPACITY: wm->normal_window_opacity = value->real; break;
    case DECLARATIVE_CONFIG_INSERT_FEEDBACK_COLOR: wm->insert_feedback_color = rgba_color_from_hex(value->u32); break;
    case DECLARATIVE_CONFIG_INSERT_FEEDBACK_RADIUS: wm->insert_feedback_radius = value->real; break;
    case DECLARATIVE_CONFIG_INSERT_FEEDBACK_WIDTH: wm->insert_feedback_width = value->real; break;
    case DECLARATIVE_CONFIG_TOP_PADDING: sm->top_padding = value->integer; break;
    case DECLARATIVE_CONFIG_BOTTOM_PADDING: sm->bottom_padding = value->integer; break;
    case DECLARATIVE_CONFIG_LEFT_PADDING: sm->left_padding = value->integer; break;
    case DECLARATIVE_CONFIG_RIGHT_PADDING: sm->right_padding = value->integer; break;
    case DECLARATIVE_CONFIG_WINDOW_GAP: sm->window_gap = value->integer; break;
    case DECLARATIVE_CONFIG_ACCORDION_PADDING: sm->accordion_padding = value->integer; break;
    case DECLARATIVE_CONFIG_SINGLE_WINDOW_MAX_WIDTH: sm->single_window_max_width = value->integer; break;
    case DECLARATIVE_CONFIG_SINGLE_WINDOW_MIN_WIDTH: sm->single_window_min_width = value->integer; break;
    case DECLARATIVE_CONFIG_SINGLE_WINDOW_WIDTH_RATIO: sm->single_window_width_ratio = value->real; break;
    case DECLARATIVE_CONFIG_LAYOUT: sm->layout = value->enumeration; break;
    case DECLARATIVE_CONFIG_SPLIT_RATIO: sm->split_ratio = value->real; break;
    case DECLARATIVE_CONFIG_SPLIT_TYPE: sm->split_type = value->enumeration; break;
    case DECLARATIVE_CONFIG_AUTO_BALANCE: sm->auto_balance = value->u32; break;
    case DECLARATIVE_CONFIG_NATIVE_EDGE_RESIZE: mouse->native_edge_resize = value->boolean; break;
    case DECLARATIVE_CONFIG_MOUSE_MOD: mouse->modifier = value->enumeration; break;
    case DECLARATIVE_CONFIG_MOUSE_ACTION1: mouse->action1 = value->enumeration; break;
    case DECLARATIVE_CONFIG_MOUSE_ACTION2: mouse->action2 = value->enumeration; break;
    case DECLARATIVE_CONFIG_MOUSE_DROP_ACTION: mouse->drop_action = value->enumeration; break;
    case DECLARATIVE_CONFIG_EXTERNAL_BAR:
        dm->mode = value->external_bar.mode;
        dm->top_padding = value->external_bar.top_padding;
        dm->bottom_padding = value->external_bar.bottom_padding;
        break;
    default: break;
    }
}

static bool declarative_install_apply_space_policy(struct declarative_startup_state *startup,
                                                    const struct declarative_space_composition *composition)
{
    struct declarative_space_topology_entry *entries = NULL;
    size_t count = 0;
    if (!declarative_topology_space_snapshot(&startup->topology, &entries, &count) ||
        count != startup->detached_views.count) return false;
    struct declarative_runtime_state empty_runtime = {0};
    struct declarative_space_defaults defaults = declarative_startup_view_defaults(startup->model);
    for (size_t i = 0; i < count; ++i) {
        struct view *view = startup->detached_views.views[i];
        struct declarative_space_target target = {
            .view = view, .sid = entries[i].sid, .index = entries[i].index,
            .uuid = entries[i].uuid, .is_user_space = entries[i].is_user_space,
            .identity = entries[i].identity
        };
        struct declarative_space_effective effective = {0};
        if (!declarative_space_resolve_effective_policy(startup->model, composition->spaces,
                                                        composition->space_count, &empty_runtime,
                                                        &defaults, &target,
                                                        DECLARATIVE_SPACE_PRESERVE_RUNTIME, &effective)) {
            declarative_topology_space_snapshot_destroy(entries);
            return false;
        }
        if (!target.is_user_space) continue;
        struct area area = view->root->area;
        area.x -= (float) view->left_padding;
        area.w += view_padding_sum(view->left_padding, view->right_padding);
        area.y -= (float) view->top_padding;
        area.h += view_padding_sum(view->top_padding, view->bottom_padding);
        struct area bounds = area;
#define APPLY_SPACE(field, id, member) do { \
    view->member = effective.fields[field].value.integer; \
} while (0)
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_LAYOUT, DECLARATIVE_CONFIG_LAYOUT, layout);
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_TOP_PADDING, DECLARATIVE_CONFIG_TOP_PADDING, top_padding);
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING, DECLARATIVE_CONFIG_BOTTOM_PADDING, bottom_padding);
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING, DECLARATIVE_CONFIG_LEFT_PADDING, left_padding);
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING, DECLARATIVE_CONFIG_RIGHT_PADDING, right_padding);
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP, DECLARATIVE_CONFIG_WINDOW_GAP, window_gap);
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING, DECLARATIVE_CONFIG_ACCORDION_PADDING, accordion_padding);
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MAX_WIDTH, DECLARATIVE_CONFIG_SINGLE_WINDOW_MAX_WIDTH, single_window_max_width);
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MIN_WIDTH, DECLARATIVE_CONFIG_SINGLE_WINDOW_MIN_WIDTH, single_window_min_width);
        view->single_window_width_ratio = effective.fields[DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_WIDTH_RATIO].value.real;
        view->split_ratio = effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real;
        APPLY_SPACE(DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE, DECLARATIVE_CONFIG_SPLIT_TYPE, split_type);
        view->auto_balance = effective.fields[DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE].value.u32;
#undef APPLY_SPACE
        area.x += (float) view->left_padding;
        area.w -= view_padding_sum(view->left_padding, view->right_padding);
        area.y += (float) view->top_padding;
        area.h -= view_padding_sum(view->top_padding, view->bottom_padding);
        if (view_layout_is_accordion(view->layout)) {
            float min_x = bounds.x, max_x = bounds.x + bounds.w;
            float min_y = bounds.y, max_y = bounds.y + bounds.h;
            float x = fminf(max_x, fmaxf(min_x, area.x));
            float y = fminf(max_y, fmaxf(min_y, area.y));
            float right = fminf(max_x, fmaxf(min_x, area.x + area.w));
            float bottom = fminf(max_y, fmaxf(min_y, area.y + area.h));
            area = (struct area) { x, y, fmaxf(0.0f, right - x), fmaxf(0.0f, bottom - y) };
        }
        if (!isfinite(area.x) || !isfinite(area.y) || !isfinite(area.w) || !isfinite(area.h) || area.w < 0 || area.h < 0) {
            declarative_topology_space_snapshot_destroy(entries);
            return false;
        }
        view->root->area = area;
        view_apply_single_window_width(view);
    }
    declarative_topology_space_snapshot_destroy(entries);
    return true;
}

static bool declarative_startup_bundle_prepare(struct declarative_startup_state *startup,
                                               const struct declarative_topology_capture_ops *topology_ops,
                                               struct declarative_startup_bundle *bundle)
{
    memset(bundle, 0, sizeof(*bundle));
    if (!startup || !startup->present || !startup->model || !startup->path[0] ||
        rule_native_generation() != 0 || g_window_manager.rules || g_space_manager.view.buckets) return false;

    struct declarative_modes_error modes_error = {0};
    if (!declarative_modes_compose(startup->model, NULL, &bundle->composition, &modes_error) ||
        (startup->model->active_mode &&
         !(bundle->selected_mode = declarative_install_strdup(startup->model->active_mode)))) return false;

    struct declarative_prepared_settings settings = {0};
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        const struct declarative_value *value = &startup->model->config[i];
        if (!value->present) continue;
        if (!declarative_prepare_setting(&settings, i, value, NULL)) return false;
    }

    struct declarative_space_defaults defaults = declarative_startup_view_defaults(startup->model);
    if (!declarative_startup_prepare_runtime(startup, &defaults, topology_ops) ||
        !declarative_install_apply_space_policy(startup, &bundle->composition)) return false;

    if (!declarative_install_window_manager_init(&bundle->window_manager)) return false;
    bundle->window_manager_initialized = true;
    declarative_install_space_defaults(&bundle->space_manager);
    bundle->display_manager = (struct display_manager) {
        .current_display_id = startup->topology.active_did,
        .last_display_id = startup->topology.active_did,
        .order = DISPLAY_ARRANGEMENT_ORDER_DEFAULT
    };
    bundle->space_manager.current_space_id = startup->topology.active_sid;
    bundle->space_manager.last_space_id = startup->topology.active_sid;
    bundle->space_manager.did_begin = true;
    bundle->mouse_state = g_mouse_state;
    bundle->menu_settings = menu_bar_manager_settings();
    bundle->verbose = g_verbose;
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        if (settings.values[i].present) declarative_install_apply_candidate_value(bundle, i, &settings.values[i]);
    }

    struct declarative_rule_topology_snapshot rule_topology = {0};
    struct declarative_rule_display_entry *owned_displays = NULL;
    struct declarative_rule_space_entry *owned_spaces = NULL;
    if (!declarative_topology_rule_snapshot(&startup->topology, &rule_topology, &owned_displays, &owned_spaces)) return false;
    const char *runtime_labels[buf_len(g_window_manager.rules) ? buf_len(g_window_manager.rules) : 1];
    size_t runtime_label_count = 0;
    for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
        if (g_window_manager.rules[i].label) runtime_labels[runtime_label_count++] = g_window_manager.rules[i].label;
    }
    enum declarative_rule_prepare_result rules_result =
        declarative_prepare_rules(startup->model->rules, startup->model->rule_count, &rule_topology,
                                  bundle->display_manager.order, 1, runtime_labels, runtime_label_count, &bundle->rules);
    if (rules_result == DECLARATIVE_RULE_PREPARE_OK) bundle->fold_context = declarative_rule_fold_context_create(&rule_topology);
    declarative_topology_rule_snapshot_destroy(owned_displays, owned_spaces);
    if (rules_result != DECLARATIVE_RULE_PREPARE_OK || !bundle->fold_context ||
        !declarative_install_prepare_labels(startup->model, bundle) ||
        !declarative_install_prepare_rule_buffer(bundle) ||
        !(bundle->path = declarative_install_strdup(startup->path))) return false;
    return true;
}

static void declarative_install_move_window_manager(struct window_manager *destination,
                                                    struct window_manager *source)
{
    destination->system_element = source->system_element;
    destination->application = source->application;
    destination->window = source->window;
    destination->managed_window = source->managed_window;
    destination->window_lost_focused_event = source->window_lost_focused_event;
    destination->application_lost_front_switched_event = source->application_lost_front_switched_event;
    destination->window_animations_table = source->window_animations_table;
    destination->insert_feedback = source->insert_feedback;
    destination->rules = source->rules;
    destination->applications_to_refresh = source->applications_to_refresh;
    destination->application_refresh_cursor = source->application_refresh_cursor;
    destination->focused_window_id = source->focused_window_id;
    destination->focused_window_psn = source->focused_window_psn;
    destination->last_window_id = source->last_window_id;
    destination->enable_mff = source->enable_mff;
    destination->configured_ffm_mode = source->configured_ffm_mode;
    destination->effective_ffm_mode = source->effective_ffm_mode;
    destination->ffm_menu_suspend_count = source->ffm_menu_suspend_count;
    destination->ffm_anonymous_menu_suspend_count = source->ffm_anonymous_menu_suspend_count;
    destination->purify_mode = source->purify_mode;
    destination->window_origin_mode = source->window_origin_mode;
    destination->window_focus_fallback = source->window_focus_fallback;
    destination->enable_window_opacity = source->enable_window_opacity;
    destination->menubar_opacity = source->menubar_opacity;
    destination->active_window_opacity = source->active_window_opacity;
    destination->normal_window_opacity = source->normal_window_opacity;
    destination->window_opacity_duration = source->window_opacity_duration;
    destination->window_animation_duration = source->window_animation_duration;
    destination->window_animation_easing = source->window_animation_easing;
    destination->insert_feedback_color = source->insert_feedback_color;
    destination->insert_feedback_radius = source->insert_feedback_radius;
    destination->insert_feedback_width = source->insert_feedback_width;
    destination->scratchpad_window = source->scratchpad_window;
    memset(source, 0, sizeof(*source));
}

static bool declarative_startup_bundle_publish(struct declarative_startup_state *startup,
                                               struct declarative_startup_bundle *bundle)
{
    struct table adopted_views = {0};
    if (!declarative_detached_view_table_adopt(&startup->detached_views, &adopted_views)) return false;
    bundle->space_manager.view = adopted_views;
    /* No allocation, topology query, table insertion, event post or setter side effect below. */
    g_verbose = bundle->verbose;
    declarative_install_move_window_manager(&g_window_manager, &bundle->window_manager);
    bundle->window_manager_initialized = false;
    g_window_manager.rules = bundle->rule_buffer;
    bundle->rule_buffer = NULL;
    bundle->rule_buffer_owned_count = 0;
    g_space_manager = bundle->space_manager;
    g_display_manager = bundle->display_manager;
    __atomic_store_n(&g_mouse_state.modifier, bundle->mouse_state.modifier, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mouse_state.action1, bundle->mouse_state.action1, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mouse_state.action2, bundle->mouse_state.action2, __ATOMIC_RELEASE);
    g_mouse_state.drop_action = bundle->mouse_state.drop_action;
    g_mouse_state.native_edge_resize = bundle->mouse_state.native_edge_resize;
    menu_bar_manager_startup_settings_commit(&bundle->menu_settings);

    declarative_installed.generation = 1;
    declarative_installed.model = startup->model;
    declarative_installed.has_scaffold = declarative_policies_have_scaffold(bundle->composition.spaces,
                                                                             bundle->composition.space_count);
    startup->model = NULL;
    declarative_installed.path = bundle->path;
    bundle->path = NULL;
    declarative_installed.selected_mode = bundle->selected_mode;
    bundle->selected_mode = NULL;
    declarative_installed.topology = startup->topology;
    memset(&startup->topology, 0, sizeof(startup->topology));
    declarative_installed.fold_context = bundle->fold_context;
    bundle->fold_context = NULL;
    declarative_installed.prepared_rules = bundle->rules;
    memset(&bundle->rules, 0, sizeof(bundle->rules));
    declarative_installed.labels = bundle->labels;
    declarative_installed.label_count = bundle->label_count;
    bundle->labels = NULL;
    bundle->label_count = 0;
    rule_native_generation_install(1, declarative_installed.prepared_rules.count,
                                   (const char *const *) declarative_installed.labels,
                                   declarative_installed.label_count,
                                   declarative_installed.fold_context);
    return true;
}

bool declarative_startup_install(struct declarative_startup_state *startup,
                                 const struct declarative_topology_capture_ops *topology_ops)
{
    if (!event_loop_is_worker_thread(&g_event_loop)) return false;
    if (!startup || !startup->present) return true;
    struct declarative_startup_bundle bundle = {0};
    if (!declarative_startup_bundle_prepare(startup, topology_ops, &bundle) ||
        !menu_bar_manager_startup_settings_can_commit(&bundle.menu_settings)) {
        declarative_startup_bundle_destroy(&bundle);
        return false;
    }

    /* This is the final fallible prerequisite. Publication below only transfers prepared non-mutex fields. */
    if (declarative_install_mutex_init(&g_window_manager.window_animations_lock, NULL) != 0) {
        declarative_startup_bundle_destroy(&bundle);
        return false;
    }
    if (!declarative_startup_bundle_publish(startup, &bundle)) {
        declarative_install_mutex_destroy(&g_window_manager.window_animations_lock);
        declarative_startup_bundle_destroy(&bundle);
        return false;
    }
    declarative_startup_bundle_destroy(&bundle);
    return true;
}

struct declarative_reload_ops
{
    bool (*select)(struct declarative_startup_state *state, const char *installed_path,
                   const char *xdg_config_home, const char *home,
                   struct declarative_error *error, void *context);
    bool (*capture_topology)(const struct declarative_topology_bounds_config *bounds,
                             struct declarative_topology *topology, void *context);
    bool (*query_sid)(uint64_t sid, const uint32_t **wids, size_t *count, void *context);
    enum declarative_space_query_result (*query_sid_result)(uint64_t sid, const uint32_t **wids,
                                                            size_t *count, void *context);
    struct window *(*find_window)(uint32_t wid, void *context);
    bool (*is_eligible)(struct window *window, void *context);
    const struct declarative_space_postcommit_ops *postcommit_ops;
    void (*refresh_insert_feedback)(void *context);
    void (*postcommit_probe)(bool committed, void *context);
    void *context;
};

static bool declarative_reload_select_system(struct declarative_startup_state *state,
                                             const char *installed_path,
                                             const char *xdg_config_home,
                                             const char *home,
                                             struct declarative_error *error,
                                             void *context)
{
    (void) context;
    return declarative_startup_select(state, installed_path, xdg_config_home, home, error);
}

static bool declarative_reload_capture_topology_system(const struct declarative_topology_bounds_config *bounds,
                                                       struct declarative_topology *topology,
                                                       void *context)
{
    (void) context;
    return declarative_topology_capture_system(bounds, topology);
}

#ifdef TESTS
static struct declarative_reload_ops declarative_reload_ops_from_test(
    const struct declarative_reload_test_ops *test_ops);
#endif

struct declarative_refresh_snapshot_entry
{
    struct application *application;
    pid_t pid;
    ProcessSerialNumber psn;
    bool launch_refresh;
    bool live;
};

struct declarative_reload_transaction
{
    struct declarative_startup_state candidate;
    struct declarative_startup_bundle bundle;
    struct declarative_error diagnostic;
    struct declarative_space_topology_entry *space_topology;
    struct declarative_space_target *targets;
    size_t target_count;
    struct declarative_refresh_snapshot_entry *refresh_snapshot;
    size_t refresh_count;
    int refresh_cursor;
    uint64_t generation;
    struct menu_bar_settings previous_menu_settings;
    bool previous_opacity_enabled;
    enum purify_mode previous_purify_mode;
    float previous_menubar_opacity;
    float previous_active_opacity;
    float previous_normal_opacity;
    struct declarative_installed_state retired;
    struct rule *retired_rule_buffer;
    size_t retired_native_count;
};

#ifdef TESTS
static uint32_t *(*declarative_mode_space_window_list)(uint64_t *, int, int, int *, bool, bool *) =
    space_window_list_for_connection_with_status;
#else
#define declarative_mode_space_window_list space_window_list_for_connection_with_status
#endif

static enum declarative_space_query_result declarative_reload_query_sid_result(
    uint64_t sid, const uint32_t **wids, size_t *count, void *context)
{
    (void) context;
    if (!wids || !count) return DECLARATIVE_SPACE_QUERY_TERMINAL;
    int result_count = 0;
    bool success = false;
    uint32_t *result = declarative_mode_space_window_list(&sid, 1, 0, &result_count, true, &success);
    if (!success || result_count < 0) return DECLARATIVE_SPACE_QUERY_RESOURCE;
    if (result_count && !result) return DECLARATIVE_SPACE_QUERY_TERMINAL;
    *wids = result;
    *count = (size_t) result_count;
    return DECLARATIVE_SPACE_QUERY_OK;
}

static bool declarative_reload_query_sid(uint64_t sid, const uint32_t **wids, size_t *count, void *context)
{
    return declarative_reload_query_sid_result(sid, wids, count, context) == DECLARATIVE_SPACE_QUERY_OK;
}

static struct window *declarative_reload_find_window(uint32_t wid, void *context)
{
    (void) context;
    return window_manager_find_window(&g_window_manager, wid);
}

static bool declarative_reload_window_is_eligible(struct window *window, void *context)
{
    (void) context;
    return window && window_manager_should_manage_window(window);
}

static void declarative_reload_refresh_insert_feedback(void *context)
{
    (void) context;
    insert_feedback_refresh_all();
}

static void declarative_reload_apply_scalars(const struct declarative_startup_bundle *bundle)
{
    const struct window_manager *wm = &bundle->window_manager;
    g_verbose = bundle->verbose;
    g_window_manager.enable_mff = wm->enable_mff;
    g_window_manager.configured_ffm_mode = wm->configured_ffm_mode;
    window_manager_recompute_effective_ffm_mode(&g_window_manager);
    g_window_manager.purify_mode = wm->purify_mode;
    g_window_manager.window_origin_mode = wm->window_origin_mode;
    g_window_manager.window_focus_fallback = wm->window_focus_fallback;
    g_window_manager.enable_window_opacity = wm->enable_window_opacity;
    g_window_manager.menubar_opacity = wm->menubar_opacity;
    g_window_manager.active_window_opacity = wm->active_window_opacity;
    g_window_manager.normal_window_opacity = wm->normal_window_opacity;
    g_window_manager.window_opacity_duration = wm->window_opacity_duration;
    g_window_manager.window_animation_duration = wm->window_animation_duration;
    g_window_manager.window_animation_easing = wm->window_animation_easing;
    g_window_manager.insert_feedback_color = wm->insert_feedback_color;
    g_window_manager.insert_feedback_radius = wm->insert_feedback_radius;
    g_window_manager.insert_feedback_width = wm->insert_feedback_width;

    g_space_manager.layout = bundle->space_manager.layout;
    g_space_manager.top_padding = bundle->space_manager.top_padding;
    g_space_manager.bottom_padding = bundle->space_manager.bottom_padding;
    g_space_manager.left_padding = bundle->space_manager.left_padding;
    g_space_manager.right_padding = bundle->space_manager.right_padding;
    g_space_manager.window_gap = bundle->space_manager.window_gap;
    g_space_manager.accordion_padding = bundle->space_manager.accordion_padding;
    g_space_manager.single_window_max_width = bundle->space_manager.single_window_max_width;
    g_space_manager.single_window_min_width = bundle->space_manager.single_window_min_width;
    g_space_manager.single_window_width_ratio = bundle->space_manager.single_window_width_ratio;
    g_space_manager.split_ratio = bundle->space_manager.split_ratio;
    g_space_manager.split_type = bundle->space_manager.split_type;
    g_space_manager.auto_balance = bundle->space_manager.auto_balance;
    g_space_manager.window_placement = bundle->space_manager.window_placement;
    g_space_manager.window_insertion_point = bundle->space_manager.window_insertion_point;
    g_space_manager.window_zoom_persist = bundle->space_manager.window_zoom_persist;
    g_space_manager.skip_window_focus_animation = bundle->space_manager.skip_window_focus_animation;

    g_display_manager.order = bundle->display_manager.order;
    g_display_manager.mode = bundle->display_manager.mode;
    g_display_manager.top_padding = bundle->display_manager.top_padding;
    g_display_manager.bottom_padding = bundle->display_manager.bottom_padding;
    __atomic_store_n(&g_mouse_state.modifier, bundle->mouse_state.modifier, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mouse_state.action1, bundle->mouse_state.action1, __ATOMIC_RELEASE);
    __atomic_store_n(&g_mouse_state.action2, bundle->mouse_state.action2, __ATOMIC_RELEASE);
    g_mouse_state.drop_action = bundle->mouse_state.drop_action;
    g_mouse_state.native_edge_resize = bundle->mouse_state.native_edge_resize;
    menu_bar_manager_settings_commit_no_post(&bundle->menu_settings);
}

static void declarative_reload_publish(void *context)
{
    native_resize_abort(&g_mouse_state, true);
    struct declarative_reload_transaction *transaction = context;
    transaction->previous_menu_settings = menu_bar_manager_settings();
    transaction->previous_opacity_enabled = g_window_manager.enable_window_opacity;
    transaction->previous_purify_mode = g_window_manager.purify_mode;
    transaction->previous_menubar_opacity = g_window_manager.menubar_opacity;
    transaction->previous_active_opacity = g_window_manager.active_window_opacity;
    transaction->previous_normal_opacity = g_window_manager.normal_window_opacity;
    transaction->retired = declarative_installed;
    memset(&declarative_installed, 0, sizeof(declarative_installed));
    transaction->retired_rule_buffer = g_window_manager.rules;
    transaction->retired_native_count = rule_native_prefix_length();

    declarative_reload_apply_scalars(&transaction->bundle);
    g_window_manager.rules = transaction->bundle.rule_buffer;
    transaction->bundle.rule_buffer = NULL;
    transaction->bundle.rule_buffer_owned_count = 0;

    declarative_installed.generation = transaction->generation;
    declarative_installed.model = transaction->candidate.model;
    declarative_installed.has_scaffold = declarative_policies_have_scaffold(transaction->bundle.composition.spaces,
                                                                             transaction->bundle.composition.space_count);
    transaction->candidate.model = NULL;
    declarative_installed.path = transaction->bundle.path;
    transaction->bundle.path = NULL;
    declarative_installed.selected_mode = transaction->bundle.selected_mode;
    transaction->bundle.selected_mode = NULL;
    declarative_installed.topology = transaction->candidate.topology;
    memset(&transaction->candidate.topology, 0, sizeof(transaction->candidate.topology));
    declarative_installed.fold_context = transaction->bundle.fold_context;
    transaction->bundle.fold_context = NULL;
    declarative_installed.prepared_rules = transaction->bundle.rules;
    memset(&transaction->bundle.rules, 0, sizeof(transaction->bundle.rules));
    declarative_installed.labels = transaction->bundle.labels;
    declarative_installed.label_count = transaction->bundle.label_count;
    transaction->bundle.labels = NULL;
    transaction->bundle.label_count = 0;
    rule_native_generation_install(transaction->generation, declarative_installed.prepared_rules.count,
                                   (const char *const *) declarative_installed.labels,
                                   declarative_installed.label_count,
                                   declarative_installed.fold_context);
}

static void declarative_reload_retire(struct declarative_reload_transaction *transaction)
{
    if (transaction->retired_rule_buffer) {
        for (size_t i = 0; i < transaction->retired_native_count; ++i) rule_destroy(&transaction->retired_rule_buffer[i]);
        free(buf__hdr(transaction->retired_rule_buffer));
        transaction->retired_rule_buffer = NULL;
        transaction->retired_native_count = 0;
    }
    declarative_config_destroy(transaction->retired.model);
    declarative_install_free(transaction->retired.path);
    declarative_install_free(transaction->retired.selected_mode);
    declarative_topology_destroy(&transaction->retired.topology);
    declarative_rule_fold_context_destroy(transaction->retired.fold_context);
    declarative_prepared_rules_destroy(&transaction->retired.prepared_rules);
    if (transaction->retired.labels) {
        for (size_t i = 0; i < transaction->retired.label_count; ++i) declarative_install_free(transaction->retired.labels[i]);
    }
    declarative_install_free(transaction->retired.labels);
    memset(&transaction->retired, 0, sizeof(transaction->retired));
}

static void declarative_reload_transaction_destroy(struct declarative_reload_transaction *transaction)
{
    declarative_topology_space_snapshot_destroy(transaction->space_topology);
    declarative_install_free(transaction->targets);
    declarative_install_free(transaction->refresh_snapshot);
    declarative_startup_bundle_destroy(&transaction->bundle);
    declarative_startup_destroy(&transaction->candidate);
    declarative_reload_retire(transaction);
}

static bool declarative_reload_fail(struct declarative_reload_transaction *transaction, const char *message)
{
    if (transaction->diagnostic.stage == DECLARATIVE_ERROR_NONE) {
        transaction->diagnostic.stage = DECLARATIVE_ERROR_RUNTIME;
        snprintf(transaction->diagnostic.path, sizeof(transaction->diagnostic.path), "%s",
                 transaction->candidate.path[0] ? transaction->candidate.path :
                 declarative_installed.path ? declarative_installed.path : "<none>");
        snprintf(transaction->diagnostic.json_path, sizeof(transaction->diagnostic.json_path), "%s", "$");
        snprintf(transaction->diagnostic.message, sizeof(transaction->diagnostic.message), "%s", message);
    }
    return false;
}

static struct application *declarative_reload_registered_refresh_application(struct application *pointer)
{
    if (!pointer) return NULL;
    struct application *registered = NULL;
    table_for (struct application *candidate, g_window_manager.application, {
        if (candidate == pointer) registered = candidate;
    });
    return registered;
}

static bool declarative_reload_prepare_refresh_plan(struct declarative_reload_transaction *transaction)
{
    int count = buf_len(g_window_manager.applications_to_refresh);
    if (count < 0) return false;
    transaction->refresh_count = (size_t) count;
    transaction->refresh_cursor = g_window_manager.application_refresh_cursor;
    if (!count) return true;
    transaction->refresh_snapshot = declarative_install_calloc((size_t) count,
                                                               sizeof(*transaction->refresh_snapshot));
    if (!transaction->refresh_snapshot) return false;
    int cursor = transaction->refresh_cursor % count;
    if (cursor < 0) cursor += count;
    for (int offset = 0; offset < count; ++offset) {
        struct application *pointer = g_window_manager.applications_to_refresh[(cursor + offset) % count];
        struct application *application = declarative_reload_registered_refresh_application(pointer);
        struct declarative_refresh_snapshot_entry *entry = &transaction->refresh_snapshot[offset];
        entry->application = pointer;
        entry->live = application != NULL;
        if (application) {
            entry->pid = application->pid;
            entry->psn = application->psn;
            entry->launch_refresh = application->launch_refresh;
        }
    }

    size_t native_count = transaction->bundle.rules.count;
    for (size_t i = 0; i < native_count; ++i) {
        transaction->bundle.rule_buffer[i].one_shot_owner_pid = 0;
        transaction->bundle.rule_buffer[i].one_shot_owner_psn_high = 0;
        transaction->bundle.rule_buffer[i].one_shot_owner_psn_low = 0;
        transaction->bundle.rule_buffer[i].one_shot_window_id = 0;
    }
    for (int offset = 0; offset < count; ++offset) {
        struct declarative_refresh_snapshot_entry *entry = &transaction->refresh_snapshot[offset];
        if (!entry->live || !entry->launch_refresh || entry->pid <= 0) continue;
        bool duplicate = false;
        for (int previous = 0; previous < offset; ++previous) {
            if (transaction->refresh_snapshot[previous].application == entry->application) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && !rule_reserve_one_shots_in_buffer(transaction->bundle.rule_buffer,
                                                             native_count, entry->application)) return false;
    }
    return true;
}

static bool declarative_reload_validate_refresh_plan(const struct declarative_reload_transaction *transaction)
{
    int count = buf_len(g_window_manager.applications_to_refresh);
    if (count < 0 || (size_t) count != transaction->refresh_count ||
        g_window_manager.application_refresh_cursor != transaction->refresh_cursor) return false;
    if (!count) return true;
    int cursor = transaction->refresh_cursor % count;
    if (cursor < 0) cursor += count;
    for (int offset = 0; offset < count; ++offset) {
        const struct declarative_refresh_snapshot_entry *entry = &transaction->refresh_snapshot[offset];
        struct application *pointer = g_window_manager.applications_to_refresh[(cursor + offset) % count];
        if (pointer != entry->application) return false;
        struct application *application = declarative_reload_registered_refresh_application(pointer);
        if ((application != NULL) != entry->live) return false;
        if (application && (application->pid != entry->pid ||
            !psn_equals(&application->psn, (ProcessSerialNumber *) &entry->psn) ||
            application->launch_refresh != entry->launch_refresh)) return false;
    }
    return true;
}

static bool declarative_reload_prepare(struct declarative_reload_transaction *transaction,
                                       const struct declarative_reload_ops *ops)
{
    memset(transaction, 0, sizeof(*transaction));
    uint64_t current_generation = declarative_installed.generation;
    if (current_generation == UINT64_MAX) return declarative_reload_fail(transaction, "configuration generation overflow");
    transaction->generation = current_generation ? current_generation + 1 : 1;

    const char *installed_path = current_generation ? declarative_installed.path : NULL;
    if (declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_SELECT) ||
        !ops || !ops->select || !ops->capture_topology || !ops->query_sid ||
        !ops->find_window || !ops->is_eligible) {
        return declarative_reload_fail(transaction, "configuration source preparation failed");
    }
    if (!ops->select(&transaction->candidate, installed_path,
                     getenv("XDG_CONFIG_HOME"), getenv("HOME"), &transaction->diagnostic, ops->context) ||
        !transaction->candidate.present) {
        if (transaction->diagnostic.stage == DECLARATIVE_ERROR_NONE) {
            return declarative_reload_fail(transaction, "configuration file was not found");
        }
        return false;
    }
    struct declarative_modes_error modes_error = {0};
    if (!declarative_modes_compose(transaction->candidate.model, NULL,
                                   &transaction->bundle.composition, &modes_error) ||
        (transaction->candidate.model->active_mode &&
         !(transaction->bundle.selected_mode = declarative_install_strdup(transaction->candidate.model->active_mode)))) {
        transaction->diagnostic = modes_error.detail;
        return declarative_reload_fail(transaction, "mode composition failed");
    }

    if (declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_SETTINGS)) return declarative_reload_fail(transaction, "setting preparation failed");
    struct declarative_prepared_settings settings = {0};
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        const struct declarative_value *value = &transaction->candidate.model->config[i];
        if (value->present && !declarative_prepare_setting(&settings, i, value, NULL)) return declarative_reload_fail(transaction, "setting preparation failed");
    }

    struct declarative_topology_bounds_config bounds = { .external_bar_mode = EXTERNAL_BAR_OFF };
    if (transaction->candidate.model->config[DECLARATIVE_CONFIG_EXTERNAL_BAR].present) {
        struct declarative_external_bar *bar = &transaction->candidate.model->config[DECLARATIVE_CONFIG_EXTERNAL_BAR].external_bar;
        bounds.external_bar_mode = bar->mode;
        bounds.external_bar_top_padding = bar->top_padding;
        bounds.external_bar_bottom_padding = bar->bottom_padding;
    }
    if (declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_TOPOLOGY) ||
        !ops->capture_topology(&bounds, &transaction->candidate.topology, ops->context)) return declarative_reload_fail(transaction, "strict topology capture failed");

    struct declarative_startup_bundle *bundle = &transaction->bundle;
    if (declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_MANAGER) ||
        !declarative_install_window_manager_init(&bundle->window_manager)) return declarative_reload_fail(transaction, "manager preparation failed");
    bundle->window_manager_initialized = true;
    declarative_install_space_defaults(&bundle->space_manager);
    bundle->display_manager.order = DISPLAY_ARRANGEMENT_ORDER_DEFAULT;
    bundle->mouse_state = (struct mouse_state) {
        .modifier = MOUSE_MOD_FN,
        .action1 = MOUSE_MODE_MOVE,
        .action2 = MOUSE_MODE_RESIZE,
        .drop_action = MOUSE_MODE_SWAP,
        .active_button = MOUSE_BUTTON_NONE
    };
    bundle->menu_settings = (struct menu_bar_settings) {0};
    bundle->verbose = false;
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        if (settings.values[i].present) declarative_install_apply_candidate_value(bundle, i, &settings.values[i]);
    }

    struct declarative_rule_topology_snapshot rule_topology = {0};
    struct declarative_rule_display_entry *owned_displays = NULL;
    struct declarative_rule_space_entry *owned_spaces = NULL;
    if (declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_RULE_SNAPSHOT) ||
        !declarative_topology_rule_snapshot(&transaction->candidate.topology, &rule_topology,
                                            &owned_displays, &owned_spaces)) return declarative_reload_fail(transaction, "rule topology preparation failed");
    size_t runtime_start = rule_native_prefix_length();
    size_t current_rule_count = (size_t) buf_len(g_window_manager.rules);
    if (runtime_start > current_rule_count) {
        declarative_topology_rule_snapshot_destroy(owned_displays, owned_spaces);
        return declarative_reload_fail(transaction, "installed rule prefix is inconsistent");
    }
    const char *runtime_labels[current_rule_count - runtime_start + 1];
    size_t runtime_label_count = 0;
    for (size_t i = runtime_start; i < current_rule_count; ++i) {
        if (g_window_manager.rules[i].label) runtime_labels[runtime_label_count++] = g_window_manager.rules[i].label;
    }
    enum declarative_rule_prepare_result rule_result = DECLARATIVE_RULE_PREPARE_ALLOCATION_FAILED;
    if (!declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_RULE_PREPARE)) {
        rule_result = declarative_prepare_rules(transaction->candidate.model->rules,
                                                transaction->candidate.model->rule_count,
                                                &rule_topology, bundle->display_manager.order,
                                                transaction->generation,
                                                runtime_labels, runtime_label_count,
                                                &bundle->rules);
    }
    if (rule_result == DECLARATIVE_RULE_PREPARE_OK &&
        !declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_FOLD_CONTEXT)) {
        bundle->fold_context = declarative_rule_fold_context_create(&rule_topology);
    }
    declarative_topology_rule_snapshot_destroy(owned_displays, owned_spaces);
    if (rule_result != DECLARATIVE_RULE_PREPARE_OK || !bundle->fold_context ||
        declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_LABEL_INDEX) ||
        !declarative_install_prepare_labels(transaction->candidate.model, bundle) ||
        declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_RULE_BUFFER) ||
        !declarative_install_prepare_rule_buffer(bundle) ||
        declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_PATH) ||
        !(bundle->path = declarative_install_strdup(transaction->candidate.path))) {
        return declarative_reload_fail(transaction, "rule generation preparation failed");
    }
    if (!declarative_reload_prepare_refresh_plan(transaction)) {
        return declarative_reload_fail(transaction, "refresh one-shot reservation preparation failed");
    }

    if (declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_SPACE_SNAPSHOT) ||
        !declarative_topology_space_snapshot(&transaction->candidate.topology,
                                             &transaction->space_topology, &transaction->target_count)) return declarative_reload_fail(transaction, "Space topology preparation failed");
    if (declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_TARGETS)) return declarative_reload_fail(transaction, "Space target preparation failed");
    transaction->targets = declarative_install_calloc(transaction->target_count, sizeof(*transaction->targets));
    if (!transaction->targets && transaction->target_count) return declarative_reload_fail(transaction, "Space target allocation failed");
    for (size_t i = 0; i < transaction->target_count; ++i) {
        struct declarative_space_topology_entry *entry = &transaction->space_topology[i];
        struct view *view = table_find(&g_space_manager.view, &entry->sid);
        if (!view) return declarative_reload_fail(transaction, "captured Space has no installed view");
        transaction->targets[i] = (struct declarative_space_target) {
            .view = view, .sid = entry->sid, .index = entry->index,
            .uuid = entry->uuid, .is_user_space = entry->is_user_space,
            .identity = entry->identity
        };
    }
    return true;
}

static pthread_mutex_t declarative_reconcile_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t declarative_reconcile_token_counter;
static uint64_t declarative_reconcile_active_token;
static uint32_t declarative_reconcile_retry_attempt;
static bool declarative_reconcile_retry_waiting;
static char declarative_reconcile_last_diagnostic[160];
/* Startup and application AX discovery can trail SkyLight inventory by several
 * seconds. Keep one bounded reservation alive long enough for those streams to
 * converge instead of turning an ordinary launch race into a user alert. */
#define DECLARATIVE_RECONCILE_MAX_RETRIES 7U

#ifdef TESTS
static uint64_t declarative_reconcile_test_reservation_count;
static bool (*declarative_reconcile_retry_test_hook)(uint64_t delay_ms, void *context);
static void *declarative_reconcile_retry_test_context;
#endif

enum declarative_reconcile_result
{
    DECLARATIVE_RECONCILE_SUCCESS,
    DECLARATIVE_RECONCILE_TRANSIENT_FAILURE,
    DECLARATIVE_RECONCILE_TERMINAL_FAILURE
};

static struct declarative_topology_bounds_config declarative_live_topology_bounds_config(void)
{
    return (struct declarative_topology_bounds_config) {
        .external_bar_mode = g_display_manager.mode,
        .external_bar_top_padding = g_display_manager.top_padding,
        .external_bar_bottom_padding = g_display_manager.bottom_padding
    };
}

static struct declarative_space_defaults declarative_reconcile_view_defaults(void)
{
    struct declarative_space_defaults defaults = declarative_startup_view_defaults(declarative_installed.model);
    defaults.split_ratio = g_space_manager.split_ratio;
    defaults.window_placement = g_space_manager.window_placement;
    defaults.window_insertion_point = g_space_manager.window_insertion_point;
    defaults.window_zoom_persist = g_space_manager.window_zoom_persist;
    defaults.focused_window_id = g_window_manager.focused_window_id;

    /* Gap enablement remains per-view; preparation snapshots each view's live flag. */
    struct view *current = table_find(&g_space_manager.view, &g_space_manager.current_space_id);
    defaults.window_gap_enabled = !current || view_check_flag(current, VIEW_ENABLE_GAP);
    return defaults;
}

struct declarative_reconcile_publication
{
    struct declarative_topology *topology;
    struct rule_fold_context **fold_context;
    struct declarative_topology retired_topology;
    struct rule_fold_context *retired_fold_context;
    bool published;
};

static void declarative_reconcile_publish(void *context)
{
    native_resize_abort(&g_mouse_state, true);
    struct declarative_reconcile_publication *publication = context;
    publication->retired_topology = declarative_installed.topology;
    publication->retired_fold_context = declarative_installed.fold_context;
    declarative_installed.topology = *publication->topology;
    memset(publication->topology, 0, sizeof(*publication->topology));
    declarative_installed.fold_context = *publication->fold_context;
    *publication->fold_context = NULL;
    rule_native_generation_install(declarative_installed.generation, rule_native_prefix_length(),
                                   (const char *const *) declarative_installed.labels,
                                   declarative_installed.label_count,
                                   declarative_installed.fold_context);
    publication->published = true;
}

static bool declarative_mode_topology_matches(const struct declarative_space_topology_entry *installed,
                                               size_t installed_count,
                                               const struct declarative_space_topology_entry *current,
                                               size_t current_count);

static bool declarative_reconcile_has_uuidless_identity_drift(
    const struct declarative_space_target *targets,
    const struct declarative_space_topology_entry *entries, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const struct declarative_space_identity *installed = &targets[i].view->declarative_identity;
        if (declarative_space_identity_valid(installed) && !installed->has_uuid &&
            !declarative_space_identity_equal(installed, &entries[i].identity)) return true;
    }
    return false;
}

static void declarative_reconcile_diagnostic(char *diagnostic, size_t capacity, const char *message)
{
    if (diagnostic && capacity) snprintf(diagnostic, capacity, "%s", message ? message : "reconciliation failure");
}

static enum declarative_reconcile_result declarative_configuration_reconcile_with_ops(
    const struct declarative_reload_ops *ops, char *diagnostic, size_t diagnostic_capacity)
{
    declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity, "reconciliation preparation failed");
    if (!event_loop_is_worker_thread(&g_event_loop) || !declarative_installed.generation ||
        !declarative_installed.model || !ops || !ops->capture_topology ||
        (!ops->query_sid && !ops->query_sid_result) || !ops->find_window || !ops->is_eligible) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity, "reconciliation invariant rejected");
        return DECLARATIVE_RECONCILE_TERMINAL_FAILURE;
    }
#ifdef TESTS
    if (declarative_reload_reconcile_entry_test_hook) declarative_reload_reconcile_entry_test_hook();
#endif

    /* Preserve-runtime reconciliation must observe one coherent effective
     * external-bar snapshot for both topology captures. */
    struct declarative_topology_bounds_config bounds = declarative_live_topology_bounds_config();
    struct declarative_topology topology = {0};
    struct declarative_topology verification_topology = {0};
    struct declarative_rule_topology_snapshot rule_topology = {0};
    struct declarative_rule_display_entry *owned_displays = NULL;
    struct declarative_rule_space_entry *owned_spaces = NULL;
    struct rule_fold_context *fold_context = NULL;
    struct declarative_space_composition composition = {0};
    struct declarative_modes_error modes_error = {0};
    struct declarative_space_topology_entry *entries = NULL;
    struct declarative_space_topology_entry *verification_entries = NULL;
    struct declarative_space_target *targets = NULL;
    struct declarative_reconcile_publication publication = {
        .topology = &topology,
        .fold_context = &fold_context
    };
    size_t count = 0, verification_count = 0;
    bool result = false;
    enum declarative_reconcile_result outcome = DECLARATIVE_RECONCILE_TRANSIENT_FAILURE;
    if (!declarative_modes_compose_selection(declarative_installed.model,
                                             declarative_installed.selected_mode, false,
                                             &composition, &modes_error)) {
        outcome = modes_error.code == DECLARATIVE_MODES_ERROR_RESOURCE
                ? DECLARATIVE_RECONCILE_TRANSIENT_FAILURE
                : DECLARATIVE_RECONCILE_TERMINAL_FAILURE;
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity,
                                         modes_error.detail.message[0]
                                         ? modes_error.detail.message : "installed mode composition failed");
        goto out;
    }
    if (!ops->capture_topology(&bounds, &topology, ops->context)) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity, "strict topology capture failed");
        goto out;
    }
    if (!declarative_topology_rule_snapshot(&topology, &rule_topology, &owned_displays, &owned_spaces)) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity, "rule topology snapshot failed");
        goto out;
    }
    if (!(fold_context = declarative_rule_fold_context_create(&rule_topology))) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity, "rule fold context preparation failed");
        goto out;
    }
    if (!declarative_topology_space_snapshot(&topology, &entries, &count)) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity, "Space topology snapshot failed");
        goto out;
    }
    declarative_topology_rule_snapshot_destroy(owned_displays, owned_spaces);
    owned_displays = NULL;
    owned_spaces = NULL;
    targets = declarative_install_calloc(count, sizeof(*targets));
    if (!targets && count) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity, "Space target allocation failed");
        goto out;
    }
    for (size_t i = 0; i < count; ++i) {
        struct view *view = table_find(&g_space_manager.view, &entries[i].sid);
        if (!view) {
            declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity,
                                             "topology settling: captured Space has no installed view");
            goto out;
        }
        targets[i] = (struct declarative_space_target) {
            .view = view, .sid = entries[i].sid, .index = entries[i].index,
            .uuid = entries[i].uuid, .is_user_space = entries[i].is_user_space,
            .identity = entries[i].identity
        };
    }
    struct declarative_space_defaults defaults = declarative_reconcile_view_defaults();
    struct declarative_space_table_contract view_contract = {
        .hash = g_space_manager.view.hash, .cmp = g_space_manager.view.cmp, .key_size = sizeof(uint64_t)
    };
    struct declarative_space_table_contract managed_contract = {
        .hash = g_window_manager.managed_window.hash, .cmp = g_window_manager.managed_window.cmp, .key_size = sizeof(uint32_t)
    };
    struct declarative_space_capture_ops capture = {
        .query_sid = ops->query_sid,
        .query_sid_result = ops->query_sid_result,
        .find_window = ops->find_window,
        .is_eligible = ops->is_eligible,
        .context = ops->context,
        .allow_unresolved_windows = true
    };
    /* Defaults, targets, contracts, and capture inputs are now fixed. Re-observe
     * immediately before inventory so index policies can never cross a settling turn. */
    if (!ops->capture_topology(&bounds, &verification_topology, ops->context) ||
        !declarative_topology_space_snapshot(&verification_topology,
                                             &verification_entries, &verification_count)) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity,
                                         "topology verification capture failed");
        goto out;
    }
    if (topology.fingerprint != verification_topology.fingerprint ||
        topology.active_did != verification_topology.active_did ||
        topology.active_sid != verification_topology.active_sid ||
        !declarative_mode_topology_matches(entries, count, verification_entries, verification_count)) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity,
                                         "topology changed during reconciliation");
        goto out;
    }
    if (declarative_reconcile_has_uuidless_identity_drift(targets, entries, count)) {
        /* The current topology is safe to publish for native-rule validation,
         * but the old Space forest cannot be transferred across fallback identities. */
        declarative_reconcile_publish(&publication);
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity,
                                         "UUID-less Space identity changed; stale rule targets suppressed");
        goto out;
    }
    enum declarative_space_apply_result space_result =
        declarative_space_apply_with_effective_publication_result(
            targets, count, entries, count, &g_space_manager.view,
            &view_contract, &managed_contract, declarative_installed.model,
            composition.spaces, composition.space_count,
            declarative_installed.generation, &g_declarative_runtime_state, &defaults,
            DECLARATIVE_SPACE_PRESERVE_RUNTIME, &capture,
            ops->postcommit_ops, NULL, declarative_reconcile_publish, &publication);
    result = space_result == DECLARATIVE_SPACE_APPLY_OK;
    if (space_result == DECLARATIVE_SPACE_APPLY_TRANSIENT) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity,
                                         "window inventory is still settling");
    } else if (space_result == DECLARATIVE_SPACE_APPLY_RESOURCE) {
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity,
                                         "Space reconciliation resource is temporarily unavailable");
    } else if (space_result == DECLARATIVE_SPACE_APPLY_TERMINAL) {
        /* A strict preparation guard can reject a snapshot that became stale
         * after capture; retry the whole transaction from fresh topology. */
        declarative_reconcile_diagnostic(diagnostic, diagnostic_capacity,
                                         "Space preparation snapshot was rejected");
    }
out:
    declarative_modes_composition_destroy(&composition);
    declarative_topology_rule_snapshot_destroy(owned_displays, owned_spaces);
    declarative_install_free(targets);
    declarative_topology_space_snapshot_destroy(entries);
    declarative_topology_space_snapshot_destroy(verification_entries);
    /* Publication either accompanied a successful Space commit or deliberately
     * advanced only topology/rule validation after UUID-less identity drift. */
    declarative_rule_fold_context_destroy(fold_context);
    declarative_topology_destroy(&topology);
    declarative_topology_destroy(&verification_topology);
    if (publication.published) {
        declarative_rule_fold_context_destroy(publication.retired_fold_context);
        declarative_topology_destroy(&publication.retired_topology);
    }
    if (result) outcome = DECLARATIVE_RECONCILE_SUCCESS;
    return outcome;
}

static struct declarative_reload_ops declarative_reconcile_ops(void)
{
#ifdef TESTS
    if (declarative_reload_message_test_ops) {
        return declarative_reload_ops_from_test(declarative_reload_message_test_ops);
    }
#endif
    return (struct declarative_reload_ops) {
        .capture_topology = declarative_reload_capture_topology_system,
        .query_sid = declarative_reload_query_sid,
        .query_sid_result = declarative_reload_query_sid_result,
        .find_window = declarative_reload_find_window,
        .is_eligible = declarative_reload_window_is_eligible
    };
}

void declarative_configuration_reconcile_cancel(uint64_t reservation_token)
{
    if (!reservation_token) return;
    pthread_mutex_lock(&declarative_reconcile_lock);
    if (declarative_reconcile_active_token == reservation_token) {
        declarative_reconcile_active_token = 0;
        declarative_reconcile_retry_attempt = 0;
        declarative_reconcile_retry_waiting = false;
        declarative_reconcile_last_diagnostic[0] = '\0';
    }
    pthread_mutex_unlock(&declarative_reconcile_lock);
}

void declarative_configuration_reconcile_shutdown(void)
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    declarative_reconcile_active_token = 0;
    declarative_reconcile_retry_attempt = 0;
    declarative_reconcile_retry_waiting = false;
    declarative_reconcile_last_diagnostic[0] = '\0';
    /* Ensure every post-restart reservation differs from delayed pre-shutdown work. */
    if (++declarative_reconcile_token_counter == 0) ++declarative_reconcile_token_counter;
    pthread_mutex_unlock(&declarative_reconcile_lock);
}

static bool declarative_reconcile_retry_ready(uint64_t reservation_token, uint64_t event_loop_generation)
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    bool owned = reservation_token && declarative_reconcile_active_token == reservation_token &&
                 declarative_reconcile_retry_waiting;
    if (owned) declarative_reconcile_retry_waiting = false;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    if (!owned) return false;
    return event_loop_try_post_generation(&g_event_loop, event_loop_generation,
                                          CONFIGURATION_RECONCILE,
                                          (void *) (uintptr_t) reservation_token, 0);
}

static bool declarative_reconcile_schedule_retry(uint64_t reservation_token, uint64_t delay_ms)
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    bool owned = reservation_token && declarative_reconcile_active_token == reservation_token;
    if (owned) declarative_reconcile_retry_waiting = true;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    if (!owned) return false;
#ifdef TESTS
    if (declarative_reconcile_retry_test_hook) {
        if (declarative_reconcile_retry_test_hook(delay_ms, declarative_reconcile_retry_test_context)) return true;
        pthread_mutex_lock(&declarative_reconcile_lock);
        if (declarative_reconcile_active_token == reservation_token) declarative_reconcile_retry_waiting = false;
        pthread_mutex_unlock(&declarative_reconcile_lock);
        return false;
    }
#endif
    uint64_t event_loop_generation = g_event_loop.generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ms * NSEC_PER_MSEC),
                   dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        (void) declarative_reconcile_retry_ready(reservation_token, event_loop_generation);
    });
    return true;
}

static void declarative_reconcile_report_with_cause(const char *prefix, const char *cause)
{
    char message[384];
    snprintf(message, sizeof(message), "%s: %s", prefix,
             cause && cause[0] ? cause : "reconciliation preparation failed");
    declarative_report_runtime(NULL, "reconciliation", declarative_installed.path, "$", 0, 0, message);
}

bool declarative_configuration_reconcile(void)
{
    struct declarative_reload_ops ops = declarative_reconcile_ops();
    char diagnostic[160] = {0};
    return declarative_configuration_reconcile_with_ops(&ops, diagnostic, sizeof(diagnostic)) ==
           DECLARATIVE_RECONCILE_SUCCESS;
}

bool declarative_configuration_reconcile_event(uint64_t reservation_token)
{
    if (!event_loop_is_worker_thread(&g_event_loop) || !declarative_installed.generation ||
        !declarative_installed.model || !reservation_token) return false;
    pthread_mutex_lock(&declarative_reconcile_lock);
    bool owned = declarative_reconcile_active_token == reservation_token;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    if (!owned) return false;

    struct declarative_reload_ops ops = declarative_reconcile_ops();
    if (!ops.capture_topology || (!ops.query_sid && !ops.query_sid_result) ||
        !ops.find_window || !ops.is_eligible) {
        declarative_reconcile_report_with_cause("terminal reconciliation failure",
                                                "reconciliation operation invariant rejected");
        declarative_configuration_reconcile_cancel(reservation_token);
        return false;
    }
    char diagnostic[160] = {0};
    enum declarative_reconcile_result outcome =
        declarative_configuration_reconcile_with_ops(&ops, diagnostic, sizeof(diagnostic));
    if (outcome == DECLARATIVE_RECONCILE_SUCCESS) {
        declarative_configuration_reconcile_cancel(reservation_token);
        return true;
    }
    if (outcome == DECLARATIVE_RECONCILE_TERMINAL_FAILURE) {
        declarative_reconcile_report_with_cause("terminal reconciliation failure", diagnostic);
        declarative_configuration_reconcile_cancel(reservation_token);
        return false;
    }

    pthread_mutex_lock(&declarative_reconcile_lock);
    if (declarative_reconcile_active_token != reservation_token) {
        pthread_mutex_unlock(&declarative_reconcile_lock);
        return false;
    }
    snprintf(declarative_reconcile_last_diagnostic, sizeof(declarative_reconcile_last_diagnostic),
             "%s", diagnostic[0] ? diagnostic : "reconciliation preparation failed");
    uint32_t attempt = ++declarative_reconcile_retry_attempt;
    char cause[sizeof(declarative_reconcile_last_diagnostic)];
    snprintf(cause, sizeof(cause), "%s", declarative_reconcile_last_diagnostic);
    pthread_mutex_unlock(&declarative_reconcile_lock);

    if (attempt > DECLARATIVE_RECONCILE_MAX_RETRIES) {
        declarative_reconcile_report_with_cause("reconciliation retry/backoff exhausted", cause);
        declarative_configuration_reconcile_cancel(reservation_token);
        return false;
    }
    uint64_t delay_ms = UINT64_C(50) << (attempt - 1);
    if (!declarative_reconcile_schedule_retry(reservation_token, delay_ms)) {
        declarative_reconcile_report_with_cause("reconciliation retry/backoff scheduling rejected", cause);
        declarative_configuration_reconcile_cancel(reservation_token);
        return false;
    }
    return false;
}

void declarative_configuration_request_reconcile(void)
{
    if (!declarative_installed.generation) return;
    pthread_mutex_lock(&declarative_reconcile_lock);
    if (declarative_reconcile_active_token) {
        pthread_mutex_unlock(&declarative_reconcile_lock);
        return;
    }
    uint64_t reservation_token = ++declarative_reconcile_token_counter;
    if (!reservation_token) reservation_token = ++declarative_reconcile_token_counter;
    declarative_reconcile_active_token = reservation_token;
#ifdef TESTS
    ++declarative_reconcile_test_reservation_count;
#endif
    declarative_reconcile_retry_attempt = 0;
    declarative_reconcile_retry_waiting = false;
    declarative_reconcile_last_diagnostic[0] = '\0';
    pthread_mutex_unlock(&declarative_reconcile_lock);
    (void) event_loop_try_post(&g_event_loop, CONFIGURATION_RECONCILE,
                               (void *) (uintptr_t) reservation_token, 0);
}

static bool declarative_config_reload_with_ops(FILE *response,
                                               const struct declarative_reload_ops *ops)
{
    if (!event_loop_is_worker_thread(&g_event_loop)) {
        declarative_report_runtime_client(response, "runtime", declarative_installed.path, "$", 0, 0,
                                          "reload must run on the event-loop worker");
        return false;
    }
    struct declarative_reload_transaction transaction;
    if (!declarative_reload_prepare(&transaction, ops)) {
        declarative_report_error_client(response, &transaction.diagnostic);
        declarative_reload_transaction_destroy(&transaction);
        return false;
    }

    struct declarative_space_defaults defaults = declarative_startup_view_defaults(transaction.candidate.model);
    defaults.focused_window_id = g_window_manager.focused_window_id;
    struct declarative_space_table_contract view_contract = {
        .hash = g_space_manager.view.hash, .cmp = g_space_manager.view.cmp, .key_size = sizeof(uint64_t)
    };
    struct declarative_space_table_contract managed_contract = {
        .hash = g_window_manager.managed_window.hash, .cmp = g_window_manager.managed_window.cmp, .key_size = sizeof(uint32_t)
    };
    struct declarative_space_capture_ops capture = {
        .query_sid = ops->query_sid,
        .query_sid_result = ops->query_sid_result,
        .find_window = ops->find_window,
        .is_eligible = ops->is_eligible,
        .context = ops->context,
        .allow_unresolved_windows = true
    };
    bool result = false;
#ifdef TESTS
    if (declarative_reload_prepublication_test_hook) declarative_reload_prepublication_test_hook();
#endif
    bool refresh_plan_valid = declarative_reload_validate_refresh_plan(&transaction);
    if (!refresh_plan_valid) {
        declarative_report_runtime_client(response, "runtime", transaction.candidate.path, "$", 0, 0,
                                          "refresh queue changed during reload preparation");
    } else if (!declarative_reload_stage_failed(DECLARATIVE_RELOAD_TEST_STAGE_FOREST)) {
        result = declarative_space_apply_with_effective_publication(transaction.targets, transaction.target_count,
                                                           transaction.space_topology, transaction.target_count,
                                                           &g_space_manager.view, &view_contract, &managed_contract,
                                                           transaction.candidate.model,
                                                           transaction.bundle.composition.spaces,
                                                           transaction.bundle.composition.space_count,
                                                           transaction.generation,
                                                           &g_declarative_runtime_state,
                                                           &defaults, DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD,
                                                           &capture, ops->postcommit_ops, NULL,
                                                           declarative_reload_publish, &transaction);
    }
    if (!result && refresh_plan_valid) {
        declarative_report_runtime_client(response, "runtime", transaction.candidate.path, "$", 0, 0,
                                          "forest and managed-window preparation failed");
    } else if (result) {
        declarative_reload_retire(&transaction);
        menu_bar_manager_settings_postcommit(&transaction.previous_menu_settings);
        if (transaction.previous_opacity_enabled != g_window_manager.enable_window_opacity) {
            window_manager_set_window_opacity_enabled(&g_window_manager, g_window_manager.enable_window_opacity);
        }
        if (transaction.previous_purify_mode != g_window_manager.purify_mode) {
            window_manager_set_purify_mode(&g_window_manager, g_window_manager.purify_mode);
        }
        if (transaction.previous_menubar_opacity != g_window_manager.menubar_opacity) {
            window_manager_set_menubar_opacity(&g_window_manager, g_window_manager.menubar_opacity);
        }
        if (transaction.previous_active_opacity != g_window_manager.active_window_opacity) {
            window_manager_set_active_window_opacity(&g_window_manager, g_window_manager.active_window_opacity);
        }
        if (transaction.previous_normal_opacity != g_window_manager.normal_window_opacity) {
            window_manager_set_normal_window_opacity(&g_window_manager, g_window_manager.normal_window_opacity);
        }
        if (ops->refresh_insert_feedback) ops->refresh_insert_feedback(ops->context);
        menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_CONFIG | MENU_BAR_DIRTY_TOPOLOGY);
    }
    if (ops->postcommit_probe) ops->postcommit_probe(result, ops->context);
    declarative_reload_transaction_destroy(&transaction);
    return result;
}

bool declarative_config_reload(FILE *response)
{
#ifdef TESTS
    if (declarative_reload_message_test_ops) {
        return declarative_config_reload_with_test_ops(response, declarative_reload_message_test_ops);
    }
#endif
    const struct declarative_reload_ops ops = {
        .select = declarative_reload_select_system,
        .capture_topology = declarative_reload_capture_topology_system,
        .query_sid = declarative_reload_query_sid,
        .find_window = declarative_reload_find_window,
        .is_eligible = declarative_reload_window_is_eligible,
        .refresh_insert_feedback = declarative_reload_refresh_insert_feedback
    };
    return declarative_config_reload_with_ops(response, &ops);
}

struct declarative_mode_publication
{
    char *selected_mode;
    char *retired_selected_mode;
    bool has_scaffold;
};

static void declarative_mode_publish(void *context)
{
    native_resize_abort(&g_mouse_state, true);
    struct declarative_mode_publication *publication = context;
    publication->retired_selected_mode = declarative_installed.selected_mode;
    declarative_installed.selected_mode = publication->selected_mode;
    publication->selected_mode = NULL;
    declarative_installed.has_scaffold = publication->has_scaffold;
}

static void declarative_mode_runtime_error(struct declarative_modes_error *error,
                                           enum declarative_modes_error_code code,
                                           const char *message)
{
    if (!error) return;
    memset(error, 0, sizeof(*error));
    error->code = code;
    error->detail.stage = DECLARATIVE_ERROR_RUNTIME;
    snprintf(error->detail.path, sizeof(error->detail.path), "%s",
             declarative_installed.path ? declarative_installed.path : "<none>");
    snprintf(error->detail.json_path, sizeof(error->detail.json_path), "%s", "$.selected_mode");
    snprintf(error->detail.message, sizeof(error->detail.message), "%s", message);
}

static bool declarative_mode_topology_matches(const struct declarative_space_topology_entry *installed,
                                               size_t installed_count,
                                               const struct declarative_space_topology_entry *current,
                                               size_t current_count)
{
    if (installed_count != current_count) return false;
    for (size_t i = 0; i < current_count; ++i) {
        const struct declarative_space_topology_entry *match = NULL;
        for (size_t j = 0; j < installed_count; ++j) {
            if (installed[j].sid == current[i].sid) { match = &installed[j]; break; }
        }
        if (!match || match->index != current[i].index ||
            match->is_user_space != current[i].is_user_space ||
            strcmp(match->uuid, current[i].uuid) != 0 ||
            !declarative_space_identity_equal(&match->identity, &current[i].identity)) return false;
    }
    return true;
}

static enum declarative_topology_result declarative_mode_capture_topology(
    const struct declarative_topology_bounds_config *bounds, struct declarative_topology *topology)
{
#ifdef TESTS
    if (declarative_reload_message_test_ops) {
        if (declarative_reload_message_test_ops->capture_topology_result) {
            return (enum declarative_topology_result)
                declarative_reload_message_test_ops->capture_topology_result(
                    bounds, topology, declarative_reload_message_test_ops->context);
        }
        return declarative_reload_message_test_ops->capture_topology &&
               declarative_reload_message_test_ops->capture_topology(
                   bounds, topology, declarative_reload_message_test_ops->context)
             ? DECLARATIVE_TOPOLOGY_OK : DECLARATIVE_TOPOLOGY_SETTLING;
    }
#endif
    return declarative_topology_capture_system_result(bounds, topology);
}

static bool declarative_mode_select(const char *name, bool active,
                                    struct declarative_modes_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
    if (!event_loop_is_worker_thread(&g_event_loop) || !declarative_installed.generation ||
        !declarative_installed.model) {
        declarative_mode_runtime_error(error, DECLARATIVE_MODES_ERROR_UNAVAILABLE,
                                       "mode activation is unavailable");
        return false;
    }
    if (active && (!name || !name[0])) {
        declarative_mode_runtime_error(error, DECLARATIVE_MODES_ERROR_TERMINAL,
                                       "mode name invariant rejected");
        return false;
    }

    struct declarative_space_composition composition = {0};
    struct declarative_topology current_topology = {0};
    struct declarative_topology verification_topology = {0};
    struct declarative_space_topology_entry *installed_entries = NULL;
    struct declarative_space_topology_entry *entries = NULL;
    struct declarative_space_topology_entry *verification_entries = NULL;
    struct declarative_space_target *targets = NULL;
    struct declarative_mode_publication publication = {0};
    size_t installed_count = 0, count = 0, verification_count = 0;
    bool result = false;
    if (!declarative_modes_compose_selection(declarative_installed.model, active ? name : NULL,
                                             false, &composition, error)) goto out;
    if (active && !(publication.selected_mode = declarative_install_strdup(name))) {
        declarative_mode_runtime_error(error, DECLARATIVE_MODES_ERROR_RESOURCE,
                                       "mode selection allocation failed");
        goto out;
    }
    publication.has_scaffold = declarative_policies_have_scaffold(composition.spaces,
                                                                   composition.space_count);
    /* Mode changes preserve runtime ownership, so both topology observations
     * use the same effective live external-bar snapshot. */
    struct declarative_topology_bounds_config bounds = declarative_live_topology_bounds_config();
    enum declarative_topology_result topology_result =
        declarative_mode_capture_topology(&bounds, &current_topology);
    if (topology_result == DECLARATIVE_TOPOLOGY_OK) {
        topology_result = declarative_topology_space_snapshot_result(
            &declarative_installed.topology, &installed_entries, &installed_count);
    }
    if (topology_result == DECLARATIVE_TOPOLOGY_OK) {
        topology_result = declarative_topology_space_snapshot_result(
            &current_topology, &entries, &count);
    }
    if (topology_result != DECLARATIVE_TOPOLOGY_OK) {
        enum declarative_modes_error_code code = topology_result == DECLARATIVE_TOPOLOGY_RESOURCE
                                               ? DECLARATIVE_MODES_ERROR_RESOURCE
                                               : topology_result == DECLARATIVE_TOPOLOGY_SETTLING
                                               ? DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED
                                               : DECLARATIVE_MODES_ERROR_TERMINAL;
        if (code == DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED) {
            declarative_configuration_request_reconcile();
        }
        declarative_mode_runtime_error(error, code, "strict current topology capture failed");
        goto out;
    }
    if (!declarative_mode_topology_matches(installed_entries, installed_count, entries, count)) {
        declarative_configuration_request_reconcile();
        declarative_mode_runtime_error(error, DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED,
                                       "Space topology changed; reconciliation is required before retry");
        goto out;
    }
    targets = declarative_install_calloc(count, sizeof(*targets));
    if (!targets && count) {
        declarative_mode_runtime_error(error, DECLARATIVE_MODES_ERROR_RESOURCE,
                                       "Space target allocation failed");
        goto out;
    }
    for (size_t i = 0; i < count; ++i) {
        struct view *view = table_find(&g_space_manager.view, &entries[i].sid);
        if (!view) {
            declarative_configuration_request_reconcile();
            declarative_mode_runtime_error(error, DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED,
                                           "installed topology no longer matches Space views");
            goto out;
        }
        targets[i] = (struct declarative_space_target) {
            .view = view, .sid = entries[i].sid, .index = entries[i].index,
            .uuid = entries[i].uuid, .is_user_space = entries[i].is_user_space,
            .identity = entries[i].identity
        };
    }
    struct declarative_space_defaults defaults = declarative_reconcile_view_defaults();
    struct declarative_space_table_contract view_contract = {
        .hash = g_space_manager.view.hash, .cmp = g_space_manager.view.cmp, .key_size = sizeof(uint64_t)
    };
    struct declarative_space_table_contract managed_contract = {
        .hash = g_window_manager.managed_window.hash, .cmp = g_window_manager.managed_window.cmp,
        .key_size = sizeof(uint32_t)
    };
    struct declarative_space_capture_ops capture = {
        .query_sid = declarative_reload_query_sid,
        .query_sid_result = declarative_reload_query_sid_result,
        .find_window = declarative_reload_find_window,
        .is_eligible = declarative_reload_window_is_eligible,
        .allow_unresolved_windows = true
    };
    const struct declarative_space_postcommit_ops *postcommit_ops = NULL;
#ifdef TESTS
    if (declarative_reload_message_test_ops) {
        capture.query_sid = declarative_reload_message_test_ops->query_sid;
        capture.query_sid_result = (enum declarative_space_query_result (*)(uint64_t, const uint32_t **, size_t *, void *))
            declarative_reload_message_test_ops->query_sid_result;
        capture.find_window = declarative_reload_message_test_ops->find_window;
        capture.is_eligible = declarative_reload_message_test_ops->is_eligible;
        capture.context = declarative_reload_message_test_ops->context;
        postcommit_ops = declarative_reload_message_test_ops->postcommit_ops;
    }
#endif
    topology_result = declarative_mode_capture_topology(&bounds, &verification_topology);
    if (topology_result == DECLARATIVE_TOPOLOGY_OK) {
        topology_result = declarative_topology_space_snapshot_result(
            &verification_topology, &verification_entries, &verification_count);
    }
    if (topology_result != DECLARATIVE_TOPOLOGY_OK) {
        enum declarative_modes_error_code code = topology_result == DECLARATIVE_TOPOLOGY_RESOURCE
                                               ? DECLARATIVE_MODES_ERROR_RESOURCE
                                               : topology_result == DECLARATIVE_TOPOLOGY_SETTLING
                                               ? DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED
                                               : DECLARATIVE_MODES_ERROR_TERMINAL;
        if (code == DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED) {
            declarative_configuration_request_reconcile();
        }
        declarative_mode_runtime_error(error, code, "mode topology verification capture failed");
        goto out;
    }
    if (current_topology.fingerprint != verification_topology.fingerprint ||
        current_topology.active_did != verification_topology.active_did ||
        current_topology.active_sid != verification_topology.active_sid ||
        !declarative_mode_topology_matches(entries, count, verification_entries, verification_count)) {
        declarative_configuration_request_reconcile();
        declarative_mode_runtime_error(error, DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED,
                                       "mode topology changed during activation; reconciliation is required before retry");
        goto out;
    }
    enum declarative_space_apply_result space_result =
        declarative_space_apply_with_effective_publication_result(
            targets, count, entries, count, &g_space_manager.view, &view_contract, &managed_contract,
            declarative_installed.model, composition.spaces, composition.space_count,
            declarative_installed.generation, &g_declarative_runtime_state, &defaults,
            DECLARATIVE_SPACE_PRESERVE_RUNTIME, &capture, postcommit_ops, NULL,
            declarative_mode_publish, &publication);
    result = space_result == DECLARATIVE_SPACE_APPLY_OK;
    if (!result) {
        enum declarative_modes_error_code code =
            space_result == DECLARATIVE_SPACE_APPLY_TRANSIENT
                ? DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED
                : space_result == DECLARATIVE_SPACE_APPLY_RESOURCE
                ? DECLARATIVE_MODES_ERROR_RESOURCE : DECLARATIVE_MODES_ERROR_TERMINAL;
        if (code == DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED) {
            declarative_configuration_request_reconcile();
        }
        declarative_mode_runtime_error(error, code,
            code == DECLARATIVE_MODES_ERROR_RECONCILIATION_REQUIRED
                ? "window inventory is still settling; reconciliation is required before retry"
                : "mode Space transaction failed");
    } else {
        declarative_configuration_request_reconcile();
    }
out:
    declarative_install_free(publication.selected_mode);
    declarative_install_free(publication.retired_selected_mode);
    declarative_install_free(targets);
    declarative_topology_space_snapshot_destroy(installed_entries);
    declarative_topology_space_snapshot_destroy(entries);
    declarative_topology_space_snapshot_destroy(verification_entries);
    declarative_topology_destroy(&current_topology);
    declarative_topology_destroy(&verification_topology);
    declarative_modes_composition_destroy(&composition);
    return result;
}

bool declarative_mode_activate(const char *name, struct declarative_modes_error *error)
{
    return declarative_mode_select(name, true, error);
}

bool declarative_mode_deactivate(struct declarative_modes_error *error)
{
    return declarative_mode_select(NULL, false, error);
}

void declarative_mode_snapshot_destroy(struct declarative_mode_snapshot *snapshot)
{
    if (!snapshot) return;
    for (size_t i = 0; i < snapshot->count; ++i) declarative_install_free(snapshot->names[i]);
    declarative_install_free(snapshot->names);
    declarative_install_free(snapshot->active_name);
    memset(snapshot, 0, sizeof(*snapshot));
}

bool declarative_mode_snapshot_create(struct declarative_mode_snapshot *snapshot)
{
    if (!snapshot || snapshot->names || snapshot->count || snapshot->active_name ||
        !event_loop_is_worker_thread(&g_event_loop)) return false;
    const struct declarative_config_model *model = declarative_installed.model;
    if (!model) return true;
    snapshot->names = declarative_install_calloc(model->mode_count, sizeof(*snapshot->names));
    if (!snapshot->names && model->mode_count) return false;
    for (; snapshot->count < model->mode_count; ++snapshot->count) {
        snapshot->names[snapshot->count] = declarative_install_strdup(model->modes[snapshot->count].name);
        if (!snapshot->names[snapshot->count]) goto fail;
    }
    if (declarative_installed.selected_mode) {
        snapshot->active_name = declarative_install_strdup(declarative_installed.selected_mode);
        if (!snapshot->active_name) goto fail;
    }
    return true;
fail:
    declarative_mode_snapshot_destroy(snapshot);
    return false;
}

uint64_t declarative_installed_generation(void) { return declarative_installed.generation; }
const struct declarative_config_model *declarative_installed_model(void) { return declarative_installed.model; }
const char *declarative_installed_path(void) { return declarative_installed.path; }
bool declarative_installed_has_scaffold(void) { return declarative_installed.has_scaffold; }
const char *declarative_installed_selected_mode(void) { return declarative_installed.selected_mode; }

#ifdef TESTS
static struct declarative_reload_ops declarative_reload_ops_from_test(
    const struct declarative_reload_test_ops *test_ops)
{
    return (struct declarative_reload_ops) {
        .select = test_ops->select,
        .capture_topology = test_ops->capture_topology,
        .query_sid = test_ops->query_sid,
        .query_sid_result = (enum declarative_space_query_result (*)(uint64_t, const uint32_t **, size_t *, void *))
            test_ops->query_sid_result,
        .find_window = test_ops->find_window,
        .is_eligible = test_ops->is_eligible,
        .postcommit_ops = test_ops->postcommit_ops,
        .refresh_insert_feedback = test_ops->refresh_insert_feedback,
        .postcommit_probe = test_ops->postcommit_probe,
        .context = test_ops->context
    };
}

bool declarative_config_reload_with_test_ops(FILE *response,
                                             const struct declarative_reload_test_ops *test_ops)
{
    if (!test_ops) return false;
    struct declarative_reload_ops ops = declarative_reload_ops_from_test(test_ops);
    return declarative_config_reload_with_ops(response, &ops);
}

bool declarative_configuration_reconcile_with_test_ops(const struct declarative_reload_test_ops *test_ops)
{
    if (!test_ops) return false;
    struct declarative_reload_ops ops = declarative_reload_ops_from_test(test_ops);
    char diagnostic[160] = {0};
    return declarative_configuration_reconcile_with_ops(&ops, diagnostic, sizeof(diagnostic)) ==
           DECLARATIVE_RECONCILE_SUCCESS;
}

uint32_t declarative_reload_test_reconcile_reservation(void)
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    uint32_t reserved = declarative_reconcile_active_token != 0;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    return reserved;
}

uint64_t declarative_reload_test_reconcile_token(void)
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    uint64_t token = declarative_reconcile_active_token;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    return token;
}

void declarative_reload_test_clear_reconcile_reservation(void)
{
    uint64_t token = declarative_reload_test_reconcile_token();
    declarative_configuration_reconcile_cancel(token);
}

uint32_t declarative_reload_test_reconcile_retry_attempt(void)
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    uint32_t attempt = declarative_reconcile_retry_attempt;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    return attempt;
}

bool declarative_reload_test_reconcile_retry_waiting(void)
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    bool waiting = declarative_reconcile_retry_waiting;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    return waiting;
}

void declarative_reload_test_reconcile_diagnostic(char *output, size_t capacity)
{
    if (!output || !capacity) return;
    pthread_mutex_lock(&declarative_reconcile_lock);
    snprintf(output, capacity, "%s", declarative_reconcile_last_diagnostic);
    pthread_mutex_unlock(&declarative_reconcile_lock);
}

bool declarative_reload_test_fire_reconcile_retry(void)
{
    pthread_mutex_lock(&declarative_reconcile_lock);
    uint64_t token = declarative_reconcile_active_token;
    pthread_mutex_unlock(&declarative_reconcile_lock);
    return declarative_reconcile_retry_ready(token, g_event_loop.generation);
}

void declarative_reload_test_set_reconcile_retry_hook(bool (*hook)(uint64_t, void *), void *context)
{
    declarative_reconcile_retry_test_hook = hook;
    declarative_reconcile_retry_test_context = context;
}

void declarative_install_test_set_generation(uint64_t generation)
{
    declarative_installed.generation = generation;
}

void declarative_install_test_reset(void)
{
    if (declarative_installed.generation) {
        for (int i = 0; i < buf_len(g_window_manager.rules); ++i) rule_destroy(&g_window_manager.rules[i]);
        free(g_window_manager.rules ? buf__hdr(g_window_manager.rules) : NULL);
        g_window_manager.rules = NULL;
        table_for (struct view *view, g_space_manager.view, {
            view_destroy(view);
            legacy_allocation_free(view);
        });
        table_free(&g_space_manager.view);
        memset(&g_space_manager.view, 0, sizeof(g_space_manager.view));
        declarative_install_mutex_destroy(&g_window_manager.window_animations_lock);
        declarative_install_destroy_window_manager(&g_window_manager);
    }
    rule_native_generation_clear();
    declarative_config_destroy(declarative_installed.model);
    declarative_install_free(declarative_installed.path);
    declarative_install_free(declarative_installed.selected_mode);
    declarative_topology_destroy(&declarative_installed.topology);
    declarative_rule_fold_context_destroy(declarative_installed.fold_context);
    declarative_prepared_rules_destroy(&declarative_installed.prepared_rules);
    if (declarative_installed.labels) {
        for (size_t i = 0; i < declarative_installed.label_count; ++i) declarative_install_free(declarative_installed.labels[i]);
    }
    declarative_install_free(declarative_installed.labels);
    memset(&declarative_installed, 0, sizeof(declarative_installed));
    declarative_install_allocation_ordinal = 0;
    declarative_install_fail_ordinal = 0;
    declarative_reload_fail_stage = DECLARATIVE_RELOAD_TEST_STAGE_NONE;
    declarative_reload_message_test_ops = NULL;
    declarative_reload_reconcile_entry_test_hook = NULL;
    declarative_reload_prepublication_test_hook = NULL;
    declarative_reconcile_retry_test_hook = NULL;
    declarative_reconcile_retry_test_context = NULL;
    pthread_mutex_lock(&declarative_reconcile_lock);
    declarative_reconcile_active_token = 0;
    declarative_reconcile_retry_attempt = 0;
    declarative_reconcile_retry_waiting = false;
    declarative_reconcile_last_diagnostic[0] = '\0';
    pthread_mutex_unlock(&declarative_reconcile_lock);
}
#endif
