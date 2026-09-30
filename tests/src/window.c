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
