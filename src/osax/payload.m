#include <Foundation/Foundation.h>

#include <mach-o/getsect.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <mach/vm_page_size.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include <CoreGraphics/CoreGraphics.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <netdb.h>
#include <dlfcn.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "message_validation.h"
#include "../misc/socket_io.h"
#include "../runtime_paths.h"

#ifdef __x86_64__
#include "x64_payload.m"
#elif __arm64__
#include "arm64_payload.m"
#include <ptrauth.h>
#endif

#define HASHTABLE_IMPLEMENTATION
#include "../misc/hashtable.h"
#undef HASHTABLE_IMPLEMENTATION

_Static_assert(SA_LAYER_KEY_BACKSTOP_MENU == kCGBackstopMenuLevelKey, "layer key contract mismatch");
_Static_assert(SA_LAYER_KEY_NORMAL == kCGNormalWindowLevelKey, "layer key contract mismatch");
_Static_assert(SA_LAYER_KEY_FLOATING == kCGFloatingWindowLevelKey, "layer key contract mismatch");

#define page_align(addr) (vm_address_t)((uintptr_t)(addr) & (~(vm_page_size - 1)))
#define unpack(v) memcpy(&v, message, sizeof(v)); message += sizeof(v)
#define lerp(a, t, b) (((1.0-t)*a) + (t*b))

extern CGDirectDisplayID CGDisplayGetDisplayIDFromUUID(CFUUIDRef uuid);
extern int SLSMainConnectionID(void);
extern CGError SLSGetConnectionPSN(int cid, ProcessSerialNumber *psn);
extern CGError SLSGetWindowAlpha(int cid, uint32_t wid, float *alpha);
extern CGError SLSSetWindowAlpha(int cid, uint32_t wid, float alpha);
extern OSStatus SLSMoveWindowWithGroup(int cid, uint32_t wid, CGPoint *point);
extern CGError SLSReassociateWindowsSpacesByGeometry(int cid, CFArrayRef window_list);
extern CGError SLSGetWindowOwner(int cid, uint32_t wid, int *window_cid);
extern CGError SLSSetWindowTags(int cid, uint32_t wid, uint64_t *tags, size_t tag_size);
extern CGError SLSClearWindowTags(int cid, uint32_t wid, uint64_t *tags, size_t tag_size);
extern CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect *frame);
extern CGError SLSGetWindowTransform(int cid, uint32_t wid, CGAffineTransform *t);
extern CGError SLSSetWindowTransform(int cid, uint32_t wid, CGAffineTransform t);
extern CGError SLSOrderWindow(int cid, uint32_t wid, int order, uint32_t rel_wid);
extern void SLSManagedDisplaySetCurrentSpace(int cid, CFStringRef display_ref, uint64_t sid);
extern uint64_t SLSManagedDisplayGetCurrentSpace(int cid, CFStringRef display_ref);
extern CFStringRef SLSCopyManagedDisplayForSpace(int cid, uint64_t sid);
extern void SLSMoveWindowsToManagedSpace(int cid, CFArrayRef window_list, uint64_t sid);
extern void SLSShowSpaces(int cid, CFArrayRef space_list);
extern void SLSHideSpaces(int cid, CFArrayRef space_list);
extern CFTypeRef SLSTransactionCreate(int cid);
extern CGError SLSTransactionCommit(CFTypeRef transaction, int synchronous);
extern CGError SLSTransactionOrderWindowGroup(CFTypeRef transaction, uint32_t wid, int order, uint32_t rel_wid);
extern CGError SLSTransactionSetWindowSystemAlpha(CFTypeRef transaction, uint32_t wid, float alpha);
extern CGError SLSSetWindowSubLevel(int cid, uint32_t wid, int level);

struct window_fade_context
{
    pthread_t thread;
    uint32_t wid;
    float alpha;
    float duration;
    uint64_t generation;
};

pthread_mutex_t window_fade_lock;
struct table window_fade_table;

#ifdef TESTS
static CGError (*window_fade_get_alpha)(int, uint32_t, float *);
static CGError (*window_fade_set_alpha)(int, uint32_t, float);
static bool (*payload_test_operation_dispatch)(enum sa_opcode, char *);
#else
#define window_fade_get_alpha SLSGetWindowAlpha
#define window_fade_set_alpha SLSSetWindowAlpha
#endif

static id dock_spaces;
static id dp_desktop_picture_manager;
static uint64_t add_space_fp;
static uint64_t remove_space_fp;
static uint64_t move_space_fp;
static uint64_t set_front_window_fp;
static uint64_t animation_time_addr;
static bool macOSSequoia;

static pthread_t daemon_thread;
static int daemon_sockfd = -1;
static int sa_lock_fd = -1;

static void dump_class_info(Class c)
{
    const char *name = class_getName(c);
    unsigned int count = 0;

    Ivar *ivar_list = class_copyIvarList(c, &count);
    for (int i = 0; i < count; i++) {
        Ivar ivar = ivar_list[i];
        const char *ivar_name = ivar_getName(ivar);
        NSLog(@"%s ivar: %s", name, ivar_name);
    }
    if (ivar_list) free(ivar_list);

    objc_property_t *property_list = class_copyPropertyList(c, &count);
    for (int i = 0; i < count; i++) {
        objc_property_t property = property_list[i];
        const char *prop_name = property_getName(property);
        NSLog(@"%s property: %s", name, prop_name);
    }
    if (property_list) free(property_list);

    Method *method_list = class_copyMethodList(c, &count);
    for (int i = 0; i < count; i++) {
        Method method = method_list[i];
        const char *method_name = sel_getName(method_getName(method));
        NSLog(@"%s method: %s", name, method_name);
    }
    if (method_list) free(method_list);
}

static Class dump_class_info_by_name(const char *name)
{
    Class c = objc_getClass(name);
    if (c != nil) {
        dump_class_info(c);
    }
    return c;
}

static uint64_t static_base_address(void)
{
    const struct segment_command_64 *command = getsegbyname("__TEXT");
    uint64_t addr = command->vmaddr;
    return addr;
}

static uint64_t image_slide(void)
{
    char path[1024];
    uint32_t size = sizeof(path);

    if (_NSGetExecutablePath(path, &size) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        if (strcmp(_dyld_get_image_name(i), path) == 0) {
            return _dyld_get_image_vmaddr_slide(i);
        }
    }

    return 0;
}

#if __arm64__
struct runtime_vm_region {
    uint64_t start;
    uint64_t end;
    vm_prot_t protection;
};

static bool checked_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool main_image_slide(int64_t *slide)
{
    const struct mach_header *header = _dyld_get_image_header(0);
    if (!header || header->filetype != MH_EXECUTE) return false;
    *slide = _dyld_get_image_vmaddr_slide(0);
    return true;
}

static bool apply_slide_and_size(uint64_t address, uint64_t size, int64_t slide,
                                 uint64_t *start, uint64_t *end)
{
    uint64_t slid_start;
    if (slide >= 0) {
        if (!checked_add_u64(address, (uint64_t) slide, &slid_start)) return false;
    } else {
        uint64_t magnitude = (uint64_t) (-(slide + 1)) + 1;
        if (magnitude > address) return false;
        slid_start = address - magnitude;
    }
    if (!checked_add_u64(slid_start, size, end)) return false;
    *start = slid_start;
    return true;
}

static bool main_segment_bounds_real(const char *name, uint64_t *start, uint64_t *end)
{
    const struct segment_command_64 *segment = getsegbyname(name);
    int64_t slide;
    return segment && main_image_slide(&slide) &&
           apply_slide_and_size(segment->vmaddr, segment->vmsize, slide, start, end);
}

static bool runtime_vm_region_real(uint64_t address, struct runtime_vm_region *region)
{
    mach_vm_address_t region_address = address;
    mach_vm_size_t region_size = 0;
    vm_region_basic_info_data_64_t info = {};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t result = mach_vm_region(mach_task_self(), &region_address, &region_size,
                                          VM_REGION_BASIC_INFO_64,
                                          (vm_region_info_t) &info, &count, &object);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    uint64_t region_end;
    if (result != KERN_SUCCESS || !checked_add_u64(region_address, region_size, &region_end)) return false;
    region->start = region_address;
    region->end = region_end;
    region->protection = info.protection;
    return true;
}

#ifdef TESTS
static bool (*payload_test_segment_bounds)(const char *, uint64_t *, uint64_t *);
static bool (*payload_test_vm_region)(uint64_t, struct runtime_vm_region *);
static Class (*payload_test_object_get_class)(id);
static int (*payload_test_get_class_list)(Class *, int);
static void *(*payload_test_calloc)(size_t, size_t);
static id (*payload_test_candidate_retain)(id);
static void (*payload_test_candidate_log)(Class);
static bool (*payload_test_isa_mask)(uintptr_t *);
#define resolver_segment_bounds(n, s, e) (payload_test_segment_bounds ? payload_test_segment_bounds(n, s, e) : main_segment_bounds_real(n, s, e))
#define resolver_vm_region(a, r) (payload_test_vm_region ? payload_test_vm_region(a, r) : runtime_vm_region_real(a, r))
#define resolver_object_get_class(o) (payload_test_object_get_class ? payload_test_object_get_class(o) : object_getClass(o))
#define resolver_get_class_list(b, c) (payload_test_get_class_list ? payload_test_get_class_list(b, c) : objc_getClassList(b, c))
#define resolver_calloc(n, s) (payload_test_calloc ? payload_test_calloc(n, s) : calloc(n, s))
#define resolver_candidate_retain(o) (payload_test_candidate_retain ? payload_test_candidate_retain(o) : [(o) retain])
#define resolver_candidate_log(c) do { if (payload_test_candidate_log) payload_test_candidate_log(c); else NSLog(@"[yabai-sa] macOS 27 dock.spaces API verified (class=%s)", class_getName(c)); } while (0)
#else
#define resolver_segment_bounds main_segment_bounds_real
#define resolver_vm_region runtime_vm_region_real
#define resolver_object_get_class object_getClass
#define resolver_get_class_list objc_getClassList
#define resolver_calloc calloc
#define resolver_candidate_retain(o) [(o) retain]
#define resolver_candidate_log(c) NSLog(@"[yabai-sa] macOS 27 dock.spaces API verified (class=%s)", class_getName(c))
#endif

static bool span_in_region(uint64_t address, uint64_t size, vm_prot_t required)
{
    struct runtime_vm_region region;
    uint64_t span_end;
    return checked_add_u64(address, size, &span_end) &&
           resolver_vm_region(address, &region) &&
           region.start <= address && address < region.end && span_end <= region.end &&
           (region.protection & required) == required;
}

static bool span_in_main_segment(uint64_t address, uint64_t size, const char *name)
{
    uint64_t segment_start, segment_end, span_end;
    return resolver_segment_bounds(name, &segment_start, &segment_end) &&
           checked_add_u64(address, size, &span_end) &&
           segment_start <= address && span_end <= segment_end;
}

static bool runtime_isa_mask(uintptr_t *mask)
{
#ifdef TESTS
    if (payload_test_isa_mask) return payload_test_isa_mask(mask);
#endif
    uintptr_t *symbol = (uintptr_t *) dlsym(RTLD_DEFAULT, "objc_debug_isa_class_mask");
    if (!symbol || !span_in_region((uint64_t) symbol, sizeof(*symbol), VM_PROT_READ)) return false;
    memcpy(mask, symbol, sizeof(*mask));
    return *mask != 0;
}

static Class registered_class_for_raw_isa(uintptr_t raw_isa)
{
    uintptr_t mask = 0;
    bool have_mask = runtime_isa_mask(&mask);
    for (int attempt = 0; attempt < 3; ++attempt) {
        int requested = resolver_get_class_list(NULL, 0);
        if (requested <= 0 || (size_t) requested > SIZE_MAX / sizeof(Class)) return Nil;
        Class *classes = resolver_calloc((size_t) requested, sizeof(Class));
        if (!classes) return Nil;
        int received = resolver_get_class_list(classes, requested);
        if (received < 0) {
            free(classes);
            return Nil;
        }
        if (received > requested) {
            free(classes);
            continue;
        }
        Class found = Nil;
        for (int i = 0; i < received; ++i) {
            uintptr_t class_pointer = (uintptr_t) classes[i];
            if (raw_isa == class_pointer || (have_mask && (raw_isa & mask) == class_pointer)) {
                found = classes[i];
                break;
            }
        }
        free(classes);
        return found;
    }
    return Nil;
}

static bool dock_spaces_candidate_is_valid(id candidate)
{
    if (candidate == nil || !span_in_region((uint64_t) candidate, sizeof(void *), VM_PROT_READ)) return false;

    uintptr_t raw_isa;
    memcpy(&raw_isa, candidate, sizeof(raw_isa));
    Class decoded_class = registered_class_for_raw_isa(raw_isa);
    if (decoded_class == Nil) return false;

    // Runtime object APIs are only entered after the raw isa decodes to a class
    // in the runtime's registered class list.
    Class candidate_class = resolver_object_get_class(candidate);
    if (candidate_class != decoded_class) return false;

    // Dock 2571.x (macOS 27.2) renamed -currentSpaceForDisplayUUID: to
    // -currentSpaceForDisplay: and changed the argument to a CGDirectDisplayID.
    bool has_current_space = class_getInstanceMethod(candidate_class, @selector(currentSpaceForDisplayUUID:)) != NULL ||
                             class_getInstanceMethod(candidate_class, @selector(currentSpaceForDisplay:)) != NULL;
    return has_current_space &&
           class_getInstanceMethod(candidate_class, @selector(spacesForDisplay:)) != NULL &&
           class_getInstanceVariable(candidate_class, "_displaySpaces") != NULL;
}

static int64_t sign_extend_21(uint64_t value)
{
    return (int64_t) (value << 43) >> 43;
}

static bool decode_adrp_add_address_checked(uint64_t instruction_address, uint32_t adrp,
                                            uint32_t add, uint64_t *result)
{
    uint64_t immlo = (adrp >> 29) & 0x3;
    uint64_t immhi = (adrp >> 5) & 0x7ffff;
    int64_t pages = sign_extend_21((immhi << 2) | immlo);
    uint64_t page = instruction_address & ~0xfffULL;
    uint64_t base;
    if (pages < 0) {
        uint64_t magnitude = (uint64_t) (-(pages + 1)) + 1;
        if (magnitude > UINT64_MAX / 0x1000 || magnitude * 0x1000 > page) return false;
        base = page - magnitude * 0x1000;
    } else {
        uint64_t delta = (uint64_t) pages * 0x1000;
        if (!checked_add_u64(page, delta, &base)) return false;
    }
    uint64_t immediate = (add >> 10) & 0xfff;
    if ((add >> 22) & 0x1) immediate *= 0x1000;
    return checked_add_u64(base, immediate, result);
}

static bool decode_ldr_pointer_address_checked(uint64_t base, uint32_t ldr, uint64_t *result)
{
    uint64_t scaled_offset = ((uint64_t) ((ldr >> 10) & 0xfff)) * sizeof(id);
    return checked_add_u64(base, scaled_offset, result);
}

static bool resolver_scan_bounds(uint64_t method_address, uint64_t *scan_end)
{
    struct runtime_vm_region region;
    uint64_t text_start, text_end, cap_end;
    if (!resolver_vm_region(method_address, &region) ||
        region.start > method_address || method_address >= region.end ||
        (region.protection & (VM_PROT_READ | VM_PROT_EXECUTE)) != (VM_PROT_READ | VM_PROT_EXECUTE) ||
        !resolver_segment_bounds("__TEXT", &text_start, &text_end) ||
        method_address < text_start || method_address >= text_end ||
        !checked_add_u64(method_address, 0x200, &cap_end)) return false;
    uint64_t end = region.end < text_end ? region.end : text_end;
    *scan_end = end < cap_end ? end : cap_end;
    return true;
}

static id scan_dock_spaces_method(uint64_t method_address)
{
    uint64_t scan_end;
    if (!resolver_scan_bounds(method_address, &scan_end)) return nil;
    for (uint64_t cursor = method_address; ; cursor += 4) {
        uint64_t instruction_end;
        if (!checked_add_u64(cursor, 12, &instruction_end) || instruction_end > scan_end) break;
        uint32_t instructions[3];
        memcpy(instructions, (const void *) cursor, sizeof(instructions));
        uint32_t adrp = instructions[0], add = instructions[1], ldr = instructions[2];

        if ((adrp & 0x9f000000) != 0x90000000 ||
            (add & 0xff800000) != 0x91000000 ||
            (ldr & 0xffc00000) != 0xf9400000) continue;
        uint32_t reg = adrp & 0x1f;
        if (reg == 31 || (add & 0x1f) != reg || ((add >> 5) & 0x1f) != reg ||
            ((ldr >> 5) & 0x1f) != reg || (ldr & 0x1f) != 0) continue;

        uint64_t base_address, global_address;
        if (!decode_adrp_add_address_checked(cursor, adrp, add, &base_address) ||
            !decode_ldr_pointer_address_checked(base_address, ldr, &global_address) ||
            (!span_in_main_segment(global_address, sizeof(id), "__DATA") &&
             !span_in_main_segment(global_address, sizeof(id), "__DATA_CONST")) ||
            !span_in_region(global_address, sizeof(id), VM_PROT_READ)) continue;
        id candidate;
        memcpy(&candidate, (const void *) global_address, sizeof(candidate));
        if (dock_spaces_candidate_is_valid(candidate)) return candidate;
    }
    return nil;
}

static bool macos27_allows_legacy_dock_spaces(NSOperatingSystemVersion os_version)
{
    return os_version.majorVersion != 27;
}

static bool adopt_runtime_dock_spaces_candidate(id candidate)
{
    if (!dock_spaces_candidate_is_valid(candidate)) return false;
    dock_spaces = resolver_candidate_retain(candidate);
    if (dock_spaces == nil) return false;
    Class candidate_class = resolver_object_get_class(candidate);
    resolver_candidate_log(candidate_class);
    return true;
}

static id resolve_dock_spaces_from_runtime(void)
{
    Class tile_class = objc_getClass("Tile");
    if (tile_class == Nil) {
        NSLog(@"[yabai-sa] runtime resolver could not locate Tile");
        return nil;
    }
    Method method = class_getInstanceMethod(tile_class, @selector(doBindingCommand:display:));
    if (!method) {
        NSLog(@"[yabai-sa] runtime resolver could not locate Tile binding method");
        return nil;
    }
    IMP implementation = method_getImplementation(method);
    if (!implementation) {
        NSLog(@"[yabai-sa] runtime resolver found a null Tile binding IMP");
        return nil;
    }
    uint64_t method_address = (uint64_t) ptrauth_strip(implementation, ptrauth_key_function_pointer);
    id candidate = scan_dock_spaces_method(method_address);
    if (candidate) {
        NSLog(@"[yabai-sa] runtime resolver found dock.spaces via Tile binding method");
    } else {
        NSLog(@"[yabai-sa] runtime resolver could not locate a valid dock.spaces candidate");
    }
    return candidate;
}
#endif

static uint64_t hex_find_seq(uint64_t baddr, const char *c_pattern)
{
    if (!baddr || !c_pattern) return 0;

    uint64_t addr = baddr;
    uint64_t pattern_length = (strlen(c_pattern) + 1) / 3;
    char buffer_a[pattern_length];
    char buffer_b[pattern_length];
    memset(buffer_a, 0, sizeof(buffer_a));
    memset(buffer_b, 0, sizeof(buffer_b));

    char *pattern = (char *) c_pattern + 1;
    for (int i = 0; i < pattern_length; ++i) {
        char c = pattern[-1];
        if (c == '?') {
            buffer_b[i] = 1;
        } else {
            int temp = c <= '9' ? 0 : 9;
            temp = (temp + c) << 0x4;
            c = pattern[0];
            int temp2 = c <= '9' ? 0xd0 : 0xc9;
            buffer_a[i] = temp2 + c + temp;
        }
        pattern += 3;
    }

loop:
    for (int counter = 0; counter < pattern_length; ++counter) {
        if ((buffer_b[counter] == 0) && (((char *)addr)[counter] != buffer_a[counter])) {
            addr = (uint64_t)((char *)addr + 1);
            if (addr - baddr < 0x1286a0) {
                goto loop;
            } else {
                return 0;
            }
        }
    }

    return addr;
}

#if __arm64__
uint64_t decode_adrp_add(uint64_t addr, uint64_t offset)
{
    uint32_t adrp_instr = *(uint32_t *) addr;

    uint32_t immlo = (0x60000000 & adrp_instr) >> 29;
    uint32_t immhi = (0xffffe0 & adrp_instr) >> 3;

    int32_t value = (immhi | immlo) << 12;
    int64_t value_64 = value;

    uint32_t add_instr = *(uint32_t *) (addr + 4);
    uint64_t imm12 = (add_instr & 0x3ffc00) >> 10;

    if (add_instr & 0xc00000) {
        imm12 <<= 12;
    }

    return (offset & 0xfffffffffffff000) + value_64 + imm12;
}
#endif

static bool verify_os_version(NSOperatingSystemVersion os_version)
{
    NSLog(@"[yabai-sa] checking for macOS %ld.%ld.%ld compatibility!", os_version.majorVersion, os_version.minorVersion, os_version.patchVersion);

#ifdef __x86_64__
    if (os_version.majorVersion == 11) {
        return true; // Big Sur 11.0
    } else if (os_version.majorVersion == 12) {
        return true; // Monterey 12.0
    } else if (os_version.majorVersion == 13) {
        return true; // Ventura 13.0
    } else if (os_version.majorVersion == 14) {
        return true; // Sonoma 14.0
    } else if (os_version.majorVersion == 15) {
        macOSSequoia = true;
        return true; // Sequoia 15.0
    } else if (os_version.majorVersion == 26) {

        NSLog(@"[yabai-sa] Detected Tahoe Preview... flagging 'macOSSequoia=true.'");
        macOSSequoia = true;
        return true; // Tahoe preview
    }

    NSLog(@"[yabai-sa] spaces functionality is only supported on macOS Big Sur 11.0.0+, Monterey 12.0.0+, Ventura 13.0.0+, Sonoma 14.0.0+, and Sequoia 15.0");
#elif __arm64__
    if (os_version.majorVersion == 12) {
        return true; // Monterey 12.0
    } else if (os_version.majorVersion == 13) {
        return true; // Ventura 13.0
    } else if (os_version.majorVersion == 14) {
        return true; // Sonoma 14.0
    } else if (os_version.majorVersion == 15) {
        macOSSequoia = true;
        return true; // Sequoia 15.0
    } else if (os_version.majorVersion == 26) {

        NSLog(@"[yabai-sa] Detected Tahoe Preview... flagging 'macOSSequoia=true.'");
        macOSSequoia = true;
        return true; // Tahoe preview
    } else if (os_version.majorVersion == 27) {
        NSLog(@"[yabai-sa] Detected macOS 27 experimental support; enabling verified scripting-addition capabilities only.");
        macOSSequoia = true;
        return true;
    }

    NSLog(@"[yabai-sa] spaces functionality is not supported on this macOS version");
#endif

    return false;
}

static void init_instances()
{
    NSOperatingSystemVersion os_version = [[NSProcessInfo processInfo] operatingSystemVersion];
    if (!verify_os_version(os_version)) return;

    uint64_t baseaddr = static_base_address() + image_slide();

    uint64_t dock_spaces_addr = 0;
    bool allow_legacy_dock_spaces = true;
#if __arm64__
    allow_legacy_dock_spaces = macos27_allows_legacy_dock_spaces(os_version);
    if (!allow_legacy_dock_spaces) {
        adopt_runtime_dock_spaces_candidate(resolve_dock_spaces_from_runtime());
    }
#endif

    if (dock_spaces == nil && allow_legacy_dock_spaces) {
        dock_spaces_addr = hex_find_seq(baseaddr + get_dock_spaces_offset(os_version), get_dock_spaces_pattern(os_version));
    }

    if (dock_spaces_addr == 0) {
        if (dock_spaces == nil) {
            NSLog(@"[yabai-sa] could not locate pointer to dock.spaces! spaces functionality will not work!");
        }
    } else {
#ifdef __x86_64__
        uint32_t dock_spaces_offset = *(int32_t *)dock_spaces_addr;
        NSLog(@"[yabai-sa] (0x%llx) dock.spaces found at address 0x%llX (0x%llx)", baseaddr, dock_spaces_addr, dock_spaces_addr - baseaddr);
        dock_spaces = [(*(id *)(dock_spaces_addr + dock_spaces_offset + 0x4)) retain];
#elif __arm64__
        uint64_t dock_spaces_offset = decode_adrp_add(dock_spaces_addr, dock_spaces_addr - baseaddr);
        NSLog(@"[yabai-sa] (0x%llx) dock.spaces found at address 0x%llX (0x%llx)", baseaddr, dock_spaces_offset, dock_spaces_offset - baseaddr);
        dock_spaces = [(*(id *)(baseaddr + dock_spaces_offset)) retain];
#endif
    }

    uint64_t dppm_addr = hex_find_seq(baseaddr + get_dppm_offset(os_version), get_dppm_pattern(os_version));
    if (dppm_addr == 0) {
        dp_desktop_picture_manager = nil;
        NSLog(@"[yabai-sa] could not locate pointer to dppm! moving spaces will not work!");
    } else {
#ifdef __x86_64__
        uint32_t dppm_offset = *(int32_t *)dppm_addr;
        NSLog(@"[yabai-sa] (0x%llx) dppm found at address 0x%llX (0x%llx)", baseaddr, dppm_addr, dppm_addr - baseaddr);
        dp_desktop_picture_manager = [(*(id *)(dppm_addr + dppm_offset + 0x4)) retain];
#elif __arm64__
        uint64_t dppm_offset = decode_adrp_add(dppm_addr, dppm_addr - baseaddr);
        NSLog(@"[yabai-sa] (0x%llx) dppm found at address 0x%llX (0x%llx)", baseaddr, dppm_offset, dppm_offset - baseaddr);
        dp_desktop_picture_manager = [(*(id *)(baseaddr + dppm_offset)) retain];
#endif

        //
        // @hack
        //
        // NOTE(asmvik): For whatever reason, in Sonoma, DPDesktopPictureManager is initialized and swapped
        // to an alternate storage location instead of where it used to be stored in previous macOS versions..
        //
        // This alternate storage location resides 8-bytes before the usual location, so we simply do
        // the subtract to arrive at the correct location in cases where the usual location is null.
        //

#ifdef __x86_64__
        if (dp_desktop_picture_manager == nil) {
            dp_desktop_picture_manager = [(*(id *)(dppm_addr + dppm_offset + 0x4 - 0x8)) retain];
        }
#elif __arm64__
        if (dp_desktop_picture_manager == nil) {
            dp_desktop_picture_manager = [(*(id *)(baseaddr + dppm_offset - 0x8)) retain];
        }
#endif
    }

    uint64_t add_space_addr = hex_find_seq(baseaddr + get_add_space_offset(os_version), get_add_space_pattern(os_version));
    if (add_space_addr == 0x0) {
        NSLog(@"[yabai-sa] failed to get pointer to addSpace function..");
        add_space_fp = 0;
    } else {
        NSLog(@"[yabai-sa] (0x%llx) addSpace found at address 0x%llX (0x%llx)", baseaddr, add_space_addr, add_space_addr - baseaddr);
#ifdef __x86_64__
        add_space_fp = add_space_addr;
#elif __arm64__
        add_space_fp = (uint64_t) ptrauth_sign_unauthenticated((void *) add_space_addr, ptrauth_key_asia, 0);
#endif
    }

    uint64_t remove_space_addr = hex_find_seq(baseaddr + get_remove_space_offset(os_version), get_remove_space_pattern(os_version));
    if (remove_space_addr == 0x0) {
        NSLog(@"[yabai-sa] failed to get pointer to removeSpace function..");
        remove_space_fp = 0;
    } else {
        NSLog(@"[yabai-sa] (0x%llx) removeSpace found at address 0x%llX (0x%llx)", baseaddr, remove_space_addr, remove_space_addr - baseaddr);
#ifdef __x86_64__
        remove_space_fp = remove_space_addr;
#elif __arm64__
        remove_space_fp = (uint64_t) ptrauth_sign_unauthenticated((void *) remove_space_addr, ptrauth_key_asia, 0);
#endif
    }

    uint64_t move_space_addr = hex_find_seq(baseaddr + get_move_space_offset(os_version), get_move_space_pattern(os_version));
    if (move_space_addr == 0x0) {
        NSLog(@"[yabai-sa] failed to get pointer to moveSpace function..");
        move_space_fp = 0;
    } else {
        NSLog(@"[yabai-sa] (0x%llx) moveSpace found at address 0x%llX (0x%llx)", baseaddr, move_space_addr, move_space_addr - baseaddr);
#ifdef __x86_64__
        move_space_fp = move_space_addr;
#elif __arm64__
        move_space_fp = (uint64_t) ptrauth_sign_unauthenticated((void *) move_space_addr, ptrauth_key_asia, 0);
#endif
    }

    uint64_t set_front_window_addr = hex_find_seq(baseaddr + get_set_front_window_offset(os_version), get_set_front_window_pattern(os_version));
    if (set_front_window_addr == 0x0) {
        NSLog(@"[yabai-sa] failed to get pointer to setFrontWindow function..");
        set_front_window_fp = 0;
    } else {
        NSLog(@"[yabai-sa] (0x%llx) setFrontWindow found at address 0x%llX (0x%llx)", baseaddr, set_front_window_addr, set_front_window_addr - baseaddr);
#ifdef __x86_64__
        set_front_window_fp = set_front_window_addr;
#elif __arm64__
        set_front_window_fp = (uint64_t) ptrauth_sign_unauthenticated((void *) set_front_window_addr, ptrauth_key_asia, 0);
#endif
    }

    animation_time_addr = hex_find_seq(baseaddr + get_fix_animation_offset(os_version), get_fix_animation_pattern(os_version));
    if (animation_time_addr == 0x0) {
        NSLog(@"[yabai-sa] failed to get pointer to animation-time..");
    } else {
        NSLog(@"[yabai-sa] (0x%llx) animation_time_addr found at address 0x%llX (0x%llx)", baseaddr, animation_time_addr, animation_time_addr - baseaddr);
        if (vm_protect(mach_task_self(), page_align(animation_time_addr), vm_page_size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS) {
#ifdef __x86_64__
            *(uint64_t *) animation_time_addr = 0x660fefc0660fefc0;
#elif __arm64__
            *(uint32_t *) animation_time_addr = 0x2f00e400;
#endif
            vm_protect(mach_task_self(), page_align(animation_time_addr), vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
        } else {
            NSLog(@"[yabai-sa] animation_time_addr vm_protect failed; unable to patch instruction!");
        }
    }
}

static inline id get_ivar_value(id instance, const char *name)
{
    id result = nil;
    object_getInstanceVariable(instance, name, (void **) &result);
    return result;
}

static inline void set_ivar_value(id instance, const char *name, id value)
{
    object_setInstanceVariable(instance, name, value);
}

static inline uint64_t get_space_id(id space)
{
    return ((uint64_t (*)(id, SEL)) objc_msgSend)(space, @selector(spid));
}

static inline id space_for_display_with_id(CFStringRef display_uuid, uint64_t space_id)
{
    NSArray *spaces_for_display = ((NSArray *(*)(id, SEL, CFStringRef)) objc_msgSend)(dock_spaces, @selector(spacesForDisplay:), display_uuid);
    for (id space in spaces_for_display) {
        if (space_id == get_space_id(space)) {
            return space;
        }
    }
    return nil;
}

static inline id display_space_for_display_uuid(CFStringRef display_uuid)
{
    if (!display_uuid) return nil;

    id result = nil;

    NSArray *display_spaces = get_ivar_value(dock_spaces, "_displaySpaces");
    if (display_spaces != nil) {
        for (id display_space in display_spaces) {
            id display_source_space = get_ivar_value(display_space, "_currentSpace");
            uint64_t sid = get_space_id(display_source_space);
            CFStringRef uuid = SLSCopyManagedDisplayForSpace(SLSMainConnectionID(), sid);
            bool match = uuid && CFEqual(uuid, display_uuid);
            if (uuid) CFRelease(uuid);
            if (match) {
                result = display_space;
                break;
            }
        }
    }

    return result;
}

static inline id display_space_for_space_with_id(uint64_t space_id)
{
    NSArray *display_spaces = get_ivar_value(dock_spaces, "_displaySpaces");
    if (display_spaces != nil) {
        for (id display_space in display_spaces) {
            id display_source_space = get_ivar_value(display_space, "_currentSpace");
            if (get_space_id(display_source_space) == space_id) {
                return display_space;
            }
        }
    }
    return nil;
}

static id current_space_for_display_uuid(CFStringRef display_uuid);

enum space_postcondition_operation {
    SPACE_POSTCONDITION_MOVE,
    SPACE_POSTCONDITION_DESTROY,
    SPACE_POSTCONDITION_CREATE,
    SPACE_POSTCONDITION_FOCUS
};

#ifdef TESTS
static int (*payload_test_space_postcondition)(enum space_postcondition_operation, uint64_t, CFStringRef, CFStringRef, NSUInteger);
static void (*payload_test_space_postcondition_sleep)(useconds_t);
#endif

static int query_space_postcondition(enum space_postcondition_operation operation, uint64_t space_id,
                                     CFStringRef source_display, CFStringRef destination_display,
                                     NSUInteger previous_count)
{
#ifdef TESTS
    if (payload_test_space_postcondition) {
        return payload_test_space_postcondition(operation, space_id, source_display,
                                                destination_display, previous_count);
    }
#endif
    if (operation == SPACE_POSTCONDITION_FOCUS) {
        id current = current_space_for_display_uuid(destination_display);
        return current ? get_space_id(current) == space_id : -1;
    }

    NSArray *destination_spaces = ((NSArray *(*)(id, SEL, CFStringRef)) objc_msgSend)(
        dock_spaces, @selector(spacesForDisplay:), destination_display);
    if (!destination_spaces) return -1;
    if (operation == SPACE_POSTCONDITION_CREATE) return [destination_spaces count] > previous_count;

    bool in_destination = false;
    for (id space in destination_spaces) {
        if (get_space_id(space) == space_id) {
            in_destination = true;
            break;
        }
    }
    if (operation == SPACE_POSTCONDITION_DESTROY) return !in_destination;

    NSArray *source_spaces = ((NSArray *(*)(id, SEL, CFStringRef)) objc_msgSend)(
        dock_spaces, @selector(spacesForDisplay:), source_display);
    if (!source_spaces) return -1;
    bool in_source = false;
    for (id space in source_spaces) {
        if (get_space_id(space) == space_id) {
            in_source = true;
            break;
        }
    }
    return in_destination && !in_source;
}

static bool wait_for_space_postcondition(enum space_postcondition_operation operation, uint64_t space_id,
                                         CFStringRef source_display, CFStringRef destination_display,
                                         NSUInteger previous_count)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        int state = query_space_postcondition(operation, space_id, source_display,
                                              destination_display, previous_count);
        if (state > 0) return true;
#ifdef TESTS
        if (payload_test_space_postcondition_sleep) payload_test_space_postcondition_sleep(10000);
        else
#endif
        usleep(10000);
    }
    return false;
}

static bool do_space_move(char *message)
{
    if (dock_spaces == nil || dp_desktop_picture_manager == nil || move_space_fp == 0) return false;

    uint64_t source_space_id, dest_space_id, source_prev_space_id;
    unpack(source_space_id);
    unpack(dest_space_id);
    unpack(source_prev_space_id);

    bool focus_dest_space;
    unpack(focus_dest_space);

    bool mutation_completed = false;
    CFStringRef source_display_uuid = SLSCopyManagedDisplayForSpace(SLSMainConnectionID(), source_space_id);
    CFStringRef dest_display_uuid = SLSCopyManagedDisplayForSpace(SLSMainConnectionID(), dest_space_id);
    if (!source_display_uuid || !dest_display_uuid) goto cleanup;

    id source_space = space_for_display_with_id(source_display_uuid, source_space_id);
    id source_display_space = display_space_for_display_uuid(source_display_uuid);
    id dest_space = space_for_display_with_id(dest_display_uuid, dest_space_id);
    id dest_display_space = display_space_for_display_uuid(dest_display_uuid);
    if (source_space == nil || source_display_space == nil || dest_space == nil || dest_display_space == nil) goto cleanup;

    id source_prev_space = nil;
    if (source_prev_space_id) {
        source_prev_space = space_for_display_with_id(source_display_uuid, source_prev_space_id);
        if (source_prev_space == nil) goto cleanup;
    }

    unsigned dest_display_id = ((unsigned (*)(id, SEL, id)) objc_msgSend)(dock_spaces, @selector(displayIDForSpace:), dest_space);

    if (source_prev_space_id) {
        NSArray *ns_source_space = @[ @(source_space_id) ];
        NSArray *ns_dest_space = @[ @(source_prev_space_id) ];
        SLSShowSpaces(SLSMainConnectionID(), (__bridge CFArrayRef) ns_dest_space);
        SLSHideSpaces(SLSMainConnectionID(), (__bridge CFArrayRef) ns_source_space);
        SLSManagedDisplaySetCurrentSpace(SLSMainConnectionID(), source_display_uuid, source_prev_space_id);
        set_ivar_value(source_display_space, "_currentSpace", [source_prev_space retain]);
        [ns_dest_space release];
        [ns_source_space release];
    }

    asm__call_move_space(source_space, dest_space, dest_display_uuid, dock_spaces, move_space_fp);

    dispatch_sync(dispatch_get_main_queue(), ^{
        ((void (*)(id, SEL, id, unsigned, CFStringRef)) objc_msgSend)(dp_desktop_picture_manager, @selector(moveSpace:toDisplay:displayUUID:), source_space, dest_display_id, dest_display_uuid);
    });

    if (focus_dest_space) {
        uint64_t new_source_space_id = SLSManagedDisplayGetCurrentSpace(SLSMainConnectionID(), source_display_uuid);
        id new_source_space = space_for_display_with_id(source_display_uuid, new_source_space_id);
        if (new_source_space != nil) {
            set_ivar_value(source_display_space, "_currentSpace", [new_source_space retain]);
        }

        NSArray *ns_dest_monitor_space = @[ @(dest_space_id) ];
        SLSHideSpaces(SLSMainConnectionID(), (__bridge CFArrayRef) ns_dest_monitor_space);
        SLSManagedDisplaySetCurrentSpace(SLSMainConnectionID(), dest_display_uuid, source_space_id);
        set_ivar_value(dest_display_space, "_currentSpace", [source_space retain]);
        [ns_dest_monitor_space release];
    }

    mutation_completed = wait_for_space_postcondition(SPACE_POSTCONDITION_MOVE, source_space_id,
                                                       source_display_uuid, dest_display_uuid, 0);

cleanup:
    if (source_display_uuid) CFRelease(source_display_uuid);
    if (dest_display_uuid) CFRelease(dest_display_uuid);
    return mutation_completed;
}

typedef void (*remove_space_call)(id space, id display_space, id dock_spaces, uint64_t space_id1, uint64_t space_id2);
static bool do_space_destroy(char *message)
{
    if (dock_spaces == nil || remove_space_fp == 0) return false;

    uint64_t space_id;
    unpack(space_id);

    CFStringRef display_uuid = SLSCopyManagedDisplayForSpace(SLSMainConnectionID(), space_id);
    if (!display_uuid) return false;

    id space = space_for_display_with_id(display_uuid, space_id);
    id display_space = display_space_for_display_uuid(display_uuid);
    if (space == nil || display_space == nil) {
        CFRelease(display_uuid);
        return false;
    }

    uint64_t active_space_id = SLSManagedDisplayGetCurrentSpace(SLSMainConnectionID(), display_uuid);
    dispatch_sync(dispatch_get_main_queue(), ^{
        ((remove_space_call) remove_space_fp)(space, display_space, dock_spaces, space_id, space_id);
    });

    if (active_space_id == space_id) {
        uint64_t dest_space_id = SLSManagedDisplayGetCurrentSpace(SLSMainConnectionID(), display_uuid);
        id dest_space = space_for_display_with_id(display_uuid, dest_space_id);
        if (dest_space != nil) {
            set_ivar_value(display_space, "_currentSpace", [dest_space retain]);
        }
    }

    bool mutation_completed = wait_for_space_postcondition(SPACE_POSTCONDITION_DESTROY, space_id,
                                                            display_uuid, display_uuid, 0);
    CFRelease(display_uuid);
    return mutation_completed;
}

static bool do_space_create(char *message)
{
    if (dock_spaces == nil || add_space_fp == 0) return false;

    uint64_t space_id;
    unpack(space_id);

    CFStringRef display_uuid = SLSCopyManagedDisplayForSpace(SLSMainConnectionID(), space_id);
    if (!display_uuid) return false;

    NSArray *spaces_before = ((NSArray *(*)(id, SEL, CFStringRef)) objc_msgSend)(
        dock_spaces, @selector(spacesForDisplay:), display_uuid);
    if (!spaces_before) {
        CFRelease(display_uuid);
        return false;
    }
    NSUInteger previous_count = [spaces_before count];
    __block bool request_dispatched = false;
    dispatch_sync(dispatch_get_main_queue(), ^{
        id display_space = display_space_for_display_uuid(display_uuid);
        if (display_space != nil) {
            id new_space = macOSSequoia
                         ? [[objc_getClass("ManagedSpace") alloc] init]
                         : [[objc_getClass("Dock.ManagedSpace") alloc] init];
            if (new_space != nil) {
                asm__call_add_space(new_space, display_space, add_space_fp);
                request_dispatched = true;
            }
        }
    });

    bool mutation_completed = request_dispatched &&
        wait_for_space_postcondition(SPACE_POSTCONDITION_CREATE, space_id,
                                     display_uuid, display_uuid, previous_count);
    CFRelease(display_uuid);
    return mutation_completed;
}


// Resolve the current space for a display UUID across Dock API generations:
//   - pre-Sequoia:        -[Spaces currentSpaceforDisplayUUID:(NSString *)]
//   - Sequoia .. 27.1:    -[Spaces currentSpaceForDisplayUUID:(NSString *)]
//   - 27.2 (Dock 2571.x): -[Spaces currentSpaceForDisplay:(CGDirectDisplayID)]
static id current_space_for_display_uuid(CFStringRef display_uuid)
{
    if (dock_spaces == nil) return nil;

    if ([dock_spaces respondsToSelector:@selector(currentSpaceForDisplayUUID:)]) {
        return ((id (*)(id, SEL, CFStringRef)) objc_msgSend)(dock_spaces, @selector(currentSpaceForDisplayUUID:), display_uuid);
    }

    if ([dock_spaces respondsToSelector:@selector(currentSpaceForDisplay:)]) {
        CGDirectDisplayID display_id = 0;
        CFUUIDRef uuid = CFUUIDCreateFromString(NULL, display_uuid);
        if (uuid) {
            display_id = CGDisplayGetDisplayIDFromUUID(uuid);
            CFRelease(uuid);
        }
        if (!display_id) return nil;
        return ((id (*)(id, SEL, CGDirectDisplayID)) objc_msgSend)(dock_spaces, @selector(currentSpaceForDisplay:), display_id);
    }

    return ((id (*)(id, SEL, CFStringRef)) objc_msgSend)(dock_spaces, @selector(currentSpaceforDisplayUUID:), display_uuid);
}

static bool do_space_focus(char *message)
{
    if (dock_spaces == nil) return false;

    uint64_t dest_space_id;
    unpack(dest_space_id);
    if (!dest_space_id) return false;

    {
        CFStringRef dest_display = SLSCopyManagedDisplayForSpace(SLSMainConnectionID(), dest_space_id);
        if (dest_display == NULL) return false;

        id source_space = current_space_for_display_uuid(dest_display);
        uint64_t source_space_id = get_space_id(source_space);
        if (!source_space_id) {
            CFRelease(dest_display);
            return false;
        }

        if (source_space_id != dest_space_id) {
            id dest_space = space_for_display_with_id(dest_display, dest_space_id);
            if (dest_space == nil) {
                CFRelease(dest_display);
                return false;
            }

            id display_space = display_space_for_space_with_id(source_space_id);
            if (display_space == nil) {
                CFRelease(dest_display);
                return false;
            }

            NSArray *ns_source_space = @[ @(source_space_id) ];
            NSArray *ns_dest_space = @[ @(dest_space_id) ];
            SLSShowSpaces(SLSMainConnectionID(), (__bridge CFArrayRef) ns_dest_space);
            SLSHideSpaces(SLSMainConnectionID(), (__bridge CFArrayRef) ns_source_space);
            SLSManagedDisplaySetCurrentSpace(SLSMainConnectionID(), dest_display, dest_space_id);
            set_ivar_value(display_space, "_currentSpace", [dest_space retain]);
            [ns_dest_space release];
            [ns_source_space release];
        }

        bool mutation_completed = wait_for_space_postcondition(SPACE_POSTCONDITION_FOCUS, dest_space_id,
                                                                dest_display, dest_display, 0);
        CFRelease(dest_display);
        return mutation_completed;
    }
}

static bool transform_is_finite(CGAffineTransform transform)
{
    return isfinite(transform.a) && isfinite(transform.b) && isfinite(transform.c) &&
           isfinite(transform.d) && isfinite(transform.tx) && isfinite(transform.ty);
}

static bool do_window_scale(char *message)
{
    uint32_t wid;
    float dx, dy, dw, dh;
    unpack(wid);
    unpack(dx);
    unpack(dy);
    unpack(dw);
    unpack(dh);
    if (!wid || !sa_scale_geometry_is_safe(dx, dy, dw, dh)) return false;

    CGRect frame = CGRectZero;
    CGAffineTransform current_transform = CGAffineTransformIdentity;
    int cid = SLSMainConnectionID();
    if (SLSGetWindowBounds(cid, wid, &frame) != kCGErrorSuccess ||
        SLSGetWindowTransform(cid, wid, &current_transform) != kCGErrorSuccess ||
        !isfinite(frame.origin.x) || !isfinite(frame.origin.y) ||
        !isfinite(frame.size.width) || frame.size.width <= 0.0 ||
        !isfinite(frame.size.height) || frame.size.height <= 0.0 ||
        !transform_is_finite(current_transform)) return false;

    CGAffineTransform original_transform = CGAffineTransformMakeTranslation(-frame.origin.x, -frame.origin.y);
    if (!transform_is_finite(original_transform)) return false;
    CGAffineTransform target_transform = original_transform;

    if (CGAffineTransformEqualToTransform(current_transform, original_transform)) {
        double x_scale, y_scale, transformed_x, transformed_y;
        if (!sa_scale_transform_components(frame.size.width, frame.size.height, dx, dy, dw, dh,
                                           &x_scale, &y_scale, &transformed_x, &transformed_y)) return false;
        CGAffineTransform scale = CGAffineTransformMakeScale(x_scale, y_scale);
        target_transform = CGAffineTransformTranslate(scale, transformed_x, transformed_y);
        if (!transform_is_finite(target_transform)) return false;
    }

    return SLSSetWindowTransform(cid, wid, target_transform) == kCGErrorSuccess;
}

static bool do_window_move(char *message)
{
    uint32_t wid;
    unpack(wid);
    if (!wid) return false;

    int x, y;
    unpack(x);
    unpack(y);

    CGPoint point = CGPointMake(x, y);
    int cid = SLSMainConnectionID();
    if (SLSMoveWindowWithGroup(cid, wid, &point) != noErr) return false;

    NSArray *window_list = [[NSArray alloc] initWithObjects:@(wid), nil];
    if (!window_list) return false;
    bool result = SLSReassociateWindowsSpacesByGeometry(cid, (__bridge CFArrayRef) window_list) == kCGErrorSuccess;
    [window_list release];
    return result;
}

static bool do_window_opacity(char *message)
{
    uint32_t wid;
    float alpha, ignored_duration;
    unpack(wid);
    unpack(alpha);
    unpack(ignored_duration);
    if (!wid || !sa_opacity_is_safe(alpha) || ignored_duration != 0.0f) return false;

    pthread_mutex_lock(&window_fade_lock);
    struct window_fade_context *context = table_find(&window_fade_table, &wid);
    if (context) {
        context->alpha = alpha;
        context->duration = 0.0f;
        context->generation = sa_fade_next_generation(context->generation);
        pthread_mutex_unlock(&window_fade_lock);
        return true;
    }
    pthread_mutex_unlock(&window_fade_lock);
    return window_fade_set_alpha &&
           window_fade_set_alpha(SLSMainConnectionID(), wid, alpha) == kCGErrorSuccess;
}

static void window_fade_destroy_locked(struct window_fade_context *context)
{
    table_remove(&window_fade_table, &context->wid);
    free(context);
}

static bool window_fade_failure_should_retry_locked(struct window_fade_context *context,
                                                    uint64_t work_generation)
{
    if (sa_fade_failure_action_for_generation(context->generation, work_generation) == SA_FADE_FAILURE_RETRY) {
        return true;
    }
    window_fade_destroy_locked(context);
    return false;
}

static void *window_fade_thread_proc(void *data)
{
    struct window_fade_context *context = data;
entry:
    pthread_mutex_lock(&window_fade_lock);
    uint64_t work_generation = context->generation;
    float end_alpha = context->alpha;
    float duration = context->duration;
    pthread_mutex_unlock(&window_fade_lock);

    float start_alpha = 0.0f;
    if (!window_fade_get_alpha ||
        window_fade_get_alpha(SLSMainConnectionID(), context->wid, &start_alpha) != kCGErrorSuccess ||
        !sa_opacity_is_safe(start_alpha)) {
        pthread_mutex_lock(&window_fade_lock);
        bool retry = window_fade_failure_should_retry_locked(context, work_generation);
        pthread_mutex_unlock(&window_fade_lock);
        if (retry) goto entry;
        return NULL;
    }

    const int frame_duration = 8;
    int total_duration = (int) ((double) duration * 1000.0);
    int frame_count = total_duration / frame_duration + 1;
    for (int frame_index = 1; frame_index <= frame_count; ++frame_index) {
        pthread_mutex_lock(&window_fade_lock);
        bool retargeted = context->generation != work_generation;
        pthread_mutex_unlock(&window_fade_lock);
        if (retargeted) goto entry;

        float t = (float) frame_index / (float) frame_count;
        float alpha = lerp(start_alpha, t, end_alpha);
        if (!sa_opacity_is_safe(alpha) ||
            (!window_fade_set_alpha ||
             window_fade_set_alpha(SLSMainConnectionID(), context->wid, alpha) != kCGErrorSuccess)) {
            pthread_mutex_lock(&window_fade_lock);
            bool retry = window_fade_failure_should_retry_locked(context, work_generation);
            pthread_mutex_unlock(&window_fade_lock);
            if (retry) goto entry;
            return NULL;
        }
        usleep(frame_duration * 1000);
    }

    pthread_mutex_lock(&window_fade_lock);
    if (context->generation != work_generation) {
        pthread_mutex_unlock(&window_fade_lock);
        goto entry;
    }
    window_fade_destroy_locked(context);
    pthread_mutex_unlock(&window_fade_lock);
    return NULL;
}

static bool do_window_opacity_fade(char *message)
{
    uint32_t wid;
    float alpha, duration;
    unpack(wid);
    unpack(alpha);
    unpack(duration);
    if (!wid || !sa_opacity_is_safe(alpha) || duration <= 0.0f || !sa_opacity_duration_is_safe(duration)) return false;

    float current_alpha = 0.0f;
    if (!window_fade_get_alpha ||
        window_fade_get_alpha(SLSMainConnectionID(), wid, &current_alpha) != kCGErrorSuccess ||
        !sa_opacity_is_safe(current_alpha)) return false;

    pthread_mutex_lock(&window_fade_lock);
    struct window_fade_context *context = table_find(&window_fade_table, &wid);
    if (context) {
        context->alpha = alpha;
        context->duration = duration;
        context->generation = sa_fade_next_generation(context->generation);
        pthread_mutex_unlock(&window_fade_lock);
        return true;
    }

    context = calloc(1, sizeof(*context));
    if (!context) {
        pthread_mutex_unlock(&window_fade_lock);
        return false;
    }
    context->wid = wid;
    context->alpha = alpha;
    context->duration = duration;
    context->generation = 1;
    if (!table_try_add(&window_fade_table, &wid, context)) {
        pthread_mutex_unlock(&window_fade_lock);
        free(context);
        return false;
    }

    pthread_attr_t attr;
    bool attr_initialized = pthread_attr_init(&attr) == 0;
    bool attr_detached = attr_initialized && pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) == 0;
    int create_result = attr_detached ? pthread_create(&context->thread, &attr, window_fade_thread_proc, context) : -1;
    if (attr_initialized) pthread_attr_destroy(&attr);
    if (create_result != 0) {
        table_remove(&window_fade_table, &wid);
        pthread_mutex_unlock(&window_fade_lock);
        free(context);
        return false;
    }
    pthread_mutex_unlock(&window_fade_lock);
    return true;
}

static bool do_window_layer(char *message)
{
    uint32_t wid;
    int layer;
    unpack(wid);
    unpack(layer);
    return wid && sa_layer_key_is_supported(layer) &&
           SLSSetWindowSubLevel(SLSMainConnectionID(), wid, CGWindowLevelForKey(layer)) == kCGErrorSuccess;
}

static bool do_window_sticky(char *message)
{
    uint32_t wid;
    bool value;
    unpack(wid);
    unpack(value);
    if (!wid) return false;

    uint64_t tags = (1 << 11);
    CGError result = value ? SLSSetWindowTags(SLSMainConnectionID(), wid, &tags, 64)
                           : SLSClearWindowTags(SLSMainConnectionID(), wid, &tags, 64);
    return result == kCGErrorSuccess;
}

typedef void (*focus_window_call)(ProcessSerialNumber psn, uint32_t wid);
static bool do_window_focus(char *message)
{
    if (set_front_window_fp == 0) return false;

    int window_connection = 0;
    ProcessSerialNumber window_psn = { 0 };

    uint32_t wid;
    unpack(wid);
    if (!wid) return false;

    if (SLSGetWindowOwner(SLSMainConnectionID(), wid, &window_connection) != kCGErrorSuccess) return false;
    if (SLSGetConnectionPSN(window_connection, &window_psn) != kCGErrorSuccess) return false;

    ((focus_window_call) set_front_window_fp)(window_psn, wid);
    return true;
}

static bool do_window_shadow(char *message)
{
    uint32_t wid;
    bool value;
    unpack(wid);
    unpack(value);
    if (!wid) return false;

    uint64_t tags = (1 << 3);
    CGError result = value ? SLSClearWindowTags(SLSMainConnectionID(), wid, &tags, 64)
                           : SLSSetWindowTags(SLSMainConnectionID(), wid, &tags, 64);
    return result == kCGErrorSuccess;
}

static bool do_window_swap_proxy(char *message, bool swap_in)
{
    int count = 0;
    unpack(count);
    if (!count) return true;

    CFTypeRef transaction = SLSTransactionCreate(SLSMainConnectionID());
    if (!transaction) return false;
    bool result = true;
    for (int i = 0; i < count; ++i) {
        uint32_t wid;
        unpack(wid);
        if (!wid) continue;
        uint32_t proxy_wid;
        unpack(proxy_wid);
        if (!proxy_wid ||
            (swap_in && (SLSTransactionOrderWindowGroup(transaction, proxy_wid, 1, wid) != kCGErrorSuccess ||
                         SLSTransactionSetWindowSystemAlpha(transaction, wid, 0.0f) != kCGErrorSuccess)) ||
            (!swap_in && (SLSTransactionSetWindowSystemAlpha(transaction, wid, 1.0f) != kCGErrorSuccess ||
                          SLSTransactionOrderWindowGroup(transaction, proxy_wid, 0, wid) != kCGErrorSuccess))) {
            result = false;
            break;
        }
    }
    if (result) result = SLSTransactionCommit(transaction, 0) == kCGErrorSuccess;
    CFRelease(transaction);
    return result;
}

static bool do_window_order(char *message)
{
    uint32_t a_wid;
    int order;
    uint32_t b_wid;
    unpack(a_wid);
    unpack(order);
    unpack(b_wid);
    return a_wid && sa_window_order_is_supported(order) &&
           SLSOrderWindow(SLSMainConnectionID(), a_wid, order, b_wid) == kCGErrorSuccess;
}

static bool do_window_order_in(char *message)
{
    int count = 0;
    unpack(count);
    if (!count) return true;

    CFTypeRef transaction = SLSTransactionCreate(SLSMainConnectionID());
    if (!transaction) return false;
    bool result = true;
    for (int i = 0; i < count; ++i) {
        uint32_t wid;
        unpack(wid);
        if (wid && SLSTransactionOrderWindowGroup(transaction, wid, 1, 0) != kCGErrorSuccess) {
            result = false;
            break;
        }
    }
    if (result) result = SLSTransactionCommit(transaction, 0) == kCGErrorSuccess;
    CFRelease(transaction);
    return result;
}

static inline CFArrayRef cfarray_of_cfnumbers(void *values, size_t size, int count, CFNumberType type)
{
    if (!values || count < 0) return NULL;

    CFMutableArrayRef result = CFArrayCreateMutable(NULL, count, &kCFTypeArrayCallBacks);
    if (!result) return NULL;

    for (int i = 0; i < count; ++i) {
        CFNumberRef number = CFNumberCreate(NULL, type, ((char *) values) + (size * i));
        if (!number) {
            CFRelease(result);
            return NULL;
        }
        CFArrayAppendValue(result, number);
        CFRelease(number);
    }

    return result;
}

static bool do_window_list_move_to_space(char *message)
{
    uint64_t sid;
    int count = 0;
    unpack(sid);
    unpack(count);
    if (!sid) return false;

    CFArrayRef window_list_ref = cfarray_of_cfnumbers(message, sizeof(uint32_t), count, kCFNumberSInt32Type);
    if (!window_list_ref) return false;
    /* SLSMoveWindowsToManagedSpace is void: success means validated and dispatched. */
    SLSMoveWindowsToManagedSpace(SLSMainConnectionID(), window_list_ref, sid);
    CFRelease(window_list_ref);
    return true;
}

static bool do_window_move_to_space(char *message)
{
    uint64_t sid;
    uint32_t wid;
    unpack(sid);
    unpack(wid);
    if (!sid || !wid) return false;

    CFArrayRef window_list_ref = cfarray_of_cfnumbers(&wid, sizeof(wid), 1, kCFNumberSInt32Type);
    if (!window_list_ref) return false;
    /* SLSMoveWindowsToManagedSpace is void: success means validated and dispatched. */
    SLSMoveWindowsToManagedSpace(SLSMainConnectionID(), window_list_ref, sid);
    CFRelease(window_list_ref);
    return true;
}

static bool do_handshake(int sockfd, uint64_t deadline)
{
    uint32_t attrib = 0;

    if (dock_spaces != nil)                attrib |= OSAX_ATTRIB_DOCK_SPACES;
    if (dp_desktop_picture_manager != nil) attrib |= OSAX_ATTRIB_DPPM;
    if (add_space_fp)                      attrib |= OSAX_ATTRIB_ADD_SPACE;
    if (remove_space_fp)                   attrib |= OSAX_ATTRIB_REM_SPACE;
    if (move_space_fp)                     attrib |= OSAX_ATTRIB_MOV_SPACE;
    if (set_front_window_fp)               attrib |= OSAX_ATTRIB_SET_WINDOW;
    if (animation_time_addr)               attrib |= OSAX_ATTRIB_ANIM_TIME;

    char bytes[BUFSIZ] = {};
    int version_length = strlen(OSAX_VERSION);
    int attrib_length = sizeof(uint32_t);
    int bytes_length = version_length + 1 + attrib_length;

    memcpy(bytes, OSAX_VERSION, version_length);
    memcpy(bytes + version_length + 1, &attrib, attrib_length);
    bytes[version_length] = '\0';
    bytes[bytes_length] = '\n';

    return socket_io_write_all_until(sockfd, bytes, (size_t) bytes_length + 1, deadline);
}

static bool operation_is_supported(enum sa_opcode op)
{
    switch (op) {
    case SA_OPCODE_SPACE_FOCUS:
        return dock_spaces != nil;
    case SA_OPCODE_SPACE_CREATE:
        return dock_spaces != nil && add_space_fp != 0;
    case SA_OPCODE_SPACE_DESTROY:
        return dock_spaces != nil && remove_space_fp != 0;
    case SA_OPCODE_SPACE_MOVE:
        return dock_spaces != nil && dp_desktop_picture_manager != nil && move_space_fp != 0;
    case SA_OPCODE_WINDOW_FOCUS:
        return set_front_window_fp != 0;
    default:
        return true;
    }
}

static bool handle_message(int sockfd, char *message, size_t length)
{
    if (!sa_message_is_valid(message, length)) {
        return sa_message_send_failure_status(sockfd);
    }

    enum sa_opcode op = *message++;
    if (op == SA_OPCODE_HANDSHAKE) {
        return do_handshake(sockfd, socket_io_deadline_after_ms(SOCKET_IO_PRODUCTION_TIMEOUT_MS));
    }

    bool status = false;
    if (!operation_is_supported(op)) {
        return sa_message_send_failure_status(sockfd);
    }

#ifdef TESTS
    if (payload_test_operation_dispatch) {
        status = payload_test_operation_dispatch(op, message);
    } else
#endif
    switch (op) {
    case SA_OPCODE_SPACE_FOCUS: {
        status = do_space_focus(message);
    } break;
    case SA_OPCODE_SPACE_CREATE: {
        status = do_space_create(message);
    } break;
    case SA_OPCODE_SPACE_DESTROY: {
        status = do_space_destroy(message);
    } break;
    case SA_OPCODE_SPACE_MOVE: {
        status = do_space_move(message);
    } break;
    case SA_OPCODE_WINDOW_MOVE: {
        status = do_window_move(message);
    } break;
    case SA_OPCODE_WINDOW_OPACITY: {
        status = do_window_opacity(message);
    } break;
    case SA_OPCODE_WINDOW_OPACITY_FADE: {
        status = do_window_opacity_fade(message);
    } break;
    case SA_OPCODE_WINDOW_LAYER: {
        status = do_window_layer(message);
    } break;
    case SA_OPCODE_WINDOW_STICKY: {
        status = do_window_sticky(message);
    } break;
    case SA_OPCODE_WINDOW_SHADOW: {
        status = do_window_shadow(message);
    } break;
    case SA_OPCODE_WINDOW_FOCUS: {
        status = do_window_focus(message);
    } break;
    case SA_OPCODE_WINDOW_SCALE: {
        status = do_window_scale(message);
    } break;
    case SA_OPCODE_WINDOW_SWAP_PROXY_IN: {
        status = do_window_swap_proxy(message, true);
    } break;
    case SA_OPCODE_WINDOW_SWAP_PROXY_OUT: {
        status = do_window_swap_proxy(message, false);
    } break;
    case SA_OPCODE_WINDOW_ORDER: {
        status = do_window_order(message);
    } break;
    case SA_OPCODE_WINDOW_ORDER_IN: {
        status = do_window_order_in(message);
    } break;
    case SA_OPCODE_WINDOW_LIST_TO_SPACE: {
        status = do_window_list_move_to_space(message);
    } break;
    case SA_OPCODE_WINDOW_TO_SPACE: {
        status = do_window_move_to_space(message);
    } break;
    default: {
        status = 0;
    } break;
    }

    return sa_message_send_status(sockfd, status);
}

static inline int read_message(int sockfd, char *message)
{
    int16_t bytes_to_read = 0;
    uint64_t deadline = socket_io_deadline_after_ms(SOCKET_IO_PRODUCTION_TIMEOUT_MS);

    if (!socket_io_read_exact_until(sockfd, &bytes_to_read, sizeof(bytes_to_read), deadline)) return 0;
    if (!sa_message_length_is_valid(bytes_to_read)) {
        (void) sa_message_send_failure_status(sockfd);
        return 0;
    }
    if (!socket_io_read_exact_until(sockfd, message, (size_t) bytes_to_read, deadline)) return 0;

    return bytes_to_read;
}

static void *handle_connection(void *unused)
{
    for (;;) {
        int sockfd = accept(daemon_sockfd, NULL, 0);
        if (sockfd == -1) continue;
        if (!socket_set_cloexec(sockfd)) {
            close(sockfd);
            continue;
        }

        // Unlike yabai, Dock does not install a process-wide SIGPIPE handler.
        int no_sigpipe = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0) {
            close(sockfd);
            continue;
        }

        char message[SA_SOCKET_BUFF_LEN];
        int message_length = read_message(sockfd, message);
        if (message_length) {
            handle_message(sockfd, message, (size_t) message_length);
        }

        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    }

    return NULL;
}

static TABLE_HASH_FUNC(hash_wid)
{
    return *(uint32_t *) key;
}

static TABLE_COMPARE_FUNC(compare_wid)
{
    return *(uint32_t *) key_a == *(uint32_t *) key_b;
}

static bool start_daemon(char *socket_path, uid_t uid)
{
    struct yabai_bound_entry bound_entry = {0};
    bool fade_lock_initialized = false;
    bool fade_table_initialized = false;
    struct sockaddr_un socket_address;
    if (!socket_address_init(&socket_address, socket_path)) return false;

    daemon_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (daemon_sockfd == -1) return false;
    if (!socket_set_cloexec(daemon_sockfd)) goto fail;

    if (!yabai_runtime_remove_stale_socket(socket_address.sun_path, uid)) goto fail;
    if (bind(daemon_sockfd, (struct sockaddr *) &socket_address, sizeof(socket_address)) == -1 ||
        !yabai_runtime_capture_bound_socket(socket_address.sun_path, uid, &bound_entry)) goto fail;

    if (chmod(socket_address.sun_path, 0600) != 0 ||
        !yabai_runtime_validate_bound_socket(socket_address.sun_path, &bound_entry)) goto fail;
    if (listen(daemon_sockfd, SOMAXCONN) == -1) goto fail;

    init_instances();
    if (pthread_mutex_init(&window_fade_lock, NULL) != 0) goto fail;
    fade_lock_initialized = true;
    if (!table_try_init(&window_fade_table, 150, hash_wid, compare_wid)) goto fail;
    fade_table_initialized = true;

    if (pthread_create(&daemon_thread, NULL, &handle_connection, NULL) != 0) goto fail;
    return true;

fail:
    if (fade_table_initialized) {
        table_free(&window_fade_table);
        memset(&window_fade_table, 0, sizeof(window_fade_table));
    }
    if (fade_lock_initialized) pthread_mutex_destroy(&window_fade_lock);
    if (daemon_sockfd != -1) close(daemon_sockfd);
    daemon_sockfd = -1;
    yabai_runtime_cleanup_bound_socket(socket_address.sun_path, &bound_entry);
    return false;
}

#ifndef TESTS
__attribute__((constructor))
#endif
void load_payload(void)
{
    NSLog(@"[yabai-sa] loaded payload..");

    struct yabai_runtime_paths runtime_paths;
    uid_t uid = getuid();
    if (!yabai_runtime_prepare_for_uid(uid, NULL, true, &runtime_paths)) {
        NSLog(@"[yabai-sa] could not prepare secure runtime directory! abort..");
        return;
    }
    sa_lock_fd = yabai_runtime_acquire_lock(runtime_paths.sa_lock, uid);
    if (sa_lock_fd == -1) {
        NSLog(@"[yabai-sa] could not acquire secure lifecycle lock! abort..");
        return;
    }

    if (start_daemon(runtime_paths.sa_socket, uid)) {
        NSLog(@"[yabai-sa] now listening..");
    } else {
        close(sa_lock_fd);
        sa_lock_fd = -1;
        NSLog(@"[yabai-sa] failed to spawn thread..");
    }
}
