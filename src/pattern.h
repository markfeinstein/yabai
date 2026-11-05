#ifndef PATTERN_H
#define PATTERN_H

#include <regex.h>

enum pattern_compile_result
{
    PATTERN_COMPILE_OK,
    PATTERN_COMPILE_INVALID,
    PATTERN_COMPILE_TOO_COMPLEX
};

double pattern_cost(const char *pattern);
enum pattern_compile_result pattern_compile(regex_t *regex, const char *pattern);

#endif
