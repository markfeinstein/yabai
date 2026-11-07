#include "focus_follows_mouse.h"

#include <limits.h>

_Static_assert(UINT32_MAX == 0xffffffffU, "focus-follow suspension counters require uint32_t");

enum ffm_mode focus_follows_mouse_effective_mode(const struct focus_follows_mouse_state *state)
{
    if (!state) return FFM_DISABLED;
    return (enum ffm_mode) __atomic_load_n(&state->effective_mode, __ATOMIC_ACQUIRE);
}

void focus_follows_mouse_recompute(struct focus_follows_mouse_state *state)
{
    if (!state) return;
    enum ffm_mode mode = state->menu_suspend_count ? FFM_DISABLED : state->configured_mode;
    __atomic_store_n(&state->effective_mode, (ffm_atomic_mode) mode, __ATOMIC_RELEASE);
}

bool focus_follows_mouse_set_mode(struct focus_follows_mouse_state *state, enum ffm_mode mode)
{
    if (!state) return false;
    state->configured_mode = mode;
    focus_follows_mouse_recompute(state);
    return true;
}

void focus_follows_mouse_anonymous_menu_opened(struct focus_follows_mouse_state *state)
{
    if (!state || state->anonymous_menu_suspend_count == UINT32_MAX ||
        state->menu_suspend_count == UINT32_MAX) return;
    ++state->anonymous_menu_suspend_count;
    ++state->menu_suspend_count;
    focus_follows_mouse_recompute(state);
}

void focus_follows_mouse_anonymous_menu_closed(struct focus_follows_mouse_state *state)
{
    if (!state || state->anonymous_menu_suspend_count == 0) return;
    --state->anonymous_menu_suspend_count;
    if (state->menu_suspend_count != 0) --state->menu_suspend_count;
    focus_follows_mouse_recompute(state);
}

void focus_follows_mouse_application_menu_opened(struct focus_follows_mouse_state *state,
                                                  uint32_t *application_suspend_count)
{
    if (!state || !application_suspend_count || *application_suspend_count == UINT32_MAX ||
        state->menu_suspend_count == UINT32_MAX) return;
    ++*application_suspend_count;
    ++state->menu_suspend_count;
    focus_follows_mouse_recompute(state);
}

void focus_follows_mouse_application_menu_closed(struct focus_follows_mouse_state *state,
                                                  uint32_t *application_suspend_count)
{
    if (!state || !application_suspend_count || *application_suspend_count == 0) return;
    --*application_suspend_count;
    if (state->menu_suspend_count != 0) --state->menu_suspend_count;
    focus_follows_mouse_recompute(state);
}

void focus_follows_mouse_application_terminated(struct focus_follows_mouse_state *state,
                                                 uint32_t *application_suspend_count)
{
    if (!state || !application_suspend_count || *application_suspend_count == 0) return;
    uint32_t released = *application_suspend_count;
    *application_suspend_count = 0;
    state->menu_suspend_count = released >= state->menu_suspend_count
                              ? 0
                              : state->menu_suspend_count - released;
    focus_follows_mouse_recompute(state);
}
