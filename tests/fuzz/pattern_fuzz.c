#include "../../src/pattern.h"

#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > DAEMON_MESSAGE_MAX_LENGTH) return 0;
    char *pattern = malloc(size + 1);
    if (!pattern) return 0;
    memcpy(pattern, data, size);
    pattern[size] = '\0';

    (void) pattern_cost(pattern);
    regex_t regex = {0};
    if (pattern_compile(&regex, pattern) == PATTERN_COMPILE_OK) regfree(&regex);

    free(pattern);
    return 0;
}
