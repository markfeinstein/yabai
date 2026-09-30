#ifndef MESSAGE_FRAME_H
#define MESSAGE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>

#define DAEMON_MESSAGE_FAILURE_MARKER '\x07'

#define DAEMON_MESSAGE_MAX_LENGTH (1024 * 1024)

static inline bool daemon_message_length_is_valid(int length)
{
    return length >= 2 && length <= DAEMON_MESSAGE_MAX_LENGTH;
}

static inline bool daemon_message_body_is_valid(const char *body, size_t length)
{
    return body && length >= 2 &&
           body[length - 2] == '\0' && body[length - 1] == '\0';
}

static inline void daemon_message_send_invalid_length(int fd)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'i','n','v','a','l','i','d',' ','m','e','s','s','a','g','e',' ','l','e','n','g','t','h','.','\n' };
    (void) send(fd, response, sizeof(response), 0);
}

static inline void daemon_message_send_invalid_body(int fd)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'i','n','v','a','l','i','d',' ','m','e','s','s','a','g','e',' ','b','o','d','y','.','\n' };
    (void) send(fd, response, sizeof(response), 0);
}

static inline void daemon_message_send_overloaded(int fd)
{
    static const char response[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     't','o','o',' ','m','a','n','y',' ','p','e','n','d','i','n','g',' ','m','e','s','s','a','g','e','s','.','\n' };
    (void) send(fd, response, sizeof(response), 0);
}

#endif
