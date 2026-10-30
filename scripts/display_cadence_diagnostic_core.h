#ifndef DISPLAY_CADENCE_DIAGNOSTIC_CORE_H
#define DISPLAY_CADENCE_DIAGNOSTIC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DISPLAY_CADENCE_DIAGNOSTIC_SCHEMA "yabai.display-cadence/v1"
#define DISPLAY_CADENCE_DIAGNOSTIC_MAX_SAMPLES 10000U
#define DISPLAY_CADENCE_DIAGNOSTIC_MAX_STRESS_CYCLES 100U

struct display_cadence_sample
{
    double target_timestamp;
    double wall_timestamp;
};

struct display_cadence_series
{
    const struct display_cadence_sample *samples;
    size_t sample_count;
    uint64_t callback_count;
};

struct display_cadence_screen
{
    bool nominal_refresh_available;
    double nominal_refresh_hz;
    bool current_mode_refresh_available;
    double current_mode_refresh_hz;
    bool backing_scale_available;
    double backing_scale;
    struct display_cadence_series series;
};

struct display_cadence_delta_stats
{
    size_t count;
    double p50;
    double p95;
    double min;
    double max;
};

struct display_cadence_stress_lane
{
    struct display_cadence_screen facts;
    size_t cycles_seen;
    size_t cycles_with_callback;
    uint64_t callback_count;
    uint64_t callbacks_after_invalidate;
};

struct display_cadence_stress_metadata
{
    const char *source_revision;
    const char *fixture_revision;
    const char *os_version;
    const char *os_build;
    const char *architecture;
    size_t cycles_requested;
    size_t cycles_completed;
    size_t reinventory_interval;
    size_t reinventory_count;
    size_t topology_changes_observed;
    size_t screen_count;
    size_t links_created;
    size_t link_creation_failures;
    size_t cycles_with_all_lanes_callback;
    uint64_t callbacks_before_invalidate;
    uint64_t callbacks_after_invalidate;
    double active_cycle_seconds;
    double invalidation_wait_seconds;
    double main_thread_load_ms;
    double lane_completion_skew_max_seconds;
    bool production_backend_unchanged;
};

struct display_cadence_metadata
{
    const char *source_revision;
    const char *fixture_revision;
    const char *os_version;
    const char *os_build;
    const char *architecture;
    double requested_duration_seconds;
    size_t requested_max_samples;
    size_t screen_count;
    size_t links_created;
    uint64_t callbacks_before_invalidate;
    uint64_t callbacks_after_invalidate;
    double invalidation_wait_seconds;
    bool invalidation_wait_bounded;
    bool production_backend_unchanged;
};

bool display_cadence_target_timestamps_are_monotonic(
    const struct display_cadence_sample *samples, size_t sample_count);
bool display_cadence_delta_stats(const struct display_cadence_sample *samples,
                                 size_t sample_count, bool target_timestamp,
                                 struct display_cadence_delta_stats *stats);
bool display_cadence_diagnostic_write_json(
    FILE *stream, const struct display_cadence_metadata *metadata,
    const struct display_cadence_screen *screens);
bool display_cadence_stress_write_json(
    FILE *stream, const struct display_cadence_stress_metadata *metadata,
    const struct display_cadence_stress_lane *lanes);

#endif
