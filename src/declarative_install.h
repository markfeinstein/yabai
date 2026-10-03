#ifndef DECLARATIVE_INSTALL_H
#define DECLARATIVE_INSTALL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct declarative_startup_state;
struct declarative_topology_capture_ops;
struct declarative_error;
struct declarative_topology;

/* Prepares and publishes generation one on the bootstrap worker. */
bool declarative_startup_install(struct declarative_startup_state *startup,
                                 const struct declarative_topology_capture_ops *topology_ops);
uint64_t declarative_installed_generation(void);
const struct declarative_config_model *declarative_installed_model(void);
const char *declarative_installed_path(void);
bool declarative_installed_has_scaffold(void);
bool declarative_config_reload(FILE *response);
bool declarative_configuration_reconcile(void);
void declarative_configuration_request_reconcile(void);

#ifdef TESTS
struct declarative_topology_bounds_config;
struct declarative_space_postcommit_ops;
struct window;

enum declarative_reload_test_stage
{
    DECLARATIVE_RELOAD_TEST_STAGE_NONE,
    DECLARATIVE_RELOAD_TEST_STAGE_SELECT,
    DECLARATIVE_RELOAD_TEST_STAGE_SETTINGS,
    DECLARATIVE_RELOAD_TEST_STAGE_TOPOLOGY,
    DECLARATIVE_RELOAD_TEST_STAGE_MANAGER,
    DECLARATIVE_RELOAD_TEST_STAGE_RULE_SNAPSHOT,
    DECLARATIVE_RELOAD_TEST_STAGE_RULE_PREPARE,
    DECLARATIVE_RELOAD_TEST_STAGE_FOLD_CONTEXT,
    DECLARATIVE_RELOAD_TEST_STAGE_LABEL_INDEX,
    DECLARATIVE_RELOAD_TEST_STAGE_RULE_BUFFER,
    DECLARATIVE_RELOAD_TEST_STAGE_PATH,
    DECLARATIVE_RELOAD_TEST_STAGE_SPACE_SNAPSHOT,
    DECLARATIVE_RELOAD_TEST_STAGE_TARGETS,
    DECLARATIVE_RELOAD_TEST_STAGE_FOREST,
    DECLARATIVE_RELOAD_TEST_STAGE_COUNT
};

struct declarative_reload_test_ops
{
    bool (*select)(struct declarative_startup_state *state, const char *installed_path,
                   const char *xdg_config_home, const char *home,
                   struct declarative_error *error, void *context);
    bool (*capture_topology)(const struct declarative_topology_bounds_config *bounds,
                             struct declarative_topology *topology, void *context);
    bool (*query_sid)(uint64_t sid, const uint32_t **wids, size_t *count, void *context);
    struct window *(*find_window)(uint32_t wid, void *context);
    bool (*is_eligible)(struct window *window, void *context);
    const struct declarative_space_postcommit_ops *postcommit_ops;
    void (*postcommit_probe)(bool committed, void *context);
    void *context;
};

bool declarative_config_reload_with_test_ops(FILE *response,
                                             const struct declarative_reload_test_ops *ops);
bool declarative_configuration_reconcile_with_test_ops(const struct declarative_reload_test_ops *ops);
void declarative_reload_test_set_message_ops(const struct declarative_reload_test_ops *ops);
void declarative_reload_test_set_reconcile_entry_hook(void (*hook)(void));
void declarative_reload_test_fail_stage(enum declarative_reload_test_stage stage);
uint32_t declarative_reload_test_reconcile_reservation(void);
void declarative_install_test_fail_allocation_at(size_t ordinal);
size_t declarative_install_test_allocation_count(void);
size_t declarative_install_test_outstanding_allocations(void);
void declarative_install_test_reset(void);
void declarative_install_test_set_generation(uint64_t generation);
#endif

#endif
