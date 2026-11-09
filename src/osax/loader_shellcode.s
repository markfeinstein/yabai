// Position-independent bootstrap copied from loader_shellcode_start through
// loader_shellcode_end. Keep the control ABI offsets in sync with
// loader_protocol.h (bootstrap=4, child=8, detail=12, child_thread_id=16).

.text
.p2align 12
.globl _loader_shellcode_start
.globl _loader_shellcode_end
.globl _loader_shellcode_pthread_create_slot
.globl _loader_shellcode_pthread_threadid_slot
.globl _loader_shellcode_pthread_join_slot
.globl _loader_shellcode_dlopen_slot
.globl _loader_shellcode_control_slot
.globl _loader_shellcode_path_slot

_loader_shellcode_start:
    sub sp, sp, #48
    stp x29, x30, [sp, #32]
    stp x19, x20, [sp, #16]
    add x29, sp, #32
    adrp x9, _loader_shellcode_control_slot@PAGE
    add x9, x9, _loader_shellcode_control_slot@PAGEOFF
    ldr x19, [x9]
    str wzr, [x19, #4]
    str wzr, [x19, #8]
    str wzr, [x19, #12]
    str xzr, [x19, #16]

    mov x0, sp
    mov x1, xzr
    adrp x2, Lchild@PAGE
    add x2, x2, Lchild@PAGEOFF
    paciza x2
    mov x3, x19
    adrp x9, _loader_shellcode_pthread_create_slot@PAGE
    add x9, x9, _loader_shellcode_pthread_create_slot@PAGEOFF
    ldr x9, [x9]
    blr x9
    cbz w0, Lcreated
    str w0, [x19, #12]
    dmb ish
    mov w9, #1
    str w9, [x19, #4]
    b Lspin

Lcreated:
    ldr x20, [sp]
    mov x0, x20
    add x1, x19, #16
    adrp x9, _loader_shellcode_pthread_threadid_slot@PAGE
    add x9, x9, _loader_shellcode_pthread_threadid_slot@PAGEOFF
    ldr x9, [x9]
    blr x9
    cbz w0, Lready
    str w0, [x19, #12]
Lready:
    dmb ish
    mov w9, #2
    str w9, [x19, #4]
    mov x0, x20
    add x1, sp, #8
    adrp x9, _loader_shellcode_pthread_join_slot@PAGE
    add x9, x9, _loader_shellcode_pthread_join_slot@PAGEOFF
    ldr x9, [x9]
    blr x9
    cbz w0, Ljoined
    str w0, [x19, #12]
    dmb ish
    mov w9, #5
    str w9, [x19, #4]
    b Lspin
Ljoined:
    ldr w9, [x19, #8]
    cmp w9, #1
    b.ne Lload_fail
    dmb ish
    mov w9, #3
    str w9, [x19, #4]
    b Lspin
Lload_fail:
    dmb ish
    mov w9, #4
    str w9, [x19, #4]
Lspin:
    yield
    b Lspin

.p2align 4
Lchild:
    pacibsp
    sub sp, sp, #32
    stp x29, x30, [sp, #16]
    str x19, [sp, #8]
    add x29, sp, #16
    mov x19, x0
    adrp x0, _loader_shellcode_path_slot@PAGE
    add x0, x0, _loader_shellcode_path_slot@PAGEOFF
    mov x1, #1
    adrp x9, _loader_shellcode_dlopen_slot@PAGE
    add x9, x9, _loader_shellcode_dlopen_slot@PAGEOFF
    ldr x9, [x9]
    blr x9
    cbz x0, Lchild_fail
    mov w9, #1
    str w9, [x19, #8]
    b Lchild_return
Lchild_fail:
    mov w9, #2
    str w9, [x19, #8]
Lchild_return:
    mov x0, xzr
    ldr x19, [sp, #8]
    ldp x29, x30, [sp, #16]
    add sp, sp, #32
    retab

.p2align 3
_loader_shellcode_pthread_create_slot:
    .quad 0
_loader_shellcode_pthread_threadid_slot:
    .quad 0
_loader_shellcode_pthread_join_slot:
    .quad 0
_loader_shellcode_dlopen_slot:
    .quad 0
_loader_shellcode_control_slot:
    .quad 0
_loader_shellcode_path_slot:
    .space 256
_loader_shellcode_end:
