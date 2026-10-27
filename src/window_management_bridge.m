#include "window_management_bridge.h"

#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <dlfcn.h>
#include <string.h>

#define WM_SKYLIGHT_PATH "/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight"
#define WM_FRAMEWORK_PATH "/System/Library/PrivateFrameworks/WindowManagement.framework/WindowManagement"

struct wm_bridge_runtime
{
    void *(*open_image)(const char *path);
    void (*close_image)(void *handle);
    Class (*lookup_class)(const char *name);
    Class (*superclass)(Class cls);
    const char *(*method_encoding)(Class cls, const char *selector);
    id (*init_object)(Class cls, const char *selector);
    id (*send_object0)(id object, const char *selector);
    id (*send_object1)(id object, const char *selector, id argument);
    void (*send_void0)(id object, const char *selector);
    bool (*object_is_kind_of)(id object, Class cls);
    void (*release_object)(id object);
};

static void *wm_bridge_open_image(const char *path)
{
    return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
}

static void wm_bridge_close_image(void *handle)
{
    if (handle) dlclose(handle);
}

static Class wm_bridge_lookup_class(const char *name)
{
    return objc_lookUpClass(name);
}

static Class wm_bridge_superclass(Class cls)
{
    return cls ? class_getSuperclass(cls) : Nil;
}

static const char *wm_bridge_method_encoding(Class cls, const char *selector)
{
    if (!cls || !selector) return NULL;
    Method method = class_getInstanceMethod(cls, sel_registerName(selector));
    return method ? method_getTypeEncoding(method) : NULL;
}

static id wm_bridge_init_object(Class cls, const char *selector)
{
    if (!cls || !selector) return nil;
    id object = ((id (*)(id, SEL)) objc_msgSend)((id) cls, sel_registerName("alloc"));
    if (!object) return nil;
    return ((id (*)(id, SEL)) objc_msgSend)(object, sel_registerName(selector));
}

static id wm_bridge_send_object0(id object, const char *selector)
{
    return object && selector
         ? ((id (*)(id, SEL)) objc_msgSend)(object, sel_registerName(selector))
         : nil;
}

static id wm_bridge_send_object1(id object, const char *selector, id argument)
{
    if (!object || !selector) return nil;
    id result = ((id (*)(id, SEL, id)) objc_msgSend)(object, sel_registerName(selector), argument);
    return result ? ((id (*)(id, SEL)) objc_msgSend)(result, sel_registerName("retain")) : nil;
}

static void wm_bridge_send_void0(id object, const char *selector)
{
    if (object && selector) ((void (*)(id, SEL)) objc_msgSend)(object, sel_registerName(selector));
}

static bool wm_bridge_object_is_kind_of(id object, Class cls)
{
    if (!object || !cls) return false;
    Class current = object_getClass(object);
    while (current) {
        if (current == cls) return true;
        current = class_getSuperclass(current);
    }
    return false;
}

static void wm_bridge_release_object(id object)
{
    if (object) ((void (*)(id, SEL)) objc_msgSend)(object, sel_registerName("release"));
}

static const struct wm_bridge_runtime wm_bridge_production_runtime = {
    .open_image = wm_bridge_open_image,
    .close_image = wm_bridge_close_image,
    .lookup_class = wm_bridge_lookup_class,
    .superclass = wm_bridge_superclass,
    .method_encoding = wm_bridge_method_encoding,
    .init_object = wm_bridge_init_object,
    .send_object0 = wm_bridge_send_object0,
    .send_object1 = wm_bridge_send_object1,
    .send_void0 = wm_bridge_send_void0,
    .object_is_kind_of = wm_bridge_object_is_kind_of,
    .release_object = wm_bridge_release_object
};

#ifdef TESTS
static const struct wm_bridge_runtime *window_management_bridge_test_runtime;
#endif

static const struct wm_bridge_runtime *wm_bridge_runtime(void)
{
#ifdef TESTS
    if (window_management_bridge_test_runtime) return window_management_bridge_test_runtime;
#endif
    return &wm_bridge_production_runtime;
}

static bool wm_bridge_method_matches(const struct wm_bridge_runtime *runtime, Class cls,
                                     const char *selector, const char *encoding,
                                     bool *available)
{
    const char *actual = runtime->method_encoding(cls, selector);
    if (!actual) {
        if (available) *available = false;
        return false;
    }
    if (available) *available = true;
    return strcmp(actual, encoding) == 0;
}

static bool wm_bridge_metadata_operation(const struct wm_bridge_runtime *runtime,
                                         const char *class_name, Class expected_superclass,
                                         const char *selector, const char *encoding)
{
    Class cls = runtime->lookup_class(class_name);
    bool available = false;
    return cls && runtime->superclass(cls) == expected_superclass &&
           wm_bridge_method_matches(runtime, cls, selector, encoding, &available);
}

bool window_management_bridge_diagnose_readonly(struct wm_bridge_diagnostic *diagnostic)
{
    if (!diagnostic) return false;
    *diagnostic = (struct wm_bridge_diagnostic) {0};
    const struct wm_bridge_runtime *runtime = wm_bridge_runtime();
    if (!runtime || !runtime->open_image || !runtime->close_image || !runtime->lookup_class ||
        !runtime->superclass || !runtime->method_encoding || !runtime->init_object ||
        !runtime->send_object0 || !runtime->send_object1 || !runtime->send_void0 ||
        !runtime->object_is_kind_of || !runtime->release_object) {
        return false;
    }

    void *skylight = runtime->open_image(WM_SKYLIGHT_PATH);
    diagnostic->capabilities.skylight_loaded = skylight != NULL;
    void *window_management = runtime->open_image(WM_FRAMEWORK_PATH);
    diagnostic->capabilities.window_management_loaded = window_management != NULL;
    if (!skylight || !window_management) goto unavailable;

    Class client = runtime->lookup_class("WMClientWindowManager");
    Class synchronous = runtime->lookup_class("SLSSynchronousBridgedWindowManagementOperation");
    Class asynchronous = runtime->lookup_class("SLSAsynchronousBridgedWindowManagementOperation");
    Class result_base = runtime->lookup_class("SLSBridgedWindowManagementOperationResult");
    Class copy_operation = runtime->lookup_class("SLSBridgedCopyManagedDisplaySpacesOperation");
    Class array_result = runtime->lookup_class("SLSBridgedWindowManagementOperationPropertyListArrayResult");
    if (!client || !synchronous || !asynchronous || !result_base || !copy_operation || !array_result) {
        goto unavailable;
    }

    bool available = false;
    if (!wm_bridge_method_matches(runtime, client, "init", "@16@0:8", &available)) {
        if (!available) goto unavailable; else goto mismatch;
    }
    if (!wm_bridge_method_matches(runtime, client, "invalidate", "v16@0:8", &available)) {
        if (!available) goto unavailable; else goto mismatch;
    }
    if (!wm_bridge_method_matches(runtime, client,
                                  "performSynchronousBridgedWindowManagementOperation:",
                                  "@24@0:8@16", &available)) {
        if (!available) goto unavailable; else goto mismatch;
    }
    if (!wm_bridge_method_matches(runtime, client,
                                  "performAsynchronousBridgedWindowManagementOperation:",
                                  "v24@0:8@16", &available)) {
        if (!available) goto unavailable; else goto mismatch;
    }
    diagnostic->capabilities.client_abi = true;

    if (runtime->superclass(copy_operation) != synchronous ||
        runtime->superclass(array_result) != result_base) goto mismatch;
    if (!wm_bridge_method_matches(runtime, copy_operation, "init", "@16@0:8", &available)) {
        if (!available) goto unavailable; else goto mismatch;
    }
    if (!wm_bridge_method_matches(runtime, copy_operation, "makeResultWithPropertyListArray:",
                                  "@24@0:8@16", &available)) {
        if (!available) goto unavailable; else goto mismatch;
    }
    if (!wm_bridge_method_matches(runtime, array_result, "propertyListArray", "@16@0:8", &available)) {
        if (!available) goto unavailable; else goto mismatch;
    }
    diagnostic->capabilities.copy_topology_abi = true;

    diagnostic->capabilities.create_space_metadata = wm_bridge_metadata_operation(
        runtime, "SLSBridgedSpaceCreateOperation", synchronous,
        "initWithOptions:values:", "@28@0:8I16@20");
    diagnostic->capabilities.destroy_space_metadata = wm_bridge_metadata_operation(
        runtime, "SLSBridgedSpaceDestroyOperation", asynchronous,
        "initWithSpaceID:", "@24@0:8Q16");
    diagnostic->capabilities.move_space_metadata = wm_bridge_metadata_operation(
        runtime, "SLSBridgedMoveManagedSpaceToDisplayIndexOperation", asynchronous,
        "initWithSpaceID:displayIdentifier:index:", "@36@0:8Q16@24I32");
    diagnostic->capabilities.set_current_space_metadata = wm_bridge_metadata_operation(
        runtime, "SLSBridgedManagedDisplaySetCurrentSpaceOperation", asynchronous,
        "initWithDisplayIdentifier:spaceID:", "@32@0:8@16Q24");

    Class create_operation = runtime->lookup_class("SLSBridgedSpaceCreateOperation");
    Class space_id_result = runtime->lookup_class("SLSBridgedWindowManagementOperationSpaceIDResult");
    diagnostic->capabilities.create_space_result_metadata =
        create_operation && space_id_result && runtime->superclass(space_id_result) == result_base &&
        wm_bridge_method_matches(runtime, create_operation, "makeResultWithSpaceID:",
                                 "@24@0:8Q16", NULL) &&
        wm_bridge_method_matches(runtime, space_id_result, "initWithSpaceID:",
                                 "@24@0:8Q16", NULL) &&
        wm_bridge_method_matches(runtime, space_id_result, "spaceID", "Q16@0:8", NULL);

    id manager = runtime->init_object(client, "init");
    if (!manager) goto failed;
    id operation = runtime->init_object(copy_operation, "init");
    if (!operation) {
        runtime->send_void0(manager, "invalidate");
        runtime->release_object(manager);
        goto failed;
    }

    id result = runtime->send_object1(manager,
        "performSynchronousBridgedWindowManagementOperation:", operation);
    if (!result || !runtime->object_is_kind_of(result, array_result)) {
        if (result) runtime->release_object(result);
        runtime->release_object(operation);
        runtime->send_void0(manager, "invalidate");
        runtime->release_object(manager);
        goto failed;
    }

    id property_list = runtime->send_object0(result, "propertyListArray");
    bool usable = [property_list isKindOfClass:[NSArray class]];
    if (usable) {
        for (id value in (NSArray *) property_list) {
            if (![value isKindOfClass:[NSDictionary class]]) {
                usable = false;
                break;
            }
        }
    }
    if (usable && [(NSArray *) property_list count] <= UINT32_MAX) {
        diagnostic->managed_display_record_count = (uint32_t) [(NSArray *) property_list count];
    } else {
        usable = false;
    }

    runtime->release_object(result);
    runtime->release_object(operation);
    runtime->send_void0(manager, "invalidate");
    runtime->release_object(manager);
    if (!usable) goto failed;

    diagnostic->read_status = WM_BRIDGE_READ_OK;
    runtime->close_image(window_management);
    runtime->close_image(skylight);
    return true;

mismatch:
    diagnostic->read_status = WM_BRIDGE_READ_ABI_MISMATCH;
    runtime->close_image(window_management);
    runtime->close_image(skylight);
    return true;
failed:
    diagnostic->read_status = WM_BRIDGE_READ_FAILED;
    runtime->close_image(window_management);
    runtime->close_image(skylight);
    return true;
unavailable:
    diagnostic->read_status = WM_BRIDGE_READ_UNAVAILABLE;
    if (window_management) runtime->close_image(window_management);
    if (skylight) runtime->close_image(skylight);
    return true;
}
