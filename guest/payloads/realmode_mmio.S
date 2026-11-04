/*
 * Stage 1 experiment: a guest physical address with no memslot behind it.
 *
 * 0xffff:0x0010 is 0x100000, the first byte above 1 MiB; run with --mem 1M so
 * that it is not RAM. Both accesses exit with KVM_EXIT_MMIO: the write shows
 * up in the VMM log, the read completes with what the VMM answers (0xff, as
 * on an empty bus) on the next KVM_RUN.
 */

#define COM1 0x3f8

    .code16
    .section .text.entry, "ax"
    .globl _start
_start:
    mov $0xffff, %ax
    mov %ax, %ds
    movb $0x42, 0x10        /* write: is_write = 1, data = 0x42 */
    mov 0x10, %al           /* read: al = whatever the VMM put in mmio.data */
    xor %bx, %bx
    mov %bx, %ds

    mov $got_ones, %si
    cmp $0xff, %al
    je 1f
    mov $got_other, %si
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
got_ones:
    .asciz "mmio read 0xff\n"
got_other:
    .asciz "mmio read something else\n"
