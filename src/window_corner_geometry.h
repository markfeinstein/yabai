#ifndef WINDOW_CORNER_GEOMETRY_H
#define WINDOW_CORNER_GEOMETRY_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stdint.h>

#define WINDOW_CORNER_GEOMETRY_RADIUS_COUNT 4
#define WINDOW_CORNER_GEOMETRY_VALID_ALL ((1u << WINDOW_CORNER_GEOMETRY_RADIUS_COUNT) - 1u)

enum window_corner_geometry_source
{
    WINDOW_CORNER_GEOMETRY_SOURCE_NONE,
    WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED
};

struct window_corner_geometry
{
    double radius[WINDOW_CORNER_GEOMETRY_RADIUS_COUNT];
    uint8_t valid_mask;
    uint8_t count;
    uint8_t source;
    uint8_t reserved;
};

struct window_corner_geometry_capability
{
    bool resolved_getter_available;
    bool collection_enabled;
    bool proxy_rendering_enabled;
};

bool window_corner_geometry_normalize(CFTypeRef value,
                                      enum window_corner_geometry_source source,
                                      struct window_corner_geometry *geometry);
bool window_corner_geometry_is_complete(const struct window_corner_geometry *geometry);
struct window_corner_geometry_capability window_corner_geometry_capability(void);

/* macOS 27.0.1 arm64 disassembly validates a one-argument iterator getter
 * returning a newly created CFArray. Physical corner ordering remains opaque.
 * Production collection therefore stays disabled until cross-version policy. */
bool window_corner_geometry_collect_resolved(CFTypeRef iterator, uint32_t window_id,
                                             struct window_corner_geometry *geometry);

/* Proxy rendering is deliberately disabled in production. The function is an
 * inert cosmetic boundary until a later PR supplies a validated setter backend. */
bool window_corner_geometry_apply_to_proxy(int connection, uint32_t proxy_window_id,
                                           CGSize proxy_size,
                                           const struct window_corner_geometry *geometry);

#endif
