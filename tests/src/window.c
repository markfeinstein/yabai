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
