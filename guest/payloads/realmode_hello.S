/*
 * Stage 1 payload: runs in 16-bit real mode.
 *
 * TODO: write "Hello from real mode\n" byte by byte to port 0x3f8 (out %al, %dx);
 *        the VMM sees KVM_EXIT_IO and prints it. Then hlt -> KVM_EXIT_HLT.
 * TODO: later variants: touch an unmapped address (KVM_EXIT_MMIO),
 *        execute an invalid opcode, cause a triple fault.
 */

    .code16
    .section .text.entry, "ax"
    .globl _start
_start:
    hlt
