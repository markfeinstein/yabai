#include "window_server_tag_corpus_core.h"

#include "../src/window_server_tags.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool corpus_json_string(FILE *stream, const char *value)
{
    if (!stream || !value || fputc('"', stream) == EOF) return false;
    for (const unsigned char *cursor = (const unsigned char *) value; *cursor; ++cursor) {
        switch (*cursor) {
        case '"': if (fputs("\\\"", stream) == EOF) return false; break;
        case '\\': if (fputs("\\\\", stream) == EOF) return false; break;
        case '\b': if (fputs("\\b", stream) == EOF) return false; break;
        case '\f': if (fputs("\\f", stream) == EOF) return false; break;
        case '\n': if (fputs("\\n", stream) == EOF) return false; break;
        case '\r': if (fputs("\\r", stream) == EOF) return false; break;
        case '\t': if (fputs("\\t", stream) == EOF) return false; break;
        default:
            if (*cursor < 0x20) {
                if (fprintf(stream, "\\u%04x", *cursor) < 0) return false;
            } else if (fputc(*cursor, stream) == EOF) {
                return false;
            }
        }
    }
    return fputc('"', stream) != EOF;
}

bool window_server_tag_corpus_scenario_is_valid(const char *scenario)
{
    if (!scenario || !scenario[0]) return false;
    for (const unsigned char *cursor = (const unsigned char *) scenario; *cursor; ++cursor) {
        if (!isalnum(*cursor) && *cursor != '.' && *cursor != '_' && *cursor != '-') return false;
    }
    return true;
}

static bool corpus_root_relevant(const struct window_server_window_record *record)
{
    return record->parent_id == 0 &&
           (record->level == 0 || record->level == 3 || record->level == 8);
}

static bool corpus_document_or_floating(const struct window_server_window_record *record)
{
    return (record->tags & WINDOW_SERVER_TAG_DOCUMENT) ||
           ((record->tags & WINDOW_SERVER_TAG_FLOATING) &&
            (record->tags & WINDOW_SERVER_TAG_MODAL));
}

static bool corpus_legacy_normal_untracked(const struct window_server_window_record *record)
{
    return corpus_root_relevant(record) &&
           ((record->attributes & UINT64_C(0x2)) ||
            (record->tags & WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING)) &&
           corpus_document_or_floating(record);
}

static bool corpus_legacy_include_minimized_untracked(const struct window_server_window_record *record)
{
    if (corpus_legacy_normal_untracked(record)) return true;
    return corpus_root_relevant(record) &&
           (record->attributes == 0 || record->attributes == 1) &&
           ((record->tags & WINDOW_SERVER_TAG_MINIATURIZED) ||
            (record->tags & (WINDOW_SERVER_TAG_FULLSCREEN_CAPABLE |
                             WINDOW_SERVER_TAG_FULLSCREEN_TILE_CAPABLE))) &&
           corpus_document_or_floating(record);
}

static const struct window_server_window_record *corpus_find_record(
    const struct window_server_snapshot *snapshot, uint32_t window_id)
{
    return window_server_snapshot_find(snapshot, window_id);
}

static int corpus_public_ordinal(const struct window_server_tag_corpus_public_window *windows,
                                 size_t count, uint32_t window_id)
{
    for (size_t i = 0; i < count; ++i) {
        if (windows[i].window_id == window_id) return (int) i;
    }
    return -1;
}

static size_t corpus_owner_ordinal(const struct window_server_tag_corpus_public_window *windows,
                                   size_t index)
{
    size_t ordinal = 0;
    for (size_t i = 0; i < index; ++i) {
        bool first = true;
        for (size_t j = 0; j < i; ++j) {
            if (windows[j].owner_pid == windows[i].owner_pid) { first = false; break; }
        }
        if (first) ++ordinal;
    }
    for (size_t i = 0; i < index; ++i) {
        if (windows[i].owner_pid != windows[index].owner_pid) continue;
        size_t existing = 0;
        for (size_t j = 0; j < i; ++j) {
            bool first = true;
            for (size_t k = 0; k < j; ++k) {
                if (windows[k].owner_pid == windows[j].owner_pid) { first = false; break; }
            }
            if (first) ++existing;
        }
        return existing;
    }
    return ordinal;
}

static bool corpus_validate(const struct window_server_tag_corpus_metadata *metadata,
                            const struct window_server_tag_corpus_public_window *windows,
                            size_t window_count,
                            const struct window_server_snapshot *snapshot)
{
    if (!metadata || !window_server_tag_corpus_scenario_is_valid(metadata->scenario) ||
        !metadata->os_version || !metadata->os_build || !metadata->architecture ||
        (!windows && window_count) || !snapshot || snapshot->count != window_count ||
        (window_count && !snapshot->records)) return false;
    for (size_t i = 0; i < window_count; ++i) {
        if (!windows[i].window_id || !corpus_find_record(snapshot, windows[i].window_id)) return false;
        for (size_t j = 0; j < i; ++j) {
            if (windows[j].window_id == windows[i].window_id) return false;
        }
    }
    return true;
}

bool window_server_tag_corpus_serialize(
    FILE *stream,
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t window_count,
    const struct window_server_snapshot *snapshot)
{
    if (!stream || !corpus_validate(metadata, windows, window_count, snapshot)) return false;

    size_t root_relevant = 0;
    size_t legacy_normal = 0;
    size_t legacy_minimized = 0;
    size_t document_or_floating = 0;
    size_t document_or_floating_rejected = 0;
    for (size_t i = 0; i < window_count; ++i) {
        const struct window_server_window_record *record =
            corpus_find_record(snapshot, windows[i].window_id);
        bool root = corpus_root_relevant(record);
        bool normal = corpus_legacy_normal_untracked(record);
        bool minimized = corpus_legacy_include_minimized_untracked(record);
        bool document = corpus_document_or_floating(record);
        root_relevant += root;
        legacy_normal += normal;
        legacy_minimized += minimized;
        document_or_floating += document;
        document_or_floating_rejected += document && !normal;
    }

    if (fputs("{\"schema\":\"" WINDOW_SERVER_TAG_CORPUS_SCHEMA
              "\",\"type\":\"capture\",\"scenario\":", stream) == EOF ||
        !corpus_json_string(stream, metadata->scenario) ||
        fputs(",\"os_version\":", stream) == EOF || !corpus_json_string(stream, metadata->os_version) ||
        fputs(",\"os_build\":", stream) == EOF || !corpus_json_string(stream, metadata->os_build) ||
        fputs(",\"architecture\":", stream) == EOF || !corpus_json_string(stream, metadata->architecture) ||
        fprintf(stream,
                ",\"tag_map\":\"modern-advisory-v1\","
                "\"tag_map_evidence\":\"mapped-26.3.1-sampled-27.0.1-unverified-27.2\","
                "\"redaction\":\"strict-v1\",\"counts\":{"
                "\"public_windows\":%zu,\"snapshot_records\":%zu,"
                "\"root_relevant\":%zu,\"legacy_normal_untracked\":%zu,"
                "\"legacy_include_minimized_untracked\":%zu,"
                "\"document_or_floating\":%zu,"
                "\"document_or_floating_rejected_by_legacy\":%zu}}\n",
                window_count, snapshot->count, root_relevant, legacy_normal,
                legacy_minimized, document_or_floating,
                document_or_floating_rejected) < 0) return false;

    for (size_t i = 0; i < window_count; ++i) {
        const struct window_server_window_record *record =
            corpus_find_record(snapshot, windows[i].window_id);
        int parent_ordinal = corpus_public_ordinal(windows, window_count, record->parent_id);
        const char *parent_kind = record->parent_id == 0 ? "root" :
                                  parent_ordinal >= 0 ? "captured" : "external";
        bool root = corpus_root_relevant(record);
        bool normal = corpus_legacy_normal_untracked(record);
        bool minimized = corpus_legacy_include_minimized_untracked(record);
        bool document = corpus_document_or_floating(record);
        if (fprintf(stream,
                    "{\"schema\":\"" WINDOW_SERVER_TAG_CORPUS_SCHEMA
                    "\",\"type\":\"window\",\"ordinal\":%zu,"
                    "\"owner_ordinal\":%zu,\"parent_kind\":\"%s\","
                    "\"parent_ordinal\":",
                    i, corpus_owner_ordinal(windows, i), parent_kind) < 0) return false;
        if (parent_ordinal >= 0) {
            if (fprintf(stream, "%d", parent_ordinal) < 0) return false;
        } else if (fputs("null", stream) == EOF) return false;
        if (fprintf(stream,
                    ",\"sls_level\":%d,\"cg_layer\":%d,\"on_screen\":%s,"
                    "\"tags_hex\":\"0x%016llx\",\"attributes_hex\":\"0x%016llx\","
                    "\"named_tags\":[",
                    record->level, windows[i].cg_layer, windows[i].on_screen ? "true" : "false",
                    (unsigned long long) record->tags,
                    (unsigned long long) record->attributes) < 0) return false;
        bool emitted = false;
        for (size_t tag = 0; tag < window_server_tag_descriptor_count; ++tag) {
            if (!(record->tags & window_server_tag_descriptors[tag].mask)) continue;
            if (emitted && fputc(',', stream) == EOF) return false;
            if (!corpus_json_string(stream, window_server_tag_descriptors[tag].name)) return false;
            emitted = true;
        }
        if (fprintf(stream,
                    "],\"unknown_tags_hex\":\"0x%016llx\","
                    "\"root_relevant\":%s,\"legacy_normal_untracked\":%s,"
                    "\"legacy_include_minimized_untracked\":%s,"
                    "\"document_or_floating\":%s}\n",
                    (unsigned long long) (record->tags & ~WINDOW_SERVER_TAG_KNOWN_MASK),
                    root ? "true" : "false", normal ? "true" : "false",
                    minimized ? "true" : "false", document ? "true" : "false") < 0) return false;
    }
    return fflush(stream) == 0 && !ferror(stream);
}

bool window_server_tag_corpus_write_file(
    const char *path,
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t window_count,
    const struct window_server_snapshot *snapshot)
{
    if (!path || !path[0]) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    FILE *stream = fdopen(fd, "w");
    if (!stream) {
        close(fd);
        unlink(path);
        return false;
    }
    bool success = window_server_tag_corpus_serialize(
        stream, metadata, windows, window_count, snapshot);
    if (fclose(stream) != 0) success = false;
    if (!success) unlink(path);
    return success;
}
