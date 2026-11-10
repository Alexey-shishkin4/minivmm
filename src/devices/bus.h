/* Address-range dispatch for PIO and MMIO devices. */

#ifndef MINIVMM_DEVICES_BUS_H
#define MINIVMM_DEVICES_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BUS_MAX_REGIONS 32

/*
 * offset is relative to the registered base. data is a byte buffer in
 * guest byte order, not an aligned host integer. A read fills all len bytes.
 * Return 0 on success, or -1 with errno set on device failure.
 */
typedef int (*bus_access_fn)(void *opaque, uint64_t offset, void *data,
                             size_t len, bool is_write);

struct bus_region {
    uint64_t base;
    uint64_t size;
    bus_access_fn access;
    void *opaque;
};

struct bus {
    struct bus_region regions[BUS_MAX_REGIONS];
    unsigned nr_regions;
    uint64_t max_addr; /* inclusive: UINT16_MAX for PIO, UINT64_MAX for MMIO */
};

enum bus_result {
    BUS_ERROR = -1,
    BUS_UNMAPPED = 0,
    BUS_HANDLED = 1,
};

/* Initialize an empty bus. No allocation; no destructor is needed. */
void bus_init(struct bus *bus, uint64_t max_addr);

/*
 * Register [base, base + size). Reject empty, overflowing, overlapping and
 * out-of-space ranges. Return 0, or -1 with errno set.
 * The caller owns opaque and keeps it alive while the bus is used.
 * Register devices before running any vCPU; no concurrent mutation.
 */
int bus_register(struct bus *bus, uint64_t base, uint64_t size,
                 bus_access_fn access, void *opaque);

/*
 * Dispatch one access. Partial overlap is BUS_ERROR: an access must fit
 * entirely inside one region. BUS_UNMAPPED leaves data unchanged.
 * BUS_ERROR sets errno. Device callbacks run synchronously on the caller.
 */
enum bus_result bus_access(const struct bus *bus, uint64_t addr, void *data,
                           size_t len, bool is_write);

#endif /* MINIVMM_DEVICES_BUS_H */
