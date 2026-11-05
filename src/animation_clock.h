#ifndef ANIMATION_CLOCK_H
#define ANIMATION_CLOCK_H

#include <stdbool.h>

struct animation_clock;

struct animation_clock_tick
{
    double elapsed_ticks;
    double ticks_per_second;
};

typedef void (*animation_clock_tick_fn)(void *context,
                                        const struct animation_clock_tick *tick);

enum animation_clock_stop_result
{
    ANIMATION_CLOCK_STOP_QUIESCED,
    ANIMATION_CLOCK_STOP_RETRY
};

struct animation_clock *animation_clock_create(animation_clock_tick_fn tick_fn, void *context);
bool animation_clock_start(struct animation_clock *clock);
enum animation_clock_stop_result animation_clock_stop(struct animation_clock *clock);
void animation_clock_destroy(struct animation_clock *clock);

#endif
