#include "tap.h"

#include "devices/bus.h"

/*
 * TODO: dispatch hits the right device; unmapped port/address reported
 * TODO: overlapping registrations rejected; access spanning two devices
 */

int main(void)
{
    tap_plan(1);
    tap_ok(1, "test binary links against minivmm_core");
    return tap_done();
}
