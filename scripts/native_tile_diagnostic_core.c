#include "native_tile_diagnostic_core.h"

#include <string.h>

const char *native_tile_status_string(enum wm_tile_membership_status status)
{
    switch (status) {
    case WM_TILE_MEMBERSHIP_OK: return "ok";
    case WM_TILE_MEMBERSHIP_PARTIAL: return "partial";
    case WM_TILE_MEMBERSHIP_UNAVAILABLE: return "unavailable";
    case WM_TILE_MEMBERSHIP_ABI_MISMATCH: return "abi-mismatch";
    case WM_TILE_MEMBERSHIP_QUERY_FAILED: return "query-failed";
    }
    return "query-failed";
}

static const char *native_tile_capability_topology(enum wm_tile_membership_status status)
{
    switch (status) {
    case WM_TILE_MEMBERSHIP_OK:
    case WM_TILE_MEMBERSHIP_PARTIAL: return "available";
    case WM_TILE_MEMBERSHIP_UNAVAILABLE: return "unavailable";
    case WM_TILE_MEMBERSHIP_ABI_MISMATCH: return "abi-mismatch";
    case WM_TILE_MEMBERSHIP_QUERY_FAILED: return "query-failed";
    }
    return "query-failed";
}

static const char *native_tile_capability_membership(enum wm_tile_membership_status status)
{
    return status == WM_TILE_MEMBERSHIP_PARTIAL ? "query-failed"
                                                : native_tile_capability_topology(status);
}

bool native_tile_diagnostic_write_json(FILE *stream,
                                       const struct native_tile_platform *platform,
                                       const struct wm_tile_membership_diagnostic *diagnostic,
                                       const struct native_tile_extended_diagnostic *extended)
{
    if (!stream || !platform || !diagnostic || !extended || !platform->build || !platform->architecture ||
        strchr(platform->build, '"') || strchr(platform->architecture, '"')) return false;
    int written = fprintf(stream,
        "{\"schema\":2,\"probe\":\"native-tile-readonly\",\"mode\":\"read-only\","
        "\"status\":\"%s\",\"platform\":{\"os-major\":%d,\"os-minor\":%d,"
        "\"os-patch\":%d,\"build\":\"%s\",\"architecture\":\"%s\"},"
        "\"capabilities\":{\"managed-space-topology\":\"%s\","
        "\"tile-space-membership\":\"%s\",\"metadata-verified\":%s,"
        "\"divider-directions\":%s,\"window-tile-rect\":%s,"
        "\"can-create-tile\":%s,\"direct-abi-verified\":%s},\"counts\":{"
        "\"managed-displays\":%u,\"managed-spaces\":%u,\"spaces-queried\":%u,"
        "\"tiled-parent-spaces\":%u,\"tile-spaces\":%u,\"failed-queries\":%u,"
        "\"divider-calls\":%u,\"divider-successes\":%u,"
        "\"divider-nonzero\":%u,\"divider-repeat-stable\":%u,"
        "\"tile-rect-calls\":%u,\"tile-rect-successes\":%u,"
        "\"tile-rect-nonempty\":%u,\"can-create-calls\":%u,"
        "\"can-create-successes\":%u,\"can-create-true\":%u,"
        "\"direct-divider-calls\":%u,\"direct-divider-matches\":%u,"
        "\"direct-tile-rect-calls\":%u,\"direct-tile-rect-matches\":%u,"
        "\"direct-can-create-calls\":%u,\"direct-can-create-matches\":%u},"
        "\"production-tile-commands\":false,\"redaction\":\"aggregate-only\"}\n",
        native_tile_status_string(diagnostic->status), platform->os_major, platform->os_minor,
        platform->os_patch, platform->build, platform->architecture,
        native_tile_capability_topology(diagnostic->status),
        native_tile_capability_membership(diagnostic->status),
        extended->metadata_verified ? "true" : "false",
        extended->divider_available ? "true" : "false",
        extended->tile_rect_available ? "true" : "false",
        extended->can_create_available ? "true" : "false",
        extended->direct_abi_verified ? "true" : "false",
        diagnostic->managed_display_count, diagnostic->managed_space_count,
        diagnostic->spaces_queried, diagnostic->tiled_parent_space_count,
        diagnostic->tile_space_count, diagnostic->failed_query_count,
        extended->divider_calls, extended->divider_successes,
        extended->divider_nonzero, extended->divider_repeat_stable,
        extended->tile_rect_calls, extended->tile_rect_successes,
        extended->tile_rect_nonempty, extended->can_create_calls,
        extended->can_create_successes, extended->can_create_true,
        extended->direct_divider_calls, extended->direct_divider_matches,
        extended->direct_tile_rect_calls, extended->direct_tile_rect_matches,
        extended->direct_can_create_calls, extended->direct_can_create_matches);
    return written > 0 && fflush(stream) == 0 && !ferror(stream);
}
