#ifndef MINIVMM_BOOT_E820_H
#define MINIVMM_BOOT_E820_H

#include <asm/bootparam.h>
#include <stddef.h>

struct guest_memory;

/* Sorted, non-overlapping ranges, including boot workspace and MMIO reserves.
 * No allocation or KVM access. On failure returns -1 with errno set. */
int e820_build(const struct guest_memory *mem, struct boot_e820_entry *entries,
               size_t capacity, size_t *count);

#endif /* MINIVMM_BOOT_E820_H */
