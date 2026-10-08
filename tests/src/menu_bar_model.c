/*
 * Standalone model test command (from repository root):
 * clang -std=c11 -Wall -Wextra -Werror -DMENU_BAR_MODEL_TEST_STANDALONE tests/src/menu_bar_model.c -o /tmp/yabai-menu-bar-model-test && /tmp/yabai-menu-bar-model-test
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MENU_BAR_MODEL_TEST_STANDALONE
#include "../../src/menu_bar_model.c"
#else
#include "../../src/menu_bar_model.h"
#endif

#ifndef TEST_CHECK
#define MENU_BAR_MODEL_TEST_OWNS_HARNESS 1
#define TEST_CHECK(r, e) do { long long actual__ = (long long) (r); long long expected__ = (long long) (e); if (actual__ != expected__) { printf("                   %s#%d %s == %s (%lld == %lld)\n", test_name, __LINE__, #r, #e, actual__, expected__); result = false; } } while (0)
#endif

#define MENU_BAR_MODEL_TEST_BEGIN(name) static bool test_##name(void) { const char *test_name = #name; bool result = true;
#define MENU_BAR_MODEL_TEST_END return result; }
#define TEST_CHECK_STRING(r, e) do { const char *actual__ = (r); const char *expected__ = (e); if (!actual__ || !expected__ || strcmp(actual__, expected__) != 0) { printf("                   %s#%d %s == %s (%s == %s)\n", test_name, __LINE__, #r, #e, actual__ ? actual__ : "(null)", expected__ ? expected__ : "(null)"); result = false; } } while (0)
#define TEST_CHECK_NULL(r) TEST_CHECK((r) == NULL, true)
#define TEST_CHECK_NOT_NULL(r) TEST_CHECK((r) != NULL, true)

static struct menu_bar_settings test_settings(enum menu_bar_enabled enabled,
                                              enum menu_bar_style style,
                                              enum menu_bar_space_filter filter)
{
    struct menu_bar_settings settings;
    settings.enabled = enabled;
    settings.style = style;
    settings.space_filter = filter;
    return settings;
}

static struct menu_bar_snapshot *test_create_two_display_snapshot(uint64_t generation,
                                                                  uint32_t focused_display_id,
                                                                  const char *focused_display_uuid,
                                                                  uint64_t failure_generation,
                                                                  const char *failure_text)
{
    static const struct menu_bar_app_summary_input apps[] = {
        { "Terminal", 2 },
        { "Safari", 1 }
    };

    static const struct menu_bar_space_input display_one_spaces[] = {
        { 100, 1, "empty", false, false, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 },
        { 101, 2, "visible-empty", false, true, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 },
        { 102, 3, "occupied", false, false, MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED, apps, 2 }
    };

    static const struct menu_bar_space_input display_two_spaces[] = {
        { 200, 4, "unknown", false, false, MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN, NULL, 0 },
        { 201, 5, "inactive-empty", false, false, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 }
    };

    static const struct menu_bar_display_input displays[] = {
        { 10, "display-a", 0, "A", "Built-in", display_one_spaces, 3 },
        { 20, "display-b", 1, "B", "Studio", display_two_spaces, 2 }
    };

    return menu_bar_model_snapshot_create(generation,
                                          test_settings(MENU_BAR_ENABLED, MENU_BAR_STYLE_EXPANDED, MENU_BAR_SPACE_FILTER_ALL),
                                          displays, 2,
                                          true, focused_display_id, focused_display_uuid,
                                          failure_generation, failure_text);
}

MENU_BAR_MODEL_TEST_BEGIN(menu_bar_model_defaults_and_zero_counts)
    struct menu_bar_settings defaults = {0};
    struct menu_bar_snapshot *empty = menu_bar_model_snapshot_create(1, defaults, NULL, 0, false, 0, NULL, 0, NULL);
    TEST_CHECK_NOT_NULL(empty);
    if (empty) {
        TEST_CHECK(empty->settings.enabled, MENU_BAR_DISABLED);
        TEST_CHECK(empty->settings.style, MENU_BAR_STYLE_EXPANDED);
        TEST_CHECK(empty->settings.space_filter, MENU_BAR_SPACE_FILTER_OCCUPIED);
        TEST_CHECK(empty->display_count, 0);
        TEST_CHECK(empty->has_focused_display, false);
    }

    static const struct menu_bar_display_input displays[] = {
        { 1, "uuid", 0, NULL, "Display", NULL, 0 }
    };
    struct menu_bar_snapshot *zero_spaces = menu_bar_model_snapshot_create(2, defaults, displays, 1, false, 0, NULL, 0, NULL);
    TEST_CHECK_NOT_NULL(zero_spaces);
    if (zero_spaces) {
        TEST_CHECK(zero_spaces->display_count, 1);
        TEST_CHECK(zero_spaces->displays[0].space_count, 0);
        TEST_CHECK(zero_spaces->displays[0].spaces == NULL, true);
    }

    struct menu_bar_snapshot *clone = menu_bar_model_snapshot_clone(zero_spaces);
    TEST_CHECK_NOT_NULL(clone);
    TEST_CHECK(menu_bar_model_snapshot_equal(zero_spaces, clone, MENU_BAR_MODEL_COMPARE_EXACT), true);

    menu_bar_model_snapshot_destroy(clone);
    menu_bar_model_snapshot_destroy(zero_spaces);
    menu_bar_model_snapshot_destroy(empty);
    menu_bar_model_snapshot_destroy(NULL);
MENU_BAR_MODEL_TEST_END

MENU_BAR_MODEL_TEST_BEGIN(menu_bar_model_classification_cases)
    struct menu_bar_window_input normal = { 1, "App", 0 };
    struct menu_bar_window_input floating = { 1, "App", MENU_BAR_WINDOW_FLOATING };
    struct menu_bar_window_input unmanaged = { 1, "App", MENU_BAR_WINDOW_UNMANAGED };
    struct menu_bar_window_input hidden = { 1, "App", MENU_BAR_WINDOW_HIDDEN };
    struct menu_bar_window_input minimized = { 1, "App", MENU_BAR_WINDOW_MINIMIZED };
    struct menu_bar_window_input fullscreen = { 1, "App", MENU_BAR_WINDOW_NATIVE_FULLSCREEN };
    struct menu_bar_window_input sticky = { 1, "App", MENU_BAR_WINDOW_STICKY };
    struct menu_bar_window_input system = { 1, "Dock", MENU_BAR_WINDOW_SYSTEM };
    struct menu_bar_window_input transient = { 1, "Tip", MENU_BAR_WINDOW_TRANSIENT };
    struct menu_bar_window_input unknown = { 0, "App", MENU_BAR_WINDOW_MEMBERSHIP_UNKNOWN };
    struct menu_bar_window_input invalid_zero_space = { 0, "App", 0 };
    struct menu_bar_window_input invalid_flag = { 1, "App", 1u << 30 };

    TEST_CHECK(menu_bar_model_classify_window(&normal), MENU_BAR_WINDOW_OCCUPANCY_COUNTED);
    TEST_CHECK(menu_bar_model_classify_window(&floating), MENU_BAR_WINDOW_OCCUPANCY_COUNTED);
    TEST_CHECK(menu_bar_model_classify_window(&unmanaged), MENU_BAR_WINDOW_OCCUPANCY_COUNTED);
    TEST_CHECK(menu_bar_model_classify_window(&hidden), MENU_BAR_WINDOW_OCCUPANCY_COUNTED);
    TEST_CHECK(menu_bar_model_classify_window(&minimized), MENU_BAR_WINDOW_OCCUPANCY_COUNTED);
    TEST_CHECK(menu_bar_model_classify_window(&fullscreen), MENU_BAR_WINDOW_OCCUPANCY_COUNTED);
    TEST_CHECK(menu_bar_model_classify_window(&sticky), MENU_BAR_WINDOW_OCCUPANCY_EXCLUDED);
    TEST_CHECK(menu_bar_model_classify_window(&system), MENU_BAR_WINDOW_OCCUPANCY_EXCLUDED);
    TEST_CHECK(menu_bar_model_classify_window(&transient), MENU_BAR_WINDOW_OCCUPANCY_EXCLUDED);
    TEST_CHECK(menu_bar_model_classify_window(&unknown), MENU_BAR_WINDOW_OCCUPANCY_UNKNOWN);
    TEST_CHECK(menu_bar_model_classify_window(&invalid_zero_space), MENU_BAR_WINDOW_OCCUPANCY_UNKNOWN);
    TEST_CHECK(menu_bar_model_classify_window(&invalid_flag), MENU_BAR_WINDOW_OCCUPANCY_UNKNOWN);
    TEST_CHECK(menu_bar_model_classify_window(NULL), MENU_BAR_WINDOW_OCCUPANCY_UNKNOWN);

    struct menu_bar_window_input counted_wins[] = { unknown, sticky, normal };
    struct menu_bar_window_input unknown_wins_empty[] = { sticky, transient, unknown };
    struct menu_bar_window_input excluded_only[] = { sticky, system, transient };

    TEST_CHECK(menu_bar_model_classify_space_membership(counted_wins, 3), MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED);
    TEST_CHECK(menu_bar_model_classify_space_membership(unknown_wins_empty, 3), MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN);
    TEST_CHECK(menu_bar_model_classify_space_membership(excluded_only, 3), MENU_BAR_SPACE_MEMBERSHIP_EMPTY);
    TEST_CHECK(menu_bar_model_classify_space_membership(NULL, 0), MENU_BAR_SPACE_MEMBERSHIP_EMPTY);
    TEST_CHECK(menu_bar_model_classify_space_membership(NULL, 1), MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN);
MENU_BAR_MODEL_TEST_END

MENU_BAR_MODEL_TEST_BEGIN(menu_bar_model_filter_focus_identity_and_failure_metadata)
    struct menu_bar_snapshot *snapshot = test_create_two_display_snapshot(7, 20, "display-b", 99, "focus failed");
    TEST_CHECK_NOT_NULL(snapshot);
    if (snapshot) {
        TEST_CHECK(snapshot->has_focused_display, true);
        TEST_CHECK(snapshot->focused_display_id, 20);
        TEST_CHECK_STRING(snapshot->focused_display_uuid, "display-b");
        TEST_CHECK(snapshot->displays[0].has_focus, false);
        TEST_CHECK(snapshot->displays[1].has_focus, true);
    }

    struct menu_bar_snapshot *occupied = menu_bar_model_snapshot_filtered_clone(snapshot, MENU_BAR_SPACE_FILTER_OCCUPIED);
    TEST_CHECK_NOT_NULL(occupied);
    if (occupied) {
        TEST_CHECK(occupied->settings.space_filter, MENU_BAR_SPACE_FILTER_OCCUPIED);
        TEST_CHECK(occupied->generation, 7);
        TEST_CHECK(occupied->action_failure_generation, 99);
        TEST_CHECK_STRING(occupied->action_failure_text, "focus failed");
        TEST_CHECK(occupied->has_focused_display, true);
        TEST_CHECK(occupied->focused_display_id, 20);
        TEST_CHECK_STRING(occupied->focused_display_uuid, "display-b");
        TEST_CHECK(occupied->display_count, 2);
        TEST_CHECK(occupied->displays[0].display_id, 10);
        TEST_CHECK(occupied->displays[0].space_count, 2);
        TEST_CHECK(occupied->displays[0].spaces[0].space_id, 101);
        TEST_CHECK(occupied->displays[0].spaces[1].space_id, 102);
        TEST_CHECK(occupied->displays[1].display_id, 20);
        TEST_CHECK(occupied->displays[1].has_focus, true);
        TEST_CHECK(occupied->displays[1].space_count, 1);
        TEST_CHECK(occupied->displays[1].spaces[0].space_id, 200);
    }

    struct menu_bar_snapshot *all = menu_bar_model_snapshot_filtered_clone(snapshot, MENU_BAR_SPACE_FILTER_ALL);
    TEST_CHECK_NOT_NULL(all);
    if (all) {
        TEST_CHECK(all->settings.space_filter, MENU_BAR_SPACE_FILTER_ALL);
        TEST_CHECK(all->displays[0].space_count, 3);
        TEST_CHECK(all->displays[1].space_count, 2);
        TEST_CHECK(all->displays[0].spaces[0].space_id, 100);
        TEST_CHECK(all->displays[1].spaces[1].space_id, 201);
    }

    TEST_CHECK(menu_bar_model_space_matches_filter(NULL, MENU_BAR_SPACE_FILTER_ALL), false);
    TEST_CHECK_NULL(menu_bar_model_snapshot_filtered_clone(snapshot, (enum menu_bar_space_filter) 99));

    menu_bar_model_snapshot_destroy(all);
    menu_bar_model_snapshot_destroy(occupied);
    menu_bar_model_snapshot_destroy(snapshot);
MENU_BAR_MODEL_TEST_END

MENU_BAR_MODEL_TEST_BEGIN(menu_bar_model_refocused_clone_preserves_inventory)
    struct menu_bar_snapshot *snapshot = test_create_two_display_snapshot(7, 10, "display-a", 99, "focus failed");
    struct menu_bar_snapshot *refocused = menu_bar_model_snapshot_refocused_clone(snapshot, 8, true, 20, "display-b");
    TEST_CHECK_NOT_NULL(snapshot);
    TEST_CHECK_NOT_NULL(refocused);
    if (snapshot && refocused) {
        TEST_CHECK(snapshot->generation, 7);
        TEST_CHECK(snapshot->focused_display_id, 10);
        TEST_CHECK(snapshot->displays[0].has_focus, true);
        TEST_CHECK(snapshot->displays[1].has_focus, false);

        TEST_CHECK(refocused->generation, 8);
        TEST_CHECK(refocused->focused_display_id, 20);
        TEST_CHECK_STRING(refocused->focused_display_uuid, "display-b");
        TEST_CHECK(refocused->displays[0].has_focus, false);
        TEST_CHECK(refocused->displays[1].has_focus, true);
        TEST_CHECK(refocused->action_failure_generation, 99);
        TEST_CHECK_STRING(refocused->action_failure_text, "focus failed");
        TEST_CHECK(refocused->display_count, snapshot->display_count);
        TEST_CHECK(refocused->displays[0].spaces[2].space_id, 102);
        TEST_CHECK(refocused->displays[0].spaces[2].app_count, 2);
        TEST_CHECK_STRING(refocused->displays[0].spaces[2].apps[0].name, "Terminal");
        TEST_CHECK(refocused->displays != snapshot->displays, true);
    }

    struct menu_bar_snapshot *unresolved = menu_bar_model_snapshot_refocused_clone(snapshot, 9, true, 99, "absent");
    TEST_CHECK_NOT_NULL(unresolved);
    if (unresolved) {
        TEST_CHECK(unresolved->generation, 9);
        TEST_CHECK(unresolved->has_focused_display, false);
        TEST_CHECK(unresolved->focused_display_id, 0);
        TEST_CHECK(unresolved->focused_display_uuid == NULL, true);
        TEST_CHECK(unresolved->displays[0].has_focus, false);
        TEST_CHECK(unresolved->displays[1].has_focus, false);
    }
    TEST_CHECK_NULL(menu_bar_model_snapshot_refocused_clone(NULL, 10, false, 0, NULL));

    menu_bar_model_snapshot_destroy(unresolved);
    menu_bar_model_snapshot_destroy(refocused);
    menu_bar_model_snapshot_destroy(snapshot);
MENU_BAR_MODEL_TEST_END

MENU_BAR_MODEL_TEST_BEGIN(menu_bar_model_deep_copy_clone_independence_unicode_and_long_labels)
    char display_uuid[32] = "deep-copy-display";
    char display_label[32] = "Original Display";
    char display_name[32] = "Studio 🖥";
    char space_label[512];
    char app_name[64] = "Terminal 🧪";

    memset(space_label, 'x', sizeof(space_label));
    space_label[0] = 'W';
    space_label[1] = 'o';
    space_label[2] = 'r';
    space_label[3] = 'k';
    space_label[4] = ' ';
    space_label[5] = '\0';
    strcat(space_label, "長いラベル ");
    size_t prefix_length = strlen(space_label);
    for (size_t index = prefix_length; index + 1 < sizeof(space_label); ++index) space_label[index] = (char) ('a' + (index % 26));
    space_label[sizeof(space_label) - 1] = '\0';

    char expected_label[sizeof(space_label)];
    strcpy(expected_label, space_label);

    struct menu_bar_app_summary_input apps[] = {
        { NULL, 3 },
        { app_name, 4 }
    };
    struct menu_bar_space_input spaces[] = {
        { 42, 6, space_label, true, true, MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED, apps, 2 }
    };
    struct menu_bar_display_input displays[] = {
        { 77, display_uuid, 4, display_label, display_name, spaces, 1 }
    };

    struct menu_bar_snapshot *snapshot = menu_bar_model_snapshot_create(11, (struct menu_bar_settings) {0}, displays, 1, true, 77, display_uuid, 0, NULL);
    TEST_CHECK_NOT_NULL(snapshot);

    strcpy(display_uuid, "mutated-display");
    strcpy(display_label, "Mutated Label");
    strcpy(display_name, "Mutated Name");
    strcpy(space_label, "mutated-space");
    strcpy(app_name, "Mutated App");

    if (snapshot) {
        TEST_CHECK_STRING(snapshot->displays[0].uuid, "deep-copy-display");
        TEST_CHECK_STRING(snapshot->focused_display_uuid, "deep-copy-display");
        TEST_CHECK_STRING(snapshot->displays[0].label, "Original Display");
        TEST_CHECK_STRING(snapshot->displays[0].name, "Studio 🖥");
        TEST_CHECK_STRING(snapshot->displays[0].spaces[0].label, expected_label);
        TEST_CHECK(snapshot->displays[0].spaces[0].is_native_fullscreen, true);
        TEST_CHECK(snapshot->displays[0].spaces[0].app_count, 1);
        TEST_CHECK_STRING(snapshot->displays[0].spaces[0].apps[0].name, "Terminal 🧪");
        TEST_CHECK(snapshot->displays[0].spaces[0].apps[0].window_count, 4);
    }

    struct menu_bar_snapshot *clone = menu_bar_model_snapshot_clone(snapshot);
    TEST_CHECK_NOT_NULL(clone);
    TEST_CHECK(menu_bar_model_snapshot_equal(snapshot, clone, MENU_BAR_MODEL_COMPARE_EXACT), true);

    menu_bar_model_snapshot_destroy(snapshot);
    if (clone) {
        TEST_CHECK_STRING(clone->displays[0].uuid, "deep-copy-display");
        TEST_CHECK_STRING(clone->displays[0].spaces[0].label, expected_label);
        TEST_CHECK_STRING(clone->displays[0].spaces[0].apps[0].name, "Terminal 🧪");
    }
    menu_bar_model_snapshot_destroy(clone);
MENU_BAR_MODEL_TEST_END

MENU_BAR_MODEL_TEST_BEGIN(menu_bar_model_invalid_inputs_and_absent_focus_neutrality)
    struct menu_bar_settings invalid_enabled = test_settings((enum menu_bar_enabled) 2, MENU_BAR_STYLE_COMPACT, MENU_BAR_SPACE_FILTER_OCCUPIED);
    struct menu_bar_settings invalid_style = test_settings(MENU_BAR_ENABLED, (enum menu_bar_style) 2, MENU_BAR_SPACE_FILTER_OCCUPIED);
    struct menu_bar_settings invalid_filter = test_settings(MENU_BAR_ENABLED, MENU_BAR_STYLE_COMPACT, (enum menu_bar_space_filter) 2);

    TEST_CHECK_NULL(menu_bar_model_snapshot_create(1, invalid_enabled, NULL, 0, false, 0, NULL, 0, NULL));
    TEST_CHECK_NULL(menu_bar_model_snapshot_create(1, invalid_style, NULL, 0, false, 0, NULL, 0, NULL));
    TEST_CHECK_NULL(menu_bar_model_snapshot_create(1, invalid_filter, NULL, 0, false, 0, NULL, 0, NULL));
    TEST_CHECK_NULL(menu_bar_model_snapshot_create(1, (struct menu_bar_settings) {0}, NULL, 1, false, 0, NULL, 0, NULL));

    static const struct menu_bar_display_input bad_space_array[] = {
        { 1, "display", 0, NULL, NULL, NULL, 1 }
    };
    TEST_CHECK_NULL(menu_bar_model_snapshot_create(1, (struct menu_bar_settings) {0}, bad_space_array, 1, false, 0, NULL, 0, NULL));

    static const struct menu_bar_space_input bad_app_space[] = {
        { 1, 1, NULL, false, false, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 1 }
    };
    static const struct menu_bar_display_input bad_app_array[] = {
        { 1, "display", 0, NULL, NULL, bad_app_space, 1 }
    };
    TEST_CHECK_NULL(menu_bar_model_snapshot_create(1, (struct menu_bar_settings) {0}, bad_app_array, 1, false, 0, NULL, 0, NULL));

    static const struct menu_bar_space_input bad_membership_space[] = {
        { 1, 1, NULL, false, false, (enum menu_bar_space_membership) 99, NULL, 0 }
    };
    static const struct menu_bar_display_input bad_membership_array[] = {
        { 1, "display", 0, NULL, NULL, bad_membership_space, 1 }
    };
    TEST_CHECK_NULL(menu_bar_model_snapshot_create(1, (struct menu_bar_settings) {0}, bad_membership_array, 1, false, 0, NULL, 0, NULL));

    static const struct menu_bar_display_input displays[] = {
        { 1, "present", 0, NULL, NULL, NULL, 0 }
    };
    struct menu_bar_snapshot *neutral = menu_bar_model_snapshot_create(3, (struct menu_bar_settings) {0}, displays, 1, true, 99, "absent", 0, NULL);
    TEST_CHECK_NOT_NULL(neutral);
    if (neutral) {
        TEST_CHECK(neutral->has_focused_display, false);
        TEST_CHECK(neutral->focused_display_id, 0);
        TEST_CHECK(neutral->focused_display_uuid == NULL, true);
        TEST_CHECK(neutral->displays[0].has_focus, false);
    }
    menu_bar_model_snapshot_destroy(neutral);
MENU_BAR_MODEL_TEST_END

MENU_BAR_MODEL_TEST_BEGIN(menu_bar_model_exact_effective_equality_and_focus_only_changes)
    struct menu_bar_snapshot *left = test_create_two_display_snapshot(1, 10, "display-a", 10, "old failure");
    struct menu_bar_snapshot *different_generation_and_failure = test_create_two_display_snapshot(2, 10, "display-a", 11, "new failure");
    struct menu_bar_snapshot *different_focus = test_create_two_display_snapshot(1, 20, "display-b", 10, "old failure");
    TEST_CHECK_NOT_NULL(left);
    TEST_CHECK_NOT_NULL(different_generation_and_failure);
    TEST_CHECK_NOT_NULL(different_focus);

    TEST_CHECK(menu_bar_model_snapshot_equal(left, different_generation_and_failure, 0), true);
    TEST_CHECK(menu_bar_model_snapshot_equal(left, different_generation_and_failure, MENU_BAR_MODEL_COMPARE_GENERATION), false);
    TEST_CHECK(menu_bar_model_snapshot_equal(left, different_generation_and_failure, MENU_BAR_MODEL_COMPARE_ACTION_FAILURE), false);
    TEST_CHECK(menu_bar_model_snapshot_equal(left, different_generation_and_failure, MENU_BAR_MODEL_COMPARE_EXACT), false);
    TEST_CHECK(menu_bar_model_snapshot_equal(left, different_focus, 0), false);

    static const struct menu_bar_space_input renamed_space[] = {
        { 100, 1, "renamed", false, false, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 },
        { 101, 2, "visible-empty", false, true, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 },
        { 102, 3, "occupied", false, false, MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED, NULL, 0 }
    };
    static const struct menu_bar_display_input renamed_display[] = {
        { 10, "display-a", 0, "A", "Built-in", renamed_space, 3 },
        { 20, "display-b", 1, "B", "Studio", NULL, 0 }
    };
    struct menu_bar_snapshot *different_content = menu_bar_model_snapshot_create(1,
                                                                                test_settings(MENU_BAR_ENABLED, MENU_BAR_STYLE_EXPANDED, MENU_BAR_SPACE_FILTER_ALL),
                                                                                renamed_display, 2,
                                                                                true, 10, "display-a",
                                                                                10, "old failure");
    TEST_CHECK_NOT_NULL(different_content);
    TEST_CHECK(menu_bar_model_snapshot_equal(left, different_content, 0), false);
    TEST_CHECK(menu_bar_model_snapshot_equal(left, different_content, 1u << 29), false);
    TEST_CHECK(menu_bar_model_snapshot_equal(NULL, NULL, 0), true);
    TEST_CHECK(menu_bar_model_snapshot_equal(left, NULL, 0), false);

    menu_bar_model_snapshot_destroy(different_content);
    menu_bar_model_snapshot_destroy(different_focus);
    menu_bar_model_snapshot_destroy(different_generation_and_failure);
    menu_bar_model_snapshot_destroy(left);
MENU_BAR_MODEL_TEST_END

#define TEST_ENTRY(name) { #name, test_##name }

#ifdef MENU_BAR_MODEL_TEST_OWNS_HARNESS
static struct {
    const char *name;
    bool (*func)(void);
} tests[] = {
    TEST_ENTRY(menu_bar_model_defaults_and_zero_counts),
    TEST_ENTRY(menu_bar_model_classification_cases),
    TEST_ENTRY(menu_bar_model_filter_focus_identity_and_failure_metadata),
    TEST_ENTRY(menu_bar_model_refocused_clone_preserves_inventory),
    TEST_ENTRY(menu_bar_model_deep_copy_clone_independence_unicode_and_long_labels),
    TEST_ENTRY(menu_bar_model_invalid_inputs_and_absent_focus_neutrality),
    TEST_ENTRY(menu_bar_model_exact_effective_equality_and_focus_only_changes)
};

int main(void)
{
    size_t succeeded = 0;
    size_t total = sizeof(tests) / sizeof(tests[0]);

    printf(" -- Running %zu menu_bar_model tests\n\n", total);
    for (size_t index = 0; index < total; ++index) {
        bool result = tests[index].func();
        printf("%s %s\n", result ? "success" : "failed ", tests[index].name);
        if (result) ++succeeded;
    }

    printf("\n -- Completed: %zu/%zu succeeded\n", succeeded, total);
    return succeeded == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif
