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
