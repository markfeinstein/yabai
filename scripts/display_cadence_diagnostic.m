#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include "display_cadence_diagnostic_core.h"

#define DISPLAY_CADENCE_DEFAULT_DURATION 1.0
#define DISPLAY_CADENCE_DEFAULT_MAX_SAMPLES 1000U
#define DISPLAY_CADENCE_INVALIDATION_WAIT 0.25

struct display_cadence_runtime_screen
{
    CADisplayLink *link;
    struct display_cadence_sample *samples;
    size_t sample_count;
    uint64_t callback_count;
};

struct display_cadence_runtime
{
    NSArray *screens;
    struct display_cadence_runtime_screen *screen_data;
    size_t screen_count;
    size_t max_samples;
    size_t links_created;
    pthread_mutex_t lock;
};

@interface DisplayCadenceProbe : NSObject
- (instancetype)initWithRuntime:(struct display_cadence_runtime *)runtime;
- (void)displayLinkDidFire:(CADisplayLink *)sender;
@end

@implementation DisplayCadenceProbe
{
    struct display_cadence_runtime *_runtime;
}

- (instancetype)initWithRuntime:(struct display_cadence_runtime *)runtime
{
    self = [super init];
    if (self) _runtime = runtime;
    return self;
}

- (void)displayLinkDidFire:(CADisplayLink *)sender
{
    if (!_runtime || !sender) return;
    pthread_mutex_lock(&_runtime->lock);
    for (size_t i = 0; i < _runtime->screen_count; ++i) {
        struct display_cadence_runtime_screen *screen = _runtime->screen_data + i;
        if (screen->link != sender) continue;
        ++screen->callback_count;
        if (screen->sample_count < _runtime->max_samples) {
            screen->samples[screen->sample_count++] = (struct display_cadence_sample) {
                .target_timestamp = sender.targetTimestamp,
                .wall_timestamp = CACurrentMediaTime()
            };
        }
        pthread_mutex_unlock(&_runtime->lock);
        return;
    }
    pthread_mutex_unlock(&_runtime->lock);
}
@end

static void display_cadence_usage(const char *program)
{
    fprintf(stderr, "usage: %s --source-revision REVISION --fixture-revision REVISION "
                    "[--duration-seconds SECONDS] [--max-samples COUNT]\n", program);
}

static bool display_cadence_parse_double(const char *value, double *result)
{
    if (!value || !value[0] || !result) return false;
    char *end = NULL;
    errno = 0;
    double parsed = strtod(value, &end);
    if (errno || end == value || *end || !isfinite(parsed)) return false;
    *result = parsed;
    return true;
}

static bool display_cadence_parse_size(const char *value, size_t *result)
{
    if (!value || !value[0] || !result) return false;
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || end == value || *end || parsed == 0 || parsed > SIZE_MAX) return false;
    *result = (size_t) parsed;
    return true;
}

static char *display_cadence_sysctl_string(const char *name)
{
    size_t size = 0;
    if (sysctlbyname(name, NULL, &size, NULL, 0) != 0 || !size) return NULL;
    char *value = calloc(size, 1);
    if (!value || sysctlbyname(name, value, &size, NULL, 0) != 0) {
        free(value);
        return NULL;
    }
    return value;
}

static char *display_cadence_os_version(void)
{
    NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
    char buffer[64];
    int written = snprintf(buffer, sizeof(buffer), "%ld.%ld.%ld", (long) version.majorVersion,
                           (long) version.minorVersion, (long) version.patchVersion);
    if (written < 0 || (size_t) written >= sizeof(buffer)) return NULL;
    return strdup(buffer);
}

static bool display_cadence_screen_facts(NSScreen *screen,
                                         struct display_cadence_screen *facts)
{
    if (!screen || !facts) return false;
    memset(facts, 0, sizeof(*facts));
    NSInteger nominal = screen.maximumFramesPerSecond;
    if (nominal > 0) {
        facts->nominal_refresh_available = true;
        facts->nominal_refresh_hz = (double) nominal;
    }
    CGFloat scale = screen.backingScaleFactor;
    if (isfinite(scale) && scale > 0.0) {
        facts->backing_scale_available = true;
        facts->backing_scale = scale;
    }
    NSNumber *screen_number = screen.deviceDescription[@"NSScreenNumber"];
    CGDirectDisplayID display = screen_number ? (CGDirectDisplayID) screen_number.unsignedIntValue : 0;
    CGDisplayModeRef mode = display ? CGDisplayCopyDisplayMode(display) : NULL;
    if (mode) {
        double refresh = CGDisplayModeGetRefreshRate(mode);
        if (isfinite(refresh) && refresh > 0.0) {
            facts->current_mode_refresh_available = true;
            facts->current_mode_refresh_hz = refresh;
        }
        CGDisplayModeRelease(mode);
    }
    return true;
}

static bool display_cadence_run(struct display_cadence_runtime *runtime,
                                double duration_seconds,
                                uint64_t *callbacks_before,
                                uint64_t *callbacks_after,
                                double *wait_seconds)
{
    if (!runtime || !callbacks_before || !callbacks_after || !wait_seconds) return false;
    *callbacks_before = 0;
    *callbacks_after = 0;
    *wait_seconds = 0.0;
    DisplayCadenceProbe *probe = [[DisplayCadenceProbe alloc] initWithRuntime:runtime];
    if (!probe) return false;
    NSRunLoop *run_loop = [NSRunLoop currentRunLoop];
    for (size_t i = 0; i < runtime->screen_count; ++i) {
        NSScreen *screen = [runtime->screens objectAtIndex:i];
        CADisplayLink *link = [screen displayLinkWithTarget:probe
                                                    selector:@selector(displayLinkDidFire:)];
        if (!link) continue;
        link = [link retain];
        runtime->screen_data[i].link = link;
        ++runtime->links_created;
        [link addToRunLoop:run_loop forMode:NSDefaultRunLoopMode];
    }

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:duration_seconds];
    while ([deadline timeIntervalSinceNow] > 0.0) {
        NSDate *next = [NSDate dateWithTimeIntervalSinceNow:0.05];
        if ([next compare:deadline] == NSOrderedDescending) next = deadline;
        [run_loop runMode:NSDefaultRunLoopMode beforeDate:next];
    }
    pthread_mutex_lock(&runtime->lock);
    for (size_t i = 0; i < runtime->screen_count; ++i) {
        *callbacks_before += runtime->screen_data[i].callback_count;
    }
    pthread_mutex_unlock(&runtime->lock);
    for (size_t i = 0; i < runtime->screen_count; ++i) {
        if (runtime->screen_data[i].link) [runtime->screen_data[i].link invalidate];
    }

    CFTimeInterval wait_start = CACurrentMediaTime();
    NSDate *wait_deadline = [NSDate dateWithTimeIntervalSinceNow:DISPLAY_CADENCE_INVALIDATION_WAIT];
    while ([wait_deadline timeIntervalSinceNow] > 0.0) {
        NSDate *next = [NSDate dateWithTimeIntervalSinceNow:0.05];
        if ([next compare:wait_deadline] == NSOrderedDescending) next = wait_deadline;
        [run_loop runMode:NSDefaultRunLoopMode beforeDate:next];
    }
    *wait_seconds = CACurrentMediaTime() - wait_start;
    pthread_mutex_lock(&runtime->lock);
    for (size_t i = 0; i < runtime->screen_count; ++i) {
        *callbacks_after += runtime->screen_data[i].callback_count;
    }
    pthread_mutex_unlock(&runtime->lock);
    for (size_t i = 0; i < runtime->screen_count; ++i) {
        if (runtime->screen_data[i].link) {
            [runtime->screen_data[i].link release];
            runtime->screen_data[i].link = nil;
        }
    }
    [probe release];
    return true;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *source_revision = NULL;
        const char *fixture_revision = NULL;
        double duration_seconds = DISPLAY_CADENCE_DEFAULT_DURATION;
        size_t max_samples = DISPLAY_CADENCE_DEFAULT_MAX_SAMPLES;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--help") == 0) {
                display_cadence_usage(argv[0]);
                return EXIT_SUCCESS;
            } else if (strcmp(argv[i], "--source-revision") == 0 && i + 1 < argc) {
                source_revision = argv[++i];
            } else if (strcmp(argv[i], "--fixture-revision") == 0 && i + 1 < argc) {
                fixture_revision = argv[++i];
            } else if (strcmp(argv[i], "--duration-seconds") == 0 && i + 1 < argc) {
                if (!display_cadence_parse_double(argv[++i], &duration_seconds)) {
                    display_cadence_usage(argv[0]); return EXIT_FAILURE;
                }
            } else if (strcmp(argv[i], "--max-samples") == 0 && i + 1 < argc) {
                if (!display_cadence_parse_size(argv[++i], &max_samples)) {
                    display_cadence_usage(argv[0]); return EXIT_FAILURE;
                }
            } else {
                display_cadence_usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        if (!source_revision || !fixture_revision || duration_seconds <= 0.0 ||
            duration_seconds > 60.0 || max_samples > DISPLAY_CADENCE_DIAGNOSTIC_MAX_SAMPLES) {
            display_cadence_usage(argv[0]);
            return EXIT_FAILURE;
        }

        NSArray *screens = [[NSScreen screens] copy];
        size_t screen_count = screens.count;
        if (screen_count > 64) { [screens release]; return EXIT_FAILURE; }
        struct display_cadence_runtime runtime = {
            .screens = screens,
            .screen_count = screen_count,
            .max_samples = max_samples,
            .links_created = 0
        };
        if (pthread_mutex_init(&runtime.lock, NULL) != 0) {
            [screens release];
            return EXIT_FAILURE;
        }
        runtime.screen_data = calloc(screen_count, sizeof(*runtime.screen_data));
        if ((!runtime.screen_data && screen_count) ||
            max_samples > SIZE_MAX / sizeof(struct display_cadence_sample)) {
            [screens release];
            free(runtime.screen_data);
            pthread_mutex_destroy(&runtime.lock);
            return EXIT_FAILURE;
        }
        for (size_t i = 0; i < screen_count; ++i) {
            runtime.screen_data[i].samples = calloc(max_samples, sizeof(*runtime.screen_data[i].samples));
            if (!runtime.screen_data[i].samples) {
                for (size_t j = 0; j < i; ++j) free(runtime.screen_data[j].samples);
                free(runtime.screen_data);
                [screens release];
                pthread_mutex_destroy(&runtime.lock);
                return EXIT_FAILURE;
            }
        }

        uint64_t callbacks_before = 0, callbacks_after = 0;
        double wait_seconds = 0.0;
        bool ran = display_cadence_run(&runtime, duration_seconds, &callbacks_before,
                                       &callbacks_after, &wait_seconds);
        char *os_version = display_cadence_os_version();
        char *os_build = display_cadence_sysctl_string("kern.osversion");
        struct utsname system_info = {0};
        bool platform_ok = os_version && os_build && uname(&system_info) == 0;
        struct display_cadence_screen *screens_out = calloc(screen_count, sizeof(*screens_out));
        if (!ran || !platform_ok || (!screens_out && screen_count)) {
            free(screens_out); free(os_version); free(os_build);
            for (size_t i = 0; i < screen_count; ++i) free(runtime.screen_data[i].samples);
            free(runtime.screen_data); [screens release];
            pthread_mutex_destroy(&runtime.lock);
            return EXIT_FAILURE;
        }
        for (size_t i = 0; i < screen_count; ++i) {
            display_cadence_screen_facts([screens objectAtIndex:i], screens_out + i);
            screens_out[i].series = (struct display_cadence_series) {
                .samples = runtime.screen_data[i].samples,
                .sample_count = runtime.screen_data[i].sample_count,
                .callback_count = runtime.screen_data[i].callback_count
            };
        }
        struct display_cadence_metadata metadata = {
            .source_revision = source_revision,
            .fixture_revision = fixture_revision,
            .os_version = os_version,
            .os_build = os_build,
            .architecture = system_info.machine,
            .requested_duration_seconds = duration_seconds,
            .requested_max_samples = max_samples,
            .screen_count = screen_count,
            .links_created = runtime.links_created,
            .callbacks_before_invalidate = callbacks_before,
            .callbacks_after_invalidate = callbacks_after - callbacks_before,
            .invalidation_wait_seconds = wait_seconds,
            .invalidation_wait_bounded = wait_seconds <= DISPLAY_CADENCE_INVALIDATION_WAIT + 0.05,
            .production_backend_unchanged = true
        };
        bool written = display_cadence_diagnostic_write_json(stdout, &metadata, screens_out);
        free(screens_out); free(os_version); free(os_build);
        for (size_t i = 0; i < screen_count; ++i) free(runtime.screen_data[i].samples);
        free(runtime.screen_data); [screens release];
        pthread_mutex_destroy(&runtime.lock);
        return written ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
