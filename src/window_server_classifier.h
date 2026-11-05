#ifndef WINDOW_SERVER_CLASSIFIER_H
#define WINDOW_SERVER_CLASSIFIER_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "window_server_snapshot.h"
#include "window_server_tags.h"

enum ws_ax_corroboration
{
    WS_AX_UNAVAILABLE,
    WS_AX_NO_MATCH,
    WS_AX_STANDARD_WINDOW,
    WS_AX_FLOATING_WINDOW,
    WS_AX_DIALOG_WINDOW,
    WS_AX_OTHER
};

enum ws_owner_kind
{
    WS_OWNER_REGULAR,
    WS_OWNER_ACCESSORY,
    WS_OWNER_PROHIBITED,
    WS_OWNER_UNKNOWN
};

enum ws_tag_layout_status
{
    WS_TAG_LAYOUT_SAMPLED,
    WS_TAG_LAYOUT_UNVERIFIED,
    WS_TAG_LAYOUT_UNSUPPORTED
};

enum ws_window_class
{
    WS_WINDOW_CLASS_MANAGEABLE,
    WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE,
    WS_WINDOW_CLASS_SYSTEM_OVERLAY,
    WS_WINDOW_CLASS_UNCERTAIN
};

enum ws_window_classification_reason
{
    WS_REASON_TRACKED_PRESERVED       = UINT64_C(1) << 0,
    WS_REASON_MISSING_BASE_METADATA   = UINT64_C(1) << 1,
    WS_REASON_UNSUPPORTED_TAG_LAYOUT  = UINT64_C(1) << 2,
    WS_REASON_HARD_SYSTEM_TAG         = UINT64_C(1) << 3,
    WS_REASON_NONROOT_PARENT          = UINT64_C(1) << 4,
    WS_REASON_UNSUPPORTED_LEVEL       = UINT64_C(1) << 5,
    WS_REASON_MINIMIZED_EXCLUDED      = UINT64_C(1) << 6,
    WS_REASON_MISSING_SEMANTIC_TAG    = UINT64_C(1) << 7,
    WS_REASON_AX_OTHER                = UINT64_C(1) << 8,
    WS_REASON_AX_NO_MATCH             = UINT64_C(1) << 9,
    WS_REASON_OWNER_PROHIBITED        = UINT64_C(1) << 10,
    WS_REASON_AX_CORROBORATED         = UINT64_C(1) << 11,
    WS_REASON_NO_AX_DOCUMENT          = UINT64_C(1) << 12,
    WS_REASON_FLOATING_WITHOUT_AX     = UINT64_C(1) << 13,
    WS_REASON_INVALID_FRAME           = UINT64_C(1) << 14,
    WS_REASON_OWNER_NOT_REGULAR       = UINT64_C(1) << 15,
    WS_REASON_ATTRIBUTE_BIT_2         = UINT64_C(1) << 16,
    WS_REASON_IGNORE_SCREEN_SHARING   = UINT64_C(1) << 17,
    WS_REASON_UNKNOWN_TAG_BITS        = UINT64_C(1) << 18,
    WS_REASON_AX_UNAVAILABLE_CONSERVATIVE = UINT64_C(1) << 19
};

struct ws_legacy_eligibility_input
{
    const struct window_server_window_record *record;
    bool tracked;
    bool tracked_minimized;
    bool include_minimized;
};

struct ws_window_classification_input
{
    uint64_t valid_fields;
    uint64_t tags;
    uint64_t attributes;
    uint32_t parent_id;
    pid_t pid;
    int level;
    int normal_level;
    int floating_level;
    int modal_level;
    CGRect frame;
    bool tracked;
    bool tracked_minimized;
    bool include_minimized;
    enum ws_owner_kind owner;
    enum ws_ax_corroboration ax;
    enum ws_tag_layout_status tag_layout;
};

struct ws_window_classification
{
    enum ws_window_class window_class;
    uint64_t reasons;
};

struct ws_window_classification_reason_descriptor
{
    uint64_t mask;
    const char *name;
};

bool ws_legacy_window_is_eligible(const struct ws_legacy_eligibility_input *input);
struct ws_window_classification ws_classify_window_candidate(
    const struct ws_window_classification_input *input);
const char *ws_window_class_name(enum ws_window_class value);
const char *ws_ax_corroboration_name(enum ws_ax_corroboration value);
const char *ws_owner_kind_name(enum ws_owner_kind value);
const char *ws_tag_layout_status_name(enum ws_tag_layout_status value);

extern const struct ws_window_classification_reason_descriptor
    ws_window_classification_reason_descriptors[];
extern const size_t ws_window_classification_reason_descriptor_count;

#endif
