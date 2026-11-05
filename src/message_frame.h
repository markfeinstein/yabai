#ifndef MESSAGE_FRAME_H
#define MESSAGE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "misc/socket_io.h"

#define DAEMON_MESSAGE_FAILURE_MARKER '\x07'

#define DAEMON_MESSAGE_MAX_LENGTH (1024 * 1024)
#define DAEMON_RESPONSE_MAX_BYTES (8U * 1024U * 1024U)
#define DAEMON_RESPONSE_FRAME_MAGIC 0x59425231U

struct daemon_response_frame
{
    uint32_t magic;
    uint32_t length;
    uint8_t failed;
    uint8_t reserved[3];
};
_Static_assert(sizeof(struct daemon_response_frame) == 12, "daemon response frame ABI changed");

static inline bool daemon_response_frame_is_valid(const struct daemon_response_frame *frame)
{
    return frame && frame->magic == DAEMON_RESPONSE_FRAME_MAGIC &&
           frame->length <= DAEMON_RESPONSE_MAX_BYTES && frame->failed <= 1 &&
           frame->reserved[0] == 0 && frame->reserved[1] == 0 && frame->reserved[2] == 0;
}

static inline bool daemon_response_write_frame_until(int fd, const char *bytes, size_t length,
                                                     bool failed, uint64_t deadline)
{
    if (length > DAEMON_RESPONSE_MAX_BYTES) return false;
    struct daemon_response_frame frame = {
        .magic = DAEMON_RESPONSE_FRAME_MAGIC,
        .length = (uint32_t) length,
        .failed = failed ? 1 : 0
    };
    return socket_io_write_all_until(fd, &frame, sizeof(frame), deadline) &&
           (!length || socket_io_write_all_until(fd, bytes, length, deadline));
}

static inline bool daemon_message_length_is_valid(int length)
{
    return length >= 2 && length <= DAEMON_MESSAGE_MAX_LENGTH;
}

static inline bool daemon_message_body_is_valid(const char *body, size_t length)
{
    if (!body || length < 2 || body[length - 2] != '\0' || body[length - 1] != '\0') return false;
    for (size_t i = 0; i + 2 < length; ++i) {
        if (body[i] == '\0' && body[i + 1] == '\0') return false;
    }
    return true;
}

static inline void daemon_message_send_incomplete_until(int fd, uint64_t deadline)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'i','n','c','o','m','p','l','e','t','e',' ','m','e','s','s','a','g','e','.','\n' };
    (void) daemon_response_write_frame_until(fd, response, sizeof(response), true, deadline);
}

static inline void daemon_message_send_invalid_length(int fd)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'i','n','v','a','l','i','d',' ','m','e','s','s','a','g','e',' ','l','e','n','g','t','h','.','\n' };
    (void) daemon_response_write_frame_until(fd, response, sizeof(response), true,
                                              socket_io_deadline_after_ms(30000));
}

static inline void daemon_message_send_invalid_body(int fd)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'i','n','v','a','l','i','d',' ','m','e','s','s','a','g','e',' ','b','o','d','y','.','\n' };
    (void) daemon_response_write_frame_until(fd, response, sizeof(response), true,
                                              socket_io_deadline_after_ms(30000));
}

static inline void daemon_message_send_overloaded(int fd)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     't','o','o',' ','m','a','n','y',' ','p','e','n','d','i','n','g',' ','m','e','s','s','a','g','e','s','.','\n' };
    (void) daemon_response_write_frame_until(fd, response, sizeof(response), true,
                                              socket_io_deadline_after_ms(30000));
}

#endif
