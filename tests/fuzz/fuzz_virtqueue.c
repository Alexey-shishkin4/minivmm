#include "fuzz.h"
#include "virtio/virtqueue.h"

/*
 * TODO: treat data as guest RAM + queue layout (desc/avail/used offsets, size,
 *        avail idx); pop chains until empty; must never read outside the buffer, loop
 *        forever or abort
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}
