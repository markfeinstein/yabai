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
                                       const struct wm_tile_membership_diagnostic *diagnostic)
{
    if (!stream || !platform || !diagnostic || !platform->build || !platform->architecture ||
        strchr(platform->build, '"') || strchr(platform->architecture, '"')) return false;
    int written = fprintf(stream,
        "{\"schema\":1,\"probe\":\"native-tile-membership\",\"mode\":\"read-only\","
        "\"status\":\"%s\",\"platform\":{\"os-major\":%d,\"os-minor\":%d,"
        "\"os-patch\":%d,\"build\":\"%s\",\"architecture\":\"%s\"},"
        "\"capabilities\":{\"managed-space-topology\":\"%s\","
        "\"tile-space-membership\":\"%s\"},\"counts\":{"
        "\"managed-displays\":%u,\"managed-spaces\":%u,\"spaces-queried\":%u,"
        "\"tiled-parent-spaces\":%u,\"tile-spaces\":%u,\"failed-queries\":%u},"
        "\"redaction\":\"aggregate-only\"}\n",
        native_tile_status_string(diagnostic->status), platform->os_major, platform->os_minor,
        platform->os_patch, platform->build, platform->architecture,
        native_tile_capability_topology(diagnostic->status),
        native_tile_capability_membership(diagnostic->status),
        diagnostic->managed_display_count, diagnostic->managed_space_count,
        diagnostic->spaces_queried, diagnostic->tiled_parent_space_count,
        diagnostic->tile_space_count, diagnostic->failed_query_count);
    return written > 0 && fflush(stream) == 0 && !ferror(stream);
}
