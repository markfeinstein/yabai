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

/* Run argv[0] and return its exit status, or 1 when it cannot be started,
 * is terminated by a signal, or its status cannot be collected. A bounded
 * call that exceeds its deadline returns SAFE_EXEC_TIMED_OUT instead.
 *
 * The running daemon ignores SIGCHLD so fire-and-forget signal commands are
 * reaped automatically, which makes a direct waitpid report ECHILD. Commands
 * therefore run under a short-lived helper with a private SIGCHLD disposition;
 * the helper returns the command status through a close-on-exec pipe. The
 * daemon is multithreaded, so only async-signal-safe calls run between fork
 * and exec or _exit.
 *
 * A nonzero timeout bounds the whole call. The helper kills the command when
 * SIGALRM fires; it observes exit with WNOWAIT and disarms the alarm before
 * reaping, so the signal can never reach a recycled PID. The caller also
 * stops waiting after a short grace period and kills its own unreaped helper,
 * which covers descendants that keep the output pipe open. */
#define SAFE_EXEC_TIMEOUT_GRACE_MS 2000
#define SAFE_EXEC_TIMED_OUT (-1)

struct safe_exec_request
{
    char *const *argv;
    bool discard_stdout;
    bool discard_stderr;
    char *output;
    size_t capacity;
    uint32_t timeout_ms;
};

static volatile pid_t safe_exec_helper_command_pid;
static volatile sig_atomic_t safe_exec_helper_alarm_fired;

static void safe_exec_helper_alarm(int signo)
{
    (void) signo;
    pid_t command = safe_exec_helper_command_pid;
    if (command > 0) {
        safe_exec_helper_alarm_fired = 1;
        kill(command, SIGKILL);
    }
}

static bool safe_exec_pipe(int fds[2])
{
    fds[0] = fds[1] = -1;
    if (pipe(fds) != 0) {
        fds[0] = fds[1] = -1;
        return false;
    }
    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) == -1 || fcntl(fds[1], F_SETFD, FD_CLOEXEC) == -1) {
        close(fds[0]);
        close(fds[1]);
        fds[0] = fds[1] = -1;
        return false;
    }
    return true;
}

static void safe_exec_close(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void safe_exec_write_all(int fd, const void *buffer, size_t length)
{
    const char *bytes = buffer;
    while (length) {
        ssize_t written = write(fd, bytes, length);
        if (written == -1 && errno == EINTR) continue;
        if (written <= 0) return;
        bytes += written;
        length -= (size_t) written;
    }
}


__attribute__((noreturn))
static void safe_exec_command_child(const struct safe_exec_request *request, int output_fd)
{
    if (output_fd >= 0 && dup2(output_fd, STDOUT_FILENO) == -1) _exit(127);
    if (request->discard_stdout || request->discard_stderr) {
        int null_fd = open("/dev/null", O_WRONLY|O_APPEND);
        if (null_fd == -1) _exit(127);
        if (request->discard_stdout && dup2(null_fd, STDOUT_FILENO) == -1) _exit(127);
        if (request->discard_stderr && dup2(null_fd, STDERR_FILENO) == -1) _exit(127);
        if (null_fd > STDERR_FILENO) close(null_fd);
    }
    extern char **environ;
    execve(request->argv[0], request->argv, environ);
    _exit(127);
}

__attribute__((noreturn))
static void safe_exec_helper_child(const struct safe_exec_request *request, int result_fd, int output_fd)
{
    struct sigaction action = { .sa_handler = SIG_DFL };
    sigemptyset(&action.sa_mask);
    sigaction(SIGCHLD, &action, NULL);

    sigset_t alarm_set;
    sigemptyset(&alarm_set);
    sigaddset(&alarm_set, SIGALRM);
    sigprocmask(SIG_BLOCK, &alarm_set, NULL);

    pid_t command = fork();
    if (command == 0) {
        sigprocmask(SIG_UNBLOCK, &alarm_set, NULL);
        safe_exec_command_child(request, output_fd);
    }
    if (output_fd >= 0) close(output_fd);

    int result = 1;
    if (command != -1) {
        if (request->timeout_ms) {
            safe_exec_helper_command_pid = command;
            struct sigaction alarm_action = { .sa_handler = safe_exec_helper_alarm };
            sigemptyset(&alarm_action.sa_mask);
            sigaction(SIGALRM, &alarm_action, NULL);
            alarm((request->timeout_ms + 999) / 1000);
            sigprocmask(SIG_UNBLOCK, &alarm_set, NULL);

            siginfo_t info;
            while (waitid(P_PID, (id_t) command, &info, WEXITED | WNOWAIT) == -1 && errno == EINTR) {}

            sigprocmask(SIG_BLOCK, &alarm_set, NULL);
            alarm(0);
            safe_exec_helper_command_pid = 0;
        }

        int status = 0;
        pid_t waited;
        do {
            waited = waitpid(command, &status, 0);
        } while (waited == -1 && errno == EINTR);
        if (waited == command && WIFEXITED(status)) result = WEXITSTATUS(status);
        if (safe_exec_helper_alarm_fired) result = SAFE_EXEC_TIMED_OUT;
    }

    safe_exec_write_all(result_fd, &result, sizeof(result));
    close(result_fd);
    _exit(0);
}

static int safe_exec_run(const struct safe_exec_request *request)
{
    if (!request || !request->argv || !request->argv[0]) return 1;
    bool capture = request->output && request->capacity;
    if (capture) request->output[0] = '\0';

    int result_pipe[2];
    int output_pipe[2] = { -1, -1 };
    if (!safe_exec_pipe(result_pipe)) return 1;
    if (capture && !safe_exec_pipe(output_pipe)) {
        safe_exec_close(&result_pipe[0]);
        safe_exec_close(&result_pipe[1]);
        return 1;
    }

    pid_t helper = fork();
    if (helper == 0) {
        close(result_pipe[0]);
        if (output_pipe[0] >= 0) close(output_pipe[0]);
        safe_exec_helper_child(request, result_pipe[1], output_pipe[1]);
    }

    safe_exec_close(&result_pipe[1]);
    safe_exec_close(&output_pipe[1]);
    if (helper == -1) {
        safe_exec_close(&result_pipe[0]);
        safe_exec_close(&output_pipe[0]);
        return 1;
    }

    uint64_t deadline = request->timeout_ms
                      ? socket_io_deadline_after_ms(request->timeout_ms + SAFE_EXEC_TIMEOUT_GRACE_MS)
                      : UINT64_MAX;
    int result = 1;
    size_t received = 0;
    size_t stored = 0;
    bool timed_out = false;
    while (result_pipe[0] >= 0 || output_pipe[0] >= 0) {
        struct pollfd fds[2];
        nfds_t count = 0;
        if (result_pipe[0] >= 0) fds[count++] = (struct pollfd) { .fd = result_pipe[0], .events = POLLIN };
        if (output_pipe[0] >= 0) fds[count++] = (struct pollfd) { .fd = output_pipe[0], .events = POLLIN };

        /* Once the status arrives the command has exited; drain only output
         * that is already buffered so an inheriting descendant cannot stall. */
        int timeout = -1;
        if (result_pipe[0] < 0) {
            timeout = 0;
        } else if (deadline != UINT64_MAX) {
            uint64_t now = socket_io_monotonic_ns();
            if (!now || now >= deadline) {
                timed_out = true;
                break;
            }
            uint64_t remaining_ms = (deadline - now + 999999ULL) / 1000000ULL;
            timeout = remaining_ms > INT_MAX ? INT_MAX : (int) remaining_ms;
        }

        int ready = poll(fds, count, timeout);
        if (ready == -1 && errno == EINTR) continue;
        if (ready <= 0) {
            if (result_pipe[0] >= 0) timed_out = true;
            break;
        }

        for (nfds_t i = 0; i < count; ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) continue;
            if (fds[i].fd == result_pipe[0]) {
                ssize_t bytes = read(result_pipe[0], (char *) &result + received, sizeof(result) - received);
                if (bytes == -1 && errno == EINTR) continue;
                if (bytes > 0) received += (size_t) bytes;
                if (bytes <= 0 || received == sizeof(result)) safe_exec_close(&result_pipe[0]);
            } else {
                char buffer[1024];
                ssize_t bytes = read(output_pipe[0], buffer, sizeof(buffer));
                if (bytes == -1 && errno == EINTR) continue;
                if (bytes <= 0) {
                    safe_exec_close(&output_pipe[0]);
                    continue;
                }
                if (!capture) continue;
                size_t available = request->capacity - 1 - stored;
                size_t copy = (size_t) bytes < available ? (size_t) bytes : available;
                memcpy(request->output + stored, buffer, copy);
                stored += copy;
            }
        }
    }
    if (capture) request->output[stored] = '\0';
    safe_exec_close(&result_pipe[0]);
    safe_exec_close(&output_pipe[0]);

    if (timed_out) kill(helper, SIGKILL);
    /* SIGCHLD is ignored by the daemon. Some macOS versions keep a helper
     * zombie visible while waitpid(..., 0) blocks waiting for its automatic
     * reap. Never let that cleanup freeze the caller after the status pipe
     * has closed. Poll for a bounded grace period instead. */
    uint64_t reap_deadline = socket_io_deadline_after_ms(SAFE_EXEC_TIMEOUT_GRACE_MS);
    int helper_status = 0;
    pid_t reaped;
    do {
        reaped = waitpid(helper, &helper_status, WNOHANG);
        if (reaped == helper || (reaped == -1 && errno == ECHILD)) break;
        if (reaped == -1 && errno != EINTR) break;
        if (socket_io_monotonic_ns() >= reap_deadline) {
            kill(helper, SIGKILL);
            break;
        }
        struct timespec pause = { .tv_nsec = 10000000L };
        nanosleep(&pause, NULL);
    } while (true);
    if (timed_out) return SAFE_EXEC_TIMED_OUT;
    return received == sizeof(result) ? result : 1;
}

static int safe_exec_capture_until(char *const argv[], bool suppress_stderr,
                                   char *output, size_t capacity, uint32_t timeout_ms)
{
    if (!output || capacity == 0) return 1;
    struct safe_exec_request request = {
        .argv = argv,
        .discard_stderr = suppress_stderr,
        .output = output,
        .capacity = capacity,
        .timeout_ms = timeout_ms
    };
    return safe_exec_run(&request);
}

static int safe_exec_until(char *const argv[], bool suppress_output, uint32_t timeout_ms)
{
    struct safe_exec_request request = {
        .argv = argv,
        .discard_stdout = suppress_output,
        .discard_stderr = suppress_output,
        .timeout_ms = timeout_ms
    };
    return safe_exec_run(&request);
}

static int safe_exec(char *const argv[], bool suppress_output)
{
    return safe_exec_until(argv, suppress_output, 0);
}

/* Menu actions run on the main thread; launchd must answer within this bound
 * or the action reports failure instead of freezing the menu bar. */
#define SERVICE_MENU_COMMAND_TIMEOUT_MS 5000

static int service_menu_exec(char *const argv[], bool suppress_output)
{
    return safe_exec_until(argv, suppress_output, SERVICE_MENU_COMMAND_TIMEOUT_MS);
}

static int service_menu_capture(char *const argv[], bool suppress_stderr, char *output, size_t capacity)
{
    return safe_exec_capture_until(argv, suppress_stderr, output, capacity,
                                   SERVICE_MENU_COMMAND_TIMEOUT_MS);
}

static char *populate_plist_path(void)
{
    CFStringRef home_ref = (__bridge CFStringRef) NSHomeDirectoryForUser(NULL);
    char *home = home_ref ? cfstring_copy(home_ref) : NULL;

    if (!home) {
        error("yabai: unable to retrieve home directory! abort..\n");
    }

    size_t size = strlen(_PATH_YABAI_PLIST)-2 + strlen(home) + 1;
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

typedef int service_exec_fn(char *const argv[], bool suppress_output);
typedef int service_capture_fn(char *const argv[], bool suppress_stderr, char *output, size_t capacity);
typedef int service_spawn_fn(char *const argv[]);

enum service_quit_result
{
    SERVICE_QUIT_DIRECT,
    SERVICE_QUIT_BOOTOUT_REQUESTED,
    SERVICE_QUIT_FAILED
};

static bool service_quit_label_is_managed(const char *label)
{
    return label && label[0] && !string_equals(label, "0") &&
           strncmp(label, "application.", 12) != 0;
}

/* launchctl print renders the job's own properties one tab deep; nested
 * dictionaries such as endpoints or spawned subprocesses are indented
 * further and must not be mistaken for the job's PID. */
static bool service_print_output_has_pid(const char *output, pid_t pid)
{
    if (!output || pid <= 0) return false;
    const char *cursor = output;
    while (*cursor) {
        if (cursor[0] == '\t' && strncmp(cursor + 1, "pid = ", 6) == 0) {
            const char *number = cursor + 7;
            errno = 0;
            char *end = NULL;
            long value = strtol(number, &end, 10);
            if (errno == 0 && end != number && value == (long) pid &&
                (*end == '\0' || *end == '\n' || *end == '\r')) return true;
        }
        cursor = strchr(cursor, '\n');
        if (!cursor) break;
        ++cursor;
    }
    return false;
}

enum service_job_ownership
{
    SERVICE_JOB_OWNED,
    SERVICE_JOB_NOT_OWNED,
    SERVICE_JOB_UNKNOWN
};

#define SERVICE_PRINT_CAPACITY (64 * 1024)

/* XPC_SERVICE_NAME is inherited environment and therefore only a candidate
 * label: the job must currently report this PID before it may be changed.
 * A job that launchd does not know, or that runs another PID, is not ours.
 * A timed-out query or output that filled the buffer is inconclusive, and
 * callers must not guess: quitting directly beneath a KeepAlive job would
 * be respawned, while changing someone else's job would be worse. */
static enum service_job_ownership service_job_ownership(const char *service_target, pid_t pid,
                                                        service_capture_fn capture_fn)
{
    char *description = malloc(SERVICE_PRINT_CAPACITY);
    if (!description) return SERVICE_JOB_UNKNOWN;

    const char *const print_args[] = { _PATH_LAUNCHCTL, "print", service_target, NULL };
    int result = capture_fn((char *const *) print_args, true, description, SERVICE_PRINT_CAPACITY);

    enum service_job_ownership ownership;
    if (result == SAFE_EXEC_TIMED_OUT) {
        ownership = SERVICE_JOB_UNKNOWN;
    } else if (result != 0) {
        ownership = SERVICE_JOB_NOT_OWNED;
    } else if (service_print_output_has_pid(description, pid)) {
        ownership = SERVICE_JOB_OWNED;
    } else if (strlen(description) >= SERVICE_PRINT_CAPACITY - 1) {
        ownership = SERVICE_JOB_UNKNOWN;
    } else {
        ownership = SERVICE_JOB_NOT_OWNED;
    }

    free(description);
    return ownership;
}

/* Build the gui-domain target for a candidate launchd label. Returns false
 * for labels that cannot belong to a managed job (unset, launchd's "0"
 * placeholder, or RunningBoard's per-launch application.* jobs). */
static bool service_managed_target(const char *label, uid_t uid, char *target, size_t capacity,
                                   bool *too_long)
{
    *too_long = false;
    if (!service_quit_label_is_managed(label)) return false;
    int length = snprintf(target, capacity, "gui/%u/%s", (unsigned int) uid, label);
    if (length < 0 || (size_t) length >= capacity) {
        *too_long = true;
        return false;
    }
    return true;
}

/* Stop the launchd job that actually owns this process. Disabling before
 * bootout prevents KeepAlive from racing the requested termination with a
 * replacement; restore the enabled state if bootout is rejected. */
static enum service_quit_result service_stop_for_quit_with_exec(const char *label, uid_t uid,
                                                                pid_t pid, service_exec_fn exec_fn,
                                                                service_capture_fn capture_fn)
{
    char service_target[MAXLEN];
    bool too_long;
    if (!service_managed_target(label, uid, service_target, sizeof(service_target), &too_long)) {
        return too_long ? SERVICE_QUIT_FAILED : SERVICE_QUIT_DIRECT;
    }

    switch (service_job_ownership(service_target, pid, capture_fn)) {
    case SERVICE_JOB_OWNED: break;
    case SERVICE_JOB_NOT_OWNED: return SERVICE_QUIT_DIRECT;
    case SERVICE_JOB_UNKNOWN: return SERVICE_QUIT_FAILED;
    }

    const char *const disable_args[] = { _PATH_LAUNCHCTL, "disable", service_target, NULL };
    if (exec_fn((char *const *) disable_args, false) != 0) return SERVICE_QUIT_FAILED;

    const char *const bootout_args[] = { _PATH_LAUNCHCTL, "bootout", service_target, NULL };
    if (exec_fn((char *const *) bootout_args, false) == 0) return SERVICE_QUIT_BOOTOUT_REQUESTED;

    const char *const enable_args[] = { _PATH_LAUNCHCTL, "enable", service_target, NULL };
    (void) exec_fn((char *const *) enable_args, false);
    return SERVICE_QUIT_FAILED;
}

static enum service_quit_result service_stop_for_quit(void)
{
    return service_stop_for_quit_with_exec(getenv("XPC_SERVICE_NAME"), getuid(), getpid(),
                                           service_menu_exec, service_menu_capture);
}

enum service_restart_result
{
    SERVICE_RESTART_REQUESTED,
    SERVICE_RESTART_UNMANAGED,
    SERVICE_RESTART_FAILED
};

static int service_spawn(char *const argv[])
{
    pid_t pid = 0;
    extern char **environ;
    return posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);
}

static enum service_restart_result service_restart_for_menu_with_ops(const char *label, uid_t uid,
                                                                     pid_t pid,
                                                                     service_capture_fn capture_fn,
                                                                     service_spawn_fn spawn_fn)
{
    char service_target[MAXLEN];
    bool too_long;
    if (!service_managed_target(label, uid, service_target, sizeof(service_target), &too_long)) {
        return too_long ? SERVICE_RESTART_FAILED : SERVICE_RESTART_UNMANAGED;
    }

    switch (service_job_ownership(service_target, pid, capture_fn)) {
    case SERVICE_JOB_OWNED: break;
    case SERVICE_JOB_NOT_OWNED: return SERVICE_RESTART_UNMANAGED;
    case SERVICE_JOB_UNKNOWN: return SERVICE_RESTART_FAILED;
    }

    const char *const restart_args[] = { _PATH_LAUNCHCTL, "kickstart", "-k", service_target, NULL };
    return spawn_fn((char *const *) restart_args) == 0
         ? SERVICE_RESTART_REQUESTED
         : SERVICE_RESTART_FAILED;
}

static enum service_restart_result service_restart_for_menu(void)
{
    return service_restart_for_menu_with_ops(getenv("XPC_SERVICE_NAME"), getuid(), getpid(),
                                             service_menu_capture, service_spawn);
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
