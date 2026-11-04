/*
 * Thin wrappers over the /dev/kvm system ioctls.
 *
 * KVM is a hierarchy of fds: /dev/kvm (system) -> KVM_CREATE_VM (vm) ->
 * KVM_CREATE_VCPU (vcpu); each level has its own set of ioctls. This module
 * owns the system fd and what it reports about the host; vmm.c owns the vm
 * fd and vcpu.c the vcpu fds.
 *
 * TODO: KVM_GET_SUPPORTED_CPUID / KVM_GET_MSR_INDEX_LIST (stage 2)
 */

#ifndef MINIVMM_KVM_H
#define MINIVMM_KVM_H

#include <stddef.h>

struct kvm_system {
    int fd;                /* /dev/kvm */
    size_t vcpu_mmap_size; /* mmap length of a vcpu fd (struct kvm_run) */
    unsigned nr_memslots;  /* KVM_CAP_NR_MEMSLOTS */
};

/*
 * Open /dev/kvm, check the API version and every capability minivmm needs.
 * Logs what is missing and returns -1 with errno set; returns 0 on success.
 */
int kvm_open(struct kvm_system *kvm);

void kvm_close(struct kvm_system *kvm);

/*
 * KVM_CHECK_EXTENSION on a system or vm fd: 0 if the capability is absent,
 * a positive value (often a limit) if present, -1 with errno on error.
 */
int kvm_check_extension(int fd, int cap);

#endif /* MINIVMM_KVM_H */
