#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../../src/osax/loader_protocol.h"

extern const unsigned char loader_shellcode_start[];
extern const unsigned char loader_shellcode_end[];
extern const unsigned char loader_shellcode_pthread_create_slot[];
extern const unsigned char loader_shellcode_pthread_threadid_slot[];
extern const unsigned char loader_shellcode_pthread_join_slot[];
extern const unsigned char loader_shellcode_dlopen_slot[];
extern const unsigned char loader_shellcode_control_slot[];
extern const unsigned char loader_shellcode_path_slot[];

static void assert_slot(const unsigned char *slot, size_t width)
{
    assert(slot >= loader_shellcode_start);
    assert(slot < loader_shellcode_end);
    assert((size_t)(loader_shellcode_end - slot) >= width);
}

static void assert_cleanup(loader_control_t *control, bool launched, bool observed,
                           bool terminate_bootstrap, bool reclaim_remote_vm)
{
    loader_cleanup_plan_t plan = loader_cleanup_plan(control, launched, observed);
    assert(plan.terminate_bootstrap == terminate_bootstrap);
    assert(plan.reclaim_remote_vm == reclaim_remote_vm);
}

int main(void)
{
    loader_control_t control = { .magic = LOADER_CONTROL_MAGIC };
    assert(sizeof(control) == 24);
    assert(loader_control_decision(&control, false) == LOADER_POLL_WAIT);
    assert(loader_control_decision(&control, true) == LOADER_POLL_FAILURE);

    assert_cleanup(&control, true, true, false, false); // INIT timeout
    control.bootstrap_state = LOADER_BOOTSTRAP_READY;
    control.child_state = LOADER_CHILD_LOAD_OK;
    control.child_thread_id = 42;
    assert(loader_control_decision(&control, false) == LOADER_POLL_WAIT);
    assert(!loader_control_proves_child_finished(&control));
    assert_cleanup(&control, true, true, false, false); // READY timeout, even with known child

    control.bootstrap_state = LOADER_BOOTSTRAP_LOAD_OK;
    assert(loader_control_decision(&control, false) == LOADER_POLL_SUCCESS);
    assert(loader_control_decision(&control, true) == LOADER_POLL_FAILURE);
    assert(loader_control_proves_child_finished(&control));
    assert_cleanup(&control, true, true, true, true);

    control.child_state = LOADER_CHILD_LOAD_FAIL;
    assert(loader_control_decision(&control, false) == LOADER_POLL_FAILURE);
    control.bootstrap_state = LOADER_BOOTSTRAP_LOAD_FAIL;
    assert(loader_control_decision(&control, false) == LOADER_POLL_FAILURE);
    assert(loader_control_proves_child_finished(&control));
    assert_cleanup(&control, true, true, true, true);

    control.bootstrap_state = LOADER_BOOTSTRAP_CREATE_FAIL;
    assert(loader_control_proves_child_finished(&control));
    assert_cleanup(&control, true, true, true, true);

    control.bootstrap_state = LOADER_BOOTSTRAP_JOIN_FAIL;
    assert(loader_control_decision(&control, false) == LOADER_POLL_FAILURE);
    assert(!loader_control_proves_child_finished(&control));
    assert_cleanup(&control, true, true, true, false);

    control.bootstrap_state = 99;
    assert(loader_control_decision(&control, false) == LOADER_POLL_FAILURE);
    assert_cleanup(&control, true, true, false, false);
    assert_cleanup(&control, true, false, false, false); // failed control read

    control.magic = 0;
    assert(loader_control_decision(&control, false) == LOADER_POLL_FAILURE);
    assert(!loader_control_proves_child_finished(&control));
    assert_cleanup(&control, true, true, false, false);

    assert_cleanup(&control, false, false, true, true); // suspended/pre-launch

    assert(loader_shellcode_end > loader_shellcode_start);
    assert_slot(loader_shellcode_pthread_create_slot, 8);
    assert_slot(loader_shellcode_pthread_threadid_slot, 8);
    assert_slot(loader_shellcode_pthread_join_slot, 8);
    assert_slot(loader_shellcode_dlopen_slot, 8);
    assert_slot(loader_shellcode_control_slot, 8);
    assert_slot(loader_shellcode_path_slot, LOADER_PAYLOAD_PATH_CAPACITY);
    assert(((uintptr_t)loader_shellcode_pthread_create_slot & 7u) == 0);
    assert(((uintptr_t)loader_shellcode_control_slot & 7u) == 0);
    return 0;
}
