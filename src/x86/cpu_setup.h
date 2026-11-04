/*
 * Initial x86 CPU state: real mode, protected mode, 64-bit long mode.
 *
 * TODO: real mode entry for raw payloads (CS:IP, flat segments)
 * TODO: long mode: identity-mapped page tables (PML4/PDPT/PD, 2 MiB pages),
 *        GDT with 64-bit code segment, CR0.PE|PG, CR4.PAE, EFER.LME|LMA
 * TODO: Linux 64-bit boot protocol entry state (RSI = boot_params)
 * TODO: KVM_SET_SREGS / KVM_SET_REGS / KVM_SET_FPU / KVM_SET_MSRS
 */

#ifndef MINIVMM_X86_CPU_SETUP_H
#define MINIVMM_X86_CPU_SETUP_H

struct vcpu;

#endif /* MINIVMM_X86_CPU_SETUP_H */
