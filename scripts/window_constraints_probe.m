#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <time.h>

#include "../src/misc/extern.h"
#include "../src/window_constraints.c"

typedef void (*constraints_iterator_fn)(CFTypeRef, CGSize *, CGSize *, CGSize *);
typedef CGError (*constraints_packages_fn)(int, uint32_t, CGSize *, CGSize *, CGSize *);

struct raw_constraints
{
    bool available;
    CGError status;
    CGSize minimum;
    CGSize maximum;
    CGSize current;
    uint64_t latency_ns;
};

struct fixture
{
    const char *name;
    const char *kind;
    NSWindow *window;
    NSSize declared_frame_min;
    NSSize declared_frame_max;
    NSSize declared_content_min;
    NSSize declared_content_max;
    NSSize effective_min;
    NSSize effective_max;
    NSSize increments;
    bool content_limits;
    bool content_increments;
    bool delegate_clamp;
    bool matrix;
    bool controller_settable_known;
    bool controller_settable;
};

struct confusion
{
    unsigned tp, fp, tn, fn, unscored;
};

#define AX_CONTROLLER_MAGIC UINT32_C(0x59425832)

struct ax_controller_result
{
    uint32_t magic;
    bool trusted;
    bool found;
    bool settable;
    bool settled;
    bool restore_settled;
    AXError write_error;
    AXError restore_error;
    CGSize baseline;
    CGSize final;
    CGSize restored;
    uint64_t settle_ns;
    uint64_t restore_ns;
};

struct summary
{
    unsigned observations;
    unsigned agreements;
    unsigned disagreements;
    unsigned writes;
    unsigned write_failures;
    unsigned timeouts;
    unsigned restore_failures;
    struct confusion iterator;
    struct confusion packages;
};

@interface ConstraintsClampDelegate : NSObject <NSWindowDelegate>
@property(assign) NSSize minimum;
@property(assign) NSSize maximum;
@property(assign) NSSize lastInput;
@property(assign) NSSize lastOutput;
@property(assign) NSUInteger calls;
@end

@implementation ConstraintsClampDelegate
- (NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)frameSize
{
    (void)sender;
    self.lastInput = frameSize;
    self.lastOutput = NSMakeSize(fmin(self.maximum.width, fmax(self.minimum.width, frameSize.width)),
                                 fmin(self.maximum.height, fmax(self.minimum.height, frameSize.height)));
    ++self.calls;
    return self.lastOutput;
}
@end

static uint64_t now_ns(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC_RAW, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static void spin_ms(unsigned milliseconds)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:(double)milliseconds / 1000.0];
    while ([deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
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

static void print_number(double value)
{
    if (isfinite(value)) printf("%.3f", value); else fputs("null", stdout);
}

static void print_size(CGSize value)
{
    fputs("{\"w\":", stdout); print_number(value.width);
    fputs(",\"h\":", stdout); print_number(value.height); fputc('}', stdout);
}

static const char *basis(CGSize raw, CGSize frame, CGSize content)
{
    if (!finite_positive(raw)) return "unknown";
    bool matches_frame = size_equal(raw, frame);
    bool matches_content = size_equal(raw, content);
    if (matches_frame && matches_content) return "both";
    if (matches_frame) return "frame";
    if (matches_content) return "content";
    return "other";
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

static AXUIElementRef ax_window_for_pid(pid_t pid, uint32_t wid)
{
    if (!AXIsProcessTrusted()) return NULL;
    AXUIElementRef application = AXUIElementCreateApplication(pid);
    CFTypeRef value = NULL;
    AXError error = AXUIElementCopyAttributeValue(application, kAXWindowsAttribute, &value);
    CFRelease(application);
    if (error != kAXErrorSuccess || !value || CFGetTypeID(value) != CFArrayGetTypeID()) {
        if (value) CFRelease(value);
        return NULL;
    }
    AXUIElementRef result = NULL;
    CFArrayRef windows = value;
    for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
        AXUIElementRef candidate = (AXUIElementRef)CFArrayGetValueAtIndex(windows, i);
        uint32_t candidate_id = 0;
        if (_AXUIElementGetWindow(candidate, &candidate_id) == kAXErrorSuccess && candidate_id == wid) {
            result = (AXUIElementRef)CFRetain(candidate);
            break;
        }
    }
    CFRelease(windows);
    return result;
}

static AXUIElementRef ax_window(uint32_t wid)
{
    return ax_window_for_pid(getpid(), wid);
}

static bool ax_copy_size(AXUIElementRef window, CGSize *size)
{
    if (!window || !size) return false;
    CFTypeRef value = NULL;
    AXError error = AXUIElementCopyAttributeValue(window, kAXSizeAttribute, &value);
    bool result = error == kAXErrorSuccess && value && CFGetTypeID(value) == AXValueGetTypeID() &&
                  AXValueGetValue((AXValueRef)value, kAXValueCGSizeType, size);
    if (value) CFRelease(value);
    return result;
}

static bool ax_size_settable(AXUIElementRef window)
{
    Boolean settable = false;
    return window && AXUIElementIsAttributeSettable(window, kAXSizeAttribute, &settable) == kAXErrorSuccess && settable;
}

static AXError ax_set_size(AXUIElementRef window, CGSize size)
{
    AXValueRef value = AXValueCreate(kAXValueCGSizeType, &size);
    if (!value) return kAXErrorFailure;
    AXError result = AXUIElementSetAttributeValue(window, kAXSizeAttribute, value);
    CFRelease(value);
    return result;
}

static bool settle_ax_size(AXUIElementRef window, CGSize *final, uint64_t *latency_ns)
{
    uint64_t start = now_ns(), first_equal = 0;
    CGSize previous = CGSizeMake(NAN, NAN);
    unsigned equal_reads = 0;
    while (now_ns() - start <= UINT64_C(2000000000)) {
        CGSize current = CGSizeZero;
        if (ax_copy_size(window, &current)) {
            if (size_equal(current, previous)) {
                if (!equal_reads) first_equal = now_ns();
                ++equal_reads;
            } else {
                equal_reads = 1;
                first_equal = now_ns();
                previous = current;
            }
            if (equal_reads >= 3 && now_ns() - first_equal >= UINT64_C(100000000)) {
                if (final) *final = current;
                if (latency_ns) *latency_ns = now_ns() - start;
                return true;
            }
        }
        spin_ms(50);
    }
    if (latency_ns) *latency_ns = now_ns() - start;
    return false;
}

static int ax_controller_execute(pid_t pid, uint32_t wid, bool write_request,
                                 CGSize request, CGSize restore)
{
    struct ax_controller_result result = { .magic = AX_CONTROLLER_MAGIC,
                                           .trusted = AXIsProcessTrusted() };
    AXUIElementRef window = ax_window_for_pid(pid, wid);
    result.found = window && ax_copy_size(window, &result.baseline);
    result.settable = ax_size_settable(window);
    result.final = result.baseline;
    result.restored = result.baseline;
    result.write_error = kAXErrorAttributeUnsupported;
    result.restore_error = kAXErrorAttributeUnsupported;
    if (write_request && result.found) {
        result.write_error = ax_set_size(window, request);
        result.settled = result.write_error == kAXErrorSuccess &&
                         settle_ax_size(window, &result.final, &result.settle_ns);
        result.restore_error = ax_set_size(window, restore);
        result.restore_settled = result.restore_error == kAXErrorSuccess &&
            settle_ax_size(window, &result.restored, &result.restore_ns) &&
            size_equal(result.restored, restore);
    }
    if (window) CFRelease(window);
    return fwrite(&result, sizeof(result), 1, stdout) == 1 && fflush(stdout) == 0
         ? EXIT_SUCCESS : EXIT_FAILURE;
}

static bool run_ax_controller(pid_t pid, uint32_t wid, bool write_request,
                              CGSize request, CGSize restore,
                              struct ax_controller_result *result)
{
    NSString *executable = NSBundle.mainBundle.executablePath;
    if (!executable || !result) return false;
    NSTask *task = [NSTask new];
    NSPipe *pipe = [NSPipe pipe];
    task.executableURL = [NSURL fileURLWithPath:executable];
    task.standardOutput = pipe;
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    task.arguments = @[
        @"--ax-controller",
        [NSString stringWithFormat:@"%d", pid],
        [NSString stringWithFormat:@"%u", wid],
        write_request ? @"resize" : @"inspect",
        [NSString stringWithFormat:@"%.17g", request.width],
        [NSString stringWithFormat:@"%.17g", request.height],
        [NSString stringWithFormat:@"%.17g", restore.width],
        [NSString stringWithFormat:@"%.17g", restore.height]
    ];
    NSError *error = nil;
    if (![task launchAndReturnError:&error]) return false;
    while (task.running) spin_ms(10);
    NSData *data = [pipe.fileHandleForReading readDataToEndOfFile];
    if (task.terminationStatus != 0 || data.length != sizeof(*result)) return false;
    memcpy(result, data.bytes, sizeof(*result));
    return result->magic == AX_CONTROLLER_MAGIC;
}

static void print_raw(const char *name, const struct raw_constraints *raw)
{
    printf("\"%s\":{\"available\":%s,\"status\":%d,\"minimum\":",
           name, raw->available ? "true" : "false", raw->status);
    print_size(raw->minimum); fputs(",\"maximum\":", stdout); print_size(raw->maximum);
    fputs(",\"current\":", stdout); print_size(raw->current);
    printf(",\"valid\":%s,\"ordered_limits\":%s,\"current_in_range\":%s,\"latency_ns\":%llu}",
           finite_positive(raw->minimum) && finite_positive(raw->maximum) && finite_positive(raw->current) ? "true" : "false",
           finite_positive(raw->minimum) && finite_positive(raw->maximum) &&
           raw->minimum.width <= raw->maximum.width && raw->minimum.height <= raw->maximum.height ? "true" : "false",
           finite_positive(raw->minimum) && finite_positive(raw->maximum) && finite_positive(raw->current) &&
           raw->current.width >= raw->minimum.width && raw->current.height >= raw->minimum.height &&
           raw->current.width <= raw->maximum.width && raw->current.height <= raw->maximum.height ? "true" : "false",
           (unsigned long long)raw->latency_ns);
}

static const char *agreement(const struct raw_constraints *iterator,
                             const struct raw_constraints *packages)
{
    if (!iterator->available && !packages->available) return "both-invalid";
    if (iterator->available && !packages->available) return "iterator-only";
    if (!iterator->available && packages->available) return "packages-only";
    if (iterator->status != packages->status) return "status-mismatch";
    return size_equal(iterator->minimum, packages->minimum) &&
           size_equal(iterator->maximum, packages->maximum) &&
           size_equal(iterator->current, packages->current) ? "equal" : "different";
}

static void collect(const struct fixture *fixture, constraints_iterator_fn iterator_getter,
                    constraints_packages_fn packages_getter, bool reverse,
                    struct raw_constraints *iterator, struct raw_constraints *packages,
                    uint64_t *skew_ns)
{
    uint32_t wid = (uint32_t)fixture->window.windowNumber;
    uint64_t first_end, second_start;
    if (!reverse) {
        iterator_constraints(wid, iterator_getter, iterator); first_end = now_ns();
        second_start = now_ns(); packages_constraints(wid, packages_getter, packages);
    } else {
        packages_constraints(wid, packages_getter, packages); first_end = now_ns();
        second_start = now_ns(); iterator_constraints(wid, iterator_getter, iterator);
    }
    *skew_ns = second_start >= first_end ? second_start - first_end : 0;
}

static void observe(const struct fixture *fixture, const char *state, unsigned epoch,
                    unsigned planned_delay_ms, uint64_t transition_start,
                    constraints_iterator_fn iterator_getter,
                    constraints_packages_fn packages_getter,
                    bool reverse, struct summary *summary,
                    struct raw_constraints *iterator_out,
                    struct raw_constraints *packages_out)
{
    struct raw_constraints iterator, packages;
    uint64_t skew_ns = 0;
    collect(fixture, iterator_getter, packages_getter, reverse, &iterator, &packages, &skew_ns);
    NSRect frame = fixture->window.frame;
    NSSize content = fixture->window.contentLayoutRect.size;
    CGSize ax_size = CGSizeZero;
    AXUIElementRef ax = ax_window((uint32_t)fixture->window.windowNumber);
    bool ax_available = ax_copy_size(ax, &ax_size);
    bool settable = fixture->controller_settable_known && fixture->controller_settable;
    if (ax) CFRelease(ax);
    const char *min_basis = basis(iterator.minimum, fixture->window.minSize,
                                  fixture->window.contentMinSize);
    const char *max_basis = basis(iterator.maximum, fixture->window.maxSize,
                                  fixture->window.contentMaxSize);
    const char *current_basis = basis(iterator.current, frame.size, content);
    const char *agree = agreement(&iterator, &packages);
    ++summary->observations;
    if (!strcmp(agree, "equal")) ++summary->agreements; else ++summary->disagreements;

    printf("{\"schema\":\"yabai.window-constraints-probe/v2\",\"kind\":\"observation\","
           "\"fixture_id\":\"%s\",\"fixture_kind\":\"%s\",\"state\":\"%s\","
           "\"state_epoch\":%u,\"planned_delay_ms\":%u,\"actual_elapsed_ms\":%llu,"
           "\"getter_order\":\"%s\",\"inter_call_skew_ns\":%llu,"
           "\"ordered\":%s,\"visible\":%s,\"minimized\":%s,\"appkit\":{\"frame\":",
           fixture->name, fixture->kind, state, epoch, planned_delay_ms,
           (unsigned long long)((now_ns() - transition_start) / UINT64_C(1000000)),
           reverse ? "packages-iterator" : "iterator-packages",
           (unsigned long long)skew_ns,
           fixture->window.isVisible ? "true" : "false",
           fixture->window.occlusionState & NSWindowOcclusionStateVisible ? "true" : "false",
           fixture->window.isMiniaturized ? "true" : "false");
    print_size(frame.size); fputs(",\"content\":", stdout); print_size(content);
    fputs(",\"frame_min\":", stdout); print_size(fixture->window.minSize);
    fputs(",\"frame_max\":", stdout); print_size(fixture->window.maxSize);
    fputs(",\"content_min\":", stdout); print_size(fixture->window.contentMinSize);
    fputs(",\"content_max\":", stdout); print_size(fixture->window.contentMaxSize);
    fputs(",\"resize_increments\":", stdout);
    print_size(fixture->content_increments ? fixture->window.contentResizeIncrements
                                           : fixture->window.resizeIncrements);
    printf("},\"ax\":{\"available\":%s,\"settable\":%s,\"size\":",
           ax_available ? "true" : "false", settable ? "true" : "false");
    print_size(ax_size); fputs("},", stdout);
    print_raw("iterator", &iterator); fputc(',', stdout); print_raw("packages", &packages);
    printf(",\"classification\":{\"minimum_basis\":\"%s\",\"maximum_basis\":\"%s\","
           "\"current_basis\":\"%s\",\"agreement_class\":\"%s\","
           "\"minimum_delta\":",
           min_basis, max_basis, current_basis, agree);
    print_size(CGSizeMake(iterator.minimum.width - packages.minimum.width,
                          iterator.minimum.height - packages.minimum.height));
    fputs(",\"maximum_delta\":", stdout);
    print_size(CGSizeMake(iterator.maximum.width - packages.maximum.width,
                          iterator.maximum.height - packages.maximum.height));
    fputs(",\"current_delta\":", stdout);
    print_size(CGSizeMake(iterator.current.width - packages.current.width,
                          iterator.current.height - packages.current.height));
    fputs("},\"authority\":{\"decision_source\":\"ax-readback\","
          "\"private_constraints_modified_request\":false,"
          "\"production_policy_enabled\":false}}\n", stdout);
    if (iterator_out) *iterator_out = iterator;
    if (packages_out) *packages_out = packages;
}

static bool predicted_adjustment(const struct raw_constraints *raw, const char *min_basis,
                                 const char *max_basis, CGSize decoration, CGSize request,
                                 bool *known, CGSize *predicted)
{
    *known = raw->available && finite_positive(raw->minimum) && finite_positive(raw->maximum) &&
             strcmp(min_basis, "other") && strcmp(max_basis, "other") &&
             strcmp(min_basis, "unknown") && strcmp(max_basis, "unknown");
    if (!*known) return false;
    CGSize minimum = raw->minimum, maximum = raw->maximum;
    if (!strcmp(min_basis, "content")) {
        minimum.width += decoration.width; minimum.height += decoration.height;
    }
    if (!strcmp(max_basis, "content")) {
        maximum.width += decoration.width; maximum.height += decoration.height;
    }
    *predicted = CGSizeMake(fmin(maximum.width, fmax(minimum.width, request.width)),
                            fmin(maximum.height, fmax(minimum.height, request.height)));
    return !size_equal(*predicted, request);
}

static const char *confusion_cell(bool known, bool predicted, bool actual, struct confusion *matrix)
{
    if (!known) { ++matrix->unscored; return "unscored"; }
    if (predicted && actual) { ++matrix->tp; return "tp"; }
    if (predicted && !actual) { ++matrix->fp; return "fp"; }
    if (!predicted && actual) { ++matrix->fn; return "fn"; }
    ++matrix->tn; return "tn";
}

static void resize_trial(const struct fixture *fixture, const char *bucket, CGSize request,
                         constraints_iterator_fn iterator_getter,
                         constraints_packages_fn packages_getter,
                         struct summary *summary)
{
    uint32_t wid = (uint32_t)fixture->window.windowNumber;
    struct raw_constraints iterator, packages;
    uint64_t skew = 0;
    collect(fixture, iterator_getter, packages_getter, false, &iterator, &packages, &skew);
    NSSize frame = fixture->window.frame.size;
    NSSize content = fixture->window.contentLayoutRect.size;
    CGSize decoration = CGSizeMake(frame.width - content.width, frame.height - content.height);
    const char *iter_min_basis = basis(iterator.minimum, fixture->window.minSize,
                                       fixture->window.contentMinSize);
    const char *iter_max_basis = basis(iterator.maximum, fixture->window.maxSize,
                                       fixture->window.contentMaxSize);
    const char *pkg_min_basis = basis(packages.minimum, fixture->window.minSize,
                                      fixture->window.contentMinSize);
    const char *pkg_max_basis = basis(packages.maximum, fixture->window.maxSize,
                                      fixture->window.contentMaxSize);
    bool iter_known = false, pkg_known = false;
    CGSize iter_final = CGSizeZero, pkg_final = CGSizeZero;
    bool iter_predict = predicted_adjustment(&iterator, iter_min_basis, iter_max_basis,
                                              decoration, request, &iter_known, &iter_final);
    bool pkg_predict = predicted_adjustment(&packages, pkg_min_basis, pkg_max_basis,
                                             decoration, request, &pkg_known, &pkg_final);
    uint64_t write_start = now_ns();
    struct ax_controller_result controller = {0};
    bool controller_ok = run_ax_controller(getpid(), wid, true, request,
                                            fixture->window.frame.size, &controller);
    AXError write_error = controller_ok ? controller.write_error : kAXErrorFailure;
    ++summary->writes;
    if (write_error != kAXErrorSuccess) ++summary->write_failures;
    CGSize baseline = controller.baseline;
    CGSize final = controller.final;
    uint64_t settle_ns = controller.settle_ns;
    bool settled = controller_ok && controller.settled;
    if (!settled && write_error == kAXErrorSuccess) ++summary->timeouts;
    bool actual_known = settled;
    bool adjusted = settled && !size_equal(final, request);
    const char *iter_cell = confusion_cell(iter_known && actual_known, iter_predict, adjusted,
                                            &summary->iterator);
    const char *pkg_cell = confusion_cell(pkg_known && actual_known, pkg_predict, adjusted,
                                           &summary->packages);
    AXError restore_error = controller_ok ? controller.restore_error : kAXErrorFailure;
    CGSize restored = controller.restored; uint64_t restore_ns = controller.restore_ns;
    bool restore_settled = controller_ok && controller.restore_settled;
    if (!restore_settled) ++summary->restore_failures;
    ConstraintsClampDelegate *delegate = fixture->delegate_clamp
        ? (ConstraintsClampDelegate *)fixture->window.delegate : nil;

    printf("{\"schema\":\"yabai.window-constraints-probe/v2\",\"kind\":\"resize_trial\","
           "\"fixture_id\":\"%s\",\"bucket\":\"%s\",\"controller_trusted\":%s,"
           "\"controller_found\":%s,\"controller_settable\":%s,\"requested\":",
           fixture->name, bucket, controller.trusted ? "true" : "false",
           controller.found ? "true" : "false", controller.settable ? "true" : "false");
    print_size(request);
    fputs(",\"pre_ax_size\":", stdout); print_size(baseline);
    printf(",\"ax_set_error\":%d,\"settled\":%s,\"settle_latency_ms\":%llu,"
           "\"final\":", write_error, settled ? "true" : "false",
           (unsigned long long)(settle_ns / UINT64_C(1000000))); print_size(final);
    printf(",\"actual_outcome\":\"%s\",\"iterator_prediction\":{\"known\":%s,"
           "\"predicts_adjustment\":%s,\"predicted_final\":",
           !controller.found ? "not-found" : write_error != kAXErrorSuccess ? "write-failed" :
           !settled ? "timeout" : adjusted ? "adjusted" : "accepted-exact",
           iter_known ? "true" : "false", iter_predict ? "true" : "false");
    print_size(iter_final);
    printf(",\"confusion\":\"%s\"},\"packages_prediction\":{\"known\":%s,"
           "\"predicts_adjustment\":%s,\"predicted_final\":",
           iter_cell, pkg_known ? "true" : "false", pkg_predict ? "true" : "false");
    print_size(pkg_final);
    printf(",\"confusion\":\"%s\"},\"delegate\":{\"calls\":%lu,\"input\":",
           pkg_cell, (unsigned long)(delegate ? delegate.calls : 0));
    print_size(delegate ? delegate.lastInput : CGSizeZero);
    fputs(",\"output\":", stdout); print_size(delegate ? delegate.lastOutput : CGSizeZero);
    printf("},\"restore\":{\"error\":%d,\"settled\":%s,\"latency_ms\":%llu,\"final\":",
           restore_error, restore_settled ? "true" : "false",
           (unsigned long long)(restore_ns / UINT64_C(1000000))); print_size(restored);
    printf("},\"write_elapsed_ms\":%llu,\"authority\":{\"decision_source\":\"ax-readback\","
           "\"private_constraints_modified_request\":false,"
           "\"production_policy_enabled\":false}}\n",
           (unsigned long long)((now_ns() - write_start) / UINT64_C(1000000)));
}

static void run_matrix(const struct fixture *fixture,
                       constraints_iterator_fn iterator_getter,
                       constraints_packages_fn packages_getter,
                       struct summary *summary)
{
    NSSize minimum = fixture->effective_min.width > 0 ? fixture->effective_min : fixture->declared_frame_min;
    NSSize maximum = fixture->effective_max.width > 0 ? fixture->effective_max : fixture->declared_frame_max;
    if (minimum.width <= 0 || maximum.width <= 0) return;
    double widths[] = { minimum.width - 40, (minimum.width + maximum.width) / 2, maximum.width + 40 };
    double heights[] = { minimum.height - 30, (minimum.height + maximum.height) / 2, maximum.height + 30 };
    const char *names[] = { "below", "within", "above" };
    char bucket[64];
    for (int w = 0; w < 3; ++w) for (int h = 0; h < 3; ++h) {
        snprintf(bucket, sizeof(bucket), "%s-%s", names[w], names[h]);
        resize_trial(fixture, bucket, CGSizeMake(widths[w], heights[h]),
                     iterator_getter, packages_getter, summary);
    }
    resize_trial(fixture, "exact-min", minimum, iterator_getter, packages_getter, summary);
    resize_trial(fixture, "exact-max", maximum, iterator_getter, packages_getter, summary);
    if (fixture->increments.width > 0 || fixture->increments.height > 0) {
        CGSize middle = CGSizeMake(widths[1], heights[1]);
        resize_trial(fixture, "off-grid-plus", CGSizeMake(middle.width + 7, middle.height + 5),
                     iterator_getter, packages_getter, summary);
        resize_trial(fixture, "off-grid-minus", CGSizeMake(middle.width - 9, middle.height - 7),
                     iterator_getter, packages_getter, summary);
    }
}

static void sample_transition(const struct fixture *fixture, const char *transition, unsigned epoch,
                              constraints_iterator_fn iterator_getter,
                              constraints_packages_fn packages_getter,
                              struct summary *summary)
{
    unsigned delays[64]; size_t count = 0;
    for (unsigned value = 0; value <= 250; value += 10) delays[count++] = value;
    for (unsigned value = 300; value <= 1000; value += 50) delays[count++] = value;
    unsigned tail[] = { 1250, 1500, 2000, 3000, 5000 };
    for (size_t i = 0; i < sizeof(tail) / sizeof(*tail); ++i) delays[count++] = tail[i];
    uint64_t start = now_ns();
    struct raw_constraints baseline_iterator = {0}, baseline_packages = {0};
    uint64_t skew = 0;
    collect(fixture, iterator_getter, packages_getter, false,
            &baseline_iterator, &baseline_packages, &skew);
    uint64_t first_iterator_change = 0, first_packages_change = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t target = start + (uint64_t)delays[i] * UINT64_C(1000000);
        while (now_ns() < target) spin_ms(1);
        struct raw_constraints iterator, packages;
        char state[96]; snprintf(state, sizeof(state), "%s-sample", transition);
        observe(fixture, state, epoch, delays[i], start, iterator_getter, packages_getter,
                (i & 1) != 0, summary, &iterator, &packages);
        if (!first_iterator_change && (!size_equal(iterator.minimum, baseline_iterator.minimum) ||
            !size_equal(iterator.maximum, baseline_iterator.maximum))) first_iterator_change = delays[i];
        if (!first_packages_change && (!size_equal(packages.minimum, baseline_packages.minimum) ||
            !size_equal(packages.maximum, baseline_packages.maximum))) first_packages_change = delays[i];
    }
    printf("{\"schema\":\"yabai.window-constraints-probe/v2\",\"kind\":\"transition_summary\","
           "\"fixture_id\":\"%s\",\"transition\":\"%s\",\"samples\":%zu,"
           "\"iterator_first_change_ms\":", fixture->name, transition, count);
    if (first_iterator_change) printf("%llu", (unsigned long long)first_iterator_change); else fputs("null", stdout);
    fputs(",\"packages_first_change_ms\":", stdout);
    if (first_packages_change) printf("%llu", (unsigned long long)first_packages_change); else fputs("null", stdout);
    fputs(",\"sample_window_ms\":5000,\"production_policy_enabled\":false}\n", stdout);
}

static NSWindow *make_window(NSRect frame, NSWindowStyleMask style)
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:frame styleMask:style
                                                    backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed = NO;
    window.title = @"Controlled Fixture";
    return window;
}

static bool constraints_equal(const struct raw_constraints *a,
                              const struct raw_constraints *b)
{
    return a->available == b->available && a->status == b->status &&
           (!a->available || (size_equal(a->minimum, b->minimum) &&
                              size_equal(a->maximum, b->maximum) &&
                              size_equal(a->current, b->current)));
}

static bool public_size_matches(CGSize raw, CGSize public_size)
{
    return finite_positive(raw) && finite_positive(public_size) && size_equal(raw, public_size);
}

static bool run_external_read_only(constraints_iterator_fn iterator_getter,
                                   constraints_packages_fn packages_getter)
{
    NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
    char build[256] = "unknown"; size_t build_size = sizeof(build);
    if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0) strcpy(build, "unknown");
    struct utsname system = {0}; uname(&system);
    printf("{\"schema\":\"yabai.window-constraints-probe/v3\","
           "\"kind\":\"run\",\"mode\":\"external-read-only\","
           "\"os_version\":\"%ld.%ld.%ld\",\"os_build\":\"%s\","
           "\"architecture\":\"%s\",\"sample_limit\":32,"
           "\"iterator_symbol\":%s,\"packages_symbol\":%s,"
           "\"ax_queries\":0,\"ax_writes\":0,\"tcc_prompt_requested\":false,"
           "\"absolute_geometry_emitted\":false,\"production_policy_enabled\":false}\n",
           version.majorVersion, version.minorVersion, version.patchVersion, build,
           system.machine, iterator_getter ? "true" : "false",
           packages_getter ? "true" : "false");

    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!windows || CFGetTypeID(windows) != CFArrayGetTypeID()) {
        if (windows) CFRelease(windows);
        return false;
    }
    NSUInteger candidates = 0, sampled = 0, iterator_available = 0;
    NSUInteger packages_available = 0, equal = 0;
    NSUInteger direct_repeat_stable = 0;
    NSUInteger limit = MIN((NSUInteger)CFArrayGetCount(windows), (NSUInteger)512);
    for (NSUInteger i = 0; i < limit && sampled < 32; ++i) {
        id row = ((__bridge NSArray *)windows)[i];
        if (![row isKindOfClass:[NSDictionary class]]) continue;
        NSNumber *owner = row[(NSString *)kCGWindowOwnerPID];
        NSNumber *number = row[(NSString *)kCGWindowNumber];
        NSNumber *layer = row[(NSString *)kCGWindowLayer];
        NSDictionary *raw_bounds = row[(NSString *)kCGWindowBounds];
        CGRect bounds = CGRectZero;
        if (![owner isKindOfClass:[NSNumber class]] || owner.intValue <= 1 ||
            owner.intValue == getpid() || ![number isKindOfClass:[NSNumber class]] ||
            !number.unsignedIntValue || number.unsignedLongLongValue > UINT32_MAX ||
            ![layer isKindOfClass:[NSNumber class]] ||
            ![raw_bounds isKindOfClass:[NSDictionary class]] ||
            !CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)raw_bounds, &bounds) ||
            !finite_positive(bounds.size)) continue;
        ++candidates;
        uint32_t wid = number.unsignedIntValue;
        struct raw_constraints iterator_first, packages_first, iterator_second, packages_second;
        iterator_constraints(wid, iterator_getter, &iterator_first);
        packages_constraints(wid, packages_getter, &packages_first);
        packages_constraints(wid, packages_getter, &packages_second);
        iterator_constraints(wid, iterator_getter, &iterator_second);
        if (iterator_first.available) ++iterator_available;
        if (packages_first.available) ++packages_available;
        if (iterator_first.available && packages_first.available &&
            !strcmp(agreement(&iterator_first, &packages_first), "equal")) ++equal;
        if ((iterator_first.available || packages_first.available) &&
            constraints_equal(&iterator_first, &iterator_second) &&
            constraints_equal(&packages_first, &packages_second)) ++direct_repeat_stable;
        ++sampled;
        printf("{\"schema\":\"yabai.window-constraints-probe/v3\","
               "\"kind\":\"external_observation\",\"ordinal\":%lu,"
               "\"owner_class\":\"external\",\"layer_class\":\"%s\",",
               (unsigned long)sampled,
               layer.intValue == 0 ? "zero" : "nonzero");
        print_raw("iterator", &iterator_first);
        fputs(",", stdout); print_raw("packages", &packages_first);
        printf(",\"getter_agreement\":\"%s\",\"repeat_stable\":%s,"
               "\"public_bounds_match\":{\"iterator_min\":%s,\"iterator_max\":%s,"
               "\"iterator_current\":%s,\"packages_min\":%s,\"packages_max\":%s,"
               "\"packages_current\":%s},\"identifiers_emitted\":false,"
               "\"ax_query\":false,\"ax_write\":false}\n",
               agreement(&iterator_first, &packages_first),
               constraints_equal(&iterator_first, &iterator_second) &&
               constraints_equal(&packages_first, &packages_second) ? "true" : "false",
               public_size_matches(iterator_first.minimum, bounds.size) ? "true" : "false",
               public_size_matches(iterator_first.maximum, bounds.size) ? "true" : "false",
               public_size_matches(iterator_first.current, bounds.size) ? "true" : "false",
               public_size_matches(packages_first.minimum, bounds.size) ? "true" : "false",
               public_size_matches(packages_first.maximum, bounds.size) ? "true" : "false",
               public_size_matches(packages_first.current, bounds.size) ? "true" : "false");
    }
    CFRelease(windows);
    printf("{\"schema\":\"yabai.window-constraints-probe/v3\","
           "\"kind\":\"external_summary\",\"candidate_windows\":%lu,"
           "\"sampled\":%lu,\"iterator_available\":%lu,\"packages_available\":%lu,"
           "\"getter_equal\":%lu,\"repeat_stable\":%lu,\"ax_queries\":0,"
           "\"ax_writes\":0,"
           "\"production_policy_enabled\":false}\n",
           (unsigned long)candidates, (unsigned long)sampled,
           (unsigned long)iterator_available, (unsigned long)packages_available,
           (unsigned long)equal, (unsigned long)direct_repeat_stable);
    return true;
}

static void print_fixture(const struct fixture *fixture)
{
    printf("{\"schema\":\"yabai.window-constraints-probe/v2\",\"kind\":\"fixture\","
           "\"fixture_id\":\"%s\",\"host_kind\":\"appkit\",\"fixture_kind\":\"%s\","
           "\"ordered\":%s,\"minimized\":%s,\"frame\":",
           fixture->name, fixture->kind, fixture->window.isVisible ? "true" : "false",
           fixture->window.isMiniaturized ? "true" : "false");
    print_size(fixture->window.frame.size); fputs(",\"declared_frame_min\":", stdout);
    print_size(fixture->declared_frame_min); fputs(",\"declared_frame_max\":", stdout);
    print_size(fixture->declared_frame_max); fputs(",\"declared_content_min\":", stdout);
    print_size(fixture->declared_content_min); fputs(",\"declared_content_max\":", stdout);
    print_size(fixture->declared_content_max); fputs(",\"increments\":", stdout);
    print_size(fixture->increments);
    printf(",\"delegate_policy\":%s,\"production_policy_enabled\":false}\n",
           fixture->delegate_clamp ? "true" : "false");
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc == 9 && strcmp(argv[1], "--ax-controller") == 0) {
            char *end = NULL;
            long pid_value = strtol(argv[2], &end, 10);
            if (!end || *end || pid_value <= 1 || pid_value > INT_MAX) return EXIT_FAILURE;
            unsigned long wid_value = strtoul(argv[3], &end, 10);
            if (!end || *end || !wid_value || wid_value > UINT32_MAX) return EXIT_FAILURE;
            bool write_request = strcmp(argv[4], "resize") == 0;
            if (!write_request && strcmp(argv[4], "inspect") != 0) return EXIT_FAILURE;
            double values[4];
            for (int i = 0; i < 4; ++i) {
                values[i] = strtod(argv[5 + i], &end);
                if (!end || *end || !isfinite(values[i])) return EXIT_FAILURE;
            }
            return ax_controller_execute((pid_t)pid_value, (uint32_t)wid_value, write_request,
                CGSizeMake(values[0], values[1]), CGSizeMake(values[2], values[3]));
        }
        bool observe_only = argc == 2 && strcmp(argv[1], "--observe-only") == 0;
        bool external_read_only = argc == 2 && strcmp(argv[1], "--external-read-only") == 0;
        if (argc != 1 && !observe_only && !external_read_only) return EXIT_FAILURE;
        constraints_iterator_fn iterator_getter = (constraints_iterator_fn)
            dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetConstraints");
        constraints_packages_fn packages_getter = (constraints_packages_fn)
            dlsym(RTLD_DEFAULT, "SLSPackagesGetWindowConstraints");
        if (external_read_only) return run_external_read_only(iterator_getter, packages_getter)
            ? EXIT_SUCCESS : EXIT_FAILURE;
        NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
        char build[256] = "unknown"; size_t build_size = sizeof(build);
        if (sysctlbyname("kern.osversion", build, &build_size, NULL, 0) != 0) strcpy(build, "unknown");
        struct utsname system = {0}; uname(&system);
        printf("{\"schema\":\"yabai.window-constraints-probe/v2\",\"kind\":\"run\","
               "\"run_id\":\"controlled-appkit-v2\",\"mode\":\"%s\","
               "\"ax_writes_enabled\":%s,\"tcc_prompt_requested\":false,"
               "\"os_version\":\"%ld.%ld.%ld\","
               "\"os_build\":\"%s\",\"architecture\":\"%s\","
               "\"symbols\":{\"iterator\":%s,\"packages\":%s},\"ax_trusted\":%s,"
               "\"sampling_schedule_ms\":\"0..250/10,300..1000/50,1250,1500,2000,3000,5000\","
               "\"electron_fixture\":\"external-controlled-host-unavailable\","
               "\"production_policy_enabled\":false}\n",
               observe_only ? "observe-only" : "resize-trials",
               observe_only ? "false" : "true",
               version.majorVersion, version.minorVersion, version.patchVersion, build, system.machine,
               iterator_getter ? "true" : "false", packages_getter ? "true" : "false",
               AXIsProcessTrusted() ? "true" : "false");
        if (!iterator_getter || (!observe_only && !AXIsProcessTrusted())) return EXIT_FAILURE;

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps:YES];
        NSWindowStyleMask titled = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        struct fixture fixtures[12] = {0};
        size_t count = 0;
#define ADD_FIXTURE(id, type, rect, style) \
        fixtures[count] = (struct fixture){ .name = id, .kind = type, \
                                            .window = make_window(rect, style) }; ++count
        ADD_FIXTURE("native-frame-control", "window", NSMakeRect(40, 60, 640, 408), titled);
        fixtures[count-1].window.minSize = fixtures[count-1].declared_frame_min = NSMakeSize(360, 260);
        fixtures[count-1].window.maxSize = fixtures[count-1].declared_frame_max = NSMakeSize(840, 620);
        fixtures[count-1].matrix = true;
        ADD_FIXTURE("native-content-control", "window", NSMakeRect(70, 90, 640, 408), titled);
        fixtures[count-1].window.contentMinSize = fixtures[count-1].declared_content_min = NSMakeSize(340, 220);
        fixtures[count-1].window.contentMaxSize = fixtures[count-1].declared_content_max = NSMakeSize(800, 580);
        fixtures[count-1].declared_frame_min = [fixtures[count-1].window frameRectForContentRect:NSMakeRect(0,0,340,220)].size;
        fixtures[count-1].declared_frame_max = [fixtures[count-1].window frameRectForContentRect:NSMakeRect(0,0,800,580)].size;
        fixtures[count-1].content_limits = true; fixtures[count-1].matrix = true;
        ADD_FIXTURE("native-fixed-control", "window", NSMakeRect(100, 120, 520, 328),
                    NSWindowStyleMaskTitled | NSWindowStyleMaskClosable);
        fixtures[count-1].window.minSize = fixtures[count-1].window.maxSize = fixtures[count-1].window.frame.size;
        fixtures[count-1].declared_frame_min = fixtures[count-1].declared_frame_max = fixtures[count-1].window.frame.size;
        ADD_FIXTURE("native-unbounded-control", "window", NSMakeRect(130, 150, 620, 378), titled);
        ADD_FIXTURE("native-delegate-clamp", "window", NSMakeRect(160, 180, 640, 408), titled);
        fixtures[count-1].window.minSize = fixtures[count-1].declared_frame_min = NSMakeSize(300, 220);
        fixtures[count-1].window.maxSize = fixtures[count-1].declared_frame_max = NSMakeSize(900, 680);
        fixtures[count-1].effective_min = NSMakeSize(440, 300); fixtures[count-1].effective_max = NSMakeSize(760, 540);
        ConstraintsClampDelegate *delegate = [ConstraintsClampDelegate new];
        delegate.minimum = fixtures[count-1].effective_min; delegate.maximum = fixtures[count-1].effective_max;
        fixtures[count-1].window.delegate = delegate; fixtures[count-1].delegate_clamp = true; fixtures[count-1].matrix = true;
        ADD_FIXTURE("native-frame-increments", "window", NSMakeRect(190, 210, 640, 448), titled);
        fixtures[count-1].window.minSize = fixtures[count-1].declared_frame_min = NSMakeSize(320, 240);
        fixtures[count-1].window.maxSize = fixtures[count-1].declared_frame_max = NSMakeSize(832, 624);
        fixtures[count-1].window.resizeIncrements = fixtures[count-1].increments = NSMakeSize(32, 24);
        fixtures[count-1].matrix = true;
        ADD_FIXTURE("native-content-increments", "window", NSMakeRect(220, 240, 640, 448), titled);
        fixtures[count-1].window.contentMinSize = fixtures[count-1].declared_content_min = NSMakeSize(300, 220);
        fixtures[count-1].window.contentMaxSize = fixtures[count-1].declared_content_max = NSMakeSize(800, 580);
        fixtures[count-1].declared_frame_min = [fixtures[count-1].window frameRectForContentRect:NSMakeRect(0,0,300,220)].size;
        fixtures[count-1].declared_frame_max = [fixtures[count-1].window frameRectForContentRect:NSMakeRect(0,0,800,580)].size;
        fixtures[count-1].window.contentResizeIncrements = fixtures[count-1].increments = NSMakeSize(25, 20);
        fixtures[count-1].content_limits = true; fixtures[count-1].content_increments = true; fixtures[count-1].matrix = true;
        ADD_FIXTURE("native-toolbar", "window", NSMakeRect(250, 270, 640, 388),
                    titled | NSWindowStyleMaskFullSizeContentView);
        fixtures[count-1].window.toolbar = [[NSToolbar alloc] initWithIdentifier:@"constraints-v2-toolbar"];
        fixtures[count-1].window.contentMinSize = fixtures[count-1].declared_content_min = NSMakeSize(360, 240);
        fixtures[count-1].window.contentMaxSize = fixtures[count-1].declared_content_max = NSMakeSize(800, 560);
        fixtures[count-1].declared_frame_min = [fixtures[count-1].window frameRectForContentRect:NSMakeRect(0,0,360,240)].size;
        fixtures[count-1].declared_frame_max = [fixtures[count-1].window frameRectForContentRect:NSMakeRect(0,0,800,560)].size;
        fixtures[count-1].content_limits = true; fixtures[count-1].matrix = true;
        NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(280, 300, 520, 320)
            styleMask:titled | NSWindowStyleMaskUtilityWindow backing:NSBackingStoreBuffered defer:NO];
        panel.releasedWhenClosed = NO; panel.title = @"Controlled Fixture";
        fixtures[count] = (struct fixture){ "native-panel", "panel", panel,
            .declared_frame_min = NSMakeSize(280,180), .declared_frame_max = NSMakeSize(680,460), .matrix = true };
        panel.minSize = fixtures[count].declared_frame_min; panel.maxSize = fixtures[count].declared_frame_max; ++count;
        ADD_FIXTURE("native-tabs-a", "tab", NSMakeRect(310, 330, 640, 408), titled);
        fixtures[count-1].window.minSize = fixtures[count-1].declared_frame_min = NSMakeSize(360,260);
        fixtures[count-1].window.maxSize = fixtures[count-1].declared_frame_max = NSMakeSize(840,620);
        fixtures[count-1].window.tabbingIdentifier = @"constraints-v2-tabs"; fixtures[count-1].matrix = true;
        ADD_FIXTURE("native-tabs-b", "tab", NSMakeRect(310, 330, 640, 408), titled);
        fixtures[count-1].window.minSize = fixtures[count-1].declared_frame_min = NSMakeSize(520,340);
        fixtures[count-1].window.maxSize = fixtures[count-1].declared_frame_max = NSMakeSize(720,500);
        fixtures[count-1].window.tabbingIdentifier = @"constraints-v2-tabs"; fixtures[count-1].matrix = true;
        [fixtures[count-2].window addTabbedWindow:fixtures[count-1].window ordered:NSWindowAbove];
        ADD_FIXTURE("native-lifecycle-dynamic", "window", NSMakeRect(340, 360, 640, 408), titled);
        fixtures[count-1].window.minSize = fixtures[count-1].declared_frame_min = NSMakeSize(340,240);
        fixtures[count-1].window.maxSize = fixtures[count-1].declared_frame_max = NSMakeSize(840,620);
        fixtures[count-1].matrix = true;
#undef ADD_FIXTURE

        NSWindow *sheet_parent = make_window(NSMakeRect(370, 390, 700, 460), titled);
        NSPanel *sheet = [[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,520,320)
            styleMask:titled backing:NSBackingStoreBuffered defer:NO];
        sheet.releasedWhenClosed = NO; sheet.minSize = NSMakeSize(400,240); sheet.maxSize = NSMakeSize(640,400);

        for (size_t i = 0; i < count; ++i) [fixtures[i].window orderFront:nil];
        [sheet_parent orderFront:nil];
        spin_ms(750);
        struct summary summary = {0};
        for (size_t i = 0; i < count; ++i) {
            struct ax_controller_result inspect = {0};
            fixtures[i].controller_settable_known = run_ax_controller(
                getpid(), (uint32_t)fixtures[i].window.windowNumber, false,
                CGSizeZero, CGSizeZero, &inspect);
            fixtures[i].controller_settable = inspect.settable;
            print_fixture(&fixtures[i]);
            observe(&fixtures[i], "ordered-quiescent", 1, 0, now_ns(), iterator_getter,
                    packages_getter, false, &summary, NULL, NULL);
        }

        [sheet_parent beginSheet:sheet completionHandler:nil]; spin_ms(250);
        struct fixture sheet_fixture = { .name = "native-sheet", .kind = "sheet", .window = sheet,
            .declared_frame_min = NSMakeSize(400,240), .declared_frame_max = NSMakeSize(640,400) };
        struct ax_controller_result sheet_inspect = {0};
        sheet_fixture.controller_settable_known = run_ax_controller(
            getpid(), (uint32_t)sheet.windowNumber, false, CGSizeZero, CGSizeZero, &sheet_inspect);
        sheet_fixture.controller_settable = sheet_inspect.settable;
        print_fixture(&sheet_fixture);
        observe(&sheet_fixture, "attached", 1, 0, now_ns(), iterator_getter, packages_getter,
                false, &summary, NULL, NULL);
        [sheet_parent endSheet:sheet]; spin_ms(250);
        [sheet orderFront:nil];
        observe(&sheet_fixture, "detached", 2, 0, now_ns(), iterator_getter, packages_getter,
                true, &summary, NULL, NULL);
        [sheet orderOut:nil]; [sheet_parent beginSheet:sheet completionHandler:nil]; spin_ms(250);
        observe(&sheet_fixture, "reattached", 3, 0, now_ns(), iterator_getter, packages_getter,
                false, &summary, NULL, NULL);
        [sheet_parent endSheet:sheet]; [sheet close]; [sheet_parent close];

        delegate.minimum = NSMakeSize(500,340); delegate.maximum = NSMakeSize(700,500);
        sample_transition(&fixtures[4], "delegate-tighten", 2, iterator_getter, packages_getter, &summary);
        delegate.minimum = NSMakeSize(440,300); delegate.maximum = NSMakeSize(760,540);
        sample_transition(&fixtures[4], "delegate-loosen", 3, iterator_getter, packages_getter, &summary);
        fixtures[7].window.toolbar.visible = NO;
        sample_transition(&fixtures[7], "toolbar-hidden", 2, iterator_getter, packages_getter, &summary);
        fixtures[7].window.toolbar.visible = YES;
        sample_transition(&fixtures[7], "toolbar-visible", 3, iterator_getter, packages_getter, &summary);
        [fixtures[10].window makeKeyAndOrderFront:nil];
        sample_transition(&fixtures[10], "tab-b-selected", 2, iterator_getter, packages_getter, &summary);
        [fixtures[9].window makeKeyAndOrderFront:nil];
        sample_transition(&fixtures[9], "tab-a-return", 3, iterator_getter, packages_getter, &summary);

        struct fixture *lifecycle = &fixtures[11];
        [lifecycle->window miniaturize:nil]; spin_ms(250);
        lifecycle->window.minSize = NSMakeSize(500,340); lifecycle->window.maxSize = NSMakeSize(720,520);
        lifecycle->declared_frame_min = lifecycle->window.minSize;
        lifecycle->declared_frame_max = lifecycle->window.maxSize;
        sample_transition(lifecycle, "minimized-limit-change", 2, iterator_getter, packages_getter, &summary);
        [lifecycle->window deminiaturize:nil]; [lifecycle->window orderFront:nil]; spin_ms(500);
        sample_transition(lifecycle, "restored", 3, iterator_getter, packages_getter, &summary);

        for (size_t i = 0; !observe_only && i < count; ++i) if (fixtures[i].matrix) {
            if (i == 9 || i == 10) {
                [fixtures[i].window makeKeyAndOrderFront:nil];
                spin_ms(250);
            }
            run_matrix(&fixtures[i], iterator_getter, packages_getter, &summary);
        }
        for (size_t i = 0; i < count; ++i) [fixtures[i].window close];

        printf("{\"schema\":\"yabai.window-constraints-probe/v2\",\"kind\":\"summary\","
               "\"observations\":%u,\"agreements\":%u,\"disagreements\":%u,"
               "\"ax_writes\":%u,\"write_failures\":%u,\"timeouts\":%u,"
               "\"restore_failures\":%u,\"iterator_confusion\":{\"tp\":%u,\"fp\":%u,"
               "\"tn\":%u,\"fn\":%u,\"unscored\":%u},"
               "\"packages_confusion\":{\"tp\":%u,\"fp\":%u,\"tn\":%u,"
               "\"fn\":%u,\"unscored\":%u},\"uncontrolled_windows_touched\":0,"
               "\"requests_modified_by_constraints\":0,\"recommendation\":\"diagnostic-only\"}\n",
               summary.observations, summary.agreements, summary.disagreements,
               summary.writes, summary.write_failures, summary.timeouts, summary.restore_failures,
               summary.iterator.tp, summary.iterator.fp, summary.iterator.tn,
               summary.iterator.fn, summary.iterator.unscored,
               summary.packages.tp, summary.packages.fp, summary.packages.tn,
               summary.packages.fn, summary.packages.unscored);
        return EXIT_SUCCESS;
    }
}
