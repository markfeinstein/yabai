#include "native_tab_event_recorder_core.h"

#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *native_tab_event_name(enum native_tab_event_type type)
{
    switch (type) {
    case NATIVE_TAB_EVENT_SLS_VISIBLE: return "sls-visible";
    case NATIVE_TAB_EVENT_SLS_INVISIBLE: return "sls-invisible";
    case NATIVE_TAB_EVENT_SLS_DESTROYED: return "sls-destroyed";
    case NATIVE_TAB_EVENT_AX_CREATED: return "ax-created";
    case NATIVE_TAB_EVENT_AX_DESTROYED: return "ax-destroyed";
    case NATIVE_TAB_EVENT_AX_MINIMIZED: return "ax-minimized";
    case NATIVE_TAB_EVENT_AX_DEMINIMIZED: return "ax-deminiaturized";
    case NATIVE_TAB_EVENT_AX_FOCUSED: return "ax-focused";
    case NATIVE_TAB_EVENT_AX_FOCUS_STABLE: return "ax-focus-stable";
    case NATIVE_TAB_EVENT_APP_HIDDEN: return "app-hidden";
    case NATIVE_TAB_EVENT_APP_VISIBLE: return "app-visible";
    case NATIVE_TAB_EVENT_APP_TERMINATED: return "app-terminated";
    case NATIVE_TAB_EVENT_SPACE_CHANGED: return "space-changed";
    case NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER: return "mission-control-enter";
    default: return "unknown";
    }
}

bool native_tab_scenario_is_valid(const char *scenario)
{
    if (!scenario || !scenario[0]) return false;
    size_t length = strlen(scenario);
    if (length >= NATIVE_TAB_SCENARIO_CAPACITY) return false;
    for (size_t i = 0; i < length; ++i) {
        char c = scenario[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}

bool native_tab_trace_open(const char *path, FILE **stream)
{
    if (!path || !path[0] || !stream) return false;
    *stream = NULL;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    struct stat status;
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != getuid() ||
        status.st_nlink != 1 || (status.st_mode & 0777) != 0600) {
        close(fd);
        unlink(path);
        return false;
    }
    FILE *result = fdopen(fd, "w");
    if (!result) {
        close(fd);
        unlink(path);
        return false;
    }
    *stream = result;
    return true;
}

bool native_tab_trace_close(FILE *stream, const char *path, bool success)
{
    if (!stream || !path) return false;
    if (fclose(stream) != 0) success = false;
    if (!success) unlink(path);
    return success;
}

static uint64_t native_tab_elapsed_ms(const struct native_tab_recorder *recorder, uint64_t now_ns)
{
    return now_ns >= recorder->start_ns ? (now_ns - recorder->start_ns) / UINT64_C(1000000) : 0;
}

static bool native_tab_finish_record(struct native_tab_recorder *recorder)
{
    if (fputc('\n', recorder->stream) == EOF) return false;
    return fflush(recorder->stream) == 0 && !ferror(recorder->stream);
}

static struct native_tab_ordinal_entry *native_tab_ordinal_entry(
    struct native_tab_recorder *recorder, uint32_t window_id, bool create)
{
    if (!window_id) return NULL;
    for (size_t i = 0; i < recorder->ordinal_count; ++i) {
        if (recorder->ordinals[i].window_id == window_id) return recorder->ordinals + i;
    }
    if (!create || recorder->ordinal_count >= NATIVE_TAB_INVENTORY_CAPACITY) return NULL;
    struct native_tab_ordinal_entry *entry = recorder->ordinals + recorder->ordinal_count;
    *entry = (struct native_tab_ordinal_entry) {
        .window_id = window_id,
        .ordinal = (uint16_t) recorder->ordinal_count
    };
    ++recorder->ordinal_count;
    return entry;
}

static const struct native_tab_ordinal_entry *native_tab_ordinal_entry_const(
    const struct native_tab_recorder *recorder, uint32_t window_id)
{
    for (size_t i = 0; i < recorder->ordinal_count; ++i) {
        if (recorder->ordinals[i].window_id == window_id) return recorder->ordinals + i;
    }
    return NULL;
}

static void native_tab_record_fact(struct native_tab_recorder *recorder,
                                   const struct native_tab_window_fact *fact)
{
    if (!fact || !fact->window_id) return;
    struct native_tab_ordinal_entry *entry = native_tab_ordinal_entry(recorder, fact->window_id, true);
    if (!entry) { ++recorder->dropped_count; return; }
    entry->fact = *fact;
    entry->has_fact = true;
    if (fact->parent_window_id) (void) native_tab_ordinal_entry(recorder, fact->parent_window_id, true);
}

static bool native_tab_inventory_contains(const struct native_tab_inventory *inventory,
                                          uint32_t window_id)
{
    if (!inventory || !window_id) return false;
    for (size_t i = 0; i < inventory->count; ++i) {
        if (inventory->windows[i].window_id == window_id) return true;
    }
    return false;
}

bool native_tab_recorder_window_known(const struct native_tab_recorder *recorder,
                                      uint32_t window_id)
{
    return recorder && native_tab_inventory_contains(&recorder->known, window_id);
}

static bool native_tab_episode_contains(const uint32_t *values, uint8_t count, uint32_t window_id)
{
    for (uint8_t i = 0; i < count; ++i) if (values[i] == window_id) return true;
    return false;
}

static void native_tab_episode_begin(struct native_tab_recorder *recorder, uint64_t now_ns)
{
    if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING) return;
    uint64_t pre_quiet = now_ns >= recorder->last_inventory_change_ns
                       ? now_ns - recorder->last_inventory_change_ns : 0;
    uint64_t inter_gap = recorder->last_action_end_ns && now_ns >= recorder->last_action_end_ns
                       ? now_ns - recorder->last_action_end_ns : 0;
    bool pre_valid = pre_quiet >= NATIVE_TAB_ACTION_SEPARATION_NS;
    bool inter_valid = !recorder->last_action_end_ns ||
                       inter_gap >= NATIVE_TAB_ACTION_SEPARATION_NS;
    recorder->episode = (struct native_tab_episode) {
        .state = NATIVE_TAB_EPISODE_COLLECTING,
        .first_event_ns = now_ns,
        .last_event_ns = now_ns,
        .settle_deadline_ns = now_ns + NATIVE_TAB_ACTION_SEPARATION_NS,
        .hard_deadline_ns = now_ns + NATIVE_TAB_HARD_LIFETIME_NS,
        .inventory_complete = true,
        .pre_quiet_valid = pre_valid,
        .inter_action_valid = inter_valid,
        .action_separation_violated = !pre_valid || !inter_valid,
        .pre_quiet_ns = pre_quiet,
        .inter_action_gap_ns = inter_gap,
        .action_start_ns = now_ns,
        .episode_id = recorder->next_episode_id++
    };
}

static bool native_tab_episode_add(struct native_tab_recorder *recorder, bool appeared,
                                   uint32_t window_id, uint64_t now_ns)
{
    if (!window_id) return true;
    native_tab_episode_begin(recorder, now_ns);
    uint32_t *values = appeared ? recorder->episode.appeared : recorder->episode.disappeared;
    uint8_t *count = appeared ? &recorder->episode.appeared_count : &recorder->episode.disappeared_count;
    recorder->episode.last_event_ns = now_ns;
    recorder->episode.settle_deadline_ns = now_ns + NATIVE_TAB_ACTION_SEPARATION_NS;
    if (native_tab_episode_contains(values, *count, window_id)) return true;
    if (*count >= NATIVE_TAB_EPISODE_CAPACITY) {
        recorder->episode.overflowed = true;
        return false;
    }
    values[(*count)++] = window_id;
    return true;
}

static bool native_tab_write_ordinal_array(struct native_tab_recorder *recorder,
                                           const uint32_t *values, uint8_t count)
{
    if (fputc('[', recorder->stream) == EOF) return false;
    for (uint8_t i = 0; i < count; ++i) {
        const struct native_tab_ordinal_entry *entry =
            native_tab_ordinal_entry_const(recorder, values[i]);
        if (i && fputc(',', recorder->stream) == EOF) return false;
        if (!entry || fprintf(recorder->stream, "%u", entry->ordinal) < 0) return false;
    }
    return fputc(']', recorder->stream) != EOF;
}

static bool native_tab_write_evidence(struct native_tab_recorder *recorder)
{
    uint32_t evidence = recorder->episode.evidence;
    return fprintf(recorder->stream,
        "{\"sls_816\":%s,\"sls_815\":%s,\"sls_804\":%s,"
        "\"ax_created\":%s,\"ax_destroyed\":%s,\"ax_minimized\":%s,"
        "\"known_id_reappeared\":%s,\"inventory_complete\":%s}",
        evidence & NATIVE_TAB_EVIDENCE_SLS_816 ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_SLS_815 ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_SLS_804 ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_AX_CREATED ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_AX_DESTROYED ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_AX_MINIMIZED ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_KNOWN_REAPPEAR ? "true" : "false",
        recorder->episode.inventory_complete ? "true" : "false") >= 0;
}

static bool native_tab_write_nullable_bool(FILE *stream, bool known, bool value)
{
    return known ? fputs(value ? "true" : "false", stream) != EOF
                 : fputs("null", stream) != EOF;
}

static bool native_tab_write_nullable_int(FILE *stream, bool known, long long value)
{
    return known ? fprintf(stream, "%lld", value) >= 0 : fputs("null", stream) != EOF;
}

static const char *native_tab_event_order_class(const struct native_tab_episode *episode)
{
    bool has_815 = episode->first_confirmed_815_sequence != 0;
    bool has_816 = episode->first_confirmed_816_sequence != 0;
    if (episode->sls_815.duplicate || episode->sls_816.duplicate) return "mixed-or-duplicate-order";
    if (has_815 && has_816) {
        return episode->first_confirmed_816_sequence < episode->first_confirmed_815_sequence
             ? "816-before-815" : "815-before-816";
    }
    if (has_816) return "816-only";
    if (has_815) return "815-only";
    return "neither";
}

static bool native_tab_write_pair(struct native_tab_recorder *recorder,
                                  uint32_t old_id, uint32_t new_id)
{
    const struct native_tab_ordinal_entry *old_entry = native_tab_ordinal_entry_const(recorder, old_id);
    const struct native_tab_ordinal_entry *new_entry = native_tab_ordinal_entry_const(recorder, new_id);
    const struct native_tab_window_fact *old_fact = old_entry && old_entry->has_fact ? &old_entry->fact : NULL;
    const struct native_tab_window_fact *new_fact = new_entry && new_entry->has_fact ? &new_entry->fact : NULL;
    if (!old_entry || !new_entry ||
        fprintf(recorder->stream,
                "{\"disappeared_ordinal\":%u,\"appeared_ordinal\":%u,"
                "\"same_owner\":true,\"shared_space\":",
                old_entry->ordinal, new_entry->ordinal) < 0) return false;
    bool space_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_SPACE) && (new_fact->validity & NATIVE_TAB_FACT_SPACE);
    if (!native_tab_write_nullable_bool(recorder->stream, space_known,
                                        space_known && old_fact->space_id == new_fact->space_id)) return false;

    if (fputs(",\"root_compatible\":", recorder->stream) == EOF) return false;
    bool parent_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_PARENT) && (new_fact->validity & NATIVE_TAB_FACT_PARENT);
    bool root_compatible = parent_known &&
        (old_fact->parent_window_id == 0) == (new_fact->parent_window_id == 0);
    if (!native_tab_write_nullable_bool(recorder->stream, parent_known, root_compatible)) return false;

    if (fputs(",\"frame_delta_8pt\":", recorder->stream) == EOF) return false;
    bool frame_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_FRAME) && (new_fact->validity & NATIVE_TAB_FACT_FRAME);
    if (frame_known) {
        long long delta[] = {
            llround((new_fact->x - old_fact->x) / 8.0),
            llround((new_fact->y - old_fact->y) / 8.0),
            llround((new_fact->width - old_fact->width) / 8.0),
            llround((new_fact->height - old_fact->height) / 8.0)
        };
        if (fprintf(recorder->stream, "[%lld,%lld,%lld,%lld]",
                    delta[0], delta[1], delta[2], delta[3]) < 0) return false;
    } else if (fputs("null", recorder->stream) == EOF) return false;

    bool level_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_LEVEL) && (new_fact->validity & NATIVE_TAB_FACT_LEVEL);
    bool sublevel_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_SUBLEVEL) && (new_fact->validity & NATIVE_TAB_FACT_SUBLEVEL);
    bool order_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_ORDER) && (new_fact->validity & NATIVE_TAB_FACT_ORDER);
    if (fputs(",\"level_delta\":", recorder->stream) == EOF ||
        !native_tab_write_nullable_int(recorder->stream, level_known,
                                       level_known ? new_fact->level - old_fact->level : 0) ||
        fputs(",\"sublevel_delta\":", recorder->stream) == EOF ||
        !native_tab_write_nullable_int(recorder->stream, sublevel_known,
                                       sublevel_known ? new_fact->sublevel - old_fact->sublevel : 0) ||
        fputs(",\"order_delta\":", recorder->stream) == EOF ||
        !native_tab_write_nullable_int(recorder->stream, order_known,
                                       order_known ? new_fact->order - old_fact->order : 0) ||
        fprintf(recorder->stream, ",\"order_adjacent\":%s",
                order_known && llabs((long long)new_fact->order - old_fact->order) == 1
                    ? "true" : "false") < 0) return false;

    if (fputs(",\"role_compatible\":", recorder->stream) == EOF) return false;
    bool role_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_ROLE) && (new_fact->validity & NATIVE_TAB_FACT_ROLE) &&
        (old_fact->validity & NATIVE_TAB_FACT_SUBROLE) && (new_fact->validity & NATIVE_TAB_FACT_SUBROLE);
    if (!native_tab_write_nullable_bool(recorder->stream, role_known,
        role_known && old_fact->role_token == new_fact->role_token &&
        old_fact->subrole_token == new_fact->subrole_token)) return false;

    if (fputs(",\"same_title_token\":", recorder->stream) == EOF) return false;
    bool title_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_TITLE) && (new_fact->validity & NATIVE_TAB_FACT_TITLE);
    if (!native_tab_write_nullable_bool(recorder->stream, title_known,
                                        title_known && old_fact->title_token == new_fact->title_token) ||
        fprintf(recorder->stream,
                ",\"event_order\":\"%s\","
                "\"sls_815_delivered\":%u,\"sls_816_delivered\":%u,"
                "\"lifecycle_exclusions\":%u,\"readback_conflict\":%s}",
                native_tab_event_order_class(&recorder->episode),
                recorder->episode.sls_815.delivered, recorder->episode.sls_816.delivered,
                recorder->episode.lifecycle_exclusions,
                recorder->episode.readback_conflict ? "true" : "false") < 0) return false;
    return true;
}

static bool native_tab_write_comparisons(struct native_tab_recorder *recorder)
{
    if (fputc('[', recorder->stream) == EOF) return false;
    size_t emitted = 0;
    for (uint8_t old = 0; old < recorder->episode.disappeared_count; ++old) {
        for (uint8_t new_index = 0; new_index < recorder->episode.appeared_count; ++new_index) {
            if (emitted++ && fputc(',', recorder->stream) == EOF) return false;
            if (!native_tab_write_pair(recorder, recorder->episode.disappeared[old],
                                       recorder->episode.appeared[new_index])) return false;
        }
    }
    return fputc(']', recorder->stream) != EOF;
}

static const char *native_tab_timeout_outcome(const struct native_tab_episode *episode)
{
    if (!episode->inventory_complete) return "inventory-incomplete";
    if (episode->disappeared_count == 1 && episode->appeared_count == 1 &&
        episode->disappeared[0] == episode->appeared[0]) return "same-id-reappeared";
    if (episode->disappeared_count == 1 && episode->appeared_count == 1) return "unexplained-one-to-one";
    if (episode->disappeared_count == 1 && episode->appeared_count > 1) return "unexplained-one-to-many";
    if (episode->disappeared_count > 1 && episode->appeared_count == 1) return "unexplained-many-to-one";
    if (episode->disappeared_count > 1 && episode->appeared_count > 1) return "unexplained-many-to-many";
    if (episode->disappeared_count) return "disappearance-only";
    if (episode->appeared_count) return "appearance-only";
    return "inventory-incomplete";
}

static bool native_tab_singleton_compatible(const struct native_tab_recorder *recorder)
{
    if (recorder->episode.disappeared_count != 1 || recorder->episode.appeared_count != 1 ||
        recorder->episode.disappeared[0] == recorder->episode.appeared[0]) return false;
    const struct native_tab_ordinal_entry *old_entry = native_tab_ordinal_entry_const(
        recorder, recorder->episode.disappeared[0]);
    const struct native_tab_ordinal_entry *new_entry = native_tab_ordinal_entry_const(
        recorder, recorder->episode.appeared[0]);
    if (!old_entry || !new_entry || !old_entry->has_fact || !new_entry->has_fact) return false;
    uint32_t required = NATIVE_TAB_FACT_PARENT | NATIVE_TAB_FACT_SPACE |
                        NATIVE_TAB_FACT_LEVEL | NATIVE_TAB_FACT_SUBLEVEL;
    if ((old_entry->fact.validity & required) != required ||
        (new_entry->fact.validity & required) != required) return false;
    return old_entry->fact.space_id == new_entry->fact.space_id &&
           ((old_entry->fact.parent_window_id == 0) ==
            (new_entry->fact.parent_window_id == 0)) &&
           old_entry->fact.level == new_entry->fact.level &&
           old_entry->fact.sublevel == new_entry->fact.sublevel;
}

static bool native_tab_resolution(struct native_tab_recorder *recorder, uint64_t now_ns,
                                  const char *outcome)
{
    if (recorder->episode.state != NATIVE_TAB_EPISODE_COLLECTING) return true;
    for (uint8_t i = 0; i < recorder->episode.outside_candidate_count; ++i) {
        if (!native_tab_recorder_window_known(recorder, recorder->episode.outside_candidates[i])) {
            recorder->episode.unresolved_outside = true;
        }
    }
    recorder->episode.action_end_ns = now_ns;
    recorder->episode.post_quiet_ns = now_ns >= recorder->episode.last_event_ns
                                    ? now_ns - recorder->episode.last_event_ns : 0;
    if (recorder->episode.post_quiet_ns < NATIVE_TAB_ACTION_SEPARATION_NS) {
        recorder->episode.action_separation_violated = true;
    }
    bool gate_eligible = recorder->episode.inventory_complete &&
        !recorder->episode.overflowed && !recorder->episode.readback_conflict &&
        !recorder->episode.unresolved_outside && !recorder->episode.lifecycle_exclusions &&
        !recorder->episode.action_separation_violated &&
        native_tab_singleton_compatible(recorder);
    if (gate_eligible) outcome = "singleton-observational-candidate";
    else if (recorder->episode.lifecycle_exclusions) outcome = "lifecycle-excluded";
    else if (recorder->episode.action_separation_violated) outcome = "action-separation-violated";
    else if (recorder->episode.readback_conflict) outcome = "readback-conflict";
    else if (recorder->episode.unresolved_outside) outcome = "unresolved-outside";

    ++recorder->sequence;
    if (fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v2\",\"redaction\":\"strict-v1\","
        "\"type\":\"resolution\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"scenario\":\"%s\",\"episode_id\":%u,\"outcome\":\"%s\","
        "\"gate_eligible\":%s,\"owner_ordinal\":0,"
        "\"pre_quiet_ms\":%llu,\"post_quiet_ms\":%llu,"
        "\"inter_action_gap_ms\":%llu,\"action_separation_violated\":%s,"
        "\"lifecycle_exclusions\":%u,\"readback_conflict\":%s,"
        "\"unresolved_outside\":%s,"
        "\"event_order\":\"%s\",\"disappeared\":",
        (unsigned long long) recorder->sequence,
        (unsigned long long) native_tab_elapsed_ms(recorder, now_ns),
        recorder->scenario, recorder->episode.episode_id, outcome,
        gate_eligible ? "true" : "false",
        (unsigned long long) (recorder->episode.pre_quiet_ns / UINT64_C(1000000)),
        (unsigned long long) (recorder->episode.post_quiet_ns / UINT64_C(1000000)),
        (unsigned long long) (recorder->episode.inter_action_gap_ns / UINT64_C(1000000)),
        recorder->episode.action_separation_violated ? "true" : "false",
        recorder->episode.lifecycle_exclusions,
        recorder->episode.readback_conflict ? "true" : "false",
        recorder->episode.unresolved_outside ? "true" : "false",
        native_tab_event_order_class(&recorder->episode)) < 0 ||
        !native_tab_write_ordinal_array(recorder, recorder->episode.disappeared,
                                        recorder->episode.disappeared_count) ||
        fputs(",\"appeared\":", recorder->stream) == EOF ||
        !native_tab_write_ordinal_array(recorder, recorder->episode.appeared,
                                        recorder->episode.appeared_count) ||
        fputs(",\"evidence\":", recorder->stream) == EOF ||
        !native_tab_write_evidence(recorder) ||
        fputs(",\"comparisons\":", recorder->stream) == EOF ||
        !native_tab_write_comparisons(recorder) ||
        fprintf(recorder->stream,
                ",\"sls_815\":{\"delivered\":%u,\"payload_invalid\":%u,"
                "\"readback_confirmed\":%u,\"readback_conflict\":%u,"
                "\"readback_unknown\":%u,\"duplicate\":%u},"
                "\"sls_816\":{\"delivered\":%u,\"payload_invalid\":%u,"
                "\"readback_confirmed\":%u,\"readback_conflict\":%u,"
                "\"readback_unknown\":%u,\"duplicate\":%u}}",
                recorder->episode.sls_815.delivered,
                recorder->episode.sls_815.payload_invalid,
                recorder->episode.sls_815.readback_confirmed,
                recorder->episode.sls_815.readback_conflict,
                recorder->episode.sls_815.readback_unknown,
                recorder->episode.sls_815.duplicate,
                recorder->episode.sls_816.delivered,
                recorder->episode.sls_816.payload_invalid,
                recorder->episode.sls_816.readback_confirmed,
                recorder->episode.sls_816.readback_conflict,
                recorder->episode.sls_816.readback_unknown,
                recorder->episode.sls_816.duplicate) < 0 ||
        !native_tab_finish_record(recorder)) return false;
    ++recorder->resolution_count;
    if (gate_eligible) ++recorder->gate_eligible_count;
    if (!recorder->episode.inventory_complete) ++recorder->incomplete_count;
    recorder->last_action_end_ns = now_ns;
    recorder->episode = (struct native_tab_episode) {0};
    return true;
}

static bool native_tab_episode_update(struct native_tab_recorder *recorder, uint64_t now_ns)
{
    if (recorder->episode.state != NATIVE_TAB_EPISODE_COLLECTING) return true;
    ++recorder->sequence;
    if (fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v2\",\"redaction\":\"strict-v1\","
        "\"type\":\"episode-update\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"scenario\":\"%s\",\"disappeared\":",
        (unsigned long long) recorder->sequence,
        (unsigned long long) native_tab_elapsed_ms(recorder, now_ns), recorder->scenario) < 0 ||
        !native_tab_write_ordinal_array(recorder, recorder->episode.disappeared,
                                        recorder->episode.disappeared_count) ||
        fputs(",\"appeared\":", recorder->stream) == EOF ||
        !native_tab_write_ordinal_array(recorder, recorder->episode.appeared,
                                        recorder->episode.appeared_count) ||
        fputs(",\"evidence\":", recorder->stream) == EOF ||
        !native_tab_write_evidence(recorder) || fputc('}', recorder->stream) == EOF ||
        !native_tab_finish_record(recorder)) return false;
    return true;
}

static bool native_tab_capacity_check(struct native_tab_recorder *recorder, uint64_t now_ns)
{
    return !recorder->episode.overflowed || native_tab_resolution(recorder, now_ns, "capacity-exceeded");
}

static bool native_tab_flush_expired_before_event(struct native_tab_recorder *recorder, uint64_t now_ns)
{
    if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING &&
        now_ns > recorder->episode.hard_deadline_ns) {
        return native_tab_resolution(recorder, now_ns, native_tab_timeout_outcome(&recorder->episode));
    }
    return true;
}

bool native_tab_recorder_init(struct native_tab_recorder *recorder, FILE *stream,
                              const char *scenario, uint64_t start_ns,
                              const struct native_tab_recorder_metadata *metadata)
{
    if (!recorder || !stream || !native_tab_scenario_is_valid(scenario) || !metadata ||
        !metadata->os_version || !metadata->os_build || !metadata->architecture ||
        !native_tab_scenario_is_valid(metadata->fixture_token) ||
        !native_tab_scenario_is_valid(metadata->batch_id) ||
        !native_tab_scenario_is_valid(metadata->direction) ||
        (!metadata->expectation ||
         (strcmp(metadata->expectation, "candidate") != 0 &&
          strcmp(metadata->expectation, "negative-control") != 0)) ||
        metadata->planned_repeat < 1 || metadata->planned_repeat > 5) return false;
    memset(recorder, 0, sizeof(*recorder));
    recorder->stream = stream;
    recorder->start_ns = start_ns;
    snprintf(recorder->scenario, sizeof(recorder->scenario), "%s", scenario);
    snprintf(recorder->fixture_token, sizeof(recorder->fixture_token), "%s", metadata->fixture_token);
    snprintf(recorder->batch_id, sizeof(recorder->batch_id), "%s", metadata->batch_id);
    snprintf(recorder->direction, sizeof(recorder->direction), "%s", metadata->direction);
    snprintf(recorder->expectation, sizeof(recorder->expectation), "%s", metadata->expectation);
    recorder->planned_repeat = metadata->planned_repeat;
    recorder->sequence = 1;
    recorder->next_episode_id = 1;
    recorder->last_inventory_change_ns = start_ns;
    recorder->initialized = true;
    if (fprintf(stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v2\",\"redaction\":\"strict-v1\","
        "\"type\":\"session\",\"seq\":1,\"elapsed_ms\":0,\"scenario\":\"%s\","
        "\"fixture_token\":\"%s\",\"batch_id\":\"%s\",\"direction\":\"%s\","
        "\"expectation\":\"%s\",\"planned_repeat\":%u,"
        "\"os_version\":\"%s\",\"os_build\":\"%s\",\"architecture\":\"%s\","
        "\"poll_ms\":100,\"hard_ms\":1000,\"action_separation_ms\":600,"
        "\"episode_capacity\":8,\"pair_capacity\":64,"
        "\"inventory_capacity\":256,\"ax_available\":%s,"
        "\"production_identity_mutation\":false}\n",
        recorder->scenario, recorder->fixture_token, recorder->batch_id, recorder->direction,
        recorder->expectation, recorder->planned_repeat,
        metadata->os_version, metadata->os_build, metadata->architecture,
        metadata->ax_available ? "true" : "false") < 0 || fflush(stream) != 0) return false;
    return true;
}

static bool native_tab_write_inventory(struct native_tab_recorder *recorder, uint64_t now_ns,
                                       const uint32_t *removed, uint8_t removed_count,
                                       const uint32_t *added, uint8_t added_count,
                                       bool complete)
{
    ++recorder->sequence;
    if (fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v2\",\"redaction\":\"strict-v1\","
        "\"type\":\"inventory\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"scenario\":\"%s\",\"complete\":%s,\"disappeared\":",
        (unsigned long long) recorder->sequence,
        (unsigned long long) native_tab_elapsed_ms(recorder, now_ns), recorder->scenario,
        complete ? "true" : "false") < 0 ||
        !native_tab_write_ordinal_array(recorder, removed, removed_count) ||
        fputs(",\"appeared\":", recorder->stream) == EOF ||
        !native_tab_write_ordinal_array(recorder, added, added_count) ||
        fputc('}', recorder->stream) == EOF ||
        !native_tab_finish_record(recorder)) return false;
    return true;
}

bool native_tab_recorder_baseline(struct native_tab_recorder *recorder, uint64_t now_ns,
                                  const struct native_tab_inventory *inventory)
{
    if (!recorder || !recorder->initialized || recorder->stopped || !inventory ||
        inventory->count > NATIVE_TAB_INVENTORY_CAPACITY) return false;
    for (size_t i = 0; i < inventory->count; ++i) {
        native_tab_record_fact(recorder, inventory->windows + i);
        struct native_tab_ordinal_entry *entry = native_tab_ordinal_entry(
            recorder, inventory->windows[i].window_id, true);
        if (entry) {
            entry->ever_observed = true;
            entry->currently_present = true;
            if (!entry->appearance_count) entry->appearance_count = 1;
        }
    }
    recorder->known = *inventory;
    return native_tab_write_inventory(recorder, now_ns, NULL, 0, NULL, 0, inventory->complete);
}

bool native_tab_recorder_inventory(struct native_tab_recorder *recorder, uint64_t now_ns,
                                   const struct native_tab_inventory *inventory)
{
    if (!recorder || !recorder->initialized || recorder->stopped || !inventory ||
        inventory->count > NATIVE_TAB_INVENTORY_CAPACITY ||
        !native_tab_flush_expired_before_event(recorder, now_ns)) return false;

    uint32_t removed[NATIVE_TAB_EPISODE_CAPACITY] = {0};
    uint32_t added[NATIVE_TAB_EPISODE_CAPACITY] = {0};
    uint8_t removed_count = 0, added_count = 0;
    bool overflow = false;
    for (size_t i = 0; i < inventory->count; ++i) native_tab_record_fact(recorder, inventory->windows + i);
    if (inventory->complete && recorder->known.complete) {
        for (size_t i = 0; i < recorder->known.count; ++i) {
            uint32_t window_id = recorder->known.windows[i].window_id;
            if (!native_tab_inventory_contains(inventory, window_id)) {
                (void) native_tab_ordinal_entry(recorder, window_id, true);
                if (removed_count < NATIVE_TAB_EPISODE_CAPACITY) removed[removed_count++] = window_id;
                else overflow = true;
            }
        }
        for (size_t i = 0; i < inventory->count; ++i) {
            uint32_t window_id = inventory->windows[i].window_id;
            if (!native_tab_inventory_contains(&recorder->known, window_id)) {
                if (added_count < NATIVE_TAB_EPISODE_CAPACITY) added[added_count++] = window_id;
                else overflow = true;
            }
        }
    }

    if (!native_tab_write_inventory(recorder, now_ns, removed, removed_count,
                                    added, added_count, inventory->complete)) return false;
    bool delta = removed_count || added_count;
    if (delta) {
        native_tab_episode_begin(recorder, now_ns);
        recorder->last_inventory_change_ns = now_ns;
    }
    bool same_episode_return = false;
    if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING) {
        recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_INVENTORY;
        recorder->episode.inventory_complete &= inventory->complete && !overflow;
        if (delta) recorder->episode.stable_inventory_polls = 0;
        else if (inventory->complete && recorder->episode.stable_inventory_polls < UINT8_MAX) {
            ++recorder->episode.stable_inventory_polls;
        }
        for (uint8_t i = 0; i < removed_count; ++i) {
            struct native_tab_ordinal_entry *entry = native_tab_ordinal_entry(recorder, removed[i], true);
            if (entry) entry->currently_present = false;
            if (!native_tab_episode_add(recorder, false, removed[i], now_ns)) overflow = true;
        }
        for (uint8_t i = 0; i < added_count; ++i) {
            struct native_tab_ordinal_entry *entry = native_tab_ordinal_entry(recorder, added[i], true);
            bool reappeared = entry && entry->ever_observed && !entry->currently_present;
            if (reappeared) recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_KNOWN_REAPPEAR;
            if (native_tab_episode_contains(recorder->episode.disappeared,
                                            recorder->episode.disappeared_count, added[i])) {
                same_episode_return = true;
            }
            if (entry) {
                entry->ever_observed = true;
                entry->currently_present = true;
                if (entry->appearance_count != UINT32_MAX) ++entry->appearance_count;
            }
            if (!native_tab_episode_add(recorder, true, added[i], now_ns)) overflow = true;
        }
        if (overflow) recorder->episode.overflowed = true;
    }
    if (inventory->complete) recorder->known = *inventory;
    if (!native_tab_capacity_check(recorder, now_ns)) return false;
    (void) same_episode_return;
    if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING &&
        recorder->episode.stable_inventory_polls >= 2 &&
        now_ns >= recorder->episode.settle_deadline_ns) {
        return native_tab_resolution(recorder, now_ns, native_tab_timeout_outcome(&recorder->episode));
    }
    return native_tab_episode_update(recorder, now_ns);
}

static bool native_tab_write_window_ordinal(struct native_tab_recorder *recorder,
                                            uint32_t window_id)
{
    const struct native_tab_ordinal_entry *entry = window_id
        ? native_tab_ordinal_entry_const(recorder, window_id) : NULL;
    return entry ? fprintf(recorder->stream, "%u", entry->ordinal) >= 0
                 : fputs("null", recorder->stream) != EOF;
}

static bool native_tab_event_payload_valid(const struct native_tab_event *event)
{
    return event->payload_valid || event->sls_code == 0;
}

static const char *native_tab_event_readback_disposition(const struct native_tab_event *event)
{
    if (event->type != NATIVE_TAB_EVENT_SLS_VISIBLE &&
        event->type != NATIVE_TAB_EVENT_SLS_INVISIBLE) return "not-applicable";
    if (!native_tab_event_payload_valid(event)) return "payload-invalid";
    if (!event->ordered_in_known) return "readback-unknown";
    bool expected = event->type == NATIVE_TAB_EVENT_SLS_VISIBLE;
    return event->ordered_in == expected ? "readback-confirmed" : "readback-conflict";
}

static bool native_tab_write_event(struct native_tab_recorder *recorder,
                                   const struct native_tab_event *event)
{
    ++recorder->sequence;
    if (fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v2\",\"redaction\":\"strict-v1\","
        "\"type\":\"event\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"scenario\":\"%s\",\"episode_id\":%u,"
        "\"event\":\"%s\",\"window_ordinal\":",
        (unsigned long long) recorder->sequence,
        (unsigned long long) native_tab_elapsed_ms(recorder, event->time_ns),
        recorder->scenario,
        recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING
            ? recorder->episode.episode_id : 0,
        native_tab_event_name(event->type)) < 0 ||
        !native_tab_write_window_ordinal(recorder, event->window_id)) return false;
    if (fputs(",\"ordered_in\":", recorder->stream) == EOF ||
        !native_tab_write_nullable_bool(recorder->stream, event->ordered_in_known,
                                        event->ordered_in) ||
        fprintf(recorder->stream,
                ",\"sls_code\":%u,\"payload_length\":%u,\"payload_valid\":%s,"
                "\"delivered_index\":%u,\"duplicate\":%s,"
                "\"readback_disposition\":\"%s\","
                "\"subscription_generation\":%u,",
                event->sls_code, event->payload_length,
                native_tab_event_payload_valid(event) ? "true" : "false", event->delivered_index,
                event->duplicate ? "true" : "false",
                native_tab_event_readback_disposition(event),
                event->subscription_generation) < 0 ||
        fputs("\"subscribed_before_delivery\":", recorder->stream) == EOF ||
        !native_tab_write_nullable_bool(recorder->stream, event->subscribed_before_known,
                                        event->subscribed_before) ||
        fputs(",\"target_pid\":", recorder->stream) == EOF ||
        !native_tab_write_nullable_bool(recorder->stream, event->target_pid_known,
                                        event->target_pid) ||
        fputs(",\"focus_subject_ordinal\":", recorder->stream) == EOF ||
        !native_tab_write_window_ordinal(recorder,
                                         event->focus_subject_known
                                            ? event->focus_subject_window_id : 0) ||
        fputs(",\"app_focus_ordinal\":", recorder->stream) == EOF ||
        !native_tab_write_window_ordinal(recorder,
                                         event->app_focus_known
                                            ? event->app_focus_window_id : 0) ||
        fputs(",\"main_window_ordinal\":", recorder->stream) == EOF ||
        !native_tab_write_window_ordinal(recorder,
                                         event->main_window_known
                                            ? event->main_window_id : 0) ||
        fputc('}', recorder->stream) == EOF) return false;
    return native_tab_finish_record(recorder);
}

bool native_tab_recorder_event(struct native_tab_recorder *recorder,
                               const struct native_tab_event *event)
{
    if (!recorder || !recorder->initialized || recorder->stopped || !event ||
        !native_tab_flush_expired_before_event(recorder, event->time_ns)) return false;
    if (event->has_fact) native_tab_record_fact(recorder, &event->fact);
    const uint32_t related[] = {
        event->window_id, event->focus_subject_window_id,
        event->app_focus_window_id, event->main_window_id
    };
    for (size_t i = 0; i < sizeof(related) / sizeof(*related); ++i) {
        if (related[i]) (void) native_tab_ordinal_entry(recorder, related[i], true);
    }

    bool causal = event->type == NATIVE_TAB_EVENT_SLS_VISIBLE ||
                  event->type == NATIVE_TAB_EVENT_SLS_INVISIBLE ||
                  event->type == NATIVE_TAB_EVENT_SLS_DESTROYED ||
                  event->type == NATIVE_TAB_EVENT_AX_CREATED ||
                  event->type == NATIVE_TAB_EVENT_AX_DESTROYED ||
                  event->type == NATIVE_TAB_EVENT_AX_MINIMIZED ||
                  event->type == NATIVE_TAB_EVENT_AX_DEMINIMIZED ||
                  event->type == NATIVE_TAB_EVENT_APP_HIDDEN ||
                  event->type == NATIVE_TAB_EVENT_APP_VISIBLE ||
                  event->type == NATIVE_TAB_EVENT_APP_TERMINATED ||
                  event->type == NATIVE_TAB_EVENT_SPACE_CHANGED ||
                  event->type == NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER;
    if (causal) {
        native_tab_episode_begin(recorder, event->time_ns);
        recorder->episode.last_event_ns = event->time_ns;
        recorder->episode.settle_deadline_ns = event->time_ns + NATIVE_TAB_ACTION_SEPARATION_NS;
    }

    if (event->type == NATIVE_TAB_EVENT_SLS_VISIBLE ||
        event->type == NATIVE_TAB_EVENT_SLS_INVISIBLE) {
        struct native_tab_delivery_counts *session = event->type == NATIVE_TAB_EVENT_SLS_VISIBLE
            ? &recorder->session_815 : &recorder->session_816;
        struct native_tab_delivery_counts *episode = event->type == NATIVE_TAB_EVENT_SLS_VISIBLE
            ? &recorder->episode.sls_815 : &recorder->episode.sls_816;
        ++session->delivered; ++episode->delivered;
        if (!native_tab_event_payload_valid(event)) {
            ++session->payload_invalid; ++episode->payload_invalid;
        } else if (!event->ordered_in_known) {
            ++session->readback_unknown; ++episode->readback_unknown;
        } else {
            bool expected = event->type == NATIVE_TAB_EVENT_SLS_VISIBLE;
            if (event->ordered_in == expected) {
                ++session->readback_confirmed; ++episode->readback_confirmed;
                uint64_t next_sequence = recorder->sequence + 1;
                if (event->type == NATIVE_TAB_EVENT_SLS_VISIBLE &&
                    !recorder->episode.first_confirmed_815_sequence) {
                    recorder->episode.first_confirmed_815_sequence = next_sequence;
                    recorder->episode.first_confirmed_815_ns = event->time_ns;
                } else if (event->type == NATIVE_TAB_EVENT_SLS_INVISIBLE &&
                           !recorder->episode.first_confirmed_816_sequence) {
                    recorder->episode.first_confirmed_816_sequence = next_sequence;
                    recorder->episode.first_confirmed_816_ns = event->time_ns;
                }
            } else {
                ++session->readback_conflict; ++episode->readback_conflict;
                recorder->episode.readback_conflict = true;
            }
        }
        if (event->duplicate) {
            ++session->duplicate; ++episode->duplicate;
        }
        recorder->episode.evidence |= event->type == NATIVE_TAB_EVENT_SLS_VISIBLE
                                    ? NATIVE_TAB_EVIDENCE_SLS_815
                                    : NATIVE_TAB_EVIDENCE_SLS_816;
        if (event->window_id && !native_tab_recorder_window_known(recorder, event->window_id) &&
            (!event->target_pid_known || event->target_pid) &&
            !native_tab_episode_contains(recorder->episode.outside_candidates,
                                         recorder->episode.outside_candidate_count,
                                         event->window_id)) {
            if (recorder->episode.outside_candidate_count < NATIVE_TAB_EPISODE_CAPACITY) {
                recorder->episode.outside_candidates[recorder->episode.outside_candidate_count++] =
                    event->window_id;
            } else {
                recorder->episode.overflowed = true;
            }
        }
    }

    if (!native_tab_write_event(recorder, event)) return false;

    switch (event->type) {
    case NATIVE_TAB_EVENT_SLS_DESTROYED:
        recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_SLS_804;
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_SLS_DESTROYED;
        break;
    case NATIVE_TAB_EVENT_AX_CREATED:
        recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_AX_CREATED;
        break;
    case NATIVE_TAB_EVENT_AX_DESTROYED:
        recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_AX_DESTROYED;
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_AX_DESTROYED;
        break;
    case NATIVE_TAB_EVENT_AX_MINIMIZED:
        recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_AX_MINIMIZED;
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_MINIMIZE;
        break;
    case NATIVE_TAB_EVENT_AX_DEMINIMIZED:
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_DEMINIMIZE;
        break;
    case NATIVE_TAB_EVENT_APP_HIDDEN:
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_APP_HIDE;
        break;
    case NATIVE_TAB_EVENT_APP_VISIBLE:
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_APP_UNHIDE;
        break;
    case NATIVE_TAB_EVENT_APP_TERMINATED:
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_APP_TERMINATE;
        break;
    case NATIVE_TAB_EVENT_SPACE_CHANGED:
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_SPACE_CHANGE;
        break;
    case NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER:
        recorder->episode.lifecycle_exclusions |= NATIVE_TAB_EXCLUSION_MISSION_CONTROL;
        break;
    case NATIVE_TAB_EVENT_SLS_VISIBLE:
    case NATIVE_TAB_EVENT_SLS_INVISIBLE:
    case NATIVE_TAB_EVENT_AX_FOCUSED:
    case NATIVE_TAB_EVENT_AX_FOCUS_STABLE:
        break;
    }
    return native_tab_capacity_check(recorder, event->time_ns);
}

bool native_tab_recorder_settle_due(const struct native_tab_recorder *recorder,
                                    uint64_t now_ns)
{
    return recorder && recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING &&
           recorder->episode.settle_deadline_ns && now_ns >= recorder->episode.settle_deadline_ns;
}

bool native_tab_recorder_hard_due(const struct native_tab_recorder *recorder,
                                  uint64_t now_ns)
{
    return recorder && recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING &&
           now_ns > recorder->episode.hard_deadline_ns;
}

bool native_tab_recorder_tick(struct native_tab_recorder *recorder, uint64_t now_ns)
{
    if (!recorder || !recorder->initialized || recorder->stopped) return false;
    if (native_tab_recorder_hard_due(recorder, now_ns)) {
        return native_tab_resolution(recorder, now_ns, native_tab_timeout_outcome(&recorder->episode));
    }
    if (native_tab_recorder_settle_due(recorder, now_ns)) {
        return native_tab_episode_update(recorder, now_ns);
    }
    return true;
}

bool native_tab_recorder_stop(struct native_tab_recorder *recorder, uint64_t now_ns)
{
    if (!recorder || !recorder->initialized || recorder->stopped) return false;
    if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING &&
        !native_tab_resolution(recorder, now_ns, "recorder-stop")) return false;
    ++recorder->sequence;
    int summary_written = fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v2\",\"redaction\":\"strict-v1\","
        "\"type\":\"summary\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"scenario\":\"%s\",\"resolutions\":%u,\"gate_eligible\":%u,"
        "\"inventory_incomplete\":%u,\"dropped_records\":%u,"
        "\"sls_815_delivered\":%u,\"sls_816_delivered\":%u,"
        "\"production_identity_mutation\":false}\n",
        (unsigned long long) recorder->sequence,
        (unsigned long long) native_tab_elapsed_ms(recorder, now_ns), recorder->scenario,
        recorder->resolution_count, recorder->gate_eligible_count,
        recorder->incomplete_count, recorder->dropped_count,
        recorder->session_815.delivered, recorder->session_816.delivered);
    int summary_flush = fflush(recorder->stream);
    if (summary_written < 0 || summary_flush != 0) return false;
    recorder->stopped = true;
    return true;
}
