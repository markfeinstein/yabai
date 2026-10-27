#include "native_tab_event_recorder_core.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
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
    recorder->episode = (struct native_tab_episode) {
        .state = NATIVE_TAB_EPISODE_COLLECTING,
        .first_event_ns = now_ns,
        .last_event_ns = now_ns,
        .settle_deadline_ns = now_ns + NATIVE_TAB_SETTLE_DELAY_NS,
        .hard_deadline_ns = now_ns + NATIVE_TAB_HARD_LIFETIME_NS,
        .inventory_complete = true
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
    recorder->episode.settle_deadline_ns = now_ns + NATIVE_TAB_SETTLE_DELAY_NS;
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
        "\"inventory_complete\":%s}",
        evidence & NATIVE_TAB_EVIDENCE_SLS_816 ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_SLS_815 ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_SLS_804 ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_AX_CREATED ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_AX_DESTROYED ? "true" : "false",
        evidence & NATIVE_TAB_EVIDENCE_AX_MINIMIZED ? "true" : "false",
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

static bool native_tab_write_comparison(struct native_tab_recorder *recorder)
{
    if (fputs("{\"shared_space\":", recorder->stream) == EOF) return false;
    if (recorder->episode.disappeared_count != 1 || recorder->episode.appeared_count != 1) {
        return fputs("null,\"root_compatible\":null,\"frame_delta_8pt\":null,"
                     "\"level_delta\":null,\"sublevel_delta\":null,\"order_delta\":null,"
                     "\"same_title_token\":null}", recorder->stream) != EOF;
    }

    const struct native_tab_ordinal_entry *old_entry = native_tab_ordinal_entry_const(
        recorder, recorder->episode.disappeared[0]);
    const struct native_tab_ordinal_entry *new_entry = native_tab_ordinal_entry_const(
        recorder, recorder->episode.appeared[0]);
    const struct native_tab_window_fact *old_fact = old_entry && old_entry->has_fact ? &old_entry->fact : NULL;
    const struct native_tab_window_fact *new_fact = new_entry && new_entry->has_fact ? &new_entry->fact : NULL;
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

    if (fputs(",\"level_delta\":", recorder->stream) == EOF) return false;
    bool level_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_LEVEL) && (new_fact->validity & NATIVE_TAB_FACT_LEVEL);
    if (!native_tab_write_nullable_int(recorder->stream, level_known,
                                       level_known ? new_fact->level - old_fact->level : 0)) return false;

    if (fputs(",\"sublevel_delta\":", recorder->stream) == EOF) return false;
    bool sublevel_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_SUBLEVEL) && (new_fact->validity & NATIVE_TAB_FACT_SUBLEVEL);
    if (!native_tab_write_nullable_int(recorder->stream, sublevel_known,
                                       sublevel_known ? new_fact->sublevel - old_fact->sublevel : 0)) return false;

    if (fputs(",\"order_delta\":", recorder->stream) == EOF) return false;
    bool order_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_ORDER) && (new_fact->validity & NATIVE_TAB_FACT_ORDER);
    if (!native_tab_write_nullable_int(recorder->stream, order_known,
                                       order_known ? new_fact->order - old_fact->order : 0)) return false;

    if (fputs(",\"same_title_token\":", recorder->stream) == EOF) return false;
    bool title_known = old_fact && new_fact &&
        (old_fact->validity & NATIVE_TAB_FACT_TITLE) && (new_fact->validity & NATIVE_TAB_FACT_TITLE);
    if (!native_tab_write_nullable_bool(recorder->stream, title_known,
                                        title_known && old_fact->title_token == new_fact->title_token)) return false;
    return fputc('}', recorder->stream) != EOF;
}

static const char *native_tab_timeout_outcome(const struct native_tab_episode *episode)
{
    if (!episode->inventory_complete) return "inventory-incomplete";
    if (episode->disappeared_count == 1 && episode->appeared_count == 1) return "unexplained-one-to-one";
    if (episode->disappeared_count == 1 && episode->appeared_count > 1) return "unexplained-one-to-many";
    if (episode->disappeared_count > 1 && episode->appeared_count == 1) return "unexplained-many-to-one";
    if (episode->disappeared_count > 1 && episode->appeared_count > 1) return "unexplained-many-to-many";
    if (episode->disappeared_count) return "disappearance-only";
    if (episode->appeared_count) return "appearance-only";
    return "inventory-incomplete";
}

static bool native_tab_resolution(struct native_tab_recorder *recorder, uint64_t now_ns,
                                  const char *outcome)
{
    if (recorder->episode.state != NATIVE_TAB_EPISODE_COLLECTING) return true;
    ++recorder->sequence;
    if (fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v1\",\"redaction\":\"strict-v1\","
        "\"type\":\"resolution\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"scenario\":\"%s\",\"outcome\":\"%s\",\"owner_ordinal\":0,"
        "\"disappeared\":",
        (unsigned long long) recorder->sequence,
        (unsigned long long) native_tab_elapsed_ms(recorder, now_ns),
        recorder->scenario, outcome) < 0 ||
        !native_tab_write_ordinal_array(recorder, recorder->episode.disappeared,
                                        recorder->episode.disappeared_count) ||
        fputs(",\"appeared\":", recorder->stream) == EOF ||
        !native_tab_write_ordinal_array(recorder, recorder->episode.appeared,
                                        recorder->episode.appeared_count) ||
        fputs(",\"evidence\":", recorder->stream) == EOF ||
        !native_tab_write_evidence(recorder) ||
        fputs(",\"comparison\":", recorder->stream) == EOF ||
        !native_tab_write_comparison(recorder) ||
        fputc('}', recorder->stream) == EOF ||
        !native_tab_finish_record(recorder)) return false;
    ++recorder->resolution_count;
    if (!recorder->episode.inventory_complete) ++recorder->incomplete_count;
    recorder->episode = (struct native_tab_episode) {0};
    return true;
}

static bool native_tab_episode_update(struct native_tab_recorder *recorder, uint64_t now_ns)
{
    if (recorder->episode.state != NATIVE_TAB_EPISODE_COLLECTING) return true;
    ++recorder->sequence;
    if (fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v1\",\"redaction\":\"strict-v1\","
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
    recorder->episode.settle_deadline_ns = 0;
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
        !metadata->os_version || !metadata->os_build || !metadata->architecture) return false;
    memset(recorder, 0, sizeof(*recorder));
    recorder->stream = stream;
    recorder->start_ns = start_ns;
    snprintf(recorder->scenario, sizeof(recorder->scenario), "%s", scenario);
    recorder->sequence = 1;
    recorder->initialized = true;
    if (fprintf(stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v1\",\"redaction\":\"strict-v1\","
        "\"type\":\"session\",\"seq\":1,\"elapsed_ms\":0,\"scenario\":\"%s\","
        "\"os_version\":\"%s\",\"os_build\":\"%s\",\"architecture\":\"%s\","
        "\"settle_ms\":100,\"hard_ms\":1000,\"episode_capacity\":8,"
        "\"inventory_capacity\":256,\"ax_available\":%s}\n",
        recorder->scenario, metadata->os_version, metadata->os_build, metadata->architecture,
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
        "{\"schema\":\"yabai.native-tab-event-trace/v1\",\"redaction\":\"strict-v1\","
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
    for (size_t i = 0; i < inventory->count; ++i) native_tab_record_fact(recorder, inventory->windows + i);
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
    if (removed_count || added_count) native_tab_episode_begin(recorder, now_ns);
    if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING) {
        recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_INVENTORY;
        recorder->episode.inventory_complete &= inventory->complete && !overflow;
        for (uint8_t i = 0; i < removed_count; ++i) {
            if (!native_tab_episode_add(recorder, false, removed[i], now_ns)) overflow = true;
        }
        for (uint8_t i = 0; i < added_count; ++i) {
            if (!native_tab_episode_add(recorder, true, added[i], now_ns)) overflow = true;
        }
        if (overflow) recorder->episode.overflowed = true;
    }
    if (inventory->complete) recorder->known = *inventory;
    return native_tab_capacity_check(recorder, now_ns) && native_tab_episode_update(recorder, now_ns);
}

static bool native_tab_write_event(struct native_tab_recorder *recorder,
                                   const struct native_tab_event *event)
{
    const struct native_tab_ordinal_entry *entry = event->window_id
        ? native_tab_ordinal_entry_const(recorder, event->window_id) : NULL;
    ++recorder->sequence;
    if (fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v1\",\"redaction\":\"strict-v1\","
        "\"type\":\"event\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"scenario\":\"%s\",\"event\":\"%s\",\"window_ordinal\":",
        (unsigned long long) recorder->sequence,
        (unsigned long long) native_tab_elapsed_ms(recorder, event->time_ns),
        recorder->scenario, native_tab_event_name(event->type)) < 0) return false;
    if (entry) {
        if (fprintf(recorder->stream, "%u", entry->ordinal) < 0) return false;
    } else if (fputs("null", recorder->stream) == EOF) return false;
    if (fputs(",\"ordered_in\":", recorder->stream) == EOF ||
        !native_tab_write_nullable_bool(recorder->stream, event->ordered_in_known,
                                        event->ordered_in) ||
        fputc('}', recorder->stream) == EOF) return false;
    return native_tab_finish_record(recorder);
}

bool native_tab_recorder_event(struct native_tab_recorder *recorder,
                               const struct native_tab_event *event)
{
    if (!recorder || !recorder->initialized || recorder->stopped || !event ||
        !native_tab_flush_expired_before_event(recorder, event->time_ns)) return false;
    if (event->has_fact) native_tab_record_fact(recorder, &event->fact);
    if (event->window_id) (void) native_tab_ordinal_entry(recorder, event->window_id, true);
    if (!native_tab_write_event(recorder, event)) return false;

    switch (event->type) {
    case NATIVE_TAB_EVENT_SLS_VISIBLE:
        if (event->ordered_in_known && event->ordered_in) {
            if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING &&
                native_tab_episode_contains(recorder->episode.disappeared,
                                            recorder->episode.disappeared_count,
                                            event->window_id)) {
                recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_SLS_815;
                return native_tab_resolution(recorder, event->time_ns, "same-id-reappeared");
            }
            if (!native_tab_recorder_window_known(recorder, event->window_id)) {
                if (!native_tab_episode_add(recorder, true, event->window_id, event->time_ns)) {
                    recorder->episode.overflowed = true;
                }
                recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_SLS_815;
            }
        }
        break;
    case NATIVE_TAB_EVENT_SLS_INVISIBLE:
        if (event->ordered_in_known && !event->ordered_in &&
            native_tab_recorder_window_known(recorder, event->window_id)) {
            native_tab_episode_begin(recorder, event->time_ns);
            recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_SLS_816;
            if (!native_tab_episode_add(recorder, false, event->window_id, event->time_ns)) {
                recorder->episode.overflowed = true;
            }
        }
        break;
    case NATIVE_TAB_EVENT_SLS_DESTROYED:
        if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING) {
            recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_SLS_804;
            return native_tab_resolution(recorder, event->time_ns, "explained-sls-destroy");
        }
        break;
    case NATIVE_TAB_EVENT_AX_CREATED:
        native_tab_episode_begin(recorder, event->time_ns);
        recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_AX_CREATED;
        if (event->window_id && !native_tab_recorder_window_known(recorder, event->window_id) &&
            !native_tab_episode_add(recorder, true, event->window_id, event->time_ns)) {
            recorder->episode.overflowed = true;
        }
        recorder->episode.last_event_ns = event->time_ns;
        recorder->episode.settle_deadline_ns = event->time_ns + NATIVE_TAB_SETTLE_DELAY_NS;
        break;
    case NATIVE_TAB_EVENT_AX_DESTROYED:
        if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING) {
            recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_AX_DESTROYED;
            return native_tab_resolution(recorder, event->time_ns, "explained-ax-destroy");
        }
        break;
    case NATIVE_TAB_EVENT_AX_MINIMIZED:
        if (recorder->episode.state == NATIVE_TAB_EPISODE_COLLECTING) {
            recorder->episode.evidence |= NATIVE_TAB_EVIDENCE_AX_MINIMIZED;
            return native_tab_resolution(recorder, event->time_ns, "explained-minimize");
        }
        break;
    case NATIVE_TAB_EVENT_APP_HIDDEN:
        return native_tab_resolution(recorder, event->time_ns, "explained-app-hide");
    case NATIVE_TAB_EVENT_APP_TERMINATED:
        return native_tab_resolution(recorder, event->time_ns, "explained-app-termination");
    case NATIVE_TAB_EVENT_SPACE_CHANGED:
        return native_tab_resolution(recorder, event->time_ns, "explained-space-transition");
    case NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER:
        return native_tab_resolution(recorder, event->time_ns, "explained-mission-control");
    case NATIVE_TAB_EVENT_AX_DEMINIMIZED:
    case NATIVE_TAB_EVENT_AX_FOCUSED:
    case NATIVE_TAB_EVENT_APP_VISIBLE:
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
    if (fprintf(recorder->stream,
        "{\"schema\":\"yabai.native-tab-event-trace/v1\",\"redaction\":\"strict-v1\","
        "\"type\":\"summary\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"scenario\":\"%s\",\"resolutions\":%u,\"inventory_incomplete\":%u,"
        "\"dropped_records\":%u}\n",
        (unsigned long long) recorder->sequence,
        (unsigned long long) native_tab_elapsed_ms(recorder, now_ns), recorder->scenario,
        recorder->resolution_count, recorder->incomplete_count, recorder->dropped_count) < 0 ||
        fflush(recorder->stream) != 0) return false;
    recorder->stopped = true;
    return true;
}
