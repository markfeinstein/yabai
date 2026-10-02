static mach_msg_option_t deferred_test_mach_options;
static mach_msg_timeout_t deferred_test_mach_timeout;
static mach_msg_return_t deferred_test_mach_send(mach_msg_header_t *header, mach_msg_option_t options,
                                                  mach_msg_size_t send_size, mach_msg_size_t receive_limit,
                                                  mach_port_name_t receive_name, mach_msg_timeout_t timeout,
                                                  mach_port_name_t notify)
{
    (void) header; (void) send_size; (void) receive_limit; (void) receive_name; (void) notify;
    deferred_test_mach_options = options;
    deferred_test_mach_timeout = timeout;
    return MACH_SEND_TIMED_OUT;
}

TEST_FUNC(mach_send_uses_bounded_timeout,
{
    __auto_type saved = mach_send_message;
    mach_send_message = deferred_test_mach_send;
    uint32_t payload = 1;
    TEST_CHECK(mach_send(1, &payload, sizeof(payload)), MACH_SEND_TIMED_OUT);
    TEST_CHECK((deferred_test_mach_options & MACH_SEND_MSG) != 0, true);
    TEST_CHECK((deferred_test_mach_options & MACH_SEND_TIMEOUT) != 0, true);
    TEST_CHECK(deferred_test_mach_timeout, (mach_msg_timeout_t) MACH_SEND_SHORT_TIMEOUT_MS);
    mach_send_message = saved;
});

static int deferred_test_sa_send_count;
static bool deferred_test_sa_send_success(struct sa_payload_builder *builder)
{
    (void) builder;
    ++deferred_test_sa_send_count;
    return true;
}

static int deferred_test_janky_send_count;
static int deferred_test_janky_fail_call;
static bool deferred_test_janky_fail_all;
static uint32_t deferred_test_janky_real_wids[2048];
static int deferred_test_janky_real_count;
static bool deferred_test_janky_check_lock;
static bool deferred_test_janky_lock_was_held;
static struct jankyborders_reconciliation *deferred_test_janky_scheduled_state;
static uint64_t deferred_test_janky_scheduled_delay;
static void deferred_test_janky_schedule(struct jankyborders_reconciliation *state, uint64_t delay)
{
    deferred_test_janky_scheduled_state = state;
    deferred_test_janky_scheduled_delay = delay;
}
static kern_return_t deferred_test_janky_send(mach_port_t port, void *data, uint32_t size)
{
    (void) port;
    (void) size;
    struct jankyborders_animation_message *message = data;
    if (deferred_test_janky_check_lock) {
        int lock_result = pthread_mutex_trylock(&g_jankyborders_desired.lock);
        deferred_test_janky_lock_was_held = lock_result != 0;
        if (lock_result == 0) pthread_mutex_unlock(&g_jankyborders_desired.lock);
    }
    for (uint32_t i = 0; i < message->count; ++i) {
        if (deferred_test_janky_real_count < array_count(deferred_test_janky_real_wids)) {
            deferred_test_janky_real_wids[deferred_test_janky_real_count++] = message->real_wid[i];
        }
    }
    ++deferred_test_janky_send_count;
    return deferred_test_janky_fail_all || deferred_test_janky_send_count == deferred_test_janky_fail_call ? KERN_FAILURE : KERN_SUCCESS;
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

    struct jankyborders_animation_message message;
    cursor = window_manager_pack_jankyborders_batch(animations, 1023, 0, 7, false, &message);
    TEST_CHECK(cursor, 512);
    TEST_CHECK(message.count, 512U);
    TEST_CHECK(message.real_wid[0], 1U);
    TEST_CHECK(message.real_wid[511], 512U);
    cursor = window_manager_pack_jankyborders_batch(animations, 1023, cursor, 7, false, &message);
    TEST_CHECK(cursor, 1023);
    TEST_CHECK(message.count, 511U);
    TEST_CHECK(message.real_wid[0], 513U);
    TEST_CHECK(message.real_wid[510], 1023U);

    kern_return_t (*saved_janky_sender)(mach_port_t, void *, uint32_t) = window_manager_jankyborders_send;
    window_manager_jankyborders_send = deferred_test_janky_send;
    deferred_test_janky_fail_all = false;
    deferred_test_janky_send_count = 0;
    deferred_test_janky_fail_call = 1;
    TEST_CHECK(window_manager_reconcile_jankyborders_state(1, window_manager_copy_jankyborders_state(animations, 1023, 7, false)), true);
    TEST_CHECK(deferred_test_janky_send_count, 3);
    deferred_test_janky_send_count = 0;
    deferred_test_janky_fail_call = 2;
    TEST_CHECK(window_manager_reconcile_jankyborders_state(1, window_manager_copy_jankyborders_state(animations, 1023, 7, false)), true);
    TEST_CHECK(deferred_test_janky_send_count, 4);
    deferred_test_janky_send_count = 0;
    deferred_test_janky_fail_call = 0;
    TEST_CHECK(window_manager_send_jankyborders_batches(1, animations, 1023, 7, false), true);
    TEST_CHECK(deferred_test_janky_send_count, 2);

    __auto_type saved_janky_scheduler = window_manager_jankyborders_retry_scheduler;
    window_manager_jankyborders_retry_scheduler = deferred_test_janky_schedule;
    deferred_test_janky_scheduled_state = NULL;
    deferred_test_janky_fail_all = true;
    TEST_CHECK(window_manager_reconcile_jankyborders_state(1, window_manager_copy_jankyborders_state(animations, 513, 9, false)), false);
    TEST_CHECK(deferred_test_janky_scheduled_state != NULL, true);
    TEST_CHECK(deferred_test_janky_scheduled_delay, 100ULL);
    if (deferred_test_janky_scheduled_state) {
        uint32_t copied_first = deferred_test_janky_scheduled_state->records[0].real_wid;
        animations[0].wid = 999999;
        TEST_CHECK(deferred_test_janky_scheduled_state->records[0].real_wid, copied_first);
        deferred_test_janky_fail_all = false;
        TEST_CHECK(window_manager_send_jankyborders_state(1, deferred_test_janky_scheduled_state), true);
        window_manager_free_jankyborders_state(deferred_test_janky_scheduled_state);
    }
    window_manager_jankyborders_retry_scheduler = saved_janky_scheduler;
    window_manager_jankyborders_send = saved_janky_sender;
    free(animations);
});

TEST_FUNC(jankyborders_generations_supersede_stale_retries,
{
    struct window_animation animations[3] = {
        { .wid = 7001, .proxy = { .id = 8001 } },
        { .wid = 7002, .proxy = { .id = 8002 } },
        { .wid = 7003, .proxy = { .id = 8003 } }
    };
    __auto_type saved_sender = window_manager_jankyborders_send;
    __auto_type saved_scheduler = window_manager_jankyborders_retry_scheduler;
    window_manager_jankyborders_send = deferred_test_janky_send;
    window_manager_jankyborders_retry_scheduler = deferred_test_janky_schedule;

    deferred_test_janky_fail_all = true;
    deferred_test_janky_scheduled_state = NULL;
    struct jankyborders_reconciliation *start =
        window_manager_copy_jankyborders_state(animations, 3, 1325, false);
    TEST_CHECK(window_manager_reconcile_jankyborders_state(1, start), false);
    TEST_CHECK(deferred_test_janky_scheduled_state == start, true);

    deferred_test_janky_fail_all = false;
    deferred_test_janky_real_count = 0;
    struct jankyborders_reconciliation *stop =
        window_manager_copy_jankyborders_state(animations, 3, 1326, false);
    TEST_CHECK(window_manager_reconcile_jankyborders_state(1, stop), true);
    int sends_before_stale = deferred_test_janky_send_count;
    TEST_CHECK(window_manager_send_jankyborders_state(1, start), true);
    TEST_CHECK(deferred_test_janky_send_count, sends_before_stale);
    window_manager_free_jankyborders_state(start);

    struct jankyborders_reconciliation *old =
        window_manager_copy_jankyborders_state(animations, 3, 1325, false);
    struct jankyborders_reconciliation *new_middle =
        window_manager_copy_jankyborders_state(&animations[1], 1, 1325, false);
    deferred_test_janky_real_count = 0;
    deferred_test_janky_check_lock = true;
    deferred_test_janky_lock_was_held = false;
    TEST_CHECK(window_manager_send_jankyborders_state(1, old), true);
    TEST_CHECK(deferred_test_janky_lock_was_held, true);
    deferred_test_janky_check_lock = false;
    TEST_CHECK(deferred_test_janky_real_count, 2);
    TEST_CHECK(deferred_test_janky_real_wids[0], 7001U);
    TEST_CHECK(deferred_test_janky_real_wids[1], 7003U);
    window_manager_free_jankyborders_state(old);
    window_manager_free_jankyborders_state(new_middle);

    struct jankyborders_reconciliation *cleanup =
        window_manager_copy_jankyborders_state(animations, 3, 1326, false);
    pthread_mutex_lock(&g_jankyborders_desired.lock);
    TEST_CHECK(window_manager_jankyborders_generation_index_locked(7001) >= 0, true);
    window_manager_jankyborders_complete_stop_locked(cleanup);
    TEST_CHECK(window_manager_jankyborders_generation_index_locked(7001), -1);
    pthread_mutex_unlock(&g_jankyborders_desired.lock);
    window_manager_free_jankyborders_state(cleanup);
    TEST_CHECK(window_manager_jankyborders_generation_matches(7001, 1), false);

    window_manager_jankyborders_send = saved_sender;
    window_manager_jankyborders_retry_scheduler = saved_scheduler;
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
