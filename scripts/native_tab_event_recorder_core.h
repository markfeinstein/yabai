#ifndef NATIVE_TAB_EVENT_RECORDER_CORE_H
#define NATIVE_TAB_EVENT_RECORDER_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define NATIVE_TAB_SETTLE_DELAY_NS UINT64_C(100000000)
#define NATIVE_TAB_HARD_LIFETIME_NS UINT64_C(1000000000)
#define NATIVE_TAB_ACTION_SEPARATION_NS UINT64_C(600000000)
#define NATIVE_TAB_EPISODE_CAPACITY 8
#define NATIVE_TAB_PAIR_CAPACITY (NATIVE_TAB_EPISODE_CAPACITY * NATIVE_TAB_EPISODE_CAPACITY)
#define NATIVE_TAB_INVENTORY_CAPACITY 256
#define NATIVE_TAB_SCENARIO_CAPACITY 64

enum native_tab_fact_validity
{
    NATIVE_TAB_FACT_PARENT   = 1u << 0,
    NATIVE_TAB_FACT_SPACE    = 1u << 1,
    NATIVE_TAB_FACT_FRAME    = 1u << 2,
    NATIVE_TAB_FACT_LEVEL    = 1u << 3,
    NATIVE_TAB_FACT_SUBLEVEL = 1u << 4,
    NATIVE_TAB_FACT_ORDER    = 1u << 5,
    NATIVE_TAB_FACT_TITLE    = 1u << 6,
    NATIVE_TAB_FACT_ROLE     = 1u << 7,
    NATIVE_TAB_FACT_SUBROLE  = 1u << 8,
    NATIVE_TAB_FACT_AX       = 1u << 9,
    NATIVE_TAB_FACT_STATE    = 1u << 10
};

struct native_tab_window_fact
{
    uint32_t window_id;
    uint32_t parent_window_id;
    uint64_t space_id;
    uint64_t title_token;
    double x;
    double y;
    double width;
    double height;
    int level;
    int sublevel;
    int order;
    uint32_t role_token;
    uint32_t subrole_token;
    bool ax_member;
    bool minimized;
    bool fullscreen;
    bool focused;
    uint32_t validity;
};

struct native_tab_inventory
{
    struct native_tab_window_fact windows[NATIVE_TAB_INVENTORY_CAPACITY];
    size_t count;
    bool complete;
};

enum native_tab_event_type
{
    NATIVE_TAB_EVENT_SLS_VISIBLE,
    NATIVE_TAB_EVENT_SLS_INVISIBLE,
    NATIVE_TAB_EVENT_SLS_DESTROYED,
    NATIVE_TAB_EVENT_AX_CREATED,
    NATIVE_TAB_EVENT_AX_DESTROYED,
    NATIVE_TAB_EVENT_AX_MINIMIZED,
    NATIVE_TAB_EVENT_AX_DEMINIMIZED,
    NATIVE_TAB_EVENT_AX_FOCUSED,
    NATIVE_TAB_EVENT_AX_FOCUS_STABLE,
    NATIVE_TAB_EVENT_CONTROL_CLOSE,
    NATIVE_TAB_EVENT_CONTROL_MINIMIZE,
    NATIVE_TAB_EVENT_CONTROL_RESTORE,
    NATIVE_TAB_EVENT_CONTROL_HIDE,
    NATIVE_TAB_EVENT_CONTROL_UNHIDE,
    NATIVE_TAB_EVENT_CONTROL_SPACE_CHANGE,
    NATIVE_TAB_EVENT_APP_HIDDEN,
    NATIVE_TAB_EVENT_APP_VISIBLE,
    NATIVE_TAB_EVENT_APP_TERMINATED,
    NATIVE_TAB_EVENT_SPACE_CHANGED,
    NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER
};

struct native_tab_event
{
    enum native_tab_event_type type;
    uint64_t time_ns;
    uint32_t window_id;
    bool ordered_in_known;
    bool ordered_in;
    bool has_fact;
    struct native_tab_window_fact fact;
    uint32_t sls_code;
    uint32_t payload_length;
    uint32_t delivered_index;
    uint32_t subscription_generation;
    bool payload_valid;
    bool duplicate;
    bool control_correlated;
    bool subscribed_before_known;
    bool subscribed_before;
    bool target_pid_known;
    bool target_pid;
    bool focus_subject_known;
    uint32_t focus_subject_window_id;
    bool app_focus_known;
    uint32_t app_focus_window_id;
    bool main_window_known;
    uint32_t main_window_id;
};

enum native_tab_episode_state
{
    NATIVE_TAB_EPISODE_IDLE,
    NATIVE_TAB_EPISODE_COLLECTING
};

enum native_tab_evidence
{
    NATIVE_TAB_EVIDENCE_SLS_815      = 1u << 0,
    NATIVE_TAB_EVIDENCE_SLS_816      = 1u << 1,
    NATIVE_TAB_EVIDENCE_SLS_804      = 1u << 2,
    NATIVE_TAB_EVIDENCE_AX_CREATED   = 1u << 3,
    NATIVE_TAB_EVIDENCE_AX_DESTROYED = 1u << 4,
    NATIVE_TAB_EVIDENCE_AX_MINIMIZED = 1u << 5,
    NATIVE_TAB_EVIDENCE_INVENTORY    = 1u << 6,
    NATIVE_TAB_EVIDENCE_KNOWN_REAPPEAR = 1u << 7
};

enum native_tab_lifecycle_exclusion
{
    NATIVE_TAB_EXCLUSION_SLS_DESTROYED   = 1u << 0,
    NATIVE_TAB_EXCLUSION_AX_DESTROYED    = 1u << 1,
    NATIVE_TAB_EXCLUSION_MINIMIZE        = 1u << 2,
    NATIVE_TAB_EXCLUSION_DEMINIMIZE      = 1u << 3,
    NATIVE_TAB_EXCLUSION_APP_HIDE        = 1u << 4,
    NATIVE_TAB_EXCLUSION_APP_UNHIDE      = 1u << 5,
    NATIVE_TAB_EXCLUSION_APP_TERMINATE   = 1u << 6,
    NATIVE_TAB_EXCLUSION_SPACE_CHANGE    = 1u << 7,
    NATIVE_TAB_EXCLUSION_MISSION_CONTROL = 1u << 8,
    NATIVE_TAB_EXCLUSION_CONTROL_MARKER   = 1u << 9
};

struct native_tab_delivery_counts
{
    uint32_t delivered;
    uint32_t payload_invalid;
    uint32_t readback_confirmed;
    uint32_t readback_conflict;
    uint32_t readback_unknown;
    uint32_t duplicate;
};

struct native_tab_episode
{
    enum native_tab_episode_state state;
    uint64_t first_event_ns;
    uint64_t last_event_ns;
    uint64_t settle_deadline_ns;
    uint64_t hard_deadline_ns;
    uint32_t disappeared[NATIVE_TAB_EPISODE_CAPACITY];
    uint32_t appeared[NATIVE_TAB_EPISODE_CAPACITY];
    uint32_t outside_candidates[NATIVE_TAB_EPISODE_CAPACITY];
    uint8_t disappeared_count;
    uint8_t appeared_count;
    uint8_t outside_candidate_count;
    uint32_t evidence;
    uint8_t stable_inventory_polls;
    bool inventory_complete;
    bool overflowed;
    bool readback_conflict;
    bool unresolved_outside;
    bool action_separation_violated;
    bool pre_quiet_valid;
    bool inter_action_valid;
    uint64_t pre_quiet_ns;
    uint64_t post_quiet_ns;
    uint64_t inter_action_gap_ns;
    uint64_t action_start_ns;
    uint64_t action_end_ns;
    uint32_t lifecycle_exclusions;
    uint32_t episode_id;
    uint64_t first_confirmed_815_sequence;
    uint64_t first_confirmed_816_sequence;
    uint64_t first_confirmed_815_ns;
    uint64_t first_confirmed_816_ns;
    struct native_tab_delivery_counts sls_815;
    struct native_tab_delivery_counts sls_816;
};

struct native_tab_ordinal_entry
{
    uint32_t window_id;
    uint16_t ordinal;
    struct native_tab_window_fact fact;
    uint32_t appearance_count;
    bool has_fact;
    bool ever_observed;
    bool currently_present;
};

struct native_tab_recorder_metadata
{
    const char *os_version;
    const char *os_build;
    const char *architecture;
    const char *fixture_token;
    const char *batch_id;
    const char *direction;
    const char *expectation;
    unsigned planned_repeat;
    bool ax_available;
    bool control_events_available;
};

struct native_tab_recorder
{
    FILE *stream;
    char scenario[NATIVE_TAB_SCENARIO_CAPACITY];
    char fixture_token[NATIVE_TAB_SCENARIO_CAPACITY];
    char batch_id[NATIVE_TAB_SCENARIO_CAPACITY];
    char direction[NATIVE_TAB_SCENARIO_CAPACITY];
    char expectation[NATIVE_TAB_SCENARIO_CAPACITY];
    unsigned planned_repeat;
    uint64_t start_ns;
    uint64_t sequence;
    struct native_tab_inventory known;
    struct native_tab_episode episode;
    struct native_tab_ordinal_entry ordinals[NATIVE_TAB_INVENTORY_CAPACITY];
    size_t ordinal_count;
    uint32_t resolution_count;
    uint32_t gate_eligible_count;
    uint32_t incomplete_count;
    uint32_t dropped_count;
    uint32_t control_marker_count;
    uint32_t control_close_count;
    uint32_t control_minimize_count;
    uint32_t control_restore_count;
    uint32_t control_hide_count;
    uint32_t control_unhide_count;
    uint32_t control_space_change_count;
    uint32_t next_episode_id;
    enum native_tab_event_type last_control_event;
    uint64_t last_control_event_ns;
    uint64_t last_inventory_change_ns;
    uint64_t last_action_end_ns;
    struct native_tab_delivery_counts session_815;
    struct native_tab_delivery_counts session_816;
    bool initialized;
    bool stopped;
};

bool native_tab_scenario_is_valid(const char *scenario);
bool native_tab_trace_open(const char *path, FILE **stream);
bool native_tab_trace_close(FILE *stream, const char *path, bool success);

bool native_tab_recorder_init(struct native_tab_recorder *recorder, FILE *stream,
                              const char *scenario, uint64_t start_ns,
                              const struct native_tab_recorder_metadata *metadata);
bool native_tab_recorder_baseline(struct native_tab_recorder *recorder, uint64_t now_ns,
                                  const struct native_tab_inventory *inventory);
bool native_tab_recorder_event(struct native_tab_recorder *recorder,
                               const struct native_tab_event *event);
bool native_tab_recorder_inventory(struct native_tab_recorder *recorder, uint64_t now_ns,
                                   const struct native_tab_inventory *inventory);
bool native_tab_recorder_tick(struct native_tab_recorder *recorder, uint64_t now_ns);
bool native_tab_recorder_stop(struct native_tab_recorder *recorder, uint64_t now_ns);

bool native_tab_recorder_window_known(const struct native_tab_recorder *recorder,
                                      uint32_t window_id);
bool native_tab_recorder_settle_due(const struct native_tab_recorder *recorder,
                                    uint64_t now_ns);
bool native_tab_recorder_hard_due(const struct native_tab_recorder *recorder,
                                  uint64_t now_ns);

#endif
