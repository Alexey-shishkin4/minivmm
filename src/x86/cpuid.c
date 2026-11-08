#include "x86/cpuid.h"

#include <asm/kvm_para.h>
#include <errno.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "kvm.h"
#include "util/log.h"
#include "vcpu.h"

static struct kvm_cpuid2 *get_supported_cpuid(int fd)
{
    /* Grow independently of nent: E2BIG need not report the required size. */
    for (unsigned capacity = 128; capacity <= 4096; capacity *= 2) {
        const size_t size = sizeof(struct kvm_cpuid2) +
                            capacity * sizeof(struct kvm_cpuid_entry2);
        struct kvm_cpuid2 *cpuid = calloc(1, size);
        int saved;

        if (cpuid == NULL) {
            return NULL;
        }

        cpuid->nent = capacity;

        if (ioctl(fd, KVM_GET_SUPPORTED_CPUID, cpuid) == 0) {
            return cpuid;
        }

        saved = errno;
        free(cpuid);
        errno = saved;

        if (saved != E2BIG) {
            return NULL;
        }
    }

    errno = E2BIG;
    return NULL;
}

int cpuid_setup(const struct kvm_system *kvm, const struct vcpu *vcpu)
{
    struct kvm_cpuid2 *cpuid;
    bool has_long_mode = false;
    int ret;
    int saved;

    /* This example is only for the single-vCPU payload stage. */
    if (vcpu->id != 0) {
        errno = EINVAL;
        log_error("cpuid_setup: only vCPU 0 is supported");
        return -1;
    }

    ret = kvm_check_extension(kvm->fd, KVM_CAP_EXT_CPUID);
    if (ret < 0) {
        log_errno("KVM_CHECK_EXTENSION(KVM_CAP_EXT_CPUID)");
        return -1;
    }
    if (ret == 0) {
        errno = ENOTSUP;
        log_error("KVM lacks KVM_CAP_EXT_CPUID");
        return -1;
    }

    cpuid = get_supported_cpuid(kvm->fd);
    if (cpuid == NULL) {
        log_errno("KVM_GET_SUPPORTED_CPUID");
        return -1;
    }

    for (unsigned i = 0; i < cpuid->nent; i++) {
        struct kvm_cpuid_entry2 *e = &cpuid->entries[i];

        if (e->function == 1) {
            /*
             * EBX[31:24]: initial APIC ID.
             * Preserve the remaining fields.
             */
            e->ebx = (e->ebx & 0x00ffffffU) | (vcpu->id << 24U);

            /* ECX[31]: hypervisor present. */
            e->ecx |= 1U << 31U;

            /* No in-kernel LAPIC yet: hide x2APIC and TSC deadline timer. */
            e->ecx &= ~((1U << 21U) | (1U << 24U));
        }

        /*
         * Intel topology leaves: EDX contains the x2APIC ID.
         * Full topology construction is deferred until the SMP stage.
         */
        if (e->function == 0x0b || e->function == 0x1f) {
            e->edx = vcpu->id;
        }

        if (e->function == KVM_CPUID_FEATURES) {
            /* PV unhalt also requires an in-kernel LAPIC. */
            e->eax &= ~(1U << KVM_FEATURE_PV_UNHALT);
        }

        if (e->function == 0x80000001U) {
            /* EDX[29]: long mode available. */
            has_long_mode = (e->edx & (1U << 29U)) != 0;
        }
    }

    if (!has_long_mode) {
        free(cpuid);
        errno = ENOTSUP;
        log_error("KVM does not expose long mode");
        return -1;
    }

    ret = ioctl(vcpu->fd, KVM_SET_CPUID2, cpuid);

    saved = errno;
    free(cpuid);
    errno = saved;

    if (ret < 0) {
        log_errno("KVM_SET_CPUID2");
    }

    return ret;
}
