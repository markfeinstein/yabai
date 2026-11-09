#define asm__call_add_space(v0,v1,func) \
    __asm__("mov x0, %0\n""mov x20, %1\n" : :"r"(v0), "r"(v1) :"x0", "x20"); ((void (*)())(func))();

#define asm__call_move_space(v0,v1,v2,v3,func) \
    __asm__("mov x0, %0\n""mov x1, %1\n""mov x2, %2\n""mov x20, %3\n" : :"r"(v0), "r"(v1), "r"(v2), "r"(v3) :"x0", "x1", "x2", "x20"); ((void (*)())(func))();

uint64_t get_dock_spaces_offset(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) return os_version.minorVersion >= 2 ? 0x20000 : 0x30000;
    if (os_version.majorVersion == 26) return 0x30000;
    return 0;
}

uint64_t get_dppm_offset(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) return 0x40000;
    if (os_version.majorVersion == 26) return 0x70000;
    return 0;
}

uint64_t get_fix_animation_offset(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) return 0x170000;
    if (os_version.majorVersion == 26) return 0x250000;
    return 0;
}

uint64_t get_add_space_offset(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) return 0x170000;
    if (os_version.majorVersion == 26) return 0x250000;
    return 0;
}

uint64_t get_remove_space_offset(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) return 0x120000;
    if (os_version.majorVersion == 26) return 0x1e0000;
    return 0;
}

uint64_t get_move_space_offset(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) return os_version.minorVersion >= 2 ? 0x100000 : 0x170000;
    if (os_version.majorVersion == 26) return 0x1c0000;
    return 0;
}

uint64_t get_set_front_window_offset(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27 || os_version.majorVersion == 26) return 0x10000;
    return 0;
}

const char *get_dock_spaces_pattern(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) {
        if (os_version.minorVersion >= 2) {
            // clearBindingForBundleID: in doBindingCommand:display:, both arm64e slices.
            return "?8 ?? ?? ?? 08 ?? ?? 91 00 01 40 F9 E2 03 13 AA ?? ?? ?? 94 ?? ?? ?? ?? 08";
        }
        // Dock 26A5378n. The second global load distinguishes dock.spaces
        // from the similar binding sequence in the 27.0/27.1 Dock.
        return "88 ?? ?? ?? 08 01 19 91 00 01 40 F9 E2 03 13 AA ?? ?? ?? 94 88 ?? ?? ?? 08 E1 1A 91 00 01 40 F9";
    }
    if (os_version.majorVersion == 26) {
        // Pulling out of doBindingCommand:display (search decompiled text in ghidra) function.
        return "?8 ?? ?? ?? 08 ?? ?? 91 00 01 40 F9 E2 03 13 AA ?? ?? ?? 94 ?? ?? ?? ?? 08";
    }
    return NULL;
}

const char *get_dock_spaces_fallback_pattern(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion != 27) return NULL;
    if (os_version.minorVersion < 2) {
        // Broader semantic fallback for later 27.0/27.1 builds. Runtime
        // candidate validation disambiguates multiple byte matches.
        return "?8 ?? ?? ?? 08 ?? ?? 91 00 01 40 F9 E2 03 13 AA ?? ?? ?? 94 ?? ?? ?? ?? 08";
    }
    // Dock 26A5388g. Kept as a fallback for cases where Objective-C
    // runtime metadata is unavailable or the semantic resolver fails.
    return "88 ?? ?? ?? 08 41 19 91 00 01 40 F9 E2 03 13 AA ?? ?? ?? 94 88 ?? ?? ?? 08 21 1B 91 00 01 40 F9";
}

const char *get_dppm_pattern(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) {
        return os_version.minorVersion >= 2 ? NULL : "?? ?? 00 ?? 08 ?? ?? 91 00 01 40 F9 E2 03 16 AA E3 03 19 AA ?? ?? ?? 94";
    }
    if (os_version.majorVersion == 26) {
        // Pulling from function 'DPRemoteConnection::_handleEvent:'.
        return "?? ?? 00 ?? 08 ?? ?? 91 00 01 40 F9 E2 03 16 AA E3 03 19 AA ?? ?? ?? 94";
    }
    return NULL;
}

const char *get_fix_animation_pattern(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27 || os_version.majorVersion == 26) {
        return "00 10 6A 1E A8 ?? ?? D1 ?? 01 ?? F8";
    }
    return NULL;
}

const char *get_add_space_pattern(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) {
        return "?? ?? ?? ?? E1 03 1E AA ?? ?? ?? 97 FE 03 01 AA FD 7B 05 A9 FD 43 01 91 F3 03 14 AA F5 03";
    }
    if (os_version.majorVersion == 26) {
        if (os_version.minorVersion >= 4) {
            return "7F 23 03 D5 E1 03 1E AA ?? ?? ?? 97 FE 03 01 AA FD 7B 05 A9 FD 43 01 91 F3 03 14 AA F5 03";
        }
        return "7F 23 03 D5 FF C3 01 D1 E1 03 1E AA ?? ?? 00 94 FE 03 01 AA FD 7B 06 A9 FD 83 01 91 F3 03";
    }
    return NULL;
}

const char *get_remove_space_pattern(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) {
        return "?? ?? ?? ?? FF ?? ?? D1 FC ?? ?? A9 FA ?? ?? A9 F8 ?? ?? A9 F6 ?? ?? A9 F4 ?? ?? A9 FD ?? ?? A9 FD ?? ?? 91 ?? 03 03 AA F5 03 02 AA F4 03 01 AA";
    }
    if (os_version.majorVersion == 26) {
        return "7F 23 03 D5 FF ?? ?? D1 FC ?? ?? A9 FA ?? ?? A9 F8 ?? ?? A9 F6 ?? ?? A9 F4 ?? ?? A9 FD ?? ?? A9 FD ?? ?? 91 ?? 03 03 AA F5 03 02 AA F4 03 01 AA";
    }
    return NULL;
}

const char *get_move_space_pattern(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) {
        if (os_version.minorVersion >= 2) {
            // Dock's Swift handler for WindowManager SpaceChange.movedToDisplay reports.
            return "?? ?? ?? ?? E5 03 1E AA ?? ?? ?? 97 FE 03 05 AA FD 7B ?? A9 FD ?? ?? 91 F6 03 14 AA F7 03 04 AA F4 03 03 AA F8 03 02 AA F9 03 01 AA E1 03 16 AA";
        }
        return "?? ?? ?? ?? E3 03 1E AA ?? ?? ?? 97 FE 03 03 AA FD 7B ?? A9 FD ?? ?? 91 F6 03 14 AA";
    }
    if (os_version.majorVersion == 26) {
        return "7F 23 03 D5 E3 03 1E AA ?? ?? ?? 97 FE 03 03 AA FD 7B ?? A9 FD ?? ?? 91 F6 03 14 AA";
    }
    return NULL;
}

const char *get_set_front_window_pattern(NSOperatingSystemVersion os_version)
{
    if (os_version.majorVersion == 27) {
        // The legacy CoreDockWindows focus entrypoint is unused by yabai and
        // absent from the 27.2 Dock; do not make it a runtime capability.
        return NULL;
    }
    if (os_version.majorVersion == 26) {
        return "21 ?? ?? 34 7F 23 03 D5 FF ?? 01 D1 F6 ?? 04 A9 F4 ?? 05 A9 FD ?? 06 A9 FD ?? 01 91";
    }
    return NULL;
}
