/*
 * Stage 2: the VMM enters this payload directly in 64-bit mode.
 * Run with --mode long --mem 1M: GPA 0x200000 must not be backed by RAM.
 */

#define COM1 0x3f8
#define MMIO_ADDR 0x200000

    .code64
    .section .text.entry, "ax"
    .globl _start
_start:
    cld
    lea hello(%rip), %rsi
    call puts

    /* Check that the upper half of a 64-bit register is preserved. */
    movabs $0x1122334455667788, %rax
    mov %rax, %rbx
    shr $32, %rbx
    cmp $0x11223344, %ebx
    jne fail_64bit

    lea arithmetic_ok(%rip), %rsi
    call puts

    /* Check that basic leaf 1 exists, then test the hypervisor bit. */
    xor %eax, %eax
    xor %ecx, %ecx
    cpuid
    cmp $1, %eax
    jb fail_cpuid

    mov $1, %eax
    xor %ecx, %ecx
    cpuid
    bt $31, %ecx
    jnc fail_cpuid

    /* Check that the extended feature leaf exists, then test LM. */
    mov $0x80000000, %eax
    xor %ecx, %ecx
    cpuid
    cmp $0x80000001, %eax
    jb fail_cpuid

    mov $0x80000001, %eax
    xor %ecx, %ecx
    cpuid
    bt $29, %edx
    jnc fail_cpuid

    lea cpuid_ok(%rip), %rsi
    call puts

    /*
     * The GVA is identity-mapped, but its GPA has no RAM memslot.
     * Both accesses should produce KVM_EXIT_MMIO.
     */
    movb $0x42, MMIO_ADDR
    movzbl MMIO_ADDR, %eax
    cmp $0xff, %eax
    jne fail_mmio

    lea mmio_ok(%rip), %rsi
    call puts
    lea passed(%rip), %rsi
    call puts
    hlt

fail_64bit:
    lea arithmetic_failed(%rip), %rsi
    jmp fail

fail_cpuid:
    lea cpuid_failed(%rip), %rsi
    jmp fail

fail_mmio:
    lea mmio_failed(%rip), %rsi

fail:
    call puts
    /* Empty IDT: #UD escalates to a triple fault, so the VMM returns 1. */
    ud2
    hlt

/* Print DS:RSI as a NUL-terminated string. Clobbers RAX, RDX, RSI, flags. */
puts:
    mov $COM1, %dx
1:  lodsb
    test %al, %al
    jz 2f
    out %al, %dx
    jmp 1b
2:  ret

    .section .rodata
hello:
    .asciz "Hello from long mode\n"
arithmetic_ok:
    .asciz "64-bit arithmetic: OK\n"
cpuid_ok:
    .asciz "CPUID: OK\n"
mmio_ok:
    .asciz "MMIO: OK\n"
passed:
    .asciz "longmode: PASS\n"
arithmetic_failed:
    .asciz "longmode: FAIL (64-bit arithmetic)\n"
cpuid_failed:
    .asciz "longmode: FAIL (CPUID)\n"
mmio_failed:
    .asciz "longmode: FAIL (MMIO)\n"
