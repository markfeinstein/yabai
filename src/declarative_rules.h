#ifndef DECLARATIVE_RULES_H
#define DECLARATIVE_RULES_H

#include <CoreGraphics/CoreGraphics.h>
#include "rule.h"
#include "declarative_config.h"
#include "declarative_space_identity.h"

struct declarative_rule_display_entry
{
    uint32_t did;
    const char *uuid;
    CGPoint center;
    int managed_ordinal;
};

struct declarative_rule_space_entry
{
    uint64_t sid;
    const char *uuid;
    int kind;
    int mission_control_ordinal;
    uint32_t parent_did;
    const char *parent_display_uuid;
    int parent_display_ordinal;
    struct declarative_space_identity identity;
};

struct declarative_rule_topology_snapshot
{
    const struct declarative_rule_display_entry *displays;
    size_t display_count;
    const struct declarative_rule_space_entry *spaces;
    size_t space_count;
};

enum declarative_rule_prepare_result
{
    DECLARATIVE_RULE_PREPARE_OK,
    DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY,
    DECLARATIVE_RULE_PREPARE_MISSING_TARGET,
    DECLARATIVE_RULE_PREPARE_LABEL_COLLISION,
    DECLARATIVE_RULE_PREPARE_INVALID_REGEX,
    DECLARATIVE_RULE_PREPARE_ALLOCATION_FAILED,
    DECLARATIVE_RULE_PREPARE_INVALID_GENERATION
};

struct declarative_prepared_rules
{
    struct rule *templates;
    struct rule *live;
    bool *live_present;
    struct rule_warning_state *warning_states;
    size_t count;
};

bool declarative_rule_topology_validate(const struct declarative_rule_topology_snapshot *snapshot);
/*
 * Strictly validates snapshot and returns an opaque fold context containing an
 * immutable deep copy of its display and Space inventories, including UUIDs.
 * The caller may mutate or release snapshot and all referenced storage after a
 * successful return. The returned handle remains valid until explicitly
 * destroyed; creation failure returns NULL and retains no caller-owned data.
 */
struct rule_fold_context *declarative_rule_fold_context_create(const struct declarative_rule_topology_snapshot *snapshot);
void declarative_rule_fold_context_destroy(struct rule_fold_context *context);
enum declarative_rule_prepare_result declarative_prepare_rules(const struct declarative_rule_model *models, size_t count,
                                                                const struct declarative_rule_topology_snapshot *snapshot,
                                                                int display_order, uint64_t generation,
                                                                const char *const *runtime_labels, size_t runtime_label_count,
                                                                struct declarative_prepared_rules *prepared);
bool declarative_prepared_rule_rearm(struct declarative_prepared_rules *prepared, size_t index);
void declarative_prepared_rules_destroy(struct declarative_prepared_rules *prepared);
#ifdef TESTS
void declarative_rules_test_fail_allocation_at(size_t ordinal);
size_t declarative_rules_test_outstanding_allocations(void);
void declarative_rules_test_reset_allocator(void);
void declarative_rules_test_note_free(void *value);
void declarative_rules_test_reset_operation_counts(void);
size_t declarative_rules_test_topology_validation_count(void);
size_t declarative_rules_test_target_lookup_count(void);
#endif

#endif
