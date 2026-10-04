#ifndef SA_COMMON_H
#define SA_COMMON_H

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define SA_SOCKET_BUFF_LEN 0x1000

#define OSAX_VERSION                "2.1.34"

#define OSAX_ATTRIB_DOCK_SPACES     0x01
#define OSAX_ATTRIB_DPPM            0x02
#define OSAX_ATTRIB_ADD_SPACE       0x04
#define OSAX_ATTRIB_REM_SPACE       0x08
#define OSAX_ATTRIB_MOV_SPACE       0x10
#define OSAX_ATTRIB_SET_WINDOW      0x20
#define OSAX_ATTRIB_ANIM_TIME       0x40

#define OSAX_ATTRIB_ALL             (OSAX_ATTRIB_DOCK_SPACES | \
                                     OSAX_ATTRIB_DPPM | \
                                     OSAX_ATTRIB_ADD_SPACE | \
                                     OSAX_ATTRIB_REM_SPACE | \
                                     OSAX_ATTRIB_MOV_SPACE | \
                                     OSAX_ATTRIB_SET_WINDOW | \
                                     OSAX_ATTRIB_ANIM_TIME)

#define SA_OPERATION_RESPONSE_TIMEOUT_MS 10000

/* Exact CoreGraphics keys used by yabai's below/normal/above layer contract. */
#define SA_LAYER_KEY_BACKSTOP_MENU 3
#define SA_LAYER_KEY_NORMAL        4
#define SA_LAYER_KEY_FLOATING      5

static inline bool sa_layer_key_is_supported(int layer)
{
    return layer == SA_LAYER_KEY_BACKSTOP_MENU ||
           layer == SA_LAYER_KEY_NORMAL ||
           layer == SA_LAYER_KEY_FLOATING;
}

static inline bool sa_window_order_is_supported(int order)
{
    return order >= -1 && order <= 1;
}

enum sa_fade_failure_action {
    SA_FADE_FAILURE_DESTROY,
    SA_FADE_FAILURE_RETRY,
};

static inline enum sa_fade_failure_action sa_fade_failure_action_for_generation(uint64_t current_generation,
                                                                                uint64_t work_generation)
{
    return current_generation == work_generation ? SA_FADE_FAILURE_DESTROY : SA_FADE_FAILURE_RETRY;
}

static inline uint64_t sa_fade_next_generation(uint64_t generation)
{
    ++generation;
    return generation ? generation : 1;
}

static inline bool sa_opacity_is_safe(float opacity)
{
    return isfinite(opacity) && opacity >= 0.0f && opacity <= 1.0f;
}

static inline bool sa_opacity_duration_is_safe(float duration)
{
    return isfinite(duration) && duration >= 0.0f &&
           (double) duration * 1000.0 <= (double) INT_MAX;
}

/* Scale coordinates remain floats on the wire. Dimensions must also survive
 * the receiver's checked integer target-size conversion. */
static inline bool sa_scale_geometry_is_safe(float x, float y, float width, float height)
{
    return isfinite(x) && (double) x >= INT_MIN && (double) x <= INT_MAX &&
           isfinite(y) && (double) y >= INT_MIN && (double) y <= INT_MAX &&
           isfinite(width) && width >= 4.0f && (double) width <= INT_MAX &&
           isfinite(height) && height >= 4.0f && (double) height <= INT_MAX;
}

static inline bool sa_scale_transform_components(double source_width, double source_height,
                                                  float x, float y, float width, float height,
                                                  double *x_scale, double *y_scale,
                                                  double *translated_x, double *translated_y)
{
    if (x_scale) *x_scale = 0.0;
    if (y_scale) *y_scale = 0.0;
    if (translated_x) *translated_x = 0.0;
    if (translated_y) *translated_y = 0.0;
    if (!x_scale || !y_scale || !translated_x || !translated_y ||
        !isfinite(source_width) || source_width <= 0.0 || source_width > INT_MAX ||
        !isfinite(source_height) || source_height <= 0.0 || source_height > INT_MAX ||
        !sa_scale_geometry_is_safe(x, y, width, height)) return false;

    double target_width_value = floor((double) width / 4.0);
    if (!isfinite(target_width_value) || target_width_value < 1.0 || target_width_value > INT_MAX) return false;
    int target_width = (int) target_width_value;
    double source_ratio = source_width / source_height;
    double target_height_value = floor((double) target_width / source_ratio);
    if (!isfinite(source_ratio) || source_ratio <= 0.0 ||
        !isfinite(target_height_value) || target_height_value < 1.0 || target_height_value > INT_MAX) return false;
    int target_height = (int) target_height_value;

    double sx = source_width / target_width;
    double sy = source_height / target_height;
    double tx = -((double) x + width) + source_width / sx;
    double ty = -(double) y;
    if (!isfinite(sx) || sx <= 0.0 || !isfinite(sy) || sy <= 0.0 ||
        !isfinite(tx) || !isfinite(ty)) return false;

    *x_scale = sx;
    *y_scale = sy;
    *translated_x = tx;
    *translated_y = ty;
    return true;
}

enum sa_opcode
{
    SA_OPCODE_HANDSHAKE             = 0x01,
    SA_OPCODE_SPACE_FOCUS           = 0x02,
    SA_OPCODE_SPACE_CREATE          = 0x03,
    SA_OPCODE_SPACE_DESTROY         = 0x04,
    SA_OPCODE_SPACE_MOVE            = 0x05,
    SA_OPCODE_WINDOW_MOVE           = 0x06,
    SA_OPCODE_WINDOW_OPACITY        = 0x07,
    SA_OPCODE_WINDOW_OPACITY_FADE   = 0x08,
    SA_OPCODE_WINDOW_LAYER          = 0x09,
    SA_OPCODE_WINDOW_STICKY         = 0x0A,
    SA_OPCODE_WINDOW_SHADOW         = 0x0B,
    SA_OPCODE_WINDOW_FOCUS          = 0x0C,
    SA_OPCODE_WINDOW_SCALE          = 0x0D,
    SA_OPCODE_WINDOW_SWAP_PROXY_IN  = 0x0E,
    SA_OPCODE_WINDOW_SWAP_PROXY_OUT = 0x0F,
    SA_OPCODE_WINDOW_ORDER          = 0x10,
    SA_OPCODE_WINDOW_ORDER_IN       = 0x11,
    SA_OPCODE_WINDOW_LIST_TO_SPACE  = 0x12,
    SA_OPCODE_WINDOW_TO_SPACE       = 0x13,
};

#endif
