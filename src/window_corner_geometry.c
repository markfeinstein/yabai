#include "window_corner_geometry.h"

#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

typedef CFArrayRef (*window_corner_geometry_resolved_getter)(CFTypeRef iterator,
                                                              uint32_t window_id);

static window_corner_geometry_resolved_getter window_corner_geometry_resolved_impl;
static pthread_once_t window_corner_geometry_resolved_once = PTHREAD_ONCE_INIT;
static const bool window_corner_geometry_production_collection_enabled = false;
static const bool window_corner_geometry_production_rendering_enabled = false;

#ifdef TESTS
static bool (*window_corner_geometry_proxy_renderer)(int connection,
                                                     uint32_t proxy_window_id,
                                                     const double radius[WINDOW_CORNER_GEOMETRY_RADIUS_COUNT]);
#endif

static void window_corner_geometry_resolve(void)
{
    window_corner_geometry_resolved_impl = (window_corner_geometry_resolved_getter)
        dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetResolvedCornerRadii");
}

bool window_corner_geometry_normalize(CFTypeRef value,
                                      enum window_corner_geometry_source source,
                                      struct window_corner_geometry *geometry)
{
    if (!geometry) return false;
    struct window_corner_geometry normalized = {0};
    if (!value || source == WINDOW_CORNER_GEOMETRY_SOURCE_NONE ||
        CFGetTypeID(value) != CFArrayGetTypeID()) {
        *geometry = normalized;
        return false;
    }

    CFArrayRef array = (CFArrayRef) value;
    if (CFArrayGetCount(array) != WINDOW_CORNER_GEOMETRY_RADIUS_COUNT) {
        *geometry = normalized;
        return false;
    }

    for (CFIndex i = 0; i < WINDOW_CORNER_GEOMETRY_RADIUS_COUNT; ++i) {
        CFTypeRef item = CFArrayGetValueAtIndex(array, i);
        double radius = 0.0;
        if (!item || CFGetTypeID(item) != CFNumberGetTypeID() ||
            !CFNumberGetValue((CFNumberRef) item, kCFNumberDoubleType, &radius) ||
            !isfinite(radius) || radius < 0.0) {
            *geometry = (struct window_corner_geometry) {0};
            return false;
        }
        normalized.radius[i] = radius;
        normalized.valid_mask |= (uint8_t) (1u << i);
    }

    normalized.count = WINDOW_CORNER_GEOMETRY_RADIUS_COUNT;
    normalized.source = (uint8_t) source;
    *geometry = normalized;
    return true;
}

bool window_corner_geometry_is_complete(const struct window_corner_geometry *geometry)
{
    return geometry && geometry->count == WINDOW_CORNER_GEOMETRY_RADIUS_COUNT &&
           geometry->valid_mask == WINDOW_CORNER_GEOMETRY_VALID_ALL &&
           geometry->source != WINDOW_CORNER_GEOMETRY_SOURCE_NONE;
}

struct window_corner_geometry_capability window_corner_geometry_capability(void)
{
    pthread_once(&window_corner_geometry_resolved_once, window_corner_geometry_resolve);
    return (struct window_corner_geometry_capability) {
        .resolved_getter_available = window_corner_geometry_resolved_impl != NULL,
        .collection_enabled = window_corner_geometry_production_collection_enabled,
        .proxy_rendering_enabled = window_corner_geometry_production_rendering_enabled
    };
}

bool window_corner_geometry_collect_resolved(CFTypeRef iterator, uint32_t window_id,
                                             struct window_corner_geometry *geometry)
{
    if (geometry) memset(geometry, 0, sizeof(*geometry));
    struct window_corner_geometry_capability capability = window_corner_geometry_capability();
    if (!iterator || !window_id || !geometry || !capability.resolved_getter_available ||
        !capability.collection_enabled) return false;

    CFArrayRef value = window_corner_geometry_resolved_impl(iterator, window_id);
    return window_corner_geometry_normalize(value, WINDOW_CORNER_GEOMETRY_SOURCE_SLS_RESOLVED,
                                            geometry);
}

bool window_corner_geometry_apply_to_proxy(int connection, uint32_t proxy_window_id,
                                           CGSize proxy_size,
                                           const struct window_corner_geometry *geometry)
{
    if (!connection || !proxy_window_id || !window_corner_geometry_is_complete(geometry) ||
        !isfinite(proxy_size.width) || !isfinite(proxy_size.height) ||
        proxy_size.width <= 0.0 || proxy_size.height <= 0.0) return false;

    double maximum = 0.5 * fmin(proxy_size.width, proxy_size.height);
    double radius[WINDOW_CORNER_GEOMETRY_RADIUS_COUNT];
    for (size_t i = 0; i < WINDOW_CORNER_GEOMETRY_RADIUS_COUNT; ++i) {
        radius[i] = fmin(geometry->radius[i], maximum);
    }

#ifdef TESTS
    if (window_corner_geometry_proxy_renderer) {
        return window_corner_geometry_proxy_renderer(connection, proxy_window_id, radius);
    }
#endif
    (void) radius;
    return false;
}
