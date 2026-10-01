#ifndef YABAI_MANAGED_SPACE_H
#define YABAI_MANAGED_SPACE_H

static inline bool cf_is_type(CFTypeRef value, CFTypeID type)
{
    return value && CFGetTypeID(value) == type;
}

static inline CFArrayRef managed_space_display_spaces(CFTypeRef display)
{
    if (!cf_is_type(display, CFDictionaryGetTypeID())) return NULL;
    CFTypeRef spaces = CFDictionaryGetValue((CFDictionaryRef) display, CFSTR("Spaces"));
    return cf_is_type(spaces, CFArrayGetTypeID()) ? (CFArrayRef) spaces : NULL;
}

static inline bool managed_space_id(CFTypeRef space, uint64_t *sid)
{
    if (!sid || !cf_is_type(space, CFDictionaryGetTypeID())) return false;
    CFTypeRef number = CFDictionaryGetValue((CFDictionaryRef) space, CFSTR("id64"));
    if (!cf_is_type(number, CFNumberGetTypeID())) return false;
    return CFNumberGetValue((CFNumberRef) number, kCFNumberSInt64Type, sid);
}

static inline bool cfindex_to_int(CFIndex value, int *result)
{
    if (!result || value < 0 || (uint64_t) value > INT_MAX) return false;
    *result = (int) value;
    return true;
}

static inline bool managed_space_count_valid_checked(CFArrayRef spaces, int *count)
{
    if (count) *count = 0;
    if (!count || !cf_is_type(spaces, CFArrayGetTypeID())) return false;
    int result = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(spaces); ++i) {
        uint64_t sid;
        if (managed_space_id(CFArrayGetValueAtIndex(spaces, i), &sid)) {
            if (result == INT_MAX) return false;
            ++result;
        }
    }
    *count = result;
    return true;
}

static inline int managed_space_count_valid(CFArrayRef spaces)
{
    int result = 0;
    managed_space_count_valid_checked(spaces, &result);
    return result;
}

static inline int managed_space_copy_valid(CFArrayRef spaces, uint64_t *list)
{
    if (!cf_is_type(spaces, CFArrayGetTypeID())) return 0;
    int result = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(spaces); ++i) {
        uint64_t sid;
        if (managed_space_id(CFArrayGetValueAtIndex(spaces, i), &sid)) list[result++] = sid;
    }
    return result;
}

static inline CFArrayRef managed_space_spaces_for_display(CFTypeRef topology, CFStringRef identifier)
{
    if (!identifier || !cf_is_type(identifier, CFStringGetTypeID()) ||
        !cf_is_type(topology, CFArrayGetTypeID())) return NULL;

    CFArrayRef displays = (CFArrayRef) topology;
    for (CFIndex i = 0; i < CFArrayGetCount(displays); ++i) {
        CFTypeRef display = CFArrayGetValueAtIndex(displays, i);
        if (!cf_is_type(display, CFDictionaryGetTypeID())) continue;
        CFTypeRef candidate = CFDictionaryGetValue((CFDictionaryRef) display, CFSTR("Display Identifier"));
        if (!cf_is_type(candidate, CFStringGetTypeID()) || !CFEqual(identifier, candidate)) continue;
        return managed_space_display_spaces(display);
    }
    return NULL;
}

static inline uint64_t managed_space_select(CFTypeRef topology, int requested_index)
{
    if (requested_index < 1 || !cf_is_type(topology, CFArrayGetTypeID())) return 0;
    int index = 1;
    CFArrayRef displays = (CFArrayRef) topology;
    for (CFIndex i = 0; i < CFArrayGetCount(displays); ++i) {
        CFArrayRef spaces = managed_space_display_spaces(CFArrayGetValueAtIndex(displays, i));
        if (!spaces) continue;
        for (CFIndex j = 0; j < CFArrayGetCount(spaces); ++j) {
            uint64_t sid;
            if (!managed_space_id(CFArrayGetValueAtIndex(spaces, j), &sid)) continue;
            if (index == requested_index) return sid;
            if (index == INT_MAX) return 0;
            ++index;
        }
    }
    return 0;
}

static inline int managed_space_index(CFTypeRef topology, uint64_t requested_sid)
{
    if (!cf_is_type(topology, CFArrayGetTypeID())) return 0;
    int index = 1;
    CFArrayRef displays = (CFArrayRef) topology;
    for (CFIndex i = 0; i < CFArrayGetCount(displays); ++i) {
        CFArrayRef spaces = managed_space_display_spaces(CFArrayGetValueAtIndex(displays, i));
        if (!spaces) continue;
        for (CFIndex j = 0; j < CFArrayGetCount(spaces); ++j) {
            uint64_t sid;
            if (!managed_space_id(CFArrayGetValueAtIndex(spaces, j), &sid)) continue;
            if (sid == requested_sid) return index;
            if (index == INT_MAX) return 0;
            ++index;
        }
    }
    return 0;
}

static inline uint64_t managed_space_previous(CFTypeRef topology, uint64_t requested_sid)
{
    uint64_t previous = 0;
    int index = managed_space_index(topology, requested_sid);
    if (index > 1) previous = managed_space_select(topology, index - 1);
    return previous;
}

static inline uint64_t managed_space_next(CFTypeRef topology, uint64_t requested_sid)
{
    int index = managed_space_index(topology, requested_sid);
    return index ? managed_space_select(topology, index + 1) : 0;
}

static inline uint64_t managed_space_first(CFTypeRef topology)
{
    return managed_space_select(topology, 1);
}

static inline uint64_t managed_space_last(CFTypeRef topology)
{
    if (!cf_is_type(topology, CFArrayGetTypeID())) return 0;
    uint64_t result = 0;
    CFArrayRef displays = (CFArrayRef) topology;
    for (CFIndex i = 0; i < CFArrayGetCount(displays); ++i) {
        CFArrayRef spaces = managed_space_display_spaces(CFArrayGetValueAtIndex(displays, i));
        if (!spaces) continue;
        for (CFIndex j = 0; j < CFArrayGetCount(spaces); ++j) {
            uint64_t sid;
            if (managed_space_id(CFArrayGetValueAtIndex(spaces, j), &sid)) result = sid;
        }
    }
    return result;
}

#endif
