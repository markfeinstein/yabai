#ifndef DECLARATIVE_TOPOLOGY_H
#define DECLARATIVE_TOPOLOGY_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <float.h>
#include <stddef.h>
#include <stdint.h>

#include "declarative_rules.h"
#include "declarative_space.h"
#include "view.h"

struct declarative_topology_display
{
    uint32_t did;
    char *uuid;
    CGRect bounds;
    int managed_ordinal;
};

struct declarative_topology_space
{
    uint64_t sid;
    char *uuid;
    int kind;
    int mission_control_ordinal;
    size_t parent_display_index;
};

struct declarative_topology
{
    struct declarative_topology_display *displays;
    size_t display_count;
    struct declarative_topology_space *spaces;
    size_t space_count;
    uint32_t active_did;
    uint64_t active_sid;
    uint64_t fingerprint;
};

struct declarative_topology_bounds_config
{
    int external_bar_mode;
    int external_bar_top_padding;
    int external_bar_bottom_padding;
};

struct declarative_topology_capture_ops
{
    bool (*copy_active_displays)(uint32_t **display_ids, size_t *count, void *context);
    void (*free_active_displays)(uint32_t *display_ids, void *context);
    bool (*active_pair)(uint32_t *did, uint64_t *sid, void *context);
    CFTypeRef (*copy_managed_displays)(void *context);
    CFTypeRef (*copy_managed_display_spaces)(void *context);
    uint32_t (*display_id)(CFStringRef uuid, void *context);
    CGRect (*display_bounds)(uint32_t did, void *context);
    CFStringRef (*copy_space_uuid)(uint64_t sid, void *context);
    bool (*space_kind)(uint64_t sid, int *kind, void *context);
    void *context;
};

struct declarative_detached_view_table
{
    struct table view_table;
    struct view **views;
    uint64_t *sids;
    size_t count;
};

bool declarative_topology_capture(const struct declarative_topology_capture_ops *ops,
                                  struct declarative_topology *out);
bool declarative_topology_capture_system(const struct declarative_topology_bounds_config *bounds_config,
                                         struct declarative_topology *out);
void declarative_topology_destroy(struct declarative_topology *topology);
bool declarative_topology_equal(const struct declarative_topology *a,
                                const struct declarative_topology *b);
bool declarative_topology_rule_snapshot(const struct declarative_topology *topology,
                                        struct declarative_rule_topology_snapshot *snapshot,
                                        struct declarative_rule_display_entry **owned_displays,
                                        struct declarative_rule_space_entry **owned_spaces);
bool declarative_topology_space_snapshot(const struct declarative_topology *topology,
                                         struct declarative_space_topology_entry **entries,
                                         size_t *count);
void declarative_topology_rule_snapshot_destroy(struct declarative_rule_display_entry *displays,
                                                struct declarative_rule_space_entry *spaces);
void declarative_topology_space_snapshot_destroy(struct declarative_space_topology_entry *entries);

bool declarative_detached_view_table_prepare(const struct declarative_topology *topology,
                                             const struct declarative_space_defaults *defaults,
                                             struct declarative_detached_view_table *out);
void declarative_detached_view_table_destroy(struct declarative_detached_view_table *table);
/* Transfers all view/table storage to the ordinary live Space-manager owner. */
void declarative_detached_view_table_adopt(struct declarative_detached_view_table *table,
                                           struct table *live_table);

#ifdef TESTS
void declarative_topology_test_fail_allocation_at(size_t ordinal);
size_t declarative_topology_test_allocation_count(void);
size_t declarative_topology_test_outstanding_allocations(void);
void declarative_topology_test_reset_allocator(void);
#endif

#endif
