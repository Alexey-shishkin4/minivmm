/* Minimal 16450-style UART: synchronous TX, IRQs, loopback RX, no FIFO. */

#ifndef MINIVMM_DEVICES_SERIAL8250_H
#define MINIVMM_DEVICES_SERIAL8250_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SERIAL8250_REG_COUNT 8

typedef int (*serial8250_irq_fn)(void *opaque, bool level);

struct serial8250 {
    int tx_fd; /* borrowed output fd; caller owns its lifetime */
    uint8_t ier;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t scr;
    uint8_t dll;
    uint8_t dlm;
    uint8_t rx_byte; /* loopback only; external RX is not implemented */
    bool rx_ready;
    uint8_t lsr_errors;
    uint8_t msr_delta;
    bool thre_pending;
    bool irq_level;
    serial8250_irq_fn set_irq;
    void *irq_opaque;
};

/* No allocation or fd ownership. Default framing is 8N1, divisor 1. */
void serial8250_init(struct serial8250 *uart, int tx_fd);

/* Optional IRQ sink. OUT2 gates delivery; errors propagate through access(). */
int serial8250_set_irq(struct serial8250 *uart, serial8250_irq_fn fn,
                       void *opaque);

/*
 * bus_access_fn callback. Only byte accesses at offsets 0..7 are supported.
 * Return 0, or -1 with errno set on invalid access or host output failure.
 * Single-threaded: no concurrent register accesses or RX producers.
 */
int serial8250_access(void *opaque, uint64_t offset, void *data, size_t len,
                      bool is_write);

#endif /* MINIVMM_DEVICES_SERIAL8250_H */
