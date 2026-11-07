#ifndef WINDOW_ANIMATION_H
#define WINDOW_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>

struct window_capture;

void window_manager_animate_window_list(struct window_capture *window_list, int window_count);
void window_manager_animate_window(struct window_capture capture);

#endif
