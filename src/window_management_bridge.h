#ifndef WINDOW_MANAGEMENT_BRIDGE_H
#define WINDOW_MANAGEMENT_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

enum wm_bridge_read_status
{
    WM_BRIDGE_READ_UNAVAILABLE,
    WM_BRIDGE_READ_ABI_MISMATCH,
    WM_BRIDGE_READ_FAILED,
    WM_BRIDGE_READ_OK
};

struct wm_bridge_capabilities
{
    bool skylight_loaded;
    bool window_management_loaded;
    bool client_abi;
    bool copy_topology_abi;
    bool create_space_metadata;
    bool destroy_space_metadata;
    bool move_space_metadata;
    bool set_current_space_metadata;
    bool create_space_result_metadata;
};

struct wm_bridge_diagnostic
{
    enum wm_bridge_read_status read_status;
    struct wm_bridge_capabilities capabilities;
    uint32_t managed_display_record_count;
};

/* Performs one allowlisted copy-topology operation after validating the exact
 * private Objective-C ABI. Mutation classes are metadata-only and are never
 * allocated or dispatched. */
bool window_management_bridge_diagnose_readonly(struct wm_bridge_diagnostic *diagnostic);

#endif
