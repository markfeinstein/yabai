#import <AppKit/AppKit.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <dlfcn.h>
#include <math.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include "../src/misc/extern.h"
#include "native_tile_diagnostic_core.h"

#define WM_SKYLIGHT_PATH "/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight"
#define WM_FRAMEWORK_PATH "/System/Library/PrivateFrameworks/WindowManagement.framework/WindowManagement"

static const char *encoding(Class cls, const char *selector)
{
    Method method = cls ? class_getInstanceMethod(cls, sel_registerName(selector)) : NULL;
    return method ? method_getTypeEncoding(method) : NULL;
}

static bool matches(Class cls, const char *selector, const char *expected)
{
    const char *actual = encoding(cls, selector);
    return actual && strcmp(actual, expected) == 0;
}

static id init0(Class cls, const char *selector)
{
    id object = cls ? ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("alloc")) : nil;
    return object ? ((id (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector)) : nil;
}

static id init_u64(Class cls, const char *selector, uint64_t value)
{
    id object = cls ? ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("alloc")) : nil;
    return object ? ((id (*)(id, SEL, uint64_t))objc_msgSend)(
        object, sel_registerName(selector), value) : nil;
}

static id init_u32(Class cls, const char *selector, uint32_t value)
{
    id object = cls ? ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("alloc")) : nil;
    return object ? ((id (*)(id, SEL, uint32_t))objc_msgSend)(
        object, sel_registerName(selector), value) : nil;
}

static id init_u64_size(Class cls, const char *selector, uint64_t value, CGSize size)
{
    id object = cls ? ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("alloc")) : nil;
    return object ? ((id (*)(id, SEL, uint64_t, CGSize))objc_msgSend)(
        object, sel_registerName(selector), value, size) : nil;
}

static id perform(id manager, id operation)
{
    if (!manager || !operation) return nil;
    id result = ((id (*)(id, SEL, id))objc_msgSend)(manager,
        sel_registerName("performSynchronousBridgedWindowManagementOperation:"), operation);
    return result ? ((id (*)(id, SEL))objc_msgSend)(result, sel_registerName("retain")) : nil;
}

static bool kind(id object, Class cls)
{
    for (Class current = object ? object_getClass(object) : Nil; current;
         current = class_getSuperclass(current)) if (current == cls) return true;
    return false;
}

static void release_object(id object)
{
    if (object) ((void (*)(id, SEL))objc_msgSend)(object, sel_registerName("release"));
}

static bool positive_u64(id value, uint64_t *result)
{
    if (![value isKindOfClass:[NSNumber class]]) return false;
    long long number = [value longLongValue];
    if (number <= 0) return false;
    *result = (uint64_t)number;
    return true;
}

typedef uint64_t (*direct_divider_fn)(uint64_t space_id);
typedef CGRect (*direct_tile_rect_fn)(int connection, uint32_t window_id);
typedef bool (*direct_can_create_fn)(int connection, uint64_t space_id, CGSize minimum_size);

static bool rect_nonempty(CGRect rect)
{
    return isfinite(rect.origin.x) && isfinite(rect.origin.y) &&
           isfinite(rect.size.width) && isfinite(rect.size.height) &&
           rect.size.width > 0.0 && rect.size.height > 0.0;
}

static bool rect_equal(CGRect a, CGRect b)
{
    return isfinite(a.origin.x) && isfinite(a.origin.y) &&
           isfinite(a.size.width) && isfinite(a.size.height) &&
           isfinite(b.origin.x) && isfinite(b.origin.y) &&
           isfinite(b.size.width) && isfinite(b.size.height) &&
           fabs(a.origin.x - b.origin.x) <= 0.01 &&
           fabs(a.origin.y - b.origin.y) <= 0.01 &&
           fabs(a.size.width - b.size.width) <= 0.01 &&
           fabs(a.size.height - b.size.height) <= 0.01;
}

static void diagnose_extended(struct native_tile_extended_diagnostic *diagnostic,
                              bool exact_abi_build)
{
    *diagnostic = (struct native_tile_extended_diagnostic) {0};
    void *skylight = dlopen(WM_SKYLIGHT_PATH, RTLD_LAZY | RTLD_LOCAL);
    void *framework = dlopen(WM_FRAMEWORK_PATH, RTLD_LAZY | RTLD_LOCAL);
    if (!skylight || !framework) goto out;
    direct_divider_fn direct_divider = (direct_divider_fn)dlsym(
        skylight, "SLSGetTileSpaceDividerDirections");
    direct_tile_rect_fn direct_rect = (direct_tile_rect_fn)dlsym(
        skylight, "SLSWindowGetTileRect");
    direct_can_create_fn direct_can = (direct_can_create_fn)dlsym(
        skylight, "SLSSpaceCanCreateTile");
    diagnostic->direct_abi_verified = exact_abi_build && direct_divider &&
                                      direct_rect && direct_can;

    Class client = objc_lookUpClass("WMClientWindowManager");
    Class synchronous = objc_lookUpClass("SLSSynchronousBridgedWindowManagementOperation");
    Class result_base = objc_lookUpClass("SLSBridgedWindowManagementOperationResult");
    Class divider_op = objc_lookUpClass("SLSBridgedGetTileSpaceDividerDirectionsOperation");
    Class divider_result = objc_lookUpClass(
        "SLSBridgedWindowManagementOperationSpaceResizeDirectionsResult");
    Class rect_op = objc_lookUpClass("SLSBridgedWindowGetTileRectOperation");
    Class rect_result = objc_lookUpClass("SLSBridgedWindowManagementOperationRectResult");
    Class can_op = objc_lookUpClass("SLSBridgedSpaceCanCreateTileOperation");
    Class bool_result = objc_lookUpClass("SLSBridgedWindowManagementOperationBoolResult");
    if (!client || !synchronous || !result_base || !divider_op || !divider_result ||
        !rect_op || !rect_result || !can_op || !bool_result) goto out;

    bool manager_abi = matches(client, "init", "@16@0:8") &&
        matches(client, "invalidate", "v16@0:8") &&
        matches(client, "performSynchronousBridgedWindowManagementOperation:", "@24@0:8@16");
    diagnostic->divider_available = class_getSuperclass(divider_op) == synchronous &&
        class_getSuperclass(divider_result) == result_base &&
        matches(divider_op, "initWithSpaceID:", "@24@0:8Q16") &&
        matches(divider_result, "spaceResizeDirections", "Q16@0:8");
    diagnostic->tile_rect_available = class_getSuperclass(rect_op) == synchronous &&
        class_getSuperclass(rect_result) == result_base &&
        matches(rect_op, "initWithWindowID:", "@20@0:8I16") &&
        matches(rect_result, "rect", "{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8");
    diagnostic->can_create_available = class_getSuperclass(can_op) == synchronous &&
        class_getSuperclass(bool_result) == result_base &&
        matches(can_op, "initWithSpaceID:minSize:", "@40@0:8Q16{CGSize=dd}24") &&
        matches(bool_result, "boolValue", "B16@0:8");
    diagnostic->metadata_verified = manager_abi && diagnostic->divider_available &&
        diagnostic->tile_rect_available && diagnostic->can_create_available;
    if (!diagnostic->metadata_verified) goto out;

    id manager = init0(client, "init");
    if (!manager) goto out;
    CFArrayRef topology = SLSCopyManagedDisplaySpaces(SLSMainConnectionID());
    if (topology && CFGetTypeID(topology) == CFArrayGetTypeID()) {
        for (id display in (__bridge NSArray *)topology) {
            id spaces = [display isKindOfClass:[NSDictionary class]] ? display[@"Spaces"] : nil;
            if (![spaces isKindOfClass:[NSArray class]]) continue;
            for (id space in spaces) {
                uint64_t sid = 0;
                if (![space isKindOfClass:[NSDictionary class]] ||
                    !positive_u64(space[@"id64"], &sid)) continue;

                uint64_t values[2] = {0}; bool successes[2] = {false};
                for (int repeat = 0; repeat < 2; ++repeat) {
                    ++diagnostic->divider_calls;
                    id operation = init_u64(divider_op, "initWithSpaceID:", sid);
                    id result = perform(manager, operation);
                    if (kind(result, divider_result)) {
                        values[repeat] = ((uint64_t (*)(id, SEL))objc_msgSend)(
                            result, sel_registerName("spaceResizeDirections"));
                        successes[repeat] = true;
                        ++diagnostic->divider_successes;
                        if (values[repeat]) ++diagnostic->divider_nonzero;
                    }
                    release_object(result); release_object(operation);
                }
                if (successes[0] && successes[1] && values[0] == values[1]) {
                    ++diagnostic->divider_repeat_stable;
                }
                if (diagnostic->direct_abi_verified) {
                    ++diagnostic->direct_divider_calls;
                    if (successes[0] && direct_divider(sid) == values[0]) {
                        ++diagnostic->direct_divider_matches;
                    }
                }

                ++diagnostic->can_create_calls;
                id operation = init_u64_size(can_op, "initWithSpaceID:minSize:", sid,
                                              CGSizeMake(320.0, 240.0));
                id result = perform(manager, operation);
                bool can_known = false, can_value = false;
                if (kind(result, bool_result)) {
                    can_value = ((bool (*)(id, SEL))objc_msgSend)(
                        result, sel_registerName("boolValue"));
                    can_known = true;
                    ++diagnostic->can_create_successes;
                    if (can_value) ++diagnostic->can_create_true;
                }
                if (diagnostic->direct_abi_verified) {
                    ++diagnostic->direct_can_create_calls;
                    bool direct_value = direct_can(SLSMainConnectionID(), sid,
                                                   CGSizeMake(320.0, 240.0));
                    if (can_known && direct_value == can_value) {
                        ++diagnostic->direct_can_create_matches;
                    }
                }
                release_object(result); release_object(operation);
            }
        }
    }
    if (topology) CFRelease(topology);

    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (windows && CFGetTypeID(windows) == CFArrayGetTypeID()) {
        NSUInteger limit = MIN((NSUInteger)CFArrayGetCount(windows), (NSUInteger)256);
        for (NSUInteger i = 0; i < limit; ++i) {
            id row = ((__bridge NSArray *)windows)[i];
            id number = [row isKindOfClass:[NSDictionary class]]
                ? row[(NSString *)kCGWindowNumber] : nil;
            id layer = [row isKindOfClass:[NSDictionary class]]
                ? row[(NSString *)kCGWindowLayer] : nil;
            id raw_bounds = [row isKindOfClass:[NSDictionary class]]
                ? row[(NSString *)kCGWindowBounds] : nil;
            CGRect public_bounds = CGRectZero;
            bool public_bounds_known = [raw_bounds isKindOfClass:[NSDictionary class]] &&
                CGRectMakeWithDictionaryRepresentation(
                    (__bridge CFDictionaryRef)raw_bounds, &public_bounds);
            bool layer_zero = [layer isKindOfClass:[NSNumber class]] &&
                [layer integerValue] == 0;
            if (![number isKindOfClass:[NSNumber class]] ||
                [number unsignedLongLongValue] > UINT32_MAX || ![number unsignedIntValue]) continue;
            ++diagnostic->tile_rect_calls;
            uint32_t wid = [number unsignedIntValue];
            id operation = init_u32(rect_op, "initWithWindowID:", wid);
            id result = perform(manager, operation);
            CGRect rect = CGRectZero; bool rect_known = false;
            if (kind(result, rect_result)) {
#ifdef __x86_64__
                ((void (*)(CGRect *, id, SEL))objc_msgSend_stret)(
                    &rect, result, sel_registerName("rect"));
#else
                rect = ((CGRect (*)(id, SEL))objc_msgSend)(result, sel_registerName("rect"));
#endif
                rect_known = true;
                ++diagnostic->tile_rect_successes;
                if (rect_nonempty(rect)) {
                    ++diagnostic->tile_rect_nonempty;
                    if (layer_zero) ++diagnostic->tile_rect_nonempty_layer_zero;
                    else ++diagnostic->tile_rect_nonempty_layer_nonzero;
                    if (public_bounds_known && rect_equal(rect, public_bounds)) {
                        ++diagnostic->tile_rect_nonempty_matches_public_bounds;
                    }
                } else {
                    ++diagnostic->tile_rect_empty;
                    if (layer_zero) ++diagnostic->tile_rect_empty_layer_zero;
                    else ++diagnostic->tile_rect_empty_layer_nonzero;
                    if (public_bounds_known && rect_nonempty(public_bounds)) {
                        ++diagnostic->tile_rect_empty_with_public_bounds;
                    }
                }
            }
            if (diagnostic->direct_abi_verified) {
                ++diagnostic->direct_tile_rect_calls;
                CGRect direct_value = direct_rect(SLSMainConnectionID(), wid);
                if (rect_known && rect_equal(direct_value, rect)) {
                    ++diagnostic->direct_tile_rect_matches;
                } else if (rect_known && !rect_nonempty(direct_value) && !rect_nonempty(rect)) {
                    ++diagnostic->direct_tile_rect_empty_matches;
                }
            }
            release_object(result); release_object(operation);
        }
    }
    if (windows) CFRelease(windows);
    ((void (*)(id, SEL))objc_msgSend)(manager, sel_registerName("invalidate"));
    release_object(manager);
out:
    if (framework) dlclose(framework);
    if (skylight) dlclose(skylight);
}

static void native_tile_usage(const char *program)
{
    printf("usage: %s [--help]\n", program);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc == 2 && strcmp(argv[1], "--help") == 0) {
            native_tile_usage(argv[0]);
            return 0;
        }
        if (argc != 1) {
            native_tile_usage(argv[0]);
            return 64;
        }

        struct wm_tile_membership_diagnostic diagnostic;
        if (!window_management_bridge_diagnose_tile_membership_readonly(&diagnostic)) return 3;
        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char build[256] = {0}; size_t build_size = sizeof(build); struct utsname system = {0};
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0 ||
            !memchr(build, '\0', sizeof(build)) || uname(&system) != 0) return 3;
        bool exact_abi_build = version.majorVersion == 27 && version.minorVersion == 0 &&
                               version.patchVersion == 1 && strcmp(build, "26A434") == 0 &&
                               strcmp(system.machine, "arm64") == 0;
        struct native_tile_extended_diagnostic extended;
        diagnose_extended(&extended, exact_abi_build);
        struct native_tile_platform platform = {
            .os_major = (int)version.majorVersion,
            .os_minor = (int)version.minorVersion,
            .os_patch = (int)version.patchVersion,
            .build = build,
            .architecture = system.machine
        };
        if (!native_tile_diagnostic_write_json(stdout, &platform, &diagnostic, &extended)) return 3;
        switch (diagnostic.status) {
        case WM_TILE_MEMBERSHIP_OK: return extended.metadata_verified ? 0 : 2;
        case WM_TILE_MEMBERSHIP_UNAVAILABLE:
        case WM_TILE_MEMBERSHIP_ABI_MISMATCH: return 2;
        case WM_TILE_MEMBERSHIP_PARTIAL:
        case WM_TILE_MEMBERSHIP_QUERY_FAILED: return 3;
        }
        return 3;
    }
}
