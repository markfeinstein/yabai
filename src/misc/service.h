#ifndef SERVICE_H
#define SERVICE_H

#include <limits.h>

#define _PATH_LAUNCHCTL   "/bin/launchctl"
#define _NAME_YABAI_PLIST "com.asmvik.yabai"
#define _PATH_YABAI_PLIST "%s/Library/LaunchAgents/"_NAME_YABAI_PLIST".plist"

#define _YABAI_PLIST \
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n" \
    "<plist version=\"1.0\">\n" \
    "<dict>\n" \
    "    <key>Label</key>\n" \
    "    <string>"_NAME_YABAI_PLIST"</string>\n" \
    "    <key>ProgramArguments</key>\n" \
    "    <array>\n" \
    "        <string>%s</string>\n" \
    "    </array>\n" \
    "    <key>EnvironmentVariables</key>\n" \
    "    <dict>\n" \
    "        <key>PATH</key>\n" \
    "        <string>%s</string>\n" \
    "    </dict>\n" \
    "    <key>RunAtLoad</key>\n" \
    "    <true/>\n" \
    "    <key>KeepAlive</key>\n" \
    "    <dict>\n" \
    "        <key>SuccessfulExit</key>\n" \
    " 	     <false/>\n" \
    " 	     <key>Crashed</key>\n" \
    " 	     <true/>\n" \
    "    </dict>\n" \
    "    <key>StandardOutPath</key>\n" \
    "    <string>/tmp/yabai_%s.out.log</string>\n" \
    "    <key>StandardErrorPath</key>\n" \
    "    <string>/tmp/yabai_%s.err.log</string>\n" \
    "    <key>ProcessType</key>\n" \
    "    <string>Interactive</string>\n" \
    "    <key>Nice</key>\n" \
    "    <integer>-20</integer>\n" \
    "</dict>\n" \
    "</plist>"

static inline bool plist_xml_add_size(size_t *size, size_t increment)
{
    if (*size > SIZE_MAX - increment) return false;
    *size += increment;
    return true;
}

static bool plist_xml_string_is_valid(const char *s)
{
    if (!s) return false;

    size_t byte_length = strlen(s);
    if (byte_length > LONG_MAX) return false;

    CFStringRef string = CFStringCreateWithBytes(kCFAllocatorDefault,
                                                  (const UInt8 *) s,
                                                  (CFIndex) byte_length,
                                                  kCFStringEncodingUTF8,
                                                  false);
    if (!string) return false;

    bool result = true;
    CFIndex length = CFStringGetLength(string);
    for (CFIndex i = 0; i < length; ++i) {
        UniChar character = CFStringGetCharacterAtIndex(string, i);
        if (CFStringIsSurrogateHighCharacter(character)) {
            if (++i >= length) {
                result = false;
                break;
            }

            UniChar low_character = CFStringGetCharacterAtIndex(string, i);
            if (!CFStringIsSurrogateLowCharacter(low_character)) {
                result = false;
                break;
            }

            UTF32Char scalar = CFStringGetLongCharacterForSurrogatePair(character, low_character);
            if (scalar < 0x10000 || scalar > 0x10ffff) {
                result = false;
                break;
            }
        } else if (CFStringIsSurrogateLowCharacter(character) ||
                   !((character == 0x09) ||
                     (character == 0x0a) ||
                     (character == 0x0d) ||
                     (character >= 0x20 && character <= 0xd7ff) ||
                     (character >= 0xe000 && character <= 0xfffd))) {
            result = false;
            break;
        }
    }

    CFRelease(string);
    return result;
}

static char *plist_xml_escape(char *s)
{
    size_t size = 0;
    for (char *cursor = s; *cursor; ++cursor) {
        switch (*cursor) {
        case '&':  if (!plist_xml_add_size(&size, 5)) return NULL; break;
        case '<':  if (!plist_xml_add_size(&size, 4)) return NULL; break;
        case '>':  if (!plist_xml_add_size(&size, 4)) return NULL; break;
        case '"': if (!plist_xml_add_size(&size, 6)) return NULL; break;
        case '\'': if (!plist_xml_add_size(&size, 6)) return NULL; break;
        default:   if (!plist_xml_add_size(&size, 1)) return NULL; break;
        }
    }

    if (size == SIZE_MAX) return NULL;
    char *result = malloc(size + 1);
    if (!result) return NULL;

    for (char *dst = result, *cursor = s; *cursor; ++cursor) {
        if (*cursor == '&') {
            memcpy(dst, "&amp;", 5);
            dst += 5;
        } else if (*cursor == '<') {
            memcpy(dst, "&lt;", 4);
            dst += 4;
        } else if (*cursor == '>') {
            memcpy(dst, "&gt;", 4);
            dst += 4;
        } else if (*cursor == '"') {
            memcpy(dst, "&quot;", 6);
            dst += 6;
        } else if (*cursor == '\'') {
            memcpy(dst, "&apos;", 6);
            dst += 6;
        } else {
            *dst++ = *cursor;
        }
    }

    result[size] = '\0';
    return result;
}

//
// NOTE(asmvik): A launchd service has the following states:
//
//          1. Installed / Uninstalled
//          2. Active (Enable / Disable)
//          3. Bootstrapped (Load / Unload)
//          4. Running (Start / Stop)
//

static int safe_exec(char *const argv[], bool suppress_output)
{
    pid_t pid;
    posix_spawn_file_actions_t actions;
    int status = posix_spawn_file_actions_init(&actions);
    if (status != 0) return 1;

    if (suppress_output &&
        (posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY|O_APPEND, 0) != 0 ||
         posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY|O_APPEND, 0) != 0)) {
        posix_spawn_file_actions_destroy(&actions);
        return 1;
    }

    status = posix_spawn(&pid, argv[0], &actions, NULL, argv, NULL);
    posix_spawn_file_actions_destroy(&actions);
    if (status != 0) return 1;

    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);

    if (waited != pid || !WIFEXITED(status)) return 1;
    return WEXITSTATUS(status);
}

static char *populate_plist_path(void)
{
    CFStringRef home_ref = (__bridge CFStringRef) NSHomeDirectoryForUser(NULL);
    char *home = home_ref ? cfstring_copy(home_ref) : NULL;

    if (!home) {
        error("yabai: unable to retrieve home directory! abort..\n");
    }

    int size = strlen(_PATH_YABAI_PLIST)-2 + strlen(home) + 1;
    char *result = malloc(size);
    if (!result) {
        error("yabai: could not allocate memory for plist path! abort..\n");
    }

    memset(result, 0, size);
    snprintf(result, size, _PATH_YABAI_PLIST, home);

    return result;
}

static char *populate_plist(int *length)
{
    char *user = getenv("USER");
    if (!user) {
        error("yabai: 'env USER' not set! abort..\n");
    }

    char *path_env = getenv("PATH");
    if (!path_env) {
        error("yabai: 'env PATH' not set! abort..\n");
    }

    char exe_path[4096];
    unsigned int exe_path_size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &exe_path_size) < 0) {
        error("yabai: unable to retrieve path of executable! abort..\n");
    }

    if (!plist_xml_string_is_valid(exe_path)) {
        error("yabai: executable path is not valid XML 1.0 text! abort..\n");
    }
    if (!plist_xml_string_is_valid(path_env)) {
        error("yabai: 'env PATH' is not valid XML 1.0 text! abort..\n");
    }
    if (!plist_xml_string_is_valid(user)) {
        error("yabai: 'env USER' is not valid XML 1.0 text! abort..\n");
    }

    char *escaped_exe_path = plist_xml_escape(exe_path);
    char *escaped_path_env = plist_xml_escape(path_env);
    char *escaped_user     = plist_xml_escape(user);
    if (!escaped_exe_path || !escaped_path_env || !escaped_user) {
        free(escaped_exe_path);
        free(escaped_path_env);
        free(escaped_user);
        error("yabai: could not allocate memory for plist contents! abort..\n");
    }

    size_t size = strlen(_YABAI_PLIST)-8;
    if (!plist_xml_add_size(&size, strlen(escaped_exe_path)) ||
        !plist_xml_add_size(&size, strlen(escaped_path_env)) ||
        !plist_xml_add_size(&size, strlen(escaped_user)) ||
        !plist_xml_add_size(&size, strlen(escaped_user)) ||
        !plist_xml_add_size(&size, 1) ||
        size-1 > INT_MAX) {
        free(escaped_exe_path);
        free(escaped_path_env);
        free(escaped_user);
        error("yabai: plist contents exceed supported length! abort..\n");
    }

    char *result = malloc(size);
    if (!result) {
        free(escaped_exe_path);
        free(escaped_path_env);
        free(escaped_user);
        error("yabai: could not allocate memory for plist contents! abort..\n");
    }

    memset(result, 0, size);
    snprintf(result, size, _YABAI_PLIST, escaped_exe_path, escaped_path_env, escaped_user, escaped_user);
    *length = (int) size-1;

    free(escaped_exe_path);
    free(escaped_path_env);
    free(escaped_user);

    return result;
}

static inline void ensure_directory_exists(char *yabai_plist_path)
{
    //
    // NOTE(asmvik): Temporarily remove filename.
    // We know the filepath will contain a slash, as
    // it is controlled by us, so don't bother checking
    // the result..
    //

    char *last_slash = strrchr(yabai_plist_path, '/');
    *last_slash = '\0';

    if (!directory_exists(yabai_plist_path)) {
        mkdir(yabai_plist_path, 0755);
    }

    //
    // NOTE(asmvik): Restore original filename.
    //

    *last_slash = '/';
}

static int service_install_internal(char *yabai_plist_path)
{
    int result = 1;
    int yabai_plist_length;
    char *yabai_plist = populate_plist(&yabai_plist_length);
    ensure_directory_exists(yabai_plist_path);

    char temporary_path[PATH_MAX];
    int temporary_path_length = snprintf(temporary_path, sizeof(temporary_path), "%s.XXXXXX", yabai_plist_path);
    if (temporary_path_length < 0 || temporary_path_length >= (int) sizeof(temporary_path)) goto cleanup;

    int fd = mkstemp(temporary_path);
    if (fd == -1) goto cleanup;

    FILE *handle = fdopen(fd, "w");
    if (!handle) {
        close(fd);
        unlink(temporary_path);
        goto cleanup;
    }

    bool write_succeeded = fwrite(yabai_plist, 1, (size_t) yabai_plist_length, handle) == (size_t) yabai_plist_length;
    bool flush_succeeded = fflush(handle) == 0;
    bool mode_succeeded = fchmod(fd, 0644) == 0;
    bool sync_succeeded = fsync(fd) == 0;
    bool close_succeeded = fclose(handle) == 0;

    if (write_succeeded && flush_succeeded && mode_succeeded && sync_succeeded && close_succeeded &&
        rename(temporary_path, yabai_plist_path) == 0) {
        result = 0;
    } else {
        unlink(temporary_path);
    }

cleanup:
    free(yabai_plist);
    return result;
}

static int service_install(void)
{
    char *yabai_plist_path = populate_plist_path();

    if (file_exists(yabai_plist_path)) {
        error("yabai: service file '%s' is already installed! abort..\n", yabai_plist_path);
    }

    return service_install_internal(yabai_plist_path);
}

static int service_uninstall(void)
{
    char *yabai_plist_path = populate_plist_path();

    if (!file_exists(yabai_plist_path)) {
        error("yabai: service file '%s' is not installed! abort..\n", yabai_plist_path);
    }

    return unlink(yabai_plist_path) == 0 ? 0 : 1;
}

static int service_start(void)
{
    char *yabai_plist_path = populate_plist_path();
    if (!file_exists(yabai_plist_path)) {
        warn("yabai: service file '%s' is not installed! attempting installation..\n", yabai_plist_path);

        int result = service_install_internal(yabai_plist_path);
        if (result) {
            error("yabai: service file '%s' could not be installed! abort..\n", yabai_plist_path);
        }
    }

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_YABAI_PLIST);

    char domain_target[MAXLEN];
    snprintf(domain_target, sizeof(domain_target), "gui/%d", getuid());

    //
    // NOTE(asmvik): Check if service is bootstrapped
    //

    const char *const args[] = { _PATH_LAUNCHCTL, "print", service_target, NULL };
    int is_bootstrapped = safe_exec((char *const*)args, true);

    if (is_bootstrapped != 0) {

        //
        // NOTE(asmvik): Service is not bootstrapped and could be disabled.
        // There is no way to query if the service is disabled, and we cannot
        // bootstrap a disabled service. Try to enable the service. This will be
        // a no-op if the service is already enabled.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "enable", service_target, NULL };
        safe_exec((char *const*)args, false);

        //
        // NOTE(asmvik): Bootstrap service into the target domain.
        // This will also start the program **iff* RunAtLoad is set to true.
        //

        const char *const args2[] = { _PATH_LAUNCHCTL, "bootstrap", domain_target, yabai_plist_path, NULL };
        return safe_exec((char *const*)args2, false);
    } else {

        //
        // NOTE(asmvik): The service has already been bootstrapped.
        // Tell the bootstrapped service to launch immediately; it is an
        // error to bootstrap a service that has already been bootstrapped.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "kickstart", service_target, NULL };
        return safe_exec((char *const*)args, false);
    }
}

static int service_restart(void)
{
    char *yabai_plist_path = populate_plist_path();
    if (!file_exists(yabai_plist_path)) {
        error("yabai: service file '%s' is not installed! abort..\n", yabai_plist_path);
    }

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_YABAI_PLIST);

    const char *const args[] = { _PATH_LAUNCHCTL, "kickstart", "-k", service_target, NULL };
    return safe_exec((char *const*)args, false);
}

static int service_stop(void)
{
    char *yabai_plist_path = populate_plist_path();
    if (!file_exists(yabai_plist_path)) {
        error("yabai: service file '%s' is not installed! abort..\n", yabai_plist_path);
    }

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_YABAI_PLIST);

    char domain_target[MAXLEN];
    snprintf(domain_target, sizeof(domain_target), "gui/%d", getuid());

    //
    // NOTE(asmvik): Check if service is bootstrapped
    //

    const char *const args[] = { _PATH_LAUNCHCTL, "print", service_target, NULL };
    int is_bootstrapped = safe_exec((char *const*)args, true);

    if (is_bootstrapped != 0) {

        //
        // NOTE(asmvik): Service is not bootstrapped, but the program
        // could still be running an instance that was started **while the service
        // was bootstrapped**, so we tell it to stop said service.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "kill", "SIGTERM", service_target, NULL };
        return safe_exec((char *const*)args, false);
    } else {

        //
        // NOTE(asmvik): Service is bootstrapped; we stop a potentially
        // running instance of the program and unload the service, making it
        // not trigger automatically in the future.
        //
        // This is NOT the same as disabling the service, which will prevent
        // it from being boostrapped in the future (without explicitly re-enabling
        // it first).
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "bootout", domain_target, yabai_plist_path, NULL };
        int result = safe_exec((char *const*)args, false);
        if (result != 0) return result;

        const char *const args2[] = { _PATH_LAUNCHCTL, "disable", service_target, NULL };
        return safe_exec((char *const*)args2, false);
    }
}

#endif
