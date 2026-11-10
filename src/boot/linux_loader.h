#ifndef MINIVMM_BOOT_LINUX_LOADER_H
#define MINIVMM_BOOT_LINUX_LOADER_H

#include <asm/bootparam.h>
#include <stddef.h>
#include <stdint.h>

struct guest_memory;

struct linux_image {
    struct setup_header header;
    size_t payload_offset;
    size_t payload_size;
};

struct linux_boot {
    uint64_t kernel_addr;
    uint64_t kernel_size; /* includes the decompressor's init_size workspace */
    uint64_t entry;
    uint64_t params_addr;
    uint64_t cmdline_addr;
    uint64_t initrd_addr;
    uint64_t initrd_size;
};

/* Pure validation/planning functions: no I/O, allocation or guest writes.
 * Supports x86-64 bzImage (up to 128 MiB), protocol >= 2.12,
 * initial identity map below 1 GiB.
 * cmdline_size includes the trailing NUL. Returns -1 with errno on failure. */
int linux_image_parse(const uint8_t *data, size_t size,
                      struct linux_image *image);
int linux_boot_plan(const struct linux_image *image,
                    const struct guest_memory *mem, uint64_t initrd_size,
                    size_t cmdline_size, struct linux_boot *boot);

/* Load regular files, build a zero page with E820, copy the command line.
 * initrd_path may be NULL. On failure the caller must discard guest memory. */
int linux_load(const struct guest_memory *mem, const char *kernel_path,
               const char *initrd_path, const char *cmdline,
               struct linux_boot *boot);

#endif /* MINIVMM_BOOT_LINUX_LOADER_H */
