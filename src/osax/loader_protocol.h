#ifndef YABAI_LOADER_PROTOCOL_H
#define YABAI_LOADER_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOADER_CONTROL_MAGIC 0x59424c44u
#define LOADER_PAYLOAD_PATH_CAPACITY 256u

enum loader_bootstrap_state {
    LOADER_BOOTSTRAP_INIT = 0,
    LOADER_BOOTSTRAP_CREATE_FAIL = 1,
    LOADER_BOOTSTRAP_READY = 2,
    LOADER_BOOTSTRAP_LOAD_OK = 3,
    LOADER_BOOTSTRAP_LOAD_FAIL = 4,
    LOADER_BOOTSTRAP_JOIN_FAIL = 5,
};

enum loader_child_state {
    LOADER_CHILD_INIT = 0,
    LOADER_CHILD_LOAD_OK = 1,
    LOADER_CHILD_LOAD_FAIL = 2,
};

typedef struct __attribute__((aligned(8))) loader_control {
    uint32_t magic;
    uint32_t bootstrap_state;
    uint32_t child_state;
    uint32_t detail;
    uint64_t child_thread_id;
} loader_control_t;

_Static_assert(sizeof(loader_control_t) == 24, "loader control ABI changed");
_Static_assert(offsetof(loader_control_t, child_thread_id) == 16, "loader control ABI changed");

typedef enum loader_poll_decision {
    LOADER_POLL_WAIT,
    LOADER_POLL_SUCCESS,
    LOADER_POLL_FAILURE,
} loader_poll_decision_t;

static inline bool loader_bootstrap_terminal(uint32_t state)
{
    return state == LOADER_BOOTSTRAP_CREATE_FAIL ||
           state == LOADER_BOOTSTRAP_LOAD_OK ||
           state == LOADER_BOOTSTRAP_LOAD_FAIL ||
           state == LOADER_BOOTSTRAP_JOIN_FAIL;
}

static inline loader_poll_decision_t loader_control_decision(const loader_control_t *control, bool timed_out)
{
    if (timed_out || control->magic != LOADER_CONTROL_MAGIC) return LOADER_POLL_FAILURE;
    if (control->bootstrap_state == LOADER_BOOTSTRAP_INIT ||
        control->bootstrap_state == LOADER_BOOTSTRAP_READY) return LOADER_POLL_WAIT;
    if (!loader_bootstrap_terminal(control->bootstrap_state)) return LOADER_POLL_FAILURE;
    if (control->bootstrap_state == LOADER_BOOTSTRAP_LOAD_OK &&
        control->child_state == LOADER_CHILD_LOAD_OK) return LOADER_POLL_SUCCESS;
    return LOADER_POLL_FAILURE;
}

static inline bool loader_control_proves_child_finished(const loader_control_t *control)
{
    return control->magic == LOADER_CONTROL_MAGIC &&
           (control->bootstrap_state == LOADER_BOOTSTRAP_CREATE_FAIL ||
            control->bootstrap_state == LOADER_BOOTSTRAP_LOAD_OK ||
            control->bootstrap_state == LOADER_BOOTSTRAP_LOAD_FAIL);
}

typedef struct loader_cleanup_plan {
    bool terminate_bootstrap;
    bool reclaim_remote_vm;
} loader_cleanup_plan_t;

static inline loader_cleanup_plan_t loader_cleanup_plan(const loader_control_t *control,
                                                         bool launched,
                                                         bool control_observed)
{
    if (!launched) {
        return (loader_cleanup_plan_t) {
            .terminate_bootstrap = true,
            .reclaim_remote_vm = true,
        };
    }

    if (!control_observed || control->magic != LOADER_CONTROL_MAGIC) {
        return (loader_cleanup_plan_t) {};
    }

    switch (control->bootstrap_state) {
    case LOADER_BOOTSTRAP_CREATE_FAIL:
    case LOADER_BOOTSTRAP_LOAD_OK:
    case LOADER_BOOTSTRAP_LOAD_FAIL:
        return (loader_cleanup_plan_t) {
            .terminate_bootstrap = true,
            .reclaim_remote_vm = true,
        };
    case LOADER_BOOTSTRAP_JOIN_FAIL:
        return (loader_cleanup_plan_t) {
            .terminate_bootstrap = true,
            .reclaim_remote_vm = false,
        };
    default:
        return (loader_cleanup_plan_t) {};
    }
}

#endif
