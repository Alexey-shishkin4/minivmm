#include "tap.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "devices/bus.h"

struct fake_device {
    uint8_t bytes[16];
    uint64_t offset;
    size_t len;
    unsigned calls;
    bool fail;
};

static int fake_access(void *opaque, uint64_t offset, void *data, size_t len,
                       bool is_write)
{
    struct fake_device *dev = opaque;

    dev->calls++;
    dev->offset = offset;
    dev->len = len;
    if (dev->fail) {
        errno = EIO;
        return -1;
    }
    if (offset > sizeof(dev->bytes) || len > sizeof(dev->bytes) - offset) {
        errno = EINVAL;
        return -1;
    }
    if (is_write) {
        memcpy(dev->bytes + offset, data, len);
    } else {
        memcpy(data, dev->bytes + offset, len);
    }
    return 0;
}

int main(void)
{
    struct bus bus;
    struct fake_device a = {0};
    struct fake_device b = {0};
    uint8_t data[16] = {0x5a};
    bool filled = true;
    unsigned calls;

    tap_plan(31);
    bus_init(&bus, UINT16_MAX);
    tap_ok(bus_access(&bus, 100, data, 1, false) == BUS_UNMAPPED &&
               data[0] == 0x5a,
           "unmapped read leaves buffer unchanged");
    tap_ok(bus_register(&bus, 100, 8, fake_access, &a) == 0,
           "register first device");
    tap_ok(bus_register(&bus, 108, 4, fake_access, &b) == 0,
           "adjacent range allowed");
    tap_ok(bus_register(&bus, 107, 2, fake_access, &a) < 0 && errno == EEXIST,
           "overlap rejected");
    tap_ok(bus_register(&bus, 99, 14, fake_access, &a) < 0 && errno == EEXIST,
           "enclosing range rejected");
    tap_ok(bus_register(&bus, 200, 0, fake_access, &a) < 0 && errno == EINVAL,
           "empty region rejected");
    tap_ok(bus_register(&bus, UINT16_MAX, 2, fake_access, &a) < 0 &&
               errno == EINVAL,
           "PIO region exceeds address space");
    tap_ok(bus_register(&bus, 200, 1, NULL, &a) < 0 && errno == EINVAL,
           "null callback rejected");
    tap_ok(bus_access(&bus, 100, data, 1, true) == BUS_HANDLED &&
               a.bytes[0] == 0x5a,
           "write routed to first byte");
    tap_ok(a.offset == 0 && a.len == 1,
           "callback receives relative offset and width");
    tap_ok(bus_access(&bus, 107, data, 1, true) == BUS_HANDLED &&
               a.bytes[7] == 0x5a,
           "last byte included");
    data[0] = 0;
    tap_ok(bus_access(&bus, 107, data, 1, false) == BUS_HANDLED &&
               data[0] == 0x5a,
           "read routed to device");
    tap_ok(bus_access(&bus, 100, data, 8, false) == BUS_HANDLED && a.len == 8,
           "whole-region access allowed");
    tap_ok(bus_access(&bus, 108, data, 1, true) == BUS_HANDLED && b.calls == 1,
           "adjacent device selected");
    calls = a.calls + b.calls;
    tap_ok(bus_access(&bus, 99, data, 2, true) == BUS_ERROR && errno == EINVAL,
           "access starts outside and overlaps region");
    tap_ok(bus_access(&bus, 107, data, 2, true) == BUS_ERROR && errno == EINVAL,
           "access crosses adjacent devices");
    tap_ok(bus_access(&bus, 104, data, 8, false) == BUS_ERROR &&
               errno == EINVAL,
           "larger access spans two regions");
    tap_ok(a.calls + b.calls == calls,
           "invalid accesses have no device side effects");
    tap_ok(bus_access(&bus, 112, data, 1, false) == BUS_UNMAPPED,
           "one past last device is unmapped");
    tap_ok(bus_access(&bus, 100, data, 0, false) == BUS_ERROR &&
               errno == EINVAL,
           "empty access rejected");
    tap_ok(bus_access(&bus, 100, NULL, 1, false) == BUS_ERROR &&
               errno == EINVAL,
           "null data rejected");
    tap_ok(bus_access(&bus, UINT16_MAX, data, 2, false) == BUS_ERROR &&
               errno == EINVAL,
           "PIO access exceeds address space");
    a.fail = true;
    tap_ok(bus_access(&bus, 100, data, 1, false) == BUS_ERROR && errno == EIO,
           "callback error and errno propagated");

    bus_init(&bus, UINT64_MAX);
    a.fail = false;
    tap_ok(bus_register(&bus, UINT64_MAX, 1, fake_access, &a) == 0,
           "region at last possible address allowed");
    tap_ok(bus_access(&bus, UINT64_MAX, data, 1, false) == BUS_HANDLED,
           "last possible byte dispatched without overflow");
    tap_ok(bus_register(&bus, UINT64_MAX, 2, fake_access, &a) < 0 &&
               errno == EINVAL,
           "overflowing region rejected");
    tap_ok(bus_access(&bus, UINT64_MAX, data, 2, false) == BUS_ERROR &&
               errno == EINVAL,
           "overflowing access rejected");
    tap_ok(bus_access(&bus, UINT64_MAX - 1, data, 2, false) == BUS_ERROR,
           "partial overlap at top of address space rejected");

    bus_init(&bus, UINT16_MAX);
    for (unsigned i = 0; i < BUS_MAX_REGIONS; i++) {
        filled = filled && bus_register(&bus, 2U * i, 1, fake_access, &a) == 0;
    }
    tap_ok(filled, "all registration slots usable");
    tap_ok(bus_register(&bus, 1000, 1, fake_access, &a) < 0 && errno == ENOSPC,
           "capacity limit enforced");
    tap_ok(bus.nr_regions == BUS_MAX_REGIONS,
           "failed registration preserves count");
    return tap_done();
}
