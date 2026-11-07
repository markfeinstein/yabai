#include "../../scripts/macho_parser.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > MACHO_PATTERN_MAX_BYTES) return 0;

    (void) macho_validate_arm64(data, size);
    (void) macho_validate_scanner_image(data, size);

    uint64_t offset = 0;
    uint64_t section_size = 0;
    (void) macho_find_section(data, size, "__TEXT", "__text", &offset, &section_size);
    (void) macho_file_offset_to_vm_address(data, size, size ? data[0] % size : 0, &offset);
    (void) macho_resolve_pointer(data, size, size ? data[size - 1] % size : 0, &offset);

    char *text = malloc(size + 1);
    if (!text) return 0;
    memcpy(text, data, size);
    text[size] = '\0';
    struct macho_byte_pattern pattern = {0};
    if (macho_parse_pattern(text, &pattern)) {
        (void) macho_pattern_matches(data, size, &pattern);
        macho_free_pattern(&pattern);
    }
    free(text);
    return 0;
}
