#include "boot/linux_loader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "boot/e820.h"
#include "boot/layout.h"
#include "memory.h"
#include "util/log.h"

#define SETUP_HEADER_OFFSET 0x1f1U
#define MIN_HEADER_END 0x264U
#define KERNEL_FILE_LIMIT (128ULL << 20U)

_Static_assert(sizeof(struct boot_params) == 4096, "Linux zero page ABI");

int linux_image_parse(const uint8_t *data, size_t size,
                      struct linux_image *image)
{
    struct linux_image parsed = {0};
    size_t header_end;
    size_t header_size;
    size_t setup_size;

    if (data == NULL || image == NULL || size < MIN_HEADER_END ||
        size > KERNEL_FILE_LIMIT) {
        errno = ENOEXEC;
        return -1;
    }
    header_end = 0x202U + data[0x201];
    setup_size =
        ((size_t)(data[SETUP_HEADER_OFFSET] != 0 ? data[SETUP_HEADER_OFFSET]
                                                 : 4) +
         1U) *
        512U;
    if (data[0x200] != 0xeb || header_end < MIN_HEADER_END ||
        header_end > size || header_end > setup_size || setup_size >= size) {
        errno = ENOEXEC;
        return -1;
    }
    header_size = header_end - SETUP_HEADER_OFFSET;
    if (header_size > sizeof(parsed.header)) {
        header_size = sizeof(parsed.header);
    }
    /* Host and guest are both little-endian x86-64; the UAPI type is packed. */
    memcpy(&parsed.header, data + SETUP_HEADER_OFFSET, header_size);
    parsed.payload_offset = setup_size;
    parsed.payload_size = size - setup_size;
    if (parsed.header.boot_flag != 0xaa55 ||
        parsed.header.header != 0x53726448 || parsed.header.version < 0x020c ||
        !(parsed.header.loadflags & LOADED_HIGH) ||
        !(parsed.header.xloadflags & XLF_KERNEL_64) ||
        parsed.payload_size <= 0x200 ||
        (uint64_t)parsed.header.syssize * 16U < parsed.payload_size ||
        (uint64_t)parsed.header.syssize * 16U > parsed.payload_size + 15U ||
        parsed.header.init_size < parsed.payload_size ||
        parsed.header.kernel_alignment < GUEST_PAGE_SIZE ||
        (parsed.header.kernel_alignment &
         (parsed.header.kernel_alignment - 1U)) != 0) {
        errno = ENOEXEC;
        return -1;
    }
    *image = parsed;
    return 0;
}

static bool backed(const struct guest_memory *mem, uint64_t addr, uint64_t size)
{
    if (size == 0 || addr >= BOOT_IDENTITY_LIMIT ||
        size > BOOT_IDENTITY_LIMIT - addr) {
        return false;
    }
    for (unsigned i = 0; i < mem->nr_regions; i++) {
        const struct guest_region *r = &mem->regions[i];
        if (addr >= r->gpa && addr - r->gpa <= r->size &&
            size <= r->size - (addr - r->gpa)) {
            return true;
        }
    }
    return false;
}

int linux_boot_plan(const struct linux_image *image,
                    const struct guest_memory *mem, uint64_t initrd_size,
                    size_t cmdline_size, struct linux_boot *boot)
{
    struct linux_boot plan = {0};
    uint64_t ceiling = 0;
    uint64_t alignment;

    if (image == NULL || mem == NULL || boot == NULL || mem->nr_regions == 0 ||
        mem->nr_regions > GUEST_MEM_MAX_REGIONS || cmdline_size == 0 ||
        image->header.kernel_alignment < GUEST_PAGE_SIZE ||
        (image->header.kernel_alignment &
         (image->header.kernel_alignment - 1U)) != 0 ||
        image->header.init_size < image->payload_size ||
        image->header.init_size <= 0x200) {
        errno = EINVAL;
        return -1;
    }
    if (cmdline_size > BOOT_CMDLINE_CAPACITY ||
        cmdline_size - 1 > image->header.cmdline_size) {
        errno = E2BIG;
        return -1;
    }
    alignment = image->header.kernel_alignment;
    plan.kernel_addr = image->header.pref_address;
    if (plan.kernel_addr == 0 && image->header.relocatable_kernel) {
        plan.kernel_addr = 0x1000000;
    }
    /* Non-relocatable kernels move to pref_address even when loaded lower. */
    if (plan.kernel_addr < 0x100000 ||
        (plan.kernel_addr & (alignment - 1U)) != 0) {
        errno = ENOEXEC;
        return -1;
    }
    plan.kernel_size = image->header.init_size;
    plan.params_addr = BOOT_PARAMS_GPA;
    plan.cmdline_addr = BOOT_CMDLINE_GPA;
    if (!backed(mem, BOOT_TABLES_GPA, BOOT_WORKSPACE_END - BOOT_TABLES_GPA) ||
        !backed(mem, plan.kernel_addr, plan.kernel_size)) {
        errno = ENOMEM;
        return -1;
    }
    plan.entry = plan.kernel_addr + 0x200;
    if (initrd_size != 0) {
        for (unsigned i = 0; i < mem->nr_regions; i++) {
            if (mem->regions[i].gpa == 0) {
                ceiling = mem->regions[i].size;
                break;
            }
        }
        if (ceiling > BOOT_IDENTITY_LIMIT) {
            ceiling = BOOT_IDENTITY_LIMIT;
        }
        if (ceiling > (uint64_t)image->header.initrd_addr_max + 1U) {
            ceiling = (uint64_t)image->header.initrd_addr_max + 1U;
        }
        if (initrd_size >= ceiling) {
            errno = ENOMEM;
            return -1;
        }
        plan.initrd_addr = (ceiling - initrd_size) & ~(GUEST_PAGE_SIZE - 1U);
        if (plan.initrd_addr < plan.kernel_addr + plan.kernel_size ||
            !backed(mem, plan.initrd_addr, initrd_size)) {
            errno = ENOMEM;
            return -1;
        }
        plan.initrd_size = initrd_size;
    }
    *boot = plan;
    return 0;
}

static int open_image(const char *path, uint64_t limit, size_t *size)
{
    struct stat st;
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    int saved;

    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st) < 0) {
        goto fail;
    }
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > limit) {
        errno = EINVAL;
        goto fail;
    }
    *size = (size_t)st.st_size;
    return fd;
fail:
    saved = errno;
    close(fd);
    errno = saved;
    return -1;
}

static int read_full(int fd, void *buf, size_t size)
{
    uint8_t *dst = buf;
    while (size != 0) {
        const ssize_t n = read(fd, dst, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            if (n == 0) {
                errno = EIO;
            }
            return -1;
        }
        dst += (size_t)n;
        size -= (size_t)n;
    }
    return 0;
}

int linux_load(const struct guest_memory *mem, const char *kernel_path,
               const char *initrd_path, const char *cmdline,
               struct linux_boot *boot)
{
    struct linux_image image;
    struct linux_boot plan;
    struct boot_params params = {0};
    uint8_t *data = NULL;
    void *kernel_dst;
    void *params_dst;
    void *cmdline_dst;
    void *initrd_dst = NULL;
    size_t kernel_size;
    size_t initrd_size = 0;
    size_t count;
    size_t cmdline_size;
    int kernel_fd = -1;
    int initrd_fd = -1;
    int ret = -1;
    int saved;

    if (mem == NULL || kernel_path == NULL || cmdline == NULL || boot == NULL) {
        errno = EINVAL;
        return -1;
    }
    cmdline_size = strlen(cmdline) + 1U;
    kernel_fd = open_image(kernel_path, KERNEL_FILE_LIMIT, &kernel_size);
    if (kernel_fd < 0) {
        log_errno("open kernel %s", kernel_path);
        goto out;
    }
    data = malloc(kernel_size);
    if (data == NULL || read_full(kernel_fd, data, kernel_size) < 0) {
        log_errno("read kernel %s", kernel_path);
        goto out;
    }
    if (linux_image_parse(data, kernel_size, &image) < 0) {
        log_errno("invalid or unsupported x86-64 bzImage %s", kernel_path);
        goto out;
    }
    if (initrd_path != NULL) {
        initrd_fd = open_image(initrd_path, BOOT_IDENTITY_LIMIT, &initrd_size);
        if (initrd_fd < 0) {
            log_errno("open initrd %s", initrd_path);
            goto out;
        }
    }
    if (linux_boot_plan(&image, mem, initrd_size, cmdline_size, &plan) < 0) {
        log_errno("Linux boot layout (kernel workspace, initrd, command line)");
        goto out;
    }
    params.hdr = image.header;
    params.hdr.type_of_loader = 0xff;
    params.hdr.code32_start = (uint32_t)plan.kernel_addr;
    params.hdr.cmd_line_ptr = (uint32_t)plan.cmdline_addr;
    params.hdr.ramdisk_image = (uint32_t)plan.initrd_addr;
    params.hdr.ramdisk_size = (uint32_t)plan.initrd_size;
    /* No real-mode setup, setup_data chain or boot-loader hooks are used. */
    params.hdr.setup_data = 0;
    params.hdr.realmode_swtch = 0;
    params.hdr.hardware_subarch = 0;
    params.hdr.hardware_subarch_data = 0;
    if (e820_build(mem, params.e820_table, E820_MAX_ENTRIES_ZEROPAGE, &count) <
        0) {
        log_errno("build E820 map");
        goto out;
    }
    params.e820_entries = (uint8_t)count;
    kernel_dst = gpa_to_hva(mem, plan.kernel_addr, plan.kernel_size);
    params_dst = gpa_to_hva(mem, plan.params_addr, sizeof(params));
    cmdline_dst = gpa_to_hva(mem, plan.cmdline_addr, cmdline_size);
    if (initrd_size != 0) {
        initrd_dst = gpa_to_hva(mem, plan.initrd_addr, initrd_size);
    }
    if (kernel_dst == NULL || params_dst == NULL || cmdline_dst == NULL ||
        (initrd_size != 0 && initrd_dst == NULL)) {
        errno = ENOMEM;
        goto out;
    }
    memset(kernel_dst, 0, (size_t)plan.kernel_size);
    memcpy(kernel_dst, data + image.payload_offset, image.payload_size);
    memcpy(params_dst, &params, sizeof(params));
    memcpy(cmdline_dst, cmdline, cmdline_size);
    if (initrd_size != 0 && read_full(initrd_fd, initrd_dst, initrd_size) < 0) {
        log_errno("read initrd %s", initrd_path);
        goto out;
    }
    *boot = plan;
    log_info("Linux: kernel 0x%llx, entry 0x%llx, initrd 0x%llx (%zu bytes)",
             (unsigned long long)plan.kernel_addr,
             (unsigned long long)plan.entry,
             (unsigned long long)plan.initrd_addr, initrd_size);
    ret = 0;
out:
    saved = errno;
    free(data);
    if (initrd_fd >= 0) {
        close(initrd_fd);
    }
    if (kernel_fd >= 0) {
        close(kernel_fd);
    }
    errno = saved;
    return ret;
}
