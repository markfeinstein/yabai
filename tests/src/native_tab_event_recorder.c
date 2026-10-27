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
        .validity = NATIVE_TAB_FACT_PARENT | NATIVE_TAB_FACT_SPACE |
                    NATIVE_TAB_FACT_FRAME | NATIVE_TAB_FACT_LEVEL |
                    NATIVE_TAB_FACT_SUBLEVEL | NATIVE_TAB_FACT_ORDER |
                    NATIVE_TAB_FACT_TITLE
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
        .architecture = "arm64", .ax_available = true
    };
    return trace->stream && native_tab_recorder_init(
        &trace->recorder, trace->stream, scenario, trace->start, &metadata);
}

static bool native_tab_test_end(struct native_tab_test_trace *trace, uint64_t elapsed_ms)
{
    bool success = native_tab_recorder_stop(
        &trace->recorder, trace->start + elapsed_ms * UINT64_C(1000000));
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

static bool native_tab_test_switch_case(bool reversed, char **output)
{
    struct native_tab_test_trace trace;
    if (!native_tab_test_begin(&trace, reversed ? "switch-reversed" : "switch-normal")) return false;
    struct native_tab_window_fact old_fact = native_tab_test_fact(
        424242, 0, UINT64_C(0xfeedface), UINT64_C(0x1111222233334444),
        100, 100, 800, 600, 1);
    struct native_tab_window_fact new_fact = native_tab_test_fact(
        525252, 0, UINT64_C(0xfeedface), UINT64_C(0x1111222233334444),
        108, 92, 800, 608, 2);
    struct native_tab_inventory baseline = native_tab_test_inventory(&old_fact, 1, true);
    if (!native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline)) return false;

    struct native_tab_event invisible = {
        .type = NATIVE_TAB_EVENT_SLS_INVISIBLE,
        .time_ns = trace.start + (reversed ? 40 : 10) * UINT64_C(1000000),
        .window_id = old_fact.window_id, .ordered_in_known = true, .ordered_in = false,
        .has_fact = true, .fact = old_fact
    };
    struct native_tab_event visible = {
        .type = NATIVE_TAB_EVENT_SLS_VISIBLE,
        .time_ns = trace.start + (reversed ? 10 : 40) * UINT64_C(1000000),
        .window_id = new_fact.window_id, .ordered_in_known = true, .ordered_in = true,
        .has_fact = true, .fact = new_fact
    };
    if (reversed) {
        if (!native_tab_recorder_event(&trace.recorder, &visible) ||
            !native_tab_recorder_event(&trace.recorder, &invisible)) return false;
    } else {
        if (!native_tab_recorder_event(&trace.recorder, &invisible) ||
            !native_tab_recorder_event(&trace.recorder, &visible)) return false;
    }
    struct native_tab_inventory switched = native_tab_test_inventory(&new_fact, 1, true);
    if (!native_tab_recorder_inventory(&trace.recorder, trace.start + UINT64_C(110000000), &switched) ||
        !native_tab_recorder_tick(&trace.recorder, trace.start + UINT64_C(1011000000)) ||
        !native_tab_test_end(&trace, 1020)) return false;
    *output = trace.output;
    return true;
}

TEST_FUNC(native_tab_event_recorder_pairs_both_switch_event_orders_without_identity_decision,
{
    for (int reversed = 0; reversed < 2; ++reversed) {
        char *output = NULL;
        TEST_CHECK(native_tab_test_switch_case(reversed != 0, &output), true);
        TEST_CHECK(output != NULL, true);
        if (output) {
            TEST_CHECK(strstr(output, "\"outcome\":\"unexplained-one-to-one\"") != NULL, true);
            TEST_CHECK(strstr(output, "\"shared_space\":true") != NULL, true);
            TEST_CHECK(strstr(output, "\"root_compatible\":true") != NULL, true);
            TEST_CHECK(strstr(output, "\"frame_delta_8pt\":[1,-1,0,1]") != NULL, true);
            TEST_CHECK(strstr(output, "\"same_title_token\":true") != NULL, true);
            TEST_CHECK(strstr(output, "replacement") == NULL, true);
            TEST_CHECK(strstr(output, "424242") == NULL, true);
            TEST_CHECK(strstr(output, "525252") == NULL, true);
            TEST_CHECK(strstr(output, "feedface") == NULL, true);
            TEST_CHECK(strstr(output, "1111222233334444") == NULL, true);
            TEST_CHECK(native_tab_test_jsonl_is_parseable(output), true);
        }
        free(output);
    }
})

TEST_FUNC(native_tab_event_recorder_classifies_reappearance_detach_and_lifecycle_explanations,
{
    struct native_tab_window_fact old_fact = native_tab_test_fact(700001, 0, 9, 1, 0, 0, 100, 100, 0);
    struct native_tab_window_fact new_fact = native_tab_test_fact(700002, 0, 9, 2, 0, 0, 100, 100, 1);

    struct native_tab_test_trace trace;
    TEST_CHECK(native_tab_test_begin(&trace, "same-id"), true);
    struct native_tab_inventory baseline = native_tab_test_inventory(&old_fact, 1, true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    struct native_tab_event event = {
        .type = NATIVE_TAB_EVENT_SLS_INVISIBLE, .time_ns = trace.start + 10,
        .window_id = old_fact.window_id, .ordered_in_known = true, .ordered_in = false
    };
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    event = (struct native_tab_event) {
        .type = NATIVE_TAB_EVENT_SLS_VISIBLE, .time_ns = trace.start + 20,
        .window_id = old_fact.window_id, .ordered_in_known = true, .ordered_in = true
    };
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    TEST_CHECK(native_tab_test_end(&trace, 1), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"same-id-reappeared\"") != NULL, true);
    free(trace.output);

    TEST_CHECK(native_tab_test_begin(&trace, "inactive-detach"), true);
    baseline = native_tab_test_inventory(&old_fact, 1, true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    event = (struct native_tab_event) {
        .type = NATIVE_TAB_EVENT_AX_CREATED, .time_ns = trace.start + UINT64_C(10000000),
        .window_id = new_fact.window_id, .has_fact = true, .fact = new_fact
    };
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    event = (struct native_tab_event) {
        .type = NATIVE_TAB_EVENT_SLS_VISIBLE, .time_ns = trace.start + UINT64_C(20000000),
        .window_id = new_fact.window_id, .ordered_in_known = true, .ordered_in = true,
        .has_fact = true, .fact = new_fact
    };
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    struct native_tab_window_fact detached_facts[] = { old_fact, new_fact };
    struct native_tab_inventory detached = native_tab_test_inventory(detached_facts, 2, true);
    TEST_CHECK(native_tab_recorder_inventory(&trace.recorder, trace.start + UINT64_C(110000000), &detached), true);
    TEST_CHECK(native_tab_recorder_tick(&trace.recorder, trace.start + UINT64_C(1011000000)), true);
    TEST_CHECK(native_tab_test_end(&trace, 1020), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"appearance-only\"") != NULL, true);
    TEST_CHECK(strstr(trace.output, "\"ax_created\":true") != NULL, true);
    free(trace.output);

    const struct {
        enum native_tab_event_type type;
        const char *outcome;
    } explanations[] = {
        { NATIVE_TAB_EVENT_AX_MINIMIZED, "explained-minimize" },
        { NATIVE_TAB_EVENT_AX_DESTROYED, "explained-ax-destroy" },
        { NATIVE_TAB_EVENT_SLS_DESTROYED, "explained-sls-destroy" },
        { NATIVE_TAB_EVENT_APP_HIDDEN, "explained-app-hide" },
        { NATIVE_TAB_EVENT_SPACE_CHANGED, "explained-space-transition" },
        { NATIVE_TAB_EVENT_MISSION_CONTROL_ENTER, "explained-mission-control" }
    };
    for (size_t i = 0; i < array_count(explanations); ++i) {
        TEST_CHECK(native_tab_test_begin(&trace, "explained"), true);
        TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
        event = (struct native_tab_event) {
            .type = NATIVE_TAB_EVENT_SLS_INVISIBLE, .time_ns = trace.start + 10,
            .window_id = old_fact.window_id, .ordered_in_known = true, .ordered_in = false
        };
        TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
        event = (struct native_tab_event) {
            .type = explanations[i].type, .time_ns = trace.start + 20,
            .window_id = old_fact.window_id
        };
        TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
        TEST_CHECK(native_tab_test_end(&trace, 1), true);
        char expected[128];
        snprintf(expected, sizeof(expected), "\"outcome\":\"%s\"", explanations[i].outcome);
        TEST_CHECK(strstr(trace.output, expected) != NULL, true);
        free(trace.output);
    }
})

TEST_FUNC(native_tab_event_recorder_enforces_deadlines_stale_events_capacity_and_stop,
{
    struct native_tab_window_fact old_fact = native_tab_test_fact(800001, 0, 1, 1, 0, 0, 100, 100, 0);
    struct native_tab_window_fact new_fact = native_tab_test_fact(800002, 0, 1, 1, 0, 0, 100, 100, 1);
    struct native_tab_inventory baseline = native_tab_test_inventory(&old_fact, 1, true);
    struct native_tab_test_trace trace;
    TEST_CHECK(native_tab_test_begin(&trace, "deadline"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    struct native_tab_event event = {
        .type = NATIVE_TAB_EVENT_SLS_INVISIBLE, .time_ns = trace.start + UINT64_C(10000000),
        .window_id = old_fact.window_id, .ordered_in_known = true, .ordered_in = true
    };
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    TEST_CHECK(trace.recorder.episode.state, NATIVE_TAB_EPISODE_IDLE);
    event.ordered_in = false;
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    event = (struct native_tab_event) {
        .type = NATIVE_TAB_EVENT_SLS_VISIBLE,
        .time_ns = trace.start + UINT64_C(1010000000),
        .window_id = new_fact.window_id, .ordered_in_known = true, .ordered_in = true,
        .has_fact = true, .fact = new_fact
    };
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    TEST_CHECK(trace.recorder.episode.appeared_count, 1);
    TEST_CHECK(native_tab_recorder_tick(&trace.recorder, trace.start + UINT64_C(1010000001)), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"unexplained-one-to-one\"") != NULL, true);
    TEST_CHECK(native_tab_test_end(&trace, 1020), true);
    free(trace.output);

    TEST_CHECK(native_tab_test_begin(&trace, "capacity"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    for (uint32_t i = 0; i < NATIVE_TAB_EPISODE_CAPACITY + 1; ++i) {
        event = (struct native_tab_event) {
            .type = NATIVE_TAB_EVENT_SLS_VISIBLE, .time_ns = trace.start + i + 1,
            .window_id = 900000 + i, .ordered_in_known = true, .ordered_in = true
        };
        TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    }
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"capacity-exceeded\"") != NULL, true);
    TEST_CHECK(native_tab_test_end(&trace, 1), true);
    free(trace.output);

    TEST_CHECK(native_tab_test_begin(&trace, "stop"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    event = (struct native_tab_event) {
        .type = NATIVE_TAB_EVENT_SLS_INVISIBLE, .time_ns = trace.start + 10,
        .window_id = old_fact.window_id, .ordered_in_known = true, .ordered_in = false
    };
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    TEST_CHECK(native_tab_test_end(&trace, 1), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"recorder-stop\"") != NULL, true);
    free(trace.output);
})

TEST_FUNC(native_tab_event_recorder_marks_incomplete_inventory_and_secures_output,
{
    struct native_tab_window_fact old_fact = native_tab_test_fact(990001, 0, 1, 1, 0, 0, 100, 100, 0);
    struct native_tab_inventory baseline = native_tab_test_inventory(&old_fact, 1, true);
    struct native_tab_test_trace trace;
    TEST_CHECK(native_tab_test_begin(&trace, "incomplete"), true);
    TEST_CHECK(native_tab_recorder_baseline(&trace.recorder, trace.start, &baseline), true);
    struct native_tab_event event = {
        .type = NATIVE_TAB_EVENT_SLS_INVISIBLE, .time_ns = trace.start + 10,
        .window_id = old_fact.window_id, .ordered_in_known = true, .ordered_in = false
    };
    TEST_CHECK(native_tab_recorder_event(&trace.recorder, &event), true);
    struct native_tab_inventory incomplete = { .complete = false };
    TEST_CHECK(native_tab_recorder_inventory(&trace.recorder, trace.start + UINT64_C(110000000), &incomplete), true);
    TEST_CHECK(native_tab_recorder_tick(&trace.recorder, trace.start + UINT64_C(1000000011)), true);
    TEST_CHECK(native_tab_test_end(&trace, 1020), true);
    TEST_CHECK(strstr(trace.output, "\"outcome\":\"inventory-incomplete\"") != NULL, true);
    TEST_CHECK(strstr(trace.output, "\"inventory_complete\":false") != NULL, true);
    free(trace.output);

    TEST_CHECK(native_tab_scenario_is_valid("active-to-inactive"), true);
    TEST_CHECK(native_tab_scenario_is_valid("bad/value"), false);
    TEST_CHECK(native_tab_scenario_is_valid(""), false);

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
    unlink(path);
    unlink(target);
    rmdir(root);
})
