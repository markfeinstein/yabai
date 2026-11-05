#ifndef YABAI_SCAN_SA_CORE_H
#define YABAI_SCAN_SA_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool scan_sa_pattern(const uint8_t *data, size_t size, uint64_t start, uint64_t candidate_span,
                     const char *pattern_text, uint64_t *first_match, size_t *match_count);
bool scan_sa_pattern_group(const uint8_t *data, size_t size, uint64_t start,
                           uint64_t candidate_span, const char *const *patterns,
                           size_t pattern_count, bool *satisfied);
bool scan_sa_find_selector(const uint8_t *data, size_t size, const char *selector,
                           uint64_t *selector_ref_address);

#endif
