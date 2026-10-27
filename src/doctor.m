#include "doctor.h"

static void doctor_emit(FILE *stream, const char *status, const char *name,
                        const char *mechanism, const char *next)
{
    fprintf(stream, "%s %-18s mechanism: %s; next: %s\n", status, name, mechanism, next);
}

int doctor_report(const struct doctor_facts *facts, FILE *stream)
{
    int failures = 0;

    if (facts->binary_executable == DOCTOR_PROBE_YES) {
        char mechanism[128];
        snprintf(mechanism, sizeof(mechanism), "running yabai-v%d.%d.%d as %s", MAJOR, MINOR, PATCH,
                 facts->runtime_arch ? facts->runtime_arch : "unknown architecture");
        doctor_emit(stream, "PASS", "binary", mechanism, "use this binary for service installation");
    } else if (facts->binary_executable == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "FAIL", "binary", "the running executable is not executable/readable",
                    "reinstall yabai and rerun yabai --doctor");
        ++failures;
    } else {
        doctor_emit(stream, "UNKNOWN", "binary", "the executable path could not be inspected",
                    "verify the installed yabai binary manually");
    }

    if (facts->translated == DOCTOR_PROBE_YES && facts->arm64_slice == DOCTOR_PROBE_YES) {
        doctor_emit(stream, "WARN", "architecture", "Rosetta translation is forced although this binary contains an arm64 slice",
                    "launch the arm64 slice instead of forcing x86_64");
    } else if (facts->translated == DOCTOR_PROBE_YES && facts->arm64_slice == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "WARN", "architecture", "Rosetta translation is required because this binary lacks an arm64 slice",
                    "install a universal or arm64 yabai binary");
    } else if (facts->translated == DOCTOR_PROBE_YES) {
        doctor_emit(stream, "WARN", "architecture", "Rosetta translation is active and binary slices are not inspectable",
                    "inspect the binary with file and install a native slice if needed");
    } else if (facts->translated == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "PASS", "architecture", "the process is running natively",
                    "no action required");
    } else {
        doctor_emit(stream, "UNKNOWN", "architecture", "translation state is unavailable",
                    "compare uname -m with the installed binary architecture");
    }

    if (facts->accessibility == DOCTOR_PROBE_YES) {
        doctor_emit(stream, "PASS", "accessibility", "AXIsProcessTrusted reports trusted",
                    "no action required");
    } else if (facts->accessibility == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "FAIL", "accessibility", "AXIsProcessTrusted reports not trusted",
                    "enable yabai in System Settings > Privacy & Security > Accessibility");
        ++failures;
    } else {
        doctor_emit(stream, "UNKNOWN", "accessibility", "Accessibility trust could not be inspected",
                    "check Privacy & Security > Accessibility");
    }

    doctor_emit(stream, "PASS", "SIP (core)",
                "core window management does not require disabling SIP",
                "keep SIP enabled unless optional scripting-addition features are needed");
    if (facts->sip_available != DOCTOR_PROBE_YES) {
        doctor_emit(stream, "UNKNOWN", "SIP (scripting)", "csr_get_active_config is unavailable",
                    "run csrutil status manually; no change is needed for core features");
    } else if ((facts->sip_config & DOCTOR_REQUIRED_SIP) == DOCTOR_REQUIRED_SIP) {
        doctor_emit(stream, "PASS", "SIP (scripting)",
                    "effective csr flags allow unrestricted filesystem and task-for-pid",
                    "no action required for the SIP prerequisite");
    } else {
        doctor_emit(stream, "WARN", "SIP (scripting)",
                    "effective csr flags do not satisfy the optional scripting addition",
                    "use core features, or follow the scripting-addition SIP documentation");
    }

    if (facts->identity != DOCTOR_PROBE_YES) {
        doctor_emit(stream, "UNKNOWN", "user identity", "real user identity could not be resolved safely",
                    "run doctor as a login user, or as root only with a valid nonroot SUDO_UID");
    } else {
        doctor_emit(stream, "PASS", "user identity", "getuid/SUDO_UID and getpwuid resolved one inspection identity",
                    "no action required");
    }

    switch (facts->service_plist) {
    case DOCTOR_SERVICE_VALID:
        doctor_emit(stream, "PASS", "service plist", "regular user-owned plist has canonical label, launch fields, mode, and executable",
                    "no plist repair is required");
        break;
    case DOCTOR_SERVICE_VALID_SYMLINK:
        doctor_emit(stream, "WARN", "service plist", "ProgramArguments uses a symlink that resolves to the running yabai executable",
                    "prefer the stable resolved executable path when reinstalling the service");
        break;
    case DOCTOR_SERVICE_ABSENT:
        doctor_emit(stream, "WARN", "service plist", "the com.asmvik.yabai launch agent plist is absent",
                    "run yabai --install-service if launchd startup is wanted");
        break;
    case DOCTOR_SERVICE_INVALID:
        doctor_emit(stream, "FAIL", "service plist", "plist type, ownership, mode, fields, or executable is unsafe or invalid",
                    "review it, then reinstall it with yabai --install-service");
        ++failures;
        break;
    case DOCTOR_SERVICE_WRONG_LABEL:
        doctor_emit(stream, "FAIL", "service plist", "the launch agent plist Label is not com.asmvik.yabai",
                    "review it, then reinstall it with yabai --install-service");
        ++failures;
        break;
    default:
        doctor_emit(stream, "UNKNOWN", "service plist", "the launch agent location is not inspectable",
                    "inspect ~/Library/LaunchAgents manually");
        break;
    }

    doctor_emit(stream, "UNKNOWN", "service runtime",
                "doctor does not query or change the launchd domain",
                "as the login user, inspect label com.asmvik.yabai in that user's gui launchd domain");

    if (facts->legacy_service_collision == DOCTOR_PROBE_YES) {
        doctor_emit(stream, "WARN", "legacy service", "com.koekeishiya.yabai.plist or homebrew.mxcl.yabai.plist is present",
                    "unload and remove the legacy service if it is no longer intended");
    } else if (facts->legacy_service_collision == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "PASS", "legacy service", "no known legacy launch agent plist is present",
                    "no action required");
    } else {
        doctor_emit(stream, "UNKNOWN", "legacy service", "legacy launch agent paths are not inspectable",
                    "check LaunchAgents for old yabai labels manually");
    }

    if (facts->config_present == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "WARN", "configuration", "no default yabairc or yabai.jsonc was found",
                    "create a config only if defaults are insufficient");
    } else if (facts->config_present == DOCTOR_PROBE_UNKNOWN || facts->config_readable == DOCTOR_PROBE_UNKNOWN) {
        doctor_emit(stream, "UNKNOWN", "configuration", "default configuration paths are not inspectable",
                    "check config ownership and parent-directory permissions");
    } else if (facts->config_readable == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "FAIL", "configuration", "a discovered configuration file is not readable",
                    "fix its ownership or read permissions");
        ++failures;
    } else {
        doctor_emit(stream, "PASS", "configuration", "discovered configuration files are readable",
                    "use yabai --check-config for JSONC schema validation");
    }

    if (facts->screen_capture_required == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "PASS", "screen recording",
                    "selected static configuration does not enable capture-dependent features",
                    "no action required");
    } else if (facts->screen_capture_required == DOCTOR_PROBE_YES &&
               facts->screen_capture_access == DOCTOR_PROBE_YES) {
        doctor_emit(stream, "PASS", "screen recording",
                    "configured window animations are authorized by CGPreflightScreenCaptureAccess",
                    "no action required");
    } else if (facts->screen_capture_required == DOCTOR_PROBE_YES &&
               facts->screen_capture_access == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "WARN", "screen recording",
                    "configured window animations require Screen Recording and CGPreflightScreenCaptureAccess reports not authorized",
                    "enable yabai in System Settings > Privacy & Security > Screen Recording, then restart yabai");
    } else if (facts->screen_capture_required == DOCTOR_PROBE_YES) {
        doctor_emit(stream, "UNKNOWN", "screen recording",
                    "configured window animations require capture but nonprompting preflight is unavailable",
                    "verify Screen Recording permission manually");
    } else {
        doctor_emit(stream, "UNKNOWN", "screen recording",
                    "static configuration cannot determine whether capture-dependent features are enabled",
                    "validate yabai.jsonc and inspect yabairc for window_animation_duration");
    }

    if (facts->arm64e_applicable == DOCTOR_PROBE_YES) {
        if (facts->arm64e_bootarg == DOCTOR_PROBE_YES) {
            doctor_emit(stream, "PASS", "arm64e boot arg", "kern.bootargs contains -arm64e_preview_abi",
                        "no action required for this scripting-addition prerequisite");
        } else if (facts->arm64e_bootarg == DOCTOR_PROBE_NO) {
            doctor_emit(stream, "WARN", "arm64e boot arg", "kern.bootargs lacks -arm64e_preview_abi",
                        "use core features, or follow scripting-addition setup documentation");
        } else {
            doctor_emit(stream, "UNKNOWN", "arm64e boot arg", "kern.bootargs is unavailable",
                        "verify the boot argument manually only if scripting features are needed");
        }
    } else if (facts->arm64e_applicable == DOCTOR_PROBE_UNKNOWN) {
        doctor_emit(stream, "UNKNOWN", "arm64e boot arg", "Apple Silicon applicability could not be determined",
                    "verify architecture and the boot argument manually only if scripting features are needed");
    }

    switch (facts->sa_install) {
    case DOCTOR_SA_CURRENT:
        doctor_emit(stream, "PASS", "scripting addition", "installed bundle metadata matches the built-in payload version",
                    "reload it only when optional features require it");
        break;
    case DOCTOR_SA_OUTDATED:
        doctor_emit(stream, "WARN", "scripting addition", "installed bundle metadata does not match this yabai version",
                    "run sudo yabai --load-sa only if optional features are needed");
        break;
    case DOCTOR_SA_ABSENT:
        doctor_emit(stream, "WARN", "scripting addition", "the optional scripting addition is not installed",
                    "no action for core features; install it only when needed");
        break;
    default:
        doctor_emit(stream, "UNKNOWN", "scripting addition", "bundle compatibility metadata is not inspectable",
                    "inspect /Library/ScriptingAdditions/yabai.osax manually");
        break;
    }

    if (facts->sa_socket == DOCTOR_PROBE_YES) {
        switch (facts->sa_runtime) {
        case DOCTOR_SA_RUNTIME_CURRENT:
            doctor_emit(stream, "PASS", "scripting status", "the live payload handshake matches this build and required capabilities",
                        "no action required");
            break;
        case DOCTOR_SA_RUNTIME_OUTDATED:
            doctor_emit(stream, "WARN", "scripting status", "the live payload handshake reports a different version",
                        "reload the scripting addition only if optional features are needed");
            break;
        case DOCTOR_SA_RUNTIME_INCOMPLETE:
            doctor_emit(stream, "WARN", "scripting status", "the live payload lacks capabilities required on this macOS release",
                        "review compatibility before using scripting-addition features");
            break;
        case DOCTOR_SA_RUNTIME_UNREACHABLE:
            doctor_emit(stream, "WARN", "scripting status", "the validated socket did not complete a bounded handshake",
                        "reload the scripting addition or restart Dock only if optional features are needed");
            break;
        default:
            doctor_emit(stream, "UNKNOWN", "scripting status", "the live payload handshake was not attempted",
                        "verify scripting-addition status manually if needed");
            break;
        }
    } else if (facts->sa_socket == DOCTOR_PROBE_NO) {
        doctor_emit(stream, "UNKNOWN", "scripting status", "no per-user scripting-addition socket exists",
                    "load the addition only if optional features are needed");
    } else {
        doctor_emit(stream, "UNKNOWN", "scripting status", "the per-user socket could not be inspected",
                    "verify scripting-addition status manually if needed");
    }

    fprintf(stream, "SUMMARY %d failure%s; doctor made no changes\n", failures, failures == 1 ? "" : "s");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

struct doctor_identity {
    uid_t uid;
    gid_t gid;
    bool elevated;
    const char *name;
    const char *home;
};

struct doctor_collect_ops {
    uid_t (*getuid_fn)(void);
    struct passwd *(*getpwuid_fn)(uid_t);
    char *(*getenv_fn)(const char *);
    int (*stat_fn)(const char *, struct stat *);
    int (*lstat_fn)(const char *, struct stat *);
    int (*access_fn)(const char *, int);
    enum doctor_probe (*target_access_fn)(const struct doctor_identity *, const char *, int);
    int (*getgrouplist_fn)(const char *, int, int *, int *);
    char *(*realpath_fn)(const char *, char *);
    int (*open_fn)(const char *, int);
    int (*fstat_fn)(int, struct stat *);
    ssize_t (*read_fn)(int, void *, size_t);
    enum doctor_probe (*screen_capture_preflight_fn)(void);
    int (*close_fn)(int);
};

static int doctor_open_readonly(const char *path, int flags) { return open(path, flags); }

static enum doctor_probe doctor_access_as_identity(const struct doctor_identity *identity,
                                                    const char *path, int mode)
{
    if (getuid() == identity->uid) {
        errno = 0;
        if (access(path, mode) == 0) return DOCTOR_PROBE_YES;
        return errno == EACCES || errno == EPERM ? DOCTOR_PROBE_NO : DOCTOR_PROBE_UNKNOWN;
    }
    if (getuid() != 0) return DOCTOR_PROBE_UNKNOWN;

    pid_t child = fork();
    if (child == -1) return DOCTOR_PROBE_UNKNOWN;
    if (child == 0) {
        if (initgroups(identity->name, identity->gid) != 0 ||
            setgid(identity->gid) != 0 || setuid(identity->uid) != 0) _exit(2);
        if (access(path, mode) == 0) _exit(0);
        _exit(errno == EACCES || errno == EPERM ? 1 : 2);
    }

    int status;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) return DOCTOR_PROBE_UNKNOWN;
    }
    if (!WIFEXITED(status)) return DOCTOR_PROBE_UNKNOWN;
    switch (WEXITSTATUS(status)) {
    case 0: return DOCTOR_PROBE_YES;
    case 1: return DOCTOR_PROBE_NO;
    default: return DOCTOR_PROBE_UNKNOWN;
    }
}

static enum doctor_probe doctor_screen_capture_preflight(void)
{
    if (@available(macOS 10.15, *)) {
        return CGPreflightScreenCaptureAccess() ? DOCTOR_PROBE_YES : DOCTOR_PROBE_NO;
    }
    return DOCTOR_PROBE_UNKNOWN;
}

static struct doctor_collect_ops doctor_production_ops = {
    .getuid_fn = getuid, .getpwuid_fn = getpwuid, .getenv_fn = getenv,
    .stat_fn = stat, .lstat_fn = lstat, .access_fn = access,
    .target_access_fn = doctor_access_as_identity,
    .getgrouplist_fn = getgrouplist, .realpath_fn = realpath,
    .open_fn = doctor_open_readonly, .fstat_fn = fstat, .read_fn = read, .close_fn = close,
    .screen_capture_preflight_fn = doctor_screen_capture_preflight
};

static bool doctor_parse_uid(const char *text, uid_t *uid)
{
    if (!text || !*text) return false;
    uintmax_t value = 0, maximum = (uintmax_t) (uid_t) -1;
    for (const unsigned char *p = (const unsigned char *) text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        unsigned digit = *p - '0';
        if (value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (value == 0) return false;
    *uid = (uid_t) value;
    return true;
}

static bool doctor_resolve_identity(const struct doctor_collect_ops *ops, struct doctor_identity *identity)
{
    uid_t real_uid = ops->getuid_fn();
    uid_t uid = real_uid;
    if (uid == 0 && !doctor_parse_uid(ops->getenv_fn("SUDO_UID"), &uid)) return false;
    struct passwd *pw = ops->getpwuid_fn(uid);
    if (!pw || !pw->pw_name || !*pw->pw_name || !pw->pw_dir || pw->pw_dir[0] != '/') return false;
    identity->uid = uid;
    identity->gid = pw->pw_gid;
    identity->elevated = real_uid == 0;
    identity->name = pw->pw_name;
    identity->home = pw->pw_dir;
    return true;
}

enum doctor_config_result { DOCTOR_CONFIG_ABSENT, DOCTOR_CONFIG_OK, DOCTOR_CONFIG_BAD, DOCTOR_CONFIG_UNKNOWN };

static enum doctor_probe doctor_access_probe(const struct doctor_collect_ops *ops, const char *path, int mode)
{
    errno = 0;
    if (ops->access_fn(path, mode) == 0) return DOCTOR_PROBE_YES;
    int error = errno;
    return error == EACCES || error == EPERM ? DOCTOR_PROBE_NO : DOCTOR_PROBE_UNKNOWN;
}

static enum doctor_probe doctor_target_permission(const struct doctor_collect_ops *ops,
                                                   const struct doctor_identity *identity,
                                                   const char *path, const struct stat *st, int mode)
{
    if (!identity->elevated) return doctor_access_probe(ops, path, mode);
    if (ops->target_access_fn) return ops->target_access_fn(identity, path, mode);
    mode_t required = 0;
    if (mode & R_OK) required |= 04;
    if (mode & W_OK) required |= 02;
    if (mode & X_OK) required |= 01;
    if (st->st_uid == identity->uid) return (st->st_mode & (required << 6)) == (required << 6) ? DOCTOR_PROBE_YES : DOCTOR_PROBE_NO;

    bool group_member = st->st_gid == identity->gid;
    if (!group_member) {
        int group_count = NGROUPS_MAX;
        int groups[NGROUPS_MAX];
        if (!ops->getgrouplist_fn ||
            ops->getgrouplist_fn(identity->name, (int) identity->gid, groups, &group_count) < 0 ||
            group_count < 0 || group_count > NGROUPS_MAX) return DOCTOR_PROBE_UNKNOWN;
        for (int i = 0; i < group_count; ++i) {
            if ((gid_t) groups[i] == st->st_gid) {
                group_member = true;
                break;
            }
        }
    }
    if (group_member) return (st->st_mode & (required << 3)) == (required << 3) ? DOCTOR_PROBE_YES : DOCTOR_PROBE_NO;
    return (st->st_mode & required) == required ? DOCTOR_PROBE_YES : DOCTOR_PROBE_NO;
}

static enum doctor_probe doctor_target_path_searchable(const struct doctor_collect_ops *ops,
                                                         const struct doctor_identity *identity,
                                                         const char *path)
{
    if (!identity->elevated) return DOCTOR_PROBE_YES;
    char current[PATH_MAX] = "/";
    size_t used = 1;
    const char *cursor = path;
    while (*cursor == '/') ++cursor;
    while (*cursor) {
        const char *slash = strchr(cursor, '/');
        if (!slash) break;
        size_t component = (size_t) (slash - cursor);
        if (component && used + component + 1 < sizeof(current)) {
            memcpy(current + used, cursor, component);
            used += component;
            current[used] = '\0';
            struct stat st;
            if (ops->stat_fn(current, &st) != 0 || !S_ISDIR(st.st_mode)) return DOCTOR_PROBE_UNKNOWN;
            enum doctor_probe searchable = doctor_target_permission(ops, identity, current, &st, X_OK);
            if (searchable != DOCTOR_PROBE_YES) return searchable;
            current[used++] = '/';
            current[used] = '\0';
        }
        cursor = slash + 1;
    }
    return DOCTOR_PROBE_YES;
}

static enum doctor_probe doctor_target_path_traversal(const struct doctor_collect_ops *ops,
                                                        const struct doctor_identity *identity,
                                                        const char *path, char canonical[PATH_MAX],
                                                        bool *contains_symlink)
{
    enum doctor_probe lexical = doctor_target_path_searchable(ops, identity, path);
    if (lexical != DOCTOR_PROBE_YES) return lexical;
    if (!ops->realpath_fn || !ops->realpath_fn(path, canonical) ||
        !memchr(canonical, '\0', PATH_MAX)) return DOCTOR_PROBE_UNKNOWN;
    enum doctor_probe resolved = doctor_target_path_searchable(ops, identity, canonical);
    if (resolved != DOCTOR_PROBE_YES) return resolved;
    if (contains_symlink) *contains_symlink = strcmp(path, canonical) != 0;
    return DOCTOR_PROBE_YES;
}

static enum doctor_config_result doctor_config_candidate(const struct doctor_collect_ops *ops,
                                                           const struct doctor_identity *identity,
                                                           const char *path, bool directory_is_absent)
{
    struct stat st;
    if (ops->stat_fn(path, &st) != 0) {
        int error = errno;
        if (error == ENOENT) return DOCTOR_CONFIG_ABSENT;
        return error == EACCES || error == EPERM ? DOCTOR_CONFIG_BAD : DOCTOR_CONFIG_UNKNOWN;
    }
    if (S_ISDIR(st.st_mode) && directory_is_absent) return DOCTOR_CONFIG_ABSENT;
    if (!S_ISREG(st.st_mode)) return DOCTOR_CONFIG_BAD;
    char canonical[PATH_MAX];
    enum doctor_probe searchable = doctor_target_path_traversal(ops, identity, path, canonical, NULL);
    if (searchable != DOCTOR_PROBE_YES) return searchable == DOCTOR_PROBE_NO ? DOCTOR_CONFIG_BAD : DOCTOR_CONFIG_UNKNOWN;
    enum doctor_probe readable = doctor_target_permission(ops, identity, path, &st, R_OK);
    return readable == DOCTOR_PROBE_YES ? DOCTOR_CONFIG_OK :
           readable == DOCTOR_PROBE_NO ? DOCTOR_CONFIG_BAD : DOCTOR_CONFIG_UNKNOWN;
}

static __attribute__((unused)) enum doctor_config_result doctor_select_config(const struct doctor_collect_ops *ops,
                                                       const struct doctor_identity *identity,
                                                       const char *const *paths, size_t count,
                                                       bool directory_is_absent)
{
    for (size_t i = 0; i < count; ++i) {
        if (!paths[i]) continue;
        enum doctor_config_result result = doctor_config_candidate(ops, identity, paths[i], directory_is_absent);
        if (result != DOCTOR_CONFIG_ABSENT) return result;
    }
    return DOCTOR_CONFIG_ABSENT;
}

static void doctor_merge_config_result(struct doctor_facts *facts, enum doctor_config_result result)
{
    if (result == DOCTOR_CONFIG_BAD) {
        facts->config_present = DOCTOR_PROBE_YES;
        facts->config_readable = DOCTOR_PROBE_NO;
    } else if (result == DOCTOR_CONFIG_UNKNOWN && facts->config_readable != DOCTOR_PROBE_NO) {
        facts->config_present = DOCTOR_PROBE_UNKNOWN;
        facts->config_readable = DOCTOR_PROBE_UNKNOWN;
    } else if (result == DOCTOR_CONFIG_OK && facts->config_readable != DOCTOR_PROBE_NO) {
        facts->config_present = DOCTOR_PROBE_YES;
    }
}

static bool doctor_format(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, size, format, args);
    va_end(args);
    return length >= 0 && (size_t) length < size;
}

static int doctor_fstat_retry(const struct doctor_collect_ops *ops, int fd, struct stat *st)
{
    int result;
    do
        result = ops->fstat_fn(fd, st);
    while (result != 0 && errno == EINTR);
    return result;
}

static bool doctor_same_stat(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
           (a->st_mode & S_IFMT) == (b->st_mode & S_IFMT) && a->st_size == b->st_size &&
           a->st_mtimespec.tv_sec == b->st_mtimespec.tv_sec &&
           a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec &&
           a->st_ctimespec.tv_sec == b->st_ctimespec.tv_sec &&
           a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
}

static enum doctor_probe doctor_read_regular_snapshot(const struct doctor_collect_ops *ops,
                                                        const char *path, const struct stat *expected,
                                                        size_t maximum, NSData **data,
                                                        struct stat *observed)
{
    int fd = ops->open_fn(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return DOCTOR_PROBE_UNKNOWN;
    struct stat st;
    if (doctor_fstat_retry(ops, fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uintmax_t) st.st_size > maximum ||
        (expected && (!S_ISREG(expected->st_mode) || !doctor_same_stat(&st, expected)))) {
        ops->close_fn(fd);
        return DOCTOR_PROBE_UNKNOWN;
    }
    size_t size = (size_t) st.st_size;
    NSMutableData *snapshot = [NSMutableData dataWithLength:size];
    size_t offset = 0;
    while (offset < size) {
        ssize_t bytes = ops->read_fn(fd, (uint8_t *) [snapshot mutableBytes] + offset, size - offset);
        if (bytes > 0) offset += (size_t) bytes;
        else if (bytes < 0 && errno == EINTR) continue;
        else { ops->close_fn(fd); return DOCTOR_PROBE_UNKNOWN; }
    }
    struct stat after;
    if (doctor_fstat_retry(ops, fd, &after) != 0 || !doctor_same_stat(&st, &after)) {
        ops->close_fn(fd);
        return DOCTOR_PROBE_UNKNOWN;
    }
    if (ops->close_fn(fd) != 0) return DOCTOR_PROBE_UNKNOWN;
    if (observed) *observed = after;
    *data = snapshot;
    return DOCTOR_PROBE_YES;
}

static bool doctor_bool(id value, bool expected)
{
    if (!value || CFGetTypeID((__bridge CFTypeRef) value) != CFBooleanGetTypeID()) return false;
    return CFBooleanGetValue((__bridge CFBooleanRef) value) == expected;
}

static enum doctor_service_plist doctor_inspect_service(const struct doctor_collect_ops *ops,
                                                         const struct doctor_identity *identity,
                                                         const char *path,
                                                         const char *intended_executable)
{
    struct stat st;
    if (ops->lstat_fn(path, &st) != 0) {
        int error = errno;
        if (error == ENOENT) return DOCTOR_SERVICE_ABSENT;
        return error == EACCES || error == EPERM ? DOCTOR_SERVICE_INVALID : DOCTOR_SERVICE_UNKNOWN;
    }
    if (!S_ISREG(st.st_mode)) return DOCTOR_SERVICE_INVALID;
    char plist_canonical[PATH_MAX];
    enum doctor_probe plist_searchable = doctor_target_path_traversal(ops, identity, path, plist_canonical, NULL);
    if (plist_searchable != DOCTOR_PROBE_YES) {
        return plist_searchable == DOCTOR_PROBE_NO ? DOCTOR_SERVICE_INVALID : DOCTOR_SERVICE_UNKNOWN;
    }

    int plist_fd = ops->open_fn(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (plist_fd < 0) return DOCTOR_SERVICE_UNKNOWN;
    struct stat descriptor_stat;
    if (doctor_fstat_retry(ops, plist_fd, &descriptor_stat) != 0 ||
        descriptor_stat.st_dev != st.st_dev || descriptor_stat.st_ino != st.st_ino ||
        !S_ISREG(descriptor_stat.st_mode)) {
        ops->close_fn(plist_fd);
        return DOCTOR_SERVICE_UNKNOWN;
    }
    if (descriptor_stat.st_uid != identity->uid || (descriptor_stat.st_mode & 022) != 0) {
        ops->close_fn(plist_fd);
        return DOCTOR_SERVICE_INVALID;
    }
    enum doctor_probe plist_readable = doctor_target_permission(ops, identity, path, &descriptor_stat, R_OK);
    if (plist_readable != DOCTOR_PROBE_YES) {
        ops->close_fn(plist_fd);
        return plist_readable == DOCTOR_PROBE_NO ? DOCTOR_SERVICE_INVALID : DOCTOR_SERVICE_UNKNOWN;
    }
    if (descriptor_stat.st_size < 0 || descriptor_stat.st_size > 1024 * 1024) {
        ops->close_fn(plist_fd);
        return DOCTOR_SERVICE_INVALID;
    }
    size_t plist_size = (size_t) descriptor_stat.st_size;
    NSMutableData *plist_data = [NSMutableData dataWithLength:plist_size];
    size_t offset = 0;
    while (offset < plist_size) {
        ssize_t bytes = ops->read_fn(plist_fd, (uint8_t *) [plist_data mutableBytes] + offset, plist_size - offset);
        if (bytes > 0) offset += (size_t) bytes;
        else if (bytes < 0 && errno == EINTR) continue;
        else {
            ops->close_fn(plist_fd);
            return DOCTOR_SERVICE_UNKNOWN;
        }
    }
    struct stat descriptor_after;
    if (doctor_fstat_retry(ops, plist_fd, &descriptor_after) != 0 ||
        !doctor_same_stat(&descriptor_stat, &descriptor_after)) {
        ops->close_fn(plist_fd);
        return DOCTOR_SERVICE_UNKNOWN;
    }
    if (ops->close_fn(plist_fd) != 0) return DOCTOR_SERVICE_UNKNOWN;
    NSError *parse_error = nil;
    id plist_object = [NSPropertyListSerialization propertyListWithData:plist_data
                                                                options:NSPropertyListImmutable
                                                                 format:NULL error:&parse_error];
    if (![plist_object isKindOfClass:[NSDictionary class]]) return DOCTOR_SERVICE_INVALID;
    NSDictionary *plist = plist_object;
    id label = plist[@"Label"];
    if (![label isKindOfClass:[NSString class]] || ![label isEqualToString:@"com.asmvik.yabai"]) {
        return [label isKindOfClass:[NSString class]] ? DOCTOR_SERVICE_WRONG_LABEL : DOCTOR_SERVICE_INVALID;
    }
    id arguments = plist[@"ProgramArguments"];
    if (![arguments isKindOfClass:[NSArray class]] || [arguments count] != 1 ||
        ![arguments[0] isKindOfClass:[NSString class]]) return DOCTOR_SERVICE_INVALID;
    const char *executable = [arguments[0] fileSystemRepresentation];
    struct stat executable_link, executable_stat, intended_stat;
    if (!executable || executable[0] != '/') return DOCTOR_SERVICE_INVALID;
    if (!intended_executable) return DOCTOR_SERVICE_UNKNOWN;
    if (ops->lstat_fn(executable, &executable_link) != 0) {
        int error = errno;
        return error == ENOENT || error == EACCES || error == EPERM ?
               DOCTOR_SERVICE_INVALID : DOCTOR_SERVICE_UNKNOWN;
    }

    char resolved_executable[PATH_MAX];
    bool executable_is_symlink = false;
    enum doctor_probe executable_traversal = doctor_target_path_traversal(ops, identity, executable,
                                                                          resolved_executable,
                                                                          &executable_is_symlink);
    if (executable_traversal != DOCTOR_PROBE_YES) {
        return executable_traversal == DOCTOR_PROBE_NO ? DOCTOR_SERVICE_INVALID : DOCTOR_SERVICE_UNKNOWN;
    }
    const char *permission_path = resolved_executable;
    if (ops->stat_fn(permission_path, &executable_stat) != 0) return DOCTOR_SERVICE_UNKNOWN;
    if (ops->stat_fn(intended_executable, &intended_stat) != 0) return DOCTOR_SERVICE_UNKNOWN;
    if (!S_ISREG(executable_stat.st_mode) ||
        (executable_stat.st_uid != identity->uid && executable_stat.st_uid != 0) ||
        (executable_stat.st_mode & 022) != 0 || executable_stat.st_dev != intended_stat.st_dev ||
        executable_stat.st_ino != intended_stat.st_ino) return DOCTOR_SERVICE_INVALID;

    enum doctor_probe executable_allowed = doctor_target_path_searchable(ops, identity, permission_path);
    if (executable_allowed == DOCTOR_PROBE_YES) {
        executable_allowed = doctor_target_permission(ops, identity, permission_path, &executable_stat, X_OK);
    }
    if (executable_allowed != DOCTOR_PROBE_YES) {
        return executable_allowed == DOCTOR_PROBE_NO ? DOCTOR_SERVICE_INVALID : DOCTOR_SERVICE_UNKNOWN;
    }
    struct stat executable_revalidated;
    if (ops->stat_fn(permission_path, &executable_revalidated) != 0 ||
        executable_revalidated.st_dev != executable_stat.st_dev ||
        executable_revalidated.st_ino != executable_stat.st_ino ||
        !S_ISREG(executable_revalidated.st_mode)) return DOCTOR_SERVICE_UNKNOWN;

    id environment = plist[@"EnvironmentVariables"];
    id environment_path = [environment isKindOfClass:[NSDictionary class]] ? environment[@"PATH"] : nil;
    id keep_alive = plist[@"KeepAlive"];
    if (![environment_path isKindOfClass:[NSString class]] || [environment_path length] == 0 ||
        !doctor_bool(plist[@"RunAtLoad"], true) || ![keep_alive isKindOfClass:[NSDictionary class]] ||
        !doctor_bool(keep_alive[@"SuccessfulExit"], false) || !doctor_bool(keep_alive[@"Crashed"], true) ||
        ![plist[@"ProcessType"] isEqual:@"Interactive"] || ![plist[@"Nice"] isEqual:@(-20)]) {
        return DOCTOR_SERVICE_INVALID;
    }

    char out_path[PATH_MAX], err_path[PATH_MAX];
    if (!doctor_format(out_path, sizeof(out_path), "/tmp/yabai_%s.out.log", identity->name) ||
        !doctor_format(err_path, sizeof(err_path), "/tmp/yabai_%s.err.log", identity->name)) {
        return DOCTOR_SERVICE_UNKNOWN;
    }
    if (![plist[@"StandardOutPath"] isEqualToString:[NSString stringWithUTF8String:out_path]] ||
        ![plist[@"StandardErrorPath"] isEqualToString:[NSString stringWithUTF8String:err_path]]) {
        return DOCTOR_SERVICE_INVALID;
    }

    char final_resolved[PATH_MAX];
    bool final_contains_symlink = false;
    enum doctor_probe final_traversal = doctor_target_path_traversal(ops, identity, executable,
                                                                     final_resolved,
                                                                     &final_contains_symlink);
    if (final_traversal != DOCTOR_PROBE_YES) return DOCTOR_SERVICE_UNKNOWN;
    struct stat final_stat, final_intended;
    if (ops->stat_fn(final_resolved, &final_stat) != 0 ||
        ops->stat_fn(intended_executable, &final_intended) != 0 ||
        !S_ISREG(final_stat.st_mode) || final_stat.st_dev != executable_stat.st_dev ||
        final_stat.st_ino != executable_stat.st_ino || final_stat.st_uid != executable_stat.st_uid ||
        final_stat.st_gid != executable_stat.st_gid ||
        (final_stat.st_mode & 07777) != (executable_stat.st_mode & 07777) ||
        (final_stat.st_mode & 022) != 0 || final_intended.st_dev != final_stat.st_dev ||
        final_intended.st_ino != final_stat.st_ino) return DOCTOR_SERVICE_UNKNOWN;
    enum doctor_probe final_execute = doctor_target_permission(ops, identity, final_resolved, &final_stat, X_OK);
    if (final_execute != DOCTOR_PROBE_YES) return DOCTOR_SERVICE_UNKNOWN;
    return executable_is_symlink || final_contains_symlink ? DOCTOR_SERVICE_VALID_SYMLINK : DOCTOR_SERVICE_VALID;
}

static enum doctor_probe doctor_inspect_socket(const struct doctor_collect_ops *ops,
                                                const struct doctor_identity *identity)
{
    struct yabai_runtime_paths paths;
    if (!yabai_runtime_derive_for_uid(identity->uid, identity->home, &paths)) {
        return DOCTOR_PROBE_UNKNOWN;
    }

    enum yabai_runtime_directory_status directory_status =
        yabai_runtime_validate_directories_readonly(&paths);
    if (directory_status == YABAI_RUNTIME_DIRECTORY_ABSENT) return DOCTOR_PROBE_NO;
    if (directory_status != YABAI_RUNTIME_DIRECTORY_VALID) return DOCTOR_PROBE_UNKNOWN;

    struct stat socket_stat;
    if (ops->lstat_fn(paths.sa_socket, &socket_stat) == 0) {
        return yabai_runtime_safe_socket_stat(&socket_stat, identity->uid, true) &&
               yabai_runtime_safe_socket_acl(paths.sa_socket, &socket_stat) ?
               DOCTOR_PROBE_YES : DOCTOR_PROBE_UNKNOWN;
    }
    return errno == ENOENT ? DOCTOR_PROBE_NO : DOCTOR_PROBE_UNKNOWN;
}

static enum doctor_probe doctor_arm64e_applicability(bool compiled_arm64, enum doctor_probe translated)
{
    if (compiled_arm64 || translated == DOCTOR_PROBE_YES) return DOCTOR_PROBE_YES;
    return translated == DOCTOR_PROBE_NO ? DOCTOR_PROBE_NO : DOCTOR_PROBE_UNKNOWN;
}

static __attribute__((unused)) enum doctor_probe doctor_classify_arm64e_bootargs(const char *bootargs, size_t length)
{
    static const char token[] = "-arm64e_preview_abi";
    if (!bootargs) return DOCTOR_PROBE_UNKNOWN;
    size_t token_length = sizeof(token) - 1;
    for (size_t i = 0; i + token_length <= length && bootargs[i]; ++i) {
        if ((i == 0 || isspace((unsigned char) bootargs[i - 1])) &&
            memcmp(bootargs + i, token, token_length) == 0 &&
            (i + token_length == length || bootargs[i + token_length] == '\0' ||
             isspace((unsigned char) bootargs[i + token_length]))) return DOCTOR_PROBE_YES;
    }
    return DOCTOR_PROBE_NO;
}

static enum doctor_probe doctor_native_screen_capture_requirement(
    const struct doctor_collect_ops *ops, const struct doctor_identity *identity,
    const char *path, enum doctor_config_result result)
{
    if (result == DOCTOR_CONFIG_ABSENT) return DOCTOR_PROBE_NO;
    if (result != DOCTOR_CONFIG_OK || !path) return DOCTOR_PROBE_UNKNOWN;

    char canonical[PATH_MAX];
    if (doctor_target_path_traversal(ops, identity, path, canonical, NULL) != DOCTOR_PROBE_YES) {
        return DOCTOR_PROBE_UNKNOWN;
    }
    struct stat st;
    if (ops->stat_fn(canonical, &st) != 0 || !S_ISREG(st.st_mode) ||
        doctor_target_permission(ops, identity, canonical, &st, R_OK) != DOCTOR_PROBE_YES) {
        return DOCTOR_PROBE_UNKNOWN;
    }

    NSData *data = nil;
    if (doctor_read_regular_snapshot(ops, canonical, &st, DECLARATIVE_CONFIG_MAX_FILE_SIZE,
                                     &data, NULL) != DOCTOR_PROBE_YES) {
        return DOCTOR_PROBE_UNKNOWN;
    }

    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    if (!declarative_config_parse_bytes([data bytes], [data length], path, &model, &error) || !model) {
        declarative_config_destroy(model);
        return DOCTOR_PROBE_UNKNOWN;
    }

    struct declarative_value animation = model->config[DECLARATIVE_CONFIG_ANIMATION_DURATION];
    enum doctor_probe required = animation.present && animation.real > 0.0
                               ? DOCTOR_PROBE_YES
                               : DOCTOR_PROBE_NO;
    declarative_config_destroy(model);
    return required;
}

static enum doctor_probe doctor_screen_capture_requirement(enum doctor_probe native_requirement,
                                                            enum doctor_config_result legacy_result)
{
    if (native_requirement == DOCTOR_PROBE_YES) return DOCTOR_PROBE_YES;
    if (native_requirement == DOCTOR_PROBE_UNKNOWN) return DOCTOR_PROBE_UNKNOWN;
    return legacy_result == DOCTOR_CONFIG_ABSENT ? DOCTOR_PROBE_NO : DOCTOR_PROBE_UNKNOWN;
}

static void doctor_collect_paths(struct doctor_facts *facts, const struct doctor_collect_ops *ops,
                                 const struct doctor_identity *identity,
                                 const char *intended_executable)
{
    char native_xdg[PATH_MAX], native_home[PATH_MAX], legacy_xdg[PATH_MAX], legacy_home[PATH_MAX], legacy_dot[PATH_MAX];
    const char *xdg = ops->getenv_fn("XDG_CONFIG_HOME");
    bool native_home_ok = doctor_format(native_home, sizeof(native_home), "%s/.config/yabai/yabai.jsonc", identity->home);
    bool legacy_home_ok = doctor_format(legacy_home, sizeof(legacy_home), "%s/.config/yabai/yabairc", identity->home);
    bool legacy_dot_ok = doctor_format(legacy_dot, sizeof(legacy_dot), "%s/.yabairc", identity->home);

    enum doctor_config_result native_result = DOCTOR_CONFIG_ABSENT;
    const char *native_path = NULL;
    if (xdg && xdg[0] == '/') {
        if (!doctor_format(native_xdg, sizeof(native_xdg), "%s/yabai/yabai.jsonc", xdg)) {
            native_result = DOCTOR_CONFIG_UNKNOWN;
        } else {
            native_path = native_xdg;
            native_result = doctor_config_candidate(ops, identity, native_xdg, false);
        }
    }
    if (native_result == DOCTOR_CONFIG_ABSENT) {
        native_path = native_home_ok ? native_home : NULL;
        native_result = native_home_ok ? doctor_config_candidate(ops, identity, native_home, false)
                                       : DOCTOR_CONFIG_UNKNOWN;
    }

    enum doctor_config_result legacy_result = DOCTOR_CONFIG_ABSENT;
    if (xdg && *xdg) {
        if (!doctor_format(legacy_xdg, sizeof(legacy_xdg), "%s/yabai/yabairc", xdg)) legacy_result = DOCTOR_CONFIG_UNKNOWN;
        else legacy_result = doctor_config_candidate(ops, identity, legacy_xdg, true);
    }
    if (legacy_result == DOCTOR_CONFIG_ABSENT) {
        legacy_result = legacy_home_ok ? doctor_config_candidate(ops, identity, legacy_home, true) : DOCTOR_CONFIG_UNKNOWN;
    }
    if (legacy_result == DOCTOR_CONFIG_ABSENT) {
        legacy_result = legacy_dot_ok ? doctor_config_candidate(ops, identity, legacy_dot, true) : DOCTOR_CONFIG_UNKNOWN;
    }

    facts->config_present = DOCTOR_PROBE_NO;
    facts->config_readable = DOCTOR_PROBE_YES;
    doctor_merge_config_result(facts, native_result);
    doctor_merge_config_result(facts, legacy_result);

    enum doctor_probe native_capture = doctor_native_screen_capture_requirement(
        ops, identity, native_path, native_result);
    facts->screen_capture_required = doctor_screen_capture_requirement(native_capture, legacy_result);
    facts->screen_capture_access = DOCTOR_PROBE_UNKNOWN;
    if (facts->screen_capture_required == DOCTOR_PROBE_YES &&
        ops->screen_capture_preflight_fn) {
        facts->screen_capture_access = ops->screen_capture_preflight_fn();
    }

    char path[PATH_MAX];
    if (doctor_format(path, sizeof(path), "%s/Library/LaunchAgents/com.asmvik.yabai.plist", identity->home)) {
        facts->service_plist = doctor_inspect_service(ops, identity, path, intended_executable);
    } else facts->service_plist = DOCTOR_SERVICE_UNKNOWN;

    const char *legacy[] = { "com.koekeishiya.yabai.plist", "homebrew.mxcl.yabai.plist" };
    facts->legacy_service_collision = DOCTOR_PROBE_NO;
    for (size_t i = 0; i < sizeof(legacy) / sizeof(*legacy); ++i) {
        if (!doctor_format(path, sizeof(path), "%s/Library/LaunchAgents/%s", identity->home, legacy[i])) {
            if (facts->legacy_service_collision != DOCTOR_PROBE_YES) facts->legacy_service_collision = DOCTOR_PROBE_UNKNOWN;
            continue;
        }
        struct stat st;
        if (ops->lstat_fn(path, &st) == 0) facts->legacy_service_collision = DOCTOR_PROBE_YES;
        else if (errno != ENOENT && facts->legacy_service_collision != DOCTOR_PROBE_YES) facts->legacy_service_collision = DOCTOR_PROBE_UNKNOWN;
    }

    facts->sa_socket = doctor_inspect_socket(ops, identity);
}

static enum doctor_probe doctor_binary_has_arm64_slice(const struct doctor_collect_ops *ops,
                                                        const char *path,
                                                        const struct stat *expected)
{
    NSData *data = nil;
    if (doctor_read_regular_snapshot(ops, path, expected, 512 * 1024 * 1024, &data, NULL) != DOCTOR_PROBE_YES ||
        [data length] < sizeof(uint32_t)) return DOCTOR_PROBE_UNKNOWN;
    const uint8_t *bytes = [data bytes];
    uint32_t magic;
    memcpy(&magic, bytes, sizeof(magic));
    if (magic == MH_MAGIC_64) {
        if ([data length] < sizeof(struct mach_header_64)) return DOCTOR_PROBE_UNKNOWN;
        return ((const struct mach_header_64 *) bytes)->cputype == CPU_TYPE_ARM64 ? DOCTOR_PROBE_YES : DOCTOR_PROBE_NO;
    }
    if (magic != FAT_CIGAM && magic != FAT_MAGIC) return DOCTOR_PROBE_UNKNOWN;
    bool swap = magic == FAT_CIGAM;
    if ([data length] < sizeof(struct fat_header)) return DOCTOR_PROBE_UNKNOWN;
    uint32_t count = ((const struct fat_header *) bytes)->nfat_arch;
    if (swap) count = OSSwapInt32(count);
    if (count > 32 || [data length] < sizeof(struct fat_header) + count * sizeof(struct fat_arch)) return DOCTOR_PROBE_UNKNOWN;
    const struct fat_arch *arches = (const struct fat_arch *) (bytes + sizeof(struct fat_header));
    for (uint32_t i = 0; i < count; ++i) {
        cpu_type_t type = arches[i].cputype;
        if (swap) type = OSSwapInt32(type);
        if (type == CPU_TYPE_ARM64) return DOCTOR_PROBE_YES;
    }
    return DOCTOR_PROBE_NO;
}

static void doctor_inspect_running_executable(const struct doctor_collect_ops *ops,
                                               const struct doctor_identity *identity,
                                               const char *path, struct doctor_facts *facts)
{
    if (!path || !identity) {
        facts->binary_executable = facts->arm64_slice = DOCTOR_PROBE_UNKNOWN;
        return;
    }
    char canonical[PATH_MAX];
    enum doctor_probe traversal = doctor_target_path_traversal(ops, identity, path, canonical, NULL);
    if (traversal != DOCTOR_PROBE_YES) {
        facts->binary_executable = traversal;
        facts->arm64_slice = DOCTOR_PROBE_UNKNOWN;
        return;
    }
    struct stat executable_stat;
    if (ops->stat_fn(canonical, &executable_stat) != 0) {
        int error = errno;
        facts->binary_executable = error == EACCES || error == EPERM ? DOCTOR_PROBE_NO : DOCTOR_PROBE_UNKNOWN;
        facts->arm64_slice = DOCTOR_PROBE_UNKNOWN;
    } else if (!S_ISREG(executable_stat.st_mode)) {
        facts->binary_executable = DOCTOR_PROBE_NO;
        facts->arm64_slice = DOCTOR_PROBE_UNKNOWN;
    } else {
        facts->binary_executable = doctor_target_permission(ops, identity, canonical, &executable_stat, R_OK | X_OK);
        facts->arm64_slice = doctor_binary_has_arm64_slice(ops, canonical, &executable_stat);
    }
}

static enum doctor_sa_install doctor_inspect_sa(const struct doctor_collect_ops *ops,
                                                    const struct doctor_identity *identity,
                                                    const char *bundle_path, const char *metadata_path)
{
    struct stat observed, bundle_stat, metadata_stat;
    if (ops->lstat_fn(bundle_path, &observed) != 0) return errno == ENOENT ? DOCTOR_SA_ABSENT : DOCTOR_SA_UNKNOWN;
    if (!identity) return DOCTOR_SA_UNKNOWN;
    char canonical[PATH_MAX];
    if (doctor_target_path_traversal(ops, identity, bundle_path, canonical, NULL) != DOCTOR_PROBE_YES ||
        ops->stat_fn(canonical, &bundle_stat) != 0 || !S_ISDIR(bundle_stat.st_mode)) return DOCTOR_SA_UNKNOWN;
    if (ops->lstat_fn(metadata_path, &observed) != 0) return DOCTOR_SA_UNKNOWN;
    if (doctor_target_path_traversal(ops, identity, metadata_path, canonical, NULL) != DOCTOR_PROBE_YES ||
        ops->stat_fn(canonical, &metadata_stat) != 0 || !S_ISREG(metadata_stat.st_mode) ||
        doctor_target_permission(ops, identity, canonical, &metadata_stat, R_OK) != DOCTOR_PROBE_YES) return DOCTOR_SA_UNKNOWN;
    NSData *metadata = nil;
    if (doctor_read_regular_snapshot(ops, canonical, &metadata_stat, 1024 * 1024, &metadata, NULL) != DOCTOR_PROBE_YES) return DOCTOR_SA_UNKNOWN;
    id plist_object = [NSPropertyListSerialization propertyListWithData:metadata options:NSPropertyListImmutable format:NULL error:NULL];
    if (![plist_object isKindOfClass:[NSDictionary class]]) return DOCTOR_SA_UNKNOWN;
    NSDictionary *plist = plist_object;
    id version = plist[@"CFBundleVersion"];
    return ![version isKindOfClass:[NSString class]] ? DOCTOR_SA_UNKNOWN :
           [version isEqualToString:@OSAX_VERSION] ? DOCTOR_SA_CURRENT : DOCTOR_SA_OUTDATED;
}

struct doctor_runtime_ops {
    void (*collect)(void *, struct doctor_facts *);
    void *context;
    FILE *stream;
};

struct doctor_production_context { char runtime_arch[32]; };

static void doctor_collect_production(void *opaque, struct doctor_facts *facts)
{
    struct doctor_production_context *context = opaque;
    facts->service_runtime = DOCTOR_PROBE_UNKNOWN;
    char executable[PATH_MAX] = {0};
    uint32_t executable_size = sizeof(executable);
    struct utsname system_info;
    if (uname(&system_info) == 0) snprintf(context->runtime_arch, sizeof(context->runtime_arch), "%s", system_info.machine);
    else snprintf(context->runtime_arch, sizeof(context->runtime_arch), "%s", "unknown architecture");
    facts->runtime_arch = context->runtime_arch;

    struct doctor_identity identity;
    bool identity_resolved = doctor_resolve_identity(&doctor_production_ops, &identity);
    facts->identity = identity_resolved ? DOCTOR_PROBE_YES : DOCTOR_PROBE_UNKNOWN;

    bool executable_known = _NSGetExecutablePath(executable, &executable_size) == 0;
    doctor_inspect_running_executable(&doctor_production_ops,
                                      identity_resolved ? &identity : NULL,
                                      executable_known ? executable : NULL, facts);

    int translated = 0;
    size_t translated_size = sizeof(translated);
    if (sysctlbyname("sysctl.proc_translated", &translated, &translated_size, NULL, 0) == 0) facts->translated = translated ? DOCTOR_PROBE_YES : DOCTOR_PROBE_NO;
    else facts->translated = errno == ENOENT ? DOCTOR_PROBE_NO : DOCTOR_PROBE_UNKNOWN;

    facts->accessibility = AXIsProcessTrusted() ? DOCTOR_PROBE_YES : DOCTOR_PROBE_NO;
    facts->sip_available = csr_get_active_config(&facts->sip_config) == 0 ? DOCTOR_PROBE_YES : DOCTOR_PROBE_UNKNOWN;
#ifdef __arm64__
    facts->arm64e_applicable = doctor_arm64e_applicability(true, facts->translated);
#else
    facts->arm64e_applicable = doctor_arm64e_applicability(false, facts->translated);
#endif
    if (facts->arm64e_applicable != DOCTOR_PROBE_NO) {
        char bootargs[2048] = {0};
        size_t bootargs_size = sizeof(bootargs) - 1;
        if (sysctlbyname("kern.bootargs", bootargs, &bootargs_size, NULL, 0) == 0) facts->arm64e_bootarg = doctor_classify_arm64e_bootargs(bootargs, bootargs_size);
        else facts->arm64e_bootarg = DOCTOR_PROBE_UNKNOWN;
    }

    if (identity_resolved) {
        doctor_collect_paths(facts, &doctor_production_ops, &identity, executable_known ? executable : NULL);
    } else {
        facts->identity = DOCTOR_PROBE_UNKNOWN;
        facts->service_plist = DOCTOR_SERVICE_UNKNOWN;
        facts->legacy_service_collision = facts->config_present = facts->config_readable =
            facts->screen_capture_required = facts->screen_capture_access = facts->sa_socket =
                DOCTOR_PROBE_UNKNOWN;
    }

    facts->sa_install = doctor_inspect_sa(&doctor_production_ops,
                                          identity_resolved ? &identity : NULL,
                                          "/Library/ScriptingAdditions/yabai.osax",
                                          "/Library/ScriptingAdditions/yabai.osax/Contents/Resources/payload.bundle/Contents/Info.plist");
    if (facts->sa_socket == DOCTOR_PROBE_YES) {
        switch (scripting_addition_probe_runtime()) {
        case SA_RUNTIME_CURRENT:    facts->sa_runtime = DOCTOR_SA_RUNTIME_CURRENT; break;
        case SA_RUNTIME_OUTDATED:   facts->sa_runtime = DOCTOR_SA_RUNTIME_OUTDATED; break;
        case SA_RUNTIME_INCOMPLETE: facts->sa_runtime = DOCTOR_SA_RUNTIME_INCOMPLETE; break;
        default:                    facts->sa_runtime = DOCTOR_SA_RUNTIME_UNREACHABLE; break;
        }
    }
}

static int doctor_run_with_ops(const struct doctor_runtime_ops *ops)
{
    struct doctor_facts facts = {0};
    ops->collect(ops->context, &facts);
    return doctor_report(&facts, ops->stream);
}

#ifdef TESTS
static const struct doctor_runtime_ops *doctor_test_runtime_override;
#endif

int doctor_run(void)
{
#ifdef TESTS
    if (doctor_test_runtime_override) return doctor_run_with_ops(doctor_test_runtime_override);
#endif
    struct doctor_production_context context = {0};
    struct doctor_runtime_ops ops = { .collect = doctor_collect_production, .context = &context, .stream = stdout };
    return doctor_run_with_ops(&ops);
}
