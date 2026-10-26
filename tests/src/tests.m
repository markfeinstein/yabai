#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>

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
static bool g_daemon_response_test_fail_allocate;
static bool g_daemon_response_test_fail_fmemopen;
static void *daemon_response_test_allocate(size_t size)
{
    return g_daemon_response_test_fail_allocate ? NULL : malloc(size);
}
static FILE *daemon_response_test_fmemopen(void *bytes, size_t size, const char *mode)
{
    if (g_daemon_response_test_fail_fmemopen) {
        errno = ENOMEM;
        return NULL;
    }
    return fmemopen(bytes, size, mode);
}
#define DAEMON_RESPONSE_ALLOCATE(size) daemon_response_test_allocate(size)
#define DAEMON_RESPONSE_FMEMOPEN(bytes, size, mode) daemon_response_test_fmemopen(bytes, size, mode)

static bool g_test_cfarray_create_mutable_fail;
static bool g_test_cfdictionary_create_fail;
static int g_test_cfnumber_create_fail_after = -1;
static int g_test_cfnumber_create_calls;

static CFMutableArrayRef test_cfarray_create_mutable(CFAllocatorRef allocator, CFIndex capacity,
                                                     const CFArrayCallBacks *callbacks)
{
    return g_test_cfarray_create_mutable_fail ? NULL : CFArrayCreateMutable(allocator, capacity, callbacks);
}

static CFDictionaryRef test_cfdictionary_create(CFAllocatorRef allocator, const void **keys,
                                                const void **values, CFIndex count,
                                                const CFDictionaryKeyCallBacks *key_callbacks,
                                                const CFDictionaryValueCallBacks *value_callbacks)
{
    return g_test_cfdictionary_create_fail ? NULL :
           CFDictionaryCreate(allocator, keys, values, count, key_callbacks, value_callbacks);
}

static CFNumberRef test_cfnumber_create(CFAllocatorRef allocator, CFNumberType type, const void *value)
{
    if (g_test_cfnumber_create_fail_after >= 0 &&
        g_test_cfnumber_create_calls++ == g_test_cfnumber_create_fail_after) return NULL;
    return CFNumberCreate(allocator, type, value);
}

#define YABAI_CFARRAY_CREATE_MUTABLE test_cfarray_create_mutable
#define YABAI_CFDICTIONARY_CREATE test_cfdictionary_create
#define YABAI_CFNUMBER_CREATE test_cfnumber_create

static const char *g_test_runtime_home_override;
static uid_t g_test_runtime_effective_uid = (uid_t) -1;
static uid_t test_runtime_effective_uid(void)
{
    return g_test_runtime_effective_uid == (uid_t) -1 ? geteuid() : g_test_runtime_effective_uid;
}
#define YABAI_RUNTIME_HOME_OVERRIDE g_test_runtime_home_override
#define YABAI_RUNTIME_GETEUID() test_runtime_effective_uid()
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

static bool test_temp_storage_ensure(uint64_t size);

#include "area.c"
#include "display.c"
#include "window.c"
#include "view.c"
#include "accordion.c"
#include "window_manager.c"
#include "process_manager.c"
#include "event_loop.c"
#include "inactive_space_launch.c"
#include "space_gesture.c"
#include "window_ordered.c"
#include "event_queue.c"
#include "menu_bar_model.c"
#include "menu_bar_manager.c"
#include "message.c"
#include "declarative_config.c"
#include "declarative_modes.c"
#include "declarative_startup.c"
#include "declarative_runtime.c"
#include "declarative_rules.c"
#include "declarative_topology.c"
#include "declarative_install.c"
#include "declarative_reload.c"
#include "declarative_reporter.c"
#include "declarative_startup_completion.c"
#include "declarative_space.c"
#include "declarative_space_phase3a.c"
#include "declarative_space_phase3a_matrix.c"
#include "declarative_space_phase3b.c"
#include "declarative_space_phase3c.c"
#include "ffm.c"
#include "mission_control.c"
#include "message_frame.c"
#include "daemon_io.c"
#include "daemon_io_additional.c"
#include "daemon_io_lifecycle.c"
#include "daemon_io_concurrency.c"
#include "message_loop_accept.c"
#include "daemon_io_capacity.c"
#include "daemon_io_failures.c"
#include "daemon_identity.c"
#include "sa_message.c"
#include "deferred_convergence.c"
#include "animation_failure.c"
#include "doctor.c"
#undef TEST_ENTRY

static bool test_temp_storage_ensure(uint64_t size)
{
    if (g_temp_storage.memory && g_temp_storage.memory != MAP_FAILED) {
        return g_temp_storage.size >= size;
    }

    memset(&g_temp_storage, 0, sizeof(g_temp_storage));
    if (!ts_init(size)) {
        memset(&g_temp_storage, 0, sizeof(g_temp_storage));
        return false;
    }

    return true;
}

static void test_table_free_and_zero(struct table *table)
{
    if (table->buckets) table_free(table);
    memset(table, 0, sizeof(*table));
}

static bool g_test_window_animations_lock_initialized;

static void test_suite_teardown(void)
{
    if (g_test_window_animations_lock_initialized) {
        pthread_mutex_destroy(&g_window_manager.window_animations_lock);
        g_test_window_animations_lock_initialized = false;
    }

    test_table_free_and_zero(&g_window_manager.window);
    test_table_free_and_zero(&g_window_manager.managed_window);
    test_table_free_and_zero(&g_window_manager.insert_feedback);
    test_table_free_and_zero(&g_space_manager.view);

    if (g_temp_storage.memory && g_temp_storage.memory != MAP_FAILED) {
        munmap(g_temp_storage.memory, g_temp_storage.size + getpagesize());
    }
    memset(&g_temp_storage, 0, sizeof(g_temp_storage));
}

TEST_FUNC(managed_space_topology_parsing_is_type_safe_and_ordered,
{
    int checked_count = -1;
    TEST_CHECK(cfindex_to_int(-1, &checked_count), false);
    TEST_CHECK(cfindex_to_int((CFIndex) INT_MAX + 1, &checked_count), false);
    TEST_CHECK(cfindex_to_int(3, &checked_count), true);
    TEST_CHECK(checked_count, 3);
    TEST_CHECK(managed_space_first(NULL), 0ULL);
    TEST_CHECK(managed_space_last(NULL), 0ULL);
    TEST_CHECK(managed_space_select(NULL, 1), 0ULL);
    TEST_CHECK(managed_space_index(NULL, 1), 0);
    TEST_CHECK(managed_space_next_index(0), 0);
    TEST_CHECK(managed_space_next_index(1), 2);
    TEST_CHECK(managed_space_next_index(INT_MAX - 1), INT_MAX);
    TEST_CHECK(managed_space_next_index(INT_MAX), 0);

    NSArray *empty = @[];
    TEST_CHECK(managed_space_first((CFArrayRef) empty), 0ULL);
    TEST_CHECK(managed_space_last((CFArrayRef) empty), 0ULL);

    NSDictionary *missing_spaces = @{ @"Display Identifier": @"A" };
    NSDictionary *wrong_spaces = @{ @"Display Identifier": @"B", @"Spaces": @"wrong" };
    NSDictionary *malformed_space = @{ @"id64": @"wrong" };
    NSDictionary *valid_space_10 = @{ @"id64": @10 };
    NSDictionary *valid_space_20 = @{ @"id64": @20 };
    NSDictionary *valid_space_30 = @{ @"id64": @30 };
    NSDictionary *first_display = @{ @"Display Identifier": @"C",
                                     @"Spaces": @[ malformed_space, valid_space_10, @"wrong", valid_space_20 ] };
    NSDictionary *second_display = @{ @"Display Identifier": @"D",
                                      @"Spaces": @[ valid_space_30 ] };
    NSArray *topology = @[ @"wrong", missing_spaces, wrong_spaces, first_display, second_display ];

    TEST_CHECK(managed_space_first((CFArrayRef) topology), 10ULL);
    TEST_CHECK(managed_space_last((CFArrayRef) topology), 30ULL);
    TEST_CHECK(managed_space_select((CFArrayRef) topology, 1), 10ULL);
    TEST_CHECK(managed_space_select((CFArrayRef) topology, 2), 20ULL);
    TEST_CHECK(managed_space_select((CFArrayRef) topology, 3), 30ULL);
    TEST_CHECK(managed_space_select((CFArrayRef) topology, 4), 0ULL);
    TEST_CHECK(managed_space_index((CFArrayRef) topology, 20), 2);
    TEST_CHECK(managed_space_previous((CFArrayRef) topology, 20), 10ULL);
    TEST_CHECK(managed_space_next((CFArrayRef) topology, 20), 30ULL);
    TEST_CHECK(managed_space_previous((CFArrayRef) topology, 10), 0ULL);
    TEST_CHECK(managed_space_next((CFArrayRef) topology, 30), 0ULL);
    TEST_CHECK(managed_space_previous((CFArrayRef) topology, 99), 0ULL);
    TEST_CHECK(managed_space_next((CFArrayRef) topology, 99), 0ULL);
});

TEST_FUNC(managed_space_ids_require_positive_signed_64_bit_values,
{
    NSDictionary *minimum = @{ @"id64": @((long long) INT64_MIN) };
    NSDictionary *negative = @{ @"id64": @-1 };
    NSDictionary *zero = @{ @"id64": @0 };
    NSDictionary *one = @{ @"id64": @1 };
    NSDictionary *maximum = @{ @"id64": @((long long) INT64_MAX) };
    NSDictionary *wrong_type = @{ @"id64": @"1" };
    NSDictionary *absent = @{};

    uint64_t sid = 99;
    TEST_CHECK(managed_space_id((CFTypeRef) minimum, &sid), false);
    TEST_CHECK(sid, 0ULL);
    sid = 99;
    TEST_CHECK(managed_space_id((CFTypeRef) negative, &sid), false);
    TEST_CHECK(sid, 0ULL);
    sid = 99;
    TEST_CHECK(managed_space_id((CFTypeRef) zero, &sid), false);
    TEST_CHECK(sid, 0ULL);
    TEST_CHECK(managed_space_id((CFTypeRef) one, &sid), true);
    TEST_CHECK(sid, 1ULL);
    TEST_CHECK(managed_space_id((CFTypeRef) maximum, &sid), true);
    TEST_CHECK(sid, (uint64_t) INT64_MAX);
    sid = 99;
    TEST_CHECK(managed_space_id((CFTypeRef) wrong_type, &sid), false);
    TEST_CHECK(sid, 0ULL);
    sid = 99;
    TEST_CHECK(managed_space_id((CFTypeRef) absent, &sid), false);
    TEST_CHECK(sid, 0ULL);
    TEST_CHECK(managed_space_id((CFTypeRef) one, NULL), false);

    NSArray *mixed = @[ minimum, negative, zero, one, wrong_type, absent, maximum ];
    int count = -1;
    TEST_CHECK(managed_space_count_valid_checked((CFArrayRef) mixed, &count), true);
    TEST_CHECK(count, 2);
    uint64_t list[2] = { 0, 0 };
    TEST_CHECK(managed_space_copy_valid((CFArrayRef) mixed, list), 2);
    TEST_CHECK(list[0], 1ULL);
    TEST_CHECK(list[1], (uint64_t) INT64_MAX);

    NSArray *topology = @[ @{ @"Display Identifier": @"A", @"Spaces": mixed } ];
    TEST_CHECK(managed_space_select((CFArrayRef) topology, 1), 1ULL);
    TEST_CHECK(managed_space_select((CFArrayRef) topology, 2), (uint64_t) INT64_MAX);
    TEST_CHECK(managed_space_select((CFArrayRef) topology, 3), 0ULL);
    TEST_CHECK(managed_space_index((CFArrayRef) topology, 0), 0);
    TEST_CHECK(managed_space_index((CFArrayRef) topology, 1), 1);
    TEST_CHECK(managed_space_index((CFArrayRef) topology, (uint64_t) INT64_MAX), 2);
});

TEST_FUNC(managed_display_space_extraction_skips_malformed_entries,
{
    NSDictionary *missing_spaces = @{ @"Display Identifier": @"missing" };
    NSDictionary *wrong_spaces = @{ @"Display Identifier": @"wrong", @"Spaces": @1 };
    NSDictionary *empty_spaces = @{ @"Display Identifier": @"empty", @"Spaces": @[] };
    NSDictionary *mixed_spaces = @{ @"Display Identifier": @"mixed",
                                     @"Spaces": @[ @{ @"id64": @1 }, @{ @"id64": @"bad" }, @{}, @{ @"id64": @3 } ] };
    NSArray *topology = @[ @1, missing_spaces, wrong_spaces, empty_spaces, mixed_spaces ];

    TEST_CHECK(managed_space_spaces_for_display(NULL, CFSTR("mixed")) == NULL, true);
    TEST_CHECK(managed_space_spaces_for_display((CFArrayRef) topology, NULL) == NULL, true);
    TEST_CHECK(managed_space_spaces_for_display((CFArrayRef) topology, CFSTR("missing")) == NULL, true);
    TEST_CHECK(managed_space_spaces_for_display((CFArrayRef) topology, CFSTR("wrong")) == NULL, true);

    CFArrayRef spaces = managed_space_spaces_for_display((CFArrayRef) topology, CFSTR("empty"));
    TEST_CHECK(spaces != NULL, true);
    TEST_CHECK(managed_space_count_valid(spaces), 0);

    spaces = managed_space_spaces_for_display((CFArrayRef) topology, CFSTR("mixed"));
    TEST_CHECK(spaces != NULL, true);
    TEST_CHECK(managed_space_count_valid(spaces), 2);
    uint64_t list[2] = { 0 };
    TEST_CHECK(managed_space_copy_valid(spaces, list), 2);
    TEST_CHECK(list[0], 1ULL);
    TEST_CHECK(list[1], 3ULL);
});

struct active_display_list_test_fixture
{
    CGError errors[2];
    uint32_t counts[2];
    uint32_t ids[3];
    uint32_t capacities[2];
    int calls;
};

static struct active_display_list_test_fixture active_display_list_test_fixture;

static CGError active_display_list_test_call(uint32_t capacity, CGDirectDisplayID *displays, uint32_t *count)
{
    int call = active_display_list_test_fixture.calls++;
    active_display_list_test_fixture.capacities[call] = capacity;
    uint32_t available = active_display_list_test_fixture.counts[call];
    if (call == 1 && displays) {
        uint32_t copied = available < capacity ? available : capacity;
        *count = copied;
        for (uint32_t i = 0; i < copied; ++i) displays[i] = active_display_list_test_fixture.ids[i];
    } else {
        *count = available;
    }
    return active_display_list_test_fixture.errors[call];
}

static void active_display_list_test_reset(CGError first_error, uint32_t first_count,
                                           CGError second_error, uint32_t second_count)
{
    active_display_list_test_fixture = (struct active_display_list_test_fixture) {
        .errors = { first_error, second_error },
        .counts = { first_count, second_count },
        .ids = { 11, 22, 33 }
    };
}

TEST_FUNC(active_display_enumeration_rejects_errors_and_unstable_samples,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;
    ts_reset();
    display_manager_test_set_active_display_list_fn(active_display_list_test_call);

    active_display_list_test_reset(kCGErrorFailure, 2, kCGErrorSuccess, 2);
    TEST_CHECK(display_manager_active_display_count(), 0);
    TEST_CHECK(active_display_list_test_fixture.calls, 1);

    active_display_list_test_reset(kCGErrorSuccess, (uint32_t) INT_MAX + 1U, kCGErrorSuccess, 0);
    TEST_CHECK(display_manager_active_display_count(), 0);

    active_display_list_test_reset(kCGErrorSuccess, 2, kCGErrorSuccess, 2);
    TEST_CHECK(display_manager_active_display_count(), 2);

    int count = 99;
    active_display_list_test_reset(kCGErrorFailure, 2, kCGErrorSuccess, 2);
    TEST_CHECK(display_manager_active_display_list(&count) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(active_display_list_test_fixture.calls, 1);

    count = 99;
    active_display_list_test_reset(kCGErrorSuccess, 0, kCGErrorSuccess, 0);
    TEST_CHECK(display_manager_active_display_list(&count) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(active_display_list_test_fixture.calls, 1);

    count = 99;
    active_display_list_test_reset(kCGErrorSuccess, (uint32_t) INT_MAX + 1U, kCGErrorSuccess, 0);
    TEST_CHECK(display_manager_active_display_list(&count) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(active_display_list_test_fixture.calls, 1);

    count = 99;
    active_display_list_test_reset(kCGErrorSuccess, 2, kCGErrorFailure, 2);
    TEST_CHECK(display_manager_active_display_list(&count) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(active_display_list_test_fixture.calls, 2);
    TEST_CHECK(active_display_list_test_fixture.capacities[1], 3U);

    count = 99;
    active_display_list_test_reset(kCGErrorSuccess, 2, kCGErrorSuccess, 3);
    TEST_CHECK(display_manager_active_display_list(&count) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(active_display_list_test_fixture.capacities[1], 3U);

    count = 99;
    active_display_list_test_reset(kCGErrorSuccess, 2, kCGErrorSuccess, 1);
    TEST_CHECK(display_manager_active_display_list(&count) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(active_display_list_test_fixture.capacities[1], 3U);

    count = 99;
    active_display_list_test_reset(kCGErrorSuccess, 2, kCGErrorSuccess, 2);
    uint32_t *displays = display_manager_active_display_list(&count);
    TEST_CHECK(displays != NULL, true);
    TEST_CHECK(count, 2);
    TEST_CHECK(active_display_list_test_fixture.capacities[1], 3U);
    if (displays) {
        TEST_CHECK(displays[0], 11U);
        TEST_CHECK(displays[1], 22U);
    }

    ts_reset();
    count = 99;
    uint32_t oversized_count = (uint32_t) (g_temp_storage.size / sizeof(uint32_t));
    active_display_list_test_reset(kCGErrorSuccess, oversized_count, kCGErrorSuccess, oversized_count);
    TEST_CHECK(display_manager_active_display_list(&count) == NULL, true);
    TEST_CHECK(count, 0);
    TEST_CHECK(active_display_list_test_fixture.calls, 1);
    TEST_CHECK(g_temp_storage.used, 0ULL);
    TEST_CHECK(ts_alloc_list_checked(uint32_t, 1) != NULL, true);
    TEST_CHECK(g_temp_storage.used, sizeof(uint32_t));

    TEST_CHECK(display_manager_active_display_list(NULL) == NULL, true);
    display_manager_test_set_active_display_list_fn(NULL);
});

TEST_FUNC(temporary_storage_checked_allocation_is_fallible_and_atomic,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;
    ts_reset();

    TEST_CHECK(ts_alloc_list_checked(uint8_t, 1) != NULL, true);
    uint64_t used = g_temp_storage.used;
    TEST_CHECK(ts_alloc_aligned_checked(3, 1) == NULL, true);
    TEST_CHECK(g_temp_storage.used, used);
    TEST_CHECK(ts_alloc_list_checked(uint64_t, UINT64_MAX) == NULL, true);
    TEST_CHECK(g_temp_storage.used, used);
    TEST_CHECK(ts_alloc_aligned_checked(8, g_temp_storage.size + 1) == NULL, true);
    TEST_CHECK(g_temp_storage.used, used);

    uint64_t *aligned = ts_alloc_list_checked(uint64_t, 1);
    TEST_CHECK(aligned != NULL, true);
    if (aligned) TEST_CHECK((uintptr_t) aligned % __alignof__(uint64_t), 0ULL);
});

TEST_FUNC(temporary_stretch_buffer_growth_charges_only_capacity,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;
    ts_reset();

    uint32_t *values = NULL;
    for (uint32_t i = 0; i < 100; ++i) {
        ts_buf_push(values, i);
        uint64_t expected = offsetof(struct ts_buf_hdr, buf) +
                            (uint64_t) ts_buf_cap(values) * sizeof(*values);
        TEST_CHECK(g_temp_storage.used, expected);
        TEST_CHECK(values[i], i);
    }
});

TEST_FUNC(core_foundation_constructors_fail_conservatively,
{
    uint32_t values[] = { 1, 2 };

    g_test_cfarray_create_mutable_fail = true;
    TEST_CHECK(cfarray_of_cfnumbers(values, sizeof(*values), array_count(values), kCFNumberSInt32Type) == NULL, true);
    g_test_cfarray_create_mutable_fail = false;

    g_test_cfnumber_create_calls = 0;
    g_test_cfnumber_create_fail_after = 0;
    TEST_CHECK(cfarray_of_cfnumbers(values, sizeof(*values), array_count(values), kCFNumberSInt32Type) == NULL, true);
    g_test_cfnumber_create_calls = 0;
    TEST_CHECK(sls_window_disable_shadow(1), false);
    g_test_cfnumber_create_fail_after = -1;

    g_test_cfnumber_create_calls = 0;
    g_test_cfnumber_create_fail_after = 1;
    TEST_CHECK(cfarray_of_cfnumbers(values, sizeof(*values), array_count(values), kCFNumberSInt32Type) == NULL, true);
    g_test_cfnumber_create_fail_after = -1;

    g_test_cfdictionary_create_fail = true;
    TEST_CHECK(sls_window_disable_shadow(1), false);
    TEST_CHECK(ax_privilege(), false);
    g_test_cfdictionary_create_fail = false;

    g_test_cfarray_create_mutable_fail = true;
    TEST_CHECK(window_is_sticky(1), false);
    g_test_cfarray_create_mutable_fail = false;

    TEST_CHECK(cfarray_of_cfnumbers(values, 0, 1, kCFNumberSInt32Type) == NULL, true);
    TEST_CHECK(cfarray_of_cfnumbers(NULL, sizeof(*values), 1, kCFNumberSInt32Type) == NULL, true);
    TEST_CHECK(cfarray_of_cfnumbers(values, sizeof(*values), -1, kCFNumberSInt32Type) == NULL, true);

    CFArrayRef empty = cfarray_of_cfnumbers(NULL, sizeof(*values), 0, kCFNumberSInt32Type);
    TEST_CHECK(empty != NULL, true);
    if (empty) {
        TEST_CHECK(CFArrayGetCount(empty), 0L);
        CFRelease(empty);
    }
});

TEST_FUNC(display_selection_helpers_are_null_and_type_safe,
{
    TEST_CHECK(display_manager_display_id_for_uuid(NULL), 0U);
    CFTypeRef wrong_uuid = kCFBooleanTrue;
    TEST_CHECK(display_manager_display_id_for_uuid((CFStringRef) wrong_uuid), 0U);
    TEST_CHECK(display_id((CFStringRef) wrong_uuid), 0U);
    TEST_CHECK(display_manager_uuid_at_index(NULL, 0) == NULL, true);
    TEST_CHECK(display_manager_copy_valid_display_uuids(NULL, DISPLAY_ARRANGEMENT_ORDER_DEFAULT) == NULL, true);

    NSArray *displays = @[ @1, @"display-a", @{}, @"display-b" ];
    g_test_cfarray_create_mutable_fail = true;
    TEST_CHECK(display_manager_copy_valid_display_uuids((CFArrayRef) displays,
                                                        DISPLAY_ARRANGEMENT_ORDER_DEFAULT) == NULL, true);
    g_test_cfarray_create_mutable_fail = false;
    TEST_CHECK(display_manager_uuid_at_index((CFArrayRef) displays, -1) == NULL, true);
    TEST_CHECK(display_manager_uuid_at_index((CFArrayRef) displays, 0) == NULL, true);
    TEST_CHECK(display_manager_uuid_at_index((CFArrayRef) displays, 4) == NULL, true);

    CFMutableArrayRef filtered = display_manager_copy_valid_display_uuids((CFArrayRef) displays, DISPLAY_ARRANGEMENT_ORDER_DEFAULT);
    TEST_CHECK(filtered != NULL, true);
    if (filtered) {
        TEST_CHECK(CFArrayGetCount(filtered), 2L);
        TEST_CHECK(CFEqual(CFArrayGetValueAtIndex(filtered, 0), CFSTR("display-a")), true);
        TEST_CHECK(CFEqual(CFArrayGetValueAtIndex(filtered, 1), CFSTR("display-b")), true);
        CFRelease(filtered);
    }

    filtered = display_manager_copy_valid_display_uuids((CFArrayRef) displays, DISPLAY_ARRANGEMENT_ORDER_X);
    TEST_CHECK(filtered != NULL, true);
    if (filtered) {
        TEST_CHECK(CFArrayGetCount(filtered), 2L);
        TEST_CHECK(cf_is_type(CFArrayGetValueAtIndex(filtered, 0), CFStringGetTypeID()), true);
        TEST_CHECK(cf_is_type(CFArrayGetValueAtIndex(filtered, 1), CFStringGetTypeID()), true);
        CFRelease(filtered);
    }
});

TEST_FUNC(display_space_empty_allocation_preserves_flattened_adjacency,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;
    ts_reset();

    NSArray *populated_a = @[ @{ @"id64": @11 }, @{ @"id64": @22 } ];
    NSArray *empty = @[];
    NSArray *populated_b = @[ @{ @"id64": @33 } ];
    int count_a = -1;
    int count_empty = -1;
    int count_b = -1;
    uint64_t *list_a = display_space_list_from_spaces((CFArrayRef) populated_a, &count_a);
    uint64_t *list_empty = display_space_list_from_spaces((CFArrayRef) empty, &count_empty);
    uint64_t *list_b = display_space_list_from_spaces((CFArrayRef) populated_b, &count_b);

    TEST_CHECK(count_a, 2);
    TEST_CHECK(count_empty, 0);
    TEST_CHECK(count_b, 1);
    TEST_CHECK(list_empty == list_a + count_a, true);
    TEST_CHECK(list_b == list_empty, true);
    TEST_CHECK(list_a[0], 11ULL);
    TEST_CHECK(list_a[1], 22ULL);
    TEST_CHECK(list_a[2], 33ULL);
});

TEST_FUNC(add_and_clamp_to_zero_saturates,
{
    TEST_CHECK(add_and_clamp_to_zero(10, -3), 7);
    TEST_CHECK(add_and_clamp_to_zero(1, -2), 0);
    TEST_CHECK(add_and_clamp_to_zero(INT_MIN, INT_MIN), 0);
    TEST_CHECK(add_and_clamp_to_zero(INT_MAX, -1), INT_MAX - 1);
    TEST_CHECK(add_and_clamp_to_zero(INT_MAX, 1), INT_MAX);
});

static int g_event_loop_test_thread_create_calls;

static int event_loop_test_fail_thread_create(pthread_t *thread, const pthread_attr_t *attr,
                                              void *(*start_routine)(void *), void *context)
{
    (void) thread;
    (void) attr;
    (void) start_routine;
    (void) context;
    ++g_event_loop_test_thread_create_calls;
    return EAGAIN;
}

TEST_FUNC(event_loop_begin_thread_creation_failure_is_atomic,
{
    struct event_loop event_loop;
    memset(&event_loop, 0xa5, sizeof(event_loop));

    __auto_type saved_thread_create = event_loop_thread_create;
    g_event_loop_test_thread_create_calls = 0;
    event_loop_thread_create = event_loop_test_fail_thread_create;
    bool began = event_loop_begin(&event_loop);
    event_loop_thread_create = saved_thread_create;

    TEST_CHECK(began, false);
    TEST_CHECK(g_event_loop_test_thread_create_calls, 1);
    TEST_CHECK(event_loop.is_running, false);
    TEST_CHECK(event_loop.semaphore == NULL, true);
    TEST_CHECK(event_loop.queue.events == NULL, true);
    TEST_CHECK(event_loop.queue.capacity, 0U);
    TEST_CHECK(event_loop.queue.head, 0U);
    TEST_CHECK(event_loop.queue.count, 0U);
});

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
    TEST_CHECK(flush_node == &parent, true);
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

TEST_FUNC(socket_open_sets_cloexec,
{
    int sockfd = -1;
    TEST_CHECK(socket_open(&sockfd), true);
    if (sockfd != -1) {
        int descriptor_flags = fcntl(sockfd, F_GETFD);
        TEST_CHECK(descriptor_flags != -1, true);
        if (descriptor_flags != -1) {
            TEST_CHECK((descriptor_flags & FD_CLOEXEC) != 0, true);
        }
        close(sockfd);
    }
});

TEST_FUNC(socket_cloexec_helper_sets_flag_and_rejects_invalid_fd,
{
    int sockets[2] = { -1, -1 };
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    if (sockets[0] != -1 && sockets[1] != -1) {
        TEST_CHECK(socket_set_cloexec(sockets[0]), true);
        int descriptor_flags = fcntl(sockets[0], F_GETFD);
        TEST_CHECK(descriptor_flags != -1, true);
        if (descriptor_flags != -1) {
            TEST_CHECK((descriptor_flags & FD_CLOEXEC) != 0, true);
        }
        close(sockets[0]);
        close(sockets[1]);
    }
    TEST_CHECK(socket_set_cloexec(-1), false);
});

TEST_FUNC(socket_address_initializer_enforces_unix_path_boundaries,
{
    struct sockaddr_un address;
    memset(&address, 0xff, sizeof(address));

    TEST_CHECK(socket_address_init(NULL, "/tmp/socket"), false);
    TEST_CHECK(socket_address_init(&address, NULL), false);
    TEST_CHECK(socket_address_init(&address, ""), false);

    char exact_fit[sizeof(address.sun_path)];
    memset(exact_fit, 'a', sizeof(exact_fit) - 1);
    exact_fit[sizeof(exact_fit) - 1] = '\0';
    TEST_CHECK(socket_address_init(&address, exact_fit), true);
    TEST_CHECK(address.sun_family, AF_UNIX);
    TEST_CHECK(strcmp(address.sun_path, exact_fit), 0);

    char too_long[sizeof(address.sun_path) + 1];
    memset(too_long, 'b', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_CHECK(socket_address_init(&address, too_long), false);
    TEST_CHECK(address.sun_family, 0);
    TEST_CHECK(address.sun_path[0], '\0');

    char rejected_path[sizeof(address.sun_path) + 1];
    memcpy(rejected_path, "/tmp/", 5);
    memset(rejected_path + 5, 'c', sizeof(address.sun_path) - 5);
    rejected_path[sizeof(address.sun_path)] = '\0';
    int rejected_fd = open(rejected_path, O_CREAT | O_WRONLY, 0600);
    TEST_CHECK(rejected_fd != -1, true);
    if (rejected_fd != -1) close(rejected_fd);
    TEST_CHECK(message_loop_begin(rejected_path), false);
    TEST_CHECK(access(rejected_path, F_OK), 0);
    unlink(rejected_path);
});

TEST_FUNC(scripting_addition_socket_identity_requires_strict_nonroot_uid,
{
    char *saved_sudo_uid = getenv("SUDO_UID") ? strdup(getenv("SUDO_UID")) : NULL;
    char saved_socket_path[MAXLEN];
    snprintf(saved_socket_path, sizeof(saved_socket_path), "%s", g_sa_socket_file);

    char socket_path[MAXLEN];
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(NULL, socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("1junk", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("+1", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("-1", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(" 1", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("1 ", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("184467440737095516160", socket_path, sizeof(socket_path)), false);
    TEST_CHECK(scripting_addition_socket_path_for_sudo_uid("0", socket_path, sizeof(socket_path)), false);

    uid_t current_uid = getuid();
    char fixture[] = "/private/tmp/yabai-controller-test.XXXXXX";
    char *fixture_root = mkdtemp(fixture);
    char fixture_home[PATH_MAX], fixture_library[PATH_MAX], fixture_caches[PATH_MAX];
    if (current_uid && fixture_root) {
        snprintf(fixture_home, sizeof(fixture_home), "%s/home", fixture_root);
        snprintf(fixture_library, sizeof(fixture_library), "%s/Library", fixture_home);
        snprintf(fixture_caches, sizeof(fixture_caches), "%s/Caches", fixture_library);
        TEST_CHECK(mkdir(fixture_home, 0700), 0);
        TEST_CHECK(mkdir(fixture_library, 0700), 0);
        TEST_CHECK(mkdir(fixture_caches, 0700), 0);
        g_test_runtime_home_override = fixture_home;
        char current_uid_string[32];
        struct yabai_runtime_paths expected;
        snprintf(current_uid_string, sizeof(current_uid_string), "%llu", (unsigned long long) current_uid);
        TEST_CHECK(yabai_runtime_derive_for_uid(current_uid, fixture_home, &expected), true);
        TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(current_uid_string, socket_path, sizeof(socket_path)), true);
        TEST_CHECK(strcmp(socket_path, expected.sa_socket), 0);
        char yabai_dir[PATH_MAX];
        snprintf(yabai_dir, sizeof(yabai_dir), "%s/Library/Caches/yabai", fixture_home);
        TEST_CHECK(access(yabai_dir, F_OK), -1);
        TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(current_uid_string, socket_path, 2), false);
        TEST_CHECK(access(yabai_dir, F_OK), -1);
        g_test_runtime_home_override = NULL;
        rmdir(fixture_caches);
        rmdir(fixture_library);
        rmdir(fixture_home);
        rmdir(fixture_root);
    }

    uid_t nonexistent_uid = (uid_t) -1;
    while (nonexistent_uid > 0 && getpwuid(nonexistent_uid)) --nonexistent_uid;
    TEST_CHECK(nonexistent_uid > 0, true);
    if (nonexistent_uid > 0) {
        char nonexistent_uid_string[32];
        snprintf(nonexistent_uid_string, sizeof(nonexistent_uid_string), "%llu", (unsigned long long) nonexistent_uid);
        TEST_CHECK(scripting_addition_socket_path_for_sudo_uid(nonexistent_uid_string, socket_path, sizeof(socket_path)), false);
    }

    unsetenv("SUDO_UID");
    TEST_CHECK(scripting_addition_set_socket_path(), false);
    setenv("SUDO_UID", "", 1);
    TEST_CHECK(scripting_addition_set_socket_path(), false);

    if (saved_sudo_uid) {
        setenv("SUDO_UID", saved_sudo_uid, 1);
        free(saved_sudo_uid);
    } else {
        unsetenv("SUDO_UID");
    }
    snprintf(g_sa_socket_file, sizeof(g_sa_socket_file), "%s", saved_socket_path);
});

static int scripting_addition_test_close_count;

static size_t scripting_addition_test_write_failure(const void *buffer, size_t size, size_t count, FILE *handle)
{
    (void) buffer;
    (void) size;
    (void) count;
    (void) handle;
    return 0;
}

static int scripting_addition_test_close_success(FILE *handle)
{
    ++scripting_addition_test_close_count;
    return fclose(handle);
}

static int scripting_addition_test_close_failure(FILE *handle)
{
    (void) fclose(handle);
    return EOF;
}

TEST_FUNC(scripting_addition_helpers_propagate_failures_and_use_literal_paths,
{
    TEST_CHECK(scripting_addition_prepare_binary("/path/that/does/not/exist"), false);

    char file_template[] = "/tmp/yabai-sa-write-test.XXXXXX";
    int file_descriptor = mkstemp(file_template);
    TEST_CHECK(file_descriptor != -1, true);
    if (file_descriptor != -1) {
        close(file_descriptor);
        TEST_CHECK(scripting_addition_write_file("payload", 7, file_template, "wb"), true);

        __auto_type saved_write_stream = scripting_addition_write_stream;
        __auto_type saved_close_file = scripting_addition_close_file;
        scripting_addition_test_close_count = 0;
        scripting_addition_write_stream = scripting_addition_test_write_failure;
        scripting_addition_close_file = scripting_addition_test_close_success;
        TEST_CHECK(scripting_addition_write_file("payload", 7, file_template, "wb"), false);
        TEST_CHECK(scripting_addition_test_close_count, 1);

        scripting_addition_write_stream = saved_write_stream;
        scripting_addition_close_file = scripting_addition_test_close_failure;
        TEST_CHECK(scripting_addition_write_file("payload", 7, file_template, "wb"), false);
        scripting_addition_close_file = saved_close_file;
        unlink(file_template);
    }

    char directory_template[] = "/tmp/yabai-sa-remove-test.XXXXXX";
    char *directory = mkdtemp(directory_template);
    TEST_CHECK(directory != NULL, true);
    if (directory) {
        char literal_path[PATH_MAX];
        char sentinel_path[PATH_MAX];
        snprintf(literal_path, sizeof(literal_path), "%s/remove;literal", directory);
        snprintf(sentinel_path, sizeof(sentinel_path), "%s/sentinel", directory);
        TEST_CHECK(mkdir(literal_path, 0700), 0);
        FILE *sentinel = fopen(sentinel_path, "w");
        TEST_CHECK(sentinel != NULL, true);
        if (sentinel) fclose(sentinel);

        TEST_CHECK(scripting_addition_remove_path(literal_path), true);
        TEST_CHECK(access(literal_path, F_OK), -1);
        TEST_CHECK(access(sentinel_path, F_OK), 0);

        unlink(sentinel_path);
        rmdir(directory);
    }
});

TEST_FUNC(plist_xml_validator_accepts_xml_characters_and_rejects_invalid_text,
{
    const char forbidden_control[] = { 'a', 0x01, 'b', '\0' };
    const char malformed_utf8[] = { (char) 0xc3, 0x28, '\0' };
    const char bmp_fffe[] = { (char) 0xef, (char) 0xbf, (char) 0xbe, '\0' };
    const char bmp_ffff[] = { (char) 0xef, (char) 0xbf, (char) 0xbf, '\0' };
    const char supplementary_scalar[] = { (char) 0xf0, (char) 0x9f, (char) 0x98, (char) 0x80, '\0' };

    TEST_CHECK(plist_xml_string_is_valid("normal UTF-8: h\xc3\xa9llo"), true);
    TEST_CHECK(plist_xml_string_is_valid("&<>\"'"), true);
    TEST_CHECK(plist_xml_string_is_valid("tab\tnewline\ncarriage-return\r"), true);
    TEST_CHECK(plist_xml_string_is_valid(forbidden_control), false);
    TEST_CHECK(plist_xml_string_is_valid(malformed_utf8), false);
    TEST_CHECK(plist_xml_string_is_valid(bmp_fffe), false);
    TEST_CHECK(plist_xml_string_is_valid(bmp_ffff), false);
    TEST_CHECK(plist_xml_string_is_valid(supplementary_scalar), true);
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

TEST_FUNC(utf8_validation_and_json_replacement_are_strict,
{
    static const unsigned char ascii[] = { 'a', 's', 'c', 'i', 'i' };
    static const unsigned char valid_2[] = { 0xc2, 0xa2 };
    static const unsigned char valid_3[] = { 0xe2, 0x82, 0xac };
    static const unsigned char valid_4[] = { 0xf0, 0x9f, 0x98, 0x80 };
    static const unsigned char truncated_2[] = { 0xc2 };
    static const unsigned char truncated_3[] = { 0xe2, 0x82 };
    static const unsigned char truncated_4[] = { 0xf0, 0x9f, 0x98 };
    static const unsigned char continuation[] = { 0x80 };
    static const unsigned char overlong_2[] = { 0xc0, 0xaf };
    static const unsigned char overlong_3[] = { 0xe0, 0x80, 0xaf };
    static const unsigned char overlong_4[] = { 0xf0, 0x80, 0x80, 0xaf };
    static const unsigned char surrogate[] = { 0xed, 0xa0, 0x80 };
    static const unsigned char too_large[] = { 0xf4, 0x90, 0x80, 0x80 };
    static const struct {
        const unsigned char *bytes;
        size_t length;
        bool valid;
    } cases[] = {
        { ascii, sizeof(ascii), true },
        { valid_2, sizeof(valid_2), true },
        { valid_3, sizeof(valid_3), true },
        { valid_4, sizeof(valid_4), true },
        { truncated_2, sizeof(truncated_2), false },
        { truncated_3, sizeof(truncated_3), false },
        { truncated_4, sizeof(truncated_4), false },
        { continuation, sizeof(continuation), false },
        { overlong_2, sizeof(overlong_2), false },
        { overlong_3, sizeof(overlong_3), false },
        { overlong_4, sizeof(overlong_4), false },
        { surrogate, sizeof(surrogate), false },
        { too_large, sizeof(too_large), false },
    };

    for (int i = 0; i < array_count(cases); ++i) {
        TEST_CHECK(utf8_validate_bytes(cases[i].bytes, cases[i].length), cases[i].valid);
    }

    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;

    uint64_t saved_temp_storage_used = g_temp_storage.used;
    ts_reset();

    char valid_string[] = { 'x', (char) 0xc2, (char) 0xa2, (char) 0xe2, (char) 0x82,
                            (char) 0xac, (char) 0xf0, (char) 0x9f, (char) 0x98, (char) 0x80, 0 };
    TEST_CHECK(utf8_validate_string(valid_string), true);
    TEST_CHECK(ts_string_escape(valid_string) == NULL, true);

    char malformed[] = { 'a', (char) 0xc0, (char) 0xaf, 'b', 0 };
    TEST_CHECK(utf8_validate_string(malformed), false);
    char *escaped = ts_string_escape(malformed);
    TEST_CHECK(escaped != NULL, true);
    TEST_CHECK(strcmp(escaped, "a\\ufffd\\ufffdb"), 0);

    char json_buffer[128];
    int json_length = snprintf(json_buffer, sizeof(json_buffer), "\"%s\"", escaped);
    TEST_CHECK(json_length > 0 && json_length < (int) sizeof(json_buffer), true);
    NSData *json_data = [NSData dataWithBytes:json_buffer length:(NSUInteger) json_length];
    NSError *json_error = nil;
    NSString *json_string = [NSJSONSerialization JSONObjectWithData:json_data options:NSJSONReadingFragmentsAllowed error:&json_error];
    static const unsigned char expected_bytes[] = { 'a', 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd, 'b' };
    NSString *expected_string = [[NSString alloc] initWithBytes:expected_bytes length:sizeof(expected_bytes) encoding:NSUTF8StringEncoding];
    TEST_CHECK(json_error == nil, true);
    TEST_CHECK(json_string != nil, true);
    TEST_CHECK([json_string isEqualToString:expected_string], true);
    [expected_string release];

    g_temp_storage.used = saved_temp_storage_used;
});

TEST_FUNC(serialization_string_escaping_is_strict,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;

    uint64_t saved_temp_storage_used = g_temp_storage.used;
    ts_reset();

    char xml_input[] = "amp& lt< gt> quote\" apos'";
    char *xml_escaped = plist_xml_escape(xml_input);
    TEST_CHECK(xml_escaped != NULL, true);
    TEST_CHECK(strcmp(xml_escaped, "amp&amp; lt&lt; gt&gt; quote&quot; apos&apos;") == 0, true);
    free(xml_escaped);

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

    ts_reset();
    char json_input[] = "quote\" backslash\\ newline\n tab\t control\x01";
    char *json_escaped = ts_string_escape(json_input);
    TEST_CHECK(strcmp(json_escaped, "quote\\\" backslash\\\\ newline\\n tab\\t control\\u0001") == 0, true);
    char expected_json[256];
    snprintf(expected_json, sizeof(expected_json), "%s", json_escaped);
    NSString *json_original = [NSString stringWithUTF8String:json_input];

    ts_reset();
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

    ts_reset();
    struct window applicationless_window = { .id = 0, .scratchpad = json_input };
    FILE *window_file = tmpfile();
    TEST_CHECK(window_file != NULL, true);
    if (window_file) {
        uint64_t window_flags = WINDOW_PROPERTY_PID |
                                WINDOW_PROPERTY_APP |
                                WINDOW_PROPERTY_SCRATCHPAD |
                                WINDOW_PROPERTY_IS_HIDDEN;
        window_serialize(window_file, &applicationless_window, window_flags);
        rewind(window_file);
        char window_buffer[2048] = {0};
        fread(window_buffer, 1, sizeof(window_buffer)-1, window_file);
        fclose(window_file);

        NSData *window_data = [NSData dataWithBytes:window_buffer length:strlen(window_buffer)];
        NSError *window_error = nil;
        NSDictionary *window_json = [NSJSONSerialization JSONObjectWithData:window_data options:0 error:&window_error];
        TEST_CHECK(window_json != nil, true);
        TEST_CHECK([[window_json objectForKey:@"pid"] intValue], 0);
        TEST_CHECK([[window_json objectForKey:@"app"] isEqualToString:@"<unknown>"], true);
        TEST_CHECK([[window_json objectForKey:@"scratchpad"] isEqualToString:json_original], true);
        TEST_CHECK([[window_json objectForKey:@"is-hidden"] boolValue], false);
    }
    TEST_CHECK(window_serialize_is_hidden(&applicationless_window), false);

    const UInt8 role_bytes[] = { 'r', 'o', 'l', 'e', '"', '\\', '\n', '\t', 0x01 };
    const UInt8 subrole_bytes[] = { 's', 'u', 'b', 'r', 'o', 'l', 'e', '"', '\\', '\n', '\t', 0x02 };
    struct window role_window = {0};
    role_window.role = CFStringCreateWithBytes(NULL, role_bytes, sizeof(role_bytes), kCFStringEncodingUTF8, false);
    role_window.subrole = CFStringCreateWithBytes(NULL, subrole_bytes, sizeof(subrole_bytes), kCFStringEncodingUTF8, false);
    TEST_CHECK(role_window.role != NULL, true);
    TEST_CHECK(role_window.subrole != NULL, true);

    if (role_window.role && role_window.subrole) {
        ts_reset();
        FILE *role_file = tmpfile();
        TEST_CHECK(role_file != NULL, true);
        if (role_file) {
            window_serialize(role_file, &role_window, WINDOW_PROPERTY_ROLE | WINDOW_PROPERTY_SUBROLE);
            rewind(role_file);
            char role_buffer[2048] = {0};
            fread(role_buffer, 1, sizeof(role_buffer) - 1, role_file);
            fclose(role_file);

            NSData *role_data = [NSData dataWithBytes:role_buffer length:strlen(role_buffer)];
            NSError *role_error = nil;
            NSDictionary *role_json = [NSJSONSerialization JSONObjectWithData:role_data options:0 error:&role_error];
            TEST_CHECK(role_json != nil, true);
            TEST_CHECK([[role_json objectForKey:@"role"] isEqualToString:(NSString *) role_window.role], true);
            TEST_CHECK([[role_json objectForKey:@"subrole"] isEqualToString:(NSString *) role_window.subrole], true);
        }
    }

    if (role_window.role) CFRelease(role_window.role);
    if (role_window.subrole) CFRelease(role_window.subrole);

    struct space_label *saved_space_labels = g_space_manager.labels;
    g_space_manager.labels = NULL;
    struct view labeled_view = { .sid = 0x7fffffe0ULL };
    space_manager_set_label_for_space(&g_space_manager, labeled_view.sid, strdup(json_input));
    ts_reset();
    FILE *view_file = tmpfile();
    TEST_CHECK(view_file != NULL, true);
    if (view_file) {
        view_serialize(view_file, &labeled_view, SPACE_PROPERTY_LABEL);
        rewind(view_file);
        char view_buffer[2048] = {0};
        fread(view_buffer, 1, sizeof(view_buffer)-1, view_file);
        fclose(view_file);

        NSData *view_data = [NSData dataWithBytes:view_buffer length:strlen(view_buffer)];
        NSError *view_error = nil;
        NSDictionary *view_json = [NSJSONSerialization JSONObjectWithData:view_data options:0 error:&view_error];
        TEST_CHECK(view_json != nil, true);
        TEST_CHECK([[view_json objectForKey:@"label"] isEqualToString:json_original], true);
    }
    space_manager_remove_label_for_space(&g_space_manager, labeled_view.sid);
    buf_free(g_space_manager.labels);
    g_space_manager.labels = saved_space_labels;

    g_temp_storage.used = saved_temp_storage_used;
});

TEST_FUNC(window_and_space_serializers_round_trip_safe_escaped_and_absent_strings,
{
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;

    uint64_t saved_temp_storage_used = g_temp_storage.used;
    char *forms[] = {
        "ordinary-safe-value",
        "quote\" backslash\\ newline\n tab\t control\x01",
        NULL
    };

    for (int i = 0; i < array_count(forms); ++i) {
        char *value = forms[i];
        NSString *expected = value ? [NSString stringWithUTF8String:value] : @"";
        struct window window = { .scratchpad = value };
        if (value) {
            window.role = CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8);
            window.subrole = CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8);
        }

        ts_reset();
        FILE *file = tmpfile();
        TEST_CHECK(file != NULL, true);
        if (file) {
            window_serialize(file, &window, WINDOW_PROPERTY_SCRATCHPAD | WINDOW_PROPERTY_ROLE | WINDOW_PROPERTY_SUBROLE);
            rewind(file);
            char buffer[2048] = {0};
            fread(buffer, 1, sizeof(buffer) - 1, file);
            fclose(file);

            NSData *data = [NSData dataWithBytes:buffer length:strlen(buffer)];
            NSError *error = nil;
            NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
            TEST_CHECK(error == nil, true);
            TEST_CHECK(json != nil, true);
            TEST_CHECK([[json objectForKey:@"scratchpad"] isEqualToString:expected], true);
            TEST_CHECK([[json objectForKey:@"role"] isEqualToString:expected], true);
            TEST_CHECK([[json objectForKey:@"subrole"] isEqualToString:expected], true);
        }

        if (window.role) CFRelease(window.role);
        if (window.subrole) CFRelease(window.subrole);
    }

    struct space_label *saved_space_labels = g_space_manager.labels;
    g_space_manager.labels = NULL;
    struct view view = { .sid = 0x7fffffdfULL };

    for (int i = 0; i < array_count(forms); ++i) {
        char *value = forms[i];
        NSString *expected = value ? [NSString stringWithUTF8String:value] : @"";
        if (value) space_manager_set_label_for_space(&g_space_manager, view.sid, strdup(value));

        ts_reset();
        FILE *file = tmpfile();
        TEST_CHECK(file != NULL, true);
        if (file) {
            view_serialize(file, &view, SPACE_PROPERTY_LABEL);
            rewind(file);
            char buffer[2048] = {0};
            fread(buffer, 1, sizeof(buffer) - 1, file);
            fclose(file);

            NSData *data = [NSData dataWithBytes:buffer length:strlen(buffer)];
            NSError *error = nil;
            NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
            TEST_CHECK(error == nil, true);
            TEST_CHECK(json != nil, true);
            TEST_CHECK([[json objectForKey:@"label"] isEqualToString:expected], true);
        }

        space_manager_remove_label_for_space(&g_space_manager, view.sid);
    }

    buf_free(g_space_manager.labels);
    g_space_manager.labels = saved_space_labels;
    g_temp_storage.used = saved_temp_storage_used;
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
    bool temp_storage_ready = test_temp_storage_ensure(KILOBYTES(64));
    TEST_CHECK(temp_storage_ready, true);
    if (!temp_storage_ready) return result;

    uint64_t saved_temp_storage_used = g_temp_storage.used;
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

    g_temp_storage.used = saved_temp_storage_used;
});

TEST_FUNC(mouse_click_state_ordinary_drag_does_not_poison_consumed_click,
{
    struct mouse_state mouse_state = {0};
    struct mouse_click_state *left_click = mouse_click_state_for_event(&mouse_state, kCGEventLeftMouseDown);

    mouse_click_state_mouse_down(left_click, false);
    TEST_CHECK(left_click->drag_detected, false);
    mouse_click_state_mouse_dragged(left_click);
    struct mouse_click_state_up_result ordinary_mouse_up = mouse_click_state_mouse_up(left_click);

    TEST_CHECK(ordinary_mouse_up.consume, false);
    TEST_CHECK(ordinary_mouse_up.repost, false);
    TEST_CHECK(left_click->drag_detected, false);

    mouse_click_state_mouse_down(left_click, true);
    struct mouse_click_state_up_result consumed_mouse_up = mouse_click_state_mouse_up(left_click);

    TEST_CHECK(consumed_mouse_up.consume, true);
    TEST_CHECK(consumed_mouse_up.repost, true);

    mouse_click_state_clear(left_click);
    TEST_CHECK(left_click->down_event == NULL, true);
    TEST_CHECK(left_click->consume, false);
    TEST_CHECK(left_click->drag_detected, false);
});

TEST_FUNC(mouse_click_state_tracks_chorded_buttons_independently,
{
    struct mouse_state mouse_state = {0};
    int left_event_token;
    int right_event_token;
    struct mouse_click_state *left_click = mouse_click_state_for_event(&mouse_state, kCGEventLeftMouseDown);
    struct mouse_click_state *right_click = mouse_click_state_for_event(&mouse_state, kCGEventRightMouseDown);

    mouse_click_state_mouse_down(left_click, true);
    left_click->down_event = (CGEventRef) &left_event_token;
    mouse_click_state_mouse_down(right_click, true);
    right_click->down_event = (CGEventRef) &right_event_token;

    struct mouse_click_state_up_result right_up = mouse_click_state_mouse_up(mouse_click_state_for_event(&mouse_state, kCGEventRightMouseUp));
    TEST_CHECK(right_up.consume, true);
    TEST_CHECK(right_up.repost, true);
    mouse_click_state_clear(right_click);
    TEST_CHECK(right_click->down_event == NULL, true);
    TEST_CHECK(left_click->consume, true);
    TEST_CHECK(left_click->down_event == (CGEventRef) &left_event_token, true);

    struct mouse_click_state_up_result left_up = mouse_click_state_mouse_up(mouse_click_state_for_event(&mouse_state, kCGEventLeftMouseUp));
    TEST_CHECK(left_up.consume, true);
    TEST_CHECK(left_up.repost, true);
    mouse_click_state_clear(left_click);

    mouse_click_state_mouse_down(right_click, true);
    right_click->down_event = (CGEventRef) &right_event_token;
    mouse_click_state_mouse_down(left_click, true);
    left_click->down_event = (CGEventRef) &left_event_token;

    left_up = mouse_click_state_mouse_up(mouse_click_state_for_event(&mouse_state, kCGEventLeftMouseUp));
    TEST_CHECK(left_up.consume, true);
    TEST_CHECK(left_up.repost, true);
    mouse_click_state_clear(left_click);
    TEST_CHECK(left_click->down_event == NULL, true);
    TEST_CHECK(right_click->consume, true);
    TEST_CHECK(right_click->down_event == (CGEventRef) &right_event_token, true);

    right_up = mouse_click_state_mouse_up(mouse_click_state_for_event(&mouse_state, kCGEventRightMouseUp));
    TEST_CHECK(right_up.consume, true);
    TEST_CHECK(right_up.repost, true);
    mouse_click_state_clear(right_click);
});

TEST_FUNC(mouse_action_classifies_buttons_and_modifiers,
{
    CGEventFlags alt = kCGEventFlagMaskAlternate;
    CGEventFlags shift = kCGEventFlagMaskShift;

    TEST_CHECK(mouse_action_for_event(kCGEventLeftMouseDown, alt, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_MOVE);
    TEST_CHECK(mouse_action_for_event(kCGEventRightMouseDown, alt, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_RESIZE);
    TEST_CHECK(mouse_action_for_event(kCGEventLeftMouseDown, shift, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_NONE);
    TEST_CHECK(mouse_action_for_event(kCGEventRightMouseDown, alt | shift, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_NONE);
    TEST_CHECK(mouse_action_for_event(kCGEventMouseMoved, alt, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE), MOUSE_MODE_NONE);
});

TEST_FUNC(mouse_queued_action_is_immutable_across_configuration_changes,
{
    struct mouse_state mouse_state = {0};
    mouse_state_set_modifier(&mouse_state, MOUSE_MOD_ALT);
    mouse_state_set_action1(&mouse_state, MOUSE_MODE_MOVE);
    mouse_state_set_action2(&mouse_state, MOUSE_MODE_RESIZE);

    enum mouse_mode queued_action = mouse_action_for_event(kCGEventLeftMouseDown,
                                                           kCGEventFlagMaskAlternate,
                                                           mouse_state.modifier,
                                                           mouse_state.action1,
                                                           mouse_state.action2);

    mouse_state_set_modifier(&mouse_state, MOUSE_MOD_SHIFT);
    mouse_state_set_action1(&mouse_state, MOUSE_MODE_RESIZE);
    mouse_state_set_action2(&mouse_state, MOUSE_MODE_MOVE);

    TEST_CHECK(queued_action, MOUSE_MODE_MOVE);
    TEST_CHECK(mouse_action_for_event(kCGEventLeftMouseDown,
                                      kCGEventFlagMaskAlternate,
                                      mouse_state.modifier,
                                      mouse_state.action1,
                                      mouse_state.action2), MOUSE_MODE_NONE);
    TEST_CHECK(mouse_action_for_event(kCGEventLeftMouseDown,
                                      kCGEventFlagMaskShift,
                                      mouse_state.modifier,
                                      mouse_state.action1,
                                      mouse_state.action2), MOUSE_MODE_RESIZE);
});

TEST_FUNC(mouse_unclassified_action_does_not_begin_gesture,
{
    struct mouse_state mouse_state = {
        .current_action = MOUSE_MODE_NONE,
        .active_button = MOUSE_BUTTON_NONE,
        .down_location = { 11.0f, 12.0f },
        .window_frame = { { 13.0f, 14.0f }, { 15.0f, 16.0f } },
        .direction = HANDLE_TOP
    };
    struct window window = { .frame = { { 20.0f, 30.0f }, { 400.0f, 300.0f } } };
    CGPoint point = { 40.0f, 50.0f };
    enum mouse_mode action = mouse_action_for_event(kCGEventLeftMouseDown, 0, MOUSE_MOD_ALT, MOUSE_MODE_MOVE, MOUSE_MODE_RESIZE);

    TEST_CHECK(action, MOUSE_MODE_NONE);
    TEST_CHECK(mouse_drag_begin(&mouse_state, action, kCGMouseButtonLeft, &window, point), false);
    TEST_CHECK(mouse_state.window == NULL, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(mouse_state.active_button, MOUSE_BUTTON_NONE);
    TEST_CHECK(mouse_state.down_location.x, 11.0f);
    TEST_CHECK(mouse_state.down_location.y, 12.0f);
    TEST_CHECK(mouse_state.window_frame.origin.x, 13.0f);
    TEST_CHECK(mouse_state.window_frame.origin.y, 14.0f);
    TEST_CHECK(mouse_state.window_frame.size.width, 15.0f);
    TEST_CHECK(mouse_state.window_frame.size.height, 16.0f);
    TEST_CHECK(mouse_state.direction, HANDLE_TOP);
});

static int g_mouse_drag_move_call_count;
static struct window *g_mouse_drag_move_window;
static int g_mouse_drag_resize_call_count;
static CGRect g_mouse_drag_resize_frames[16];
static uint64_t g_mouse_drag_resize_time;
static int g_mouse_drop_adjust_call_count;
static struct mouse_window_info g_mouse_drop_adjust_info;
static bool g_native_resize_space_visible;
static int g_native_resize_schedule_count;
static uint32_t g_native_resize_scheduled_generation;
static uint64_t g_native_resize_scheduled_delay;
static bool g_native_resize_live_frame_result;
static bool g_native_resize_live_frame_override;
static CGRect g_native_resize_live_frame;
static int g_native_resize_restore_count;

enum native_resize_ax_frame_case
{
    NATIVE_RESIZE_AX_FRAME_VALID,
    NATIVE_RESIZE_AX_FRAME_POSITION_COPY_FAIL,
    NATIVE_RESIZE_AX_FRAME_SIZE_COPY_FAIL,
    NATIVE_RESIZE_AX_FRAME_WRONG_CF_TYPE,
    NATIVE_RESIZE_AX_FRAME_WRONG_AX_TYPE,
    NATIVE_RESIZE_AX_FRAME_POINT_GET_FAIL,
    NATIVE_RESIZE_AX_FRAME_SIZE_GET_FAIL,
    NATIVE_RESIZE_AX_FRAME_NONFINITE,
    NATIVE_RESIZE_AX_FRAME_NONPOSITIVE
};

static enum native_resize_ax_frame_case g_native_resize_ax_frame_case;
static CGRect g_native_resize_ax_frame;
static int g_native_resize_ax_release_count;

static bool test_native_resize_space_visible(uint64_t sid)
{
    (void) sid;
    return g_native_resize_space_visible;
}

static void test_native_resize_restore(uint32_t window_id)
{
    (void) window_id;
    ++g_native_resize_restore_count;
}

static bool test_native_resize_live_frame(struct window *window, CGRect *frame)
{
    (void) window;
    if (!g_native_resize_live_frame_result) return false;
    *frame = g_native_resize_live_frame_override ? g_native_resize_live_frame
                                                  : g_mouse_state.native_resize.latest_frame;
    return true;
}

static AXError test_native_resize_ax_copy(AXUIElementRef element, CFStringRef attribute, CFTypeRef *value)
{
    (void) element;
    bool position = CFEqual(attribute, kAXPositionAttribute);
    *value = NULL;
    if (position && g_native_resize_ax_frame_case == NATIVE_RESIZE_AX_FRAME_POSITION_COPY_FAIL) {
        return kAXErrorCannotComplete;
    }
    if (!position && g_native_resize_ax_frame_case == NATIVE_RESIZE_AX_FRAME_SIZE_COPY_FAIL) {
        return kAXErrorCannotComplete;
    }
    if (position && g_native_resize_ax_frame_case == NATIVE_RESIZE_AX_FRAME_WRONG_CF_TYPE) {
        *value = CFRetain(CFSTR("not-an-ax-value"));
        return kAXErrorSuccess;
    }
    if (position) {
        if (g_native_resize_ax_frame_case == NATIVE_RESIZE_AX_FRAME_WRONG_AX_TYPE) {
            CGSize size = g_native_resize_ax_frame.size;
            *value = AXValueCreate(kAXValueTypeCGSize, &size);
        } else {
            CGPoint point = g_native_resize_ax_frame.origin;
            if (g_native_resize_ax_frame_case == NATIVE_RESIZE_AX_FRAME_NONFINITE) point.x = NAN;
            *value = AXValueCreate(kAXValueTypeCGPoint, &point);
        }
    } else {
        CGSize size = g_native_resize_ax_frame.size;
        if (g_native_resize_ax_frame_case == NATIVE_RESIZE_AX_FRAME_NONPOSITIVE) size.width = 0.0;
        *value = AXValueCreate(kAXValueTypeCGSize, &size);
    }
    return *value ? kAXErrorSuccess : kAXErrorFailure;
}

static Boolean test_native_resize_ax_get(AXValueRef value, AXValueType type, void *result)
{
    if (type == kAXValueTypeCGPoint &&
        g_native_resize_ax_frame_case == NATIVE_RESIZE_AX_FRAME_POINT_GET_FAIL) return false;
    if (type == kAXValueTypeCGSize &&
        g_native_resize_ax_frame_case == NATIVE_RESIZE_AX_FRAME_SIZE_GET_FAIL) return false;
    return AXValueGetValue(value, type, result);
}

static void test_native_resize_ax_release(CFTypeRef value)
{
    ++g_native_resize_ax_release_count;
    CFRelease(value);
}

static void test_native_resize_schedule(uint32_t generation, uint64_t delay)
{
    ++g_native_resize_schedule_count;
    g_native_resize_scheduled_generation = generation;
    g_native_resize_scheduled_delay = delay;
}

static void test_mouse_drag_move_window(struct window *window, float x, float y)
{
    (void) x;
    (void) y;
    ++g_mouse_drag_move_call_count;
    g_mouse_drag_move_window = window;
}

static void test_mouse_drag_resize_window(struct window *window, CGRect frame)
{
    (void) window;
    g_mouse_drag_resize_frames[g_mouse_drag_resize_call_count++] = frame;
}

static uint64_t test_mouse_drag_time(void)
{
    return g_mouse_drag_resize_time;
}

static void test_mouse_drop_adjust(struct window_manager *wm, struct view *view, struct window *window, struct mouse_window_info *info)
{
    (void) wm;
    (void) view;
    (void) window;
    ++g_mouse_drop_adjust_call_count;
    g_mouse_drop_adjust_info = *info;
}

struct native_resize_test_fixture
{
    struct mouse_state mouse_state;
    struct table windows;
    struct table managed;
    uint32_t focused_window_id;
    enum mission_control_mode mission_control_mode;
    bool (*visible_hook)(uint64_t);
    void (*schedule_hook)(uint32_t, uint64_t);
    void (*restore_hook)(uint32_t);
    bool (*live_frame_hook)(struct window *, CGRect *);
    AXError (*ax_copy_hook)(AXUIElementRef, CFStringRef, CFTypeRef *);
    Boolean (*ax_get_hook)(AXValueRef, AXValueType, void *);
    void (*ax_release_hook)(CFTypeRef);
    void (*drop_hook)(struct window_manager *, struct view *, struct window *, struct mouse_window_info *);
    struct window window;
    struct window_node node;
    struct view view;
};

static void native_resize_test_begin(struct native_resize_test_fixture *fixture, uint32_t wid)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->mouse_state = g_mouse_state;
    fixture->windows = g_window_manager.window;
    fixture->managed = g_window_manager.managed_window;
    fixture->focused_window_id = g_window_manager.focused_window_id;
    fixture->mission_control_mode = g_mission_control_mode;
    fixture->visible_hook = event_loop_test_native_resize_space_visible;
    fixture->schedule_hook = event_loop_test_native_resize_schedule;
    fixture->restore_hook = event_loop_test_native_resize_restore;
    fixture->live_frame_hook = event_loop_test_native_resize_live_frame;
    fixture->ax_copy_hook = event_loop_test_native_resize_ax_copy;
    fixture->ax_get_hook = event_loop_test_native_resize_ax_get;
    fixture->ax_release_hook = event_loop_test_native_resize_ax_release;
    fixture->drop_hook = event_loop_test_mouse_drop_try_adjust_bsp_grid;

    table_init(&g_window_manager.window, 1, hash_wm, compare_wm);
    table_init(&g_window_manager.managed_window, 1, hash_wm, compare_wm);
    fixture->node = (struct window_node) {
        .area = { .x = 20.0f, .y = 30.0f, .w = 400.0f, .h = 300.0f },
        .window_count = 1, .window_list = { wid }, .window_order = { wid }
    };
    fixture->view = (struct view) { .sid = 1, .layout = VIEW_BSP, .root = &fixture->node };
    fixture->window = (struct window) {
        .id = wid, .frame = { { 20.0f, 30.0f }, { 400.0f, 300.0f } }, .flags = WINDOW_RESIZABLE
    };
    fixture->window.id_ptr = &fixture->window.id;
    table_add(&g_window_manager.window, &fixture->window.id, &fixture->window);
    table_add(&g_window_manager.managed_window, &fixture->window.id, &fixture->view);
    g_window_manager.focused_window_id = wid;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
    g_mouse_state = (struct mouse_state) { .native_edge_resize = true, .active_button = MOUSE_BUTTON_NONE };
    g_native_resize_space_visible = true;
    g_native_resize_schedule_count = 0;
    g_native_resize_live_frame_result = true;
    g_native_resize_live_frame_override = false;
    g_native_resize_live_frame = CGRectZero;
    g_native_resize_restore_count = 0;
    g_native_resize_ax_frame_case = NATIVE_RESIZE_AX_FRAME_VALID;
    g_native_resize_ax_frame = CGRectZero;
    g_native_resize_ax_release_count = 0;
    g_mouse_drop_adjust_call_count = 0;
    event_loop_test_native_resize_space_visible = test_native_resize_space_visible;
    event_loop_test_native_resize_schedule = test_native_resize_schedule;
    event_loop_test_native_resize_restore = test_native_resize_restore;
    event_loop_test_native_resize_live_frame = test_native_resize_live_frame;
    event_loop_test_mouse_drop_try_adjust_bsp_grid = test_mouse_drop_adjust;
}

static void native_resize_test_end(struct native_resize_test_fixture *fixture)
{
    event_loop_test_native_resize_space_visible = fixture->visible_hook;
    event_loop_test_native_resize_schedule = fixture->schedule_hook;
    event_loop_test_native_resize_restore = fixture->restore_hook;
    event_loop_test_native_resize_live_frame = fixture->live_frame_hook;
    event_loop_test_native_resize_ax_copy = fixture->ax_copy_hook;
    event_loop_test_native_resize_ax_get = fixture->ax_get_hook;
    event_loop_test_native_resize_ax_release = fixture->ax_release_hook;
    event_loop_test_mouse_drop_try_adjust_bsp_grid = fixture->drop_hook;
    g_mouse_state = fixture->mouse_state;
    table_free(&g_window_manager.window);
    table_free(&g_window_manager.managed_window);
    g_window_manager.window = fixture->windows;
    g_window_manager.managed_window = fixture->managed;
    g_window_manager.focused_window_id = fixture->focused_window_id;
    g_mission_control_mode = fixture->mission_control_mode;
}

static void native_resize_test_mouse_down(CGPoint point)
{
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, point, kCGMouseButtonLeft);
    if (event) EVENT_HANDLER_MOUSE_DOWN(event, MOUSE_MODE_NONE);
}

static void native_resize_test_mouse_up(CGPoint point)
{
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, point, kCGMouseButtonLeft);
    if (event) EVENT_HANDLER_MOUSE_UP(event, 0);
}

TEST_FUNC(native_edge_resize_accepts_mouse_up_before_and_between_geometry_events,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 990);

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(g_mouse_state.native_resize.window_id, 990U);
    TEST_CHECK(g_mouse_state.native_resize.edges, HANDLE_RIGHT);
    native_resize_test_mouse_up(CGPointMake(440.0f, 150.0f));
    TEST_CHECK(g_mouse_state.native_resize.released, true);
    uint32_t release_generation = g_native_resize_scheduled_generation;
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 420.0f, 300.0f), true), true);
    uint32_t geometry_generation = g_native_resize_scheduled_generation;
    TEST_CHECK(geometry_generation != release_generation, true);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) release_generation, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 990U);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) geometry_generation, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 1);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 20.0f);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);

    native_resize_test_mouse_down(CGPointMake(20.0f, 30.0f));
    TEST_CHECK(g_mouse_state.native_resize.edges, HANDLE_LEFT | HANDLE_TOP);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(15.0f, 25.0f, 400.0f, 300.0f), false), true);
    native_resize_test_mouse_up(CGPointMake(15.0f, 25.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(15.0f, 25.0f, 405.0f, 305.0f), true), true);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 2);
    TEST_CHECK(g_mouse_drop_adjust_info.dx, -5.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dy, -5.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 5.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dh, 5.0f);

    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_adapts_post_release_quiescence_to_geometry_state,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 996);

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    uint32_t pre_release_timeout = g_native_resize_scheduled_generation;
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_TIMEOUT_MS);
    native_resize_test_mouse_up(CGPointMake(430.0f, 150.0f));
    uint32_t settled_release = g_native_resize_scheduled_generation;
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_SETTLED_QUIESCENCE_MS);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) pre_release_timeout, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 996U);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) settled_release, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 1);

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    native_resize_test_mouse_up(CGPointMake(420.0f, 150.0f));
    uint32_t no_geometry_grace = g_native_resize_scheduled_generation;
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_TRAILING_GRACE_MS);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    uint32_t first_settled = g_native_resize_scheduled_generation;
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_SETTLED_QUIESCENCE_MS);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 415.0f, 300.0f), true), true);
    uint32_t second_settled = g_native_resize_scheduled_generation;
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_SETTLED_QUIESCENCE_MS);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) no_geometry_grace, 0);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) first_settled, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 996U);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) second_settled, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 2);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 15.0f);

    native_resize_test_mouse_down(CGPointMake(20.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    TEST_CHECK(g_mouse_state.native_resize.provisional, true);
    native_resize_test_mouse_up(CGPointMake(10.0f, 150.0f));
    uint32_t provisional_grace = g_native_resize_scheduled_generation;
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_TRAILING_GRACE_MS);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(10.0f, 30.0f, 410.0f, 300.0f), false), true);
    uint32_t complementary_settled = g_native_resize_scheduled_generation;
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_SETTLED_QUIESCENCE_MS);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) provisional_grace, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 996U);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) complementary_settled, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 3);
    TEST_CHECK(g_mouse_drop_adjust_info.dx, -10.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 10.0f);

    native_resize_test_mouse_down(CGPointMake(20.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(15.0f, 30.0f, 405.0f, 300.0f), true), true);
    native_resize_test_mouse_up(CGPointMake(15.0f, 150.0f));
    uint32_t stale_settled = g_native_resize_scheduled_generation;
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_SETTLED_QUIESCENCE_MS);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(15.0f, 30.0f, 410.0f, 300.0f), true), true);
    uint32_t later_provisional = g_native_resize_scheduled_generation;
    TEST_CHECK(g_mouse_state.native_resize.provisional, true);
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_TRAILING_GRACE_MS);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) stale_settled, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 996U);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) later_provisional, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 3);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);

    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_finalizer_reconciles_newer_live_ax_geometry,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 997);
    g_native_resize_live_frame_override = true;

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    native_resize_test_mouse_up(CGPointMake(430.0f, 150.0f));
    uint32_t ready_settled = g_native_resize_scheduled_generation;
    g_native_resize_live_frame = CGRectMake(20.0f, 30.0f, 420.0f, 300.0f);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) ready_settled, 0);
    uint32_t newer_settled = g_native_resize_scheduled_generation;
    TEST_CHECK(newer_settled != ready_settled, true);
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_SETTLED_QUIESCENCE_MS);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 997U);
    TEST_CHECK(g_mouse_state.native_resize.latest_frame.size.width, 420.0f);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 0);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) ready_settled, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 997U);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) newer_settled, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 1);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 20.0f);

    native_resize_test_mouse_down(CGPointMake(20.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(15.0f, 30.0f, 405.0f, 300.0f), true), true);
    native_resize_test_mouse_up(CGPointMake(15.0f, 150.0f));
    uint32_t ready_before_partial = g_native_resize_scheduled_generation;
    g_native_resize_live_frame = CGRectMake(15.0f, 30.0f, 410.0f, 300.0f);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) ready_before_partial, 0);
    uint32_t provisional_generation = g_native_resize_scheduled_generation;
    TEST_CHECK(g_mouse_state.native_resize.provisional, true);
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_TRAILING_GRACE_MS);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 1);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) ready_before_partial, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 997U);

    g_native_resize_live_frame = CGRectMake(10.0f, 30.0f, 410.0f, 300.0f);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) provisional_generation, 0);
    uint32_t reconciled_generation = g_native_resize_scheduled_generation;
    TEST_CHECK(g_mouse_state.native_resize.provisional, false);
    TEST_CHECK(g_native_resize_scheduled_delay, (uint64_t) NATIVE_RESIZE_SETTLED_QUIESCENCE_MS);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 1);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) provisional_generation, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 997U);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) reconciled_generation, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 2);
    TEST_CHECK(g_mouse_drop_adjust_info.dx, -10.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 10.0f);

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    native_resize_test_mouse_up(CGPointMake(430.0f, 150.0f));
    g_native_resize_live_frame_result = false;
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
        (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
    TEST_CHECK(g_native_resize_restore_count, 1);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 2);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);

    g_native_resize_live_frame_result = true;
    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    native_resize_test_mouse_up(CGPointMake(430.0f, 150.0f));
    g_native_resize_live_frame = CGRectMake(15.0f, 30.0f, 405.0f, 300.0f);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
        (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
    TEST_CHECK(g_native_resize_restore_count, 2);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 2);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);

    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_strict_live_ax_frame_validation_restores_failures,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 998);
    event_loop_test_native_resize_live_frame = NULL;
    event_loop_test_native_resize_ax_copy = test_native_resize_ax_copy;
    event_loop_test_native_resize_ax_get = test_native_resize_ax_get;
    event_loop_test_native_resize_ax_release = test_native_resize_ax_release;
    g_native_resize_ax_frame = CGRectMake(20.0f, 30.0f, 410.0f, 300.0f);

    struct {
        enum native_resize_ax_frame_case test_case;
        int released;
    } failures[] = {
        { NATIVE_RESIZE_AX_FRAME_POSITION_COPY_FAIL, 0 },
        { NATIVE_RESIZE_AX_FRAME_SIZE_COPY_FAIL, 1 },
        { NATIVE_RESIZE_AX_FRAME_WRONG_CF_TYPE, 2 },
        { NATIVE_RESIZE_AX_FRAME_WRONG_AX_TYPE, 2 },
        { NATIVE_RESIZE_AX_FRAME_POINT_GET_FAIL, 2 },
        { NATIVE_RESIZE_AX_FRAME_SIZE_GET_FAIL, 2 },
        { NATIVE_RESIZE_AX_FRAME_NONFINITE, 2 },
        { NATIVE_RESIZE_AX_FRAME_NONPOSITIVE, 2 }
    };
    for (size_t i = 0; i < array_count(failures); ++i) {
        native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
        TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                         g_native_resize_ax_frame, true), true);
        native_resize_test_mouse_up(CGPointMake(430.0f, 150.0f));
        g_native_resize_ax_frame_case = failures[i].test_case;
        g_native_resize_ax_release_count = 0;
        int restore_before = g_native_resize_restore_count;
        int adjust_before = g_mouse_drop_adjust_call_count;
        EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
            (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
        TEST_CHECK(g_native_resize_ax_release_count, failures[i].released);
        TEST_CHECK(g_native_resize_restore_count, restore_before + 1);
        TEST_CHECK(g_mouse_drop_adjust_call_count, adjust_before);
        TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    }

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     g_native_resize_ax_frame, true), true);
    native_resize_test_mouse_up(CGPointMake(430.0f, 150.0f));
    g_native_resize_ax_frame_case = NATIVE_RESIZE_AX_FRAME_VALID;
    g_native_resize_ax_release_count = 0;
    int restore_before = g_native_resize_restore_count;
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
        (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
    TEST_CHECK(g_native_resize_ax_release_count, 2);
    TEST_CHECK(g_native_resize_restore_count, restore_before);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 1);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 10.0f);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);

    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_partial_snapshots_settle_only_after_matching_components,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 994);

    native_resize_test_mouse_down(CGPointMake(20.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    uint32_t left_partial = g_native_resize_scheduled_generation;
    TEST_CHECK(g_mouse_state.native_resize.provisional, true);
    TEST_CHECK(g_mouse_state.native_resize.saw_resize, false);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(10.0f, 30.0f, 410.0f, 300.0f), false), true);
    TEST_CHECK(g_mouse_state.native_resize.provisional, false);
    TEST_CHECK(g_mouse_state.native_resize.saw_resize, true);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) left_partial, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 994U);
    native_resize_test_mouse_up(CGPointMake(10.0f, 150.0f));
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
        (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 1);
    TEST_CHECK(g_mouse_drop_adjust_info.dx, -10.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 10.0f);

    native_resize_test_mouse_down(CGPointMake(200.0f, 30.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 400.0f, 310.0f), true), true);
    native_resize_test_mouse_up(CGPointMake(200.0f, 20.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 20.0f, 400.0f, 310.0f), false), true);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
        (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 2);
    TEST_CHECK(g_mouse_drop_adjust_info.dy, -10.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dh, 10.0f);

    native_resize_test_mouse_down(CGPointMake(20.0f, 30.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 310.0f), true), true);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(10.0f, 20.0f, 410.0f, 310.0f), false), true);
    native_resize_test_mouse_up(CGPointMake(10.0f, 20.0f));
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
        (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 3);
    TEST_CHECK(g_mouse_drop_adjust_info.dx, -10.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dy, -10.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 10.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dh, 10.0f);

    native_resize_test_mouse_down(CGPointMake(20.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    native_resize_test_mouse_up(CGPointMake(10.0f, 150.0f));
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
        (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 3);

    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_later_partial_components_never_commit_stale_settled_frame,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 995);

    for (int ordering = 0; ordering < 4; ++ordering) {
        native_resize_test_mouse_down(CGPointMake(20.0f, 150.0f));
        TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                         CGRectMake(15.0f, 30.0f, 405.0f, 300.0f), true), true);
        if (ordering == 0 || ordering == 2) {
            TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                             CGRectMake(15.0f, 30.0f, 410.0f, 300.0f), true), true);
        }
        if (ordering == 1 || ordering == 3) {
            TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                             CGRectMake(10.0f, 30.0f, 405.0f, 300.0f), false), true);
        }
        if (ordering == 2) {
            TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                             CGRectMake(10.0f, 30.0f, 410.0f, 300.0f), false), true);
        }
        if (ordering == 3) {
            TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                             CGRectMake(15.0f, 30.0f, 410.0f, 300.0f), true), true);
        }
        native_resize_test_mouse_up(CGPointMake(10.0f, 150.0f));
        EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
            (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
        int expected = ordering >= 2 ? ordering - 1 : 0;
        TEST_CHECK(g_mouse_drop_adjust_call_count, expected);
    }
    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_rejects_center_unrelated_and_expired_geometry,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 991);

    native_resize_test_mouse_down(CGPointMake(200.0f, 200.0f));
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 420.0f, 300.0f), true), false);

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(g_mouse_state.native_resize.window_id, 991U);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(15.0f, 30.0f, 405.0f, 300.0f), true), false);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    fixture.node.area.w = 390.0f;
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), false);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    fixture.node.area.w = 400.0f;

    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    native_resize_test_mouse_up(CGPointMake(420.0f, 150.0f));
    uint32_t expired = g_native_resize_scheduled_generation;
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) expired, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 420.0f, 300.0f), true), false);

    fixture.node.window_count = 2;
    fixture.node.window_list[1] = 992;
    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);

    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_all_edges_and_corners_preserve_directional_geometry,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 993);
    fixture.view.auto_balance = SPLIT_X | SPLIT_Y;
    fixture.view.scaffold_fingerprint = 77;
    struct {
        CGPoint down;
        CGRect frame;
        uint8_t edges;
        float dx, dy, dw, dh;
    } cases[] = {
        { { 20.0f, 150.0f },  { { 10.0f, 30.0f }, { 410.0f, 300.0f } }, HANDLE_LEFT, -10.0f, 0.0f, 10.0f, 0.0f },
        { { 420.0f, 150.0f }, { { 20.0f, 30.0f }, { 410.0f, 300.0f } }, HANDLE_RIGHT, 0.0f, 0.0f, 10.0f, 0.0f },
        { { 200.0f, 30.0f },  { { 20.0f, 20.0f }, { 400.0f, 310.0f } }, HANDLE_TOP, 0.0f, -10.0f, 0.0f, 10.0f },
        { { 200.0f, 330.0f }, { { 20.0f, 30.0f }, { 400.0f, 310.0f } }, HANDLE_BOTTOM, 0.0f, 0.0f, 0.0f, 10.0f },
        { { 420.0f, 30.0f },  { { 20.0f, 20.0f }, { 410.0f, 310.0f } }, HANDLE_RIGHT | HANDLE_TOP, 0.0f, -10.0f, 10.0f, 10.0f },
        { { 20.0f, 330.0f },  { { 10.0f, 30.0f }, { 410.0f, 310.0f } }, HANDLE_LEFT | HANDLE_BOTTOM, -10.0f, 0.0f, 10.0f, 10.0f }
    };
    for (size_t i = 0; i < array_count(cases); ++i) {
        native_resize_test_mouse_down(cases[i].down);
        TEST_CHECK(g_mouse_state.native_resize.edges, cases[i].edges);
        TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                         cases[i].frame, true), true);
        native_resize_test_mouse_up(cases[i].down);
        EVENT_HANDLER_NATIVE_RESIZE_FINALIZE(
            (void *) (uintptr_t) g_native_resize_scheduled_generation, 0);
        TEST_CHECK(g_mouse_drop_adjust_call_count, (int) i + 1);
        TEST_CHECK(g_mouse_drop_adjust_info.dx, cases[i].dx);
        TEST_CHECK(g_mouse_drop_adjust_info.dy, cases[i].dy);
        TEST_CHECK(g_mouse_drop_adjust_info.dw, cases[i].dw);
        TEST_CHECK(g_mouse_drop_adjust_info.dh, cases[i].dh);
    }
    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_reschedules_inactivity_and_invalidates_on_config_change,
{
    struct native_resize_test_fixture fixture;
    native_resize_test_begin(&fixture, 992);
    native_resize_test_mouse_down(CGPointMake(420.0f, 150.0f));
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 410.0f, 300.0f), true), true);
    uint32_t first_geometry = g_native_resize_scheduled_generation;
    TEST_CHECK(native_resize_observe(&fixture.window, &fixture.view, &fixture.node,
                                     CGRectMake(20.0f, 30.0f, 415.0f, 300.0f), true), true);
    uint32_t second_geometry = g_native_resize_scheduled_generation;
    TEST_CHECK(second_geometry != first_geometry, true);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) first_geometry, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 992U);

    table_remove(&g_window_manager.managed_window, &fixture.window.id);
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_NATIVE_EDGE_RESIZE,
        &(struct declarative_value) { .present = true, .boolean = false }, 0), DECLARATIVE_APPLY_OK);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    uint32_t cancelled_generation = g_mouse_state.native_resize.generation;
    TEST_CHECK(declarative_apply_runtime_setting(DECLARATIVE_CONFIG_NATIVE_EDGE_RESIZE,
        &(struct declarative_value) { .present = true, .boolean = true }, 0), DECLARATIVE_APPLY_OK);
    EVENT_HANDLER_NATIVE_RESIZE_FINALIZE((void *) (uintptr_t) second_geometry, 0);
    TEST_CHECK(g_mouse_state.native_resize.window_id, 0U);
    TEST_CHECK(g_mouse_state.native_resize.generation != cancelled_generation, true);

    native_resize_test_end(&fixture);
});

TEST_FUNC(native_edge_resize_plain_mouse_events_pass_through_unchanged,
{
    struct event_loop saved_event_loop = g_event_loop;
    struct mouse_state mouse_state = {
        .modifier = MOUSE_MOD_ALT,
        .action1 = MOUSE_MODE_MOVE,
        .action2 = MOUSE_MODE_RESIZE,
        .native_edge_resize = true,
        .active_button = MOUSE_BUTTON_NONE
    };
    struct mouse_handler_context handler_context = { .mouse_state = &mouse_state, .handle = NULL };
    CGEventRef down = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown,
                                              CGPointMake(10.0f, 20.0f), kCGMouseButtonLeft);
    CGEventRef up = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp,
                                            CGPointMake(10.0f, 20.0f), kCGMouseButtonLeft);
    TEST_CHECK(down != NULL, true);
    TEST_CHECK(up != NULL, true);
    if (!down || !up) {
        if (down) CFRelease(down);
        if (up) CFRelease(up);
        return result;
    }

    g_event_loop = (struct event_loop) {0};
    TEST_CHECK(event_queue_init(&g_event_loop.queue, 4), true);
    pthread_mutex_init(&g_event_loop.producer_lock, NULL);
    pthread_cond_init(&g_event_loop.producer_cond, NULL);
    g_event_loop.producer_sync_initialized = true;
    __atomic_store_n(&g_event_loop.producer_admission, EVENT_LOOP_ADMISSION_OPEN, __ATOMIC_RELEASE);
    g_event_loop.queue.wake_pending = true;

    TEST_CHECK(mouse_handler(NULL, kCGEventLeftMouseDown, down, &handler_context) == down, true);
    TEST_CHECK(mouse_state.left_click.consume, false);
    TEST_CHECK(mouse_handler(NULL, kCGEventLeftMouseUp, up, &handler_context) == up, true);
    TEST_CHECK(mouse_state.left_click.consume, false);
    TEST_CHECK(g_event_loop.queue.count, 2U);
    struct event posted = {0};
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &posted), true);
    TEST_CHECK(posted.type, MOUSE_DOWN);
    event_loop_release_event_context(posted.type, posted.context);
    TEST_CHECK(event_queue_pop(&g_event_loop.queue, &posted), true);
    TEST_CHECK(posted.type, MOUSE_UP);
    event_loop_release_event_context(posted.type, posted.context);

    event_loop_close_producer_admission(&g_event_loop);
    pthread_cond_destroy(&g_event_loop.producer_cond);
    pthread_mutex_destroy(&g_event_loop.producer_lock);
    event_queue_destroy(&g_event_loop.queue);
    g_event_loop = saved_event_loop;
    CFRelease(down);
    CFRelease(up);
});

TEST_FUNC(mouse_resize_hybrid_policy_uses_latest_absolute_fractional_geometry,
{
    struct mouse_state saved_mouse_state = g_mouse_state;
    struct table saved_managed_window = g_window_manager.managed_window;
    enum mission_control_mode saved_mission_control_mode = g_mission_control_mode;
    __auto_type saved_resize_callback = event_loop_test_mouse_drag_resize_window;
    __auto_type saved_time_callback = event_loop_test_mouse_drag_time;
    int saved_resize_call_count = g_mouse_drag_resize_call_count;
    struct table unmanaged_window;
    table_init(&unmanaged_window, 1, hash_wm, compare_wm);

    AXUIElementRef window_ref = AXUIElementCreateApplication(getpid());
    struct window grabbed_window = {
        .ref = window_ref,
        .id = 988,
        .frame = { { 20.0f, 30.0f }, { 400.0f, 300.0f } },
        .flags = WINDOW_FLOAT
    };
    grabbed_window.id_ptr = &grabbed_window.id;

    g_window_manager.managed_window = unmanaged_window;
    memset(&g_mouse_state, 0, sizeof(g_mouse_state));
    g_mouse_state.window = &grabbed_window;
    g_mouse_state.window_frame = grabbed_window.frame;
    g_mouse_state.resize_requested_frame = grabbed_window.frame;
    g_mouse_state.down_location = CGPointMake(100.0f, 100.0f);
    g_mouse_state.resize_requested_location = g_mouse_state.down_location;
    g_mouse_state.direction = HANDLE_RIGHT | HANDLE_BOTTOM;
    g_mouse_state.current_action = MOUSE_MODE_RESIZE;
    g_mouse_state.active_button = kCGMouseButtonRight;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
    g_mouse_drag_resize_call_count = 0;
    g_mouse_drag_resize_time = read_os_freq();
    g_mouse_state.resize_requested_time = g_mouse_drag_resize_time;
    event_loop_test_mouse_drag_resize_window = test_mouse_drag_resize_window;
    event_loop_test_mouse_drag_time = test_mouse_drag_time;

    g_mouse_drag_resize_time += read_os_freq() / 50;
    CGEventRef diagonal_dead_zone = CGEventCreateMouseEvent(NULL, kCGEventRightMouseDragged,
                                                            CGPointMake(101.0f, 101.0f), kCGMouseButtonRight);
    TEST_CHECK(diagonal_dead_zone != NULL, true);
    if (diagonal_dead_zone) EVENT_HANDLER_MOUSE_DRAGGED(diagonal_dead_zone, 0);
    TEST_CHECK(g_mouse_drag_resize_call_count, 0);

    CGEventRef fractional_diagonal = CGEventCreateMouseEvent(NULL, kCGEventRightMouseDragged,
                                                             CGPointMake(101.5f, 101.5f), kCGMouseButtonRight);
    TEST_CHECK(fractional_diagonal != NULL, true);
    if (fractional_diagonal) EVENT_HANDLER_MOUSE_DRAGGED(fractional_diagonal, 0);
    TEST_CHECK(g_mouse_drag_resize_call_count, 1);
    TEST_CHECK(g_mouse_drag_resize_frames[0].size.width, 401.5f);
    TEST_CHECK(g_mouse_drag_resize_frames[0].size.height, 301.5f);

    g_mouse_drag_resize_time += read_os_freq() / 1000;
    CGEventRef rate_limited_jump = CGEventCreateMouseEvent(NULL, kCGEventRightMouseDragged,
                                                           CGPointMake(120.0f, 130.0f), kCGMouseButtonRight);
    TEST_CHECK(rate_limited_jump != NULL, true);
    if (rate_limited_jump) EVENT_HANDLER_MOUSE_DRAGGED(rate_limited_jump, 0);
    TEST_CHECK(g_mouse_drag_resize_call_count, 1);

    g_mouse_drag_resize_time += read_os_freq() / 50;
    CGEventRef accepted_latest_jump = CGEventCreateMouseEvent(NULL, kCGEventRightMouseDragged,
                                                              CGPointMake(120.0f, 130.0f), kCGMouseButtonRight);
    TEST_CHECK(accepted_latest_jump != NULL, true);
    if (accepted_latest_jump) EVENT_HANDLER_MOUSE_DRAGGED(accepted_latest_jump, 0);
    TEST_CHECK(g_mouse_drag_resize_call_count, 2);
    TEST_CHECK(g_mouse_drag_resize_frames[1].size.width, 420.0f);
    TEST_CHECK(g_mouse_drag_resize_frames[1].size.height, 330.0f);

    g_mouse_drag_resize_time += read_os_freq() / 50;
    CGEventRef second_accepted = CGEventCreateMouseEvent(NULL, kCGEventRightMouseDragged,
                                                         CGPointMake(122.0f, 132.0f), kCGMouseButtonRight);
    TEST_CHECK(second_accepted != NULL, true);
    if (second_accepted) EVENT_HANDLER_MOUSE_DRAGGED(second_accepted, 0);
    TEST_CHECK(g_mouse_drag_resize_call_count, 3);
    TEST_CHECK(g_mouse_drag_resize_frames[2].size.width, 422.0f);
    TEST_CHECK(grabbed_window.frame.size.width, 400.0f);
    TEST_CHECK(grabbed_window.frame.size.height, 300.0f);

    CGEventRef mouse_up = CGEventCreateMouseEvent(NULL, kCGEventRightMouseUp,
                                                  CGPointMake(122.5f, 132.25f), kCGMouseButtonRight);
    TEST_CHECK(mouse_up != NULL, true);
    if (mouse_up) EVENT_HANDLER_MOUSE_UP(mouse_up, 0);
    TEST_CHECK(g_mouse_drag_resize_call_count, 4);
    TEST_CHECK(g_mouse_drag_resize_frames[3].size.width, 422.5f);
    TEST_CHECK(g_mouse_drag_resize_frames[3].size.height, 332.25f);
    TEST_CHECK(grabbed_window.frame.size.width, 400.0f);
    TEST_CHECK(g_mouse_state.window == NULL, true);
    TEST_CHECK(g_mouse_state.active_button, MOUSE_BUTTON_NONE);

    event_loop_test_mouse_drag_resize_window = saved_resize_callback;
    event_loop_test_mouse_drag_time = saved_time_callback;
    g_mouse_drag_resize_call_count = saved_resize_call_count;
    g_mouse_state = saved_mouse_state;
    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed_window;
    g_mission_control_mode = saved_mission_control_mode;
    if (window_ref) CFRelease(window_ref);
});

TEST_FUNC(mouse_resize_managed_release_uses_synchronous_intended_geometry,
{
    struct mouse_state saved_mouse_state = g_mouse_state;
    struct table saved_managed_window = g_window_manager.managed_window;
    enum mission_control_mode saved_mission_control_mode = g_mission_control_mode;
    __auto_type saved_resize_callback = event_loop_test_mouse_drag_resize_window;
    __auto_type saved_time_callback = event_loop_test_mouse_drag_time;
    __auto_type saved_drop_callback = event_loop_test_mouse_drop_try_adjust_bsp_grid;
    int saved_resize_call_count = g_mouse_drag_resize_call_count;
    int saved_drop_call_count = g_mouse_drop_adjust_call_count;
    struct mouse_window_info saved_drop_info = g_mouse_drop_adjust_info;
    struct table managed_window;
    struct view managed_view = { .layout = VIEW_BSP };
    table_init(&managed_window, 1, hash_wm, compare_wm);

    AXUIElementRef window_ref = AXUIElementCreateApplication(getpid());
    struct window grabbed_window = {
        .ref = window_ref,
        .id = 989,
        .frame = { { 20.0f, 30.0f }, { 400.0f, 300.0f } }
    };
    grabbed_window.id_ptr = &grabbed_window.id;
    table_add(&managed_window, &grabbed_window.id, &managed_view);

    g_window_manager.managed_window = managed_window;
    memset(&g_mouse_state, 0, sizeof(g_mouse_state));
    g_mouse_state.window = &grabbed_window;
    g_mouse_state.window_frame = grabbed_window.frame;
    g_mouse_state.resize_requested_frame = grabbed_window.frame;
    g_mouse_state.down_location = CGPointMake(100.0f, 100.0f);
    g_mouse_state.resize_requested_location = g_mouse_state.down_location;
    g_mouse_state.direction = HANDLE_RIGHT | HANDLE_TOP;
    g_mouse_state.current_action = MOUSE_MODE_RESIZE;
    g_mouse_state.active_button = kCGMouseButtonRight;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
    g_mouse_drag_resize_call_count = 0;
    g_mouse_drop_adjust_call_count = 0;
    g_mouse_drag_resize_time = read_os_freq();
    g_mouse_state.resize_requested_time = g_mouse_drag_resize_time;
    event_loop_test_mouse_drag_resize_window = test_mouse_drag_resize_window;
    event_loop_test_mouse_drag_time = test_mouse_drag_time;
    event_loop_test_mouse_drop_try_adjust_bsp_grid = test_mouse_drop_adjust;

    CGEventRef mouse_up = CGEventCreateMouseEvent(NULL, kCGEventRightMouseUp,
                                                  CGPointMake(105.5f, 97.25f), kCGMouseButtonRight);
    TEST_CHECK(mouse_up != NULL, true);
    if (mouse_up) EVENT_HANDLER_MOUSE_UP(mouse_up, 0);
    TEST_CHECK(g_mouse_drag_resize_call_count, 1);
    TEST_CHECK(g_mouse_drag_resize_frames[0].origin.y, 27.25f);
    TEST_CHECK(g_mouse_drag_resize_frames[0].size.width, 405.5f);
    TEST_CHECK(g_mouse_drag_resize_frames[0].size.height, 302.75f);
    TEST_CHECK(grabbed_window.frame.origin.y, 30.0f);
    TEST_CHECK(grabbed_window.frame.size.width, 400.0f);
    TEST_CHECK(g_mouse_drop_adjust_call_count, 1);
    TEST_CHECK(g_mouse_drop_adjust_info.dx, 0.0f);
    TEST_CHECK(g_mouse_drop_adjust_info.dy, -2.75f);
    TEST_CHECK(g_mouse_drop_adjust_info.dw, 5.5f);
    TEST_CHECK(g_mouse_drop_adjust_info.dh, 2.75f);
    TEST_CHECK(g_mouse_state.window == NULL, true);
    TEST_CHECK(g_mouse_state.active_button, MOUSE_BUTTON_NONE);

    event_loop_test_mouse_drag_resize_window = saved_resize_callback;
    event_loop_test_mouse_drag_time = saved_time_callback;
    event_loop_test_mouse_drop_try_adjust_bsp_grid = saved_drop_callback;
    g_mouse_drag_resize_call_count = saved_resize_call_count;
    g_mouse_drop_adjust_call_count = saved_drop_call_count;
    g_mouse_drop_adjust_info = saved_drop_info;
    g_mouse_state = saved_mouse_state;
    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed_window;
    g_mission_control_mode = saved_mission_control_mode;
    if (window_ref) CFRelease(window_ref);
});

TEST_FUNC(mouse_dragged_unmanaged_window_preserves_owner_gesture,
{
    struct mouse_state saved_mouse_state = g_mouse_state;
    struct table saved_managed_window = g_window_manager.managed_window;
    enum mission_control_mode saved_mission_control_mode = g_mission_control_mode;
    __auto_type saved_move_callback = event_loop_test_mouse_drag_move_window;
    int saved_move_call_count = g_mouse_drag_move_call_count;
    struct window *saved_move_window = g_mouse_drag_move_window;
    struct table unmanaged_window;
    table_init(&unmanaged_window, 1, hash_wm, compare_wm);

    AXUIElementRef window_ref = AXUIElementCreateApplication(getpid());
    struct window grabbed_window = {
        .ref = window_ref,
        .id = 987,
        .frame = { { 20.0f, 30.0f }, { 400.0f, 300.0f } },
        .flags = WINDOW_FLOAT
    };
    grabbed_window.id_ptr = &grabbed_window.id;

    g_window_manager.managed_window = unmanaged_window;
    memset(&g_mouse_state, 0, sizeof(g_mouse_state));
    g_mouse_state.window = &grabbed_window;
    g_mouse_state.window_frame = grabbed_window.frame;
    g_mouse_state.down_location = CGPointMake(40.0f, 50.0f);
    g_mouse_state.current_action = MOUSE_MODE_MOVE;
    g_mouse_state.active_button = kCGMouseButtonLeft;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
    g_mouse_drag_move_call_count = 0;
    g_mouse_drag_move_window = NULL;
    event_loop_test_mouse_drag_move_window = test_mouse_drag_move_window;

    CGEventRef first_drag = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDragged,
                                                    CGPointMake(45.0f, 55.0f), kCGMouseButtonLeft);
    TEST_CHECK(first_drag != NULL, true);
    if (first_drag) EVENT_HANDLER_MOUSE_DRAGGED(first_drag, 0);

    TEST_CHECK(g_mouse_drag_move_call_count, 1);
    TEST_CHECK(g_mouse_drag_move_window == &grabbed_window, true);
    TEST_CHECK(g_mouse_state.window == &grabbed_window, true);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_MOVE);
    TEST_CHECK(g_mouse_state.active_button, kCGMouseButtonLeft);

    CGEventRef second_drag = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDragged,
                                                     CGPointMake(50.0f, 60.0f), kCGMouseButtonLeft);
    TEST_CHECK(second_drag != NULL, true);
    if (second_drag) EVENT_HANDLER_MOUSE_DRAGGED(second_drag, 0);

    TEST_CHECK(g_mouse_drag_move_call_count, 2);
    TEST_CHECK(g_mouse_drag_move_window == &grabbed_window, true);
    TEST_CHECK(g_mouse_state.window == &grabbed_window, true);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_MOVE);
    TEST_CHECK(g_mouse_state.active_button, kCGMouseButtonLeft);

    CGEventRef mouse_up = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp,
                                                  CGPointMake(50.0f, 60.0f), kCGMouseButtonLeft);
    TEST_CHECK(mouse_up != NULL, true);
    if (mouse_up) EVENT_HANDLER_MOUSE_UP(mouse_up, 0);

    TEST_CHECK(g_mouse_state.window == NULL, true);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(g_mouse_state.active_button, MOUSE_BUTTON_NONE);

    event_loop_test_mouse_drag_move_window = saved_move_callback;
    g_mouse_drag_move_call_count = saved_move_call_count;
    g_mouse_drag_move_window = saved_move_window;
    g_mouse_state = saved_mouse_state;
    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed_window;
    g_mission_control_mode = saved_mission_control_mode;
    if (window_ref) CFRelease(window_ref);
});

TEST_FUNC(mouse_drag_owner_button_controls_events_and_reset,
{
    struct window grabbed_window = {0};
    struct mouse_state mouse_state = {
        .window = &grabbed_window,
        .current_action = MOUSE_MODE_MOVE,
        .active_button = kCGMouseButtonLeft
    };

    TEST_CHECK(mouse_drag_event_is_owner(&mouse_state, kCGMouseButtonRight), false);
    TEST_CHECK(mouse_drag_reset_if_owner(&mouse_state, kCGMouseButtonRight), false);
    TEST_CHECK(mouse_state.window == &grabbed_window, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_MOVE);
    TEST_CHECK(mouse_state.active_button, kCGMouseButtonLeft);
    TEST_CHECK(mouse_drag_event_is_owner(&mouse_state, kCGMouseButtonLeft), true);
    TEST_CHECK(mouse_drag_reset_if_owner(&mouse_state, kCGMouseButtonLeft), true);
    TEST_CHECK(mouse_state.window == NULL, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(mouse_state.active_button, MOUSE_BUTTON_NONE);

    mouse_state.window = &grabbed_window;
    mouse_state.current_action = MOUSE_MODE_RESIZE;
    mouse_state.active_button = kCGMouseButtonRight;

    TEST_CHECK(mouse_drag_event_is_owner(&mouse_state, kCGMouseButtonLeft), false);
    TEST_CHECK(mouse_drag_reset_if_owner(&mouse_state, kCGMouseButtonLeft), false);
    TEST_CHECK(mouse_state.window == &grabbed_window, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_RESIZE);
    TEST_CHECK(mouse_state.active_button, kCGMouseButtonRight);
    TEST_CHECK(mouse_drag_event_is_owner(&mouse_state, kCGMouseButtonRight), true);
    TEST_CHECK(mouse_drag_reset_if_owner(&mouse_state, kCGMouseButtonRight), true);
    TEST_CHECK(mouse_state.window == NULL, true);
    TEST_CHECK(mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(mouse_state.active_button, MOUSE_BUTTON_NONE);
});

TEST_FUNC(mouse_drag_reset_clears_feedback_window_action_and_button_state,
{
    struct window_node feedback_node = { .insert_dir = DIR_EAST };
    struct window grabbed_window = {0};
    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;
    struct window *saved_window = g_mouse_state.window;
    enum mouse_mode saved_action = g_mouse_state.current_action;
    int64_t saved_active_button = g_mouse_state.active_button;

    g_mouse_state.feedback_node = &feedback_node;
    g_mouse_state.window = &grabbed_window;
    g_mouse_state.current_action = MOUSE_MODE_MOVE;
    g_mouse_state.active_button = kCGMouseButtonLeft;
    mouse_drag_reset(&g_mouse_state);

    TEST_CHECK(feedback_node.insert_dir, 0);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    TEST_CHECK(g_mouse_state.window == NULL, true);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(g_mouse_state.active_button, MOUSE_BUTTON_NONE);

    g_mouse_state.feedback_node = saved_feedback_node;
    g_mouse_state.window = saved_window;
    g_mouse_state.current_action = saved_action;
    g_mouse_state.active_button = saved_active_button;
});

TEST_FUNC(mouse_drag_feedback_rejects_source_node_and_normalizes_stack_layout,
{
    struct window_node source = { .insert_dir = DIR_EAST };
    struct window_node target = {0};
    struct view bsp = { .layout = VIEW_BSP };
    struct view stack = { .layout = VIEW_STACK };

    TEST_CHECK(mouse_drag_feedback_target_is_valid(&source, &target), true);
    TEST_CHECK(mouse_drag_feedback_target_is_valid(&source, &source), false);
    TEST_CHECK(mouse_drag_feedback_target_is_valid(&source, NULL), false);
    TEST_CHECK(mouse_drag_feedback_target_is_valid(NULL, &target), false);
    TEST_CHECK(mouse_drag_feedback_direction(&bsp, DIR_EAST), DIR_EAST);
    TEST_CHECK(mouse_drag_feedback_direction(&stack, DIR_EAST), STACK);
    TEST_CHECK(mouse_drag_feedback_direction(&stack, 0), 0);

    struct mouse_state state = { .feedback_node = &source };
    mouse_drag_clear_feedback(&state);
    TEST_CHECK(source.insert_dir, 0);
    TEST_CHECK(state.feedback_node == NULL, true);
});

TEST_FUNC(insert_feedback_destroy_clears_mouse_reference,
{
    struct window_node node = {0};
    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;
    g_mouse_state.feedback_node = &node;

    insert_feedback_destroy(&node);

    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    g_mouse_state.feedback_node = saved_feedback_node;
});

TEST_FUNC(insert_feedback_invalid_direction_and_no_action_cleanup,
{
    struct window_node node = {0};
    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;

    g_mouse_state.feedback_node = &node;
    node.insert_dir = 0;
    insert_feedback_show(&node);
    TEST_CHECK(node.feedback_window.id, 0U);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);

    g_mouse_state.feedback_node = &node;
    node.insert_dir = UINT8_MAX;
    insert_feedback_show(&node);
    TEST_CHECK(node.feedback_window.id, 0U);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);

    g_mouse_state.feedback_node = saved_feedback_node;
});

TEST_FUNC(insert_feedback_rekeys_on_stack_order_mutations,
{
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;

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

    table_remove(&g_window_manager.insert_feedback, &new_remove_key);
    table_remove(&g_window_manager.insert_feedback, &a_new_key);
    table_remove(&g_window_manager.insert_feedback, &b_new_key);
    table_remove(&g_window_manager.insert_feedback, &new_reorder_key);
    table_free(&g_window_manager.insert_feedback);
    memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback));

    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
});

TEST_FUNC(insert_feedback_rekeys_collapsed_child_before_free,
{
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;

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

    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;
    g_mouse_state.feedback_node = &child;
    insert_feedback_adopt(&parent, &child);

    uint32_t survivor_key = 52;
    TEST_CHECK(table_find(&g_window_manager.insert_feedback, &survivor_key) == &parent, true);
    TEST_CHECK(child.feedback_window.id, 0);
    TEST_CHECK(g_mouse_state.feedback_node == &parent, true);
    table_remove(&g_window_manager.insert_feedback, &survivor_key);
    memset(&parent.feedback_window, 0, sizeof(parent.feedback_window));

    struct window_node no_window_parent = {0};
    struct window_node no_window_child = {0};
    g_mouse_state.feedback_node = &no_window_child;
    insert_feedback_adopt(&no_window_parent, &no_window_child);
    TEST_CHECK(g_mouse_state.feedback_node == &no_window_parent, true);

    g_mouse_state.feedback_node = saved_feedback_node;
    table_free(&g_window_manager.insert_feedback);
    memset(&g_window_manager.insert_feedback, 0, sizeof(g_window_manager.insert_feedback));

    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
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
    struct window_node *saved_feedback_node = g_mouse_state.feedback_node;
    g_mouse_state.feedback_node = left;

    view_remove_window_node(&view, &src_window);

    struct window_node *resolved_dst_node = view_find_window_node(&view, 62);
    TEST_CHECK(resolved_dst_node == NULL, false);
    TEST_CHECK(resolved_dst_node == cached_dst_node, false);
    TEST_CHECK(resolved_dst_node == view.root, true);
    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    g_mouse_state.feedback_node = saved_feedback_node;

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

TEST_FUNC(mouse_drop_action_warp_capture_includes_added_root_after_source_collapse,
{
    struct window_node removed = {0};
    struct window_node added = {0};
    struct window_node parent = {0};
    struct window_node grandparent = {0};

    added.parent = &parent;
    parent.parent = &grandparent;

    TEST_CHECK(mouse_drop_action_warp_should_capture_add(NULL, &added), true);
    TEST_CHECK(mouse_drop_action_warp_should_capture_add(&removed, &added), true);
    TEST_CHECK(mouse_drop_action_warp_should_capture_add(&added, &added), false);
    TEST_CHECK(mouse_drop_action_warp_should_capture_add(&parent, &added), false);
    TEST_CHECK(mouse_drop_action_warp_should_capture_add(&grandparent, &added), false);
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
    TEST_ENTRY(doctor_healthy_fixture_returns_success)         \
    TEST_ENTRY(scripting_addition_runtime_classification_is_nonmutating_and_versioned) \
    TEST_ENTRY(doctor_failures_and_unknown_privileged_checks_are_classified) \
    TEST_ENTRY(doctor_all_unknown_and_optional_warnings_return_success) \
    TEST_ENTRY(doctor_identity_uses_real_uid_or_valid_nonroot_sudo_uid) \
    TEST_ENTRY(doctor_config_selection_stops_at_preferred_regular_candidate) \
    TEST_ENTRY(doctor_service_requires_canonical_fields_mode_and_executable) \
    TEST_ENTRY(doctor_root_permission_checks_use_resolved_supplementary_groups) \
    TEST_ENTRY(doctor_root_permission_checks_use_effective_target_access) \
    TEST_ENTRY(doctor_effective_access_observes_acl_denies) \
    TEST_ENTRY(doctor_runtime_orchestration_and_exact_cli_are_injectable) \
    TEST_ENTRY(doctor_sa_bundle_absence_and_nested_metadata_are_distinct) \
    TEST_ENTRY(doctor_bootarg_socket_architecture_and_cli_probes_are_bounded) \
    TEST_ENTRY(mission_control_observe_rejects_zero_pid_and_creation_failures) \
    TEST_ENTRY(mission_control_observe_rolls_back_each_notification_failure) \
    TEST_ENTRY(mission_control_observe_publishes_only_complete_success_and_is_repeatable) \
    TEST_ENTRY(mission_control_unobserve_cleans_partial_state_without_boolean_gate) \
    TEST_ENTRY(mission_control_observe_retries_after_transaction_failure) \
    TEST_ENTRY(os_timer_elapsed_is_baseline_independent_and_regression_safe) \
    TEST_ENTRY(mission_control_linearized_contexts_polls_and_side_effects) \
    TEST_ENTRY(mission_control_ingress_epoch_ordering_and_observer_retry) \
    TEST_ENTRY(mission_control_sample_tristate_is_defensive) \
    TEST_ENTRY(mission_control_poll_reservation_and_inconclusive_backoff) \
    TEST_ENTRY(mission_control_rejected_ingress_recovers_without_token_corruption) \
    TEST_ENTRY(mission_control_initial_allocation_fallback_recovers_all_kinds) \
    TEST_ENTRY(mission_control_shutdown_closes_retry_and_restart_admission) \
    TEST_ENTRY(mission_control_real_event_loop_context_ownership) \
    TEST_ENTRY(mission_control_show_a_show_b_delayed_exit_confirms_b) \
    TEST_ENTRY(mission_control_fallback_ring_preserves_order_overflow_and_reuse) \
    TEST_ENTRY(mission_control_fallback_concurrent_cancel_and_drain_is_owned) \
    TEST_ENTRY(display_area_is_in_direction)                   \
    TEST_ENTRY(closest_display_in_direction)                   \
    TEST_ENTRY(display_visible_frame_property_selection_and_serialization_are_explicit) \
    TEST_ENTRY(display_internal_constrained_bounds_retain_menu_bar_placement_adjustment) \
    TEST_ENTRY(display_internal_constrained_bounds_cover_system_ui_and_external_bar) \
    TEST_ENTRY(display_animation_query_excludes_bigsur_and_modern_releases) \
    TEST_ENTRY(window_observe_retries_only_transient_failures) \
    TEST_ENTRY(window_diagnostic_visibility_queries_preserve_unknown_state) \
    TEST_ENTRY(window_serialization_reuses_space_membership_and_preserves_fallbacks) \
    TEST_ENTRY(process_create_allocation_failure_does_not_publish_owned_state) \
    TEST_ENTRY(application_create_rejects_invalid_and_allocation_failure) \
    TEST_ENTRY(window_create_allocation_failure_consumes_retained_reference) \
    TEST_ENTRY(window_subrole_is_unknown)                      \
    TEST_ENTRY(window_is_unknown_does_not_consume_cached_subrole) \
    TEST_ENTRY(window_unobserve_without_application_clears_notifications) \
    TEST_ENTRY(native_resize_shutdown_serializes_final_cancellation_and_restart_state) \
    TEST_ENTRY(event_loop_batched_views_deduplicate_in_first_seen_order) \
    TEST_ENTRY(event_loop_concurrent_shutdown_has_one_owner_and_one_final_cancel) \
    TEST_ENTRY(application_observation_retries_only_transient_required_failures) \
    TEST_ENTRY(missed_focused_window_adoption_is_bounded_per_application_identity) \
    TEST_ENTRY(window_created_replaces_only_same_id_window_elements) \
    TEST_ENTRY(application_launch_and_startup_table_failures_retry_and_recover_through_callers) \
    TEST_ENTRY(window_created_event_ax_ownership_matrix_is_exact) \
    TEST_ENTRY(application_termination_rejects_stale_pid_reuse_identity) \
    TEST_ENTRY(inactive_space_launch_partial_inventory_reprobes_and_removes_real_queue_entry) \
    TEST_ENTRY(inactive_space_launch_inventory_failure_is_deduplicated_and_retried) \
    TEST_ENTRY(inactive_space_launch_overlapping_reservations_are_application_owned) \
    TEST_ENTRY(inactive_space_launch_reserved_one_shot_is_available_to_only_one_window) \
    TEST_ENTRY(inactive_space_launch_reserved_rule_ignores_ordinary_window_created_cleanup) \
    TEST_ENTRY(inactive_space_launch_refresh_cursor_tracks_swap_deleted_identity) \
    TEST_ENTRY(inactive_space_launch_global_probe_budget_round_robins_queue) \
    TEST_ENTRY(inactive_space_launch_retry_survives_finalization_and_rejects_stale_pid_reuse) \
    TEST_ENTRY(inactive_space_launch_window_creation_defers_all_rule_effects_until_retry) \
    TEST_ENTRY(inactive_space_launch_unreserved_one_shots_preserve_ordinary_semantics) \
    TEST_ENTRY(inactive_space_launch_initial_reservation_failures_are_atomic_and_retried) \
    TEST_ENTRY(inactive_space_launch_one_shot_handoff_is_cursor_ordered_and_psn_stable) \
    TEST_ENTRY(inactive_space_launch_termination_cancels_reservation_and_real_queue_entry) \
    TEST_ENTRY(event_loop_zero_initialized_state_rejects_without_queue_access) \
    TEST_ENTRY(declarative_space_create_destroy_and_absent_event_coalesce_reconcile) \
    TEST_ENTRY(declarative_scaffold_membership_add_remove_requests_one_reconcile) \
    TEST_ENTRY(declarative_reconcile_reservation_token_cancellation_is_generation_safe) \
    TEST_ENTRY(declarative_dock_restart_and_system_wake_coalesce_reconcile) \
    TEST_ENTRY(declarative_reconcile_reserves_before_post_and_coalesces) \
    TEST_ENTRY(event_loop_bootstrap_is_first_worker_turn_and_barrier_reports_result) \
    TEST_ENTRY(event_loop_shutdown_waits_for_inflight_posters_and_rejects_after_close) \
    TEST_ENTRY(event_loop_front_switch_helpers_preserve_monotonic_observation_on_delivery_failure) \
    TEST_ENTRY(event_loop_late_pre_admission_post_after_cleanup_releases_context) \
    TEST_ENTRY(event_loop_worker_guards_reject_sync_and_self_shutdown) \
    TEST_ENTRY(event_loop_worker_identity_is_scoped_to_the_supplied_loop) \
    TEST_ENTRY(event_loop_sync_during_thread_create_interval_completes_on_success_and_failure) \
    TEST_ENTRY(event_loop_sync_submission_concurrent_with_shutdown_completes) \
    TEST_ENTRY(event_loop_bootstrap_failure_drains_queued_daemon_messages) \
    TEST_ENTRY(managed_window_correction_bounds_retries_and_rearms_from_signals) \
    TEST_ENTRY(window_removal_clears_focus_and_recycled_identity_state) \
    TEST_ENTRY(window_minimize_events_resolve_stable_ids)       \
    TEST_ENTRY(display_change_rejects_invalid_pairs_before_commit) \
    TEST_ENTRY(ordinary_application_front_switch_no_refresh_preserves_bookkeeping_without_direct_animation_transition) \
    TEST_ENTRY(window_focus_preserves_bookkeeping_without_direct_animation_transition) \
    TEST_ENTRY(window_resize_debounce_preserves_fullscreen_state_transitions) \
    TEST_ENTRY(mouse_follows_focus_ignores_ineligible_auxiliary_transitions) \
    TEST_ENTRY(update_window_notifications_append_grows_past_fixed_boundary) \
    TEST_ENTRY(window_focus_fallback_trigger_policy_and_behavior_are_deterministic) \
    TEST_ENTRY(window_focus_fallback_queue_reservation_coalesces_retries_and_clears) \
    TEST_ENTRY(window_focus_fallback_selector_matches_public_recent_helper) \
    TEST_ENTRY(application_front_switched_owned_identity_rejects_stale_generation_and_pid_reuse) \
    TEST_ENTRY(bsp_split_areas_preserve_truncation_and_handle_extreme_geometry) \
    TEST_ENTRY(single_window_width_ratio_scales_and_combines_with_absolute_ceiling) \
    TEST_ENTRY(single_window_width_ratio_reconciles_stack_membership_transitions) \
    TEST_ENTRY(single_window_width_ratio_reconciles_bsp_split_and_collapse) \
    TEST_ENTRY(insert_feedback_refresh_visits_each_active_node_once) \
    TEST_ENTRY(insert_feedback_geometry_clamps_configured_values_to_safe_rectangles) \
    TEST_ENTRY(legacy_view_and_split_allocations_are_transactional) \
    TEST_ENTRY(space_manager_begin_reports_and_cleans_initialization_failures) \
    TEST_ENTRY(view_find_min_depth_leaf_node_handles_complete_tree_boundary) \
    TEST_ENTRY(view_remove_promotes_internal_sibling_geometry)             \
    TEST_ENTRY(view_insertion_preparation_enforces_single_root_capacity_and_layout) \
    TEST_ENTRY(scratchpad_ordered_query_failure_preserves_automatic_mode) \
    TEST_ENTRY(rule_scratchpad_allocation_failure_preserves_fold_and_one_shot) \
    TEST_ENTRY(rule_scratchpad_application_copy_failure_is_atomic) \
    TEST_ENTRY(window_manager_checked_publications_reject_bucket_failures) \
    TEST_ENTRY(stale_ax_reference_refreshes_and_retries_once) \
    TEST_ENTRY(window_management_rejects_zero_area_helpers_but_preserves_explicit_rules) \
    TEST_ENTRY(window_selector_area_comparison_preserves_fractional_and_zero_geometry) \
    TEST_ENTRY(space_move_pair_stops_rolls_back_and_reports_partial_commit) \
    TEST_ENTRY(window_space_move_prefers_observable_sa_then_verified_fallbacks) \
    TEST_ENTRY(cross_space_node_move_rolls_back_before_tree_mutation) \
    TEST_ENTRY(cross_display_space_swap_is_transactional) \
    TEST_ENTRY(managed_tiling_reserves_publication_before_tree_mutation) \
    TEST_ENTRY(window_reconciliation_prepares_destination_before_detaching_source) \
    TEST_ENTRY(window_reconciliation_preserves_transient_fullscreen_exit_membership) \
    TEST_ENTRY(mouse_no_target_cross_space_transfer_prepares_before_source_mutation) \
    TEST_ENTRY(single_root_transfer_capacity_is_atomic_and_max_minus_one_commits) \
    TEST_ENTRY(window_manager_warp_preparation_failure_preserves_trees) \
    TEST_ENTRY(window_manager_warp_success_state_and_reconcile_matrix) \
    TEST_ENTRY(window_inventory_compares_membership_not_cardinality) \
    TEST_ENTRY(window_manager_remaps_both_stacked_nodes_after_cross_space_swap) \
    TEST_ENTRY(process_table_failures_recover_through_launch_and_startup_callers) \
    TEST_ENTRY(process_front_switch_observation_publishes_identity_before_missing_process_discovery) \
    TEST_ENTRY(process_discovery_retry_generation_rejects_stale_callbacks) \
    TEST_ENTRY(process_discovery_retry_registry_deduplicates_bounds_and_recovers_rejected_slots) \
    TEST_ENTRY(window_node_toggle_zoom_parent_switches_siblings) \
    TEST_ENTRY(config_file_execution_preserves_literal_path)     \
    TEST_ENTRY(safe_exec_reports_child_outcomes)                 \
    TEST_ENTRY(socket_open_sets_cloexec)                         \
    TEST_ENTRY(socket_cloexec_helper_sets_flag_and_rejects_invalid_fd) \
    TEST_ENTRY(socket_address_initializer_enforces_unix_path_boundaries) \
    TEST_ENTRY(scripting_addition_socket_identity_requires_strict_nonroot_uid) \
    TEST_ENTRY(scripting_addition_helpers_propagate_failures_and_use_literal_paths) \
    TEST_ENTRY(plist_xml_validator_accepts_xml_characters_and_rejects_invalid_text) \
    TEST_ENTRY(service_install_publishes_complete_plist_atomically) \
    TEST_ENTRY(utf8_validation_and_json_replacement_are_strict)   \
    TEST_ENTRY(serialization_string_escaping_is_strict)          \
    TEST_ENTRY(window_and_space_serializers_round_trip_safe_escaped_and_absent_strings) \
    TEST_ENTRY(view_stack_window_node_rejects_full_node_without_mutation) \
    TEST_ENTRY(view_find_window_list_expands_for_full_stack_node) \
    TEST_ENTRY(accordion_geometry_matrix_is_bounded_overlapping_and_focus_stable) \
    TEST_ENTRY(accordion_geometry_handles_fractional_and_degenerate_axes) \
    TEST_ENTRY(accordion_single_window_normalizes_only_invalid_selected_axis) \
    TEST_ENTRY(accordion_geometry_large_origins_keep_equal_finite_frames) \
    TEST_ENTRY(accordion_focus_change_updates_and_flushes_once_without_reordering) \
    TEST_ENTRY(accordion_batched_lifecycle_recomputes_before_visibility_and_deduplicates) \
    TEST_ENTRY(accordion_swap_flush_defers_only_when_focus_notification_will_recompute) \
    TEST_ENTRY(accordion_membership_insertion_removal_and_focus_preserve_order) \
    TEST_ENTRY(mouse_click_state_ordinary_drag_does_not_poison_consumed_click) \
    TEST_ENTRY(mouse_click_state_tracks_chorded_buttons_independently) \
    TEST_ENTRY(mouse_action_classifies_buttons_and_modifiers) \
    TEST_ENTRY(mouse_queued_action_is_immutable_across_configuration_changes) \
    TEST_ENTRY(mouse_unclassified_action_does_not_begin_gesture) \
    TEST_ENTRY(native_edge_resize_accepts_mouse_up_before_and_between_geometry_events) \
    TEST_ENTRY(native_edge_resize_adapts_post_release_quiescence_to_geometry_state) \
    TEST_ENTRY(native_edge_resize_finalizer_reconciles_newer_live_ax_geometry) \
    TEST_ENTRY(native_edge_resize_strict_live_ax_frame_validation_restores_failures) \
    TEST_ENTRY(native_edge_resize_partial_snapshots_settle_only_after_matching_components) \
    TEST_ENTRY(native_edge_resize_later_partial_components_never_commit_stale_settled_frame) \
    TEST_ENTRY(native_edge_resize_rejects_center_unrelated_and_expired_geometry) \
    TEST_ENTRY(native_edge_resize_all_edges_and_corners_preserve_directional_geometry) \
    TEST_ENTRY(native_edge_resize_reschedules_inactivity_and_invalidates_on_config_change) \
    TEST_ENTRY(native_edge_resize_plain_mouse_events_pass_through_unchanged) \
    TEST_ENTRY(mouse_resize_hybrid_policy_uses_latest_absolute_fractional_geometry) \
    TEST_ENTRY(mouse_resize_managed_release_uses_synchronous_intended_geometry) \
    TEST_ENTRY(mouse_dragged_unmanaged_window_preserves_owner_gesture) \
    TEST_ENTRY(mouse_drag_owner_button_controls_events_and_reset) \
    TEST_ENTRY(mouse_drag_reset_clears_feedback_window_action_and_button_state) \
    TEST_ENTRY(mouse_drag_feedback_rejects_source_node_and_normalizes_stack_layout) \
    TEST_ENTRY(insert_feedback_destroy_clears_mouse_reference) \
    TEST_ENTRY(insert_feedback_invalid_direction_and_no_action_cleanup) \
    TEST_ENTRY(insert_feedback_rekeys_on_stack_order_mutations) \
    TEST_ENTRY(insert_feedback_rekeys_collapsed_child_before_free) \
    TEST_ENTRY(view_remove_window_node_sibling_collapse_requires_destination_reresolve) \
    TEST_ENTRY(mouse_drop_action_restore_insertion_point_prefers_sibling_for_singleton_source) \
    TEST_ENTRY(mouse_drop_action_warp_capture_includes_added_root_after_source_collapse) \
    TEST_ENTRY(mouse_drop_action_warp_capacity_preflight_rejects_full_stack_destinations) \
    TEST_ENTRY(mouse_drop_action_warp_capacity_preflight_rejects_full_stack_insert_nodes) \
    TEST_ENTRY(mouse_drop_action_rollback_failstop_exits) \
    TEST_ENTRY(space_gesture_encoding)                          \
    TEST_ENTRY(space_gesture_invalid_request)                   \
    TEST_ENTRY(view_padding_sum_widens_before_addition)         \
    TEST_ENTRY(view_bsp_insertion_and_equalization_use_the_space_split_ratio) \
    TEST_ENTRY(imperative_layout_transaction_exhausts_preparation_faults_without_publication) \
    TEST_ENTRY(imperative_float_layout_uses_no_external_inventory_and_removes_mappings) \
    TEST_ENTRY(imperative_global_layout_commits_detached_roots_and_runtime_ownership) \
    TEST_ENTRY(window_inventory_failure_preserves_and_empty_success_clears_membership) \
    TEST_ENTRY(layout_inventory_failure_is_transactional_and_empty_success_applies) \
    TEST_ENTRY(scaffold_identity_is_invalidated_across_rapid_layout_conversions) \
    TEST_ENTRY(scaffold_identity_is_invalidated_for_global_layout_batch) \
    TEST_ENTRY(stack_layout_capacity_preflight_preserves_then_accepts_bounded_inventory) \
    TEST_ENTRY(window_ordered_query_failure_preserves_state)    \
    TEST_ENTRY(window_ordered_out_leaves_the_tree)              \
    TEST_ENTRY(window_ordered_out_stays_unmanaged)              \
    TEST_ENTRY(window_ordered_in_is_tiled_again)                \
    TEST_ENTRY(window_ordered_in_keeps_float)                   \
    TEST_ENTRY(window_reordered_stays_tiled)                    \
    TEST_ENTRY(window_visible_event_while_still_ordered_out)    \
    TEST_ENTRY(macos_version_newer_than_listed)                 \
    TEST_ENTRY(managed_space_topology_parsing_is_type_safe_and_ordered) \
    TEST_ENTRY(managed_space_ids_require_positive_signed_64_bit_values) \
    TEST_ENTRY(managed_display_space_extraction_skips_malformed_entries) \
    TEST_ENTRY(active_display_enumeration_rejects_errors_and_unstable_samples) \
    TEST_ENTRY(temporary_storage_checked_allocation_is_fallible_and_atomic) \
    TEST_ENTRY(temporary_stretch_buffer_growth_charges_only_capacity)       \
    TEST_ENTRY(core_foundation_constructors_fail_conservatively) \
    TEST_ENTRY(display_selection_helpers_are_null_and_type_safe) \
    TEST_ENTRY(display_space_empty_allocation_preserves_flattened_adjacency) \
    TEST_ENTRY(add_and_clamp_to_zero_saturates)                 \
    TEST_ENTRY(event_loop_begin_thread_creation_failure_is_atomic) \
    TEST_ENTRY(event_queue_normal_and_hard_cap_reserve_is_bounded) \
    TEST_ENTRY(event_loop_topology_retry_chain_is_single_and_generation_bound) \
    TEST_ENTRY(event_queue_all_destructive_events_use_hard_reserve) \
    TEST_ENTRY(event_queue_topology_rejections_coalesce_and_rearm_without_admission) \
    TEST_ENTRY(event_loop_topology_recovery_is_transactional_complete_and_retryable) \
    TEST_ENTRY(event_loop_topology_recovery_preserves_identity_through_sid_cycles) \
    TEST_ENTRY(event_loop_topology_double_capture_uuidless_stability_failures_and_ownership) \
    TEST_ENTRY(event_loop_accepted_space_creation_failure_reserves_and_recovers_topology) \
    TEST_ENTRY(event_queue_grows_past_512k_fifo_and_release_once) \
    TEST_ENTRY(event_queue_wrapped_growth_preserves_fifo_and_release_once) \
    TEST_ENTRY(event_queue_merge_releases_replaced_mouse_move_once) \
    TEST_ENTRY(event_queue_adjacent_mouse_drags_coalesce_and_release_replaced_context) \
    TEST_ENTRY(event_queue_different_mouse_button_drags_preserve_fifo) \
    TEST_ENTRY(event_queue_mouse_up_boundary_preserves_drag_fifo) \
    TEST_ENTRY(event_queue_full_releases_owned_context_once)    \
    TEST_ENTRY(window_created_retry_rejection_consumes_context_and_ax_reference_once) \
    TEST_ENTRY(event_queue_full_closes_daemon_message_fd_once)  \
    TEST_ENTRY(event_queue_full_failstops_destructive_events)    \
    TEST_ENTRY(event_queue_binary_wake_converges_after_merges)   \
    TEST_ENTRY(focus_pending_accounting_is_saturating_and_owned) \
    TEST_ENTRY(producer_pending_state_rolls_back_and_retries_after_rejection) \
    TEST_ENTRY(mach_send_uses_bounded_timeout) \
    TEST_ENTRY(animation_payload_packers_bound_batches_and_preserve_order) \
    TEST_ENTRY(jankyborders_generations_supersede_stale_retries) \
    TEST_ENTRY(animation_null_frame_transaction_still_claims_terminal_completion) \
    TEST_ENTRY(animation_proxy_creation_faults_are_non_active_and_unwound) \
    TEST_ENTRY(animation_alpha_restoration_handles_partial_simd_tail) \
    TEST_ENTRY(animation_display_link_setup_stages_propagate_failures) \
    TEST_ENTRY(animation_final_frames_and_failed_owner_pruning) \
    TEST_ENTRY(animation_context_setup_allocation_and_connection_failures_cleanup) \
    TEST_ENTRY(animation_teardown_has_single_non_callback_owner) \
    TEST_ENTRY(animation_out_failure_retains_resources_until_confirmed) \
    TEST_ENTRY(animation_teardown_skips_replaced_ownership) \
    TEST_ENTRY(daemon_message_admission_bounds_and_reuses_slots) \
    TEST_ENTRY(menu_bar_model_defaults_and_zero_counts)          \
    TEST_ENTRY(menu_bar_model_classification_cases)              \
    TEST_ENTRY(menu_bar_model_filter_focus_identity_and_failure_metadata) \
    TEST_ENTRY(menu_bar_model_refocused_clone_preserves_inventory) \
    TEST_ENTRY(menu_bar_model_deep_copy_clone_independence_unicode_and_long_labels) \
    TEST_ENTRY(menu_bar_model_invalid_inputs_and_absent_focus_neutrality) \
    TEST_ENTRY(menu_bar_model_exact_effective_equality_and_focus_only_changes) \
    TEST_ENTRY(menu_bar_manager_config_value_parsing)                  \
    TEST_ENTRY(menu_bar_manager_reuses_collected_window_and_space_state) \
    TEST_ENTRY(menu_bar_manager_topology_snapshot_drives_space_metadata) \
    TEST_ENTRY(menu_bar_manager_snapshot_reuses_native_metadata_per_window) \
    TEST_ENTRY(menu_bar_manager_samples_each_space_type_once)          \
    TEST_ENTRY(menu_bar_manager_app_summary_growth_is_geometric)       \
    TEST_ENTRY(menu_bar_manager_focus_only_refresh_reuses_inventory)   \
    TEST_ENTRY(menu_bar_manager_live_worker_startup_seal_is_ordered_and_single_refresh) \
    TEST_ENTRY(menu_bar_manager_startup_staging_coalesces_until_sealed_and_recovers) \
    TEST_ENTRY(menu_bar_manager_startup_staging_seal_reports_refresh_rejection) \
    TEST_ENTRY(menu_bar_manager_application_inventory_lifecycle_boundaries) \
    TEST_ENTRY(window_manager_scratchpad_refresh_routes_inventory_changes) \
    TEST_ENTRY(menu_bar_manager_ignores_setting_actions_after_disable) \
    TEST_ENTRY(menu_bar_manager_rejects_old_lifecycle_but_accepts_valid_setting_generation) \
    TEST_ENTRY(menu_bar_manager_preserves_action_focus_during_async_menu_open) \
    TEST_ENTRY(menu_bar_manager_late_close_after_disable_does_not_overemit) \
    TEST_ENTRY(menu_bar_manager_raw_membership_is_captured_once_per_space) \
    TEST_ENTRY(menu_bar_manager_raw_and_window_membership_disagreement_is_unknown) \
    TEST_ENTRY(menu_bar_manager_tracked_membership_failure_uses_raw_or_unknown) \
    TEST_ENTRY(message_utf8_ingress_validation)                            \
    TEST_ENTRY(message_scratchpad_recover_requires_no_window)             \
    TEST_ENTRY(message_rule_and_signal_allocation_failures_are_atomic)    \
    TEST_ENTRY(message_rule_and_signal_reject_duplicate_fields)           \
    TEST_ENTRY(message_query_rejects_trailing_arguments)                   \
    TEST_ENTRY(message_query_backend_failures_are_reported)                \
    TEST_ENTRY(message_window_query_reuses_topology_across_serialization)  \
    TEST_ENTRY(message_rule_filter_diagnostic_lists_all_matchers)          \
    TEST_ENTRY(message_mode_domain_is_exclusive_strict_and_json_safe)      \
    TEST_ENTRY(declarative_reload_grammar_rejects_scoped_trailing_and_mixed_before_mutation) \
    TEST_ENTRY(message_space_create_destroy_success_requests_one_reconcile) \
    TEST_ENTRY(declarative_inventory_is_canonical)                         \
    TEST_ENTRY(declarative_jsonc_normalization_preserves_source)           \
    TEST_ENTRY(declarative_jsonc_rejects_encoding_duplicates_and_surrogates) \
    TEST_ENTRY(declarative_json_numeric_domain_table_matches_documented_contract) \
    TEST_ENTRY(declarative_numeric_conversions_are_checked)                \
    TEST_ENTRY(declarative_exact_schema_builds_owned_model)                \
    TEST_ENTRY(declarative_split_ratio_is_valid_globally_and_in_space_policy) \
    TEST_ENTRY(declarative_scaffold_schema_is_strict_and_owned)            \
    TEST_ENTRY(declarative_modes_v1_compatibility_and_v2_schema)            \
    TEST_ENTRY(declarative_modes_schema_errors_and_limits)                  \
    TEST_ENTRY(declarative_modes_composition_is_pure_stable_and_transactional) \
    TEST_ENTRY(declarative_file_loader_enforces_descriptor_contract)       \
    TEST_ENTRY(declarative_primary_action_classifier_is_exclusive)         \
    TEST_ENTRY(declarative_startup_paths_are_absolute_lexical_and_bounded)  \
    TEST_ENTRY(declarative_check_subprocess_has_exact_streams_and_grammar)  \
    TEST_ENTRY(declarative_help_lists_native_jsonc_actions)                 \
    TEST_ENTRY(declarative_daemon_flags_preserve_legacy_and_native_independence) \
    TEST_ENTRY(declarative_startup_selection_obeys_discovery_and_missing_policy) \
    TEST_ENTRY(declarative_enums_are_runtime_values)                       \
    TEST_ENTRY(declarative_window_focus_fallback_schema_is_strict_and_global_only) \
    TEST_ENTRY(declarative_follow_presence_is_validated)                   \
    TEST_ENTRY(declarative_model_allocator_is_transactional)               \
    TEST_ENTRY(declarative_file_ops_cover_detectable_changes)              \
    TEST_ENTRY(declarative_public_paths_are_checked_and_escaped)           \
    TEST_ENTRY(declarative_rule_inventory_drives_runtime_surfaces)         \
    TEST_ENTRY(declarative_generated_limit_boundaries)                     \
    TEST_ENTRY(declarative_schema_paths_are_exact_and_redacted)            \
    TEST_ENTRY(declarative_exact_integral_lexemes_are_enforced)            \
    TEST_ENTRY(declarative_numeric_offsets_scale_with_bounded_traversals)  \
    TEST_ENTRY(declarative_signed_integer_boundaries_are_overflow_safe)     \
    TEST_ENTRY(declarative_positive_integer_boundaries_are_overflow_safe)   \
    TEST_ENTRY(declarative_source_positions_handle_cr_and_crlf)            \
    TEST_ENTRY(declarative_numeric_field_matrix)                           \
    TEST_ENTRY(declarative_phase1_boundary_matrix)                         \
    TEST_ENTRY(declarative_runtime_preparation_covers_all_settings_without_live_mutation) \
    TEST_ENTRY(declarative_command_matrix_covers_all_43_query_invalid_and_mutation_paths) \
    TEST_ENTRY(focus_follows_mouse_query_is_canonical_and_round_trips)       \
    TEST_ENTRY(declarative_imperative_numeric_settings_accept_signed_ints_and_integer_floats) \
    TEST_ENTRY(declarative_all_global_commands_reject_scoped_queries_and_mutations_before_changes) \
    TEST_ENTRY(declarative_scoped_split_ratio_query_and_mutation_own_only_the_selected_space) \
    TEST_ENTRY(declarative_scoped_mixed_batch_uses_one_admitted_pointer_lookup) \
    TEST_ENTRY(space_manager_global_split_ratio_preserves_runtime_space_ownership) \
    TEST_ENTRY(declarative_scoped_mixed_batch_preflights_before_space_mutation) \
    TEST_ENTRY(declarative_scoped_missing_view_rejects_all_runtime_settings_without_side_effects) \
    TEST_ENTRY(declarative_scoped_missing_view_fails_before_query_preflight_or_batch_mutation) \
    TEST_ENTRY(declarative_runtime_live_matrix_covers_all_43_typed_setters)   \
    TEST_ENTRY(imperative_numeric_domain_table_matches_documented_contract)  \
    TEST_ENTRY(documented_numeric_domains_match_runtime_boundaries)           \
    TEST_ENTRY(declarative_animation_preparation_never_prompts_and_preserves_candidate) \
    TEST_ENTRY(declarative_runtime_scalar_application_records_ownership_after_success) \
    TEST_ENTRY(declarative_runtime_operation_failures_do_not_publish_targets_or_ownership) \
    TEST_ENTRY(declarative_space_layout_message_surfaces_use_single_authority_and_preserve_failures) \
    TEST_ENTRY(declarative_accordion_layouts_round_trip_through_imperative_surfaces) \
    TEST_ENTRY(declarative_non_user_space_layout_boundary_preserves_target_and_ownership) \
    TEST_ENTRY(declarative_imperative_failures_preserve_target_and_ownership_with_exact_errors) \
    TEST_ENTRY(declarative_imperative_animation_denial_requests_permission_with_exact_error) \
    TEST_ENTRY(declarative_per_space_ownership_follows_retained_view_identity) \
    TEST_ENTRY(declarative_menu_startup_staging_is_silent_and_sealed)      \
    TEST_ENTRY(declarative_space_resolver_is_fieldwise_and_mode_explicit) \
    TEST_ENTRY(declarative_space_index_policy_is_dormant_until_target_appears) \
    TEST_ENTRY(declarative_split_ratio_resolver_preserves_all_precedence_layers_and_mode_overlay) \
    TEST_ENTRY(declarative_space_resolver_retains_view_ownership_and_preserves_non_user_scalars) \
    TEST_ENTRY(declarative_mode_policy_order_scaffold_inheritance_clear_and_stable_hash) \
    TEST_ENTRY(declarative_scaffold_compacts_subsets_and_ignores_opening_order) \
    TEST_ENTRY(declarative_scaffold_uses_effective_ratio_only_for_its_root_split) \
    TEST_ENTRY(declarative_scaffold_ratio_change_rebuilds_existing_canonical_root) \
    TEST_ENTRY(declarative_scaffold_primary_selection_and_overflow_stability) \
    TEST_ENTRY(declarative_space_capture_is_per_sid_once_owned_and_rejects_duplicates) \
    TEST_ENTRY(declarative_space_root_validation_rejects_shape_order_parent_and_zoom_corruption) \
    TEST_ENTRY(declarative_space_root_validation_accepts_retained_split_child_and_rejects_invalid_domain) \
    TEST_ENTRY(declarative_space_preparation_classifies_removals_and_owns_clear_images_without_live_writes) \
    TEST_ENTRY(declarative_space_target_and_old_table_validation_is_bijective) \
    TEST_ENTRY(declarative_space_before_image_zero_topology_and_window_identity_guards) \
    TEST_ENTRY(declarative_space_heap_root_and_table_topology_negatives) \
    TEST_ENTRY(declarative_space_generated_precedence_matrix_covers_all_fields_and_layer_pairs) \
    TEST_ENTRY(declarative_space_generated_full_chain_and_native_overlap_are_fieldwise) \
    TEST_ENTRY(declarative_space_generated_ownership_matrix_preserves_clears_and_detects_each_mutation) \
    TEST_ENTRY(declarative_space_generated_multi_target_allocator_matrix_is_transactional) \
    TEST_ENTRY(declarative_space_phase3b_same_layout_reconciles_detached_and_preserves_structure) \
    TEST_ENTRY(declarative_space_phase3b_zero_view_insertion_uses_focused_leaf) \
    TEST_ENTRY(declarative_space_phase3b_empty_leaf_collapse_matches_live_parent_semantics) \
    TEST_ENTRY(declarative_space_phase3b_internal_collapse_reparents_and_remaps_zoom) \
    TEST_ENTRY(declarative_space_phase3b_auto_balance_uses_effective_axis_after_same_layout_split) \
    TEST_ENTRY(declarative_space_phase3b_converted_bsp_balances_each_constructed_split) \
    TEST_ENTRY(declarative_space_phase3b_cross_view_owner_table_and_scalar_only_reuse) \
    TEST_ENTRY(declarative_space_phase3b_capacity_is_positive_and_overflow_checked) \
    TEST_ENTRY(declarative_space_phase3b_bsp_split_matches_captured_live_semantics) \
    TEST_ENTRY(declarative_space_phase3b_dynamic_capture_supports_maximum_and_detects_limit_plus_one_duplicate) \
    TEST_ENTRY(declarative_space_phase3b_complete_pipeline_has_indexed_operation_bound) \
    TEST_ENTRY(declarative_space_phase3b_large_changed_layout_is_index_bounded) \
    TEST_ENTRY(declarative_space_phase3b_large_same_layout_reconciliation_is_index_bounded) \
    TEST_ENTRY(declarative_space_phase3b_replacement_table_collision_matrix) \
    TEST_ENTRY(declarative_space_phase3b_detached_destructor_isolation_matrix) \
    TEST_ENTRY(declarative_space_phase3b_large_inventory_has_indexed_operation_bound) \
    TEST_ENTRY(declarative_space_phase3b_generated_production_scenario_matrix) \
    TEST_ENTRY(declarative_space_phase3b_allocation_ordinal_matrix_e) \
    TEST_ENTRY(declarative_space_phase3c_publication_postcommit_and_retirement_are_ordered) \
    TEST_ENTRY(declarative_space_phase3c_stale_scalar_and_event_turn_reject_before_publication) \
    TEST_ENTRY(declarative_space_phase3c_rejects_shared_empty_roots_across_views) \
    TEST_ENTRY(declarative_space_phase3c_generated_stale_before_image_matrix) \
    TEST_ENTRY(declarative_space_phase3c_generated_postcommit_outcome_matrix) \
    TEST_ENTRY(declarative_space_phase3c_ownership_modes_zero_one_multi_view_matrix) \
    TEST_ENTRY(declarative_space_phase3c_lifecycle_rejects_every_repeated_transition) \
    TEST_ENTRY(declarative_space_phase3c_default_view_flush_visible_and_hidden) \
    TEST_ENTRY(declarative_space_phase3c_aliasing_and_retirement_guards) \
    TEST_ENTRY(declarative_space_phase3c_foreign_thread_precommit_is_rejected) \
    TEST_ENTRY(ffm_configured_and_effective_state_handles_nested_suspension_and_latest_updates) \
    TEST_ENTRY(declarative_topology_capture_is_owned_strict_and_coherent) \
    TEST_ENTRY(declarative_topology_uuidless_remap_is_never_admitted) \
    TEST_ENTRY(declarative_topology_uuidless_live_shape_has_private_stable_identity) \
    TEST_ENTRY(declarative_topology_uuidless_swap_keeps_identity_with_logical_space) \
    TEST_ENTRY(declarative_topology_rejects_malformed_incomplete_zero_and_ambiguous_observations) \
    TEST_ENTRY(declarative_topology_active_pair_is_strict_parented_and_stable) \
    TEST_ENTRY(declarative_topology_query_failures_leave_no_partial_capture) \
    TEST_ENTRY(declarative_topology_rejects_duplicates_and_changed_second_observation) \
    TEST_ENTRY(declarative_topology_system_active_display_count_fill_contract_is_strict) \
    TEST_ENTRY(declarative_topology_system_active_pair_is_preinventory_safe) \
    TEST_ENTRY(declarative_topology_candidate_bounds_wrapper_preserves_platform_constraints) \
    TEST_ENTRY(declarative_topology_candidate_external_bar_bounds_match_view_constraints) \
    TEST_ENTRY(declarative_detached_views_use_only_capture_identity_bounds_kind_and_defaults) \
    TEST_ENTRY(declarative_topology_startup_preparation_is_worker_confined_and_transactional) \
    TEST_ENTRY(declarative_topology_production_bootstrap_retains_no_early_candidate) \
    TEST_ENTRY(declarative_topology_allocation_failures_leave_no_owned_state) \
    TEST_ENTRY(declarative_view_adoption_is_atomic_at_every_registry_allocation) \
    TEST_ENTRY(declarative_startup_install_candidate_mapping_covers_all_settings) \
    TEST_ENTRY(declarative_startup_install_retains_unavailable_rules_and_dormant_policy) \
    TEST_ENTRY(declarative_startup_install_publishes_owned_generation_atomically) \
    TEST_ENTRY(declarative_startup_install_failure_preserves_live_managers) \
    TEST_ENTRY(declarative_mode_snapshot_is_worker_only_fresh_owned_and_destroyable) \
    TEST_ENTRY(declarative_reconcile_omits_floating_windows_from_published_management) \
    TEST_ENTRY(declarative_reload_omits_skylight_only_windows_until_discovery) \
    TEST_ENTRY(declarative_native_edge_resize_reload_defaults_and_failures_are_transactional) \
    TEST_ENTRY(declarative_reload_stage_failure_matrix_preserves_installed_state) \
    TEST_ENTRY(declarative_reload_success_is_synchronous_atomic_and_rearms_rules) \
    TEST_ENTRY(declarative_reload_reserves_fresh_native_one_shots_for_pending_refreshes) \
    TEST_ENTRY(declarative_reload_accepts_unavailable_native_targets_atomically) \
    TEST_ENTRY(declarative_reconcile_refreshes_stale_rule_context_without_retargeting) \
    TEST_ENTRY(declarative_reconcile_uuidless_tuple_drift_stales_rules_without_space_transfer) \
    TEST_ENTRY(declarative_reconcile_preserves_live_imperative_bsp_defaults_across_lifecycle_triggers) \
    TEST_ENTRY(declarative_preserve_runtime_topology_uses_effective_external_bar) \
    TEST_ENTRY(declarative_modes_runtime_worker_transaction_and_topology_gate) \
    TEST_ENTRY(declarative_mode_messages_classify_typed_failures_without_spurious_reconcile) \
    TEST_ENTRY(declarative_modes_runtime_stale_topology_matrix) \
    TEST_ENTRY(declarative_modes_runtime_second_topology_gate_and_recovery) \
    TEST_ENTRY(declarative_reconcile_failure_class_diagnostics_are_durable_and_exact) \
    TEST_ENTRY(declarative_reconcile_second_topology_gate_and_recovery) \
    TEST_ENTRY(declarative_modes_runtime_allocation_failure_publication_matrix) \
    TEST_ENTRY(declarative_reconcile_retry_backoff_terminal_and_recovery) \
    TEST_ENTRY(declarative_reload_generation_zero_rediscovers_then_pins_installed_path) \
    TEST_ENTRY(declarative_reporter_bounds_redacts_and_escapes_structural_fields) \
    TEST_ENTRY(declarative_reporter_isolates_sinks_and_uses_literal_bounded_payloads) \
    TEST_ENTRY(declarative_reporter_client_marker_is_inside_the_primary_bound) \
    TEST_ENTRY(declarative_stale_warning_acknowledges_after_sink_failure_and_rearms_by_generation) \
    TEST_ENTRY(declarative_startup_completion_orders_inventory_notifications_menu_socket_and_legacy) \
    TEST_ENTRY(declarative_startup_production_ops_observe_bootstrap_install_before_inventory) \
    TEST_ENTRY(declarative_startup_completion_failure_cutoffs_are_exact) \
    TEST_ENTRY(declarative_rule_topology_is_strict_and_cross_linked) \
    TEST_ENTRY(declarative_rule_maximum_topology_validates_once_and_folds_are_bounded) \
    TEST_ENTRY(declarative_rule_preparation_resolves_orders_fingerprints_and_effects) \
    TEST_ENTRY(declarative_rule_unavailable_targets_preserve_rules_precedence_and_independent_effects) \
    TEST_ENTRY(declarative_rule_preparation_supports_each_matcher_independently) \
    TEST_ENTRY(declarative_rule_preparation_rejects_labels_regex_and_preserves_manager) \
    TEST_ENTRY(declarative_rule_preparation_cleans_every_allocator_failure) \
    TEST_ENTRY(declarative_rule_fold_context_creation_cleans_every_allocator_failure) \
    TEST_ENTRY(declarative_rule_maximum_preparation_and_destruction_tracks_every_allocation) \
    TEST_ENTRY(declarative_native_one_shot_template_survives_live_destruction_and_rearms) \
    TEST_ENTRY(declarative_rule_folding_is_provenance_aware_and_warns_once) \
    TEST_ENTRY(declarative_rule_warning_state_survives_rearm_acknowledges_and_fails_closed) \
    TEST_ENTRY(declarative_rule_apply_warns_only_after_first_matching_root) \
    TEST_ENTRY(declarative_rule_reapply_without_matching_roots_does_not_transition_warning) \
    TEST_ENTRY(declarative_rule_owned_context_is_immutable_and_rejects_stale_targets) \
    TEST_ENTRY(rule_protected_label_messages_are_exact_and_ordered) \
    TEST_ENTRY(rule_label_ownership_results_preserve_runtime_order_and_protect_native) \
    TEST_ENTRY(ffm_menu_suspension_saturates_and_extra_close_is_safe) \
    TEST_ENTRY(ffm_application_menu_termination_releases_only_owned_suspensions) \
    TEST_ENTRY(ffm_config_query_reports_configured_mode_while_effective_is_suspended) \
    TEST_ENTRY(ffm_moved_handler_uses_one_atomic_snapshot_during_mode_change) \
    TEST_ENTRY(ffm_menu_bar_actions_apply_before_causally_queued_mouse_moves) \
    TEST_ENTRY(ffm_menu_bar_tracking_transitions_count_once_without_derived_events) \
    TEST_ENTRY(ffm_mouse_callback_suppresses_disabled_moves_and_posts_enabled_moves_once) \
    TEST_ENTRY(message_numeric_token_boundaries)                          \
    TEST_ENTRY(event_signal_pool_drops_records_when_full_without_overwriting) \
    TEST_ENTRY(event_signal_dispatch_uses_isolated_spawn_environment_and_descriptors) \
    TEST_ENTRY(message_hexadecimal_token_boundaries)                      \
    TEST_ENTRY(message_duration_values_must_be_finite_and_nonnegative)    \
    TEST_ENTRY(message_window_grid_numeric_validation)                    \
    TEST_ENTRY(message_rule_grid_numeric_validation)                      \
    TEST_ENTRY(message_direct_opacity_numeric_validation)                 \
    TEST_ENTRY(message_rule_opacity_numeric_validation)                   \
    TEST_ENTRY(message_external_bar_parser_is_strict)                     \
    TEST_ENTRY(message_space_padding_parser_is_strict)                    \
    TEST_ENTRY(message_space_gap_parser_is_strict)                        \
    TEST_ENTRY(message_window_float_commands_are_strict)                 \
    TEST_ENTRY(daemon_message_signed_boundaries_and_suffixes)             \
    TEST_ENTRY(daemon_message_incomplete_frame_has_failure_response)      \
    TEST_ENTRY(daemon_message_complete_invalid_frames_have_failure_responses) \
    TEST_ENTRY(daemon_identity_macho_uuid_bounds_are_strict)                \
    TEST_ENTRY(daemon_identity_json_contains_protocol_identity)             \
    TEST_ENTRY(daemon_response_buffer_preserves_exact_bytes)              \
    TEST_ENTRY(daemon_response_buffer_accepts_empty_and_boundary_responses) \
    TEST_ENTRY(daemon_response_buffer_overflow_selects_failure_response)  \
    TEST_ENTRY(daemon_io_partial_worker_initialization_rolls_back)       \
    TEST_ENTRY(daemon_io_post_rejection_consumes_owned_job_once)         \
    TEST_ENTRY(daemon_io_all_startup_failures_are_idempotent_and_restartable) \
    TEST_ENTRY(daemon_io_rejects_shutdown_from_event_and_transport_workers) \
    TEST_ENTRY(daemon_io_open_peer_read_and_writer_stalls_observe_injected_deadlines) \
    TEST_ENTRY(daemon_io_response_failures_skip_or_preserve_handler_and_static_fallback) \
    TEST_ENTRY(daemon_io_stale_generation_is_discarded_after_event_loop_restart) \
    TEST_ENTRY(daemon_io_queue_growth_rejection_finalizes_once)            \
    TEST_ENTRY(daemon_io_shutdown_times_out_without_event_progress_then_recovers_after_drain) \
    TEST_ENTRY(daemon_io_concurrent_end_has_single_teardown_owner)          \
    TEST_ENTRY(daemon_io_immediate_reader_finalize_cannot_create_phantom_active_job) \
    TEST_ENTRY(daemon_io_enqueue_lease_blocks_teardown_until_transfer_or_release) \
    TEST_ENTRY(message_loop_accept_errors_retry_backoff_fail_and_recover)   \
    TEST_ENTRY(message_loop_lifecycle_unblocks_accept_and_recovers_from_thread_failure) \
    TEST_ENTRY(message_loop_admission_caps_at_32_and_excess_gets_overload_wire) \
    TEST_ENTRY(daemon_io_allocation_reserve_and_submit_failures_finalize_cleanly) \
    TEST_ENTRY(daemon_io_writer_submit_failure_finalizes_static_fallback_once) \
    TEST_ENTRY(daemon_io_partial_body_never_blocks_event_worker)         \
    TEST_ENTRY(daemon_io_nonreading_peer_never_blocks_event_worker_and_handler_is_event_thread) \
    TEST_ENTRY(daemon_io_invalid_frame_is_rejected_before_handler)       \
    TEST_ENTRY(socket_io_fragmented_exact_read)                           \
    TEST_ENTRY(socket_io_read_retries_interrupted_poll)                   \
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
    TEST_ENTRY(client_response_requires_complete_frame_and_payload)       \
    TEST_ENTRY(sa_handshake_accepts_fragmented_version_and_attributes)    \
    TEST_ENTRY(sa_handshake_accepts_empty_version)                        \
    TEST_ENTRY(sa_handshake_rejects_missing_terminator_at_capacity)       \
    TEST_ENTRY(sa_handshake_rejects_truncated_attributes)                 \
    TEST_ENTRY(sa_handshake_rejects_early_eof)                            \
    TEST_ENTRY(sa_handshake_observes_total_timeout)                       \
    TEST_ENTRY(sa_request_without_status_observes_total_timeout)          \
    TEST_ENTRY(sa_operation_response_has_separate_bounded_budget)         \
    TEST_ENTRY(sa_payload_builder_boundaries_and_variable_counts)         \
    TEST_ENTRY(sa_legacy_hashtable_init_fails_stop_on_allocation_failure) \
    TEST_ENTRY(sa_checked_hashtable_insert_rehashes_and_rolls_back_failures) \
    TEST_ENTRY(sa_message_fixed_sender_layouts_are_strict)                \
    TEST_ENTRY(sa_message_opacity_fade_duration_is_validated)             \
    TEST_ENTRY(sa_sender_validation_precedes_transport_and_valid_boundaries_dispatch_once) \
    TEST_ENTRY(sa_message_layer_and_order_modes_are_exact)                \
    TEST_ENTRY(sa_fade_generation_retries_stale_blocked_getter_failure)   \
    TEST_ENTRY(sa_fade_generation_retries_stale_blocked_setter_failure)   \
    TEST_ENTRY(sa_message_scale_geometry_and_derived_boundaries)          \
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
    if (pthread_mutex_init(&g_window_manager.window_animations_lock, NULL) != 0) {
        fprintf(stderr, "failed to initialize animation test mutex\n");
        return EXIT_FAILURE;
    }
    g_test_window_animations_lock_initialized = true;

    int succeeded = 0;
    int failed = 0;
    int total = array_count(tests);
    printf("\e[1;34m -- Running %d tests\e[m\n\n", total);

    uint64_t cpu_freq  = read_cpu_freq();
    uint64_t begin_tsc = read_cpu_timer();

    for (int i = 0; i < total; ++i) {
        uint64_t tsc = read_cpu_timer();
        bool result;
        @autoreleasepool {
            result = tests[i].func();
        }
        double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - tsc) / (double)cpu_freq;

        printf("(%0.4fms) %s \e[1;33m%s\e[m\n", ms_elapsed, result ? "\e[1;32msuccess\e[m" : " \e[1;31mfailed\e[m", tests[i].name);
        if (result) ++succeeded; else ++failed;
    }

    test_suite_teardown();

    double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - begin_tsc) / (double)cpu_freq;
    printf("\n\e[1;34m -- Completed (%0.4fms)\e[m\n", ms_elapsed);
    printf("\t%d \e[1;32msucceeded\e[m\n", succeeded);
    printf("\t%d \e[1;31mfailed\e[m\n", failed);
    printf("\t%d \e[1;33mtotal\e[m\n", total);

    return total == succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
