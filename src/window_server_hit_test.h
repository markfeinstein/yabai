#ifndef WINDOW_SERVER_HIT_TEST_H
#define WINDOW_SERVER_HIT_TEST_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stdint.h>

struct window_server_hit_test_request
{
    int32_t connection;
    uint32_t filter_window_id;
    int32_t flags;
    int32_t reserved;
    CGPoint screen_point;
};

struct window_server_hit_test_result
{
    CGError status;
    CGPoint window_point;
    uint32_t window_id;
    int32_t window_connection;
    bool invoked;
};

struct window_server_hit_test_comparison
{
    struct window_server_hit_test_result legacy;
    struct window_server_hit_test_result geometry;
    bool geometry_available;
    bool same_window;
    bool same_owner;
    bool same_window_point;
};

void window_server_hit_test_legacy(const struct window_server_hit_test_request *request,
                                   struct window_server_hit_test_result *result);
void window_server_hit_test_compare(const struct window_server_hit_test_request *request,
                                    struct window_server_hit_test_comparison *comparison);

#endif
