#ifndef SA_MESSAGE_VALIDATION_H
#define SA_MESSAGE_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include "common.h"

static inline bool sa_message_length_is_valid(int16_t length)
{
    return length > 0 && length < SA_SOCKET_BUFF_LEN;
}

static inline void sa_message_send_failure_status(int fd)
{
    uint8_t status = 0;
    (void) send(fd, &status, sizeof(status), 0);
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
    if (op == SA_OPCODE_WINDOW_OPACITY_FADE) {
        if (length != 13) return false;
        float duration;
        memcpy(&duration, bytes + 9, sizeof(duration));
        return sa_opacity_duration_is_safe(duration);
    }
    if (sa_message_fixed_length(op, length)) return true;

    struct sa_message_cursor cursor = { bytes + 1, bytes + length };
    int32_t count;
    uint32_t wid;
    uint64_t sid;

    switch (op) {
    case SA_OPCODE_WINDOW_SWAP_PROXY_IN:
    case SA_OPCODE_WINDOW_SWAP_PROXY_OUT:
        if (!sa_message_read(&cursor, &count, sizeof(count)) || count < 0 || count > 1022) return false;
        for (int32_t i = 0; i < count; ++i) {
            if (!sa_message_read(&cursor, &wid, sizeof(wid))) return false;
            if (wid && !sa_message_read(&cursor, &wid, sizeof(wid))) return false;
        }
        break;
    case SA_OPCODE_WINDOW_ORDER_IN:
        if (!sa_message_read(&cursor, &count, sizeof(count)) || count < 0 || count > 1022) return false;
        for (int32_t i = 0; i < count; ++i) {
            if (!sa_message_read(&cursor, &wid, sizeof(wid))) return false;
        }
        break;
    case SA_OPCODE_WINDOW_LIST_TO_SPACE:
        if (!sa_message_read(&cursor, &sid, sizeof(sid)) ||
            !sa_message_read(&cursor, &count, sizeof(count)) || count < 0 || count > 1020) return false;
        for (int32_t i = 0; i < count; ++i) {
            if (!sa_message_read(&cursor, &wid, sizeof(wid))) return false;
        }
        break;
    default:
        return false;
    }

    return cursor.at == cursor.end;
}

#endif
