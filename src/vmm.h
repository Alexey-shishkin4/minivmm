/*
 * Top-level VM object: owns KVM fds, guest memory, vCPUs and device buses.
 *
 * TODO: vcpu array and vmm_run() (with vcpu.c)
 * TODO: pio/mmio buses, PCI root, event loop
 * TODO: KVM_CREATE_IRQCHIP, KVM_CREATE_PIT2 (stage 4)
 * TODO: orderly shutdown: guest reboot/poweroff, SIGINT, vCPU errors
 */

#ifndef MINIVMM_VMM_H
#define MINIVMM_VMM_H

#include <stdint.h>

#include "kvm.h"
#include "memory.h"

struct vmm_config {
    uint64_t ram_size; /* bytes, page-aligned */
};

struct vmm {
    struct kvm_system kvm;
    int vm_fd;
    struct guest_memory mem;
};

/*
 * Open KVM, create the VM and give it ram_size bytes of RAM. Logs the failing
 * step and returns -1 with errno set; on failure everything is released.
 */
int vmm_create(struct vmm *vm, const struct vmm_config *cfg);

/* Release in reverse order of creation. Safe on a partly created vmm. */
void vmm_destroy(struct vmm *vm);

#endif /* MINIVMM_VMM_H */
