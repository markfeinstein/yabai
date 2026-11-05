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
#define DISPLAY_CADENCE_STRESS_ACTIVE_SECONDS 0.08
#define DISPLAY_CADENCE_STRESS_INVALIDATION_WAIT 0.025

struct display_cadence_stress_runtime_lane
{
    CGDirectDisplayID display_id;
    struct display_cadence_stress_lane output;
    CADisplayLink *link;
    id target;
    uint64_t cycle_callbacks;
    uint64_t cycle_callbacks_before_invalidate;
    double cycle_last_callback_wall;
};

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

@interface DisplayCadenceStressProbe : NSObject
- (instancetype)initWithLane:(struct display_cadence_stress_runtime_lane *)lane
                         lock:(pthread_mutex_t *)lock;
- (void)displayLinkDidFire:(CADisplayLink *)sender;
- (void)callbackSnapshot:(uint64_t *)callbacks lastWall:(double *)last_wall;
@end

@implementation DisplayCadenceStressProbe
{
    struct display_cadence_stress_runtime_lane *_lane;
    pthread_mutex_t *_lock;
    uint64_t _cycle_callbacks;
    double _cycle_last_callback_wall;
}

- (instancetype)initWithLane:(struct display_cadence_stress_runtime_lane *)lane
                         lock:(pthread_mutex_t *)lock
{
    self = [super init];
    if (self) { _lane = lane; _lock = lock; }
    return self;
}

- (void)displayLinkDidFire:(CADisplayLink *)sender
{
    if (!_lane || !_lock || !sender) return;
    pthread_mutex_lock(_lock);
    ++_lane->output.callback_count;
    ++_cycle_callbacks;
    _cycle_last_callback_wall = CACurrentMediaTime();
    pthread_mutex_unlock(_lock);
}

- (void)callbackSnapshot:(uint64_t *)callbacks lastWall:(double *)last_wall
{
    if (!_lock) return;
    pthread_mutex_lock(_lock);
    if (callbacks) *callbacks = _cycle_callbacks;
    if (last_wall) *last_wall = _cycle_last_callback_wall;
    pthread_mutex_unlock(_lock);
}
@end

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
                    "[--duration-seconds SECONDS] [--max-samples COUNT] [--stress "
                    "[--stress-cycles COUNT] [--reinventory-interval COUNT] "
                    "[--main-thread-load-ms MS]]\n", program);
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

static CGDirectDisplayID display_cadence_display_id(NSScreen *screen)
{
    NSNumber *number = screen.deviceDescription[@"NSScreenNumber"];
    return number ? (CGDirectDisplayID) number.unsignedIntValue : 0;
}

static bool display_cadence_screens_equal(NSArray *left, NSArray *right)
{
    if (left.count != right.count) return false;
    for (NSUInteger i = 0; i < left.count; ++i) {
        NSScreen *a = [left objectAtIndex:i], *b = [right objectAtIndex:i];
        CGDirectDisplayID a_id = display_cadence_display_id(a);
        CGDirectDisplayID b_id = display_cadence_display_id(b);
        if (a_id || b_id) {
            if (!a_id || !b_id || a_id != b_id) return false;
        } else if (!NSEqualRects(a.frame, b.frame) ||
                   fabs(a.backingScaleFactor - b.backingScaleFactor) > 0.001 ||
                   a.maximumFramesPerSecond != b.maximumFramesPerSecond) {
            return false;
        }
    }
    return true;
}

static bool display_cadence_stress_reinventory(
    NSArray **screens, struct display_cadence_stress_runtime_lane **lanes,
    size_t *screen_count, bool *changed)
{
    if (!screens || !lanes || !screen_count || !changed) return false;
    *changed = false;
    NSArray *fresh = [[NSScreen screens] copy];
    if (!fresh || fresh.count > 64) { [fresh release]; return false; }
    if (display_cadence_screens_equal(*screens, fresh)) {
        for (size_t i = 0; i < fresh.count; ++i) {
            display_cadence_screen_facts([fresh objectAtIndex:i],
                                         &(*lanes)[i].output.facts);
        }
        [*screens release];
        *screens = fresh;
        return true;
    }
    struct display_cadence_stress_runtime_lane *replacement =
        calloc(fresh.count, sizeof(*replacement));
    if (!replacement && fresh.count) { [fresh release]; return false; }
    for (size_t i = 0; i < fresh.count; ++i) {
        NSScreen *new_screen = [fresh objectAtIndex:i];
        replacement[i].display_id = display_cadence_display_id(new_screen);
        display_cadence_screen_facts(new_screen, &replacement[i].output.facts);
        for (size_t j = 0; j < *screen_count; ++j) {
            NSScreen *old_screen = [*screens objectAtIndex:j];
            bool match = replacement[i].display_id
                ? replacement[i].display_id == (*lanes)[j].display_id
                : display_cadence_screens_equal(@[ old_screen ], @[ new_screen ]);
            if (match) {
                struct display_cadence_screen facts = replacement[i].output.facts;
                replacement[i].output = (*lanes)[j].output;
                replacement[i].output.facts = facts;
                break;
            }
        }
    }
    free(*lanes);
    [*screens release];
    *screens = fresh;
    *lanes = replacement;
    *screen_count = fresh.count;
    *changed = true;
    return true;
}

static void display_cadence_stress_runloop_for(NSRunLoop *run_loop, double seconds)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([deadline timeIntervalSinceNow] > 0.0) {
        NSDate *next = [NSDate dateWithTimeIntervalSinceNow:0.01];
        if ([next compare:deadline] == NSOrderedDescending) next = deadline;
        [run_loop runMode:NSDefaultRunLoopMode beforeDate:next];
    }
}

static void display_cadence_stress_load(double milliseconds)
{
    if (milliseconds <= 0.0) return;
    CFTimeInterval deadline = CACurrentMediaTime() + milliseconds / 1000.0;
    volatile double value = 1.0;
    while (CACurrentMediaTime() < deadline) value = value * 1.0000001 + 0.000001;
    (void)value;
}

static bool display_cadence_run_stress(const char *source_revision,
                                       const char *fixture_revision,
                                       size_t requested_cycles,
                                       size_t reinventory_interval,
                                       double main_thread_load_ms)
{
    [NSApplication sharedApplication];
    NSArray *screens = [[NSScreen screens] copy];
    size_t screen_count = screens.count;
    if (!screens || screen_count > 64) { [screens release]; return false; }
    struct display_cadence_stress_runtime_lane *lanes =
        calloc(screen_count, sizeof(*lanes));
    pthread_mutex_t lock;
    if ((!lanes && screen_count) || pthread_mutex_init(&lock, NULL) != 0) {
        free(lanes); [screens release]; return false;
    }
    for (size_t i = 0; i < screen_count; ++i) {
        NSScreen *screen = [screens objectAtIndex:i];
        lanes[i].display_id = display_cadence_display_id(screen);
        display_cadence_screen_facts(screen, &lanes[i].output.facts);
    }
    NSRunLoop *run_loop = [NSRunLoop currentRunLoop];
    size_t cycles_completed = 0, reinventory_count = 0, topology_changes = 0;
    size_t links_created = 0, link_creation_failures = 0, all_lanes_completed = 0;
    uint64_t callbacks_before = 0, callbacks_after = 0;
    double maximum_invalidation_wait = 0.0, maximum_lane_skew = 0.0;
    bool success = true;

    for (size_t cycle = 0; cycle < requested_cycles && success; ++cycle) {
        if (cycle && cycle % reinventory_interval == 0) {
            bool changed = false;
            if (!display_cadence_stress_reinventory(&screens, &lanes, &screen_count, &changed)) {
                success = false; break;
            }
            ++reinventory_count;
            if (changed) ++topology_changes;
        }
        for (size_t i = 0; i < screen_count; ++i) {
            lanes[i].cycle_callbacks = 0;
            lanes[i].cycle_last_callback_wall = 0.0;
            ++lanes[i].output.cycles_seen;
            DisplayCadenceStressProbe *target = [[DisplayCadenceStressProbe alloc]
                initWithLane:lanes + i lock:&lock];
            if (!target) { ++link_creation_failures; continue; }
            CADisplayLink *link = [[screens objectAtIndex:i]
                displayLinkWithTarget:target selector:@selector(displayLinkDidFire:)];
            if (!link) {
                ++link_creation_failures;
                [target release];
                continue;
            }
            lanes[i].target = target;
            lanes[i].link = [link retain];
            ++links_created;
            [link addToRunLoop:run_loop forMode:NSDefaultRunLoopMode];
        }

        NSDate *active_deadline = [NSDate dateWithTimeIntervalSinceNow:
            DISPLAY_CADENCE_STRESS_ACTIVE_SECONDS];
        while ([active_deadline timeIntervalSinceNow] > 0.0) {
            NSDate *next = [NSDate dateWithTimeIntervalSinceNow:0.01];
            if ([next compare:active_deadline] == NSOrderedDescending) next = active_deadline;
            [run_loop runMode:NSDefaultRunLoopMode beforeDate:next];
            display_cadence_stress_load(main_thread_load_ms);
        }

        uint64_t cycle_callbacks = 0;
        bool all_current_lanes_completed = screen_count > 0;
        double first_completion = INFINITY, last_completion = 0.0;
        for (size_t i = 0; i < screen_count; ++i) {
            uint64_t callbacks = 0;
            double last_wall = 0.0;
            if (lanes[i].target) {
                [(DisplayCadenceStressProbe *)lanes[i].target
                    callbackSnapshot:&callbacks lastWall:&last_wall];
            }
            lanes[i].cycle_callbacks = callbacks;
            lanes[i].cycle_last_callback_wall = last_wall;
            cycle_callbacks += callbacks;
            lanes[i].cycle_callbacks_before_invalidate = callbacks;
            if (callbacks) {
                ++lanes[i].output.cycles_with_callback;
                if (last_wall < first_completion) first_completion = last_wall;
                if (last_wall > last_completion) last_completion = last_wall;
            } else {
                all_current_lanes_completed = false;
            }
            if (!lanes[i].link) all_current_lanes_completed = false;
        }
        callbacks_before += cycle_callbacks;
        if (all_current_lanes_completed) {
            ++all_lanes_completed;
            double skew = last_completion - first_completion;
            if (skew > maximum_lane_skew) maximum_lane_skew = skew;
        }

        for (size_t i = 0; i < screen_count; ++i) {
            if (lanes[i].link) [lanes[i].link invalidate];
        }
        CFTimeInterval wait_start = CACurrentMediaTime();
        display_cadence_stress_runloop_for(run_loop,
                                           DISPLAY_CADENCE_STRESS_INVALIDATION_WAIT);
        double waited = CACurrentMediaTime() - wait_start;
        if (waited > maximum_invalidation_wait) maximum_invalidation_wait = waited;
        for (size_t i = 0; i < screen_count; ++i) {
            uint64_t after = lanes[i].cycle_callbacks_before_invalidate;
            if (lanes[i].target) {
                [(DisplayCadenceStressProbe *)lanes[i].target
                    callbackSnapshot:&after lastWall:NULL];
            }
            after -= lanes[i].cycle_callbacks_before_invalidate;
            lanes[i].output.callbacks_after_invalidate += after;
            callbacks_after += after;
        }
        for (size_t i = 0; i < screen_count; ++i) {
            if (lanes[i].link) { [lanes[i].link release]; lanes[i].link = nil; }
            if (lanes[i].target) { [lanes[i].target release]; lanes[i].target = nil; }
        }
        ++cycles_completed;
    }

    struct display_cadence_stress_metadata metadata = {0};
    struct display_cadence_stress_lane *output_lanes =
        calloc(screen_count, sizeof(*output_lanes));
    char *os_version = display_cadence_os_version();
    char *os_build = display_cadence_sysctl_string("kern.osversion");
    struct utsname system_info = {0};
    bool platform_ok = os_version && os_build && uname(&system_info) == 0;
    if (!output_lanes && screen_count) success = false;
    if (success && platform_ok) {
        for (size_t i = 0; i < screen_count; ++i) output_lanes[i] = lanes[i].output;
        metadata = (struct display_cadence_stress_metadata) {
            .source_revision = source_revision, .fixture_revision = fixture_revision,
            .os_version = os_version, .os_build = os_build,
            .architecture = system_info.machine,
            .cycles_requested = requested_cycles, .cycles_completed = cycles_completed,
            .reinventory_interval = reinventory_interval,
            .reinventory_count = reinventory_count,
            .topology_changes_observed = topology_changes,
            .screen_count = screen_count, .links_created = links_created,
            .link_creation_failures = link_creation_failures,
            .cycles_with_all_lanes_callback = all_lanes_completed,
            .callbacks_before_invalidate = callbacks_before,
            .callbacks_after_invalidate = callbacks_after,
            .active_cycle_seconds = DISPLAY_CADENCE_STRESS_ACTIVE_SECONDS,
            .invalidation_wait_seconds = maximum_invalidation_wait,
            .main_thread_load_ms = main_thread_load_ms,
            .lane_completion_skew_max_seconds = maximum_lane_skew,
            .production_backend_unchanged = true
        };
        success = display_cadence_stress_write_json(stdout, &metadata, output_lanes);
    } else {
        success = false;
    }
    for (size_t i = 0; i < screen_count; ++i) {
        if (lanes[i].link) { [lanes[i].link invalidate]; [lanes[i].link release]; }
        if (lanes[i].target) [lanes[i].target release];
    }
    free(output_lanes); free(os_version); free(os_build); free(lanes);
    [screens release];
    pthread_mutex_destroy(&lock);
    return success;
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
        bool stress_mode = false;
        bool stress_options_used = false;
        size_t stress_cycles = 20;
        size_t reinventory_interval = 5;
        double main_thread_load_ms = 0.0;
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
            } else if (strcmp(argv[i], "--stress") == 0) {
                stress_mode = true;
            } else if (strcmp(argv[i], "--stress-cycles") == 0 && i + 1 < argc) {
                stress_options_used = true;
                if (!display_cadence_parse_size(argv[++i], &stress_cycles)) {
                    display_cadence_usage(argv[0]); return EXIT_FAILURE;
                }
            } else if (strcmp(argv[i], "--reinventory-interval") == 0 && i + 1 < argc) {
                stress_options_used = true;
                if (!display_cadence_parse_size(argv[++i], &reinventory_interval)) {
                    display_cadence_usage(argv[0]); return EXIT_FAILURE;
                }
            } else if (strcmp(argv[i], "--main-thread-load-ms") == 0 && i + 1 < argc) {
                stress_options_used = true;
                if (!display_cadence_parse_double(argv[++i], &main_thread_load_ms)) {
                    display_cadence_usage(argv[0]); return EXIT_FAILURE;
                }
            } else {
                display_cadence_usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        if (!source_revision || !fixture_revision || duration_seconds <= 0.0 ||
            duration_seconds > 60.0 || max_samples > DISPLAY_CADENCE_DIAGNOSTIC_MAX_SAMPLES ||
            (stress_options_used && !stress_mode) ||
            stress_cycles > DISPLAY_CADENCE_DIAGNOSTIC_MAX_STRESS_CYCLES ||
            reinventory_interval > DISPLAY_CADENCE_DIAGNOSTIC_MAX_STRESS_CYCLES ||
            main_thread_load_ms < 0.0 || main_thread_load_ms > 10.0) {
            display_cadence_usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (stress_mode) {
            bool stress_ran = display_cadence_run_stress(source_revision, fixture_revision,
                stress_cycles, reinventory_interval, main_thread_load_ms);
            return stress_ran ? EXIT_SUCCESS : EXIT_FAILURE;
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
