extern struct space_manager g_space_manager;
extern struct window_manager g_window_manager;

static struct {
    uint64_t generation;
    size_t native_prefix_length;
    const char *const *labels;
    size_t label_count;
    const struct rule_fold_context *context;
} rule_native_state;

void rule_native_generation_install(uint64_t generation, size_t native_prefix_length,
                                    const char *const *labels, size_t label_count,
                                    const struct rule_fold_context *context)
{
    rule_native_state.generation = generation;
    rule_native_state.native_prefix_length = native_prefix_length;
    rule_native_state.labels = labels;
    rule_native_state.label_count = label_count;
    rule_native_state.context = context;
}

void rule_native_generation_clear(void) { memset(&rule_native_state, 0, sizeof(rule_native_state)); }
uint64_t rule_native_generation(void) { return rule_native_state.generation; }
size_t rule_native_prefix_length(void) { return rule_native_state.native_prefix_length; }

bool rule_native_label_is_protected(const char *label)
{
    if (!label) return false;
    for (size_t i = 0; i < rule_native_state.label_count; ++i) {
        if (rule_native_state.labels[i] && string_equals(rule_native_state.labels[i], label)) return true;
    }
    return false;
}

static const struct rule_fold_context *rule_installed_fold_context(struct rule *rule,
                                                                   const struct rule_fold_context *provided)
{
    if (rule && rule->owner == RULE_OWNER_NATIVE) {
        if (rule_native_state.context && rule->generation == rule_native_state.generation) {
            return rule_native_state.context;
        }
        if (rule_native_state.generation) return NULL;
    }
    return provided;
}

static void rule_free(void *value)
{
#ifdef TESTS
    declarative_rules_test_note_free(value);
#endif
    free(value);
}

void rule_serialize(FILE *rsp, struct rule *rule, int index)
{
    TIME_FUNCTION;

    char *label      = rule->label;
    char *app        = rule->app;
    char *title      = rule->title;
    char *role       = rule->role;
    char *srole      = rule->subrole;
    char *scratchpad = rule->effects.scratchpad;

    char *escaped_label      = label      ? ts_string_escape(label)      : NULL;
    char *escaped_app        = app        ? ts_string_escape(app)        : NULL;
    char *escaped_title      = title      ? ts_string_escape(title)      : NULL;
    char *escaped_role       = role       ? ts_string_escape(role)       : NULL;
    char *escaped_srole      = srole      ? ts_string_escape(srole)      : NULL;
    char *escaped_scratchpad = scratchpad ? ts_string_escape(scratchpad) : NULL;

    fprintf(rsp,
            "{\n"
            "\t\"index\":%d,\n"
            "\t\"%s\":\"%s\",\n"
            "\t\"%s\":\"%s\",\n"
            "\t\"%s\":\"%s\",\n"
            "\t\"%s\":\"%s\",\n"
            "\t\"%s\":\"%s\",\n"
            "\t\"%s\":%d,\n"
            "\t\"%s\":%d,\n"
            "\t\"%s\":%s,\n"
            "\t\"%s\":%.4f,\n"
            "\t\"%s\":%s,\n"
            "\t\"%s\":%s,\n"
            "\t\"%s\":%s,\n"
            "\t\"%s\":\"%s\",\n"
            "\t\"%s\":%s,\n"
            "\t\"%s\":\"%d:%d:%d:%d:%d:%d\",\n"
            "\t\"%s\":\"%s\",\n"
            "\t\"%s\":%s,\n"
            "\t\"flags\":\"0x%08x\"\n"
            "}",
            index,
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_LABEL].query_name, escaped_label ? escaped_label : label ? label : "",
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_APP].query_name, escaped_app ? escaped_app : app ? app : "",
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_TITLE].query_name, escaped_title ? escaped_title : title ? title : "",
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_ROLE].query_name, escaped_role ? escaped_role : role ? role : "",
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_SUBROLE].query_name, escaped_srole ? escaped_srole : srole ? srole : "",
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_DISPLAY].query_name, rule->effects.did ? display_manager_display_id_arrangement(rule->effects.did) : 0,
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_SPACE].query_name, rule->effects.sid ? space_manager_mission_control_index(rule->effects.sid) : 0,
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_FOLLOW].query_name, json_bool(rule_effects_check_flag(&rule->effects, RULE_FOLLOW_SPACE)),
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_OPACITY].query_name, rule->effects.opacity,
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_MANAGE].query_name, json_optional_bool(rule->effects.manage),
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_STICKY].query_name, json_optional_bool(rule->effects.sticky),
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_MFF].query_name, json_optional_bool(rule->effects.mff),
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_SUB_LAYER].query_name, rule_effects_check_flag(&rule->effects, RULE_LAYER) ? layer_str[rule->effects.layer] : "",
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_FULLSCREEN].query_name, json_optional_bool(rule->effects.fullscreen),
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_GRID].query_name, rule->effects.grid[0], rule->effects.grid[1],
            rule->effects.grid[2], rule->effects.grid[3],
            rule->effects.grid[4], rule->effects.grid[5],
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_SCRATCHPAD].query_name, escaped_scratchpad ? escaped_scratchpad : scratchpad ? scratchpad : "",
            declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_ONE_SHOT].query_name, json_bool(rule_check_flag(rule, RULE_ONE_SHOT)),
            (uint32_t)(rule->effects.flags << 16) | (uint32_t)rule->flags);
}

void rule_combine_effects(struct rule_effects *effects, struct rule_effects *result)
{
    if (effects->did) {
        result->did = effects->did;
        result->sid = 0;
        if (rule_effects_check_flag(effects, RULE_FOLLOW_SPACE)) {
            rule_effects_set_flag(result, RULE_FOLLOW_SPACE);
        } else {
            rule_effects_clear_flag(result, RULE_FOLLOW_SPACE);
        }
    }

    if (effects->sid) {
        result->sid = effects->sid;
        result->did = 0;
        if (rule_effects_check_flag(effects, RULE_FOLLOW_SPACE)) {
            rule_effects_set_flag(result, RULE_FOLLOW_SPACE);
        } else {
            rule_effects_clear_flag(result, RULE_FOLLOW_SPACE);
        }
    }

    if (rule_effects_check_flag(effects, RULE_OPACITY) && in_range_ii(effects->opacity, 0.0f, 1.0f)) {
        result->opacity = effects->opacity;
        rule_effects_set_flag(result, RULE_OPACITY);
    }

    if (rule_effects_check_flag(effects, RULE_LAYER)) {
        result->layer = effects->layer;
        rule_effects_set_flag(result, RULE_LAYER);
    }

    if (effects->scratchpad) {
        if (result->scratchpad) free(result->scratchpad);
        result->scratchpad = string_copy(effects->scratchpad);
    }

    if (effects->manage     != RULE_PROP_UD) result->manage     = effects->manage;
    if (effects->sticky     != RULE_PROP_UD) result->sticky     = effects->sticky;
    if (effects->mff        != RULE_PROP_UD) result->mff        = effects->mff;
    if (effects->fullscreen != RULE_PROP_UD) result->fullscreen = effects->fullscreen;

    if (effects->grid[0] != 0 && effects->grid[1] != 0) {
        result->grid[0] = effects->grid[0];
        result->grid[1] = effects->grid[1];
        result->grid[2] = effects->grid[2];
        result->grid[3] = effects->grid[3];
        result->grid[4] = effects->grid[4];
        result->grid[5] = effects->grid[5];
    }
}

enum rule_fold_result rule_combine_rule_effects(struct rule *rule, struct rule_effects *result, const struct rule_fold_context *context)
{
    context = rule_installed_fold_context(rule, context);
    if (rule->owner != RULE_OWNER_NATIVE) {
        rule_combine_effects(&rule->effects, result);
        return RULE_FOLD_OK;
    }

    bool has_target = rule->effects.sid || rule->effects.did;
    bool valid = !has_target;
    if (has_target) {
        if (rule->effects.sid) {
            valid = rule_fold_context_target_is_valid(context, RULE_TARGET_SPACE,
                                                       rule->effects.sid, rule->space_uuid,
                                                       rule->expected_space_kind, &rule->space_identity);
        } else {
            valid = rule_fold_context_target_is_valid(context, RULE_TARGET_DISPLAY,
                                                       rule->effects.did, rule->display_uuid, 0, NULL);
        }
    }

    struct rule_effects contribution = rule->effects;
    if (!valid) {
        contribution.did = 0;
        contribution.sid = 0;
        rule_effects_clear_flag(&contribution, RULE_FOLLOW_SPACE);
    }
    rule_combine_effects(&contribution, result);

    if (!valid && rule->warning_state) {
        uint64_t emitted = __atomic_load_n(&rule->warning_state->emitted_generation, __ATOMIC_ACQUIRE);
        uint64_t pending = __atomic_load_n(&rule->warning_state->pending_generation, __ATOMIC_ACQUIRE);
        if (emitted != rule->generation && pending != rule->generation) {
            if (__atomic_compare_exchange_n(&rule->warning_state->pending_generation, &pending, rule->generation,
                                            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                return RULE_FOLD_WARNING_NEWLY_REQUIRED;
            }
        }
    }
    return RULE_FOLD_OK;
}

bool rule_acknowledge_stale_warning(struct rule *rule)
{
    if (!rule || !rule->warning_state) return false;
    uint64_t pending = __atomic_load_n(&rule->warning_state->pending_generation, __ATOMIC_ACQUIRE);
    if (pending != rule->generation) return false;
    __atomic_store_n(&rule->warning_state->emitted_generation, rule->generation, __ATOMIC_RELEASE);
    return true;
}

void rule_report_fold_warning(struct rule *rule, enum rule_fold_result result)
{
    if (result != RULE_FOLD_WARNING_NEWLY_REQUIRED) return;
    declarative_report_stale_rule();
    (void) rule_acknowledge_stale_warning(rule);
}

void rule_reapply_all(void)
{
    table_for (struct window *window, g_window_manager.window, {
        if (window->is_root) {
            char *window_title = window_title_ts(window);
            char *window_role = window_role_ts(window);
            char *window_subrole = window_subrole_ts(window);

            window_manager_apply_manage_rules_to_window(&g_space_manager, &g_window_manager, window, window_title, window_role, window_subrole, false, 0);

            if (window_manager_is_window_eligible(window)) {
                window->is_eligible = true;
                window_manager_apply_rules_to_window(&g_space_manager, &g_window_manager, window, window_title, window_role, window_subrole, false, 0);
            }
        }
    })
}

bool rule_reapply_by_index(int index, const struct rule_fold_context *context)
{
    for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
        if (i == index) {
            if (!rule_check_flag(&g_window_manager.rules[i], RULE_ONE_SHOT)) {
                rule_apply(&g_window_manager.rules[i], context);
            }
            return true;
        }
    }

    return false;
}

bool rule_reapply_by_label(char *label, const struct rule_fold_context *context)
{
    for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
        if (string_equals(g_window_manager.rules[i].label, label)) {
            if (!rule_check_flag(&g_window_manager.rules[i], RULE_ONE_SHOT)) {
                rule_apply(&g_window_manager.rules[i], context);
            }
            return true;
        }
    }

    return false;
}

void rule_apply(struct rule *rule, const struct rule_fold_context *context)
{
    struct rule_effects effects = {0};
    enum rule_fold_result fold = rule_combine_rule_effects(rule, &effects, context);
    rule_report_fold_warning(rule, fold);
    table_for (struct window *window, g_window_manager.window, {
        if (window->is_root) {
            if (window_manager_rule_matches_window(rule, window, window_title_ts(window), window_role_ts(window), window_subrole_ts(window))) {
                window_manager_apply_manage_rule_effects_to_window(&g_space_manager, &g_window_manager, window, &effects);

                if (window_manager_is_window_eligible(window)) {
                    window->is_eligible = true;
                    window_manager_apply_rule_effects_to_window(&g_space_manager, &g_window_manager, window, &effects);
                }
            }
        }
    })
    rule_free(effects.scratchpad);
}

static void rule_buffer_delete_ordered(int index)
{
    int count = buf_len(g_window_manager.rules);
    if (index < 0 || index >= count) return;
    if (index + 1 < count) {
        memmove(&g_window_manager.rules[index], &g_window_manager.rules[index + 1],
                (size_t) (count - index - 1) * sizeof(*g_window_manager.rules));
    }
    buf__hdr(g_window_manager.rules)->len = count - 1;
}

enum rule_add_result rule_add(struct rule *rule)
{
    if (rule->label) {
        if (rule_native_label_is_protected(rule->label)) return RULE_ADD_PROTECTED_COLLISION;
        for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
            if (string_equals(g_window_manager.rules[i].label, rule->label)) {
                if (g_window_manager.rules[i].owner == RULE_OWNER_NATIVE) return RULE_ADD_PROTECTED_COLLISION;
                rule_destroy(&g_window_manager.rules[i]);
                rule_buffer_delete_ordered(i);
                break;
            }
        }
    }
    rule->owner = RULE_OWNER_RUNTIME;
    buf_push(g_window_manager.rules, *rule);
    return RULE_ADD_OK;
}

enum rule_remove_result rule_remove_by_index(int index)
{
    for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
        if (i == index) {
            if (g_window_manager.rules[i].owner == RULE_OWNER_NATIVE) return RULE_REMOVE_PROTECTED_DENIED;
            rule_destroy(&g_window_manager.rules[i]);
            rule_buffer_delete_ordered(i);
            return RULE_REMOVE_OK;
        }
    }

    return RULE_REMOVE_NOT_FOUND;
}

enum rule_remove_result rule_remove_by_label(char *label)
{
    if (rule_native_label_is_protected(label)) return RULE_REMOVE_PROTECTED_DENIED;
    for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
        if (string_equals(g_window_manager.rules[i].label, label)) {
            if (g_window_manager.rules[i].owner == RULE_OWNER_NATIVE) return RULE_REMOVE_PROTECTED_DENIED;
            rule_destroy(&g_window_manager.rules[i]);
            rule_buffer_delete_ordered(i);
            return RULE_REMOVE_OK;
        }
    }

    return RULE_REMOVE_NOT_FOUND;
}

static void rule_remove_one_shot_at(int index)
{
    int count = buf_len(g_window_manager.rules);
    bool native = g_window_manager.rules[index].owner == RULE_OWNER_NATIVE;
    rule_destroy(&g_window_manager.rules[index]);
    if (index + 1 < count) {
        memmove(&g_window_manager.rules[index], &g_window_manager.rules[index + 1],
                (size_t) (count - index - 1) * sizeof(*g_window_manager.rules));
    }
    buf__hdr(g_window_manager.rules)->len = count - 1;
    if (native && rule_native_state.native_prefix_length) --rule_native_state.native_prefix_length;
}

struct rule_one_shot_owner_plan
{
    pid_t pid;
    uint32_t psn_high;
    uint32_t psn_low;
    bool remove;
};

static bool rule_one_shot_reassignment_pending;

#ifdef TESTS
static int rule_test_reservation_result = -1;
static size_t rule_test_reservation_calls;
static size_t rule_test_reservation_fail_ordinal;
static size_t rule_test_reservation_allocation_ordinal;
static size_t rule_test_reservation_allocation_fail_ordinal;
void rule_test_set_reservation_match_result(int result)
{
    rule_test_reservation_result = result;
    rule_test_reservation_fail_ordinal = 0;
    rule_test_reservation_calls = 0;
}
void rule_test_fail_reservation_match_at(size_t ordinal, int result)
{
    rule_test_reservation_result = result;
    rule_test_reservation_fail_ordinal = ordinal;
    rule_test_reservation_calls = 0;
}
void rule_test_fail_reservation_allocation_at(size_t ordinal)
{
    rule_test_reservation_allocation_fail_ordinal = ordinal;
    rule_test_reservation_allocation_ordinal = 0;
}
size_t rule_test_reservation_match_calls(void) { return rule_test_reservation_calls; }
#endif

static void *rule_reservation_calloc(size_t count, size_t size)
{
#ifdef TESTS
    if (++rule_test_reservation_allocation_ordinal == rule_test_reservation_allocation_fail_ordinal) return NULL;
#endif
    return calloc(count, size);
}

static int rule_reservation_app_match(struct rule *rule, const char *name)
{
    if (!rule_check_flag(rule, RULE_APP_VALID)) return 0;
#ifdef TESTS
    ++rule_test_reservation_calls;
    if (rule_test_reservation_result >= 0 &&
        (!rule_test_reservation_fail_ordinal ||
         rule_test_reservation_calls == rule_test_reservation_fail_ordinal)) return rule_test_reservation_result;
#endif
    return regexec(&rule->app_regex, name, 0, NULL, 0);
}

static bool rule_one_shot_owned_by(const struct rule *rule, const struct application *application)
{
    return application && rule->one_shot_owner_pid == application->pid &&
           rule->one_shot_owner_psn_high == (uint32_t) application->psn.highLongOfPSN &&
           rule->one_shot_owner_psn_low == (uint32_t) application->psn.lowLongOfPSN;
}

static void rule_owner_plan_init(struct rule_one_shot_owner_plan *plan,
                                 struct rule *rules, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        plan[i].pid = rules[i].one_shot_owner_pid;
        plan[i].psn_high = rules[i].one_shot_owner_psn_high;
        plan[i].psn_low = rules[i].one_shot_owner_psn_low;
    }
}

static bool rule_owner_plan_reserve(struct rule_one_shot_owner_plan *plan,
                                    struct rule *rules, size_t count,
                                    struct application *application)
{
    for (size_t i = 0; i < count; ++i) {
        if (!rule_check_flag(&rules[i], RULE_ONE_SHOT) || plan[i].pid || plan[i].remove) continue;
        int match = rule_reservation_app_match(&rules[i], application->name);
        if (match != 0 && match != REG_NOMATCH) return false;
        bool excluded = rule_check_flag(&rules[i], RULE_APP_EXCLUDE);
        if ((match == 0) != excluded) {
            plan[i].pid = application->pid;
            plan[i].psn_high = (uint32_t) application->psn.highLongOfPSN;
            plan[i].psn_low = (uint32_t) application->psn.lowLongOfPSN;
        }
    }
    return true;
}

static void rule_owner_plan_commit(struct rule_one_shot_owner_plan *plan,
                                   struct rule *rules, size_t count, bool allow_remove)
{
    for (size_t i = 0; i < count; ++i) {
        if (plan[i].remove) continue;
        rules[i].one_shot_owner_pid = plan[i].pid;
        rules[i].one_shot_owner_psn_high = plan[i].psn_high;
        rules[i].one_shot_owner_psn_low = plan[i].psn_low;
    }
    if (allow_remove) {
        for (size_t i = count; i > 0; --i) if (plan[i - 1].remove) rule_remove_one_shot_at((int) i - 1);
    }
}

bool rule_reserve_one_shots_in_buffer(struct rule *rules, size_t count, struct application *application)
{
    if (!application) return false;
    if (!count) return true;
    struct rule_one_shot_owner_plan *plan = rule_reservation_calloc(count, sizeof(*plan));
    if (!plan) return false;
    rule_owner_plan_init(plan, rules, count);
    bool result = rule_owner_plan_reserve(plan, rules, count, application);
    if (result) rule_owner_plan_commit(plan, rules, count, false);
    free(plan);
    return result;
}

bool rule_reserve_one_shots(struct application *application)
{
    bool result = rule_reserve_one_shots_in_buffer(g_window_manager.rules,
                                                   (size_t) buf_len(g_window_manager.rules), application);
    if (!result) rule_one_shot_reassignment_pending = true;
    return result;
}

static bool rule_refresh_application_is_live(struct application *application)
{
    if (!application) return false;
    bool registered = false;
    table_for (struct application *candidate, g_window_manager.application, {
        if (candidate == application) registered = true;
    });
    return registered && application->launch_refresh && application->pid > 0 &&
           window_manager_find_application(&g_window_manager, application->pid) == application;
}

static bool rule_owner_plan_has_live_owner(const struct rule_one_shot_owner_plan *owner)
{
    int count = buf_len(g_window_manager.applications_to_refresh);
    for (int i = 0; i < count; ++i) {
        struct application *application = g_window_manager.applications_to_refresh[i];
        if (rule_refresh_application_is_live(application) && owner->pid == application->pid &&
            owner->psn_high == (uint32_t) application->psn.highLongOfPSN &&
            owner->psn_low == (uint32_t) application->psn.lowLongOfPSN) return true;
    }
    return false;
}

static bool rule_owner_plan_reserve_queue(struct rule_one_shot_owner_plan *plan,
                                          struct rule *rules, size_t rule_count,
                                          struct application *skip)
{
    int count = buf_len(g_window_manager.applications_to_refresh);
    if (!count) return true;
    int cursor = g_window_manager.application_refresh_cursor % count;
    if (cursor < 0) cursor += count;
    for (int offset = 0; offset < count; ++offset) {
        struct application *application = g_window_manager.applications_to_refresh[(cursor + offset) % count];
        if (application == skip || !rule_refresh_application_is_live(application)) continue;
        bool duplicate = false;
        for (int previous = 0; previous < offset; ++previous) {
            if (g_window_manager.applications_to_refresh[(cursor + previous) % count] == application) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && !rule_owner_plan_reserve(plan, rules, rule_count, application)) return false;
    }
    return true;
}

static bool rule_plan_release_and_handoff(struct application *application)
{
    size_t count = (size_t) buf_len(g_window_manager.rules);
    if (!count) { rule_one_shot_reassignment_pending = false; return true; }
    struct rule_one_shot_owner_plan *plan = rule_reservation_calloc(count, sizeof(*plan));
    if (!plan) { rule_one_shot_reassignment_pending = true; return false; }
    rule_owner_plan_init(plan, g_window_manager.rules, count);
    for (size_t i = 0; i < count; ++i) {
        bool release = rule_one_shot_owned_by(&g_window_manager.rules[i], application) ||
                       (plan[i].pid && !rule_owner_plan_has_live_owner(&plan[i]));
        if (!release) continue;
        if (g_window_manager.rules[i].one_shot_window_id ||
            rule_check_flag(&g_window_manager.rules[i], RULE_ONE_SHOT_REMOVE)) {
            plan[i].remove = true;
        } else {
            plan[i].pid = 0;
            plan[i].psn_high = 0;
            plan[i].psn_low = 0;
        }
    }
    bool result = rule_owner_plan_reserve_queue(plan, g_window_manager.rules, count, application);
    if (result) {
        rule_owner_plan_commit(plan, g_window_manager.rules, count, true);
        rule_one_shot_reassignment_pending = false;
    } else {
        rule_one_shot_reassignment_pending = true;
    }
    free(plan);
    return result;
}

bool rule_retry_pending_one_shots(void)
{
    if (!rule_one_shot_reassignment_pending) return true;
    size_t count = (size_t) buf_len(g_window_manager.rules);
    if (!count) { rule_one_shot_reassignment_pending = false; return true; }
    struct rule_one_shot_owner_plan *plan = rule_reservation_calloc(count, sizeof(*plan));
    if (!plan) return false;
    rule_owner_plan_init(plan, g_window_manager.rules, count);
    for (size_t i = 0; i < count; ++i) {
        if (!plan[i].pid || rule_owner_plan_has_live_owner(&plan[i])) continue;
        if (g_window_manager.rules[i].one_shot_window_id ||
            rule_check_flag(&g_window_manager.rules[i], RULE_ONE_SHOT_REMOVE)) {
            plan[i].remove = true;
        } else {
            plan[i].pid = 0;
            plan[i].psn_high = 0;
            plan[i].psn_low = 0;
        }
    }
    bool result = rule_owner_plan_reserve_queue(plan, g_window_manager.rules, count, NULL);
    if (result) {
        rule_owner_plan_commit(plan, g_window_manager.rules, count, true);
        rule_one_shot_reassignment_pending = false;
    }
    free(plan);
    return result;
}

bool rule_one_shot_reassignment_is_pending(void) { return rule_one_shot_reassignment_pending; }

bool rule_finalize_one_shots(struct application *application)
{
    return rule_plan_release_and_handoff(application);
}

bool rule_cancel_one_shots(struct application *application)
{
    return rule_plan_release_and_handoff(application);
}

void rule_cleanup_consumed_one_shots(struct application *application)
{
    for (int i = 0; i < buf_len(g_window_manager.rules);) {
        struct rule *rule = &g_window_manager.rules[i];
        if (rule_one_shot_owned_by(rule, application) && rule->one_shot_window_id) {
            rule_remove_one_shot_at(i);
        } else {
            ++i;
        }
    }
}

void rule_cleanup_one_shots(void)
{
    for (int i = 0; i < buf_len(g_window_manager.rules);) {
        struct rule *rule = &g_window_manager.rules[i];
        if (!rule_check_flag(rule, RULE_ONE_SHOT_REMOVE) || rule->one_shot_owner_pid) {
            ++i;
        } else {
            rule_remove_one_shot_at(i);
        }
    }
}

void rule_destroy(struct rule *rule)
{
    if (rule_check_flag(rule, RULE_APP_VALID))     regfree(&rule->app_regex);
    if (rule_check_flag(rule, RULE_TITLE_VALID))   regfree(&rule->title_regex);
    if (rule_check_flag(rule, RULE_ROLE_VALID))    regfree(&rule->role_regex);
    if (rule_check_flag(rule, RULE_SUBROLE_VALID)) regfree(&rule->subrole_regex);

    rule_free(rule->label);
    rule_free(rule->app);
    rule_free(rule->title);
    rule_free(rule->role);
    rule_free(rule->subrole);

    rule_free(rule->effects.scratchpad);
    rule_free(rule->display_uuid);
    rule_free(rule->space_uuid);
    memset(rule, 0, sizeof(*rule));
}
