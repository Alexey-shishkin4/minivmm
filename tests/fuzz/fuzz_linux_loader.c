#include <stddef.h>
#include <stdint.h>

#include "boot/e820.h"
#include "boot/linux_loader.h"
#include "fuzz.h"
#include "memory.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct linux_image image;
    struct linux_boot boot;
    struct boot_e820_entry entries[E820_MAX_ENTRIES_ZEROPAGE];
    struct guest_memory mem = {.regions = {{.gpa = 0, .size = 128ULL << 20U}},
                               .nr_regions = 1};
    size_t count;

    if (linux_image_parse(data, size, &image) == 0) {
        (void)linux_boot_plan(&image, &mem, 0, 1, &boot);
        (void)linux_boot_plan(&image, &mem, size, 128, &boot);
        /* Guest file sizes and RAM boundaries are independently untrusted. */
        (void)linux_boot_plan(&image, &mem, image.header.pref_address,
                              (size_t)image.header.cmdline_size + 1U, &boot);
        mem.regions[0].size = image.header.init_size;
        (void)linux_boot_plan(&image, &mem, UINT64_MAX, 4097, &boot);
        (void)e820_build(&mem, entries, E820_MAX_ENTRIES_ZEROPAGE, &count);
    }
    return 0;
}
