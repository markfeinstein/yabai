TEST_FUNC(sa_payload_builder_boundaries_and_variable_counts,
{
    uint8_t bytes[SA_SOCKET_BUFF_LEN];
    struct sa_payload_builder builder;
    size_t length = 0;
    uint32_t wid = 7;
    uint64_t sid = 9;

    uint8_t exact[8];
    uint8_t exact_value[5] = {};
    TEST_CHECK(sa_payload_builder_init(&builder, exact, sizeof(exact), SA_OPCODE_HANDSHAKE), true);
    TEST_CHECK(sa_payload_builder_append(&builder, exact_value, sizeof(exact_value)), true);
    TEST_CHECK(sa_payload_builder_append(&builder, exact_value, 1), false);
    TEST_CHECK(sa_payload_builder_finish(&builder, &length), true);
    TEST_CHECK(length, sizeof(exact));

    TEST_CHECK(sa_payload_builder_init(&builder, bytes, sizeof(bytes), SA_OPCODE_WINDOW_ORDER_IN), true);
    TEST_CHECK(sa_payload_builder_append_count_max(&builder, 1022, 1022), true);
    for (int i = 0; i < 1022; ++i) TEST_CHECK(sa_payload_builder_append(&builder, &wid, sizeof(wid)), true);
    TEST_CHECK(sa_payload_builder_finish(&builder, &length), true);
    TEST_CHECK(length, (size_t) 4095);
    TEST_CHECK(sa_message_is_valid(bytes + sizeof(int16_t), length - sizeof(int16_t)), true);
    TEST_CHECK(sa_payload_builder_append(&builder, &wid, sizeof(wid)), false);

    TEST_CHECK(sa_payload_builder_init(&builder, bytes, sizeof(bytes), SA_OPCODE_WINDOW_ORDER_IN), true);
    TEST_CHECK(sa_payload_builder_append_count_max(&builder, 1023, 1022), false);

    TEST_CHECK(sa_payload_builder_init(&builder, bytes, sizeof(bytes), SA_OPCODE_WINDOW_LIST_TO_SPACE), true);
    TEST_CHECK(sa_payload_builder_append(&builder, &sid, sizeof(sid)), true);
    TEST_CHECK(sa_payload_builder_append_count_max(&builder, 1020, 1020), true);
    for (int i = 0; i < 1020; ++i) TEST_CHECK(sa_payload_builder_append(&builder, &wid, sizeof(wid)), true);
    TEST_CHECK(sa_payload_builder_finish(&builder, &length), true);
    TEST_CHECK(length, (size_t) 4095);
    TEST_CHECK(sa_message_is_valid(bytes + sizeof(int16_t), length - sizeof(int16_t)), true);

    TEST_CHECK(sa_payload_builder_init(&builder, bytes, sizeof(bytes), SA_OPCODE_WINDOW_LIST_TO_SPACE), true);
    TEST_CHECK(sa_payload_builder_append(&builder, &sid, sizeof(sid)), true);
    TEST_CHECK(sa_payload_builder_append_count_max(&builder, 1021, 1020), false);

    TEST_CHECK(sa_payload_builder_init(&builder, bytes, sizeof(bytes), SA_OPCODE_WINDOW_SWAP_PROXY_IN), true);
    TEST_CHECK(sa_payload_builder_append_count(&builder, 512), true);
    for (int i = 0; i < 510; ++i) TEST_CHECK(sa_payload_builder_append_swap_record(&builder, false, wid, wid + 1), true);
    TEST_CHECK(sa_payload_builder_append_swap_record(&builder, true, wid, wid + 1), true);
    TEST_CHECK(sa_payload_builder_append_swap_record(&builder, true, wid, wid + 1), true);
    TEST_CHECK(sa_payload_builder_finish(&builder, &length), true);
    TEST_CHECK(length, (size_t) 4095);
    TEST_CHECK(sa_message_is_valid(bytes + sizeof(int16_t), length - sizeof(int16_t)), true);

    TEST_CHECK(sa_payload_builder_init(&builder, bytes, sizeof(bytes), SA_OPCODE_WINDOW_SWAP_PROXY_OUT), true);
    TEST_CHECK(sa_payload_builder_append_count(&builder, 512), true);
    for (int i = 0; i < 511; ++i) TEST_CHECK(sa_payload_builder_append_swap_record(&builder, false, wid, wid + 1), true);
    TEST_CHECK(sa_payload_builder_append_swap_record(&builder, true, wid, wid + 1), false);

    TEST_CHECK(sa_payload_builder_init(&builder, bytes, sizeof(bytes), SA_OPCODE_WINDOW_ORDER_IN), true);
    TEST_CHECK(sa_payload_builder_append_count_max(&builder, -1, 1022), false);
    TEST_CHECK(sa_message_length_is_valid(INT16_MIN), false);
    TEST_CHECK(sa_message_length_is_valid(0), false);
    TEST_CHECK(sa_message_length_is_valid(1), true);
    TEST_CHECK(sa_message_length_is_valid(SA_SOCKET_BUFF_LEN - 1), true);

    int fd[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
        result = false;
    } else {
        uint8_t status = 1;
        sa_message_send_failure_status(fd[0]);
        TEST_CHECK(recv(fd[1], &status, sizeof(status), 0), (ssize_t) sizeof(status));
        TEST_CHECK(status, 0);
        close(fd[0]);
        close(fd[1]);
    }
});

static void sa_test_append(void *buffer, size_t *length, const void *value, size_t size)
{
    memcpy((char *) buffer + *length, value, size);
    *length += size;
}

static bool sa_test_strict_shape(const uint8_t *message, size_t length)
{
    if (!sa_message_is_valid(message, length)) return false;
    for (size_t i = 0; i < length; ++i) {
        if (sa_message_is_valid(message, i)) return false;
    }
    uint8_t appended[SA_SOCKET_BUFF_LEN];
    memcpy(appended, message, length);
    appended[length] = 0;
    return !sa_message_is_valid(appended, length + 1);
}

TEST_FUNC(sa_message_fixed_sender_layouts_are_strict,
{
    static const struct { uint8_t op; size_t length; } layouts[] = {
        { SA_OPCODE_HANDSHAKE, 1 },
        { SA_OPCODE_SPACE_FOCUS, 9 },
        { SA_OPCODE_SPACE_CREATE, 9 },
        { SA_OPCODE_SPACE_DESTROY, 9 },
        { SA_OPCODE_SPACE_MOVE, 26 },
        { SA_OPCODE_WINDOW_MOVE, 13 },
        { SA_OPCODE_WINDOW_OPACITY, 13 },
        { SA_OPCODE_WINDOW_OPACITY_FADE, 13 },
        { SA_OPCODE_WINDOW_LAYER, 9 },
        { SA_OPCODE_WINDOW_STICKY, 6 },
        { SA_OPCODE_WINDOW_SHADOW, 6 },
        { SA_OPCODE_WINDOW_FOCUS, 5 },
        { SA_OPCODE_WINDOW_SCALE, 21 },
        { SA_OPCODE_WINDOW_ORDER, 13 },
        { SA_OPCODE_WINDOW_TO_SPACE, 13 },
    };
    uint8_t message[32] = {};
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
        memset(message, 0, sizeof(message));
        message[0] = layouts[i].op;
        TEST_CHECK(sa_test_strict_shape(message, layouts[i].length), true);
    }
    TEST_CHECK(sa_message_is_valid(NULL, 1), false);
    TEST_CHECK(sa_message_is_valid(message, 0), false);
    message[0] = 0xff;
    TEST_CHECK(sa_message_is_valid(message, 1), false);
});

TEST_FUNC(sa_message_swap_sparse_and_mixed_records_are_strict,
{
    for (uint8_t op = SA_OPCODE_WINDOW_SWAP_PROXY_IN; op <= SA_OPCODE_WINDOW_SWAP_PROXY_OUT; ++op) {
        uint8_t message[64] = { op };
        size_t length = 1;
        int32_t count = 3;
        uint32_t zero = 0, wid = 10, proxy = 20, wid2 = 30, proxy2 = 40;
        sa_test_append(message, &length, &count, sizeof(count));
        sa_test_append(message, &length, &zero, sizeof(zero));
        sa_test_append(message, &length, &wid, sizeof(wid));
        sa_test_append(message, &length, &proxy, sizeof(proxy));
        sa_test_append(message, &length, &wid2, sizeof(wid2));
        sa_test_append(message, &length, &proxy2, sizeof(proxy2));
        TEST_CHECK(sa_test_strict_shape(message, length), true);
    }
});

TEST_FUNC(sa_message_variable_counts_and_entries_are_strict,
{
    uint8_t message[64] = {};
    size_t length;
    int32_t count;
    uint32_t wid = 7;
    uint64_t sid = 9;

    message[0] = SA_OPCODE_WINDOW_ORDER_IN;
    length = 1; count = 2;
    sa_test_append(message, &length, &count, sizeof(count));
    sa_test_append(message, &length, &wid, sizeof(wid));
    sa_test_append(message, &length, &wid, sizeof(wid));
    TEST_CHECK(sa_test_strict_shape(message, length), true);

    message[0] = SA_OPCODE_WINDOW_LIST_TO_SPACE;
    length = 1;
    sa_test_append(message, &length, &sid, sizeof(sid));
    sa_test_append(message, &length, &count, sizeof(count));
    sa_test_append(message, &length, &wid, sizeof(wid));
    sa_test_append(message, &length, &wid, sizeof(wid));
    TEST_CHECK(sa_test_strict_shape(message, length), true);

    count = -1;
    uint8_t swap_negative[5] = { SA_OPCODE_WINDOW_SWAP_PROXY_IN };
    memcpy(swap_negative + 1, &count, sizeof(count));
    TEST_CHECK(sa_message_is_valid(swap_negative, sizeof(swap_negative)), false);
    uint8_t order_negative[5] = { SA_OPCODE_WINDOW_ORDER_IN };
    memcpy(order_negative + 1, &count, sizeof(count));
    TEST_CHECK(sa_message_is_valid(order_negative, sizeof(order_negative)), false);
    uint8_t list_negative[14] = { SA_OPCODE_WINDOW_LIST_TO_SPACE };
    memcpy(list_negative + 1, &sid, sizeof(sid));
    memcpy(list_negative + 9, &count, sizeof(count));
    TEST_CHECK(sa_message_is_valid(list_negative, 13), false);

    count = 1023;
    memcpy(swap_negative + 1, &count, sizeof(count));
    memcpy(order_negative + 1, &count, sizeof(count));
    TEST_CHECK(sa_message_is_valid(swap_negative, sizeof(swap_negative)), false);
    TEST_CHECK(sa_message_is_valid(order_negative, sizeof(order_negative)), false);
    count = 1021;
    memcpy(list_negative + 9, &count, sizeof(count));
    TEST_CHECK(sa_message_is_valid(list_negative, 13), false);

    count = 2;
    memcpy(list_negative + 9, &count, sizeof(count));
    TEST_CHECK(sa_message_is_valid(list_negative, 13), false);
    count = 0;
    memcpy(list_negative + 9, &count, sizeof(count));
    list_negative[13] = 0;
    TEST_CHECK(sa_message_is_valid(list_negative, 14), false);
});
