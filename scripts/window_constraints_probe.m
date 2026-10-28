#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <dlfcn.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <time.h>

#include "../src/misc/extern.h"
#include "../src/window_constraints.c"

typedef void (*constraints_iterator_fn)(CFTypeRef, CGSize *, CGSize *, CGSize *);
typedef CGError (*constraints_packages_fn)(int, uint32_t, CGSize *, CGSize *, CGSize *);

struct fixture
{
    const char *name;
    NSWindow *window;
};

struct raw_constraints
{
    bool available;
    CGError status;
    CGSize minimum;
    CGSize maximum;
    CGSize current;
    uint64_t latency_ns;
};

struct summary
{
    unsigned observations;
    unsigned iterator_available;
    unsigned packages_available;
    unsigned iterator_packages_agree;
    unsigned minimum_frame;
    unsigned minimum_content;
    unsigned minimum_both;
    unsigned minimum_other;
    unsigned maximum_frame;
    unsigned maximum_content;
    unsigned maximum_both;
    unsigned maximum_other;
    unsigned current_frame;
    unsigned current_content;
    unsigned current_both;
    unsigned current_other;
};

static uint64_t now_ns(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC_RAW, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static bool finite_positive(CGSize value)
{
    return isfinite(value.width) && isfinite(value.height) &&
           value.width > 0.0 && value.height > 0.0;
}

static bool size_equal(CGSize a, CGSize b)
{
    return fabs(a.width - b.width) <= 0.5 && fabs(a.height - b.height) <= 0.5;
}

static const char *basis(CGSize raw, CGSize frame, CGSize content)
{
    if (!finite_positive(raw)) return "invalid";
    bool matches_frame = size_equal(raw, frame);
    bool matches_content = size_equal(raw, content);
    if (matches_frame && matches_content) return "frame-and-content";
    if (matches_frame) return "frame";
    if (matches_content) return "content";
    return "other";
}

static void count_basis(const char *value, unsigned *frame, unsigned *content,
                        unsigned *both, unsigned *other)
{
    if (!strcmp(value, "frame")) ++*frame;
    else if (!strcmp(value, "content")) ++*content;
    else if (!strcmp(value, "frame-and-content")) ++*both;
    else ++*other;
}

static void print_number(double value)
{
    if (isfinite(value)) printf("%.3f", value); else fputs("null", stdout);
}

static void print_size(CGSize value)
{
    fputs("{\"w\":", stdout); print_number(value.width);
    fputs(",\"h\":", stdout); print_number(value.height); fputc('}', stdout);
}

static bool iterator_constraints(uint32_t wid, constraints_iterator_fn getter,
                                 struct raw_constraints *result)
{
    *result = (struct raw_constraints) { .status = kCGErrorFailure };
    if (!getter || !wid) return false;
    int32_t value = (int32_t)wid;
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
    if (!number) return false;
    const void *values[] = { number };
    CFArrayRef ids = CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
    CFRelease(number);
    if (!ids) return false;
    CFTypeRef query = SLSWindowQueryWindows(SLSMainConnectionID(), ids, 1);
    CFRelease(ids);
    if (!query) return false;
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    CFRelease(query);
    if (!iterator) return false;
    bool found = SLSWindowIteratorGetCount(iterator) == 1 && SLSWindowIteratorAdvance(iterator) &&
                 SLSWindowIteratorGetWindowID(iterator) == wid;
    if (found) {
        uint64_t start = now_ns();
        getter(iterator, &result->minimum, &result->maximum, &result->current);
        result->latency_ns = now_ns() - start;
        result->status = kCGErrorSuccess;
        result->available = true;
    }
    CFRelease(iterator);
    return found;
}

static bool packages_constraints(uint32_t wid, constraints_packages_fn getter,
                                 struct raw_constraints *result)
{
    *result = (struct raw_constraints) { .status = kCGErrorFailure };
    if (!getter || !wid) return false;
    int owner = 0;
    if (SLSGetWindowOwner(SLSMainConnectionID(), wid, &owner) != kCGErrorSuccess || !owner) return false;
    uint64_t start = now_ns();
    result->status = getter(owner, wid, &result->minimum, &result->maximum, &result->current);
    result->latency_ns = now_ns() - start;
    result->available = result->status == kCGErrorSuccess;
    return result->available;
}

static bool ax_size_for_window(uint32_t wid, CGSize *size)
{
    if (!AXIsProcessTrusted()) return false;
    AXUIElementRef application = AXUIElementCreateApplication(getpid());
    CFTypeRef value = NULL;
    AXError error = AXUIElementCopyAttributeValue(application, kAXWindowsAttribute, &value);
    CFRelease(application);
    if (error != kAXErrorSuccess || !value || CFGetTypeID(value) != CFArrayGetTypeID()) {
        if (value) CFRelease(value);
        return false;
    }
    bool found = false;
    CFArrayRef windows = value;
    for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
        AXUIElementRef window = (AXUIElementRef)CFArrayGetValueAtIndex(windows, i);
        uint32_t candidate = 0;
        if (_AXUIElementGetWindow(window, &candidate) != kAXErrorSuccess || candidate != wid) continue;
        CFTypeRef size_value = NULL;
        if (AXUIElementCopyAttributeValue(window, kAXSizeAttribute, &size_value) == kAXErrorSuccess &&
            size_value && CFGetTypeID(size_value) == AXValueGetTypeID()) {
            found = AXValueGetValue((AXValueRef)size_value, kAXValueCGSizeType, size);
        }
        if (size_value) CFRelease(size_value);
        break;
    }
    CFRelease(windows);
    return found;
}

static void print_raw(const char *name, const struct raw_constraints *raw)
{
    printf("\"%s\":{\"available\":%s,\"status\":%d,\"minimum\":",
           name, raw->available ? "true" : "false", raw->status);
    print_size(raw->minimum); fputs(",\"maximum\":", stdout); print_size(raw->maximum);
    fputs(",\"current\":", stdout); print_size(raw->current);
    printf(",\"latency_ns\":%llu}", (unsigned long long)raw->latency_ns);
}

static void observe(const struct fixture *fixture, const char *state,
                    constraints_iterator_fn iterator_getter,
                    constraints_packages_fn packages_getter,
                    struct summary *summary)
{
    uint32_t wid = (uint32_t)fixture->window.windowNumber;
    struct raw_constraints iterator, packages;
    iterator_constraints(wid, iterator_getter, &iterator);
    packages_constraints(wid, packages_getter, &packages);
    NSRect frame = fixture->window.frame;
    NSSize content = fixture->window.contentLayoutRect.size;
    NSSize frame_min = fixture->window.minSize;
    NSSize frame_max = fixture->window.maxSize;
    NSSize content_min = fixture->window.contentMinSize;
    NSSize content_max = fixture->window.contentMaxSize;
    CGSize ax_size = CGSizeZero;
    bool ax_available = ax_size_for_window(wid, &ax_size);
    struct window_constraints normalized = {0};
    if (iterator.available) {
        struct window_constraint_observation observation = {
            iterator.minimum, iterator.maximum, iterator.current
        };
        window_constraints_update(&normalized, &observation,
                                  WINDOW_CONSTRAINT_SOURCE_ITERATOR,
                                  ax_available ? &ax_size : NULL, now_ns());
    }
    const char *minimum_basis = basis(iterator.minimum, frame_min, content_min);
    const char *maximum_basis = basis(iterator.maximum, frame_max, content_max);
    const char *current_basis = basis(iterator.current, frame.size, content);
    bool agree = iterator.available && packages.available &&
                 size_equal(iterator.minimum, packages.minimum) &&
                 size_equal(iterator.maximum, packages.maximum) &&
                 size_equal(iterator.current, packages.current);
    ++summary->observations;
    if (iterator.available) ++summary->iterator_available;
    if (packages.available) ++summary->packages_available;
    if (agree) ++summary->iterator_packages_agree;
    count_basis(minimum_basis, &summary->minimum_frame, &summary->minimum_content,
                &summary->minimum_both, &summary->minimum_other);
    count_basis(maximum_basis, &summary->maximum_frame, &summary->maximum_content,
                &summary->maximum_both, &summary->maximum_other);
    count_basis(current_basis, &summary->current_frame, &summary->current_content,
                &summary->current_both, &summary->current_other);

    printf("{\"schema\":\"yabai.window-constraints-probe/v1\",\"kind\":\"observation\","
           "\"fixture\":\"%s\",\"state\":\"%s\",\"appkit\":{\"frame\":",
           fixture->name, state);
    print_size(frame.size); fputs(",\"content\":", stdout); print_size(content);
    fputs(",\"frame_min\":", stdout); print_size(frame_min);
    fputs(",\"frame_max\":", stdout); print_size(frame_max);
    fputs(",\"content_min\":", stdout); print_size(content_min);
    fputs(",\"content_max\":", stdout); print_size(content_max); fputs("},", stdout);
    printf("\"ax\":{\"available\":%s,\"size\":", ax_available ? "true" : "false");
    print_size(ax_size); fputs("},", stdout);
    print_raw("iterator", &iterator); fputc(',', stdout); print_raw("packages", &packages);
    printf(",\"normalized\":{\"limits_known\":%s,\"current_known\":%s,"
           "\"current_in_range\":%s,\"decoration_delta_known\":%s,\"decoration_delta\":",
           window_constraints_has_limits(&normalized) ? "true" : "false",
           window_constraints_has_current(&normalized) ? "true" : "false",
           normalized.validity & WINDOW_CONSTRAINT_CURRENT_IN_RANGE ? "true" : "false",
           normalized.validity & WINDOW_CONSTRAINT_DECORATION_DELTA ? "true" : "false");
    print_size(normalized.decoration_delta);
    printf("},\"classification\":{\"minimum_basis\":\"%s\",\"maximum_basis\":\"%s\","
           "\"current_basis\":\"%s\",\"iterator_packages_agree\":%s}}\n",
           minimum_basis, maximum_basis, current_basis, agree ? "true" : "false");
}

static NSWindow *make_window(NSRect frame, NSWindowStyleMask style)
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:frame styleMask:style
                                                    backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed = NO;
    window.title = @"Constraints Fixture";
    return window;
}

int main(void)
{
    @autoreleasepool {
        constraints_iterator_fn iterator_getter = (constraints_iterator_fn)
            dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetConstraints");
        constraints_packages_fn packages_getter = (constraints_packages_fn)
            dlsym(RTLD_DEFAULT, "SLSPackagesGetWindowConstraints");
        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char build[256] = "unknown"; size_t build_size = sizeof(build);
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0) strcpy(build, "unknown");
        struct utsname system = {0}; uname(&system);
        printf("{\"schema\":\"yabai.window-constraints-probe/v1\",\"kind\":\"run\","
               "\"os_version\":\"%ld.%ld.%ld\",\"os_build\":\"%s\","
               "\"architecture\":\"%s\",\"symbols\":{\"iterator\":%s,\"packages\":%s},"
               "\"ax_trusted\":%s,\"production_policy_enabled\":false}\n",
               version.majorVersion, version.minorVersion, version.patchVersion, build, system.machine,
               iterator_getter ? "true" : "false", packages_getter ? "true" : "false",
               AXIsProcessTrusted() ? "true" : "false");
        if (!iterator_getter) return EXIT_FAILURE;

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
        NSWindowStyleMask titled = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                   NSWindowStyleMaskResizable;
        struct fixture fixtures[6] = {0};
        fixtures[0] = (struct fixture){ "frame-limits", make_window(NSMakeRect(60, 80, 640, 420), titled) };
        fixtures[0].window.minSize = NSMakeSize(360, 280);
        fixtures[0].window.maxSize = NSMakeSize(900, 700);
        fixtures[1] = (struct fixture){ "content-limits", make_window(NSMakeRect(90, 110, 640, 420), titled) };
        fixtures[1].window.contentMinSize = NSMakeSize(320, 240);
        fixtures[1].window.contentMaxSize = NSMakeSize(800, 600);
        fixtures[2] = (struct fixture){ "toolbar-content", make_window(NSMakeRect(120, 140, 640, 420),
            titled | NSWindowStyleMaskFullSizeContentView) };
        fixtures[2].window.toolbar = [[NSToolbar alloc] initWithIdentifier:@"constraints-fixture"];
        fixtures[2].window.contentMinSize = NSMakeSize(360, 240);
        fixtures[2].window.contentMaxSize = NSMakeSize(900, 620);
        fixtures[3] = (struct fixture){ "borderless", make_window(NSMakeRect(150, 170, 600, 400),
            NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable) };
        fixtures[3].window.minSize = NSMakeSize(300, 200);
        fixtures[3].window.maxSize = NSMakeSize(700, 500);
        fixtures[4] = (struct fixture){ "fixed-size", make_window(NSMakeRect(180, 200, 500, 320),
            NSWindowStyleMaskTitled | NSWindowStyleMaskClosable) };
        fixtures[4].window.minSize = fixtures[4].window.frame.size;
        fixtures[4].window.maxSize = fixtures[4].window.frame.size;
        fixtures[5] = (struct fixture){ "unbounded-default", make_window(NSMakeRect(210, 230, 620, 410), titled) };

        for (size_t i = 0; i < 6; ++i) [fixtures[i].window orderFront:nil];
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:0.5];
        while ([deadline timeIntervalSinceNow] > 0) {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        struct summary summary = {0};
        for (size_t i = 0; i < 6; ++i) observe(&fixtures[i], "ordered-quiescent", iterator_getter, packages_getter, &summary);
        fixtures[0].window.minSize = NSMakeSize(400, 300);
        fixtures[0].window.maxSize = NSMakeSize(760, 580);
        deadline = [NSDate dateWithTimeIntervalSinceNow:0.25];
        while ([deadline timeIntervalSinceNow] > 0) {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        observe(&fixtures[0], "after-dynamic-change-250ms", iterator_getter, packages_getter, &summary);
        deadline = [NSDate dateWithTimeIntervalSinceNow:0.75];
        while ([deadline timeIntervalSinceNow] > 0) {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        observe(&fixtures[0], "after-dynamic-change-1000ms", iterator_getter, packages_getter, &summary);
        for (size_t i = 0; i < 6; ++i) [fixtures[i].window close];
        printf("{\"schema\":\"yabai.window-constraints-probe/v1\",\"kind\":\"summary\","
               "\"observations\":%u,\"iterator_available\":%u,\"packages_available\":%u,"
               "\"iterator_packages_agree\":%u,\"minimum_basis\":{\"frame\":%u,\"content\":%u,\"both\":%u,\"other\":%u},"
               "\"maximum_basis\":{\"frame\":%u,\"content\":%u,\"both\":%u,\"other\":%u},"
               "\"current_basis\":{\"frame\":%u,\"content\":%u,\"both\":%u,\"other\":%u},"
               "\"recommendation\":\"diagnostic-only\"}\n",
               summary.observations, summary.iterator_available, summary.packages_available,
               summary.iterator_packages_agree,
               summary.minimum_frame, summary.minimum_content, summary.minimum_both, summary.minimum_other,
               summary.maximum_frame, summary.maximum_content, summary.maximum_both, summary.maximum_other,
               summary.current_frame, summary.current_content, summary.current_both, summary.current_other);
    }
    return EXIT_SUCCESS;
}
