struct wm_bridge_fake_class
{
    const char *name;
    struct wm_bridge_fake_class *superclass;
};

static struct wm_bridge_fake_class wm_fake_client;
static struct wm_bridge_fake_class wm_fake_sync;
static struct wm_bridge_fake_class wm_fake_async;
static struct wm_bridge_fake_class wm_fake_result_base;
static struct wm_bridge_fake_class wm_fake_copy;
static struct wm_bridge_fake_class wm_fake_array_result;
static struct wm_bridge_fake_class wm_fake_create;
static struct wm_bridge_fake_class wm_fake_destroy;
static struct wm_bridge_fake_class wm_fake_move;
static struct wm_bridge_fake_class wm_fake_focus;
static struct wm_bridge_fake_class wm_fake_space_id_result;
static const char *wm_fake_missing_class;
static const char *wm_fake_missing_selector;
static const char *wm_fake_wrong_selector;
static bool wm_fake_fail_skylight;
static bool wm_fake_fail_window_management;
static bool wm_fake_nil_manager;
static bool wm_fake_nil_operation;
static bool wm_fake_nil_result;
static bool wm_fake_wrong_result_class;
static id wm_fake_property_list;
static int wm_fake_manager_init_calls;
static int wm_fake_copy_init_calls;
static int wm_fake_mutation_init_calls;
static int wm_fake_sync_dispatch_calls;
static int wm_fake_async_dispatch_calls;
static int wm_fake_invalidate_calls;
static int wm_fake_release_calls;
static int wm_fake_open_calls;
static int wm_fake_close_calls;
static int wm_fake_manager_token;
static int wm_fake_operation_token;
static int wm_fake_result_token;
static int wm_fake_skylight_token;
static int wm_fake_window_management_token;

struct wm_bridge_fake_method
{
    const char *class_name;
    const char *selector;
    const char *encoding;
};

static const struct wm_bridge_fake_method wm_fake_methods[] = {
    { "WMClientWindowManager", "init", "@16@0:8" },
    { "WMClientWindowManager", "invalidate", "v16@0:8" },
    { "WMClientWindowManager", "performSynchronousBridgedWindowManagementOperation:", "@24@0:8@16" },
    { "WMClientWindowManager", "performAsynchronousBridgedWindowManagementOperation:", "v24@0:8@16" },
    { "SLSBridgedCopyManagedDisplaySpacesOperation", "init", "@16@0:8" },
    { "SLSBridgedCopyManagedDisplaySpacesOperation", "makeResultWithPropertyListArray:", "@24@0:8@16" },
    { "SLSBridgedWindowManagementOperationPropertyListArrayResult", "propertyListArray", "@16@0:8" },
    { "SLSBridgedSpaceCreateOperation", "initWithOptions:values:", "@28@0:8I16@20" },
    { "SLSBridgedSpaceDestroyOperation", "initWithSpaceID:", "@24@0:8Q16" },
    { "SLSBridgedMoveManagedSpaceToDisplayIndexOperation", "initWithSpaceID:displayIdentifier:index:", "@36@0:8Q16@24I32" },
    { "SLSBridgedManagedDisplaySetCurrentSpaceOperation", "initWithDisplayIdentifier:spaceID:", "@32@0:8@16Q24" },
    { "SLSBridgedSpaceCreateOperation", "makeResultWithSpaceID:", "@24@0:8Q16" },
    { "SLSBridgedWindowManagementOperationSpaceIDResult", "initWithSpaceID:", "@24@0:8Q16" },
    { "SLSBridgedWindowManagementOperationSpaceIDResult", "spaceID", "Q16@0:8" }
};

static struct wm_bridge_fake_class *wm_fake_class_named(const char *name)
{
    struct wm_bridge_fake_class *classes[] = {
        &wm_fake_client, &wm_fake_sync, &wm_fake_async, &wm_fake_result_base,
        &wm_fake_copy, &wm_fake_array_result, &wm_fake_create, &wm_fake_destroy,
        &wm_fake_move, &wm_fake_focus, &wm_fake_space_id_result
    };
    for (size_t i = 0; i < array_count(classes); ++i) {
        if (strcmp(classes[i]->name, name) == 0) return classes[i];
    }
    return NULL;
}

static void *wm_fake_open_image(const char *path)
{
    ++wm_fake_open_calls;
    if (strcmp(path, WM_SKYLIGHT_PATH) == 0) {
        return wm_fake_fail_skylight ? NULL : &wm_fake_skylight_token;
    }
    if (strcmp(path, WM_FRAMEWORK_PATH) == 0) {
        return wm_fake_fail_window_management ? NULL : &wm_fake_window_management_token;
    }
    return NULL;
}

static void wm_fake_close_image(void *handle)
{
    if (handle) ++wm_fake_close_calls;
}

static Class wm_fake_lookup_class(const char *name)
{
    if (wm_fake_missing_class && strcmp(wm_fake_missing_class, name) == 0) return Nil;
    return (Class) wm_fake_class_named(name);
}

static Class wm_fake_superclass(Class cls)
{
    struct wm_bridge_fake_class *fake = (struct wm_bridge_fake_class *) cls;
    return fake ? (Class) fake->superclass : Nil;
}

static const char *wm_fake_method_encoding(Class cls, const char *selector)
{
    struct wm_bridge_fake_class *fake = (struct wm_bridge_fake_class *) cls;
    if (!fake || !selector) return NULL;
    if (wm_fake_missing_selector && strcmp(wm_fake_missing_selector, selector) == 0) return NULL;
    if (wm_fake_wrong_selector && strcmp(wm_fake_wrong_selector, selector) == 0) return "!wrong!";
    for (size_t i = 0; i < array_count(wm_fake_methods); ++i) {
        if (strcmp(wm_fake_methods[i].class_name, fake->name) == 0 &&
            strcmp(wm_fake_methods[i].selector, selector) == 0) {
            return wm_fake_methods[i].encoding;
        }
    }
    return NULL;
}

static id wm_fake_init_object(Class cls, const char *selector)
{
    struct wm_bridge_fake_class *fake = (struct wm_bridge_fake_class *) cls;
    if (fake == &wm_fake_client && strcmp(selector, "init") == 0) {
        ++wm_fake_manager_init_calls;
        return wm_fake_nil_manager ? nil : (id) &wm_fake_manager_token;
    }
    if (fake == &wm_fake_copy && strcmp(selector, "init") == 0) {
        ++wm_fake_copy_init_calls;
        return wm_fake_nil_operation ? nil : (id) &wm_fake_operation_token;
    }
    ++wm_fake_mutation_init_calls;
    return nil;
}

static id wm_fake_send_object0(id object, const char *selector)
{
    if (object == (id) &wm_fake_result_token && strcmp(selector, "propertyListArray") == 0) {
        return wm_fake_property_list;
    }
    return nil;
}

static id wm_fake_send_object1(id object, const char *selector, id argument)
{
    if (object == (id) &wm_fake_manager_token &&
        strcmp(selector, "performSynchronousBridgedWindowManagementOperation:") == 0 &&
        argument == (id) &wm_fake_operation_token) {
        ++wm_fake_sync_dispatch_calls;
        return wm_fake_nil_result ? nil : (id) &wm_fake_result_token;
    }
    if (strcmp(selector, "performAsynchronousBridgedWindowManagementOperation:") == 0) {
        ++wm_fake_async_dispatch_calls;
    }
    return nil;
}

static void wm_fake_send_void0(id object, const char *selector)
{
    if (object == (id) &wm_fake_manager_token && strcmp(selector, "invalidate") == 0) {
        ++wm_fake_invalidate_calls;
    }
}

static bool wm_fake_object_is_kind_of(id object, Class cls)
{
    return object == (id) &wm_fake_result_token && cls == (Class) &wm_fake_array_result &&
           !wm_fake_wrong_result_class;
}

static void wm_fake_release_object(id object)
{
    if (object) ++wm_fake_release_calls;
}

static const struct wm_bridge_runtime wm_fake_runtime = {
    .open_image = wm_fake_open_image,
    .close_image = wm_fake_close_image,
    .lookup_class = wm_fake_lookup_class,
    .superclass = wm_fake_superclass,
    .method_encoding = wm_fake_method_encoding,
    .init_object = wm_fake_init_object,
    .send_object0 = wm_fake_send_object0,
    .send_object1 = wm_fake_send_object1,
    .send_void0 = wm_fake_send_void0,
    .object_is_kind_of = wm_fake_object_is_kind_of,
    .release_object = wm_fake_release_object
};

static void wm_fake_begin(void)
{
    wm_fake_client = (struct wm_bridge_fake_class) { "WMClientWindowManager", NULL };
    wm_fake_sync = (struct wm_bridge_fake_class) { "SLSSynchronousBridgedWindowManagementOperation", NULL };
    wm_fake_async = (struct wm_bridge_fake_class) { "SLSAsynchronousBridgedWindowManagementOperation", NULL };
    wm_fake_result_base = (struct wm_bridge_fake_class) { "SLSBridgedWindowManagementOperationResult", NULL };
    wm_fake_copy = (struct wm_bridge_fake_class) { "SLSBridgedCopyManagedDisplaySpacesOperation", &wm_fake_sync };
    wm_fake_array_result = (struct wm_bridge_fake_class) { "SLSBridgedWindowManagementOperationPropertyListArrayResult", &wm_fake_result_base };
    wm_fake_create = (struct wm_bridge_fake_class) { "SLSBridgedSpaceCreateOperation", &wm_fake_sync };
    wm_fake_destroy = (struct wm_bridge_fake_class) { "SLSBridgedSpaceDestroyOperation", &wm_fake_async };
    wm_fake_move = (struct wm_bridge_fake_class) { "SLSBridgedMoveManagedSpaceToDisplayIndexOperation", &wm_fake_async };
    wm_fake_focus = (struct wm_bridge_fake_class) { "SLSBridgedManagedDisplaySetCurrentSpaceOperation", &wm_fake_async };
    wm_fake_space_id_result = (struct wm_bridge_fake_class) { "SLSBridgedWindowManagementOperationSpaceIDResult", &wm_fake_result_base };
    wm_fake_missing_class = NULL;
    wm_fake_missing_selector = NULL;
    wm_fake_wrong_selector = NULL;
    wm_fake_fail_skylight = false;
    wm_fake_fail_window_management = false;
    wm_fake_nil_manager = false;
    wm_fake_nil_operation = false;
    wm_fake_nil_result = false;
    wm_fake_wrong_result_class = false;
    wm_fake_property_list = @[ @{ @"sentinel": @"DO_NOT_PRINT_UUID_TITLE_VALUE" }, @{} ];
    wm_fake_manager_init_calls = 0;
    wm_fake_copy_init_calls = 0;
    wm_fake_mutation_init_calls = 0;
    wm_fake_sync_dispatch_calls = 0;
    wm_fake_async_dispatch_calls = 0;
    wm_fake_invalidate_calls = 0;
    wm_fake_release_calls = 0;
    wm_fake_open_calls = 0;
    wm_fake_close_calls = 0;
    window_management_bridge_test_runtime = &wm_fake_runtime;
}

static void wm_fake_end(void)
{
    window_management_bridge_test_runtime = NULL;
}

TEST_FUNC(window_management_bridge_exact_abi_runs_only_allowlisted_read,
{
    wm_fake_begin();
    struct wm_bridge_diagnostic diagnostic;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_OK);
    TEST_CHECK(diagnostic.managed_display_record_count, 2U);
    TEST_CHECK(diagnostic.capabilities.client_abi, true);
    TEST_CHECK(diagnostic.capabilities.copy_topology_abi, true);
    TEST_CHECK(diagnostic.capabilities.create_space_metadata, true);
    TEST_CHECK(diagnostic.capabilities.destroy_space_metadata, true);
    TEST_CHECK(diagnostic.capabilities.move_space_metadata, true);
    TEST_CHECK(diagnostic.capabilities.set_current_space_metadata, true);
    TEST_CHECK(diagnostic.capabilities.create_space_result_metadata, true);
    TEST_CHECK(wm_fake_manager_init_calls, 1);
    TEST_CHECK(wm_fake_copy_init_calls, 1);
    TEST_CHECK(wm_fake_sync_dispatch_calls, 1);
    TEST_CHECK(wm_fake_invalidate_calls, 1);
    TEST_CHECK(wm_fake_mutation_init_calls, 0);
    TEST_CHECK(wm_fake_async_dispatch_calls, 0);
    TEST_CHECK(wm_fake_release_calls, 3);
    TEST_CHECK(wm_fake_open_calls, 2);
    TEST_CHECK(wm_fake_close_calls, 2);
    wm_fake_end();
});

TEST_FUNC(window_management_bridge_missing_and_mismatched_abi_fails_closed,
{
    wm_fake_begin();
    wm_fake_fail_skylight = true;
    struct wm_bridge_diagnostic diagnostic;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_UNAVAILABLE);
    TEST_CHECK(wm_fake_manager_init_calls, 0);
    wm_fake_end();

    const char *required_classes[] = {
        "WMClientWindowManager", "SLSSynchronousBridgedWindowManagementOperation",
        "SLSAsynchronousBridgedWindowManagementOperation", "SLSBridgedWindowManagementOperationResult",
        "SLSBridgedCopyManagedDisplaySpacesOperation",
        "SLSBridgedWindowManagementOperationPropertyListArrayResult"
    };
    for (size_t i = 0; i < array_count(required_classes); ++i) {
        wm_fake_begin();
        wm_fake_missing_class = required_classes[i];
        TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
        TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_UNAVAILABLE);
        TEST_CHECK(wm_fake_manager_init_calls, 0);
        wm_fake_end();
    }

    for (size_t i = 0; i < 7; ++i) {
        wm_fake_begin();
        wm_fake_wrong_selector = wm_fake_methods[i].selector;
        TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
        TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_ABI_MISMATCH);
        TEST_CHECK(wm_fake_manager_init_calls, 0);
        wm_fake_end();

        wm_fake_begin();
        wm_fake_missing_selector = wm_fake_methods[i].selector;
        TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
        TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_UNAVAILABLE);
        TEST_CHECK(wm_fake_manager_init_calls, 0);
        wm_fake_end();
    }

    wm_fake_begin();
    wm_fake_copy.superclass = &wm_fake_async;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_ABI_MISMATCH);
    TEST_CHECK(wm_fake_manager_init_calls, 0);
    wm_fake_end();

    wm_fake_begin();
    wm_fake_array_result.superclass = &wm_fake_sync;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_ABI_MISMATCH);
    TEST_CHECK(wm_fake_manager_init_calls, 0);
    wm_fake_end();

    for (size_t i = 7; i < array_count(wm_fake_methods); ++i) {
        wm_fake_begin();
        wm_fake_wrong_selector = wm_fake_methods[i].selector;
        TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
        TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_OK);
        const struct wm_bridge_capabilities *capabilities = &diagnostic.capabilities;
        TEST_CHECK(capabilities->create_space_metadata && capabilities->destroy_space_metadata &&
                   capabilities->move_space_metadata && capabilities->set_current_space_metadata &&
                   capabilities->create_space_result_metadata, false);
        TEST_CHECK(wm_fake_mutation_init_calls, 0);
        TEST_CHECK(wm_fake_async_dispatch_calls, 0);
        wm_fake_end();
    }
});

TEST_FUNC(window_management_bridge_read_results_are_validated_and_metadata_is_advisory,
{
    struct wm_bridge_diagnostic diagnostic;
    wm_fake_begin(); wm_fake_nil_manager = true;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_FAILED); wm_fake_end();

    wm_fake_begin(); wm_fake_nil_operation = true;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_FAILED); wm_fake_end();

    wm_fake_begin(); wm_fake_nil_result = true;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_FAILED); wm_fake_end();

    wm_fake_begin(); wm_fake_wrong_result_class = true;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_FAILED); wm_fake_end();

    wm_fake_begin(); wm_fake_property_list = nil;
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_FAILED); wm_fake_end();

    wm_fake_begin(); wm_fake_property_list = @"not-an-array";
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_FAILED); wm_fake_end();

    wm_fake_begin(); wm_fake_property_list = @[ @{}, @"not-a-dictionary" ];
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_FAILED); wm_fake_end();

    wm_fake_begin(); wm_fake_property_list = @[];
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_OK);
    TEST_CHECK(diagnostic.managed_display_record_count, 0U); wm_fake_end();

    wm_fake_begin(); wm_fake_missing_selector = "initWithSpaceID:";
    TEST_CHECK(window_management_bridge_diagnose_readonly(&diagnostic), true);
    TEST_CHECK(diagnostic.read_status, WM_BRIDGE_READ_OK);
    TEST_CHECK(diagnostic.capabilities.destroy_space_metadata, false);
    TEST_CHECK(wm_fake_mutation_init_calls, 0);
    TEST_CHECK(wm_fake_async_dispatch_calls, 0);
    wm_fake_end();
});
