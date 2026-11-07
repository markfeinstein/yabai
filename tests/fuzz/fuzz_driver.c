#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool parse_size(const char *text, size_t *value)
{
    if (!text || !*text || !value) return false;
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || !end || *end || parsed > SIZE_MAX) return false;
    *value = (size_t) parsed;
    return true;
}

static uint64_t fuzz_next(uint64_t *state)
{
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

int main(int argc, char **argv)
{
    size_t runs = 1000;
    size_t max_length = 4096;
    if ((argc > 1 && !parse_size(argv[1], &runs)) ||
        (argc > 2 && !parse_size(argv[2], &max_length)) || argc > 3 ||
        runs == 0 || max_length == 0 || max_length > 1024U * 1024U) {
        fprintf(stderr, "usage: %s [runs] [max-length<=1048576]\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint8_t *data = malloc(max_length);
    if (!data) return EXIT_FAILURE;
    uint64_t state = UINT64_C(0x79616261692d667a);
    for (size_t run = 0; run < runs; ++run) {
        size_t length = (size_t) (fuzz_next(&state) % (max_length + 1));
        for (size_t i = 0; i < length; ++i) data[i] = (uint8_t) fuzz_next(&state);
        (void) LLVMFuzzerTestOneInput(data, length);
    }
    free(data);
    printf("fuzz smoke passed: %zu runs, max length %zu\n", runs, max_length);
    return EXIT_SUCCESS;
}
