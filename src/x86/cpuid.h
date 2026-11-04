/*
 * CPUID table presented to the guest.
 *
 * TODO: start from KVM_GET_SUPPORTED_CPUID, set leaf 0x1 APIC id per vCPU,
 *        hypervisor bit + KVM signature leaves 0x40000000.., KVM_SET_CPUID2
 * TODO: decide what to hide (and why) -- document security-relevant bits
 *        (e.g. speculation-control enumeration)
 */

#ifndef MINIVMM_X86_CPUID_H
#define MINIVMM_X86_CPUID_H

struct vcpu;

#endif /* MINIVMM_X86_CPUID_H */
