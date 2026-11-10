#include <errno.h>
#include <linux/kvm.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "boot/layout.h"
#include "boot/linux_loader.h"
#include "memory.h"
#include "tap.h"
#include "vcpu.h"
#include "x86/cpu_setup.h"

static struct kvm_regs regs;
static struct kvm_sregs sregs;
static bool fail_set;

int __wrap_ioctl(int fd, unsigned long request, ...);

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    void *arg;
    (void)fd;
    va_start(args, request);
    arg = va_arg(args, void *);
    va_end(args);
    switch (request) {
    case KVM_GET_SREGS:
        memset(arg, 0, sizeof(sregs));
        return 0;
    case KVM_SET_SREGS:
        sregs = *(struct kvm_sregs *)arg;
        break;
    case KVM_SET_REGS:
        regs = *(struct kvm_regs *)arg;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    if (fail_set) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int main(void)
{
    struct guest_memory mem;
    struct vcpu vcpu = {.fd = -1};
    const struct linux_boot boot = {.entry = 0x1000200,
                                    .params_addr = BOOT_PARAMS_GPA};
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *gdt;

    tap_plan(7);
    if (guest_memory_init(&mem, 64ULL << 20U) < 0) {
        return 1;
    }
    tap_ok(cpu_setup_linux(&vcpu, &mem, &boot) == 0 && regs.rip == boot.entry &&
               regs.rsi == BOOT_PARAMS_GPA && regs.rflags == 2 &&
               regs.rbx == 0 && regs.rdi == 0 && regs.rbp == 0,
           "Linux entry registers follow 64-bit boot ABI, interrupts disabled");
    tap_ok(
        sregs.cs.selector == 0x10 && sregs.cs.l == 1 && sregs.cs.db == 0 &&
            sregs.ds.selector == 0x18 && sregs.es.selector == 0x18 &&
            sregs.ss.selector == 0x18 && sregs.gdt.limit == 31,
        "Linux uses BOOT_CS=0x10 and BOOT_DS=0x18 in GDT and cached segments");
    pml4 = gpa_to_hva(&mem, BOOT_TABLES_GPA, 4096);
    pdpt = gpa_to_hva(&mem, BOOT_TABLES_GPA + 4096, 4096);
    pd = gpa_to_hva(&mem, BOOT_TABLES_GPA + 8192, 4096);
    gdt = gpa_to_hva(&mem, BOOT_TABLES_GPA + 12288, 4096);
    tap_ok(
        pml4[0] == 0x11003 && pml4[1] == 0 && pdpt[0] == 0x12003 &&
            pdpt[1] == 0 && pd[0] == 0x83 && pd[511] == 0x3fe00083 &&
            sregs.cr3 == BOOT_TABLES_GPA &&
            (sregs.cr0 & 0x80000001) == 0x80000001 && (sregs.cr4 & 0x20) &&
            (sregs.efer & 0x500) == 0x500,
        "initial identity mapping covers exactly first GiB with 2 MiB pages");
    tap_ok(gdt[2] == 0x00af9b000000ffffULL && gdt[3] == 0x00cf93000000ffffULL &&
               regs.rsp == 0x18000,
           "GDT and bootstrap stack are inside reserved boot workspace");
    tap_ok(cpu_setup_long_mode(&vcpu, &mem) == 0 && regs.rip == 0x1000 &&
               regs.rsi == 0 && sregs.cs.selector == 8 &&
               sregs.ds.selector == 16,
           "raw long-mode payload ABI is preserved");
    fail_set = true;
    tap_ok(cpu_setup_linux(&vcpu, &mem, &boot) < 0 && errno == EIO,
           "KVM state setup failure propagated");
    guest_memory_destroy(&mem);
    mem = (struct guest_memory){0};
    tap_ok(cpu_setup_linux(&vcpu, &mem, &boot) < 0 && errno == ENOMEM,
           "missing boot workspace rejected without accessing KVM");
    return tap_done();
}
