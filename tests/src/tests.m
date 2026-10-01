#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>

unsigned char __src_osax_payload[1];
unsigned int __src_osax_payload_len;
unsigned char __src_osax_loader[1];
unsigned int __src_osax_loader_len;

static bool g_event_queue_test_fail_allocation;
static void *event_queue_test_allocate(size_t size)
{
    return g_event_queue_test_fail_allocation ? NULL : malloc(size);
}

#define EVENT_QUEUE_ALLOCATE(size) event_queue_test_allocate(size)
#include "../../src/manifest.m"
#include "../../src/osax/message_validation.h"
#include "../../src/sa_message.h"
#undef EVENT_QUEUE_ALLOCATE

#define TEST_SIG(name) bool test_##name(void)
typedef TEST_SIG(function);

#define TEST_FUNC(name, ...) static TEST_SIG(name) { char *test_name = #name; bool result = true; {__VA_ARGS__} return result; }
#define TEST_CHECK(r, e) do { \
    __auto_type test_actual = (r); \
    __auto_type test_expected = (e); \
    if (test_actual != test_expected) { \
        printf("                   \e[1;33m%s\e[m\e[1;31m#%d %s == %s\e[m\n", test_name, __LINE__, #r, #e); \
        result = false; \
    } \
} while (0)

#include "area.c"
#include "window.c"
#include "event_loop.c"
#include "space_gesture.c"
#include "window_ordered.c"
#include "event_queue.c"
#include "menu_bar_model.c"
#include "menu_bar_manager.c"
#include "message.c"
#include "message_frame.c"
#include "sa_message.c"
#undef TEST_ENTRY

static bool test_temp_storage_ensure(uint64_t size)
{
    if (g_temp_storage.memory && g_temp_storage.memory != MAP_FAILED) {
        return g_temp_storage.size >= size;
    }

    memset(&g_temp_storage, 0, sizeof(g_temp_storage));
    if (!ts_init(size)) {
        memset(&g_temp_storage, 0, sizeof(g_temp_storage));
        return false;
    }

    return true;
}

static void test_table_free_and_zero(struct table *table)
{
    if (table->buckets) table_free(table);
    memset(table, 0, sizeof(*table));
}

static void test_suite_teardown(void)
{
    test_table_free_and_zero(&g_window_manager.window);
    test_table_free_and_zero(&g_window_manager.managed_window);
    test_table_free_and_zero(&g_window_manager.insert_feedback);
    test_table_free_and_zero(&g_space_manager.view);

    if (g_temp_storage.memory && g_temp_storage.memory != MAP_FAILED) {
        munmap(g_temp_storage.memory, g_temp_storage.size + getpagesize());
    }
    memset(&g_temp_storage, 0, sizeof(g_temp_storage));
}

TEST_FUNC(add_and_clamp_to_zero_saturates,
{
    TEST_CHECK(add_and_clamp_to_zero(10, -3), 7);
    TEST_CHECK(add_and_clamp_to_zero(1, -2), 0);
    TEST_CHECK(add_and_clamp_to_zero(INT_MIN, INT_MIN), 0);
    TEST_CHECK(add_and_clamp_to_zero(INT_MAX, -1), INT_MAX - 1);
    TEST_CHECK(add_and_clamp_to_zero(INT_MAX, 1), INT_MAX);
});

static int g_event_loop_test_thread_create_calls;

static int event_loop_test_fail_thread_create(pthread_t *thread, const pthread_attr_t *attr,
                                              void *(*start_routine)(void *), void *context)
{
    (void) thread;
    (void) attr;
    (void) start_routine;
    (void) context;
    ++g_event_loop_test_thread_create_calls;
    return EAGAIN;
}

TEST_FUNC(event_loop_begin_thread_creation_failure_is_atomic,
{
    struct event_loop event_loop;
    memset(&event_loop, 0xa5, sizeof(event_loop));

    __auto_type saved_thread_create = event_loop_thread_create;
    g_event_loop_test_thread_create_calls = 0;
    event_loop_thread_create = event_loop_test_fail_thread_create;
    bool began = event_loop_begin(&event_loop);
    event_loop_thread_create = saved_thread_create;

    TEST_CHECK(began, false);
    TEST_CHECK(g_event_loop_test_thread_create_calls, 1);
    TEST_CHECK(event_loop.is_running, false);
    TEST_CHECK(event_loop.semaphore == NULL, true);
    TEST_CHECK(event_loop.queue.events == NULL, true);
    TEST_CHECK(event_loop.queue.capacity, 0U);
    TEST_CHECK(event_loop.queue.head, 0U);
    TEST_CHECK(event_loop.queue.count, 0U);
});

TEST_FUNC(window_node_toggle_zoom_parent_switches_siblings,
{
    struct window_node parent = {0};
    struct window_node left = {0};
    struct window_node right = {0};

    parent.left = &left;
    parent.right = &right;
    left.parent = &parent;
    right.parent = &parent;

    struct window_node *flush_node = window_node_toggle_zoom_parent(&left);
    TEST_CHECK(flush_node == &parent, true);
    TEST_CHECK(left.zoom == &parent, true);
    TEST_CHECK(right.zoom == NULL, true);

    flush_node = window_node_toggle_zoom_parent(&right);
    TEST_CHECK(flush_node == &parent, true);
    TEST_CHECK(left.zoom == NULL, true);
    TEST_CHECK(right.zoom == &parent, true);

    flush_node = window_node_toggle_zoom_parent(&right);
    TEST_CHECK(flush_node == &right, true);
    TEST_CHECK(left.zoom == NULL, true);
    TEST_CHECK(right.zoom == NULL, true);
});

TEST_FUNC(config_file_execution_preserves_literal_path,
{
    char directory[] = "/tmp/yabai-config-test.XXXXXX";
    char *created_directory = mkdtemp(directory);
    TEST_CHECK(created_directory != NULL, true);

    if (created_directory) {
        char config_path[PATH_MAX];
        snprintf(config_path, sizeof(config_path), "%s/config path;literal", created_directory);

        char *old_test_env = getenv("YABAI_CONFIG_TEST_ENV") ? strdup(getenv("YABAI_CONFIG_TEST_ENV")) : NULL;
        TEST_CHECK(setenv("YABAI_CONFIG_TEST_ENV", "inherited-value", 1), 0);

        FILE *config_file = fopen(config_path, "w");
        TEST_CHECK(config_file != NULL, true);
        if (config_file) {
            static const char script[] = "#!/bin/sh\n[ \"$YABAI_CONFIG_TEST_ENV\" = inherited-value ]\n";
            TEST_CHECK(fwrite(script, 1, sizeof(script) - 1, config_file), sizeof(script) - 1);
            TEST_CHECK(fclose(config_file), 0);
        }

        char *argv[4] = { 0 };
        config_file_exec_argv(config_path, true, argv);
        TEST_CHECK(argv[0] == config_path, true);
        TEST_CHECK(argv[1] == NULL, true);

        pid_t pid = -1;
        int status = 0;
        TEST_CHECK(chmod(config_path, 0700), 0);
        TEST_CHECK(file_can_execute(config_path), true);
        TEST_CHECK(config_file_spawn(config_path, true, &pid), 0);
        TEST_CHECK(waitpid(pid, &status, 0), pid);
        TEST_CHECK(WIFEXITED(status), true);
        if (WIFEXITED(status)) TEST_CHECK(WEXITSTATUS(status), 0);

        config_file = fopen(config_path, "w");
        TEST_CHECK(config_file != NULL, true);
        if (config_file) {
            static const char script[] = "[ \"$YABAI_CONFIG_TEST_ENV\" = inherited-value ]\n";
            TEST_CHECK(fwrite(script, 1, sizeof(script) - 1, config_file), sizeof(script) - 1);
            TEST_CHECK(fclose(config_file), 0);
        }

        pid = -1;
        status = 0;
        TEST_CHECK(file_can_execute(config_path), true);
        TEST_CHECK(config_file_spawn(config_path, true, &pid), 0);
        TEST_CHECK(waitpid(pid, &status, 0), pid);
        TEST_CHECK(WIFEXITED(status), true);
        if (WIFEXITED(status)) TEST_CHECK(WEXITSTATUS(status), 0);

        memset(argv, 0, sizeof(argv));
        config_file_exec_argv(config_path, false, argv);
        TEST_CHECK(strcmp(argv[0], "/usr/bin/env"), 0);
        TEST_CHECK(strcmp(argv[1], "sh"), 0);
        TEST_CHECK(argv[2] == config_path, true);
        TEST_CHECK(argv[3] == NULL, true);

        pid = -1;
        status = 0;
        TEST_CHECK(chmod(config_path, 0600), 0);
        TEST_CHECK(file_can_execute(config_path), false);
        TEST_CHECK(config_file_spawn(config_path, false, &pid), 0);
        TEST_CHECK(waitpid(pid, &status, 0), pid);
        TEST_CHECK(WIFEXITED(status), true);
        if (WIFEXITED(status)) TEST_CHECK(WEXITSTATUS(status), 0);

        if (old_test_env) {
            setenv("YABAI_CONFIG_TEST_ENV", old_test_env, 1);
            free(old_test_env);
        } else {
            unsetenv("YABAI_CONFIG_TEST_ENV");
        }
        unlink(config_path);
        rmdir(created_directory);
    }
});

TEST_FUNC(safe_exec_reports_child_outcomes,
{
    char *success[] = { "/usr/bin/true", NULL };
    char *failure[] = { "/usr/bin/false", NULL };
    char *signaled[] = { "/bin/sh", "-c", "kill -TERM $$", NULL };

    TEST_CHECK(safe_exec(success, false), 0);
    TEST_CHECK(safe_exec(success, true), 0);
    TEST_CHECK(safe_exec(failure, false), 1);
    TEST_CHECK(safe_exec(signaled, false), 1);
});

TEST_FUNC(socket_open_sets_cloexec,
{
    int sockfd = -1;
    TEST_CHECK(socket_open(&sockfd), true);
    if (sockfd != -1) {
        int descriptor_flags = fcntl(sockfd, F_GETFD);
        TEST_CHECK(descriptor_flags != -1, true);
        if (descriptor_flags != -1) {
            TEST_CHECK((descriptor_flags & FD_CLOEXEC) != 0, true);
        }
        close(sockfd);
    }
});

TEST_FUNC(socket_cloexec_helper_sets_flag_and_rejects_invalid_fd,
{
    int sockets[2] = { -1, -1 };
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    if (sockets[0] != -1 && sockets[1] != -1) {
        TEST_CHECK(socket_set_cloexec(sockets[0]), true);
        int descriptor_flags = fcntl(sockets[0], F_GETFD);
        TEST_CHECK(descriptor_flags != -1, true);
        if (descriptor_flags != -1) {
            TEST_CHECK((descriptor_flags & FD_CLOEXEC) != 0, true);
        }
        close(sockets[0]);
        close(sockets[1]);
    }
    TEST_CHECK(socket_set_cloexec(-1), false);
});

TEST_FUNC(socket_address_initializer_enforces_unix_path_boundaries,
{
    struct sockaddr_un address;
    memset(&address, 0xff, sizeof(address));

    TEST_CHECK(socket_address_init(NULL, "/tmp/socket"), false);
    TEST_CHECK(socket_address_init(&address, NULL), false);
    TEST_CHECK(socket_address_init(&address, ""), false);

    char exact_fit[sizeof(address.sun_path)];
    memset(exact_fit, 'a', sizeof(exact_fit) - 1);
    exact_fit[sizeof(exact_fit) - 1] = '\0';
    TEST_CHECK(socket_address_init(&address, exact_fit), true);
    TEST_CHECK(address.sun_family, AF_UNIX);
    TEST_CHECK(strcmp(address.sun_path, exact_fit), 0);

    char too_long[sizeof(address.sun_path) + 1];
    memset(too_long, 'b', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_CHECK(socket_address_init(&address, too_long), false);
    TEST_CHECK(address.sun_family, 0);
    TEST_CHECK(address.sun_path[0], '\0');

    char rejected_path[sizeof(address.sun_path) + 1];
    memcpy(rejected_path, "/tmp/", 5);
    memset(rejected_path + 5, 'c', sizeof(address.sun_path) - 5);
    rejected_path[sizeof(address.sun_path)] = '\0';
    int rejected_fd = open(rejected_path, O_CREAT | O_WRONLY, 0600);
    TEST_CHECK(rejected_fd != -1, true);
    if (rejected_fd != -1) close(rejected_fd);
    TEST_CHECK(message_loop_begin(rejected_path), false);
    TEST_CHECK(access(rejected_path, F_OK), 0);
    unlink(rejected_path);
});

TEST_FUNC(socket_listener_cleanup_invalidates_descriptor_and_only_unlinks_bound_path,
{
    char path[] = "/tmp/yabai-listener-cleanup.XXXXXX";
    int file_fd = mkstemp(path);
    TEST_CHECK(file_fd != -1, true);
    if (file_fd != -1) {
        close(file_fd);

        int sockets[2] = { -1, -1 };
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
        if (sockets[0] != -1 && sockets[1] != -1) {
            int listener_fd = sockets[0];
            socket_listener_cleanup(&sockets[0], path, false);
            TEST_CHECK(sockets[0], -1);
            TEST_CHECK(fcntl(listener_fd, F_GETFD), -1);
            TEST_CHECK(access(path, F_OK), 0);
            close(sockets[1]);
        }

        sockets[0] = -1;
        sockets[1] = -1;
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
        if (sockets[0] != -1 && sockets[1] != -1) {
            socket_listener_cleanup(&sockets[0], path, true);
            TEST_CHECK(sockets[0], -1);
            TEST_CHECK(access(path, F_OK), -1);
            close(sockets[1]);
        }

        unlink(path);
    }
});

TEST_FUNC(scripting_addition_socket_identity_requires_strict_nonroot_uid,
{
    char *saved_sudo_uid = getenv("SUDO_UID") ? strdup(getenv("SUDO_UID")) : NULL;
    char saved_socket_path[MAXLEN];
    snprintf(saved_socket_path, sizeof(saved_socket_path), "%s", g_sa_socket_file);

    char socket_path[MAXLEN];
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(NULL, socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("1junk", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("+1", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("-1", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(" 1", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("1 ", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("184467440737095516160", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("0", socket_path, sizeof(socket_path)), false);

    uid_t current_uid = getuid();
    struct passwd *current_account = current_uid ? getpwuid(current_uid) : NULL;
    if (current_account) {
        char current_uid_string[32];
        char expected_socket_path[MAXLEN];
        snprintf(current_uid_string, sizeof(current_uid_string), "%llu", (unsigned long long) current_uid);
        snprintf(expected_socket_path, sizeof(expected_socket_path), SA_SOCKET_PATH_FMT, current_account->pw_name);
        TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(current_uid_string, socket_path, sizeof(socket_path)), true);
        TEST_CHECK(strcmp(socket_path, expected_socket_path), 0);
    }

    uid_t nonexistent_uid = (uid_t) -1;
    while (nonexistent_uid > 0 && getpwuid(nonexistent_uid)) --nonexistent_uid;
    TEST_CHECK(nonexistent_uid > 0, true);
    if (nonexistent_uid > 0) {
        char nonexistent_uid_string[32];
        snprintf(nonexistent_uid_string, sizeof(nonexistent_uid_string), "%llu", (unsigned long long) nonexistent_uid);
        TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(nonexistent_uid_string, socket_path, sizeof(socket_path)), false);
    }

    unsetenv("SUDO_UID");
    TEST_CHECK(scripting_addition_set_socket_path(), false);
    setenv("SUDO_UID", "", 1);
    TEST_CHECK(scripting_addition_set_socket_path(), false);

    if (saved_sudo_uid) {
        setenv("SUDO_UID", saved_sudo_uid, 1);
        free(saved_sudo_uid);
    } else {
        unsetenv("SUDO_UID");
    }
    snprintf(g_sa_socket_file, sizeof(g_sa_socket_file), "%s", saved_socket_path);
});

TEST_FUNC(scripting_addition_helpers_propagate_failures_and_use_literal_paths,
{
    TEST_CHECK(scripting_addition_prepare_binary("/path/that/does/not/exist"), false);

    char directory_template[] = "/tmp/yabai-sa-remove-test.XXXXXX";
    char *directory = mkdtemp(directory_template);
    TEST_CHECK(directory != NULL, true);
    if (directory) {
        char literal_path[PATH_MAX];
        char sentinel_path[PATH_MAX];
        snprintf(literal_path, sizeof(literal_path), "%s/remove;literal", directory);
        snprintf(sentinel_path, sizeof(sentinel_path), "%s/sentinel", directory);
        TEST_CHECK(mkdir(literal_path, 0700), 0);
        FILE *sentinel = fopen(sentinel_path, "w");
        TEST_CHECK(sentinel != NULL, true);
        if (sentinel) fclose(sentinel);

        TEST_CHECK(scripting_addition_remove_path(literal_path), true);
        TEST_CHECK(access(literal_path, F_OK), -1);
        TEST_CHECK(access(sentinel_path, F_OK), 0);

        unlink(sentinel_path);
        rmdir(directory);
    }
});

TEST_FUNC(plist_xml_validator_accepts_xml_characters_and_rejects_invalid_text,
{
    const char forbidden_control[] = { 'a', 0x01, 'b', '\0' };
    const char malformed_utf8[] = { (char) 0xc3, 0x28, '\0' };
    const char bmp_fffe[] = { (char) 0xef, (char) 0xbf, (char) 0xbe, '\0' };
    const char bmp_ffff[] = { (char) 0xef, (char) 0xbf, (char) 0xbf, '\0' };
    const char supplementary_scalar[] = { (char) 0xf0, (char) 0x9f, (char) 0x98, (char) 0x80, '\0' };

    TEST_CHECK(plist_xml_string_is_valid("normal UTF-8: h\xc3\xa9llo"), true);
    TEST_CHECK(plist_xml_string_is_valid("&<>\"'"), true);
    TEST_CHECK(plist_xml_string_is_valid("tab\tnewline\ncarriage-return\r"), true);
    TEST_CHECK(plist_xml_string_is_valid(forbidden_control), false);
    TEST_CHECK(plist_xml_string_is_valid(malformed_utf8), false);
    TEST_CHECK(plist_xml_string_is_valid(bmp_fffe), false);
    TEST_CHECK(plist_xml_string_is_valid(bmp_ffff), false);
    TEST_CHECK(plist_xml_string_is_valid(supplementary_scalar), true);
});

TEST_FUNC(service_install_publishes_complete_plist_atomically,
{
    char directory[] = "/tmp/yabai-service-test.XXXXXX";
    char *created_directory = mkdtemp(directory);
    TEST_CHECK(created_directory != NULL, true);

    if (created_directory) {
        char plist_path[PATH_MAX];
        snprintf(plist_path, sizeof(plist_path), "%s/com.asmvik.yabai.plist", created_directory);
        TEST_CHECK(service_install_internal(plist_path), 0);

        struct stat plist_stat = { 0 };
        TEST_CHECK(stat(plist_path, &plist_stat), 0);
        TEST_CHECK(plist_stat.st_mode & 0777, 0644);

        FILE *plist_file = fopen(plist_path, "r");
        TEST_CHECK(plist_file != NULL, true);
        if (plist_file) {
            char contents[8192] = { 0 };
            size_t length = fread(contents, 1, sizeof(contents) - 1, plist_file);
            TEST_CHECK(ferror(plist_file), 0);
            TEST_CHECK(length > 0, true);
            TEST_CHECK(strstr(contents, "<key>Label</key>") != NULL, true);
            TEST_CHECK(strstr(contents, "<string>com.asmvik.yabai</string>") != NULL, true);
            fclose(plist_file);
        }

        DIR *dir = opendir(created_directory);
        TEST_CHECK(dir != NULL, true);
        if (dir) {
            struct dirent *entry;
            int temporary_files = 0;
            while ((entry = readdir(dir))) {
                if (strncmp(entry->d_name, "com.asmvik.yabai.plist.", strlen("com.asmvik.yabai.plist.")) == 0) ++temporary_files;
            }
            TEST_CHECK(temporary_files, 0);
            closedir(dir);
        }

        unlink(plist_path);
        rmdir(created_directory);
    }
});

TEST_FUNC(utf8_validation_and_json_replacement_are_strict,
{
    static const unsigned char ascii[] = { 'a', 's', 'c', 'i', 'i' };
    static const unsigned char valid_2[] = { 0xc2, 0xa2 };
    static const unsigned char valid_3[] = { 0xe2, 0x82, 0xac };
    static const unsigned char valid_4[] = { 0xf0, 0x9f, 0x98, 0x80 };
    static const unsigned char truncated_2[] = { 0xc2 };
    static const unsigned char truncated_3[] = { 0xe2, 0x82 };
    static const unsigned char truncated_4[] = { 0xf0, 0x9f, 0x98 };
    static const unsigned char continuation[] = { 0x80 };
    static const unsigned char overlong_2[] = { 0xc0, 0xaf };
    static const unsigned char overlong_3[] = { 0xe0, 0x80, 0xaf };
    static const unsigned char overlong_4[] = { 0xf0, 0x80, 0x80, 0xaf };
    static const unsigned char surrogate[] = { 0xed, 0xa0, 0x80 };
    static const unsigned char too_large[] = { 0xf4, 0x90, 0x80, 0x80 };
    static const struct {
        const unsigned char *bytes;
        size_t length;
        bool valid;
    } cases[] = {
        { ascii, sizeof(ascii), true },
        { valid_2, sizeof(valid_2), true },
        { valid_3, sizeof(valid_3), true },
        { valid_4, sizeof(valid_4), true },
        { truncated_2, sizeof(truncated_2), false },
        { truncated_3, sizeof(truncated_3), false },
        { truncated_4, sizeof(truncated_4), false },
        { continuation, sizeof(continuation), false },
        { overlong_2, sizeof(overlong_2), false },
        { overlong_3, sizeof(overlong_3), false },
        { overlong_4, sizeof(overlong_4), false },
        { surrogate, sizeof(surrogate), false },
        { too_large, sizeof(too_large), false },
    };

    for (int i = 0; i < array_count(cases); ++i) {
        TEST_CHECK(utf8_validate_bytes(cases[i].bytes, cases[i].length), cases[i].valid);
    }

    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;

    uint64_t saved_temp_storage_used = g_temp_storage.used;
    ts_reset();

    char valid_string[] = { 'x', (char) 0xc2, (char) 0xa2, (char) 0xe2, (char) 0x82,
                            (char) 0xac, (char) 0xf0, (char) 0x9f, (char) 0x98, (char) 0x80, 0 };
    TEST_CHECK(utf8_validate_string(valid_string), true);
    TEST_CHECK(ts_string_escape(valid_string) == NULL, true);

    char malformed[] = { 'a', (char) 0xc0, (char) 0xaf, 'b', 0 };
    TEST_CHECK(utf8_validate_string(malformed), false);
    char *escaped = ts_string_escape(malformed);
    TEST_CHECK(escaped != NULL, true);
    TEST_CHECK(strcmp(escaped, "a\\ufffd\\ufffdb"), 0);

    char json_buffer[128];
    int json_length = snprintf(json_buffer, sizeof(json_buffer), "\"%s\"", escaped);
    TEST_CHECK(json_length > 0 && json_length < (int) sizeof(json_buffer), true);
    NSData *json_data = [NSData dataWithBytes:json_buffer length:(NSUInteger) json_length];
    NSError *json_error = nil;
    NSString *json_string = [NSJSONSerialization JSONObjectWithData:json_data options:NSJSONReadingFragmentsAllowed error:&json_error];
    static const unsigned char expected_bytes[] = { 'a', 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd, 'b' };
    NSString *expected_string = [[NSString alloc] initWithBytes:expected_bytes length:sizeof(expected_bytes) encoding:NSUTF8StringEncoding];
    TEST_CHECK(json_error == nil, true);
    TEST_CHECK(json_string != nil, true);
    TEST_CHECK([json_string isEqualToString:expected_string], true);
    [expected_string release];

    g_temp_storage.used = saved_temp_storage_used;
});

TEST_FUNC(serialization_string_escaping_is_strict,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;

    uint64_t saved_temp_storage_used = g_temp_storage.used;
    ts_reset();

    char xml_input[] = "amp& lt< gt> quote\" apos'";
    char *xml_escaped = plist_xml_escape(xml_input);
    TEST_CHECK(xml_escaped != NULL, true);
    TEST_CHECK(strcmp(xml_escaped, "amp&amp; lt&lt; gt&gt; quote&quot; apos&apos;") == 0, true);
    free(xml_escaped);

    char *old_user = getenv("USER") ? strdup(getenv("USER")) : NULL;
    char *old_path = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
    setenv("USER", "user&<>\"'", 1);
    setenv("PATH", "/bin:/tmp/a&b<c>d\"e'f", 1);
    int plist_length = 0;
    char *plist = populate_plist(&plist_length);
    TEST_CHECK(plist != NULL, true);
    TEST_CHECK(plist_length == (int) strlen(plist), true);
    TEST_CHECK(strstr(plist, "<string>/bin:/tmp/a&amp;b&lt;c&gt;d&quot;e&apos;f</string>") != NULL, true);
    TEST_CHECK(strstr(plist, "/tmp/yabai_user&amp;&lt;&gt;&quot;&apos;.out.log") != NULL, true);
    TEST_CHECK(strstr(plist, "/tmp/yabai_user&amp;&lt;&gt;&quot;&apos;.err.log") != NULL, true);

    NSData *plist_data = [NSData dataWithBytes:plist length:(NSUInteger)plist_length];
    NSError *plist_error = nil;
    id plist_object = [NSPropertyListSerialization propertyListWithData:plist_data options:NSPropertyListImmutable format:nil error:&plist_error];
    TEST_CHECK(plist_object != nil, true);
    NSDictionary *plist_dictionary = (NSDictionary *) plist_object;
    TEST_CHECK([[[plist_dictionary objectForKey:@"EnvironmentVariables"] objectForKey:@"PATH"] isEqualToString:@"/bin:/tmp/a&b<c>d\"e'f"], true);
    TEST_CHECK([[plist_dictionary objectForKey:@"StandardOutPath"] isEqualToString:@"/tmp/yabai_user&<>\"'.out.log"], true);
    TEST_CHECK([[plist_dictionary objectForKey:@"StandardErrorPath"] isEqualToString:@"/tmp/yabai_user&<>\"'.err.log"], true);
    TEST_CHECK([[plist_dictionary objectForKey:@"ProgramArguments"] count], (NSUInteger) 1);
    free(plist);
    if (old_user) { setenv("USER", old_user, 1); free(old_user); } else { unsetenv("USER"); }
    if (old_path) { setenv("PATH", old_path, 1); free(old_path); } else { unsetenv("PATH"); }

    ts_reset();
    char json_input[] = "quote\" backslash\\ newline\n tab\t control\x01";
    char *json_escaped = ts_string_escape(json_input);
    TEST_CHECK(strcmp(json_escaped, "quote\\\" backslash\\\\ newline\\n tab\\t control\\u0001") == 0, true);
    char expected_json[256];
    snprintf(expected_json, sizeof(expected_json), "%s", json_escaped);
    NSString *json_original = [NSString stringWithUTF8String:json_input];

    ts_reset();
    struct rule rule = {0};
    rule.label = json_input;
    rule.effects.scratchpad = json_input;
    FILE *rule_file = tmpfile();
    TEST_CHECK(rule_file != NULL, true);
    rule_serialize(rule_file, &rule, 7);
    rewind(rule_file);
    char rule_buffer[2048] = {0};
    fread(rule_buffer, 1, sizeof(rule_buffer)-1, rule_file);
    fclose(rule_file);
    char expected_rule_label[256];
    char expected_rule_scratchpad[256];
    snprintf(expected_rule_label, sizeof(expected_rule_label), "\"label\":\"%s\"", expected_json);
    snprintf(expected_rule_scratchpad, sizeof(expected_rule_scratchpad), "\"scratchpad\":\"%s\"", expected_json);
    TEST_CHECK(strstr(rule_buffer, expected_rule_label) != NULL, true);
    TEST_CHECK(strstr(rule_buffer, expected_rule_scratchpad) != NULL, true);
    NSData *rule_data = [NSData dataWithBytes:rule_buffer length:strlen(rule_buffer)];
    NSError *rule_error = nil;
    NSDictionary *rule_json = [NSJSONSerialization JSONObjectWithData:rule_data options:0 error:&rule_error];
    TEST_CHECK(rule_json != nil, true);
    TEST_CHECK([[rule_json objectForKey:@"label"] isEqualToString:json_original], true);
    TEST_CHECK([[rule_json objectForKey:@"scratchpad"] isEqualToString:json_original], true);

    struct signal signal = {0};
    signal.label = json_input;
    signal.command = json_input;
    FILE *signal_file = tmpfile();
    TEST_CHECK(signal_file != NULL, true);
    event_signal_serialize(signal_file, &signal, SIGNAL_APPLICATION_LAUNCHED, 3);
    rewind(signal_file);
    char signal_buffer[2048] = {0};
    fread(signal_buffer, 1, sizeof(signal_buffer)-1, signal_file);
    fclose(signal_file);
    char expected_signal_label[256];
    char expected_signal_action[256];
    snprintf(expected_signal_label, sizeof(expected_signal_label), "\"label\":\"%s\"", expected_json);
    snprintf(expected_signal_action, sizeof(expected_signal_action), "\"action\":\"%s\"", expected_json);
    TEST_CHECK(strstr(signal_buffer, expected_signal_label) != NULL, true);
    TEST_CHECK(strstr(signal_buffer, expected_signal_action) != NULL, true);
    NSData *signal_data = [NSData dataWithBytes:signal_buffer length:strlen(signal_buffer)];
    NSError *signal_error = nil;
    NSDictionary *signal_json = [NSJSONSerialization JSONObjectWithData:signal_data options:0 error:&signal_error];
    TEST_CHECK(signal_json != nil, true);
    TEST_CHECK([[signal_json objectForKey:@"label"] isEqualToString:json_original], true);
    TEST_CHECK([[signal_json objectForKey:@"action"] isEqualToString:json_original], true);

    ts_reset();
    struct window applicationless_window = { .id = 0, .scratchpad = json_input };
    FILE *window_file = tmpfile();
    TEST_CHECK(window_file != NULL, true);
    if (window_file) {
        uint64_t window_flags = WINDOW_PROPERTY_PID |
                                WINDOW_PROPERTY_APP |
                                WINDOW_PROPERTY_SCRATCHPAD |
                                WINDOW_PROPERTY_IS_HIDDEN;
        window_serialize(window_file, &applicationless_window, window_flags);
        rewind(window_file);
        char window_buffer[2048] = {0};
        fread(window_buffer, 1, sizeof(window_buffer)-1, window_file);
        fclose(window_file);

        NSData *window_data = [NSData dataWithBytes:window_buffer length:strlen(window_buffer)];
        NSError *window_error = nil;
        NSDictionary *window_json = [NSJSONSerialization JSONObjectWithData:window_data options:0 error:&window_error];
        TEST_CHECK(window_json != nil, true);
        TEST_CHECK([[window_json objectForKey:@"pid"] intValue], 0);
        TEST_CHECK([[window_json objectForKey:@"app"] isEqualToString:@"<unknown>"], true);
        TEST_CHECK([[window_json objectForKey:@"scratchpad"] isEqualToString:json_original], true);
        TEST_CHECK([[window_json objectForKey:@"is-hidden"] boolValue], false);
    }
    TEST_CHECK(window_serialize_is_hidden(&applicationless_window), false);

    const UInt8 role_bytes[] = { 'r', 'o', 'l', 'e', '"', '\\', '\n', '\t', 0x01 };
    const UInt8 subrole_bytes[] = { 's', 'u', 'b', 'r', 'o', 'l', 'e', '"', '\\', '\n', '\t', 0x02 };
    struct window role_window = {0};
    role_window.role = CFStringCreateWithBytes(NULL, role_bytes, sizeof(role_bytes), kCFStringEncodingUTF8, false);
    role_window.subrole = CFStringCreateWithBytes(NULL, subrole_bytes, sizeof(subrole_bytes), kCFStringEncodingUTF8, false);
    TEST_CHECK(role_window.role != NULL, true);
    TEST_CHECK(role_window.subrole != NULL, true);

    if (role_window.role && role_window.subrole) {
        ts_reset();
        FILE *role_file = tmpfile();
        TEST_CHECK(role_file != NULL, true);
        if (role_file) {
            window_serialize(role_file, &role_window, WINDOW_PROPERTY_ROLE | WINDOW_PROPERTY_SUBROLE);
            rewind(role_file);
            char role_buffer[2048] = {0};
            fread(role_buffer, 1, sizeof(role_buffer) - 1, role_file);
            fclose(role_file);

            NSData *role_data = [NSData dataWithBytes:role_buffer length:strlen(role_buffer)];
            NSError *role_error = nil;
            NSDictionary *role_json = [NSJSONSerialization JSONObjectWithData:role_data options:0 error:&role_error];
            TEST_CHECK(role_json != nil, true);
            TEST_CHECK([[role_json objectForKey:@"role"] isEqualToString:(NSString *) role_window.role], true);
            TEST_CHECK([[role_json objectForKey:@"subrole"] isEqualToString:(NSString *) role_window.subrole], true);
        }
    }

    if (role_window.role) CFRelease(role_window.role);
    if (role_window.subrole) CFRelease(role_window.subrole);

    struct space_label *saved_space_labels = g_space_manager.labels;
    g_space_manager.labels = NULL;
    struct view labeled_view = { .sid = 0x7fffffe0ULL };
    space_manager_set_label_for_space(&g_space_manager, labeled_view.sid, strdup(json_input));
    ts_reset();
    FILE *view_file = tmpfile();
    TEST_CHECK(view_file != NULL, true);
    if (view_file) {
        view_serialize(view_file, &labeled_view, SPACE_PROPERTY_LABEL);
        rewind(view_file);
        char view_buffer[2048] = {0};
        fread(view_buffer, 1, sizeof(view_buffer)-1, view_file);
        fclose(view_file);

        NSData *view_data = [NSData dataWithBytes:view_buffer length:strlen(view_buffer)];
        NSError *view_error = nil;
        NSDictionary *view_json = [NSJSONSerialization JSONObjectWithData:view_data options:0 error:&view_error];
        TEST_CHECK(view_json != nil, true);
        TEST_CHECK([[view_json objectForKey:@"label"] isEqualToString:json_original], true);
    }
    space_manager_remove_label_for_space(&g_space_manager, labeled_view.sid);
    buf_free(g_space_manager.labels);
    g_space_manager.labels = saved_space_labels;

    g_temp_storage.used = saved_temp_storage_used;
});

TEST_FUNC(window_and_space_serializers_round_trip_safe_escaped_and_absent_strings,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;

    uint64_t saved_temp_storage_used = g_temp_storage.used;
    char *forms[] = {
        "ordinary-safe-value",
        "quote\" backslash\\ newline\n tab\t control\x01",
        NULL
    };

    for (int i = 0; i < array_count(forms); ++i) {
        char *value = forms[i];
        NSString *expected = value ? [NSString stringWithUTF8String:value] : @"";
        struct window window = { .scratchpad = value };
        if (value) {
            window.role = CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8);
            window.subrole = CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8);
        }

        ts_reset();
        FILE *file = tmpfile();
        TEST_CHECK(file != NULL, true);
        if (file) {
            window_serialize(file, &window, WINDOW_PROPERTY_SCRATCHPAD | WINDOW_PROPERTY_ROLE | WINDOW_PROPERTY_SUBROLE);
            rewind(file);
            char buffer[2048] = {0};
            fread(buffer, 1, sizeof(buffer) - 1, file);
            fclose(file);

            NSData *data = [NSData dataWithBytes:buffer length:strlen(buffer)];
            NSError *error = nil;
            NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
            TEST_CHECK(error == nil, true);
            TEST_CHECK(json != nil, true);
            TEST_CHECK([[json objectForKey:@"scratchpad"] isEqualToString:expected], true);
            TEST_CHECK([[json objectForKey:@"role"] isEqualToString:expected], true);
            TEST_CHECK([[json objectForKey:@"subrole"] isEqualToString:expected], true);
        }

        if (window.role) CFRelease(window.role);
        if (window.subrole) CFRelease(window.subrole);
    }

    struct space_label *saved_space_labels = g_space_manager.labels;
    g_space_manager.labels = NULL;
    struct view view = { .sid = 0x7fffffdfULL };

    for (int i = 0; i < array_count(forms); ++i) {
        char *value = forms[i];
        NSString *expected = value ? [NSString stringWithUTF8String:value] : @"";
        if (value) space_manager_set_label_for_space(&g_space_manager, view.sid, strdup(value));

        ts_reset();
        FILE *file = tmpfile();
        TEST_CHECK(file != NULL, true);
        if (file) {
            view_serialize(file, &view, SPACE_PROPERTY_LABEL);
            rewind(file);
            char buffer[2048] = {0};
            fread(buffer, 1, sizeof(buffer) - 1, file);
            fclose(file);

            NSData *data = [NSData dataWithBytes:buffer length:strlen(buffer)];
            NSError *error = nil;
            NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
            TEST_CHECK(error == nil, true);
            TEST_CHECK(json != nil, true);
            TEST_CHECK([[json objectForKey:@"label"] isEqualToString:expected], true);
        }

        space_manager_remove_label_for_space(&g_space_manager, view.sid);
    }

    buf_free(g_space_manager.labels);
    g_space_manager.labels = saved_space_labels;
    g_temp_storage.used = saved_temp_storage_used;
});

TEST_FUNC(view_stack_window_node_rejects_full_node_without_mutation,
{
    struct window_node node = {0};
    struct window window = {0};

    for (int i = 0; i < NODE_MAX_WINDOW_COUNT; ++i) {
        node.window_list[i] = 100 + i;
        node.window_order[i] = 200 + i;
    }

    node.window_count = NODE_MAX_WINDOW_COUNT;
    window.id = 999;

    TEST_CHECK(view_stack_window_node(&node, &window), false);
    TEST_CHECK(node.window_count, NODE_MAX_WINDOW_COUNT);
    TEST_CHECK(node.window_list[0], 100);
    TEST_CHECK(node.window_order[0], 200);
});

TEST_FUNC(view_find_window_list_expands_for_full_stack_node,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;

    uint64_t saved_temp_storage_used = g_temp_storage.used;
    ts_reset();

    struct view view = {0};
    struct window_node root = {0};
    view.root = &root;

    for (int i = 0; i < NODE_MAX_WINDOW_COUNT; ++i) {
        root.window_list[i] = 300 + i;
    }

    root.window_count = NODE_MAX_WINDOW_COUNT;

    int window_count = 0;
    uint32_t *window_list = view_find_window_list(&view, &window_count);

    TEST_CHECK(window_count, NODE_MAX_WINDOW_COUNT);
    TEST_CHECK(window_list[0], 300);
    TEST_CHECK(window_list[NODE_MAX_WINDOW_COUNT - 1], 300 + NODE_MAX_WINDOW_COUNT - 1);

    g_temp_storage.used = saved_temp_storage_used;
});

TEST_FUNC(mouse_click_state_ordinary_drag_does_not_poison_consumed_click,
{
    struct mouse_state mouse_state = {0};
    struct mouse_click_state *left_click = mouse_click_state_for_event(&mouse_state, kCGEventLeftMouseDown);

    mouse_click_state_mouse_down(left_click, false);
    TEST_CHECK(left_click->drag_detected, false);
    mouse_click_state_mouse_dragged(left_click);
    struct mouse_click_state_up_result ordinary_mouse_up = mouse_click_state_mouse_up(left_click);

    TEST_CHECK(ordinary_mouse_up.consume, false);
    TEST_CHECK(ordinary_mouse_up.repost, false);
    TEST_CHECK(left_click->drag_detected, false);

    mouse_click_state_mouse_down(left_click, true);
    struct mouse_click_state_up_result consumed_mouse_up = mouse_click_state_mouse_up(left_click);

    TEST_CHECK(consumed_mouse_up.consume, true);
    TEST_CHECK(consumed_mouse_up.repost, true);

    mouse_click_state_clear(left_click);
    TEST_CHECK(left_click->down_event == NULL, true);
    TEST_CHECK(left_click->consume, false);
    TEST_CHECK(left_click->drag_detected, false);
});

TEST_FUNC(mouse_click_state_tracks_chorded_buttons_independently,
{
    struct mouse_state mouse_state = {0};
    int left_event_token;
    int right_event_token;
    struct mouse_click_state *left_click = mouse_click_state_for_event(&mouse_state, kCGEventLeftMouseDown);
    struct mouse_click_state *right_click = mouse_click_state_for_event(&mouse_state, kCGEventRightMouseDown);

    mouse_click_state_mouse_down(left_click, true);
    left_click->down_event = (CGEventRef) &left_event_token;
    mouse_click_state_mouse_down(right_click, true);
    right_click->down_event = (CGEventRef) &right_event_token;

    struct mouse_click_state_up_result right_up = mouse_click_state_mouse_up(mouse_click_state_for_event(&mouse_state, kCGEventRightMouseUp));
    TEST_CHECK(right_up.consume, true);
    TEST_CHECK(right_up.repost, true);
    mouse_click_state_clear(right_click);
    TEST_CHECK(right_click->down_event == NULL, true);
    TEST_CHECK(left_click->consume, true);
    TEST_CHECK(left_click->down_event == (CGEventRef) &left_event_token, true);

    struct mouse_click_state_up_result left_up = mouse_click_state_mouse_up(mouse_click_state_for_event(&mouse_state, kCGEventLeftMouseUp));
    TEST_CHECK(left_up.consume, true);
    TEST_CHECK(left_up.repost, true);
    mouse_click_state_clear(left_click);

    mouse_click_state_mouse_down(right_click, true);
    right_click->down_event = (CGEventRef) &right_event_token;
    mouse_click_state_mouse_down(left_click, true);
    left_click->down_event = (CGEventRef) &left_event_token;

    left_up = mouse_click_state_mouse_up(mouse_click_state_for_event(&mouse_state, kCGEventLeftMouseUp));
    TEST_CHECK(left_up.consume, true);
    TEST_CHECK(left_up.repost, true);
    mouse_click_state_clear(left_click);
    TEST_CHECK(left_click->down_event == NULL, true);
    TEST_CHECK(right_click->consume, true);
    TEST_CHECK(right_click->down_event == (CGEventRef) &right_event_token, true);

    right_up = mouse_click_state_mouse_up(mouse_click_state_for_event(&mouse_state, kCGEventRightMouseUp));
    TEST_CHECK(right_up.consume, true);
    TEST_CHECK(right_up.repost, true);
    mouse_click_state_clear(right_click);
});

TEST_FUNC(mouse_action_classifies_buttons_and_modifiers,
{
    CGEventFlags alt = kCGEventFlagMaskAlternate;
    CGEventFlags shift = kCGEventFlagMaskShift;

    TEST_CHECK(mouse_action_for_event(kCGEventLeftMouseDown, alt, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_MOVE);
    TEST_CHECK(mouse_action_for_event(kCGEventRightMouseDown, alt, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_RESIZE);
    TEST_CHECK(mouse_action_for_event(kCGEventLeftMouseDown, shift, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_NONE);
    TEST_CHECK(mouse_action_for_event(kCGEventRightMouseDown, alt | shift, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_NONE);
    TEST_CHECK(mouse_action_for_event(kCGEventMouseMoved, alt, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_NONE);
});

TEST_FUNC(mouse_queued_action_is_immutable_across_configuration_changes,
{
    struct mouse_state mouse_state = {0};
    mouse_state_set_modifier(&mouse_state, MOUSE_MOD_ALT);
    mouse_state_set_action1(&mouse_state, MOUSE_MODE_MOVE);
    mouse_state_set_action2(&mouse_state, MOUSE_MODE_RESIZE);

    enum mouse_mode queued_action = mouse_action_for_event(kCGEventLeftMouseDown,
                                                           kCGEventFlagMaskAlternate,
                                                           mouse_state.modifier,
                                                           mouse_state.action1,
                                                           mouse_state.action2);

    mouse_state_set_modifier(&mouse_state, MOUSE_MOD_SHIFT);
    mouse_state_set_action1(&mouse_state, MOUSE_MODE_RESIZE);
    mouse_state_set_action2(&mouse_state, MOUSE_MODE_MOVE);

    TEST_CHECK(queued_action, MOUSE_MODE_MOVE);
    TEST_CHECK(mouse_action_for_event(kCGEventLeftMouseDown,
                                      kCGEventFlagMaskAlternate,
                                      mouse_state.modifier,
                                      mouse_state.action1,
                                      mouse_state.action2), MOUSE_MODE_NONE);
    TEST_CHECK(mouse_action_for_event(kCGEventLeftMouseDown,
                                      kCGEventFlagMaskShift,
                                      mouse_state.modifier,
                                      mouse_state.action1,
                                      mouse_state.action2), MOUSE_MODE_RESIZE);
});

TEST_FUNC(mouse_unclassified_action_does_not_begin_gesture,
{
    struct mouse_state mouse_state = {
        .current_action = MOUSE_MODE_NONE,
        .active_button = MOUSE_BUTTON_NONE,
        .down_location = { 11.0f, 12.0f },
        .window_frame = { { 13.0f, 14.0f }, { 15.0f, 16.0f } },
        .direction = HANDLE_TOP
    };
    struct window window = { .frame = { { 20.0f, 30.0f }, { 400.0f, 300.0f } } };
    CGPoint point = { 40.0f, 50.0f };
    enum mouse_mode action = mouse_action_for_event(kCGEventLeftMouseDown, 0, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE);

    TEST_CHECK(action, MOUSE_MODE_NONE);
    TEST_CHECK(mouse_drag_begin(&mouse_state, action, kCGMouseButtonLeft, &window, point), false);
    TEST_CHECK(mouse_state.window == NULL, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(mouse_state.active_button, MOUSE_BUTTON_NONE);
    TEST_CHECK(mouse_state.down_location.x, 11.0f);
    TEST_CHECK(mouse_state.down_location.y, 12.0f);
    TEST_CHECK(mouse_state.window_frame.origin.x, 13.0f);
    TEST_CHECK(mouse_state.window_frame.origin.y, 14.0f);
    TEST_CHECK(mouse_state.window_frame.size.width, 15.0f);
    TEST_CHECK(mouse_state.window_frame.size.height, 16.0f);
    TEST_CHECK(mouse_state.direction, HANDLE_TOP);
});

static int g_mouse_drag_move_call_count;
static struct window *g_mouse_drag_move_window;

static void test_mouse_drag_move_window(struct window *window, float x, float y)
{
    (void) x;
    (void) y;
    ++g_mouse_drag_move_call_count;
    g_mouse_drag_move_window = window;
}

TEST_FUNC(mouse_dragged_unmanaged_window_preserves_owner_gesture,
{
    struct mouse_state saved_mouse_state = g_mouse_state;
    struct table saved_managed_window = g_window_manager.managed_window;
    enum mission_control_mode saved_mission_control_mode = g_mission_control_mode;
    __auto_type saved_move_callback = event_loop_test_mouse_drag_move_window;
    int saved_move_call_count = g_mouse_drag_move_call_count;
    struct window *saved_move_window = g_mouse_drag_move_window;
    struct table unmanaged_window;
    table_init(&unmanaged_window, 1, hash_wm, compare_wm);

    AXUIElementRef window_ref = AXUIElementCreateApplication(getpid());
    struct window grabbed_window = {
        .ref = window_ref,
        .id = 987,
        .frame = { { 20.0f, 30.0f }, { 400.0f, 300.0f } },
        .flags = WINDOW_FLOAT
    };
    grabbed_window.id_ptr = &grabbed_window.id;

    g_window_manager.managed_window = unmanaged_window;
    memset(&g_mouse_state, 0, sizeof(g_mouse_state));
    g_mouse_state.window = &grabbed_window;
    g_mouse_state.window_frame = grabbed_window.frame;
    g_mouse_state.down_location = CGPointMake(40.0f, 50.0f);
    g_mouse_state.current_action = MOUSE_MODE_MOVE;
    g_mouse_state.active_button = kCGMouseButtonLeft;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
    g_mouse_drag_move_call_count = 0;
    g_mouse_drag_move_window = NULL;
    event_loop_test_mouse_drag_move_window = test_mouse_drag_move_window;

    CGEventRef first_drag = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDragged,
                                                    CGPointMake(45.0f, 55.0f), kCGMouseButtonLeft);
    TEST_CHECK(first_drag != NULL, true);
    if (first_drag) EVENT_HANDLER_MOUSE_DRAGGED(first_drag, 0);

    TEST_CHECK(g_mouse_drag_move_call_count, 1);
    TEST_CHECK(g_mouse_drag_move_window == &grabbed_window, true);
    TEST_CHECK(g_mouse_state.window == &grabbed_window, true);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_MOVE);
    TEST_CHECK(g_mouse_state.active_button, kCGMouseButtonLeft);

    CGEventRef second_drag = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDragged,
                                                     CGPointMake(50.0f, 60.0f), kCGMouseButtonLeft);
    TEST_CHECK(second_drag != NULL, true);
    if (second_drag) EVENT_HANDLER_MOUSE_DRAGGED(second_drag, 0);

    TEST_CHECK(g_mouse_drag_move_call_count, 2);
    TEST_CHECK(g_mouse_drag_move_window == &grabbed_window, true);
    TEST_CHECK(g_mouse_state.window == &grabbed_window, true);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_MOVE);
    TEST_CHECK(g_mouse_state.active_button, kCGMouseButtonLeft);

    CGEventRef mouse_up = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp,
                                                  CGPointMake(50.0f, 60.0f), kCGMouseButtonLeft);
    TEST_CHECK(mouse_up != NULL, true);
    if (mouse_up) EVENT_HANDLER_MOUSE_UP(mouse_up, 0);

    TEST_CHECK(g_mouse_state.window == NULL, true);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(g_mouse_state.active_button, MOUSE_BUTTON_NONE);

    event_loop_test_mouse_drag_move_window = saved_move_callback;
    g_mouse_drag_move_call_count = saved_move_call_count;
    g_mouse_drag_move_window = saved_move_window;
    g_mouse_state = saved_mouse_state;
    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed_window;
    g_mission_control_mode = saved_mission_control_mode;
    if (window_ref) CFRelease(window_ref);
});

TEST_FUNC(mouse_drag_owner_button_controls_events_and_reset,
{
    struct window grabbed_window = {0};
    struct mouse_state mouse_state = {
        .window = &grabbed_window,
        .current_action = MOUSE_MODE_MOVE,
        .active_button = kCGMouseButtonLeft
    };

    TEST_CHECK(mouse_drag_event_is_owner(&mouse_state, kCGMouseButtonRight), false);
    TEST_CHECK(mouse_drag_reset_if_owner(&mouse_state, kCGMouseButtonRight), false);
    TEST_CHECK(mouse_state.window == &grabbed_window, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_MOVE);
    TEST_CHECK(mouse_state.active_button, kCGMouseButtonLeft);
    TEST_CHECK(mouse_drag_event_is_owner(&mouse_state, kCGMouseButtonLeft), true);
    TEST_CHECK(mouse_drag_reset_if_owner(&mouse_state, kCGMouseButtonLeft), true);
    TEST_CHECK(mouse_state.window == NULL, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(mouse_state.active_button, MOUSE_BUTTON_NONE);

    mouse_state.window = &grabbed_window;
    mouse_state.current_action = MOUSE_MODE_RESIZE;
    mouse_state.active_button = kCGMouseButtonRight;

    TEST_CHECK(mouse_drag_event_is_owner(&mouse_state, kCGMouseButtonLeft), false);
    TEST_CHECK(mouse_drag_reset_if_owner(&mouse_state, kCGMouseButtonLeft), false);
    TEST_CHECK(mouse_state.window == &grabbed_window, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_RESIZE);
    TEST_CHECK(mouse_state.active_button, kCGMouseButtonRight);
    TEST_CHECK(mouse_drag_event_is_owner(&mouse_state, kCGMouseButtonRight), true);
    TEST_CHECK(mouse_drag_reset_if_owner(&mouse_state, kCGMouseButtonRight), true);
    TEST_CHECK(mouse_state.window == NULL, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(mouse_state.active_button, MOUSE_BUTTON_NONE);
});

TEST_FUNC(mouse_drag_reset_clears_feedback_window_action_and_button_state,
{
    struct window_node feedback_node = { .insert_dir = DIR_EAST };
    struct window grabbed_window = {0};
    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;
    struct window *saved_window = g_mouse_state.window;
    enum mouse_mode saved_action = g_mouse_state.current_action;
    int64_t saved_active_button = g_mouse_state.active_button;

    g_mouse_state.feedback_node = &feedback_node;
    g_mouse_state.window = &grabbed_window;
    g_mouse_state.current_action = MOUSE_MODE_MOVE;
    g_mouse_state.active_button = kCGMouseButtonLeft;
    mouse_drag_reset(&g_mouse_state);

    TEST_CHECK(feedback_node.insert_dir, 0);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    TEST_CHECK(g_mouse_state.window == NULL, true);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(g_mouse_state.active_button, MOUSE_BUTTON_NONE);

    g_mouse_state.feedback_node = saved_feedback_node;
    g_mouse_state.window = saved_window;
    g_mouse_state.current_action = saved_action;
    g_mouse_state.active_button = saved_active_button;
});

TEST_FUNC(insert_feedback_destroy_clears_mouse_reference,
{
    struct window_node node = {0};
    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;
    g_mouse_state.feedback_node = &node;

    insert_feedback_destroy(&node);

    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    g_mouse_state.feedback_node = saved_feedback_node;
});

TEST_FUNC(insert_feedback_invalid_direction_and_no_action_cleanup,
{
    struct window_node node = {0};
    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;

    g_mouse_state.feedback_node = &node;
    node.insert_dir = 0;
    insert_feedback_show(&node);
    TEST_CHECK(node.feedback_window.id, 0U);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);

    g_mouse_state.feedback_node = &node;
    node.insert_dir = UINT8_MAX;
    insert_feedback_show(&node);
    TEST_CHECK(node.feedback_window.id, 0U);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);

    g_mouse_state.feedback_node = saved_feedback_node;
});

TEST_FUNC(insert_feedback_rekeys_on_stack_order_mutations,
{
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;

    workspace_set_macos_version(15);
    table_init(&g_window_manager.insert_feedback, 20, hash_wm, compare_wm);

    struct window_node remove_node = {0};
    remove_node.window_list[0] = 11;
    remove_node.window_list[1] = 12;
    remove_node.window_order[0] = 11;
    remove_node.window_order[1] = 12;
    remove_node.window_count = 2;
    remove_node.feedback_window.id = 1;
    table_add(&g_window_manager.insert_feedback, &remove_node.window_order[0], &remove_node);

    struct view remove_view = { .root = &remove_node };
    struct window removed_window = { .id = 11 };
    view_remove_window_node(&remove_view, &removed_window);

    uint32_t old_remove_key = 11;
    uint32_t new_remove_key = 12;
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &old_remove_key) == NULL, true);
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &new_remove_key) == &remove_node, true);

    struct window_node a_node = {0};
    struct window_node b_node = {0};
    a_node.window_list[0] = 21;
    a_node.window_order[0] = 21;
    a_node.window_count = 1;
    a_node.feedback_window.id = 2;
    b_node.window_list[0] = 31;
    b_node.window_order[0] = 31;
    b_node.window_count = 1;
    b_node.feedback_window.id = 3;
    table_add(&g_window_manager.insert_feedback, &a_node.window_order[0], &a_node);
    table_add(&g_window_manager.insert_feedback, &b_node.window_order[0], &b_node);

    window_node_swap_window_list(&a_node, &b_node);

    uint32_t a_new_key = 31;
    uint32_t b_new_key = 21;
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &a_new_key) == &a_node, true);
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &b_new_key) == &b_node, true);

    struct window_node reorder_node = {0};
    reorder_node.window_list[0] = 41;
    reorder_node.window_list[1] = 42;
    reorder_node.window_order[0] = 41;
    reorder_node.window_order[1] = 42;
    reorder_node.window_count = 2;
    reorder_node.feedback_window.id = 4;
    table_add(&g_window_manager.insert_feedback, &reorder_node.window_order[0], &reorder_node);

    TEST_CHECK(window_node_make_window_order_head(&reorder_node, 42), true);

    uint32_t old_reorder_key = 41;
    uint32_t new_reorder_key = 42;
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &old_reorder_key) == NULL, true);
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &new_reorder_key) == &reorder_node, true);

    table_remove(&g_window_manager.insert_feedback, &new_remove_key);
    table_remove(&g_window_manager.insert_feedback, &a_new_key);
    table_remove(&g_window_manager.insert_feedback, &b_new_key);
    table_remove(&g_window_manager.insert_feedback, &new_reorder_key);
    table_free(&g_window_manager.insert_feedback);
    memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback));

    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
});

TEST_FUNC(insert_feedback_rekeys_collapsed_child_before_free,
{
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;

    workspace_set_macos_version(15);
    table_init(&g_window_manager.insert_feedback, 20, hash_wm, compare_wm);

    struct window_node parent = {0};
    struct window_node child = {0};
    parent.window_list[0] = 52;
    parent.window_order[0] = 52;
    parent.window_count = 1;
    child.window_list[0] = 52;
    child.window_order[0] = 52;
    child.window_count = 1;
    child.feedback_window.id = 5;
    table_add(&g_window_manager.insert_feedback, &child.window_order[0], &child);

    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;
    g_mouse_state.feedback_node = &child;
    insert_feedback_adopt(&parent, &child);

    uint32_t survivor_key = 52;
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &survivor_key) == &parent, true);
    TEST_CHECK(child.feedback_window.id, 0);
    TEST_CHECK(g_mouse_state.feedback_node == &parent, true);
    table_remove(&g_window_manager.insert_feedback, &survivor_key);
    memset(&parent.feedback_window, 0, sizeof(parent.feedback_window));

    struct window_node no_window_parent = {0};
    struct window_node no_window_child = {0};
    g_mouse_state.feedback_node = &no_window_child;
    insert_feedback_adopt(&no_window_parent, &no_window_child);
    TEST_CHECK(g_mouse_state.feedback_node == &no_window_parent, true);

    g_mouse_state.feedback_node = saved_feedback_node;
    table_free(&g_window_manager.insert_feedback);
    memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback));

    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
});

TEST_FUNC(view_remove_window_node_sibling_collapse_requires_destination_reresolve,
{
    struct view view = {0};
    struct window_node *parent = malloc(sizeof(struct window_node));
    struct window_node *left = malloc(sizeof(struct window_node));
    struct window_node *right = malloc(sizeof(struct window_node));
    memset(parent, 0, sizeof(struct window_node));
    memset(left, 0, sizeof(struct window_node));
    memset(right, 0, sizeof(struct window_node));

    view.root = parent;
    parent->left = left;
    parent->right = right;
    left->parent = parent;
    right->parent = parent;

    left->window_list[0] = 61;
    left->window_order[0] = 61;
    left->window_count = 1;

    right->window_list[0] = 62;
    right->window_order[0] = 62;
    right->window_count = 1;

    struct window src_window = { .id = 61 };
    struct window_node *cached_dst_node = right;
    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;
    g_mouse_state.feedback_node = left;

    view_remove_window_node(&view, &src_window);

    struct window_node *resolved_dst_node = view_find_window_node(&view, 62);
    TEST_CHECK(resolved_dst_node == NULL, false);
    TEST_CHECK(resolved_dst_node == cached_dst_node, false);
    TEST_CHECK(resolved_dst_node == view.root, true);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    g_mouse_state.feedback_node = saved_feedback_node;

    free(parent);
});

TEST_FUNC(mouse_drop_action_restore_insertion_point_prefers_sibling_for_singleton_source,
{
    struct window_node parent = {0};
    struct window_node src_node = {0};
    struct window_node dst_node = {0};

    parent.left = &src_node;
    parent.right = &dst_node;
    src_node.parent = &parent;
    dst_node.parent = &parent;

    src_node.window_list[0] = 71;
    src_node.window_order[0] = 71;
    src_node.window_count = 1;

    dst_node.window_list[0] = 72;
    dst_node.window_order[0] = 72;
    dst_node.window_count = 1;

    struct window src_window = { .id = 71 };
    TEST_CHECK(mouse_drop_action_restore_insertion_point(&src_node, &src_window), 72);
});

TEST_FUNC(mouse_drop_action_warp_capture_includes_added_root_after_source_collapse,
{
    struct window_node removed = {0};
    struct window_node added = {0};
    struct window_node parent = {0};
    struct window_node grandparent = {0};

    added.parent = &parent;
    parent.parent = &grandparent;

    TEST_CHECK(mouse_drop_action_warp_should_capture_add(NULL, &added), true);
    TEST_CHECK(mouse_drop_action_warp_should_capture_add(&removed, &added), true);
    TEST_CHECK(mouse_drop_action_warp_should_capture_add(&added, &added), false);
    TEST_CHECK(mouse_drop_action_warp_should_capture_add(&parent, &added), false);
    TEST_CHECK(mouse_drop_action_warp_should_capture_add(&grandparent, &added), false);
});

TEST_FUNC(mouse_drop_action_warp_capacity_preflight_rejects_full_stack_destinations,
{
    struct view dst_view = {0};
    struct window_node dst_root = {0};
    struct window dst_window = { .id = 81 };

    dst_view.layout = VIEW_STACK;
    dst_view.root = &dst_root;

    for (int i = 0; i < NODE_MAX_WINDOW_COUNT; ++i) {
        dst_root.window_list[i] = 81 + i;
        dst_root.window_order[i] = 81 + i;
    }

    dst_root.window_count = NODE_MAX_WINDOW_COUNT;
    TEST_CHECK(mouse_drop_action_warp_capacity_available(&dst_view, &dst_window), false);

    dst_root.window_count = NODE_MAX_WINDOW_COUNT - 1;
    TEST_CHECK(mouse_drop_action_warp_capacity_available(&dst_view, &dst_window), true);
});

TEST_FUNC(mouse_drop_action_warp_capacity_preflight_rejects_full_stack_insert_nodes,
{
    struct view dst_view = {0};
    struct window_node dst_root = {0};
    struct window dst_window = { .id = 91 };

    dst_view.layout = VIEW_BSP;
    dst_view.root = &dst_root;

    for (int i = 0; i < NODE_MAX_WINDOW_COUNT; ++i) {
        dst_root.window_list[i] = 91 + i;
        dst_root.window_order[i] = 91 + i;
    }

    dst_root.window_count = NODE_MAX_WINDOW_COUNT;
    dst_root.insert_dir = STACK;
    TEST_CHECK(mouse_drop_action_warp_capacity_available(&dst_view, &dst_window), false);

    dst_root.insert_dir = DIR_EAST;
    TEST_CHECK(mouse_drop_action_warp_capacity_available(&dst_view, &dst_window), true);
});

TEST_FUNC(mouse_drop_action_rollback_failstop_exits,
{
    int status = 0;
    pid_t pid = fork();

    TEST_CHECK(pid != -1, true);
    if (pid == -1) return result;

    if (pid == 0) {
        mouse_drop_action_fail_rollback("test");
        _exit(EXIT_SUCCESS);
    }

    TEST_CHECK(waitpid(pid, &status, 0), pid);
    TEST_CHECK(WIFEXITED(status), true);
    if (WIFEXITED(status)) {
        TEST_CHECK(WEXITSTATUS(status) == EXIT_FAILURE, true);
    }
});

#define TEST_ENTRY(name) { #name, test_##name },
#define TEST_LIST                                              \
    TEST_ENTRY(display_area_is_in_direction)                   \
    TEST_ENTRY(closest_display_in_direction)                   \
    TEST_ENTRY(window_subrole_is_unknown)                      \
    TEST_ENTRY(window_is_unknown_does_not_consume_cached_subrole) \
    TEST_ENTRY(window_unobserve_without_application_clears_notifications) \
    TEST_ENTRY(update_window_notifications_append_grows_past_fixed_boundary) \
    TEST_ENTRY(window_node_toggle_zoom_parent_switches_siblings) \
    TEST_ENTRY(config_file_execution_preserves_literal_path)     \
    TEST_ENTRY(safe_exec_reports_child_outcomes)                 \
    TEST_ENTRY(socket_open_sets_cloexec)                         \
    TEST_ENTRY(socket_cloexec_helper_sets_flag_and_rejects_invalid_fd) \
    TEST_ENTRY(socket_address_initializer_enforces_unix_path_boundaries) \
    TEST_ENTRY(socket_listener_cleanup_invalidates_descriptor_and_only_unlinks_bound_path) \
    TEST_ENTRY(scripting_addition_socket_identity_requires_strict_nonroot_uid) \
    TEST_ENTRY(scripting_addition_helpers_propagate_failures_and_use_literal_paths) \
    TEST_ENTRY(plist_xml_validator_accepts_xml_characters_and_rejects_invalid_text) \
    TEST_ENTRY(service_install_publishes_complete_plist_atomically) \
    TEST_ENTRY(utf8_validation_and_json_replacement_are_strict)   \
    TEST_ENTRY(serialization_string_escaping_is_strict)          \
    TEST_ENTRY(window_and_space_serializers_round_trip_safe_escaped_and_absent_strings) \
    TEST_ENTRY(view_stack_window_node_rejects_full_node_without_mutation) \
    TEST_ENTRY(view_find_window_list_expands_for_full_stack_node) \
    TEST_ENTRY(mouse_click_state_ordinary_drag_does_not_poison_consumed_click) \
    TEST_ENTRY(mouse_click_state_tracks_chorded_buttons_independently) \
    TEST_ENTRY(mouse_action_classifies_buttons_and_modifiers) \
    TEST_ENTRY(mouse_queued_action_is_immutable_across_configuration_changes) \
    TEST_ENTRY(mouse_unclassified_action_does_not_begin_gesture) \
    TEST_ENTRY(mouse_dragged_unmanaged_window_preserves_owner_gesture) \
    TEST_ENTRY(mouse_drag_owner_button_controls_events_and_reset) \
    TEST_ENTRY(mouse_drag_reset_clears_feedback_window_action_and_button_state) \
    TEST_ENTRY(insert_feedback_destroy_clears_mouse_reference) \
    TEST_ENTRY(insert_feedback_invalid_direction_and_no_action_cleanup) \
    TEST_ENTRY(insert_feedback_rekeys_on_stack_order_mutations) \
    TEST_ENTRY(insert_feedback_rekeys_collapsed_child_before_free) \
    TEST_ENTRY(view_remove_window_node_sibling_collapse_requires_destination_reresolve) \
    TEST_ENTRY(mouse_drop_action_restore_insertion_point_prefers_sibling_for_singleton_source) \
    TEST_ENTRY(mouse_drop_action_warp_capture_includes_added_root_after_source_collapse) \
    TEST_ENTRY(mouse_drop_action_warp_capacity_preflight_rejects_full_stack_destinations) \
    TEST_ENTRY(mouse_drop_action_warp_capacity_preflight_rejects_full_stack_insert_nodes) \
    TEST_ENTRY(mouse_drop_action_rollback_failstop_exits) \
    TEST_ENTRY(space_gesture_encoding)                          \
    TEST_ENTRY(space_gesture_invalid_request)                   \
    TEST_ENTRY(window_ordered_out_leaves_the_tree)              \
    TEST_ENTRY(window_ordered_out_stays_unmanaged)              \
    TEST_ENTRY(window_ordered_in_is_tiled_again)                \
    TEST_ENTRY(window_ordered_in_keeps_float)                   \
    TEST_ENTRY(window_reordered_stays_tiled)                    \
    TEST_ENTRY(window_visible_event_while_still_ordered_out)    \
    TEST_ENTRY(macos_version_newer_than_listed)                 \
    TEST_ENTRY(add_and_clamp_to_zero_saturates)                 \
    TEST_ENTRY(event_loop_begin_thread_creation_failure_is_atomic) \
    TEST_ENTRY(event_queue_grows_past_512k_fifo_and_release_once) \
    TEST_ENTRY(event_queue_wrapped_growth_preserves_fifo_and_release_once) \
    TEST_ENTRY(event_queue_merge_releases_replaced_mouse_move_once) \
    TEST_ENTRY(event_queue_full_releases_owned_context_once)    \
    TEST_ENTRY(event_queue_full_closes_daemon_message_fd_once)  \
    TEST_ENTRY(event_queue_full_failstops_destructive_events)    \
    TEST_ENTRY(daemon_message_admission_bounds_and_reuses_slots) \
    TEST_ENTRY(menu_bar_model_defaults_and_zero_counts)          \
    TEST_ENTRY(menu_bar_model_classification_cases)              \
    TEST_ENTRY(menu_bar_model_filter_focus_identity_and_failure_metadata) \
    TEST_ENTRY(menu_bar_model_deep_copy_clone_independence_unicode_and_long_labels) \
    TEST_ENTRY(menu_bar_model_invalid_inputs_and_absent_focus_neutrality) \
    TEST_ENTRY(menu_bar_model_exact_effective_equality_and_focus_only_changes) \
    TEST_ENTRY(menu_bar_manager_config_value_parsing)                  \
    TEST_ENTRY(menu_bar_manager_application_inventory_lifecycle_boundaries) \
    TEST_ENTRY(window_manager_scratchpad_refresh_routes_inventory_changes) \
    TEST_ENTRY(menu_bar_manager_ignores_setting_actions_after_disable) \
    TEST_ENTRY(menu_bar_manager_rejects_old_lifecycle_but_accepts_valid_setting_generation) \
    TEST_ENTRY(menu_bar_manager_preserves_action_focus_during_async_menu_open) \
    TEST_ENTRY(menu_bar_manager_late_close_after_disable_does_not_overemit) \
    TEST_ENTRY(menu_bar_manager_tracked_membership_failure_uses_raw_or_unknown) \
    TEST_ENTRY(message_utf8_ingress_validation)                            \
    TEST_ENTRY(message_numeric_token_boundaries)                          \
    TEST_ENTRY(message_hexadecimal_token_boundaries)                      \
    TEST_ENTRY(message_duration_values_must_be_finite_and_nonnegative)    \
    TEST_ENTRY(message_window_grid_numeric_validation)                    \
    TEST_ENTRY(message_rule_grid_numeric_validation)                      \
    TEST_ENTRY(message_rule_opacity_numeric_validation)                   \
    TEST_ENTRY(message_external_bar_parser_is_strict)                     \
    TEST_ENTRY(message_space_padding_parser_is_strict)                    \
    TEST_ENTRY(message_space_gap_parser_is_strict)                        \
    TEST_ENTRY(message_window_float_commands_are_strict)                 \
    TEST_ENTRY(daemon_message_signed_boundaries_and_suffixes)             \
    TEST_ENTRY(daemon_message_incomplete_frame_has_failure_response)      \
    TEST_ENTRY(daemon_message_complete_invalid_frames_have_failure_responses) \
    TEST_ENTRY(daemon_response_buffer_preserves_exact_bytes)              \
    TEST_ENTRY(daemon_response_buffer_accepts_empty_and_boundary_responses) \
    TEST_ENTRY(daemon_response_buffer_overflow_selects_failure_response)  \
    TEST_ENTRY(socket_io_fragmented_exact_read)                           \
    TEST_ENTRY(socket_io_write_all_delivers_fragmented_buffer)            \
    TEST_ENTRY(daemon_response_large_buffer_reaches_draining_peer)        \
    TEST_ENTRY(daemon_response_stalled_peer_has_bounded_write_failure)    \
    TEST_ENTRY(socket_io_write_all_times_out_with_blocking_descriptor_and_stalled_peer) \
    TEST_ENTRY(socket_io_write_all_rejects_peer_close_without_sigpipe)    \
    TEST_ENTRY(socket_io_rejects_early_eof_and_total_timeout)             \
    TEST_ENTRY(client_response_stalled_peer_times_out)                    \
    TEST_ENTRY(client_response_fragmented_stream_refreshes_inactivity_deadline) \
    TEST_ENTRY(client_response_fragmented_failure_routes_everything_to_stderr) \
    TEST_ENTRY(client_response_later_failure_marker_and_embedded_nul_remain_stdout) \
    TEST_ENTRY(client_response_eof_completes_successfully)                \
    TEST_ENTRY(sa_handshake_accepts_fragmented_version_and_attributes)    \
    TEST_ENTRY(sa_handshake_accepts_empty_version)                        \
    TEST_ENTRY(sa_handshake_rejects_missing_terminator_at_capacity)       \
    TEST_ENTRY(sa_handshake_rejects_truncated_attributes)                 \
    TEST_ENTRY(sa_handshake_rejects_early_eof)                            \
    TEST_ENTRY(sa_handshake_observes_total_timeout)                       \
    TEST_ENTRY(sa_request_without_status_observes_total_timeout)          \
    TEST_ENTRY(sa_payload_builder_boundaries_and_variable_counts)         \
    TEST_ENTRY(sa_message_fixed_sender_layouts_are_strict)                \
    TEST_ENTRY(sa_message_opacity_fade_duration_is_validated)             \
    TEST_ENTRY(sa_message_swap_sparse_and_mixed_records_are_strict)       \
    TEST_ENTRY(sa_message_variable_counts_and_entries_are_strict)

static struct {
    char *name;
    test_function *func;
} tests[] = {
    TEST_LIST
};

int main(int argc, char **argv)
{
    int succeeded = 0;
    int failed = 0;
    int total = array_count(tests);
    printf("\e[1;34m -- Running %d tests\e[m\n\n", total);

    uint64_t cpu_freq  = read_cpu_freq();
    uint64_t begin_tsc = read_cpu_timer();

    for (int i = 0; i < total; ++i) {
        uint64_t tsc = read_cpu_timer();
        bool result = tests[i].func();
        double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - tsc) / (double)cpu_freq;

        printf("(%0.4fms) %s \e[1;33m%s\e[m\n", ms_elapsed, result ? "\e[1;32msuccess\e[m" : " \e[1;31mfailed\e[m", tests[i].name);
        if (result) ++succeeded; else ++failed;
    }

    test_suite_teardown();

    double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - begin_tsc) / (double)cpu_freq;
    printf("\n\e[1;34m -- Completed (%0.4fms)\e[m\n", ms_elapsed);
    printf("\t%d \e[1;32msucceeded\e[m\n", succeeded);
    printf("\t%d \e[1;31mfailed\e[m\n", failed);
    printf("\t%d \e[1;33mtotal\e[m\n", total);

    return total == succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
