/*
 * Linux x86 boot protocol: bzImage + initrd + command line.
 *
 * TODO: parse struct setup_header (Documentation/arch/x86/boot.rst), check
 *        magic "HdrS", version, loadflags, xloadflags (XLF_KERNEL_64)
 * TODO: load protected-mode kernel at 1 MiB (or pref_address), initrd high
 * TODO: fill struct boot_params: e820 table, cmd_line_ptr, ramdisk_image/size
 * TODO: treat the image as untrusted input: validate every size/offset
 */

#ifndef MINIVMM_BOOT_LINUX_LOADER_H
#define MINIVMM_BOOT_LINUX_LOADER_H

struct vmm;

#endif /* MINIVMM_BOOT_LINUX_LOADER_H */
