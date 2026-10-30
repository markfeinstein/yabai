#include "window_constraints.h"

#include <math.h>
#include <string.h>
#include <time.h>

#define WINDOW_CONSTRAINT_RANGE_TOLERANCE 0.5

static uint64_t window_constraints_generation;

static bool window_constraint_size_is_valid(CGSize size)
{
    return isfinite(size.width) && isfinite(size.height) &&
           size.width > 0.0 && size.height > 0.0;
}

static uint64_t window_constraints_now(void)
{
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t) value.tv_sec * UINT64_C(1000000000) + (uint64_t) value.tv_nsec;
}

static uint64_t window_constraints_next_generation(void)
{
    uint64_t generation = __atomic_add_fetch(&window_constraints_generation, 1, __ATOMIC_RELAXED);
    return generation ? generation : __atomic_add_fetch(&window_constraints_generation, 1, __ATOMIC_RELAXED);
}

void window_constraints_update(struct window_constraints *constraints,
                               const struct window_constraint_observation *observation,
                               enum window_constraint_source source,
                               const CGSize *ax_size,
                               uint64_t observed_at)
{
    if (!constraints) return;
    struct window_constraints next = {
        .generation = window_constraints_next_generation(),
        .observed_at = observed_at ? observed_at : window_constraints_now(),
        .source = (uint8_t) source
    };
    if (observation) {
        next.minimum = observation->minimum;
        next.maximum = observation->maximum;
        next.observed_current = observation->current;
        if (window_constraint_size_is_valid(observation->minimum)) next.validity |= WINDOW_CONSTRAINT_MINIMUM_VALID;
        if (window_constraint_size_is_valid(observation->maximum)) next.validity |= WINDOW_CONSTRAINT_MAXIMUM_VALID;
        if (window_constraint_size_is_valid(observation->current)) next.validity |= WINDOW_CONSTRAINT_CURRENT_VALID;
        if ((next.validity & (WINDOW_CONSTRAINT_MINIMUM_VALID | WINDOW_CONSTRAINT_MAXIMUM_VALID)) ==
            (WINDOW_CONSTRAINT_MINIMUM_VALID | WINDOW_CONSTRAINT_MAXIMUM_VALID) &&
            observation->minimum.width <= observation->maximum.width &&
            observation->minimum.height <= observation->maximum.height) {
            next.validity |= WINDOW_CONSTRAINT_LIMITS_ORDERED;
            if ((next.validity & WINDOW_CONSTRAINT_CURRENT_VALID) &&
                observation->current.width >= observation->minimum.width - WINDOW_CONSTRAINT_RANGE_TOLERANCE &&
                observation->current.width <= observation->maximum.width + WINDOW_CONSTRAINT_RANGE_TOLERANCE &&
                observation->current.height >= observation->minimum.height - WINDOW_CONSTRAINT_RANGE_TOLERANCE &&
                observation->current.height <= observation->maximum.height + WINDOW_CONSTRAINT_RANGE_TOLERANCE) {
                next.validity |= WINDOW_CONSTRAINT_CURRENT_IN_RANGE;
            }
        }
        if (ax_size && window_constraint_size_is_valid(*ax_size)) {
            next.validity |= WINDOW_CONSTRAINT_AX_SIZE_VALID;
            if (next.validity & WINDOW_CONSTRAINT_CURRENT_VALID) {
                next.decoration_delta = CGSizeMake(ax_size->width - observation->current.width,
                                                   ax_size->height - observation->current.height);
                next.validity |= WINDOW_CONSTRAINT_DECORATION_DELTA;
            }
        }
    }
    *constraints = next;
}

void window_constraints_invalidate(struct window_constraints *constraints)
{
    if (!constraints) return;
    uint64_t generation = window_constraints_next_generation();
    memset(constraints, 0, sizeof(*constraints));
    constraints->generation = generation;
}

bool window_constraints_has_limits(const struct window_constraints *constraints)
{
    return constraints &&
           (constraints->validity & (WINDOW_CONSTRAINT_MINIMUM_VALID |
                                     WINDOW_CONSTRAINT_MAXIMUM_VALID |
                                     WINDOW_CONSTRAINT_LIMITS_ORDERED)) ==
           (WINDOW_CONSTRAINT_MINIMUM_VALID |
            WINDOW_CONSTRAINT_MAXIMUM_VALID |
            WINDOW_CONSTRAINT_LIMITS_ORDERED);
}

bool window_constraints_has_current(const struct window_constraints *constraints)
{
    return constraints && (constraints->validity & WINDOW_CONSTRAINT_CURRENT_VALID);
}

bool window_constraints_has_ax_corroboration(const struct window_constraints *constraints)
{
    return constraints &&
           (constraints->validity & (WINDOW_CONSTRAINT_AX_SIZE_VALID |
                                     WINDOW_CONSTRAINT_DECORATION_DELTA)) ==
           (WINDOW_CONSTRAINT_AX_SIZE_VALID | WINDOW_CONSTRAINT_DECORATION_DELTA);
}
