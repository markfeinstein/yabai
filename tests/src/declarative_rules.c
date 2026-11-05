static struct declarative_rule_display_entry declarative_rule_test_displays[] = {
    { 10, "display-a", { 100, 50 }, 0 },
    { 20, "display-b", { 100, 50 }, 1 },
    { 30, "display-c", { 50, 100 }, 2 }
};
static struct declarative_rule_space_entry declarative_rule_test_spaces[] = {
    { 101, "space-a", 0, 0, 10, "display-a", 0 },
    { 102, "space-b", 4, 1, 20, "display-b", 1 },
    { 103, "space-c", 0, 2, 30, "display-c", 2 }
};
static const struct declarative_rule_topology_snapshot declarative_rule_test_topology = {
    declarative_rule_test_displays, 3, declarative_rule_test_spaces, 3
};

struct declarative_rule_maximum_topology
{
    struct declarative_rule_display_entry *displays;
    struct declarative_rule_space_entry *spaces;
    char (*display_uuids)[32];
    char (*space_uuids)[32];
    struct declarative_rule_topology_snapshot snapshot;
};

static bool declarative_rule_maximum_topology_init(struct declarative_rule_maximum_topology *topology)
{
    memset(topology, 0, sizeof(*topology));
    size_t count = DECLARATIVE_CONFIG_MAX_SPACES;
    topology->displays = calloc(count, sizeof(*topology->displays));
    topology->spaces = calloc(count, sizeof(*topology->spaces));
    topology->display_uuids = calloc(count, sizeof(*topology->display_uuids));
    topology->space_uuids = calloc(count, sizeof(*topology->space_uuids));
    if (!topology->displays || !topology->spaces || !topology->display_uuids || !topology->space_uuids) return false;
    for (size_t i = 0; i < count; ++i) {
        snprintf(topology->display_uuids[i], sizeof(*topology->display_uuids), "display-%zu", i);
        snprintf(topology->space_uuids[i], sizeof(*topology->space_uuids), "space-%zu", i);
        topology->displays[i] = (struct declarative_rule_display_entry) {
            (uint32_t) i + 1, topology->display_uuids[i], { (double) i, (double) i }, (int) i
        };
        topology->spaces[i] = (struct declarative_rule_space_entry) {
            (uint64_t) i + 1, topology->space_uuids[i], (int) (i % 4), (int) i,
            (uint32_t) i + 1, topology->display_uuids[i], (int) i
        };
    }
    topology->snapshot = (struct declarative_rule_topology_snapshot) {
        topology->displays, count, topology->spaces, count
    };
    return true;
}

static void declarative_rule_maximum_topology_destroy(struct declarative_rule_maximum_topology *topology)
{
    free(topology->displays);
    free(topology->spaces);
    free(topology->display_uuids);
    free(topology->space_uuids);
    memset(topology, 0, sizeof(*topology));
}

#define INVALID_DISPLAY(statement) do { memcpy(displays, declarative_rule_test_displays, sizeof(displays)); statement; TEST_CHECK(declarative_rule_topology_validate(&topology), false); } while (0)
#define INVALID_SPACE(statement) do { memcpy(spaces, declarative_rule_test_spaces, sizeof(spaces)); statement; TEST_CHECK(declarative_rule_topology_validate(&topology), false); } while (0)
TEST_FUNC(declarative_rule_topology_is_strict_and_cross_linked,
{
    TEST_CHECK(declarative_rule_topology_validate(&declarative_rule_test_topology), true);
    struct rule_fold_context *fold_context = declarative_rule_fold_context_create(&declarative_rule_test_topology);
    TEST_CHECK(fold_context != NULL, true);
    declarative_rule_fold_context_destroy(fold_context);
    struct declarative_rule_display_entry displays[3];
    struct declarative_rule_space_entry spaces[3];
    memcpy(displays, declarative_rule_test_displays, sizeof(displays));
    memcpy(spaces, declarative_rule_test_spaces, sizeof(spaces));
    struct declarative_rule_topology_snapshot topology = { displays, 3, spaces, 3 };
    INVALID_DISPLAY(displays[0].did = 0);
    INVALID_DISPLAY(displays[0].uuid = "");
    INVALID_DISPLAY(displays[1].did = displays[0].did);
    INVALID_DISPLAY(displays[1].uuid = displays[0].uuid);
    INVALID_DISPLAY(displays[1].managed_ordinal = 0);
    INVALID_DISPLAY(displays[2].managed_ordinal = 4);
    memcpy(displays, declarative_rule_test_displays, sizeof(displays));
    INVALID_SPACE(spaces[0].sid = 0);
    INVALID_SPACE(spaces[0].uuid = NULL);
    INVALID_SPACE(spaces[1].sid = spaces[0].sid);
    INVALID_SPACE(spaces[1].uuid = spaces[0].uuid);
    INVALID_SPACE(spaces[1].mission_control_ordinal = 0);
    INVALID_SPACE(spaces[2].mission_control_ordinal = 4);
    INVALID_SPACE(spaces[0].parent_did = 99);
    INVALID_SPACE(spaces[0].parent_display_uuid = "display-b");
    INVALID_SPACE(spaces[0].parent_display_ordinal = 1);
    INVALID_SPACE(spaces[0].kind = -1);
    struct declarative_rule_topology_snapshot no_spaces = { displays, 3, NULL, 0 };
    TEST_CHECK(declarative_rule_topology_validate(&no_spaces), false);
    TEST_CHECK(declarative_rule_fold_context_create(&no_spaces) == NULL, true);
    memcpy(spaces, declarative_rule_test_spaces, sizeof(spaces));
    spaces[2].parent_did = 20;
    spaces[2].parent_display_uuid = "display-b";
    spaces[2].parent_display_ordinal = 1;
    TEST_CHECK(declarative_rule_topology_validate(&topology), false);
});
#undef INVALID_DISPLAY
#undef INVALID_SPACE

TEST_FUNC(declarative_rule_maximum_topology_validates_once_and_folds_are_bounded,
{
    struct declarative_rule_maximum_topology topology;
    bool initialized = declarative_rule_maximum_topology_init(&topology);
    TEST_CHECK(initialized, true);
    if (!initialized) {
        declarative_rule_maximum_topology_destroy(&topology);
        return result;
    }

    declarative_rules_test_reset_operation_counts();
    struct rule_fold_context *context = declarative_rule_fold_context_create(&topology.snapshot);
    TEST_CHECK(context != NULL, true);
    TEST_CHECK(declarative_rules_test_topology_validation_count(), 1ULL);
    declarative_rule_maximum_topology_destroy(&topology);

    size_t rule_count = DECLARATIVE_CONFIG_MAX_RULES;
    struct rule *rules = calloc(rule_count, sizeof(*rules));
    struct rule_warning_state *warnings = calloc(rule_count, sizeof(*warnings));
    TEST_CHECK(rules != NULL, true);
    TEST_CHECK(warnings != NULL, true);
    if (rules && warnings) {
        for (size_t i = 0; i < rule_count; ++i) {
            rules[i].owner = RULE_OWNER_NATIVE;
            rules[i].generation = 1;
            rules[i].warning_state = &warnings[i];
            if ((i & 1) == 0) {
                rules[i].effects.did = DECLARATIVE_CONFIG_MAX_SPACES;
                rules[i].display_uuid = "display-1023";
            } else {
                rules[i].effects.sid = DECLARATIVE_CONFIG_MAX_SPACES;
                rules[i].space_uuid = "space-1023";
                rules[i].expected_space_kind = (DECLARATIVE_CONFIG_MAX_SPACES - 1) % 4;
            }
            struct rule_effects effects = {0};
            TEST_CHECK(rule_combine_rule_effects(&rules[i], &effects, context), RULE_FOLD_OK);
        }
        TEST_CHECK(declarative_rules_test_topology_validation_count(), 1ULL);
        TEST_CHECK(declarative_rules_test_target_lookup_count(),
                   (size_t) DECLARATIVE_CONFIG_MAX_RULES * DECLARATIVE_CONFIG_MAX_SPACES);
    }
    free(rules);
    free(warnings);
    declarative_rule_fold_context_destroy(context);
    TEST_CHECK(declarative_rules_test_outstanding_allocations(), 0ULL);
});

static struct declarative_rule_model declarative_rule_test_model(void)
{
    struct declarative_rule_model model = {0};
    model.label = "native";
    model.one_shot = true;
    model.matcher_present[2] = true;
    model.matchers[2] = (struct declarative_matcher) { "^AXWindow$", false };
    model.matcher_present[3] = true;
    model.matchers[3] = (struct declarative_matcher) { "Dialog", true };
    model.display_present = true;
    model.display_index = 1;
    model.space_present = true;
    model.space_index = 2;
    model.follow_present = true;
    model.follow = true;
    model.grid_present = true;
    unsigned grid[] = { 2, 3, 0, 0, 1, 1 };
    memcpy(model.grid, grid, sizeof(grid));
    model.opacity_present = true; model.opacity = 0.7f;
    model.sticky_present = true; model.sticky = true;
    model.mff_present = true; model.mff = false;
    model.layer_present = true; model.layer = 1;
    model.fullscreen_present = true; model.fullscreen = true;
    model.scratchpad = "scratch";
    return model;
}

TEST_FUNC(declarative_rule_preparation_resolves_orders_fingerprints_and_effects,
{
    struct declarative_rule_model model = declarative_rule_test_model();
    struct declarative_prepared_rules prepared = {0};
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 7, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    struct rule *rule = &prepared.live[0];
    TEST_CHECK(rule->owner, RULE_OWNER_NATIVE);
    TEST_CHECK(rule->generation, 7ULL);
    TEST_CHECK(rule->effects.did, 10U);
    TEST_CHECK(rule->effects.sid, 102ULL);
    TEST_CHECK(strcmp(rule->display_uuid, "display-a"), 0);
    TEST_CHECK(strcmp(rule->space_uuid, "space-b"), 0);
    TEST_CHECK(rule->expected_space_kind, 4);
    TEST_CHECK(rule_check_flag(rule, RULE_ROLE_VALID), true);
    TEST_CHECK(rule_check_flag(rule, RULE_SUBROLE_VALID), true);
    TEST_CHECK(rule_check_flag(rule, RULE_SUBROLE_EXCLUDE), true);
    TEST_CHECK(rule_check_flag(rule, RULE_ONE_SHOT), true);
    TEST_CHECK(rule_effects_check_flag(&rule->effects, RULE_FOLLOW_SPACE), true);
    TEST_CHECK(rule_effects_check_flag(&rule->effects, RULE_OPACITY), true);
    TEST_CHECK(rule_effects_check_flag(&rule->effects, RULE_LAYER), true);
    TEST_CHECK(rule->effects.manage, RULE_PROP_OFF);
    TEST_CHECK(rule->effects.sticky, RULE_PROP_ON);
    TEST_CHECK(rule->effects.mff, RULE_PROP_OFF);
    TEST_CHECK(rule->effects.fullscreen, RULE_PROP_ON);
    TEST_CHECK(strcmp(rule->effects.scratchpad, "scratch"), 0);
    TEST_CHECK(rule->effects.grid[1], 3U);
    TEST_CHECK(rule->label != prepared.templates[0].label, true);
    TEST_CHECK(rule->role != prepared.templates[0].role, true);
    TEST_CHECK(rule->space_uuid != prepared.templates[0].space_uuid, true);
    declarative_prepared_rules_destroy(&prepared);

    model.space_present = false;
    model.display_index = 1;
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_X, 1, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    TEST_CHECK(prepared.live[0].effects.did, 30U);
    declarative_prepared_rules_destroy(&prepared);
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_Y, 1, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    TEST_CHECK(prepared.live[0].effects.did, 10U);
    declarative_prepared_rules_destroy(&prepared);
    model.display_index = 4;
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    TEST_CHECK(prepared.live[0].target_unavailable, true);
    TEST_CHECK(prepared.live[0].effects.did, 0U);
    TEST_CHECK(rule_effects_check_flag(&prepared.live[0].effects, RULE_FOLLOW_SPACE), true);
    declarative_prepared_rules_destroy(&prepared);
});

TEST_FUNC(declarative_rule_unavailable_targets_preserve_rules_precedence_and_independent_effects,
{
    struct declarative_rule_model model = declarative_rule_test_model();
    struct declarative_prepared_rules prepared = {0};

    model.space_index = 9;
    model.display_index = 1;
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 17, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    TEST_CHECK(prepared.count, 1ULL);
    TEST_CHECK(prepared.live_present[0], true);
    TEST_CHECK(prepared.live[0].source_rule_index, 0U);
    TEST_CHECK(strcmp(prepared.live[0].label, "native"), 0);
    TEST_CHECK(prepared.live[0].target_unavailable, true);
    TEST_CHECK(prepared.templates[0].target_unavailable, true);
    TEST_CHECK(prepared.live[0].effects.did, 0U);
    TEST_CHECK(prepared.live[0].effects.sid, 0ULL);
    TEST_CHECK(prepared.templates[0].effects.did, 0U);
    TEST_CHECK(prepared.templates[0].effects.sid, 0ULL);
    TEST_CHECK(rule_effects_check_flag(&prepared.live[0].effects, RULE_FOLLOW_SPACE), true);

    char *query = NULL;
    size_t query_length = 0;
    FILE *query_stream = open_memstream(&query, &query_length);
    rule_serialize(query_stream, &prepared.live[0], 0);
    fclose(query_stream);
    TEST_CHECK(strstr(query, "\"display\":0") != NULL, true);
    TEST_CHECK(strstr(query, "\"space\":0") != NULL, true);
    free(query);

    /* Serialization fails closed even if unavailable provenance is paired with
     * corrupt internal target IDs. */
    prepared.live[0].effects.did = 10;
    prepared.live[0].effects.sid = 101;
    query = NULL;
    query_length = 0;
    query_stream = open_memstream(&query, &query_length);
    rule_serialize(query_stream, &prepared.live[0], 0);
    fclose(query_stream);
    TEST_CHECK(strstr(query, "\"display\":0") != NULL, true);
    TEST_CHECK(strstr(query, "\"space\":0") != NULL, true);
    free(query);
    prepared.live[0].effects.did = 0;
    prepared.live[0].effects.sid = 0;

    struct rule_effects effects = {0};
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, NULL), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    TEST_CHECK(effects.did, 0U);
    TEST_CHECK(effects.sid, 0ULL);
    TEST_CHECK(rule_effects_check_flag(&effects, RULE_FOLLOW_SPACE), false);
    TEST_CHECK(effects.sticky, RULE_PROP_ON);
    TEST_CHECK(effects.fullscreen, RULE_PROP_ON);
    TEST_CHECK(rule_effects_check_flag(&effects, RULE_OPACITY), true);
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, NULL), RULE_FOLD_OK);
    free(effects.scratchpad);

    rule_destroy(&prepared.live[0]);
    prepared.live_present[0] = false;
    TEST_CHECK(declarative_prepared_rule_rearm(&prepared, 0), true);
    TEST_CHECK(prepared.live[0].target_unavailable, true);
    TEST_CHECK(prepared.live[0].warning_state, &prepared.warning_states[0]);
    declarative_prepared_rules_destroy(&prepared);

    model.space_index = 2;
    model.display_index = 99;
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 18, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    TEST_CHECK(prepared.live[0].target_unavailable, false);
    TEST_CHECK(prepared.live[0].effects.sid, 102ULL);
    TEST_CHECK(prepared.live[0].effects.did, 0U);
    declarative_prepared_rules_destroy(&prepared);

    model.space_index = 0;
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 19, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY);
    model.space_present = false;
    model.display_index = 0;
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 19, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY);
});

TEST_FUNC(declarative_rule_preparation_supports_each_matcher_independently,
{
    enum rule_flag valid_flags[] = { RULE_APP_VALID, RULE_TITLE_VALID, RULE_ROLE_VALID, RULE_SUBROLE_VALID };
    enum rule_flag exclude_flags[] = { RULE_APP_EXCLUDE, RULE_TITLE_EXCLUDE, RULE_ROLE_EXCLUDE, RULE_SUBROLE_EXCLUDE };
    for (int matcher = 0; matcher < 4; ++matcher) {
        struct declarative_rule_model model = {0};
        model.matcher_present[matcher] = true;
        model.matchers[matcher] = (struct declarative_matcher) { ".+", true };
        struct declarative_prepared_rules prepared = {0};
        TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                            DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
        TEST_CHECK(rule_check_flag(&prepared.live[0], valid_flags[matcher]), true);
        TEST_CHECK(rule_check_flag(&prepared.live[0], exclude_flags[matcher]), true);
        char *template_patterns[] = { prepared.templates[0].app, prepared.templates[0].title, prepared.templates[0].role, prepared.templates[0].subrole };
        char *live_patterns[] = { prepared.live[0].app, prepared.live[0].title, prepared.live[0].role, prepared.live[0].subrole };
        regex_t *template_regexes[] = { &prepared.templates[0].app_regex, &prepared.templates[0].title_regex, &prepared.templates[0].role_regex, &prepared.templates[0].subrole_regex };
        regex_t *live_regexes[] = { &prepared.live[0].app_regex, &prepared.live[0].title_regex, &prepared.live[0].role_regex, &prepared.live[0].subrole_regex };
        TEST_CHECK(template_patterns[matcher] != live_patterns[matcher], true);
        TEST_CHECK(regexec(template_regexes[matcher], "match", 0, NULL, 0), 0);
        TEST_CHECK(regexec(live_regexes[matcher], "match", 0, NULL, 0), 0);
        for (int other = 0; other < 4; ++other) {
            if (other != matcher) TEST_CHECK(rule_check_flag(&prepared.live[0], valid_flags[other]), false);
        }
        declarative_prepared_rules_destroy(&prepared);
    }
});

TEST_FUNC(declarative_rule_preparation_rejects_labels_regex_and_preserves_manager,
{
    struct declarative_rule_model models[] = { declarative_rule_test_model(), declarative_rule_test_model() };
    struct declarative_prepared_rules prepared = {0};
    int before = buf_len(g_window_manager.rules);
    TEST_CHECK(declarative_prepare_rules(models, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 0, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_INVALID_GENERATION);
    TEST_CHECK(declarative_prepare_rules(models, 2, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_LABEL_COLLISION);
    TEST_CHECK(buf_len(g_window_manager.rules), before);
    const char *labels[] = { "native" };
    TEST_CHECK(declarative_prepare_rules(models, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1, labels, 1, &prepared), DECLARATIVE_RULE_PREPARE_LABEL_COLLISION);
    models[0].label = "other";
    models[0].matchers[2].pattern = "[";
    TEST_CHECK(declarative_prepare_rules(models, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_INVALID_REGEX);
    TEST_CHECK(buf_len(g_window_manager.rules), before);
    TEST_CHECK(declarative_prepare_rules(models, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1, NULL, 1, &prepared), DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY);
    TEST_CHECK(declarative_prepare_rules(models, DECLARATIVE_CONFIG_MAX_RULES + 1ULL, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY);
});

TEST_FUNC(declarative_rule_preparation_cleans_every_allocator_failure,
{
    struct declarative_rule_model model = declarative_rule_test_model();
    for (size_t ordinal = 1; ordinal <= 30; ++ordinal) {
        declarative_rules_test_fail_allocation_at(ordinal);
        struct declarative_prepared_rules prepared = {0};
        enum declarative_rule_prepare_result prepare_result = declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                                                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1,
                                                                                        NULL, 0, &prepared);
        if (prepare_result == DECLARATIVE_RULE_PREPARE_OK) declarative_prepared_rules_destroy(&prepared);
        else TEST_CHECK(prepare_result, DECLARATIVE_RULE_PREPARE_ALLOCATION_FAILED);
        TEST_CHECK(prepared.count, 0ULL);
        TEST_CHECK(declarative_rules_test_outstanding_allocations(), 0ULL);
    }
    declarative_rules_test_reset_allocator();
});

TEST_FUNC(declarative_rule_fold_context_creation_cleans_every_allocator_failure,
{
    for (size_t ordinal = 1; ordinal <= 14; ++ordinal) {
        declarative_rules_test_fail_allocation_at(ordinal);
        struct rule_fold_context *context = declarative_rule_fold_context_create(&declarative_rule_test_topology);
        if (context) declarative_rule_fold_context_destroy(context);
        TEST_CHECK(declarative_rules_test_outstanding_allocations(), 0ULL);
    }
    declarative_rules_test_reset_allocator();
});

TEST_FUNC(declarative_rule_maximum_preparation_and_destruction_tracks_every_allocation,
{
    struct declarative_rule_maximum_topology topology;
    bool initialized = declarative_rule_maximum_topology_init(&topology);
    TEST_CHECK(initialized, true);
    if (!initialized) {
        declarative_rule_maximum_topology_destroy(&topology);
        return result;
    }

    size_t count = DECLARATIVE_CONFIG_MAX_RULES;
    struct declarative_rule_model *models = calloc(count, sizeof(*models));
    char (*labels)[32] = calloc(count, sizeof(*labels));
    TEST_CHECK(models != NULL, true);
    TEST_CHECK(labels != NULL, true);
    if (models && labels) {
        for (size_t i = 0; i < count; ++i) {
            snprintf(labels[i], sizeof(*labels), "maximum-rule-%zu", i);
            models[i].label = labels[i];
            for (int matcher = 0; matcher < 4; ++matcher) {
                models[i].matcher_present[matcher] = true;
                models[i].matchers[matcher] = (struct declarative_matcher) { "^.*$", matcher & 1 };
            }
            models[i].display_present = true;
            models[i].display_index = (int) i + 1;
            models[i].space_present = true;
            models[i].space_index = (int) i + 1;
            models[i].scratchpad = "maximum-scratchpad";
        }
        struct declarative_prepared_rules prepared = {0};
        TEST_CHECK(declarative_prepare_rules(models, count, &topology.snapshot,
                                            DISPLAY_ARRANGEMENT_ORDER_DEFAULT, UINT64_MAX,
                                            NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
        TEST_CHECK(declarative_rules_test_outstanding_allocations() > 8192, true);
        declarative_prepared_rules_destroy(&prepared);
        TEST_CHECK(declarative_rules_test_outstanding_allocations(), 0ULL);
    }
    free(models);
    free(labels);
    declarative_rule_maximum_topology_destroy(&topology);
});

TEST_FUNC(declarative_native_one_shot_template_survives_live_destruction_and_rearms,
{
    struct declarative_rule_model model = declarative_rule_test_model();
    struct declarative_prepared_rules prepared = {0};
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 9, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    char *template_label = prepared.templates[0].label;
    rule_destroy(&prepared.live[0]);
    prepared.live_present[0] = false;
    TEST_CHECK(strcmp(template_label, "native"), 0);
    TEST_CHECK(declarative_prepared_rule_rearm(&prepared, 0), true);
    TEST_CHECK(prepared.live_present[0], true);
    TEST_CHECK(strcmp(prepared.live[0].label, "native"), 0);
    TEST_CHECK(prepared.live[0].label != template_label, true);
    TEST_CHECK(prepared.live[0].generation, 9ULL);
    declarative_prepared_rules_destroy(&prepared);
});

TEST_FUNC(declarative_rule_folding_is_provenance_aware_and_warns_once,
{
    struct declarative_rule_model model = declarative_rule_test_model();
    struct declarative_prepared_rules prepared = {0};
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 3, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    struct declarative_rule_space_entry current_spaces[3];
    memcpy(current_spaces, declarative_rule_test_spaces, sizeof(current_spaces));
    current_spaces[1].sid = 202;
    current_spaces[1].uuid = "current-space-b";
    struct declarative_rule_topology_snapshot current_topology = {
        declarative_rule_test_displays, 3, current_spaces, 3
    };
    struct rule_fold_context *context = declarative_rule_fold_context_create(&current_topology);
    TEST_CHECK(context != NULL, true);
    struct rule_effects effects = {0};
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, context), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    TEST_CHECK(effects.sid, 0ULL);
    TEST_CHECK(effects.did, 0U);
    TEST_CHECK(rule_effects_check_flag(&effects, RULE_FOLLOW_SPACE), false);
    TEST_CHECK(effects.sticky, RULE_PROP_ON);
    TEST_CHECK(rule_effects_check_flag(&effects, RULE_OPACITY), true);
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, context), RULE_FOLD_OK);

    struct rule runtime = { .effects = { .did = 77 } };
    rule_combine_rule_effects(&runtime, &effects, NULL);
    TEST_CHECK(effects.did, 77U);
    TEST_CHECK(effects.sid, 0ULL);

    declarative_rule_fold_context_destroy(context);
    context = declarative_rule_fold_context_create(&declarative_rule_test_topology);
    TEST_CHECK(context != NULL, true);
    free(effects.scratchpad);
    memset(&effects, 0, sizeof(effects));
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, context), RULE_FOLD_OK);
    TEST_CHECK(effects.sid, 102ULL);
    struct rule later_runtime_display = { .effects = { .did = 88 } };
    rule_combine_rule_effects(&later_runtime_display, &effects, NULL);
    TEST_CHECK(effects.did, 88U);
    TEST_CHECK(effects.sid, 0ULL);
    free(effects.scratchpad);
    memset(&effects, 0, sizeof(effects));
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, context), RULE_FOLD_OK);
    struct rule missing_runtime = {0};
    rule_combine_rule_effects(&missing_runtime, &effects, NULL);
    TEST_CHECK(effects.sid, 102ULL);
    free(effects.scratchpad);
    declarative_rule_fold_context_destroy(context);
    declarative_prepared_rules_destroy(&prepared);
});

TEST_FUNC(declarative_rule_warning_state_survives_rearm_acknowledges_and_fails_closed,
{
    struct declarative_rule_model model = declarative_rule_test_model();
    struct declarative_prepared_rules prepared = {0};
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 1, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    struct rule_effects effects = {0};
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, NULL), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    TEST_CHECK(effects.sid, 0ULL);
    TEST_CHECK(effects.did, 0U);
    TEST_CHECK(effects.sticky, RULE_PROP_ON);
    TEST_CHECK(prepared.warning_states[0].pending_generation, 1ULL);
    TEST_CHECK(prepared.warning_states[0].emitted_generation, 0ULL);
    free(effects.scratchpad);
    memset(&effects, 0, sizeof(effects));
    TEST_CHECK(declarative_prepared_rule_rearm(&prepared, 0), true);
    TEST_CHECK(prepared.live[0].warning_state, &prepared.warning_states[0]);
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, NULL), RULE_FOLD_OK);
    TEST_CHECK(rule_acknowledge_stale_warning(&prepared.live[0]), true);
    TEST_CHECK(prepared.warning_states[0].emitted_generation, 1ULL);
    prepared.live[0].generation = UINT64_MAX;
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, NULL), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    TEST_CHECK(prepared.warning_states[0].pending_generation, UINT64_MAX);
    TEST_CHECK(rule_acknowledge_stale_warning(&prepared.live[0]), true);
    TEST_CHECK(prepared.warning_states[0].emitted_generation, UINT64_MAX);
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, NULL), RULE_FOLD_OK);
    free(effects.scratchpad);
    declarative_prepared_rules_destroy(&prepared);
});

TEST_FUNC(declarative_rule_apply_warns_only_after_first_matching_root,
{
    struct declarative_rule_model model = {0};
    model.matcher_present[0] = true;
    model.matchers[0] = (struct declarative_matcher) { "^Match$", false };
    model.display_present = true;
    model.display_index = 4;
    struct declarative_prepared_rules prepared = {0};
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 23, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);

    struct table saved_windows = g_window_manager.window;
    g_window_manager.window = (struct table) {0};
    table_init(&g_window_manager.window, 4, hash_wm, compare_wm);
    struct application application = { .name = "Other" };
    struct window window = { .id = 9001, .application = &application, .is_root = true };
    table_add(&g_window_manager.window, &window.id, &window);

    rule_apply(&prepared.live[0], NULL);
    TEST_CHECK(prepared.warning_states[0].pending_generation, 0ULL);
    application.name = "Match";
    rule_apply(&prepared.live[0], NULL);
    TEST_CHECK(prepared.warning_states[0].pending_generation, 23ULL);
    TEST_CHECK(prepared.warning_states[0].emitted_generation, 23ULL);
    rule_apply(&prepared.live[0], NULL);
    TEST_CHECK(prepared.warning_states[0].pending_generation, 23ULL);
    TEST_CHECK(prepared.warning_states[0].emitted_generation, 23ULL);

    table_free(&g_window_manager.window);
    g_window_manager.window = saved_windows;
    declarative_prepared_rules_destroy(&prepared);
});

TEST_FUNC(declarative_rule_reapply_without_matching_roots_does_not_transition_warning,
{
    struct rule *saved = g_window_manager.rules;
    g_window_manager.rules = NULL;
    struct declarative_rule_model model = declarative_rule_test_model();
    model.one_shot = false;
    struct declarative_prepared_rules prepared = {0};
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 13, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    buf_push(g_window_manager.rules, prepared.live[0]);
    memset(&prepared.live[0], 0, sizeof(prepared.live[0]));
    prepared.live_present[0] = false;
    struct declarative_rule_space_entry current_spaces[3];
    memcpy(current_spaces, declarative_rule_test_spaces, sizeof(current_spaces));
    current_spaces[1].sid = 202;
    struct declarative_rule_topology_snapshot current_topology = {
        declarative_rule_test_displays, 3, current_spaces, 3
    };
    struct rule_fold_context *context = declarative_rule_fold_context_create(&current_topology);
    TEST_CHECK(context != NULL, true);
    TEST_CHECK(rule_reapply_by_index(0, context), true);
    TEST_CHECK(prepared.warning_states[0].pending_generation, 0ULL);
    TEST_CHECK(rule_acknowledge_stale_warning(&g_window_manager.rules[0]), false);
    g_window_manager.rules[0].generation = 14;
    TEST_CHECK(rule_reapply_by_label("native", context), true);
    TEST_CHECK(prepared.warning_states[0].pending_generation, 0ULL);
    declarative_rule_fold_context_destroy(context);
    rule_destroy(&g_window_manager.rules[0]);
    buf_free(g_window_manager.rules);
    declarative_prepared_rules_destroy(&prepared);
    g_window_manager.rules = saved;
});

TEST_FUNC(declarative_rule_owned_context_is_immutable_and_rejects_stale_targets,
{
    struct declarative_rule_model model = declarative_rule_test_model();
    struct declarative_prepared_rules prepared = {0};
    TEST_CHECK(declarative_prepare_rules(&model, 1, &declarative_rule_test_topology,
                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT, 3, NULL, 0, &prepared), DECLARATIVE_RULE_PREPARE_OK);
    struct declarative_rule_display_entry displays[3];
    struct declarative_rule_space_entry spaces[3];
    memcpy(displays, declarative_rule_test_displays, sizeof(displays));
    memcpy(spaces, declarative_rule_test_spaces, sizeof(spaces));
    struct declarative_rule_topology_snapshot topology = { displays, 3, spaces, 3 };
    struct rule_fold_context *context = declarative_rule_fold_context_create(&topology);
    TEST_CHECK(context != NULL, true);

    displays[1].uuid = "mutated-display";
    spaces[1].uuid = "mutated-space";
    spaces[1].kind = 9;
    spaces[1].sid = 999;
    struct rule_effects effects = {0};
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, context), RULE_FOLD_OK);
    TEST_CHECK(effects.sid, 102ULL);
    declarative_rule_fold_context_destroy(context);

    free(effects.scratchpad);
    memset(&effects, 0, sizeof(effects));
    memcpy(displays, declarative_rule_test_displays, sizeof(displays));
    memcpy(spaces, declarative_rule_test_spaces, sizeof(spaces));
    spaces[1].uuid = "reused-space";
    context = declarative_rule_fold_context_create(&topology);
    TEST_CHECK(context != NULL, true);
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, context), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    TEST_CHECK(effects.sid, 0ULL);
    declarative_rule_fold_context_destroy(context);

    spaces[1].uuid = "space-b";
    spaces[1].kind = 9;
    prepared.live[0].generation = 4;
    context = declarative_rule_fold_context_create(&topology);
    TEST_CHECK(context != NULL, true);
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, context), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    declarative_rule_fold_context_destroy(context);

    spaces[1].kind = 4;
    spaces[1].sid = 999;
    prepared.live[0].generation = 5;
    context = declarative_rule_fold_context_create(&topology);
    TEST_CHECK(context != NULL, true);
    TEST_CHECK(rule_combine_rule_effects(&prepared.live[0], &effects, context), RULE_FOLD_WARNING_NEWLY_REQUIRED);
    declarative_rule_fold_context_destroy(context);
    free(effects.scratchpad);
    declarative_prepared_rules_destroy(&prepared);
});

TEST_FUNC(rule_protected_label_messages_are_exact_and_ordered,
{
    struct rule *saved = g_window_manager.rules;
    g_window_manager.rules = NULL;
    struct rule native = { .label = strdup("native"), .owner = RULE_OWNER_NATIVE };
    buf_push(g_window_manager.rules, native);
    char *output = NULL;
    size_t output_length = 0;
    FILE *rsp = open_memstream(&output, &output_length);
    char add[] = "--add\0label=native\0app=native\0";
    char remove_label[] = "--remove\0native\0";
    char remove_index[] = "--remove\0" "0\0";
    struct token domain = { .text = "rule", .length = 4 };
    handle_domain_rule(rsp, domain, add);
    handle_domain_rule(rsp, domain, remove_label);
    handle_domain_rule(rsp, domain, remove_index);
    fclose(rsp);
    TEST_CHECK(strcmp(output, "\x07rule with label 'native' is native and cannot be replaced.\n"
                              "\x07rule with label 'native' is native and cannot be removed.\n"
                              "\x07rule with index '0' is native and cannot be removed.\n"), 0);
    free(output);
    rule_destroy(&g_window_manager.rules[0]);
    buf_free(g_window_manager.rules);
    g_window_manager.rules = saved;
});

TEST_FUNC(rule_label_ownership_results_preserve_runtime_order_and_protect_native,
{
    struct rule *saved = g_window_manager.rules;
    g_window_manager.rules = NULL;
    struct rule runtime1 = { .label = strdup("same") };
    struct rule runtime2 = { .label = strdup("same") };
    TEST_CHECK(rule_add(&runtime1), RULE_ADD_OK);
    TEST_CHECK(rule_add(&runtime2), RULE_ADD_OK);
    TEST_CHECK(buf_len(g_window_manager.rules), 1);
    struct rule native = { .label = strdup("native"), .owner = RULE_OWNER_NATIVE };
    buf_push(g_window_manager.rules, native);
    struct rule collision = { .label = strdup("native") };
    TEST_CHECK(rule_add(&collision), RULE_ADD_PROTECTED_COLLISION);
    rule_destroy(&collision);
    TEST_CHECK(buf_len(g_window_manager.rules), 2);
    TEST_CHECK(rule_remove_by_label("missing"), RULE_REMOVE_NOT_FOUND);
    TEST_CHECK(rule_remove_by_label("native"), RULE_REMOVE_PROTECTED_DENIED);
    TEST_CHECK(rule_remove_by_index(1), RULE_REMOVE_PROTECTED_DENIED);
    TEST_CHECK(rule_remove_by_label("same"), RULE_REMOVE_OK);
    TEST_CHECK(strcmp(g_window_manager.rules[0].label, "native"), 0);
    rule_destroy(&g_window_manager.rules[0]);
    buf_free(g_window_manager.rules);
    g_window_manager.rules = saved;
});
