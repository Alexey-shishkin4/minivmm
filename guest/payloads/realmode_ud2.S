/*
 * Stage 1 experiment: an exception is handled by the guest, not the VMM.
 *
 * ud2 raises #UD (vector 6). In real mode the CPU looks the handler up in
 * the IVT at address 0 (4 bytes per vector: offset, then segment), pushes
 * FLAGS, CS and IP and jumps there; KVM does not exit for it. #UD is a
 * fault, so the pushed IP points at ud2 itself, not past it.
 */

#define COM1 0x3f8
#define UD_VECTOR 6

    .code16
    .section .text.entry, "ax"
    .globl _start
_start:
    movw $ud_handler, UD_VECTOR * 4
    movw $0, UD_VECTOR * 4 + 2
faulting:
    ud2
    hlt                     /* not reached: the handler does not return */

ud_handler:
    mov %sp, %bp
    mov $at_ud2, %si
    cmpw $faulting, (%bp)   /* IP pushed by the CPU */
    je 1f
    mov $elsewhere, %si
1:  mov $COM1, %dx
    call puts
    hlt

/* Print the NUL-terminated string at DS:SI to port DX. */
puts:
    lodsb
    test %al, %al
    jz 1f
    out %al, %dx
    jmp puts
1:  ret

    .section .rodata
at_ud2:
    .asciz "#UD at ud2\n"
elsewhere:
    .asciz "#UD with a wrong return address\n"
