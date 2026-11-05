#ifndef YABAI_MACHO_PARSER_H
#define YABAI_MACHO_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MACHO_PATTERN_MAX_BYTES (1024U * 1024U)

struct macho_byte_pattern {
    uint8_t *bytes;
    bool *wildcards;
    size_t length;
};

bool macho_validate_arm64(const uint8_t *data, size_t size);
bool macho_validate_scanner_image(const uint8_t *data, size_t size);
bool macho_find_section(const uint8_t *data, size_t size, const char *segment_name,
                        const char *section_name, uint64_t *offset, uint64_t *section_size);
bool macho_file_offset_to_vm_address(const uint8_t *data, size_t size, uint64_t file_offset,
                                     uint64_t *vm_address);
enum macho_pointer_result {
    MACHO_POINTER_RESOLVED,
    MACHO_POINTER_NOT_CHAINED,
    MACHO_POINTER_UNSUPPORTED,
    MACHO_POINTER_MALFORMED,
};

/* Resolves a raw pointer, or a supported unauthenticated chained rebase at file_offset. */
enum macho_pointer_result macho_resolve_pointer(const uint8_t *data, size_t size,
                                                uint64_t file_offset, uint64_t *target);

bool macho_parse_pattern(const char *text, struct macho_byte_pattern *result);
void macho_free_pattern(struct macho_byte_pattern *pattern);
bool macho_pattern_matches(const uint8_t *data, size_t available,
                           const struct macho_byte_pattern *pattern);

#ifdef MACHO_PARSER_TESTING
/* Deterministic test seam. SIZE_MAX disables failure. */
void macho_test_set_allocation_failure(size_t successful_allocations);
size_t macho_test_outstanding_allocations(void);
#endif

#endif
