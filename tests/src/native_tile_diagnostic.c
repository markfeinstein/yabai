#include "../../scripts/native_tile_diagnostic_core.c"

TEST_FUNC(native_tile_diagnostic_json_is_stable_aggregate_and_redacted,
{
    struct native_tile_platform platform = {
        .os_major = 27, .os_minor = 2, .os_patch = 0,
        .build = "26B123", .architecture = "arm64"
    };
    struct wm_tile_membership_diagnostic diagnostic = {
        .status = WM_TILE_MEMBERSHIP_OK,
        .managed_display_count = 1,
        .managed_space_count = 4,
        .spaces_queried = 4,
        .tiled_parent_space_count = 1,
        .tile_space_count = 2,
        .failed_query_count = 0
    };
    char *output = NULL; size_t length = 0;
    FILE *stream = open_memstream(&output, &length);
    TEST_CHECK(stream != NULL, true);
    TEST_CHECK(native_tile_diagnostic_write_json(stream, &platform, &diagnostic), true);
    TEST_CHECK(fclose(stream), 0);
    static const char expected[] =
        "{\"schema\":1,\"probe\":\"native-tile-membership\",\"mode\":\"read-only\","
        "\"status\":\"ok\",\"platform\":{\"os-major\":27,\"os-minor\":2,"
        "\"os-patch\":0,\"build\":\"26B123\",\"architecture\":\"arm64\"},"
        "\"capabilities\":{\"managed-space-topology\":\"available\","
        "\"tile-space-membership\":\"available\"},\"counts\":{"
        "\"managed-displays\":1,\"managed-spaces\":4,\"spaces-queried\":4,"
        "\"tiled-parent-spaces\":1,\"tile-spaces\":2,\"failed-queries\":0},"
        "\"redaction\":\"aggregate-only\"}\n";
    TEST_CHECK(length, sizeof(expected) - 1);
    TEST_CHECK(strcmp(output, expected), 0);
    TEST_CHECK(strstr(output, "SECRET_UUID") == NULL, true);
    TEST_CHECK(strstr(output, "id64") == NULL, true);
    free(output);
})

TEST_FUNC(native_tile_diagnostic_status_vocabulary_and_invalid_metadata_are_exact,
{
    TEST_CHECK(strcmp(native_tile_status_string(WM_TILE_MEMBERSHIP_OK), "ok"), 0);
    TEST_CHECK(strcmp(native_tile_status_string(WM_TILE_MEMBERSHIP_PARTIAL), "partial"), 0);
    TEST_CHECK(strcmp(native_tile_status_string(WM_TILE_MEMBERSHIP_UNAVAILABLE), "unavailable"), 0);
    TEST_CHECK(strcmp(native_tile_status_string(WM_TILE_MEMBERSHIP_ABI_MISMATCH), "abi-mismatch"), 0);
    TEST_CHECK(strcmp(native_tile_status_string(WM_TILE_MEMBERSHIP_QUERY_FAILED), "query-failed"), 0);
    struct native_tile_platform bad = { .build = "bad\"build", .architecture = "arm64" };
    struct wm_tile_membership_diagnostic diagnostic = {0};
    FILE *stream = tmpfile();
    TEST_CHECK(native_tile_diagnostic_write_json(stream, &bad, &diagnostic), false);
    fclose(stream);
})
