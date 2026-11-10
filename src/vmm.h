/*
 * Top-level VM object: owns KVM fds, guest memory, vCPUs and device buses.
 *
 * TODO: vcpu array and vmm_run() (with vcpu.c)
 * TODO: PCI root, event loop
 * TODO: orderly shutdown: guest reboot/poweroff, SIGINT, vCPU errors
 */

#ifndef MINIVMM_VMM_H
#define MINIVMM_VMM_H

#include <stdbool.h>
#include <stdint.h>

#include "devices/bus.h"
#include "devices/i8042.h"
#include "devices/serial8250.h"
#include "kvm.h"
#include "memory.h"

struct vmm_config {
    uint64_t ram_size; /* bytes, page-aligned */
    bool linux_boot;   /* in-kernel PIC/IOAPIC/LAPIC and PIT for Linux */
};

struct vmm {
    struct kvm_system kvm;
    int vm_fd;
    struct guest_memory mem;
    struct bus pio_bus;
    struct bus mmio_bus;
    struct serial8250 serial;
    struct i8042 i8042;
    bool has_irqchip;
};

/*
 * Open KVM, create the VM and give it ram_size bytes of RAM. Logs the failing
 * step and returns -1 with errno set; on failure everything is released.
 */
int vmm_create(struct vmm *vm, const struct vmm_config *cfg);

/* Release in reverse order of creation. Safe on a partly created vmm. */
void vmm_destroy(struct vmm *vm);

#endif /* MINIVMM_VMM_H */
