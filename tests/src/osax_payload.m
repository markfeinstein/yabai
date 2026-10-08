#include <Foundation/Foundation.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TESTS 1
#include "../../src/osax/payload.m"

struct alpha_control {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int get_calls;
    int set_calls;
    int fail_get_call;
    int fail_set_call;
    bool block_get[4];
    bool release_get[4];
    bool entered_get[4];
    bool block_set[4];
    bool release_set[4];
    bool entered_set[4];
    float set_alpha[4];
};

static struct alpha_control alpha_control;
static int failures;

@interface PayloadOwnedIvarHolder : NSObject {
    id _currentSpace;
}
@end
@implementation PayloadOwnedIvarHolder
@end

@interface PayloadTestSpace : NSObject {
    uint64_t _spaceID;
}
- (instancetype)initWithID:(uint64_t)spaceID;
- (uint64_t)spid;
@end
@implementation PayloadTestSpace
- (instancetype)initWithID:(uint64_t)spaceID
{
    self = [super init];
    if (self) _spaceID = spaceID;
    return self;
}
- (uint64_t)spid { return _spaceID; }
@end

#if __arm64__
@interface PayloadResolverCandidate : NSObject {
    id _displaySpaces;
}
- (id)currentSpaceForDisplayUUID:(id)value;
- (id)spacesForDisplay:(id)value;
@end
@implementation PayloadResolverCandidate
- (id)currentSpaceForDisplayUUID:(id)value { return value; }
- (id)spacesForDisplay:(id)value { return value; }
@end

static struct runtime_vm_region resolver_test_region;
static uint64_t resolver_text_start, resolver_text_end;
static uint64_t resolver_data_start, resolver_data_end;
static int resolver_region_calls;
static int resolver_class_list_mode;
static Class resolver_returned_class;
static Class resolver_listed_class;
static int resolver_retain_calls;
static int resolver_log_calls;
static int resolver_object_get_class_calls;

static bool test_segment_bounds(const char *name, uint64_t *start, uint64_t *end)
{
    if (strcmp(name, "__TEXT") == 0) {
        *start = resolver_text_start;
        *end = resolver_text_end;
        return true;
    }
    if (strcmp(name, "__DATA") == 0 || strcmp(name, "__DATA_CONST") == 0) {
        *start = resolver_data_start;
        *end = resolver_data_end;
        return true;
    }
    return false;
}

static bool test_vm_region(uint64_t address, struct runtime_vm_region *region)
{
    (void) address;
    ++resolver_region_calls;
    *region = resolver_test_region;
    return true;
}

static bool test_vm_region_failure(uint64_t address, struct runtime_vm_region *region)
{
    (void) address; (void) region;
    return false;
}

static int test_class_list(Class *classes, int count)
{
    if (!classes) return resolver_class_list_mode == 1 ? 1 : 2;
    if (resolver_class_list_mode == 2) return count + 1;
    if (resolver_class_list_mode == 3) return -1;
    if (count > 0) classes[0] = resolver_listed_class;
    return 1;
}

static void *test_calloc_failure(size_t count, size_t size)
{
    (void) count; (void) size;
    return NULL;
}

static Class test_object_class(id object)
{
    (void) object;
    ++resolver_object_get_class_calls;
    return resolver_returned_class;
}

static bool test_isa_mask(uintptr_t *mask)
{
    uintptr_t *symbol = (uintptr_t *) dlsym(RTLD_DEFAULT, "objc_debug_isa_class_mask");
    if (!symbol) return false;
    *mask = *symbol;
    return *mask != 0;
}

static id test_candidate_retain(id candidate)
{
    ++resolver_retain_calls;
    return candidate;
}

static void test_candidate_log(Class candidate_class)
{
    (void) candidate_class;
    ++resolver_log_calls;
}

static uint32_t make_adrp(int64_t pages, uint32_t reg)
{
    uint64_t immediate = (uint64_t) pages & 0x1fffff;
    return 0x90000000u | (uint32_t) ((immediate & 3) << 29) |
           (uint32_t) (((immediate >> 2) & 0x7ffff) << 5) | reg;
}

static uint32_t make_add(uint32_t immediate, bool shift, uint32_t reg)
{
    return 0x91000000u | (shift ? 1u << 22 : 0) | (immediate << 10) |
           (reg << 5) | reg;
}

static void reset_resolver_hooks(void)
{
    payload_test_segment_bounds = test_segment_bounds;
    payload_test_vm_region = test_vm_region;
    payload_test_object_get_class = NULL;
    payload_test_get_class_list = NULL;
    payload_test_calloc = NULL;
    payload_test_candidate_retain = NULL;
    payload_test_candidate_log = NULL;
    payload_test_isa_mask = test_isa_mask;
    resolver_region_calls = 0;
    resolver_retain_calls = 0;
    resolver_log_calls = 0;
    resolver_object_get_class_calls = 0;
}
#endif

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        ++failures; \
    } \
} while (0)

static void alpha_control_reset(void)
{
    memset(&alpha_control, 0, sizeof(alpha_control));
    pthread_mutex_init(&alpha_control.lock, NULL);
    pthread_cond_init(&alpha_control.condition, NULL);
    for (int i = 0; i < 4; ++i) {
        alpha_control.release_get[i] = true;
        alpha_control.release_set[i] = true;
    }
}

static void alpha_control_destroy(void)
{
    pthread_cond_destroy(&alpha_control.condition);
    pthread_mutex_destroy(&alpha_control.lock);
}

static CGError test_get_alpha(int cid, uint32_t wid, float *alpha)
{
    (void) cid;
    (void) wid;
    pthread_mutex_lock(&alpha_control.lock);
    int call = ++alpha_control.get_calls;
    if (call < 4) {
        alpha_control.entered_get[call] = true;
        pthread_cond_broadcast(&alpha_control.condition);
        while (alpha_control.block_get[call] && !alpha_control.release_get[call]) {
            pthread_cond_wait(&alpha_control.condition, &alpha_control.lock);
        }
    }
    bool fail = call == alpha_control.fail_get_call;
    pthread_mutex_unlock(&alpha_control.lock);
    if (!fail) *alpha = 0.25f;
    return fail ? kCGErrorFailure : kCGErrorSuccess;
}

static CGError test_set_alpha(int cid, uint32_t wid, float alpha)
{
    (void) cid;
    (void) wid;
    pthread_mutex_lock(&alpha_control.lock);
    int call = ++alpha_control.set_calls;
    if (call < 4) {
        alpha_control.set_alpha[call] = alpha;
        alpha_control.entered_set[call] = true;
        pthread_cond_broadcast(&alpha_control.condition);
        while (alpha_control.block_set[call] && !alpha_control.release_set[call]) {
            pthread_cond_wait(&alpha_control.condition, &alpha_control.lock);
        }
    }
    bool fail = call == alpha_control.fail_set_call;
    pthread_mutex_unlock(&alpha_control.lock);
    return fail ? kCGErrorFailure : kCGErrorSuccess;
}

static bool wait_for_flag(bool *flag)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 3;
    pthread_mutex_lock(&alpha_control.lock);
    int wait_result = 0;
    while (!*flag && wait_result == 0) {
        wait_result = pthread_cond_timedwait(&alpha_control.condition, &alpha_control.lock, &deadline);
    }
    bool reached = *flag;
    pthread_mutex_unlock(&alpha_control.lock);
    return reached;
}

static bool wait_for_get(int call)
{
    return wait_for_flag(&alpha_control.entered_get[call]);
}

static bool wait_for_set(int call)
{
    return wait_for_flag(&alpha_control.entered_set[call]);
}

static void release_get(int call)
{
    pthread_mutex_lock(&alpha_control.lock);
    alpha_control.release_get[call] = true;
    pthread_cond_broadcast(&alpha_control.condition);
    pthread_mutex_unlock(&alpha_control.lock);
}

static void release_set(int call)
{
    pthread_mutex_lock(&alpha_control.lock);
    alpha_control.release_set[call] = true;
    pthread_cond_broadcast(&alpha_control.condition);
    pthread_mutex_unlock(&alpha_control.lock);
}

static bool retarget_direct(uint32_t wid, float alpha)
{
    char message[sizeof(wid) + 2 * sizeof(float)];
    float duration = 0.0f;
    memcpy(message, &wid, sizeof(wid));
    memcpy(message + sizeof(wid), &alpha, sizeof(alpha));
    memcpy(message + sizeof(wid) + sizeof(alpha), &duration, sizeof(duration));
    return do_window_opacity(message);
}

static struct window_fade_context *start_context(uint32_t wid, float alpha, pthread_t *thread)
{
    struct window_fade_context *context = calloc(1, sizeof(*context));
    if (!context) return NULL;
    context->wid = wid;
    context->alpha = alpha;
    context->duration = 0.001f;
    context->generation = 1;
    if (!table_try_add(&window_fade_table, &wid, context) ||
        pthread_create(thread, NULL, window_fade_thread_proc, context) != 0) {
        table_remove(&window_fade_table, &wid);
        free(context);
        return NULL;
    }
    return context;
}

static void prepare_fade_test(void)
{
    memset(&window_fade_table, 0, sizeof(window_fade_table));
    CHECK(pthread_mutex_init(&window_fade_lock, NULL) == 0);
    CHECK(table_try_init(&window_fade_table, 8, hash_wid, compare_wid));
    window_fade_get_alpha = test_get_alpha;
    window_fade_set_alpha = test_set_alpha;
    alpha_control_reset();
}

static void finish_fade_test(void)
{
    table_free(&window_fade_table);
    pthread_mutex_destroy(&window_fade_lock);
    alpha_control_destroy();
}

static void test_retarget_during_blocked_getter_failure(void)
{
    prepare_fade_test();
    alpha_control.block_get[1] = true;
    alpha_control.release_get[1] = false;
    alpha_control.fail_get_call = 1;
    alpha_control.block_get[2] = true;
    alpha_control.release_get[2] = false;

    uint32_t wid = 101;
    pthread_t thread;
    CHECK(start_context(wid, 0.4f, &thread) != NULL);
    CHECK(wait_for_get(1));
    CHECK(retarget_direct(wid, 0.8f));
    release_get(1);
    CHECK(wait_for_get(2));
    CHECK(table_find(&window_fade_table, &wid) != NULL);
    release_get(2);
    pthread_join(thread, NULL);
    CHECK(alpha_control.set_calls == 1);
    CHECK(alpha_control.set_alpha[1] == 0.8f);
    CHECK(table_find(&window_fade_table, &wid) == NULL);
    finish_fade_test();
}

static void test_retarget_during_blocked_setter_failure(void)
{
    prepare_fade_test();
    alpha_control.block_set[1] = true;
    alpha_control.release_set[1] = false;
    alpha_control.fail_set_call = 1;
    alpha_control.block_get[2] = true;
    alpha_control.release_get[2] = false;

    uint32_t wid = 102;
    pthread_t thread;
    CHECK(start_context(wid, 0.4f, &thread) != NULL);
    CHECK(wait_for_set(1));
    CHECK(retarget_direct(wid, 0.7f));
    release_set(1);
    CHECK(wait_for_get(2));
    CHECK(table_find(&window_fade_table, &wid) != NULL);
    release_get(2);
    pthread_join(thread, NULL);
    CHECK(alpha_control.set_calls == 2);
    CHECK(alpha_control.set_alpha[2] == 0.7f);
    CHECK(table_find(&window_fade_table, &wid) == NULL);
    finish_fade_test();
}

static void test_retarget_after_final_frame_restarts(void)
{
    prepare_fade_test();
    alpha_control.block_set[1] = true;
    alpha_control.release_set[1] = false;
    alpha_control.block_get[2] = true;
    alpha_control.release_get[2] = false;

    uint32_t wid = 103;
    pthread_t thread;
    CHECK(start_context(wid, 0.4f, &thread) != NULL);
    CHECK(wait_for_set(1));
    CHECK(retarget_direct(wid, 0.9f));
    release_set(1);
    CHECK(wait_for_get(2));
    CHECK(table_find(&window_fade_table, &wid) != NULL);
    release_get(2);
    pthread_join(thread, NULL);
    CHECK(alpha_control.set_calls == 2);
    CHECK(alpha_control.set_alpha[2] == 0.9f);
    CHECK(table_find(&window_fade_table, &wid) == NULL);
    finish_fade_test();
}

struct handle_context {
    int fd;
    char message[13];
    bool result;
};

static bool delayed_operation_result;
static bool delayed_operation(enum sa_opcode op, char *message)
{
    (void) message;
    CHECK(op == SA_OPCODE_WINDOW_MOVE);
    usleep(1200000);
    return delayed_operation_result;
}

static void *run_handle_message(void *data)
{
    struct handle_context *context = data;
    context->result = handle_message(context->fd, context->message, sizeof(context->message));
    return NULL;
}

static void test_handle_message_fresh_status_deadline(void)
{
    payload_test_operation_dispatch = delayed_operation;
    for (int succeeded = 0; succeeded <= 1; ++succeeded) {
        int fd[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
        int no_sigpipe = 1;
        CHECK(setsockopt(fd[0], SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) == 0);
        struct handle_context context = { .fd = fd[0], .message = { SA_OPCODE_WINDOW_MOVE }, .result = false };
        uint32_t wid = 1;
        memcpy(context.message + 1, &wid, sizeof(wid));
        delayed_operation_result = succeeded != 0;
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, run_handle_message, &context) == 0);
        uint8_t status = 2;
        CHECK(socket_io_read_exact_until(fd[1], &status, sizeof(status), socket_io_deadline_after_ms(2500)));
        CHECK(status == (uint8_t) succeeded);
        pthread_join(thread, NULL);
        CHECK(context.result);
        close(fd[0]);
        close(fd[1]);
    }
    payload_test_operation_dispatch = NULL;
}

#if __arm64__
static void test_resolver_scan_bounds(void)
{
    uint32_t words[4] = {};
    uint64_t start = (uint64_t) words;
    uint64_t end;
    reset_resolver_hooks();
    resolver_text_start = start;
    resolver_text_end = start + sizeof(words);
    resolver_test_region = (struct runtime_vm_region) { start, start + sizeof(words), VM_PROT_READ | VM_PROT_EXECUTE };

    CHECK(resolver_scan_bounds(start, &end) && end == start + sizeof(words));
    CHECK(scan_dock_spaces_method(start + sizeof(words) - 4) == nil);
    CHECK(scan_dock_spaces_method(start + sizeof(words) - 8) == nil);
    CHECK(scan_dock_spaces_method(start + sizeof(words) - 12) == nil);
    CHECK(scan_dock_spaces_method(start + sizeof(words)) == nil);

    resolver_test_region.protection = VM_PROT_READ;
    CHECK(!resolver_scan_bounds(start, &end));
    resolver_test_region.protection = VM_PROT_EXECUTE;
    CHECK(!resolver_scan_bounds(start, &end));
    payload_test_vm_region = test_vm_region_failure;
    CHECK(!resolver_scan_bounds(start, &end));

    payload_test_vm_region = test_vm_region;
    resolver_test_region = (struct runtime_vm_region) { start + 4, start + sizeof(words), VM_PROT_READ | VM_PROT_EXECUTE };
    CHECK(!resolver_scan_bounds(start, &end));
    resolver_test_region = (struct runtime_vm_region) { start, UINT64_MAX, VM_PROT_READ | VM_PROT_EXECUTE };
    resolver_text_start = start;
    resolver_text_end = UINT64_MAX;
    CHECK(!resolver_scan_bounds(UINT64_MAX - 0x100, &end));
    resolver_text_start = start + 4;
    CHECK(!resolver_scan_bounds(start, &end));

    resolver_text_start = start;
    resolver_text_end = start + 12;
    resolver_test_region = (struct runtime_vm_region) { start, start + 12, VM_PROT_READ | VM_PROT_EXECUTE };
    CHECK(resolver_scan_bounds(start, &end) && end == start + 12);
    CHECK(scan_dock_spaces_method(start) == nil);
}

static void test_resolver_decode(void)
{
    uint64_t result, end;
    CHECK(apply_slide_and_size(0x1000, 0x100, 0x2000, &result, &end) && result == 0x3000 && end == 0x3100);
    CHECK(apply_slide_and_size(0x3000, 0x100, -0x2000, &result, &end) && result == 0x1000 && end == 0x1100);
    CHECK(!apply_slide_and_size(UINT64_MAX, 1, 1, &result, &end));
    CHECK(!apply_slide_and_size(0, 1, -1, &result, &end));
    CHECK(!apply_slide_and_size(UINT64_MAX - 1, 2, 0, &result, &end));
    CHECK(!checked_add_u64(UINT64_MAX, 1, &result));
    CHECK(decode_adrp_add_address_checked(0x100123, make_adrp(2, 1), make_add(3, false, 1), &result));
    CHECK(result == 0x102003);
    CHECK(decode_adrp_add_address_checked(0x100123, make_adrp(-2, 1), make_add(3, false, 1), &result));
    CHECK(result == 0xfe003);
    CHECK(decode_adrp_add_address_checked(0x100123, make_adrp(0, 1), make_add(2, true, 1), &result));
    CHECK(result == 0x102000);

    CHECK(decode_adrp_add_address_checked(0x100000123ULL, make_adrp(-0x100000, 1), make_add(0, false, 1), &result));
    CHECK(result == 0);
    CHECK(!decode_adrp_add_address_checked(0xfffff123ULL, make_adrp(-0x100000, 1), make_add(0, false, 1), &result));
    CHECK(decode_adrp_add_address_checked(0x123, make_adrp(0xfffff, 1), make_add(0, false, 1), &result));
    CHECK(result == 0xfffff000ULL);
    CHECK(decode_adrp_add_address_checked(0xffffffff00000000ULL, make_adrp(0xfffff, 1), make_add(0, false, 1), &result));
    CHECK(result == 0xfffffffffffff000ULL);
    CHECK(!decode_adrp_add_address_checked(0xffffffff00001000ULL, make_adrp(0xfffff, 1), make_add(0, false, 1), &result));

    CHECK(!decode_adrp_add_address_checked(0x100, make_adrp(-1, 1), make_add(0, false, 1), &result));
    CHECK(!decode_adrp_add_address_checked(UINT64_MAX - 0xfff, make_adrp(1, 1), make_add(0, false, 1), &result));
    CHECK(!decode_adrp_add_address_checked(UINT64_MAX - 0xfff, make_adrp(0, 1), make_add(1, true, 1), &result));
    CHECK(decode_ldr_pointer_address_checked(0x1000, 1u << 10, &result) && result == 0x1008);
    CHECK(!decode_ldr_pointer_address_checked(UINT64_MAX - 7, 1u << 10, &result));
}

static void test_resolver_spans_and_classes(void)
{
    uintptr_t storage[2] = {};
    uint64_t address = (uint64_t) storage;
    reset_resolver_hooks();
    resolver_test_region = (struct runtime_vm_region) { address, address + sizeof(void *), VM_PROT_READ };
    resolver_data_start = address;
    resolver_data_end = address + sizeof(storage);
    CHECK(span_in_region(address, sizeof(void *), VM_PROT_READ));
    CHECK(!span_in_region(address, sizeof(storage), VM_PROT_READ));
    CHECK(span_in_main_segment(address, sizeof(void *), "__DATA"));
    resolver_data_end = address + sizeof(void *) - 1;
    CHECK(!span_in_main_segment(address, sizeof(void *), "__DATA"));

    PayloadResolverCandidate *candidate = [PayloadResolverCandidate new];
    address = (uint64_t) candidate;
    resolver_test_region = (struct runtime_vm_region) { address, address + sizeof(void *), VM_PROT_READ };
    payload_test_object_get_class = test_object_class;
    payload_test_get_class_list = test_class_list;
    resolver_returned_class = [PayloadResolverCandidate class];
    resolver_listed_class = resolver_returned_class;
    resolver_class_list_mode = 0;
    CHECK(dock_spaces_candidate_is_valid(candidate));
    CHECK(resolver_object_get_class_calls == 1);

    uintptr_t fake_candidate = (uintptr_t) 0x12345678;
    resolver_test_region = (struct runtime_vm_region) {
        (uint64_t) &fake_candidate,
        (uint64_t) &fake_candidate + sizeof(fake_candidate),
        VM_PROT_READ
    };
    resolver_object_get_class_calls = 0;
    CHECK(!dock_spaces_candidate_is_valid((id) &fake_candidate));
    CHECK(resolver_object_get_class_calls == 0);
    resolver_test_region = (struct runtime_vm_region) { address, address + sizeof(void *), VM_PROT_READ };

    struct {
        uint32_t instructions[4];
        id globals[2];
    } fixture = {};
    uint64_t method_address = (uint64_t) fixture.instructions;
    uint64_t base_address = (uint64_t) &fixture.globals[0];
    uint64_t global_address = (uint64_t) &fixture.globals[1];
    int64_t pages = (int64_t) ((base_address & ~0xfffULL) - (method_address & ~0xfffULL)) / 0x1000;
    uint32_t valid_adrp = make_adrp(pages, 1);
    uint32_t valid_add = make_add((uint32_t) (base_address & 0xfff), false, 1);
    uint32_t valid_ldr = 0xf9400000u | (1u << 10) | (1u << 5);
    fixture.instructions[0] = valid_adrp;
    fixture.instructions[1] = valid_add;
    fixture.instructions[2] = valid_ldr;
    fixture.globals[1] = candidate;
    resolver_text_start = method_address;
    resolver_text_end = method_address + 12;
    resolver_data_start = global_address;
    resolver_data_end = global_address + sizeof(id);
    resolver_test_region = (struct runtime_vm_region) { 0, UINT64_MAX, VM_PROT_READ | VM_PROT_EXECUTE };
    CHECK(scan_dock_spaces_method(method_address) == candidate);

    fixture.instructions[1] = valid_add | (1u << 23);
    CHECK(scan_dock_spaces_method(method_address) == nil);
    fixture.instructions[1] = (valid_add & ~0x1fu) | 2u;
    CHECK(scan_dock_spaces_method(method_address) == nil);
    fixture.instructions[1] = (valid_add & ~(0x1fu << 5)) | (2u << 5) | 1u;
    CHECK(scan_dock_spaces_method(method_address) == nil);
    fixture.instructions[1] = valid_add;
    fixture.instructions[2] = (valid_ldr & ~(0x1fu << 5)) | (2u << 5);
    CHECK(scan_dock_spaces_method(method_address) == nil);
    fixture.instructions[2] = valid_ldr | 2u;
    CHECK(scan_dock_spaces_method(method_address) == nil);
    fixture.instructions[0] = make_adrp(pages, 31);
    fixture.instructions[1] = make_add((uint32_t) (base_address & 0xfff), false, 31);
    fixture.instructions[2] = 0xf9400000u | (1u << 10) | (31u << 5);
    CHECK(scan_dock_spaces_method(method_address) == nil);
    fixture.instructions[0] = valid_adrp;
    fixture.instructions[1] = valid_add;
    fixture.instructions[2] = valid_ldr;

    payload_test_candidate_retain = test_candidate_retain;
    payload_test_candidate_log = test_candidate_log;
    CHECK(!macos27_allows_legacy_dock_spaces((NSOperatingSystemVersion) { .majorVersion = 27 }));
    CHECK(macos27_allows_legacy_dock_spaces((NSOperatingSystemVersion) { .majorVersion = 26 }));
    resolver_listed_class = [NSObject class];
    dock_spaces = nil;
    CHECK(!adopt_runtime_dock_spaces_candidate(candidate));
    CHECK(dock_spaces == nil && resolver_retain_calls == 0 && resolver_log_calls == 0);
    resolver_listed_class = resolver_returned_class;
    CHECK(adopt_runtime_dock_spaces_candidate(candidate));
    CHECK(dock_spaces == candidate && resolver_retain_calls == 1 && resolver_log_calls == 1);
    dock_spaces = nil;

#if __arm64__
    resolver_retain_calls = 0;
    resolver_log_calls = 0;
    fixture.globals[1] = candidate;
    CHECK(adopt_dock_spaces_slot((uint64_t) &fixture.globals[1]));
    CHECK(dock_spaces == candidate && resolver_retain_calls == 1 && resolver_log_calls == 1);
    dock_spaces = nil;
    fixture.globals[1] = (id) &fake_candidate;
    CHECK(!adopt_dock_spaces_slot((uint64_t) &fixture.globals[1]));
    CHECK(dock_spaces == nil && resolver_retain_calls == 1 && resolver_log_calls == 1);
    fixture.globals[1] = candidate;
#endif

    resolver_listed_class = [NSObject class];
    CHECK(!dock_spaces_candidate_is_valid(candidate));
    resolver_listed_class = resolver_returned_class;
    resolver_class_list_mode = 2;
    CHECK(!dock_spaces_candidate_is_valid(candidate));
    resolver_class_list_mode = 3;
    CHECK(!dock_spaces_candidate_is_valid(candidate));
    resolver_class_list_mode = 0;
    payload_test_calloc = test_calloc_failure;
    CHECK(!dock_spaces_candidate_is_valid(candidate));
    payload_test_calloc = NULL;
    resolver_test_region.end = address + sizeof(void *) - 1;
    CHECK(!dock_spaces_candidate_is_valid(candidate));
    [candidate release];
}
#endif

static int space_postcondition_calls;
static int space_postcondition_success_after;
static int space_postcondition_value;

static int test_space_postcondition(enum space_postcondition_operation operation, uint64_t space_id,
                                    CFStringRef source_display, CFStringRef destination_display,
                                    NSUInteger previous_count)
{
    (void) operation;
    (void) space_id;
    (void) source_display;
    (void) destination_display;
    (void) previous_count;
    ++space_postcondition_calls;
    return space_postcondition_calls >= space_postcondition_success_after ? space_postcondition_value : 0;
}

static void test_space_postcondition_sleep(useconds_t delay)
{
    (void) delay;
}

static void test_space_postcondition_waits(void)
{
    payload_test_space_postcondition = test_space_postcondition;
    payload_test_space_postcondition_sleep = test_space_postcondition_sleep;
    for (int operation = SPACE_POSTCONDITION_MOVE; operation <= SPACE_POSTCONDITION_FOCUS; ++operation) {
        space_postcondition_calls = 0;
        space_postcondition_success_after = 3;
        space_postcondition_value = 1;
        CHECK(wait_for_space_postcondition(operation, 42, CFSTR("source"), CFSTR("destination"), 2));
        CHECK(space_postcondition_calls == 3);

        space_postcondition_calls = 0;
        space_postcondition_success_after = 1;
        space_postcondition_value = 0;
        CHECK(!wait_for_space_postcondition(operation, 42, CFSTR("source"), CFSTR("wrong"), 2));
        CHECK(space_postcondition_calls == 50);

        space_postcondition_calls = 0;
        space_postcondition_value = -1;
        CHECK(!wait_for_space_postcondition(operation, 42, CFSTR("source"), CFSTR("destination"), 2));
        CHECK(space_postcondition_calls == 50);
    }
    payload_test_space_postcondition = NULL;
    payload_test_space_postcondition_sleep = NULL;
}

static void test_same_display_move_postcondition_checks_order(void)
{
    PayloadTestSpace *first = [[PayloadTestSpace alloc] initWithID:1];
    PayloadTestSpace *second = [[PayloadTestSpace alloc] initWithID:2];
    PayloadTestSpace *third = [[PayloadTestSpace alloc] initWithID:3];
    NSArray *ordered = @[ first, second, third ];
    NSArray *wrong = @[ second, first, third ];

    CHECK(space_follows_space_in_list(ordered, 2, 1));
    CHECK(!space_follows_space_in_list(ordered, 1, 2));
    CHECK(!space_follows_space_in_list(ordered, 3, 1));
    CHECK(space_follows_space_in_list(wrong, 1, 2));

    [first release];
    [second release];
    [third release];
}

static int move_postcondition_move_result;
static int move_postcondition_focus_result;
static int move_postcondition_move_calls;
static int move_postcondition_focus_calls;
static int test_move_postcondition(enum space_postcondition_operation operation, uint64_t space_id,
                                   CFStringRef source_display, CFStringRef destination_display,
                                   NSUInteger context_value)
{
    (void) space_id;
    (void) source_display;
    (void) destination_display;
    (void) context_value;
    if (operation == SPACE_POSTCONDITION_MOVE) {
        ++move_postcondition_move_calls;
        return move_postcondition_move_result;
    }
    if (operation == SPACE_POSTCONDITION_FOCUS) {
        ++move_postcondition_focus_calls;
        return move_postcondition_focus_result;
    }
    return 0;
}

static void test_focused_move_requires_order_and_focus_postconditions(void)
{
    payload_test_space_postcondition = test_move_postcondition;
    payload_test_space_postcondition_sleep = test_space_postcondition_sleep;

    move_postcondition_move_calls = move_postcondition_focus_calls = 0;
    move_postcondition_move_result = 1;
    move_postcondition_focus_result = 0;
    CHECK(!wait_for_space_move_postconditions(1, 2, CFSTR("display"), CFSTR("display"), true));
    CHECK(move_postcondition_move_calls == 1);
    CHECK(move_postcondition_focus_calls == 50);

    move_postcondition_move_calls = move_postcondition_focus_calls = 0;
    move_postcondition_focus_result = 1;
    CHECK(wait_for_space_move_postconditions(1, 2, CFSTR("display"), CFSTR("display"), true));
    CHECK(move_postcondition_move_calls == 1);
    CHECK(move_postcondition_focus_calls == 1);

    move_postcondition_move_calls = move_postcondition_focus_calls = 0;
    CHECK(wait_for_space_move_postconditions(1, 2, CFSTR("source"), CFSTR("destination"), false));
    CHECK(move_postcondition_move_calls == 1);
    CHECK(move_postcondition_focus_calls == 0);

    payload_test_space_postcondition = NULL;
    payload_test_space_postcondition_sleep = NULL;
}

static void test_owned_ivar_replacement_balances_retain_counts(void)
{
    PayloadOwnedIvarHolder *holder = [[PayloadOwnedIvarHolder alloc] init];
    NSObject *first = [[NSObject alloc] init];
    NSObject *second = [[NSObject alloc] init];

    CHECK([first retainCount] == 1);
    CHECK([second retainCount] == 1);
    replace_owned_ivar_value(holder, "_currentSpace", first);
    CHECK([first retainCount] == 2);
    replace_owned_ivar_value(holder, "_currentSpace", second);
    CHECK([first retainCount] == 1);
    CHECK([second retainCount] == 2);
    replace_owned_ivar_value(holder, "_currentSpace", nil);
    CHECK([second retainCount] == 1);

    [first release];
    [second release];
    [holder release];
}

static int osax_payload_test_main(void)
{
#if __arm64__
    test_resolver_scan_bounds();
    test_resolver_decode();
    test_resolver_spans_and_classes();
#endif
    test_space_postcondition_waits();
    test_same_display_move_postcondition_checks_order();
    test_focused_move_requires_order_and_focus_postconditions();
    test_owned_ivar_replacement_balances_retain_counts();
    test_retarget_during_blocked_getter_failure();
    test_retarget_during_blocked_setter_failure();
    test_retarget_after_final_frame_restarts();
    test_handle_message_fresh_status_deadline();
    if (failures) {
        fprintf(stderr, "%d OSAX payload tests failed\n", failures);
        return 1;
    }
    puts("OSAX payload tests passed (resolver bounds, Space postconditions, fade worker races, and status deadlines)");
    return 0;
}

int main(void)
{
    @autoreleasepool {
        return osax_payload_test_main();
    }
}
