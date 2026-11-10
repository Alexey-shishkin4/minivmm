#ifndef MINIVMM_BOOT_LAYOUT_H
#define MINIVMM_BOOT_LAYOUT_H

/* Low-memory workspace shared by the Linux loader, E820 and CPU setup. */
#define BOOT_TABLES_GPA 0x10000ULL
#define BOOT_PARAMS_GPA 0x20000ULL
#define BOOT_CMDLINE_GPA 0x21000ULL
#define BOOT_WORKSPACE_END 0x22000ULL
#define BOOT_CMDLINE_CAPACITY 0x1000U
#define BOOT_IDENTITY_LIMIT 0x40000000ULL /* initial page tables cover 1 GiB */

#endif /* MINIVMM_BOOT_LAYOUT_H */
