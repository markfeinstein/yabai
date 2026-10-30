#ifndef WINDOW_SERVER_SNAPSHOT_H
#define WINDOW_SERVER_SNAPSHOT_H

#include <CoreFoundation/CoreFoundation.h>
#include <stddef.h>
#include <stdint.h>

#include "window_constraints.h"
#include "window_corner_geometry.h"

enum window_server_window_field
{
    WINDOW_SERVER_WINDOW_FIELD_ID         = 1u << 0,
    WINDOW_SERVER_WINDOW_FIELD_PARENT_ID  = 1u << 1,
    WINDOW_SERVER_WINDOW_FIELD_LEVEL      = 1u << 2,
    WINDOW_SERVER_WINDOW_FIELD_TAGS       = 1u << 3,
    WINDOW_SERVER_WINDOW_FIELD_ATTRIBUTES = 1u << 4,
    WINDOW_SERVER_WINDOW_FIELD_CONSTRAINTS = 1u << 5,
    WINDOW_SERVER_WINDOW_FIELD_CORNER_GEOMETRY = 1u << 6,
    WINDOW_SERVER_WINDOW_FIELD_BASE       = WINDOW_SERVER_WINDOW_FIELD_ID |
                                            WINDOW_SERVER_WINDOW_FIELD_PARENT_ID |
                                            WINDOW_SERVER_WINDOW_FIELD_LEVEL |
                                            WINDOW_SERVER_WINDOW_FIELD_TAGS |
                                            WINDOW_SERVER_WINDOW_FIELD_ATTRIBUTES
};

struct window_server_window_record
{
    uint32_t id;
    uint32_t parent_id;
    int level;
    uint64_t tags;
    uint64_t attributes;
    struct window_constraint_observation constraints;
    struct window_corner_geometry corner_geometry;
    uint64_t valid_fields;
};

struct window_server_snapshot
{
    uint64_t generation;
    const struct window_server_window_record *records;
    size_t count;
};

/* Creates an immutable, owned snapshot in iterator order. The returned snapshot
 * contains no borrowed WindowServer or CoreFoundation storage. */
struct window_server_snapshot *window_server_snapshot_create(int connection,
                                                              CFArrayRef window_ids,
                                                              int expected_count);
const struct window_server_window_record *window_server_snapshot_find(const struct window_server_snapshot *snapshot,
                                                                      uint32_t window_id);
void window_server_snapshot_destroy(struct window_server_snapshot *snapshot);

#endif
