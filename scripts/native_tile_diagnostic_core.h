#ifndef NATIVE_TILE_DIAGNOSTIC_CORE_H
#define NATIVE_TILE_DIAGNOSTIC_CORE_H

#include <stdbool.h>
#include <stdio.h>

#include "../src/window_management_bridge.h"

struct native_tile_extended_diagnostic
{
    bool metadata_verified;
    bool divider_available;
    bool tile_rect_available;
    bool can_create_available;
    bool direct_abi_verified;
    uint32_t divider_calls;
    uint32_t divider_successes;
    uint32_t divider_nonzero;
    uint32_t divider_repeat_stable;
    uint32_t tile_rect_calls;
    uint32_t tile_rect_successes;
    uint32_t tile_rect_nonempty;
    uint32_t can_create_calls;
    uint32_t can_create_successes;
    uint32_t can_create_true;
    uint32_t direct_divider_calls;
    uint32_t direct_divider_matches;
    uint32_t direct_tile_rect_calls;
    uint32_t direct_tile_rect_matches;
    uint32_t direct_can_create_calls;
    uint32_t direct_can_create_matches;
};

struct native_tile_platform
{
    int os_major;
    int os_minor;
    int os_patch;
    const char *build;
    const char *architecture;
};

const char *native_tile_status_string(enum wm_tile_membership_status status);
bool native_tile_diagnostic_write_json(FILE *stream,
                                       const struct native_tile_platform *platform,
                                       const struct wm_tile_membership_diagnostic *diagnostic,
                                       const struct native_tile_extended_diagnostic *extended);

#endif
