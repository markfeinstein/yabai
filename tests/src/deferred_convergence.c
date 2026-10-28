static int deferred_test_sa_send_count;
static bool deferred_test_sa_send_success(struct sa_payload_builder *builder)
{
    (void) builder;
    ++deferred_test_sa_send_count;
    return true;
}

TEST_FUNC(animation_payload_packers_bound_batches_and_preserve_order,
{
    struct window_animation *animations = calloc(1023, sizeof(*animations));
    TEST_CHECK(animations != NULL, true);
    if (!animations) return result;

    for (int i = 0; i < 1023; ++i) {
        animations[i].wid = (uint32_t) (i + 1);
        animations[i].proxy.id = (uint32_t) (10000 + i);
    }

    const int active_counts[] = { 511, 512, 513 };
    for (int c = 0; c < array_count(active_counts); ++c) {
        int cursor = 0;
        int covered = 0;
        int batches = 0;
        while (cursor < active_counts[c]) {
            uint8_t guarded[SA_SOCKET_BUFF_LEN + 2];
            memset(guarded, 0xa5, sizeof(guarded));
            struct sa_payload_builder builder;
            int next = scripting_addition_pack_swap_window_proxy_batch(SA_OPCODE_WINDOW_SWAP_PROXY_IN,
                                                                       animations, active_counts[c], cursor,
                                                                       guarded + 1, &builder);
            TEST_CHECK(next > cursor, true);
            size_t length = 0;
            TEST_CHECK(sa_payload_builder_finish(&builder, &length), true);
            TEST_CHECK(length < SA_SOCKET_BUFF_LEN, true);
            TEST_CHECK(sa_message_is_valid(guarded + 1 + sizeof(int16_t), length - sizeof(int16_t)), true);
            TEST_CHECK(guarded[0], (uint8_t) 0xa5);
            TEST_CHECK(guarded[SA_SOCKET_BUFF_LEN + 1], (uint8_t) 0xa5);
            int count = 0;
            memcpy(&count, guarded + 1 + sizeof(int16_t) + 1, sizeof(count));
            TEST_CHECK(count, next - cursor);
            covered += count;
            cursor = next;
            ++batches;
        }
        TEST_CHECK(covered, active_counts[c]);
        TEST_CHECK(batches, active_counts[c] <= 511 ? 1 : 2);
    }

    for (int i = 0; i < 1022; ++i) animations[i].skip = true;
    uint8_t bytes[SA_SOCKET_BUFF_LEN];
    struct sa_payload_builder builder;
    int next = scripting_addition_pack_swap_window_proxy_batch(SA_OPCODE_WINDOW_SWAP_PROXY_OUT,
                                                               animations, 1022, 0, bytes, &builder);
    TEST_CHECK(next, 1022);
    size_t length = 0;
    TEST_CHECK(sa_payload_builder_finish(&builder, &length), true);
    TEST_CHECK(length, (size_t) 4095);
    TEST_CHECK(sa_message_is_valid(bytes + sizeof(int16_t), length - sizeof(int16_t)), true);

    for (int i = 0; i < 1023; ++i) animations[i].skip = (i % 3) == 0;
    int cursor = 0;
    int covered = 0;
    while (cursor < 1023) {
        int batch_start = cursor;
        next = scripting_addition_pack_swap_window_proxy_batch(SA_OPCODE_WINDOW_SWAP_PROXY_IN,
                                                               animations, 1023, cursor, bytes, &builder);
        TEST_CHECK(next > cursor, true);
        int count = 0;
        memcpy(&count, builder.bytes + sizeof(int16_t) + 1, sizeof(count));
        TEST_CHECK(count, next - batch_start);
        covered += count;
        cursor = next;
    }
    TEST_CHECK(covered, 1023);

    TEST_CHECK(window_manager_animation_can_proxy(511), true);
    TEST_CHECK(window_manager_animation_can_proxy(512), false);

    bool (*saved_swap_sender)(struct sa_payload_builder *) = scripting_addition_swap_send_payload;
    scripting_addition_swap_send_payload = deferred_test_sa_send_success;
    deferred_test_sa_send_count = 0;
    for (int i = 0; i < 512; ++i) animations[i].skip = false;
    TEST_CHECK(scripting_addition_swap_window_proxy_in(animations, 511), true);
    TEST_CHECK(deferred_test_sa_send_count, 1);
    deferred_test_sa_send_count = 0;
    TEST_CHECK(scripting_addition_swap_window_proxy_in(animations, 512), false);
    TEST_CHECK(deferred_test_sa_send_count, 0);
    scripting_addition_swap_send_payload = saved_swap_sender;

    free(animations);
});

TEST_FUNC(event_queue_binary_wake_converges_after_merges,
{
    struct event_queue queue;
    TEST_CHECK(event_queue_init(&queue, 4), true);
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;
    TEST_CHECK(event_queue_push(&queue, (struct event) { .type = MENU_BAR_REFRESH }, true,
                                &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    TEST_CHECK(post_wake, true);
    for (int i = 0; i < 1000; ++i) {
        post_wake = true;
        TEST_CHECK(event_queue_push(&queue, (struct event) { .type = MENU_BAR_REFRESH, .param1 = i }, true,
                                    &replaced, &capacity, &post_wake), EVENT_QUEUE_MERGED);
        TEST_CHECK(post_wake, false);
    }
    TEST_CHECK(queue.count, 1U);
    struct event event;
    TEST_CHECK(event_queue_pop(&queue, &event), true);
    TEST_CHECK(event_queue_pop(&queue, &event), false);
    TEST_CHECK(queue.wake_pending, false);
    TEST_CHECK(event_queue_push(&queue, (struct event) { .type = WINDOW_MOVED }, false,
                                &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);
    TEST_CHECK(post_wake, true);
    event_queue_destroy(&queue);

    struct event_loop loop = { 0 };
    TEST_CHECK(event_queue_init(&loop.queue, 4), true);
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai_wake_test_%d", getpid());
    loop.semaphore = sem_open(semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(semaphore_name);
    TEST_CHECK(loop.semaphore != SEM_FAILED, true);
    if (loop.semaphore != SEM_FAILED) {
        pthread_mutex_init(&loop.producer_lock, NULL);
        pthread_cond_init(&loop.producer_cond, NULL);
        loop.producer_sync_initialized = true;
        __atomic_store_n(&loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
        TEST_CHECK(event_loop_try_post(&loop, MENU_BAR_REFRESH, NULL, 1), true);
        for (int i = 0; i < 1000; ++i) TEST_CHECK(event_loop_try_post(&loop, MENU_BAR_REFRESH, NULL, i), true);
        TEST_CHECK(sem_trywait(loop.semaphore), 0);
        errno = 0;
        TEST_CHECK(sem_trywait(loop.semaphore), -1);
        TEST_CHECK(errno, EAGAIN);
        TEST_CHECK(event_queue_pop(&loop.queue, &event), true);
        TEST_CHECK(event_queue_pop(&loop.queue, &event), false);
        TEST_CHECK(event_loop_try_post(&loop, WINDOW_MOVED, NULL, 0), true);
        TEST_CHECK(sem_trywait(loop.semaphore), 0);
        errno = 0;
        TEST_CHECK(sem_trywait(loop.semaphore), -1);
        TEST_CHECK(errno, EAGAIN);
        event_loop_close_producer_admission(&loop);
        pthread_cond_destroy(&loop.producer_cond);
        pthread_mutex_destroy(&loop.producer_lock);
        sem_close(loop.semaphore);
    }
    event_queue_destroy(&loop.queue);
});

TEST_FUNC(focus_pending_accounting_is_saturating_and_owned,
{
    volatile uint32_t pending = UINT32_MAX - 1;
    TEST_CHECK(event_loop_pending_reserve(&pending), true);
    TEST_CHECK(__atomic_load_n(&pending, __ATOMIC_ACQUIRE), UINT32_MAX);
    TEST_CHECK(event_loop_pending_reserve(&pending), false);
    TEST_CHECK(__atomic_load_n(&pending, __ATOMIC_ACQUIRE), UINT32_MAX);
    TEST_CHECK(event_loop_pending_consume(&pending), true);
    TEST_CHECK(__atomic_load_n(&pending, __ATOMIC_ACQUIRE), UINT32_MAX - 1);
    __atomic_store_n(&pending, 0, __ATOMIC_RELEASE);
    TEST_CHECK(event_loop_pending_consume(&pending), false);
    TEST_CHECK(__atomic_load_n(&pending, __ATOMIC_ACQUIRE), 0U);

    uint32_t saved = __pending_window_focus;
    __atomic_store_n(&__pending_window_focus, UINT32_MAX, __ATOMIC_RELEASE);
    application_post_window_focused(99);
    TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), UINT32_MAX);
    __atomic_store_n(&__pending_window_focus, 5, __ATOMIC_RELEASE);
    event_loop_consume_window_focus(0);
    TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 5U);
    event_loop_consume_window_focus(1);
    TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 4U);
    __atomic_store_n(&__pending_window_focus, saved, __ATOMIC_RELEASE);
});

TEST_FUNC(producer_pending_state_rolls_back_and_retries_after_rejection,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct menu_bar_manager_state saved_menu_bar_manager = g_menu_bar_manager;
    uint32_t saved_pending_focus = __pending_window_focus;
    g_event_loop = (struct event_loop) { 0 };
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 1), true);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    struct event replaced = { 0 };
    uint32_t capacity = 0;
    bool post_wake = false;
    TEST_CHECK(event_queue_push(&g_event_loop.queue, (struct event) { .type = WINDOW_MOVED }, false,
                                &replaced, &capacity, &post_wake), EVENT_QUEUE_ADDED);

    g_menu_bar_manager = (struct menu_bar_manager_state) { 0 };
    g_menu_bar_manager.initialized = true;
    g_menu_bar_manager.settings.enabled = MENU_BAR_ENABLED;
    g_event_queue_test_fail_allocation = true;
    menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_WINDOW);
    TEST_CHECK(g_menu_bar_manager.refresh_pending, false);
    TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW);
    application_post_window_focused(42);
    TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 0U);

    g_event_queue_test_fail_allocation = false;
    struct event event;
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);
    char semaphore_name[64];
    snprintf(semaphore_name, sizeof(semaphore_name), "/yabai_pending_test_%d", getpid());
    g_event_loop.semaphore = sem_open(semaphore_name, O_CREAT, 0600, 0);
    sem_unlink(semaphore_name);
    TEST_CHECK(g_event_loop.semaphore != SEM_FAILED, true);
    if (g_event_loop.semaphore != SEM_FAILED) {
        menu_bar_manager_mark_dirty(MENU_BAR_DIRTY_FOCUS);
        TEST_CHECK(g_menu_bar_manager.refresh_pending, true);
        TEST_CHECK(g_menu_bar_manager.dirty_reasons, MENU_BAR_DIRTY_WINDOW | MENU_BAR_DIRTY_FOCUS);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), false);
        application_post_window_focused(42);
        application_post_window_focused(43);
        TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 2U);
        g_event_queue_test_fail_allocation = true;
        application_post_window_focused(44);
        g_event_queue_test_fail_allocation = false;
        TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 2U);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        event_loop_consume_window_focus(event.param1);
        TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 1U);
        TEST_CHECK(event_queue_pop(&g_event_loop.queue, &event), true);
        event_loop_consume_window_focus(event.param1);
        TEST_CHECK(__atomic_load_n(&__pending_window_focus, __ATOMIC_ACQUIRE), 0U);
        sem_close(g_event_loop.semaphore);
    }

    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    event_queue_destroy(&g_event_loop.queue);
    g_event_loop = saved_event_loop;
    g_menu_bar_manager = saved_menu_bar_manager;
    __atomic_store_n(&__pending_window_focus, saved_pending_focus, __ATOMIC_RELEASE);
});
