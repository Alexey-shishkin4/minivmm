#include <errno.h>
#include <stdint.h>

#include "boot/e820.h"
#include "boot/layout.h"
#include "memory.h"
#include "tap.h"

static bool ordered(const struct boot_e820_entry *e, size_t n)
{
    uint64_t end = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i].size == 0 || e[i].addr < end ||
            e[i].size > UINT64_MAX - e[i].addr) {
            return false;
        }
        end = e[i].addr + e[i].size;
    }
    return true;
}

static uint64_t ram_bytes(const struct boot_e820_entry *e, size_t n)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i].type == 1) {
            sum += e[i].size;
        }
    }
    return sum;
}

int main(void)
{
    struct guest_memory mem = {.regions = {{.gpa = 0, .size = 64ULL << 20U}},
                               .nr_regions = 1};
    struct boot_e820_entry e[E820_MAX_ENTRIES_ZEROPAGE];
    size_t n;
    const uint64_t reserved =
        BOOT_WORKSPACE_END - BOOT_TABLES_GPA + 0x100000 - 0x9fc00;

    tap_plan(12);
    tap_ok(e820_build(&mem, e, E820_MAX_ENTRIES_ZEROPAGE, &n) == 0 && n == 6,
           "64 MiB map includes low RAM, workspace, BIOS and MMIO reserves");
    tap_ok(ordered(e, n), "ranges sorted and non-overlapping");
    tap_ok(e[1].addr == BOOT_TABLES_GPA && e[1].type == 2 &&
               e[1].size == BOOT_WORKSPACE_END - BOOT_TABLES_GPA,
           "live boot tables and zero page remain reserved");
    tap_ok(e[3].addr == 0x9fc00 && e[3].type == 2 &&
               e[3].size == 0x100000 - 0x9fc00,
           "EBDA and legacy BIOS are reserved");
    tap_ok(e[5].addr == GUEST_MMIO_HOLE_START && e[5].type == 2 &&
               e[5].size == GUEST_MMIO_HOLE_END - GUEST_MMIO_HOLE_START,
           "device aperture is reserved without inventing RAM in the gap");
    tap_ok(ram_bytes(e, n) == mem.regions[0].size - reserved,
           "usable RAM equals backed memory minus reserves");
    mem.regions[0].size = GUEST_MMIO_HOLE_START;
    mem.regions[1] =
        (struct guest_region){.gpa = GUEST_MMIO_HOLE_END, .size = 3ULL << 30U};
    mem.nr_regions = 2;
    tap_ok(e820_build(&mem, e, E820_MAX_ENTRIES_ZEROPAGE, &n) == 0 && n == 7 &&
               ordered(e, n) && e[6].addr == GUEST_MMIO_HOLE_END &&
               ram_bytes(e, n) == (6ULL << 30U) - reserved,
           "6 GiB RAM is split around the 3..4 GiB hole");
    tap_ok(e820_build(&mem, e, 2, &n) < 0 && errno == ENOSPC && n == 0,
           "insufficient output capacity rejected");
    mem.regions[1].gpa = GUEST_MMIO_HOLE_START - 4096;
    tap_ok(e820_build(&mem, e, 128, &n) < 0 && errno == EINVAL,
           "overlapping regions rejected");
    mem.regions[1] =
        (struct guest_region){.gpa = UINT64_MAX - 4095, .size = 4096};
    tap_ok(e820_build(&mem, e, 128, &n) < 0 && errno == EINVAL,
           "wrapping physical range rejected");
    mem.nr_regions = 1;
    mem.regions[0].size = GUEST_MMIO_HOLE_START + 4096;
    tap_ok(e820_build(&mem, e, 128, &n) < 0 && errno == EINVAL,
           "RAM crossing device aperture rejected");
    mem.nr_regions = 0;
    tap_ok(e820_build(&mem, e, 128, &n) < 0 && errno == EINVAL,
           "empty memory map rejected");
    return tap_done();
}
