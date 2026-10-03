#ifndef DECLARATIVE_SPACE_H
#define DECLARATIVE_SPACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "declarative_config.h"
#include "declarative_runtime.h"
#include "declarative_space_identity.h"
#include "view.h"

#include "misc/hashtable.h"

struct bucket;
struct table;
struct view;
struct window;
struct window_node;

enum declarative_space_ownership_mode
{
    DECLARATIVE_SPACE_PRESERVE_RUNTIME,
    DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD
};

struct declarative_space_defaults
{
    enum view_type layout;
    int top_padding;
    int bottom_padding;
    int left_padding;
    int right_padding;
    int window_gap;
    int accordion_padding;
    /* Captured global gap-enable state used by detached geometry calculations. */
    bool window_gap_enabled;
    enum window_node_split split_type;
    uint32_t auto_balance;
    /* Event-turn inputs used by detached BSP insertion; never read from live globals. */
    float split_ratio;
    enum window_node_child window_placement;
    enum window_insertion_point window_insertion_point;
    uint32_t focused_window_id;
    bool window_zoom_persist;
};

struct declarative_space_effective
{
    struct declarative_runtime_cell fields[DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT];
};

struct declarative_space_target
{
    struct view *view;
    uint64_t sid;
    int index;
    const char *uuid;
    bool is_user_space;
    struct declarative_space_identity identity;
};

/* Immutable Mission Control topology captured for the same event turn. */
struct declarative_space_topology_entry
{
    uint64_t sid;
    int index;
    const char *uuid;
    bool is_user_space;
    struct declarative_space_identity identity;
};

struct declarative_space_table_contract
{
    table_hash_func *hash;
    table_compare_func *cmp;
    size_t key_size;
};

struct declarative_space_inventory_entry
{
    uint32_t wid;
    uint64_t sid;
    struct window *window;
    bool eligible;
};

struct declarative_space_inventory_index_entry
{
    uint32_t wid;
    size_t position;
};

struct declarative_space_inventory
{
    struct declarative_space_inventory_entry *entries;
    size_t count;
    /* Sorted once after capture; entries remain in per-space capture order. */
    struct declarative_space_inventory_index_entry *wid_index;
};

struct declarative_space_capture_ops
{
    bool (*query_sid)(uint64_t sid, const uint32_t **wids, size_t *count, void *context);
    struct window *(*find_window)(uint32_t wid, void *context);
    bool (*is_eligible)(struct window *window, void *context);
    void *context;
};

struct declarative_space_root_before_image
{
    struct view *view;
    struct window_node *root;
    uint64_t fingerprint;
    size_t node_count;
    size_t window_count;
};

struct declarative_space_table_before_image
{
    struct bucket **buckets;
    int capacity;
    int count;
    float max_load;
    table_hash_func *hash;
    table_compare_func *cmp;
    uint64_t fingerprint;
};

struct declarative_space_view_before_image
{
    struct declarative_space_identity identity;
    enum view_type layout;
    int top_padding;
    int bottom_padding;
    int left_padding;
    int right_padding;
    int window_gap;
    int accordion_padding;
    enum window_node_split split_type;
    uint32_t auto_balance;
    uint32_t insertion_point;
    uint64_t flags;
};

struct declarative_space_ownership_image
{
    struct declarative_runtime_cell view[DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT];
    uint64_t legacy_flags;
};

struct declarative_space_tree_index_entry
{
    uint32_t wid;
    struct window_node *node;
    size_t membership_position;
    size_t order_position;
    size_t flatten_sequence;
    bool zoom_parent;
    bool zoom_root;
};

struct declarative_space_prepared_target
{
    struct declarative_space_target target;
    char *owned_uuid;
    struct declarative_space_effective effective;
    struct declarative_space_ownership_image ownership_before;
    struct declarative_space_ownership_image ownership_after;
    struct declarative_space_view_before_image view_before;
    struct declarative_space_root_before_image root_before;
    struct window_node *final_root; /* borrowed when unchanged, transition-owned when root_owned */
    uint64_t final_root_fingerprint;
    size_t final_node_count;
    uint32_t final_insertion_point;
    bool changed;
    bool layout_changed;
    bool membership_changed;
    bool root_owned;
    /* Preparation-owned edit index and stable flatten sequences. */
    struct declarative_space_tree_index_entry *tree_index;
    size_t tree_index_count;
    uint32_t *flatten_membership;
    uint32_t *flatten_order;
    size_t flatten_count;
    uint32_t *final_wids; /* capture-order final membership for this target */
    size_t final_count;
};

struct declarative_space_final_owner
{
    uint32_t wid;
    struct window *window; /* borrowed */
    struct view *view;     /* borrowed */
};

struct declarative_space_target_index_entry
{
    uint64_t sid;
    struct view *view;
    size_t position;
};

struct declarative_space_preparation
{
    enum declarative_space_ownership_mode ownership_mode;
    struct declarative_runtime_state *runtime; /* borrowed; guarded through publication */
    pthread_t preparing_thread;
#ifdef TESTS
    uint64_t admission_token;
#endif
    struct declarative_runtime_cell global_ownership_before[DECLARATIVE_CONFIG_COUNT];
    struct declarative_runtime_cell global_ownership_after[DECLARATIVE_CONFIG_COUNT];
    struct declarative_space_prepared_target *targets;
    size_t target_count;
    /* Checked SID-sorted target index reused by all preparation joins. */
    struct declarative_space_target_index_entry *target_index;
    struct declarative_space_inventory inventory;
    uint32_t *removal_wids;
    struct window **removal_windows; /* captured with removal_wids; may contain NULL */
    size_t removal_count;
    struct declarative_space_final_owner *final_owners;
    size_t final_owner_count;
    /* One preparation-owned allocation, partitioned into sorted old/final node indexes. */
    struct window_node **old_node_index;
    size_t old_node_index_count;
    struct window_node **final_node_index;
    size_t final_node_index_count;
    struct table replacement_managed_window;
    struct declarative_space_table_before_image table_before;
    bool prepared;
};

bool declarative_space_resolve_policy(const struct declarative_config_model *native,
                                      const struct declarative_runtime_state *runtime,
                                      const struct declarative_space_defaults *defaults,
                                      const struct declarative_space_target *target,
                                      enum declarative_space_ownership_mode mode,
                                      struct declarative_space_effective *out);

bool declarative_space_validate_targets(const struct declarative_space_target *targets,
                                        size_t target_count,
                                        const struct declarative_space_topology_entry *topology,
                                        size_t topology_count,
                                        const struct table *current_views,
                                        const struct declarative_space_table_contract *contract);
bool declarative_space_capture_inventory(const struct declarative_space_target *targets,
                                         size_t target_count,
                                         const struct declarative_space_capture_ops *ops,
                                         struct declarative_space_inventory *out);
void declarative_space_inventory_destroy(struct declarative_space_inventory *inventory);

bool declarative_space_validate_root(const struct view *view,
                                     struct declarative_space_root_before_image *out);
void declarative_space_capture_view_before(const struct view *view,
                                           struct declarative_space_view_before_image *out);
bool declarative_space_view_before_matches(const struct view *view,
                                            const struct declarative_space_view_before_image *image);
bool declarative_space_global_ownership_before_matches(const struct declarative_runtime_state *runtime,
                                                        const struct declarative_runtime_cell *image);
bool declarative_space_view_ownership_before_matches(const struct view *view,
                                                      const struct declarative_space_ownership_image *image);
bool declarative_space_validate_forest_and_table(const struct declarative_space_target *targets,
                                                 size_t target_count,
                                                 const struct table *managed_window,
                                                 const struct declarative_space_table_contract *contract,
                                                 struct declarative_space_root_before_image *roots,
                                                 struct declarative_space_table_before_image *table_image);

#ifdef TESTS
bool declarative_space_prepare(const struct declarative_space_target *targets,
                               size_t target_count,
                               const struct declarative_space_topology_entry *topology,
                               size_t topology_count,
                               const struct table *current_views,
                               const struct declarative_space_table_contract *view_table_contract,
                               const struct table *managed_window,
                               const struct declarative_space_table_contract *managed_table_contract,
                               const struct declarative_config_model *native,
                               const struct declarative_runtime_state *runtime,
                               const struct declarative_space_defaults *defaults,
                               enum declarative_space_ownership_mode mode,
                               const struct declarative_space_capture_ops *capture_ops,
                               struct declarative_space_preparation *out);
/* Test-only lifecycle seams; production exposes only declarative_space_apply(). */
void declarative_space_destroy_detached_root(struct window_node *root);
void declarative_space_preparation_destroy(struct declarative_space_preparation *preparation);
#endif

enum declarative_space_transition_state
{
    DECLARATIVE_SPACE_TRANSITION_EMPTY,
    DECLARATIVE_SPACE_TRANSITION_PREPARED,
    DECLARATIVE_SPACE_TRANSITION_COMMITTED,
    DECLARATIVE_SPACE_TRANSITION_POSTCOMMIT_ATTEMPTED,
    DECLARATIVE_SPACE_TRANSITION_RETIRED
};

struct declarative_space_postcommit_diagnostics
{
    size_t attempted;
    size_t failed;
    size_t layer_normal_failed;
    size_t layer_below_failed;
    size_t purify_failed;
    size_t update_failed;
    size_t flush_failed;
    size_t flush_deferred;
    size_t feedback_failed;
};

struct declarative_space_postcommit_ops
{
    bool (*layer_normal)(struct window *window, void *context);
    bool (*layer_below)(struct window *window, void *context);
    bool (*purify)(struct window *window, void *context);
    bool (*update)(struct view *view, void *context);
    bool (*flush)(struct view *view, void *context);
    bool (*dispose_feedback)(struct window_node *node, void *context);
    void *context;
};

typedef void (*declarative_space_publication_callback)(void *context);

struct declarative_space_transition
{
    struct declarative_space_preparation preparation;
    struct table retired_managed_window;
    bool retired_table_disposed;
    enum declarative_space_transition_state state;
    struct declarative_space_postcommit_diagnostics diagnostics;
};

/* The sole production entry point performs capture through retirement synchronously. */
bool declarative_space_apply_with_publication(const struct declarative_space_target *targets,
                                              size_t target_count,
                                              const struct declarative_space_topology_entry *topology,
                                              size_t topology_count,
                                              const struct table *current_views,
                                              const struct declarative_space_table_contract *view_table_contract,
                                              const struct declarative_space_table_contract *managed_table_contract,
                                              const struct declarative_config_model *native,
                                              struct declarative_runtime_state *runtime,
                                              const struct declarative_space_defaults *defaults,
                                              enum declarative_space_ownership_mode mode,
                                              const struct declarative_space_capture_ops *capture_ops,
                                              const struct declarative_space_postcommit_ops *postcommit_ops,
                                              struct declarative_space_postcommit_diagnostics *diagnostics,
                                              declarative_space_publication_callback publish,
                                              void *publish_context);
bool declarative_space_apply(const struct declarative_space_target *targets,
                             size_t target_count,
                             const struct declarative_space_topology_entry *topology,
                             size_t topology_count,
                             const struct table *current_views,
                             const struct declarative_space_table_contract *view_table_contract,
                             const struct declarative_space_table_contract *managed_table_contract,
                             const struct declarative_config_model *native,
                             struct declarative_runtime_state *runtime,
                             const struct declarative_space_defaults *defaults,
                             enum declarative_space_ownership_mode mode,
                             const struct declarative_space_capture_ops *capture_ops,
                             const struct declarative_space_postcommit_ops *postcommit_ops,
                             struct declarative_space_postcommit_diagnostics *diagnostics);
#ifdef TESTS
/* Low-level lifecycle is visible only to invariant and ordering tests. */
bool declarative_space_transition_begin(struct declarative_space_transition *transition,
                                        struct declarative_space_preparation *preparation);
bool declarative_space_apply_prepared(struct declarative_space_preparation *preparation,
                                      const struct declarative_space_postcommit_ops *ops,
                                      struct declarative_space_postcommit_diagnostics *diagnostics);
void declarative_space_transition_destroy(struct declarative_space_transition *transition);
bool declarative_space_transition_commit(struct declarative_space_transition *transition);
bool declarative_space_transition_postcommit(struct declarative_space_transition *transition,
                                              const struct declarative_space_postcommit_ops *ops);
bool declarative_space_dispose_retired_table(struct declarative_space_transition *transition);
bool declarative_space_retire_old_roots(struct declarative_space_transition *transition,
                                        const struct declarative_space_postcommit_ops *ops);
void declarative_space_test_set_admission(uint64_t token, bool allowed);
typedef void (*declarative_space_test_assertion_observer)(void *context);
void declarative_space_test_set_assertion_observer(declarative_space_test_assertion_observer observer,
                                                    void *context);
size_t declarative_space_test_assertion_count(void);
size_t declarative_space_test_commit_forbidden_calls(void);
void declarative_space_test_fail_allocation_at(size_t ordinal);
size_t declarative_space_test_allocation_count(void);
size_t declarative_space_test_outstanding_allocations(void);
void declarative_space_test_reset_index_operations(void);
size_t declarative_space_test_index_operations(void);
#endif

#endif
