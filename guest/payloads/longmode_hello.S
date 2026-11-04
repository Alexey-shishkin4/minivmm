/*
 * Stage 2 payload: the VMM sets up 64-bit long mode (page tables, GDT,
 * CR0/CR4/EFER) and jumps here directly.
 *
 * TODO: print via port I/O, then exercise: cpuid (check what the VMM
 *        exposes), rdmsr/wrmsr, an MMIO write to an unmapped GPA,
 *        a write to a read-only page (EPT/NPT violation path).
 */

    .code64
    .section .text.entry, "ax"
    .globl _start
_start:
    hlt
