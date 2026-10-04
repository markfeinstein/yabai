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
        TEST_CHECK(sa_message_send_failure_status(fd[0]), true);
        TEST_CHECK(recv(fd[1], &status, sizeof(status), 0), (ssize_t) sizeof(status));
        TEST_CHECK(status, 0);
        close(fd[0]);
        close(fd[1]);
    }
});

static TABLE_HASH_FUNC(sa_test_hash_u32)
{
    uint32_t value;
    memcpy(&value, key, sizeof(value));
    return value;
}

static TABLE_COMPARE_FUNC(sa_test_compare_u32)
{
    uint32_t a, b;
    memcpy(&a, key_a, sizeof(a));
    memcpy(&b, key_b, sizeof(b));
    return a == b;
}

TEST_FUNC(sa_legacy_hashtable_init_fails_stop_on_allocation_failure,
{
    pid_t pid = fork();
    if (pid == 0) {
        struct table table = {};
        table_test_fail_allocation_after(0);
        table_init(&table, 1, sa_test_hash_u32, sa_test_compare_u32);
        _exit(0);
    }
    if (pid < 0) {
        result = false;
    } else {
        int status = 0;
        TEST_CHECK(waitpid(pid, &status, 0), pid);
        TEST_CHECK(WIFSIGNALED(status), true);
        if (WIFSIGNALED(status)) TEST_CHECK(WTERMSIG(status), SIGABRT);
    }
    table_test_fail_allocation_after(-1);
});

TEST_FUNC(sa_checked_hashtable_insert_rehashes_and_rolls_back_failures,
{
    struct table table = {};
    uint32_t keys[] = { 1, 2, 3 };
    int values[] = { 10, 20, 30 };

    table_test_fail_allocation_after(0);
    TEST_CHECK(table_try_init(&table, 1, sa_test_hash_u32, sa_test_compare_u32), false);
    TEST_CHECK(table_find(&table, &keys[0]) == NULL, true);
    table_remove(&table, &keys[0]);
    table_free(&table);
    TEST_CHECK(table.capacity, 0);
    TEST_CHECK(table.buckets == NULL, true);

    table_test_fail_allocation_after(-1);
    TEST_CHECK(table_try_init(&table, 1, sa_test_hash_u32, sa_test_compare_u32), true);

    table_test_fail_allocation_after(0);
    TEST_CHECK(table_try_add(&table, &keys[0], &values[0]), false);
    TEST_CHECK(table.count, 0);
    TEST_CHECK(table_find(&table, &keys[0]) == NULL, true);

    table_test_fail_allocation_after(1);
    TEST_CHECK(table_try_add(&table, &keys[0], &values[0]), false);
    TEST_CHECK(table.count, 0);
    TEST_CHECK(table_find(&table, &keys[0]) == NULL, true);

    table_test_fail_allocation_after(2);
    TEST_CHECK(table_try_add(&table, &keys[0], &values[0]), false);
    TEST_CHECK(table.count, 0);
    TEST_CHECK(table.capacity, 1);
    TEST_CHECK(table_find(&table, &keys[0]) == NULL, true);

    table_test_fail_allocation_after(-1);
    for (size_t i = 0; i < 3; ++i) TEST_CHECK(table_try_add(&table, &keys[i], &values[i]), true);
    TEST_CHECK(table.count, 3);
    TEST_CHECK(table.capacity >= 4, true);
    for (size_t i = 0; i < 3; ++i) TEST_CHECK(table_find(&table, &keys[i]), &values[i]);
    TEST_CHECK(table_try_add(&table, &keys[0], &values[1]), true);
    TEST_CHECK(table_find(&table, &keys[0]), &values[0]);
    TEST_CHECK(_table_try_add(&table, &keys[0], 0, &values[0]), false);
    table_free(&table);
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
        uint64_t nonzero = 1;
        memcpy(message + 1, &nonzero, layouts[i].length >= 9 ? sizeof(nonzero) : sizeof(uint32_t));
        if (layouts[i].op == SA_OPCODE_SPACE_MOVE) {
            memcpy(message + 9, &nonzero, sizeof(nonzero));
        } else if (layouts[i].op == SA_OPCODE_WINDOW_OPACITY || layouts[i].op == SA_OPCODE_WINDOW_OPACITY_FADE) {
            float opacity = 0.5f;
            float duration = layouts[i].op == SA_OPCODE_WINDOW_OPACITY_FADE ? 1.0f : 0.0f;
            memcpy(message + 5, &opacity, sizeof(opacity));
            memcpy(message + 9, &duration, sizeof(duration));
        } else if (layouts[i].op == SA_OPCODE_WINDOW_LAYER) {
            int layer = SA_LAYER_KEY_NORMAL;
            memcpy(message + 5, &layer, sizeof(layer));
        } else if (layouts[i].op == SA_OPCODE_WINDOW_SCALE) {
            float geometry[] = { 1.0f, 2.0f, 4.0f, 4.0f };
            memcpy(message + 5, geometry, sizeof(geometry));
        } else if (layouts[i].op == SA_OPCODE_WINDOW_TO_SPACE) {
            uint32_t wid = 1;
            memcpy(message + 9, &wid, sizeof(wid));
        }
        TEST_CHECK(sa_test_strict_shape(message, layouts[i].length), true);
    }
    TEST_CHECK(sa_message_is_valid(NULL, 1), false);
    TEST_CHECK(sa_message_is_valid(message, 0), false);
    message[0] = 0xff;
    TEST_CHECK(sa_message_is_valid(message, 1), false);
});

TEST_FUNC(sa_message_opacity_fade_duration_is_validated,
{
    uint8_t message[13] = { SA_OPCODE_WINDOW_OPACITY_FADE };
    size_t length = 1;
    uint32_t wid = 7;
    float opacity = 0.5f;
    float duration = 2147483.5f;
    sa_test_append(message, &length, &wid, sizeof(wid));
    sa_test_append(message, &length, &opacity, sizeof(opacity));
    sa_test_append(message, &length, &duration, sizeof(duration));
    TEST_CHECK(sa_test_strict_shape(message, length), true);

    duration = 2147483.75f;
    memcpy(message + 9, &duration, sizeof(duration));
    TEST_CHECK(sa_message_is_valid(message, sizeof(message)), false);

    duration = NAN;
    memcpy(message + 9, &duration, sizeof(duration));
    TEST_CHECK(sa_message_is_valid(message, sizeof(message)), false);

    message[0] = SA_OPCODE_WINDOW_OPACITY;
    TEST_CHECK(sa_message_is_valid(message, sizeof(message)), false);

    duration = 0.0f;
    memcpy(message + 9, &duration, sizeof(duration));
    TEST_CHECK(sa_message_is_valid(message, sizeof(message)), true);
    message[0] = SA_OPCODE_WINDOW_OPACITY_FADE;
    TEST_CHECK(sa_message_is_valid(message, sizeof(message)), false);
    duration = FLT_MIN;
    memcpy(message + 9, &duration, sizeof(duration));
    TEST_CHECK(sa_message_is_valid(message, sizeof(message)), true);
    float invalid_opacities[] = { NAN, INFINITY, -INFINITY, -0.01f, 1.01f };
    for (size_t i = 0; i < sizeof(invalid_opacities) / sizeof(*invalid_opacities); ++i) {
        memcpy(message + 5, &invalid_opacities[i], sizeof(float));
        TEST_CHECK(sa_message_is_valid(message, sizeof(message)), false);
    }
});

static int sa_test_transport_calls;

static bool sa_test_transport_succeeds(char *bytes, size_t length)
{
    ++sa_test_transport_calls;
    return bytes != NULL && length >= 3;
}

TEST_FUNC(sa_sender_validation_precedes_transport_and_valid_boundaries_dispatch_once,
{
    bool (*saved_transport)(char *, size_t) = scripting_addition_transport;
    scripting_addition_transport = sa_test_transport_succeeds;

    sa_test_transport_calls = 0;
    TEST_CHECK(scripting_addition_set_opacity(0, 0.5f, 0.0f), false);
    TEST_CHECK(scripting_addition_set_opacity(7, NAN, 0.0f), false);
    TEST_CHECK(scripting_addition_set_opacity(7, 0.5f, INFINITY), false);
    TEST_CHECK(scripting_addition_scale_window(0, 0, 0, 4, 4), false);
    TEST_CHECK(scripting_addition_scale_window(7, NAN, 0, 4, 4), false);
    TEST_CHECK(scripting_addition_scale_window(7, 0, 0, -0.0f, 4), false);
    TEST_CHECK(scripting_addition_scale_window(7, 0, 0, 4, FLT_MAX), false);
    TEST_CHECK(scripting_addition_set_layer(7, INT_MIN), false);
    TEST_CHECK(scripting_addition_set_layer(7, INT_MAX), false);
    TEST_CHECK(scripting_addition_order_window(7, INT_MIN, 0), false);
    TEST_CHECK(scripting_addition_order_window(7, INT_MAX, 0), false);
    TEST_CHECK(sa_test_transport_calls, 0);

    TEST_CHECK(scripting_addition_set_opacity(7, 0.0f, 0.0f), true);
    TEST_CHECK(sa_test_transport_calls, 1);
    sa_test_transport_calls = 0;
    TEST_CHECK(scripting_addition_set_opacity(7, 1.0f, FLT_MIN), true);
    TEST_CHECK(sa_test_transport_calls, 1);
    sa_test_transport_calls = 0;
    TEST_CHECK(scripting_addition_scale_window(7, 0, 0, 4, 4), true);
    TEST_CHECK(sa_test_transport_calls, 1);
    int layers[] = { SA_LAYER_KEY_BACKSTOP_MENU, SA_LAYER_KEY_NORMAL, SA_LAYER_KEY_FLOATING };
    for (size_t i = 0; i < sizeof(layers) / sizeof(*layers); ++i) {
        sa_test_transport_calls = 0;
        TEST_CHECK(scripting_addition_set_layer(7, layers[i]), true);
        TEST_CHECK(sa_test_transport_calls, 1);
    }
    for (int order = -1; order <= 1; ++order) {
        sa_test_transport_calls = 0;
        TEST_CHECK(scripting_addition_order_window(7, order, 0), true);
        TEST_CHECK(sa_test_transport_calls, 1);
    }

    scripting_addition_transport = saved_transport;
});

TEST_FUNC(sa_message_layer_and_order_modes_are_exact,
{
    uint8_t message[13] = {};
    uint32_t wid = 7, relative_wid = 0;
    int supported_layers[] = { SA_LAYER_KEY_BACKSTOP_MENU, SA_LAYER_KEY_NORMAL, SA_LAYER_KEY_FLOATING };
    int supported_orders[] = { -1, 0, 1 };
    int rejected_layers[] = { INT_MIN, -1, 0, 1, 2, 6, INT_MAX };
    int rejected_orders[] = { INT_MIN, -2, 2, INT_MAX };

    for (size_t i = 0; i < sizeof(supported_layers) / sizeof(*supported_layers); ++i) {
        message[0] = SA_OPCODE_WINDOW_LAYER;
        memcpy(message + 1, &wid, sizeof(wid));
        memcpy(message + 5, &supported_layers[i], sizeof(int));
        TEST_CHECK(sa_message_is_valid(message, 9), true);
    }
    for (size_t i = 0; i < sizeof(rejected_layers) / sizeof(*rejected_layers); ++i) {
        message[0] = SA_OPCODE_WINDOW_LAYER;
        memcpy(message + 5, &rejected_layers[i], sizeof(int));
        TEST_CHECK(sa_message_is_valid(message, 9), false);
    }
    for (size_t i = 0; i < sizeof(supported_orders) / sizeof(*supported_orders); ++i) {
        message[0] = SA_OPCODE_WINDOW_ORDER;
        memcpy(message + 1, &wid, sizeof(wid));
        memcpy(message + 5, &supported_orders[i], sizeof(int));
        memcpy(message + 9, &relative_wid, sizeof(relative_wid));
        TEST_CHECK(sa_message_is_valid(message, 13), true);
    }
    for (size_t i = 0; i < sizeof(rejected_orders) / sizeof(*rejected_orders); ++i) {
        message[0] = SA_OPCODE_WINDOW_ORDER;
        memcpy(message + 5, &rejected_orders[i], sizeof(int));
        TEST_CHECK(sa_message_is_valid(message, 13), false);
    }
});

TEST_FUNC(sa_fade_generation_retries_stale_blocked_getter_failure,
{
    uint64_t blocked_generation = 1;
    uint64_t current_generation = sa_fade_next_generation(blocked_generation);
    TEST_CHECK(sa_fade_failure_action_for_generation(current_generation, blocked_generation), SA_FADE_FAILURE_RETRY);
    TEST_CHECK(sa_fade_failure_action_for_generation(blocked_generation, blocked_generation), SA_FADE_FAILURE_DESTROY);
});

TEST_FUNC(sa_fade_generation_retries_stale_blocked_setter_failure,
{
    uint64_t blocked_generation = UINT64_MAX;
    uint64_t current_generation = sa_fade_next_generation(blocked_generation);
    TEST_CHECK(current_generation, 1ULL);
    TEST_CHECK(sa_fade_failure_action_for_generation(current_generation, blocked_generation), SA_FADE_FAILURE_RETRY);
    TEST_CHECK(sa_fade_failure_action_for_generation(current_generation, current_generation), SA_FADE_FAILURE_DESTROY);
});

TEST_FUNC(sa_message_scale_geometry_and_derived_boundaries,
{
    uint8_t message[21] = { SA_OPCODE_WINDOW_SCALE };
    uint32_t wid = 7;
    float geometry[] = { 0.0f, 0.0f, 4.0f, 4.0f };
    memcpy(message + 1, &wid, sizeof(wid));
    memcpy(message + 5, geometry, sizeof(geometry));
    TEST_CHECK(sa_message_is_valid(message, sizeof(message)), true);

    float hostile[] = { NAN, INFINITY, -INFINITY, FLT_MAX, -FLT_MAX };
    for (size_t field = 0; field < 4; ++field) {
        float saved = geometry[field];
        for (size_t i = 0; i < sizeof(hostile) / sizeof(*hostile); ++i) {
            geometry[field] = hostile[i];
            memcpy(message + 5, geometry, sizeof(geometry));
            TEST_CHECK(sa_message_is_valid(message, sizeof(message)), false);
        }
        geometry[field] = saved;
    }
    float bad_dimensions[] = { -1.0f, -0.0f, 0.0f, 3.999f };
    for (size_t i = 0; i < sizeof(bad_dimensions) / sizeof(*bad_dimensions); ++i) {
        geometry[2] = bad_dimensions[i];
        memcpy(message + 5, geometry, sizeof(geometry));
        TEST_CHECK(sa_message_is_valid(message, sizeof(message)), false);
    }

    double sx = 9, sy = 9, tx = 9, ty = 9;
    TEST_CHECK(sa_scale_transform_components(100.0, 50.0, 0, 0, 4, 4, &sx, &sy, &tx, &ty), false);
    TEST_CHECK(sx, 0.0);
    TEST_CHECK(sa_scale_transform_components(1000.0, 1.0, 0, 0, 4, 4, &sx, &sy, &tx, &ty), false);
    TEST_CHECK(sa_scale_transform_components(100.0, 50.0, 1, 2, 400, 200, &sx, &sy, &tx, &ty), true);
    TEST_CHECK(isfinite(sx) && isfinite(sy) && isfinite(tx) && isfinite(ty), true);
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
