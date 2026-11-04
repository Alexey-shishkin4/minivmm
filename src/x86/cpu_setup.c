#include "x86/cpu_setup.h"

#include <asm/kvm.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

#include "util/log.h"
#include "vcpu.h"

#define X86_CR0_PE 0x1ULL        /* protection enable */
#define X86_CR0_PG 0x80000000ULL /* paging */

#define X86_RFLAGS_FIXED 0x2ULL /* bit 1 is reserved and must be 1 */

/* Descriptor types (s = 1): bit 3 code, bit 1 read/write, bit 0 accessed */
#define SEG_TYPE_DATA_RW 0x3
#define SEG_TYPE_CODE_RX 0xb

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
