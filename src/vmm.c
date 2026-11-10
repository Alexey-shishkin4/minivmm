#include "vmm.h"

#include <errno.h>
#include <inttypes.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util/log.h"

#define VMM_TSS_ADDR 0xFFFBD000UL
#define COM1_BASE 0x3f8

static int serial_irq(void *opaque, bool level)
{
    const struct vmm *vm = opaque;
    const struct kvm_irq_level irq = {.irq = 4, .level = level ? 1U : 0U};

    return ioctl(vm->vm_fd, KVM_IRQ_LINE, &irq);
}

static int setup_linux_platform(struct vmm *vm)
{
    const struct kvm_pit_config pit = {0};

    if (kvm_check_extension(vm->kvm.fd, KVM_CAP_IRQCHIP) <= 0 ||
        kvm_check_extension(vm->kvm.fd, KVM_CAP_PIT2) <= 0) {
        errno = ENOTSUP;
        log_error("Linux boot requires KVM_CAP_IRQCHIP and KVM_CAP_PIT2");
        return -1;
    }
    if (ioctl(vm->vm_fd, KVM_CREATE_IRQCHIP, 0UL) < 0) {
        log_errno("KVM_CREATE_IRQCHIP");
        return -1;
    }
    if (ioctl(vm->vm_fd, KVM_CREATE_PIT2, &pit) < 0) {
        log_errno("KVM_CREATE_PIT2");
        return -1;
    }
    vm->has_irqchip = true;
    if (serial8250_set_irq(&vm->serial, serial_irq, vm) < 0 ||
        bus_register(&vm->pio_bus, I8042_COMMAND_PORT, 1, i8042_access,
                     &vm->i8042) < 0) {
        log_errno("register Linux platform devices");
        return -1;
    }
    return 0;
}

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

    *vm = (struct vmm){.kvm = {.fd = -1}, .vm_fd = -1};
    bus_init(&vm->pio_bus, UINT16_MAX);
    bus_init(&vm->mmio_bus, UINT64_MAX);
    serial8250_init(&vm->serial, STDOUT_FILENO);

    if (bus_register(&vm->pio_bus, COM1_BASE, SERIAL8250_REG_COUNT,
                     serial8250_access, &vm->serial) < 0) {
        log_errno("register COM1 UART");
        return -1;
    }

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

    /* The in-kernel irqchip must exist before creating the first vCPU. */
    if (cfg->linux_boot && setup_linux_platform(vm) < 0) {
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
