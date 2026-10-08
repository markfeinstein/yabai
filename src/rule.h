#ifndef RULE_H
#define RULE_H

struct application;

#include "declarative_space_identity.h"

#define RULE_PROP_UD  0
#define RULE_PROP_ON  1
#define RULE_PROP_OFF 2

enum rule_flag
{
    RULE_APP_VALID       = 0x001,
    RULE_TITLE_VALID     = 0x002,
    RULE_ROLE_VALID      = 0x004,
    RULE_SUBROLE_VALID   = 0x008,
    RULE_APP_EXCLUDE     = 0x010,
    RULE_TITLE_EXCLUDE   = 0x020,
    RULE_ROLE_EXCLUDE    = 0x040,
    RULE_SUBROLE_EXCLUDE = 0x080,
    RULE_ONE_SHOT        = 0x100,
    RULE_ONE_SHOT_REMOVE = 0x200
};

enum rule_effects_flag
{
    RULE_FOLLOW_SPACE = 0x01,
    RULE_OPACITY      = 0x02,
    RULE_LAYER        = 0x04
};

enum rule_owner
{
    RULE_OWNER_RUNTIME,
    RULE_OWNER_NATIVE
};

enum rule_add_result
{
    RULE_ADD_OK,
    RULE_ADD_PROTECTED_COLLISION
};

enum rule_remove_result
{
    RULE_REMOVE_OK,
    RULE_REMOVE_NOT_FOUND,
    RULE_REMOVE_PROTECTED_DENIED
};

enum rule_target_kind
{
    RULE_TARGET_DISPLAY,
    RULE_TARGET_SPACE
};

/*
 * Opaque, immutable topology inventory used while folding native rules.
 * Instances are created and destroyed by the declarative rule API.
 */
struct rule_fold_context;

bool rule_fold_context_target_is_valid(const struct rule_fold_context *context,
                                       enum rule_target_kind target, uint64_t id,
                                       const char *uuid, int kind,
                                       const struct declarative_space_identity *identity);

struct rule_warning_state
{
    uint64_t pending_generation;
    uint64_t emitted_generation;
};

enum rule_fold_result
{
    RULE_FOLD_OK,
    RULE_FOLD_WARNING_NEWLY_REQUIRED,
    RULE_FOLD_RESOURCE
};

struct rule_effects
{
    uint32_t did;
    uint64_t sid;
    float opacity;
    int manage;
    int sticky;
    int mff;
    int layer;
    int fullscreen;
    unsigned grid[6];
    char *scratchpad;
    uint16_t flags;
};

struct rule
{
    char *label;
    char *app;
    char *title;
    char *role;
    char *subrole;
    regex_t app_regex;
    regex_t title_regex;
    regex_t role_regex;
    regex_t subrole_regex;
    struct rule_effects effects;
    uint16_t flags;
    enum rule_owner owner;
    uint32_t source_rule_index;
    uint64_t generation;
    bool target_unavailable;
    char *display_uuid;
    char *space_uuid;
    int expected_space_kind;
    struct declarative_space_identity space_identity;
    struct rule_warning_state *warning_state;
    int one_shot_owner_pid;
    uint32_t one_shot_owner_psn_high;
    uint32_t one_shot_owner_psn_low;
    uint32_t one_shot_window_id;
};

static inline bool rule_check_flag(struct rule *r, enum rule_flag x) { return r->flags & x; }
static inline void rule_clear_flag(struct rule *r, enum rule_flag x) { r->flags &= ~x; }
static inline void rule_set_flag(struct rule *r, enum rule_flag x) { r->flags |= x; }

static inline bool rule_effects_check_flag(struct rule_effects *e, enum rule_effects_flag x) { return e->flags & x; }
static inline void rule_effects_clear_flag(struct rule_effects *e, enum rule_effects_flag x) { e->flags &= ~x; }
static inline void rule_effects_set_flag(struct rule_effects *e, enum rule_effects_flag x) { e->flags |= x; }

void rule_serialize(FILE *rsp, struct rule *rule, int index);
bool rule_combine_effects(struct rule_effects *rule_effects, struct rule_effects *result);
enum rule_fold_result rule_combine_rule_effects(struct rule *rule, struct rule_effects *result, const struct rule_fold_context *context);
bool rule_acknowledge_stale_warning(struct rule *rule);
void rule_report_fold_warning(struct rule *rule, enum rule_fold_result result);
void rule_reapply_all(void);
bool rule_reapply_by_index(int index, const struct rule_fold_context *context);
bool rule_reapply_by_label(char *label, const struct rule_fold_context *context);
void rule_apply(struct rule *rule, const struct rule_fold_context *context);

/* Generation state is prepared before publication and installed without allocation. */
void rule_native_generation_install(uint64_t generation, size_t native_prefix_length,
                                    const char *const *labels, size_t label_count,
                                    const struct rule_fold_context *context);
void rule_native_generation_clear(void);
uint64_t rule_native_generation(void);
size_t rule_native_prefix_length(void);
bool rule_native_label_is_protected(const char *label);
bool rule_reserve_one_shots_in_buffer(struct rule *rules, size_t count, struct application *application);
bool rule_reserve_one_shots(struct application *application);
bool rule_retry_pending_one_shots(void);
bool rule_one_shot_reassignment_is_pending(void);
bool rule_finalize_one_shots(struct application *application);
bool rule_cancel_one_shots(struct application *application);
void rule_cleanup_consumed_one_shots(struct application *application);
void rule_cleanup_one_shots(void);
#ifdef TESTS
void rule_test_set_reservation_match_result(int result);
void rule_test_fail_reservation_match_at(size_t ordinal, int result);
void rule_test_fail_reservation_allocation_at(size_t ordinal);
size_t rule_test_reservation_match_calls(void);
#endif
enum rule_add_result rule_add(struct rule *rule);
enum rule_remove_result rule_remove_by_index(int index);
enum rule_remove_result rule_remove_by_label(char *label);
void rule_destroy(struct rule *rule);

#endif
