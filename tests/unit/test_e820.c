#include "tap.h"

#include "boot/e820.h"

/*
 * TODO: table for 64 MiB / 3 GiB / 8 GiB guests: hole below 4 GiB, sorted, no
 *        overlaps
 */

int main(void)
{
    tap_plan(1);
    tap_ok(1, "test binary links against minivmm_core");
    return tap_done();
}
