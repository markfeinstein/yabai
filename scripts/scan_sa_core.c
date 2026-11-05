#include "scan_sa_core.h"
#include "macho_parser.h"

#include <string.h>

bool scan_sa_pattern(const uint8_t *data, size_t size, uint64_t start, uint64_t candidate_span,
                     const char *pattern_text, uint64_t *first_match, size_t *match_count)
{
    struct macho_byte_pattern pattern = {0};
    uint64_t candidates;
    if (!data || !pattern_text || !first_match || !match_count ||
        !macho_parse_pattern(pattern_text, &pattern)) return false;
    *first_match = 0; *match_count = 0;
    if (start < size) {
        candidates = candidate_span;
        if (candidates > size - (size_t) start) candidates = size - (size_t) start;
        for (uint64_t index = 0; index < candidates; ++index) {
            uint64_t cursor = start + index;
            if (pattern.length > size - (size_t) cursor) break;
            if (macho_pattern_matches(data + (size_t) cursor, size - (size_t) cursor, &pattern)) {
                if (*match_count == 0) *first_match = cursor;
                ++*match_count;
            }
        }
    }
    macho_free_pattern(&pattern);
    return true;
}

bool scan_sa_pattern_group(const uint8_t *data, size_t size, uint64_t start,
                           uint64_t candidate_span, const char *const *patterns,
                           size_t pattern_count, bool *satisfied)
{
    bool supported = false;
    if (!patterns || !satisfied) return false;
    *satisfied = false;
    for (size_t index = 0; index < pattern_count; ++index) {
        uint64_t first; size_t matches;
        if (!patterns[index]) continue;
        supported = true;
        if (!scan_sa_pattern(data, size, start, candidate_span, patterns[index], &first, &matches))
            return false;
        if (matches != 0) *satisfied = true;
    }
    if (!supported) *satisfied = true;
    return true;
}

static int selector_in_string_section(const uint8_t *data, size_t size,
                                       uint64_t strings_start, uint64_t strings_size,
                                       uint64_t refs_start, uint64_t refs_size,
                                       const char *selector, uint64_t *result)
{
    size_t length = strlen(selector);
    if (length == SIZE_MAX) return -1;
    ++length;
    if (strings_start > size || strings_size > size - (size_t) strings_start ||
        refs_start > size || refs_size > size - (size_t) refs_start || refs_size % sizeof(uint64_t) != 0)
        return -1;
    for (uint64_t relative = 0; relative <= strings_size && length <= strings_size - relative; ++relative) {
        uint64_t string_offset = strings_start + relative, string_address;
        if (memcmp(data + (size_t) string_offset, selector, length) != 0 ||
            !macho_file_offset_to_vm_address(data, size, string_offset, &string_address)) continue;
        for (uint64_t ref = refs_start; ref - refs_start < refs_size; ref += sizeof(uint64_t)) {
            uint64_t pointer = 0;
            enum macho_pointer_result pointer_status = macho_resolve_pointer(data, size, ref, &pointer);
            if (pointer_status == MACHO_POINTER_MALFORMED) return -1;
            if (pointer_status != MACHO_POINTER_RESOLVED) continue;
            if (pointer == string_address) {
                return macho_file_offset_to_vm_address(data, size, ref, result) ? 1 : -1;
            }
        }
    }
    return 0;
}

bool scan_sa_find_selector(const uint8_t *data, size_t size, const char *selector,
                           uint64_t *selector_ref_address)
{
    uint64_t refs_start, refs_size, strings_start, strings_size;
    if (!data || !selector || !selector_ref_address || !macho_validate_scanner_image(data, size)) return false;
    if (!macho_find_section(data, size, "__DATA", "__objc_selrefs", &refs_start, &refs_size) &&
        !macho_find_section(data, size, "__DATA_CONST", "__objc_selrefs", &refs_start, &refs_size)) return false;
    if (refs_size % sizeof(uint64_t) != 0) return false;
    if (macho_find_section(data, size, "__TEXT", "__objc_methname", &strings_start, &strings_size)) {
        int status = selector_in_string_section(data, size, strings_start, strings_size, refs_start,
                                                refs_size, selector, selector_ref_address);
        if (status != 0) return status > 0;
    }
    if (macho_find_section(data, size, "__TEXT", "__cstring", &strings_start, &strings_size)) {
        int status = selector_in_string_section(data, size, strings_start, strings_size, refs_start,
                                                refs_size, selector, selector_ref_address);
        if (status != 0) return status > 0;
    }
    return false;
}
