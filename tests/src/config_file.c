TEST_FUNC(config_file_discovery_rejects_truncated_paths,
{
    char directory[] = "/tmp/yabai-config-discovery.XXXXXX";
    char *root = mkdtemp(directory);
    TEST_CHECK(root != NULL, true);
    if (!root) return result;

    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char *saved_home = home ? strdup(home) : NULL;
    char *saved_xdg = xdg ? strdup(xdg) : NULL;
    bool saved_environment = (!home || saved_home) && (!xdg || saved_xdg);
    TEST_CHECK(saved_environment, true);
    if (!saved_environment) {
        free(saved_home);
        free(saved_xdg);
        rmdir(root);
        return result;
    }

    char xdg_root[PATH_MAX];
    char xdg_yabai[PATH_MAX];
    char home_config[PATH_MAX];
    char home_yabai[PATH_MAX];
    snprintf(xdg_root, sizeof(xdg_root), "%s/xdg", root);
    snprintf(xdg_yabai, sizeof(xdg_yabai), "%s/xdg/yabai", root);
    snprintf(home_config, sizeof(home_config), "%s/.config", root);
    snprintf(home_yabai, sizeof(home_yabai), "%s/.config/yabai", root);
    TEST_CHECK(mkdir(xdg_root, 0700), 0);
    TEST_CHECK(mkdir(xdg_yabai, 0700), 0);
    TEST_CHECK(mkdir(home_config, 0700), 0);
    TEST_CHECK(mkdir(home_yabai, 0700), 0);
    TEST_CHECK(setenv("HOME", root, 1), 0);

    char paths[3][PATH_MAX];
    snprintf(paths[0], sizeof(paths[0]), "%s/xdg/yabai/config", root);
    snprintf(paths[1], sizeof(paths[1]), "%s/.config/yabai/config", root);
    snprintf(paths[2], sizeof(paths[2]), "%s/.settings", root);
    const char *names[] = { "config", "config", "settings" };
    const char *long_names[] = { "config-suffix", "config-suffix", "settings-suffix" };

    for (int i = 0; i < 3; ++i) {
        if (i == 0) TEST_CHECK(setenv("XDG_CONFIG_HOME", xdg_root, 1), 0);
        else TEST_CHECK(unsetenv("XDG_CONFIG_HOME"), 0);
        FILE *file = fopen(paths[i], "w");
        TEST_CHECK(file != NULL, true);
        if (file) TEST_CHECK(fclose(file), 0);

        char buffer[PATH_MAX];
        int size = (int) strlen(paths[i]) + 1;
        TEST_CHECK(get_config_file(names[i], buffer, size), true);
        TEST_CHECK(strcmp(buffer, paths[i]), 0);
        // The longer name truncates to an existing, but different, file.
        TEST_CHECK(get_config_file(long_names[i], buffer, size), false);
        TEST_CHECK(buffer[0], '\0');
        TEST_CHECK(unlink(paths[i]), 0);
    }

    // An overflowing XDG/standard candidate must not prevent a shorter fallback.
    FILE *fallback = fopen(paths[2], "w");
    TEST_CHECK(fallback != NULL, true);
    if (fallback) TEST_CHECK(fclose(fallback), 0);
    TEST_CHECK(setenv("XDG_CONFIG_HOME", xdg_root, 1), 0);
    char buffer[PATH_MAX];
    TEST_CHECK(get_config_file("settings", buffer, (int) strlen(paths[2]) + 1), true);
    TEST_CHECK(strcmp(buffer, paths[2]), 0);
    TEST_CHECK(unlink(paths[2]), 0);

    TEST_CHECK(unsetenv("HOME"), 0);
    TEST_CHECK(get_config_file("missing", buffer, sizeof(buffer)), false);
    TEST_CHECK(buffer[0], '\0');
    TEST_CHECK(unsetenv("XDG_CONFIG_HOME"), 0);
    TEST_CHECK(get_config_file("missing", buffer, sizeof(buffer)), false);
    TEST_CHECK(buffer[0], '\0');
    TEST_CHECK(get_config_file(NULL, buffer, sizeof(buffer)), false);
    TEST_CHECK(get_config_file("config", NULL, 1), false);
    char sentinel = 'x';
    TEST_CHECK(get_config_file("config", &sentinel, 0), false);
    TEST_CHECK(get_config_file("config", &sentinel, -1), false);
    TEST_CHECK(sentinel, 'x');

    if (saved_home) { setenv("HOME", saved_home, 1); free(saved_home); }
    else unsetenv("HOME");
    if (saved_xdg) { setenv("XDG_CONFIG_HOME", saved_xdg, 1); free(saved_xdg); }
    else unsetenv("XDG_CONFIG_HOME");
    TEST_CHECK(rmdir(home_yabai), 0);
    TEST_CHECK(rmdir(home_config), 0);
    TEST_CHECK(rmdir(xdg_yabai), 0);
    TEST_CHECK(rmdir(xdg_root), 0);
    TEST_CHECK(rmdir(root), 0);
});
