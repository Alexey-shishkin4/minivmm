#ifndef MINIVMM_DEVICES_I8042_H
#define MINIVMM_DEVICES_I8042_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define I8042_COMMAND_PORT 0x64U

/* Reset-only controller, not a keyboard. Zero initialization is sufficient. */
struct i8042 {
    bool reset_requested;
};

int i8042_access(void *opaque, uint64_t offset, void *data, size_t len,
                 bool is_write);

#endif /* MINIVMM_DEVICES_I8042_H */
