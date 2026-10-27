#include "window_server_hit_test.h"

#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

extern OSStatus SLSFindWindowAndOwner(int cid, int filter_window_id, int flags, int reserved,
                                      CGPoint *screen_point, CGPoint *window_point,
                                      uint32_t *window_id, int *window_cid);

typedef CGError (*sls_find_window_by_geometry_fn)(
    int32_t cid, uint32_t filter_window_id, int32_t flags, int32_t reserved,
    const CGPoint *screen_point, CGPoint *window_point,
    uint32_t *window_id, int32_t *window_cid);

static sls_find_window_by_geometry_fn window_server_hit_test_geometry_impl;
static pthread_once_t window_server_hit_test_geometry_once = PTHREAD_ONCE_INIT;

static void window_server_hit_test_resolve_geometry(void)
{
    window_server_hit_test_geometry_impl = (sls_find_window_by_geometry_fn)
        dlsym(RTLD_DEFAULT, "SLSFindWindowByGeometry");
}

#ifdef TESTS
static OSStatus (*window_server_hit_test_legacy_impl)(int, int, int, int, CGPoint *, CGPoint *, uint32_t *, int *) =
    SLSFindWindowAndOwner;
static sls_find_window_by_geometry_fn window_server_hit_test_geometry_test_impl;
static bool window_server_hit_test_geometry_test_available;
#else
#define window_server_hit_test_legacy_impl SLSFindWindowAndOwner
#endif

static sls_find_window_by_geometry_fn window_server_hit_test_geometry(void)
{
#ifdef TESTS
    return window_server_hit_test_geometry_test_available
         ? window_server_hit_test_geometry_test_impl : NULL;
#else
    pthread_once(&window_server_hit_test_geometry_once,
                 window_server_hit_test_resolve_geometry);
    return window_server_hit_test_geometry_impl;
#endif
}

enum window_server_hit_test_relation window_server_hit_test_classify_relation(
    bool valid, bool same_window, bool parent_child, bool associated, bool same_owner)
{
    if (!valid) return WINDOW_SERVER_HIT_TEST_RELATION_UNAVAILABLE;
    if (same_window) return WINDOW_SERVER_HIT_TEST_RELATION_SAME_WINDOW;
    if (parent_child) return WINDOW_SERVER_HIT_TEST_RELATION_PARENT_CHILD;
    if (associated) return WINDOW_SERVER_HIT_TEST_RELATION_ASSOCIATED;
    if (same_owner) return WINDOW_SERVER_HIT_TEST_RELATION_SAME_OWNER;
    return WINDOW_SERVER_HIT_TEST_RELATION_UNRELATED;
}

const char *window_server_hit_test_relation_string(enum window_server_hit_test_relation relation)
{
    switch (relation) {
    case WINDOW_SERVER_HIT_TEST_RELATION_UNAVAILABLE: return "unavailable";
    case WINDOW_SERVER_HIT_TEST_RELATION_SAME_WINDOW: return "same-window";
    case WINDOW_SERVER_HIT_TEST_RELATION_PARENT_CHILD: return "parent-child";
    case WINDOW_SERVER_HIT_TEST_RELATION_ASSOCIATED: return "associated";
    case WINDOW_SERVER_HIT_TEST_RELATION_SAME_OWNER: return "same-owner";
    case WINDOW_SERVER_HIT_TEST_RELATION_UNRELATED: return "unrelated";
    }
    return "unavailable";
}

void window_server_hit_test_legacy(const struct window_server_hit_test_request *request,
                                   struct window_server_hit_test_result *result)
{
    if (!result) return;
    *result = (struct window_server_hit_test_result) {0};
    if (!request) return;
    CGPoint screen_point = request->screen_point;
    result->invoked = true;
    result->status = (CGError) window_server_hit_test_legacy_impl(
        request->connection, (int) request->filter_window_id,
        request->flags, request->reserved, &screen_point,
        &result->window_point, &result->window_id,
        (int *) &result->window_connection);
    if (result->status != kCGErrorSuccess) {
        result->window_point = CGPointZero;
        result->window_id = 0;
        result->window_connection = 0;
    }
}

void window_server_hit_test_compare(const struct window_server_hit_test_request *request,
                                    struct window_server_hit_test_comparison *comparison)
{
    if (!comparison) return;
    *comparison = (struct window_server_hit_test_comparison) {0};
    window_server_hit_test_legacy(request, &comparison->legacy);
    sls_find_window_by_geometry_fn geometry = window_server_hit_test_geometry();
    comparison->geometry_available = geometry != NULL;
    if (!request || !geometry) return;
    comparison->geometry.invoked = true;
    comparison->geometry.status = geometry(
        request->connection, request->filter_window_id, request->flags, request->reserved,
        &request->screen_point, &comparison->geometry.window_point,
        &comparison->geometry.window_id, &comparison->geometry.window_connection);
    if (comparison->geometry.status != kCGErrorSuccess) {
        comparison->geometry.window_point = CGPointZero;
        comparison->geometry.window_id = 0;
        comparison->geometry.window_connection = 0;
        return;
    }
    if (comparison->legacy.status == kCGErrorSuccess) {
        comparison->same_window = comparison->legacy.window_id == comparison->geometry.window_id;
        comparison->same_owner = comparison->legacy.window_connection == comparison->geometry.window_connection;
        comparison->same_window_point = CGPointEqualToPoint(
            comparison->legacy.window_point, comparison->geometry.window_point);
    }
}
