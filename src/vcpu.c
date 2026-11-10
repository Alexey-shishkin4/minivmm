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
        .vm = vm,
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

static int handle_io(const struct vcpu *vcpu)
{
    struct kvm_run *run = vcpu->run;
    const size_t size = run->io.size;
    const uint64_t offset = run->io.data_offset;
    bool unhandled = false;
    bool out;
    uint8_t *data;

    if ((size != 1 && size != 2 && size != 4) || run->io.count == 0 ||
        (run->io.direction != KVM_EXIT_IO_IN &&
         run->io.direction != KVM_EXIT_IO_OUT)) {
        log_error("invalid KVM_EXIT_IO metadata");
        errno = EINVAL;
        return -1;
    }
    if (offset > vcpu->run_size ||
        run->io.count > (vcpu->run_size - offset) / size) {
        log_error("PIO data lies outside kvm_run mapping");
        errno = EINVAL;
        return -1;
    }
    data = (uint8_t *)run + (size_t)offset;
    out = run->io.direction == KVM_EXIT_IO_OUT;

    /* REP repeats accesses to the same port; only the data pointer moves. */
    for (uint32_t i = 0; i < run->io.count; i++) {
        uint8_t *element = data + (size_t)i * size;
        const enum bus_result result =
            bus_access(&vcpu->vm->pio_bus, run->io.port, element, size, out);

        if (result == BUS_ERROR) {
            log_errno("PIO %s port 0x%x, size %zu", out ? "out" : "in",
                      run->io.port, size);
            return -1;
        }
        if (result == BUS_UNMAPPED) {
            unhandled = true;
            if (!out) {
                memset(element, 0xff, size);
            }
        }
    }
    if (unhandled) {
        log_warn("unhandled %s port 0x%x, size %u, count %u",
                 out ? "out" : "in", run->io.port, run->io.size, run->io.count);
    }
    return 0;
}

static int handle_mmio(const struct vcpu *vcpu)
{
    struct kvm_run *run = vcpu->run;
    uint64_t value = 0;
    enum bus_result result;

    if (run->mmio.len == 0 || run->mmio.len > sizeof(run->mmio.data) ||
        run->mmio.is_write > 1) {
        log_error("invalid KVM_EXIT_MMIO metadata");
        errno = EINVAL;
        return -1;
    }
    if (run->mmio.is_write) {
        memcpy(&value, run->mmio.data, run->mmio.len);
    }
    result = bus_access(&vcpu->vm->mmio_bus, run->mmio.phys_addr,
                        run->mmio.data, run->mmio.len, run->mmio.is_write != 0);
    if (result == BUS_ERROR) {
        log_errno("MMIO %s at 0x%llx", run->mmio.is_write ? "write" : "read",
                  (unsigned long long)run->mmio.phys_addr);
        return -1;
    }
    if (result == BUS_HANDLED) {
        return 0;
    }
    if (!run->mmio.is_write) {
        memset(run->mmio.data, 0xff, run->mmio.len);
    }
    log_warn("unhandled mmio %s 0x%llx, len %u, value 0x%" PRIx64,
             run->mmio.is_write ? "write" : "read",
             (unsigned long long)run->mmio.phys_addr, run->mmio.len, value);
    return 0;
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
static bool handle_exit(const struct vcpu *vcpu, enum vcpu_stop *stop)
{
    struct kvm_run *run = vcpu->run;

    switch (run->exit_reason) {
    case KVM_EXIT_IO:
        if (handle_io(vcpu) == 0) {
            return true;
        }
        *stop = VCPU_STOP_ERROR;
        return false;
    case KVM_EXIT_MMIO:
        if (handle_mmio(vcpu) == 0) {
            return true;
        }
        *stop = VCPU_STOP_ERROR;
        return false;
    case KVM_EXIT_HLT:
        if (vcpu->vm->has_irqchip) {
            log_error("unexpected userspace HLT with in-kernel irqchip");
            *stop = VCPU_STOP_ERROR;
            return false;
        }
        *stop = VCPU_STOP_HLT;
        return false;
    case KVM_EXIT_SYSTEM_EVENT:
        if (vcpu->vm->has_irqchip &&
            run->system_event.type == KVM_SYSTEM_EVENT_RESET) {
            *stop = VCPU_STOP_REBOOT;
            return false;
        }
        log_error("unsupported KVM system event %u", run->system_event.type);
        *stop = VCPU_STOP_ERROR;
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
            /* Retry an interrupted KVM_RUN without dispatching exit_reason. */
            if (errno == EINTR) {
                continue;
            }
            log_errno("KVM_RUN");
            stop = VCPU_STOP_ERROR;
            break;
        }
        if (!handle_exit(vcpu, &stop)) {
            break;
        }
        if (vcpu->vm->i8042.reset_requested) {
            /* Finish the pending OUT before stopping. KVM checks immediate_exit
             * after completing userspace I/O, without executing another insn. */
            vcpu->run->immediate_exit = 1;
            const int ret = ioctl(vcpu->fd, KVM_RUN, 0UL);
            const int saved = errno;
            vcpu->run->immediate_exit = 0;
            if (ret < 0 && saved == EINTR) {
                log_info("guest requested reboot via i8042");
                stop = VCPU_STOP_REBOOT;
            } else {
                errno = ret < 0 ? saved : EIO;
                log_errno("complete i8042 reset I/O");
                stop = VCPU_STOP_ERROR;
            }
            break;
        }
    }

    if (stop != VCPU_STOP_HLT && stop != VCPU_STOP_REBOOT) {
        vcpu_dump_regs(vcpu);
    }
    log_set_vcpu(-1);
    return stop;
}
