#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

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

TEST_FUNC(serialization_string_escaping_is_strict,
{
    char xml_input[] = "amp& lt< gt> quote\" apos'";
    char *xml_escaped = plist_xml_escape(xml_input);
    TEST_CHECK(xml_escaped != NULL, true);
    TEST_CHECK(strcmp(xml_escaped, "amp&amp; lt&lt; gt&gt; quote&quot; apos&apos;") == 0, true);
    free(xml_escaped);

    __auto_type saved_temp_storage = g_temp_storage;
    memset(&g_temp_storage, 0, sizeof(g_temp_storage));

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

    g_temp_storage = saved_temp_storage;
    if (!g_temp_storage.memory) {
        TEST_CHECK(ts_init(KILOBYTES(64)), true);
    }

    g_temp_storage.used = 0;
    char json_input[] = "quote\" backslash\\ newline\n tab\t control\x01";
    char *json_escaped = ts_string_escape(json_input);
    TEST_CHECK(strcmp(json_escaped, "quote\\\" backslash\\\\ newline\\n tab\\t control\\u0001") == 0, true);
    char expected_json[256];
    snprintf(expected_json, sizeof(expected_json), "%s", json_escaped);
    NSString *json_original = [NSString stringWithUTF8String:json_input];

    g_temp_storage.used = 0;
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
    ts_init(4096);
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
});

TEST_FUNC(insert_feedback_rekeys_on_stack_order_mutations,
{
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
});

TEST_FUNC(insert_feedback_rekeys_collapsed_child_before_free,
{
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

    insert_feedback_adopt(&parent, &child);

    uint32_t survivor_key = 52;
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &survivor_key) == &parent, true);
    TEST_CHECK(child.feedback_window.id, 0);
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

    view_remove_window_node(&view, &src_window);

    struct window_node *resolved_dst_node = view_find_window_node(&view, 62);
    TEST_CHECK(resolved_dst_node == NULL, false);
    TEST_CHECK(resolved_dst_node == cached_dst_node, false);
    TEST_CHECK(resolved_dst_node == view.root, true);

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
    TEST_ENTRY(service_install_publishes_complete_plist_atomically) \
    TEST_ENTRY(serialization_string_escaping_is_strict)          \
    TEST_ENTRY(view_stack_window_node_rejects_full_node_without_mutation) \
    TEST_ENTRY(view_find_window_list_expands_for_full_stack_node) \
    TEST_ENTRY(insert_feedback_rekeys_on_stack_order_mutations) \
    TEST_ENTRY(insert_feedback_rekeys_collapsed_child_before_free) \
    TEST_ENTRY(view_remove_window_node_sibling_collapse_requires_destination_reresolve) \
    TEST_ENTRY(mouse_drop_action_restore_insertion_point_prefers_sibling_for_singleton_source) \
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
    TEST_ENTRY(menu_bar_manager_ignores_setting_actions_after_disable) \
    TEST_ENTRY(menu_bar_manager_rejects_old_lifecycle_but_accepts_valid_setting_generation) \
    TEST_ENTRY(menu_bar_manager_preserves_action_focus_during_async_menu_open) \
    TEST_ENTRY(menu_bar_manager_late_close_after_disable_does_not_overemit) \
    TEST_ENTRY(menu_bar_manager_tracked_membership_failure_uses_raw_or_unknown) \
    TEST_ENTRY(message_numeric_token_boundaries)                          \
    TEST_ENTRY(message_hexadecimal_token_boundaries)                      \
    TEST_ENTRY(message_duration_values_must_be_finite_and_nonnegative)    \
    TEST_ENTRY(message_window_grid_numeric_validation)                    \
    TEST_ENTRY(message_rule_grid_numeric_validation)                      \
    TEST_ENTRY(message_direct_float_components_reject_non_finite)         \
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

    double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - begin_tsc) / (double)cpu_freq;
    printf("\n\e[1;34m -- Completed (%0.4fms)\e[m\n", ms_elapsed);
    printf("\t%d \e[1;32msucceeded\e[m\n", succeeded);
    printf("\t%d \e[1;31mfailed\e[m\n", failed);
    printf("\t%d \e[1;33mtotal\e[m\n", total);

    return total == succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
