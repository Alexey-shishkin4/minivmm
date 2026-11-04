/*
 * vCPU threads and the KVM_RUN exit loop.
 *
 * TODO: struct vcpu: fd, id, mmap'ed struct kvm_run, thread
 * TODO: run loop: KVM_RUN -> switch (run->exit_reason):
 *        KVM_EXIT_IO, KVM_EXIT_MMIO -> bus dispatch
 *        KVM_EXIT_HLT, KVM_EXIT_SHUTDOWN, KVM_EXIT_FAIL_ENTRY,
 *        KVM_EXIT_INTERNAL_ERROR, KVM_EXIT_SYSTEM_EVENT
 * TODO: kick a vCPU out of KVM_RUN (signal + immediate_exit)
 * TODO: per-exit-reason counters and timing for the performance work
 * TODO: debug dump of regs/sregs on fatal exits
 */

#ifndef MINIVMM_VCPU_H
#define MINIVMM_VCPU_H

struct vcpu;

#endif /* MINIVMM_VCPU_H */
