/*
 * vCPU threads and the KVM_RUN exit loop.
 *
 * TODO: thread per vCPU (stage 7, SMP)
 * TODO: guest poweroff (only explicit reset is currently supported)
 * TODO: kick a vCPU out of KVM_RUN (signal + immediate_exit)
 * TODO: per-exit-reason counters and timing for the performance work
 */

#ifndef MINIVMM_VCPU_H
#define MINIVMM_VCPU_H

#include <stddef.h>

struct kvm_run;
struct vmm;

struct vcpu {
    const struct vmm *vm; /* borrowed; the VM must outlive this vCPU */
    int fd;
    unsigned id;
    struct kvm_run *run; /* shared with KVM, mmap'ed from fd */
    size_t run_size;
};

/* Why vcpu_run() returned. */
enum vcpu_stop {
    VCPU_STOP_HLT,      /* raw guest executed hlt without in-kernel irqchip */
    VCPU_STOP_REBOOT,   /* explicit platform reset, not a triple fault */
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

/* Run until a raw HLT, explicit reset or error; dump registers on errors. */
enum vcpu_stop vcpu_run(struct vcpu *vcpu);

#endif /* MINIVMM_VCPU_H */
