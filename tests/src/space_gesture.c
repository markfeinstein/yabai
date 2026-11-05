// Decode the serialized event independently of the production payload structs.
// None of these tests post events or change desktops.
static uint32_t test_gesture_le32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static bool test_gesture_find_payload(CFDataRef data, const uint8_t **payload, size_t *payload_size)
{
    const uint8_t *bytes = CFDataGetBytePtr(data);
    size_t length = CFDataGetLength(data);
    if (length < 4 || memcmp(bytes, "\0\0\0\2", 4)) return false;

    int matches = 0;
    for (size_t offset = 4; offset < length;) {
        if (length - offset < 4) return false;
        uint16_t size = (uint16_t) bytes[offset] << 8 | bytes[offset + 1];
        uint16_t tag_field = (uint16_t) bytes[offset + 2] << 8 | bytes[offset + 3];
        uint16_t tag = tag_field >> 14;
        size_t field_size;
        if (tag == 0 && size > 0) field_size = size == 1 ? 8 : size;
        else if (tag == 1 || tag == 3) field_size = (size_t) size * 4;
        else return false;
        offset += 4;
        if (field_size > length - offset) return false;
        if ((tag_field & 0x3fff) == 4205) {
            if (tag != 0) return false;
            ++matches;
            *payload = bytes + offset;
            *payload_size = field_size;
        }
        offset += field_size;
    }
    return matches == 1;
}

static int64_t g_space_gesture_test_post_types[16];
static uint32_t g_space_gesture_test_post_phases[16];
static int g_space_gesture_test_post_count;
static useconds_t g_space_gesture_test_waits[8];
static int g_space_gesture_test_wait_count;
static int g_space_gesture_test_bad_tap_count;
static int g_space_gesture_test_create_count;
static int g_space_gesture_test_create_fail_at;
static enum wm_space_switch_status g_space_direct_test_status;
static int g_space_direct_test_call_count;
static uint32_t g_space_direct_test_did;
static uint64_t g_space_direct_test_origin;
static uint64_t g_space_direct_test_target;
static bool g_space_direct_test_uuid_available;
static bool g_space_direct_test_display_match;
static int g_space_swipe_test_post_count;
static int g_space_swipe_test_direction;
static bool g_space_swipe_test_accept_post;

static CFStringRef test_space_direct_display_uuid(uint32_t did)
{
    g_space_direct_test_did = did;
    return g_space_direct_test_uuid_available ? CFRetain(CFSTR("display-direct-test")) : NULL;
}

static enum wm_space_switch_status test_space_direct_switch(
    CFStringRef display_identifier, uint64_t origin_space_id, uint64_t target_space_id)
{
    g_space_direct_test_display_match = CFEqual(display_identifier, CFSTR("display-direct-test"));
    ++g_space_direct_test_call_count;
    g_space_direct_test_origin = origin_space_id;
    g_space_direct_test_target = target_space_id;
    return g_space_direct_test_status;
}

static bool test_space_swipe_post(int direction)
{
    ++g_space_swipe_test_post_count;
    g_space_swipe_test_direction = direction;
    return g_space_swipe_test_accept_post;
}

static CGEventRef test_space_swipe_event(int phase, double progress, double velocity, int motion)
{
    CGEventRef event = CGEventCreate(NULL);
    if (!event) return NULL;
    CGEventSetIntegerValueField(event, /* kCGSEventTypeField */ 55, /* kCGSEventDockControl */ 30);
    CGEventSetIntegerValueField(event, /* kCGEventGestureHIDType */ 110, /* kIOHIDEventTypeDockSwipe */ 23);
    CGEventSetIntegerValueField(event, /* kCGEventGestureSwipeMotion */ 123, motion);
    CGEventSetIntegerValueField(event, /* kCGEventGesturePhase */ 132, phase);
    CGEventSetDoubleValueField(event, /* kCGEventGestureSwipeProgress */ 124, progress);
    CGEventSetDoubleValueField(event, /* kCGEventGestureSwipeVelocityX */ 129, velocity);
    return event;
}

static CGEventRef test_space_gesture_event_create(CGEventSourceRef source)
{
    ++g_space_gesture_test_create_count;
    if (g_space_gesture_test_create_count == g_space_gesture_test_create_fail_at) return NULL;
    return CGEventCreate(source);
}

static void test_space_gesture_reset_seam(int requires_payload)
{
    memset(g_space_gesture_test_post_types, 0, sizeof(g_space_gesture_test_post_types));
    memset(g_space_gesture_test_post_phases, 0, sizeof(g_space_gesture_test_post_phases));
    memset(g_space_gesture_test_waits, 0, sizeof(g_space_gesture_test_waits));
    g_space_gesture_test_post_count = 0;
    g_space_gesture_test_wait_count = 0;
    g_space_gesture_test_bad_tap_count = 0;
    g_space_gesture_test_create_count = 0;
    space_gesture_test_requires_payload = requires_payload;
}

static void test_space_gesture_post_event(CGEventTapLocation tap, CGEventRef event)
{
    if (tap != kCGSessionEventTap) ++g_space_gesture_test_bad_tap_count;
    if (g_space_gesture_test_post_count < (int) (sizeof(g_space_gesture_test_post_types) / sizeof(g_space_gesture_test_post_types[0]))) {
        g_space_gesture_test_post_types[g_space_gesture_test_post_count] = CGEventGetIntegerValueField(event, 55);
        g_space_gesture_test_post_phases[g_space_gesture_test_post_count] = CGEventGetIntegerValueField(event, 132);
    }
    ++g_space_gesture_test_post_count;
}

static void test_space_gesture_wait(useconds_t usec)
{
    if (g_space_gesture_test_wait_count < (int) (sizeof(g_space_gesture_test_waits) / sizeof(g_space_gesture_test_waits[0]))) {
        g_space_gesture_test_waits[g_space_gesture_test_wait_count] = usec;
    }
    ++g_space_gesture_test_wait_count;
}

TEST_FUNC(space_gesture_encoding,
{
    space_gesture_test_requires_payload = true;
    for (int natural = 0; natural <= 1; ++natural) {
        for (int direction = -1; direction <= 1; direction += 2) {
            for (int i = 0; i < 3; ++i) {
                uint32_t phase = 1u << i;
                bool ended = phase == 4;
                int sign = natural ? -direction : direction;
                CGEventRef event = space_gesture_create_event(direction, natural, phase);
                TEST_CHECK(event != NULL, true);
                if (!event) continue;
                TEST_CHECK((int) CGEventGetIntegerValueField(event, 55), 30);
                TEST_CHECK((int) CGEventGetIntegerValueField(event, 110), 23);
                TEST_CHECK((int) CGEventGetIntegerValueField(event, 123), 1);
                TEST_CHECK((int) CGEventGetIntegerValueField(event, 132), (int) phase);
                TEST_CHECK((int) CGEventGetIntegerValueField(event, 134), (int) phase);
                double progress = CGEventGetDoubleValueField(event, 124) * sign;
                TEST_CHECK(progress > 0.099999 && progress < 0.100001, true);
                double position_x = CGEventGetDoubleValueField(event, 125);
                TEST_CHECK(position_x > 0.099999 && position_x < 0.100001, true);
                TEST_CHECK(CGEventGetDoubleValueField(event, 138), 3.0);
                TEST_CHECK(CGEventGetDoubleValueField(event, 169) > 0, true);
                TEST_CHECK((int) CGEventGetDoubleValueField(event, 129), ended ? sign * 9999 : 0);

                CFDataRef data = CGEventCreateData(kCFAllocatorDefault, event);
                CFRelease(event);
                TEST_CHECK(data != NULL, true);
                if (!data) continue;
                const uint8_t *payload = NULL;
                size_t payload_size = 0;
                bool found = test_gesture_find_payload(data, &payload, &payload_size);
                TEST_CHECK(found, true);
                TEST_CHECK((int) payload_size, ended ? 96 : 68);
                if (found && payload_size == (ended ? 96 : 68)) {
                    // Header: event count; fluid record: size, type, phase, depth,
                    // position, horizontal motion, Dock flavor, 16.16 progress.
                    TEST_CHECK((uint64_t) test_gesture_le32(payload + 8), 0ull);
                    TEST_CHECK((uint64_t) test_gesture_le32(payload + 12), 0ull);
                    TEST_CHECK((int) test_gesture_le32(payload + 16), 0);
                    TEST_CHECK((int) test_gesture_le32(payload + 20), 0);
                    TEST_CHECK((int) test_gesture_le32(payload + 24), ended ? 2 : 1);
                    TEST_CHECK((int) test_gesture_le32(payload + 28), 40);
                    TEST_CHECK((int) test_gesture_le32(payload + 32), 23);
                    TEST_CHECK((int) (test_gesture_le32(payload + 36) >> 24), (int) phase);
                    TEST_CHECK((int) (test_gesture_le32(payload + 36) & 0x00ffffff), 0);
                    TEST_CHECK((int) payload[40], 0);
                    TEST_CHECK((int) payload[41], 0);
                    TEST_CHECK((int) payload[42], 0);
                    TEST_CHECK((int) payload[43], 0);
                    TEST_CHECK((int) test_gesture_le32(payload + 44), 6553);
                    TEST_CHECK((int) test_gesture_le32(payload + 48), 0);
                    TEST_CHECK((int) test_gesture_le32(payload + 52), 0);
                    TEST_CHECK((int) test_gesture_le32(payload + 56), 0);
                    TEST_CHECK((int) payload[60], 1);
                    TEST_CHECK((int) payload[62], 3);
                    TEST_CHECK((int32_t) test_gesture_le32(payload + 64), sign * 6553);
                    if (ended) {
                        TEST_CHECK((int) test_gesture_le32(payload + 68), 28);
                        TEST_CHECK((int) test_gesture_le32(payload + 72), 9);
                        TEST_CHECK((int) test_gesture_le32(payload + 76), 0);
                        TEST_CHECK((int) payload[80], 1);
                        TEST_CHECK((int) payload[81], 0);
                        TEST_CHECK((int) payload[82], 0);
                        TEST_CHECK((int) payload[83], 0);
                        TEST_CHECK((int32_t) test_gesture_le32(payload + 84), sign * 655294464);
                        TEST_CHECK((int) test_gesture_le32(payload + 88), 0);
                        TEST_CHECK((int) test_gesture_le32(payload + 92), 0);
                    }
                }
                CFRelease(data);
            }
        }
    }

    space_gesture_test_requires_payload = false;
    CGEventRef legacy_event = space_gesture_create_event(1, false, 1);
    TEST_CHECK(legacy_event != NULL, true);
    if (legacy_event) {
        double legacy_progress = CGEventGetDoubleValueField(legacy_event, 124);
        TEST_CHECK(legacy_progress > 0.000015 && legacy_progress < 0.000017, true);
        CFDataRef legacy_data = CGEventCreateData(kCFAllocatorDefault, legacy_event);
        CFRelease(legacy_event);
        TEST_CHECK(legacy_data != NULL, true);
        if (legacy_data) {
            const uint8_t *legacy_payload = NULL;
            size_t legacy_payload_size = 0;
            bool found = test_gesture_find_payload(legacy_data, &legacy_payload, &legacy_payload_size);
            TEST_CHECK(found, true);
            if (found && legacy_payload_size >= 68) {
                TEST_CHECK((int32_t) test_gesture_le32(legacy_payload + 64), 1);
            }
            CFRelease(legacy_data);
        }
    }
    space_gesture_test_requires_payload = -1;
});

TEST_FUNC(space_manager_direct_focus_uses_owned_display_identity,
{
    __auto_type saved_display_uuid = space_manager_test_display_uuid;
    __auto_type saved_direct_switch = space_manager_test_direct_space_switch;
    space_manager_test_display_uuid = test_space_direct_display_uuid;
    space_manager_test_direct_space_switch = test_space_direct_switch;

    g_space_direct_test_uuid_available = true;
    g_space_direct_test_status = WM_SPACE_SWITCH_DISPATCHED;
    g_space_direct_test_call_count = 0;
    TEST_CHECK(space_manager_focus_space_direct(7, 11, 12), WM_SPACE_SWITCH_DISPATCHED);
    TEST_CHECK(g_space_direct_test_call_count, 1);
    TEST_CHECK(g_space_direct_test_display_match, true);
    TEST_CHECK(g_space_direct_test_did, 7U);
    TEST_CHECK(g_space_direct_test_origin, 11ULL);
    TEST_CHECK(g_space_direct_test_target, 12ULL);

    g_space_direct_test_status = WM_SPACE_SWITCH_ABI_MISMATCH;
    TEST_CHECK(space_manager_focus_space_direct(8, 21, 22), WM_SPACE_SWITCH_ABI_MISMATCH);
    TEST_CHECK(g_space_direct_test_call_count, 2);
    TEST_CHECK(g_space_direct_test_did, 8U);
    TEST_CHECK(g_space_direct_test_origin, 21ULL);
    TEST_CHECK(g_space_direct_test_target, 22ULL);

    g_space_direct_test_uuid_available = false;
    TEST_CHECK(space_manager_focus_space_direct(9, 31, 32), WM_SPACE_SWITCH_UNAVAILABLE);
    TEST_CHECK(g_space_direct_test_call_count, 2);

    space_manager_test_display_uuid = saved_display_uuid;
    space_manager_test_direct_space_switch = saved_direct_switch;
});

TEST_FUNC(mouse_handler_direct_space_swipe_suppresses_only_horizontal_gestures,
{
    __auto_type saved_post = mouse_handler_test_post_direct_space_swipe;
    bool saved_direct = g_space_manager.direct_space_switch;
    bool saved_pending = __atomic_load_n(&__pending_gesture, __ATOMIC_ACQUIRE);
    uint64_t saved_last = __atomic_load_n(&__last_gesture_time, __ATOMIC_ACQUIRE);
    mouse_handler_test_post_direct_space_swipe = test_space_swipe_post;
    g_space_manager.direct_space_switch = true;
    __atomic_store_n(&__pending_gesture, false, __ATOMIC_RELEASE);

    struct mouse_state mouse_state = {0};
    struct mouse_handler_context context = {
        .mouse_state = &mouse_state,
        .direct_space_switch_available = true
    };
    g_space_swipe_test_accept_post = true;
    g_space_swipe_test_post_count = 0;
    g_space_swipe_test_direction = 0;

    TEST_CHECK(mouse_handler_direct_swipe_direction(0.079, 0.99), 0);
    TEST_CHECK(mouse_handler_direct_swipe_direction(0.08, 0.0), 1);
    TEST_CHECK(mouse_handler_direct_swipe_direction(-0.08, 0.0), -1);
    TEST_CHECK(mouse_handler_direct_swipe_direction(0.02, 1.0), 1);
    TEST_CHECK(mouse_handler_direct_swipe_direction(-0.02, -1.0), -1);
    TEST_CHECK(mouse_handler_direct_swipe_direction(0.1, -3.0), 1);

    CGEventRef began = test_space_swipe_event(1, 0.0, 0.0, 1);
    CGEventRef companion = CGEventCreate(NULL);
    CGEventRef tiny_changed = test_space_swipe_event(2, 0.04, 0.0, 1);
    CGEventRef changed = test_space_swipe_event(2, 0.1, 0.0, 1);
    CGEventRef ended = test_space_swipe_event(4, 0.5, 3.0, 1);
    TEST_CHECK(began != NULL && companion != NULL && tiny_changed != NULL && changed != NULL && ended != NULL, true);
    if (began && companion && tiny_changed && changed && ended) {
        TEST_CHECK(mouse_handler(NULL, 30, began, &context) == NULL, true);
        TEST_CHECK(context.direct_swipe_tracking, true);
        TEST_CHECK(__atomic_load_n(&__pending_gesture, __ATOMIC_ACQUIRE), true);
        TEST_CHECK(mouse_handler(NULL, 29, companion, &context) == NULL, true);
        TEST_CHECK(mouse_handler(NULL, 30, tiny_changed, &context) == NULL, true);
        TEST_CHECK(g_space_swipe_test_post_count, 0);
        TEST_CHECK(context.direct_swipe_fired, false);
        TEST_CHECK(mouse_handler(NULL, 30, changed, &context) == NULL, true);
        TEST_CHECK(g_space_swipe_test_post_count, 1);
        TEST_CHECK(g_space_swipe_test_direction, 1);
        TEST_CHECK(context.direct_swipe_fired, true);
        TEST_CHECK(mouse_handler(NULL, 30, ended, &context) == ended, true);
        TEST_CHECK(CGEventGetDoubleValueField(ended, 124), 0.0);
        TEST_CHECK(CGEventGetDoubleValueField(ended, 129), 0.0);
        TEST_CHECK(context.direct_swipe_tracking, false);
        TEST_CHECK(context.direct_swipe_fired, false);
        TEST_CHECK(__atomic_load_n(&__pending_gesture, __ATOMIC_ACQUIRE), false);
        TEST_CHECK(mouse_handler(NULL, 29, companion, &context) == companion, true);
    }
    if (ended) CFRelease(ended);
    if (changed) CFRelease(changed);
    if (tiny_changed) CFRelease(tiny_changed);
    if (companion) CFRelease(companion);
    if (began) CFRelease(began);

    context.direct_swipe_tracking = false;
    context.direct_swipe_fired = false;
    g_space_swipe_test_post_count = 0;
    CGEventRef began_without_change = test_space_swipe_event(1, 0.0, 0.0, 1);
    CGEventRef ended_without_change = test_space_swipe_event(4, -0.02, -2.0, 1);
    if (began_without_change && ended_without_change) {
        TEST_CHECK(mouse_handler(NULL, 30, began_without_change, &context) == NULL, true);
        TEST_CHECK(mouse_handler(NULL, 30, ended_without_change, &context) == ended_without_change, true);
        TEST_CHECK(g_space_swipe_test_post_count, 1);
        TEST_CHECK(g_space_swipe_test_direction, -1);
    }
    if (ended_without_change) CFRelease(ended_without_change);
    if (began_without_change) CFRelease(began_without_change);

    context.direct_swipe_tracking = false;
    context.direct_swipe_fired = false;
    g_space_swipe_test_post_count = 0;
    CGEventRef tiny_began = test_space_swipe_event(1, 0.0, 0.0, 1);
    CGEventRef tiny_ended = test_space_swipe_event(4, 0.04, 0.4, 1);
    if (tiny_began && tiny_ended) {
        TEST_CHECK(mouse_handler(NULL, 30, tiny_began, &context) == NULL, true);
        TEST_CHECK(mouse_handler(NULL, 30, tiny_ended, &context) == NULL, true);
        TEST_CHECK(g_space_swipe_test_post_count, 0);
        TEST_CHECK(context.direct_swipe_tracking, false);
        TEST_CHECK(context.direct_swipe_fired, false);
    }
    if (tiny_ended) CFRelease(tiny_ended);
    if (tiny_began) CFRelease(tiny_began);

    g_space_manager.direct_space_switch = true;
    CGEventRef toggled_began = test_space_swipe_event(1, 0.0, 0.0, 1);
    CGEventRef toggled_companion = CGEventCreate(NULL);
    if (toggled_began && toggled_companion) {
        TEST_CHECK(mouse_handler(NULL, 30, toggled_began, &context) == NULL, true);
        g_space_manager.direct_space_switch = false;
        TEST_CHECK(mouse_handler(NULL, 29, toggled_companion, &context) == toggled_companion, true);
        TEST_CHECK(context.direct_swipe_tracking, false);
    }
    if (toggled_companion) CFRelease(toggled_companion);
    if (toggled_began) CFRelease(toggled_began);

    g_space_manager.direct_space_switch = false;
    g_space_swipe_test_post_count = 0;
    CGEventRef native_began = test_space_swipe_event(1, 0.0, 0.0, 1);
    CGEventRef native_ended = test_space_swipe_event(4, 0.2, 2.0, 1);
    if (native_began && native_ended) {
        TEST_CHECK(mouse_handler(NULL, 30, native_began, &context) == native_began, true);
        TEST_CHECK(mouse_handler(NULL, 30, native_ended, &context) == native_ended, true);
        TEST_CHECK(g_space_swipe_test_post_count, 0);
    }
    if (native_ended) CFRelease(native_ended);
    if (native_began) CFRelease(native_began);

    g_space_manager.direct_space_switch = true;
    context.direct_space_switch_available = false;
    CGEventRef unavailable = test_space_swipe_event(1, 0.0, 0.0, 1);
    if (unavailable) {
        TEST_CHECK(mouse_handler(NULL, 30, unavailable, &context) == unavailable, true);
        CFRelease(unavailable);
    }

    context.direct_space_switch_available = true;
    CGEventRef vertical = test_space_swipe_event(1, 0.0, 0.0, 2);
    if (vertical) {
        TEST_CHECK(mouse_handler(NULL, 30, vertical, &context) == vertical, true);
        CFRelease(vertical);
    }

    CGEventRef synthetic = test_space_swipe_event(1, 0.0, 0.0, 1);
    if (synthetic) {
        CGEventSetIntegerValueField(synthetic, kCGEventSourceUserData, SPACE_GESTURE_EVENT_TAG);
        TEST_CHECK(mouse_handler(NULL, 30, synthetic, &context) == synthetic, true);
        CFRelease(synthetic);
    }

    mouse_handler_test_post_direct_space_swipe = saved_post;
    g_space_manager.direct_space_switch = saved_direct;
    __atomic_store_n(&__pending_gesture, saved_pending, __ATOMIC_RELEASE);
    __atomic_store_n(&__last_gesture_time, saved_last, __ATOMIC_RELEASE);
});

TEST_FUNC(space_gesture_invalid_request,
{
    space_gesture_test_post_event = test_space_gesture_post_event;
    space_gesture_test_wait = test_space_gesture_wait;

    test_space_gesture_reset_seam(true);
    TEST_CHECK(space_gesture_perform(1, 1), true);
    TEST_CHECK(g_space_gesture_test_post_count, 6);
    for (int i = 0; i < 3; ++i) {
        int dock = i * 2;
        int companion = dock + 1;
        TEST_CHECK((int) g_space_gesture_test_post_types[dock], 30);
        TEST_CHECK((int) g_space_gesture_test_post_phases[dock], 1 << i);
        TEST_CHECK((int) g_space_gesture_test_post_types[companion], 29);
        TEST_CHECK((int) g_space_gesture_test_post_phases[companion], 0);
    }
    TEST_CHECK(g_space_gesture_test_bad_tap_count, 0);
    TEST_CHECK(g_space_gesture_test_wait_count, 2);
    TEST_CHECK((int) g_space_gesture_test_waits[0], 10000);
    TEST_CHECK((int) g_space_gesture_test_waits[1], 10000);

    test_space_gesture_reset_seam(false);
    TEST_CHECK(space_gesture_perform(1, 1), true);
    TEST_CHECK(g_space_gesture_test_post_count, 3);
    for (int i = 0; i < 3; ++i) {
        TEST_CHECK((int) g_space_gesture_test_post_types[i], 30);
        TEST_CHECK((int) g_space_gesture_test_post_phases[i], 1 << i);
    }
    TEST_CHECK(g_space_gesture_test_bad_tap_count, 0);
    TEST_CHECK(g_space_gesture_test_wait_count, 0);

    test_space_gesture_reset_seam(true);
    TEST_CHECK(space_gesture_perform(1, 2), true);
    TEST_CHECK(g_space_gesture_test_post_count, 12);
    for (int step = 0; step < 2; ++step) {
        for (int i = 0; i < 3; ++i) {
            int dock = step * 6 + i * 2;
            int companion = dock + 1;
            TEST_CHECK((int) g_space_gesture_test_post_types[dock], 30);
            TEST_CHECK((int) g_space_gesture_test_post_phases[dock], 1 << i);
            TEST_CHECK((int) g_space_gesture_test_post_types[companion], 29);
            TEST_CHECK((int) g_space_gesture_test_post_phases[companion], 0);
        }
    }
    TEST_CHECK(g_space_gesture_test_bad_tap_count, 0);
    TEST_CHECK(g_space_gesture_test_wait_count, 4);
    TEST_CHECK((int) g_space_gesture_test_waits[0], 10000);
    TEST_CHECK((int) g_space_gesture_test_waits[1], 10000);
    TEST_CHECK((int) g_space_gesture_test_waits[2], 10000);
    TEST_CHECK((int) g_space_gesture_test_waits[3], 10000);

    space_gesture_test_event_create = test_space_gesture_event_create;
    for (int fail_at = 1; fail_at <= 6; ++fail_at) {
        g_space_gesture_test_create_fail_at = fail_at;
        test_space_gesture_reset_seam(true);
        TEST_CHECK(space_gesture_perform(1, 1), false);
        TEST_CHECK(g_space_gesture_test_post_count, 0);
        TEST_CHECK(g_space_gesture_test_wait_count, 0);
    }
    for (int fail_at = 1; fail_at <= 3; ++fail_at) {
        g_space_gesture_test_create_fail_at = fail_at;
        test_space_gesture_reset_seam(false);
        TEST_CHECK(space_gesture_perform(1, 1), false);
        TEST_CHECK(g_space_gesture_test_post_count, 0);
        TEST_CHECK(g_space_gesture_test_wait_count, 0);
    }
    g_space_gesture_test_create_fail_at = 0;
    space_gesture_test_event_create = CGEventCreate;
    space_gesture_test_post_event = NULL;
    space_gesture_test_wait = NULL;
    space_gesture_test_requires_payload = -1;
    TEST_CHECK(space_gesture_create_event(0, true, 1) == NULL, true);
    TEST_CHECK(space_gesture_create_event(2, true, 1) == NULL, true);
    TEST_CHECK(space_gesture_create_event(1, true, 0) == NULL, true);
    TEST_CHECK(space_gesture_create_event(1, true, 3) == NULL, true);
    TEST_CHECK(space_gesture_perform(0, 1), false);
    TEST_CHECK(space_gesture_perform(1, 0), false);
    TEST_CHECK(space_gesture_perform(1, -1), false);
});
