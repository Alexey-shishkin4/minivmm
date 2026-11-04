#include "memory.h"

#include <errno.h>
#include <linux/kvm.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

_Static_assert(sizeof(size_t) == sizeof(uint64_t), "64-bit host required");

static int add_region(struct guest_memory *mem, uint64_t gpa, uint64_t size)
{
    void *hva;

    if (mem->nr_regions == GUEST_MEM_MAX_REGIONS) {
        errno = ENOSPC;
        return -1;
    }

    hva = mmap(NULL, size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (hva == MAP_FAILED) {
        return -1;
    }

    mem->regions[mem->nr_regions++] = (struct guest_region){
        .gpa = gpa,
        .size = size,
        .hva = hva,
    };

    return 0;
}

int guest_memory_init(struct guest_memory *mem, uint64_t ram_size)
{
    const uint64_t low =
        ram_size < GUEST_MMIO_HOLE_START ? ram_size : GUEST_MMIO_HOLE_START;

    memset(mem, 0, sizeof(*mem));
    if (ram_size == 0 || ram_size % GUEST_PAGE_SIZE != 0) {
        errno = EINVAL;
        return -1;
    }
    if (add_region(mem, 0, low) < 0) {
        return -1;
    }
    if (ram_size > low &&
        add_region(mem, GUEST_MMIO_HOLE_END, ram_size - low) < 0) {
        const int saved = errno;

        guest_memory_destroy(mem);
        errno = saved;
        return -1;
    }
    return 0;
}

void guest_memory_destroy(struct guest_memory *mem)
{
    for (unsigned i = 0; i < mem->nr_regions; i++) {
        munmap(mem->regions[i].hva, mem->regions[i].size);
    }

    mem->nr_regions = 0;
}

int guest_memory_register(const struct guest_memory *mem, int vm_fd)
{
    for (unsigned i = 0; i < mem->nr_regions; i++) {
        const struct guest_region *r = &mem->regions[i];
        const struct kvm_userspace_memory_region slot = {
            .slot = i,
            .flags = 0,
            .guest_phys_addr = r->gpa,
            .memory_size = r->size,
            .userspace_addr = (uint64_t)(uintptr_t)r->hva,
        };

        if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &slot) < 0) {
            return -1;
        }
    }

    return 0;
}

void *gpa_to_hva(const struct guest_memory *mem, uint64_t gpa, uint64_t len)
{
    if (len == 0) {
        return NULL;
    }

    for (unsigned i = 0; i < mem->nr_regions; i++) {
        const struct guest_region *r = &mem->regions[i];
        uint64_t off;

        if (gpa < r->gpa) {
            continue;
        }
        off = gpa - r->gpa;
        if (off >= r->size) {
            continue;
        }
        if (len > r->size - off) {
            return NULL;
        }
        return r->hva + off;
    }
    return NULL;
}
