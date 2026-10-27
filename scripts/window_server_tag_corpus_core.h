#ifndef WINDOW_SERVER_TAG_CORPUS_CORE_H
#define WINDOW_SERVER_TAG_CORPUS_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/window_server_snapshot.h"
#include "../src/window_server_classifier.h"

#define WINDOW_SERVER_TAG_CORPUS_SCHEMA "yabai.window-server-tag-corpus/v1"

struct window_server_tag_corpus_metadata
{
    const char *scenario;
    const char *os_version;
    const char *os_build;
    const char *architecture;
};

enum window_server_tag_corpus_title_state
{
    WINDOW_SERVER_TITLE_UNAVAILABLE,
    WINDOW_SERVER_TITLE_EMPTY,
    WINDOW_SERVER_TITLE_NONEMPTY
};

struct window_server_tag_corpus_label
{
    bool present;
    const char *fixture_role;
    enum ws_window_class expected_normal;
    enum ws_window_class expected_include_minimized;
};

struct window_server_tag_corpus_comparison_window
{
    enum ws_owner_kind owner;
    enum ws_ax_corroboration ax;
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
