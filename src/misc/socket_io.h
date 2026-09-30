#ifndef SOCKET_IO_H
#define SOCKET_IO_H

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SOCKET_IO_PRODUCTION_TIMEOUT_MS 1000

enum socket_io_read_result {
    SOCKET_IO_READ_DATA,
    SOCKET_IO_READ_EOF,
    SOCKET_IO_READ_TIMEOUT,
    SOCKET_IO_READ_ERROR,
};

static inline uint64_t socket_io_monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t) now.tv_sec * 1000000000ULL + (uint64_t) now.tv_nsec;
}

static inline uint64_t socket_io_deadline_after_ms(uint32_t timeout_ms)
{
    uint64_t now = socket_io_monotonic_ns();
    uint64_t delta = (uint64_t) timeout_ms * 1000000ULL;
    return UINT64_MAX - now < delta ? UINT64_MAX : now + delta;
}

static inline bool socket_io_write_all_until(int fd, const void *buffer, size_t length, uint64_t deadline_ns)
{
    if (!length) return true;

    int send_lowat;
    socklen_t send_lowat_length = sizeof(send_lowat);
    if (getsockopt(fd, SOL_SOCKET, SO_SNDLOWAT, &send_lowat, &send_lowat_length) != 0 || send_lowat <= 0) {
        return false;
    }

    size_t offset = 0;
    while (offset < length) {
        uint64_t now = socket_io_monotonic_ns();
        if (!now || now >= deadline_ns) return false;

        uint64_t remaining_ns = deadline_ns - now;
        uint64_t remaining_ms = remaining_ns / 1000000ULL + (remaining_ns % 1000000ULL != 0);
        int timeout_ms = remaining_ms > INT_MAX ? INT_MAX : (int) remaining_ms;
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        int poll_result = poll(&pfd, 1, timeout_ms);
        if (poll_result < 0 && errno == EINTR) continue;
        if (poll_result <= 0 || (pfd.revents & (POLLERR | POLLNVAL))) return false;

        size_t remaining = length - offset;
        size_t send_length = remaining < (size_t) send_lowat ? remaining : (size_t) send_lowat;
        ssize_t bytes_written = send(fd, (const char *) buffer + offset, send_length, MSG_DONTWAIT);
        if (bytes_written > 0) {
            offset += (size_t) bytes_written;
        } else if (bytes_written == 0) {
            return false;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return false;
        }
    }
    return true;
}

static inline enum socket_io_read_result socket_io_read_some_until(int fd, void *buffer, size_t capacity,
                                                                   uint64_t deadline_ns, size_t *length)
{
    if (!capacity || !length) return SOCKET_IO_READ_ERROR;
    *length = 0;

    for (;;) {
        uint64_t now = socket_io_monotonic_ns();
        if (!now || now >= deadline_ns) return SOCKET_IO_READ_TIMEOUT;

        uint64_t remaining_ns = deadline_ns - now;
        uint64_t remaining_ms = remaining_ns / 1000000ULL + (remaining_ns % 1000000ULL != 0);
        int timeout_ms = remaining_ms > INT_MAX ? INT_MAX : (int) remaining_ms;
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int poll_result = poll(&pfd, 1, timeout_ms);
        if (poll_result < 0 && errno == EINTR) continue;
        if (poll_result < 0 || (pfd.revents & (POLLERR | POLLNVAL))) return SOCKET_IO_READ_ERROR;
        if (poll_result == 0) return SOCKET_IO_READ_TIMEOUT;

        ssize_t bytes_read = recv(fd, buffer, capacity, MSG_DONTWAIT);
        if (bytes_read > 0) {
            *length = (size_t) bytes_read;
            return SOCKET_IO_READ_DATA;
        }
        if (bytes_read == 0) return SOCKET_IO_READ_EOF;
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return SOCKET_IO_READ_ERROR;
    }
}

static inline bool socket_io_read_exact_until(int fd, void *buffer, size_t length, uint64_t deadline_ns)
{
    size_t offset = 0;
    while (offset < length) {
        uint64_t now = socket_io_monotonic_ns();
        if (!now || now >= deadline_ns) return false;

        uint64_t remaining_ns = deadline_ns - now;
        uint64_t remaining_ms = remaining_ns / 1000000ULL + (remaining_ns % 1000000ULL != 0);
        int timeout_ms = remaining_ms > INT_MAX ? INT_MAX : (int) remaining_ms;
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int poll_result = poll(&pfd, 1, timeout_ms);
        if (poll_result < 0 && errno == EINTR) continue;
        if (poll_result <= 0 || (pfd.revents & (POLLERR | POLLNVAL))) return false;

        ssize_t bytes_read = read(fd, (char *) buffer + offset, length - offset);
        if (bytes_read > 0) {
            offset += (size_t) bytes_read;
        } else if (bytes_read == 0) {
            return false;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return false;
        }
    }
    return true;
}

static inline bool socket_io_read_cstring_until(int fd, char *buffer, size_t capacity, uint64_t deadline_ns)
{
    for (size_t offset = 0; offset < capacity; ++offset) {
        if (!socket_io_read_exact_until(fd, buffer + offset, 1, deadline_ns)) return false;
        if (buffer[offset] == '\0') return true;
    }
    return false;
}

#endif
