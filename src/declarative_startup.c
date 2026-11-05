extern struct event_loop g_event_loop;

static bool declarative_token_is_established_primary(const char *token)
{
    static const char *tokens[] = {
        "--help", "-h", "--version", "-v", "--doctor", "--message", "-m",
        "--load-sa", "--uninstall-sa", "--install-service", "--uninstall-service",
        "--start-service", "--restart-service", "--stop-service"
    };
    for (size_t i = 0; i < sizeof(tokens) / sizeof(*tokens); ++i) {
        if (strcmp(token, tokens[i]) == 0) return true;
    }
    return false;
}

enum declarative_primary_action declarative_primary_action_classify(int argc, char **argv)
{
    if (argc <= 1) return DECLARATIVE_PRIMARY_DAEMON;

    bool check_present = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0 ||
            strcmp(argv[i], "--config-jsonc") == 0) {
            if (i + 1 < argc) ++i;
            continue;
        }
        if (strcmp(argv[i], "--check-config") == 0) check_present = true;
    }
    if (check_present) {
        if (argc == 3 && strcmp(argv[1], "--check-config") == 0 &&
            argv[2][0] != '-' && argv[2][0] != '\0') return DECLARATIVE_PRIMARY_CHECK;
        return DECLARATIVE_PRIMARY_INVALID_CHECK;
    }
    if (declarative_token_is_established_primary(argv[1])) return DECLARATIVE_PRIMARY_ESTABLISHED;
    return DECLARATIVE_PRIMARY_DAEMON;
}

static void declarative_startup_path_error(struct declarative_error *error, const char *message)
{
    if (!error) return;
    memset(error, 0, sizeof(*error));
    error->stage = DECLARATIVE_ERROR_PATH;
    snprintf(error->json_path, sizeof(error->json_path), "%s", "$");
    snprintf(error->message, sizeof(error->message), "%s", message);
}

bool declarative_startup_capture_path(const char *path, const char *cwd, char output[PATH_MAX], struct declarative_error *error)
{
    if (!path || !output) {
        declarative_startup_path_error(error, "path is required");
        return false;
    }

    int length;
    if (path[0] == '/') {
        length = snprintf(output, PATH_MAX, "%s", path);
    } else {
        if (!cwd || cwd[0] != '/') {
            declarative_startup_path_error(error, "could not capture current directory");
            return false;
        }
        length = snprintf(output, PATH_MAX, "%s/%s", cwd, path);
    }
    if (length < 0 || length >= PATH_MAX) {
        output[0] = '\0';
        declarative_startup_path_error(error, "path exceeds limit");
        return false;
    }
    if (!declarative_config_validate_path(output, error)) {
        output[0] = '\0';
        return false;
    }
    return true;
}

static enum declarative_file_result declarative_startup_try_candidate(struct declarative_startup_state *state,
                                                                       const char *path,
                                                                       struct declarative_error *error)
{
    struct declarative_config_model *model = NULL;
    enum declarative_file_result result = declarative_config_parse_optional_file(path, &model, error);
    if (result == DECLARATIVE_FILE_OK) {
        snprintf(state->path, sizeof(state->path), "%s", path);
        state->model = model;
        state->present = true;
    }
    return result;
}

bool declarative_startup_select(struct declarative_startup_state *state, const char *explicit_path,
                                const char *xdg_config_home, const char *home,
                                struct declarative_error *error)
{
    if (!state) return false;
    declarative_startup_destroy(state);
    if (error) memset(error, 0, sizeof(*error));

    if (explicit_path) {
        enum declarative_file_result result = declarative_startup_try_candidate(state, explicit_path, error);
        if (result == DECLARATIVE_FILE_ABSENT && error) {
            error->stage = DECLARATIVE_ERROR_OPEN;
            snprintf(error->json_path, sizeof(error->json_path), "%s", "$");
            snprintf(error->message, sizeof(error->message), "could not open configuration");
        }
        return result == DECLARATIVE_FILE_OK;
    }

    char candidate[PATH_MAX];
    if (xdg_config_home && xdg_config_home[0] == '/') {
        int length = snprintf(candidate, sizeof(candidate), "%s/yabai/yabai.jsonc", xdg_config_home);
        if (length < 0 || length >= (int)sizeof(candidate)) {
            declarative_startup_path_error(error, "path exceeds limit");
            return false;
        }
        enum declarative_file_result result = declarative_startup_try_candidate(state, candidate, error);
        if (result == DECLARATIVE_FILE_OK) return true;
        if (result == DECLARATIVE_FILE_ERROR) return false;
    }

    if (home && home[0] == '/') {
        int length = snprintf(candidate, sizeof(candidate), "%s/.config/yabai/yabai.jsonc", home);
        if (length < 0 || length >= (int)sizeof(candidate)) {
            declarative_startup_path_error(error, "path exceeds limit");
            return false;
        }
        enum declarative_file_result result = declarative_startup_try_candidate(state, candidate, error);
        if (result == DECLARATIVE_FILE_OK) return true;
        if (result == DECLARATIVE_FILE_ERROR) return false;
    }
    if (error) memset(error, 0, sizeof(*error));
    return true;
}

struct declarative_space_defaults declarative_startup_view_defaults(const struct declarative_config_model *model)
{
    struct declarative_space_defaults defaults = {
        .layout = VIEW_FLOAT,
        .accordion_padding = 30,
        .single_window_width_ratio = 1.0f,
        .split_type = SPLIT_AUTO,
        .split_ratio = 0.5f,
        .window_placement = CHILD_SECOND,
        .window_insertion_point = INSERT_FOCUSED,
        .window_zoom_persist = true
    };
    if (!model) return defaults;
#define NATIVE_INT(id, field) if (model->config[DECLARATIVE_CONFIG_##id].present) defaults.field = model->config[DECLARATIVE_CONFIG_##id].integer
    NATIVE_INT(TOP_PADDING, top_padding);
    NATIVE_INT(BOTTOM_PADDING, bottom_padding);
    NATIVE_INT(LEFT_PADDING, left_padding);
    NATIVE_INT(RIGHT_PADDING, right_padding);
    NATIVE_INT(WINDOW_GAP, window_gap);
    NATIVE_INT(ACCORDION_PADDING, accordion_padding);
    NATIVE_INT(SINGLE_WINDOW_MAX_WIDTH, single_window_max_width);
    NATIVE_INT(SINGLE_WINDOW_MIN_WIDTH, single_window_min_width);
#undef NATIVE_INT
    if (model->config[DECLARATIVE_CONFIG_SINGLE_WINDOW_WIDTH_RATIO].present) defaults.single_window_width_ratio = model->config[DECLARATIVE_CONFIG_SINGLE_WINDOW_WIDTH_RATIO].real;
    if (model->config[DECLARATIVE_CONFIG_LAYOUT].present) defaults.layout = model->config[DECLARATIVE_CONFIG_LAYOUT].enumeration;
    if (model->config[DECLARATIVE_CONFIG_SPLIT_TYPE].present) defaults.split_type = model->config[DECLARATIVE_CONFIG_SPLIT_TYPE].enumeration;
    if (model->config[DECLARATIVE_CONFIG_AUTO_BALANCE].present) defaults.auto_balance = model->config[DECLARATIVE_CONFIG_AUTO_BALANCE].u32;
    if (model->config[DECLARATIVE_CONFIG_SPLIT_RATIO].present) defaults.split_ratio = model->config[DECLARATIVE_CONFIG_SPLIT_RATIO].real;
    if (model->config[DECLARATIVE_CONFIG_WINDOW_PLACEMENT].present) defaults.window_placement = model->config[DECLARATIVE_CONFIG_WINDOW_PLACEMENT].enumeration;
    if (model->config[DECLARATIVE_CONFIG_WINDOW_INSERT_POINT].present) defaults.window_insertion_point = model->config[DECLARATIVE_CONFIG_WINDOW_INSERT_POINT].enumeration;
    if (model->config[DECLARATIVE_CONFIG_WINDOW_ZOOM_PERSIST].present) defaults.window_zoom_persist = model->config[DECLARATIVE_CONFIG_WINDOW_ZOOM_PERSIST].boolean;
    return defaults;
}

static struct declarative_topology_bounds_config declarative_startup_topology_bounds_config(const struct declarative_config_model *model)
{
    struct declarative_topology_bounds_config result = { .external_bar_mode = EXTERNAL_BAR_OFF };
    if (model && model->config[DECLARATIVE_CONFIG_EXTERNAL_BAR].present) {
        const struct declarative_external_bar *external_bar =
            &model->config[DECLARATIVE_CONFIG_EXTERNAL_BAR].external_bar;
        result.external_bar_mode = external_bar->mode;
        result.external_bar_top_padding = external_bar->top_padding;
        result.external_bar_bottom_padding = external_bar->bottom_padding;
    }
    return result;
}

bool declarative_startup_prepare_runtime(struct declarative_startup_state *state,
                                         const struct declarative_space_defaults *defaults,
                                         const struct declarative_topology_capture_ops *ops)
{
    if (!state || !defaults || state->detached_prepared) return false;
    if (!state->present) return true;
    if (!event_loop_is_worker_thread(&g_event_loop)) return false;

    struct declarative_topology_bounds_config bounds_config = declarative_startup_topology_bounds_config(state->model);

    struct declarative_topology topology = {0};
    struct declarative_detached_view_table views = {0};
    bool captured = ops ? declarative_topology_capture(ops, &topology)
                        : declarative_topology_capture_system(&bounds_config, &topology);
    if (!captured || !declarative_detached_view_table_prepare(&topology, defaults, &views)) {
        declarative_detached_view_table_destroy(&views);
        declarative_topology_destroy(&topology);
        return false;
    }

    state->topology = topology;
    state->detached_views = views;
    state->detached_prepared = true;
    return true;
}

enum declarative_startup_completion_result declarative_startup_complete(const struct declarative_startup_completion_ops *ops)
{
    if (!ops || !ops->inventory || !ops->seal_menu_staging || !ops->start_socket ||
        !ops->execute_legacy_config || (ops->notifications_required && !ops->rebuild_notifications)) {
        return DECLARATIVE_STARTUP_COMPLETION_INVALID;
    }

    ops->inventory(ops->context);
    if (ops->notifications_required && !ops->rebuild_notifications(ops->context)) {
        return DECLARATIVE_STARTUP_COMPLETION_NOTIFICATION_FAILED;
    }
    if (!ops->seal_menu_staging(ops->context)) return DECLARATIVE_STARTUP_COMPLETION_MENU_FAILED;
    if (!ops->start_socket(ops->context)) return DECLARATIVE_STARTUP_COMPLETION_SOCKET_FAILED;
    ops->execute_legacy_config(ops->context);
    return DECLARATIVE_STARTUP_COMPLETION_OK;
}

void declarative_startup_destroy(struct declarative_startup_state *state)
{
    if (!state) return;
    declarative_detached_view_table_destroy(&state->detached_views);
    declarative_topology_destroy(&state->topology);
    declarative_config_destroy(state->model);
    memset(state, 0, sizeof(*state));
}

const char *declarative_error_stage_name(enum declarative_error_stage stage)
{
    static const char *names[] = {
        "none", "path", "open", "read", "file", "encoding", "jsonc", "json",
        "schema", "regex", "allocation", "runtime"
    };
    return stage >= DECLARATIVE_ERROR_NONE && stage <= DECLARATIVE_ERROR_RUNTIME ? names[stage] : "unknown";
}
