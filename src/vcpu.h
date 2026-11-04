/*
 * vCPU threads and the KVM_RUN exit loop.
 *
 * TODO: thread per vCPU (stage 5, SMP)
 * TODO: KVM_EXIT_IO, KVM_EXIT_MMIO -> bus dispatch (stage 3)
 * TODO: KVM_EXIT_SYSTEM_EVENT (guest reboot/poweroff)
 * TODO: kick a vCPU out of KVM_RUN (signal + immediate_exit)
 * TODO: per-exit-reason counters and timing for the performance work
 * TODO: debug dump of regs/sregs on fatal exits (stage 2)
 */

#ifndef MINIVMM_VCPU_H
#define MINIVMM_VCPU_H

#include <stddef.h>

struct kvm_run;
struct vmm;

struct vcpu {
    int fd;
    unsigned id;
    struct kvm_run *run; /* shared with KVM, mmap'ed from fd */
    size_t run_size;
};

/* Why vcpu_run() returned. */
enum vcpu_stop {
    VCPU_STOP_HLT,      /* guest executed hlt (no in-kernel irqchip yet) */
    VCPU_STOP_SHUTDOWN, /* triple fault: the CPU gave up on the guest */
    VCPU_STOP_ERROR,    /* failed entry, internal error, unhandled exit */
};

/*
 * Create vCPU number id in vm and map its struct kvm_run. Logs the failing
 * step and returns -1 with errno set; on failure everything is released.
 */
int vcpu_create(struct vcpu *vcpu, const struct vmm *vm, unsigned id);

/* Unmap kvm_run and close the fd. Safe on a partly created vcpu. */
void vcpu_destroy(struct vcpu *vcpu);

/* Run the guest until it stops; the reason is logged unless it is hlt. */
enum vcpu_stop vcpu_run(struct vcpu *vcpu);

#endif /* MINIVMM_VCPU_H */
