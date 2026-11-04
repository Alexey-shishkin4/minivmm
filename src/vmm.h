/*
 * Top-level VM object: owns KVM fds, guest memory, vCPUs and device buses.
 *
 * TODO: struct vmm: kvm fd, vm fd (KVM_CREATE_VM), memory map, vcpu array,
 *        pio/mmio buses, PCI root, event loop
 * TODO: vmm_create(config) / vmm_run() / vmm_destroy()
 * TODO: KVM_SET_TSS_ADDR, KVM_SET_IDENTITY_MAP_ADDR, KVM_CREATE_IRQCHIP, KVM_CREATE_PIT2
 * TODO: orderly shutdown: guest reboot/poweroff, SIGINT, vCPU errors
 */

#ifndef MINIVMM_VMM_H
#define MINIVMM_VMM_H

struct vmm;

#endif /* MINIVMM_VMM_H */
