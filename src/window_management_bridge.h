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

enum wm_tile_membership_status
{
    WM_TILE_MEMBERSHIP_UNAVAILABLE,
    WM_TILE_MEMBERSHIP_ABI_MISMATCH,
    WM_TILE_MEMBERSHIP_QUERY_FAILED,
    WM_TILE_MEMBERSHIP_PARTIAL,
    WM_TILE_MEMBERSHIP_OK
};

struct wm_tile_membership_diagnostic
{
    enum wm_tile_membership_status status;
    uint32_t managed_display_count;
    uint32_t managed_space_count;
    uint32_t spaces_queried;
    uint32_t tiled_parent_space_count;
    uint32_t tile_space_count;
    uint32_t failed_query_count;
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
bool window_management_bridge_diagnose_tile_membership_readonly(
    struct wm_tile_membership_diagnostic *diagnostic);

#endif
