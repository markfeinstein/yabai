#include "../../scripts/window_server_tag_corpus_core.c"

static char *window_server_tag_corpus_test_output(
    const struct window_server_tag_corpus_metadata *metadata,
    const struct window_server_tag_corpus_public_window *windows,
    size_t count,
    const struct window_server_snapshot *snapshot,
    size_t *length_out)
{
    char *output = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&output, &length);
    if (!stream) return NULL;
    bool success = window_server_tag_corpus_serialize(stream, metadata, windows, count, snapshot);
    if (fclose(stream) != 0) success = false;
    if (!success) {
        free(output);
        return NULL;
    }
    if (length_out) *length_out = length;
    return output;
}

static void window_server_tag_corpus_test_fixture(
    struct window_server_tag_corpus_metadata *metadata,
    struct window_server_tag_corpus_public_window windows[3],
    struct window_server_window_record records[3],
    struct window_server_snapshot *snapshot)
{
    *metadata = (struct window_server_tag_corpus_metadata) {
        .scenario = "baseline", .capture_set_id = "baseline-set",
        .repeat_index = 1, .os_version = "27.0.1",
        .os_build = "26A434", .architecture = "arm64",
        .source_revision = "test-revision",
        .ax_mode = WINDOW_SERVER_TAG_CORPUS_AX_AUTO,
        .display_count = 1, .separate_spaces = "yes", .stage_manager = "off"
    };
    windows[0] = (struct window_server_tag_corpus_public_window) {
        .window_id = 10, .owner_pid = 100, .cg_layer = 0, .on_screen = true,
        .title = "SECRET_TITLE", .owner_name = "SECRET_OWNER",
        .bundle_id = "SECRET_BUNDLE", .executable_path = "/SECRET/PATH",
        .display_uuid = "SECRET_UUID", .space_id = UINT64_C(0xfeedface),
        .x = 101, .y = 102, .width = 103, .height = 104
    };
    windows[1] = (struct window_server_tag_corpus_public_window) {
        .window_id = 20, .owner_pid = 100, .cg_layer = 3, .on_screen = false
    };
    windows[2] = (struct window_server_tag_corpus_public_window) {
        .window_id = 30, .owner_pid = 200, .cg_layer = 8, .on_screen = true
    };
    records[0] = (struct window_server_window_record) {
        .id = 20, .parent_id = 10, .level = 3,
        .tags = WINDOW_SERVER_TAG_FLOATING | WINDOW_SERVER_TAG_MODAL |
                WINDOW_SERVER_TAG_MINIATURIZED,
        .attributes = 0, .valid_fields = WINDOW_SERVER_WINDOW_FIELD_BASE
    };
    records[1] = (struct window_server_window_record) {
        .id = 10, .parent_id = 0, .level = 0,
        .tags = WINDOW_SERVER_TAG_DOCUMENT | (UINT64_C(1) << 7),
        .attributes = 2, .valid_fields = WINDOW_SERVER_WINDOW_FIELD_BASE
    };
    records[2] = (struct window_server_window_record) {
        .id = 30, .parent_id = 999, .level = 8,
        .tags = WINDOW_SERVER_TAG_MENU_BAR,
        .attributes = 2, .valid_fields = WINDOW_SERVER_WINDOW_FIELD_BASE
    };
    *snapshot = (struct window_server_snapshot) {
        .generation = 1, .records = records, .count = 3
    };
}

TEST_FUNC(window_server_tag_corpus_golden_jsonl_is_stable_redacted_and_parseable,
{
    struct window_server_tag_corpus_metadata metadata;
    struct window_server_tag_corpus_public_window windows[3];
    struct window_server_window_record records[3];
    struct window_server_snapshot snapshot;
    window_server_tag_corpus_test_fixture(&metadata, windows, records, &snapshot);
    size_t length = 0;
    char *output = window_server_tag_corpus_test_output(
        &metadata, windows, 3, &snapshot, &length);
    TEST_CHECK(output != NULL, true);
    static const char expected[] =
        "{\"schema\":\"yabai.window-server-tag-corpus/v2\",\"type\":\"capture\",\"scenario\":\"baseline\",\"capture_set_id\":\"baseline-set\",\"repeat_index\":1,\"os_version\":\"27.0.1\",\"os_build\":\"26A434\",\"architecture\":\"arm64\",\"source_revision\":\"test-revision\",\"ax_mode\":\"auto\",\"display_count\":1,\"separate_spaces\":\"yes\",\"stage_manager\":\"off\",\"tag_map\":\"modern-advisory-v1\",\"tag_map_evidence\":\"mapped-26.3.1-sampled-27.0.1-unverified-27.2\",\"redaction\":\"strict-v1\",\"counts\":{\"public_windows\":3,\"snapshot_records\":3,\"root_relevant\":1,\"legacy_normal_untracked\":1,\"legacy_include_minimized_untracked\":1,\"document_or_floating\":2,\"document_or_floating_rejected_by_legacy\":1}}\n"
        "{\"schema\":\"yabai.window-server-tag-corpus/v2\",\"type\":\"window\",\"ordinal\":0,\"owner_ordinal\":0,\"parent_kind\":\"root\",\"parent_ordinal\":null,\"sls_level\":0,\"cg_layer\":0,\"on_screen\":true,\"tags_hex\":\"0x0000000000000081\",\"attributes_hex\":\"0x0000000000000002\",\"named_tags\":[\"document\"],\"unknown_tags_hex\":\"0x0000000000000080\",\"root_relevant\":true,\"legacy_normal_untracked\":true,\"legacy_include_minimized_untracked\":true,\"document_or_floating\":true}\n"
        "{\"schema\":\"yabai.window-server-tag-corpus/v2\",\"type\":\"window\",\"ordinal\":1,\"owner_ordinal\":0,\"parent_kind\":\"captured\",\"parent_ordinal\":0,\"sls_level\":3,\"cg_layer\":3,\"on_screen\":false,\"tags_hex\":\"0x1000000080000002\",\"attributes_hex\":\"0x0000000000000000\",\"named_tags\":[\"floating\",\"modal\",\"miniaturized\"],\"unknown_tags_hex\":\"0x0000000000000000\",\"root_relevant\":false,\"legacy_normal_untracked\":false,\"legacy_include_minimized_untracked\":false,\"document_or_floating\":true}\n"
        "{\"schema\":\"yabai.window-server-tag-corpus/v2\",\"type\":\"window\",\"ordinal\":2,\"owner_ordinal\":1,\"parent_kind\":\"external\",\"parent_ordinal\":null,\"sls_level\":8,\"cg_layer\":8,\"on_screen\":true,\"tags_hex\":\"0x0000400000000000\",\"attributes_hex\":\"0x0000000000000002\",\"named_tags\":[\"menu_bar\"],\"unknown_tags_hex\":\"0x0000000000000000\",\"root_relevant\":false,\"legacy_normal_untracked\":false,\"legacy_include_minimized_untracked\":false,\"document_or_floating\":false}\n";
    if (output) {
        TEST_CHECK(length, sizeof(expected) - 1);
        TEST_CHECK(strcmp(output, expected), 0);
        const char *redacted[] = {
            "SECRET_TITLE", "SECRET_OWNER", "SECRET_BUNDLE", "/SECRET/PATH",
            "SECRET_UUID", "feedface", "101", "102", "103", "104"
        };
        for (size_t i = 0; i < array_count(redacted); ++i) {
            TEST_CHECK(strstr(output, redacted[i]) == NULL, true);
        }
        char *copy = strdup(output);
        char *save = NULL;
        for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            NSData *data = [NSData dataWithBytes:line length:strlen(line)];
            id object = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
            TEST_CHECK([object isKindOfClass:[NSDictionary class]], true);
        }
        free(copy);
    }
    free(output);
});

TEST_FUNC(window_server_tag_corpus_comparison_is_redacted_stable_and_labeled,
{
    struct window_server_tag_corpus_metadata metadata;
    struct window_server_tag_corpus_public_window windows[3];
    struct window_server_window_record records[3];
    struct window_server_snapshot snapshot;
    window_server_tag_corpus_test_fixture(&metadata, windows, records, &snapshot);
    struct window_server_tag_corpus_comparison_window comparison_windows[3] = {
        {
            .owner = WS_OWNER_REGULAR, .ax = WS_AX_STANDARD_WINDOW,
            .ax_observation = WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_MATCHED,
            .title = WINDOW_SERVER_TITLE_NONEMPTY,
            .label = {
                .present = true, .fixture_id = "document-1", .fixture_role = "primary-document",
                .expected_normal = WS_WINDOW_CLASS_MANAGEABLE,
                .expected_include_minimized = WS_WINDOW_CLASS_MANAGEABLE,
                .adjudication = WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_CONFIRMED,
                .adjudication_reason = "controlled-document", .evidence_ref = "evidence-1",
                .bit58_state = WINDOW_SERVER_TAG_CORPUS_BIT58_NOT_APPLICABLE
            }
        },
        {
            .owner = WS_OWNER_REGULAR, .ax = WS_AX_DIALOG_WINDOW,
            .ax_observation = WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_MATCHED,
            .title = WINDOW_SERVER_TITLE_NONEMPTY,
            .label = {
                .present = true, .fixture_id = "sheet-1", .fixture_role = "child-sheet",
                .expected_normal = WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE,
                .expected_include_minimized = WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE,
                .adjudication = WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_CONFIRMED,
                .adjudication_reason = "controlled-sheet", .evidence_ref = "evidence-2",
                .bit58_state = WINDOW_SERVER_TAG_CORPUS_BIT58_NOT_APPLICABLE
            }
        },
        {
            .owner = WS_OWNER_PROHIBITED, .ax = WS_AX_UNAVAILABLE,
            .ax_observation = WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_UNAVAILABLE,
            .title = WINDOW_SERVER_TITLE_UNAVAILABLE,
            .label = {
                .present = true, .fixture_id = "menu-1", .fixture_role = "menu-negative",
                .expected_normal = WS_WINDOW_CLASS_SYSTEM_OVERLAY,
                .expected_include_minimized = WS_WINDOW_CLASS_SYSTEM_OVERLAY,
                .adjudication = WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_CONFIRMED,
                .adjudication_reason = "controlled-menu", .evidence_ref = "evidence-3",
                .bit58_state = WINDOW_SERVER_TAG_CORPUS_BIT58_NOT_APPLICABLE
            }
        }
    };
    struct window_server_tag_corpus_comparison comparison = {
        .tag_layout = WS_TAG_LAYOUT_SAMPLED,
        .normal_level = 0, .floating_level = 3, .modal_level = 8,
        .windows = comparison_windows, .window_count = 3
    };
    char *output = NULL; size_t length = 0;
    FILE *stream = open_memstream(&output, &length);
    TEST_CHECK(stream != NULL, true);
    TEST_CHECK(window_server_tag_corpus_serialize_comparison(
        stream, &metadata, windows, 3, &snapshot, &comparison), true);
    TEST_CHECK(fclose(stream), 0);
    TEST_CHECK(output != NULL, true);
    if (output) {
        char *copy = strdup(output); char *save = NULL; int line_count = 0;
        for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            NSData *data = [NSData dataWithBytes:line length:strlen(line)];
            id object = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
            TEST_CHECK([object isKindOfClass:[NSDictionary class]], true);
            if (line_count == 0) {
                TEST_CHECK([object[@"schema"] isEqualToString:@"yabai.window-server-classifier-comparison/v2"], true);
                TEST_CHECK([object[@"counts"][@"labeled"] integerValue], 3L);
                TEST_CHECK([object[@"counts"][@"false_positive"] integerValue], 0L);
                TEST_CHECK([object[@"counts"][@"false_negative"] integerValue], 0L);
            }
            ++line_count;
        }
        TEST_CHECK(line_count, 4);
        free(copy);
        const char *redacted[] = { "SECRET_TITLE", "SECRET_OWNER", "SECRET_BUNDLE",
                                   "/SECRET/PATH", "SECRET_UUID", "feedface" };
        for (size_t i = 0; i < array_count(redacted); ++i) {
            TEST_CHECK(strstr(output, redacted[i]) == NULL, true);
        }
        TEST_CHECK(strstr(output, "\"fixture_id\":\"document-1\"") != NULL, true);
        TEST_CHECK(strstr(output, "\"fixture_role\":\"primary-document\"") != NULL, true);
        TEST_CHECK(strstr(output, "\"adjudication_status\":\"confirmed\"") != NULL, true);
        TEST_CHECK(strstr(output, "\"candidate_normal\":\"manageable\"") != NULL, true);
        TEST_CHECK(strstr(output, "\"parent_kind\":\"captured\"") != NULL, true);
    }
    free(output);

    comparison_windows[0].ax = WS_AX_NO_MATCH;
    comparison_windows[0].ax_observation = WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_NO_MATCH;
    windows[0].on_screen = false;
    output = NULL; length = 0;
    stream = open_memstream(&output, &length);
    TEST_CHECK(stream != NULL, true);
    TEST_CHECK(window_server_tag_corpus_serialize_comparison(
        stream, &metadata, windows, 3, &snapshot, &comparison), true);
    TEST_CHECK(fclose(stream), 0);
    if (output) {
        char *newline = strchr(output, '\n');
        TEST_CHECK(newline != NULL, true);
        NSData *data = [NSData dataWithBytes:output length:newline ? (NSUInteger)(newline - output) : 0];
        id object = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        id profiles = object[@"counts"][@"disagreement_profiles"];
        TEST_CHECK([profiles[@"unique_legacy_only"] integerValue], 1L);
        TEST_CHECK([profiles[@"legacy_only_offscreen_ax_no_match"] integerValue], 1L);
        TEST_CHECK([profiles[@"legacy_only_attribute_bit_2"] integerValue], 1L);
        TEST_CHECK([profiles[@"legacy_only_unknown_tag_bits"] integerValue], 1L);
        TEST_CHECK([profiles[@"legacy_only_include_minimized_only"] integerValue], 0L);
        TEST_CHECK([profiles[@"unique_candidate_only"] integerValue], 0L);
        TEST_CHECK([object[@"counts"][@"adjudicated_disagreement"] integerValue], 1L);
        TEST_CHECK([object[@"counts"][@"pending_disagreement"] integerValue], 0L);
        TEST_CHECK([object[@"counts"][@"ax_profiles"][@"no_match_on_screen"] integerValue], 0L);
        TEST_CHECK([object[@"counts"][@"ax_profiles"][@"no_match_offscreen"] integerValue], 1L);
    }
    free(output);

    const char *saved_role = comparison_windows[0].label.fixture_role;
    comparison_windows[0].label.fixture_role = "invalid role";
    char path[] = "/private/tmp/yabai-comparison-invalid.XXXXXX";
    int fd = mkstemp(path); TEST_CHECK(fd >= 0, true); if (fd >= 0) close(fd); unlink(path);
    TEST_CHECK(window_server_tag_corpus_write_comparison_file(
        path, &metadata, windows, 3, &snapshot, &comparison), false);
    TEST_CHECK(access(path, F_OK), -1);
    comparison_windows[0].label.fixture_role = saved_role;

    const char *saved_reason = comparison_windows[0].label.adjudication_reason;
    comparison_windows[0].label.adjudication_reason = NULL;
    TEST_CHECK(window_server_tag_corpus_write_comparison_file(
        path, &metadata, windows, 3, &snapshot, &comparison), false);
    TEST_CHECK(access(path, F_OK), -1);
    comparison_windows[0].label.adjudication_reason = saved_reason;

    comparison_windows[0].label.fixture_role = "bit58-ordinary";
    comparison_windows[0].label.control_pair_id = "bit58-pair";
    comparison_windows[0].label.bit58_state = WINDOW_SERVER_TAG_CORPUS_BIT58_SET;
    TEST_CHECK(window_server_tag_corpus_write_comparison_file(
        path, &metadata, windows, 3, &snapshot, &comparison), false);
    TEST_CHECK(access(path, F_OK), -1);
    records[1].tags |= WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING;
    TEST_CHECK(window_server_tag_corpus_write_comparison_file(
        path, &metadata, windows, 3, &snapshot, &comparison), true);
    unlink(path);
});

TEST_FUNC(window_server_tag_corpus_rejects_invalid_inputs_and_cleans_partial_files,
{
    TEST_CHECK(window_server_tag_corpus_scenario_is_valid("baseline-27.0_1"), true);
    TEST_CHECK(window_server_tag_corpus_scenario_is_valid(""), false);
    TEST_CHECK(window_server_tag_corpus_scenario_is_valid("bad/value"), false);

    struct window_server_tag_corpus_metadata metadata;
    struct window_server_tag_corpus_public_window windows[3];
    struct window_server_window_record records[3];
    struct window_server_snapshot snapshot;
    window_server_tag_corpus_test_fixture(&metadata, windows, records, &snapshot);

    FILE *read_only = fdopen(open("/dev/null", O_RDONLY), "r");
    TEST_CHECK(window_server_tag_corpus_serialize(
        read_only, &metadata, windows, 3, &snapshot), false);
    fclose(read_only);

    struct window_server_snapshot wrong_count = snapshot;
    wrong_count.count = 2;
    FILE *stream = tmpfile();
    TEST_CHECK(window_server_tag_corpus_serialize(
        stream, &metadata, windows, 3, &wrong_count), false);
    fclose(stream);

    uint32_t saved_id = windows[1].window_id;
    windows[1].window_id = windows[0].window_id;
    char root[] = "/private/tmp/yabai-tag-corpus-test.XXXXXX";
    TEST_CHECK(mkdtemp(root) != NULL, true);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/output.jsonl", root);
    TEST_CHECK(window_server_tag_corpus_write_file(
        path, &metadata, windows, 3, &snapshot), false);
    TEST_CHECK(access(path, F_OK), -1);
    windows[1].window_id = saved_id;

    TEST_CHECK(window_server_tag_corpus_write_file(
        path, &metadata, windows, 3, &snapshot), true);
    struct stat st;
    TEST_CHECK(stat(path, &st), 0);
    TEST_CHECK(st.st_mode & 0777, 0600);
    TEST_CHECK(window_server_tag_corpus_write_file(
        path, &metadata, windows, 3, &snapshot), false);
    unlink(path);
    char target[PATH_MAX];
    snprintf(target, sizeof(target), "%s/target", root);
    TEST_CHECK(close(open(target, O_CREAT | O_WRONLY, 0600)), 0);
    TEST_CHECK(symlink(target, path), 0);
    TEST_CHECK(window_server_tag_corpus_write_file(
        path, &metadata, windows, 3, &snapshot), false);
    unlink(path);
    unlink(target);
    rmdir(root);
});

static bool window_server_tag_corpus_benchmark_case(size_t record_count)
{
    enum { sample_count = 20 };
    int iterations = record_count <= 100 ? 50 : 10;
    struct window_server_tag_corpus_public_window windows[1000] = {0};
    struct window_server_window_record records[1000] = {0};
    struct window_server_tag_corpus_metadata metadata = {
        .scenario = "benchmark", .capture_set_id = "benchmark-set", .repeat_index = 1,
        .os_version = "27.0.1", .os_build = "26A434", .architecture = "arm64",
        .source_revision = "benchmark-revision", .ax_mode = WINDOW_SERVER_TAG_CORPUS_AX_AUTO,
        .display_count = 1, .separate_spaces = "yes", .stage_manager = "off"
    };
    for (size_t i = 0; i < record_count; ++i) {
        windows[i] = (struct window_server_tag_corpus_public_window) {
            .window_id = 1000u + (uint32_t) i,
            .owner_pid = 100 + (int) (i % 13),
            .cg_layer = (int) (i % 9), .on_screen = (i % 3) != 0
        };
        records[i] = (struct window_server_window_record) {
            .id = windows[i].window_id,
            .parent_id = i % 5 ? 0 : UINT32_C(999999),
            .level = (int[]){0, 3, 8, 17}[i % 4],
            .tags = (UINT64_C(1) << (i % 63)) | WINDOW_SERVER_TAG_DOCUMENT,
            .attributes = i % 4,
            .valid_fields = WINDOW_SERVER_WINDOW_FIELD_BASE
        };
    }
    struct window_server_snapshot snapshot = {
        .generation = 1, .records = records, .count = record_count
    };
    uint64_t elapsed[sample_count] = {0};
    uint64_t checksum = 0;
    size_t bytes = 0;
    bool valid = true;
    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            size_t length = 0;
            char *output = window_server_tag_corpus_test_output(
                &metadata, windows, record_count, &snapshot, &length);
            if (!output) valid = false;
            bytes += length;
            if (output && length) checksum += (unsigned char) output[length - 1] + length;
            free(output);
        }
        elapsed[sample] = (read_os_timer() - start) /
                          ((uint64_t) iterations * (uint64_t) record_count);
    }
    qsort(elapsed, sample_count, sizeof(elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    int p95 = (sample_count * 95 + 99) / 100 - 1;
    size_t total_records = (size_t) sample_count * (size_t) iterations * record_count;
    printf("{\"case\":\"window-server-tag-corpus\",\"records\":%zu,"
           "\"iterations\":%d,\"p50_ns_per_record\":%llu,"
           "\"p95_ns_per_record\":%llu,\"bytes_per_record\":%zu,"
           "\"checksum\":%llu}\n",
           record_count, sample_count * iterations,
           (unsigned long long) elapsed[sample_count / 2],
           (unsigned long long) elapsed[p95], bytes / total_records,
           (unsigned long long) checksum);
    return valid && checksum != 0 && bytes != 0;
}

static bool window_server_tag_corpus_benchmark_run(void)
{
    bool small = window_server_tag_corpus_benchmark_case(10);
    bool medium = window_server_tag_corpus_benchmark_case(100);
    bool large = window_server_tag_corpus_benchmark_case(1000);
    return small && medium && large;
}

static bool window_server_classifier_comparison_benchmark_case(size_t record_count)
{
    enum { sample_count = 20 };
    int iterations = record_count <= 100 ? 50 : 10;
    struct window_server_tag_corpus_public_window windows[1000] = {0};
    struct window_server_window_record records[1000] = {0};
    struct window_server_tag_corpus_comparison_window comparison_windows[1000] = {0};
    struct window_server_tag_corpus_metadata metadata = {
        .scenario = "benchmark", .capture_set_id = "benchmark-set", .repeat_index = 1,
        .os_version = "27.0.1", .os_build = "26A434", .architecture = "arm64",
        .source_revision = "benchmark-revision", .ax_mode = WINDOW_SERVER_TAG_CORPUS_AX_AUTO,
        .display_count = 1, .separate_spaces = "yes", .stage_manager = "off"
    };
    for (size_t i = 0; i < record_count; ++i) {
        windows[i] = (struct window_server_tag_corpus_public_window) {
            .window_id = 2000u + (uint32_t) i, .owner_pid = 200 + (int)(i % 17),
            .cg_layer = (int)(i % 9), .on_screen = (i % 2) != 0,
            .x = 10, .y = 20, .width = 800, .height = 600
        };
        records[i] = (struct window_server_window_record) {
            .id = windows[i].window_id, .parent_id = i % 7 ? 0 : 999999,
            .level = (int[]){0, 3, 8, 17}[i % 4],
            .tags = WINDOW_SERVER_TAG_DOCUMENT | (UINT64_C(1) << (i % 63)),
            .attributes = i % 4, .valid_fields = WINDOW_SERVER_WINDOW_FIELD_BASE
        };
        comparison_windows[i] = (struct window_server_tag_corpus_comparison_window) {
            .owner = (enum ws_owner_kind)(i % 4),
            .ax = (enum ws_ax_corroboration)(i % 6),
            .ax_observation = (enum window_server_tag_corpus_ax_observation)(i % 5),
            .title = (enum window_server_tag_corpus_title_state)(i % 3)
        };
    }
    struct window_server_snapshot snapshot = { .generation = 1, .records = records, .count = record_count };
    struct window_server_tag_corpus_comparison comparison = {
        .tag_layout = WS_TAG_LAYOUT_SAMPLED, .normal_level = 0,
        .floating_level = 3, .modal_level = 8,
        .windows = comparison_windows, .window_count = record_count
    };
    uint64_t elapsed[sample_count] = {0}; size_t bytes = 0; uint64_t checksum = 0; bool valid = true;
    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t start = read_os_timer();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            char *output = NULL; size_t length = 0; FILE *stream = open_memstream(&output, &length);
            bool success = stream && window_server_tag_corpus_serialize_comparison(
                stream, &metadata, windows, record_count, &snapshot, &comparison);
            if (stream && fclose(stream) != 0) success = false;
            if (!success) valid = false;
            bytes += length; if (output && length) checksum += length + (unsigned char)output[length - 1];
            free(output);
        }
        elapsed[sample] = (read_os_timer() - start) /
                          ((uint64_t)iterations * (uint64_t)record_count);
    }
    qsort(elapsed, sample_count, sizeof(elapsed[0]), window_server_snapshot_benchmark_compare_u64);
    int p95 = (sample_count * 95 + 99) / 100 - 1;
    size_t total_records = (size_t)sample_count * (size_t)iterations * record_count;
    printf("{\"case\":\"window-server-classifier-comparison\",\"records\":%zu,"
           "\"iterations\":%d,\"p50_ns_per_record\":%llu,\"p95_ns_per_record\":%llu,"
           "\"bytes_per_record\":%zu,\"checksum\":%llu}\n",
           record_count, sample_count * iterations,
           (unsigned long long)elapsed[sample_count / 2],
           (unsigned long long)elapsed[p95], bytes / total_records,
           (unsigned long long)checksum);
    return valid && bytes && checksum;
}

static bool window_server_classifier_comparison_benchmark_run(void)
{
    bool small = window_server_classifier_comparison_benchmark_case(10);
    bool medium = window_server_classifier_comparison_benchmark_case(100);
    bool large = window_server_classifier_comparison_benchmark_case(1000);
    return small && medium && large;
}
