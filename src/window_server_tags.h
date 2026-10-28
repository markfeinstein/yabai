#ifndef WINDOW_SERVER_TAGS_H
#define WINDOW_SERVER_TAGS_H

#include <stddef.h>
#include <stdint.h>

/* Advisory names derived from macOS 26.3.1 reverse engineering, sampled on
 * 27.0.1, and not independently validated on 27.2. Raw masks remain the
 * authoritative production contract. */
#define WINDOW_SERVER_TAG_DOCUMENT                   (UINT64_C(1) << 0)
#define WINDOW_SERVER_TAG_FLOATING                   (UINT64_C(1) << 1)
/* Observed on yabai-owned click-through overlays on macOS 27.0.1. Keep this
 * overlay-only tag out of WINDOW_SERVER_TAG_KNOWN_MASK until a classifier
 * corpus establishes its semantics for application-owned windows. */
#define WINDOW_SERVER_TAG_IGNORE_FOR_EVENTS          (UINT64_C(1) << 9)
#define WINDOW_SERVER_TAG_ON_ALL_WORKSPACES          (UINT64_C(1) << 11)
#define WINDOW_SERVER_TAG_KIT_VISIBLE                (UINT64_C(1) << 13)
#define WINDOW_SERVER_TAG_MODAL                      (UINT64_C(1) << 31)
#define WINDOW_SERVER_TAG_WINDOW_MANAGER_AWARE       (UINT64_C(1) << 32)
#define WINDOW_SERVER_TAG_HIDDEN                     (UINT64_C(1) << 39)
#define WINDOW_SERVER_TAG_FULLSCREEN                 (UINT64_C(1) << 42)
#define WINDOW_SERVER_TAG_SUPER_STICKY                (UINT64_C(1) << 44)
#define WINDOW_SERVER_TAG_MENU_BAR                    (UINT64_C(1) << 46)
#define WINDOW_SERVER_TAG_DESKTOP_PICTURE             (UINT64_C(1) << 49)
#define WINDOW_SERVER_TAG_FULLSCREEN_CAPABLE          (UINT64_C(1) << 56)
#define WINDOW_SERVER_TAG_FULLSCREEN_TILE_CAPABLE     (UINT64_C(1) << 57)
#define WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING   (UINT64_C(1) << 58)
#define WINDOW_SERVER_TAG_MINIATURIZED                 (UINT64_C(1) << 60)

#define WINDOW_SERVER_TAG_KNOWN_MASK ( \
    WINDOW_SERVER_TAG_DOCUMENT | \
    WINDOW_SERVER_TAG_FLOATING | \
    WINDOW_SERVER_TAG_ON_ALL_WORKSPACES | \
    WINDOW_SERVER_TAG_KIT_VISIBLE | \
    WINDOW_SERVER_TAG_MODAL | \
    WINDOW_SERVER_TAG_WINDOW_MANAGER_AWARE | \
    WINDOW_SERVER_TAG_HIDDEN | \
    WINDOW_SERVER_TAG_FULLSCREEN | \
    WINDOW_SERVER_TAG_SUPER_STICKY | \
    WINDOW_SERVER_TAG_MENU_BAR | \
    WINDOW_SERVER_TAG_DESKTOP_PICTURE | \
    WINDOW_SERVER_TAG_FULLSCREEN_CAPABLE | \
    WINDOW_SERVER_TAG_FULLSCREEN_TILE_CAPABLE | \
    WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING | \
    WINDOW_SERVER_TAG_MINIATURIZED)

#define WINDOW_SERVER_TAG_DESCRIPTOR_LIST(X) \
    X(WINDOW_SERVER_TAG_DOCUMENT,                 0,  "document") \
    X(WINDOW_SERVER_TAG_FLOATING,                 1,  "floating") \
    X(WINDOW_SERVER_TAG_ON_ALL_WORKSPACES,        11, "on_all_workspaces") \
    X(WINDOW_SERVER_TAG_KIT_VISIBLE,              13, "kit_visible") \
    X(WINDOW_SERVER_TAG_MODAL,                    31, "modal") \
    X(WINDOW_SERVER_TAG_WINDOW_MANAGER_AWARE,     32, "window_manager_aware") \
    X(WINDOW_SERVER_TAG_HIDDEN,                   39, "hidden") \
    X(WINDOW_SERVER_TAG_FULLSCREEN,               42, "fullscreen") \
    X(WINDOW_SERVER_TAG_SUPER_STICKY,              44, "super_sticky") \
    X(WINDOW_SERVER_TAG_MENU_BAR,                  46, "menu_bar") \
    X(WINDOW_SERVER_TAG_DESKTOP_PICTURE,           49, "desktop_picture") \
    X(WINDOW_SERVER_TAG_FULLSCREEN_CAPABLE,        56, "fullscreen_capable") \
    X(WINDOW_SERVER_TAG_FULLSCREEN_TILE_CAPABLE,   57, "fullscreen_tile_capable") \
    X(WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING, 58, "ignore_for_screen_sharing") \
    X(WINDOW_SERVER_TAG_MINIATURIZED,               60, "miniaturized")

struct window_server_tag_descriptor
{
    uint64_t mask;
    unsigned bit;
    const char *name;
};

static const struct window_server_tag_descriptor window_server_tag_descriptors[] = {
#define WINDOW_SERVER_TAG_DESCRIPTOR(mask, bit, name) { mask, bit, name },
    WINDOW_SERVER_TAG_DESCRIPTOR_LIST(WINDOW_SERVER_TAG_DESCRIPTOR)
#undef WINDOW_SERVER_TAG_DESCRIPTOR
};

static const size_t window_server_tag_descriptor_count =
    sizeof(window_server_tag_descriptors) / sizeof(window_server_tag_descriptors[0]);

#endif
