#ifndef MENU_BAR_MANAGER_H
#define MENU_BAR_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "menu_bar.h"

#define MENU_BAR_CONFIG_ENABLED      "menubar"
#define MENU_BAR_CONFIG_STYLE        "menubar_style"
#define MENU_BAR_CONFIG_SPACE_FILTER "menubar_space_filter"

#define MENU_BAR_STYLE_COMPACT_STR       "compact"
#define MENU_BAR_STYLE_EXPANDED_STR      "expanded"
#define MENU_BAR_STYLE_AUTOMATIC_STR     "automatic"
#define MENU_BAR_SPACE_FILTER_OCCUPIED_STR "occupied"
#define MENU_BAR_SPACE_FILTER_ALL_STR      "all"

enum menu_bar_tracking_transition
{
    MENU_BAR_TRACKING_TRANSITION_NONE = 0,
    MENU_BAR_TRACKING_TRANSITION_OPENED,
    MENU_BAR_TRACKING_TRANSITION_CLOSED
};

enum menu_bar_dirty_reason
{
    MENU_BAR_DIRTY_STARTUP        = 1u << 0,
    MENU_BAR_DIRTY_CONFIG         = 1u << 1,
    MENU_BAR_DIRTY_LABEL          = 1u << 2,
    MENU_BAR_DIRTY_FOCUS          = 1u << 3,
    MENU_BAR_DIRTY_SPACE          = 1u << 4,
    MENU_BAR_DIRTY_DISPLAY        = 1u << 5,
    MENU_BAR_DIRTY_WINDOW         = 1u << 6,
    MENU_BAR_DIRTY_TOPOLOGY       = 1u << 7,
    MENU_BAR_DIRTY_ACTION         = 1u << 8
};

bool menu_bar_manager_init(void);
void menu_bar_manager_shutdown(void);

struct menu_bar_settings menu_bar_manager_settings(void);

enum menu_bar_enabled menu_bar_manager_enabled(void);
enum menu_bar_style menu_bar_manager_style(void);
enum menu_bar_space_filter menu_bar_manager_space_filter(void);

const char *menu_bar_manager_enabled_string(enum menu_bar_enabled enabled);
const char *menu_bar_manager_style_string(enum menu_bar_style style);
const char *menu_bar_manager_space_filter_string(enum menu_bar_space_filter filter);

bool menu_bar_manager_parse_enabled(const char *text, size_t length, enum menu_bar_enabled *enabled);
bool menu_bar_manager_parse_style(const char *text, size_t length, enum menu_bar_style *style);
bool menu_bar_manager_parse_space_filter(const char *text, size_t length, enum menu_bar_space_filter *filter);

bool menu_bar_manager_set_enabled(enum menu_bar_enabled enabled);
bool menu_bar_manager_set_style(enum menu_bar_style style);
bool menu_bar_manager_set_space_filter(enum menu_bar_space_filter filter);

/* Startup-only staging changes settings without publishing or scheduling work. */
void menu_bar_manager_startup_staging_open(void);
bool menu_bar_manager_startup_staging_seal(void);
void menu_bar_manager_startup_staging_abort(void);
bool menu_bar_manager_startup_stage_enabled(enum menu_bar_enabled enabled);
bool menu_bar_manager_startup_stage_style(enum menu_bar_style style);
bool menu_bar_manager_startup_stage_space_filter(enum menu_bar_space_filter filter);
bool menu_bar_manager_startup_settings_can_commit(const struct menu_bar_settings *settings);
void menu_bar_manager_startup_settings_commit(const struct menu_bar_settings *settings);
void menu_bar_manager_settings_commit_no_post(const struct menu_bar_settings *settings);
void menu_bar_manager_settings_postcommit(const struct menu_bar_settings *previous);

void menu_bar_manager_mark_dirty(uint32_t reason);
void menu_bar_manager_refresh(void);
enum menu_bar_tracking_transition menu_bar_manager_handle_action(struct menu_bar_action *action);
void menu_bar_manager_release_action(struct menu_bar_action *action);

#endif
