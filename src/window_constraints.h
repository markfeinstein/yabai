#ifndef WINDOW_CONSTRAINTS_H
#define WINDOW_CONSTRAINTS_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stdint.h>

enum window_constraint_source
{
    WINDOW_CONSTRAINT_SOURCE_NONE,
    WINDOW_CONSTRAINT_SOURCE_ITERATOR,
    WINDOW_CONSTRAINT_SOURCE_PACKAGES
};

enum window_constraint_validity
{
    WINDOW_CONSTRAINT_MINIMUM_VALID    = 1u << 0,
    WINDOW_CONSTRAINT_MAXIMUM_VALID    = 1u << 1,
    WINDOW_CONSTRAINT_CURRENT_VALID    = 1u << 2,
    WINDOW_CONSTRAINT_LIMITS_ORDERED   = 1u << 3,
    WINDOW_CONSTRAINT_CURRENT_IN_RANGE = 1u << 4,
    WINDOW_CONSTRAINT_AX_SIZE_VALID    = 1u << 5,
    WINDOW_CONSTRAINT_DECORATION_DELTA = 1u << 6
};

struct window_constraint_observation
{
    CGSize minimum;
    CGSize maximum;
    CGSize current;
};

struct window_constraints
{
    CGSize minimum;
    CGSize maximum;
    CGSize observed_current;
    CGSize decoration_delta;
    uint64_t generation;
    uint64_t observed_at;
    uint8_t validity;
    uint8_t source;
};

void window_constraints_update(struct window_constraints *constraints,
                               const struct window_constraint_observation *observation,
                               enum window_constraint_source source,
                               const CGSize *ax_size,
                               uint64_t observed_at);
void window_constraints_invalidate(struct window_constraints *constraints);
bool window_constraints_has_limits(const struct window_constraints *constraints);
bool window_constraints_has_current(const struct window_constraints *constraints);
bool window_constraints_has_ax_corroboration(const struct window_constraints *constraints);

#endif
