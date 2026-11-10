#include "devices/i8042.h"

#include <errno.h>

int i8042_access(void *opaque, uint64_t offset, void *data, size_t len,
                 bool is_write)
{
    struct i8042 *controller = opaque;
    uint8_t *value = data;

    if (controller == NULL || offset != 0 || data == NULL || len != 1) {
        errno = EINVAL;
        return -1;
    }
    if (is_write) {
        if (*value == 0xfe) {
            controller->reset_requested = true;
        }
    } else {
        /* Input buffer empty: Linux's kb_wait() must be allowed to finish. */
        *value = 0;
    }
    return 0;
}
