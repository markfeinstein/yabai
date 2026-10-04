static struct doctor_facts doctor_test_healthy_facts(void)
{
    return (struct doctor_facts) {
        .identity = DOCTOR_PROBE_YES,
        .binary_executable = DOCTOR_PROBE_YES,
        .translated = DOCTOR_PROBE_NO,
        .arm64_slice = DOCTOR_PROBE_YES,
        .accessibility = DOCTOR_PROBE_YES,
        .sip_available = DOCTOR_PROBE_YES,
        .sip_config = DOCTOR_REQUIRED_SIP,
        .arm64e_applicable = DOCTOR_PROBE_YES,
        .arm64e_bootarg = DOCTOR_PROBE_YES,
        .service_plist = DOCTOR_SERVICE_VALID,
        .service_runtime = DOCTOR_PROBE_UNKNOWN,
        .legacy_service_collision = DOCTOR_PROBE_NO,
        .config_present = DOCTOR_PROBE_YES,
        .config_readable = DOCTOR_PROBE_YES,
        .sa_install = DOCTOR_SA_CURRENT,
        .sa_socket = DOCTOR_PROBE_YES,
        .runtime_arch = "arm64"
    };
}

static bool doctor_test_output_contains(FILE *stream, const char *needle)
{
    char output[16384] = {0};
    rewind(stream);
    size_t length = fread(output, 1, sizeof(output) - 1, stream);
    output[length] = '\0';
    return strstr(output, needle) != NULL;
}

TEST_FUNC(doctor_healthy_fixture_returns_success,
{
    struct doctor_facts facts = doctor_test_healthy_facts();
    FILE *stream = tmpfile();
    TEST_CHECK(stream != NULL, true);
    if (stream) {
        TEST_CHECK(doctor_report(&facts, stream), EXIT_SUCCESS);
        TEST_CHECK(doctor_test_output_contains(stream, "UNKNOWN service runtime"), true);
        TEST_CHECK(doctor_test_output_contains(stream, "WARN scripting status"), true);
        TEST_CHECK(doctor_test_output_contains(stream, "SUMMARY 0 failures; doctor made no changes"), true);
        fclose(stream);
    }
});

TEST_FUNC(doctor_failures_and_unknown_privileged_checks_are_classified,
{
    struct doctor_facts facts = doctor_test_healthy_facts();
    facts.accessibility = DOCTOR_PROBE_NO;
    facts.config_readable = DOCTOR_PROBE_NO;
    facts.sip_available = DOCTOR_PROBE_UNKNOWN;
    facts.sa_install = DOCTOR_SA_UNKNOWN;
    FILE *stream = tmpfile();
    TEST_CHECK(stream != NULL, true);
    if (stream) {
        TEST_CHECK(doctor_report(&facts, stream), EXIT_FAILURE);
        TEST_CHECK(doctor_test_output_contains(stream, "FAIL accessibility"), true);
        TEST_CHECK(doctor_test_output_contains(stream, "FAIL configuration"), true);
        TEST_CHECK(doctor_test_output_contains(stream, "UNKNOWN SIP (scripting)"), true);
        TEST_CHECK(doctor_test_output_contains(stream, "SUMMARY 2 failures; doctor made no changes"), true);
        fclose(stream);
    }
});

TEST_FUNC(doctor_all_unknown_and_optional_warnings_return_success,
{
    struct doctor_facts facts = { .runtime_arch = "unknown" };
    facts.service_plist = DOCTOR_SERVICE_ABSENT;
    facts.config_present = DOCTOR_PROBE_NO;
    facts.sa_install = DOCTOR_SA_ABSENT;
    FILE *stream = tmpfile();
    TEST_CHECK(doctor_report(&facts, stream), EXIT_SUCCESS);
    TEST_CHECK(doctor_test_output_contains(stream, "SUMMARY 0 failures"), true);
    fclose(stream);
});

static uid_t doctor_test_uid;
static const char *doctor_test_sudo_uid;
static struct passwd doctor_test_passwd;
static uid_t doctor_test_getuid(void) { return doctor_test_uid; }
static struct passwd *doctor_test_getpwuid(uid_t uid) { return uid == doctor_test_passwd.pw_uid ? &doctor_test_passwd : NULL; }
static char *doctor_test_getenv(const char *name)
{
    return strcmp(name, "SUDO_UID") == 0 ? (char *) doctor_test_sudo_uid : NULL;
}

TEST_FUNC(doctor_identity_uses_real_uid_or_valid_nonroot_sudo_uid,
{
    struct doctor_collect_ops ops = doctor_production_ops;
    ops.getuid_fn = doctor_test_getuid;
    ops.getpwuid_fn = doctor_test_getpwuid;
    ops.getenv_fn = doctor_test_getenv;
    doctor_test_passwd = (struct passwd) { .pw_uid = 501, .pw_gid = 20, .pw_name = "fixture", .pw_dir = "/Users/fixture" };
    struct doctor_identity identity = {0};

    doctor_test_uid = 501; doctor_test_sudo_uid = NULL;
    TEST_CHECK(doctor_resolve_identity(&ops, &identity), true);
    TEST_CHECK(identity.uid, 501);
    TEST_CHECK(identity.gid, 20);
    TEST_CHECK(identity.elevated, false);
    doctor_test_uid = 0; doctor_test_sudo_uid = "501";
    TEST_CHECK(doctor_resolve_identity(&ops, &identity), true);
    TEST_CHECK(identity.elevated, true);
    doctor_test_sudo_uid = NULL;
    TEST_CHECK(doctor_resolve_identity(&ops, &identity), false);
    doctor_test_sudo_uid = "0";
    TEST_CHECK(doctor_resolve_identity(&ops, &identity), false);
});

static bool doctor_test_mkdir(const char *path) { return mkdir(path, 0700) == 0; }
static bool doctor_test_touch(const char *path)
{
    int fd = open(path, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) return false;
    return close(fd) == 0;
}
static char *doctor_test_xdg;
static char *doctor_test_config_getenv(const char *name)
{
    return strcmp(name, "XDG_CONFIG_HOME") == 0 ? doctor_test_xdg : NULL;
}
static int doctor_test_access_error;
static int doctor_test_access_failure(const char *path, int mode)
{
    (void) path; (void) mode;
    errno = doctor_test_access_error;
    return -1;
}

TEST_FUNC(doctor_config_selection_stops_at_preferred_regular_candidate,
{
    char root[] = "/tmp/yabai-doctor-config.XXXXXX";
    TEST_CHECK(mkdtemp(root) != NULL, true);
    char xdg[PATH_MAX], xdg_yabai[PATH_MAX], home_config[PATH_MAX], home_yabai[PATH_MAX];
    snprintf(xdg, sizeof(xdg), "%s/xdg", root); TEST_CHECK(doctor_test_mkdir(xdg), true);
    snprintf(xdg_yabai, sizeof(xdg_yabai), "%s/yabai", xdg); TEST_CHECK(doctor_test_mkdir(xdg_yabai), true);
    snprintf(home_config, sizeof(home_config), "%s/.config", root); TEST_CHECK(doctor_test_mkdir(home_config), true);
    snprintf(home_yabai, sizeof(home_yabai), "%s/yabai", home_config); TEST_CHECK(doctor_test_mkdir(home_yabai), true);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/yabai.jsonc", xdg_yabai); TEST_CHECK(doctor_test_touch(path), true);
    snprintf(path, sizeof(path), "%s/yabairc", xdg_yabai); TEST_CHECK(doctor_test_touch(path), true);
    snprintf(path, sizeof(path), "%s/yabai.jsonc", home_yabai); TEST_CHECK(doctor_test_mkdir(path), true);
    snprintf(path, sizeof(path), "%s/yabairc", home_yabai); TEST_CHECK(doctor_test_mkdir(path), true);

    doctor_test_xdg = xdg;
    struct doctor_collect_ops ops = doctor_production_ops; ops.getenv_fn = doctor_test_config_getenv;
    struct doctor_identity identity = { .uid = getuid(), .gid = getgid(), .name = "doctor-fixture", .home = root };
    struct doctor_facts facts = {0};
    doctor_collect_paths(&facts, &ops, &identity, "/bin/true");
    TEST_CHECK(facts.config_present, DOCTOR_PROBE_YES);
    TEST_CHECK(facts.config_readable, DOCTOR_PROBE_YES);
    snprintf(path, sizeof(path), "%s/selected-directory", root); TEST_CHECK(doctor_test_mkdir(path), true);
    TEST_CHECK(doctor_config_candidate(&ops, &identity, path, false), DOCTOR_CONFIG_BAD);
    char readable_file[PATH_MAX]; snprintf(readable_file, sizeof(readable_file), "%s/readable", root);
    TEST_CHECK(doctor_test_touch(readable_file), true);
    struct doctor_collect_ops access_ops = ops; access_ops.access_fn = doctor_test_access_failure;
    doctor_test_access_error = EACCES;
    TEST_CHECK(doctor_config_candidate(&access_ops, &identity, readable_file, false), DOCTOR_CONFIG_BAD);
    doctor_test_access_error = EIO;
    TEST_CHECK(doctor_config_candidate(&access_ops, &identity, readable_file, false), DOCTOR_CONFIG_UNKNOWN);
    doctor_test_access_error = ENOENT;
    TEST_CHECK(doctor_config_candidate(&access_ops, &identity, readable_file, false), DOCTOR_CONFIG_UNKNOWN);

    char fallback[PATH_MAX]; snprintf(fallback, sizeof(fallback), "%s/yabai.jsonc", xdg_yabai);
    const char *matrix[2] = { path, fallback };
    TEST_CHECK(doctor_select_config(&ops, &identity, matrix, 2, true), DOCTOR_CONFIG_OK);
    char fifo[PATH_MAX]; snprintf(fifo, sizeof(fifo), "%s/fifo", root); TEST_CHECK(mkfifo(fifo, 0600) == 0, true);
    matrix[0] = fifo;
    TEST_CHECK(doctor_select_config(&ops, &identity, matrix, 2, true), DOCTOR_CONFIG_BAD);
    unlink(fifo);
    char directory_link[PATH_MAX]; snprintf(directory_link, sizeof(directory_link), "%s/directory-link", root);
    TEST_CHECK(symlink(path, directory_link) == 0, true); matrix[0] = directory_link;
    TEST_CHECK(doctor_select_config(&ops, &identity, matrix, 2, true), DOCTOR_CONFIG_OK);

    char oversized_home[PATH_MAX + 32];
    oversized_home[0] = '/'; memset(oversized_home + 1, 'a', sizeof(oversized_home) - 2); oversized_home[sizeof(oversized_home) - 1] = '\0';
    struct doctor_identity oversized_identity = { .uid = getuid(), .gid = getgid(), .name = "fixture", .home = oversized_home };
    struct doctor_facts oversized_facts = {0}; doctor_test_xdg = NULL;
    doctor_collect_paths(&oversized_facts, &ops, &oversized_identity, "/bin/true");
    TEST_CHECK(oversized_facts.config_present, DOCTOR_PROBE_UNKNOWN);
    TEST_CHECK(oversized_facts.config_readable, DOCTOR_PROBE_UNKNOWN);
    TEST_CHECK(oversized_facts.service_plist, DOCTOR_SERVICE_UNKNOWN);
    const char *rm_args[] = { "/bin/rm", "-rf", root, NULL }; safe_exec((char *const *) rm_args, true);
});

static const char *doctor_test_fifo_replace_path;
static int doctor_test_fifo_replace_open(const char *path, int flags)
{
    if (doctor_test_fifo_replace_path && strcmp(path, doctor_test_fifo_replace_path) == 0) {
        unlink(path);
        if (mkfifo(path, 0600) != 0) return -1;
    }
    return open(path, flags);
}

static int doctor_test_read_error;
static ssize_t doctor_test_read_failure(int fd, void *buffer, size_t size)
{
    (void) fd; (void) buffer; (void) size;
    errno = doctor_test_read_error;
    return -1;
}

static int doctor_test_fstat_mode;
static int doctor_test_fstat_override(int fd, struct stat *st)
{
    int result = fstat(fd, st);
    if (result == 0 && doctor_test_fstat_mode == 1) st->st_uid += 1;
    if (result == 0 && doctor_test_fstat_mode == 2) st->st_ino += 1;
    return result;
}

static const char *doctor_test_replaced_path;
static int doctor_test_replaced_count;
static int doctor_test_replaced_mode;
static const char *doctor_test_denied_directory;
static const char *doctor_test_stat_error_path;
static int doctor_test_stat_error;
static int doctor_test_stat_override(const char *path, struct stat *st)
{
    if (doctor_test_stat_error_path && strcmp(path, doctor_test_stat_error_path) == 0) {
        errno = doctor_test_stat_error;
        return -1;
    }
    int result = stat(path, st);
    if (result == 0 && doctor_test_replaced_path && strcmp(path, doctor_test_replaced_path) == 0 &&
        ++doctor_test_replaced_count > 2) {
        if (doctor_test_replaced_mode == 1) st->st_ino += 1;
        else if (doctor_test_replaced_mode == 2) st->st_mode ^= S_IXUSR;
        else if (doctor_test_replaced_mode == 3) { st->st_uid += 1; st->st_gid += 1; }
    }
    if (result == 0 && doctor_test_denied_directory && strcmp(path, doctor_test_denied_directory) == 0) {
        st->st_mode &= ~(S_IXUSR | S_IXGRP | S_IXOTH);
    }
    return result;
}

static const char *doctor_test_retarget_path;
static const char *doctor_test_retarget_destination;
static int doctor_test_retarget_count;
static char *doctor_test_realpath_retarget(const char *path, char *resolved)
{
    if (doctor_test_retarget_path && strcmp(path, doctor_test_retarget_path) == 0 &&
        ++doctor_test_retarget_count > 1) {
        return realpath(doctor_test_retarget_destination, resolved);
    }
    return realpath(path, resolved);
}

static char *doctor_test_realpath_failure(const char *path, char *resolved)
{
    (void) path; (void) resolved;
    errno = EACCES;
    return NULL;
}

TEST_FUNC(doctor_service_requires_canonical_fields_mode_and_executable,
{
    char root[] = "/private/tmp/yabai-doctor-service.XXXXXX"; TEST_CHECK(mkdtemp(root) != NULL, true);
    char executable[PATH_MAX], plist_path[PATH_MAX];
    snprintf(executable, sizeof(executable), "%s/yabai", root); TEST_CHECK(doctor_test_touch(executable), true); chmod(executable, 0755);
    snprintf(plist_path, sizeof(plist_path), "%s/service.plist", root);
    NSString *out = @"/tmp/yabai_fixture.out.log", *err = @"/tmp/yabai_fixture.err.log";
    NSDictionary *plist = @{ @"Label": @"com.asmvik.yabai", @"ProgramArguments": @[ [NSString stringWithUTF8String:executable] ],
        @"EnvironmentVariables": @{ @"PATH": @"/usr/bin:/bin" }, @"RunAtLoad": @YES,
        @"KeepAlive": @{ @"SuccessfulExit": @NO, @"Crashed": @YES }, @"StandardOutPath": out,
        @"StandardErrorPath": err, @"ProcessType": @"Interactive", @"Nice": @(-20) };
    TEST_CHECK([plist writeToFile:[NSString stringWithUTF8String:plist_path] atomically:YES], true); chmod(plist_path, 0644);
    struct doctor_identity identity = { .uid = getuid(), .gid = getgid(), .name = "fixture", .home = root };
    TEST_CHECK(doctor_inspect_service(&doctor_production_ops, &identity, plist_path, executable), DOCTOR_SERVICE_VALID);
    TEST_CHECK(doctor_inspect_service(&doctor_production_ops, &identity, plist_path, NULL), DOCTOR_SERVICE_UNKNOWN);
    struct doctor_collect_ops descriptor_ops = doctor_production_ops; descriptor_ops.fstat_fn = doctor_test_fstat_override;
    doctor_test_fstat_mode = 1;
    TEST_CHECK(doctor_inspect_service(&descriptor_ops, &identity, plist_path, executable), DOCTOR_SERVICE_INVALID);
    doctor_test_fstat_mode = 2;
    TEST_CHECK(doctor_inspect_service(&descriptor_ops, &identity, plist_path, executable), DOCTOR_SERVICE_UNKNOWN);
    doctor_test_fstat_mode = 0;
    struct doctor_collect_ops fifo_ops = doctor_production_ops; fifo_ops.open_fn = doctor_test_fifo_replace_open;
    doctor_test_fifo_replace_path = plist_path;
    TEST_CHECK(doctor_inspect_service(&fifo_ops, &identity, plist_path, executable), DOCTOR_SERVICE_UNKNOWN);
    doctor_test_fifo_replace_path = NULL; unlink(plist_path);
    TEST_CHECK([plist writeToFile:[NSString stringWithUTF8String:plist_path] atomically:YES], true); chmod(plist_path, 0644);
    struct doctor_collect_ops replacement_ops = doctor_production_ops; replacement_ops.stat_fn = doctor_test_stat_override;
    doctor_test_replaced_path = executable;
    for (int replacement_mode = 1; replacement_mode <= 3; ++replacement_mode) {
        doctor_test_replaced_mode = replacement_mode; doctor_test_replaced_count = 0;
        TEST_CHECK(doctor_inspect_service(&replacement_ops, &identity, plist_path, executable), DOCTOR_SERVICE_UNKNOWN);
    }
    doctor_test_replaced_path = NULL;
    NSMutableDictionary *changed = [plist mutableCopy];
    changed[@"ProgramArguments"] = @[ @"/bin/true" ];
    TEST_CHECK([changed writeToFile:[NSString stringWithUTF8String:plist_path] atomically:YES], true); chmod(plist_path, 0644);
    TEST_CHECK(doctor_inspect_service(&doctor_production_ops, &identity, plist_path, executable), DOCTOR_SERVICE_INVALID);
    char executable_link[PATH_MAX]; snprintf(executable_link, sizeof(executable_link), "%s/yabai-link", root);
    TEST_CHECK(symlink(executable, executable_link) == 0, true);
    changed[@"ProgramArguments"] = @[ [NSString stringWithUTF8String:executable_link] ];
    TEST_CHECK([changed writeToFile:[NSString stringWithUTF8String:plist_path] atomically:YES], true); chmod(plist_path, 0644);
    identity.elevated = true;
    TEST_CHECK(doctor_inspect_service(&doctor_production_ops, &identity, plist_path, executable), DOCTOR_SERVICE_VALID_SYMLINK);
    struct doctor_collect_ops unresolved_ops = doctor_production_ops;
    unresolved_ops.realpath_fn = doctor_test_realpath_failure;
    TEST_CHECK(doctor_inspect_service(&unresolved_ops, &identity, plist_path, executable), DOCTOR_SERVICE_UNKNOWN);
    char lexical_dir[PATH_MAX], canonical_dir[PATH_MAX], lexical_link[PATH_MAX], canonical_executable[PATH_MAX];
    snprintf(lexical_dir, sizeof(lexical_dir), "%s/lexical", root); TEST_CHECK(doctor_test_mkdir(lexical_dir), true);
    snprintf(canonical_dir, sizeof(canonical_dir), "%s/canonical", root); TEST_CHECK(doctor_test_mkdir(canonical_dir), true);
    snprintf(canonical_executable, sizeof(canonical_executable), "%s/yabai", canonical_dir);
    TEST_CHECK(doctor_test_touch(canonical_executable), true); chmod(canonical_executable, 0755);
    char intermediate_link[PATH_MAX]; snprintf(intermediate_link, sizeof(intermediate_link), "%s/target", lexical_dir);
    TEST_CHECK(symlink(canonical_dir, intermediate_link) == 0, true);
    snprintf(lexical_link, sizeof(lexical_link), "%s/target/yabai", lexical_dir);
    changed[@"ProgramArguments"] = @[ [NSString stringWithUTF8String:lexical_link] ];
    TEST_CHECK([changed writeToFile:[NSString stringWithUTF8String:plist_path] atomically:YES], true); chmod(plist_path, 0644);
    char retarget_executable[PATH_MAX]; snprintf(retarget_executable, sizeof(retarget_executable), "%s/retarget", canonical_dir);
    TEST_CHECK(doctor_test_touch(retarget_executable), true); chmod(retarget_executable, 0755);
    struct doctor_collect_ops retarget_ops = doctor_production_ops; retarget_ops.realpath_fn = doctor_test_realpath_retarget;
    doctor_test_retarget_path = lexical_link; doctor_test_retarget_destination = retarget_executable; doctor_test_retarget_count = 0;
    TEST_CHECK(doctor_inspect_service(&retarget_ops, &identity, plist_path, canonical_executable), DOCTOR_SERVICE_UNKNOWN);
    doctor_test_retarget_path = NULL;
    struct doctor_collect_ops path_ops = doctor_production_ops; path_ops.stat_fn = doctor_test_stat_override;
    doctor_test_denied_directory = lexical_dir;
    TEST_CHECK(doctor_inspect_service(&path_ops, &identity, plist_path, canonical_executable), DOCTOR_SERVICE_INVALID);
    char resolved_canonical_dir[PATH_MAX];
    TEST_CHECK(realpath(canonical_dir, resolved_canonical_dir) != NULL, true);
    doctor_test_denied_directory = resolved_canonical_dir;
    TEST_CHECK(doctor_inspect_service(&path_ops, &identity, plist_path, canonical_executable), DOCTOR_SERVICE_INVALID);
    doctor_test_denied_directory = NULL;

    doctor_test_stat_error_path = canonical_executable; doctor_test_stat_error = EIO;
    TEST_CHECK(doctor_inspect_service(&path_ops, &identity, plist_path, canonical_executable), DOCTOR_SERVICE_UNKNOWN);
    doctor_test_stat_error = ENOENT;
    TEST_CHECK(doctor_inspect_service(&path_ops, &identity, plist_path, canonical_executable), DOCTOR_SERVICE_UNKNOWN);
    doctor_test_stat_error_path = NULL;

    identity.elevated = false;
    changed[@"ProgramArguments"] = plist[@"ProgramArguments"];
    changed[@"RunAtLoad"] = @1;
    TEST_CHECK([changed writeToFile:[NSString stringWithUTF8String:plist_path] atomically:YES], true); chmod(plist_path, 0644);
    TEST_CHECK(doctor_inspect_service(&doctor_production_ops, &identity, plist_path, executable), DOCTOR_SERVICE_INVALID);
    TEST_CHECK([plist writeToFile:[NSString stringWithUTF8String:plist_path] atomically:YES], true); chmod(plist_path, 0644);
    struct doctor_collect_ops access_ops = doctor_production_ops; access_ops.access_fn = doctor_test_access_failure;
    doctor_test_access_error = EACCES;
    TEST_CHECK(doctor_inspect_service(&access_ops, &identity, plist_path, executable), DOCTOR_SERVICE_INVALID);
    doctor_test_access_error = EIO;
    TEST_CHECK(doctor_inspect_service(&access_ops, &identity, plist_path, executable), DOCTOR_SERVICE_UNKNOWN);
    chmod(plist_path, 0666);
    TEST_CHECK(doctor_inspect_service(&doctor_production_ops, &identity, plist_path, executable), DOCTOR_SERVICE_INVALID);
    chmod(plist_path, 0644); chmod(executable, 0777);
    TEST_CHECK(doctor_inspect_service(&doctor_production_ops, &identity, plist_path, executable), DOCTOR_SERVICE_INVALID);
    chmod(executable, 0755);
    char real_plist[PATH_MAX]; snprintf(real_plist, sizeof(real_plist), "%s/real.plist", root);
    TEST_CHECK(rename(plist_path, real_plist) == 0, true);
    TEST_CHECK(symlink(real_plist, plist_path) == 0, true);
    TEST_CHECK(doctor_inspect_service(&doctor_production_ops, &identity, plist_path, executable), DOCTOR_SERVICE_INVALID);
    const char *rm_args[] = { "/bin/rm", "-rf", root, NULL }; safe_exec((char *const *) rm_args, true);
});

static int doctor_test_groups_mode;
static int doctor_test_getgrouplist(const char *name, int base, int *groups, int *count)
{
    (void) name; (void) base;
    if (doctor_test_groups_mode < 0) return -1;
    if (*count < 1) return -1;
    groups[0] = doctor_test_groups_mode;
    *count = 1;
    return 0;
}

TEST_FUNC(doctor_root_permission_checks_use_resolved_supplementary_groups,
{
    struct doctor_collect_ops ops = doctor_production_ops;
    ops.getgrouplist_fn = doctor_test_getgrouplist;
    struct doctor_identity identity = { .uid = 501, .gid = 20, .elevated = true, .name = "fixture" };
    struct stat st = { .st_uid = 501, .st_gid = 20, .st_mode = S_IFREG | 0000 };
    TEST_CHECK(doctor_target_permission(&ops, &identity, "/dev/null", &st, R_OK), DOCTOR_PROBE_NO);

    st.st_uid = 502; st.st_gid = 21; st.st_mode = S_IFREG | 0040;
    doctor_test_groups_mode = 21;
    TEST_CHECK(doctor_target_permission(&ops, &identity, "/dev/null", &st, R_OK), DOCTOR_PROBE_YES);
    st.st_mode = S_IFREG | 0004;
    TEST_CHECK(doctor_target_permission(&ops, &identity, "/dev/null", &st, R_OK), DOCTOR_PROBE_NO);

    doctor_test_groups_mode = 22;
    TEST_CHECK(doctor_target_permission(&ops, &identity, "/dev/null", &st, R_OK), DOCTOR_PROBE_YES);
    doctor_test_groups_mode = -1;
    TEST_CHECK(doctor_target_permission(&ops, &identity, "/dev/null", &st, R_OK), DOCTOR_PROBE_UNKNOWN);
});

static int doctor_test_collect_count;
static void doctor_test_runtime_collect(void *context, struct doctor_facts *facts)
{
    ++doctor_test_collect_count;
    *facts = *(struct doctor_facts *) context;
}

struct doctor_test_cli_result { int status; char output[16384]; char error[4096]; };

static void doctor_test_read_pipe(int fd, char *buffer, size_t capacity)
{
    size_t used = 0;
    char discard[512];
    for (;;) {
        char *destination = used + 1 < capacity ? buffer + used : discard;
        size_t available = used + 1 < capacity ? capacity - used - 1 : sizeof(discard);
        ssize_t bytes = read(fd, destination, available);
        if (bytes > 0) {
            if (destination != discard) used += (size_t) bytes;
            continue;
        }
        if (bytes < 0 && errno == EINTR) continue;
        break;
    }
    buffer[used] = '\0';
}

static void doctor_test_expected_output(struct doctor_facts *facts, char output[16384])
{
    FILE *stream = tmpfile();
    doctor_report(facts, stream);
    fflush(stream);
    rewind(stream);
    size_t bytes = fread(output, 1, 16383, stream);
    output[bytes] = '\0';
    fclose(stream);
}

static struct doctor_test_cli_result doctor_test_capture_cli(int argc, char **argv, struct doctor_facts *facts)
{
    struct doctor_test_cli_result result = {0};
    int output_pipe[2], error_pipe[2];
    if (pipe(output_pipe) != 0 || pipe(error_pipe) != 0) { result.status = -1; return result; }
    fflush(NULL);
    pid_t child = fork();
    if (child == 0) {
        close(output_pipe[0]); close(error_pipe[0]);
        dup2(output_pipe[1], STDOUT_FILENO); dup2(error_pipe[1], STDERR_FILENO);
        close(output_pipe[1]); close(error_pipe[1]);
        struct doctor_runtime_ops ops = { .collect = doctor_test_runtime_collect, .context = facts, .stream = stdout };
        doctor_test_runtime_override = &ops;
        parse_arguments(argc, argv);
        _exit(99);
    }
    close(output_pipe[1]); close(error_pipe[1]);
    doctor_test_read_pipe(output_pipe[0], result.output, sizeof(result.output));
    doctor_test_read_pipe(error_pipe[0], result.error, sizeof(result.error));
    close(output_pipe[0]); close(error_pipe[0]);
    waitpid(child, &result.status, 0);
    return result;
}

TEST_FUNC(doctor_runtime_orchestration_and_exact_cli_are_injectable,
{
    struct doctor_facts facts = doctor_test_healthy_facts();
    FILE *stream = tmpfile();
    struct doctor_runtime_ops ops = { .collect = doctor_test_runtime_collect, .context = &facts, .stream = stream };
    doctor_test_collect_count = 0;
    TEST_CHECK(doctor_run_with_ops(&ops), EXIT_SUCCESS);
    TEST_CHECK(doctor_test_collect_count, 1);
    TEST_CHECK(doctor_test_output_contains(stream, "SUMMARY 0 failures; doctor made no changes"), true);
    fclose(stream);

    setenv("DOCTOR_SECRET_MARKER", "must-not-appear", 1);
    char *exact_argv[] = { "yabai", "--doctor" };
    char expected[16384]; doctor_test_expected_output(&facts, expected);
    struct doctor_test_cli_result success = doctor_test_capture_cli(2, exact_argv, &facts);
    TEST_CHECK(WIFEXITED(success.status) && WEXITSTATUS(success.status) == EXIT_SUCCESS, true);
    TEST_CHECK(strcmp(success.output, expected), 0);
    TEST_CHECK(success.error[0], '\0');
    TEST_CHECK(strstr(success.output, "must-not-appear") == NULL, true);

    facts.accessibility = DOCTOR_PROBE_NO;
    doctor_test_expected_output(&facts, expected);
    struct doctor_test_cli_result failure = doctor_test_capture_cli(2, exact_argv, &facts);
    TEST_CHECK(WIFEXITED(failure.status) && WEXITSTATUS(failure.status) == EXIT_FAILURE, true);
    TEST_CHECK(strcmp(failure.output, expected), 0);
    TEST_CHECK(failure.error[0], '\0');
    TEST_CHECK(strstr(failure.output, "must-not-appear") == NULL, true);

    char *trailing_argv[] = { "yabai", "--doctor", "extra" };
    struct doctor_test_cli_result trailing = doctor_test_capture_cli(3, trailing_argv, &facts);
    TEST_CHECK(WIFEXITED(trailing.status) && WEXITSTATUS(trailing.status) == EXIT_FAILURE, true);
    TEST_CHECK(trailing.output[0], '\0');
    TEST_CHECK(strcmp(trailing.error, "yabai: option '--doctor' does not accept arguments!\n"), 0);
    TEST_CHECK(strstr(trailing.error, "must-not-appear") == NULL, true);
    unsetenv("DOCTOR_SECRET_MARKER");
});

TEST_FUNC(doctor_sa_bundle_absence_and_nested_metadata_are_distinct,
{
    char root[] = "/private/tmp/yabai-doctor-sa.XXXXXX"; TEST_CHECK(mkdtemp(root) != NULL, true);
    char absent_bundle[PATH_MAX], metadata[PATH_MAX];
    snprintf(absent_bundle, sizeof(absent_bundle), "%s/absent.osax", root);
    snprintf(metadata, sizeof(metadata), "%s/Info.plist", root);
    struct doctor_identity identity = { .uid = getuid(), .gid = getgid(), .name = "fixture", .home = root };
    TEST_CHECK(doctor_inspect_sa(&doctor_production_ops, &identity, absent_bundle, metadata), DOCTOR_SA_ABSENT);
    TEST_CHECK(doctor_inspect_sa(&doctor_production_ops, &identity, root, metadata), DOCTOR_SA_UNKNOWN);
    NSDictionary *plist = @{ @"CFBundleVersion": @OSAX_VERSION };
    TEST_CHECK([plist writeToFile:[NSString stringWithUTF8String:metadata] atomically:YES], true);
    TEST_CHECK(doctor_inspect_sa(&doctor_production_ops, &identity, root, metadata), DOCTOR_SA_CURRENT);
    struct doctor_collect_ops snapshot_ops = doctor_production_ops; snapshot_ops.fstat_fn = doctor_test_fstat_override;
    doctor_test_fstat_mode = 2;
    TEST_CHECK(doctor_inspect_sa(&snapshot_ops, &identity, root, metadata), DOCTOR_SA_UNKNOWN);
    doctor_test_fstat_mode = 0; snapshot_ops = doctor_production_ops; snapshot_ops.read_fn = doctor_test_read_failure;
    doctor_test_read_error = EIO;
    TEST_CHECK(doctor_inspect_sa(&snapshot_ops, &identity, root, metadata), DOCTOR_SA_UNKNOWN);
    unlink(metadata);
    TEST_CHECK(doctor_inspect_sa(&doctor_production_ops, &identity, root, metadata), DOCTOR_SA_UNKNOWN);
    const char *rm_args[] = { "/bin/rm", "-rf", root, NULL }; safe_exec((char *const *) rm_args, true);
});

TEST_FUNC(doctor_bootarg_socket_architecture_and_cli_probes_are_bounded,
{
    TEST_CHECK(doctor_arm64e_applicability(true, DOCTOR_PROBE_NO), DOCTOR_PROBE_YES);
    TEST_CHECK(doctor_arm64e_applicability(false, DOCTOR_PROBE_YES), DOCTOR_PROBE_YES);
    TEST_CHECK(doctor_arm64e_applicability(false, DOCTOR_PROBE_NO), DOCTOR_PROBE_NO);
    TEST_CHECK(doctor_arm64e_applicability(false, DOCTOR_PROBE_UNKNOWN), DOCTOR_PROBE_UNKNOWN);

    struct doctor_facts platform = doctor_test_healthy_facts();
    platform.runtime_arch = "x86_64"; platform.translated = DOCTOR_PROBE_YES;
    platform.arm64e_applicable = DOCTOR_PROBE_YES; platform.arm64e_bootarg = DOCTOR_PROBE_NO;
    FILE *platform_stream = tmpfile(); doctor_report(&platform, platform_stream);
    TEST_CHECK(doctor_test_output_contains(platform_stream, "WARN arm64e boot arg"), true); fclose(platform_stream);
    platform.translated = DOCTOR_PROBE_NO; platform.arm64e_applicable = DOCTOR_PROBE_NO;
    platform_stream = tmpfile(); doctor_report(&platform, platform_stream);
    TEST_CHECK(doctor_test_output_contains(platform_stream, "arm64e boot arg"), false); fclose(platform_stream);

    TEST_CHECK(doctor_classify_arm64e_bootargs("foo -arm64e_preview_abi bar", 28), DOCTOR_PROBE_YES);
    TEST_CHECK(doctor_classify_arm64e_bootargs("foo", 3), DOCTOR_PROBE_NO);
    TEST_CHECK(doctor_classify_arm64e_bootargs("x-arm64e_preview_abi", 21), DOCTOR_PROBE_NO);
    TEST_CHECK(doctor_classify_arm64e_bootargs("-arm64e_preview_abi_suffix", 27), DOCTOR_PROBE_NO);
    TEST_CHECK(doctor_classify_arm64e_bootargs("\t-arm64e_preview_abi\n", 22), DOCTOR_PROBE_YES);
    TEST_CHECK(doctor_classify_arm64e_bootargs(NULL, 0), DOCTOR_PROBE_UNKNOWN);

    char thin[] = "/tmp/yabai-doctor-thin.XXXXXX"; int fd = mkstemp(thin);
    struct mach_header_64 header = { .magic = MH_MAGIC_64, .cputype = CPU_TYPE_X86_64 };
    TEST_CHECK(write(fd, &header, sizeof(header)), (ssize_t) sizeof(header)); close(fd); chmod(thin, 0700);
    struct stat thin_stat; TEST_CHECK(stat(thin, &thin_stat) == 0, true);
    TEST_CHECK(doctor_binary_has_arm64_slice(&doctor_production_ops, thin, &thin_stat), DOCTOR_PROBE_NO);
    struct doctor_collect_ops snapshot_ops = doctor_production_ops; snapshot_ops.fstat_fn = doctor_test_fstat_override;
    doctor_test_fstat_mode = 2;
    TEST_CHECK(doctor_binary_has_arm64_slice(&snapshot_ops, thin, &thin_stat), DOCTOR_PROBE_UNKNOWN);
    doctor_test_fstat_mode = 0; snapshot_ops = doctor_production_ops; snapshot_ops.read_fn = doctor_test_read_failure;
    doctor_test_read_error = EIO;
    TEST_CHECK(doctor_binary_has_arm64_slice(&snapshot_ops, thin, &thin_stat), DOCTOR_PROBE_UNKNOWN);
    char thin_link[PATH_MAX]; snprintf(thin_link, sizeof(thin_link), "%s-link", thin); unlink(thin_link);
    TEST_CHECK(symlink(thin, thin_link) == 0, true);
    struct doctor_identity binary_identity = { .uid = getuid(), .gid = getgid(), .name = "fixture", .home = "/tmp" };
    struct doctor_facts executable_facts = {0};
    doctor_inspect_running_executable(&doctor_production_ops, &binary_identity, thin_link, &executable_facts);
    TEST_CHECK(executable_facts.binary_executable, DOCTOR_PROBE_YES);
    TEST_CHECK(executable_facts.arm64_slice, DOCTOR_PROBE_NO);
    binary_identity.elevated = true; memset(&executable_facts, 0, sizeof(executable_facts));
    doctor_inspect_running_executable(&doctor_production_ops, &binary_identity, thin_link, &executable_facts);
    TEST_CHECK(executable_facts.binary_executable, DOCTOR_PROBE_YES);
    binary_identity.elevated = false;
    struct doctor_collect_ops executable_ops = doctor_production_ops;
    executable_ops.access_fn = doctor_test_access_failure;
    doctor_test_access_error = EACCES; memset(&executable_facts, 0, sizeof(executable_facts));
    doctor_inspect_running_executable(&executable_ops, &binary_identity, thin_link, &executable_facts);
    TEST_CHECK(executable_facts.binary_executable, DOCTOR_PROBE_NO);
    doctor_test_access_error = EIO; memset(&executable_facts, 0, sizeof(executable_facts));
    doctor_inspect_running_executable(&executable_ops, &binary_identity, thin_link, &executable_facts);
    TEST_CHECK(executable_facts.binary_executable, DOCTOR_PROBE_UNKNOWN);
    doctor_test_access_error = ENOENT; memset(&executable_facts, 0, sizeof(executable_facts));
    doctor_inspect_running_executable(&executable_ops, &binary_identity, thin_link, &executable_facts);
    TEST_CHECK(executable_facts.binary_executable, DOCTOR_PROBE_UNKNOWN);
    executable_ops = doctor_production_ops; executable_ops.stat_fn = doctor_test_stat_override;
    char resolved_thin_link[PATH_MAX]; TEST_CHECK(realpath(thin_link, resolved_thin_link) != NULL, true);
    doctor_test_stat_error_path = resolved_thin_link; doctor_test_stat_error = EACCES;
    memset(&executable_facts, 0, sizeof(executable_facts));
    doctor_inspect_running_executable(&executable_ops, &binary_identity, thin_link, &executable_facts);
    TEST_CHECK(executable_facts.binary_executable, DOCTOR_PROBE_NO);
    doctor_test_stat_error = EIO; memset(&executable_facts, 0, sizeof(executable_facts));
    doctor_inspect_running_executable(&executable_ops, &binary_identity, thin_link, &executable_facts);
    TEST_CHECK(executable_facts.binary_executable, DOCTOR_PROBE_UNKNOWN);
    doctor_test_stat_error = ENOENT; memset(&executable_facts, 0, sizeof(executable_facts));
    doctor_inspect_running_executable(&executable_ops, &binary_identity, thin_link, &executable_facts);
    TEST_CHECK(executable_facts.binary_executable, DOCTOR_PROBE_UNKNOWN);
    doctor_test_stat_error_path = NULL;
    unlink(thin_link); unlink(thin);

    char runtime_home[] = "/private/tmp/yabai-doctor-runtime.XXXXXX";
    TEST_CHECK(mkdtemp(runtime_home) != NULL, true);
    char library[PATH_MAX], caches[PATH_MAX];
    snprintf(library, sizeof(library), "%s/Library", runtime_home);
    snprintf(caches, sizeof(caches), "%s/Library/Caches", runtime_home);
    TEST_CHECK(mkdir(library, 0700) == 0, true);
    TEST_CHECK(mkdir(caches, 0700) == 0, true);
    char absent_yabai[PATH_MAX];
    snprintf(absent_yabai, sizeof(absent_yabai), "%s/Library/Caches/yabai", runtime_home);
    struct stat caches_before, caches_after;
    TEST_CHECK(stat(caches, &caches_before) == 0, true);
    TEST_CHECK(lstat(absent_yabai, &caches_after) != 0 && errno == ENOENT, true);
    struct doctor_identity identity = { .uid = getuid(), .gid = getgid(), .name = "ignored-name", .home = runtime_home };
    TEST_CHECK(doctor_inspect_socket(&doctor_production_ops, &identity), DOCTOR_PROBE_NO);
    identity.elevated = true; /* Simulates a root caller inspecting SUDO_UID. */
    TEST_CHECK(doctor_inspect_socket(&doctor_production_ops, &identity), DOCTOR_PROBE_NO);
    TEST_CHECK(lstat(absent_yabai, &caches_after) != 0 && errno == ENOENT, true);
    TEST_CHECK(stat(caches, &caches_after) == 0, true);
    TEST_CHECK(caches_after.st_dev, caches_before.st_dev);
    TEST_CHECK(caches_after.st_ino, caches_before.st_ino);
    TEST_CHECK(caches_after.st_mode, caches_before.st_mode);
    TEST_CHECK(caches_after.st_uid, caches_before.st_uid);
    TEST_CHECK(caches_after.st_gid, caches_before.st_gid);
    TEST_CHECK(caches_after.st_mtimespec.tv_sec, caches_before.st_mtimespec.tv_sec);
    TEST_CHECK(caches_after.st_mtimespec.tv_nsec, caches_before.st_mtimespec.tv_nsec);

    struct yabai_runtime_paths runtime_paths;
    TEST_CHECK(yabai_runtime_prepare_for_uid(getuid(), runtime_home, true, &runtime_paths), true);
    g_test_runtime_effective_uid = 0;
    struct yabai_runtime_paths mutating_paths;
    TEST_CHECK(yabai_runtime_prepare_for_uid(getuid(), runtime_home, false, &mutating_paths), false);
    char expected_socket[PATH_MAX];
    snprintf(expected_socket, sizeof(expected_socket), "%s/Library/Caches/yabai/runtime/sa.socket", runtime_home);
    TEST_CHECK(strcmp(runtime_paths.sa_socket, expected_socket), 0);
    int sock = socket(AF_UNIX, SOCK_STREAM, 0); struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", runtime_paths.sa_socket);
    TEST_CHECK(bind(sock, (struct sockaddr *) &addr, sizeof(addr)) == 0, true); chmod(runtime_paths.sa_socket, 0600);
    TEST_CHECK(doctor_inspect_socket(&doctor_production_ops, &identity), DOCTOR_PROBE_YES);
    chmod(runtime_paths.sa_socket, 0666);
    TEST_CHECK(doctor_inspect_socket(&doctor_production_ops, &identity), DOCTOR_PROBE_UNKNOWN);
    close(sock); unlink(runtime_paths.sa_socket);
    TEST_CHECK(doctor_test_touch(runtime_paths.sa_socket), true);
    TEST_CHECK(doctor_inspect_socket(&doctor_production_ops, &identity), DOCTOR_PROBE_UNKNOWN);
    unlink(runtime_paths.sa_socket);
    chmod(caches, 0777);
    TEST_CHECK(doctor_inspect_socket(&doctor_production_ops, &identity), DOCTOR_PROBE_UNKNOWN);
    chmod(caches, 0700);
    g_test_runtime_effective_uid = (uid_t) -1;
    const char *runtime_rm_args[] = { "/bin/rm", "-rf", runtime_home, NULL };
    safe_exec((char *const *) runtime_rm_args, true);

    char *argv[] = { "yabai", "--doctor" };
    TEST_CHECK(declarative_primary_action_classify(2, argv), DECLARATIVE_PRIMARY_ESTABLISHED);
});
