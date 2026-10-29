#include "display_cadence_diagnostic_core.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool cadence_json_string(FILE *stream, const char *value)
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
            } else if (fputc(*cursor, stream) == EOF) return false;
        }
    }
    return fputc('"', stream) != EOF;
}

static bool cadence_revision_is_valid(const char *value)
{
    if (!value || !value[0]) return false;
    for (const unsigned char *cursor = (const unsigned char *) value; *cursor; ++cursor) {
        if ((*cursor < '0' || *cursor > '9') &&
            (*cursor < 'A' || *cursor > 'Z') &&
            (*cursor < 'a' || *cursor > 'z') &&
            *cursor != '.' && *cursor != '_' && *cursor != '-' && *cursor != '/') return false;
    }
    return true;
}

bool display_cadence_target_timestamps_are_monotonic(
    const struct display_cadence_sample *samples, size_t sample_count)
{
    if (!samples && sample_count) return false;
    for (size_t i = 0; i < sample_count; ++i) {
        if (!isfinite(samples[i].target_timestamp)) return false;
        if (i && samples[i].target_timestamp <= samples[i - 1].target_timestamp) return false;
    }
    return true;
}

static int cadence_double_compare(const void *left, const void *right)
{
    const double a = *(const double *) left;
    const double b = *(const double *) right;
    return a < b ? -1 : a > b ? 1 : 0;
}

bool display_cadence_delta_stats(const struct display_cadence_sample *samples,
                                 size_t sample_count, bool target_timestamp,
                                 struct display_cadence_delta_stats *stats)
{
    if (!stats || (!samples && sample_count)) return false;
    *stats = (struct display_cadence_delta_stats) {0};
    if (sample_count < 2) return true;

    size_t count = sample_count - 1;
    double *deltas = calloc(count, sizeof(*deltas));
    if (!deltas) return false;
    for (size_t i = 1; i < sample_count; ++i) {
        double previous = target_timestamp ? samples[i - 1].target_timestamp
                                           : samples[i - 1].wall_timestamp;
        double current = target_timestamp ? samples[i].target_timestamp
                                          : samples[i].wall_timestamp;
        deltas[i - 1] = current - previous;
        if (!isfinite(deltas[i - 1]) || deltas[i - 1] <= 0.0) {
            free(deltas);
            return false;
        }
    }
    qsort(deltas, count, sizeof(*deltas), cadence_double_compare);
    size_t p50_index = (count + 1) / 2 - 1;
    size_t p95_index = (count * 95 + 99) / 100 - 1;
    stats->count = count;
    stats->p50 = deltas[p50_index];
    stats->p95 = deltas[p95_index];
    stats->min = deltas[0];
    stats->max = deltas[count - 1];
    free(deltas);
    return true;
}

static bool cadence_write_number_or_null(FILE *stream, bool available, double value)
{
    if (!available) return fputs("null", stream) != EOF;
    return isfinite(value) && fprintf(stream, "%.9f", value) >= 0;
}

static bool cadence_write_stats(FILE *stream, const struct display_cadence_delta_stats *stats)
{
    return fprintf(stream, "{\"count\":%zu,\"p50\":", stats->count) >= 0 &&
           cadence_write_number_or_null(stream, stats->count != 0, stats->p50) &&
           fputs(",\"p95\":", stream) != EOF &&
           cadence_write_number_or_null(stream, stats->count != 0, stats->p95) &&
           fputs(",\"min\":", stream) != EOF &&
           cadence_write_number_or_null(stream, stats->count != 0, stats->min) &&
           fputs(",\"max\":", stream) != EOF &&
           cadence_write_number_or_null(stream, stats->count != 0, stats->max) &&
           fputc('}', stream) != EOF;
}

static bool cadence_metadata_is_valid(const struct display_cadence_metadata *metadata)
{
    return metadata && cadence_revision_is_valid(metadata->source_revision) &&
           cadence_revision_is_valid(metadata->fixture_revision) && metadata->os_version &&
           metadata->os_build && metadata->architecture && metadata->screen_count <= 64 &&
           metadata->requested_max_samples > 0 &&
           metadata->requested_max_samples <= DISPLAY_CADENCE_DIAGNOSTIC_MAX_SAMPLES &&
           isfinite(metadata->requested_duration_seconds) &&
           metadata->requested_duration_seconds > 0.0 &&
           metadata->requested_duration_seconds <= 60.0 &&
           metadata->links_created <= metadata->screen_count &&
           isfinite(metadata->invalidation_wait_seconds) &&
           metadata->invalidation_wait_seconds >= 0.0 &&
           metadata->invalidation_wait_seconds <= 1.0;
}

bool display_cadence_diagnostic_write_json(
    FILE *stream, const struct display_cadence_metadata *metadata,
    const struct display_cadence_screen *screens)
{
    if (!stream || !cadence_metadata_is_valid(metadata) ||
        (!screens && metadata->screen_count)) return false;
    if (fprintf(stream, "{\"schema\":\"%s\",\"tool\":\"display-cadence-diagnostic\","
                "\"mode\":\"public-nsscreen-cadisplaylink-read-only\","
                "\"source_revision\":", DISPLAY_CADENCE_DIAGNOSTIC_SCHEMA) < 0 ||
        !cadence_json_string(stream, metadata->source_revision) ||
        fputs(",\"fixture_revision\":", stream) == EOF ||
        !cadence_json_string(stream, metadata->fixture_revision) ||
        fputs(",\"platform\":{\"os_version\":", stream) == EOF ||
        !cadence_json_string(stream, metadata->os_version) ||
        fputs(",\"os_build\":", stream) == EOF || !cadence_json_string(stream, metadata->os_build) ||
        fputs(",\"architecture\":", stream) == EOF || !cadence_json_string(stream, metadata->architecture) ||
        fprintf(stream, "},\"requested\":{\"duration_seconds\":%.9f,"
                      "\"max_samples_per_screen\":%zu},\"lifecycle\":{"
                      "\"screens_current\":%zu,\"links_created\":%zu,"
                      "\"links_created_for_all_current_screens\":%s,"
                      "\"callbacks_before_invalidate\":%llu,"
                      "\"callbacks_after_invalidate\":%llu,"
                      "\"invalidation_wait_seconds\":%.9f,"
                      "\"invalidation_wait_bounded\":%s,"
                      "\"production_backend_unchanged\":%s},\"screens\":[",
                      metadata->requested_duration_seconds, metadata->requested_max_samples,
                      metadata->screen_count, metadata->links_created,
                      metadata->links_created == metadata->screen_count ? "true" : "false",
                      (unsigned long long) metadata->callbacks_before_invalidate,
                      (unsigned long long) metadata->callbacks_after_invalidate,
                      metadata->invalidation_wait_seconds,
                      metadata->invalidation_wait_bounded ? "true" : "false",
                      metadata->production_backend_unchanged ? "true" : "false") < 0) return false;

    for (size_t i = 0; i < metadata->screen_count; ++i) {
        const struct display_cadence_screen *screen = screens + i;
        struct display_cadence_delta_stats target = {0}, wall = {0};
        if (screen->series.sample_count > metadata->requested_max_samples ||
            screen->series.sample_count > DISPLAY_CADENCE_DIAGNOSTIC_MAX_SAMPLES ||
            !display_cadence_delta_stats(screen->series.samples, screen->series.sample_count,
                                         true, &target) ||
            !display_cadence_delta_stats(screen->series.samples, screen->series.sample_count,
                                         false, &wall)) return false;
        if (i && fputc(',', stream) == EOF) return false;
        if (fprintf(stream, "{\"ordinal\":%zu,\"nominal_refresh_hz\":", i) < 0 ||
            !cadence_write_number_or_null(stream, screen->nominal_refresh_available,
                                          screen->nominal_refresh_hz) ||
            fputs(",\"current_mode_refresh_hz\":", stream) == EOF ||
            !cadence_write_number_or_null(stream, screen->current_mode_refresh_available,
                                          screen->current_mode_refresh_hz) ||
            fputs(",\"backing_scale\":", stream) == EOF ||
            !cadence_write_number_or_null(stream, screen->backing_scale_available,
                                          screen->backing_scale) ||
            fprintf(stream, ",\"callback_count\":%llu,\"target_timestamp_monotonic\":%s,"
                          "\"target_delta_seconds\":",
                    (unsigned long long) screen->series.callback_count,
                    display_cadence_target_timestamps_are_monotonic(
                        screen->series.samples, screen->series.sample_count) ? "true" : "false") < 0 ||
            !cadence_write_stats(stream, &target) || fputs(",\"wall_clock_delta_seconds\":", stream) == EOF ||
            !cadence_write_stats(stream, &wall) || fputc('}', stream) == EOF) return false;
    }
    return fputs("],\"redaction\":\"strict-v1\"}\n", stream) != EOF &&
           fflush(stream) == 0 && !ferror(stream);
}

static bool cadence_stress_metadata_is_valid(
    const struct display_cadence_stress_metadata *metadata)
{
    return metadata && cadence_revision_is_valid(metadata->source_revision) &&
           cadence_revision_is_valid(metadata->fixture_revision) && metadata->os_version &&
           metadata->os_build && metadata->architecture &&
           metadata->cycles_requested > 0 &&
           metadata->cycles_requested <= DISPLAY_CADENCE_DIAGNOSTIC_MAX_STRESS_CYCLES &&
           metadata->cycles_completed <= metadata->cycles_requested &&
           metadata->reinventory_interval > 0 &&
           metadata->reinventory_count <= metadata->cycles_completed &&
           metadata->topology_changes_observed <= metadata->reinventory_count &&
           metadata->screen_count <= 64 &&
           metadata->links_created <= metadata->cycles_completed * 64 &&
           metadata->link_creation_failures <= metadata->cycles_completed * 64 &&
           metadata->cycles_with_all_lanes_callback <= metadata->cycles_completed &&
           isfinite(metadata->active_cycle_seconds) &&
           metadata->active_cycle_seconds > 0.0 && metadata->active_cycle_seconds <= 1.0 &&
           isfinite(metadata->invalidation_wait_seconds) &&
           metadata->invalidation_wait_seconds >= 0.0 && metadata->invalidation_wait_seconds <= 0.1 &&
           isfinite(metadata->main_thread_load_ms) &&
           metadata->main_thread_load_ms >= 0.0 && metadata->main_thread_load_ms <= 10.0 &&
           isfinite(metadata->lane_completion_skew_max_seconds) &&
           metadata->lane_completion_skew_max_seconds >= 0.0 &&
           metadata->lane_completion_skew_max_seconds <= 1.0;
}

bool display_cadence_stress_write_json(
    FILE *stream, const struct display_cadence_stress_metadata *metadata,
    const struct display_cadence_stress_lane *lanes)
{
    if (!stream || !cadence_stress_metadata_is_valid(metadata) ||
        (!lanes && metadata->screen_count)) return false;
    if (fprintf(stream, "{\"schema\":\"yabai.display-cadence-stress/v1\","
                "\"tool\":\"display-cadence-diagnostic\","
                "\"mode\":\"public-nsscreen-cadisplaylink-stress-read-only\","
                "\"source_revision\":") < 0 ||
        !cadence_json_string(stream, metadata->source_revision) ||
        fputs(",\"fixture_revision\":", stream) == EOF ||
        !cadence_json_string(stream, metadata->fixture_revision) ||
        fputs(",\"platform\":{\"os_version\":", stream) == EOF ||
        !cadence_json_string(stream, metadata->os_version) ||
        fputs(",\"os_build\":", stream) == EOF ||
        !cadence_json_string(stream, metadata->os_build) ||
        fputs(",\"architecture\":", stream) == EOF ||
        !cadence_json_string(stream, metadata->architecture) ||
        fprintf(stream, "},\"requested\":{\"cycles\":%zu,\"reinventory_interval\":%zu,"
                      "\"active_cycle_seconds\":%.6f,\"main_thread_load_ms\":%.3f},"
                      "\"lifecycle\":{\"cycles_completed\":%zu,"
                      "\"reinventory_count\":%zu,\"topology_changes_observed\":%zu,"
                      "\"screens_current\":%zu,\"links_created\":%zu,"
                      "\"link_creation_failures\":%zu,\"callbacks_before_invalidate\":%llu,"
                      "\"callbacks_after_invalidate\":%llu,"
                      "\"cycles_with_all_lanes_callback\":%zu,"
                      "\"lane_completion_skew_max_seconds\":%.9f,"
                      "\"invalidation_wait_seconds\":%.6f,"
                      "\"production_backend_unchanged\":%s},\"lanes\":[",
                      metadata->cycles_requested, metadata->reinventory_interval,
                      metadata->active_cycle_seconds, metadata->main_thread_load_ms,
                      metadata->cycles_completed, metadata->reinventory_count,
                      metadata->topology_changes_observed, metadata->screen_count,
                      metadata->links_created, metadata->link_creation_failures,
                      (unsigned long long) metadata->callbacks_before_invalidate,
                      (unsigned long long) metadata->callbacks_after_invalidate,
                      metadata->cycles_with_all_lanes_callback,
                      metadata->lane_completion_skew_max_seconds,
                      metadata->invalidation_wait_seconds,
                      metadata->production_backend_unchanged ? "true" : "false") < 0) return false;
    for (size_t i = 0; i < metadata->screen_count; ++i) {
        const struct display_cadence_stress_lane *lane = lanes + i;
        if (lane->cycles_with_callback > lane->cycles_seen ||
            lane->cycles_seen > metadata->cycles_completed) return false;
        if (i && fputc(',', stream) == EOF) return false;
        if (fprintf(stream, "{\"ordinal\":%zu,\"nominal_refresh_hz\":", i) < 0 ||
            !cadence_write_number_or_null(stream, lane->facts.nominal_refresh_available,
                                          lane->facts.nominal_refresh_hz) ||
            fputs(",\"current_mode_refresh_hz\":", stream) == EOF ||
            !cadence_write_number_or_null(stream, lane->facts.current_mode_refresh_available,
                                          lane->facts.current_mode_refresh_hz) ||
            fputs(",\"backing_scale\":", stream) == EOF ||
            !cadence_write_number_or_null(stream, lane->facts.backing_scale_available,
                                          lane->facts.backing_scale) ||
            fprintf(stream, ",\"cycles_seen\":%zu,\"cycles_with_callback\":%zu,"
                          "\"callbacks\":%llu,\"callbacks_after_invalidate\":%llu}",
                    lane->cycles_seen, lane->cycles_with_callback,
                    (unsigned long long) lane->callback_count,
                    (unsigned long long) lane->callbacks_after_invalidate) < 0) return false;
    }
    return fputs("],\"redaction\":\"strict-v1\"}\n", stream) != EOF &&
           fflush(stream) == 0 && !ferror(stream);
}
