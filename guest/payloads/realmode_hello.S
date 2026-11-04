/*
 * Stage 1 payload: runs in 16-bit real mode.
 *
 * Writes a greeting byte by byte to port 0x3f8; each out is a KVM_EXIT_IO
 * that the VMM prints. Then hlt -> KVM_EXIT_HLT, minivmm exits with 0.
 *
 * TODO: later variants: touch an unmapped address (KVM_EXIT_MMIO),
 *        execute an invalid opcode, cause a triple fault.
 */

#define COM1 0x3f8

    .code16
    .section .text.entry, "ax"
    .globl _start
_start:
    mov $msg, %si
    mov $COM1, %dx
    call puts
    hlt

/* Print the NUL-terminated string at DS:SI to port DX. */
puts:
    lodsb                   /* al = [ds:si++] */
    test %al, %al
    jz 1f
    out %al, %dx
    jmp puts
1:  ret

    .section .rodata
msg:
    .asciz "Hello from real mode\n"
