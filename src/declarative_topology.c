extern int g_connection;

#ifdef TESTS
static size_t declarative_topology_fail_ordinal;
static size_t declarative_topology_allocation_ordinal;
static size_t declarative_topology_outstanding;

void declarative_topology_test_fail_allocation_at(size_t ordinal)
{
    declarative_topology_fail_ordinal = ordinal;
    declarative_topology_allocation_ordinal = 0;
}
size_t declarative_topology_test_allocation_count(void) { return declarative_topology_allocation_ordinal; }
size_t declarative_topology_test_outstanding_allocations(void) { return declarative_topology_outstanding; }
void declarative_topology_test_reset_allocator(void)
{
    declarative_topology_fail_ordinal = 0;
    declarative_topology_allocation_ordinal = 0;
}
static bool declarative_topology_should_fail(void)
{
    ++declarative_topology_allocation_ordinal;
    return declarative_topology_fail_ordinal && declarative_topology_allocation_ordinal == declarative_topology_fail_ordinal;
}
#else
static bool declarative_topology_should_fail(void) { return false; }
#endif

static void *declarative_topology_calloc(size_t count, size_t size)
{
    if (count && size > SIZE_MAX / count) return NULL;
    if (declarative_topology_should_fail()) return NULL;
    void *value = calloc(count, size);
#ifdef TESTS
    if (value) ++declarative_topology_outstanding;
#endif
    return value;
}

static void declarative_topology_free(void *value)
{
    if (!value) return;
#ifdef TESTS
    assert(declarative_topology_outstanding > 0);
    --declarative_topology_outstanding;
#endif
    free(value);
}

static void declarative_topology_disown(void *value)
{
#ifdef TESTS
    if (value) {
        assert(declarative_topology_outstanding > 0);
        --declarative_topology_outstanding;
    }
#else
    (void) value;
#endif
}

static char *declarative_topology_copy_cfstring_allow_empty(CFStringRef value, bool allow_empty)
{
    if (!cf_is_type(value, CFStringGetTypeID()) || (!allow_empty && CFStringGetLength(value) == 0)) return NULL;
    char bounded[DECLARATIVE_CONFIG_MAX_STRING_BYTES + 1];
    if (!CFStringGetCString(value, bounded, sizeof(bounded), kCFStringEncodingUTF8) ||
        (!allow_empty && bounded[0] == '\0')) return NULL;
    size_t length = strlen(bounded);
    char *copy = declarative_topology_calloc(length + 1, 1);
    if (!copy) return NULL;
    memcpy(copy, bounded, length + 1);
    return copy;
}

static char *declarative_topology_copy_cfstring(CFStringRef value)
{
    return declarative_topology_copy_cfstring_allow_empty(value, false);
}

static bool declarative_topology_identity_init(struct declarative_space_identity *identity,
                                               const char *uuid,
                                               const struct declarative_topology_display *display,
                                               uint64_t sid, int kind, int ordinal)
{
    if (!identity || !uuid || !display || !display->uuid || strlen(uuid) >= sizeof(identity->uuid) ||
        strlen(display->uuid) >= sizeof(identity->parent_display_uuid)) return false;
    *identity = (struct declarative_space_identity) {
        .has_uuid = uuid[0] != '\0', .parent_did = display->did,
        .parent_display_ordinal = display->managed_ordinal, .sid = sid,
        .kind = kind, .mission_control_ordinal = ordinal
    };
    memcpy(identity->uuid, uuid, strlen(uuid) + 1);
    memcpy(identity->parent_display_uuid, display->uuid, strlen(display->uuid) + 1);
    identity->fingerprint = declarative_space_identity_fingerprint(identity);
    return declarative_space_identity_valid(identity);
}

static uint64_t declarative_topology_hash_bytes(uint64_t hash, const void *bytes, size_t length)
{
    const unsigned char *cursor = bytes;
    for (size_t i = 0; i < length; ++i) {
        hash ^= cursor[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t declarative_topology_fingerprint(const struct declarative_topology *topology)
{
    uint64_t hash = 1469598103934665603ULL;
    hash = declarative_topology_hash_bytes(hash, &topology->display_count, sizeof(topology->display_count));
    hash = declarative_topology_hash_bytes(hash, &topology->space_count, sizeof(topology->space_count));
    hash = declarative_topology_hash_bytes(hash, &topology->active_did, sizeof(topology->active_did));
    hash = declarative_topology_hash_bytes(hash, &topology->active_sid, sizeof(topology->active_sid));
    for (size_t i = 0; i < topology->display_count; ++i) {
        const struct declarative_topology_display *display = &topology->displays[i];
        hash = declarative_topology_hash_bytes(hash, &display->did, sizeof(display->did));
        hash = declarative_topology_hash_bytes(hash, display->uuid, strlen(display->uuid) + 1);
        hash = declarative_topology_hash_bytes(hash, &display->bounds, sizeof(display->bounds));
        hash = declarative_topology_hash_bytes(hash, &display->managed_ordinal, sizeof(display->managed_ordinal));
    }
    for (size_t i = 0; i < topology->space_count; ++i) {
        const struct declarative_topology_space *space = &topology->spaces[i];
        hash = declarative_topology_hash_bytes(hash, &space->sid, sizeof(space->sid));
        hash = declarative_topology_hash_bytes(hash, space->uuid, strlen(space->uuid) + 1);
        hash = declarative_topology_hash_bytes(hash, &space->identity.fingerprint,
                                               sizeof(space->identity.fingerprint));
        hash = declarative_topology_hash_bytes(hash, &space->kind, sizeof(space->kind));
        hash = declarative_topology_hash_bytes(hash, &space->mission_control_ordinal, sizeof(space->mission_control_ordinal));
        hash = declarative_topology_hash_bytes(hash, &space->parent_display_index, sizeof(space->parent_display_index));
    }
    return hash;
}

void declarative_topology_destroy(struct declarative_topology *topology)
{
    if (!topology) return;
    if (topology->displays) {
        for (size_t i = 0; i < topology->display_count; ++i) declarative_topology_free(topology->displays[i].uuid);
    }
    if (topology->spaces) {
        for (size_t i = 0; i < topology->space_count; ++i) declarative_topology_free(topology->spaces[i].uuid);
    }
    declarative_topology_free(topology->displays);
    declarative_topology_free(topology->spaces);
    memset(topology, 0, sizeof(*topology));
}

static bool declarative_topology_valid_bounds(CGRect bounds)
{
    return isfinite(bounds.origin.x) && isfinite(bounds.origin.y) &&
           isfinite(bounds.size.width) && isfinite(bounds.size.height) &&
           fabs(bounds.origin.x) <= FLT_MAX && fabs(bounds.origin.y) <= FLT_MAX &&
           bounds.size.width > 0.0 && bounds.size.width <= FLT_MAX &&
           bounds.size.height > 0.0 && bounds.size.height <= FLT_MAX;
}

static CFDictionaryRef declarative_topology_find_display(CFArrayRef topology, CFStringRef uuid)
{
    CFDictionaryRef match = NULL;
    for (CFIndex i = 0; i < CFArrayGetCount(topology); ++i) {
        CFTypeRef value = CFArrayGetValueAtIndex(topology, i);
        if (!cf_is_type(value, CFDictionaryGetTypeID())) return NULL;
        CFTypeRef identifier = CFDictionaryGetValue((CFDictionaryRef) value, CFSTR("Display Identifier"));
        CFTypeRef spaces = CFDictionaryGetValue((CFDictionaryRef) value, CFSTR("Spaces"));
        if (!cf_is_type(identifier, CFStringGetTypeID()) || !cf_is_type(spaces, CFArrayGetTypeID()) ||
            CFArrayGetCount((CFArrayRef) spaces) <= 0) return NULL;
        if (CFEqual(identifier, uuid)) {
            if (match) return NULL;
            match = (CFDictionaryRef) value;
        }
    }
    return match;
}

static bool declarative_topology_parse_observation(const struct declarative_topology_capture_ops *ops,
                                                   struct declarative_topology *out)
{
    uint32_t *active_displays = NULL;
    size_t active_display_count = 0;
    bool active_ok = ops->copy_active_displays(&active_displays, &active_display_count, ops->context);
    bool active_pair_ok = ops->active_pair(&out->active_did, &out->active_sid, ops->context);
    CFTypeRef managed_value = ops->copy_managed_displays(ops->context);
    CFTypeRef topology_value = ops->copy_managed_display_spaces(ops->context);
    bool result = false;
    if (!active_ok || !active_pair_ok || !out->active_did || !out->active_sid ||
        !active_displays || !active_display_count ||
        !cf_is_type(managed_value, CFArrayGetTypeID()) || !cf_is_type(topology_value, CFArrayGetTypeID())) goto out;

    CFArrayRef managed = managed_value;
    CFArrayRef topology = topology_value;
    CFIndex display_count_cf = CFArrayGetCount(managed);
    if (display_count_cf <= 0 || display_count_cf > DECLARATIVE_CONFIG_MAX_SPACES ||
        CFArrayGetCount(topology) != display_count_cf || active_display_count != (size_t) display_count_cf) goto out;
    out->display_count = (size_t) display_count_cf;
    out->displays = declarative_topology_calloc(out->display_count, sizeof(*out->displays));
    if (!out->displays) goto out;

    size_t total_spaces = 0;
    for (CFIndex i = 0; i < display_count_cf; ++i) {
        CFTypeRef managed_uuid = CFArrayGetValueAtIndex(managed, i);
        if (!cf_is_type(managed_uuid, CFStringGetTypeID())) goto out;
        CFDictionaryRef display_dict = declarative_topology_find_display(topology, managed_uuid);
        if (!display_dict) goto out;
        CFArrayRef spaces_value = CFDictionaryGetValue(display_dict, CFSTR("Spaces"));
        if ((uint64_t) CFArrayGetCount(spaces_value) > DECLARATIVE_CONFIG_MAX_SPACES - total_spaces) goto out;
        total_spaces += (size_t) CFArrayGetCount(spaces_value);

        struct declarative_topology_display *display = &out->displays[i];
        display->uuid = declarative_topology_copy_cfstring(managed_uuid);
        display->did = ops->display_id(managed_uuid, ops->context);
        display->bounds = ops->display_bounds(display->did, ops->context);
        display->managed_ordinal = (int) i;
        if (!display->uuid || !display->did || !declarative_topology_valid_bounds(display->bounds)) goto out;
        for (CFIndex j = 0; j < i; ++j) {
            if (out->displays[j].did == display->did || strcmp(out->displays[j].uuid, display->uuid) == 0) goto out;
        }
    }
    for (size_t i = 0; i < active_display_count; ++i) {
        if (!active_displays[i]) goto out;
        bool found = false;
        for (size_t j = 0; j < i; ++j) if (active_displays[j] == active_displays[i]) goto out;
        for (size_t j = 0; j < out->display_count; ++j) {
            if (out->displays[j].did == active_displays[i]) {
                found = true;
                break;
            }
        }
        if (!found) goto out;
    }
    if (!total_spaces) goto out;
    out->space_count = total_spaces;
    out->spaces = declarative_topology_calloc(total_spaces, sizeof(*out->spaces));
    if (!out->spaces) goto out;

    size_t ordinal = 0;
    for (CFIndex i = 0; i < display_count_cf; ++i) {
        CFStringRef managed_uuid = CFArrayGetValueAtIndex(managed, i);
        CFDictionaryRef display_dict = declarative_topology_find_display(topology, managed_uuid);
        if (!display_dict) goto out;
        CFArrayRef spaces = CFDictionaryGetValue(display_dict, CFSTR("Spaces"));
        for (CFIndex j = 0; j < CFArrayGetCount(spaces); ++j) {
            CFTypeRef space_value = CFArrayGetValueAtIndex(spaces, j);
            uint64_t sid = 0;
            if (!managed_space_id(space_value, &sid) || !sid) goto out;
            CFStringRef uuid = ops->copy_space_uuid(sid, ops->context);
            int kind = -1;
            bool kind_ok = ops->space_kind(sid, &kind, ops->context);
            struct declarative_topology_space *space = &out->spaces[ordinal];
            space->sid = sid;
            space->uuid = declarative_topology_copy_cfstring_allow_empty(uuid, true);
            if (uuid) CFRelease(uuid);
            space->kind = kind;
            space->mission_control_ordinal = (int) ordinal;
            space->parent_display_index = (size_t) i;
            if (!kind_ok || kind < 0 || !space->uuid ||
                !declarative_topology_identity_init(&space->identity, space->uuid, &out->displays[i],
                                                    sid, kind, (int) ordinal)) goto out;
            for (size_t k = 0; k < ordinal; ++k) {
                bool duplicate_uuid = space->uuid[0] && out->spaces[k].uuid[0] &&
                                      strcmp(out->spaces[k].uuid, space->uuid) == 0;
                bool same_identity = declarative_space_identity_equal(&out->spaces[k].identity, &space->identity);
                bool collision = out->spaces[k].identity.fingerprint == space->identity.fingerprint && !same_identity;
                if (out->spaces[k].sid == sid || duplicate_uuid || same_identity || collision) goto out;
            }
            ++ordinal;
        }
    }
    bool active_display_found = false;
    size_t active_display_index = 0;
    for (size_t i = 0; i < out->display_count; ++i) {
        if (out->displays[i].did == out->active_did) {
            active_display_found = true;
            active_display_index = i;
            break;
        }
    }
    bool active_space_found = false;
    for (size_t i = 0; i < out->space_count; ++i) {
        if (out->spaces[i].sid == out->active_sid) {
            active_space_found = out->spaces[i].parent_display_index == active_display_index;
            break;
        }
    }
    if (!active_display_found || !active_space_found) goto out;
    out->fingerprint = declarative_topology_fingerprint(out);
    result = true;
out:
    if (active_displays) ops->free_active_displays(active_displays, ops->context);
    if (managed_value) CFRelease(managed_value);
    if (topology_value) CFRelease(topology_value);
    if (!result) declarative_topology_destroy(out);
    return result;
}

static bool declarative_topology_validate_owned(const struct declarative_topology *topology)
{
    if (!topology || !topology->displays || !topology->spaces || !topology->display_count || !topology->space_count ||
        !topology->active_did || !topology->active_sid ||
        topology->display_count > DECLARATIVE_CONFIG_MAX_SPACES || topology->space_count > DECLARATIVE_CONFIG_MAX_SPACES) return false;
    bool has_space[topology->display_count];
    memset(has_space, 0, sizeof(has_space));
    for (size_t i = 0; i < topology->display_count; ++i) {
        const struct declarative_topology_display *display = &topology->displays[i];
        if (!display->did || !display->uuid || !display->uuid[0] || display->managed_ordinal != (int) i ||
            !declarative_topology_valid_bounds(display->bounds)) return false;
        for (size_t j = 0; j < i; ++j) {
            if (topology->displays[j].did == display->did || strcmp(topology->displays[j].uuid, display->uuid) == 0) return false;
        }
    }
    for (size_t i = 0; i < topology->space_count; ++i) {
        const struct declarative_topology_space *space = &topology->spaces[i];
        if (!space->sid || !space->uuid || space->kind < 0 ||
            space->mission_control_ordinal != (int) i || space->parent_display_index >= topology->display_count ||
            !declarative_space_identity_valid(&space->identity)) return false;
        const struct declarative_topology_display *parent = &topology->displays[space->parent_display_index];
        if (space->identity.has_uuid != (space->uuid[0] != '\0') ||
            strcmp(space->identity.uuid, space->uuid) != 0 || space->identity.sid != space->sid ||
            space->identity.kind != space->kind || space->identity.mission_control_ordinal != space->mission_control_ordinal ||
            space->identity.parent_did != parent->did ||
            space->identity.parent_display_ordinal != parent->managed_ordinal ||
            strcmp(space->identity.parent_display_uuid, parent->uuid) != 0) return false;
        has_space[space->parent_display_index] = true;
        for (size_t j = 0; j < i; ++j) {
            bool duplicate_uuid = space->uuid[0] && topology->spaces[j].uuid[0] &&
                                  strcmp(topology->spaces[j].uuid, space->uuid) == 0;
            bool same_identity = declarative_space_identity_equal(&topology->spaces[j].identity, &space->identity);
            bool collision = topology->spaces[j].identity.fingerprint == space->identity.fingerprint && !same_identity;
            if (topology->spaces[j].sid == space->sid || duplicate_uuid || same_identity || collision) return false;
        }
    }
    for (size_t i = 0; i < topology->display_count; ++i) if (!has_space[i]) return false;
    size_t active_display_index = SIZE_MAX;
    for (size_t i = 0; i < topology->display_count; ++i) {
        if (topology->displays[i].did == topology->active_did) active_display_index = i;
    }
    bool active_space_matches = false;
    for (size_t i = 0; i < topology->space_count; ++i) {
        if (topology->spaces[i].sid == topology->active_sid) {
            active_space_matches = topology->spaces[i].parent_display_index == active_display_index;
        }
    }
    return active_space_matches && topology->fingerprint == declarative_topology_fingerprint(topology);
}

bool declarative_topology_equal(const struct declarative_topology *a,
                                const struct declarative_topology *b)
{
    if (!declarative_topology_validate_owned(a) || !declarative_topology_validate_owned(b) ||
        a->fingerprint != b->fingerprint || a->display_count != b->display_count ||
        a->space_count != b->space_count || a->active_did != b->active_did ||
        a->active_sid != b->active_sid) return false;
    for (size_t i = 0; i < a->display_count; ++i) {
        const struct declarative_topology_display *x = &a->displays[i], *y = &b->displays[i];
        if (x->did != y->did || x->managed_ordinal != y->managed_ordinal || strcmp(x->uuid, y->uuid) != 0 ||
            memcmp(&x->bounds, &y->bounds, sizeof(x->bounds)) != 0) return false;
    }
    for (size_t i = 0; i < a->space_count; ++i) {
        const struct declarative_topology_space *x = &a->spaces[i], *y = &b->spaces[i];
        if (x->sid != y->sid || x->kind != y->kind || x->mission_control_ordinal != y->mission_control_ordinal ||
            x->parent_display_index != y->parent_display_index || strcmp(x->uuid, y->uuid) != 0 ||
            !declarative_space_identity_equal(&x->identity, &y->identity)) return false;
    }
    return true;
}

bool declarative_topology_capture(const struct declarative_topology_capture_ops *ops,
                                  struct declarative_topology *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!ops || !ops->copy_active_displays || !ops->free_active_displays || !ops->active_pair ||
        !ops->copy_managed_displays || !ops->copy_managed_display_spaces || !ops->display_id ||
        !ops->display_bounds || !ops->copy_space_uuid || !ops->space_kind) return false;
    struct declarative_topology end = {0};
    if (!declarative_topology_parse_observation(ops, out) ||
        !declarative_topology_parse_observation(ops, &end) || !declarative_topology_equal(out, &end)) {
        declarative_topology_destroy(out);
        declarative_topology_destroy(&end);
        return false;
    }
    declarative_topology_destroy(&end);
    return true;
}

#ifdef TESTS
static CGError (*declarative_topology_active_display_list)(uint32_t, CGDirectDisplayID *, uint32_t *) = CGGetActiveDisplayList;
static uint32_t (*declarative_topology_active_display_id)(void) = display_manager_active_display_id;
static uint64_t (*declarative_topology_display_space_id)(uint32_t) = display_space_id;
#else
#define declarative_topology_active_display_list CGGetActiveDisplayList
#define declarative_topology_active_display_id display_manager_active_display_id
#define declarative_topology_display_space_id display_space_id
#endif

static bool declarative_topology_system_active_displays(uint32_t **display_ids, size_t *count, void *context)
{
    (void) context;
    if (!display_ids || !count) return false;
    *display_ids = NULL;
    *count = 0;
    uint32_t display_count = 0;
    if (declarative_topology_active_display_list(0, NULL, &display_count) != kCGErrorSuccess || !display_count ||
        display_count > DECLARATIVE_CONFIG_MAX_SPACES) return false;
    uint32_t *ids = declarative_topology_calloc(display_count, sizeof(*ids));
    if (!ids) return false;
    uint32_t observed_count = display_count;
    if (declarative_topology_active_display_list(display_count, ids, &observed_count) != kCGErrorSuccess ||
        observed_count != display_count) {
        declarative_topology_free(ids);
        return false;
    }
    *display_ids = ids;
    *count = observed_count;
    return true;
}

static void declarative_topology_system_free_active_displays(uint32_t *display_ids, void *context)
{
    (void) context;
    declarative_topology_free(display_ids);
}

static bool declarative_topology_system_active_pair(uint32_t *did, uint64_t *sid, void *context)
{
    (void) context;
    if (!did || !sid) return false;
    *did = declarative_topology_active_display_id();
    *sid = *did ? declarative_topology_display_space_id(*did) : 0;
    return *did != 0 && *sid != 0;
}

static CFTypeRef declarative_topology_system_displays(void *context) { (void) context; return SLSCopyManagedDisplays(g_connection); }
static CFTypeRef declarative_topology_system_spaces(void *context) { (void) context; return SLSCopyManagedDisplaySpaces(g_connection); }
static uint32_t declarative_topology_system_display_id(CFStringRef uuid, void *context) { (void) context; return display_id(uuid); }
static CGRect declarative_topology_system_bounds(uint32_t did, void *context)
{
    const struct declarative_topology_bounds_config *config = context;
    return display_bounds_constrained_with_external_bar(did, config->external_bar_mode,
                                                        config->external_bar_top_padding,
                                                        config->external_bar_bottom_padding);
}
static CFStringRef declarative_topology_system_space_uuid(uint64_t sid, void *context) { (void) context; return SLSSpaceCopyName(g_connection, sid); }
static bool declarative_topology_system_space_kind(uint64_t sid, int *kind, void *context)
{
    (void) context;
    if (!kind) return false;
    int value = SLSSpaceGetType(g_connection, sid);
    if (value < 0) return false;
    *kind = value;
    return true;
}

bool declarative_topology_capture_system(const struct declarative_topology_bounds_config *bounds_config,
                                         struct declarative_topology *out)
{
    if (!bounds_config || bounds_config->external_bar_mode < EXTERNAL_BAR_OFF ||
        bounds_config->external_bar_mode > EXTERNAL_BAR_ALL) return false;
    struct declarative_topology_capture_ops ops = {
        .copy_active_displays = declarative_topology_system_active_displays,
        .free_active_displays = declarative_topology_system_free_active_displays,
        .active_pair = declarative_topology_system_active_pair,
        .copy_managed_displays = declarative_topology_system_displays,
        .copy_managed_display_spaces = declarative_topology_system_spaces,
        .display_id = declarative_topology_system_display_id,
        .display_bounds = declarative_topology_system_bounds,
        .copy_space_uuid = declarative_topology_system_space_uuid,
        .space_kind = declarative_topology_system_space_kind,
        .context = (void *) bounds_config
    };
    return declarative_topology_capture(&ops, out);
}

bool declarative_topology_rule_snapshot(const struct declarative_topology *topology,
                                        struct declarative_rule_topology_snapshot *snapshot,
                                        struct declarative_rule_display_entry **owned_displays,
                                        struct declarative_rule_space_entry **owned_spaces)
{
    if (!snapshot || !owned_displays || !owned_spaces) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    *owned_displays = NULL;
    *owned_spaces = NULL;
    if (!declarative_topology_validate_owned(topology)) return false;
    *owned_displays = declarative_topology_calloc(topology->display_count, sizeof(**owned_displays));
    *owned_spaces = declarative_topology_calloc(topology->space_count, sizeof(**owned_spaces));
    if (!*owned_displays || !*owned_spaces) goto fail;
    for (size_t i = 0; i < topology->display_count; ++i) {
        const struct declarative_topology_display *source = &topology->displays[i];
        (*owned_displays)[i] = (struct declarative_rule_display_entry) {
            .did = source->did, .uuid = source->uuid,
            .center = CGPointMake(CGRectGetMidX(source->bounds), CGRectGetMidY(source->bounds)),
            .managed_ordinal = source->managed_ordinal
        };
    }
    for (size_t i = 0; i < topology->space_count; ++i) {
        const struct declarative_topology_space *source = &topology->spaces[i];
        const struct declarative_topology_display *parent = &topology->displays[source->parent_display_index];
        (*owned_spaces)[i] = (struct declarative_rule_space_entry) {
            .sid = source->sid, .uuid = source->uuid, .kind = source->kind,
            .mission_control_ordinal = source->mission_control_ordinal,
            .parent_did = parent->did, .parent_display_uuid = parent->uuid,
            .parent_display_ordinal = parent->managed_ordinal, .identity = source->identity
        };
    }
    *snapshot = (struct declarative_rule_topology_snapshot) {
        .displays = *owned_displays, .display_count = topology->display_count,
        .spaces = *owned_spaces, .space_count = topology->space_count
    };
    if (!declarative_rule_topology_validate(snapshot)) goto fail;
    return true;
fail:
    memset(snapshot, 0, sizeof(*snapshot));
    declarative_topology_free(*owned_displays);
    declarative_topology_free(*owned_spaces);
    *owned_displays = NULL;
    *owned_spaces = NULL;
    return false;
}

void declarative_topology_rule_snapshot_destroy(struct declarative_rule_display_entry *displays,
                                                struct declarative_rule_space_entry *spaces)
{
    declarative_topology_free(displays);
    declarative_topology_free(spaces);
}

void declarative_topology_space_snapshot_destroy(struct declarative_space_topology_entry *entries)
{
    declarative_topology_free(entries);
}

bool declarative_topology_space_snapshot(const struct declarative_topology *topology,
                                         struct declarative_space_topology_entry **entries,
                                         size_t *count)
{
    if (!entries || !count) return false;
    *entries = NULL;
    *count = 0;
    if (!declarative_topology_validate_owned(topology)) return false;
    *entries = declarative_topology_calloc(topology->space_count, sizeof(**entries));
    if (!*entries) return false;
    for (size_t i = 0; i < topology->space_count; ++i) {
        (*entries)[i] = (struct declarative_space_topology_entry) {
            .sid = topology->spaces[i].sid, .index = topology->spaces[i].mission_control_ordinal + 1,
            .uuid = topology->spaces[i].uuid, .is_user_space = topology->spaces[i].kind == 0,
            .identity = topology->spaces[i].identity
        };
    }
    *count = topology->space_count;
    return true;
}

static TABLE_HASH_FUNC(declarative_detached_view_hash)
{
    return *(uint64_t *) key;
}

static TABLE_COMPARE_FUNC(declarative_detached_view_compare)
{
    return *(uint64_t *) key_a == *(uint64_t *) key_b;
}

static bool declarative_detached_view_table_insert(struct table *table, uint64_t sid, struct view *view)
{
    size_t index = declarative_detached_view_hash(&sid) % (size_t) table->capacity;
    struct bucket **cursor = &table->buckets[index];
    while (*cursor) {
        if (declarative_detached_view_compare((*cursor)->key, &sid)) return false;
        cursor = &(*cursor)->next;
    }
    struct bucket *bucket = declarative_topology_calloc(1, sizeof(*bucket));
    uint64_t *key = declarative_topology_calloc(1, sizeof(*key));
    if (!bucket || !key) {
        declarative_topology_free(bucket);
        declarative_topology_free(key);
        return false;
    }
    *key = sid;
    bucket->key = key;
    bucket->value = view;
    *cursor = bucket;
    ++table->count;
    return true;
}

static void declarative_detached_view_hash_destroy(struct table *table)
{
    if (!table || !table->buckets) return;
    for (int i = 0; i < table->capacity; ++i) {
        struct bucket *bucket = table->buckets[i];
        while (bucket) {
            struct bucket *next = bucket->next;
            declarative_topology_free(bucket->key);
            declarative_topology_free(bucket);
            bucket = next;
        }
    }
    declarative_topology_free(table->buckets);
    memset(table, 0, sizeof(*table));
}

static struct view *declarative_detached_view_create(const struct declarative_topology_space *space,
                                                     const struct declarative_topology_display *display,
                                                     const struct declarative_space_defaults *defaults)
{
    if (!space || !space->sid || !space->uuid || !display || !display->did || !display->uuid ||
        !declarative_topology_valid_bounds(display->bounds)) return NULL;
    struct view *view = declarative_topology_calloc(1, sizeof(*view));
    struct window_node *root = declarative_topology_calloc(1, sizeof(*root));
    if (!view || !root) {
        declarative_topology_free(view);
        declarative_topology_free(root);
        return NULL;
    }
    view->uuid = CFStringCreateWithCString(NULL, space->uuid, kCFStringEncodingUTF8);
    if (!view->uuid) {
        declarative_topology_free(root);
        declarative_topology_free(view);
        return NULL;
    }
    view->sid = space->sid;
    view->declarative_identity = space->identity;
    view->root = root;
    view_set_flag(view, VIEW_ENABLE_PADDING);
    view_set_flag(view, VIEW_ENABLE_GAP);
    if (space->kind == 0) {
        view->layout = defaults->layout;
        view->top_padding = defaults->top_padding;
        view->bottom_padding = defaults->bottom_padding;
        view->left_padding = defaults->left_padding;
        view->right_padding = defaults->right_padding;
        view->window_gap = defaults->window_gap;
        view->auto_balance = defaults->auto_balance;
        view->split_type = defaults->split_type;
        CGRect bounds = display->bounds;
        root->area = (struct area) {
            .x = (float) bounds.origin.x,
            .y = (float) bounds.origin.y,
            .w = (float) bounds.size.width,
            .h = (float) bounds.size.height
        };
        root->area.x += (float) view->left_padding;
        root->area.w -= (float) view->left_padding;
        root->area.w -= (float) view->right_padding;
        root->area.y += (float) view->top_padding;
        root->area.h -= (float) view->top_padding;
        root->area.h -= (float) view->bottom_padding;
        if (!isfinite(root->area.x) || !isfinite(root->area.y) ||
            !isfinite(root->area.w) || !isfinite(root->area.h)) {
            CFRelease(view->uuid);
            declarative_topology_free(root);
            declarative_topology_free(view);
            return NULL;
        }
        view_set_flag(view, VIEW_IS_VALID);
        view_set_flag(view, VIEW_IS_DIRTY);
    } else {
        view->layout = VIEW_FLOAT;
    }
    return view;
}

void declarative_detached_view_table_adopt(struct declarative_detached_view_table *table,
                                           struct table *live_table)
{
    if (!table || !live_table) return;
    *live_table = table->view_table;
    for (int i = 0; i < live_table->capacity; ++i) {
        for (struct bucket *bucket = live_table->buckets[i]; bucket; bucket = bucket->next) {
            declarative_topology_disown(bucket->key);
            declarative_topology_disown(bucket);
        }
    }
    declarative_topology_disown(live_table->buckets);
    for (size_t i = 0; i < table->count; ++i) {
        declarative_topology_disown(table->views[i] ? table->views[i]->root : NULL);
        declarative_topology_disown(table->views[i]);
    }
    declarative_topology_free(table->views);
    declarative_topology_free(table->sids);
    memset(table, 0, sizeof(*table));
}

void declarative_detached_view_table_destroy(struct declarative_detached_view_table *table)
{
    if (!table) return;
    for (size_t i = 0; i < table->count; ++i) {
        struct view *view = table->views[i];
        if (!view) continue;
        if (view->uuid) CFRelease(view->uuid);
        declarative_topology_free(view->root);
        declarative_topology_free(view);
    }
    declarative_detached_view_hash_destroy(&table->view_table);
    declarative_topology_free(table->views);
    declarative_topology_free(table->sids);
    memset(table, 0, sizeof(*table));
}

bool declarative_detached_view_table_prepare(const struct declarative_topology *topology,
                                             const struct declarative_space_defaults *defaults,
                                             struct declarative_detached_view_table *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!declarative_topology_validate_owned(topology) || !defaults ||
        defaults->layout < VIEW_BSP || defaults->layout > VIEW_FLOAT ||
        defaults->split_type < SPLIT_Y || defaults->split_type > SPLIT_AUTO) return false;
    if (topology->space_count > (size_t) (INT_MAX - 1) / 2) return false;
    out->view_table = (struct table) {
        .capacity = (int) (topology->space_count * 2 + 1),
        .max_load = 0.75f,
        .hash = declarative_detached_view_hash,
        .cmp = declarative_detached_view_compare
    };
    out->view_table.buckets = declarative_topology_calloc((size_t) out->view_table.capacity,
                                                          sizeof(*out->view_table.buckets));
    out->views = declarative_topology_calloc(topology->space_count, sizeof(*out->views));
    out->sids = declarative_topology_calloc(topology->space_count, sizeof(*out->sids));
    if (!out->view_table.buckets || !out->views || !out->sids) goto fail;
    out->count = topology->space_count;
    for (size_t i = 0; i < topology->space_count; ++i) {
        const struct declarative_topology_space *space = &topology->spaces[i];
        const struct declarative_topology_display *display = &topology->displays[space->parent_display_index];
        out->sids[i] = space->sid;
        out->views[i] = declarative_detached_view_create(space, display, defaults);
        if (!out->views[i] || !declarative_detached_view_table_insert(&out->view_table, out->sids[i], out->views[i])) goto fail;
    }
    return true;
fail:
    declarative_detached_view_table_destroy(out);
    return false;
}
