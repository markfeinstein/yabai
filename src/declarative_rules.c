#ifdef TESTS
static size_t declarative_rules_allocation_ordinal;
static size_t declarative_rules_fail_ordinal;
static void **declarative_rules_allocations;
static size_t declarative_rules_allocation_count;
static size_t declarative_rules_allocation_capacity;
static size_t declarative_rules_topology_validation_count;
static size_t declarative_rules_target_lookup_count;

static bool declarative_rules_should_fail(void)
{
    return ++declarative_rules_allocation_ordinal == declarative_rules_fail_ordinal;
}

static bool declarative_rules_track(void *value)
{
    if (!value) return true;
    if (declarative_rules_allocation_count == declarative_rules_allocation_capacity) {
        size_t capacity = declarative_rules_allocation_capacity ? declarative_rules_allocation_capacity * 2 : 256;
        if (capacity < declarative_rules_allocation_capacity || capacity > SIZE_MAX / sizeof(*declarative_rules_allocations)) return false;
        void **allocations = realloc(declarative_rules_allocations, capacity * sizeof(*allocations));
        if (!allocations) return false;
        declarative_rules_allocations = allocations;
        declarative_rules_allocation_capacity = capacity;
    }
    declarative_rules_allocations[declarative_rules_allocation_count++] = value;
    return true;
}

void declarative_rules_test_note_free(void *value)
{
    if (!value) return;
    for (size_t i = 0; i < declarative_rules_allocation_count; ++i) {
        if (declarative_rules_allocations[i] == value) {
            declarative_rules_allocations[i] = declarative_rules_allocations[--declarative_rules_allocation_count];
            if (declarative_rules_allocation_count == 0) {
                free(declarative_rules_allocations);
                declarative_rules_allocations = NULL;
                declarative_rules_allocation_capacity = 0;
            }
            return;
        }
    }
}

void declarative_rules_test_fail_allocation_at(size_t ordinal)
{
    declarative_rules_allocation_ordinal = 0;
    declarative_rules_fail_ordinal = ordinal;
}

size_t declarative_rules_test_outstanding_allocations(void) { return declarative_rules_allocation_count; }

void declarative_rules_test_reset_allocator(void)
{
    declarative_rules_allocation_ordinal = 0;
    declarative_rules_fail_ordinal = 0;
}

void declarative_rules_test_reset_operation_counts(void)
{
    declarative_rules_topology_validation_count = 0;
    declarative_rules_target_lookup_count = 0;
}

size_t declarative_rules_test_topology_validation_count(void) { return declarative_rules_topology_validation_count; }
size_t declarative_rules_test_target_lookup_count(void) { return declarative_rules_target_lookup_count; }
#else
static bool declarative_rules_should_fail(void) { return false; }
#endif

static void *declarative_rules_calloc(size_t count, size_t size)
{
    if (declarative_rules_should_fail()) return NULL;
    void *value = calloc(count, size);
#ifdef TESTS
    if (!declarative_rules_track(value)) {
        free(value);
        return NULL;
    }
#endif
    return value;
}

static char *declarative_rules_strdup(const char *value)
{
    if (!value) return NULL;
    if (declarative_rules_should_fail()) return NULL;
    char *copy = strdup(value);
#ifdef TESTS
    if (!declarative_rules_track(copy)) {
        free(copy);
        return NULL;
    }
#endif
    return copy;
}

static void declarative_rules_free(void *value)
{
#ifdef TESTS
    declarative_rules_test_note_free(value);
#endif
    free(value);
}

static bool declarative_rule_usable_uuid(const char *uuid)
{
    return uuid && uuid[0] != '\0';
}

static bool declarative_rule_space_identity(const struct declarative_rule_space_entry *space,
                                            struct declarative_space_identity *identity)
{
    if (!space || !identity) return false;
    *identity = space->identity;
    if (declarative_space_identity_valid(identity)) return true;
    if (!space->uuid || !space->uuid[0] || strlen(space->uuid) >= sizeof(identity->uuid)) return false;
    *identity = (struct declarative_space_identity) {
        .has_uuid = true, .sid = space->sid, .kind = space->kind,
        .mission_control_ordinal = space->mission_control_ordinal
    };
    memcpy(identity->uuid, space->uuid, strlen(space->uuid) + 1);
    identity->fingerprint = declarative_space_identity_fingerprint(identity);
    return declarative_space_identity_valid(identity);
}

bool declarative_rule_topology_validate(const struct declarative_rule_topology_snapshot *snapshot)
{
#ifdef TESTS
    ++declarative_rules_topology_validation_count;
#endif
    if (!snapshot || (!snapshot->displays && snapshot->display_count) ||
        (!snapshot->spaces && snapshot->space_count) || snapshot->display_count == 0 ||
        snapshot->space_count == 0 || snapshot->display_count > DECLARATIVE_CONFIG_MAX_SPACES ||
        snapshot->space_count > DECLARATIVE_CONFIG_MAX_SPACES) return false;

    for (size_t i = 0; i < snapshot->display_count; ++i) {
        const struct declarative_rule_display_entry *display = &snapshot->displays[i];
        if (!display->did || !declarative_rule_usable_uuid(display->uuid) ||
            !isfinite(display->center.x) || !isfinite(display->center.y) ||
            display->managed_ordinal < 0 || (size_t) display->managed_ordinal >= snapshot->display_count) return false;
        for (size_t j = 0; j < i; ++j) {
            if (snapshot->displays[j].did == display->did ||
                strcmp(snapshot->displays[j].uuid, display->uuid) == 0 ||
                snapshot->displays[j].managed_ordinal == display->managed_ordinal) return false;
        }
    }

    bool display_has_space[snapshot->display_count];
    memset(display_has_space, 0, sizeof(display_has_space));
    for (size_t i = 0; i < snapshot->space_count; ++i) {
        const struct declarative_rule_space_entry *space = &snapshot->spaces[i];
        struct declarative_space_identity identity = {0};
        if (!space->sid || !space->uuid || space->kind < 0 ||
            space->mission_control_ordinal < 0 || (size_t) space->mission_control_ordinal >= snapshot->space_count ||
            !space->parent_did || !declarative_rule_usable_uuid(space->parent_display_uuid) ||
            space->parent_display_ordinal < 0 || (size_t) space->parent_display_ordinal >= snapshot->display_count ||
            !declarative_rule_space_identity(space, &identity)) return false;

        const struct declarative_rule_display_entry *parent = NULL;
        for (size_t j = 0; j < snapshot->display_count; ++j) {
            const struct declarative_rule_display_entry *candidate = &snapshot->displays[j];
            if (candidate->did == space->parent_did) parent = candidate;
        }
        if (!parent || parent->managed_ordinal != space->parent_display_ordinal ||
            strcmp(parent->uuid, space->parent_display_uuid) != 0 ||
            identity.sid != space->sid || identity.kind != space->kind ||
            identity.mission_control_ordinal != space->mission_control_ordinal ||
            strcmp(identity.uuid, space->uuid) != 0 ||
            (!identity.has_uuid &&
             (identity.parent_did != space->parent_did ||
              identity.parent_display_ordinal != space->parent_display_ordinal ||
              strcmp(identity.parent_display_uuid, space->parent_display_uuid) != 0))) return false;
        display_has_space[parent->managed_ordinal] = true;

        for (size_t j = 0; j < i; ++j) {
            bool duplicate_uuid = space->uuid[0] && snapshot->spaces[j].uuid[0] &&
                                  strcmp(snapshot->spaces[j].uuid, space->uuid) == 0;
            struct declarative_space_identity previous_identity = {0};
            if (!declarative_rule_space_identity(&snapshot->spaces[j], &previous_identity)) return false;
            bool same_identity = declarative_space_identity_equal(&previous_identity, &identity);
            bool collision = previous_identity.fingerprint == identity.fingerprint && !same_identity;
            if (snapshot->spaces[j].sid == space->sid || duplicate_uuid || same_identity || collision ||
                snapshot->spaces[j].mission_control_ordinal == space->mission_control_ordinal) return false;
        }
    }
    for (size_t i = 0; i < snapshot->display_count; ++i) if (!display_has_space[i]) return false;
    return true;
}

struct rule_fold_context
{
    struct declarative_rule_display_entry *displays;
    size_t display_count;
    struct declarative_rule_space_entry *spaces;
    size_t space_count;
};

void declarative_rule_fold_context_destroy(struct rule_fold_context *context)
{
    if (!context) return;
    if (context->displays) {
        for (size_t i = 0; i < context->display_count; ++i) {
            declarative_rules_free((void *) context->displays[i].uuid);
        }
    }
    if (context->spaces) {
        for (size_t i = 0; i < context->space_count; ++i) {
            declarative_rules_free((void *) context->spaces[i].uuid);
            declarative_rules_free((void *) context->spaces[i].parent_display_uuid);
        }
    }
    declarative_rules_free(context->displays);
    declarative_rules_free(context->spaces);
    declarative_rules_free(context);
}

struct rule_fold_context *declarative_rule_fold_context_create(const struct declarative_rule_topology_snapshot *snapshot)
{
    if (!declarative_rule_topology_validate(snapshot)) return NULL;

    struct rule_fold_context *context = declarative_rules_calloc(1, sizeof(*context));
    if (!context) return NULL;
    context->display_count = snapshot->display_count;
    context->space_count = snapshot->space_count;
    context->displays = declarative_rules_calloc(context->display_count, sizeof(*context->displays));
    context->spaces = declarative_rules_calloc(context->space_count, sizeof(*context->spaces));
    if (!context->displays || !context->spaces) goto fail;

    for (size_t i = 0; i < context->display_count; ++i) {
        context->displays[i] = snapshot->displays[i];
        context->displays[i].uuid = declarative_rules_strdup(snapshot->displays[i].uuid);
        if (!context->displays[i].uuid) goto fail;
    }
    for (size_t i = 0; i < context->space_count; ++i) {
        context->spaces[i] = snapshot->spaces[i];
        if (!declarative_rule_space_identity(&snapshot->spaces[i], &context->spaces[i].identity)) goto fail;
        context->spaces[i].uuid = declarative_rules_strdup(snapshot->spaces[i].uuid);
        context->spaces[i].parent_display_uuid = declarative_rules_strdup(snapshot->spaces[i].parent_display_uuid);
        if (!context->spaces[i].uuid || !context->spaces[i].parent_display_uuid) goto fail;
    }
    return context;

fail:
    declarative_rule_fold_context_destroy(context);
    return NULL;
}

bool rule_fold_context_target_is_valid(const struct rule_fold_context *context, enum rule_target_kind target,
                                       uint64_t id, const char *uuid, int kind,
                                       const struct declarative_space_identity *identity)
{
    if (!context || !uuid || !id) return false;
    if (target == RULE_TARGET_DISPLAY) {
        for (size_t i = 0; i < context->display_count; ++i) {
#ifdef TESTS
            ++declarative_rules_target_lookup_count;
#endif
            if (context->displays[i].did == id) return strcmp(context->displays[i].uuid, uuid) == 0;
        }
    } else if (target == RULE_TARGET_SPACE) {
        for (size_t i = 0; i < context->space_count; ++i) {
#ifdef TESTS
            ++declarative_rules_target_lookup_count;
#endif
            if (context->spaces[i].sid == id) {
                if (context->spaces[i].kind != kind) return false;
                if (declarative_space_identity_valid(identity)) {
                    return declarative_space_identity_equal(&context->spaces[i].identity, identity);
                }
                return uuid && uuid[0] && strcmp(context->spaces[i].uuid, uuid) == 0;
            }
        }
    }
    return false;
}

static int declarative_rule_display_compare(const struct declarative_rule_display_entry *a,
                                            const struct declarative_rule_display_entry *b, int order)
{
    double ap = order == DISPLAY_ARRANGEMENT_ORDER_Y ? a->center.y : a->center.x;
    double bp = order == DISPLAY_ARRANGEMENT_ORDER_Y ? b->center.y : b->center.x;
    if (ap < bp) return -1;
    if (ap > bp) return 1;
    double as = order == DISPLAY_ARRANGEMENT_ORDER_Y ? a->center.x : a->center.y;
    double bs = order == DISPLAY_ARRANGEMENT_ORDER_Y ? b->center.x : b->center.y;
    if (as < bs) return -1;
    if (as > bs) return 1;
    return (a->managed_ordinal > b->managed_ordinal) - (a->managed_ordinal < b->managed_ordinal);
}

static char *declarative_rule_strdup(const char *value)
{
    return declarative_rules_strdup(value);
}

static bool declarative_rule_copy_matcher(struct rule *rule, int index, const char *pattern, bool exclude)
{
    char **strings[] = { &rule->app, &rule->title, &rule->role, &rule->subrole };
    regex_t *regexes[] = { &rule->app_regex, &rule->title_regex, &rule->role_regex, &rule->subrole_regex };
    enum rule_flag valid[] = { RULE_APP_VALID, RULE_TITLE_VALID, RULE_ROLE_VALID, RULE_SUBROLE_VALID };
    enum rule_flag excluded[] = { RULE_APP_EXCLUDE, RULE_TITLE_EXCLUDE, RULE_ROLE_EXCLUDE, RULE_SUBROLE_EXCLUDE };
    *strings[index] = declarative_rule_strdup(pattern);
    if (!*strings[index]) return false;
    if (regcomp(regexes[index], *strings[index], REG_EXTENDED) != 0) return false;
    rule_set_flag(rule, valid[index]);
    if (exclude) rule_set_flag(rule, excluded[index]);
    return true;
}

static bool declarative_rule_clone(const struct rule *source, struct rule *destination)
{
    memset(destination, 0, sizeof(*destination));
    destination->owner = source->owner;
    destination->source_rule_index = source->source_rule_index;
    destination->generation = source->generation;
    destination->target_unavailable = source->target_unavailable;
    destination->expected_space_kind = source->expected_space_kind;
    destination->space_identity = source->space_identity;
    destination->warning_state = source->warning_state;
    destination->effects = source->effects;
    destination->effects.scratchpad = NULL;
    destination->flags = source->flags & ~(RULE_APP_VALID | RULE_TITLE_VALID | RULE_ROLE_VALID | RULE_SUBROLE_VALID | RULE_ONE_SHOT_REMOVE);

    if ((source->label && !(destination->label = declarative_rule_strdup(source->label))) ||
        (source->display_uuid && !(destination->display_uuid = declarative_rule_strdup(source->display_uuid))) ||
        (source->space_uuid && !(destination->space_uuid = declarative_rule_strdup(source->space_uuid))) ||
        (source->effects.scratchpad && !(destination->effects.scratchpad = declarative_rule_strdup(source->effects.scratchpad)))) goto fail;

    const char *patterns[] = { source->app, source->title, source->role, source->subrole };
    enum rule_flag valid[] = { RULE_APP_VALID, RULE_TITLE_VALID, RULE_ROLE_VALID, RULE_SUBROLE_VALID };
    enum rule_flag excluded[] = { RULE_APP_EXCLUDE, RULE_TITLE_EXCLUDE, RULE_ROLE_EXCLUDE, RULE_SUBROLE_EXCLUDE };
    for (int i = 0; i < 4; ++i) {
        if (rule_check_flag((struct rule *) source, valid[i]) &&
            !declarative_rule_copy_matcher(destination, i, patterns[i], rule_check_flag((struct rule *) source, excluded[i]))) goto fail;
    }
    return true;
fail:
    rule_destroy(destination);
    return false;
}

static enum declarative_rule_prepare_result declarative_rule_from_model(const struct declarative_rule_model *model,
                                                                         size_t model_index, uint64_t generation,
                                                                         const struct declarative_rule_display_entry *display,
                                                                         const struct declarative_rule_space_entry *space,
                                                                         bool target_unavailable,
                                                                         struct rule *rule)
{
    memset(rule, 0, sizeof(*rule));
    rule->owner = RULE_OWNER_NATIVE;
    rule->source_rule_index = (uint32_t) model_index;
    rule->generation = generation;
    rule->target_unavailable = target_unavailable;
    if (model->label && !(rule->label = declarative_rule_strdup(model->label))) goto allocation_failure;
    for (int i = 0; i < 4; ++i) {
        if (model->matcher_present[i] && !declarative_rule_copy_matcher(rule, i, model->matchers[i].pattern, model->matchers[i].exclude)) {
            if ((i == 0 && rule->app) || (i == 1 && rule->title) || (i == 2 && rule->role) || (i == 3 && rule->subrole)) {
                rule_destroy(rule);
                return DECLARATIVE_RULE_PREPARE_INVALID_REGEX;
            }
            goto allocation_failure;
        }
    }
    if (model->one_shot) rule_set_flag(rule, RULE_ONE_SHOT);
    if (display) {
        rule->effects.did = display->did;
        if (!(rule->display_uuid = declarative_rule_strdup(display->uuid))) goto allocation_failure;
    }
    if (space) {
        rule->effects.sid = space->sid;
        rule->expected_space_kind = space->kind;
        if (!declarative_rule_space_identity(space, &rule->space_identity)) goto allocation_failure;
        if (!(rule->space_uuid = declarative_rule_strdup(space->uuid))) goto allocation_failure;
    }
    if ((model->display_present || model->space_present) && model->follow) rule_effects_set_flag(&rule->effects, RULE_FOLLOW_SPACE);
    if (model->grid_present) memcpy(rule->effects.grid, model->grid, sizeof(model->grid));
    if (model->opacity_present) { rule->effects.opacity = model->opacity; rule_effects_set_flag(&rule->effects, RULE_OPACITY); }
    rule->effects.manage = model->scratchpad ? RULE_PROP_OFF :
                           model->manage_present ? (model->manage ? RULE_PROP_ON : RULE_PROP_OFF) : RULE_PROP_UD;
    rule->effects.sticky = model->sticky_present ? (model->sticky ? RULE_PROP_ON : RULE_PROP_OFF) : RULE_PROP_UD;
    rule->effects.mff = model->mff_present ? (model->mff ? RULE_PROP_ON : RULE_PROP_OFF) : RULE_PROP_UD;
    rule->effects.fullscreen = model->fullscreen_present ? (model->fullscreen ? RULE_PROP_ON : RULE_PROP_OFF) : RULE_PROP_UD;
    if (model->layer_present) { rule->effects.layer = model->layer; rule_effects_set_flag(&rule->effects, RULE_LAYER); }
    if (model->scratchpad && !(rule->effects.scratchpad = declarative_rule_strdup(model->scratchpad))) goto allocation_failure;
    return DECLARATIVE_RULE_PREPARE_OK;
allocation_failure:
    rule_destroy(rule);
    return DECLARATIVE_RULE_PREPARE_ALLOCATION_FAILED;
}

void declarative_prepared_rules_destroy(struct declarative_prepared_rules *prepared)
{
    if (!prepared) return;
    for (size_t i = 0; i < prepared->count; ++i) {
        if (prepared->templates) rule_destroy(&prepared->templates[i]);
        if (prepared->live && prepared->live_present && prepared->live_present[i]) rule_destroy(&prepared->live[i]);
    }
    declarative_rules_free(prepared->templates);
    declarative_rules_free(prepared->live);
    declarative_rules_free(prepared->live_present);
    declarative_rules_free(prepared->warning_states);
    memset(prepared, 0, sizeof(*prepared));
}

bool declarative_prepared_rule_rearm(struct declarative_prepared_rules *prepared, size_t index)
{
    if (!prepared || index >= prepared->count) return false;
    struct rule replacement = {0};
    if (!declarative_rule_clone(&prepared->templates[index], &replacement)) return false;
    if (prepared->live_present[index]) rule_destroy(&prepared->live[index]);
    prepared->live[index] = replacement;
    prepared->live_present[index] = true;
    return true;
}

enum declarative_rule_prepare_result declarative_prepare_rules(const struct declarative_rule_model *models, size_t count,
                                                                const struct declarative_rule_topology_snapshot *snapshot,
                                                                int display_order, uint64_t generation,
                                                                const char *const *runtime_labels, size_t runtime_label_count,
                                                                struct declarative_prepared_rules *prepared)
{
    if (!prepared) return DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY;
    memset(prepared, 0, sizeof(*prepared));
    if (generation == 0) return DECLARATIVE_RULE_PREPARE_INVALID_GENERATION;
    if (!declarative_rule_topology_validate(snapshot) ||
        display_order < DISPLAY_ARRANGEMENT_ORDER_DEFAULT || display_order > DISPLAY_ARRANGEMENT_ORDER_Y)
        return DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY;
    if ((!models && count) || (!runtime_labels && runtime_label_count) ||
        count > DECLARATIVE_CONFIG_MAX_RULES) return DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY;

    for (size_t i = 0; i < count; ++i) {
        if (!models[i].label) continue;
        for (size_t j = 0; j < i; ++j) if (models[j].label && strcmp(models[i].label, models[j].label) == 0)
            return DECLARATIVE_RULE_PREPARE_LABEL_COLLISION;
        for (size_t j = 0; j < runtime_label_count; ++j) if (runtime_labels[j] && strcmp(models[i].label, runtime_labels[j]) == 0)
            return DECLARATIVE_RULE_PREPARE_LABEL_COLLISION;
    }

    const struct declarative_rule_display_entry **ordered = declarative_rules_calloc(snapshot->display_count, sizeof(*ordered));
    if (!ordered) return DECLARATIVE_RULE_PREPARE_ALLOCATION_FAILED;
    for (size_t i = 0; i < snapshot->display_count; ++i) ordered[snapshot->displays[i].managed_ordinal] = &snapshot->displays[i];
    if (display_order != DISPLAY_ARRANGEMENT_ORDER_DEFAULT) {
        for (size_t i = 1; i < snapshot->display_count; ++i) {
            const struct declarative_rule_display_entry *entry = ordered[i];
            size_t j = i;
            while (j > 0 && declarative_rule_display_compare(entry, ordered[j - 1], display_order) < 0) {
                ordered[j] = ordered[j - 1];
                --j;
            }
            ordered[j] = entry;
        }
    }

    prepared->templates = declarative_rules_calloc(count, sizeof(*prepared->templates));
    prepared->live = declarative_rules_calloc(count, sizeof(*prepared->live));
    prepared->live_present = declarative_rules_calloc(count, sizeof(*prepared->live_present));
    prepared->warning_states = declarative_rules_calloc(count, sizeof(*prepared->warning_states));
    prepared->count = count;
    if (count && (!prepared->templates || !prepared->live || !prepared->live_present || !prepared->warning_states)) {
        declarative_rules_free(ordered);
        declarative_prepared_rules_destroy(prepared);
        return DECLARATIVE_RULE_PREPARE_ALLOCATION_FAILED;
    }

    enum declarative_rule_prepare_result result = DECLARATIVE_RULE_PREPARE_OK;
    for (size_t i = 0; i < count; ++i) {
        const struct declarative_rule_display_entry *display = NULL;
        const struct declarative_rule_space_entry *space = NULL;
        bool target_unavailable = false;
        if (models[i].display_present && models[i].display_index <= 0) {
            result = DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY;
            break;
        }
        if (models[i].space_present && models[i].space_index <= 0) {
            result = DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY;
            break;
        }

        /* Space is the effective target when both are authored. An absent
         * lower-priority display must not invalidate or suppress a valid Space. */
        if (models[i].space_present) {
            if ((size_t) models[i].space_index > snapshot->space_count) {
                target_unavailable = true;
            } else {
                for (size_t j = 0; j < snapshot->space_count; ++j) {
                    if (snapshot->spaces[j].mission_control_ordinal == models[i].space_index - 1) {
                        space = &snapshot->spaces[j];
                        break;
                    }
                }
                if (!space) { result = DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY; break; }
            }
            if (space && models[i].display_present && (size_t) models[i].display_index <= snapshot->display_count) {
                display = ordered[models[i].display_index - 1];
                if (!display) { result = DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY; break; }
            }
        } else if (models[i].display_present) {
            if ((size_t) models[i].display_index > snapshot->display_count) {
                target_unavailable = true;
            } else {
                display = ordered[models[i].display_index - 1];
                if (!display) { result = DECLARATIVE_RULE_PREPARE_INVALID_TOPOLOGY; break; }
            }
        }
        result = declarative_rule_from_model(&models[i], i, generation, display, space,
                                             target_unavailable, &prepared->templates[i]);
        if (result != DECLARATIVE_RULE_PREPARE_OK) break;
        prepared->templates[i].warning_state = &prepared->warning_states[i];
        if (!declarative_rule_clone(&prepared->templates[i], &prepared->live[i])) { result = DECLARATIVE_RULE_PREPARE_ALLOCATION_FAILED; break; }
        prepared->live_present[i] = true;
    }
    declarative_rules_free(ordered);
    if (result != DECLARATIVE_RULE_PREPARE_OK) declarative_prepared_rules_destroy(prepared);
    return result;
}
