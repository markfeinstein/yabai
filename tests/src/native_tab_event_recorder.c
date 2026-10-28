#include "../../scripts/native_tab_event_recorder_core.c"

struct native_tab_test_trace
{
    struct native_tab_recorder recorder;
    char *output;
    size_t length;
    FILE *stream;
    uint64_t start;
};

static struct native_tab_window_fact native_tab_test_fact(
    uint32_t window_id, uint32_t parent, uint64_t space, uint64_t title,
    double x, double y, double width, double height, int order)
{
    return (struct native_tab_window_fact) {
        .window_id = window_id,
        .parent_window_id = parent,
        .space_id = space,
        .title_token = title,
        .x = x, .y = y, .width = width, .height = height,
        .level = 0, .sublevel = 0, .order = order,
        .role_token = 1, .subrole_token = 2,
        .ax_member = true,
        .validity = NATIVE_TAB_FACT_PARENT | NATIVE_TAB_FACT_SPACE |
                    NATIVE_TAB_FACT_FRAME | NATIVE_TAB_FACT_LEVEL |
                    NATIVE_TAB_FACT_SUBLEVEL | NATIVE_TAB_FACT_ORDER |
                    NATIVE_TAB_FACT_TITLE | NATIVE_TAB_FACT_ROLE |
                    NATIVE_TAB_FACT_SUBROLE | NATIVE_TAB_FACT_AX |
                    NATIVE_TAB_FACT_STATE
    };
}

static struct native_tab_inventory native_tab_test_inventory(
    const struct native_tab_window_fact *facts, size_t count, bool complete)
{
    struct native_tab_inventory inventory = { .count = count, .complete = complete };
    if (count) memcpy(inventory.windows, facts, count * sizeof(*facts));
    return inventory;
}

static bool native_tab_test_begin(struct native_tab_test_trace *trace, const char *scenario)
{
    memset(trace, 0, sizeof(*trace));
    trace->start = UINT64_C(10000000000);
    trace->stream = open_memstream(&trace->output, &trace->length);
    struct native_tab_recorder_metadata metadata = {
        .os_version = "27.0.1", .os_build = "26A434",
        .architecture = "arm64", .fixture_token = "controlled-tabs",
        .batch_id = "batch-1", .direction = scenario,
        .expectation = "candidate", .planned_repeat = 1,
        .ax_available = true
    };
    return trace->stream && native_tab_recorder_init(
        &trace->recorder, trace->stream, scenario, trace->start, &metadata);
}

static bool native_tab_test_end(struct native_tab_test_trace *trace, uint64_t elapsed_ns)
{
    bool success = native_tab_recorder_stop(&trace->recorder, trace->start + elapsed_ns);
    if (trace->stream && fclose(trace->stream) != 0) success = false;
    trace->stream = NULL;
    return success;
}

static bool native_tab_test_jsonl_is_parseable(const char *output)
{
    if (!output) return false;
    char *copy = strdup(output);
    if (!copy) return false;
    bool valid = true;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        NSData *data = [NSData dataWithBytes:line length:strlen(line)];
        id value = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        valid &= [value isKindOfClass:[NSDictionary class]];
    }
    free(copy);
    return valid;
}

static size_t native_tab_test_count(const char *text, const char *needle)
{
    size_t count = 0, length = strlen(needle);
    for (const char *match = text; (match = strstr(match, needle)); match += length) ++count;
    return count;
}

static struct native_tab_event native_tab_test_sls(
    enum native_tab_event_type type, uint64_t time_ns, uint32_t window_id,
    bool ordered_in, uint32_t delivered_index)
{
    return (struct native_tab_event) {
        .type = type, .time_ns = time_ns, .window_id = window_id,
        .ordered_in_known = true, .ordered_in = ordered_in,
        .sls_code = type == NATIVE_TAB_EVENT_SLS_VISIBLE ? 815 : 816,
        .payload_length = sizeof(uint32_t), .payload_valid = true,
        .delivered_index = delivered_index,
        .subscribed_before_known = true, .subscribed_before = true,
        .target_pid_known = true, .target_pid = true
    };
}

static bool native_tab_test_terminal(
    struct native_tab_test_trace *trace, uint64_t action_ns,
    const struct native_tab_inventory *terminal)
{
    return native_tab_recorder_inventory(&trace->recorder, action_ns, terminal) &&
           native_tab_recorder_inventory(&trace->recorder,
                                         action_ns + UINT64_C(100000000), terminal) &&
           native_tab_recorder_inventory(&trace->recorder,
                                         action_ns + UINT64_C(200000000), terminal) &&
           native_tab_recorder_inventory(&trace->recorder,
                                         action_ns + NATIVE_TAB_ACTION_SEPARATION_NS, terminal);
}

static bool native_tab_test_switch_case(bool visible_first, char **output)
{
    struct native_tab_test_trace trace;
    if (!native_tab_test_begin(&trace, visible_first ? "visible-first" : "invisible-first")) return false;
    struct native_tab_window_fact old_fact = native_tab_test_fact(
        424242, 0, 7, 11, 100, 100, 800, 600, 1);
    struct native_tab_window_fact new_fact = native_tab_test_fact(
        525252, 0, 7, 11, 108, 92, 800, 608, 2);
    struct native_tab_inventory baseline = native_tab_test_inventory(&old_fact, 1, true);
    struct native_tab_inventory terminal = native_tab_test_inventory(&new_fact, 1, true);
    if (!native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline)) return false;
    uint64_t action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
    struct native_tab_event invisible = native_tab_test_sls(
        NATIVE_TAB_EVENT_SLS_INVISIBLE, action + (visible_first ? 10 : 0),
        old_fact.window_id, false, 1);
    invisible.has_fact = true; invisible.fact = old_fact;
    struct native_tab_event visible = native_tab_test_sls(
        NATIVE_TAB_EVENT_SLS_VISIBLE, action + (visible_first ? 0 : 10),
        new_fact.window_id, true, 1);
    visible.has_fact = true; visible.fact = new_fact;
    if (visible_first) {
        if (!native_tab_recorder_event(&trace.recorder, &visible) ||
            !native_tab_recorder_event(&trace.recorder, &invisible)) return false;
    } else if (!native_tab_recorder_event(&trace.recorder, &invisible) ||
               !native_tab_recorder_event(&trace.recorder, &visible)) return false;
    if (!native_tab_test_terminal(&trace, action + UINT64_C(10000000), &terminal) ||
        !native_tab_test_end(&trace, UINT64_C(1300000000))) return false;
    *output = trace.output;
    return true;
}

TEST_FUNC(native_tab_event_recorder_pairs_both_switch_event_orders_without_identity_decision,
{
    for (int visible_first = 0; visible_first < 2; ++visible_first) {
        char *output = NULL;
        TEST_CHECK(native_tab_test_switch_case(visible_first != 0, &output), true);
        TEST_CHECK(output != NULL, true);
        if (output) {
            TEST_CHECK(strstr(output, "\"schema\":\"yabai.native-tab-event-trace/v2\"") != NULL, true);
            TEST_CHECK(strstr(output, "\"outcome\":\"singleton-observational-candidate\"") != NULL, true);
            TEST_CHECK(strstr(output, "\"gate_eligible\":true") != NULL, true);
            TEST_CHECK(strstr(output, visible_first ? "\"event_order\":\"815-before-816\""
                                                    : "\"event_order\":\"816-before-815\"") != NULL, true);
            TEST_CHECK(strstr(output, "\"shared_space\":true") != NULL, true);
            TEST_CHECK(strstr(output, "\"root_compatible\":true") != NULL, true);
            TEST_CHECK(strstr(output, "\"frame_delta_8pt\":[1,-1,0,1]") != NULL, true);
            TEST_CHECK(strstr(output, "\"role_compatible\":true") != NULL, true);
            TEST_CHECK(strstr(output, "\"same_title_token\":true") != NULL, true);
            TEST_CHECK(strstr(output, "production_identity_mutation\":false") != NULL, true);
            TEST_CHECK(strstr(output, "replace_window_identity") == NULL, true);
            TEST_CHECK(strstr(output, "424242") == NULL, true);
            TEST_CHECK(strstr(output, "525252") == NULL, true);
            TEST_CHECK(native_tab_test_jsonl_is_parseable(output), true);
        }
        free(output);
    }
})

TEST_FUNC(native_tab_event_recorder_separates_quiescent_inventory_episodes_and_tracks_reappearance,
{
    struct native_tab_window_fact a = native_tab_test_fact(610001, 0, 7, 1, 0, 0, 100, 100, 0);
    struct native_tab_window_fact b = native_tab_test_fact(610002, 0, 7, 1, 0, 0, 100, 100, 1);
    struct native_tab_test_trace trace;
    struct native_tab_inventory baseline = native_tab_test_inventory(&a, 1, true);
    struct native_tab_inventory terminal = native_tab_test_inventory(&b, 1, true);

    TEST_CHECK(native_tab_test_begin(&trace, "exact-600ms"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    uint64_t action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
    TEST_CHECK(native_tab_test_terminal(&trace, action, &terminal), true);
    TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1300000000)), true);
    TEST_CHECK(strstr(trace.output, "\"gate_eligible\":true") != NULL, true);
    TEST_CHECK(strstr(trace.output, "\"pre_quiet_ms\":600") != NULL, true);
    free(trace.output);

    TEST_CHECK(native_tab_test_begin(&trace, "below-600ms"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS - 1;
    TEST_CHECK(native_tab_test_terminal(&trace, action, &terminal), true);
    TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1300000000)), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"action-separation-violated\"") != NULL, true);
    TEST_CHECK(strstr(trace.output, "\"gate_eligible\":false") != NULL, true);
    free(trace.output);

    TEST_CHECK(native_tab_test_begin(&trace, "same-id-reappeared"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    struct native_tab_inventory empty = native_tab_test_inventory(NULL, 0, true);
    action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
    TEST_CHECK(native_tab_recorder_inventory(&trace.recorder, action, &empty), true);
    TEST_CHECK(native_tab_recorder_inventory(&trace.recorder, action + UINT64_C(100000000), &baseline), true);
    TEST_CHECK(native_tab_recorder_inventory(&trace.recorder, action + UINT64_C(200000000), &baseline), true);
    TEST_CHECK(native_tab_recorder_inventory(&trace.recorder, action + UINT64_C(700000000), &baseline), true);
    TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1400000000)), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"same-id-reappeared\"") != NULL, true);
    TEST_CHECK(strstr(trace.output, "\"known_id_reappeared\":true") != NULL, true);
    free(trace.output);
})

TEST_FUNC(native_tab_event_recorder_classifies_reappearance_detach_and_lifecycle_explanations,
{
    struct native_tab_window_fact old_facts[] = {
        native_tab_test_fact(700001, 0, 9, 1, 0, 0, 100, 100, 0),
        native_tab_test_fact(700002, 0, 9, 2, 110, 0, 100, 100, 1)
    };
    struct native_tab_window_fact new_facts[] = {
        native_tab_test_fact(700003, 0, 9, 3, 0, 0, 100, 100, 0),
        native_tab_test_fact(700004, 0, 9, 4, 110, 0, 100, 100, 1)
    };
    struct native_tab_inventory baseline = native_tab_test_inventory(old_facts, 2, true);
    struct native_tab_inventory terminal = native_tab_test_inventory(new_facts, 2, true);
    struct native_tab_test_trace trace;
    TEST_CHECK(native_tab_test_begin(&trace, "many-to-many"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    uint64_t action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
    TEST_CHECK(native_tab_test_terminal(&trace, action, &terminal), true);
    TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1300000000)), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"unexplained-many-to-many\"") != NULL, true);
    TEST_CHECK(native_tab_test_count(trace.output, "\"disappeared_ordinal\":"), 4U);
    TEST_CHECK(strstr(trace.output, "\"gate_eligible\":false") != NULL, true);
    free(trace.output);

    const enum native_tab_event_type exclusions[] = {
        NATIVE_TAB_EVENT_SLS_DESTROYED, NATIVE_TAB_EVENT_AX_DESTROYED,
        NATIVE_TAB_EVENT_AX_MINIMIZED, NATIVE_TAB_EVENT_AX_DEMINIMIZED,
        NATIVE_TAB_EVENT_APP_HIDDEN, NATIVE_TAB_EVENT_APP_VISIBLE,
        NATIVE_TAB_EVENT_APP_TERMINATED, NATIVE_TAB_EVENT_SPACE_CHANGED,
        NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER
    };
    struct native_tab_inventory one_old = native_tab_test_inventory(old_facts, 1, true);
    struct native_tab_inventory one_new = native_tab_test_inventory(new_facts, 1, true);
    for (size_t i = 0; i < array_count(exclusions); ++i) {
        TEST_CHECK(native_tab_test_begin(&trace, "lifecycle-negative"), true);
        TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &one_old), true);
        action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
        struct native_tab_event lifecycle = { .type = exclusions[i], .time_ns = action + 1,
                                              .window_id = old_facts[0].window_id };
        TEST_CHECK(native_tab_recorder_event(&trace.recorder, &lifecycle), true);
        TEST_CHECK(native_tab_test_terminal(&trace, action + 2, &one_new), true);
        TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1300000000)), true);
        TEST_CHECK(strstr(trace.output, "\"outcome\":\"lifecycle-excluded\"") != NULL, true);
        TEST_CHECK(strstr(trace.output, "\"gate_eligible\":false") != NULL, true);
        free(trace.output);
    }
})

TEST_FUNC(native_tab_event_recorder_enforces_deadlines_stale_events_capacity_and_stop,
{
    struct native_tab_window_fact old_fact = native_tab_test_fact(800001, 0, 1, 1, 0, 0, 100, 100, 0);
    struct native_tab_window_fact new_fact = native_tab_test_fact(800002, 0, 1, 1, 0, 0, 100, 100, 1);
    struct native_tab_inventory baseline = native_tab_test_inventory(&old_fact, 1, true);
    struct native_tab_inventory terminal = native_tab_test_inventory(&new_fact, 1, true);
    struct native_tab_test_trace trace;
    TEST_CHECK(native_tab_test_begin(&trace, "readback-conflict"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    uint64_t action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
    struct native_tab_event conflict = native_tab_test_sls(
        NATIVE_TAB_EVENT_SLS_INVISIBLE, action, old_fact.window_id, true, 1);
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &conflict), true);
    struct native_tab_event visible_conflict = native_tab_test_sls(
        NATIVE_TAB_EVENT_SLS_VISIBLE, action + 1, new_fact.window_id, false, 1);
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &visible_conflict), true);
    TEST_CHECK(native_tab_test_terminal(&trace, action + 2, &terminal), true);
    TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1300000000)), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"readback-conflict\"") != NULL, true);
    TEST_CHECK(native_tab_test_count(trace.output,
        "\"readback_disposition\":\"readback-conflict\""), 2U);
    free(trace.output);

    TEST_CHECK(native_tab_test_begin(&trace, "duplicates"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
    struct native_tab_event duplicate = native_tab_test_sls(
        NATIVE_TAB_EVENT_SLS_INVISIBLE, action, old_fact.window_id, false, 2);
    duplicate.duplicate = true;
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &duplicate), true);
    TEST_CHECK(native_tab_test_terminal(&trace, action + 1, &terminal), true);
    TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1300000000)), true);
    TEST_CHECK(strstr(trace.output, "\"event_order\":\"mixed-or-duplicate-order\"") != NULL, true);
    TEST_CHECK(strstr(trace.output, "\"duplicate\":1") != NULL, true);
    free(trace.output);

    TEST_CHECK(native_tab_test_begin(&trace, "outside"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
    struct native_tab_event outside = native_tab_test_sls(
        NATIVE_TAB_EVENT_SLS_VISIBLE, action, 899999, true, 1);
    outside.target_pid_known = false;
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &outside), true);
    TEST_CHECK(native_tab_recorder_tick(&trace.recorder,
        action + NATIVE_TAB_HARD_LIFETIME_NS + 1), true);
    TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1700000000)), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"unresolved-outside\"") != NULL, true);
    free(trace.output);
})

TEST_FUNC(native_tab_event_recorder_marks_incomplete_inventory_and_secures_output,
{
    struct native_tab_window_fact old_fact = native_tab_test_fact(990001, 0, 1, 1, 0, 0, 100, 100, 0);
    struct native_tab_inventory baseline = native_tab_test_inventory(&old_fact, 1, true);
    struct native_tab_test_trace trace;
    TEST_CHECK(native_tab_test_begin(&trace, "incomplete"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    uint64_t action = trace.start + NATIVE_TAB_ACTION_SEPARATION_NS;
    struct native_tab_event event = native_tab_test_sls(
        NATIVE_TAB_EVENT_SLS_INVISIBLE, action, old_fact.window_id, false, 1);
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    struct native_tab_inventory incomplete = { .complete = false };
    TEST_CHECK(native_tab_recorder_inventory(&trace.recorder, action + 1, &incomplete), true);
    TEST_CHECK(native_tab_recorder_tick(&trace.recorder,
        action + NATIVE_TAB_HARD_LIFETIME_NS + 1), true);
    TEST_CHECK(native_tab_test_end(&trace, UINT64_C(1700000000)), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"inventory-incomplete\"") != NULL, true);
    TEST_CHECK(strstr(trace.output, "\"inventory_complete\":false") != NULL, true);
    TEST_CHECK(native_tab_test_jsonl_is_parseable(trace.output), true);
    free(trace.output);

    TEST_CHECK(native_tab_scenario_is_valid("active-to-inactive"), true);
    TEST_CHECK(native_tab_scenario_is_valid("bad/value"), false);
    char root[] = "/private/tmp/yabai-native-tab-test.XXXXXX";
    TEST_CHECK(mkdtemp(root) != NULL, true);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/trace.jsonl", root);
    FILE *stream = NULL;
    TEST_CHECK(native_tab_trace_open(path, &stream), true);
    struct stat status;
    TEST_CHECK(stat(path, &status), 0);
    TEST_CHECK(status.st_mode & 0777, 0600);
    TEST_CHECK(native_tab_trace_close(stream, path, true), true);
    TEST_CHECK(native_tab_trace_open(path, &stream), false);
    unlink(path);
    char target[PATH_MAX];
    snprintf(target, sizeof(target), "%s/target", root);
    TEST_CHECK(close(open(target, O_CREAT | O_WRONLY, 0600)), 0);
    TEST_CHECK(symlink(target, path), 0);
    TEST_CHECK(native_tab_trace_open(path, &stream), false);
    unlink(path); unlink(target); rmdir(root);
})
