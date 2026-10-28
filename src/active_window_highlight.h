#ifndef ACTIVE_WINDOW_HIGHLIGHT_H
#define ACTIVE_WINDOW_HIGHLIGHT_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stdint.h>

#define ACTIVE_WINDOW_HIGHLIGHT_COLOR_DEFAULT 0xffd75f5fU
#define ACTIVE_WINDOW_HIGHLIGHT_RADIUS_DEFAULT 9.0f
#define ACTIVE_WINDOW_HIGHLIGHT_WIDTH_DEFAULT 4.0f

struct active_window_highlight_geometry
{
    CGRect frame;
    CGRect bounds;
    CGRect stroke_rect;
    CGFloat stroke_width;
    CGFloat corner_radius_x;
    CGFloat corner_radius_y;
};

struct active_window_highlight
{
    uint32_t id;
    CGContextRef context;
    CGRect frame;
    uint32_t target_wid;
    uint32_t color;
    uint64_t sid;
    int level;
    int sub_level;
    float width;
    float radius;
    CGRect pending_target_frame;
    uint32_t pending_target_wid;
    bool sticky;
    bool ordered;
    bool needs_reorder;
    bool pending_target_frame_valid;
};

struct active_window_highlight_geometry active_window_highlight_geometry_make(CGRect target_frame,
                                                                                float configured_width,
                                                                                float configured_radius);
bool active_window_highlight_show(struct active_window_highlight *highlight,
                                  uint32_t target_wid, CGRect target_frame,
                                  int level, int sub_level, uint64_t sid, bool sticky,
                                  uint32_t color, float width, float radius);
void active_window_highlight_hide(struct active_window_highlight *highlight);
void active_window_highlight_invalidate_order(struct active_window_highlight *highlight);
void active_window_highlight_destroy(struct active_window_highlight *highlight);

#endif
