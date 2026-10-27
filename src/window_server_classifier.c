#include "window_server_classifier.h"

#include <math.h>

const struct ws_window_classification_reason_descriptor
ws_window_classification_reason_descriptors[] = {
    { WS_REASON_TRACKED_PRESERVED, "tracked_preserved" },
    { WS_REASON_MISSING_BASE_METADATA, "missing_base_metadata" },
    { WS_REASON_UNSUPPORTED_TAG_LAYOUT, "unsupported_tag_layout" },
    { WS_REASON_HARD_SYSTEM_TAG, "hard_system_tag" },
    { WS_REASON_NONROOT_PARENT, "nonroot_parent" },
    { WS_REASON_UNSUPPORTED_LEVEL, "unsupported_level" },
    { WS_REASON_MINIMIZED_EXCLUDED, "minimized_excluded" },
    { WS_REASON_MISSING_SEMANTIC_TAG, "missing_semantic_tag" },
    { WS_REASON_AX_OTHER, "ax_other" },
    { WS_REASON_AX_NO_MATCH, "ax_no_match" },
    { WS_REASON_OWNER_PROHIBITED, "owner_prohibited" },
    { WS_REASON_AX_CORROBORATED, "ax_corroborated" },
    { WS_REASON_NO_AX_DOCUMENT, "no_ax_document" },
    { WS_REASON_FLOATING_WITHOUT_AX, "floating_without_ax" },
    { WS_REASON_INVALID_FRAME, "invalid_frame" },
    { WS_REASON_OWNER_NOT_REGULAR, "owner_not_regular" },
    { WS_REASON_ATTRIBUTE_BIT_2, "attribute_bit_2" },
    { WS_REASON_IGNORE_SCREEN_SHARING, "ignore_for_screen_sharing" },
    { WS_REASON_UNKNOWN_TAG_BITS, "unknown_tag_bits" }
};

const size_t ws_window_classification_reason_descriptor_count =
    sizeof(ws_window_classification_reason_descriptors) /
    sizeof(ws_window_classification_reason_descriptors[0]);

bool ws_legacy_window_is_eligible(const struct ws_legacy_eligibility_input *input)
{
    if (!input || !input->record) return false;
    const struct window_server_window_record *record = input->record;
    if (input->tracked && (input->include_minimized || !input->tracked_minimized)) return true;
    if (record->parent_id != 0 ||
        (record->level != 0 && record->level != 3 && record->level != 8)) return false;

    bool semantic = (record->tags & WINDOW_SERVER_TAG_DOCUMENT) ||
                    ((record->tags & WINDOW_SERVER_TAG_FLOATING) &&
                     (record->tags & WINDOW_SERVER_TAG_MODAL));
    if (((record->attributes & UINT64_C(0x2)) ||
         (record->tags & WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING)) && semantic) {
        return true;
    }
    return input->include_minimized &&
           (record->attributes == 0 || record->attributes == 1) &&
           ((record->tags & WINDOW_SERVER_TAG_MINIATURIZED) ||
            (record->tags & (WINDOW_SERVER_TAG_FULLSCREEN_CAPABLE |
                             WINDOW_SERVER_TAG_FULLSCREEN_TILE_CAPABLE))) &&
           semantic;
}

static bool ws_ax_is_user_facing(enum ws_ax_corroboration ax)
{
    return ax == WS_AX_STANDARD_WINDOW || ax == WS_AX_FLOATING_WINDOW ||
           ax == WS_AX_DIALOG_WINDOW;
}

static bool ws_frame_is_positive(CGRect frame)
{
    return isfinite(frame.origin.x) && isfinite(frame.origin.y) &&
           isfinite(frame.size.width) && isfinite(frame.size.height) &&
           frame.size.width > 0.0 && frame.size.height > 0.0;
}

static struct ws_window_classification ws_classification(enum ws_window_class window_class,
                                                          uint64_t reasons)
{
    return (struct ws_window_classification) { window_class, reasons };
}

struct ws_window_classification ws_classify_window_candidate(
    const struct ws_window_classification_input *input)
{
    if (!input) return ws_classification(WS_WINDOW_CLASS_UNCERTAIN,
                                         WS_REASON_MISSING_BASE_METADATA);
    uint64_t reasons = 0;
    if (input->attributes & UINT64_C(0x2)) reasons |= WS_REASON_ATTRIBUTE_BIT_2;
    if (input->tags & WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING) {
        reasons |= WS_REASON_IGNORE_SCREEN_SHARING;
    }
    if (input->tags & ~WINDOW_SERVER_TAG_KNOWN_MASK) reasons |= WS_REASON_UNKNOWN_TAG_BITS;

    if (input->tracked && (input->include_minimized || !input->tracked_minimized)) {
        return ws_classification(WS_WINDOW_CLASS_MANAGEABLE,
                                 reasons | WS_REASON_TRACKED_PRESERVED);
    }
    if ((input->valid_fields & WINDOW_SERVER_WINDOW_FIELD_BASE) !=
        WINDOW_SERVER_WINDOW_FIELD_BASE) {
        return ws_classification(WS_WINDOW_CLASS_UNCERTAIN,
                                 reasons | WS_REASON_MISSING_BASE_METADATA);
    }
    if (input->tag_layout == WS_TAG_LAYOUT_UNSUPPORTED) {
        return ws_classification(WS_WINDOW_CLASS_UNCERTAIN,
                                 reasons | WS_REASON_UNSUPPORTED_TAG_LAYOUT);
    }
    if (input->tags & (WINDOW_SERVER_TAG_MENU_BAR | WINDOW_SERVER_TAG_DESKTOP_PICTURE)) {
        return ws_classification(WS_WINDOW_CLASS_SYSTEM_OVERLAY,
                                 reasons | WS_REASON_HARD_SYSTEM_TAG);
    }

    bool ax_user_facing = ws_ax_is_user_facing(input->ax);
    if (input->parent_id != 0) {
        return ws_classification(ax_user_facing
                                 ? WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE
                                 : WS_WINDOW_CLASS_UNCERTAIN,
                                 reasons | WS_REASON_NONROOT_PARENT);
    }
    bool relevant_level = input->level == input->normal_level ||
                          input->level == input->floating_level ||
                          input->level == input->modal_level;
    if (!relevant_level) {
        return ws_classification(ax_user_facing
                                 ? WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE
                                 : WS_WINDOW_CLASS_UNCERTAIN,
                                 reasons | WS_REASON_UNSUPPORTED_LEVEL);
    }

    bool document = (input->tags & WINDOW_SERVER_TAG_DOCUMENT) != 0;
    bool floating = (input->tags & WINDOW_SERVER_TAG_FLOATING) != 0;
    bool minimized = input->tracked_minimized ||
                     (input->tags & WINDOW_SERVER_TAG_MINIATURIZED);
    if (minimized && !input->include_minimized) {
        return ws_classification((ax_user_facing || document || floating)
                                 ? WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE
                                 : WS_WINDOW_CLASS_UNCERTAIN,
                                 reasons | WS_REASON_MINIMIZED_EXCLUDED);
    }
    if (!document && !floating) {
        return ws_classification(WS_WINDOW_CLASS_UNCERTAIN,
                                 reasons | WS_REASON_MISSING_SEMANTIC_TAG);
    }
    if (input->ax == WS_AX_OTHER) {
        return ws_classification(WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE,
                                 reasons | WS_REASON_AX_OTHER);
    }
    if (input->ax == WS_AX_NO_MATCH) {
        return ws_classification(WS_WINDOW_CLASS_UNCERTAIN,
                                 reasons | WS_REASON_AX_NO_MATCH);
    }
    if (input->owner == WS_OWNER_PROHIBITED) {
        return ws_classification(WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE,
                                 reasons | WS_REASON_OWNER_PROHIBITED);
    }
    if (ax_user_facing) {
        return ws_classification(WS_WINDOW_CLASS_MANAGEABLE,
                                 reasons | WS_REASON_AX_CORROBORATED);
    }

    if (floating && !document) reasons |= WS_REASON_FLOATING_WITHOUT_AX;
    if (!ws_frame_is_positive(input->frame)) reasons |= WS_REASON_INVALID_FRAME;
    if (input->owner != WS_OWNER_REGULAR) reasons |= WS_REASON_OWNER_NOT_REGULAR;
    if (input->ax == WS_AX_UNAVAILABLE && document && !floating &&
        input->owner == WS_OWNER_REGULAR && input->level == input->normal_level &&
        ws_frame_is_positive(input->frame)) {
        return ws_classification(WS_WINDOW_CLASS_MANAGEABLE,
                                 reasons | WS_REASON_NO_AX_DOCUMENT);
    }
    return ws_classification(WS_WINDOW_CLASS_UNCERTAIN, reasons);
}

const char *ws_window_class_name(enum ws_window_class value)
{
    switch (value) {
    case WS_WINDOW_CLASS_MANAGEABLE: return "manageable";
    case WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE: return "user-facing-nonmanageable";
    case WS_WINDOW_CLASS_SYSTEM_OVERLAY: return "system-overlay";
    case WS_WINDOW_CLASS_UNCERTAIN: return "uncertain";
    }
    return "uncertain";
}

const char *ws_ax_corroboration_name(enum ws_ax_corroboration value)
{
    switch (value) {
    case WS_AX_UNAVAILABLE: return "unavailable";
    case WS_AX_NO_MATCH: return "no-match";
    case WS_AX_STANDARD_WINDOW: return "standard";
    case WS_AX_FLOATING_WINDOW: return "floating";
    case WS_AX_DIALOG_WINDOW: return "dialog";
    case WS_AX_OTHER: return "other";
    }
    return "unavailable";
}

const char *ws_owner_kind_name(enum ws_owner_kind value)
{
    switch (value) {
    case WS_OWNER_REGULAR: return "regular";
    case WS_OWNER_ACCESSORY: return "accessory";
    case WS_OWNER_PROHIBITED: return "prohibited";
    case WS_OWNER_UNKNOWN: return "unknown";
    }
    return "unknown";
}

const char *ws_tag_layout_status_name(enum ws_tag_layout_status value)
{
    switch (value) {
    case WS_TAG_LAYOUT_SAMPLED: return "sampled";
    case WS_TAG_LAYOUT_UNVERIFIED: return "unverified";
    case WS_TAG_LAYOUT_UNSUPPORTED: return "unsupported";
    }
    return "unsupported";
}
