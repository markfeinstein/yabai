static size_t daemon_test_make_response(char *wire, size_t capacity, const char *payload,
                                        size_t payload_length, bool failed)
{
    size_t wire_length = sizeof(struct daemon_response_frame) + payload_length;
    if (!wire || wire_length > capacity || payload_length > DAEMON_RESPONSE_MAX_BYTES) return 0;
    struct daemon_response_frame frame = {
        .magic = DAEMON_RESPONSE_FRAME_MAGIC,
        .length = (uint32_t) payload_length,
        .failed = failed ? 1 : 0
    };
    memcpy(wire, &frame, sizeof(frame));
    if (payload_length) memcpy(wire + sizeof(frame), payload, payload_length);
    return wire_length;
}

static bool daemon_test_read_response(int fd, char *response, size_t capacity,
                                      size_t *length, bool *failed)
{
    struct daemon_response_frame frame = {0};
    if (!socket_io_read_exact_until(fd, &frame, sizeof(frame), socket_io_deadline_after_ms(1000)) ||
        !daemon_response_frame_is_valid(&frame) || frame.length > capacity) return false;
    if (frame.length &&
        !socket_io_read_exact_until(fd, response, frame.length, socket_io_deadline_after_ms(1000))) return false;
    if (length) *length = frame.length;
    if (failed) *failed = frame.failed;
    return true;
}

TEST_FUNC(daemon_message_signed_boundaries_and_suffixes,
{
    char valid[] = { 'x', '\0', '\0' };
    char bad[] = { 'x', '\0', 'x' };
    char interior_empty[] = { 'x', '\0', '\0', 'y', '\0', '\0' };
    TEST_CHECK(daemon_message_length_is_valid(INT_MIN), false);
    TEST_CHECK(daemon_message_length_is_valid(-1), false);
    TEST_CHECK(daemon_message_length_is_valid(0), false);
    TEST_CHECK(daemon_message_length_is_valid(1), false);
    TEST_CHECK(daemon_message_length_is_valid(2), true);
    TEST_CHECK(daemon_message_length_is_valid(DAEMON_MESSAGE_MAX_LENGTH), true);
    TEST_CHECK(daemon_message_length_is_valid(DAEMON_MESSAGE_MAX_LENGTH + 1), false);
    TEST_CHECK(daemon_message_length_is_valid(INT_MAX), false);
    TEST_CHECK(daemon_message_body_is_valid(NULL, 2), false);
    TEST_CHECK(daemon_message_body_is_valid(valid, 1), false);
    TEST_CHECK(daemon_message_body_is_valid(valid, sizeof(valid)), true);
    TEST_CHECK(daemon_message_body_is_valid(bad, sizeof(bad)), false);
    TEST_CHECK(daemon_message_body_is_valid(interior_empty, sizeof(interior_empty)), false);
});

TEST_FUNC(daemon_message_incomplete_frame_has_failure_response,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        static const char expected[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                         'i','n','c','o','m','p','l','e','t','e',' ','m','e','s','s','a','g','e','.','\n' };
        char response[sizeof(expected)] = { 0 };
        size_t response_length = 0;
        bool failed = false;
        daemon_message_send_incomplete_until(fd[0], socket_io_deadline_after_ms(500));
        TEST_CHECK(daemon_test_read_response(fd[1], response, sizeof(response), &response_length, &failed), true);
        TEST_CHECK(failed, true);
        TEST_CHECK(response_length, sizeof(expected));
        TEST_CHECK(memcmp(response, expected, sizeof(expected)), 0);
        close(fd[0]);
        close(fd[1]);
    }
});

TEST_FUNC(daemon_message_complete_invalid_frames_have_failure_responses,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        char response[64] = {};
        size_t length = 0;
        bool failed = false;
        daemon_message_send_invalid_length(fd[0]);
        TEST_CHECK(daemon_test_read_response(fd[1], response, sizeof(response), &length, &failed), true);
        TEST_CHECK(failed, true);
        TEST_CHECK(length > 1, true);
        TEST_CHECK(response[0], DAEMON_MESSAGE_FAILURE_MARKER);
        TEST_CHECK(strstr(response + 1, "invalid message length") != NULL, true);
        close(fd[0]);
        close(fd[1]);
    }

    fd[0] = fd[1] = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        char response[64] = {};
        size_t length = 0;
        bool failed = false;
        daemon_message_send_invalid_body(fd[0]);
        TEST_CHECK(daemon_test_read_response(fd[1], response, sizeof(response), &length, &failed), true);
        TEST_CHECK(failed, true);
        TEST_CHECK(length > 1, true);
        TEST_CHECK(response[0], DAEMON_MESSAGE_FAILURE_MARKER);
        TEST_CHECK(strstr(response + 1, "invalid message body") != NULL, true);
        close(fd[0]);
        close(fd[1]);
    }

    fd[0] = fd[1] = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        char response[64] = {};
        size_t length = 0;
        bool failed = false;
        daemon_message_send_overloaded(fd[0]);
        TEST_CHECK(daemon_test_read_response(fd[1], response, sizeof(response), &length, &failed), true);
        TEST_CHECK(failed, true);
        TEST_CHECK(length > 1, true);
        TEST_CHECK(response[0], DAEMON_MESSAGE_FAILURE_MARKER);
        TEST_CHECK(strstr(response + 1, "too many pending messages") != NULL, true);
        close(fd[0]);
        close(fd[1]);
    }
});

TEST_FUNC(daemon_response_buffer_preserves_exact_bytes,
{
    static const char expected[] = { DAEMON_MESSAGE_FAILURE_MARKER, 'a', '\0', 'b', '\n' };
    struct daemon_response_buffer response = {};

    TEST_CHECK(daemon_response_buffer_open(&response), true);
    if (response.stream) {
        TEST_CHECK(fwrite(expected, 1, sizeof(expected), response.stream), sizeof(expected));
        TEST_CHECK(daemon_response_buffer_finalize(&response), true);
        TEST_CHECK(response.length, sizeof(expected));
        TEST_CHECK(memcmp(response.bytes, expected, sizeof(expected)), 0);
    }

    daemon_response_buffer_destroy(&response);
});

TEST_FUNC(daemon_response_buffer_accepts_empty_and_boundary_responses,
{
    struct daemon_response_buffer response = {};
    TEST_CHECK(daemon_response_buffer_open(&response), true);
    if (response.stream) {
        TEST_CHECK(daemon_response_buffer_finalize(&response), true);
        TEST_CHECK(response.length, 0U);
    }
    daemon_response_buffer_destroy(&response);

    TEST_CHECK(daemon_response_buffer_open(&response), true);
    if (response.stream) {
        TEST_CHECK(fseek(response.stream, (long) DAEMON_RESPONSE_MAX_BYTES - 1, SEEK_SET), 0);
        TEST_CHECK(fputc('x', response.stream), 'x');
        TEST_CHECK(daemon_response_buffer_finalize(&response), true);
        TEST_CHECK(response.length, (size_t) DAEMON_RESPONSE_MAX_BYTES);
        TEST_CHECK(response.bytes[DAEMON_RESPONSE_MAX_BYTES - 1], 'x');
    }
    daemon_response_buffer_destroy(&response);
});

TEST_FUNC(daemon_response_buffer_overflow_selects_failure_response,
{
    static const char expected[] = { DAEMON_MESSAGE_FAILURE_MARKER,
                                     'f','a','i','l','e','d',' ','t','o',' ','b','u','f','f','e','r',' ','r','e','s','p','o','n','s','e','.','\n' };
    int fd[2] = { -1, -1 };
    struct daemon_response_buffer response = {};

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0 ||
        !daemon_response_buffer_open(&response)) {
        result = false;
    } else {
        char actual[sizeof(expected)] = {};
        size_t actual_length = 0;
        bool failed = false;
        TEST_CHECK(fseek(response.stream, (long) DAEMON_RESPONSE_MAX_BYTES, SEEK_SET), 0);
        (void) fputc('x', response.stream);
        TEST_CHECK(fflush(response.stream) != 0, true);
        TEST_CHECK(ferror(response.stream) != 0, true);
        TEST_CHECK(daemon_response_buffer_send_until(fd[0], &response,
                                                     socket_io_deadline_after_ms(500)), false);
        TEST_CHECK(response.length, 0U);
        TEST_CHECK(shutdown(fd[0], SHUT_WR), 0);
        TEST_CHECK(daemon_test_read_response(fd[1], actual, sizeof(actual), &actual_length, &failed), true);
        TEST_CHECK(failed, true);
        TEST_CHECK(actual_length, sizeof(expected));
        TEST_CHECK(memcmp(actual, expected, sizeof(expected)), 0);
        TEST_CHECK(recv(fd[1], actual, 1, 0), 0);
    }

    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    daemon_response_buffer_destroy(&response);
});

struct socket_io_test_write {
    int fd;
    const char *bytes;
    size_t length;
    useconds_t initial_delay_us;
    useconds_t delay_us;
    bool succeeded;
};

static void *socket_io_test_fragment_writer(void *context)
{
    struct socket_io_test_write *write_context = context;
    write_context->succeeded = true;
    usleep(write_context->initial_delay_us);
    for (size_t i = 0; i < write_context->length; ++i) {
        if (write(write_context->fd, write_context->bytes + i, 1) != 1) {
            write_context->succeeded = false;
            break;
        }
        usleep(write_context->delay_us);
    }
    close(write_context->fd);
    return NULL;
}

static void socket_io_test_signal_handler(int signal)
{
    (void) signal;
}

struct socket_io_test_interrupt {
    pthread_t target;
    int fd;
};

static void *socket_io_test_interrupt_then_write(void *data)
{
    struct socket_io_test_interrupt *context = data;
    usleep(10000);
    pthread_kill(context->target, SIGUSR1);
    usleep(10000);
    (void) write(context->fd, "x", 1);
    return NULL;
}

TEST_FUNC(socket_io_fragmented_exact_read,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        char input[] = "fragmented";
        char output[sizeof(input)] = {};
        struct socket_io_test_write context = { fd[1], input, sizeof(input), 0, 1000, false };
        pthread_t writer;
        if (pthread_create(&writer, NULL, socket_io_test_fragment_writer, &context) != 0) {
            result = false;
            close(fd[0]);
            close(fd[1]);
        } else {
            TEST_CHECK(socket_io_read_exact_until(fd[0], output, sizeof(output), socket_io_deadline_after_ms(500)), true);
            TEST_CHECK(memcmp(input, output, sizeof(input)), 0);
            pthread_join(writer, NULL);
            TEST_CHECK(context.succeeded, true);
            close(fd[0]);
        }
    }
});

TEST_FUNC(socket_io_read_retries_interrupted_poll,
{
    int fd[2] = { -1, -1 };
    struct sigaction action = { .sa_handler = socket_io_test_signal_handler }, previous;
    sigemptyset(&action.sa_mask);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0 || sigaction(SIGUSR1, &action, &previous) != 0) {
        result = false;
    } else {
        struct socket_io_test_interrupt context = { pthread_self(), fd[1] };
        pthread_t interrupter;
        if (pthread_create(&interrupter, NULL, socket_io_test_interrupt_then_write, &context) != 0) {
            result = false;
        } else {
            char value = 0;
            TEST_CHECK(socket_io_read_exact_until(fd[0], &value, 1, socket_io_deadline_after_ms(500)), true);
            TEST_CHECK(value, 'x');
            pthread_join(interrupter, NULL);
        }
        sigaction(SIGUSR1, &previous, NULL);
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
});

struct socket_io_test_read {
    int fd;
    char *bytes;
    size_t capacity;
    size_t length;
};

static void *socket_io_test_fragment_reader(void *context)
{
    struct socket_io_test_read *read_context = context;
    while (read_context->length < read_context->capacity) {
        size_t remaining = read_context->capacity - read_context->length;
        size_t fragment = remaining < 257 ? remaining : 257;
        ssize_t length = read(read_context->fd, read_context->bytes + read_context->length, fragment);
        if (length <= 0) break;
        read_context->length += (size_t) length;
        usleep(100);
    }
    return NULL;
}

TEST_FUNC(socket_io_write_all_delivers_fragmented_buffer,
{
    int fd[2] = { -1, -1 };
    const size_t length = 512 * 1024;
    char *input = malloc(length);
    char *output = calloc(length, 1);
    if (!input || !output || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        for (size_t i = 0; i < length; ++i) input[i] = (char) i;
        int send_buffer = 1024;
        setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
        TEST_CHECK((fcntl(fd[0], F_GETFL) & O_NONBLOCK) != 0, false);

        struct socket_io_test_read context = { fd[1], output, length, 0 };
        pthread_t reader;
        if (pthread_create(&reader, NULL, socket_io_test_fragment_reader, &context) != 0) {
            result = false;
        } else {
            TEST_CHECK(socket_io_write_all_until(fd[0], input, length, socket_io_deadline_after_ms(5000)), true);
            shutdown(fd[0], SHUT_WR);
            pthread_join(reader, NULL);
            TEST_CHECK(context.length, length);
            TEST_CHECK(memcmp(input, output, length), 0);
            TEST_CHECK((fcntl(fd[0], F_GETFL) & O_NONBLOCK) != 0, false);
        }
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    free(input);
    free(output);
});

TEST_FUNC(daemon_response_large_buffer_reaches_draining_peer,
{
    int fd[2] = { -1, -1 };
    const size_t length = 512 * 1024;
    const size_t wire_length = sizeof(struct daemon_response_frame) + length;
    char *input = malloc(length);
    char *output = calloc(wire_length, 1);
    struct daemon_response_buffer response = {};
    if (!input || !output || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0 ||
        !daemon_response_buffer_open(&response)) {
        result = false;
    } else {
        for (size_t i = 0; i < length; ++i) input[i] = (char) i;
        TEST_CHECK(fwrite(input, 1, length, response.stream), length);
        TEST_CHECK(daemon_response_buffer_finalize(&response), true);

        int send_buffer = 1024;
        setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
        struct socket_io_test_read context = { fd[1], output, wire_length, 0 };
        pthread_t reader;
        if (pthread_create(&reader, NULL, socket_io_test_fragment_reader, &context) != 0) {
            result = false;
        } else {
            TEST_CHECK(daemon_response_buffer_write_until(fd[0], &response,
                                                          socket_io_deadline_after_ms(5000)), true);
            shutdown(fd[0], SHUT_WR);
            pthread_join(reader, NULL);
            TEST_CHECK(context.length, wire_length);
            struct daemon_response_frame frame = {0};
            memcpy(&frame, output, sizeof(frame));
            TEST_CHECK(daemon_response_frame_is_valid(&frame), true);
            TEST_CHECK(frame.failed, 0);
            TEST_CHECK(frame.length, length);
            TEST_CHECK(memcmp(input, output + sizeof(frame), length), 0);
        }
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    daemon_response_buffer_destroy(&response);
    free(input);
    free(output);
});

TEST_FUNC(daemon_response_stalled_peer_has_bounded_write_failure,
{
    int fd[2] = { -1, -1 };
    const size_t length = 4 * 1024 * 1024;
    char *input = malloc(length);
    struct daemon_response_buffer response = {};
    if (!input || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0 ||
        !daemon_response_buffer_open(&response)) {
        result = false;
    } else {
        memset(input, 'x', length);
        TEST_CHECK(fwrite(input, 1, length, response.stream), length);
        TEST_CHECK(daemon_response_buffer_finalize(&response), true);
        int send_buffer = 1024;
        setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));

        uint64_t begin = socket_io_monotonic_ns();
        TEST_CHECK(daemon_response_buffer_write_until(fd[0], &response,
                                                      socket_io_deadline_after_ms(20)), false);
        uint64_t elapsed_ms = (socket_io_monotonic_ns() - begin) / 1000000ULL;
        TEST_CHECK(elapsed_ms < 5000, true);
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    daemon_response_buffer_destroy(&response);
    free(input);
});

TEST_FUNC(socket_io_write_all_times_out_with_blocking_descriptor_and_stalled_peer,
{
    int fd[2] = { -1, -1 };
    const size_t length = 4 * 1024 * 1024;
    char *input = malloc(length);
    if (!input || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        memset(input, 'x', length);
        int send_buffer = 1024;
        setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
        TEST_CHECK((fcntl(fd[0], F_GETFL) & O_NONBLOCK) != 0, false);

        uint64_t begin = socket_io_monotonic_ns();
        TEST_CHECK(socket_io_write_all_until(fd[0], input, length, socket_io_deadline_after_ms(20)), false);
        uint64_t elapsed_ms = (socket_io_monotonic_ns() - begin) / 1000000ULL;
        TEST_CHECK(elapsed_ms < 5000, true);
        TEST_CHECK((fcntl(fd[0], F_GETFL) & O_NONBLOCK) != 0, false);
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    free(input);
});

TEST_FUNC(socket_io_write_all_rejects_peer_close_without_sigpipe,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        int no_sigpipe = 1;
        TEST_CHECK(setsockopt(fd[0], SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)), 0);
        close(fd[1]);
        fd[1] = -1;
        TEST_CHECK(socket_io_write_all_until(fd[0], "x", 1, socket_io_deadline_after_ms(50)), false);
        close(fd[0]);
        fd[0] = -1;
    }
});

TEST_FUNC(socket_io_rejects_early_eof_and_total_timeout,
{
    int fd[2] = { -1, -1 };
    char value[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        write(fd[1], "x", 1);
        close(fd[1]);
        TEST_CHECK(socket_io_read_exact_until(fd[0], value, sizeof(value), socket_io_deadline_after_ms(50)), false);
        close(fd[0]);
    }

    fd[0] = fd[1] = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        uint64_t begin = socket_io_monotonic_ns();
        TEST_CHECK(socket_io_read_exact_until(fd[0], value, 1, socket_io_deadline_after_ms(10)), false);
        uint64_t elapsed_ms = (socket_io_monotonic_ns() - begin) / 1000000ULL;
        TEST_CHECK(elapsed_ms < 5000, true);
        close(fd[0]);
        close(fd[1]);
    }
});

TEST_FUNC(client_response_stalled_peer_times_out,
{
    int fd[2] = { -1, -1 };
    FILE *standard_output = tmpfile();
    FILE *standard_error = tmpfile();
    if (!standard_output || !standard_error || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        uint64_t begin = socket_io_monotonic_ns();
        TEST_CHECK(client_read_response_fd(fd[0], standard_output, standard_error, 20), EXIT_FAILURE);
        uint64_t elapsed_ms = (socket_io_monotonic_ns() - begin) / 1000000ULL;
        TEST_CHECK(elapsed_ms < 5000, true);
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    if (standard_output) fclose(standard_output);
    if (standard_error) fclose(standard_error);
});

TEST_FUNC(client_response_fragmented_stream_refreshes_inactivity_deadline,
{
    int fd[2] = { -1, -1 };
    FILE *standard_output = tmpfile();
    FILE *standard_error = tmpfile();
    const char response[] = "streamed response over many fragments";
    char wire[sizeof(struct daemon_response_frame) + sizeof(response) - 1];
    size_t wire_length = daemon_test_make_response(wire, sizeof(wire), response, sizeof(response) - 1, false);
    if (!standard_output || !standard_error || !wire_length || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        struct socket_io_test_write context = { fd[1], wire, wire_length, 0, 10000, false };
        pthread_t writer;
        if (pthread_create(&writer, NULL, socket_io_test_fragment_writer, &context) != 0) {
            result = false;
        } else {
            fd[1] = -1;
            TEST_CHECK(client_read_response_fd(fd[0], standard_output, standard_error, 250), EXIT_SUCCESS);
            pthread_join(writer, NULL);
            TEST_CHECK(context.succeeded, true);

            char output[sizeof(response)] = {};
            rewind(standard_output);
            TEST_CHECK(fread(output, 1, sizeof(output)-1, standard_output), sizeof(output)-1);
            TEST_CHECK(memcmp(output, response, sizeof(response)-1), 0);
        }
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    if (standard_output) fclose(standard_output);
    if (standard_error) fclose(standard_error);
});

TEST_FUNC(client_response_fragmented_failure_routes_everything_to_stderr,
{
    int fd[2] = { -1, -1 };
    FILE *standard_output = tmpfile();
    FILE *standard_error = tmpfile();
    const char response[] = "fragmented failure";
    char payload[1 + sizeof(response) - 1];
    payload[0] = FAILURE_MESSAGE[0];
    memcpy(payload + 1, response, sizeof(response) - 1);
    char wire[sizeof(struct daemon_response_frame) + sizeof(payload)];
    size_t wire_length = daemon_test_make_response(wire, sizeof(wire), payload, sizeof(payload), true);
    if (!standard_output || !standard_error || !wire_length || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        struct socket_io_test_write context = { fd[1], wire, wire_length, 100000, 1000, false };
        pthread_t writer;
        if (pthread_create(&writer, NULL, socket_io_test_fragment_writer, &context) != 0) {
            result = false;
        } else {
            fd[1] = -1;
            TEST_CHECK(client_read_response_fd(fd[0], standard_output, standard_error, 1000), EXIT_FAILURE);
            pthread_join(writer, NULL);
            TEST_CHECK(context.succeeded, true);

            char error[sizeof(response)] = {};
            TEST_CHECK(fseek(standard_error, 0, SEEK_END), 0);
            TEST_CHECK(ftell(standard_error), (long) sizeof(response)-1);
            rewind(standard_error);
            TEST_CHECK(fread(error, 1, sizeof(error)-1, standard_error), sizeof(error)-1);
            TEST_CHECK(memcmp(error, response, sizeof(response)-1), 0);
            TEST_CHECK(fseek(standard_output, 0, SEEK_END), 0);
            TEST_CHECK(ftell(standard_output), 0L);
        }
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    if (standard_output) fclose(standard_output);
    if (standard_error) fclose(standard_error);
});

TEST_FUNC(client_response_later_failure_marker_and_embedded_nul_remain_stdout,
{
    int fd[2] = { -1, -1 };
    FILE *standard_output = tmpfile();
    FILE *standard_error = tmpfile();
    const char expected[] = { 's', 'u', 'c', 'c', 'e', 's', 's', ':',
                              FAILURE_MESSAGE[0], 'x', '\0', 'y' };
    char wire[sizeof(struct daemon_response_frame) + sizeof(expected)];
    size_t wire_length = daemon_test_make_response(wire, sizeof(wire), expected, sizeof(expected), false);
    if (!standard_output || !standard_error || !wire_length || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        struct socket_io_test_write context = { fd[1], wire, wire_length, 0, 1000, false };
        pthread_t writer;
        if (pthread_create(&writer, NULL, socket_io_test_fragment_writer, &context) != 0) {
            result = false;
        } else {
            fd[1] = -1;
            TEST_CHECK(client_read_response_fd(fd[0], standard_output, standard_error, 1000), EXIT_SUCCESS);
            pthread_join(writer, NULL);
            TEST_CHECK(context.succeeded, true);

            char output[sizeof(expected)] = {};
            TEST_CHECK(fseek(standard_output, 0, SEEK_END), 0);
            TEST_CHECK(ftell(standard_output), (long) sizeof(expected));
            rewind(standard_output);
            TEST_CHECK(fread(output, 1, sizeof(output), standard_output), sizeof(output));
            TEST_CHECK(memcmp(output, expected, sizeof(expected)), 0);
            TEST_CHECK(fseek(standard_error, 0, SEEK_END), 0);
            TEST_CHECK(ftell(standard_error), 0L);
        }
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    if (standard_output) fclose(standard_output);
    if (standard_error) fclose(standard_error);
});

TEST_FUNC(client_response_requires_complete_frame_and_payload,
{
    int fd[2] = { -1, -1 };
    FILE *standard_output = tmpfile();
    FILE *standard_error = tmpfile();
    if (!standard_output || !standard_error || socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        close(fd[1]);
        fd[1] = -1;
        TEST_CHECK(client_read_response_fd(fd[0], standard_output, standard_error, 50), EXIT_FAILURE);
    }
    if (fd[0] != -1) close(fd[0]);

    fd[0] = fd[1] = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        struct daemon_response_frame frame = {
            .magic = DAEMON_RESPONSE_FRAME_MAGIC,
            .length = 5
        };
        TEST_CHECK(write(fd[1], &frame, sizeof(frame)), (ssize_t) sizeof(frame));
        TEST_CHECK(write(fd[1], "ab", 2), 2);
        close(fd[1]);
        fd[1] = -1;
        TEST_CHECK(client_read_response_fd(fd[0], standard_output, standard_error, 50), EXIT_FAILURE);
    }
    if (fd[0] != -1) close(fd[0]);

    fd[0] = fd[1] = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        struct daemon_response_frame frame = { .magic = DAEMON_RESPONSE_FRAME_MAGIC };
        TEST_CHECK(write(fd[1], &frame, sizeof(frame)), (ssize_t) sizeof(frame));
        TEST_CHECK(client_read_response_fd(fd[0], standard_output, standard_error, 50), EXIT_SUCCESS);
    }
    if (fd[0] != -1) close(fd[0]);
    if (fd[1] != -1) close(fd[1]);
    if (standard_output) fclose(standard_output);
    if (standard_error) fclose(standard_error);
});

struct sa_handshake_fragment_context {
    int fd;
    const char *response;
    size_t response_length;
    bool request_valid;
};

static void *sa_handshake_fragment_writer(void *context)
{
    struct sa_handshake_fragment_context *writer = context;
    char request[3];
    const char expected[] = { 0x01, 0x00, SA_OPCODE_HANDSHAKE };
    writer->request_valid = socket_io_read_exact_until(writer->fd, request, sizeof(request),
                                                        socket_io_deadline_after_ms(500));
    writer->request_valid = writer->request_valid && memcmp(request, expected, sizeof(expected)) == 0;
    for (size_t i = 0; i < writer->response_length; ++i) {
        if (write(writer->fd, writer->response + i, 1) != 1) break;
        usleep(1000);
    }
    close(writer->fd);
    return NULL;
}

TEST_FUNC(sa_handshake_accepts_fragmented_version_and_attributes,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        const uint32_t expected_attrib = 0x12345678;
        char response[sizeof("7.1.2") + sizeof(expected_attrib) + 1];
        memcpy(response, "7.1.2", sizeof("7.1.2"));
        memcpy(response + sizeof("7.1.2"), &expected_attrib, sizeof(expected_attrib));
        response[sizeof(response) - 1] = '\n';
        struct sa_handshake_fragment_context context = { fd[1], response, sizeof(response), false };
        pthread_t writer;
        if (pthread_create(&writer, NULL, sa_handshake_fragment_writer, &context) != 0) {
            result = false;
            close(fd[1]);
        } else {
            char version[32] = {};
            uint32_t attrib = 0;
            TEST_CHECK(scripting_addition_handshake_fd(fd[0], version, sizeof(version), &attrib,
                                                       socket_io_deadline_after_ms(500)), true);
            TEST_CHECK(strcmp(version, "7.1.2"), 0);
            TEST_CHECK(attrib, expected_attrib);
            pthread_join(writer, NULL);
            TEST_CHECK(context.request_valid, true);
        }
        close(fd[0]);
    }
});

TEST_FUNC(sa_handshake_accepts_empty_version,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        const uint32_t expected_attrib = 42;
        char response[1 + sizeof(expected_attrib)] = {};
        memcpy(response + 1, &expected_attrib, sizeof(expected_attrib));
        write(fd[1], response, sizeof(response));
        shutdown(fd[1], SHUT_WR);
        char version[4] = { 'x', 'x', 'x', 'x' };
        uint32_t attrib = 0;
        TEST_CHECK(scripting_addition_handshake_fd(fd[0], version, sizeof(version), &attrib,
                                                   socket_io_deadline_after_ms(50)), true);
        TEST_CHECK(version[0], '\0');
        TEST_CHECK(attrib, expected_attrib);
        close(fd[0]);
        close(fd[1]);
    }
});

TEST_FUNC(sa_handshake_rejects_missing_terminator_at_capacity,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        write(fd[1], "abcd", 4);
        shutdown(fd[1], SHUT_WR);
        char version[4] = {};
        uint32_t attrib = 0;
        TEST_CHECK(scripting_addition_handshake_fd(fd[0], version, sizeof(version), &attrib,
                                                   socket_io_deadline_after_ms(50)), false);
        TEST_CHECK(memcmp(version, "abcd", sizeof(version)), 0);
        close(fd[0]);
        close(fd[1]);
    }
});

TEST_FUNC(sa_handshake_rejects_truncated_attributes,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        const char response[] = { '1', '\0', 0x01, 0x02, 0x03 };
        write(fd[1], response, sizeof(response));
        shutdown(fd[1], SHUT_WR);
        char version[4] = {};
        uint32_t attrib = 0;
        TEST_CHECK(scripting_addition_handshake_fd(fd[0], version, sizeof(version), &attrib,
                                                   socket_io_deadline_after_ms(50)), false);
        close(fd[0]);
        close(fd[1]);
    }
});

TEST_FUNC(sa_handshake_rejects_early_eof,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        write(fd[1], "1", 1);
        shutdown(fd[1], SHUT_WR);
        char version[4] = {};
        uint32_t attrib = 0;
        TEST_CHECK(scripting_addition_handshake_fd(fd[0], version, sizeof(version), &attrib,
                                                   socket_io_deadline_after_ms(50)), false);
        close(fd[0]);
        close(fd[1]);
    }
});

TEST_FUNC(sa_handshake_observes_total_timeout,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        char version[4] = {};
        uint32_t attrib = 0;
        uint64_t begin = socket_io_monotonic_ns();
        TEST_CHECK(scripting_addition_handshake_fd(fd[0], version, sizeof(version), &attrib,
                                                   socket_io_deadline_after_ms(10)), false);
        uint64_t elapsed_ms = (socket_io_monotonic_ns() - begin) / 1000000ULL;
        TEST_CHECK(elapsed_ms < 5000, true);
        close(fd[0]);
        close(fd[1]);
    }
});

struct sa_delayed_status_context {
    int fd;
    size_t request_length;
    useconds_t delay_us;
    uint8_t status;
    bool request_read;
    bool status_written;
};

static void *sa_delayed_status_writer(void *data)
{
    struct sa_delayed_status_context *context = data;
    char request[32];
    context->request_read = context->request_length <= sizeof(request) &&
                            socket_io_read_exact_until(context->fd, request, context->request_length,
                                                       socket_io_deadline_after_ms(500));
    usleep(context->delay_us);
    context->status_written = sa_message_send_status(context->fd, context->status != 0);
    return NULL;
}

TEST_FUNC(sa_request_without_status_observes_total_timeout,
{
    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        char request[] = { 0x01, 0x00, SA_OPCODE_SPACE_FOCUS };
        char received[sizeof(request)] = {};
        uint64_t begin = socket_io_monotonic_ns();
        TEST_CHECK(scripting_addition_send_bytes_fd(fd[0], request, sizeof(request),
                                                     socket_io_deadline_after_ms(10), 10), false);
        uint64_t elapsed_ms = (socket_io_monotonic_ns() - begin) / 1000000ULL;
        TEST_CHECK(elapsed_ms < 5000, true);
        TEST_CHECK(read(fd[1], received, sizeof(received)), (ssize_t) sizeof(received));
        TEST_CHECK(memcmp(received, request, sizeof(request)), 0);
        close(fd[0]);
        close(fd[1]);
    }
});

TEST_FUNC(sa_operation_response_has_separate_bounded_budget,
{
    for (uint8_t status = 0; status <= 1; ++status) {
        int fd[2] = { -1, -1 };
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
            result = false;
            break;
        }
        char request[] = { 0x01, 0x00, SA_OPCODE_SPACE_FOCUS };
        struct sa_delayed_status_context context = {
            .fd = fd[1], .request_length = sizeof(request), .delay_us = 1200000,
            .status = status, .request_read = false, .status_written = false
        };
        pthread_t writer;
        if (pthread_create(&writer, NULL, sa_delayed_status_writer, &context) != 0) {
            result = false;
        } else {
            TEST_CHECK(scripting_addition_send_bytes_fd(fd[0], request, sizeof(request),
                                                         socket_io_deadline_after_ms(100), 2000), status != 0);
            pthread_join(writer, NULL);
            TEST_CHECK(context.request_read, true);
            TEST_CHECK(context.status_written, true);
        }
        close(fd[0]);
        close(fd[1]);
    }
});
