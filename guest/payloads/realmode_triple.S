/*
 * Stage 1 experiment: a triple fault, KVM_EXIT_SHUTDOWN.
 *
 * With an IDT limit of 0 no vector can be delivered: int3 (vector 3) is
 * outside the table -> #GP, #GP is outside too -> #DF, and a fault while
 * delivering #DF shuts the CPU down. minivmm reports it and exits with 1.
 */

    .code16
    .section .text.entry, "ax"
    .globl _start
_start:
    lidt null_idt
    int3
    hlt                     /* not reached */

    .section .rodata
null_idt:
    .word 0                 /* limit */
    .long 0                 /* base */
