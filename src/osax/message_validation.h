#ifndef SA_MESSAGE_VALIDATION_H
#define SA_MESSAGE_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "common.h"
#include "../misc/socket_io.h"

static inline bool sa_message_length_is_valid(int16_t length)
{
    return length > 0 && length < SA_SOCKET_BUFF_LEN;
}

static inline bool sa_message_send_status_until(int fd, bool succeeded, uint64_t deadline)
{
    uint8_t status = succeeded ? 1 : 0;
    return socket_io_write_all_until(fd, &status, sizeof(status), deadline);
}

static inline bool sa_message_send_status(int fd, bool succeeded)
{
    return sa_message_send_status_until(fd, succeeded,
                                        socket_io_deadline_after_ms(SOCKET_IO_PRODUCTION_TIMEOUT_MS));
}

static inline bool sa_message_send_failure_status(int fd)
{
    return sa_message_send_status(fd, false);
}

struct sa_message_cursor {
    const uint8_t *at;
    const uint8_t *end;
};

static inline bool sa_message_read(struct sa_message_cursor *cursor, void *value, size_t size)
{
    if ((size_t) (cursor->end - cursor->at) < size) return false;
    memcpy(value, cursor->at, size);
    cursor->at += size;
    return true;
}

static inline bool sa_message_fixed_length(uint8_t op, size_t length)
{
    switch (op) {
    case SA_OPCODE_HANDSHAKE:             return length == 1;
    case SA_OPCODE_SPACE_FOCUS:
    case SA_OPCODE_SPACE_CREATE:
    case SA_OPCODE_SPACE_DESTROY:         return length == 9;
    case SA_OPCODE_SPACE_MOVE:            return length == 26;
    case SA_OPCODE_WINDOW_MOVE:
    case SA_OPCODE_WINDOW_OPACITY:
    case SA_OPCODE_WINDOW_OPACITY_FADE:   return length == 13;
    case SA_OPCODE_WINDOW_LAYER:           return length == 9;
    case SA_OPCODE_WINDOW_STICKY:
    case SA_OPCODE_WINDOW_SHADOW:          return length == 6;
    case SA_OPCODE_WINDOW_FOCUS:           return length == 5;
    case SA_OPCODE_WINDOW_SCALE:           return length == 21;
    case SA_OPCODE_WINDOW_ORDER:           return length == 13;
    case SA_OPCODE_WINDOW_TO_SPACE:        return length == 13;
    default:                               return false;
    }
}

static inline bool sa_message_is_valid(const void *message, size_t length)
{
    if (!message || length == 0 || length >= SA_SOCKET_BUFF_LEN) return false;

    const uint8_t *bytes = message;
    uint8_t op = bytes[0];
    if (!sa_message_fixed_length(op, length)) {
        struct sa_message_cursor cursor = { bytes + 1, bytes + length };
        int32_t count;
        uint32_t wid, proxy_wid;
        uint64_t sid;

        switch (op) {
        case SA_OPCODE_WINDOW_SWAP_PROXY_IN:
        case SA_OPCODE_WINDOW_SWAP_PROXY_OUT:
            if (!sa_message_read(&cursor, &count, sizeof(count)) || count < 0 || count > 1022) return false;
            for (int32_t i = 0; i < count; ++i) {
                if (!sa_message_read(&cursor, &wid, sizeof(wid))) return false;
                if (wid && (!sa_message_read(&cursor, &proxy_wid, sizeof(proxy_wid)) || !proxy_wid)) return false;
            }
            break;
        case SA_OPCODE_WINDOW_ORDER_IN:
            if (!sa_message_read(&cursor, &count, sizeof(count)) || count < 0 || count > 1022) return false;
            for (int32_t i = 0; i < count; ++i) if (!sa_message_read(&cursor, &wid, sizeof(wid))) return false;
            break;
        case SA_OPCODE_WINDOW_LIST_TO_SPACE:
            if (!sa_message_read(&cursor, &sid, sizeof(sid)) || !sid ||
                !sa_message_read(&cursor, &count, sizeof(count)) || count < 0 || count > 1020) return false;
            for (int32_t i = 0; i < count; ++i) {
                if (!sa_message_read(&cursor, &wid, sizeof(wid)) || !wid) return false;
            }
            break;
        default:
            return false;
        }
        return cursor.at == cursor.end;
    }

    struct sa_message_cursor cursor = { bytes + 1, bytes + length };
    uint32_t wid, relative_wid;
    uint64_t sid;
    float opacity, duration, x, y, width, height;
    int32_t integer;
    uint8_t boolean;

    switch (op) {
    case SA_OPCODE_HANDSHAKE:
        break;
    case SA_OPCODE_SPACE_FOCUS:
    case SA_OPCODE_SPACE_CREATE:
    case SA_OPCODE_SPACE_DESTROY:
        return sa_message_read(&cursor, &sid, sizeof(sid)) && sid && cursor.at == cursor.end;
    case SA_OPCODE_SPACE_MOVE: {
        uint64_t destination, previous;
        return sa_message_read(&cursor, &sid, sizeof(sid)) && sid &&
               sa_message_read(&cursor, &destination, sizeof(destination)) && destination &&
               sa_message_read(&cursor, &previous, sizeof(previous)) &&
               sa_message_read(&cursor, &boolean, sizeof(boolean)) && boolean <= 1 &&
               cursor.at == cursor.end;
    }
    case SA_OPCODE_WINDOW_MOVE:
        return sa_message_read(&cursor, &wid, sizeof(wid)) && wid &&
               sa_message_read(&cursor, &integer, sizeof(integer)) &&
               sa_message_read(&cursor, &integer, sizeof(integer)) && cursor.at == cursor.end;
    case SA_OPCODE_WINDOW_OPACITY:
        return sa_message_read(&cursor, &wid, sizeof(wid)) && wid &&
               sa_message_read(&cursor, &opacity, sizeof(opacity)) && sa_opacity_is_safe(opacity) &&
               sa_message_read(&cursor, &duration, sizeof(duration)) && duration == 0.0f &&
               cursor.at == cursor.end;
    case SA_OPCODE_WINDOW_OPACITY_FADE:
        return sa_message_read(&cursor, &wid, sizeof(wid)) && wid &&
               sa_message_read(&cursor, &opacity, sizeof(opacity)) && sa_opacity_is_safe(opacity) &&
               sa_message_read(&cursor, &duration, sizeof(duration)) && duration > 0.0f &&
               sa_opacity_duration_is_safe(duration) && cursor.at == cursor.end;
    case SA_OPCODE_WINDOW_LAYER:
        return sa_message_read(&cursor, &wid, sizeof(wid)) && wid &&
               sa_message_read(&cursor, &integer, sizeof(integer)) && sa_layer_key_is_supported(integer) &&
               cursor.at == cursor.end;
    case SA_OPCODE_WINDOW_STICKY:
    case SA_OPCODE_WINDOW_SHADOW:
        return sa_message_read(&cursor, &wid, sizeof(wid)) && wid &&
               sa_message_read(&cursor, &boolean, sizeof(boolean)) && boolean <= 1 &&
               cursor.at == cursor.end;
    case SA_OPCODE_WINDOW_FOCUS:
        return sa_message_read(&cursor, &wid, sizeof(wid)) && wid && cursor.at == cursor.end;
    case SA_OPCODE_WINDOW_SCALE:
        return sa_message_read(&cursor, &wid, sizeof(wid)) && wid &&
               sa_message_read(&cursor, &x, sizeof(x)) && sa_message_read(&cursor, &y, sizeof(y)) &&
               sa_message_read(&cursor, &width, sizeof(width)) && sa_message_read(&cursor, &height, sizeof(height)) &&
               sa_scale_geometry_is_safe(x, y, width, height) && cursor.at == cursor.end;
    case SA_OPCODE_WINDOW_ORDER:
        return sa_message_read(&cursor, &wid, sizeof(wid)) && wid &&
               sa_message_read(&cursor, &integer, sizeof(integer)) && sa_window_order_is_supported(integer) &&
               sa_message_read(&cursor, &relative_wid, sizeof(relative_wid)) && cursor.at == cursor.end;
    case SA_OPCODE_WINDOW_TO_SPACE:
        return sa_message_read(&cursor, &sid, sizeof(sid)) && sid &&
               sa_message_read(&cursor, &wid, sizeof(wid)) && wid && cursor.at == cursor.end;
    default:
        return false;
    }
    return cursor.at == cursor.end;
}

#endif
