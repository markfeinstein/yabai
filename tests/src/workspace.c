static workspace_context *workspace_test_create_nil(void)
{
    return nil;
}

static workspace_context *workspace_test_create_sentinel(void)
{
    return (workspace_context *)(uintptr_t) 0x1234;
}

TEST_FUNC(workspace_event_handler_uses_initializer_result_and_rejects_invalid_output,
{
    long saved_macos_version = workspace_macos_major_version();
    TEST_CHECK(workspace_event_handler_begin(NULL), false);

    TEST_OVERRIDE(workspace_test_context_create, workspace_test_create_nil);
    void *context = (void *)(uintptr_t) 0xfeed;
    TEST_CHECK(workspace_event_handler_begin(&context), false);
    TEST_CHECK(context, (void *)(uintptr_t) 0xfeed);

    workspace_test_context_create = workspace_test_create_sentinel;
    TEST_CHECK(workspace_event_handler_begin(&context), true);
    TEST_CHECK(context, (void *)(uintptr_t) 0x1234);
    workspace_set_macos_version(saved_macos_version);
});
