#include "boot/e820.h"

#include <errno.h>
#include <stdint.h>

#include "boot/layout.h"
#include "memory.h"

#define E820_RAM 1U
#define E820_RESERVED 2U

static int append(struct boot_e820_entry *entries, size_t capacity,
                  size_t *count, uint64_t start, uint64_t end, uint32_t type)
{
    if (start == end) {
        return 0;
    }
    if (*count == capacity) {
        errno = ENOSPC;
        return -1;
    }
    entries[(*count)++] = (struct boot_e820_entry){
        .addr = start, .size = end - start, .type = type};
    return 0;
}

int e820_build(const struct guest_memory *mem, struct boot_e820_entry *entries,
               size_t capacity, size_t *count)
{
    static const uint64_t boundaries[] = {
        0,        BOOT_TABLES_GPA,       BOOT_WORKSPACE_END,  0x9fc00,
        0x100000, GUEST_MMIO_HOLE_START, GUEST_MMIO_HOLE_END,
    };
    size_t n = 0;
    uint64_t previous_end = 0;

    if (mem == NULL || entries == NULL || count == NULL ||
        mem->nr_regions == 0 || mem->nr_regions > GUEST_MEM_MAX_REGIONS) {
        errno = EINVAL;
        return -1;
    }
    *count = 0;
    for (unsigned i = 0; i < mem->nr_regions; i++) {
        const struct guest_region *region = &mem->regions[i];
        const uint64_t start = region->gpa;
        uint64_t end;

        if (region->size == 0 || region->size > UINT64_MAX - start ||
            start < previous_end ||
            (start < GUEST_MMIO_HOLE_END &&
             (start >= GUEST_MMIO_HOLE_START ||
              region->size > GUEST_MMIO_HOLE_START - start))) {
            errno = EINVAL;
            return -1;
        }
        end = start + region->size;
        if (start >= GUEST_MMIO_HOLE_END &&
            previous_end < GUEST_MMIO_HOLE_END &&
            append(entries, capacity, &n, GUEST_MMIO_HOLE_START,
                   GUEST_MMIO_HOLE_END, E820_RESERVED) < 0) {
            return -1;
        }
        for (uint64_t pos = start; pos < end;) {
            uint64_t next = end;
            const uint32_t type =
                ((pos >= BOOT_TABLES_GPA && pos < BOOT_WORKSPACE_END) ||
                 (pos >= 0x9fc00 && pos < 0x100000))
                    ? E820_RESERVED
                    : E820_RAM;
            for (size_t j = 0; j < sizeof(boundaries) / sizeof(boundaries[0]);
                 j++) {
                if (boundaries[j] > pos && boundaries[j] < next) {
                    next = boundaries[j];
                }
            }
            if (append(entries, capacity, &n, pos, next, type) < 0) {
                return -1;
            }
            pos = next;
        }
        previous_end = end;
    }
    if (previous_end <= GUEST_MMIO_HOLE_START &&
        append(entries, capacity, &n, GUEST_MMIO_HOLE_START,
               GUEST_MMIO_HOLE_END, E820_RESERVED) < 0) {
        return -1;
    }
    *count = n;
    return 0;
}
