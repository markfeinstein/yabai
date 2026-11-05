TEST_FUNC(declarative_primary_action_classifier_is_exclusive,
    char *daemon[] = { "yabai", "--verbose" };
    char *check[] = { "yabai", "--check-config", "candidate.jsonc" };
    char *missing[] = { "yabai", "--check-config" };
    char *mixed[] = { "yabai", "--verbose", "--check-config", "candidate.jsonc" };
    char *duplicate[] = { "yabai", "--check-config", "a", "--check-config", "b" };
    char *flag_path[] = { "yabai", "--check-config", "--verbose" };
    char *help_first[] = { "yabai", "--help", "--check-config" };
    const char *established[] = {
        "--help", "-h", "--version", "-v", "--message", "-m", "--load-sa",
        "--uninstall-sa", "--install-service", "--uninstall-service", "--start-service",
        "--restart-service", "--stop-service"
    };
    char *short_literal[] = { "yabai", "-c", "--check-config" };
    char *long_literal[] = { "yabai", "--config", "--check-config" };
    char *native_literal[] = { "yabai", "--config-jsonc", "--check-config" };
    char *missing_legacy_value[] = { "yabai", "-c" };
    char *missing_native_value[] = { "yabai", "--config-jsonc" };
    char *mixed_after_value[] = { "yabai", "-c", "legacy", "--check-config", "candidate.jsonc" };
    TEST_CHECK(declarative_primary_action_classify(2, daemon), DECLARATIVE_PRIMARY_DAEMON);
    TEST_CHECK(declarative_primary_action_classify(3, check), DECLARATIVE_PRIMARY_CHECK);
    TEST_CHECK(declarative_primary_action_classify(2, missing), DECLARATIVE_PRIMARY_INVALID_CHECK);
    TEST_CHECK(declarative_primary_action_classify(4, mixed), DECLARATIVE_PRIMARY_INVALID_CHECK);
    TEST_CHECK(declarative_primary_action_classify(5, duplicate), DECLARATIVE_PRIMARY_INVALID_CHECK);
    TEST_CHECK(declarative_primary_action_classify(3, flag_path), DECLARATIVE_PRIMARY_INVALID_CHECK);
    TEST_CHECK(declarative_primary_action_classify(3, help_first), DECLARATIVE_PRIMARY_INVALID_CHECK);
    for (size_t i = 0; i < array_count(established); ++i) {
        char *argv[] = { "yabai", (char *) established[i], "--check-config" };
        TEST_CHECK(declarative_primary_action_classify(3, argv), DECLARATIVE_PRIMARY_INVALID_CHECK);
    }
    TEST_CHECK(declarative_primary_action_classify(3, short_literal), DECLARATIVE_PRIMARY_DAEMON);
    TEST_CHECK(declarative_primary_action_classify(3, long_literal), DECLARATIVE_PRIMARY_DAEMON);
    TEST_CHECK(declarative_primary_action_classify(3, native_literal), DECLARATIVE_PRIMARY_DAEMON);
    TEST_CHECK(declarative_primary_action_classify(2, missing_legacy_value), DECLARATIVE_PRIMARY_DAEMON);
    TEST_CHECK(declarative_primary_action_classify(2, missing_native_value), DECLARATIVE_PRIMARY_DAEMON);
    TEST_CHECK(declarative_primary_action_classify(5, mixed_after_value), DECLARATIVE_PRIMARY_INVALID_CHECK);
)

TEST_FUNC(declarative_startup_paths_are_absolute_lexical_and_bounded,
    char output[PATH_MAX]; struct declarative_error error = {0};
    TEST_CHECK(declarative_startup_capture_path("dir/../candidate.jsonc", "/tmp/base", output, &error), true);
    TEST_CHECK(strcmp(output, "/tmp/base/dir/../candidate.jsonc"), 0);
    TEST_CHECK(declarative_startup_capture_path("/tmp/link.jsonc", NULL, output, &error), true);
    TEST_CHECK(strcmp(output, "/tmp/link.jsonc"), 0);
    char invalid[] = { 'x', (char)0xff, 0 };
    TEST_CHECK(declarative_startup_capture_path(invalid, "/tmp", output, &error), false);
    TEST_CHECK(error.stage, DECLARATIVE_ERROR_PATH);
    char *long_path = malloc(PATH_MAX + 1); memset(long_path, 'x', PATH_MAX); long_path[PATH_MAX] = 0;
    TEST_CHECK(declarative_startup_capture_path(long_path, "/tmp", output, &error), false);
    TEST_CHECK(error.stage, DECLARATIVE_ERROR_PATH); free(long_path);
)

static int declarative_cli_child(int argc, char **argv, char *out, size_t out_size, char *err, size_t err_size)
{
    fflush(NULL);
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(out_pipe[0]); close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO); dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]); close(err_pipe[1]);
        parse_arguments(argc, argv);
        _exit(99);
    }
    close(out_pipe[1]); close(err_pipe[1]);
    ssize_t n = read(out_pipe[0], out, out_size - 1); out[n > 0 ? (size_t)n : 0] = 0;
    n = read(err_pipe[0], err, err_size - 1); err[n > 0 ? (size_t)n : 0] = 0;
    close(out_pipe[0]); close(err_pipe[0]);
    int status = 0; waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

TEST_FUNC(declarative_check_subprocess_has_exact_streams_and_grammar,
    char path[] = "/tmp/yabai-check-XXXXXX"; int fd = mkstemp(path);
    const char json[] = "{\"version\":1}"; TEST_CHECK(fd >= 0, true);
    if (fd >= 0) { TEST_CHECK(write(fd, json, sizeof(json)-1), (ssize_t)sizeof(json)-1); close(fd); }
    char out[512], err[1024]; char *valid[] = { "yabai", "--check-config", path };
    TEST_CHECK(declarative_cli_child(3, valid, out, sizeof(out), err, sizeof(err)), EXIT_SUCCESS);
    TEST_CHECK(strcmp(out, "configuration is valid; runtime preparation was not checked\n"), 0);
    TEST_CHECK(err[0], 0);
    char *missing[] = { "yabai", "--check-config" };
    TEST_CHECK(declarative_cli_child(2, missing, out, sizeof(out), err, sizeof(err)), EXIT_FAILURE);
    TEST_CHECK(out[0], 0); TEST_CHECK(strstr(err, "requires exactly one path") != NULL, true);
    char *mixed[] = { "yabai", "--check-config", path, "--verbose" };
    TEST_CHECK(declarative_cli_child(4, mixed, out, sizeof(out), err, sizeof(err)), EXIT_FAILURE);
    TEST_CHECK(out[0], 0); TEST_CHECK(strstr(err, "requires exactly one path") != NULL, true);
    char *actual_mixed[] = { "yabai", "-c", "legacy", "--check-config", path };
    TEST_CHECK(declarative_cli_child(5, actual_mixed, out, sizeof(out), err, sizeof(err)), EXIT_FAILURE);
    TEST_CHECK(out[0], 0); TEST_CHECK(strstr(err, "requires exactly one path") != NULL, true);
    const char *established[] = {
        "--help", "-h", "--version", "-v", "--message", "-m", "--load-sa",
        "--uninstall-sa", "--install-service", "--uninstall-service", "--start-service",
        "--restart-service", "--stop-service"
    };
    for (size_t i = 0; i < array_count(established); ++i) {
        char *argv[] = { "yabai", (char *) established[i], "--check-config" };
        TEST_CHECK(declarative_cli_child(3, argv, out, sizeof(out), err, sizeof(err)), EXIT_FAILURE);
        TEST_CHECK(out[0], 0);
        TEST_CHECK(strstr(err, "requires exactly one path") != NULL, true);
    }
    char *missing_legacy_value[] = { "yabai", "-c" };
    TEST_CHECK(declarative_cli_child(2, missing_legacy_value, out, sizeof(out), err, sizeof(err)), EXIT_FAILURE);
    TEST_CHECK(out[0], 0); TEST_CHECK(strstr(err, "requires an argument") != NULL, true);
    TEST_CHECK(strstr(err, "requires exactly one path") == NULL, true);
    char *missing_native_value[] = { "yabai", "--config-jsonc" };
    TEST_CHECK(declarative_cli_child(2, missing_native_value, out, sizeof(out), err, sizeof(err)), EXIT_FAILURE);
    TEST_CHECK(out[0], 0); TEST_CHECK(strstr(err, "requires an argument") != NULL, true);
    TEST_CHECK(strstr(err, "requires exactly one path") == NULL, true);
    unlink(path);
)

TEST_FUNC(declarative_help_lists_native_jsonc_actions,
    char out[4096], err[512];
    char *argv[] = { "yabai", "--help" };
    TEST_CHECK(declarative_cli_child(2, argv, out, sizeof(out), err, sizeof(err)), EXIT_SUCCESS);
    TEST_CHECK(err[0], 0);
    TEST_CHECK(strstr(out, "--config-jsonc <config>") != NULL, true);
    TEST_CHECK(strstr(out, "Use the specified native JSONC configuration file.") != NULL, true);
    TEST_CHECK(strstr(out, "--check-config <config>") != NULL, true);
    TEST_CHECK(strstr(out, "Validate JSONC schema only; exit 0 if valid, nonzero otherwise.") != NULL, true);
)

TEST_FUNC(declarative_daemon_flags_preserve_legacy_and_native_independence,
    memset(g_config_file, 0, sizeof(g_config_file)); memset(g_native_config_file, 0, sizeof(g_native_config_file));
    g_native_config_explicit = false; g_verbose = false;
    char *short_literal[] = { "yabai", "-c", "--check-config" };
    parse_arguments(3, short_literal);
    TEST_CHECK(strcmp(g_config_file, "--check-config"), 0);

    memset(g_config_file, 0, sizeof(g_config_file));
    char *long_literal[] = { "yabai", "--config", "--check-config" };
    parse_arguments(3, long_literal);
    TEST_CHECK(strcmp(g_config_file, "--check-config"), 0);

    char cwd[PATH_MAX], expected[PATH_MAX]; TEST_CHECK(getcwd(cwd, sizeof(cwd)) != NULL, true);
    snprintf(expected, sizeof(expected), "%s/--check-config", cwd);
    char *native_literal[] = { "yabai", "--config-jsonc", "--check-config" };
    parse_arguments(3, native_literal);
    TEST_CHECK(g_native_config_explicit, true); TEST_CHECK(strcmp(g_native_config_file, expected), 0);

    memset(g_config_file, 0, sizeof(g_config_file)); memset(g_native_config_file, 0, sizeof(g_native_config_file));
    g_native_config_explicit = false;
    char *argv[] = { "yabai", "--config", "literal/../legacy file", "--config-jsonc", "native.jsonc", "--verbose" };
    parse_arguments(6, argv);
    TEST_CHECK(strcmp(g_config_file, "literal/../legacy file"), 0);
    TEST_CHECK(g_native_config_explicit, true); TEST_CHECK(g_native_config_file[0], '/'); TEST_CHECK(g_verbose, true);
)

TEST_FUNC(declarative_startup_selection_obeys_discovery_and_missing_policy,
    char root[] = "/tmp/yabai-startup-XXXXXX"; TEST_CHECK(mkdtemp(root) != NULL, true);
    char home[PATH_MAX], config[PATH_MAX], yabai[PATH_MAX], file[PATH_MAX];
    snprintf(home, sizeof(home), "%s/home", root); mkdir(home, 0700);
    snprintf(config, sizeof(config), "%s/.config", home); mkdir(config, 0700);
    snprintf(yabai, sizeof(yabai), "%s/yabai", config); mkdir(yabai, 0700);
    snprintf(file, sizeof(file), "%s/yabai.jsonc", yabai);
    int fd = open(file, O_CREAT|O_WRONLY|O_TRUNC, 0600); const char json[] = "{\"version\":1}";
    TEST_CHECK(fd >= 0, true); if (fd >= 0) { TEST_CHECK(write(fd, json, sizeof(json)-1), (ssize_t)sizeof(json)-1); close(fd); }

    struct declarative_startup_state state = {0}; struct declarative_error error = {0};
    TEST_CHECK(declarative_startup_select(&state, NULL, "relative-xdg", home, &error), true);
    TEST_CHECK(state.present, true); TEST_CHECK(strcmp(state.path, file), 0); declarative_startup_destroy(&state);
    TEST_CHECK(declarative_startup_select(&state, "/tmp/yabai-explicit-does-not-exist", NULL, NULL, &error), false);
    TEST_CHECK(error.stage, DECLARATIVE_ERROR_OPEN); TEST_CHECK(state.present, false);
    TEST_CHECK(declarative_startup_select(&state, NULL, "/tmp/yabai-xdg-does-not-exist", "/tmp/yabai-home-does-not-exist", &error), true);
    TEST_CHECK(state.present, false); declarative_startup_destroy(&state);
    unlink(file); rmdir(yabai); rmdir(config); rmdir(home); rmdir(root);
)
