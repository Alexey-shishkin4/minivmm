#include "fuzz.h"
#include "pci/pci.h"

/*
 * TODO: interpret data as a sequence of (offset, size, value, is_write) config
 *        accesses against a virtio-pci device
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}
