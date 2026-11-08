#include "vcpu.h"

#include <errno.h>
#include <inttypes.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "util/log.h"
#include "vmm.h"

/* Stage 1 stand-in for the 8250 UART: bytes written here go to stdout. */
#define DEBUG_PORT 0x3f8

#define EXIT_NAME(name) [name] = #name

static const char *const exit_names[] = {
    EXIT_NAME(KVM_EXIT_UNKNOWN),
    EXIT_NAME(KVM_EXIT_EXCEPTION),
    EXIT_NAME(KVM_EXIT_IO),
    EXIT_NAME(KVM_EXIT_HYPERCALL),
    EXIT_NAME(KVM_EXIT_DEBUG),
    EXIT_NAME(KVM_EXIT_HLT),
    EXIT_NAME(KVM_EXIT_MMIO),
    EXIT_NAME(KVM_EXIT_IRQ_WINDOW_OPEN),
    EXIT_NAME(KVM_EXIT_SHUTDOWN),
    EXIT_NAME(KVM_EXIT_FAIL_ENTRY),
    EXIT_NAME(KVM_EXIT_INTR),
    EXIT_NAME(KVM_EXIT_SET_TPR),
    EXIT_NAME(KVM_EXIT_TPR_ACCESS),
    EXIT_NAME(KVM_EXIT_NMI),
    EXIT_NAME(KVM_EXIT_INTERNAL_ERROR),
    EXIT_NAME(KVM_EXIT_SYSTEM_EVENT),
};

static const char *exit_name(uint32_t reason)
{
    if (reason < sizeof(exit_names) / sizeof(exit_names[0]) &&
        exit_names[reason] != NULL) {
        return exit_names[reason];
    }
    return "?";
}

int vcpu_create(struct vcpu *vcpu, const struct vmm *vm, unsigned id)
{
    int saved_errno;
    void *run;

    *vcpu = (struct vcpu){
        .fd = -1,
        .id = id,
        .run_size = vm->kvm.vcpu_mmap_size,
    };

    vcpu->fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, (unsigned long)id);
    if (vcpu->fd < 0) {
        log_errno("KVM_CREATE_VCPU %u", id);
        return -1;
    }

    /*
     * MAP_SHARED: KVM describes each exit in these pages and reads our answer
     * (IN data, MMIO read data) back from them on the next KVM_RUN.
     */
    run = mmap(NULL, vcpu->run_size, PROT_READ | PROT_WRITE, MAP_SHARED,
               vcpu->fd, 0);
    if (run == MAP_FAILED) {
        log_errno("mmap kvm_run of vcpu %u", id);
        goto fail;
    }
    vcpu->run = run;
    return 0;

fail:
    saved_errno = errno;
    vcpu_destroy(vcpu);
    errno = saved_errno;
    return -1;
}

void vcpu_destroy(struct vcpu *vcpu)
{
    if (vcpu->run != NULL) {
        munmap(vcpu->run, vcpu->run_size);
        vcpu->run = NULL;
    }
    if (vcpu->fd >= 0) {
        close(vcpu->fd);
        vcpu->fd = -1;
    }
}

static void handle_io(struct kvm_run *run)
{
    /* The data sits in the same mapping; count > 1 for rep ins/outs. */
    uint8_t *data = (uint8_t *)run + run->io.data_offset;
    const size_t len = (size_t)run->io.size * run->io.count;
    const bool out = run->io.direction == KVM_EXIT_IO_OUT;

    if (out && run->io.port == DEBUG_PORT && run->io.size == 1) {
        fwrite(data, 1, len, stdout);
        return;
    }
    if (!out) {
        /* Nobody decodes this port: an empty ISA bus reads as all ones. */
        memset(data, 0xff, len);
    }
    log_warn("unhandled %s port 0x%x, size %u, count %u", out ? "out" : "in",
             run->io.port, run->io.size, run->io.count);
}

static void handle_mmio(struct kvm_run *run)
{
    uint64_t value = 0;

    if (run->mmio.is_write) {
        memcpy(&value, run->mmio.data, run->mmio.len);
    } else {
        /* Same as for ports: unassigned physical addresses read as ones. */
        memset(run->mmio.data, 0xff, run->mmio.len);
    }
    log_warn("unhandled mmio %s 0x%llx, len %u, value 0x%" PRIx64,
             run->mmio.is_write ? "write" : "read",
             (unsigned long long)run->mmio.phys_addr, run->mmio.len, value);
}

static void log_internal_error(const struct kvm_run *run)
{
    const uint32_t max =
        sizeof(run->internal.data) / sizeof(run->internal.data[0]);
    const uint32_t n = run->internal.ndata < max ? run->internal.ndata : max;

    /* suberror 1 (KVM_INTERNAL_ERROR_EMULATION): KVM could not emulate. */
    log_error("KVM internal error, suberror %u", run->internal.suberror);
    for (uint32_t i = 0; i < n; i++) {
        log_error("  data[%u] = 0x%llx", i,
                  (unsigned long long)run->internal.data[i]);
    }
}

/* Returns true if the guest may run on, otherwise sets *stop. */
static bool handle_exit(struct kvm_run *run, enum vcpu_stop *stop)
{
    switch (run->exit_reason) {
    case KVM_EXIT_IO:
        handle_io(run);
        return true;
    case KVM_EXIT_MMIO:
        handle_mmio(run);
        return true;
    case KVM_EXIT_HLT:
        *stop = VCPU_STOP_HLT;
        return false;
    case KVM_EXIT_SHUTDOWN:
        log_error("guest shutdown (triple fault)");
        *stop = VCPU_STOP_SHUTDOWN;
        return false;
    case KVM_EXIT_FAIL_ENTRY:
        log_error(
            "VM entry failed, hardware reason 0x%llx",
            (unsigned long long)run->fail_entry.hardware_entry_failure_reason);
        *stop = VCPU_STOP_ERROR;
        return false;
    case KVM_EXIT_INTERNAL_ERROR:
        log_internal_error(run);
        *stop = VCPU_STOP_ERROR;
        return false;
    default:
        log_error("unhandled exit %s (%u)", exit_name(run->exit_reason),
                  run->exit_reason);
        *stop = VCPU_STOP_ERROR;
        return false;
    }
}

static void dump_segment(const char *name, const struct kvm_segment *s)
{
    log_error("%s: sel=%04x base=%016llx limit=%08x "
              "type=%x p=%u s=%u dpl=%u l=%u db=%u g=%u unusable=%u",
              name, (unsigned)s->selector, (unsigned long long)s->base,
              (unsigned)s->limit, (unsigned)s->type, (unsigned)s->present,
              (unsigned)s->s, (unsigned)s->dpl, (unsigned)s->l, (unsigned)s->db,
              (unsigned)s->g, (unsigned)s->unusable);
}

#define DUMP_REG(regs, name)                                                   \
    log_error(#name "=%016llx", (unsigned long long)(regs).name)

static void vcpu_dump_regs(const struct vcpu *vcpu)
{
    const int saved = errno;
    struct kvm_regs r;
    struct kvm_sregs s;

    if (ioctl(vcpu->fd, KVM_GET_REGS, &r) < 0) {
        log_errno("KVM_GET_REGS (dump)");
    } else {
        DUMP_REG(r, rip);
        DUMP_REG(r, rsp);
        DUMP_REG(r, rbp);
        DUMP_REG(r, rflags);
        DUMP_REG(r, rax);
        DUMP_REG(r, rbx);
        DUMP_REG(r, rcx);
        DUMP_REG(r, rdx);
        DUMP_REG(r, rsi);
        DUMP_REG(r, rdi);
        DUMP_REG(r, r8);
        DUMP_REG(r, r9);
        DUMP_REG(r, r10);
        DUMP_REG(r, r11);
        DUMP_REG(r, r12);
        DUMP_REG(r, r13);
        DUMP_REG(r, r14);
        DUMP_REG(r, r15);
    }

    /*
     * Attempt this even if KVM_GET_REGS failed: special registers may
     * still be available.
     */
    if (ioctl(vcpu->fd, KVM_GET_SREGS, &s) < 0) {
        log_errno("KVM_GET_SREGS (dump)");
    } else {
        DUMP_REG(s, cr0);
        DUMP_REG(s, cr2);
        DUMP_REG(s, cr3);
        DUMP_REG(s, cr4);
        DUMP_REG(s, cr8);
        DUMP_REG(s, efer);

        dump_segment("cs", &s.cs);
        dump_segment("ss", &s.ss);
        dump_segment("ds", &s.ds);
        dump_segment("es", &s.es);
        dump_segment("fs", &s.fs);
        dump_segment("gs", &s.gs);
        dump_segment("tr", &s.tr);
        dump_segment("ldt", &s.ldt);

        log_error("gdt: base=%016llx limit=%04x",
                  (unsigned long long)s.gdt.base, (unsigned)s.gdt.limit);
        log_error("idt: base=%016llx limit=%04x",
                  (unsigned long long)s.idt.base, (unsigned)s.idt.limit);
    }

    errno = saved;
}

#undef DUMP_REG

enum vcpu_stop vcpu_run(struct vcpu *vcpu)
{
    enum vcpu_stop stop;

    log_set_vcpu((int)vcpu->id);
    for (;;) {
        if (ioctl(vcpu->fd, KVM_RUN, 0UL) < 0) {
            /* A signal is pending (exit_reason is KVM_EXIT_INTR): re-enter. */
            if (errno == EINTR) {
                continue;
            }
            log_errno("KVM_RUN");
            stop = VCPU_STOP_ERROR;
            break;
        }
        if (!handle_exit(vcpu->run, &stop)) {
            break;
        }
    }

    if (stop != VCPU_STOP_HLT) {
        vcpu_dump_regs(vcpu);
    }
    log_set_vcpu(-1);
    return stop;
}
