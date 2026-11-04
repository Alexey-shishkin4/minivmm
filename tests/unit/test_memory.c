#include "tap.h"

#include <stdint.h>

#include "memory.h"

#define MiB 0x100000ULL
#define GiB 0x40000000ULL

static void test_small(void)
{
    struct guest_memory mem;
    const uint64_t size = 2 * MiB;
    uint8_t *base;

    tap_ok(guest_memory_init(&mem, size) == 0, "init 2 MiB");
    base = mem.regions[0].hva;
    tap_ok(mem.nr_regions == 1, "2 MiB fits in one region");

    tap_ok(gpa_to_hva(&mem, 0, 1) == base, "first byte");
    tap_ok(gpa_to_hva(&mem, 0x1000, 16) == base + 0x1000, "in range");
    tap_ok(gpa_to_hva(&mem, size - 1, 1) == base + size - 1, "last byte");
    tap_ok(gpa_to_hva(&mem, 0, size) == base, "whole region");
    tap_ok(gpa_to_hva(&mem, size, 1) == NULL, "one past the end");
    tap_ok(gpa_to_hva(&mem, size - 1, 2) == NULL, "straddles the end");
    tap_ok(gpa_to_hva(&mem, 0, size + 1) == NULL, "len larger than region");
    tap_ok(gpa_to_hva(&mem, 0x1000, UINT64_MAX) == NULL,
           "gpa + len wraps around");
    tap_ok(gpa_to_hva(&mem, UINT64_MAX, 2) == NULL, "gpa at UINT64_MAX");
    tap_ok(gpa_to_hva(&mem, 0x1000, 0) == NULL, "len 0 rejected");

    guest_memory_destroy(&mem);
    guest_memory_destroy(&mem);
    tap_ok(mem.nr_regions == 0, "destroy is idempotent");
}

static void test_hole(void)
{
    struct guest_memory mem;
    const struct guest_region *lo;
    const struct guest_region *hi;

    tap_ok(guest_memory_init(&mem, 6 * GiB) == 0, "init 6 GiB");
    lo = &mem.regions[0];
    hi = &mem.regions[1];
    tap_ok(mem.nr_regions == 2, "6 GiB splits around the hole");
    tap_ok(lo->gpa == 0 && lo->size == GUEST_MMIO_HOLE_START,
           "low region ends at the hole");
    tap_ok(hi->gpa == GUEST_MMIO_HOLE_END && hi->size == 3 * GiB,
           "high region starts at 4 GiB");

    tap_ok(gpa_to_hva(&mem, GUEST_MMIO_HOLE_START, 1) == NULL,
           "hole start not backed");
    tap_ok(gpa_to_hva(&mem, 0xFEE00000, 4) == NULL, "LAPIC page not backed");
    tap_ok(gpa_to_hva(&mem, GUEST_MMIO_HOLE_END - 1, 1) == NULL,
           "hole end not backed");
    tap_ok(gpa_to_hva(&mem, GUEST_MMIO_HOLE_START - 1, 2) == NULL,
           "range into the hole rejected");
    tap_ok(gpa_to_hva(&mem, GUEST_MMIO_HOLE_END, 1) == hi->hva,
           "first byte above 4 GiB");
    tap_ok(gpa_to_hva(&mem, (7 * GiB) - 1, 1) == hi->hva + (3 * GiB) - 1,
           "last byte of high region");
    tap_ok(gpa_to_hva(&mem, 7 * GiB, 1) == NULL, "past high region");

    guest_memory_destroy(&mem);
}

static void test_bad_size(void)
{
    struct guest_memory mem;

    tap_ok(guest_memory_init(&mem, 0) < 0, "size 0 rejected");
    tap_ok(guest_memory_init(&mem, 4097) < 0, "unaligned size rejected");
}

int main(void)
{
    tap_plan(26);
    test_small();
    test_hole();
    test_bad_size();
    return tap_done();
}
