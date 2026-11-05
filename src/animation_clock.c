#include "animation_clock.h"

#include <CoreVideo/CoreVideo.h>
#include <stdint.h>
#include <stdlib.h>

enum animation_clock_state
{
    ANIMATION_CLOCK_NEW,
    ANIMATION_CLOCK_LINK_CREATED,
    ANIMATION_CLOCK_STOP_REQUIRED,
    ANIMATION_CLOCK_QUIESCED
};

struct animation_clock
{
    animation_clock_tick_fn tick_fn;
    void *context;
    CVDisplayLinkRef cv_link;
    uint64_t origin_host_time;
    double host_clock_frequency;
    enum animation_clock_state state;
    bool origin_set;
};

#ifdef TESTS
static CVReturn (*animation_clock_cv_create)(CVDisplayLinkRef *) = CVDisplayLinkCreateWithActiveCGDisplays;
static CVReturn (*animation_clock_cv_set_callback)(CVDisplayLinkRef, CVDisplayLinkOutputCallback, void *) =
    CVDisplayLinkSetOutputCallback;
static CVReturn (*animation_clock_cv_start)(CVDisplayLinkRef) = CVDisplayLinkStart;
static CVReturn (*animation_clock_cv_stop)(CVDisplayLinkRef) = CVDisplayLinkStop;
static void (*animation_clock_cv_release)(CVDisplayLinkRef) = CVDisplayLinkRelease;
static double (*animation_clock_cv_frequency)(void) = CVGetHostClockFrequency;
static void *(*animation_clock_allocate)(size_t, size_t) = calloc;
static void (*animation_clock_deallocate)(void *) = free;
#else
#define animation_clock_cv_create CVDisplayLinkCreateWithActiveCGDisplays
#define animation_clock_cv_set_callback CVDisplayLinkSetOutputCallback
#define animation_clock_cv_start CVDisplayLinkStart
#define animation_clock_cv_stop CVDisplayLinkStop
#define animation_clock_cv_release CVDisplayLinkRelease
#define animation_clock_cv_frequency CVGetHostClockFrequency
#define animation_clock_allocate calloc
#define animation_clock_deallocate free
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static CVReturn animation_clock_cv_tick(CVDisplayLinkRef link, const CVTimeStamp *now,
                                        const CVTimeStamp *output_time, CVOptionFlags flags,
                                        CVOptionFlags *flags_out, void *context)
{
    (void) link;
    (void) flags;
    (void) flags_out;
    struct animation_clock *clock = context;
    if (!clock || !clock->tick_fn || !now || !output_time) return kCVReturnSuccess;
    if (!clock->origin_set) {
        clock->origin_host_time = now->hostTime;
        clock->origin_set = true;
    }
    struct animation_clock_tick tick = {
        .elapsed_ticks = (double) (output_time->hostTime - clock->origin_host_time),
        .ticks_per_second = clock->host_clock_frequency
    };
    clock->tick_fn(clock->context, &tick);
    return kCVReturnSuccess;
}

struct animation_clock *animation_clock_create(animation_clock_tick_fn tick_fn, void *context)
{
    if (!tick_fn) return NULL;
    struct animation_clock *clock = animation_clock_allocate(1, sizeof(*clock));
    if (!clock) return NULL;
    clock->tick_fn = tick_fn;
    clock->context = context;
    clock->host_clock_frequency = animation_clock_cv_frequency();
    clock->state = ANIMATION_CLOCK_NEW;
    return clock;
}

bool animation_clock_start(struct animation_clock *clock)
{
    if (!clock || clock->state != ANIMATION_CLOCK_NEW) return false;
    if (animation_clock_cv_create(&clock->cv_link) != kCVReturnSuccess || !clock->cv_link) return false;
    clock->state = ANIMATION_CLOCK_LINK_CREATED;
    if (animation_clock_cv_set_callback(clock->cv_link, animation_clock_cv_tick, clock) != kCVReturnSuccess) {
        return false;
    }
    clock->state = ANIMATION_CLOCK_STOP_REQUIRED;
    return animation_clock_cv_start(clock->cv_link) == kCVReturnSuccess;
}

enum animation_clock_stop_result animation_clock_stop(struct animation_clock *clock)
{
    if (!clock || clock->state == ANIMATION_CLOCK_NEW ||
        clock->state == ANIMATION_CLOCK_LINK_CREATED ||
        clock->state == ANIMATION_CLOCK_QUIESCED) {
        if (clock && clock->state == ANIMATION_CLOCK_LINK_CREATED) {
            clock->state = ANIMATION_CLOCK_QUIESCED;
        }
        return ANIMATION_CLOCK_STOP_QUIESCED;
    }

    CVReturn result = animation_clock_cv_stop(clock->cv_link);
    if (result == kCVReturnSuccess || result == kCVReturnDisplayLinkNotRunning) {
        clock->state = ANIMATION_CLOCK_QUIESCED;
        return ANIMATION_CLOCK_STOP_QUIESCED;
    }
    return ANIMATION_CLOCK_STOP_RETRY;
}

void animation_clock_destroy(struct animation_clock *clock)
{
    if (!clock || clock->state == ANIMATION_CLOCK_STOP_REQUIRED) return;
    if (clock->cv_link) animation_clock_cv_release(clock->cv_link);
    animation_clock_deallocate(clock);
}
#pragma clang diagnostic pop
