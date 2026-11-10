#include <errno.h>
#include <stdint.h>

#include "devices/bus.h"
#include "devices/i8042.h"
#include "tap.h"

int main(void)
{
    struct i8042 controller = {0};
    struct bus bus;
    uint8_t value = 0xff;

    tap_plan(6);
    bus_init(&bus, UINT16_MAX);
    tap_ok(bus_register(&bus, I8042_COMMAND_PORT, 1, i8042_access,
                        &controller) == 0 &&
               bus_access(&bus, I8042_COMMAND_PORT, &value, 1, false) ==
                   BUS_HANDLED &&
               value == 0 && !controller.reset_requested,
           "status reports empty input buffer without requesting reset");
    value = 0x20;
    tap_ok(bus_access(&bus, I8042_COMMAND_PORT, &value, 1, true) ==
                   BUS_HANDLED &&
               !controller.reset_requested,
           "unimplemented non-reset command is ignored");
    value = 0xfe;
    tap_ok(bus_access(&bus, I8042_COMMAND_PORT, &value, 1, true) ==
                   BUS_HANDLED &&
               controller.reset_requested,
           "reset command latches shutdown request");
    tap_ok(i8042_access(&controller, 0, &value, 2, true) < 0 && errno == EINVAL,
           "wide access rejected before reading caller buffer");
    tap_ok(i8042_access(&controller, 1, &value, 1, false) < 0 &&
               errno == EINVAL,
           "invalid offset rejected");
    tap_ok(i8042_access(&controller, 0, NULL, 1, false) < 0 && errno == EINVAL,
           "null data rejected");
    return tap_done();
}
