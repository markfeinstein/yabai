#ifndef WINDOW_SERVER_TAG_CORPUS_CORE_H
#define WINDOW_SERVER_TAG_CORPUS_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/window_server_snapshot.h"
#include "../src/window_server_classifier.h"

#define WINDOW_SERVER_TAG_CORPUS_SCHEMA "yabai.window-server-tag-corpus/v2"
#define WINDOW_SERVER_CLASSIFIER_COMPARISON_SCHEMA "yabai.window-server-classifier-comparison/v2"

#define WINDOW_SERVER_TAG_CORPUS_AX_MODE_LIST(X) \
    X(WINDOW_SERVER_TAG_CORPUS_AX_AUTO, "auto") \
    X(WINDOW_SERVER_TAG_CORPUS_AX_UNAVAILABLE, "unavailable")

enum window_server_tag_corpus_ax_mode
{
#define WINDOW_SERVER_TAG_CORPUS_AX_ENUM(value, name) value,
    WINDOW_SERVER_TAG_CORPUS_AX_MODE_LIST(WINDOW_SERVER_TAG_CORPUS_AX_ENUM)
#undef WINDOW_SERVER_TAG_CORPUS_AX_ENUM
};

struct window_server_tag_corpus_metadata
{
    const char *scenario;
    const char *capture_set_id;
    unsigned repeat_index;
    const char *os_version;
    const char *os_build;
    const char *architecture;
    const char *source_revision;
    enum window_server_tag_corpus_ax_mode ax_mode;
    unsigned display_count;
    const char *separate_spaces;
    const char *stage_manager;
};

enum window_server_tag_corpus_title_state
{
    WINDOW_SERVER_TITLE_UNAVAILABLE,
    WINDOW_SERVER_TITLE_EMPTY,
    WINDOW_SERVER_TITLE_NONEMPTY
};

enum window_server_tag_corpus_ax_observation
{
    WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_UNAVAILABLE,
    WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_NO_MATCH,
    WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_INVENTORY_ERROR,
    WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_ROLE_ERROR,
    WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_MATCHED
};

enum window_server_tag_corpus_adjudication
{
    WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_PENDING,
    WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_CONFIRMED,
    WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_REJECTED
};

enum window_server_tag_corpus_bit58_state
{
    WINDOW_SERVER_TAG_CORPUS_BIT58_NOT_APPLICABLE,
    WINDOW_SERVER_TAG_CORPUS_BIT58_CLEAR,
    WINDOW_SERVER_TAG_CORPUS_BIT58_SET
};

struct window_server_tag_corpus_label
{
    bool present;
    const char *fixture_id;
    const char *fixture_role;
    enum ws_window_class expected_normal;
    enum ws_window_class expected_include_minimized;
    enum window_server_tag_corpus_adjudication adjudication;
    const char *adjudication_reason;
    const char *evidence_ref;
    const char *control_pair_id;
    enum window_server_tag_corpus_bit58_state bit58_state;
};

struct window_server_tag_corpus_comparison_window
{
    enum ws_owner_kind owner;
    enum ws_ax_corroboration ax;
    enum window_server_tag_corpus_ax_observation ax_observation;
    enum window_server_tag_corpus_title_state title;
    struct window_server_tag_corpus_label label;
};

struct window_server_tag_corpus_comparison
{
    enum ws_tag_layout_status tag_layout;
    int normal_level;
    int floating_level;
    int modal_level;
    const struct window_server_tag_corpus_comparison_window *windows;
    size_t window_count;
};

struct window_server_tag_corpus_public_window
{
    uint32_t window_id;
    int owner_pid;
    int cg_layer;
    bool on_screen;

    /* Sensitive fixture/live metadata deliberately ignored by strict-v1. */
    const char *title;
    const char *owner_name;
    const char *bundle_id;
    const char *executable_path;
    const char *display_uuid;
    uint64_t space_id;
    double x, y, width, height;
};

bool window_server_tag_corpus_scenario_is_valid(const char *scenario);
bool window_server_tag_corpus_serialize(
    FILE *stream,
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t window_count,
    const struct window_server_snapshot *snapshot);
bool window_server_tag_corpus_serialize_comparison(
    FILE *stream,
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t window_count,
    const struct window_server_snapshot *snapshot,
    const struct window_server_tag_corpus_comparison *comparison);
bool window_server_tag_corpus_write_comparison_file(
    const char *path,
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t window_count,
    const struct window_server_snapshot *snapshot,
    const struct window_server_tag_corpus_comparison *comparison);
bool window_server_tag_corpus_write_file(
    const char *path,
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t window_count,
    const struct window_server_snapshot *snapshot);

#endif
