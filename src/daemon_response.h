#ifndef DAEMON_RESPONSE_H
#define DAEMON_RESPONSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "message_frame.h"
#include "misc/socket_io.h"

#define DAEMON_RESPONSE_WRITE_TIMEOUT_MS 30000

#ifndef DAEMON_RESPONSE_ALLOCATE
#define DAEMON_RESPONSE_ALLOCATE(size) malloc(size)
#endif
#ifndef DAEMON_RESPONSE_FMEMOPEN
#define DAEMON_RESPONSE_FMEMOPEN(bytes, size, mode) fmemopen(bytes, size, mode)
#endif

// Bound daemon response generation so a request cannot consume unbounded memory.
// This is a protocol protection limit, independent of how clients display responses.
#define DAEMON_RESPONSE_MAX_BYTES (8U * 1024U * 1024U)

struct daemon_response_buffer
{
    FILE *stream;
    char *bytes;
    size_t length;
};

static inline bool daemon_response_buffer_open(struct daemon_response_buffer *response)
{
    response->stream = NULL;
    response->bytes = DAEMON_RESPONSE_ALLOCATE(DAEMON_RESPONSE_MAX_BYTES);
    response->length = 0;
    if (!response->bytes) return false;

    response->stream = DAEMON_RESPONSE_FMEMOPEN(response->bytes, DAEMON_RESPONSE_MAX_BYTES, "w");
    if (!response->stream) {
        free(response->bytes);
        response->bytes = NULL;
        return false;
    }

    return true;
}

static inline bool daemon_response_buffer_finalize(struct daemon_response_buffer *response)
{
    if (!response->stream) return false;

    bool result = ferror(response->stream) == 0;
    if (fflush(response->stream) != 0) result = false;
    if (ferror(response->stream) != 0) result = false;

    long position = ftell(response->stream);
    if (position < 0 || (unsigned long) position > DAEMON_RESPONSE_MAX_BYTES) result = false;

    if (fclose(response->stream) != 0) result = false;
    response->stream = NULL;
    response->length = result ? (size_t) position : 0;
    return result;
}

static inline bool daemon_response_buffer_write_until(int fd, const struct daemon_response_buffer *response,
                                                      uint64_t deadline)
{
    return socket_io_write_all_until(fd, response->bytes, response->length, deadline);
}

static inline void daemon_response_buffer_destroy(struct daemon_response_buffer *response)
{
    if (response->stream) fclose(response->stream);
    free(response->bytes);
    response->stream = NULL;
    response->bytes = NULL;
    response->length = 0;
}

static inline void daemon_response_send_buffer_failure_until(int fd, uint64_t deadline)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'f','a','i','l','e','d',' ','t','o',' ','b','u','f','f','e','r',' ','r','e','s','p','o','n','s','e','.','\n' };
    (void) socket_io_write_all_until(fd, response, sizeof(response), deadline);
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
