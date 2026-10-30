#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "../src/misc/extern.h"
#include "../src/window_server_snapshot.c"
#include "../src/window_server_classifier.c"
#include "window_server_tag_corpus_core.c"

static bool corpus_number(NSDictionary *dictionary, CFStringRef key, int64_t *value)
{
    id object = dictionary[(NSString *) key];
    if (![object isKindOfClass:[NSNumber class]]) return false;
    *value = [object longLongValue];
    return true;
}

static void corpus_usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --scenario NAME --capture-set ID --repeat-index N "
            "--source-revision REV --output PATH [--ax-mode auto|unavailable] "
            "[--separate-spaces yes|no|unknown] [--stage-manager on|off|unknown] "
            "[--compare-classifier [--labels PATH] [--label-template PATH]]\n",
            program);
}

static int corpus_public_index(const struct window_server_tag_corpus_public_window *windows,
                               size_t count, uint32_t window_id)
{
    for (size_t i = 0; i < count; ++i) if (windows[i].window_id == window_id) return (int) i;
    return -1;
}

static enum ws_owner_kind corpus_owner_kind(pid_t pid)
{
    NSRunningApplication *application =
        [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (!application) return WS_OWNER_UNKNOWN;
    switch ([application activationPolicy]) {
    case NSApplicationActivationPolicyRegular: return WS_OWNER_REGULAR;
    case NSApplicationActivationPolicyAccessory: return WS_OWNER_ACCESSORY;
    case NSApplicationActivationPolicyProhibited: return WS_OWNER_PROHIBITED;
    }
    return WS_OWNER_UNKNOWN;
}

static enum ws_ax_corroboration corpus_ax_class(CFTypeRef role, CFTypeRef subrole)
{
    if (!role || CFGetTypeID(role) != CFStringGetTypeID() ||
        !CFEqual(role, kAXWindowRole) || !subrole ||
        CFGetTypeID(subrole) != CFStringGetTypeID()) return WS_AX_OTHER;
    if (CFEqual(subrole, kAXStandardWindowSubrole)) return WS_AX_STANDARD_WINDOW;
    if (CFEqual(subrole, kAXFloatingWindowSubrole)) return WS_AX_FLOATING_WINDOW;
    if (CFEqual(subrole, kAXDialogSubrole)) return WS_AX_DIALOG_WINDOW;
    return WS_AX_OTHER;
}

static void corpus_mark_owner_ax_error(
    const struct window_server_tag_corpus_public_window *windows, size_t count,
    struct window_server_tag_corpus_comparison_window *comparison,
    int owner_pid, enum window_server_tag_corpus_ax_observation observation)
{
    for (size_t i = 0; i < count; ++i) {
        if (windows[i].owner_pid != owner_pid) continue;
        comparison[i].ax = WS_AX_UNAVAILABLE;
        comparison[i].ax_observation = observation;
    }
}

static void corpus_enrich_ax(const struct window_server_tag_corpus_public_window *windows,
                             size_t count,
                             struct window_server_tag_corpus_comparison_window *comparison,
                             enum window_server_tag_corpus_ax_mode mode)
{
    if (mode == WINDOW_SERVER_TAG_CORPUS_AX_UNAVAILABLE || !AXIsProcessTrusted()) return;
    for (size_t i = 0; i < count; ++i) {
        comparison[i].ax = WS_AX_NO_MATCH;
        comparison[i].ax_observation = WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_NO_MATCH;
    }
    for (size_t owner = 0; owner < count; ++owner) {
        bool first = true;
        for (size_t previous = 0; previous < owner; ++previous) {
            if (windows[previous].owner_pid == windows[owner].owner_pid) { first = false; break; }
        }
        if (!first || windows[owner].owner_pid <= 0) continue;
        AXUIElementRef application = AXUIElementCreateApplication(windows[owner].owner_pid);
        CFTypeRef value = NULL;
        AXError error = AXUIElementCopyAttributeValue(application, kAXWindowsAttribute, &value);
        CFRelease(application);
        if (error != kAXErrorSuccess || !value || CFGetTypeID(value) != CFArrayGetTypeID()) {
            corpus_mark_owner_ax_error(windows, count, comparison, windows[owner].owner_pid,
                                       WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_INVENTORY_ERROR);
            if (value) CFRelease(value);
            continue;
        }
        CFArrayRef ax_windows = value;
        CFIndex ax_count = CFArrayGetCount(ax_windows);
        for (CFIndex index = 0; index < ax_count; ++index) {
            AXUIElementRef ax_window = (AXUIElementRef) CFArrayGetValueAtIndex(ax_windows, index);
            uint32_t window_id = 0;
            if (_AXUIElementGetWindow(ax_window, &window_id) != kAXErrorSuccess) continue;
            int public_index = corpus_public_index(windows, count, window_id);
            if (public_index < 0) continue;
            CFTypeRef role = NULL, subrole = NULL, title = NULL;
            AXError role_error = AXUIElementCopyAttributeValue(ax_window, kAXRoleAttribute, &role);
            AXError subrole_error = AXUIElementCopyAttributeValue(ax_window, kAXSubroleAttribute, &subrole);
            AXError title_error = AXUIElementCopyAttributeValue(ax_window, kAXTitleAttribute, &title);
            if (role_error != kAXErrorSuccess || subrole_error != kAXErrorSuccess ||
                !role || !subrole || CFGetTypeID(role) != CFStringGetTypeID() ||
                CFGetTypeID(subrole) != CFStringGetTypeID()) {
                comparison[public_index].ax = WS_AX_UNAVAILABLE;
                comparison[public_index].ax_observation =
                    WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_ROLE_ERROR;
            } else {
                comparison[public_index].ax = corpus_ax_class(role, subrole);
                comparison[public_index].ax_observation =
                    WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_MATCHED;
            }
            if (title_error == kAXErrorSuccess && title && CFGetTypeID(title) == CFStringGetTypeID()) {
                comparison[public_index].title = CFStringGetLength((CFStringRef) title)
                                               ? WINDOW_SERVER_TITLE_NONEMPTY
                                               : WINDOW_SERVER_TITLE_EMPTY;
            }
            if (role) CFRelease(role);
            if (subrole) CFRelease(subrole);
            if (title) CFRelease(title);
        }
        CFRelease(ax_windows);
    }
}

static bool corpus_fixture_role_is_valid(NSString *role)
{
    static NSSet<NSString *> *roles;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        roles = [[NSSet alloc] initWithArray:@[
            @"primary-document", @"root-dialog", @"child-sheet", @"floating-panel",
            @"menu-negative", @"overlay-negative", @"electron-main", @"electron-helper",
            @"fullscreen", @"split-view", @"minimized", @"restored",
            @"stage-manager-off", @"stage-manager-on", @"stage-manager-shelf",
            @"native-tab-active", @"native-tab-inactive", @"native-tab-detached",
            @"native-tab-bar", @"menu-extra-negative", @"dock-negative",
            @"system-overlay-negative", @"bit58-ordinary"
        ]];
    });
    return [role isKindOfClass:[NSString class]] && [roles containsObject:role];
}

static bool corpus_parse_class(NSString *value, enum ws_window_class *result)
{
    if (![value isKindOfClass:[NSString class]]) return false;
    for (int candidate = WS_WINDOW_CLASS_MANAGEABLE;
         candidate <= WS_WINDOW_CLASS_UNCERTAIN; ++candidate) {
        if ([value isEqualToString:[NSString stringWithUTF8String:
                                    ws_window_class_name((enum ws_window_class) candidate)]]) {
            *result = (enum ws_window_class) candidate;
            return true;
        }
    }
    return false;
}

static bool corpus_parse_adjudication(NSString *value,
                                        enum window_server_tag_corpus_adjudication *result)
{
    if ([value isEqualToString:@"pending"]) {
        *result = WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_PENDING; return true;
    }
    if ([value isEqualToString:@"confirmed"]) {
        *result = WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_CONFIRMED; return true;
    }
    if ([value isEqualToString:@"rejected"]) {
        *result = WINDOW_SERVER_TAG_CORPUS_ADJUDICATION_REJECTED; return true;
    }
    return false;
}

static bool corpus_parse_bit58_state(NSString *value,
                                     enum window_server_tag_corpus_bit58_state *result)
{
    if ([value isEqualToString:@"not-applicable"]) {
        *result = WINDOW_SERVER_TAG_CORPUS_BIT58_NOT_APPLICABLE; return true;
    }
    if ([value isEqualToString:@"clear"]) {
        *result = WINDOW_SERVER_TAG_CORPUS_BIT58_CLEAR; return true;
    }
    if ([value isEqualToString:@"set"]) {
        *result = WINDOW_SERVER_TAG_CORPUS_BIT58_SET; return true;
    }
    return false;
}

static bool corpus_write_label_template(
    const char *path, const struct window_server_tag_corpus_public_window *windows, size_t count)
{
    if (!path || !path[0]) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    FILE *stream = fdopen(fd, "w");
    bool success = stream &&
        fputs("{\"schema\":\"yabai.window-server-tag-labels/v2\",\"windows\":[", stream) != EOF;
    for (size_t i = 0; success && i < count; ++i) {
        success = (!i || fputc(',', stream) != EOF) &&
            fprintf(stream,
                    "{\"window_id\":%u,\"ordinal\":%zu,"
                    "\"fixture_id\":\"replace\",\"fixture_role\":\"replace\","
                    "\"expected_normal\":\"uncertain\","
                    "\"expected_include_minimized\":\"uncertain\","
                    "\"adjudication_status\":\"pending\","
                    "\"bit58_state\":\"not-applicable\"}",
                    windows[i].window_id, i) >= 0;
    }
    if (success) success = fputs("]}\n", stream) != EOF && fflush(stream) == 0;
    if (stream) {
        if (fclose(stream) != 0) success = false;
    } else {
        close(fd);
    }
    if (!success) unlink(path);
    return success;
}

static bool corpus_load_labels(const char *path,
                               const struct window_server_tag_corpus_public_window *windows,
                               size_t count,
                               struct window_server_tag_corpus_comparison_window *comparison)
{
    struct stat before;
    if (!path || lstat(path, &before) != 0 || !S_ISREG(before.st_mode) ||
        (before.st_mode & 077) != 0 || before.st_size < 0 || before.st_size > 1024 * 1024) return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    NSMutableData *data = [NSMutableData dataWithLength:(NSUInteger) before.st_size];
    uint8_t *bytes = [data mutableBytes];
    size_t used = 0;
    while (used < (size_t) before.st_size) {
        ssize_t length = read(fd, bytes + used, (size_t) before.st_size - used);
        if (length <= 0) { close(fd); return false; }
        used += (size_t) length;
    }
    struct stat after;
    bool stable = fstat(fd, &after) == 0 && before.st_dev == after.st_dev &&
                  before.st_ino == after.st_ino && before.st_size == after.st_size &&
                  before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec &&
                  before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec;
    close(fd);
    if (!stable) return false;
    id object = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    if (![object isKindOfClass:[NSDictionary class]] ||
        ![object[@"schema"] isEqualToString:@"yabai.window-server-tag-labels/v2"] ||
        ![object[@"windows"] isKindOfClass:[NSArray class]]) return false;
    for (id label in object[@"windows"]) {
        if (![label isKindOfClass:[NSDictionary class]] ||
            ![label[@"window_id"] isKindOfClass:[NSNumber class]] ||
            ![label[@"fixture_id"] isKindOfClass:[NSString class]] ||
            !corpus_fixture_role_is_valid(label[@"fixture_role"]) ||
            ![label[@"adjudication_status"] isKindOfClass:[NSString class]] ||
            ![label[@"bit58_state"] isKindOfClass:[NSString class]]) return false;
        long long raw_id = [label[@"window_id"] longLongValue];
        if (raw_id <= 0 || raw_id > UINT32_MAX) return false;
        int index = corpus_public_index(windows, count, (uint32_t) raw_id);
        if (index < 0 || comparison[index].label.present) return false;
        enum ws_window_class normal, minimized;
        enum window_server_tag_corpus_adjudication adjudication;
        enum window_server_tag_corpus_bit58_state bit58_state;
        if (!corpus_parse_class(label[@"expected_normal"], &normal) ||
            !corpus_parse_class(label[@"expected_include_minimized"], &minimized) ||
            !corpus_parse_adjudication(label[@"adjudication_status"], &adjudication) ||
            !corpus_parse_bit58_state(label[@"bit58_state"], &bit58_state)) return false;
        NSString *reason = label[@"adjudication_reason"];
        NSString *evidence = label[@"evidence_ref"];
        NSString *pair = label[@"control_pair_id"];
        if ((reason && ![reason isKindOfClass:[NSString class]]) ||
            (evidence && ![evidence isKindOfClass:[NSString class]]) ||
            (pair && ![pair isKindOfClass:[NSString class]])) return false;
        comparison[index].label = (struct window_server_tag_corpus_label) {
            .present = true,
            .fixture_id = [label[@"fixture_id"] UTF8String],
            .fixture_role = [label[@"fixture_role"] UTF8String],
            .expected_normal = normal,
            .expected_include_minimized = minimized,
            .adjudication = adjudication,
            .adjudication_reason = [reason UTF8String],
            .evidence_ref = [evidence UTF8String],
            .control_pair_id = [pair UTF8String],
            .bit58_state = bit58_state
        };
    }
    return true;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *scenario = NULL;
        const char *capture_set_id = NULL;
        const char *source_revision = NULL;
        const char *output = NULL;
        const char *labels = NULL;
        const char *label_template = NULL;
        const char *separate_spaces = "unknown";
        const char *stage_manager = "unknown";
        unsigned repeat_index = 0;
        enum window_server_tag_corpus_ax_mode ax_mode = WINDOW_SERVER_TAG_CORPUS_AX_AUTO;
        bool compare_classifier = false;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) scenario = argv[++i];
            else if (strcmp(argv[i], "--capture-set") == 0 && i + 1 < argc) capture_set_id = argv[++i];
            else if (strcmp(argv[i], "--repeat-index") == 0 && i + 1 < argc) {
                char *end = NULL; unsigned long value = strtoul(argv[++i], &end, 10);
                if (!end || *end || !value || value > UINT_MAX) { corpus_usage(argv[0]); return EXIT_FAILURE; }
                repeat_index = (unsigned) value;
            }
            else if (strcmp(argv[i], "--source-revision") == 0 && i + 1 < argc) source_revision = argv[++i];
            else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output = argv[++i];
            else if (strcmp(argv[i], "--ax-mode") == 0 && i + 1 < argc) {
                const char *value = argv[++i];
                if (strcmp(value, "auto") == 0) ax_mode = WINDOW_SERVER_TAG_CORPUS_AX_AUTO;
                else if (strcmp(value, "unavailable") == 0) ax_mode = WINDOW_SERVER_TAG_CORPUS_AX_UNAVAILABLE;
                else { corpus_usage(argv[0]); return EXIT_FAILURE; }
            }
            else if (strcmp(argv[i], "--separate-spaces") == 0 && i + 1 < argc) separate_spaces = argv[++i];
            else if (strcmp(argv[i], "--stage-manager") == 0 && i + 1 < argc) stage_manager = argv[++i];
            else if (strcmp(argv[i], "--compare-classifier") == 0) compare_classifier = true;
            else if (strcmp(argv[i], "--labels") == 0 && i + 1 < argc) labels = argv[++i];
            else if (strcmp(argv[i], "--label-template") == 0 && i + 1 < argc) label_template = argv[++i];
            else { corpus_usage(argv[0]); return EXIT_FAILURE; }
        }
        if (!window_server_tag_corpus_scenario_is_valid(scenario) ||
            !window_server_tag_corpus_scenario_is_valid(capture_set_id) ||
            !window_server_tag_corpus_scenario_is_valid(source_revision) ||
            !repeat_index || !output ||
            (strcmp(separate_spaces, "yes") != 0 && strcmp(separate_spaces, "no") != 0 &&
             strcmp(separate_spaces, "unknown") != 0) ||
            (strcmp(stage_manager, "on") != 0 && strcmp(stage_manager, "off") != 0 &&
             strcmp(stage_manager, "unknown") != 0) ||
            ((labels || label_template) && !compare_classifier) ||
            (labels && label_template)) {
            corpus_usage(argv[0]);
            return EXIT_FAILURE;
        }

        CFArrayRef info_ref = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
        if (!info_ref || CFGetTypeID(info_ref) != CFArrayGetTypeID() ||
            CFArrayGetCount(info_ref) < 0 || (uintmax_t) CFArrayGetCount(info_ref) > INT_MAX) {
            if (info_ref) CFRelease(info_ref);
            return EXIT_FAILURE;
        }
        size_t count = (size_t) CFArrayGetCount(info_ref);
        struct window_server_tag_corpus_public_window *windows = calloc(count ? count : 1, sizeof(*windows));
        struct window_server_tag_corpus_comparison_window *comparison_windows =
            compare_classifier ? calloc(count ? count : 1, sizeof(*comparison_windows)) : NULL;
        CFMutableArrayRef ids = CFArrayCreateMutable(NULL, (CFIndex) count, &kCFTypeArrayCallBacks);
        bool valid = windows && ids && (!compare_classifier || comparison_windows);
        for (size_t i = 0; valid && i < count; ++i) {
            id value = (id) CFArrayGetValueAtIndex(info_ref, (CFIndex) i);
            if (![value isKindOfClass:[NSDictionary class]]) { valid = false; break; }
            NSDictionary *dictionary = value;
            int64_t window_id = 0, owner_pid = 0, layer = 0;
            if (!corpus_number(dictionary, kCGWindowNumber, &window_id) || window_id <= 0 ||
                window_id > UINT32_MAX || !corpus_number(dictionary, kCGWindowOwnerPID, &owner_pid) ||
                owner_pid < INT_MIN || owner_pid > INT_MAX ||
                !corpus_number(dictionary, kCGWindowLayer, &layer) ||
                layer < INT_MIN || layer > INT_MAX) { valid = false; break; }
            id on_screen = dictionary[(NSString *) kCGWindowIsOnscreen];
            CGRect bounds = CGRectMake(NAN, NAN, NAN, NAN);
            id bounds_value = dictionary[(NSString *) kCGWindowBounds];
            if ([bounds_value isKindOfClass:[NSDictionary class]]) {
                CGRectMakeWithDictionaryRepresentation((CFDictionaryRef) bounds_value, &bounds);
            }
            windows[i] = (struct window_server_tag_corpus_public_window) {
                .window_id = (uint32_t) window_id, .owner_pid = (int) owner_pid,
                .cg_layer = (int) layer,
                .on_screen = [on_screen isKindOfClass:[NSNumber class]] ? [on_screen boolValue] != NO : false,
                .x = bounds.origin.x, .y = bounds.origin.y,
                .width = bounds.size.width, .height = bounds.size.height
            };
            if (compare_classifier) {
                comparison_windows[i] = (struct window_server_tag_corpus_comparison_window) {
                    .owner = corpus_owner_kind((pid_t) owner_pid),
                    .ax = WS_AX_UNAVAILABLE,
                    .ax_observation = WINDOW_SERVER_TAG_CORPUS_AX_OBSERVATION_UNAVAILABLE,
                    .title = WINDOW_SERVER_TITLE_UNAVAILABLE
                };
            }
            int32_t id_value = (int32_t) window_id;
            CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &id_value);
            if (!number) { valid = false; break; }
            CFArrayAppendValue(ids, number);
            CFRelease(number);
        }
        CFRelease(info_ref);
        if (!valid) { if (ids) CFRelease(ids); free(comparison_windows); free(windows); return EXIT_FAILURE; }

        struct window_server_snapshot *snapshot = window_server_snapshot_create(SLSMainConnectionID(), ids, (int) count);
        CFRelease(ids);
        if (!snapshot) { free(comparison_windows); free(windows); return EXIT_FAILURE; }

        NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
        char os_version[64];
        snprintf(os_version, sizeof(os_version), "%ld.%ld.%ld",
                 version.majorVersion, version.minorVersion, version.patchVersion);
        char os_build[256] = {0}; size_t os_build_size = sizeof(os_build);
        struct utsname system_info;
        if (sysctlbyname("kern.osversion", os_build, &os_build_size, NULL, 0) != 0 ||
            !memchr(os_build, '\0', sizeof(os_build)) || uname(&system_info) != 0) {
            window_server_snapshot_destroy(snapshot); free(comparison_windows); free(windows); return EXIT_FAILURE;
        }
        NSUInteger screen_count = [NSScreen screens].count;
        if (!screen_count || screen_count > UINT_MAX) {
            window_server_snapshot_destroy(snapshot); free(comparison_windows); free(windows); return EXIT_FAILURE;
        }
        struct window_server_tag_corpus_metadata metadata = {
            .scenario = scenario, .capture_set_id = capture_set_id,
            .repeat_index = repeat_index, .os_version = os_version,
            .os_build = os_build, .architecture = system_info.machine,
            .source_revision = source_revision, .ax_mode = ax_mode,
            .display_count = (unsigned) screen_count,
            .separate_spaces = separate_spaces, .stage_manager = stage_manager
        };
        bool success;
        if (compare_classifier) {
            corpus_enrich_ax(windows, count, comparison_windows, ax_mode);
            if (labels && !corpus_load_labels(labels, windows, count, comparison_windows)) {
                window_server_snapshot_destroy(snapshot); free(comparison_windows); free(windows); return EXIT_FAILURE;
            }
            enum ws_tag_layout_status layout = WS_TAG_LAYOUT_UNSUPPORTED;
            if (version.majorVersion == 27 && version.minorVersion < 2) {
                layout = WS_TAG_LAYOUT_SAMPLED;
            } else if (version.majorVersion == 26 || version.majorVersion == 27) {
                layout = WS_TAG_LAYOUT_UNVERIFIED;
            }
            struct window_server_tag_corpus_comparison comparison = {
                .tag_layout = layout,
                .normal_level = CGWindowLevelForKey(kCGNormalWindowLevelKey),
                .floating_level = CGWindowLevelForKey(kCGFloatingWindowLevelKey),
                .modal_level = CGWindowLevelForKey(kCGModalPanelWindowLevelKey),
                .windows = comparison_windows, .window_count = count
            };
            success = window_server_tag_corpus_write_comparison_file(
                output, &metadata, windows, count, snapshot, &comparison);
        } else {
            success = window_server_tag_corpus_write_file(output, &metadata, windows, count, snapshot);
        }
        if (success && label_template &&
            !corpus_write_label_template(label_template, windows, count)) {
            unlink(output);
            success = false;
        }
        window_server_snapshot_destroy(snapshot);
        free(comparison_windows);
        free(windows);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
