/*
 * Loader for flat-binary payloads (guest/payloads) used in early stages.
 *
 * A payload is raw machine code with no header and no boot protocol: it is
 * copied to RAW_LOAD_ADDR and run from its first byte.
 */

#ifndef MINIVMM_BOOT_RAW_LOADER_H
#define MINIVMM_BOOT_RAW_LOADER_H

#include "memory.h"

/* Must match the location counter in guest/payloads/payload.ld. */
#define RAW_LOAD_ADDR 0x1000

/*
 * Real mode with CS = 0 only reaches the first 64 KiB, so a payload must end
 * below that.
 */
#define RAW_MAX_SIZE (0x10000 - RAW_LOAD_ADDR)

/*
 * Copy the file at path into guest memory at RAW_LOAD_ADDR. Logs what went
 * wrong and returns -1 with errno set; returns 0 on success.
 */
int raw_load(const struct guest_memory *mem, const char *path);

#endif /* MINIVMM_BOOT_RAW_LOADER_H */
