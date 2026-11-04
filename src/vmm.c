#include "vmm.h"

#include <errno.h>
#include <inttypes.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util/log.h"

#define VMM_TSS_ADDR 0xFFFBD000UL

static int create_vm(int kvm_fd)
{
    int fd;

    do {
        fd = ioctl(kvm_fd, KVM_CREATE_VM, 0UL);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

int vmm_create(struct vmm *vm, const struct vmm_config *cfg)
{
    int saved_errno;

    vm->vm_fd = -1;
    vm->mem.nr_regions = 0;

    if (kvm_open(&vm->kvm) < 0) {
        return -1;
    }

    vm->vm_fd = create_vm(vm->kvm.fd);
    if (vm->vm_fd < 0) {
        log_errno("KVM_CREATE_VM");
        goto fail;
    }

    if (ioctl(vm->vm_fd, KVM_SET_TSS_ADDR, VMM_TSS_ADDR) < 0) {
        log_errno("KVM_SET_TSS_ADDR");
        goto fail;
    }

    if (guest_memory_init(&vm->mem, cfg->ram_size) < 0) {
        log_errno("allocate %" PRIu64 " bytes of guest RAM", cfg->ram_size);
        goto fail;
    }
    if (guest_memory_register(&vm->mem, vm->vm_fd) < 0) {
        log_errno("KVM_SET_USER_MEMORY_REGION");
        goto fail;
    }

    log_info("vm: %" PRIu64 " MiB RAM in %u memslot(s)", cfg->ram_size >> 20U,
             vm->mem.nr_regions);
    return 0;

fail:
    saved_errno = errno;
    vmm_destroy(vm);
    errno = saved_errno;
    return -1;
}

void vmm_destroy(struct vmm *vm)
{
    /* vCPU fds go first once they exist: each holds a reference to the VM. */
    if (vm->vm_fd >= 0) {
        close(vm->vm_fd);
        vm->vm_fd = -1;
    }
    /* Only now: while the VM lives, KVM may still access this memory. */
    guest_memory_destroy(&vm->mem);
    kvm_close(&vm->kvm);
}
