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
    TEST_ENTRY(window_node_toggle_zoom_parent_switches_siblings) \
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
