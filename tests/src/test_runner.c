TEST_FUNC(test_runner_defaults_and_help_are_explicit,
{
    struct test_runner_options options = { .list = true, .filter = "old" };
    const char *error = "old";
    char *defaults[] = { "tests" };
    TEST_CHECK(test_runner_parse(1, defaults, &options, &error), TEST_RUNNER_PARSE_OK);
    TEST_CHECK(options.list, false);
    TEST_CHECK(options.filter == NULL, true);
    TEST_CHECK(error == NULL, true);
    char *help[] = { "tests", "--help" };
    TEST_CHECK(test_runner_parse(2, help, &options, &error), TEST_RUNNER_PARSE_HELP);
});

TEST_FUNC(test_runner_list_and_filter_compose_in_either_order,
{
    char *arguments[][4] = {
        { "tests", "--list", "--filter", "window_" },
        { "tests", "--filter", "window_", "--list" }
    };
    for (int i = 0; i < array_count(arguments); ++i) {
        struct test_runner_options options;
        const char *error;
        TEST_CHECK(test_runner_parse(4, arguments[i], &options, &error), TEST_RUNNER_PARSE_OK);
        TEST_CHECK(options.list, true);
        TEST_CHECK(strcmp(options.filter, "window_"), 0);
        TEST_CHECK(error == NULL, true);
    }
});

TEST_FUNC(test_runner_rejects_malformed_unknown_and_repeated_arguments,
{
    char *arguments[][5] = {
        { "tests", "--unknown" }, { "tests", "unexpected" },
        { "tests", "--filter" }, { "tests", "--filter", "" },
        { "tests", "--filter", "--list" }, { "tests", "--list", "--list" },
        { "tests", "--filter", "one", "--filter", "two" },
        { "tests", "--help", "--list" },
        { "tests", "--benchmark-unknown" }
    };
    int counts[] = { 2, 2, 2, 3, 3, 3, 5, 3, 2 };
    for (int i = 0; i < array_count(arguments); ++i) {
        struct test_runner_options options;
        const char *error = NULL;
        TEST_CHECK(test_runner_parse(counts[i], arguments[i], &options, &error), TEST_RUNNER_PARSE_ERROR);
        TEST_CHECK(error != NULL, true);
    }
});

TEST_FUNC(test_runner_matches_case_sensitive_literal_substrings,
{
    struct test_runner_options options = {0};
    TEST_CHECK(test_runner_matches(&options, "window_constraints_normalize"), true);
    options.filter = "constraints";
    TEST_CHECK(test_runner_matches(&options, "window_constraints_normalize"), true);
    TEST_CHECK(test_runner_matches(&options, "constraints"), true);
    TEST_CHECK(test_runner_matches(&options, "window_close"), false);
    options.filter = "Constraints";
    TEST_CHECK(test_runner_matches(&options, "window_constraints_normalize"), false);
    options.filter = "window_*";
    TEST_CHECK(test_runner_matches(&options, "window_close"), false);
});
