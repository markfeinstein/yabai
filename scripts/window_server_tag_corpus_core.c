#include "window_server_tag_corpus_core.h"

#include "../src/window_server_tags.h"
#include "../src/window_server_classifier.h"

#include <ctype.h>
#include <fcntl.h>
#include <math.h>
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

static bool corpus_legacy_eligible(const struct window_server_window_record *record,
                                   bool include_minimized)
{
    return ws_legacy_window_is_eligible(&(struct ws_legacy_eligibility_input) {
        .record = record,
        .tracked = false,
        .tracked_minimized = false,
        .include_minimized = include_minimized
    });
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
        bool normal = corpus_legacy_eligible(record, false);
        bool minimized = corpus_legacy_eligible(record, true);
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
        bool normal = corpus_legacy_eligible(record, false);
        bool minimized = corpus_legacy_eligible(record, true);
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

enum corpus_comparison_category
{
    CORPUS_SAME_INCLUDE,
    CORPUS_SAME_EXCLUDE,
    CORPUS_CANDIDATE_ONLY,
    CORPUS_LEGACY_ONLY,
    CORPUS_COMPARISON_CATEGORY_COUNT
};

static enum corpus_comparison_category corpus_comparison(bool legacy,
                                                          enum ws_window_class candidate)
{
    bool candidate_include = candidate == WS_WINDOW_CLASS_MANAGEABLE;
    if (legacy && candidate_include) return CORPUS_SAME_INCLUDE;
    if (!legacy && !candidate_include) return CORPUS_SAME_EXCLUDE;
    return candidate_include ? CORPUS_CANDIDATE_ONLY : CORPUS_LEGACY_ONLY;
}

static const char *corpus_comparison_name(enum corpus_comparison_category category)
{
    switch (category) {
    case CORPUS_SAME_INCLUDE: return "same-include";
    case CORPUS_SAME_EXCLUDE: return "same-exclude";
    case CORPUS_CANDIDATE_ONLY: return "candidate-only";
    case CORPUS_LEGACY_ONLY: return "legacy-only";
    case CORPUS_COMPARISON_CATEGORY_COUNT: break;
    }
    return "same-exclude";
}

static const char *corpus_title_state_name(enum window_server_tag_corpus_title_state state)
{
    switch (state) {
    case WINDOW_SERVER_TITLE_UNAVAILABLE: return "unavailable";
    case WINDOW_SERVER_TITLE_EMPTY: return "empty";
    case WINDOW_SERVER_TITLE_NONEMPTY: return "nonempty";
    }
    return "unavailable";
}

static const char *corpus_frame_state(const struct window_server_tag_corpus_public_window *window)
{
    if (!isfinite(window->x) || !isfinite(window->y) || !isfinite(window->width) ||
        !isfinite(window->height)) return "invalid";
    return window->width > 0.0 && window->height > 0.0 ? "positive" : "zero";
}

static bool corpus_write_reasons(FILE *stream, uint64_t reasons)
{
    bool emitted = false;
    if (fputc('[', stream) == EOF) return false;
    for (size_t i = 0; i < ws_window_classification_reason_descriptor_count; ++i) {
        if (!(reasons & ws_window_classification_reason_descriptors[i].mask)) continue;
        if (emitted && fputc(',', stream) == EOF) return false;
        if (!corpus_json_string(stream, ws_window_classification_reason_descriptors[i].name)) return false;
        emitted = true;
    }
    return fputc(']', stream) != EOF;
}

static bool corpus_comparison_validate(
    const struct window_server_tag_corpus_comparison *comparison,
    size_t window_count)
{
    if (!comparison || comparison->window_count != window_count ||
        (!comparison->windows && window_count)) return false;
    for (size_t i = 0; i < window_count; ++i) {
        const struct window_server_tag_corpus_label *label = &comparison->windows[i].label;
        if (!label->present) continue;
        if (!window_server_tag_corpus_scenario_is_valid(label->fixture_role) ||
            label->expected_normal < WS_WINDOW_CLASS_MANAGEABLE ||
            label->expected_normal > WS_WINDOW_CLASS_UNCERTAIN ||
            label->expected_include_minimized < WS_WINDOW_CLASS_MANAGEABLE ||
            label->expected_include_minimized > WS_WINDOW_CLASS_UNCERTAIN) return false;
    }
    return true;
}

bool window_server_tag_corpus_serialize_comparison(
    FILE *stream,
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t window_count,
    const struct window_server_snapshot *snapshot,
    const struct window_server_tag_corpus_comparison *comparison)
{
    if (!stream || !corpus_validate(metadata, windows, window_count, snapshot) ||
        !corpus_comparison_validate(comparison, window_count)) return false;

    size_t class_counts[2][4] = {{0}};
    size_t comparison_counts[2][CORPUS_COMPARISON_CATEGORY_COUNT] = {{0}};
    size_t labeled_count = 0, unlabeled_disagreement = 0;
    size_t false_positive = 0, false_negative = 0;
    size_t ax_matched = 0, ax_unavailable = 0, ax_error = 0;
    struct ws_window_classification classifications[2];

    for (size_t i = 0; i < window_count; ++i) {
        const struct window_server_window_record *record =
            corpus_find_record(snapshot, windows[i].window_id);
        enum ws_ax_corroboration ax = comparison->windows[i].ax;
        if (ax == WS_AX_STANDARD_WINDOW || ax == WS_AX_FLOATING_WINDOW ||
            ax == WS_AX_DIALOG_WINDOW) ++ax_matched;
        else if (ax == WS_AX_UNAVAILABLE) ++ax_unavailable;
        else ++ax_error;
        bool disagreement = false;
        for (int mode = 0; mode < 2; ++mode) {
            struct ws_window_classification_input input = {
                .valid_fields = record->valid_fields,
                .tags = record->tags,
                .attributes = record->attributes,
                .parent_id = record->parent_id,
                .pid = windows[i].owner_pid,
                .level = record->level,
                .normal_level = comparison->normal_level,
                .floating_level = comparison->floating_level,
                .modal_level = comparison->modal_level,
                .frame = CGRectMake(windows[i].x, windows[i].y,
                                    windows[i].width, windows[i].height),
                .include_minimized = mode != 0,
                .owner = comparison->windows[i].owner,
                .ax = ax,
                .tag_layout = comparison->tag_layout
            };
            classifications[mode] = ws_classify_window_candidate(&input);
            ++class_counts[mode][classifications[mode].window_class];
            bool legacy = corpus_legacy_eligible(record, mode != 0);
            enum corpus_comparison_category category =
                corpus_comparison(legacy, classifications[mode].window_class);
            ++comparison_counts[mode][category];
            disagreement |= category == CORPUS_CANDIDATE_ONLY || category == CORPUS_LEGACY_ONLY;
            const struct window_server_tag_corpus_label *label = &comparison->windows[i].label;
            if (label->present) {
                enum ws_window_class expected = mode ? label->expected_include_minimized
                                                     : label->expected_normal;
                if (classifications[mode].window_class == WS_WINDOW_CLASS_MANAGEABLE &&
                    expected != WS_WINDOW_CLASS_MANAGEABLE) ++false_positive;
                if (classifications[mode].window_class != WS_WINDOW_CLASS_MANAGEABLE &&
                    expected == WS_WINDOW_CLASS_MANAGEABLE) ++false_negative;
            }
        }
        if (comparison->windows[i].label.present) ++labeled_count;
        else if (disagreement) ++unlabeled_disagreement;
    }

    if (fputs("{\"schema\":\"yabai.window-server-classifier-comparison/v1\","
              "\"type\":\"capture\",\"scenario\":", stream) == EOF ||
        !corpus_json_string(stream, metadata->scenario) ||
        fputs(",\"os_version\":", stream) == EOF || !corpus_json_string(stream, metadata->os_version) ||
        fputs(",\"os_build\":", stream) == EOF || !corpus_json_string(stream, metadata->os_build) ||
        fputs(",\"architecture\":", stream) == EOF || !corpus_json_string(stream, metadata->architecture) ||
        fprintf(stream,
                ",\"legacy_algorithm\":\"legacy-space-eligibility-v1\","
                "\"candidate_algorithm\":\"conservative-candidate-v1\","
                "\"tag_layout_status\":\"%s\",\"redaction\":\"strict-v1\","
                "\"counts\":{\"public_windows\":%zu,\"snapshot_records\":%zu,"
                "\"normal_classes\":{\"manageable\":%zu,\"user-facing-nonmanageable\":%zu,"
                "\"system-overlay\":%zu,\"uncertain\":%zu},"
                "\"include_minimized_classes\":{\"manageable\":%zu,"
                "\"user-facing-nonmanageable\":%zu,\"system-overlay\":%zu,\"uncertain\":%zu},"
                "\"normal_comparison\":{\"same_include\":%zu,\"same_exclude\":%zu,"
                "\"candidate_only\":%zu,\"legacy_only\":%zu},"
                "\"include_minimized_comparison\":{\"same_include\":%zu,\"same_exclude\":%zu,"
                "\"candidate_only\":%zu,\"legacy_only\":%zu},"
                "\"labeled\":%zu,\"unlabeled_disagreement\":%zu,"
                "\"false_positive\":%zu,\"false_negative\":%zu,"
                "\"ax_matched\":%zu,\"ax_unavailable\":%zu,\"ax_error\":%zu}}\n",
                ws_tag_layout_status_name(comparison->tag_layout), window_count, snapshot->count,
                class_counts[0][0], class_counts[0][1], class_counts[0][2], class_counts[0][3],
                class_counts[1][0], class_counts[1][1], class_counts[1][2], class_counts[1][3],
                comparison_counts[0][0], comparison_counts[0][1], comparison_counts[0][2], comparison_counts[0][3],
                comparison_counts[1][0], comparison_counts[1][1], comparison_counts[1][2], comparison_counts[1][3],
                labeled_count, unlabeled_disagreement, false_positive, false_negative,
                ax_matched, ax_unavailable, ax_error) < 0) return false;

    for (size_t i = 0; i < window_count; ++i) {
        const struct window_server_window_record *record =
            corpus_find_record(snapshot, windows[i].window_id);
        int parent_ordinal = corpus_public_ordinal(windows, window_count, record->parent_id);
        const char *parent_kind = record->parent_id == 0 ? "root" :
                                  parent_ordinal >= 0 ? "captured" : "external";
        struct ws_window_classification classification[2];
        bool legacy[2];
        enum corpus_comparison_category category[2];
        for (int mode = 0; mode < 2; ++mode) {
            classification[mode] = ws_classify_window_candidate(
                &(struct ws_window_classification_input) {
                    .valid_fields = record->valid_fields, .tags = record->tags,
                    .attributes = record->attributes, .parent_id = record->parent_id,
                    .pid = windows[i].owner_pid, .level = record->level,
                    .normal_level = comparison->normal_level,
                    .floating_level = comparison->floating_level,
                    .modal_level = comparison->modal_level,
                    .frame = CGRectMake(windows[i].x, windows[i].y,
                                        windows[i].width, windows[i].height),
                    .include_minimized = mode != 0,
                    .owner = comparison->windows[i].owner,
                    .ax = comparison->windows[i].ax,
                    .tag_layout = comparison->tag_layout
                });
            legacy[mode] = corpus_legacy_eligible(record, mode != 0);
            category[mode] = corpus_comparison(legacy[mode], classification[mode].window_class);
        }
        if (fprintf(stream,
                    "{\"schema\":\"yabai.window-server-classifier-comparison/v1\","
                    "\"type\":\"window\",\"ordinal\":%zu,\"owner_ordinal\":%zu,"
                    "\"parent_kind\":\"%s\",\"parent_ordinal\":",
                    i, corpus_owner_ordinal(windows, i), parent_kind) < 0) return false;
        if (parent_ordinal >= 0) { if (fprintf(stream, "%d", parent_ordinal) < 0) return false; }
        else if (fputs("null", stream) == EOF) return false;
        if (fprintf(stream,
                    ",\"sls_level\":%d,\"cg_layer\":%d,\"on_screen\":%s,"
                    "\"tags_hex\":\"0x%016llx\",\"attributes_hex\":\"0x%016llx\","
                    "\"unknown_tags_hex\":\"0x%016llx\",\"owner_kind\":\"%s\","
                    "\"frame_state\":\"%s\",\"ax_status\":\"%s\","
                    "\"title_state\":\"%s\",\"legacy_normal\":%s,"
                    "\"legacy_include_minimized\":%s,\"candidate_normal\":\"%s\","
                    "\"candidate_include_minimized\":\"%s\","
                    "\"comparison_normal\":\"%s\",\"comparison_include_minimized\":\"%s\","
                    "\"reasons_normal\":",
                    record->level, windows[i].cg_layer, windows[i].on_screen ? "true" : "false",
                    (unsigned long long) record->tags, (unsigned long long) record->attributes,
                    (unsigned long long) (record->tags & ~WINDOW_SERVER_TAG_KNOWN_MASK),
                    ws_owner_kind_name(comparison->windows[i].owner), corpus_frame_state(windows + i),
                    ws_ax_corroboration_name(comparison->windows[i].ax),
                    corpus_title_state_name(comparison->windows[i].title),
                    legacy[0] ? "true" : "false", legacy[1] ? "true" : "false",
                    ws_window_class_name(classification[0].window_class),
                    ws_window_class_name(classification[1].window_class),
                    corpus_comparison_name(category[0]), corpus_comparison_name(category[1])) < 0 ||
            !corpus_write_reasons(stream, classification[0].reasons) ||
            fputs(",\"reasons_include_minimized\":", stream) == EOF ||
            !corpus_write_reasons(stream, classification[1].reasons)) return false;
        const struct window_server_tag_corpus_label *label = &comparison->windows[i].label;
        if (label->present) {
            if (fputs(",\"fixture_role\":", stream) == EOF ||
                !corpus_json_string(stream, label->fixture_role) ||
                fprintf(stream, ",\"expected_normal\":\"%s\","
                                "\"expected_include_minimized\":\"%s\"",
                        ws_window_class_name(label->expected_normal),
                        ws_window_class_name(label->expected_include_minimized)) < 0) return false;
        }
        if (fputs("}\n", stream) == EOF) return false;
    }
    return fflush(stream) == 0 && !ferror(stream);
}

bool window_server_tag_corpus_write_comparison_file(
    const char *path,
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t window_count,
    const struct window_server_snapshot *snapshot,
    const struct window_server_tag_corpus_comparison *comparison)
{
    if (!path || !path[0]) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    FILE *stream = fdopen(fd, "w");
    if (!stream) { close(fd); unlink(path); return false; }
    bool success = window_server_tag_corpus_serialize_comparison(
        stream, metadata, windows, window_count, snapshot, comparison);
    if (fclose(stream) != 0) success = false;
    if (!success) unlink(path);
    return success;
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
