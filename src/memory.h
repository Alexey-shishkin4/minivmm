/*
 * Guest physical memory: allocation, KVM memslots, GPA -> HVA translation.
 *
 * TODO: struct guest_memory: regions {gpa, size, hva, slot}
 * TODO: allocate with mmap(MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE), later memfd
 *        (needed to share memory with a vhost-user backend)
 * TODO: KVM_SET_USER_MEMORY_REGION; leave a hole below 4 GiB for MMIO/PCI
 * TODO: gpa_to_hva(mem, gpa, len): bounds-checked, overflow-safe -- every
 *        guest-supplied address goes through it (the guest is untrusted)
 * TODO: dirty logging (KVM_GET_DIRTY_LOG) later, for live-migration experiments
 */

/*
 * Guest physical memory: allocation, KVM memslots, GPA -> HVA translation.
 *
 * RAM is laid out below and above a hole for MMIO/PCI at [3 GiB, 4 GiB): the
 * hole is never backed, so device windows (LAPIC, IOAPIC, PCI BARs) there
 * exit to userspace as KVM_EXIT_MMIO.
 */

#ifndef MINIVMM_MEMORY_H
#define MINIVMM_MEMORY_H

#include <stdint.h>

#define GUEST_PAGE_SIZE 0x1000ULL
#define GUEST_MMIO_HOLE_START 0xC0000000ULL /* 3 GiB */
#define GUEST_MMIO_HOLE_END 0x100000000ULL  /* 4 GiB */
#define GUEST_MEM_MAX_REGIONS 8

struct guest_region {
    uint64_t gpa; //guest virtal address
    uint64_t size;
    uint8_t *hva; // host virtual address
};

struct guest_memory {
    struct guest_region regions[GUEST_MEM_MAX_REGIONS];
    unsigned nr_regions;
};

/*
 * Map ram_size bytes of anonymous memory (page-aligned, non-zero) and lay it
 * out around the MMIO hole. Returns 0, or -1 with errno set; on failure
 * nothing stays mapped.
 */
int guest_memory_init(struct guest_memory *mem, uint64_t ram_size);

/* Unmap all regions. Call after the VM fd is closed. Safe to call twice. */
void guest_memory_destroy(struct guest_memory *mem);

/*
 * Register every region as a KVM memslot (slot = region index).
 * Returns 0, or -1 with errno set.
 */
int guest_memory_register(const struct guest_memory *mem, int vm_fd);

/*
 * Translate [gpa, gpa + len) to a host pointer. Returns NULL unless the whole
 * range lies inside one region; len == 0 is rejected. Every guest-supplied
 * address must go through this: the guest is untrusted.
 */
void *gpa_to_hva(const struct guest_memory *mem, uint64_t gpa, uint64_t len);

#endif /* MINIVMM_MEMORY_H */
