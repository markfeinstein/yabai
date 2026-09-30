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
#include "space_gesture.c"
#include "window_ordered.c"
#include "event_queue.c"
#include "menu_bar_model.c"
#include "menu_bar_manager.c"
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

#define TEST_ENTRY(name) { #name, test_##name },
#define TEST_LIST                                              \
    TEST_ENTRY(display_area_is_in_direction)                   \
    TEST_ENTRY(closest_display_in_direction)                   \
    TEST_ENTRY(window_subrole_is_unknown)                      \
    TEST_ENTRY(window_is_unknown_does_not_consume_cached_subrole) \
    TEST_ENTRY(window_node_toggle_zoom_parent_switches_siblings) \
    TEST_ENTRY(serialization_string_escaping_is_strict)          \
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
    TEST_ENTRY(daemon_message_signed_boundaries_and_suffixes)             \
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
