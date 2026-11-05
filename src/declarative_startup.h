#ifndef DECLARATIVE_STARTUP_H
#define DECLARATIVE_STARTUP_H

#include <stdbool.h>
#include <limits.h>

#include "declarative_config.h"
#include "declarative_topology.h"

enum declarative_primary_action {
    DECLARATIVE_PRIMARY_DAEMON,
    DECLARATIVE_PRIMARY_ESTABLISHED,
    DECLARATIVE_PRIMARY_CHECK,
    DECLARATIVE_PRIMARY_INVALID_CHECK
};

struct declarative_startup_state {
    struct declarative_config_model *model;
    struct declarative_topology topology;
    struct declarative_detached_view_table detached_views;
    char path[PATH_MAX];
    bool present;
    bool detached_prepared;
};

typedef void (*declarative_startup_step)(void *context);
typedef bool (*declarative_startup_gate)(void *context);

struct declarative_startup_completion_ops {
    declarative_startup_step inventory;
    declarative_startup_gate rebuild_notifications;
    declarative_startup_gate seal_menu_staging;
    declarative_startup_gate start_socket;
    declarative_startup_step execute_legacy_config;
    void *context;
    bool notifications_required;
};

enum declarative_startup_completion_result {
    DECLARATIVE_STARTUP_COMPLETION_OK,
    DECLARATIVE_STARTUP_COMPLETION_INVALID,
    DECLARATIVE_STARTUP_COMPLETION_NOTIFICATION_FAILED,
    DECLARATIVE_STARTUP_COMPLETION_MENU_FAILED,
    DECLARATIVE_STARTUP_COMPLETION_SOCKET_FAILED
};

enum declarative_primary_action declarative_primary_action_classify(int argc, char **argv);
bool declarative_startup_capture_path(const char *path, const char *cwd, char output[PATH_MAX], struct declarative_error *error);
bool declarative_startup_select(struct declarative_startup_state *state, const char *explicit_path,
                                const char *xdg_config_home, const char *home,
                                struct declarative_error *error);
struct declarative_space_defaults declarative_startup_view_defaults(const struct declarative_config_model *model);
bool declarative_startup_prepare_runtime(struct declarative_startup_state *state,
                                         const struct declarative_space_defaults *defaults,
                                         const struct declarative_topology_capture_ops *ops);
enum declarative_startup_completion_result declarative_startup_complete(const struct declarative_startup_completion_ops *ops);
void declarative_startup_destroy(struct declarative_startup_state *state);
const char *declarative_error_stage_name(enum declarative_error_stage stage);

#endif
