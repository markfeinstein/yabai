#define SA_SOCKET_PATH_FMT      "/tmp/yabai-sa_%s.socket"
#define SOCKET_PATH_FMT         "/tmp/yabai_%s.socket"
#define LCFILE_PATH_FMT         "/tmp/yabai_%s.lock"

#define SCRPT_ADD_LOAD_OPT      "--load-sa"
#define SCRPT_ADD_UNINSTALL_OPT "--uninstall-sa"
#define SERVICE_INSTALL_OPT     "--install-service"
#define SERVICE_UNINSTALL_OPT   "--uninstall-service"
#define SERVICE_START_OPT       "--start-service"
#define SERVICE_RESTART_OPT     "--restart-service"
#define SERVICE_STOP_OPT        "--stop-service"
#define CLIENT_OPT_LONG         "--message"
#define CLIENT_OPT_SHRT         "-m"
#define CONFIG_OPT_LONG         "--config"
#define CONFIG_OPT_SHRT         "-c"
#define NATIVE_CONFIG_OPT_LONG  "--config-jsonc"
#define DEBUG_VERBOSE_OPT_LONG  "--verbose"
#define DEBUG_VERBOSE_OPT_SHRT  "-V"
#define VERSION_OPT_LONG        "--version"
#define VERSION_OPT_SHRT        "-v"
#define HELP_OPT_LONG           "--help"
#define HELP_OPT_SHRT           "-h"
#define DOCTOR_OPT_LONG         "--doctor"

#define CLIENT_RESPONSE_INACTIVITY_TIMEOUT_MS 30000

#define MAJOR  7
#define MINOR  1
#define PATCH 25

struct signal *g_signal_event[SIGNAL_TYPE_COUNT];
struct process_manager g_process_manager;
struct display_manager g_display_manager;
struct window_manager g_window_manager;
struct space_manager g_space_manager;
struct memory_pool g_signal_storage;
struct mouse_state g_mouse_state;
struct event_loop g_event_loop;
void *g_workspace_context;

enum mission_control_mode g_mission_control_mode;
double g_cv_host_clock_frequency;
int g_layer_normal_window_level;
int g_layer_below_window_level;
int g_layer_above_window_level;
uint8_t *g_event_bytes;

char g_sa_socket_file[MAXLEN];
char g_socket_file[MAXLEN];
char g_config_file[4096];
char g_native_config_file[PATH_MAX];
char g_lock_file[MAXLEN];
struct declarative_startup_state g_declarative_startup;
static bool g_native_config_explicit;

mach_port_t g_bs_port;
int g_connection;
bool g_verbose;
pid_t g_pid;

static int client_read_response_fd(int sockfd, FILE *standard_output, FILE *standard_error,
                                   uint32_t inactivity_timeout_ms)
{
    int result = EXIT_SUCCESS;
    bool first_byte = true;
    FILE *output = standard_output;
    char rsp[BUFSIZ];
    uint64_t deadline = socket_io_deadline_after_ms(inactivity_timeout_ms);

    for (;;) {
        size_t bytes_read;
        enum socket_io_read_result read_result = socket_io_read_some_until(sockfd, rsp, sizeof(rsp),
                                                                           deadline, &bytes_read);
        if (read_result == SOCKET_IO_READ_EOF) return result;
        if (read_result != SOCKET_IO_READ_DATA) return EXIT_FAILURE;

        deadline = socket_io_deadline_after_ms(inactivity_timeout_ms);

        size_t offset = 0;
        if (first_byte) {
            first_byte = false;
            if (rsp[0] == FAILURE_MESSAGE[0]) {
                result = EXIT_FAILURE;
                output = standard_error;
                offset = 1;
            }
        }

        size_t output_length = bytes_read - offset;
        if ((output_length && fwrite(rsp + offset, 1, output_length, output) != output_length) ||
            fflush(output) != 0) {
            return EXIT_FAILURE;
        }
    }
}

static int client_send_message(int argc, char **argv)
{
    if (argc <= 1) {
        error("yabai-msg: no arguments given! abort..\n");
    }

    char *user = getenv("USER");
    if (!user) {
        error("yabai-msg: 'env USER' not set! abort..\n");
    }

    int message_length = argc;
    int argl[argc];

    for (int i = 1; i < argc; ++i) {
        argl[i] = strlen(argv[i]);
        message_length += argl[i];
    }

    char *message = malloc(sizeof(int)+message_length);
    char *temp = sizeof(int)+message;

    memcpy(message, &message_length, sizeof(int));
    for (int i = 1; i < argc; ++i) {
        memcpy(temp, argv[i], argl[i]);
        temp += argl[i];
        *temp++ = '\0';
    }
    *temp++ = '\0';

    int sockfd;
    char socket_file[MAXLEN];
    snprintf(socket_file, sizeof(socket_file), SOCKET_PATH_FMT, user);

    if (!socket_open(&sockfd)) {
        error("yabai-msg: failed to open socket..\n");
    }

    if (!socket_connect(sockfd, socket_file)) {
        error("yabai-msg: failed to connect to socket..\n");
    }

    uint64_t deadline = socket_io_deadline_after_ms(SOCKET_IO_PRODUCTION_TIMEOUT_MS);
    if (!socket_io_write_all_until(sockfd, message, sizeof(int)+message_length, deadline)) {
        error("yabai-msg: failed to send data..\n");
    }

    shutdown(sockfd, SHUT_WR);
    free(message);

    int result = client_read_response_fd(sockfd, stdout, stderr,
                                         CLIENT_RESPONSE_INACTIVITY_TIMEOUT_MS);
    socket_close(sockfd);
    return result;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static inline bool configure_settings_and_acquire_lock(void)
{
    char *user = getenv("USER");
    if (!user) {
        error("yabai: 'env USER' not set! abort..\n");
    }

    snprintf(g_sa_socket_file, sizeof(g_sa_socket_file), SA_SOCKET_PATH_FMT, user);
    snprintf(g_socket_file, sizeof(g_socket_file), SOCKET_PATH_FMT, user);
    snprintf(g_lock_file, sizeof(g_lock_file), LCFILE_PATH_FMT, user);

    NSApplicationLoad();
    g_pid = getpid();
    g_event_bytes = malloc(0x100);
    memset(g_event_bytes, 0, 0x100);
    g_connection = SLSMainConnectionID();
    g_cv_host_clock_frequency   = CVGetHostClockFrequency();
    g_layer_normal_window_level = CGWindowLevelForKey(LAYER_NORMAL);
    g_layer_below_window_level  = CGWindowLevelForKey(LAYER_BELOW);
    g_layer_above_window_level  = CGWindowLevelForKey(LAYER_ABOVE);
    CGSGetConnectionPortById    = macho_find_symbol("/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight", "_CGSGetConnectionPortById");
    SLSPerformAsynchronousBridgedWindowManagementOperation = macho_find_symbol("/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight", "__ZL54SLSPerformAsynchronousBridgedWindowManagementOperationP47SLSAsynchronousBridgedWindowManagementOperation");

    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    CGSetLocalEventsSuppressionInterval(0.0f);
    CGEnableEventStateCombining(false);
    mouse_state_init(&g_mouse_state);
    task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &g_bs_port);

#if 0
    hook_nsobject_autorelease();
    hook_autoreleasepool_drain();
    hook_autoreleasepool_release();
#endif

    int handle = open(g_lock_file, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    if (handle == -1) {
        error("yabai: could not create lock-file! abort..\n");
    }

    struct flock lockfd = {
        .l_start  = 0,
        .l_len    = 0,
        .l_pid    = g_pid,
        .l_type   = F_WRLCK,
        .l_whence = SEEK_SET
    };

    return fcntl(handle, F_SETLK, &lockfd) != -1;
}
#pragma clang diagnostic pop

static void print_declarative_diagnostic(const struct declarative_error *diagnostic)
{
    declarative_report_error(stderr, diagnostic);
}

static bool capture_invocation_path(const char *path, char output[PATH_MAX], struct declarative_error *diagnostic)
{
    char cwd[PATH_MAX];
    if (path && path[0] == '/') return declarative_startup_capture_path(path, NULL, output, diagnostic);
    if (!getcwd(cwd, sizeof(cwd))) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->stage = DECLARATIVE_ERROR_PATH;
        snprintf(diagnostic->json_path, sizeof(diagnostic->json_path), "%s", "$");
        snprintf(diagnostic->message, sizeof(diagnostic->message), "could not capture current directory");
        return false;
    }
    return declarative_startup_capture_path(path, cwd, output, diagnostic);
}

static void parse_arguments(int argc, char **argv)
{
    enum declarative_primary_action primary_action = declarative_primary_action_classify(argc, argv);

    if (primary_action == DECLARATIVE_PRIMARY_INVALID_CHECK) {
        struct declarative_error diagnostic = { .stage = DECLARATIVE_ERROR_PATH };
        snprintf(diagnostic.json_path, sizeof(diagnostic.json_path), "%s", "$");
        snprintf(diagnostic.message, sizeof(diagnostic.message), "--check-config requires exactly one path and cannot be combined with other arguments");
        print_declarative_diagnostic(&diagnostic);
        exit(EXIT_FAILURE);
    }

    if ((string_equals(argv[1], HELP_OPT_LONG)) ||
        (string_equals(argv[1], HELP_OPT_SHRT))) {
        fprintf(stdout, "Usage: yabai [option]\n"
                        "Options:\n"
                        "    --load-sa              Install and load the scripting-addition.\n"
                        "    --uninstall-sa         Uninstall the scripting-addition.\n"
                        "    --install-service      Write launchd service file to disk.\n"
                        "    --uninstall-service    Remove launchd service file from disk.\n"
                        "    --start-service        Enable, load, and start the launchd service.\n"
                        "    --restart-service      Attempts to restart the service instance.\n"
                        "    --stop-service         Stops a running instance of the service.\n"
                        "    --message, -m <msg>    Send message to a running instance of yabai.\n"
                        "    --config, -c <config>  Use the specified configuration file.\n"
                        "    --config-jsonc <config> Use the specified native JSONC configuration file.\n"
                        "    --check-config <config> Validate JSONC schema only; exit 0 if valid, nonzero otherwise.\n"
                        "    --doctor               Run read-only installation and compatibility diagnostics.\n"
                        "    --verbose, -V          Output debug information to stdout.\n"
                        "    --version, -v          Print version to stdout and exit.\n"
                        "    --help, -h             Print options to stdout and exit.\n"
                        "Type `man yabai` for more information, or visit: "
                        "https://github.com/asmvik/yabai/blob/v%d.%d.%d/doc/yabai.asciidoc\n", MAJOR, MINOR, PATCH);
        exit(EXIT_SUCCESS);
    }

    if ((string_equals(argv[1], VERSION_OPT_LONG)) ||
        (string_equals(argv[1], VERSION_OPT_SHRT))) {
        fprintf(stdout, "yabai-v%d.%d.%d\n", MAJOR, MINOR, PATCH);
        exit(EXIT_SUCCESS);
    }

    if (string_equals(argv[1], DOCTOR_OPT_LONG)) {
        if (argc != 2) error("yabai: option '%s' does not accept arguments!\n", DOCTOR_OPT_LONG);
        exit(doctor_run());
    }

    if ((string_equals(argv[1], CLIENT_OPT_LONG)) ||
        (string_equals(argv[1], CLIENT_OPT_SHRT))) {
        exit(client_send_message(argc-1, argv+1));
    }

    if (string_equals(argv[1], SCRPT_ADD_UNINSTALL_OPT)) {
        exit(scripting_addition_uninstall());
    }

    if (string_equals(argv[1], SCRPT_ADD_LOAD_OPT)) {
        exit(scripting_addition_load());
    }

    if (string_equals(argv[1], SERVICE_INSTALL_OPT)) {
        exit(service_install());
    }

    if (string_equals(argv[1], SERVICE_UNINSTALL_OPT)) {
        exit(service_uninstall());
    }

    if (string_equals(argv[1], SERVICE_START_OPT)) {
        exit(service_start());
    }

    if (string_equals(argv[1], SERVICE_RESTART_OPT)) {
        exit(service_restart());
    }

    if (string_equals(argv[1], SERVICE_STOP_OPT)) {
        exit(service_stop());
    }

    if (primary_action == DECLARATIVE_PRIMARY_CHECK) {
        char path[PATH_MAX];
        struct declarative_error diagnostic = {0};
        struct declarative_config_model *model = NULL;
        if (!capture_invocation_path(argv[2], path, &diagnostic) ||
            !declarative_config_parse_file(path, &model, &diagnostic)) {
            print_declarative_diagnostic(&diagnostic);
            declarative_config_destroy(model);
            exit(EXIT_FAILURE);
        }
        declarative_config_destroy(model);
        fputs("configuration is valid; runtime preparation was not checked\n", stdout);
        exit(EXIT_SUCCESS);
    }

    for (int i = 1; i < argc; ++i) {
        char *opt = argv[i];

        if ((string_equals(opt, DEBUG_VERBOSE_OPT_LONG)) ||
            (string_equals(opt, DEBUG_VERBOSE_OPT_SHRT))) {
            g_verbose = true;
        } else if ((string_equals(opt, CONFIG_OPT_LONG)) ||
                   (string_equals(opt, CONFIG_OPT_SHRT))) {
            char *val = i < argc - 1 ? argv[++i] : NULL;
            if (!val) error("yabai: option '%s|%s' requires an argument!\n", CONFIG_OPT_LONG, CONFIG_OPT_SHRT);
            snprintf(g_config_file, sizeof(g_config_file), "%s", val);
        } else if (string_equals(opt, NATIVE_CONFIG_OPT_LONG)) {
            char *val = i < argc - 1 ? argv[++i] : NULL;
            if (!val) error("yabai: option '%s' requires an argument!\n", NATIVE_CONFIG_OPT_LONG);
            if (g_native_config_explicit) error("yabai: option '%s' may only be specified once!\n", NATIVE_CONFIG_OPT_LONG);
            struct declarative_error diagnostic = {0};
            if (!capture_invocation_path(val, g_native_config_file, &diagnostic)) {
                print_declarative_diagnostic(&diagnostic);
                exit(EXIT_FAILURE);
            }
            g_native_config_explicit = true;
        } else {
            error("yabai: '%s' is not a valid option!\n", opt);
        }
    }
}

static bool declarative_startup_bootstrap(void *context)
{
    return declarative_startup_install(context, NULL);
}

#ifdef TESTS
static struct declarative_startup_completion_ops declarative_startup_production_test_overrides;
#endif

static bool menu_bar_startup_staging_seal_on_event_loop(void *context)
{
    (void) context;
    return menu_bar_manager_startup_staging_seal();
}

static void declarative_startup_inventory(void *context)
{
    (void) context;
#ifdef TESTS
    if (declarative_startup_production_test_overrides.inventory) {
        declarative_startup_production_test_overrides.inventory(declarative_startup_production_test_overrides.context);
        return;
    }
#endif
    window_manager_begin(&g_space_manager, &g_window_manager);
}

static bool declarative_startup_rebuild_notifications(void *context)
{
    (void) context;
#ifdef TESTS
    if (declarative_startup_production_test_overrides.rebuild_notifications) {
        return declarative_startup_production_test_overrides.rebuild_notifications(
            declarative_startup_production_test_overrides.context);
    }
#endif
    return update_window_notifications();
}

static bool declarative_startup_seal_menu(void *context)
{
    (void) context;
#ifdef TESTS
    if (declarative_startup_production_test_overrides.seal_menu_staging) {
        return declarative_startup_production_test_overrides.seal_menu_staging(
            declarative_startup_production_test_overrides.context);
    }
#endif
    return event_loop_perform_sync(&g_event_loop, menu_bar_startup_staging_seal_on_event_loop, NULL);
}

static bool declarative_startup_start_socket(void *context)
{
    (void) context;
#ifdef TESTS
    if (declarative_startup_production_test_overrides.start_socket) {
        return declarative_startup_production_test_overrides.start_socket(
            declarative_startup_production_test_overrides.context);
    }
#endif
    return message_loop_begin(g_socket_file);
}

static void declarative_startup_execute_legacy_config(void *context)
{
    (void) context;
#ifdef TESTS
    if (declarative_startup_production_test_overrides.execute_legacy_config) {
        declarative_startup_production_test_overrides.execute_legacy_config(
            declarative_startup_production_test_overrides.context);
        return;
    }
#endif
    exec_config_file(g_config_file, sizeof(g_config_file));
}

static struct declarative_startup_completion_ops declarative_startup_production_completion_ops(void)
{
    return (struct declarative_startup_completion_ops) {
        .inventory = declarative_startup_inventory,
        .rebuild_notifications = declarative_startup_rebuild_notifications,
        .seal_menu_staging = declarative_startup_seal_menu,
        .start_socket = declarative_startup_start_socket,
        .execute_legacy_config = declarative_startup_execute_legacy_config,
        .notifications_required = workspace_is_macos_sequoia() || workspace_is_macos_tahoe()
    };
}

#ifndef TESTS
int main(int argc, char **argv)
{
    if (argc > 1) {
        parse_arguments(argc, argv);
    }

    struct declarative_error native_diagnostic = {0};
    if (!declarative_startup_select(&g_declarative_startup,
                                    g_native_config_explicit ? g_native_config_file : NULL,
                                    getenv("XDG_CONFIG_HOME"), getenv("HOME"),
                                    &native_diagnostic)) {
        print_declarative_diagnostic(&native_diagnostic);
        declarative_startup_destroy(&g_declarative_startup);
        return EXIT_FAILURE;
    }

    if (is_root()) {
        require("yabai: running as root is not allowed! abort..\n");
    }

    if (!ax_privilege()) {
        require("yabai: could not access accessibility features! abort..\n");
    }

    if (!(SLSGetSpaceManagementMode(SLSMainConnectionID()) == 1)) {
        require("yabai: 'display has separate spaces' is disabled! abort..\n");
    }

    if (!ts_init(MEGABYTES(8))) {
        error("yabai: could not allocate temporary storage! abort..\n");
    }

    if (!memory_pool_init(&g_signal_storage, KILOBYTES(256))) {
        error("yabai: could not allocate event signal storage! abort..\n");
    }

    if (!configure_settings_and_acquire_lock()) {
        error("yabai: could not acquire lock-file! abort..\n");
    }

    if (!menu_bar_manager_init()) {
        error("yabai: could not initialize menu bar manager! abort..\n");
    }
    menu_bar_manager_startup_staging_open();

    if (!event_loop_begin_bootstrap(&g_event_loop, declarative_startup_bootstrap, &g_declarative_startup)) {
        menu_bar_manager_startup_staging_abort();
        menu_bar_manager_shutdown();
        declarative_report_runtime(stderr, "runtime", g_declarative_startup.path, "$", 0, 0,
                                   "startup runtime preparation failed");
        declarative_startup_destroy(&g_declarative_startup);
        return EXIT_FAILURE;
    }

    if (!workspace_event_handler_begin(&g_workspace_context)) {
        error("yabai: could not start workspace context! abort..\n");
    }

    if (!process_manager_begin(&g_process_manager)) {
        error("yabai: could not start process manager! abort..\n");
    }

    bool native_installed = declarative_installed_generation() != 0;
    if (!(native_installed ? display_manager_observe() : display_manager_begin(&g_display_manager))) {
        error("yabai: could not start display manager! abort..\n");
    }

    if (!mouse_handler_begin(&g_mouse_state, MOUSE_EVENT_MASK_FFM)) {
        error("yabai: could not start mouse handler! abort..\n");
    }

    if (workspace_is_macos_monterey() ||
        workspace_is_macos_ventura() ||
        workspace_is_macos_sonoma() ||
        workspace_is_macos_sequoia() ||
        workspace_is_macos_tahoe()) {
        mission_control_observe();

        if (workspace_is_macos_ventura() ||
            workspace_is_macos_sonoma() ||
            workspace_is_macos_sequoia() ||
            workspace_is_macos_tahoe()) {
            SLSRegisterConnectionNotifyProc(g_connection, connection_handler, 1327, NULL);
            SLSRegisterConnectionNotifyProc(g_connection, connection_handler, 1328, NULL);
        }
    } else {
        SLSRegisterConnectionNotifyProc(g_connection, connection_handler, 1204, NULL);
    }

    SLSRegisterConnectionNotifyProc(g_connection, connection_handler, 808, NULL);
    SLSRegisterConnectionNotifyProc(g_connection, connection_handler, 1202, NULL);

    if (workspace_is_macos_sequoia() || workspace_is_macos_tahoe()) {
        SLSRegisterConnectionNotifyProc(g_connection, connection_handler, 804, NULL);

        // NOTE: Only delivered for windows subscribed to, which is every window from Sequoia on.
        SLSRegisterConnectionNotifyProc(g_connection, connection_handler, 815, NULL);
        SLSRegisterConnectionNotifyProc(g_connection, connection_handler, 816, NULL);
    }

    if (!native_installed) {
        window_manager_init(&g_window_manager);
        space_manager_begin(&g_space_manager);
    }

    struct declarative_startup_completion_ops completion = declarative_startup_production_completion_ops();
    switch (declarative_startup_complete(&completion)) {
    case DECLARATIVE_STARTUP_COMPLETION_OK: break;
    case DECLARATIVE_STARTUP_COMPLETION_NOTIFICATION_FAILED:
        error("yabai: could not rebuild window notifications! abort..\n");
    case DECLARATIVE_STARTUP_COMPLETION_MENU_FAILED:
        error("yabai: could not seal startup menu bar staging! abort..\n");
    case DECLARATIVE_STARTUP_COMPLETION_SOCKET_FAILED:
        error("yabai: could not start message loop! abort..\n");
    case DECLARATIVE_STARTUP_COMPLETION_INVALID:
        error("yabai: invalid startup completion contract! abort..\n");
    }

    [NSApp run];

    return 0;
}
#endif

PROFILER_END_TRANSLATION_UNIT
