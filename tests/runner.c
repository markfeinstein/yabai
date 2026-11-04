#include "runner.h"

#include <stddef.h>
#include <string.h>

enum test_runner_parse_result test_runner_parse(int argc, char *const argv[],
                                                 struct test_runner_options *options,
                                                 const char **error)
{
    *options = (struct test_runner_options) {0};
    *error = NULL;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) return TEST_RUNNER_PARSE_HELP;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 && !options->list) {
            options->list = true;
        } else if (strcmp(argv[i], "--filter") == 0 && !options->filter) {
            if (++i == argc || !argv[i][0] || strncmp(argv[i], "--", 2) == 0) {
                *error = "--filter requires a nonempty test-name substring";
                return TEST_RUNNER_PARSE_ERROR;
            }
            options->filter = argv[i];
        } else {
            *error = "unknown, repeated, or incompatible test-runner argument";
            return TEST_RUNNER_PARSE_ERROR;
        }
    }
    return TEST_RUNNER_PARSE_OK;
}

bool test_runner_matches(const struct test_runner_options *options, const char *name)
{
    return !options->filter || strstr(name, options->filter) != NULL;
}
