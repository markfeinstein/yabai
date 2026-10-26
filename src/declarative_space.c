#ifdef TESTS
#define DECLARATIVE_SPACE_INTERNAL
#else
#define DECLARATIVE_SPACE_INTERNAL static
#endif

static size_t declarative_space_allocation_ordinal;
static size_t declarative_space_fail_ordinal;
static size_t declarative_space_outstanding;
static _Thread_local bool declarative_space_allocation_failed;
static _Thread_local enum declarative_space_apply_result declarative_space_prepare_failure;
#ifdef TESTS
static bool declarative_space_commit_active;
static size_t declarative_space_commit_forbidden_calls;
static size_t declarative_space_index_operations;
#define DECLARATIVE_SPACE_INDEX_OPERATION() (++declarative_space_index_operations)
#else
#define DECLARATIVE_SPACE_INDEX_OPERATION() ((void) 0)
#endif

static void *declarative_space_allocate(size_t count, size_t size)
{
#ifdef TESTS
    if (declarative_space_commit_active) ++declarative_space_commit_forbidden_calls;
#endif
    if (count && size > SIZE_MAX / count) { declarative_space_allocation_failed = true; return NULL; }
    ++declarative_space_allocation_ordinal;
#ifdef TESTS
    if (declarative_space_fail_ordinal && declarative_space_allocation_ordinal == declarative_space_fail_ordinal) {
        declarative_space_allocation_failed = true;
        return NULL;
    }
#endif
    void *result = calloc(count, size);
    if (!result) declarative_space_allocation_failed = true;
    if (result) ++declarative_space_outstanding;
    return result;
}

static void declarative_space_free(void *pointer)
{
    if (!pointer) return;
#ifdef TESTS
    if (declarative_space_commit_active) ++declarative_space_commit_forbidden_calls;
#endif
    free(pointer);
    --declarative_space_outstanding;
}

#ifdef TESTS
void declarative_space_test_fail_allocation_at(size_t ordinal)
{
    declarative_space_allocation_ordinal = 0;
    declarative_space_fail_ordinal = ordinal;
}

size_t declarative_space_test_allocation_count(void)
{
    return declarative_space_allocation_ordinal;
}

size_t declarative_space_test_outstanding_allocations(void)
{
    return declarative_space_outstanding;
}

void declarative_space_test_reset_index_operations(void)
{
    declarative_space_index_operations = 0;
}

size_t declarative_space_test_index_operations(void)
{
    return declarative_space_index_operations;
}
#endif

static uint64_t declarative_space_legacy_flag(enum declarative_runtime_space_field field)
{
    static const uint64_t flags[DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT] = {
        VIEW_LAYOUT, VIEW_TOP_PADDING, VIEW_BOTTOM_PADDING, VIEW_LEFT_PADDING,
        VIEW_RIGHT_PADDING, VIEW_WINDOW_GAP, VIEW_ACCORDION_PADDING, VIEW_SINGLE_WINDOW_MAX_WIDTH,
        VIEW_SINGLE_WINDOW_MIN_WIDTH, VIEW_SINGLE_WINDOW_WIDTH_RATIO, VIEW_SPLIT_RATIO,
        VIEW_SPLIT_TYPE, VIEW_AUTO_BALANCE
    };
    return flags[field];
}

static union declarative_runtime_scalar declarative_space_scalar(enum declarative_config_id id,
                                                                  const struct declarative_value *value)
{
    union declarative_runtime_scalar scalar = { 0 };
    switch (id) {
    case DECLARATIVE_CONFIG_LAYOUT:
    case DECLARATIVE_CONFIG_SPLIT_TYPE: scalar.integer = value->enumeration; break;
    case DECLARATIVE_CONFIG_SINGLE_WINDOW_WIDTH_RATIO:
    case DECLARATIVE_CONFIG_SPLIT_RATIO: scalar.real = value->real; break;
    case DECLARATIVE_CONFIG_AUTO_BALANCE: scalar.u32 = value->u32; break;
    default: scalar.integer = value->integer; break;
    }
    return scalar;
}

static void declarative_space_set_value(struct declarative_space_effective *effective,
                                        enum declarative_runtime_space_field field,
                                        enum declarative_setting_source source,
                                        union declarative_runtime_scalar value)
{
    effective->fields[field] = (struct declarative_runtime_cell) {
        .present = true,
        .source = source,
        .value = value
    };
}

static void declarative_space_set_native(struct declarative_space_effective *effective,
                                         const struct declarative_value *settings,
                                         enum declarative_setting_source source)
{
    for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) {
        int field = declarative_space_field_for_id(id);
        if (field >= 0 && settings[id].present) {
            declarative_space_set_value(effective, field, source,
                                        declarative_space_scalar(id, &settings[id]));
        }
    }
}

static void declarative_space_set_current_non_user(const struct view *view,
                                                   struct declarative_space_effective *effective)
{
    union declarative_runtime_scalar value = { 0 };
    value.integer = VIEW_FLOAT;
    declarative_space_set_value(effective, DECLARATIVE_RUNTIME_SPACE_LAYOUT,
                                DECLARATIVE_SETTING_BUILTIN, value);
#define SET_CURRENT(field, member, scalar_member) do { \
    value.scalar_member = view->member; \
    declarative_space_set_value(effective, field, DECLARATIVE_SETTING_BUILTIN, value); \
} while (0)
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_TOP_PADDING, top_padding, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING, bottom_padding, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING, left_padding, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING, right_padding, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP, window_gap, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING, accordion_padding, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MAX_WIDTH, single_window_max_width, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MIN_WIDTH, single_window_min_width, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_WIDTH_RATIO, single_window_width_ratio, real);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO, split_ratio, real);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE, split_type, integer);
    SET_CURRENT(DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE, auto_balance, u32);
#undef SET_CURRENT
}

bool declarative_space_resolve_effective_policy(const struct declarative_config_model *native,
                                                const struct declarative_space_policy *policies,
                                                size_t policy_count,
                                                const struct declarative_runtime_state *runtime,
                                                const struct declarative_space_defaults *defaults,
                                                const struct declarative_space_target *target,
                                                enum declarative_space_ownership_mode mode,
                                                struct declarative_space_effective *out)
{
    if (!defaults || !target || !target->view || !target->uuid || !out || target->sid == 0 || target->index <= 0) return false;
    if (mode != DECLARATIVE_SPACE_PRESERVE_RUNTIME && mode != DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD) return false;
    memset(out, 0, sizeof(*out));

    union declarative_runtime_scalar value = { 0 };
#define SET_DEFAULT(field, member, scalar_member) do { \
    value.scalar_member = defaults->member; \
    declarative_space_set_value(out, field, DECLARATIVE_SETTING_BUILTIN, value); \
} while (0)
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_LAYOUT, layout, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_TOP_PADDING, top_padding, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING, bottom_padding, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING, left_padding, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING, right_padding, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP, window_gap, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING, accordion_padding, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MAX_WIDTH, single_window_max_width, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MIN_WIDTH, single_window_min_width, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_WIDTH_RATIO, single_window_width_ratio, real);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO, split_ratio, real);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE, split_type, integer);
    SET_DEFAULT(DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE, auto_balance, u32);
#undef SET_DEFAULT

    if (!target->is_user_space) {
        declarative_space_set_current_non_user(target->view, out);
        return true;
    }

    if (native) {
        declarative_space_set_native(out, native->config, DECLARATIVE_SETTING_NATIVE_GLOBAL);
        for (size_t i = 0; i < policy_count; ++i) {
            const struct declarative_space_policy *policy = &policies[i];
            bool matches = policy->by_index ? policy->index == target->index
                                            : policy->uuid && strcmp(policy->uuid, target->uuid) == 0;
            if (matches) declarative_space_set_native(out, policy->settings, DECLARATIVE_SETTING_NATIVE_SPACE);
        }
    }

    if (mode == DECLARATIVE_SPACE_PRESERVE_RUNTIME && runtime) {
        for (int id = 0; id < DECLARATIVE_CONFIG_COUNT; ++id) {
            int field = declarative_space_field_for_id(id);
            if (field < 0) continue;
            if (runtime->global[id].present) out->fields[field] = runtime->global[id];
            if (target->view->runtime_ownership[field].present) {
                out->fields[field] = target->view->runtime_ownership[field];
            }
        }
    }
    return true;
}

bool declarative_space_resolve_policy(const struct declarative_config_model *native,
                                      const struct declarative_runtime_state *runtime,
                                      const struct declarative_space_defaults *defaults,
                                      const struct declarative_space_target *target,
                                      enum declarative_space_ownership_mode mode,
                                      struct declarative_space_effective *out)
{
    return declarative_space_resolve_effective_policy(native, native ? native->spaces : NULL,
                                                       native ? native->space_count : 0, runtime,
                                                       defaults, target, mode, out);
}

static bool declarative_space_public_identity(const char *uuid, uint64_t sid, int index, bool is_user_space,
                                              struct declarative_space_identity *identity)
{
    if (!uuid || !uuid[0] || !sid || index <= 0 || !identity || strlen(uuid) >= sizeof(identity->uuid)) return false;
    *identity = (struct declarative_space_identity) {
        .has_uuid = true, .sid = sid, .kind = is_user_space ? 0 : 2,
        .mission_control_ordinal = index - 1
    };
    memcpy(identity->uuid, uuid, strlen(uuid) + 1);
    identity->fingerprint = declarative_space_identity_fingerprint(identity);
    return declarative_space_identity_valid(identity);
}

static bool declarative_space_uuid_matches(const struct view *view, const char *uuid)
{
    if (!view || !view->uuid || !uuid) return false;
    CFStringRef candidate = CFStringCreateWithCString(NULL, uuid, kCFStringEncodingUTF8);
    if (!candidate) return false;
    bool matches = CFEqual(view->uuid, candidate);
    CFRelease(candidate);
    return matches;
}

static bool declarative_space_table_shape_valid(const struct table *table,
                                                const struct declarative_space_table_contract *contract,
                                                size_t expected_key_size)
{
    if (!table || !contract || !contract->hash || !contract->cmp ||
        contract->key_size != expected_key_size || table->hash != contract->hash ||
        table->cmp != contract->cmp || table->capacity <= 0 || !table->buckets ||
        table->count < 0 || !(table->max_load > 0.0f && table->max_load <= 1.0f) ||
        (double) table->count > (double) table->capacity * table->max_load) return false;
    size_t physical_count = 0;
    for (int slot = 0; slot < table->capacity; ++slot) {
        size_t chain = 0;
        for (struct bucket *bucket = table->buckets[slot]; bucket; bucket = bucket->next) {
            if (++chain > (size_t) table->count || !bucket->key || !bucket->value ||
                contract->hash(bucket->key) % (unsigned long) table->capacity != (unsigned long) slot) return false;
            if (++physical_count > (size_t) table->count) return false;
        }
    }
    return physical_count == (size_t) table->count;
}

struct declarative_space_identity_index
{
    uint64_t sid;
    int index;
    const char *uuid;
    const void *view;
    bool is_user_space;
    struct declarative_space_identity identity;
};

static int declarative_space_compare_identity_sid(const void *left, const void *right);
static int declarative_space_compare_identity_index(const void *left, const void *right);
static int declarative_space_compare_identity_fingerprint(const void *left, const void *right);
static int declarative_space_compare_identity_view(const void *left, const void *right);
static bool declarative_space_identity_unique(struct declarative_space_identity_index *items, size_t count,
                                               int (*compare)(const void *, const void *));
static const struct declarative_space_identity_index *declarative_space_identity_find_sid(
    const struct declarative_space_identity_index *items, size_t count, uint64_t sid);

bool declarative_space_validate_targets(const struct declarative_space_target *targets,
                                        size_t target_count,
                                        const struct declarative_space_topology_entry *topology,
                                        size_t topology_count,
                                        const struct table *current_views,
                                        const struct declarative_space_table_contract *contract)
{
    if ((!targets && target_count) || (!topology && topology_count) || target_count != topology_count ||
        !declarative_space_table_shape_valid(current_views, contract, sizeof(uint64_t)) ||
        (size_t) current_views->count != target_count ||
        target_count > SIZE_MAX / sizeof(struct declarative_space_identity_index)) return false;
    struct declarative_space_identity_index *target_index = NULL, *topology_index = NULL;
    if (target_count) {
        target_index = declarative_space_allocate(target_count, sizeof(*target_index));
        topology_index = declarative_space_allocate(target_count, sizeof(*topology_index));
        if (!target_index || !topology_index) goto fail;
    }
    for (size_t i = 0; i < target_count; ++i) {
        const struct declarative_space_target *target = &targets[i];
        const struct declarative_space_topology_entry *entry = &topology[i];
        struct declarative_space_identity target_identity = target->identity;
        struct declarative_space_identity entry_identity = entry->identity;
        if (!declarative_space_identity_valid(&target_identity)) {
            (void) declarative_space_public_identity(target->uuid, target->sid, target->index,
                                                     target->is_user_space, &target_identity);
        }
        if (!declarative_space_identity_valid(&entry_identity)) {
            (void) declarative_space_public_identity(entry->uuid, entry->sid, entry->index,
                                                     entry->is_user_space, &entry_identity);
        }
        if (!target->view || !target->sid || target->index <= 0 || !target->uuid ||
            !declarative_space_uuid_matches(target->view, target->uuid) || target->view->sid != target->sid ||
            !entry->sid || entry->index <= 0 || !entry->uuid ||
            !declarative_space_identity_valid(&target_identity) ||
            !declarative_space_identity_valid(&entry_identity) ||
            target_identity.has_uuid != (target->uuid[0] != '\0') ||
            entry_identity.has_uuid != (entry->uuid[0] != '\0') ||
            strcmp(target_identity.uuid, target->uuid) != 0 ||
            strcmp(entry_identity.uuid, entry->uuid) != 0 ||
            !declarative_space_identity_equal(&target_identity, &entry_identity) ||
            (declarative_space_identity_valid(&target->view->declarative_identity) &&
             !target->view->declarative_identity.has_uuid &&
             !declarative_space_identity_equal(&target->view->declarative_identity, &target_identity))) goto fail;
        target_index[i] = (struct declarative_space_identity_index) {
            .sid = target->sid, .index = target->index, .uuid = target->uuid,
            .view = target->view, .is_user_space = target->is_user_space,
            .identity = target_identity
        };
        topology_index[i] = (struct declarative_space_identity_index) {
            .sid = entry->sid, .index = entry->index, .uuid = entry->uuid,
            .view = NULL, .is_user_space = entry->is_user_space,
            .identity = entry_identity
        };
    }
    if (!declarative_space_identity_unique(target_index, target_count, declarative_space_compare_identity_view) ||
        !declarative_space_identity_unique(target_index, target_count, declarative_space_compare_identity_index) ||
        !declarative_space_identity_unique(target_index, target_count, declarative_space_compare_identity_fingerprint) ||
        !declarative_space_identity_unique(target_index, target_count, declarative_space_compare_identity_sid) ||
        !declarative_space_identity_unique(topology_index, topology_count, declarative_space_compare_identity_index) ||
        !declarative_space_identity_unique(topology_index, topology_count, declarative_space_compare_identity_fingerprint) ||
        !declarative_space_identity_unique(topology_index, topology_count, declarative_space_compare_identity_sid)) goto fail;
    /* Both arrays are now SID-sorted and can be checked and joined in one pass. */
    for (size_t i = 0; i < target_count; ++i) {
        const struct declarative_space_identity_index *target = &target_index[i];
        const struct declarative_space_identity_index *entry = declarative_space_identity_find_sid(topology_index, topology_count, target->sid);
        if (!entry || entry->index != target->index || entry->is_user_space != target->is_user_space ||
            strcmp(entry->uuid, target->uuid) != 0 ||
            !declarative_space_identity_equal(&entry->identity, &target->identity) ||
            table_find((struct table *) current_views, (void *) &target->sid) != target->view) goto fail;
    }
    declarative_space_free(target_index);
    declarative_space_free(topology_index);
    return true;
fail:
    declarative_space_free(target_index);
    declarative_space_free(topology_index);
    return false;
}

static bool declarative_space_validate_imperative_targets(
    const struct declarative_space_target *targets, size_t target_count,
    const struct declarative_space_topology_entry *topology, size_t topology_count,
    const struct table *current_views, const struct declarative_space_table_contract *contract)
{
    if ((!targets && target_count) || (!topology && topology_count) || target_count != topology_count ||
        !declarative_space_table_shape_valid(current_views, contract, sizeof(uint64_t)) ||
        (size_t) current_views->count != target_count) return false;
    for (size_t i = 0; i < target_count; ++i) {
        const struct declarative_space_target *target = &targets[i];
        const struct declarative_space_topology_entry *entry = &topology[i];
        if (!target->view || !target->sid || target->view->sid != target->sid || target->index <= 0 ||
            !target->uuid || !target->uuid[0] || !declarative_space_identity_valid(&target->identity) ||
            table_find((struct table *) current_views, (void *) &target->sid) != target->view ||
            entry->sid != target->sid || entry->index != target->index ||
            entry->is_user_space != target->is_user_space || strcmp(entry->uuid, target->uuid) != 0 ||
            !declarative_space_identity_equal(&entry->identity, &target->identity)) return false;
        for (size_t j = 0; j < i; ++j) {
            if (targets[j].sid == target->sid || targets[j].index == target->index ||
                strcmp(targets[j].uuid, target->uuid) == 0 ||
                targets[j].identity.fingerprint == target->identity.fingerprint) return false;
        }
    }
    return true;
}

static bool declarative_space_grow(void **items, size_t *capacity, size_t count, size_t item_size);

static int declarative_space_compare_identity_sid(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint64_t a = ((const struct declarative_space_identity_index *) left)->sid;
    uint64_t b = ((const struct declarative_space_identity_index *) right)->sid;
    return (a > b) - (a < b);
}

static int declarative_space_compare_identity_index(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    int a = ((const struct declarative_space_identity_index *) left)->index;
    int b = ((const struct declarative_space_identity_index *) right)->index;
    return (a > b) - (a < b);
}

static int declarative_space_compare_identity_fingerprint(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint64_t a = ((const struct declarative_space_identity_index *) left)->identity.fingerprint;
    uint64_t b = ((const struct declarative_space_identity_index *) right)->identity.fingerprint;
    return (a > b) - (a < b);
}

static int declarative_space_compare_identity_view(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uintptr_t a = (uintptr_t) ((const struct declarative_space_identity_index *) left)->view;
    uintptr_t b = (uintptr_t) ((const struct declarative_space_identity_index *) right)->view;
    return (a > b) - (a < b);
}

static bool declarative_space_identity_unique(struct declarative_space_identity_index *items, size_t count,
                                               int (*compare)(const void *, const void *))
{
    if (count > 1) qsort(items, count, sizeof(*items), compare);
    for (size_t i = 1; i < count; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (compare(&items[i - 1], &items[i]) == 0) return false;
    }
    return true;
}

static const struct declarative_space_identity_index *declarative_space_identity_find_sid(
    const struct declarative_space_identity_index *items, size_t count, uint64_t sid)
{
    size_t low = 0, high = count;
    while (low < high) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        size_t middle = low + (high - low) / 2;
        if (items[middle].sid < sid) low = middle + 1;
        else high = middle;
    }
    DECLARATIVE_SPACE_INDEX_OPERATION();
    return low < count && items[low].sid == sid ? &items[low] : NULL;
}

static int declarative_space_compare_inventory_index(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint32_t a = ((const struct declarative_space_inventory_index_entry *) left)->wid;
    uint32_t b = ((const struct declarative_space_inventory_index_entry *) right)->wid;
    return (a > b) - (a < b);
}

void declarative_space_inventory_destroy(struct declarative_space_inventory *inventory)
{
    if (!inventory) return;
    declarative_space_free(inventory->entries);
    declarative_space_free(inventory->wid_index);
    memset(inventory, 0, sizeof(*inventory));
}

enum declarative_space_apply_result declarative_space_capture_inventory_result(
                                         const struct declarative_space_target *targets,
                                         size_t target_count,
                                         const struct declarative_space_capture_ops *ops,
                                         struct declarative_space_inventory *out)
{
    if (!out || (!targets && target_count) || !ops ||
        (!ops->query_sid && !ops->query_sid_result) || !ops->find_window || !ops->is_eligible) {
        return DECLARATIVE_SPACE_APPLY_TERMINAL;
    }
    memset(out, 0, sizeof(*out));
    declarative_space_allocation_failed = false;
    enum declarative_space_apply_result failure = DECLARATIVE_SPACE_APPLY_TERMINAL;
    size_t capacity = 0;
    for (size_t i = 0; i < target_count; ++i) {
        const uint32_t *wids = NULL;
        size_t count = 0;
        enum declarative_space_query_result query = ops->query_sid_result
            ? ops->query_sid_result(targets[i].sid, &wids, &count, ops->context)
            : ops->query_sid(targets[i].sid, &wids, &count, ops->context)
            ? DECLARATIVE_SPACE_QUERY_OK : DECLARATIVE_SPACE_QUERY_TERMINAL;
        if (query != DECLARATIVE_SPACE_QUERY_OK) {
            failure = query == DECLARATIVE_SPACE_QUERY_RESOURCE
                    ? DECLARATIVE_SPACE_APPLY_RESOURCE : DECLARATIVE_SPACE_APPLY_TERMINAL;
            goto fail;
        }
        if ((count && !wids) || count > SIZE_MAX - out->count) goto fail;
        for (size_t j = 0; j < count; ++j) {
            if (!wids[j] || !declarative_space_grow((void **) &out->entries, &capacity, out->count + 1,
                                                     sizeof(*out->entries))) goto fail;
            struct window *window = ops->find_window(wids[j], ops->context);
            /* SkyLight can publish a new window before its AX discovery event reaches
             * the worker. That disagreement is retryable; an aliased id is not. */
            if (!window) {
                if (ops->allow_unresolved_windows) continue;
                failure = DECLARATIVE_SPACE_APPLY_TRANSIENT;
                goto fail;
            }
            if (window->id != wids[j]) goto fail;
            out->entries[out->count++] = (struct declarative_space_inventory_entry) {
                .wid = wids[j], .sid = targets[i].sid, .window = window,
                .eligible = ops->is_eligible(window, ops->context)
            };
        }
    }
    if (out->count) {
        out->wid_index = declarative_space_allocate(out->count, sizeof(*out->wid_index));
        if (!out->wid_index) goto fail;
        for (size_t i = 0; i < out->count; ++i) {
            out->wid_index[i] = (struct declarative_space_inventory_index_entry) { out->entries[i].wid, i };
        }
        qsort(out->wid_index, out->count, sizeof(*out->wid_index), declarative_space_compare_inventory_index);
        for (size_t i = 1; i < out->count; ++i) {
            DECLARATIVE_SPACE_INDEX_OPERATION();
            if (out->wid_index[i - 1].wid == out->wid_index[i].wid) goto fail;
        }
    }
    return DECLARATIVE_SPACE_APPLY_OK;
fail:
    if (declarative_space_allocation_failed) failure = DECLARATIVE_SPACE_APPLY_RESOURCE;
    declarative_space_inventory_destroy(out);
    return failure;
}

bool declarative_space_capture_inventory(const struct declarative_space_target *targets,
                                         size_t target_count,
                                         const struct declarative_space_capture_ops *ops,
                                         struct declarative_space_inventory *out)
{
    return declarative_space_capture_inventory_result(targets, target_count, ops, out) ==
           DECLARATIVE_SPACE_APPLY_OK;
}

static uint64_t declarative_space_hash_u64(uint64_t hash, uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        hash ^= (uint8_t) (value >> (i * 8));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

struct declarative_space_root_context
{
    const struct view *view;
    struct window_node **nodes;
    size_t node_count;
    size_t node_capacity;
    uint32_t *wids;
    size_t wid_count;
    size_t wid_capacity;
    uint64_t hash;
};

static bool declarative_space_grow(void **items, size_t *capacity, size_t count, size_t item_size)
{
    if (count <= *capacity) return true;
    size_t next = *capacity ? *capacity * 2 : 8;
    if (next < count || next > SIZE_MAX / item_size) return false;
    void *copy = declarative_space_allocate(next, item_size);
    if (!copy) return false;
    if (*items) memcpy(copy, *items, (*capacity) * item_size);
    declarative_space_free(*items);
    *items = copy;
    *capacity = next;
    return true;
}

static bool declarative_space_visit_node(struct declarative_space_root_context *context,
                                         struct window_node *node,
                                         struct window_node *parent)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    if (!node || node->parent != parent || (node->left && node->left == node->right)) return false;
    if (!declarative_space_grow((void **) &context->nodes, &context->node_capacity, context->node_count + 1, sizeof(*context->nodes))) return false;
    context->nodes[context->node_count++] = node;
    context->hash = declarative_space_hash_u64(context->hash, (uintptr_t) node);
    context->hash = declarative_space_hash_u64(context->hash, (uint32_t) node->window_count);
    context->hash = declarative_space_hash_u64(context->hash, (uintptr_t) node->parent);
    context->hash = declarative_space_hash_u64(context->hash, (uintptr_t) node->left);
    context->hash = declarative_space_hash_u64(context->hash, (uintptr_t) node->right);
    context->hash = declarative_space_hash_u64(context->hash, (uintptr_t) node->zoom);
    context->hash = declarative_space_hash_u64(context->hash, (uint32_t) node->split);
    context->hash = declarative_space_hash_u64(context->hash, (uint32_t) node->child);
    context->hash = declarative_space_hash_u64(context->hash, (uint32_t) node->insert_dir);
    context->hash = declarative_space_hash_u64(context->hash, node->feedback_window.id);
    context->hash = declarative_space_hash_u64(context->hash, (uintptr_t) node->feedback_window.context);
    context->hash = declarative_space_hash_u64(context->hash, node->has_window_areas);
    uint32_t bits;
    memcpy(&bits, &node->ratio, sizeof(bits));
    context->hash = declarative_space_hash_u64(context->hash, bits);
    memcpy(&bits, &node->area.x, sizeof(bits)); context->hash = declarative_space_hash_u64(context->hash, bits);
    memcpy(&bits, &node->area.y, sizeof(bits)); context->hash = declarative_space_hash_u64(context->hash, bits);
    memcpy(&bits, &node->area.w, sizeof(bits)); context->hash = declarative_space_hash_u64(context->hash, bits);
    memcpy(&bits, &node->area.h, sizeof(bits)); context->hash = declarative_space_hash_u64(context->hash, bits);

    if (node->split < SPLIT_NONE || node->split > SPLIT_AUTO ||
        node->child < CHILD_NONE || node->child > CHILD_FIRST ||
        (node->insert_dir != 0 && node->insert_dir != STACK && node->insert_dir != DIR_NORTH &&
         node->insert_dir != DIR_EAST && node->insert_dir != DIR_SOUTH && node->insert_dir != DIR_WEST) ||
        (!node->feedback_window.id != !node->feedback_window.context) ||
        (node->feedback_window.id && !node->insert_dir)) return false;

    bool leaf = !node->left && !node->right;
    if (!leaf) {
        if (!node->left || !node->right || node->window_count != 0 ||
            (node->split != SPLIT_X && node->split != SPLIT_Y) ||
            node->insert_dir != 0 || node->feedback_window.id || node->zoom) return false;
        return declarative_space_visit_node(context, node->left, node) &&
               declarative_space_visit_node(context, node->right, node);
    }
    if (node != context->view->root && node->window_count == 0) return false;
    if (node->window_count < 0 || node->window_count > NODE_MAX_WINDOW_COUNT) return false;
    bool order_seen[NODE_MAX_WINDOW_COUNT] = { 0 };
    for (int i = 0; i < node->window_count; ++i) {
        uint32_t wid = node->window_list[i];
        if (!wid) return false;
        for (int j = 0; j < i; ++j) if (node->window_list[j] == wid) return false;
        if (!declarative_space_grow((void **) &context->wids, &context->wid_capacity, context->wid_count + 1, sizeof(*context->wids))) return false;
        context->wids[context->wid_count++] = wid;
        context->hash = declarative_space_hash_u64(context->hash, wid);
        if (node->has_window_areas) {
            memcpy(&bits, &node->window_area[i].x, sizeof(bits)); context->hash = declarative_space_hash_u64(context->hash, bits);
            memcpy(&bits, &node->window_area[i].y, sizeof(bits)); context->hash = declarative_space_hash_u64(context->hash, bits);
            memcpy(&bits, &node->window_area[i].w, sizeof(bits)); context->hash = declarative_space_hash_u64(context->hash, bits);
            memcpy(&bits, &node->window_area[i].h, sizeof(bits)); context->hash = declarative_space_hash_u64(context->hash, bits);
        }
        for (int j = 0; j < node->window_count; ++j) {
            if (node->window_order[j] == wid) {
                if (order_seen[j]) return false;
                order_seen[j] = true;
            }
        }
    }
    for (int i = 0; i < node->window_count; ++i) {
        if (!order_seen[i]) return false;
        for (int j = 0; j < i; ++j) if (node->window_order[j] == node->window_order[i]) return false;
        context->hash = declarative_space_hash_u64(context->hash, node->window_order[i]);
    }
    return true;
}

static int declarative_space_compare_u32(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint32_t a = *(const uint32_t *) left, b = *(const uint32_t *) right;
    return (a > b) - (a < b);
}

bool declarative_space_validate_root(const struct view *view,
                                     struct declarative_space_root_before_image *out)
{
    if (!view || !view->root || !out) return false;
    struct declarative_space_root_context context = { .view = view, .hash = UINT64_C(1469598103934665603) };
    bool valid = declarative_space_visit_node(&context, view->root, NULL);
    if (valid && context.wid_count > 1) {
        qsort(context.wids, context.wid_count, sizeof(*context.wids), declarative_space_compare_u32);
        for (size_t i = 1; i < context.wid_count; ++i) {
            if (context.wids[i - 1] == context.wids[i]) { valid = false; break; }
        }
    }
    if (valid) {
        if (view_layout_is_single_root(view->layout) && (context.node_count != 1 || (view->root->left || view->root->right))) valid = false;
        if (view->layout == VIEW_FLOAT && (context.node_count != 1 || context.wid_count != 0)) valid = false;
        if (view->layout < VIEW_BSP || view->layout > VIEW_ACCORDION_VERTICAL) valid = false;
        bool insertion_found = view->insertion_point == 0;
        for (size_t i = 0; valid && i < context.node_count; ++i) {
            struct window_node *node = context.nodes[i];
            bool leaf = !node->left && !node->right;
            if (node->zoom && (view->layout != VIEW_BSP || !leaf || node == view->root ||
                (node->zoom != node->parent && node->zoom != view->root))) valid = false;
            for (int j = 0; j < node->window_count; ++j) {
                if (node->window_list[j] == view->insertion_point) insertion_found = true;
            }
        }
        if (!insertion_found) valid = false;
    }
    if (valid) *out = (struct declarative_space_root_before_image) {
        .view = (struct view *) view, .root = view->root, .fingerprint = context.hash,
        .node_count = context.node_count, .window_count = context.wid_count
    };
    declarative_space_free(context.nodes);
    declarative_space_free(context.wids);
    return valid;
}

void declarative_space_capture_view_before(const struct view *view,
                                           struct declarative_space_view_before_image *out)
{
    if (!view || !out) return;
    *out = (struct declarative_space_view_before_image) {
        .identity = view->declarative_identity,
        .layout = view->layout,
        .top_padding = view->top_padding,
        .bottom_padding = view->bottom_padding,
        .left_padding = view->left_padding,
        .right_padding = view->right_padding,
        .window_gap = view->window_gap,
        .accordion_padding = view->accordion_padding,
        .single_window_max_width = view->single_window_max_width,
        .single_window_min_width = view->single_window_min_width,
        .single_window_width_ratio = view->single_window_width_ratio,
        .split_ratio = view->split_ratio,
        .split_type = view->split_type,
        .auto_balance = view->auto_balance,
        .insertion_point = view->insertion_point,
        .scaffold_fingerprint = view->scaffold_fingerprint,
        .flags = view->flags
    };
    memcpy(out->scaffold_primary, view->scaffold_primary, sizeof(out->scaffold_primary));
}

bool declarative_space_view_before_matches(const struct view *view,
                                            const struct declarative_space_view_before_image *image)
{
    if (!view || !image) return false;
    bool identity_matches = (!declarative_space_identity_valid(&view->declarative_identity) &&
                             !declarative_space_identity_valid(&image->identity)) ||
                            declarative_space_identity_equal(&view->declarative_identity, &image->identity);
    return identity_matches && view->layout == image->layout &&
           view->top_padding == image->top_padding &&
           view->bottom_padding == image->bottom_padding &&
           view->left_padding == image->left_padding &&
           view->right_padding == image->right_padding &&
           view->window_gap == image->window_gap &&
           view->accordion_padding == image->accordion_padding &&
           view->single_window_max_width == image->single_window_max_width &&
           view->single_window_min_width == image->single_window_min_width &&
           view->single_window_width_ratio == image->single_window_width_ratio &&
           view->split_ratio == image->split_ratio &&
           view->split_type == image->split_type &&
           view->auto_balance == image->auto_balance &&
           view->insertion_point == image->insertion_point &&
           view->scaffold_fingerprint == image->scaffold_fingerprint &&
           memcmp(view->scaffold_primary, image->scaffold_primary, sizeof(view->scaffold_primary)) == 0 &&
           view->flags == image->flags;
}

bool declarative_space_global_ownership_before_matches(const struct declarative_runtime_state *runtime,
                                                        const struct declarative_runtime_cell *image)
{
    if (!image) return false;
    struct declarative_runtime_cell empty[DECLARATIVE_CONFIG_COUNT] = { 0 };
    const struct declarative_runtime_cell *current = runtime ? runtime->global : empty;
    return memcmp(current, image, sizeof(empty)) == 0;
}

bool declarative_space_view_ownership_before_matches(const struct view *view,
                                                      const struct declarative_space_ownership_image *image)
{
    return view && image &&
           memcmp(view->runtime_ownership, image->view, sizeof(image->view)) == 0 &&
           view->flags == image->legacy_flags;
}

struct declarative_space_forest_index_entry
{
    uint32_t wid;
    struct view *view;
    struct window_node *node;
};

struct declarative_space_table_index_entry
{
    uint32_t wid;
    struct view *view;
};

static void declarative_space_fill_forest_index(struct window_node *node, struct view *view,
                                                 struct declarative_space_forest_index_entry *items,
                                                 size_t *count)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    if (node->left) {
        declarative_space_fill_forest_index(node->left, view, items, count);
        declarative_space_fill_forest_index(node->right, view, items, count);
        return;
    }
    for (int i = 0; i < node->window_count; ++i) {
        items[(*count)++] = (struct declarative_space_forest_index_entry) { node->window_list[i], view, node };
    }
}

static int declarative_space_compare_node_pointer(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uintptr_t a = (uintptr_t) *(struct window_node * const *) left;
    uintptr_t b = (uintptr_t) *(struct window_node * const *) right;
    return (a > b) - (a < b);
}

static void declarative_space_fill_node_index(struct window_node *node,
                                               struct window_node **items,
                                               size_t *count)
{
    if (!node) return;
    DECLARATIVE_SPACE_INDEX_OPERATION();
    items[(*count)++] = node;
    declarative_space_fill_node_index(node->left, items, count);
    declarative_space_fill_node_index(node->right, items, count);
}

static bool declarative_space_sorted_nodes_contain(struct window_node * const *items,
                                                    size_t count,
                                                    const struct window_node *node)
{
    uintptr_t key = (uintptr_t) node;
    size_t low = 0, high = count;
    while (low < high) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        size_t middle = low + (high - low) / 2;
        if ((uintptr_t) items[middle] < key) low = middle + 1;
        else high = middle;
    }
    DECLARATIVE_SPACE_INDEX_OPERATION();
    return low < count && items[low] == node;
}

static bool declarative_space_tree_disjoint_from_index(struct window_node *node,
                                                        struct window_node * const *items,
                                                        size_t count,
                                                        size_t *visited)
{
    if (!node) return true;
    ++*visited;
    return !declarative_space_sorted_nodes_contain(items, count, node) &&
           declarative_space_tree_disjoint_from_index(node->left, items, count, visited) &&
           declarative_space_tree_disjoint_from_index(node->right, items, count, visited);
}

static int declarative_space_compare_table_index(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint32_t a = ((const struct declarative_space_table_index_entry *) left)->wid;
    uint32_t b = ((const struct declarative_space_table_index_entry *) right)->wid;
    return (a > b) - (a < b);
}

static int declarative_space_compare_forest_index(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint32_t a = ((const struct declarative_space_forest_index_entry *) left)->wid;
    uint32_t b = ((const struct declarative_space_forest_index_entry *) right)->wid;
    return (a > b) - (a < b);
}

static const struct declarative_space_forest_index_entry *declarative_space_forest_find(
    const struct declarative_space_forest_index_entry *items, size_t count, uint32_t wid)
{
    size_t low = 0, high = count;
    while (low < high) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        size_t middle = low + (high - low) / 2;
        if (items[middle].wid < wid) low = middle + 1;
        else high = middle;
    }
    DECLARATIVE_SPACE_INDEX_OPERATION();
    return low < count && items[low].wid == wid ? &items[low] : NULL;
}

bool declarative_space_validate_forest_and_table(const struct declarative_space_target *targets,
                                                 size_t target_count,
                                                 const struct table *managed_window,
                                                 const struct declarative_space_table_contract *contract,
                                                 struct declarative_space_root_before_image *roots,
                                                 struct declarative_space_table_before_image *table_image)
{
    if ((!targets && target_count) || (!roots && target_count) || !table_image ||
        !declarative_space_table_shape_valid(managed_window, contract, sizeof(uint32_t))) return false;
    size_t total_windows = 0, total_nodes = 0;
    for (size_t i = 0; i < target_count; ++i) {
        if (!declarative_space_validate_root(targets[i].view, &roots[i]) ||
            roots[i].window_count > SIZE_MAX - total_windows ||
            roots[i].node_count > SIZE_MAX - total_nodes) return false;
        total_windows += roots[i].window_count;
        total_nodes += roots[i].node_count;
    }
    struct window_node **nodes = NULL;
    if (total_nodes) {
        nodes = declarative_space_allocate(total_nodes, sizeof(*nodes));
        if (!nodes) return false;
    }
    size_t node_count = 0;
    for (size_t i = 0; i < target_count; ++i) {
        declarative_space_fill_node_index(roots[i].root, nodes, &node_count);
    }
    if (node_count > 1) qsort(nodes, node_count, sizeof(*nodes), declarative_space_compare_node_pointer);
    for (size_t i = 1; i < node_count; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (nodes[i - 1] == nodes[i]) { declarative_space_free(nodes); return false; }
    }
    declarative_space_free(nodes);
    if (total_windows != (size_t) managed_window->count ||
        total_windows > SIZE_MAX / sizeof(struct declarative_space_forest_index_entry)) return false;
    struct declarative_space_forest_index_entry *forest = NULL;
    struct declarative_space_table_index_entry *table_entries = NULL;
    if (total_windows) {
        forest = declarative_space_allocate(total_windows, sizeof(*forest));
        table_entries = declarative_space_allocate(total_windows, sizeof(*table_entries));
        if (!forest || !table_entries) {
            declarative_space_free(forest); declarative_space_free(table_entries); return false;
        }
    }
    size_t forest_count = 0;
    for (size_t i = 0; i < target_count; ++i) {
        declarative_space_fill_forest_index(roots[i].root, targets[i].view, forest, &forest_count);
    }
    if (forest_count > 1) qsort(forest, forest_count, sizeof(*forest), declarative_space_compare_forest_index);
    for (size_t i = 1; i < forest_count; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (forest[i - 1].wid == forest[i].wid) {
            declarative_space_free(table_entries); declarative_space_free(forest); return false;
        }
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = declarative_space_hash_u64(hash, (uintptr_t) managed_window->buckets);
    hash = declarative_space_hash_u64(hash, (uint32_t) managed_window->capacity);
    hash = declarative_space_hash_u64(hash, (uint32_t) managed_window->count);
    uint32_t max_load_bits;
    memcpy(&max_load_bits, &managed_window->max_load, sizeof(max_load_bits));
    hash = declarative_space_hash_u64(hash, max_load_bits);
    hash = declarative_space_hash_u64(hash, (uintptr_t) managed_window->hash);
    hash = declarative_space_hash_u64(hash, (uintptr_t) managed_window->cmp);
    size_t seen = 0;
    for (int i = 0; i < managed_window->capacity; ++i) {
        size_t chain = 0;
        for (struct bucket *bucket = managed_window->buckets[i]; bucket; bucket = bucket->next) {
            if (++chain > (size_t) managed_window->count + 1 || !bucket->key || !bucket->value ||
                seen >= total_windows) {
                declarative_space_free(table_entries); declarative_space_free(forest); return false;
            }
            uint32_t wid = *(uint32_t *) bucket->key;
            if (!wid) {
                declarative_space_free(table_entries); declarative_space_free(forest); return false;
            }
            struct view *owner = bucket->value;
            table_entries[seen] = (struct declarative_space_table_index_entry) { wid, owner };
            hash = declarative_space_hash_u64(hash, (uint32_t) i);
            hash = declarative_space_hash_u64(hash, (uintptr_t) bucket);
            hash = declarative_space_hash_u64(hash, (uintptr_t) bucket->key);
            hash = declarative_space_hash_u64(hash, wid);
            hash = declarative_space_hash_u64(hash, (uintptr_t) owner);
            hash = declarative_space_hash_u64(hash, (uintptr_t) bucket->next);
            ++seen;
        }
    }
    if (seen != (size_t) managed_window->count) {
        declarative_space_free(table_entries); declarative_space_free(forest); return false;
    }
    if (seen > 1) qsort(table_entries, seen, sizeof(*table_entries), declarative_space_compare_table_index);
    for (size_t i = 0; i < seen; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (i && table_entries[i - 1].wid == table_entries[i].wid) {
            declarative_space_free(table_entries); declarative_space_free(forest); return false;
        }
        if (table_entries[i].wid != forest[i].wid || table_entries[i].view != forest[i].view ||
            table_find((struct table *) managed_window, &table_entries[i].wid) != table_entries[i].view) {
            declarative_space_free(table_entries); declarative_space_free(forest); return false;
        }
    }
    *table_image = (struct declarative_space_table_before_image) {
        .buckets = managed_window->buckets, .capacity = managed_window->capacity,
        .count = managed_window->count, .max_load = managed_window->max_load,
        .hash = managed_window->hash, .cmp = managed_window->cmp, .fingerprint = hash
    };
    declarative_space_free(table_entries);
    declarative_space_free(forest);
    return true;
}

static const struct declarative_space_inventory_entry *declarative_space_inventory_find(const struct declarative_space_inventory *inventory,
                                                                                         uint32_t wid)
{
    size_t low = 0, high = inventory->count;
    while (low < high) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        size_t middle = low + (high - low) / 2;
        if (inventory->wid_index[middle].wid < wid) low = middle + 1;
        else high = middle;
    }
    DECLARATIVE_SPACE_INDEX_OPERATION();
    return low < inventory->count && inventory->wid_index[low].wid == wid
         ? &inventory->entries[inventory->wid_index[low].position] : NULL;
}

static enum view_type declarative_space_final_layout(const struct declarative_space_prepared_target *target)
{
    int layout = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_LAYOUT].value.integer;
    /* Runtime cells are validated at ingress; retaining the live layout keeps synthetic before-image fixtures safe. */
    return layout >= VIEW_BSP && layout <= VIEW_ACCORDION_VERTICAL ? (enum view_type) layout : target->target.view->layout;
}

static bool declarative_space_target_accepts(const struct declarative_space_prepared_target *target, uint64_t sid)
{
    return target->target.sid == sid && target->target.is_user_space &&
           declarative_space_final_layout(target) != VIEW_FLOAT;
}

DECLARATIVE_SPACE_INTERNAL void declarative_space_destroy_detached_root(struct window_node *root)
{
    if (!root) return;
    declarative_space_destroy_detached_root(root->left);
    declarative_space_destroy_detached_root(root->right);
    /* Prepared nodes deliberately own neither feedback resources nor live table entries. */
    declarative_space_free(root);
}

static void declarative_space_destroy_replacement_table(struct table *table)
{
    if (!table) return;
    if (table->buckets) {
        for (int i = 0; i < table->capacity; ++i) {
            struct bucket *bucket = table->buckets[i];
            while (bucket) {
                struct bucket *next = bucket->next;
                declarative_space_free(bucket->key);
                declarative_space_free(bucket);
                bucket = next;
            }
        }
        declarative_space_free(table->buckets);
    }
    memset(table, 0, sizeof(*table));
}

static struct window_node *declarative_space_clone_node(const struct window_node *source,
                                                         struct window_node *parent,
                                                         const struct window_node *source_root,
                                                         struct window_node *root)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    struct window_node *node = declarative_space_allocate(1, sizeof(*node));
    if (!node) return NULL;
    *node = *source;
    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    node->feedback_window = (struct feedback_window) { 0 };
    if (source->zoom == source_root) node->zoom = root;
    else if (source->zoom == source->parent) node->zoom = parent;
    else node->zoom = NULL;
    if (source->left) {
        node->left = declarative_space_clone_node(source->left, node, source_root, root);
        if (!node->left) { declarative_space_destroy_detached_root(node); return NULL; }
    }
    if (source->right) {
        node->right = declarative_space_clone_node(source->right, node, source_root, root);
        if (!node->right) { declarative_space_destroy_detached_root(node); return NULL; }
    }
    return node;
}

static struct window_node *declarative_space_clone_root(const struct window_node *source)
{
    struct window_node *root = declarative_space_allocate(1, sizeof(*root));
    if (!root) return NULL;
    *root = *source;
    root->parent = NULL;
    root->left = NULL;
    root->right = NULL;
    root->feedback_window = (struct feedback_window) { 0 };
    root->zoom = NULL;
    if (source->left) {
        root->left = declarative_space_clone_node(source->left, root, source, root);
        if (!root->left) { declarative_space_destroy_detached_root(root); return NULL; }
    }
    if (source->right) {
        root->right = declarative_space_clone_node(source->right, root, source, root);
        if (!root->right) { declarative_space_destroy_detached_root(root); return NULL; }
    }
    return root;
}

static struct declarative_space_tree_index_entry *declarative_space_tree_find(
    struct declarative_space_prepared_target *target, uint32_t wid);

static bool declarative_space_node_remove_wid(struct window_node *node, uint32_t wid)
{
    int list_index = -1, order_index = -1;
    if (!node || node->left) return false;
    for (int i = 0; i < node->window_count; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (node->window_list[i] == wid) list_index = i;
        if (node->window_order[i] == wid) order_index = i;
    }
    if (list_index < 0 || order_index < 0) return false;
    memmove(node->window_list + list_index, node->window_list + list_index + 1,
            (size_t) (node->window_count - list_index - 1) * sizeof(uint32_t));
    memmove(node->window_order + order_index, node->window_order + order_index + 1,
            (size_t) (node->window_count - order_index - 1) * sizeof(uint32_t));
    --node->window_count;
    node->window_list[node->window_count] = 0;
    node->window_order[node->window_count] = 0;
    if (!node->window_count) node->zoom = NULL;
    return true;
}

static void declarative_space_split_areas(struct window_node *node, int gap);

static void declarative_space_update_areas(struct window_node *node, int gap)
{
    if (!node || !node->left) return;
    declarative_space_split_areas(node, gap);
    declarative_space_update_areas(node->left, gap);
    declarative_space_update_areas(node->right, gap);
}

static void declarative_space_clear_zoom(struct window_node *node)
{
    if (!node) return;
    node->zoom = NULL;
    declarative_space_clear_zoom(node->left);
    declarative_space_clear_zoom(node->right);
}

static bool declarative_space_prune_empty_leaf(struct declarative_space_prepared_target *target,
                                                struct window_node *node,
                                                bool window_zoom_persist,
                                                int gap)
{
    if (node->left || node->window_count) return true;
    if (!node->parent) {
        struct area area = node->area;
        memset(node, 0, sizeof(*node));
        node->area = area;
        return true;
    }
    struct window_node *parent = node->parent;
    struct window_node *sibling = parent->left == node ? parent->right : parent->left;

    memcpy(parent->window_list, sibling->window_list,
           sizeof(uint32_t) * (size_t) sibling->window_count);
    memcpy(parent->window_order, sibling->window_order,
           sizeof(uint32_t) * (size_t) sibling->window_count);
    parent->window_count = sibling->window_count;
    parent->left = NULL;
    parent->right = NULL;
    parent->zoom = !window_zoom_persist || !sibling->zoom ? NULL :
                   sibling->zoom == parent ? parent->parent : target->final_root;

    /* Live collapse retains the parent's area, ratio and split unless the
     * surviving node has active insertion feedback. Detached nodes never
     * adopt the feedback resource itself. */
    if (sibling->insert_dir) {
        parent->insert_dir = sibling->insert_dir;
        parent->split = sibling->split;
        parent->child = sibling->child;
    }
    parent->feedback_window = (struct feedback_window) { 0 };

    if (sibling->left) {
        parent->left = sibling->left;
        parent->right = sibling->right;
        parent->left->parent = parent;
        parent->right->parent = parent;
        parent->left->zoom = !window_zoom_persist || !parent->left->zoom ? NULL :
                             parent->left->zoom == sibling ? parent : target->final_root;
        parent->right->zoom = !window_zoom_persist || !parent->right->zoom ? NULL :
                              parent->right->zoom == sibling ? parent : target->final_root;
        if (!window_zoom_persist) declarative_space_clear_zoom(parent);
        declarative_space_update_areas(parent, gap);
    } else {
        for (int i = 0; i < parent->window_count; ++i) {
            struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, parent->window_list[i]);
            if (!entry || entry->node != sibling) return false;
            entry->node = parent;
        }
    }
    declarative_space_free(node);
    declarative_space_free(sibling);
    return true;
}

static bool declarative_space_stack_add(struct window_node *node, uint32_t wid)
{
    if (node->window_count >= NODE_MAX_WINDOW_COUNT) return false;
    int index = node->window_count;
    if (node->window_count) {
        for (int i = 0; i < node->window_count; ++i) {
            if (node->window_list[i] == node->window_order[0]) { index = i + 1; break; }
        }
        memmove(node->window_list + index + 1, node->window_list + index,
                (size_t) (node->window_count - index) * sizeof(uint32_t));
        memmove(node->window_order + 1, node->window_order,
                (size_t) node->window_count * sizeof(uint32_t));
    }
    node->window_list[index] = wid;
    node->window_order[0] = wid;
    ++node->window_count;
    return true;
}

struct declarative_space_leaf_heap_entry
{
    struct window_node *node;
    size_t depth;
};

struct declarative_space_edit_context
{
    struct declarative_space_leaf_heap_entry *heap;
    size_t heap_count;
    size_t heap_capacity;
    struct window_node *first;
    struct window_node *last;
};

static bool declarative_space_leaf_precedes(const struct declarative_space_leaf_heap_entry *a,
                                             const struct declarative_space_leaf_heap_entry *b)
{
    if (a->depth != b->depth) return a->depth < b->depth;
    struct window_node *left = a->node, *right = b->node;
    if (left == right) return false;
    while (left->parent != right->parent) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        left = left->parent;
        right = right->parent;
    }
    return left->parent && left->parent->left == left;
}

static bool declarative_space_heap_push(struct declarative_space_edit_context *context,
                                        struct window_node *node, size_t depth)
{
    if (!declarative_space_grow((void **) &context->heap, &context->heap_capacity,
                                context->heap_count + 1, sizeof(*context->heap))) return false;
    size_t at = context->heap_count++;
    context->heap[at] = (struct declarative_space_leaf_heap_entry) { node, depth };
    while (at) {
        size_t parent = (at - 1) / 2;
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (!declarative_space_leaf_precedes(&context->heap[at], &context->heap[parent])) break;
        struct declarative_space_leaf_heap_entry swap = context->heap[parent];
        context->heap[parent] = context->heap[at]; context->heap[at] = swap;
        at = parent;
    }
    return true;
}

static void declarative_space_heap_pop(struct declarative_space_edit_context *context)
{
    context->heap[0] = context->heap[--context->heap_count];
    size_t at = 0;
    while (at * 2 + 1 < context->heap_count) {
        size_t child = at * 2 + 1;
        if (child + 1 < context->heap_count &&
            declarative_space_leaf_precedes(&context->heap[child + 1], &context->heap[child])) ++child;
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (!declarative_space_leaf_precedes(&context->heap[child], &context->heap[at])) break;
        struct declarative_space_leaf_heap_entry swap = context->heap[at];
        context->heap[at] = context->heap[child]; context->heap[child] = swap;
        at = child;
    }
}

static bool declarative_space_index_leaves(struct window_node *node, size_t depth,
                                            struct declarative_space_edit_context *context)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    if (node->left) return declarative_space_index_leaves(node->left, depth + 1, context) &&
                           declarative_space_index_leaves(node->right, depth + 1, context);
    if (!context->first) context->first = node;
    context->last = node;
    return node->window_count ? declarative_space_heap_push(context, node, depth) : true;
}

static struct window_node *declarative_space_select_leaf(struct declarative_space_edit_context *context,
                                                          struct declarative_space_prepared_target *target,
                                                          uint32_t insertion_point,
                                                          const struct declarative_space_defaults *defaults)
{
    struct declarative_space_tree_index_entry *entry = insertion_point
        ? declarative_space_tree_find(target, insertion_point) : NULL;
    if ((!entry || !entry->node) && defaults->window_insertion_point == INSERT_FOCUSED && defaults->focused_window_id) {
        entry = declarative_space_tree_find(target, defaults->focused_window_id);
    }
    if (entry && entry->node && !entry->node->left) return entry->node;
    if (defaults->window_insertion_point == INSERT_FIRST && context->first) return context->first;
    if (defaults->window_insertion_point == INSERT_LAST && context->last) return context->last;
    while (context->heap_count && context->heap[0].node->left) declarative_space_heap_pop(context);
    return context->heap_count ? context->heap[0].node : NULL;
}

static void declarative_space_split_areas(struct window_node *node, int gap)
{
    area_split_pair(node->split, gap, node->ratio, &node->area,
                    &node->left->area, &node->right->area);
}

static bool declarative_space_bsp_add(struct window_node *root, struct window_node *leaf,
                                      uint32_t wid, enum window_node_split view_split, int gap,
                                      const struct declarative_space_defaults *defaults)
{
    if (!root->window_count && !root->left) {
        root->window_list[0] = root->window_order[0] = wid;
        root->window_count = 1;
        return true;
    }
    if (!leaf || leaf->left) return false;

    struct window_node *left = declarative_space_allocate(1, sizeof(*left));
    if (!left) return false;
    struct window_node *right = declarative_space_allocate(1, sizeof(*right));
    if (!right) { declarative_space_free(left); return false; }

    enum window_node_child child = leaf->child != CHILD_NONE ? leaf->child : defaults->window_placement;
    enum window_node_split split = leaf->split;
    if (split == SPLIT_NONE) split = view_split;
    if (split == SPLIT_NONE) split = defaults->split_type;
    if (split == SPLIT_AUTO || split == SPLIT_NONE) split = leaf->area.w >= leaf->area.h ? SPLIT_Y : SPLIT_X;
    float ratio = leaf->ratio >= 0.1f && leaf->ratio <= 0.9f ? leaf->ratio : defaults->split_ratio;
    if (!(ratio >= 0.1f && ratio <= 0.9f)) ratio = 0.5f;

    struct window_node *old = child == CHILD_SECOND ? left : right;
    struct window_node *added = child == CHILD_SECOND ? right : left;
    memcpy(old->window_list, leaf->window_list, sizeof(uint32_t) * (size_t) leaf->window_count);
    memcpy(old->window_order, leaf->window_order, sizeof(uint32_t) * (size_t) leaf->window_count);
    old->window_count = leaf->window_count;
    if (defaults->window_zoom_persist && leaf->zoom) {
        old->zoom = leaf->zoom == leaf->parent ? leaf : root;
    }
    added->window_list[0] = added->window_order[0] = wid;
    added->window_count = 1;
    left->parent = right->parent = leaf;

    memset(leaf->window_list, 0, sizeof(leaf->window_list));
    memset(leaf->window_order, 0, sizeof(leaf->window_order));
    leaf->window_count = 0;
    leaf->left = left;
    leaf->right = right;
    leaf->zoom = NULL;
    leaf->feedback_window = (struct feedback_window) { 0 };
    leaf->insert_dir = 0;
    leaf->split = split;
    leaf->ratio = ratio;
    declarative_space_split_areas(leaf, gap);
    return true;
}

struct declarative_space_balance_node
{
    int y_count;
    int x_count;
};

static struct declarative_space_balance_node declarative_space_balance(struct window_node *node,
                                                                         uint32_t axis_flag)
{
    if (!node->left) {
        return (struct declarative_space_balance_node) {
            node->parent ? node->parent->split == SPLIT_Y : 0,
            node->parent ? node->parent->split == SPLIT_X : 0
        };
    }

    struct declarative_space_balance_node left = declarative_space_balance(node->left, axis_flag);
    struct declarative_space_balance_node right = declarative_space_balance(node->right, axis_flag);
    struct declarative_space_balance_node total = {
        left.y_count + right.y_count, left.x_count + right.x_count
    };
    if ((axis_flag & SPLIT_Y) && node->split == SPLIT_Y) {
        node->ratio = (float) left.y_count / total.y_count;
        --total.y_count;
    }
    if ((axis_flag & SPLIT_X) && node->split == SPLIT_X) {
        node->ratio = (float) left.x_count / total.x_count;
        --total.x_count;
    }
    if (node->parent) {
        total.y_count += node->parent->split == SPLIT_Y;
        total.x_count += node->parent->split == SPLIT_X;
    }
    return total;
}

static bool declarative_space_bsp_add_indexed(struct window_node *root,
                                               struct declarative_space_prepared_target *target,
                                               struct declarative_space_edit_context *context,
                                               uint32_t wid, uint32_t insertion_point,
                                               enum window_node_split view_split, uint32_t auto_balance,
                                               int gap,
                                               const struct declarative_space_defaults *defaults)
{
    struct declarative_space_tree_index_entry *added_entry = declarative_space_tree_find(target, wid);
    if (!added_entry || added_entry->node) return false;
    if (!root->window_count && !root->left) {
        if (!declarative_space_bsp_add(root, NULL, wid, view_split, gap, defaults)) return false;
        added_entry->node = root;
        context->first = context->last = root;
        return declarative_space_heap_push(context, root, 0);
    }
    struct window_node *leaf = declarative_space_select_leaf(context, target, insertion_point, defaults);
    if (!leaf) return false;
    size_t depth = 0;
    for (struct window_node *at = leaf; at->parent; at = at->parent) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        ++depth;
    }
    bool was_first = context->first == leaf, was_last = context->last == leaf;
    enum window_node_child child = leaf->child != CHILD_NONE ? leaf->child : defaults->window_placement;
    if (!declarative_space_bsp_add(root, leaf, wid, view_split, gap, defaults)) return false;
    struct window_node *old = child == CHILD_SECOND ? leaf->left : leaf->right;
    struct window_node *added = child == CHILD_SECOND ? leaf->right : leaf->left;
    for (int i = 0; i < old->window_count; ++i) {
        struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, old->window_list[i]);
        if (!entry || entry->node != leaf) return false;
        entry->node = old;
    }
    added_entry->node = added;
    if (was_first) context->first = leaf->left;
    if (was_last) context->last = leaf->right;
    if (auto_balance != SPLIT_NONE) {
        (void) declarative_space_balance(root, auto_balance);
        declarative_space_update_areas(root, gap);
    }
    return declarative_space_heap_push(context, leaf->left, depth + 1) &&
           declarative_space_heap_push(context, leaf->right, depth + 1);
}

static int declarative_space_compare_target_index(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint64_t a = ((const struct declarative_space_target_index_entry *) left)->sid;
    uint64_t b = ((const struct declarative_space_target_index_entry *) right)->sid;
    return (a > b) - (a < b);
}

static const struct declarative_space_prepared_target *declarative_space_prepared_target_find(
    const struct declarative_space_preparation *preparation, uint64_t sid)
{
    size_t low = 0, high = preparation->target_count;
    while (low < high) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        size_t middle = low + (high - low) / 2;
        if (preparation->target_index[middle].sid < sid) low = middle + 1;
        else high = middle;
    }
    DECLARATIVE_SPACE_INDEX_OPERATION();
    return low < preparation->target_count && preparation->target_index[low].sid == sid
         ? &preparation->targets[preparation->target_index[low].position] : NULL;
}

static int declarative_space_compare_final_owner(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint32_t a = ((const struct declarative_space_final_owner *) left)->wid;
    uint32_t b = ((const struct declarative_space_final_owner *) right)->wid;
    return (a > b) - (a < b);
}

static const struct declarative_space_final_owner *declarative_space_final_owner_find(
    const struct declarative_space_preparation *preparation, uint32_t wid)
{
    size_t low = 0, high = preparation->final_owner_count;
    while (low < high) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        size_t middle = low + (high - low) / 2;
        uint32_t candidate = preparation->final_owners[middle].wid;
        if (candidate < wid) low = middle + 1;
        else high = middle;
    }
    DECLARATIVE_SPACE_INDEX_OPERATION();
    return low < preparation->final_owner_count && preparation->final_owners[low].wid == wid
         ? &preparation->final_owners[low] : NULL;
}

static int declarative_space_compare_tree_index(const void *left, const void *right)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    uint32_t a = ((const struct declarative_space_tree_index_entry *) left)->wid;
    uint32_t b = ((const struct declarative_space_tree_index_entry *) right)->wid;
    return (a > b) - (a < b);
}

static struct declarative_space_tree_index_entry *declarative_space_tree_find(
    struct declarative_space_prepared_target *target, uint32_t wid)
{
    size_t low = 0, high = target->tree_index_count;
    while (low < high) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        size_t middle = low + (high - low) / 2;
        if (target->tree_index[middle].wid < wid) low = middle + 1;
        else high = middle;
    }
    DECLARATIVE_SPACE_INDEX_OPERATION();
    return low < target->tree_index_count && target->tree_index[low].wid == wid
         ? &target->tree_index[low] : NULL;
}

static bool declarative_space_fill_tree_index(struct window_node *node, struct window_node *root,
                                               struct declarative_space_prepared_target *target,
                                               size_t *entry_count, size_t *flatten_count)
{
    DECLARATIVE_SPACE_INDEX_OPERATION();
    if (node->left) {
        return declarative_space_fill_tree_index(node->left, root, target, entry_count, flatten_count) &&
               declarative_space_fill_tree_index(node->right, root, target, entry_count, flatten_count);
    }
    for (int i = 0; i < node->window_count; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        uint32_t wid = node->window_list[i];
        size_t order_position = SIZE_MAX;
        for (int j = 0; j < node->window_count; ++j) {
            DECLARATIVE_SPACE_INDEX_OPERATION();
            if (node->window_order[j] == wid) { order_position = (size_t) j; break; }
        }
        if (order_position == SIZE_MAX) return false;
        target->flatten_membership[*flatten_count] = wid;
        target->flatten_order[*flatten_count] = node->window_order[i];
        target->tree_index[(*entry_count)++] = (struct declarative_space_tree_index_entry) {
            .wid = wid, .node = node, .membership_position = (size_t) i,
            .order_position = order_position, .flatten_sequence = *flatten_count,
            .zoom_parent = node->zoom && node->zoom == node->parent,
            .zoom_root = node->zoom && node->zoom == root
        };
        ++*flatten_count;
    }
    return true;
}

static bool declarative_space_build_tree_index(struct declarative_space_prepared_target *target,
                                                struct window_node *root)
{
    size_t old_count = target->root_before.window_count;
    size_t final_count = target->final_count;
    if (old_count > SIZE_MAX - final_count) return false;
    size_t capacity = old_count + final_count;
    if (capacity) {
        target->tree_index = declarative_space_allocate(capacity, sizeof(*target->tree_index));
        if (!target->tree_index) return false;
    }
    if (old_count) {
        target->flatten_membership = declarative_space_allocate(old_count, sizeof(uint32_t));
        target->flatten_order = declarative_space_allocate(old_count, sizeof(uint32_t));
        if (!target->flatten_membership || !target->flatten_order) return false;
    }
    size_t count = 0, flatten = 0;
    if (!declarative_space_fill_tree_index(root, root, target, &count, &flatten) || flatten != old_count) return false;
    target->flatten_count = flatten;
    for (size_t i = 0; i < target->final_count; ++i) {
        target->tree_index[count++] = (struct declarative_space_tree_index_entry) {
            .wid = target->final_wids[i], .membership_position = SIZE_MAX,
            .order_position = SIZE_MAX, .flatten_sequence = SIZE_MAX
        };
    }
    if (count > 1) qsort(target->tree_index, count, sizeof(*target->tree_index), declarative_space_compare_tree_index);
    size_t out = 0;
    for (size_t i = 0; i < count; ++i) {
        if (out && target->tree_index[out - 1].wid == target->tree_index[i].wid) {
            if (target->tree_index[out - 1].node && target->tree_index[i].node) return false;
            if (!target->tree_index[out - 1].node) target->tree_index[out - 1] = target->tree_index[i];
        } else {
            target->tree_index[out++] = target->tree_index[i];
        }
    }
    target->tree_index_count = out;
    return true;
}

static bool declarative_space_wid_owned_by(const struct declarative_space_preparation *preparation,
                                            uint32_t wid, const struct view *view)
{
    const struct declarative_space_final_owner *owner = declarative_space_final_owner_find(preparation, wid);
    return owner && owner->view == view;
}

static bool declarative_space_root_membership_matches(const struct window_node *node,
                                                       const struct declarative_space_preparation *preparation,
                                                       const struct view *view, size_t *count)
{
    if (node->left) return declarative_space_root_membership_matches(node->left, preparation, view, count) &&
                           declarative_space_root_membership_matches(node->right, preparation, view, count);
    for (int i = 0; i < node->window_count; ++i) {
        if (!declarative_space_wid_owned_by(preparation, node->window_list[i], view)) return false;
        ++*count;
    }
    return true;
}

static int declarative_space_target_gap(const struct declarative_space_prepared_target *target)
{
    return (target->view_before.flags & VIEW_ENABLE_GAP)
         ? target->effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer : 0;
}

static const struct declarative_space_policy *declarative_space_scaffold_policy(
    const struct declarative_space_policy *policies, size_t policy_count,
    const struct declarative_space_target *target)
{
    if (!target->is_user_space) return NULL;
    const struct declarative_space_policy *result = NULL;
    for (size_t i = 0; i < policy_count; ++i) {
        const struct declarative_space_policy *policy = &policies[i];
        bool matches = policy->by_index ? policy->index == target->index
                                        : policy->uuid && strcmp(policy->uuid, target->uuid) == 0;
        if (matches && (policy->scaffold_present || policy->scaffold_count)) result = policy;
    }
    return result;
}

static uint64_t declarative_space_scaffold_hash(uint64_t generation,
                                                 const struct declarative_space_policy *policy)
{
    uint64_t hash = declarative_space_hash_u64(UINT64_C(1469598103934665603), generation);
    for (size_t i = 0; i < policy->scaffold_count; ++i) {
        const unsigned char *at = (const unsigned char *) policy->scaffold[i].pattern;
        while (*at) { hash ^= *at++; hash *= UINT64_C(1099511628211); }
        hash ^= policy->scaffold[i].exclude ? 1u : 0u;
        hash *= UINT64_C(1099511628211);
    }
    hash ^= policy->scaffold_count;
    hash *= UINT64_C(1099511628211);
    return hash ? hash : 1;
}

static bool declarative_space_prepare_scaffolds(struct declarative_space_preparation *preparation,
                                                 const struct declarative_space_policy *policies,
                                                 size_t policy_count,
                                                 uint64_t policy_generation)
{
    for (size_t i = 0; i < preparation->target_count; ++i) {
        struct declarative_space_prepared_target *target = &preparation->targets[i];
        if (declarative_space_final_layout(target) != VIEW_BSP) continue;
        const struct declarative_space_policy *policy = declarative_space_scaffold_policy(policies, policy_count, &target->target);
        if (!policy || !policy->scaffold_count) continue;
        regex_t regex[DECLARATIVE_CONFIG_MAX_SCAFFOLD_SLOTS];
        size_t compiled = 0;
        bool valid = true;
        for (; compiled < policy->scaffold_count; ++compiled) {
            if (regcomp(&regex[compiled], policy->scaffold[compiled].pattern, REG_EXTENDED)) { valid = false; break; }
        }
        if (valid) {
            target->final_scaffold_count = policy->scaffold_count;
            target->final_scaffold_fingerprint = declarative_space_scaffold_hash(policy_generation, policy);
            for (size_t wid_index = 0; wid_index < target->final_count; ++wid_index) {
                uint32_t wid = target->final_wids[wid_index];
                const struct declarative_space_final_owner *owner =
                    declarative_space_final_owner_find(preparation, wid);
                if (!owner || !owner->window || !owner->window->application ||
                    !owner->window->application->name) { valid = false; break; }
                const char *app = owner->window->application->name;
                for (size_t slot = 0; slot < policy->scaffold_count; ++slot) {
                    bool matched = regexec(&regex[slot], app, 0, NULL, 0) == 0;
                    if (policy->scaffold[slot].exclude) matched = !matched;
                    if (!matched) continue;
                    uint32_t current = target->final_scaffold_primary[slot];
                    if (!current || wid < current) target->final_scaffold_primary[slot] = wid;
                    break;
                }
            }
        }
        for (size_t slot = 0; slot < compiled; ++slot) regfree(&regex[slot]);
        if (!valid) return false;
    }
    return true;
}

static bool declarative_space_make_converted_root(struct declarative_space_preparation *preparation,
                                                   struct declarative_space_prepared_target *target,
                                                   enum view_type layout,
                                                   const struct declarative_space_defaults *defaults)
{
    struct declarative_space_defaults effective_defaults = *defaults;
    effective_defaults.split_ratio = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real;
    defaults = &effective_defaults;
    if (!declarative_space_build_tree_index(target, target->target.view->root)) return false;
    struct window_node *root = declarative_space_allocate(1, sizeof(*root));
    if (!root) return false;
    target->final_root = root;
    target->root_owned = true;
    for (size_t i = 0; i < target->tree_index_count; ++i) target->tree_index[i].node = NULL;
    if (layout == VIEW_FLOAT) return true;
    if (layout == VIEW_BSP) root->area = target->target.view->root->area;
    if (view_layout_is_single_root(layout)) {
        for (size_t i = 0; i < target->flatten_count; ++i) {
            uint32_t wid = target->flatten_membership[i];
            struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, wid);
            if (!entry || !declarative_space_wid_owned_by(preparation, wid, target->target.view)) continue;
            if (root->window_count >= NODE_MAX_WINDOW_COUNT) return false;
            root->window_list[root->window_count++] = wid;
            entry->node = root;
        }
        int order_out = 0;
        for (size_t i = 0; i < target->flatten_count; ++i) {
            uint32_t wid = target->flatten_order[i];
            if (declarative_space_wid_owned_by(preparation, wid, target->target.view)) root->window_order[order_out++] = wid;
        }
        for (size_t i = 0; i < target->final_count; ++i) {
            uint32_t wid = target->final_wids[i];
            struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, wid);
            if (!entry || entry->node) continue;
            if (!declarative_space_stack_add(root, wid)) return false;
            entry->node = root;
        }
        return true;
    }
    struct declarative_space_edit_context context = { 0 };
    for (size_t i = 0; i < target->flatten_count; ++i) {
        uint32_t wid = target->flatten_order[i];
        struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, wid);
        if (!entry || entry->node || !declarative_space_wid_owned_by(preparation, wid, target->target.view)) continue;
        if (!declarative_space_bsp_add_indexed(root, target, &context, wid, 0,
                target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].value.integer,
                target->effective.fields[DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE].value.u32,
                declarative_space_target_gap(target), defaults)) goto bsp_fail;
    }
    for (size_t i = 0; i < target->final_count; ++i) {
        uint32_t wid = target->final_wids[i];
        struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, wid);
        if (!entry || entry->node) continue;
        if (!declarative_space_bsp_add_indexed(root, target, &context, wid, 0,
                target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].value.integer,
                target->effective.fields[DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE].value.u32,
                declarative_space_target_gap(target), defaults)) goto bsp_fail;
    }
    declarative_space_free(context.heap);
    return true;
bsp_fail:
    declarative_space_free(context.heap);
    return false;
}

static bool declarative_space_make_scaffold_root(struct declarative_space_prepared_target *target,
                                                  const struct declarative_space_defaults *defaults)
{
    struct declarative_space_defaults effective_defaults = *defaults;
    effective_defaults.split_ratio = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real;
    defaults = &effective_defaults;
    if (!declarative_space_build_tree_index(target, target->target.view->root)) return false;
    struct window_node *root = declarative_space_allocate(1, sizeof(*root));
    if (!root) return false;
    target->final_root = root;
    target->root_owned = true;
    root->area = target->target.view->root->area;
    for (size_t i = 0; i < target->tree_index_count; ++i) target->tree_index[i].node = NULL;

    uint32_t primary[DECLARATIVE_CONFIG_MAX_SCAFFOLD_SLOTS] = { 0 };
    size_t primary_count = 0;
    for (size_t slot = 0; slot < target->final_scaffold_count; ++slot) {
        if (target->final_scaffold_primary[slot]) primary[primary_count++] = target->final_scaffold_primary[slot];
    }
    struct declarative_space_edit_context context = { 0 };
    int gap = declarative_space_target_gap(target);
    for (size_t i = 0; i < primary_count; ++i) {
        uint32_t insertion_point = i ? primary[i - 1] : 0;
        if (i == 1) {
            struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, primary[0]);
            if (!entry || !entry->node) goto fail;
            entry->node->split = SPLIT_Y;
            entry->node->child = CHILD_SECOND;
            entry->node->ratio = defaults->split_ratio;
        } else if (i == 2) {
            struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, primary[1]);
            if (!entry || !entry->node) goto fail;
            entry->node->split = SPLIT_X;
            entry->node->child = CHILD_SECOND;
            entry->node->ratio = 0.5f;
        }
        if (!declarative_space_bsp_add_indexed(root, target, &context, primary[i], insertion_point,
                                               SPLIT_NONE, SPLIT_NONE, gap, defaults)) goto fail;
        if (i) {
            struct window_node *parent = declarative_space_tree_find(target, primary[i])->node->parent;
            if (parent) parent->child = CHILD_NONE;
        }
    }
    for (size_t i = 0; i < target->final_count; ++i) {
        uint32_t wid = target->final_wids[i];
        bool is_primary = false;
        for (size_t j = 0; j < primary_count; ++j) if (primary[j] == wid) { is_primary = true; break; }
        if (is_primary) continue;
        if (!declarative_space_bsp_add_indexed(root, target, &context, wid, target->final_insertion_point,
                target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].value.integer,
                target->effective.fields[DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE].value.u32,
                gap, defaults)) goto fail;
    }
    declarative_space_free(context.heap);
    return true;
fail:
    declarative_space_free(context.heap);
    return false;
}

static bool declarative_space_reconcile_same_layout(struct declarative_space_preparation *preparation,
                                                     struct declarative_space_prepared_target *target,
                                                     enum view_type layout,
                                                     const struct declarative_space_defaults *defaults)
{
    struct declarative_space_defaults effective_defaults = *defaults;
    effective_defaults.split_ratio = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real;
    defaults = &effective_defaults;
    struct window_node *root = declarative_space_clone_root(target->target.view->root);
    if (!root) return false;
    target->final_root = root;
    target->root_owned = true;
    if (!declarative_space_build_tree_index(target, root)) return false;
    for (size_t i = 0; i < target->flatten_count; ++i) {
        uint32_t wid = target->flatten_membership[i];
        if (declarative_space_wid_owned_by(preparation, wid, target->target.view)) continue;
        struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, wid);
        if (!entry || !entry->node || !declarative_space_node_remove_wid(entry->node, wid)) return false;
        struct window_node *node = entry->node;
        entry->node = NULL;
        bool collapsed = !node->left && !node->window_count && node->parent;
        int gap = declarative_space_target_gap(target);
        if (!declarative_space_prune_empty_leaf(target, node, defaults->window_zoom_persist, gap)) return false;
        uint32_t auto_balance = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE].value.u32;
        if (collapsed && auto_balance != SPLIT_NONE) {
            (void) declarative_space_balance(root, auto_balance);
            declarative_space_update_areas(root, gap);
        }
    }
    for (size_t i = 0; i < target->tree_index_count; ++i) {
        struct declarative_space_tree_index_entry *entry = &target->tree_index[i];
        if (!entry->node) continue;
        entry->node->zoom = !defaults->window_zoom_persist ? NULL :
                            entry->zoom_parent ? entry->node->parent : entry->zoom_root ? root : NULL;
    }
    struct declarative_space_edit_context context = { 0 };
    if (layout == VIEW_BSP && !declarative_space_index_leaves(root, 0, &context)) {
        declarative_space_free(context.heap);
        return false;
    }
    for (size_t i = 0; i < target->final_count; ++i) {
        uint32_t wid = target->final_wids[i];
        struct declarative_space_tree_index_entry *entry = declarative_space_tree_find(target, wid);
        if (!entry || entry->node) continue;
        bool added = view_layout_is_single_root(layout) ? declarative_space_stack_add(root, wid) :
                     declarative_space_bsp_add_indexed(root, target, &context, wid, target->final_insertion_point,
                         target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].value.integer,
                         target->effective.fields[DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE].value.u32,
                         declarative_space_target_gap(target), defaults);
        if (!added) { declarative_space_free(context.heap); return false; }
        if (view_layout_is_single_root(layout)) entry->node = root;
    }
    declarative_space_free(context.heap);
    return true;
}

static bool declarative_space_build_owner_map(struct declarative_space_preparation *preparation)
{
    if (preparation->inventory.count) {
        preparation->final_owners = declarative_space_allocate(preparation->inventory.count, sizeof(*preparation->final_owners));
        if (!preparation->final_owners) return false;
    }
    for (size_t i = 0; i < preparation->inventory.count; ++i) {
        const struct declarative_space_inventory_entry *entry = &preparation->inventory.entries[i];
        const struct declarative_space_prepared_target *found = declarative_space_prepared_target_find(preparation, entry->sid);
        if (!found) return false;
        if (!entry->eligible || !declarative_space_target_accepts(found, entry->sid)) continue;
        if (!entry->window || entry->window->id != entry->wid) return false;
        ++preparation->targets[found - preparation->targets].final_count;
    }
    for (size_t i = 0; i < preparation->target_count; ++i) {
        struct declarative_space_prepared_target *target = &preparation->targets[i];
        if (target->final_count) {
            target->final_wids = declarative_space_allocate(target->final_count, sizeof(*target->final_wids));
            if (!target->final_wids) return false;
        }
        target->final_count = 0;
    }
    for (size_t i = 0; i < preparation->inventory.count; ++i) {
        const struct declarative_space_inventory_entry *entry = &preparation->inventory.entries[i];
        struct declarative_space_prepared_target *target = (struct declarative_space_prepared_target *)
            declarative_space_prepared_target_find(preparation, entry->sid);
        if (!entry->eligible || !declarative_space_target_accepts(target, entry->sid)) continue;
        target->final_wids[target->final_count++] = entry->wid;
        preparation->final_owners[preparation->final_owner_count++] = (struct declarative_space_final_owner) {
            entry->wid, entry->window, target->target.view
        };
    }
    if (preparation->final_owner_count > 1) {
        qsort(preparation->final_owners, preparation->final_owner_count,
              sizeof(*preparation->final_owners), declarative_space_compare_final_owner);
    }
    for (size_t i = 1; i < preparation->final_owner_count; ++i) {
        if (preparation->final_owners[i - 1].wid == preparation->final_owners[i].wid) return false;
    }
    return true;
}

static bool declarative_space_build_final_roots(struct declarative_space_preparation *preparation,
                                                 const struct declarative_space_defaults *defaults)
{
    for (size_t i = 0; i < preparation->target_count; ++i) {
        struct declarative_space_prepared_target *target = &preparation->targets[i];
        enum view_type layout = declarative_space_final_layout(target);
        size_t old_final_count = 0;
        bool old_subset = declarative_space_root_membership_matches(target->target.view->root, preparation,
                                                                    target->target.view, &old_final_count);
        size_t final_count = target->final_count;
        target->layout_changed = layout != target->target.view->layout;
        target->membership_changed = !old_subset || old_final_count != final_count;
        target->scaffold_changed = target->target.view->scaffold_fingerprint != target->final_scaffold_fingerprint ||
            memcmp(target->target.view->scaffold_primary, target->final_scaffold_primary,
                   sizeof(target->final_scaffold_primary)) != 0 ||
            (target->final_scaffold_fingerprint &&
             target->target.view->split_ratio !=
             target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real);
        target->changed = target->layout_changed || target->membership_changed || target->scaffold_changed ||
                          (layout == VIEW_FLOAT && target->root_before.window_count != 0);
        target->final_insertion_point = target->target.view->insertion_point;
        if (!declarative_space_wid_owned_by(preparation, target->final_insertion_point, target->target.view)) {
            target->final_insertion_point = 0;
        }
        if (!target->changed) {
            target->final_root = target->target.view->root;
            continue;
        }
        if (layout == VIEW_BSP && target->final_scaffold_fingerprint && target->scaffold_changed) {
            if (!declarative_space_make_scaffold_root(target, defaults)) return false;
        } else if (!target->layout_changed && layout != VIEW_FLOAT) {
            if (!declarative_space_reconcile_same_layout(preparation, target, layout, defaults)) return false;
        } else if (!declarative_space_make_converted_root(preparation, target, layout, defaults)) return false;
        struct view validation = *target->target.view;
        validation.root = target->final_root;
        validation.layout = layout;
        validation.insertion_point = target->final_insertion_point;
        struct declarative_space_root_before_image image;
        if (!declarative_space_validate_root(&validation, &image)) return false;
    }
    return true;
}

static bool declarative_space_table_capacity(size_t count, int *capacity)
{
    if (count > (SIZE_MAX - 2) / 4) return false;
    size_t needed = count ? (count * 4 + 2) / 3 : 1;
    if (needed > INT_MAX) return false;
    size_t result = 1;
    while (result < needed) {
        if (result > (size_t) INT_MAX / 2) return false;
        result *= 2;
    }
    *capacity = (int) result;
    return true;
}

static bool declarative_space_build_replacement_table(struct declarative_space_preparation *preparation,
                                                       const struct declarative_space_table_contract *contract)
{
    struct table *table = &preparation->replacement_managed_window;
    if (!contract || !contract->hash || !contract->cmp || contract->key_size != sizeof(uint32_t) ||
        !declarative_space_table_capacity(preparation->final_owner_count, &table->capacity)) return false;
    table->max_load = 0.75f;
    table->hash = contract->hash;
    table->cmp = contract->cmp;
    table->buckets = declarative_space_allocate((size_t) table->capacity, sizeof(*table->buckets));
    if (!table->buckets) return false;
    for (size_t i = 0; i < preparation->final_owner_count; ++i) {
        const struct declarative_space_final_owner *owner = &preparation->final_owners[i];
        unsigned long slot = table->hash((void *) &owner->wid) % (unsigned long) table->capacity;
        for (struct bucket *existing = table->buckets[slot]; existing; existing = existing->next) {
            if (table->cmp(existing->key, (void *) &owner->wid)) return false;
        }
        struct bucket *bucket = declarative_space_allocate(1, sizeof(*bucket));
        if (!bucket) return false;
        bucket->key = declarative_space_allocate(1, sizeof(owner->wid));
        if (!bucket->key) { declarative_space_free(bucket); return false; }
        memcpy(bucket->key, &owner->wid, sizeof(owner->wid));
        bucket->value = owner->view;
        bucket->next = table->buckets[slot];
        table->buckets[slot] = bucket;
        ++table->count;
    }
    return true;
}

static bool declarative_space_sorted_node_indexes_disjoint(struct window_node * const *left,
                                                            size_t left_count,
                                                            struct window_node * const *right,
                                                            size_t right_count)
{
    size_t i = 0, j = 0;
    while (i < left_count && j < right_count) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        uintptr_t a = (uintptr_t) left[i], b = (uintptr_t) right[j];
        if (a == b) return false;
        if (a < b) ++i; else ++j;
    }
    return true;
}

static bool declarative_space_prepared_roots_disjoint(const struct declarative_space_preparation *preparation)
{
    if ((!preparation->old_node_index && preparation->old_node_index_count) ||
        (!preparation->final_node_index && preparation->final_node_index_count)) return false;
    for (size_t i = 1; i < preparation->old_node_index_count; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (preparation->old_node_index[i - 1] == preparation->old_node_index[i]) return false;
    }
    for (size_t i = 1; i < preparation->final_node_index_count; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (preparation->final_node_index[i - 1] == preparation->final_node_index[i]) return false;
    }
    return declarative_space_sorted_node_indexes_disjoint(preparation->old_node_index,
                                                           preparation->old_node_index_count,
                                                           preparation->final_node_index,
                                                           preparation->final_node_index_count);
}

static bool declarative_space_build_node_indexes(struct declarative_space_preparation *preparation)
{
    size_t old_count = 0, final_count = 0;
    for (size_t i = 0; i < preparation->target_count; ++i) {
        struct declarative_space_prepared_target *target = &preparation->targets[i];
        struct view validation = *target->target.view;
        validation.root = target->final_root;
        validation.layout = declarative_space_final_layout(target);
        validation.insertion_point = target->final_insertion_point;
        struct declarative_space_root_before_image image;
        if (!declarative_space_validate_root(&validation, &image) ||
            image.node_count > SIZE_MAX - final_count ||
            (target->changed && target->root_before.node_count > SIZE_MAX - old_count)) return false;
        target->final_root_fingerprint = image.fingerprint;
        target->final_node_count = image.node_count;
        final_count += image.node_count;
        if (target->changed) old_count += target->root_before.node_count;
    }
    if (old_count > SIZE_MAX - final_count ||
        old_count + final_count > SIZE_MAX / sizeof(*preparation->old_node_index)) return false;
    if (old_count + final_count) {
        preparation->old_node_index = declarative_space_allocate(old_count + final_count,
                                                                  sizeof(*preparation->old_node_index));
        if (!preparation->old_node_index) return false;
    }
    preparation->old_node_index_count = old_count;
    preparation->final_node_index = preparation->old_node_index ? preparation->old_node_index + old_count : NULL;
    preparation->final_node_index_count = final_count;
    size_t old_position = 0, final_position = 0;
    for (size_t i = 0; i < preparation->target_count; ++i) {
        struct declarative_space_prepared_target *target = &preparation->targets[i];
        if (target->changed) declarative_space_fill_node_index(target->root_before.root,
                                                               preparation->old_node_index, &old_position);
        declarative_space_fill_node_index(target->final_root, preparation->final_node_index, &final_position);
    }
    if (old_count > 1) qsort(preparation->old_node_index, old_count,
                             sizeof(*preparation->old_node_index), declarative_space_compare_node_pointer);
    if (final_count > 1) qsort(preparation->final_node_index, final_count,
                               sizeof(*preparation->final_node_index), declarative_space_compare_node_pointer);
    return declarative_space_prepared_roots_disjoint(preparation);
}

static bool declarative_space_validate_final_state(const struct declarative_space_preparation *preparation)
{
    if (!declarative_space_prepared_roots_disjoint(preparation)) return false;
    size_t forest_count = 0;
    for (size_t i = 0; i < preparation->target_count; ++i) {
        const struct declarative_space_prepared_target *target = &preparation->targets[i];
        struct view validation = *target->target.view;
        validation.root = target->final_root;
        validation.layout = declarative_space_final_layout(target);
        validation.insertion_point = target->final_insertion_point;
        struct declarative_space_root_before_image image;
        if (!declarative_space_validate_root(&validation, &image)) return false;
        forest_count += image.window_count;
    }
    if (forest_count != preparation->final_owner_count ||
        preparation->replacement_managed_window.count != (int) preparation->final_owner_count ||
        forest_count > SIZE_MAX / sizeof(struct declarative_space_forest_index_entry)) return false;
    struct declarative_space_forest_index_entry *forest = NULL;
    if (forest_count) {
        forest = declarative_space_allocate(forest_count, sizeof(*forest));
        if (!forest) return false;
    }
    size_t indexed = 0;
    for (size_t i = 0; i < preparation->target_count; ++i) {
        declarative_space_fill_forest_index(preparation->targets[i].final_root,
                                            preparation->targets[i].target.view, forest, &indexed);
    }
    if (indexed > 1) qsort(forest, indexed, sizeof(*forest), declarative_space_compare_forest_index);
    for (size_t i = 0; i < preparation->final_owner_count; ++i) {
        const struct declarative_space_final_owner *owner = &preparation->final_owners[i];
        const struct declarative_space_forest_index_entry *entry = declarative_space_forest_find(forest, indexed, owner->wid);
        if (!entry || entry->view != owner->view ||
            table_find((struct table *) &preparation->replacement_managed_window, (void *) &owner->wid) != owner->view) {
            declarative_space_free(forest);
            return false;
        }
    }
    declarative_space_free(forest);
    return true;
}

DECLARATIVE_SPACE_INTERNAL void declarative_space_preparation_destroy(struct declarative_space_preparation *preparation)
{
    if (!preparation) return;
    for (size_t i = 0; i < preparation->target_count; ++i) {
        declarative_space_free(preparation->targets[i].owned_uuid);
        declarative_space_free(preparation->targets[i].tree_index);
        declarative_space_free(preparation->targets[i].flatten_membership);
        declarative_space_free(preparation->targets[i].flatten_order);
        declarative_space_free(preparation->targets[i].final_wids);
        if (preparation->targets[i].root_owned) declarative_space_destroy_detached_root(preparation->targets[i].final_root);
    }
    declarative_space_free(preparation->targets);
    declarative_space_free(preparation->target_index);
    declarative_space_inventory_destroy(&preparation->inventory);
    declarative_space_free(preparation->removal_wids);
    declarative_space_free(preparation->removal_windows);
    declarative_space_free(preparation->final_owners);
    declarative_space_free(preparation->old_node_index);
    declarative_space_destroy_replacement_table(&preparation->replacement_managed_window);
    memset(preparation, 0, sizeof(*preparation));
}

static bool declarative_space_prepare_effective(const struct declarative_space_target *targets,
                               size_t target_count,
                               const struct declarative_space_topology_entry *topology,
                               size_t topology_count,
                               const struct table *current_views,
                               const struct declarative_space_table_contract *view_table_contract,
                               const struct table *managed_window,
                               const struct declarative_space_table_contract *managed_table_contract,
                               const struct declarative_config_model *native,
                               const struct declarative_space_policy *policies,
                               size_t policy_count,
                               uint64_t policy_generation,
                               const struct declarative_runtime_state *runtime,
                               const struct declarative_space_defaults *defaults,
                               enum declarative_space_ownership_mode mode,
                               const struct declarative_space_capture_ops *capture_ops,
                               bool imperative_layout, enum view_type imperative_layout_value,
                               struct declarative_space_preparation *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if ((mode != DECLARATIVE_SPACE_PRESERVE_RUNTIME && mode != DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD) ||
        !defaults || defaults->layout < VIEW_BSP || defaults->layout > VIEW_ACCORDION_VERTICAL ||
        defaults->accordion_padding < 0 || defaults->single_window_max_width < 0 ||
        defaults->single_window_min_width < 0 ||
        !(defaults->single_window_width_ratio > 0.0f && defaults->single_window_width_ratio <= 1.0f) ||
        !isfinite(defaults->single_window_width_ratio) ||
        defaults->split_type < SPLIT_NONE || defaults->split_type > SPLIT_AUTO ||
        !(defaults->split_ratio >= 0.1f && defaults->split_ratio <= 0.9f) ||
        (defaults->window_placement != CHILD_FIRST && defaults->window_placement != CHILD_SECOND) ||
        defaults->window_insertion_point < INSERT_FOCUSED || defaults->window_insertion_point > INSERT_LAST ||
        (policy_count && !policies) || !capture_ops ||
        (!capture_ops->query_sid && !capture_ops->query_sid_result) ||
        !capture_ops->find_window || !capture_ops->is_eligible ||
        !(imperative_layout
          ? declarative_space_validate_imperative_targets(targets, target_count, topology, topology_count,
                                                          current_views, view_table_contract)
          : declarative_space_validate_targets(targets, target_count, topology, topology_count,
                                               current_views, view_table_contract))) return false;
    out->ownership_mode = mode;
    out->imperative_layout = imperative_layout;
    out->runtime = (struct declarative_runtime_state *) runtime;
    out->preparing_thread = pthread_self();
#ifdef TESTS
    extern uint64_t declarative_space_test_admission_token;
    out->admission_token = declarative_space_test_admission_token;
#endif
    if (runtime) memcpy(out->global_ownership_before, runtime->global, sizeof(out->global_ownership_before));
    memcpy(out->global_ownership_after, out->global_ownership_before, sizeof(out->global_ownership_after));
    if (mode == DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD) memset(out->global_ownership_after, 0, sizeof(out->global_ownership_after));
    if (target_count) {
        out->targets = declarative_space_allocate(target_count, sizeof(*out->targets));
        if (!out->targets) goto fail;
    }
    out->target_count = target_count;
    if (target_count) {
        out->target_index = declarative_space_allocate(target_count, sizeof(*out->target_index));
        if (!out->target_index) goto fail;
    }
    for (size_t i = 0; i < target_count; ++i) {
        size_t uuid_length = strlen(targets[i].uuid);
        out->targets[i].owned_uuid = declarative_space_allocate(uuid_length + 1, 1);
        if (!out->targets[i].owned_uuid) goto fail;
        memcpy(out->targets[i].owned_uuid, targets[i].uuid, uuid_length + 1);
        out->targets[i].target = targets[i];
        out->targets[i].target.uuid = out->targets[i].owned_uuid;
        if (!declarative_space_identity_valid(&out->targets[i].target.identity)) {
            (void) declarative_space_public_identity(out->targets[i].target.uuid, out->targets[i].target.sid,
                                                     out->targets[i].target.index,
                                                     out->targets[i].target.is_user_space,
                                                     &out->targets[i].target.identity);
        }
        if (!declarative_space_resolve_effective_policy(native, policies, policy_count, runtime, defaults,
                                                        &targets[i], mode, &out->targets[i].effective)) goto fail;
        if (imperative_layout) {
            struct declarative_space_effective *effective = &out->targets[i].effective;
            union declarative_runtime_scalar scalar = { 0 };
#define PRESERVE_CURRENT(field, member, scalar_member) do { \
    scalar.scalar_member = targets[i].view->member; \
    declarative_space_set_value(effective, field, DECLARATIVE_SETTING_BUILTIN, scalar); \
} while (0)
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_LAYOUT, layout, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_TOP_PADDING, top_padding, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING, bottom_padding, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING, left_padding, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING, right_padding, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP, window_gap, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING, accordion_padding, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MAX_WIDTH, single_window_max_width, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MIN_WIDTH, single_window_min_width, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_WIDTH_RATIO, single_window_width_ratio, real);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO, split_ratio, real);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE, split_type, integer);
            PRESERVE_CURRENT(DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE, auto_balance, u32);
#undef PRESERVE_CURRENT
            out->targets[i].imperative_affected = targets[i].imperative_layout_target;
            if (targets[i].imperative_layout_target) {
                scalar.integer = imperative_layout_value;
                declarative_space_set_value(effective, DECLARATIVE_RUNTIME_SPACE_LAYOUT,
                                            DECLARATIVE_SETTING_RUNTIME_SPACE, scalar);
            } else {
                out->targets[i].final_scaffold_fingerprint = targets[i].view->scaffold_fingerprint;
                memcpy(out->targets[i].final_scaffold_primary, targets[i].view->scaffold_primary,
                       sizeof(out->targets[i].final_scaffold_primary));
            }
        }
        memcpy(out->targets[i].ownership_before.view, targets[i].view->runtime_ownership, sizeof(out->targets[i].ownership_before.view));
        out->targets[i].ownership_before.legacy_flags = targets[i].view->flags;
        declarative_space_capture_view_before(targets[i].view, &out->targets[i].view_before);
        out->targets[i].ownership_after = out->targets[i].ownership_before;
        out->target_index[i] = (struct declarative_space_target_index_entry) {
            targets[i].sid, targets[i].view, i
        };
        if (mode == DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD) {
            memset(out->targets[i].ownership_after.view, 0, sizeof(out->targets[i].ownership_after.view));
            for (int field = 0; field < DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT; ++field) {
                out->targets[i].ownership_after.legacy_flags &= ~declarative_space_legacy_flag(field);
            }
        }
    }
    if (target_count > 1) qsort(out->target_index, target_count, sizeof(*out->target_index),
                                declarative_space_compare_target_index);
    for (size_t i = 1; i < target_count; ++i) {
        DECLARATIVE_SPACE_INDEX_OPERATION();
        if (out->target_index[i - 1].sid == out->target_index[i].sid) goto fail;
    }
    struct declarative_space_root_before_image *root_images = NULL;
    if (target_count) {
        root_images = declarative_space_allocate(target_count, sizeof(*root_images));
        if (!root_images) goto fail;
    }
    if (!declarative_space_validate_forest_and_table(targets, target_count, managed_window,
                                                     managed_table_contract, root_images, &out->table_before)) {
        declarative_space_free(root_images);
        goto fail;
    }
    for (size_t i = 0; i < target_count; ++i) out->targets[i].root_before = root_images[i];
    declarative_space_free(root_images);
    enum declarative_space_apply_result inventory_result = declarative_space_capture_inventory_result(
        targets, target_count, capture_ops, &out->inventory);
    if (inventory_result != DECLARATIVE_SPACE_APPLY_OK) {
        declarative_space_prepare_failure = inventory_result;
        goto fail;
    }

    for (size_t i = 0; i < out->inventory.count; ++i) {
        const struct declarative_space_inventory_entry *entry = &out->inventory.entries[i];
        const struct declarative_space_prepared_target *owner = declarative_space_prepared_target_find(out, entry->sid);
        if (!owner) goto fail;
        if (entry->eligible && declarative_space_target_accepts(owner, entry->sid) && !entry->window) goto fail;
    }

    if (managed_window->count) {
        out->removal_wids = declarative_space_allocate((size_t) managed_window->count, sizeof(*out->removal_wids));
        out->removal_windows = declarative_space_allocate((size_t) managed_window->count, sizeof(*out->removal_windows));
        if (!out->removal_wids || !out->removal_windows) goto fail;
    }
    for (int i = 0; i < managed_window->capacity; ++i) {
        for (struct bucket *bucket = managed_window->buckets[i]; bucket; bucket = bucket->next) {
            uint32_t wid = *(uint32_t *) bucket->key;
            const struct declarative_space_inventory_entry *entry = declarative_space_inventory_find(&out->inventory, wid);
            bool remains = false;
            if (entry && entry->eligible && entry->window) {
                const struct declarative_space_prepared_target *target = declarative_space_prepared_target_find(out, entry->sid);
                remains = target && declarative_space_target_accepts(target, entry->sid);
            }
            if (!remains) {
                out->removal_wids[out->removal_count] = wid;
                out->removal_windows[out->removal_count] = entry ? entry->window : capture_ops->find_window(wid, capture_ops->context);
                ++out->removal_count;
            }
        }
    }
    if (!declarative_space_build_owner_map(out) ||
        !declarative_space_prepare_scaffolds(out, policies, policy_count, policy_generation) ||
        !declarative_space_build_final_roots(out, defaults) ||
        !declarative_space_build_replacement_table(out, managed_table_contract) ||
        !declarative_space_build_node_indexes(out) ||
        !declarative_space_validate_final_state(out)) goto fail;
    out->prepared = true;
    return true;
fail:
    declarative_space_preparation_destroy(out);
    return false;
}

/* Phase 3C: the publication core below deliberately contains only guards and stores. */
#ifdef TESTS
uint64_t declarative_space_test_admission_token;
static bool declarative_space_test_admission_allowed = true;
static declarative_space_test_assertion_observer declarative_space_assertion_observer;
static void *declarative_space_assertion_context;
static size_t declarative_space_assertions;
void declarative_space_test_set_admission(uint64_t token, bool allowed)
{
    declarative_space_test_admission_token = token;
    declarative_space_test_admission_allowed = allowed;
}
void declarative_space_test_set_assertion_observer(declarative_space_test_assertion_observer observer,
                                                    void *context)
{
    declarative_space_assertion_observer = observer;
    declarative_space_assertion_context = context;
    declarative_space_assertions = 0;
}
size_t declarative_space_test_assertion_count(void)
{
    return declarative_space_assertions;
}
size_t declarative_space_test_commit_forbidden_calls(void)
{
    return declarative_space_commit_forbidden_calls;
}
#endif
static bool declarative_space_postcommit_active;

static bool declarative_space_invariant_rejected(void)
{
#ifdef TESTS
    ++declarative_space_assertions;
    if (declarative_space_assertion_observer) {
        declarative_space_assertion_observer(declarative_space_assertion_context);
    }
#elif !defined(NDEBUG)
    assert(!"declarative space synchronous publication invariant violated");
#endif
    return false;
}

static bool declarative_space_thread_admitted(const struct declarative_space_preparation *preparation)
{
    if (!pthread_equal(pthread_self(), preparation->preparing_thread)) return false;
#ifdef TESTS
    return declarative_space_test_admission_allowed &&
           preparation->admission_token == declarative_space_test_admission_token;
#else
    extern struct event_loop g_event_loop;
    return pthread_equal(pthread_self(), g_event_loop.thread);
#endif
}

static bool declarative_space_root_fingerprint_node(struct window_node *node, size_t *remaining, uint64_t *hash)
{
    if (!node) return true;
    if (!*remaining) return false;
    --*remaining;
    *hash = declarative_space_hash_u64(*hash, (uintptr_t) node);
    *hash = declarative_space_hash_u64(*hash, (uint32_t) node->window_count);
    *hash = declarative_space_hash_u64(*hash, (uintptr_t) node->parent);
    *hash = declarative_space_hash_u64(*hash, (uintptr_t) node->left);
    *hash = declarative_space_hash_u64(*hash, (uintptr_t) node->right);
    *hash = declarative_space_hash_u64(*hash, (uintptr_t) node->zoom);
    *hash = declarative_space_hash_u64(*hash, (uint32_t) node->split);
    *hash = declarative_space_hash_u64(*hash, (uint32_t) node->child);
    *hash = declarative_space_hash_u64(*hash, (uint32_t) node->insert_dir);
    *hash = declarative_space_hash_u64(*hash, node->feedback_window.id);
    *hash = declarative_space_hash_u64(*hash, (uintptr_t) node->feedback_window.context);
    *hash = declarative_space_hash_u64(*hash, node->has_window_areas);
    uint32_t bits;
    memcpy(&bits, &node->ratio, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
    memcpy(&bits, &node->area.x, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
    memcpy(&bits, &node->area.y, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
    memcpy(&bits, &node->area.w, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
    memcpy(&bits, &node->area.h, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
    if (node->window_count < 0 || node->window_count > NODE_MAX_WINDOW_COUNT) return false;
    for (int i = 0; i < node->window_count; ++i) {
        *hash = declarative_space_hash_u64(*hash, node->window_list[i]);
        if (node->has_window_areas) {
            memcpy(&bits, &node->window_area[i].x, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
            memcpy(&bits, &node->window_area[i].y, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
            memcpy(&bits, &node->window_area[i].w, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
            memcpy(&bits, &node->window_area[i].h, sizeof(bits)); *hash = declarative_space_hash_u64(*hash, bits);
        }
    }
    for (int i = 0; i < node->window_count; ++i) *hash = declarative_space_hash_u64(*hash, node->window_order[i]);
    return declarative_space_root_fingerprint_node(node->left, remaining, hash) &&
           declarative_space_root_fingerprint_node(node->right, remaining, hash);
}

static uint64_t declarative_space_table_fingerprint(const struct table *table)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = declarative_space_hash_u64(hash, (uintptr_t) table->buckets);
    hash = declarative_space_hash_u64(hash, (uint32_t) table->capacity);
    hash = declarative_space_hash_u64(hash, (uint32_t) table->count);
    uint32_t bits; memcpy(&bits, &table->max_load, sizeof(bits));
    hash = declarative_space_hash_u64(hash, bits);
    hash = declarative_space_hash_u64(hash, (uintptr_t) table->hash);
    hash = declarative_space_hash_u64(hash, (uintptr_t) table->cmp);
    for (int i = 0; i < table->capacity; ++i) {
        for (struct bucket *bucket = table->buckets[i]; bucket; bucket = bucket->next) {
            hash = declarative_space_hash_u64(hash, (uint32_t) i);
            hash = declarative_space_hash_u64(hash, (uintptr_t) bucket);
            hash = declarative_space_hash_u64(hash, (uintptr_t) bucket->key);
            hash = declarative_space_hash_u64(hash, *(uint32_t *) bucket->key);
            hash = declarative_space_hash_u64(hash, (uintptr_t) bucket->value);
            hash = declarative_space_hash_u64(hash, (uintptr_t) bucket->next);
        }
    }
    return hash;
}

static bool declarative_space_commit_guards(const struct declarative_space_transition *transition)
{
    const struct declarative_space_preparation *p = &transition->preparation;
    if (transition->state != DECLARATIVE_SPACE_TRANSITION_PREPARED) return false;
    if (!p->prepared || declarative_space_postcommit_active || !declarative_space_thread_admitted(p) ||
        !declarative_space_global_ownership_before_matches(p->runtime, p->global_ownership_before) ||
        !declarative_space_prepared_roots_disjoint(p)) {
        return declarative_space_invariant_rejected();
    }
    const struct declarative_space_table_before_image *table = &p->table_before;
    if (g_window_manager.managed_window.buckets != table->buckets ||
        g_window_manager.managed_window.capacity != table->capacity ||
        g_window_manager.managed_window.count != table->count ||
        g_window_manager.managed_window.max_load != table->max_load ||
        g_window_manager.managed_window.hash != table->hash ||
        g_window_manager.managed_window.cmp != table->cmp ||
        declarative_space_table_fingerprint(&g_window_manager.managed_window) != table->fingerprint) {
        return declarative_space_invariant_rejected();
    }
    for (size_t i = 0; i < p->target_count; ++i) {
        const struct declarative_space_prepared_target *target = &p->targets[i];
        size_t remaining = target->root_before.node_count;
        uint64_t fingerprint = UINT64_C(1469598103934665603);
        size_t final_remaining = target->final_node_count;
        uint64_t final_fingerprint = UINT64_C(1469598103934665603);
        size_t visited = 0;
        if (target->target.view->root != target->root_before.root ||
            !declarative_space_root_fingerprint_node(target->target.view->root, &remaining, &fingerprint) || remaining != 0 ||
            fingerprint != target->root_before.fingerprint ||
            !declarative_space_root_fingerprint_node(target->final_root, &final_remaining, &final_fingerprint) ||
            final_remaining != 0 || final_fingerprint != target->final_root_fingerprint ||
            (target->changed &&
             (!declarative_space_tree_disjoint_from_index(target->root_before.root, p->final_node_index,
                                                          p->final_node_index_count, &visited) ||
              visited != target->root_before.node_count)) ||
            !declarative_space_view_before_matches(target->target.view, &target->view_before) ||
            !declarative_space_view_ownership_before_matches(target->target.view, &target->ownership_before)) {
            return declarative_space_invariant_rejected();
        }
    }
    return true;
}

static size_t declarative_space_root_node_count(struct window_node *root)
{
    return root ? 1 + declarative_space_root_node_count(root->left) + declarative_space_root_node_count(root->right) : 0;
}

static void declarative_space_transfer_active_allocations(struct declarative_space_preparation *p)
{
    size_t transferred = p->replacement_managed_window.buckets ? 1 + (size_t) p->replacement_managed_window.count * 2 : 0;
    for (size_t i = 0; i < p->target_count; ++i) {
        if (p->targets[i].root_owned) transferred += declarative_space_root_node_count(p->targets[i].final_root);
    }
    assert(declarative_space_outstanding >= transferred);
    declarative_space_outstanding -= transferred;
}

static bool declarative_space_transition_commit_impl(struct declarative_space_transition *transition)
{
#ifdef TESTS
    declarative_space_commit_forbidden_calls = 0;
#endif
    if (!transition || !declarative_space_commit_guards(transition)) return false;
#ifdef TESTS
    declarative_space_commit_active = true;
#endif
    struct declarative_space_preparation *p = &transition->preparation;
    transition->retired_managed_window = g_window_manager.managed_window;
    for (size_t i = 0; i < p->target_count; ++i) {
        struct declarative_space_prepared_target *target = &p->targets[i];
        struct view *view = target->target.view;
        bool geometry_changed = target->changed || view->layout != declarative_space_final_layout(target) ||
            view->top_padding != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].value.integer ||
            view->bottom_padding != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING].value.integer ||
            view->left_padding != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING].value.integer ||
            view->right_padding != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING].value.integer ||
            view->window_gap != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer ||
            view->accordion_padding != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING].value.integer ||
            view->single_window_max_width != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MAX_WIDTH].value.integer ||
            view->single_window_min_width != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MIN_WIDTH].value.integer ||
            view->single_window_width_ratio != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_WIDTH_RATIO].value.real ||
            (target->final_scaffold_fingerprint &&
             view->split_ratio != target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real);
        if (target->changed) view->root = target->final_root;
        if (!p->imperative_layout) view->declarative_identity = target->target.identity;
        view->layout = declarative_space_final_layout(target);
        view->top_padding = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_TOP_PADDING].value.integer;
        view->bottom_padding = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING].value.integer;
        view->left_padding = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING].value.integer;
        view->right_padding = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING].value.integer;
        view->window_gap = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP].value.integer;
        view->accordion_padding = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING].value.integer;
        view->single_window_max_width = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MAX_WIDTH].value.integer;
        view->single_window_min_width = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_MIN_WIDTH].value.integer;
        view->single_window_width_ratio = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SINGLE_WINDOW_WIDTH_RATIO].value.real;
        view->split_ratio = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO].value.real;
        view->split_type = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE].value.integer;
        view->auto_balance = target->effective.fields[DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE].value.u32;
        view->insertion_point = target->final_insertion_point;
        view->scaffold_fingerprint = target->final_scaffold_fingerprint;
        memcpy(view->scaffold_primary, target->final_scaffold_primary, sizeof(view->scaffold_primary));
        if (p->ownership_mode == DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD) {
            memcpy(view->runtime_ownership, target->ownership_after.view, sizeof(view->runtime_ownership));
            view->flags = target->ownership_after.legacy_flags;
        }
        if (geometry_changed) { view_clear_flag(view, VIEW_IS_VALID); view_set_flag(view, VIEW_IS_DIRTY); }
    }
    if (p->ownership_mode == DECLARATIVE_SPACE_CLEAR_RUNTIME_FOR_RELOAD && p->runtime) {
        memcpy(p->runtime->global, p->global_ownership_after, sizeof(p->runtime->global));
    }
    g_window_manager.managed_window = p->replacement_managed_window;
    declarative_space_transfer_active_allocations(p);
    memset(&p->replacement_managed_window, 0, sizeof(p->replacement_managed_window));
    for (size_t i = 0; i < p->target_count; ++i) p->targets[i].root_owned = false;
    transition->state = DECLARATIVE_SPACE_TRANSITION_COMMITTED;
#ifdef TESTS
    declarative_space_commit_active = false;
#endif
    return true;
}

#ifdef TESTS
bool declarative_space_transition_commit(struct declarative_space_transition *transition)
{
    return declarative_space_transition_commit_impl(transition);
}
#endif

static bool declarative_space_default_layer_normal(struct window *window, void *context)
{ (void) context; window_manager_adjust_layer(window, LAYER_NORMAL); return true; }
static bool declarative_space_default_layer_below(struct window *window, void *context)
{ (void) context; window_manager_adjust_layer(window, LAYER_BELOW); return true; }
static bool declarative_space_default_purify(struct window *window, void *context)
{ (void) context; window_manager_purify_window(&g_window_manager, window); return true; }
static bool declarative_space_default_update(struct view *view, void *context)
{ (void) context; view_update(view); return true; }
static bool declarative_space_default_flush(struct view *view, void *context)
{ (void) context; view_flush(view); return true; }
static bool declarative_space_default_feedback(struct window_node *node, void *context)
{ (void) context; insert_feedback_destroy(node); return true; }

static struct declarative_space_postcommit_ops declarative_space_ops(const struct declarative_space_postcommit_ops *ops)
{
    struct declarative_space_postcommit_ops result = ops ? *ops : (struct declarative_space_postcommit_ops) { 0 };
    if (!result.layer_normal) result.layer_normal = declarative_space_default_layer_normal;
    if (!result.layer_below) result.layer_below = declarative_space_default_layer_below;
    if (!result.purify) result.purify = declarative_space_default_purify;
    if (!result.update) result.update = declarative_space_default_update;
    if (!result.flush) result.flush = declarative_space_default_flush;
    if (!result.dispose_feedback) result.dispose_feedback = declarative_space_default_feedback;
    return result;
}

#define DECLARATIVE_SPACE_ATTEMPT(t, field, expression) do { \
    ++(t)->diagnostics.attempted; \
    if (!(expression)) { ++(t)->diagnostics.failed; ++(t)->diagnostics.field; } \
} while (0)

static bool declarative_space_transition_postcommit_impl(struct declarative_space_transition *transition,
                                                          const struct declarative_space_postcommit_ops *provided)
{
    if (!transition || transition->state != DECLARATIVE_SPACE_TRANSITION_COMMITTED ||
        declarative_space_postcommit_active) return false;
    struct declarative_space_postcommit_ops ops = declarative_space_ops(provided);
    struct declarative_space_preparation *p = &transition->preparation;
    declarative_space_postcommit_active = true;
    for (size_t i = 0; i < p->removal_count; ++i) {
        struct window *window = p->removal_windows[i];
        if (!window) continue;
        DECLARATIVE_SPACE_ATTEMPT(transition, layer_normal_failed, ops.layer_normal(window, ops.context));
        DECLARATIVE_SPACE_ATTEMPT(transition, purify_failed, ops.purify(window, ops.context));
    }
    for (size_t i = 0; i < p->final_owner_count; ++i) {
        if (p->imperative_layout) {
            const struct declarative_space_prepared_target *target =
                declarative_space_prepared_target_find(p, p->final_owners[i].view->sid);
            if (!target || !target->imperative_affected) continue;
        }
        struct window *window = p->final_owners[i].window;
        DECLARATIVE_SPACE_ATTEMPT(transition, layer_below_failed, ops.layer_below(window, ops.context));
        DECLARATIVE_SPACE_ATTEMPT(transition, purify_failed, ops.purify(window, ops.context));
    }
    for (size_t i = 0; i < p->target_count; ++i) {
        struct declarative_space_prepared_target *target = &p->targets[i];
        if (p->imperative_layout && !target->imperative_affected && !target->membership_changed) continue;
        struct view *view = target->target.view;
        if (!view_is_invalid(view) && !view_is_dirty(view)) continue;
        bool updated = ops.update(view, ops.context);
        DECLARATIVE_SPACE_ATTEMPT(transition, update_failed, updated);
        if (updated) { view_set_flag(view, VIEW_IS_VALID); view_set_flag(view, VIEW_IS_DIRTY); }
        bool flushed = ops.flush(view, ops.context);
        DECLARATIVE_SPACE_ATTEMPT(transition, flush_failed, flushed);
        /* view_flush deliberately leaves hidden spaces dirty.  A successful
         * call is therefore distinct from an applied flush. */
        bool deferred = updated && flushed && view_is_dirty(view);
        if (deferred) ++transition->diagnostics.flush_deferred;
        if (!updated || !flushed || deferred) {
            view_clear_flag(view, VIEW_IS_VALID);
            view_set_flag(view, VIEW_IS_DIRTY);
        } else {
            view_set_flag(view, VIEW_IS_VALID);
            view_clear_flag(view, VIEW_IS_DIRTY);
        }
    }
    declarative_space_postcommit_active = false;
    transition->state = DECLARATIVE_SPACE_TRANSITION_POSTCOMMIT_ATTEMPTED;
    return true;
}

#ifdef TESTS
bool declarative_space_transition_postcommit(struct declarative_space_transition *transition,
                                              const struct declarative_space_postcommit_ops *ops)
{ return declarative_space_transition_postcommit_impl(transition, ops); }
#endif

static bool declarative_space_dispose_retired_table_impl(struct declarative_space_transition *transition)
{
    if (!transition || transition->state != DECLARATIVE_SPACE_TRANSITION_POSTCOMMIT_ATTEMPTED ||
        transition->retired_table_disposed) return false;
    if (transition->retired_managed_window.buckets) table_free(&transition->retired_managed_window);
    memset(&transition->retired_managed_window, 0, sizeof(transition->retired_managed_window));
    transition->retired_table_disposed = true;
    return true;
}

#ifdef TESTS
bool declarative_space_dispose_retired_table(struct declarative_space_transition *transition)
{ return declarative_space_dispose_retired_table_impl(transition); }
#endif

static void declarative_space_retire_node(struct window_node *node,
                                          const struct declarative_space_postcommit_ops *ops,
                                          struct declarative_space_transition *transition)
{
    if (!node) return;
    declarative_space_retire_node(node->left, ops, transition);
    declarative_space_retire_node(node->right, ops, transition);
    if (g_mouse_state.feedback_node == node) g_mouse_state.feedback_node = NULL;
    if (node->feedback_window.id) {
        table_remove(&g_window_manager.insert_feedback, &node->window_order[0]);
        DECLARATIVE_SPACE_ATTEMPT(transition, feedback_failed, ops->dispose_feedback(node, ops->context));
        memset(&node->feedback_window, 0, sizeof(node->feedback_window));
    }
    free(node);
}

static bool declarative_space_retire_old_roots_impl(struct declarative_space_transition *transition,
                                                     const struct declarative_space_postcommit_ops *provided)
{
    if (!transition || transition->state != DECLARATIVE_SPACE_TRANSITION_POSTCOMMIT_ATTEMPTED ||
        !transition->retired_table_disposed || transition->retired_managed_window.buckets) return false;
    struct declarative_space_postcommit_ops ops = declarative_space_ops(provided);
    struct declarative_space_preparation *p = &transition->preparation;
    bool safe = true;
    for (size_t i = 0; i < p->target_count; ++i) {
        struct declarative_space_prepared_target *target = &p->targets[i];
        if (!target->changed || target->root_before.root == target->final_root) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (p->targets[j].changed && p->targets[j].root_before.root == target->root_before.root) duplicate = true;
        }
        if (!duplicate) {
            size_t remaining = target->root_before.node_count;
            uint64_t fingerprint = UINT64_C(1469598103934665603);
            size_t visited = 0;
            bool intact = remaining != 0 &&
                          declarative_space_root_fingerprint_node(target->root_before.root, &remaining, &fingerprint) &&
                          remaining == 0 && fingerprint == target->root_before.fingerprint;
            bool disjoint = declarative_space_tree_disjoint_from_index(target->root_before.root,
                                                                        p->final_node_index,
                                                                        p->final_node_index_count,
                                                                        &visited) &&
                            visited == target->root_before.node_count;
            if (!intact || !disjoint) {
                (void) declarative_space_invariant_rejected();
                safe = false;
            } else {
                declarative_space_retire_node(target->root_before.root, &ops, transition);
            }
        }
        target->root_before.root = NULL;
    }
    transition->state = DECLARATIVE_SPACE_TRANSITION_RETIRED;
    return safe;
}

#ifdef TESTS
bool declarative_space_retire_old_roots(struct declarative_space_transition *transition,
                                        const struct declarative_space_postcommit_ops *ops)
{ return declarative_space_retire_old_roots_impl(transition, ops); }
#endif

DECLARATIVE_SPACE_INTERNAL bool declarative_space_transition_begin(struct declarative_space_transition *transition,
                                        struct declarative_space_preparation *preparation)
{
    if (!transition || !preparation || !preparation->prepared) return false;
    memset(transition, 0, sizeof(*transition));
    transition->preparation = *preparation;
    memset(preparation, 0, sizeof(*preparation));
    transition->state = DECLARATIVE_SPACE_TRANSITION_PREPARED;
    return true;
}

DECLARATIVE_SPACE_INTERNAL void declarative_space_transition_destroy(struct declarative_space_transition *transition)
{
    if (!transition) return;
    if (transition->state == DECLARATIVE_SPACE_TRANSITION_COMMITTED) {
        (void) declarative_space_transition_postcommit_impl(transition, NULL);
    }
    if (transition->state == DECLARATIVE_SPACE_TRANSITION_POSTCOMMIT_ATTEMPTED) {
        (void) declarative_space_dispose_retired_table_impl(transition);
        (void) declarative_space_retire_old_roots_impl(transition, NULL);
    }
    if (transition->state == DECLARATIVE_SPACE_TRANSITION_PREPARED ||
        transition->state == DECLARATIVE_SPACE_TRANSITION_RETIRED) {
        declarative_space_preparation_destroy(&transition->preparation);
        memset(transition, 0, sizeof(*transition));
    }
}

#ifdef TESTS
bool declarative_space_apply_prepared(struct declarative_space_preparation *preparation,
                                      const struct declarative_space_postcommit_ops *ops,
                                      struct declarative_space_postcommit_diagnostics *diagnostics)
{
    struct declarative_space_transition transition;
    if (!declarative_space_transition_begin(&transition, preparation)) return false;
    if (!declarative_space_transition_commit_impl(&transition)) {
        declarative_space_transition_destroy(&transition);
        return false;
    }
    /* Publication is the point of no rollback.  Every subsequent phase is
     * attempted unconditionally and the transition is always reclaimed. */
    (void) declarative_space_transition_postcommit_impl(&transition, ops);
    (void) declarative_space_dispose_retired_table_impl(&transition);
    (void) declarative_space_retire_old_roots_impl(&transition, ops);
    if (diagnostics) *diagnostics = transition.diagnostics;
    declarative_space_transition_destroy(&transition);
    return true;
}
#endif

#ifdef TESTS
bool declarative_space_prepare(const struct declarative_space_target *targets, size_t target_count,
                               const struct declarative_space_topology_entry *topology, size_t topology_count,
                               const struct table *current_views,
                               const struct declarative_space_table_contract *view_table_contract,
                               const struct table *managed_window,
                               const struct declarative_space_table_contract *managed_table_contract,
                               const struct declarative_config_model *native,
                               const struct declarative_runtime_state *runtime,
                               const struct declarative_space_defaults *defaults,
                               enum declarative_space_ownership_mode mode,
                               const struct declarative_space_capture_ops *capture_ops,
                               struct declarative_space_preparation *out)
{
    return declarative_space_prepare_effective(targets, target_count, topology, topology_count,
                                               current_views, view_table_contract, managed_window,
                                               managed_table_contract, native,
                                               native ? native->spaces : NULL,
                                               native ? native->space_count : 0, 0,
                                               runtime, defaults, mode, capture_ops,
                                               false, VIEW_DEFAULT, out);
}
#endif

enum declarative_space_apply_result declarative_space_apply_imperative_layout(
                                              const struct declarative_space_target *targets,
                                              size_t target_count,
                                              const struct declarative_space_topology_entry *topology,
                                              const struct table *current_views,
                                              const struct declarative_space_table_contract *view_table_contract,
                                              const struct declarative_space_table_contract *managed_table_contract,
                                              const struct declarative_space_defaults *defaults,
                                              enum view_type layout,
                                              const struct declarative_space_capture_ops *capture_ops,
                                              declarative_space_publication_callback publish,
                                              void *publish_context)
{
    struct declarative_space_preparation preparation;
    struct declarative_space_transition transition;
    declarative_space_allocation_failed = false;
    declarative_space_prepare_failure = DECLARATIVE_SPACE_APPLY_TERMINAL;
    if (!declarative_space_prepare_effective(targets, target_count, topology, target_count,
                                             current_views, view_table_contract,
                                             &g_window_manager.managed_window, managed_table_contract,
                                             NULL, NULL, 0, 0, &g_declarative_runtime_state, defaults,
                                             DECLARATIVE_SPACE_PRESERVE_RUNTIME, capture_ops,
                                             true, layout, &preparation)) {
        return declarative_space_allocation_failed ? DECLARATIVE_SPACE_APPLY_RESOURCE
                                                   : declarative_space_prepare_failure;
    }
    if (!declarative_space_transition_begin(&transition, &preparation)) {
        declarative_space_preparation_destroy(&preparation);
        return DECLARATIVE_SPACE_APPLY_TERMINAL;
    }
    if (!declarative_space_transition_commit_impl(&transition)) {
        declarative_space_transition_destroy(&transition);
        return DECLARATIVE_SPACE_APPLY_TERMINAL;
    }
    if (publish) publish(publish_context);
    (void) declarative_space_transition_postcommit_impl(&transition, NULL);
    (void) declarative_space_dispose_retired_table_impl(&transition);
    (void) declarative_space_retire_old_roots_impl(&transition, NULL);
    declarative_space_transition_destroy(&transition);
    return DECLARATIVE_SPACE_APPLY_OK;
}

enum declarative_space_apply_result declarative_space_apply_with_effective_publication_result(
                                              const struct declarative_space_target *targets,
                                              size_t target_count,
                                              const struct declarative_space_topology_entry *topology,
                                              size_t topology_count,
                                              const struct table *current_views,
                                              const struct declarative_space_table_contract *view_table_contract,
                                              const struct declarative_space_table_contract *managed_table_contract,
                                              const struct declarative_config_model *native,
                                              const struct declarative_space_policy *policies,
                                              size_t policy_count,
                                              uint64_t policy_generation,
                                              struct declarative_runtime_state *runtime,
                                              const struct declarative_space_defaults *defaults,
                                              enum declarative_space_ownership_mode mode,
                                              const struct declarative_space_capture_ops *capture_ops,
                                              const struct declarative_space_postcommit_ops *postcommit_ops,
                                              struct declarative_space_postcommit_diagnostics *diagnostics,
                                              declarative_space_publication_callback publish,
                                              void *publish_context)
{
    struct declarative_space_preparation preparation;
    struct declarative_space_transition transition;
    declarative_space_allocation_failed = false;
    declarative_space_prepare_failure = DECLARATIVE_SPACE_APPLY_TERMINAL;
    if (!declarative_space_prepare_effective(targets, target_count, topology, topology_count,
                                             current_views, view_table_contract,
                                             &g_window_manager.managed_window, managed_table_contract,
                                             native, policies, policy_count, policy_generation,
                                             runtime, defaults, mode, capture_ops,
                                             false, VIEW_DEFAULT, &preparation)) {
        return declarative_space_allocation_failed ? DECLARATIVE_SPACE_APPLY_RESOURCE
                                                   : declarative_space_prepare_failure;
    }
    if (!declarative_space_transition_begin(&transition, &preparation)) {
        declarative_space_preparation_destroy(&preparation);
        return DECLARATIVE_SPACE_APPLY_TERMINAL;
    }
    if (!declarative_space_transition_commit_impl(&transition)) {
        declarative_space_transition_destroy(&transition);
        return DECLARATIVE_SPACE_APPLY_TERMINAL;
    }
    if (publish) publish(publish_context);
    (void) declarative_space_transition_postcommit_impl(&transition, postcommit_ops);
    (void) declarative_space_dispose_retired_table_impl(&transition);
    (void) declarative_space_retire_old_roots_impl(&transition, postcommit_ops);
    if (diagnostics) *diagnostics = transition.diagnostics;
    declarative_space_transition_destroy(&transition);
    return DECLARATIVE_SPACE_APPLY_OK;
}

bool declarative_space_apply_with_effective_publication(const struct declarative_space_target *targets,
                                              size_t target_count,
                                              const struct declarative_space_topology_entry *topology,
                                              size_t topology_count,
                                              const struct table *current_views,
                                              const struct declarative_space_table_contract *view_table_contract,
                                              const struct declarative_space_table_contract *managed_table_contract,
                                              const struct declarative_config_model *native,
                                              const struct declarative_space_policy *policies,
                                              size_t policy_count,
                                              uint64_t policy_generation,
                                              struct declarative_runtime_state *runtime,
                                              const struct declarative_space_defaults *defaults,
                                              enum declarative_space_ownership_mode mode,
                                              const struct declarative_space_capture_ops *capture_ops,
                                              const struct declarative_space_postcommit_ops *postcommit_ops,
                                              struct declarative_space_postcommit_diagnostics *diagnostics,
                                              declarative_space_publication_callback publish,
                                              void *publish_context)
{
    return declarative_space_apply_with_effective_publication_result(
        targets, target_count, topology, topology_count, current_views, view_table_contract,
        managed_table_contract, native, policies, policy_count, policy_generation, runtime,
        defaults, mode, capture_ops, postcommit_ops, diagnostics, publish, publish_context) ==
        DECLARATIVE_SPACE_APPLY_OK;
}

bool declarative_space_apply_with_publication(const struct declarative_space_target *targets,
                                              size_t target_count,
                                              const struct declarative_space_topology_entry *topology,
                                              size_t topology_count,
                                              const struct table *current_views,
                                              const struct declarative_space_table_contract *view_table_contract,
                                              const struct declarative_space_table_contract *managed_table_contract,
                                              const struct declarative_config_model *native,
                                              struct declarative_runtime_state *runtime,
                                              const struct declarative_space_defaults *defaults,
                                              enum declarative_space_ownership_mode mode,
                                              const struct declarative_space_capture_ops *capture_ops,
                                              const struct declarative_space_postcommit_ops *postcommit_ops,
                                              struct declarative_space_postcommit_diagnostics *diagnostics,
                                              declarative_space_publication_callback publish,
                                              void *publish_context)
{
    return declarative_space_apply_with_effective_publication(targets, target_count, topology, topology_count,
                                                               current_views, view_table_contract, managed_table_contract,
                                                               native, native ? native->spaces : NULL,
                                                               native ? native->space_count : 0, 0, runtime, defaults,
                                                               mode, capture_ops, postcommit_ops, diagnostics,
                                                               publish, publish_context);
}

bool declarative_space_apply(const struct declarative_space_target *targets,
                             size_t target_count,
                             const struct declarative_space_topology_entry *topology,
                             size_t topology_count,
                             const struct table *current_views,
                             const struct declarative_space_table_contract *view_table_contract,
                             const struct declarative_space_table_contract *managed_table_contract,
                             const struct declarative_config_model *native,
                             struct declarative_runtime_state *runtime,
                             const struct declarative_space_defaults *defaults,
                             enum declarative_space_ownership_mode mode,
                             const struct declarative_space_capture_ops *capture_ops,
                             const struct declarative_space_postcommit_ops *postcommit_ops,
                             struct declarative_space_postcommit_diagnostics *diagnostics)
{
    return declarative_space_apply_with_publication(targets, target_count, topology, topology_count,
                                                    current_views, view_table_contract, managed_table_contract,
                                                    native, runtime, defaults, mode, capture_ops, postcommit_ops,
                                                    diagnostics, NULL, NULL);
}
