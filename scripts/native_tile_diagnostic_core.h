#ifndef NATIVE_TILE_DIAGNOSTIC_CORE_H
#define NATIVE_TILE_DIAGNOSTIC_CORE_H

#include <stdbool.h>
#include <stdio.h>

#include "../src/window_management_bridge.h"

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
                                       const struct wm_tile_membership_diagnostic *diagnostic);

#endif
