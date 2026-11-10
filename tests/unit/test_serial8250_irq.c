#include <errno.h>
#include <linux/serial_reg.h>
#include <stdint.h>
#include <unistd.h>

#include "devices/serial8250.h"
#include "tap.h"

struct irq_sink {
    bool level;
    bool fail;
    unsigned edges;
};
static bool accesses_ok = true;

static int irq(void *opaque, bool level)
{
    struct irq_sink *sink = opaque;
    if (sink->fail) {
        errno = EIO;
        return -1;
    }
    sink->level = level;
    sink->edges++;
    return 0;
}

static void wr(struct serial8250 *uart, uint64_t reg, uint8_t value)
{
    if (serial8250_access(uart, reg, &value, 1, true) < 0) {
        accesses_ok = false;
    }
}

static uint8_t rd(struct serial8250 *uart, uint64_t reg)
{
    uint8_t value = 0;
    if (serial8250_access(uart, reg, &value, 1, false) < 0) {
        accesses_ok = false;
    }
    return value;
}

int main(void)
{
    struct serial8250 uart;
    struct irq_sink sink = {0};
    int fds[2];
    uint8_t value;

    tap_plan(17);
    if (pipe(fds) < 0) {
        return 1;
    }
    serial8250_init(&uart, fds[1]);
    tap_ok(serial8250_set_irq(&uart, irq, &sink) == 0 && !sink.level,
           "IRQ starts deasserted");
    wr(&uart, UART_IER, UART_IER_THRI);
    tap_ok(!sink.level && sink.edges == 0, "OUT2 gates external IRQ delivery");
    wr(&uart, UART_MCR, UART_MCR_OUT2);
    tap_ok(sink.level && sink.edges == 1,
           "enabling OUT2 delivers pending THRE");
    (void)rd(&uart, UART_LSR);
    tap_ok(sink.level, "LSR read does not acknowledge THRE interrupt");
    tap_ok(rd(&uart, UART_IIR) == UART_IIR_THRI && !sink.level &&
               sink.edges == 2,
           "reading THRE IIR acknowledges interrupt and lowers IRQ");
    tap_ok(rd(&uart, UART_IIR) == UART_IIR_NO_INT, "THRE stays acknowledged");
    wr(&uart, UART_TX, 'X');
    tap_ok(sink.level && rd(&uart, UART_IIR) == UART_IIR_THRI && !sink.level,
           "synchronous TX completion raises another THRE interrupt");
    tap_ok(read(fds[0], &value, 1) == 1 && value == 'X',
           "interrupting TX still emits the actual byte");
    wr(&uart, UART_IER, 0);
    wr(&uart, UART_IER, UART_IER_THRI);
    tap_ok(sink.level,
           "reenabling THRI while empty recreates pending interrupt");
    wr(&uart, UART_MCR, 0);
    tap_ok(!sink.level && rd(&uart, UART_IIR) == UART_IIR_THRI,
           "OUT2 masks wire level while IIR still reports the source");
    wr(&uart, UART_MCR, UART_MCR_OUT2 | UART_MCR_LOOP);
    wr(&uart, UART_IER, UART_IER_THRI | UART_IER_RDI | UART_IER_RLSI);
    wr(&uart, UART_TX, 'A');
    tap_ok(rd(&uart, UART_IIR) == UART_IIR_RDI && sink.level,
           "received loopback data has priority over THRE");
    tap_ok(rd(&uart, UART_RX) == 'A' && rd(&uart, UART_IIR) == UART_IIR_THRI &&
               !sink.level,
           "RX consume reveals THRE; its acknowledgement lowers IRQ");
    wr(&uart, UART_TX, 'B');
    wr(&uart, UART_TX, 'C');
    tap_ok(rd(&uart, UART_IIR) == UART_IIR_RLSI &&
               (rd(&uart, UART_LSR) & UART_LSR_OE) &&
               rd(&uart, UART_IIR) == UART_IIR_RDI,
           "line-status error has priority and clears on LSR read");
    tap_ok(rd(&uart, UART_RX) == 'B' && rd(&uart, UART_IIR) == UART_IIR_THRI &&
               !sink.level,
           "overrun preserves unread data and all sources can be acknowledged");
    (void)rd(&uart, UART_MSR);
    wr(&uart, UART_IER, UART_IER_MSI);
    wr(&uart, UART_MCR, UART_MCR_OUT2 | UART_MCR_LOOP | UART_MCR_RTS);
    tap_ok(sink.level && rd(&uart, UART_IIR) == UART_IIR_MSI &&
               (rd(&uart, UART_MSR) & UART_MSR_DCTS) && !sink.level,
           "modem delta interrupt clears on MSR read");
    sink.fail = true;
    value = UART_IER_THRI;
    tap_ok(serial8250_access(&uart, UART_IER, &value, 1, true) < 0 &&
               errno == EIO,
           "IRQ delivery error propagates to caller");
    tap_ok(accesses_ok, "all other register accesses succeeded");
    close(fds[0]);
    close(fds[1]);
    return tap_done();
}
