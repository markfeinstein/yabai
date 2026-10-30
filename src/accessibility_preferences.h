#ifndef ACCESSIBILITY_PREFERENCES_H
#define ACCESSIBILITY_PREFERENCES_H

#include <stdbool.h>

struct accessibility_display_preferences
{
    bool reduce_motion;
    bool reduce_transparency;
    bool differentiate_without_color;
};

/* Samples the current public NSWorkspace accessibility display preferences and
 * begins observing live changes. A false return means the initial snapshot is
 * still available, but live observation could not be installed. */
bool accessibility_preferences_begin(void);
void accessibility_preferences_end(void);
struct accessibility_display_preferences accessibility_preferences_snapshot(void);

#endif
