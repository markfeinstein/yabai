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
