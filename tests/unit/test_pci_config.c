#include "tap.h"

#include "pci/pci.h"

/*
 * TODO: config read/write via 0xCF8/0xCFC; read-only bits stay read-only
 * TODO: BAR sizing: write ~0, read back size mask; capability list walk
 *        terminates
 */

int main(void)
{
    tap_plan(1);
    tap_ok(1, "test binary links against minivmm_core");
    return tap_done();
}
