#include "devices/bus.h"

#include <errno.h>

static bool range_fits(uint64_t max_addr, uint64_t base, uint64_t size)
{
    return size != 0 && base <= max_addr && size - 1 <= max_addr - base;
}

void bus_init(struct bus *bus, uint64_t max_addr)
{
    *bus = (struct bus){.max_addr = max_addr};
}

int bus_register(struct bus *bus, uint64_t base, uint64_t size,
                 bus_access_fn access, void *opaque)
{
    uint64_t last;

    if (access == NULL || !range_fits(bus->max_addr, base, size)) {
        errno = EINVAL;
        return -1;
    }

    last = base + (size - 1);

    for (unsigned i = 0; i < bus->nr_regions; i++) {
        const struct bus_region *r = &bus->regions[i];
        const uint64_t rlast = r->base + (r->size - 1);

        if (base <= rlast && r->base <= last) {
            errno = EEXIST;
            return -1;
        }
    }

    if (bus->nr_regions == BUS_MAX_REGIONS) {
        errno = ENOSPC;
        return -1;
    }

    bus->regions[bus->nr_regions++] = (struct bus_region){
        .base = base,
        .size = size,
        .access = access,
        .opaque = opaque,
    };

    return 0;
}

enum bus_result bus_access(const struct bus *bus, uint64_t addr, void *data,
                           size_t len, bool is_write)
{
    uint64_t last;

    if (data == NULL || !range_fits(bus->max_addr, addr, len)) {
        errno = EINVAL;
        return BUS_ERROR;
    }

    last = addr + ((uint64_t)len - 1);

    for (unsigned i = 0; i < bus->nr_regions; i++) {
        const struct bus_region *r = &bus->regions[i];
        const uint64_t rlast = r->base + (r->size - 1);

        if (last < r->base || addr > rlast) {
            continue;
        }

        if (addr < r->base || last > rlast) {
            errno = EINVAL;
            return BUS_ERROR;
        }

        if (r->access(r->opaque, addr - r->base, data, len, is_write) < 0) {
            return BUS_ERROR;
        }

        return BUS_HANDLED;
    }

    return BUS_UNMAPPED;
}
