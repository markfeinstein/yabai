#import <Foundation/Foundation.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/osax/arm64_payload.m"
#include "macho_parser.h"
#include "scan_sa_core.h"

struct pattern_spec {
    const char *name;
    uint64_t (*offset)(NSOperatingSystemVersion);
    const char *(*pattern)(NSOperatingSystemVersion);
};

static bool report_pattern(const uint8_t *data, size_t size, NSOperatingSystemVersion version,
                           const struct pattern_spec *spec, bool *matched)
{
    uint64_t start = spec->offset(version), first = 0;
    const char *text = spec->pattern(version);
    size_t matches = 0;
    *matched = false;
    if (!text) {
        printf("%-18s UNSUPPORTED\n", spec->name);
        return true;
    }
    if (!scan_sa_pattern(data, size, start, 0x1286a0, text, &first, &matches)) {
        fprintf(stderr, "%s: invalid pattern\n", spec->name);
        return false;
    }
    if (matches == 0) {
        printf("%-18s MISSING  scan-start=0x%llx bytes=0x%x\n", spec->name,
               (unsigned long long) start, 0x1286a0);
        return true;
    }
    printf("%-18s MATCH    offset=0x%llx matches=%zu\n", spec->name,
           (unsigned long long) first, matches);
    *matched = true;
    return true;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/System/Library/CoreServices/Dock.app/Contents/MacOS/Dock";
    int fd = open(path, O_RDONLY);
    if (fd == -1) { fprintf(stderr, "open %s: %s\n", path, strerror(errno)); return EXIT_FAILURE; }
    struct stat status;
    if (fstat(fd, &status) == -1) {
        fprintf(stderr, "stat %s: %s\n", path, strerror(errno)); close(fd); return EXIT_FAILURE;
    }
    if (!S_ISREG(status.st_mode) || status.st_size <= 0 || (uintmax_t) status.st_size > SIZE_MAX) {
        fprintf(stderr, "%s: expected a nonempty regular file of representable size\n", path);
        close(fd); return EXIT_FAILURE;
    }
    size_t mapped_size = (size_t) status.st_size;
    uint8_t *data = mmap(NULL, mapped_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "mmap %s: %s\n", path, strerror(errno)); close(fd); return EXIT_FAILURE;
    }
    if (!macho_validate_scanner_image(data, mapped_size)) {
        fprintf(stderr, "%s: invalid or unsupported arm64 executable Mach-O image\n", path);
        munmap(data, mapped_size); close(fd); return EXIT_FAILURE;
    }

    NSOperatingSystemVersion target = { .majorVersion = 27, .minorVersion = 0, .patchVersion = 0 };
    const struct pattern_spec patterns[] = {
        { "dock_spaces_5378n", get_dock_spaces_offset, get_dock_spaces_pattern },
        { "dock_spaces_5388g", get_dock_spaces_offset, get_dock_spaces_fallback_pattern },
        { "dppm", get_dppm_offset, get_dppm_pattern },
        { "fix_animation", get_fix_animation_offset, get_fix_animation_pattern },
        { "add_space", get_add_space_offset, get_add_space_pattern },
        { "remove_space", get_remove_space_offset, get_remove_space_pattern },
        { "move_space", get_move_space_offset, get_move_space_pattern },
        { "set_front_window", get_set_front_window_offset, get_set_front_window_pattern },
    };
    const char *selectors[] = {
        "_handleEvent:", "addSpace:forDisplayUUID:", "doBindingCommand:display:",
        "moveSpace:toDisplay:displayUUID:", "removeSpace:",
    };

    bool success = true;
    printf("Dock: %s\nTesting macOS 27 arm64 patterns against %zu bytes\n", path, mapped_size);
    bool dock_match_a = false, dock_match_b = false, dock_satisfied = false;
    bool dock_valid_a = report_pattern(data, mapped_size, target, &patterns[0], &dock_match_a);
    bool dock_valid_b = report_pattern(data, mapped_size, target, &patterns[1], &dock_match_b);
    const char *dock_alternatives[] = { patterns[0].pattern(target), patterns[1].pattern(target) };
    if (!dock_valid_a || !dock_valid_b ||
        !scan_sa_pattern_group(data, mapped_size, patterns[0].offset(target), 0x1286a0,
                               dock_alternatives, 2, &dock_satisfied) || !dock_satisfied) success = false;
    for (size_t index = 2; index < sizeof(patterns) / sizeof(patterns[0]); ++index) {
        bool matched = false;
        const char *text = patterns[index].pattern(target);
        if (!report_pattern(data, mapped_size, target, &patterns[index], &matched) ||
            (text && !matched)) success = false;
    }

    printf("Objective-C selector references\n");
    for (size_t index = 0; index < sizeof(selectors) / sizeof(selectors[0]); ++index) {
        uint64_t address;
        if (scan_sa_find_selector(data, mapped_size, selectors[index], &address))
            printf("%-32s selref=0x%llx\n", selectors[index], (unsigned long long) address);
        else {
            printf("%-32s selref=MISSING_OR_UNSUPPORTED\n", selectors[index]);
            success = false;
        }
    }
    munmap(data, mapped_size); close(fd);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
