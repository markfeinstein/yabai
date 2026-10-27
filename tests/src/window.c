TEST_FUNC(window_sub_level_message_id_tracks_tahoe_protocol,
{
    long saved_macos_version = workspace_macos_major_version();

    workspace_set_macos_version(15);
    TEST_CHECK(window_sub_level_message_id(), UINT32_C(0x73C3));
    workspace_set_macos_version(26);
    TEST_CHECK(window_sub_level_message_id(), UINT32_C(0x76E3));
    workspace_set_macos_version(27);
    TEST_CHECK(window_sub_level_message_id(), UINT32_C(0x76E3));
    workspace_set_macos_version(28);
    TEST_CHECK(window_sub_level_message_id(), UINT32_C(0x76E3));

    workspace_set_macos_version(saved_macos_version);
});

enum window_ax_snapshot_test_mode
{
    WINDOW_AX_SNAPSHOT_TEST_VALID,
    WINDOW_AX_SNAPSHOT_TEST_MIXED,
    WINDOW_AX_SNAPSHOT_TEST_SHORT,
    WINDOW_AX_SNAPSHOT_TEST_OVERSIZED,
    WINDOW_AX_SNAPSHOT_TEST_BATCH_FAILURE,
    WINDOW_AX_SNAPSHOT_TEST_WRONG_AX_TYPE
};

static enum window_ax_snapshot_test_mode window_ax_snapshot_test_mode;
static bool window_ax_snapshot_test_create_failure;
static bool window_ax_snapshot_test_size_fallback_failure;
static AXUIElementRef window_ax_snapshot_test_parent_value;
static int window_ax_snapshot_test_create_calls;
static int window_ax_snapshot_test_multiple_calls;
static int window_ax_snapshot_test_single_calls;
static AXCopyMultipleAttributeOptions window_ax_snapshot_test_last_options;
static CFIndex window_ax_snapshot_test_last_attribute_count;
static bool window_ax_snapshot_test_attribute_order_valid;

static CFArrayRef window_ax_snapshot_test_create(CFAllocatorRef allocator, const void **values,
                                                  CFIndex count, const CFArrayCallBacks *callbacks)
{
    ++window_ax_snapshot_test_create_calls;
    return window_ax_snapshot_test_create_failure ? NULL : CFArrayCreate(allocator, values, count, callbacks);
}

static CFTypeRef window_ax_snapshot_test_value(CFStringRef attribute)
{
    if (CFEqual(attribute, kAXPositionAttribute)) {
        if (window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_WRONG_AX_TYPE) {
            CGSize size = CGSizeMake(300, 200);
            return AXValueCreate(kAXValueTypeCGSize, &size);
        }
        CGPoint position = CGPointMake(10, 20);
        return AXValueCreate(kAXValueTypeCGPoint, &position);
    }
    if (CFEqual(attribute, kAXSizeAttribute)) {
        if (window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_MIXED) return CFRetain(kCFNull);
        CGSize size = CGSizeMake(300, 200);
        return AXValueCreate(kAXValueTypeCGSize, &size);
    }
    if (CFEqual(attribute, kAXRoleAttribute)) {
        return CFRetain(window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_MIXED
                        ? (CFTypeRef) kCFBooleanTrue : (CFTypeRef) kAXWindowRole);
    }
    if (CFEqual(attribute, kAXSubroleAttribute)) {
        if (window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_MIXED) {
            AXError error = kAXErrorNoValue;
            return AXValueCreate(kAXValueTypeAXError, &error);
        }
        return CFRetain(kAXStandardWindowSubrole);
    }
    if (CFEqual(attribute, kAXTitleAttribute)) {
        return CFRetain(window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_MIXED
                        ? (CFTypeRef) kCFNull : (CFTypeRef) CFSTR("snapshot-title"));
    }
    if (CFEqual(attribute, kAXParentAttribute)) {
        if (window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_MIXED) {
            return CFRetain(CFSTR("not-an-ax-element"));
        }
        return window_ax_snapshot_test_parent_value
             ? CFRetain(window_ax_snapshot_test_parent_value)
             : AXUIElementCreateApplication(getpid());
    }
    if (CFEqual(attribute, kAXMinimizedAttribute)) {
        if (window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_MIXED) {
            AXError error = kAXErrorAttributeUnsupported;
            return AXValueCreate(kAXValueTypeAXError, &error);
        }
        return CFRetain(kCFBooleanFalse);
    }
    if (CFEqual(attribute, kAXFullscreenAttribute)) return CFRetain(kCFBooleanTrue);
    return CFRetain(kCFNull);
}

static AXError window_ax_snapshot_test_copy_multiple(AXUIElementRef element, CFArrayRef attributes,
                                                      AXCopyMultipleAttributeOptions options,
                                                      CFArrayRef *values)
{
    (void) element;
    ++window_ax_snapshot_test_multiple_calls;
    window_ax_snapshot_test_last_options = options;
    window_ax_snapshot_test_last_attribute_count = CFArrayGetCount(attributes);
    window_ax_snapshot_test_attribute_order_valid = true;
    int previous_slot = -1;
    for (CFIndex index = 0; index < window_ax_snapshot_test_last_attribute_count; ++index) {
        CFStringRef attribute = CFArrayGetValueAtIndex(attributes, index);
        int slot = -1;
        for (int candidate = 0; candidate < WINDOW_AX_SNAPSHOT_SLOT_COUNT; ++candidate) {
            if (CFEqual(attribute, window_ax_snapshot_attribute[candidate])) {
                slot = candidate;
                break;
            }
        }
        if (slot <= previous_slot) window_ax_snapshot_test_attribute_order_valid = false;
        previous_slot = slot;
    }
    *values = NULL;
    if (window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_BATCH_FAILURE) {
        return kAXErrorCannotComplete;
    }

    CFIndex attribute_count = CFArrayGetCount(attributes);
    CFIndex value_count = attribute_count;
    if (window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_SHORT && value_count > 0) --value_count;
    if (window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_OVERSIZED) ++value_count;

    CFTypeRef result_values[WINDOW_AX_SNAPSHOT_SLOT_COUNT + 1] = {0};
    for (CFIndex index = 0; index < value_count; ++index) {
        if (index < attribute_count) {
            CFStringRef attribute = CFArrayGetValueAtIndex(attributes, index);
            result_values[index] = window_ax_snapshot_test_value(attribute);
        } else {
            result_values[index] = CFRetain(kCFNull);
        }
    }

    *values = CFArrayCreate(NULL, (const void **) result_values, value_count, &kCFTypeArrayCallBacks);
    for (CFIndex index = 0; index < value_count; ++index) CFRelease(result_values[index]);
    return *values ? kAXErrorSuccess : kAXErrorFailure;
}

static AXError window_ax_snapshot_test_copy_single(AXUIElementRef element, CFStringRef attribute,
                                                    CFTypeRef *value)
{
    (void) element;
    ++window_ax_snapshot_test_single_calls;
    *value = NULL;

    if (CFEqual(attribute, kAXPositionAttribute)) {
        CGPoint position = CGPointMake(40, 50);
        *value = AXValueCreate(kAXValueTypeCGPoint, &position);
    } else if (CFEqual(attribute, kAXSizeAttribute)) {
        if (window_ax_snapshot_test_size_fallback_failure) return kAXErrorCannotComplete;
        CGSize size = CGSizeMake(600, 700);
        *value = AXValueCreate(kAXValueTypeCGSize, &size);
    } else if (CFEqual(attribute, kAXRoleAttribute) &&
               window_ax_snapshot_test_mode == WINDOW_AX_SNAPSHOT_TEST_BATCH_FAILURE) {
        *value = CFRetain(kAXWindowRole);
    } else if (CFEqual(attribute, kAXSubroleAttribute)) {
        *value = CFRetain(kAXDialogSubrole);
    } else if (CFEqual(attribute, kAXParentAttribute) && window_ax_snapshot_test_parent_value) {
        *value = CFRetain(window_ax_snapshot_test_parent_value);
    } else if (CFEqual(attribute, kAXMinimizedAttribute)) {
        *value = CFRetain(kCFBooleanTrue);
    } else if (CFEqual(attribute, kAXTitleAttribute)) {
        return kAXErrorCannotComplete;
    } else {
        return kAXErrorAttributeUnsupported;
    }

    return *value ? kAXErrorSuccess : kAXErrorFailure;
}

static void window_ax_snapshot_test_begin(enum window_ax_snapshot_test_mode mode)
{
    window_ax_snapshot_test_mode = mode;
    window_ax_snapshot_test_create_failure = false;
    window_ax_snapshot_test_size_fallback_failure = false;
    window_ax_snapshot_test_parent_value = NULL;
    window_ax_snapshot_test_create_calls = 0;
    window_ax_snapshot_test_multiple_calls = 0;
    window_ax_snapshot_test_single_calls = 0;
    window_ax_snapshot_test_last_options = 0;
    window_ax_snapshot_test_last_attribute_count = 0;
    window_ax_snapshot_test_attribute_order_valid = true;
    window_ax_snapshot_create_attributes = window_ax_snapshot_test_create;
    window_ax_snapshot_copy_multiple = window_ax_snapshot_test_copy_multiple;
    window_ax_snapshot_copy_single = window_ax_snapshot_test_copy_single;
}

static void window_ax_snapshot_test_end(void)
{
    window_ax_snapshot_create_attributes = CFArrayCreate;
    window_ax_snapshot_copy_multiple = AXUIElementCopyMultipleAttributeValues;
    window_ax_snapshot_copy_single = AXUIElementCopyAttributeValue;
}

TEST_FUNC(window_ax_snapshot_reads_and_owns_valid_batch_values,
{
    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_VALID);
    AXUIElementRef ref = AXUIElementCreateApplication(getpid());
    uint32_t requested = WINDOW_AX_POSITION | WINDOW_AX_SIZE | WINDOW_AX_ROLE | WINDOW_AX_SUBROLE |
                         WINDOW_AX_TITLE | WINDOW_AX_PARENT | WINDOW_AX_MINIMIZED | WINDOW_AX_FULLSCREEN;
    struct window_ax_snapshot snapshot = {0};

    TEST_CHECK(window_ax_snapshot_read(ref, requested, &snapshot), true);
    TEST_CHECK(snapshot.requested, requested);
    TEST_CHECK(snapshot.valid, requested);
    TEST_CHECK(snapshot.position.x, 10.0);
    TEST_CHECK(snapshot.position.y, 20.0);
    TEST_CHECK(snapshot.size.width, 300.0);
    TEST_CHECK(snapshot.size.height, 200.0);
    TEST_CHECK(snapshot.role != NULL, true);
    if (snapshot.role) TEST_CHECK(CFEqual(snapshot.role, kAXWindowRole), true);
    TEST_CHECK(snapshot.subrole != NULL, true);
    if (snapshot.subrole) TEST_CHECK(CFEqual(snapshot.subrole, kAXStandardWindowSubrole), true);
    TEST_CHECK(snapshot.title != NULL, true);
    if (snapshot.title) TEST_CHECK(CFEqual(snapshot.title, CFSTR("snapshot-title")), true);
    TEST_CHECK(snapshot.parent != NULL, true);
    if (snapshot.parent) TEST_CHECK(CFGetTypeID(snapshot.parent), AXUIElementGetTypeID());
    TEST_CHECK(snapshot.minimized, false);
    TEST_CHECK(snapshot.fullscreen, true);
    TEST_CHECK(window_ax_snapshot_test_create_calls, 1);
    TEST_CHECK(window_ax_snapshot_test_multiple_calls, 1);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 0);

    CGRect frame = CGRectZero;
    TEST_CHECK(window_ax_snapshot_frame(&snapshot, &frame), true);
    TEST_CHECK(CGRectEqualToRect(frame, CGRectMake(10, 20, 300, 200)), true);

    window_ax_snapshot_destroy(&snapshot);
    TEST_CHECK(snapshot.requested, 0U);
    TEST_CHECK(snapshot.valid, 0U);
    TEST_CHECK(snapshot.role == NULL, true);
    TEST_CHECK(snapshot.parent == NULL, true);
    window_ax_snapshot_destroy(&snapshot);
    CFRelease(ref);
    window_ax_snapshot_test_end();
});

TEST_FUNC(window_ax_snapshot_validates_slots_and_couples_frame_fallback,
{
    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_MIXED);
    AXUIElementRef ref = AXUIElementCreateApplication(getpid());
    uint32_t requested = WINDOW_AX_POSITION | WINDOW_AX_SIZE | WINDOW_AX_ROLE | WINDOW_AX_SUBROLE |
                         WINDOW_AX_TITLE | WINDOW_AX_PARENT | WINDOW_AX_MINIMIZED | WINDOW_AX_FULLSCREEN;
    struct window_ax_snapshot snapshot = {0};

    TEST_CHECK(window_ax_snapshot_read(ref, requested, &snapshot), false);
    uint32_t expected_valid = WINDOW_AX_POSITION | WINDOW_AX_SIZE | WINDOW_AX_SUBROLE |
                              WINDOW_AX_MINIMIZED | WINDOW_AX_FULLSCREEN;
    TEST_CHECK(snapshot.valid, expected_valid);
    TEST_CHECK(snapshot.position.x, 40.0);
    TEST_CHECK(snapshot.position.y, 50.0);
    TEST_CHECK(snapshot.size.width, 600.0);
    TEST_CHECK(snapshot.size.height, 700.0);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_ROLE_SLOT], kAXErrorIllegalArgument);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_SUBROLE_SLOT], kAXErrorSuccess);
    TEST_CHECK(snapshot.subrole != NULL, true);
    if (snapshot.subrole) TEST_CHECK(CFEqual(snapshot.subrole, kAXDialogSubrole), true);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_TITLE_SLOT], kAXErrorCannotComplete);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_PARENT_SLOT], kAXErrorIllegalArgument);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_MINIMIZED_SLOT], kAXErrorSuccess);
    TEST_CHECK(snapshot.minimized, true);
    TEST_CHECK(snapshot.fullscreen, true);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 5);

    window_ax_snapshot_destroy(&snapshot);
    window_ax_snapshot_test_single_calls = 0;
    window_ax_snapshot_test_size_fallback_failure = true;
    uint32_t frame_fields = WINDOW_AX_POSITION | WINDOW_AX_SIZE;
    TEST_CHECK(window_ax_snapshot_read(ref, frame_fields, &snapshot), false);
    TEST_CHECK(snapshot.valid & frame_fields, 0U);
    TEST_CHECK(CGPointEqualToPoint(snapshot.position, CGPointZero), true);
    TEST_CHECK(CGSizeEqualToSize(snapshot.size, CGSizeZero), true);
    CGRect frame = CGRectZero;
    TEST_CHECK(window_ax_snapshot_frame(&snapshot, &frame), false);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 2);

    window_ax_snapshot_destroy(&snapshot);
    CFRelease(ref);
    window_ax_snapshot_test_end();
});

TEST_FUNC(window_ax_snapshot_rejects_malformed_batch_shapes_and_types,
{
    AXUIElementRef ref = AXUIElementCreateApplication(getpid());
    struct window_ax_snapshot snapshot = {0};

    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_SHORT);
    uint32_t text_fields = WINDOW_AX_ROLE | WINDOW_AX_TITLE;
    TEST_CHECK(window_ax_snapshot_read(ref, text_fields, &snapshot), false);
    TEST_CHECK(snapshot.valid, WINDOW_AX_ROLE);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_TITLE_SLOT], kAXErrorCannotComplete);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 1);
    window_ax_snapshot_destroy(&snapshot);
    window_ax_snapshot_test_end();

    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_OVERSIZED);
    TEST_CHECK(window_ax_snapshot_read(ref, WINDOW_AX_ROLE, &snapshot), false);
    TEST_CHECK(snapshot.valid, 0U);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_ROLE_SLOT], kAXErrorAttributeUnsupported);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 1);
    window_ax_snapshot_destroy(&snapshot);
    window_ax_snapshot_test_end();

    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_WRONG_AX_TYPE);
    TEST_CHECK(window_ax_snapshot_read(ref, WINDOW_AX_POSITION, &snapshot), false);
    TEST_CHECK(snapshot.valid, 0U);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_POSITION_SLOT], kAXErrorIllegalArgument);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 0);
    window_ax_snapshot_destroy(&snapshot);
    window_ax_snapshot_test_end();

    CFRelease(ref);
});

TEST_FUNC(window_ax_snapshot_recovers_batch_failure_and_handles_allocation_failure,
{
    AXUIElementRef ref = AXUIElementCreateApplication(getpid());
    struct window_ax_snapshot snapshot = {0};

    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_BATCH_FAILURE);
    uint32_t requested = WINDOW_AX_POSITION | WINDOW_AX_SIZE | WINDOW_AX_ROLE;
    TEST_CHECK(window_ax_snapshot_read(ref, requested, &snapshot), true);
    TEST_CHECK(snapshot.valid, requested);
    TEST_CHECK(snapshot.position.x, 40.0);
    TEST_CHECK(snapshot.size.width, 600.0);
    TEST_CHECK(snapshot.role != NULL, true);
    if (snapshot.role) TEST_CHECK(CFEqual(snapshot.role, kAXWindowRole), true);
    TEST_CHECK(window_ax_snapshot_test_multiple_calls, 1);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 3);
    window_ax_snapshot_destroy(&snapshot);
    window_ax_snapshot_test_end();

    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_VALID);
    window_ax_snapshot_test_create_failure = true;
    TEST_CHECK(window_ax_snapshot_read(ref, WINDOW_AX_ROLE, &snapshot), false);
    TEST_CHECK(snapshot.requested, WINDOW_AX_ROLE);
    TEST_CHECK(snapshot.valid, 0U);
    TEST_CHECK(snapshot.slot_error[WINDOW_AX_ROLE_SLOT], kAXErrorAttributeUnsupported);
    TEST_CHECK(window_ax_snapshot_test_multiple_calls, 0);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 1);
    window_ax_snapshot_destroy(&snapshot);
    window_ax_snapshot_test_end();

    CFRelease(ref);
});

static uint32_t window_create_test_parent_id;
static bool window_create_test_shadow_result;
static bool window_create_test_sticky_result;
static uint64_t window_create_test_space_id;
static bool window_create_test_space_fullscreen_result;
static bool window_create_test_can_move_result;
static bool window_create_test_can_resize_result;
static int window_create_test_parent_calls;
static int window_create_test_shadow_calls;
static int window_create_test_sticky_calls;
static int window_create_test_space_calls;
static int window_create_test_space_fullscreen_calls;
static uint64_t window_create_test_last_space_id;
static int window_create_test_can_move_calls;
static int window_create_test_can_resize_calls;

static uint32_t window_create_test_parent(uint32_t wid)
{
    (void) wid;
    ++window_create_test_parent_calls;
    return window_create_test_parent_id;
}

static bool window_create_test_shadow(uint32_t wid)
{
    (void) wid;
    ++window_create_test_shadow_calls;
    return window_create_test_shadow_result;
}

static bool window_create_test_sticky(uint32_t wid)
{
    (void) wid;
    ++window_create_test_sticky_calls;
    return window_create_test_sticky_result;
}

static uint64_t window_create_test_space(uint32_t wid)
{
    (void) wid;
    ++window_create_test_space_calls;
    return window_create_test_space_id;
}

static bool window_create_test_space_is_fullscreen(uint64_t sid)
{
    window_create_test_last_space_id = sid;
    ++window_create_test_space_fullscreen_calls;
    return window_create_test_space_fullscreen_result;
}

static bool window_create_test_can_move(struct window *window)
{
    (void) window;
    ++window_create_test_can_move_calls;
    return window_create_test_can_move_result;
}

static bool window_create_test_can_resize(struct window *window)
{
    (void) window;
    ++window_create_test_can_resize_calls;
    return window_create_test_can_resize_result;
}

static void window_create_test_begin(void)
{
    window_create_test_parent_id = 0;
    window_create_test_shadow_result = false;
    window_create_test_sticky_result = false;
    window_create_test_space_id = 0;
    window_create_test_space_fullscreen_result = false;
    window_create_test_can_move_result = false;
    window_create_test_can_resize_result = false;
    window_create_test_parent_calls = 0;
    window_create_test_shadow_calls = 0;
    window_create_test_sticky_calls = 0;
    window_create_test_space_calls = 0;
    window_create_test_space_fullscreen_calls = 0;
    window_create_test_last_space_id = 0;
    window_create_test_can_move_calls = 0;
    window_create_test_can_resize_calls = 0;
    window_create_parent_id = window_create_test_parent;
    window_create_shadow = window_create_test_shadow;
    window_create_sticky = window_create_test_sticky;
    window_create_space_id = window_create_test_space;
    window_create_space_is_fullscreen = window_create_test_space_is_fullscreen;
    window_create_can_move = window_create_test_can_move;
    window_create_can_resize = window_create_test_can_resize;
}

static void window_create_test_end(void)
{
    window_create_parent_id = window_parent;
    window_create_shadow = window_shadow;
    window_create_sticky = window_is_sticky;
    window_create_space_id = window_space;
    window_create_space_is_fullscreen = space_is_fullscreen;
    window_create_can_move = window_ax_can_move;
    window_create_can_resize = window_ax_can_resize;
}

TEST_FUNC(window_create_uses_one_complete_snapshot_and_independent_capabilities,
{
    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_VALID);
    window_create_test_begin();

    struct application application = {0};
    application.ref = AXUIElementCreateApplication(getpid());
    window_ax_snapshot_test_parent_value = application.ref;
    window_create_test_parent_id = 99;
    window_create_test_shadow_result = true;
    window_create_test_sticky_result = true;
    window_create_test_space_id = 42;
    window_create_test_can_move_result = true;
    window_create_test_can_resize_result = false;

    AXUIElementRef ref = AXUIElementCreateApplication(getpid());
    struct window *window = window_create(&application, ref, 77);
    TEST_CHECK(window != NULL, true);
    if (window) {
        TEST_CHECK(CGRectEqualToRect(window->frame, CGRectMake(10, 20, 300, 200)), true);
        TEST_CHECK(window->role != NULL, true);
        if (window->role) TEST_CHECK(CFEqual(window->role, kAXWindowRole), true);
        TEST_CHECK(window->subrole != NULL, true);
        if (window->subrole) TEST_CHECK(CFEqual(window->subrole, kAXStandardWindowSubrole), true);
        TEST_CHECK(window->title != NULL, true);
        if (window->title) TEST_CHECK(CFEqual(window->title, CFSTR("snapshot-title")), true);
        TEST_CHECK(window->is_root, true);
        TEST_CHECK(window_check_flag(window, WINDOW_SHADOW), true);
        TEST_CHECK(window_check_flag(window, WINDOW_MOVABLE), true);
        TEST_CHECK(window_check_flag(window, WINDOW_RESIZABLE), false);
        TEST_CHECK(window_check_flag(window, WINDOW_MINIMIZE), false);
        TEST_CHECK(window_check_flag(window, WINDOW_FULLSCREEN), true);
        TEST_CHECK(window_check_flag(window, WINDOW_STICKY), true);
        window_destroy(window);
    }

    TEST_CHECK(window_ax_snapshot_test_multiple_calls, 1);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 0);
    TEST_CHECK(window_ax_snapshot_test_last_options, 0U);
    TEST_CHECK(window_ax_snapshot_test_last_attribute_count, 8L);
    TEST_CHECK(window_ax_snapshot_test_attribute_order_valid, true);
    TEST_CHECK(window_create_test_parent_calls, 1);
    TEST_CHECK(window_create_test_shadow_calls, 1);
    TEST_CHECK(window_create_test_sticky_calls, 1);
    TEST_CHECK(window_create_test_can_move_calls, 1);
    TEST_CHECK(window_create_test_can_resize_calls, 1);
    TEST_CHECK(window_create_test_space_calls, 0);
    TEST_CHECK(window_create_test_space_fullscreen_calls, 0);

    CFRelease(application.ref);
    window_create_test_end();
    window_ax_snapshot_test_end();
});

TEST_FUNC(window_create_preserves_partial_snapshot_root_and_space_fallbacks,
{
    struct application application = {0};
    application.ref = AXUIElementCreateApplication(getpid());

    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_MIXED);
    window_create_test_begin();
    window_create_test_parent_id = 99;
    window_create_test_can_move_result = false;
    window_create_test_can_resize_result = true;
    AXUIElementRef ref = AXUIElementCreateApplication(getpid());
    struct window *window = window_create(&application, ref, 78);
    TEST_CHECK(window != NULL, true);
    if (window) {
        TEST_CHECK(CGRectEqualToRect(window->frame, CGRectMake(40, 50, 600, 700)), true);
        TEST_CHECK(window->role == NULL, true);
        TEST_CHECK(window->subrole != NULL, true);
        if (window->subrole) TEST_CHECK(CFEqual(window->subrole, kAXDialogSubrole), true);
        TEST_CHECK(window->title == NULL, true);
        TEST_CHECK(window->is_root, false);
        TEST_CHECK(window_check_flag(window, WINDOW_MINIMIZE), true);
        TEST_CHECK(window_check_flag(window, WINDOW_FULLSCREEN), true);
        TEST_CHECK(window_check_flag(window, WINDOW_MOVABLE), false);
        TEST_CHECK(window_check_flag(window, WINDOW_RESIZABLE), true);
        window_destroy(window);
    }
    TEST_CHECK(window_ax_snapshot_test_multiple_calls, 1);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 5);
    TEST_CHECK(window_create_test_space_calls, 0);
    window_create_test_end();
    window_ax_snapshot_test_end();

    window_ax_snapshot_test_begin(WINDOW_AX_SNAPSHOT_TEST_BATCH_FAILURE);
    window_create_test_begin();
    window_ax_snapshot_test_parent_value = application.ref;
    window_create_test_parent_id = 99;
    window_create_test_space_id = 44;
    window_create_test_space_fullscreen_result = true;
    ref = AXUIElementCreateApplication(getpid());
    window = window_create(&application, ref, 79);
    TEST_CHECK(window != NULL, true);
    if (window) {
        TEST_CHECK(window->is_root, true);
        TEST_CHECK(window_check_flag(window, WINDOW_MINIMIZE), true);
        TEST_CHECK(window_check_flag(window, WINDOW_FULLSCREEN), true);
        TEST_CHECK(window->role != NULL, true);
        if (window->role) TEST_CHECK(CFEqual(window->role, kAXWindowRole), true);
        window_destroy(window);
    }
    TEST_CHECK(window_ax_snapshot_test_multiple_calls, 1);
    TEST_CHECK(window_ax_snapshot_test_single_calls, 8);
    TEST_CHECK(window_create_test_space_calls, 1);
    TEST_CHECK(window_create_test_space_fullscreen_calls, 1);
    TEST_CHECK(window_create_test_last_space_id, 44ULL);
    window_create_test_end();
    window_ax_snapshot_test_end();

    CFRelease(application.ref);
});

static AXError window_test_observe_result;
static int window_test_observe_call_count;

static AXError window_test_add_notification(AXObserverRef observer, AXUIElementRef element,
                                            CFStringRef notification, void *context)
{
    (void) observer;
    (void) element;
    (void) notification;
    (void) context;
    return window_test_observe_result;
}

static AXError window_test_add_mixed_notification(AXObserverRef observer, AXUIElementRef element,
                                                  CFStringRef notification, void *context)
{
    (void) observer;
    (void) element;
    (void) notification;
    (void) context;
    return window_test_observe_call_count++ == 0 ? kAXErrorCannotComplete : kAXErrorNotImplemented;
}

TEST_FUNC(window_observe_retries_only_transient_failures,
{
    struct application application = {0};
    struct window window = { .application = &application };
    bool retry = false;
    __auto_type saved_add_notification = window_observe_add_notification;
    window_observe_add_notification = window_test_add_notification;

    window_test_observe_result = kAXErrorCannotComplete;
    TEST_CHECK(window_observe(&window, &retry), false);
    TEST_CHECK(retry, true);
    TEST_CHECK(window.notification, 0U);

    retry = true;
    window_test_observe_result = kAXErrorNotImplemented;
    TEST_CHECK(window_observe(&window, &retry), false);
    TEST_CHECK(retry, false);
    TEST_CHECK(window.notification, 0U);

    window_observe_add_notification = window_test_add_mixed_notification;
    window_test_observe_call_count = 0;
    TEST_CHECK(window_observe(&window, &retry), false);
    TEST_CHECK(retry, false);
    TEST_CHECK(window.notification, 0U);

    window_observe_add_notification = window_test_add_notification;
    window_test_observe_result = kAXErrorSuccess;
    TEST_CHECK(window_observe(&window, &retry), true);
    TEST_CHECK(retry, false);
    TEST_CHECK(window.notification, AX_WINDOW_ALL);

    window_observe_add_notification = saved_add_notification;
});

static CGError window_test_ordered_in_success(int connection, uint32_t wid, uint8_t *ordered_in)
{
    (void) connection;
    (void) wid;
    *ordered_in = 1;
    return kCGErrorSuccess;
}

static CGError window_test_ordered_in_failure(int connection, uint32_t wid, uint8_t *ordered_in)
{
    (void) connection;
    (void) wid;
    (void) ordered_in;
    return kCGErrorFailure;
}

static CFArrayRef window_test_on_screen_description(CFArrayRef windows)
{
    (void) windows;
    const void *keys[] = { kCGWindowIsOnscreen };
    const void *values[] = { kCFBooleanTrue };
    CFDictionaryRef description = CFDictionaryCreate(NULL, keys, values, 1,
                                                       &kCFTypeDictionaryKeyCallBacks,
                                                       &kCFTypeDictionaryValueCallBacks);
    const void *descriptions[] = { description };
    CFArrayRef result = CFArrayCreate(NULL, descriptions, 1, &kCFTypeArrayCallBacks);
    CFRelease(description);
    return result;
}

static CFArrayRef window_test_off_screen_description(CFArrayRef windows)
{
    (void) windows;
    const void *keys[] = { kCGWindowIsOnscreen };
    const void *values[] = { kCFBooleanFalse };
    CFDictionaryRef description = CFDictionaryCreate(NULL, keys, values, 1,
                                                       &kCFTypeDictionaryKeyCallBacks,
                                                       &kCFTypeDictionaryValueCallBacks);
    const void *descriptions[] = { description };
    CFArrayRef result = CFArrayCreate(NULL, descriptions, 1, &kCFTypeArrayCallBacks);
    CFRelease(description);
    return result;
}

static CFArrayRef window_test_missing_screen_description(CFArrayRef windows)
{
    (void) windows;
    CFDictionaryRef description = CFDictionaryCreate(NULL, NULL, NULL, 0,
                                                       &kCFTypeDictionaryKeyCallBacks,
                                                       &kCFTypeDictionaryValueCallBacks);
    const void *descriptions[] = { description };
    CFArrayRef result = CFArrayCreate(NULL, descriptions, 1, &kCFTypeArrayCallBacks);
    CFRelease(description);
    return result;
}

static CFArrayRef window_test_invalid_screen_description(CFArrayRef windows)
{
    (void) windows;
    const void *keys[] = { kCGWindowIsOnscreen };
    const void *values[] = { CFSTR("invalid") };
    CFDictionaryRef description = CFDictionaryCreate(NULL, keys, values, 1,
                                                       &kCFTypeDictionaryKeyCallBacks,
                                                       &kCFTypeDictionaryValueCallBacks);
    const void *descriptions[] = { description };
    CFArrayRef result = CFArrayCreate(NULL, descriptions, 1, &kCFTypeArrayCallBacks);
    CFRelease(description);
    return result;
}

TEST_FUNC(window_diagnostic_visibility_queries_preserve_unknown_state,
{
    __auto_type saved_ordered = window_test_is_ordered_in;
    __auto_type saved_descriptions = window_test_description_from_array;
    TEST_CHECK(window_serialization_flags(0) & WINDOW_PROPERTY_ID, WINDOW_PROPERTY_ID);
    TEST_CHECK(window_serialization_flags(0) & WINDOW_PROPERTY_DIAGNOSTIC_FLAGS, 0ULL);
    TEST_CHECK(window_serialization_flags(WINDOW_PROPERTY_TAGS), WINDOW_PROPERTY_TAGS);
    bool known = true;

    window_test_is_ordered_in = window_test_ordered_in_success;
    TEST_CHECK(window_is_ordered_in(77, &known), true);
    TEST_CHECK(known, true);

    window_test_is_ordered_in = window_test_ordered_in_failure;
    known = true;
    TEST_CHECK(window_is_ordered_in(77, &known), false);
    TEST_CHECK(known, false);
    known = true;
    TEST_CHECK(window_is_ordered_in(0, &known), false);
    TEST_CHECK(known, false);

    window_test_description_from_array = window_test_on_screen_description;
    TEST_CHECK(window_is_on_screen(77, &known), true);
    TEST_CHECK(known, true);

    window_test_description_from_array = window_test_off_screen_description;
    known = false;
    TEST_CHECK(window_is_on_screen(77, &known), false);
    TEST_CHECK(known, true);

    window_test_description_from_array = window_test_missing_screen_description;
    known = true;
    TEST_CHECK(window_is_on_screen(77, &known), false);
    TEST_CHECK(known, false);
    struct window missing = { .id = 77 };
    FILE *missing_file = tmpfile();
    TEST_CHECK(missing_file != NULL, true);
    if (missing_file) {
        window_serialize(missing_file, &missing, WINDOW_PROPERTY_IS_ON_SCREEN);
        rewind(missing_file);
        char missing_buffer[128] = {0};
        fread(missing_buffer, 1, sizeof(missing_buffer) - 1, missing_file);
        fclose(missing_file);
        TEST_CHECK(strstr(missing_buffer, "\"is-on-screen\":null") != NULL, true);
    }

    window_test_description_from_array = window_test_invalid_screen_description;
    known = true;
    TEST_CHECK(window_is_on_screen(77, &known), false);
    TEST_CHECK(known, false);

    window_test_is_ordered_in = window_test_ordered_in_success;
    window_test_description_from_array = window_test_on_screen_description;
    struct window window = { .id = UINT32_C(0x7fffffff), .is_root = true };
    FILE *file = tmpfile();
    TEST_CHECK(file != NULL, true);
    if (file) {
        window_serialize(file, &window, WINDOW_PROPERTY_IS_ORDERED_IN | WINDOW_PROPERTY_IS_ON_SCREEN);
        rewind(file);
        char buffer[512] = {0};
        fread(buffer, 1, sizeof(buffer) - 1, file);
        fclose(file);
        TEST_CHECK(strstr(buffer, "\"is-ordered-in\":true") != NULL, true);
        TEST_CHECK(strstr(buffer, "\"is-on-screen\":true") != NULL, true);
    }

    window_test_is_ordered_in = saved_ordered;
    window_test_description_from_array = saved_descriptions;
});

enum window_serialization_membership_fixture_mode
{
    WINDOW_SERIALIZATION_MEMBERSHIP_LIST,
    WINDOW_SERIALIZATION_MEMBERSHIP_EMPTY,
    WINDOW_SERIALIZATION_MEMBERSHIP_MALFORMED,
    WINDOW_SERIALIZATION_MEMBERSHIP_FAILURE
};

static enum window_serialization_membership_fixture_mode window_serialization_membership_mode;
static int window_serialization_membership_copy_calls;
static int window_serialization_membership_fallback_calls;
static int window_serialization_membership_combined_calls;
static int window_serialization_membership_space_calls;
static int window_serialization_membership_sticky_calls;

static CFArrayRef window_serialization_membership_copy_spaces(int connection, int selector,
                                                              CFArrayRef windows)
{
    (void) connection;
    (void) selector;
    (void) windows;
    ++window_serialization_membership_copy_calls;
    if (window_serialization_membership_mode == WINDOW_SERIALIZATION_MEMBERSHIP_FAILURE) return NULL;
    if (window_serialization_membership_mode == WINDOW_SERIALIZATION_MEMBERSHIP_EMPTY) {
        return CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
    }
    if (window_serialization_membership_mode == WINDOW_SERIALIZATION_MEMBERSHIP_MALFORMED) {
        const void *values[] = { CFSTR("invalid") };
        return CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
    }
    uint64_t spaces[] = { 41, 42 };
    return cfarray_of_cfnumbers(spaces, sizeof(uint64_t), array_count(spaces), kCFNumberSInt64Type);
}

static uint64_t window_serialization_membership_fallback(uint32_t wid)
{
    (void) wid;
    ++window_serialization_membership_fallback_calls;
    return 99;
}

static void window_serialization_membership_combined(uint32_t wid, uint64_t *sid, bool *is_sticky)
{
    (void) wid;
    ++window_serialization_membership_combined_calls;
    *sid = 41;
    *is_sticky = true;
}

static uint64_t window_serialization_membership_space(uint32_t wid)
{
    (void) wid;
    ++window_serialization_membership_space_calls;
    return 42;
}

static bool window_serialization_membership_sticky(uint32_t wid)
{
    (void) wid;
    ++window_serialization_membership_sticky_calls;
    return true;
}

static void window_serialization_membership_reset_counts(void)
{
    window_serialization_membership_copy_calls = 0;
    window_serialization_membership_fallback_calls = 0;
    window_serialization_membership_combined_calls = 0;
    window_serialization_membership_space_calls = 0;
    window_serialization_membership_sticky_calls = 0;
}

TEST_FUNC(window_serialization_reuses_space_membership_and_preserves_fallbacks,
{
    __auto_type saved_copy_spaces = window_test_serialization_copy_spaces;
    __auto_type saved_display_space = window_test_serialization_display_space;
    __auto_type saved_membership = window_test_serialization_membership;
    __auto_type saved_space = window_test_serialization_space;
    __auto_type saved_sticky = window_test_serialization_is_sticky;
    window_test_serialization_copy_spaces = window_serialization_membership_copy_spaces;
    window_test_serialization_display_space = window_serialization_membership_fallback;

    uint64_t sid = 0;
    bool is_sticky = false;
    window_serialization_membership_mode = WINDOW_SERIALIZATION_MEMBERSHIP_LIST;
    window_serialization_membership_reset_counts();
    window_serialization_membership(77, &sid, &is_sticky);
    TEST_CHECK(sid, 41ULL);
    TEST_CHECK(is_sticky, true);
    TEST_CHECK(window_serialization_membership_copy_calls, 1);
    TEST_CHECK(window_serialization_membership_fallback_calls, 0);

    window_serialization_membership_mode = WINDOW_SERIALIZATION_MEMBERSHIP_EMPTY;
    window_serialization_membership_reset_counts();
    window_serialization_membership(77, &sid, &is_sticky);
    TEST_CHECK(sid, 99ULL);
    TEST_CHECK(is_sticky, false);
    TEST_CHECK(window_serialization_membership_copy_calls, 1);
    TEST_CHECK(window_serialization_membership_fallback_calls, 1);

    window_serialization_membership_mode = WINDOW_SERIALIZATION_MEMBERSHIP_MALFORMED;
    window_serialization_membership_reset_counts();
    window_serialization_membership(77, &sid, &is_sticky);
    TEST_CHECK(sid, 99ULL);
    TEST_CHECK(is_sticky, false);
    TEST_CHECK(window_serialization_membership_copy_calls, 1);
    TEST_CHECK(window_serialization_membership_fallback_calls, 1);

    window_serialization_membership_mode = WINDOW_SERIALIZATION_MEMBERSHIP_FAILURE;
    window_serialization_membership_reset_counts();
    window_serialization_membership(77, &sid, &is_sticky);
    TEST_CHECK(sid, 99ULL);
    TEST_CHECK(is_sticky, false);
    TEST_CHECK(window_serialization_membership_copy_calls, 1);
    TEST_CHECK(window_serialization_membership_fallback_calls, 1);

    NSArray *topology = @[
        @{ @"Display Identifier": @"display-one",
           @"Spaces": @[ @{ @"id64": @41 }, @{ @"id64": @42 }, @{ @"id64": @99 } ] }
    ];
    struct window_serialization_context context = {
        .managed_display_spaces = (CFArrayRef) topology,
        .has_managed_display_spaces_snapshot = true
    };
    struct window window = { .id = 77, .is_root = true };
    window_test_serialization_membership = window_serialization_membership_combined;
    window_test_serialization_space = window_serialization_membership_space;
    window_test_serialization_is_sticky = window_serialization_membership_sticky;

    window_serialization_membership_reset_counts();
    FILE *tracked = tmpfile();
    TEST_CHECK(tracked != NULL, true);
    if (tracked) {
        window_serialize_with_context(tracked, &window,
                                      WINDOW_PROPERTY_SPACE | WINDOW_PROPERTY_IS_STICKY,
                                      &context);
        rewind(tracked);
        char output[256] = {0};
        fread(output, 1, sizeof(output) - 1, tracked);
        fclose(tracked);
        TEST_CHECK(strstr(output, "\"space\":1") != NULL, true);
        TEST_CHECK(strstr(output, "\"is-sticky\":true") != NULL, true);
    }
    TEST_CHECK(window_serialization_membership_combined_calls, 1);
    TEST_CHECK(window_serialization_membership_space_calls, 0);
    TEST_CHECK(window_serialization_membership_sticky_calls, 0);

    window_serialization_membership_reset_counts();
    FILE *nonax = tmpfile();
    TEST_CHECK(nonax != NULL, true);
    if (nonax) {
        window_nonax_serialize_with_context(nonax, 78,
                                            WINDOW_PROPERTY_SPACE | WINDOW_PROPERTY_IS_STICKY,
                                            &context);
        rewind(nonax);
        char output[256] = {0};
        fread(output, 1, sizeof(output) - 1, nonax);
        fclose(nonax);
        TEST_CHECK(strstr(output, "\"space\":1") != NULL, true);
        TEST_CHECK(strstr(output, "\"is-sticky\":true") != NULL, true);
    }
    TEST_CHECK(window_serialization_membership_combined_calls, 1);
    TEST_CHECK(window_serialization_membership_space_calls, 0);
    TEST_CHECK(window_serialization_membership_sticky_calls, 0);

    window_serialization_membership_reset_counts();
    FILE *space_only = tmpfile();
    TEST_CHECK(space_only != NULL, true);
    if (space_only) {
        window_serialize_with_context(space_only, &window, WINDOW_PROPERTY_SPACE, &context);
        fclose(space_only);
    }
    TEST_CHECK(window_serialization_membership_combined_calls, 0);
    TEST_CHECK(window_serialization_membership_space_calls, 1);
    TEST_CHECK(window_serialization_membership_sticky_calls, 0);

    window_serialization_membership_reset_counts();
    FILE *sticky_only = tmpfile();
    TEST_CHECK(sticky_only != NULL, true);
    if (sticky_only) {
        window_serialize_with_context(sticky_only, &window, WINDOW_PROPERTY_IS_STICKY, &context);
        fclose(sticky_only);
    }
    TEST_CHECK(window_serialization_membership_combined_calls, 0);
    TEST_CHECK(window_serialization_membership_space_calls, 0);
    TEST_CHECK(window_serialization_membership_sticky_calls, 1);

    window.flags = WINDOW_STICKY;
    window_serialization_membership_reset_counts();
    FILE *cached_sticky = tmpfile();
    TEST_CHECK(cached_sticky != NULL, true);
    if (cached_sticky) {
        window_serialize_with_context(cached_sticky, &window,
                                      WINDOW_PROPERTY_SPACE | WINDOW_PROPERTY_IS_STICKY,
                                      &context);
        fclose(cached_sticky);
    }
    TEST_CHECK(window_serialization_membership_combined_calls, 0);
    TEST_CHECK(window_serialization_membership_space_calls, 1);
    TEST_CHECK(window_serialization_membership_sticky_calls, 0);

    window_test_serialization_copy_spaces = saved_copy_spaces;
    window_test_serialization_display_space = saved_display_space;
    window_test_serialization_membership = saved_membership;
    window_test_serialization_space = saved_space;
    window_test_serialization_is_sticky = saved_sticky;
});

TEST_FUNC(window_serialization_reports_singleton_fullscreen_zoom,
{
    struct table saved_managed = g_window_manager.managed_window;
    memset(&g_window_manager.managed_window, 0, sizeof(g_window_manager.managed_window));
    table_init(&g_window_manager.managed_window, 8, hash_wm, compare_wm);

    struct window window = { .id = 77 };
    struct window_node root = {
        .window_count = 1, .window_list = { 77 }, .window_order = { 77 }
    };
    struct view view = {
        .layout = VIEW_BSP, .root = &root, .flags = VIEW_SINGLE_WINDOW_ZOOM
    };
    table_add(&g_window_manager.managed_window, &window.id, &view);

    FILE *file = tmpfile();
    TEST_CHECK(file != NULL, true);
    if (file) {
        window_serialize(file, &window, WINDOW_PROPERTY_HAS_FULLSCREEN_ZOOM);
        rewind(file);
        char output[256] = {0};
        fread(output, 1, sizeof(output) - 1, file);
        TEST_CHECK(strstr(output, "\"has-fullscreen-zoom\":true") != NULL, true);
        fclose(file);
    }

    view_clear_flag(&view, VIEW_SINGLE_WINDOW_ZOOM);
    file = tmpfile();
    TEST_CHECK(file != NULL, true);
    if (file) {
        window_serialize(file, &window, WINDOW_PROPERTY_HAS_FULLSCREEN_ZOOM);
        rewind(file);
        char output[256] = {0};
        fread(output, 1, sizeof(output) - 1, file);
        TEST_CHECK(strstr(output, "\"has-fullscreen-zoom\":false") != NULL, true);
        fclose(file);
    }

    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed;
});

static char *lifecycle_test_cfstring_copy_failure(CFStringRef value)
{
    (void) value;
    return NULL;
}

static AXUIElementRef lifecycle_test_ax_application_failure(pid_t pid)
{
    (void) pid;
    return NULL;
}

TEST_FUNC(process_create_allocation_failure_does_not_publish_owned_state,
{
    ProcessSerialNumber psn = {0};
    TEST_CHECK(GetCurrentProcess(&psn), noErr);
    legacy_allocation_test_fail_at(1);
    TEST_CHECK(process_create(psn, getpid()) == NULL, true);
    legacy_allocation_test_reset();
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);

    __auto_type saved_copy = process_create_cfstring_copy;
    process_create_cfstring_copy = lifecycle_test_cfstring_copy_failure;
    TEST_CHECK(process_create(psn, getpid()) == NULL, true);
    process_create_cfstring_copy = saved_copy;
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
});

TEST_FUNC(application_create_rejects_invalid_and_allocation_failure,
{
    TEST_CHECK(application_create(NULL) == NULL, true);
    struct process process = { .pid = getpid(), .name = "test" };
    legacy_allocation_test_fail_at(1);
    TEST_CHECK(application_create(&process) == NULL, true);
    legacy_allocation_test_reset();
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);

    __auto_type saved_create = application_create_ax_element;
    application_create_ax_element = lifecycle_test_ax_application_failure;
    TEST_CHECK(application_create(&process) == NULL, true);
    application_create_ax_element = saved_create;
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
});

TEST_FUNC(window_create_allocation_failure_consumes_retained_reference,
{
    struct application application = {0};
    AXUIElementRef ref = AXUIElementCreateApplication(getpid());
    CFRetain(ref);
    CFIndex before = CFGetRetainCount(ref);
    legacy_allocation_test_fail_at(1);
    TEST_CHECK(window_create(&application, ref, 77) == NULL, true);
    legacy_allocation_test_reset();
    TEST_CHECK(CFGetRetainCount(ref), before - 1);
    TEST_CHECK(legacy_allocation_test_outstanding(), 0UL);
    CFRelease(ref);
});

TEST_FUNC(window_subrole_is_unknown,
{
    TEST_CHECK(window_subrole_is_unknown(kAXUnknownSubrole), true);
    TEST_CHECK(window_subrole_is_unknown(kAXSystemDialogSubrole), false);
    TEST_CHECK(window_subrole_is_unknown(CFSTR("AXUnknownDialog")), false);
    TEST_CHECK(window_subrole_is_unknown(NULL), false);
});

TEST_FUNC(window_is_unknown_does_not_consume_cached_subrole,
{
    struct window window = {0};
    window.ref = AXUIElementCreateApplication(getpid());
    window.subrole = CFStringCreateCopy(NULL, kAXUnknownSubrole);

    CFIndex retain_count_before = CFGetRetainCount(window.subrole);
    bool result = window_is_unknown(&window);
    CFIndex retain_count_after = CFGetRetainCount(window.subrole);

    TEST_CHECK(result, false);
    TEST_CHECK(retain_count_after, retain_count_before);
    TEST_CHECK(CFEqual(window.subrole, kAXUnknownSubrole), true);

    CFRelease(window.subrole);
    CFRelease(window.ref);
});

TEST_FUNC(window_unobserve_without_application_clears_notifications,
{
    struct window window = {0};
    window.ref = AXUIElementCreateApplication(getpid());
    window.notification = AX_WINDOW_ALL;

    window_unobserve(&window);

    TEST_CHECK(window.notification, 0u);

    CFRelease(window.ref);
});
