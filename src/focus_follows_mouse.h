#ifndef FOCUS_FOLLOWS_MOUSE_H
#define FOCUS_FOLLOWS_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

enum ffm_mode
{
    FFM_DISABLED,
    FFM_AUTOFOCUS,
    FFM_AUTORAISE
};

typedef uint32_t ffm_atomic_mode;
_Static_assert(FFM_AUTORAISE <= UINT32_MAX, "ffm mode must fit its atomic storage");

struct focus_follows_mouse_state
{
    enum ffm_mode configured_mode;
    ffm_atomic_mode effective_mode;
    uint32_t menu_suspend_count;
    uint32_t anonymous_menu_suspend_count;
};

enum ffm_mode focus_follows_mouse_effective_mode(const struct focus_follows_mouse_state *state);
void focus_follows_mouse_recompute(struct focus_follows_mouse_state *state);
bool focus_follows_mouse_set_mode(struct focus_follows_mouse_state *state, enum ffm_mode mode);
void focus_follows_mouse_anonymous_menu_opened(struct focus_follows_mouse_state *state);
void focus_follows_mouse_anonymous_menu_closed(struct focus_follows_mouse_state *state);
void focus_follows_mouse_application_menu_opened(struct focus_follows_mouse_state *state,
                                                  uint32_t *application_suspend_count);
void focus_follows_mouse_application_menu_closed(struct focus_follows_mouse_state *state,
                                                  uint32_t *application_suspend_count);
void focus_follows_mouse_application_terminated(struct focus_follows_mouse_state *state,
                                                 uint32_t *application_suspend_count);

#endif
