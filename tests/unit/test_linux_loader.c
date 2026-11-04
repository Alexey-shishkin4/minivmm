#include "tap.h"

#include "boot/linux_loader.h"

/*
 * TODO: parse setup_header from a real bzImage (guest/out) and from truncated /
 *        corrupted copies
 * TODO: initrd placement respects initrd_addr_max
 */

int main(void)
{
    tap_plan(1);
    tap_ok(1, "test binary links against minivmm_core");
    return tap_done();
}
