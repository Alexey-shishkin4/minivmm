/* Exercise the public vcpu_run() interface without accessing /dev/kvm. */
#include <errno.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tap.h"
#include "vcpu.h"
#include "vmm.h"

static struct kvm_run *active_run;
static unsigned entries;

int __wrap_ioctl(int fd, unsigned long request, ...);

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    if (request == KVM_RUN) {
        if (active_run->immediate_exit) {
            entries++;
            errno = EINTR;
            return -1;
        }
        if (entries++ != 0) {
            active_run->exit_reason = KVM_EXIT_HLT;
        }
        return 0;
    }
    errno = EIO;
    return -1;
}

struct device {
    unsigned calls;
    uint8_t written[8];
    bool wrong_offset;
};

static int access_byte(void *opaque, uint64_t offset, void *data, size_t len,
                       bool is_write)
{
    struct device *dev = opaque;
    uint8_t *byte = data;

    if (len != 1 || dev->calls >= sizeof(dev->written)) {
        errno = EINVAL;
        return -1;
    }
    dev->wrong_offset = dev->wrong_offset || offset != 0;
    if (is_write) {
        dev->written[dev->calls] = *byte;
    } else {
        *byte = (uint8_t)(0xa0U + dev->calls);
    }
    dev->calls++;
    return 0;
}

int main(void)
{
    struct vmm vm = {0};
    struct device dev;
    void *mapping = calloc(1, 8192);
    struct vcpu vcpu = {.vm = &vm, .fd = -1, .run = mapping, .run_size = 8192};
    uint8_t *data;

    tap_plan(14);
    if (mapping == NULL) {
        return 1;
    }
    active_run = mapping;
    data = (uint8_t *)mapping + 4096;

    for (unsigned write = 0; write <= 1; write++) {
        bus_init(&vm.pio_bus, UINT16_MAX);
        dev = (struct device){0};
        if (bus_register(&vm.pio_bus, 500, 1, access_byte, &dev) < 0) {
            return 1;
        }
        *active_run = (struct kvm_run){.exit_reason = KVM_EXIT_IO};
        active_run->io.port = 500;
        active_run->io.size = 1;
        active_run->io.count = 3;
        active_run->io.direction = write ? KVM_EXIT_IO_OUT : KVM_EXIT_IO_IN;
        active_run->io.data_offset = 4096;
        memcpy(data, "ABC", 3);
        entries = 0;
        tap_ok(vcpu_run(&vcpu) == VCPU_STOP_HLT && dev.calls == 3 &&
                   !dev.wrong_offset &&
                   (write ? memcmp(dev.written, "ABC", 3) == 0
                          : memcmp(data, "\xa0\xa1\xa2", 3) == 0),
               write ? "REP OUT: three writes to the same port"
                     : "REP IN: three results in consecutive buffer slots");
    }

    bus_init(&vm.pio_bus, UINT16_MAX);
    active_run->exit_reason = KVM_EXIT_IO;
    active_run->io.direction = KVM_EXIT_IO_IN;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_HLT &&
               memcmp(data, "\xff\xff\xff", 3) == 0,
           "unmapped repeated PIO reads return ones");
    active_run->exit_reason = KVM_EXIT_IO;
    active_run->io.data_offset = 8191;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_ERROR && entries == 1,
           "PIO buffer overflow rejected before access");

    for (unsigned write = 0; write <= 1; write++) {
        bus_init(&vm.mmio_bus, UINT64_MAX);
        dev = (struct device){0};
        if (bus_register(&vm.mmio_bus, 0x200000, 1, access_byte, &dev) < 0) {
            return 1;
        }
        *active_run = (struct kvm_run){.exit_reason = KVM_EXIT_MMIO};
        active_run->mmio.phys_addr = 0x200000;
        active_run->mmio.len = 1;
        active_run->mmio.is_write = (uint8_t)write;
        active_run->mmio.data[0] = 0x42;
        entries = 0;
        tap_ok(vcpu_run(&vcpu) == VCPU_STOP_HLT && dev.calls == 1 &&
                   !dev.wrong_offset &&
                   (write ? dev.written[0] == 0x42
                          : active_run->mmio.data[0] == 0xa0),
               write ? "MMIO write reaches callback"
                     : "MMIO read fills shared result");
    }

    bus_init(&vm.mmio_bus, UINT64_MAX);
    active_run->exit_reason = KVM_EXIT_MMIO;
    active_run->mmio.is_write = 0;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_HLT && active_run->mmio.data[0] == 0xff,
           "unmapped MMIO read returns ones");
    active_run->exit_reason = KVM_EXIT_MMIO;
    active_run->mmio.len = 9;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_ERROR && entries == 1,
           "oversized MMIO rejected before touching data");
    active_run->exit_reason = KVM_EXIT_IO;
    active_run->io.size = 3;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_ERROR && entries == 1,
           "invalid PIO width rejected");

    bus_init(&vm.pio_bus, UINT16_MAX);
    if (bus_register(&vm.pio_bus, I8042_COMMAND_PORT, 1, i8042_access,
                     &vm.i8042) < 0) {
        return 1;
    }
    vm.has_irqchip = true;
    *active_run = (struct kvm_run){.exit_reason = KVM_EXIT_IO};
    active_run->io.port = I8042_COMMAND_PORT;
    active_run->io.size = 1;
    active_run->io.count = 1;
    active_run->io.direction = KVM_EXIT_IO_OUT;
    active_run->io.data_offset = 4096;
    data[0] = 0xfe;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_REBOOT && vm.i8042.reset_requested &&
               entries == 2 && active_run->immediate_exit == 0,
           "reset OUT completed with immediate_exit before clean stop");
    vm.i8042.reset_requested = false;
    active_run->exit_reason = KVM_EXIT_HLT;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_ERROR,
           "Linux HLT is not reported as successful boot/shutdown");
    active_run->exit_reason = KVM_EXIT_SHUTDOWN;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_SHUTDOWN,
           "Linux triple fault remains an error, not a reboot");
    active_run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
    active_run->system_event.type = KVM_SYSTEM_EVENT_RESET;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_REBOOT,
           "explicit KVM reset event stops cleanly");
    active_run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
    active_run->system_event.type = KVM_SYSTEM_EVENT_CRASH;
    entries = 0;
    tap_ok(vcpu_run(&vcpu) == VCPU_STOP_ERROR,
           "crash event is not accepted as successful reboot");

    free(mapping);
    return tap_done();
}
