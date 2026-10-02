struct declarative_topology_fixture
{
    CFArrayRef displays[2];
    CFArrayRef topology[2];
    uint32_t active_displays[2][3];
    size_t active_display_count[2];
    uint32_t active_did[2];
    uint64_t active_sid[2];
    int observation;
    bool duplicate_did;
    bool zero_did;
    bool invalid_bounds;
    uint64_t missing_uuid_sid;
    uint64_t missing_kind_sid;
    uint64_t replacement_uuid_sid;
    const char *replacement_uuid;
    uint64_t replacement_kind_sid;
    int replacement_kind;
    int fail_copy_query;
    int query_count;
};

static CFNumberRef declarative_topology_test_number(uint64_t value)
{
    return CFNumberCreate(NULL, kCFNumberSInt64Type, &value);
}

static CFDictionaryRef declarative_topology_test_space(uint64_t sid)
{
    CFNumberRef number = declarative_topology_test_number(sid);
    const void *keys[] = { CFSTR("id64") };
    const void *values[] = { number };
    CFDictionaryRef result = CFDictionaryCreate(NULL, keys, values, 1,
                                                &kCFTypeDictionaryKeyCallBacks,
                                                &kCFTypeDictionaryValueCallBacks);
    CFRelease(number);
    return result;
}

static CFDictionaryRef declarative_topology_test_display(CFStringRef uuid, const uint64_t *sids, size_t count)
{
    CFMutableArrayRef spaces = CFArrayCreateMutable(NULL, (CFIndex) count, &kCFTypeArrayCallBacks);
    for (size_t i = 0; i < count; ++i) {
        CFDictionaryRef space = declarative_topology_test_space(sids[i]);
        CFArrayAppendValue(spaces, space);
        CFRelease(space);
    }
    const void *keys[] = { CFSTR("Display Identifier"), CFSTR("Spaces") };
    const void *values[] = { uuid, spaces };
    CFDictionaryRef result = CFDictionaryCreate(NULL, keys, values, 2,
                                                &kCFTypeDictionaryKeyCallBacks,
                                                &kCFTypeDictionaryValueCallBacks);
    CFRelease(spaces);
    return result;
}

static CFArrayRef declarative_topology_test_displays(CFStringRef a, CFStringRef b)
{
    const void *values[] = { a, b };
    return CFArrayCreate(NULL, values, 2, &kCFTypeArrayCallBacks);
}

static CFArrayRef declarative_topology_test_topology(CFStringRef a, const uint64_t *a_sids, size_t a_count,
                                                     CFStringRef b, const uint64_t *b_sids, size_t b_count)
{
    CFDictionaryRef first = declarative_topology_test_display(a, a_sids, a_count);
    CFDictionaryRef second = declarative_topology_test_display(b, b_sids, b_count);
    const void *values[] = { first, second };
    CFArrayRef result = CFArrayCreate(NULL, values, 2, &kCFTypeArrayCallBacks);
    CFRelease(first);
    CFRelease(second);
    return result;
}

static void declarative_topology_test_fixture_init(struct declarative_topology_fixture *fixture, bool changed)
{
    memset(fixture, 0, sizeof(*fixture));
    uint64_t first[] = { 11, 12 };
    uint64_t second[] = { 21 };
    uint64_t changed_second[] = { 22 };
    fixture->active_displays[0][0] = 1;
    fixture->active_displays[0][1] = 2;
    fixture->active_display_count[0] = 2;
    memcpy(fixture->active_displays[1], fixture->active_displays[0], sizeof(fixture->active_displays[0]));
    fixture->active_display_count[1] = fixture->active_display_count[0];
    fixture->active_did[0] = fixture->active_did[1] = 1;
    fixture->active_sid[0] = fixture->active_sid[1] = 11;
    fixture->displays[0] = declarative_topology_test_displays(CFSTR("display-a"), CFSTR("display-b"));
    fixture->topology[0] = declarative_topology_test_topology(CFSTR("display-a"), first, 2,
                                                             CFSTR("display-b"), second, 1);
    fixture->displays[1] = CFRetain(fixture->displays[0]);
    fixture->topology[1] = changed
        ? declarative_topology_test_topology(CFSTR("display-a"), first, 2,
                                             CFSTR("display-b"), changed_second, 1)
        : CFRetain(fixture->topology[0]);
}

static void declarative_topology_test_fixture_destroy(struct declarative_topology_fixture *fixture)
{
    for (int i = 0; i < 2; ++i) {
        if (fixture->displays[i]) CFRelease(fixture->displays[i]);
        if (fixture->topology[i]) CFRelease(fixture->topology[i]);
    }
}

static bool declarative_topology_test_copy_active_displays(uint32_t **display_ids, size_t *count, void *context)
{
    struct declarative_topology_fixture *fixture = context;
    int index = fixture->observation > 1 ? 1 : fixture->observation;
    if (++fixture->query_count == fixture->fail_copy_query) return false;
    size_t active_count = fixture->active_display_count[index];
    uint32_t *ids = declarative_topology_calloc(active_count, sizeof(*ids));
    if (!ids) return false;
    memcpy(ids, fixture->active_displays[index], active_count * sizeof(*ids));
    *display_ids = ids;
    *count = active_count;
    return true;
}

static void declarative_topology_test_free_active_displays(uint32_t *display_ids, void *context)
{
    (void) context;
    declarative_topology_free(display_ids);
}

static bool declarative_topology_test_active_pair(uint32_t *did, uint64_t *sid, void *context)
{
    struct declarative_topology_fixture *fixture = context;
    int index = fixture->observation > 1 ? 1 : fixture->observation;
    if (++fixture->query_count == fixture->fail_copy_query) return false;
    *did = fixture->active_did[index];
    *sid = fixture->active_sid[index];
    return true;
}

static CFTypeRef declarative_topology_test_copy_displays(void *context)
{
    struct declarative_topology_fixture *fixture = context;
    int index = fixture->observation > 1 ? 1 : fixture->observation;
    if (++fixture->query_count == fixture->fail_copy_query) return NULL;
    return CFRetain(fixture->displays[index]);
}

static CFTypeRef declarative_topology_test_copy_spaces(void *context)
{
    struct declarative_topology_fixture *fixture = context;
    int index = fixture->observation > 1 ? 1 : fixture->observation;
    ++fixture->observation;
    if (++fixture->query_count == fixture->fail_copy_query) return NULL;
    return CFRetain(fixture->topology[index]);
}

static uint32_t declarative_topology_test_did(CFStringRef uuid, void *context)
{
    struct declarative_topology_fixture *fixture = context;
    if (fixture->zero_did) return 0;
    if (fixture->duplicate_did) return 1;
    return CFEqual(uuid, CFSTR("display-a")) ? 1 : 2;
}

static CGRect declarative_topology_test_bounds(uint32_t did, void *context)
{
    struct declarative_topology_fixture *fixture = context;
    if (fixture->invalid_bounds) return CGRectNull;
    return did == 1 ? CGRectMake(0, 0, 1000, 800) : CGRectMake(1000, 0, 1200, 900);
}

static CFStringRef declarative_topology_test_space_uuid(uint64_t sid, void *context)
{
    struct declarative_topology_fixture *fixture = context;
    if (fixture->missing_uuid_sid == sid) return NULL;
    if (fixture->replacement_uuid_sid == sid && fixture->replacement_uuid) {
        return CFStringCreateWithCString(NULL, fixture->replacement_uuid, kCFStringEncodingUTF8);
    }
    return CFStringCreateWithFormat(NULL, NULL, CFSTR("space-%llu"), sid);
}

static bool declarative_topology_test_space_kind(uint64_t sid, int *kind, void *context)
{
    struct declarative_topology_fixture *fixture = context;
    if (fixture->missing_kind_sid == sid) return false;
    if (fixture->replacement_kind_sid == sid) {
        *kind = fixture->replacement_kind;
        return true;
    }
    *kind = sid == 21 || sid == 22 ? 4 : 0;
    return true;
}

static struct declarative_topology_capture_ops declarative_topology_test_ops(struct declarative_topology_fixture *fixture)
{
    return (struct declarative_topology_capture_ops) {
        .copy_active_displays = declarative_topology_test_copy_active_displays,
        .free_active_displays = declarative_topology_test_free_active_displays,
        .active_pair = declarative_topology_test_active_pair,
        .copy_managed_displays = declarative_topology_test_copy_displays,
        .copy_managed_display_spaces = declarative_topology_test_copy_spaces,
        .display_id = declarative_topology_test_did,
        .display_bounds = declarative_topology_test_bounds,
        .copy_space_uuid = declarative_topology_test_space_uuid,
        .space_kind = declarative_topology_test_space_kind,
        .context = fixture
    };
}

TEST_FUNC(declarative_topology_capture_is_owned_strict_and_coherent,
{
    struct declarative_topology_fixture fixture;
    declarative_topology_test_fixture_init(&fixture, false);
    struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
    struct declarative_topology topology = {0};
    TEST_CHECK(declarative_topology_capture(&ops, &topology), true);
    TEST_CHECK(fixture.query_count, 8);
    TEST_CHECK(topology.active_did, 1U);
    TEST_CHECK(topology.active_sid, 11ULL);
    TEST_CHECK(topology.display_count, 2U);
    TEST_CHECK(topology.space_count, 3U);
    TEST_CHECK(topology.displays[1].did, 2U);
    TEST_CHECK(strcmp(topology.displays[1].uuid, "display-b"), 0);
    TEST_CHECK(topology.spaces[2].sid, 21ULL);
    TEST_CHECK(topology.spaces[2].kind, 4);
    TEST_CHECK(topology.spaces[2].parent_display_index, 1U);

    CFRelease(fixture.displays[0]); fixture.displays[0] = NULL;
    CFRelease(fixture.topology[0]); fixture.topology[0] = NULL;
    TEST_CHECK(strcmp(topology.spaces[0].uuid, "space-11"), 0);

    struct declarative_rule_topology_snapshot rules = {0};
    struct declarative_rule_display_entry *displays = NULL;
    struct declarative_rule_space_entry *spaces = NULL;
    TEST_CHECK(declarative_topology_rule_snapshot(&topology, &rules, &displays, &spaces), true);
    TEST_CHECK(rules.spaces[2].parent_did, 2U);
    struct declarative_space_topology_entry *space_entries = NULL;
    size_t space_count = 0;
    TEST_CHECK(declarative_topology_space_snapshot(&topology, &space_entries, &space_count), true);
    TEST_CHECK(space_entries[1].index, 2);
    TEST_CHECK(space_entries[2].is_user_space, false);
    declarative_topology_rule_snapshot_destroy(displays, spaces);
    declarative_topology_space_snapshot_destroy(space_entries);
    declarative_topology_destroy(&topology);
    declarative_topology_test_fixture_destroy(&fixture);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_topology_rejects_malformed_incomplete_zero_and_ambiguous_observations,
{
    struct declarative_topology topology = {0};

    struct declarative_topology_fixture zero;
    declarative_topology_test_fixture_init(&zero, false);
    zero.zero_did = true;
    struct declarative_topology_capture_ops zero_ops = declarative_topology_test_ops(&zero);
    TEST_CHECK(declarative_topology_capture(&zero_ops, &topology), false);
    declarative_topology_test_fixture_destroy(&zero);

    struct declarative_topology_fixture reordered;
    declarative_topology_test_fixture_init(&reordered, false);
    CFRelease(reordered.displays[0]);
    CFRelease(reordered.displays[1]);
    reordered.displays[0] = declarative_topology_test_displays(CFSTR("display-b"), CFSTR("display-a"));
    reordered.displays[1] = CFRetain(reordered.displays[0]);
    struct declarative_topology_capture_ops reordered_ops = declarative_topology_test_ops(&reordered);
    TEST_CHECK(declarative_topology_capture(&reordered_ops, &topology), true);
    TEST_CHECK(topology.displays[0].did, 2U);
    TEST_CHECK(topology.spaces[0].sid, 21ULL);
    declarative_topology_destroy(&topology);
    declarative_topology_test_fixture_destroy(&reordered);

    struct declarative_topology_fixture ambiguous;
    declarative_topology_test_fixture_init(&ambiguous, false);
    uint64_t duplicate_sid[] = { 31 };
    CFDictionaryRef duplicate_a = declarative_topology_test_display(CFSTR("display-a"), duplicate_sid, 1);
    const void *ambiguous_values[] = { CFArrayGetValueAtIndex(ambiguous.topology[0], 0), duplicate_a };
    CFArrayRef ambiguous_topology = CFArrayCreate(NULL, ambiguous_values, 2, &kCFTypeArrayCallBacks);
    CFRelease(duplicate_a);
    CFRelease(ambiguous.topology[0]);
    CFRelease(ambiguous.topology[1]);
    ambiguous.topology[0] = ambiguous_topology;
    ambiguous.topology[1] = CFRetain(ambiguous_topology);
    struct declarative_topology_capture_ops ambiguous_ops = declarative_topology_test_ops(&ambiguous);
    TEST_CHECK(declarative_topology_capture(&ambiguous_ops, &topology), false);
    declarative_topology_test_fixture_destroy(&ambiguous);

    struct declarative_topology_fixture incomplete;
    declarative_topology_test_fixture_init(&incomplete, false);
    const void *keys[] = { CFSTR("Display Identifier") };
    const void *values[] = { CFSTR("display-a") };
    CFDictionaryRef bad_display = CFDictionaryCreate(NULL, keys, values, 1,
                                                      &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks);
    const void *bad_values[] = { bad_display, CFArrayGetValueAtIndex(incomplete.topology[0], 1) };
    CFArrayRef bad_topology = CFArrayCreate(NULL, bad_values, 2, &kCFTypeArrayCallBacks);
    CFRelease(bad_display);
    CFRelease(incomplete.topology[0]);
    CFRelease(incomplete.topology[1]);
    incomplete.topology[0] = bad_topology;
    incomplete.topology[1] = CFRetain(bad_topology);
    struct declarative_topology_capture_ops incomplete_ops = declarative_topology_test_ops(&incomplete);
    TEST_CHECK(declarative_topology_capture(&incomplete_ops, &topology), false);
    declarative_topology_test_fixture_destroy(&incomplete);

    TEST_CHECK(topology.displays == NULL, true);
    TEST_CHECK(topology.spaces == NULL, true);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_topology_active_pair_is_strict_parented_and_stable,
{
    struct declarative_topology topology = {0};
    for (int scenario = 0; scenario < 5; ++scenario) {
        struct declarative_topology_fixture fixture;
        declarative_topology_test_fixture_init(&fixture, false);
        switch (scenario) {
        case 0: fixture.active_did[0] = fixture.active_did[1] = 0; break;
        case 1: fixture.active_sid[0] = fixture.active_sid[1] = 0; break;
        case 2: fixture.active_did[0] = fixture.active_did[1] = 99; break;
        case 3: fixture.active_sid[0] = fixture.active_sid[1] = 99; break;
        case 4: fixture.active_did[0] = fixture.active_did[1] = 2; break;
        }
        struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
        TEST_CHECK(declarative_topology_capture(&ops, &topology), false);
        declarative_topology_test_fixture_destroy(&fixture);
    }

    struct declarative_topology_fixture changed_did;
    declarative_topology_test_fixture_init(&changed_did, false);
    changed_did.active_did[1] = 2;
    changed_did.active_sid[1] = 21;
    struct declarative_topology_capture_ops did_ops = declarative_topology_test_ops(&changed_did);
    TEST_CHECK(declarative_topology_capture(&did_ops, &topology), false);
    declarative_topology_test_fixture_destroy(&changed_did);

    struct declarative_topology_fixture changed_sid;
    declarative_topology_test_fixture_init(&changed_sid, false);
    changed_sid.active_sid[1] = 12;
    struct declarative_topology_capture_ops sid_ops = declarative_topology_test_ops(&changed_sid);
    TEST_CHECK(declarative_topology_capture(&sid_ops, &topology), false);
    declarative_topology_test_fixture_destroy(&changed_sid);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_topology_query_failures_leave_no_partial_capture,
{
    for (int query = 1; query <= 8; ++query) {
        struct declarative_topology_fixture fixture;
        declarative_topology_test_fixture_init(&fixture, false);
        fixture.fail_copy_query = query;
        struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
        struct declarative_topology topology = {0};
        TEST_CHECK(declarative_topology_capture(&ops, &topology), false);
        TEST_CHECK(topology.displays == NULL, true);
        TEST_CHECK(topology.spaces == NULL, true);
        declarative_topology_test_fixture_destroy(&fixture);
    }
    for (int failure = 0; failure < 3; ++failure) {
        struct declarative_topology_fixture fixture;
        declarative_topology_test_fixture_init(&fixture, false);
        fixture.invalid_bounds = failure == 0;
        fixture.missing_uuid_sid = failure == 1 ? 11 : 0;
        fixture.missing_kind_sid = failure == 2 ? 11 : 0;
        struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
        struct declarative_topology topology = {0};
        TEST_CHECK(declarative_topology_capture(&ops, &topology), false);
        TEST_CHECK(topology.displays == NULL, true);
        TEST_CHECK(topology.spaces == NULL, true);
        declarative_topology_test_fixture_destroy(&fixture);
    }
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_topology_rejects_duplicates_and_changed_second_observation,
{
    struct declarative_topology_fixture duplicate;
    declarative_topology_test_fixture_init(&duplicate, false);
    duplicate.duplicate_did = true;
    struct declarative_topology_capture_ops duplicate_ops = declarative_topology_test_ops(&duplicate);
    struct declarative_topology topology = {0};
    TEST_CHECK(declarative_topology_capture(&duplicate_ops, &topology), false);
    TEST_CHECK(topology.displays == NULL, true);
    declarative_topology_test_fixture_destroy(&duplicate);

    for (int failure = 0; failure < 5; ++failure) {
        struct declarative_topology_fixture active;
        declarative_topology_test_fixture_init(&active, false);
        if (failure == 0) {
            active.active_displays[0][2] = active.active_displays[1][2] = 3;
            active.active_display_count[0] = active.active_display_count[1] = 3;
        } else if (failure == 1) {
            active.active_display_count[0] = active.active_display_count[1] = 1;
        } else if (failure == 2) {
            active.active_displays[0][1] = active.active_displays[1][1] = 1;
        } else if (failure == 3) {
            active.active_displays[0][0] = active.active_displays[1][0] = 0;
        } else {
            active.active_displays[1][1] = 3;
        }
        struct declarative_topology_capture_ops active_ops = declarative_topology_test_ops(&active);
        TEST_CHECK(declarative_topology_capture(&active_ops, &topology), false);
        TEST_CHECK(topology.displays == NULL, true);
        TEST_CHECK(topology.spaces == NULL, true);
        declarative_topology_test_fixture_destroy(&active);
    }

    struct declarative_topology_fixture changed;
    declarative_topology_test_fixture_init(&changed, true);
    struct declarative_topology_capture_ops changed_ops = declarative_topology_test_ops(&changed);
    TEST_CHECK(declarative_topology_capture(&changed_ops, &topology), false);
    TEST_CHECK(changed.query_count, 8);
    TEST_CHECK(topology.spaces == NULL, true);
    declarative_topology_test_fixture_destroy(&changed);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
});

struct declarative_topology_active_display_list_fixture
{
    CGError errors[2];
    uint32_t counts[2];
    uint32_t ids[3];
    uint32_t capacities[2];
    int calls;
};

static struct declarative_topology_active_display_list_fixture *declarative_topology_active_display_list_fixture;

static CGError declarative_topology_test_active_display_list(uint32_t capacity, CGDirectDisplayID *ids, uint32_t *count)
{
    struct declarative_topology_active_display_list_fixture *fixture = declarative_topology_active_display_list_fixture;
    int call = fixture->calls++;
    fixture->capacities[call] = capacity;
    *count = fixture->counts[call];
    if (ids && fixture->errors[call] == kCGErrorSuccess) {
        uint32_t copied = fixture->counts[call] < capacity ? fixture->counts[call] : capacity;
        memcpy(ids, fixture->ids, copied * sizeof(*ids));
    }
    return fixture->errors[call];
}

TEST_FUNC(declarative_topology_system_active_display_count_fill_contract_is_strict,
{
    __auto_type saved_active_display_list = declarative_topology_active_display_list;
    declarative_topology_active_display_list = declarative_topology_test_active_display_list;

    for (int scenario = 0; scenario < 4; ++scenario) {
        declarative_topology_test_reset_allocator();
        struct declarative_topology_active_display_list_fixture fixture = {
            .errors = { kCGErrorSuccess, kCGErrorSuccess },
            .counts = { 2, 2 },
            .ids = { 11, 22, 33 }
        };
        if (scenario == 1) fixture.counts[1] = 3;
        if (scenario == 2) { fixture.counts[0] = 3; fixture.counts[1] = 2; }
        if (scenario == 3) fixture.errors[1] = kCGErrorFailure;
        declarative_topology_active_display_list_fixture = &fixture;

        uint32_t *ids = NULL;
        size_t count = 0;
        bool result = declarative_topology_system_active_displays(&ids, &count, NULL);
        TEST_CHECK(result, scenario == 0);
        TEST_CHECK(fixture.calls, 2);
        TEST_CHECK(fixture.capacities[0], 0U);
        TEST_CHECK(fixture.capacities[1], fixture.counts[0]);
        if (scenario == 0) {
            TEST_CHECK(count, 2U);
            TEST_CHECK(ids[0], 11U);
            TEST_CHECK(ids[1], 22U);
            declarative_topology_system_free_active_displays(ids, NULL);
        } else {
            TEST_CHECK(ids == NULL, true);
            TEST_CHECK(count, 0U);
        }
        TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
    }

    declarative_topology_test_fail_allocation_at(1);
    struct declarative_topology_active_display_list_fixture allocation = {
        .errors = { kCGErrorSuccess, kCGErrorSuccess },
        .counts = { 2, 2 },
        .ids = { 11, 22 }
    };
    declarative_topology_active_display_list_fixture = &allocation;
    uint32_t *ids = NULL;
    size_t count = 0;
    TEST_CHECK(declarative_topology_system_active_displays(&ids, &count, NULL), false);
    TEST_CHECK(allocation.calls, 1);
    TEST_CHECK(ids == NULL, true);
    TEST_CHECK(count, 0U);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);

    declarative_topology_test_reset_allocator();
    declarative_topology_active_display_list_fixture = NULL;
    declarative_topology_active_display_list = saved_active_display_list;
});

struct declarative_display_bounds_fixture
{
    CGRect bounds;
    uint32_t main_did;
    bool menu_hidden;
    int notch_height;
    CGRect menu;
    bool dock_hidden;
    uint32_t dock_did;
    int dock_orientation;
    CGRect dock;
};

static CGRect declarative_display_bounds_test_bounds(uint32_t did, void *context)
{
    (void) did;
    return ((struct declarative_display_bounds_fixture *) context)->bounds;
}
static uint32_t declarative_display_bounds_test_main(void *context) { return ((struct declarative_display_bounds_fixture *) context)->main_did; }
static bool declarative_display_bounds_test_menu_hidden(void *context) { return ((struct declarative_display_bounds_fixture *) context)->menu_hidden; }
static int declarative_display_bounds_test_notch(uint32_t did, void *context) { (void) did; return ((struct declarative_display_bounds_fixture *) context)->notch_height; }
static CGRect declarative_display_bounds_test_menu(uint32_t did, void *context) { (void) did; return ((struct declarative_display_bounds_fixture *) context)->menu; }
static bool declarative_display_bounds_test_dock_hidden(void *context) { return ((struct declarative_display_bounds_fixture *) context)->dock_hidden; }
static uint32_t declarative_display_bounds_test_dock_display(void *context) { return ((struct declarative_display_bounds_fixture *) context)->dock_did; }
static int declarative_display_bounds_test_dock_orientation(void *context) { return ((struct declarative_display_bounds_fixture *) context)->dock_orientation; }
static CGRect declarative_display_bounds_test_dock(void *context) { return ((struct declarative_display_bounds_fixture *) context)->dock; }

static void declarative_display_bounds_test_install(struct declarative_display_bounds_fixture *fixture)
{
    display_bounds_test_ops = (struct display_bounds_platform_ops) {
        .display_bounds = declarative_display_bounds_test_bounds,
        .main_display_id = declarative_display_bounds_test_main,
        .menu_bar_hidden = declarative_display_bounds_test_menu_hidden,
        .notch_height = declarative_display_bounds_test_notch,
        .menu_bar_rect = declarative_display_bounds_test_menu,
        .dock_hidden = declarative_display_bounds_test_dock_hidden,
        .dock_display_id = declarative_display_bounds_test_dock_display,
        .dock_orientation = declarative_display_bounds_test_dock_orientation,
        .dock_rect = declarative_display_bounds_test_dock,
        .context = fixture
    };
}

TEST_FUNC(declarative_topology_candidate_bounds_wrapper_preserves_platform_constraints,
{
    struct display_manager saved_display_manager = g_display_manager;
    struct declarative_display_bounds_fixture fixture = {
        .bounds = CGRectMake(0, 0, 1000, 800),
        .main_did = 1,
        .menu = CGRectMake(0, 0, 1000, 25),
        .dock_hidden = true,
        .dock_did = 2,
        .dock = CGRectMake(0, 0, 80, 60)
    };
    declarative_display_bounds_test_install(&fixture);

    CGRect frame = display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_MAIN, 30, 10);
    TEST_CHECK(frame.origin.y, 25.0);
    TEST_CHECK(frame.size.height, 775.0);

    fixture.dock_hidden = false;
    fixture.dock_orientation = DOCK_ORIENTATION_LEFT;
    frame = display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_MAIN, 30, 10);
    TEST_CHECK(frame.origin.x, 80.0);
    TEST_CHECK(frame.size.width, 920.0);
    TEST_CHECK(frame.origin.y, 25.0);
    TEST_CHECK(frame.size.height, 775.0);

    fixture.dock_orientation = DOCK_ORIENTATION_RIGHT;
    frame = display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_MAIN, 30, 10);
    TEST_CHECK(frame.origin.x, 0.0);
    TEST_CHECK(frame.size.width, 920.0);

    fixture.dock_orientation = DOCK_ORIENTATION_BOTTOM;
    frame = display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_MAIN, 30, 10);
    TEST_CHECK(frame.size.height, 715.0);

    fixture.dock_did = 3;
    frame = display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_MAIN, 30, 10);
    TEST_CHECK(frame.size.width, 1000.0);
    TEST_CHECK(frame.size.height, 775.0);

    fixture.menu_hidden = true;
    fixture.notch_height = 24;
    fixture.dock_hidden = true;
    fixture.dock_did = 2;
    frame = display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_ALL, 30, 10);
    TEST_CHECK(frame.origin.y, 30.0);
    TEST_CHECK(frame.size.height, 760.0);

    fixture.menu_hidden = false;
    fixture.dock_hidden = false;
    fixture.dock_orientation = DOCK_ORIENTATION_LEFT;
    frame = display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_ALL, 30, 10);
    TEST_CHECK(frame.origin.x, 80.0);
    TEST_CHECK(frame.origin.y, 55.0);
    TEST_CHECK(frame.size.width, 920.0);
    TEST_CHECK(frame.size.height, 735.0);

    g_display_manager.mode = EXTERNAL_BAR_ALL;
    g_display_manager.top_padding = 30;
    g_display_manager.bottom_padding = 10;
    TEST_CHECK(CGRectEqualToRect(display_bounds_constrained(2, false),
                                 display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_ALL, 30, 10)), true);
    TEST_CHECK(CGRectEqualToRect(display_bounds_constrained(2, true),
                                 display_bounds_constrained_with_external_bar(2, EXTERNAL_BAR_OFF, 30, 10)), true);

    display_bounds_test_ops = (struct display_bounds_platform_ops) {0};
    g_display_manager = saved_display_manager;
});

TEST_FUNC(declarative_topology_candidate_external_bar_bounds_match_view_constraints,
{
    struct declarative_config_model model = {0};
    struct declarative_topology_bounds_config built_in = declarative_startup_topology_bounds_config(NULL);
    TEST_CHECK(built_in.external_bar_mode, EXTERNAL_BAR_OFF);
    model.config[DECLARATIVE_CONFIG_EXTERNAL_BAR] = (struct declarative_value) {
        .present = true,
        .external_bar = { .mode = EXTERNAL_BAR_ALL, .top_padding = 30, .bottom_padding = 10 }
    };
    struct declarative_topology_bounds_config candidate = declarative_startup_topology_bounds_config(&model);
    TEST_CHECK(candidate.external_bar_mode, EXTERNAL_BAR_ALL);
    TEST_CHECK(candidate.external_bar_top_padding, 30);
    TEST_CHECK(candidate.external_bar_bottom_padding, 10);

    CGRect base = CGRectMake(0, 0, 1000, 800);
    CGRect menu = CGRectZero;
    CGRect dock = CGRectZero;
    CGRect main = display_bounds_apply_constraints(base, 1, 1, EXTERNAL_BAR_MAIN,
                                                   30, 10, true, 24, menu,
                                                   true, false, 0, dock);
    TEST_CHECK(main.origin.y, 30.0);
    TEST_CHECK(main.size.height, 760.0);

    CGRect non_main = display_bounds_apply_constraints(base, 2, 1, EXTERNAL_BAR_MAIN,
                                                       30, 10, true, 24, menu,
                                                       true, false, 0, dock);
    TEST_CHECK(non_main.origin.y, 24.0);
    TEST_CHECK(non_main.size.height, 776.0);

    CGRect all = display_bounds_apply_constraints(base, 2, 1, EXTERNAL_BAR_ALL,
                                                  30, 10, true, 24, menu,
                                                  true, false, 0, dock);
    TEST_CHECK(all.origin.y, 30.0);
    TEST_CHECK(all.size.height, 760.0);

    CGRect negative = display_bounds_apply_constraints(base, 1, 1, EXTERNAL_BAR_MAIN,
                                                       -20, -10, true, 24, menu,
                                                       true, false, 0, dock);
    TEST_CHECK(negative.origin.y, 24.0);
    TEST_CHECK(negative.size.height, 786.0);
});

TEST_FUNC(declarative_detached_views_use_only_capture_identity_bounds_kind_and_defaults,
{
    struct declarative_topology_fixture fixture;
    declarative_topology_test_fixture_init(&fixture, false);
    struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
    struct declarative_topology topology = {0};
    TEST_CHECK(declarative_topology_capture(&ops, &topology), true);
    int query_count = fixture.query_count;
    struct declarative_space_defaults defaults = {
        .layout = VIEW_BSP, .top_padding = 10, .bottom_padding = 20,
        .left_padding = 30, .right_padding = 40, .window_gap = 8,
        .split_type = SPLIT_X, .auto_balance = SPLIT_Y,
        .split_ratio = 0.5f, .window_placement = CHILD_SECOND,
        .window_insertion_point = INSERT_FOCUSED, .window_zoom_persist = true
    };
    struct declarative_detached_view_table table = {0};
    TEST_CHECK(declarative_detached_view_table_prepare(&topology, &defaults, &table), true);
    TEST_CHECK(fixture.query_count, query_count);
    TEST_CHECK(table.count, 3U);
    TEST_CHECK(table.view_table.count, 3);
    TEST_CHECK(table.sids[0], 11ULL);
    uint64_t lookup_sid = 12;
    TEST_CHECK(table_find(&table.view_table, &lookup_sid) == table.views[1], true);
    TEST_CHECK(table.views[0]->layout, VIEW_BSP);
    TEST_CHECK(table.views[0]->root->area.x, 30.0f);
    TEST_CHECK(table.views[0]->root->area.y, 10.0f);
    TEST_CHECK(table.views[0]->root->area.w, 930.0f);
    TEST_CHECK(table.views[0]->root->area.h, 770.0f);
    TEST_CHECK(table.views[2]->layout, VIEW_FLOAT);
    TEST_CHECK(CFEqual(table.views[2]->uuid, CFSTR("space-21")), true);
    declarative_detached_view_table_destroy(&table);

    defaults.left_padding = 700;
    defaults.right_padding = 400;
    defaults.top_padding = -100;
    defaults.bottom_padding = -50;
    TEST_CHECK(declarative_detached_view_table_prepare(&topology, &defaults, &table), true);
    TEST_CHECK(fixture.query_count, query_count);
    TEST_CHECK(table.views[0]->root->area.x, 700.0f);
    TEST_CHECK(table.views[0]->root->area.y, -100.0f);
    TEST_CHECK(table.views[0]->root->area.w, -100.0f);
    TEST_CHECK(table.views[0]->root->area.h, 950.0f);
    declarative_detached_view_table_destroy(&table);
    declarative_topology_destroy(&topology);
    declarative_topology_test_fixture_destroy(&fixture);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
});

struct declarative_topology_startup_test_context
{
    struct declarative_startup_state *state;
    struct declarative_space_defaults defaults;
    struct declarative_topology_capture_ops ops;
    pthread_t worker;
    bool worker_matched;
};

static bool declarative_topology_test_startup_bootstrap(void *context)
{
    struct declarative_topology_startup_test_context *test = context;
    test->worker = pthread_self();
    return declarative_startup_prepare_runtime(test->state, &test->defaults, &test->ops);
}

static bool declarative_topology_test_prepare_on_worker(struct declarative_startup_state *state,
                                                        const struct declarative_space_defaults *defaults,
                                                        const struct declarative_topology_capture_ops *ops,
                                                        bool *worker_matched)
{
    struct event_loop saved_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    struct declarative_topology_startup_test_context test = {
        .state = state,
        .defaults = *defaults,
        .ops = *ops
    };
    bool result = event_loop_begin_bootstrap(&g_event_loop, declarative_topology_test_startup_bootstrap, &test);
    if (result) {
        test.worker_matched = pthread_equal(test.worker, g_event_loop.thread);
        event_loop_end(&g_event_loop);
    }
    if (worker_matched) *worker_matched = test.worker_matched;
    g_event_loop = saved_loop;
    return result;
}

TEST_FUNC(declarative_topology_startup_preparation_is_worker_confined_and_transactional,
{
    struct declarative_topology_fixture fixture;
    declarative_topology_test_fixture_init(&fixture, false);
    struct declarative_startup_state state = { .present = true };
    struct space_manager space_before = g_space_manager;
    struct display_manager display_before = g_display_manager;
    struct window_manager window_before = g_window_manager;
    struct declarative_topology_startup_test_context test = {
        .state = &state,
        .defaults = {
            .layout = VIEW_FLOAT, .split_type = SPLIT_AUTO, .split_ratio = 0.5f,
            .window_placement = CHILD_SECOND, .window_insertion_point = INSERT_FOCUSED,
            .window_zoom_persist = true
        },
        .ops = declarative_topology_test_ops(&fixture)
    };
    bool worker_matched = false;
    TEST_CHECK(declarative_topology_test_prepare_on_worker(&state, &test.defaults, &test.ops, &worker_matched), true);
    TEST_CHECK(worker_matched, true);
    TEST_CHECK(state.detached_prepared, true);
    TEST_CHECK(state.topology.space_count, 3U);
    TEST_CHECK(state.detached_views.count, 3U);
    TEST_CHECK(memcmp(&g_space_manager, &space_before, sizeof(space_before)), 0);
    TEST_CHECK(memcmp(&g_display_manager, &display_before, sizeof(display_before)), 0);
    TEST_CHECK(memcmp(&g_window_manager, &window_before, sizeof(window_before)), 0);
    declarative_startup_destroy(&state);
    declarative_topology_test_fixture_destroy(&fixture);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);

    struct declarative_topology_fixture changed;
    declarative_topology_test_fixture_init(&changed, true);
    struct declarative_startup_state failed = { .present = true };
    struct declarative_space_defaults defaults = test.defaults;
    struct declarative_topology_capture_ops changed_ops = declarative_topology_test_ops(&changed);
    int changed_queries = changed.query_count;
    TEST_CHECK(declarative_startup_prepare_runtime(&failed, &defaults, &changed_ops), false);
    TEST_CHECK(changed.query_count, changed_queries);
    TEST_CHECK(declarative_topology_test_prepare_on_worker(&failed, &defaults, &changed_ops, NULL), false);
    TEST_CHECK(failed.present, true);
    TEST_CHECK(failed.detached_prepared, false);
    TEST_CHECK(failed.topology.displays == NULL, true);
    TEST_CHECK(failed.detached_views.views == NULL, true);
    TEST_CHECK(memcmp(&g_space_manager, &space_before, sizeof(space_before)), 0);
    TEST_CHECK(memcmp(&g_display_manager, &display_before, sizeof(display_before)), 0);
    TEST_CHECK(memcmp(&g_window_manager, &window_before, sizeof(window_before)), 0);
    declarative_startup_destroy(&failed);
    declarative_topology_test_fixture_destroy(&changed);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
});

TEST_FUNC(declarative_topology_production_bootstrap_retains_no_early_candidate,
{
    struct event_loop saved_loop = g_event_loop;
    g_event_loop = (struct event_loop) {0};
    struct declarative_startup_state state = { .present = false };
    TEST_CHECK(event_loop_begin_bootstrap(&g_event_loop, declarative_startup_bootstrap, &state), true);
    TEST_CHECK(state.detached_prepared, false);
    TEST_CHECK(state.topology.displays == NULL, true);
    TEST_CHECK(state.detached_views.views == NULL, true);
    event_loop_end(&g_event_loop);
    declarative_startup_destroy(&state);
    g_event_loop = saved_loop;
});

TEST_FUNC(declarative_topology_allocation_failures_leave_no_owned_state,
{
    declarative_topology_test_reset_allocator();
    struct declarative_topology_fixture baseline_fixture;
    declarative_topology_test_fixture_init(&baseline_fixture, false);
    struct declarative_topology_capture_ops baseline_ops = declarative_topology_test_ops(&baseline_fixture);
    struct declarative_topology baseline = {0};
    TEST_CHECK(declarative_topology_capture(&baseline_ops, &baseline), true);
    size_t capture_allocations = declarative_topology_test_allocation_count();
    declarative_topology_destroy(&baseline);
    declarative_topology_test_fixture_destroy(&baseline_fixture);

    for (size_t ordinal = 1; ordinal <= capture_allocations; ++ordinal) {
        declarative_topology_test_fail_allocation_at(ordinal);
        struct declarative_topology_fixture fixture;
        declarative_topology_test_fixture_init(&fixture, false);
        struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
        struct declarative_topology topology = {0};
        TEST_CHECK(declarative_topology_capture(&ops, &topology), false);
        TEST_CHECK(topology.displays == NULL, true);
        TEST_CHECK(topology.spaces == NULL, true);
        declarative_topology_test_fixture_destroy(&fixture);
        TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
    }

    declarative_topology_test_reset_allocator();
    struct declarative_topology_fixture startup_baseline_fixture;
    declarative_topology_test_fixture_init(&startup_baseline_fixture, false);
    struct declarative_topology_capture_ops startup_baseline_ops = declarative_topology_test_ops(&startup_baseline_fixture);
    struct declarative_startup_state startup_baseline = { .present = true };
    struct declarative_space_defaults defaults = {
        .layout = VIEW_BSP, .split_type = SPLIT_AUTO, .split_ratio = 0.5f,
        .window_placement = CHILD_SECOND, .window_insertion_point = INSERT_FOCUSED,
        .window_zoom_persist = true
    };
    TEST_CHECK(declarative_topology_test_prepare_on_worker(&startup_baseline, &defaults, &startup_baseline_ops, NULL), true);
    size_t startup_allocations = declarative_topology_test_allocation_count();
    declarative_startup_destroy(&startup_baseline);
    declarative_topology_test_fixture_destroy(&startup_baseline_fixture);
    TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);

    for (size_t ordinal = 1; ordinal <= startup_allocations; ++ordinal) {
        declarative_topology_test_fail_allocation_at(ordinal);
        struct declarative_topology_fixture fixture;
        declarative_topology_test_fixture_init(&fixture, false);
        struct declarative_topology_capture_ops ops = declarative_topology_test_ops(&fixture);
        struct declarative_startup_state state = { .present = true };
        TEST_CHECK(declarative_topology_test_prepare_on_worker(&state, &defaults, &ops, NULL), false);
        TEST_CHECK(state.detached_prepared, false);
        TEST_CHECK(state.topology.displays == NULL, true);
        TEST_CHECK(state.detached_views.views == NULL, true);
        declarative_startup_destroy(&state);
        declarative_topology_test_fixture_destroy(&fixture);
        TEST_CHECK(declarative_topology_test_outstanding_allocations(), 0U);
    }
    declarative_topology_test_reset_allocator();
});
