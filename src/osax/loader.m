#include <Cocoa/Cocoa.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "loader_protocol.h"

#ifdef __arm64__
#include <ptrauth.h>
static kern_return_t (*thread_convert_thread_state_fn)(thread_act_t, int, thread_state_flavor_t, thread_state_t, mach_msg_type_number_t, thread_state_t, mach_msg_type_number_t *);
#endif

extern const unsigned char loader_shellcode_start[];
extern const unsigned char loader_shellcode_end[];
extern const unsigned char loader_shellcode_pthread_create_slot[];
extern const unsigned char loader_shellcode_pthread_threadid_slot[];
extern const unsigned char loader_shellcode_pthread_join_slot[];
extern const unsigned char loader_shellcode_dlopen_slot[];
extern const unsigned char loader_shellcode_control_slot[];
extern const unsigned char loader_shellcode_path_slot[];

static const char payload_path[] = "/Library/ScriptingAdditions/yabai.osax/Contents/Resources/payload.bundle/Contents/MacOS/payload";
static const uint64_t loader_timeout_ns = 2000000000ull;

static pid_t get_dock_pid(void)
{
    NSArray *list = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"];
    if (list.count == 1 && [list[0] isFinishedLaunching] == YES) return [list[0] processIdentifier];
    return 0;
}

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static bool blob_offset(const unsigned char *symbol, size_t blob_size, size_t width, size_t *offset)
{
    if (symbol < loader_shellcode_start || symbol > loader_shellcode_end) return false;
    size_t value = (size_t)(symbol - loader_shellcode_start);
    if (value > blob_size || width > blob_size - value) return false;
    *offset = value;
    return true;
}

static uint64_t callable_address(void *symbol)
{
#ifdef __arm64__
    return (uint64_t)ptrauth_strip(symbol, ptrauth_key_function_pointer);
#else
    return (uint64_t)symbol;
#endif
}

static int loader_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int result = 1;
    task_t task = MACH_PORT_NULL;
    thread_act_t bootstrap = MACH_PORT_NULL;
    mach_vm_address_t stack = 0;
    mach_vm_address_t code = 0;
    const mach_vm_size_t stack_size = 16 * 1024;
    bool stack_allocated = false;
    bool code_allocated = false;
    bool launched = false;
    bool control_observed = false;
    bool bootstrap_stopped = false;
    bool completion_success = false;
    bool reclaim_success = true;
    loader_control_t control = { .magic = LOADER_CONTROL_MAGIC };

    size_t blob_size = (size_t)(loader_shellcode_end - loader_shellcode_start);
    size_t create_offset, threadid_offset, join_offset, dlopen_offset, control_offset, path_offset;
    if (blob_size == 0 ||
        !blob_offset(loader_shellcode_pthread_create_slot, blob_size, sizeof(uint64_t), &create_offset) ||
        !blob_offset(loader_shellcode_pthread_threadid_slot, blob_size, sizeof(uint64_t), &threadid_offset) ||
        !blob_offset(loader_shellcode_pthread_join_slot, blob_size, sizeof(uint64_t), &join_offset) ||
        !blob_offset(loader_shellcode_dlopen_slot, blob_size, sizeof(uint64_t), &dlopen_offset) ||
        !blob_offset(loader_shellcode_control_slot, blob_size, sizeof(uint64_t), &control_offset) ||
        !blob_offset(loader_shellcode_path_slot, blob_size, sizeof(payload_path), &path_offset) ||
        path_offset + LOADER_PAYLOAD_PATH_CAPACITY > blob_size) {
        fprintf(stderr, "invalid loader shellcode layout\n");
        goto cleanup;
    }

    void *pthread_create_symbol = dlsym(RTLD_DEFAULT, "pthread_create_from_mach_thread");
    void *pthread_threadid_symbol = dlsym(RTLD_DEFAULT, "pthread_threadid_np");
    void *pthread_join_symbol = dlsym(RTLD_DEFAULT, "pthread_join");
    void *dlopen_symbol = dlsym(RTLD_DEFAULT, "dlopen");
    if (!pthread_create_symbol || !pthread_threadid_symbol || !pthread_join_symbol || !dlopen_symbol) {
        fprintf(stderr, "could not resolve loader runtime symbols\n");
        goto cleanup;
    }

#ifdef __arm64__
    void *kernel = dlopen("/usr/lib/system/libsystem_kernel.dylib", RTLD_GLOBAL | RTLD_LAZY);
    if (kernel) {
        thread_convert_thread_state_fn = dlsym(kernel, "thread_convert_thread_state");
        dlclose(kernel);
    }
    if (!thread_convert_thread_state_fn) {
        fprintf(stderr, "could not resolve thread_convert_thread_state\n");
        goto cleanup;
    }
#endif

    pid_t pid = get_dock_pid();
    if (!pid) {
        fprintf(stderr, "could not locate Dock.app pid\n");
        goto cleanup;
    }
    if (task_for_pid(mach_task_self(), pid, &task) != KERN_SUCCESS) {
        fprintf(stderr, "could not retrieve task port for pid: %d\n", pid);
        goto cleanup;
    }
    if (mach_vm_allocate(task, &stack, stack_size, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        fprintf(stderr, "could not allocate stack segment\n");
        goto cleanup;
    }
    stack_allocated = true;
    if ((stack & 7) != 0 || sizeof(control) > stack_size / 4 ||
        mach_vm_write(task, stack, (vm_address_t)&control, sizeof(control)) != KERN_SUCCESS ||
        vm_protect(task, stack, stack_size, false, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS) {
        fprintf(stderr, "could not initialize stack/control segment\n");
        goto cleanup;
    }
    if (mach_vm_allocate(task, &code, blob_size, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        fprintf(stderr, "could not allocate code segment\n");
        goto cleanup;
    }
    code_allocated = true;

    unsigned char *blob = malloc(blob_size);
    if (!blob) goto cleanup;
    memcpy(blob, loader_shellcode_start, blob_size);
    uint64_t value = callable_address(pthread_create_symbol); memcpy(blob + create_offset, &value, sizeof(value));
    value = callable_address(pthread_threadid_symbol); memcpy(blob + threadid_offset, &value, sizeof(value));
    value = callable_address(pthread_join_symbol); memcpy(blob + join_offset, &value, sizeof(value));
    value = callable_address(dlopen_symbol); memcpy(blob + dlopen_offset, &value, sizeof(value));
    value = stack; memcpy(blob + control_offset, &value, sizeof(value));
    memcpy(blob + path_offset, payload_path, sizeof(payload_path));
    kern_return_t kr = mach_vm_write(task, code, (vm_address_t)blob, (mach_msg_type_number_t)blob_size);
    free(blob);
    if (kr != KERN_SUCCESS || vm_protect(task, code, blob_size, false, VM_PROT_EXECUTE | VM_PROT_READ) != KERN_SUCCESS) {
        fprintf(stderr, "could not initialize code segment\n");
        goto cleanup;
    }

#ifdef __x86_64__
    x86_thread_state64_t state = {};
    state.__rip = code;
    state.__rsp = (stack + stack_size / 2) & ~0xfull;
    kr = thread_create_running(task, x86_THREAD_STATE64, (thread_state_t)&state, x86_THREAD_STATE64_COUNT, &bootstrap);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "could not spawn remote thread: %s\n", mach_error_string(kr));
        goto cleanup;
    }
    launched = true;
#elif __arm64__
    arm_thread_state64_t state = {}, machine_state = {};
    mach_msg_type_number_t machine_count = ARM_THREAD_STATE64_COUNT;
    __darwin_arm_thread_state64_set_pc_fptr(state, ptrauth_sign_unauthenticated((void *)code, ptrauth_key_asia, 0));
    __darwin_arm_thread_state64_set_sp(state, (stack + stack_size / 2) & ~0xfull);
    kr = thread_create(task, &bootstrap);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "could not create remote thread: %s\n", mach_error_string(kr));
        goto cleanup;
    }
    kr = thread_convert_thread_state_fn(bootstrap, 2, ARM_THREAD_STATE64, (thread_state_t)&state,
                                         ARM_THREAD_STATE64_COUNT, (thread_state_t)&machine_state, &machine_count);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "could not convert thread state: %s\n", mach_error_string(kr));
        goto cleanup;
    }
    NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
    if ((version.majorVersion == 14 && version.minorVersion >= 4) || version.majorVersion >= 15) {
        thread_act_t old_bootstrap = bootstrap;
        kr = thread_terminate(old_bootstrap);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "could not terminate initial remote thread: %s\n", mach_error_string(kr));
            goto cleanup;
        }
        mach_port_deallocate(mach_task_self(), old_bootstrap);
        bootstrap = MACH_PORT_NULL;
        kr = thread_create_running(task, ARM_THREAD_STATE64, (thread_state_t)&machine_state, machine_count, &bootstrap);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "could not spawn replacement remote thread: %s\n", mach_error_string(kr));
            goto cleanup;
        }
    } else {
        kr = thread_set_state(bootstrap, ARM_THREAD_STATE64, (thread_state_t)&machine_state, machine_count);
        if (kr == KERN_SUCCESS) kr = thread_resume(bootstrap);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "could not start remote thread: %s\n", mach_error_string(kr));
            goto cleanup;
        }
    }
    launched = true;
#endif

    uint64_t start = monotonic_ns();
    for (;;) {
        mach_vm_size_t read_size = 0;
        kr = mach_vm_read_overwrite(task, stack, sizeof(control), (mach_vm_address_t)&control, &read_size);
        if (kr != KERN_SUCCESS || read_size != sizeof(control)) {
            control_observed = false;
            fprintf(stderr, "could not read loader control block\n");
            break;
        }
        control_observed = true;
        uint64_t now = monotonic_ns();
        bool timed_out = start == 0 || now == 0 || now - start >= loader_timeout_ns;
        loader_poll_decision_t decision = loader_control_decision(&control, timed_out);
        if (decision != LOADER_POLL_WAIT) {
            completion_success = decision == LOADER_POLL_SUCCESS;
            if (timed_out) fprintf(stderr, "loader timed out\n");
            break;
        }
        struct timespec delay = { .tv_nsec = 10000000 };
        nanosleep(&delay, NULL);
    }

cleanup:
    ;
    loader_cleanup_plan_t cleanup_plan = loader_cleanup_plan(&control, launched, control_observed);
    if (bootstrap != MACH_PORT_NULL) {
        if (cleanup_plan.terminate_bootstrap) {
            kr = thread_terminate(bootstrap);
            bootstrap_stopped = kr == KERN_SUCCESS;
            if (!bootstrap_stopped) fprintf(stderr, "failed to terminate bootstrap: %s\n", mach_error_string(kr));
        }
        mach_port_deallocate(mach_task_self(), bootstrap);
    } else {
        bootstrap_stopped = !launched;
    }

    bool reclaim = cleanup_plan.reclaim_remote_vm && bootstrap_stopped;
    if (reclaim && task != MACH_PORT_NULL) {
        if (code_allocated && mach_vm_deallocate(task, code, blob_size) != KERN_SUCCESS) reclaim_success = false;
        if (stack_allocated && mach_vm_deallocate(task, stack, stack_size) != KERN_SUCCESS) reclaim_success = false;
    } else if (code_allocated || stack_allocated) {
        fprintf(stderr, "retaining remote VM because injected execution could not be disproved\n");
    }
    if (task != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), task);

    if (completion_success && reclaim && reclaim_success) result = 0;
    return result;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        return loader_main(argc, argv);
    }
}
