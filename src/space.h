#ifndef SPACE_H
#define SPACE_H

struct window_server_snapshot;

uint32_t space_display_id(uint64_t sid);
uint32_t *space_window_list_for_connection_with_snapshot(uint64_t *space_list, int space_count, int cid, int *count,
                                                         bool include_minimized, bool *success,
                                                         struct window_server_snapshot **snapshot);
uint32_t *space_window_list_for_connection_with_status(uint64_t *space_list, int space_count, int cid, int *count, bool include_minimized, bool *success);
uint32_t *space_window_list_for_connection(uint64_t *space_list, int space_count, int cid, int *count, bool include_minimized);
uint32_t *space_window_list(uint64_t sid, int *count, bool include_minimized);
bool space_is_user(uint64_t sid);
bool space_is_system(uint64_t sid);
bool space_is_fullscreen(uint64_t sid);
bool space_is_visible(uint64_t sid);

#endif
