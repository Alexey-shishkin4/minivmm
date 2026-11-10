#include "x86/cpu_setup.h"

#include <asm/kvm.h>
#include <errno.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

#include "boot/layout.h"
#include "boot/linux_loader.h"
#include "boot/raw_loader.h"
#include "memory.h"
#include "util/log.h"
#include "vcpu.h"

#define X86_CR0_PE 0x1ULL        /* protection enable */
#define X86_CR0_PG 0x80000000ULL /* paging */

#define X86_RFLAGS_FIXED 0x2ULL /* bit 1 is reserved and must be 1 */

/* Descriptor types (s = 1): bit 3 code, bit 1 read/write, bit 0 accessed */
#define SEG_TYPE_DATA_RW 0x3
#define SEG_TYPE_CODE_RX 0xb

#define LM_BOOT_GPA BOOT_TABLES_GPA

#define LM_PAGE_FLAGS 0x3ULL
#define LM_LARGE_PAGE 0x80ULL

#define LM_CR0 0x80010033ULL //PG | WP |NE |ET |MP |PE
#define LM_CR4_PAE 0x20ULL
#define LM_EFER 0x500ULL

/*
 * The structure is placed at LM_BOOT_GPA in guest RAM.
 * Each table begins at a 4 KiB boundary.
 *
 * [0x10000, 0x11000): PML4
 * [0x11000, 0x12000): PDPT
 * [0x12000, 0x13000): PD
 * [0x13000, 0x14000): GDT storage
 * [0x14000, 0x18000): stack
 */
struct long_mode_boot {
    uint64_t pml4[512];
    uint64_t pdpt[512];
    uint64_t pd[512];
    uint64_t gdt[512];
    uint8_t stack[0x4000];
};

_Static_assert(RAW_LOAD_ADDR + RAW_MAX_SIZE <= LM_BOOT_GPA,
               "payload overlaps long-mode boot data");
_Static_assert(LM_BOOT_GPA + sizeof(struct long_mode_boot) <= BOOT_PARAMS_GPA,
               "page tables and bootstrap stack overlap Linux zero page");

/*
 * A real mode segment as the CPU keeps it after loading a selector: the
 * hidden part (base, limit, attributes) is what translation actually uses,
 * and KVM lets us write it directly.
 */
static void real_mode_segment(struct kvm_segment *seg, uint16_t selector,
                              uint8_t type)
{
    memset(seg, 0, sizeof(*seg));
    seg->selector = selector;
    seg->base = (uint64_t)selector << 4U;
    seg->limit = 0xffff;
    seg->type = type;
    seg->present = 1;
    seg->s = 1; /* code/data, not a system seg */
}

int cpu_setup_real_mode(const struct vcpu *vcpu, uint16_t ip)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
        log_errno("KVM_GET_SREGS");
        return -1;
    }
    real_mode_segment(&sregs.cs, 0, SEG_TYPE_CODE_RX);
    real_mode_segment(&sregs.ds, 0, SEG_TYPE_DATA_RW);
    real_mode_segment(&sregs.es, 0, SEG_TYPE_DATA_RW);
    real_mode_segment(&sregs.fs, 0, SEG_TYPE_DATA_RW);
    real_mode_segment(&sregs.gs, 0, SEG_TYPE_DATA_RW);
    real_mode_segment(&sregs.ss, 0, SEG_TYPE_DATA_RW);
    sregs.cr0 &= ~(X86_CR0_PE | X86_CR0_PG);
    if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
        log_errno("KVM_SET_SREGS");
        return -1;
    }

    memset(&regs, 0, sizeof(regs));
    regs.rip = ip;
    regs.rsp = ip;
    regs.rflags = X86_RFLAGS_FIXED; /* IF = 0: nothing may interrupt us yet */
    if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
        log_errno("KVM_SET_REGS");
        return -1;
    }
    return 0;
}

static int setup_long_mode(const struct vcpu *vcpu,
                           const struct guest_memory *mem, uint64_t entry,
                           uint64_t params, bool linux_guest)
{
    struct long_mode_boot *boot;
    struct kvm_sregs sregs;
    struct kvm_regs regs = {0};

    boot = gpa_to_hva(mem, LM_BOOT_GPA, sizeof(*boot));
    if (boot == NULL) {
        errno = ENOMEM;
        log_error("long-mode boot data does not fit in guest RAM");
        return -1;
    }

    /*
     * Preserve reset-state fields that this setup does not change,
     * including TR, LDTR and APIC base.
     */
    if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
        log_errno("KVM_GET_SREGS");
        return -1;
    }

    memset(boot, 0, sizeof(*boot));

    /*
     * Table entries contain guest physical addresses, not host pointers.
     * Only entry 0 is needed at the PML4 and PDPT levels.
     */
    boot->pml4[0] =
        (LM_BOOT_GPA + offsetof(struct long_mode_boot, pdpt)) | LM_PAGE_FLAGS;

    boot->pdpt[0] =
        (LM_BOOT_GPA + offsetof(struct long_mode_boot, pd)) | LM_PAGE_FLAGS;

    /* Identity-map [0, 1 GiB) with 512 pages of 2 MiB each. */
    for (unsigned i = 0; i < 512; i++) {
        boot->pd[i] = ((uint64_t)i << 21U) | LM_PAGE_FLAGS | LM_LARGE_PAGE;
    }

    /*
     * GDT entries (raw payload / Linux boot ABI):
     *   0: null
     *   1 / 2: executable/readable code, L=1, D=0, DPL=0
     *   2 / 3: writable data, D/B=1, DPL=0
     * Accessed bits are already set.
     */
    const unsigned code_index = linux_guest ? 2U : 1U;
    const unsigned data_index = code_index + 1U;
    boot->gdt[code_index] = 0x00af9b000000ffffULL;
    boot->gdt[data_index] = 0x00cf93000000ffffULL;

    sregs.gdt.base = LM_BOOT_GPA + offsetof(struct long_mode_boot, gdt);
    sregs.gdt.limit = (uint16_t)(((data_index + 1U) * sizeof(uint64_t)) - 1U);

    /*
     * KVM takes the cached segment state directly.
     * Writing descriptors into guest RAM alone would not initialize CS.
     */
    sregs.cs = (struct kvm_segment){
        .base = 0,
        .limit = 0xffffffffU,
        .selector = (uint16_t)(code_index * 8U),
        .type = SEG_TYPE_CODE_RX,
        .present = 1,
        .s = 1,
        .l = 1,
        .g = 1,
    };

    sregs.ds = (struct kvm_segment){
        .base = 0,
        .limit = 0xffffffffU,
        .selector = (uint16_t)(data_index * 8U),
        .type = SEG_TYPE_DATA_RW,
        .present = 1,
        .s = 1,
        .db = 1,
        .g = 1,
    };

    sregs.es = sregs.ds;
    sregs.fs = sregs.ds;
    sregs.gs = sregs.ds;
    sregs.ss = sregs.ds;

    /* No exception handlers yet: a fault will normally cause shutdown. */
    sregs.idt.base = 0;
    sregs.idt.limit = 0;

    sregs.cr0 = LM_CR0;
    sregs.cr2 = 0;
    sregs.cr3 = LM_BOOT_GPA;
    sregs.cr4 = LM_CR4_PAE;
    sregs.cr8 = 0;
    sregs.efer = LM_EFER;

    if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
        log_errno("KVM_SET_SREGS (long mode)");
        return -1;
    }

    regs.rip = entry;
    regs.rsi = params;

    /* The first push writes below this address, inside the stack. */
    regs.rsp = LM_BOOT_GPA + sizeof(*boot);

    /* Reserved bit 1 is set; interrupts and direction flag are clear. */
    regs.rflags = X86_RFLAGS_FIXED;

    if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
        log_errno("KVM_SET_REGS (long mode)");
        return -1;
    }

    return 0;
}

int cpu_setup_long_mode(const struct vcpu *vcpu, const struct guest_memory *mem)
{
    return setup_long_mode(vcpu, mem, RAW_LOAD_ADDR, 0, false);
}

int cpu_setup_linux(const struct vcpu *vcpu, const struct guest_memory *mem,
                    const struct linux_boot *boot)
{
    if (boot == NULL || boot->entry >= BOOT_IDENTITY_LIMIT ||
        boot->params_addr >= BOOT_IDENTITY_LIMIT) {
        errno = EINVAL;
        return -1;
    }
    return setup_long_mode(vcpu, mem, boot->entry, boot->params_addr, true);
}
