#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "boot/layout.h"
#include "boot/linux_loader.h"
#include "memory.h"
#include "tap.h"

#define IMAGE_SIZE 4096U
#define HEADER_OFFSET 0x1f1U

static void make_image(uint8_t *data, struct setup_header *header)
{
    *header = (struct setup_header){
        .setup_sects = 1,
        .syssize = (IMAGE_SIZE - 1024) / 16,
        .boot_flag = 0xaa55,
        .jump = 0x6aeb,
        .header = 0x53726448,
        .version = 0x20f,
        .loadflags = LOADED_HIGH,
        .initrd_addr_max = 0x7fffffff,
        .kernel_alignment = 0x200000,
        .xloadflags = XLF_KERNEL_64,
        .cmdline_size = 2047,
        .pref_address = 0x1000000,
        .init_size = 0x100000,
    };
    memset(data, 0, IMAGE_SIZE);
    memcpy(data + HEADER_OFFSET, header, sizeof(*header));
    memset(data + 1024, 0xa5, IMAGE_SIZE - 1024);
}

#define REJECT(field, value, description)                                      \
    do {                                                                       \
        make_image(data, &header);                                             \
        header.field = (value);                                                \
        memcpy(data + HEADER_OFFSET, &header, sizeof(header));                 \
        tap_ok(linux_image_parse(data, sizeof(data), &image) < 0,              \
               description);                                                   \
    } while (0)

int main(void)
{
    uint8_t data[IMAGE_SIZE];
    struct setup_header header;
    struct linux_image image;
    struct linux_boot boot;
    struct guest_memory mem = {.regions = {{.gpa = 0, .size = 64ULL << 20U}},
                               .nr_regions = 1};
    struct guest_memory allocated;
    struct boot_params *params;
    char kernel_path[] = "/tmp/minivmm-kernel-XXXXXX";
    char initrd_path[] = "/tmp/minivmm-initrd-XXXXXX";
    const char cmdline[] = "console=ttyS0 rdinit=/init reboot=k";
    const uint8_t initrd[] = {1, 2, 3, 4, 5};
    int kernel_fd;
    int initrd_fd;

    tap_plan(32);
    make_image(data, &header);
    tap_ok(linux_image_parse(data, sizeof(data), &image) == 0 &&
               image.payload_offset == 1024 && image.payload_size == 3072,
           "valid x86-64 bzImage parsed");
    tap_ok(linux_image_parse(data, 0x263, &image) < 0,
           "truncated setup header rejected");
    REJECT(boot_flag, 0, "bad boot flag rejected");
    REJECT(header, 0, "bad HdrS magic rejected");
    REJECT(version, 0x20b, "protocol before 2.12 rejected");
    REJECT(xloadflags, 0, "32-bit kernel rejected");
    REJECT(loadflags, 0, "low-loaded zImage rejected");
    REJECT(jump, 0x01eb,
           "advertised header shorter than required fields rejected");
    REJECT(setup_sects, 255, "setup offset past end of file rejected");
    REJECT(setup_sects, 0,
           "zero setup_sects uses historical four-sector default");
    REJECT(syssize, UINT32_MAX, "oversized protected-mode size rejected");
    REJECT(init_size, 1024,
           "decompressor workspace smaller than payload rejected");
    REJECT(kernel_alignment, 0, "zero alignment rejected");
    REJECT(kernel_alignment, 0x300000, "non-power-of-two alignment rejected");
    make_image(data, &header);
    if (linux_image_parse(data, sizeof(data), &image) < 0) {
        return 1;
    }
    tap_ok(linux_boot_plan(&image, &mem, 8193, sizeof(cmdline), &boot) == 0 &&
               boot.kernel_addr == 0x1000000 && boot.entry == 0x1000200 &&
               boot.kernel_size == header.init_size &&
               boot.initrd_addr % GUEST_PAGE_SIZE == 0 &&
               boot.initrd_addr >= boot.kernel_addr + boot.kernel_size &&
               boot.initrd_addr + boot.initrd_size <= mem.regions[0].size,
           "kernel workspace and page-aligned initrd do not overlap");
    tap_ok(linux_boot_plan(&image, &mem, 0, 1, &boot) == 0 &&
               boot.initrd_addr == 0 && boot.initrd_size == 0,
           "initrd is optional");
    tap_ok(linux_boot_plan(&image, &mem, UINT64_MAX, 1, &boot) < 0 &&
               errno == ENOMEM,
           "initrd size overflow rejected");
    tap_ok(linux_boot_plan(&image, &mem, 64ULL << 20U, 1, &boot) < 0,
           "initrd larger than available RAM rejected");
    tap_ok(linux_boot_plan(&image, &mem, 48ULL << 20U, 1, &boot) < 0,
           "initrd overlapping decompressor workspace rejected");
    tap_ok(linux_boot_plan(&image, &mem, 0, 2049, &boot) < 0 && errno == E2BIG,
           "command line limited by kernel header, including NUL correctly");
    tap_ok(linux_boot_plan(&image, &mem, 0, 2048, &boot) == 0,
           "maximum command line plus NUL accepted");
    image.header.pref_address = UINT64_MAX - 0x1fffff;
    tap_ok(linux_boot_plan(&image, &mem, 0, 1, &boot) < 0,
           "wrapping kernel workspace rejected");
    image.header.pref_address = 0x1000001;
    tap_ok(linux_boot_plan(&image, &mem, 0, 1, &boot) < 0,
           "misaligned load address rejected");
    image.header.pref_address = 0x1000000;
    image.header.initrd_addr_max = 0x100000;
    tap_ok(linux_boot_plan(&image, &mem, 4096, 1, &boot) < 0,
           "initrd_addr_max enforced");
    image.header.initrd_addr_max = UINT32_MAX;
    mem.regions[0].size = 8ULL << 30U;
    tap_ok(
        linux_boot_plan(&image, &mem, 4096, 1, &boot) == 0 &&
            boot.initrd_addr + boot.initrd_size <= BOOT_IDENTITY_LIMIT,
        "boot objects remain within initial identity map even with large RAM");

    kernel_fd = mkstemp(kernel_path);
    initrd_fd = mkstemp(initrd_path);
    if (kernel_fd < 0 || initrd_fd < 0 ||
        write(kernel_fd, data, sizeof(data)) != (ssize_t)sizeof(data) ||
        write(initrd_fd, initrd, sizeof(initrd)) != (ssize_t)sizeof(initrd) ||
        guest_memory_init(&allocated, 64ULL << 20U) < 0) {
        return 1;
    }
    close(kernel_fd);
    close(initrd_fd);
    tap_ok(linux_load(&allocated, kernel_path, initrd_path, cmdline, &boot) ==
               0,
           "full loader copies files into actual guest memory");
    params = gpa_to_hva(&allocated, BOOT_PARAMS_GPA, sizeof(*params));
    tap_ok(params != NULL && params->hdr.type_of_loader == 0xff &&
               params->hdr.code32_start == boot.kernel_addr &&
               params->hdr.cmd_line_ptr == boot.cmdline_addr &&
               params->hdr.ramdisk_image == boot.initrd_addr &&
               params->hdr.ramdisk_size == sizeof(initrd) &&
               params->e820_entries == 6 && params->sentinel == 0 &&
               params->hdr.setup_data == 0,
           "zero page contains loader fields and E820, with no stale sentinel");
    tap_ok(memcmp(gpa_to_hva(&allocated, boot.kernel_addr, 3072), data + 1024,
                  3072) == 0 &&
               *(uint8_t *)gpa_to_hva(&allocated, boot.kernel_addr + 3072, 1) ==
                   0,
           "protected-mode payload copied without setup sectors; workspace "
           "zeroed");
    tap_ok(
        memcmp(gpa_to_hva(&allocated, boot.initrd_addr, sizeof(initrd)), initrd,
               sizeof(initrd)) == 0 &&
            strcmp(gpa_to_hva(&allocated, boot.cmdline_addr, sizeof(cmdline)),
                   cmdline) == 0,
        "initrd bytes and NUL-terminated command line copied exactly");
    tap_ok(linux_load(&allocated, kernel_path, NULL, "", &boot) == 0 &&
               boot.initrd_size == 0,
           "full loader accepts absent initrd and empty command line");
    tap_ok(linux_load(&allocated, "/dev/zero", NULL, "", &boot) < 0,
           "non-regular input rejected without unbounded read");
    unlink(kernel_path);
    unlink(initrd_path);
    tap_ok(linux_load(&allocated, kernel_path, NULL, "", &boot) < 0 &&
               errno == ENOENT,
           "missing kernel reports ENOENT");
    guest_memory_destroy(&allocated);
    return tap_done();
}
