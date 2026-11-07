#ifndef DAEMON_RESPONSE_H
#define DAEMON_RESPONSE_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message_frame.h"
#include "misc/socket_io.h"

#define DAEMON_RESPONSE_WRITE_TIMEOUT_MS 30000
#define DAEMON_RESPONSE_INITIAL_BYTES 4096U

#ifndef DAEMON_RESPONSE_ALLOCATE
#define DAEMON_RESPONSE_ALLOCATE(size) malloc(size)
#endif
#ifndef DAEMON_RESPONSE_REALLOC
#define DAEMON_RESPONSE_REALLOC(bytes, size) realloc((bytes), (size))
#endif
#ifndef DAEMON_RESPONSE_FUNOPEN
#define DAEMON_RESPONSE_FUNOPEN(cookie, writefn, seekfn) funopen((cookie), NULL, (writefn), (seekfn), NULL)
#endif

struct daemon_response_buffer
{
    FILE *stream;
    char *bytes;
    size_t length;
    size_t capacity;
    size_t position;
    bool write_failed;
};

static inline bool daemon_response_buffer_reserve(struct daemon_response_buffer *response, size_t needed)
{
    if (needed <= response->capacity) return true;
    if (needed > DAEMON_RESPONSE_MAX_BYTES) return false;

    size_t capacity = response->capacity ? response->capacity : DAEMON_RESPONSE_INITIAL_BYTES;
    while (capacity < needed) {
        if (capacity > DAEMON_RESPONSE_MAX_BYTES / 2) {
            capacity = DAEMON_RESPONSE_MAX_BYTES;
        } else {
            capacity *= 2;
        }
    }

    char *bytes = DAEMON_RESPONSE_REALLOC(response->bytes, capacity);
    if (!bytes) return false;
    response->bytes = bytes;
    response->capacity = capacity;
    return true;
}

static inline int daemon_response_buffer_write(void *cookie, const char *bytes, int length)
{
    struct daemon_response_buffer *response = cookie;
    if (!response || !bytes || length < 0) {
        errno = EINVAL;
        return -1;
    }
    if (length == 0) return 0;

    size_t count = (size_t) length;
    if (response->position > DAEMON_RESPONSE_MAX_BYTES ||
        count > DAEMON_RESPONSE_MAX_BYTES - response->position) {
        response->write_failed = true;
        errno = EFBIG;
        return -1;
    }

    size_t end = response->position + count;
    if (!daemon_response_buffer_reserve(response, end)) {
        response->write_failed = true;
        errno = ENOMEM;
        return -1;
    }
    if (response->position > response->length) {
        memset(response->bytes + response->length, 0, response->position - response->length);
    }
    memcpy(response->bytes + response->position, bytes, count);
    response->position = end;
    if (response->length < end) response->length = end;
    return length;
}

static inline fpos_t daemon_response_buffer_seek(void *cookie, fpos_t offset, int whence)
{
    struct daemon_response_buffer *response = cookie;
    if (!response) {
        errno = EINVAL;
        return (fpos_t) -1;
    }

    fpos_t base;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = (fpos_t) response->position; break;
    case SEEK_END: base = (fpos_t) response->length; break;
    default:
        errno = EINVAL;
        return (fpos_t) -1;
    }

    __int128 position = (__int128) base + (__int128) offset;
    if (position < 0 || position > DAEMON_RESPONSE_MAX_BYTES) {
        errno = EINVAL;
        return (fpos_t) -1;
    }
    response->position = (size_t) position;
    return (fpos_t) response->position;
}

static inline bool daemon_response_buffer_open(struct daemon_response_buffer *response)
{
    *response = (struct daemon_response_buffer) {0};
    response->bytes = DAEMON_RESPONSE_ALLOCATE(DAEMON_RESPONSE_INITIAL_BYTES);
    if (!response->bytes) return false;
    response->capacity = DAEMON_RESPONSE_INITIAL_BYTES;

    response->stream = DAEMON_RESPONSE_FUNOPEN(response, daemon_response_buffer_write,
                                                daemon_response_buffer_seek);
    if (!response->stream) {
        free(response->bytes);
        *response = (struct daemon_response_buffer) {0};
        return false;
    }

    return true;
}

static inline bool daemon_response_buffer_finalize(struct daemon_response_buffer *response)
{
    if (!response->stream) return false;

    bool result = !response->write_failed && ferror(response->stream) == 0;
    if (fflush(response->stream) != 0) result = false;
    if (response->write_failed || ferror(response->stream) != 0 ||
        response->length > DAEMON_RESPONSE_MAX_BYTES) result = false;

    if (fclose(response->stream) != 0) result = false;
    response->stream = NULL;
    if (!result) response->length = 0;
    return result;
}

static inline bool daemon_response_buffer_write_until(int fd, const struct daemon_response_buffer *response,
                                                      uint64_t deadline)
{
    bool failed = response->length && response->bytes[0] == DAEMON_MESSAGE_FAILURE_MARKER;
    return daemon_response_write_frame_until(fd, response->bytes, response->length, failed, deadline);
}

static inline void daemon_response_buffer_destroy(struct daemon_response_buffer *response)
{
    if (response->stream) fclose(response->stream);
    free(response->bytes);
    *response = (struct daemon_response_buffer) {0};
}

static inline void daemon_response_send_buffer_failure_until(int fd, uint64_t deadline)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'f','a','i','l','e','d',' ','t','o',' ','b','u','f','f','e','r',' ','r','e','s','p','o','n','s','e','.','\n' };
    (void) daemon_response_write_frame_until(fd, response, sizeof(response), true, deadline);
}

static inline bool daemon_response_buffer_send_until(int fd, struct daemon_response_buffer *response,
                                                     uint64_t deadline)
{
    if (!daemon_response_buffer_finalize(response)) {
        daemon_response_send_buffer_failure_until(fd, deadline);
        return false;
    }

    return daemon_response_buffer_write_until(fd, response, deadline);
}

#endif
