#include "boot/linux_loader.h"
#include "fuzz.h"

/*
 * TODO: data is a bzImage: parse setup_header and plan the load into a small
 *        guest memory without writing out of bounds
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}
