/*
 * Initial x86 CPU state: real mode, protected mode, 64-bit long mode.
 *
 * TODO: long mode: identity-mapped page tables (PML4/PDPT/PD, 2 MiB pages),
 *        GDT with 64-bit code segment, CR0.PE|PG, CR4.PAE, EFER.LME|LMA
 * TODO: Linux 64-bit boot protocol entry state (RSI = boot_params)
 * TODO: KVM_SET_FPU / KVM_SET_MSRS
 */

#ifndef MINIVMM_X86_CPU_SETUP_H
#define MINIVMM_X86_CPU_SETUP_H

#include <stdint.h>

struct guest_memory;
struct vcpu;

/*
 * Put a vCPU into 16-bit real mode with every segment at base 0, so that
 * guest offsets are guest physical addresses below 64 KiB: CS:IP = 0:ip,
 * SS:SP = 0:ip (the stack grows down below the code), interrupts off.
 * Returns -1 with errno set on failure.
 */
int cpu_setup_real_mode(const struct vcpu *vcpu, uint16_t ip);

int cpu_setup_long_mode(const struct vcpu *vcpu,
                        const struct guest_memory *mem);

#endif /* MINIVMM_X86_CPU_SETUP_H */
