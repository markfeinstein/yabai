#ifndef YABAI_TEST_RUNNER_H
#define YABAI_TEST_RUNNER_H

#include <stdbool.h>

enum test_runner_parse_result
{
    TEST_RUNNER_PARSE_OK,
    TEST_RUNNER_PARSE_HELP,
    TEST_RUNNER_PARSE_ERROR
};

struct test_runner_options
{
    bool list;
    const char *filter;
};

// Parsing and name matching have no platform, allocation, or test-suite effects.
enum test_runner_parse_result test_runner_parse(int argc, char *const argv[],
                                                 struct test_runner_options *options,
                                                 const char **error);
bool test_runner_matches(const struct test_runner_options *options, const char *name);

#endif
