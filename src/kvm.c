#include "kvm.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "memory.h"
#include "util/log.h"

#define KVM_DEV "/dev/kvm"
#define CAP(name) {(name), #name}

static const struct {
    int cap;
    const char *name;
} required_caps[] = {
    CAP(KVM_CAP_USER_MEMORY),
    CAP(KVM_CAP_IMMEDIATE_EXIT),
};

int kvm_check_extension(int fd, int cap)
{
    return ioctl(fd, KVM_CHECK_EXTENSION, (unsigned long)cap);
}

static int check_caps(int fd)
{
    for (size_t i = 0; i < sizeof(required_caps) / sizeof(required_caps[0]);
         i++) {
        const int ret = kvm_check_extension(fd, required_caps[i].cap);

        if (ret < 0) {
            log_errno("KVM_CHECK_EXTENSION(%s)", required_caps[i].name);
            return -1;
        }
        if (ret == 0) {
            log_error("KVM lacks %s", required_caps[i].name);
            errno = ENOTSUP;
            return -1;
        }
    }
    return 0;
}

int kvm_open(struct kvm_system *kvm)
{
    int ret;
    int saved_errno;

    kvm->fd = open(KVM_DEV, O_RDWR | O_CLOEXEC);
    if (kvm->fd < 0) {
        log_errno("open %s", KVM_DEV);
        return -1;
    }

    ret = ioctl(kvm->fd, KVM_GET_API_VERSION, 0);
    if (ret != KVM_API_VERSION) {
        log_error("KVM API version %d, expected %d", ret, KVM_API_VERSION);
        errno = ENOTSUP;
        goto fail;
    }

    if (check_caps(kvm->fd) < 0) {
        goto fail;
    }

    ret = kvm_check_extension(kvm->fd, KVM_CAP_NR_MEMSLOTS);
    if (ret < GUEST_MEM_MAX_REGIONS) {
        log_error("KVM offers %d memslots, need %d", ret,
                  GUEST_MEM_MAX_REGIONS);
        errno = ENOTSUP;
        goto fail;
    }
    kvm->nr_memslots = (unsigned)ret;

    ret = ioctl(kvm->fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (ret < 0) {
        log_errno("KVM_GET_VCPU_MMAP_SIZE");
        goto fail;
    }
    kvm->vcpu_mmap_size = (size_t)ret;

    log_debug("kvm: api %d, %u memslots, vcpu mmap %zu bytes", KVM_API_VERSION,
              kvm->nr_memslots, kvm->vcpu_mmap_size);

    return 0;

fail:
    saved_errno = errno;
    close(kvm->fd);
    kvm->fd = -1;
    errno = saved_errno;
    return -1;
}

void kvm_close(struct kvm_system *kvm)
{
    if (kvm->fd >= 0) {
        close(kvm->fd);
        kvm->fd = -1;
    }
}
