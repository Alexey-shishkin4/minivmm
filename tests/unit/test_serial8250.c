#include "tap.h"

#include <errno.h>
#include <linux/serial_reg.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "devices/bus.h"
#include "devices/serial8250.h"

static bool accesses_ok = true;

static void reg_write(struct serial8250 *uart, uint64_t reg, uint8_t value)
{
    if (serial8250_access(uart, reg, &value, 1, true) < 0) {
        accesses_ok = false;
    }
}

static uint8_t reg_read(struct serial8250 *uart, uint64_t reg)
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
    struct bus bus;
    int fds[2];
    uint8_t data[2] = {0};
    uint8_t msr;
    char output[4] = {0};

    tap_plan(32);
    if (pipe(fds) < 0) {
        return 1;
    }
    serial8250_init(&uart, fds[1]);
    tap_ok(reg_read(&uart, UART_LSR) == (UART_LSR_THRE | UART_LSR_TEMT),
           "transmitter starts empty, receiver has no data");
    tap_ok(reg_read(&uart, UART_RX) == 0, "empty RBR reads zero");
    tap_ok(reg_read(&uart, UART_IIR) == UART_IIR_NO_INT, "no IRQ pending");
    reg_write(&uart, UART_SCR, 0xa5);
    tap_ok(reg_read(&uart, UART_SCR) == 0xa5, "scratch pattern a5");
    reg_write(&uart, UART_SCR, 0x5a);
    tap_ok(reg_read(&uart, UART_SCR) == 0x5a, "scratch pattern 5a");
    reg_write(&uart, UART_IER, 0xff);
    tap_ok(reg_read(&uart, UART_IER) == 0x0f, "IER masks reserved bits");
    reg_write(&uart, UART_LCR, UART_LCR_DLAB | UART_LCR_WLEN8);
    reg_write(&uart, UART_DLL, 0x34);
    reg_write(&uart, UART_DLM, 0x12);
    tap_ok(reg_read(&uart, UART_DLL) == 0x34 &&
               reg_read(&uart, UART_DLM) == 0x12,
           "divisor latches readable with DLAB set");
    reg_write(&uart, UART_LCR, UART_LCR_WLEN8);
    tap_ok(reg_read(&uart, UART_IER) == 0x0f,
           "DLM writes do not overwrite IER");
    tap_ok(reg_read(&uart, UART_LCR) == UART_LCR_WLEN8, "LCR readback");
    reg_write(&uart, UART_FCR, UART_FCR_ENABLE_FIFO);
    tap_ok((reg_read(&uart, UART_IIR) & UART_IIR_FIFO_ENABLED) == 0,
           "unsupported FIFO is not advertised");
    tap_ok((reg_read(&uart, UART_MSR) & 0xf0) == 0xb0,
           "connected modem inputs");
    reg_write(&uart, UART_MCR, UART_MCR_DTR | UART_MCR_RTS);
    tap_ok(reg_read(&uart, UART_MSR) == 0xb0,
           "nonzero MCR without LOOP preserves external modem inputs");
    reg_write(&uart, UART_MCR, UART_MCR_LOOP | UART_MCR_RTS | UART_MCR_OUT2);
    tap_ok((reg_read(&uart, UART_MSR) & 0xf0) == (UART_MSR_CTS | UART_MSR_DCD),
           "modem loopback maps RTS and OUT2");
    reg_write(&uart, UART_MCR,
              UART_MCR_LOOP | UART_MCR_RTS | UART_MCR_OUT2 | UART_MCR_DTR);
    msr = reg_read(&uart, UART_MSR);
    tap_ok((msr & UART_MSR_DDSR) != 0 && (msr & UART_MSR_DSR) != 0,
           "modem input change sets delta bit");
    tap_ok((reg_read(&uart, UART_MSR) & UART_MSR_ANY_DELTA) == 0,
           "MSR read clears delta bits");
    reg_write(&uart, UART_TX, 'A');
    tap_ok((reg_read(&uart, UART_LSR) & UART_LSR_DR) != 0,
           "loopback sets data ready");
    tap_ok(reg_read(&uart, UART_RX) == 'A' &&
               !(reg_read(&uart, UART_LSR) & UART_LSR_DR),
           "RBR consumes loopback byte");
    reg_write(&uart, UART_TX, 'B');
    reg_write(&uart, UART_TX, 'C');
    tap_ok((reg_read(&uart, UART_LSR) & UART_LSR_OE) != 0,
           "unread byte causes overrun");
    tap_ok(!(reg_read(&uart, UART_LSR) & UART_LSR_OE),
           "LSR read clears overrun");
    tap_ok(reg_read(&uart, UART_RX) == 'B', "overrun retains unread byte");
    reg_write(&uart, UART_MCR, UART_MCR_LOOP | UART_MCR_OUT1);
    (void)reg_read(&uart, UART_MSR);
    reg_write(&uart, UART_MCR, UART_MCR_LOOP);
    tap_ok((reg_read(&uart, UART_MSR) & UART_MSR_TERI) != 0,
           "falling RI sets TERI");
    reg_write(&uart, UART_MCR, 0);
    reg_write(&uart, UART_LSR, 0);
    tap_ok(reg_read(&uart, UART_LSR) == 0x60, "LSR writes ignored");

    bus_init(&bus, UINT16_MAX);
    tap_ok(bus_register(&bus, 0x3f8, SERIAL8250_REG_COUNT, serial8250_access,
                        &uart) == 0 &&
               bus_access(&bus, 0x3fd, data, 1, false) == BUS_HANDLED &&
               data[0] == 0x60,
           "bus routes LSR access by relative offset");
    tap_ok(serial8250_access(&uart, 0, data, 2, false) < 0 && errno == EINVAL,
           "wide register access rejected");
    tap_ok(serial8250_access(&uart, 8, data, 1, false) < 0 && errno == EINVAL,
           "out-of-range register rejected");

    tap_ok(serial8250_access(NULL, 0, data, 1, false) < 0 && errno == EINVAL,
           "null UART rejected");
    tap_ok(serial8250_access(&uart, 0, NULL, 1, false) < 0 && errno == EINVAL,
           "null data rejected");
    tap_ok(serial8250_access(&uart, 0, data, 0, false) < 0 && errno == EINVAL,
           "empty access rejected");
    tap_ok(serial8250_access(&uart, UINT64_MAX, data, 1, false) < 0 &&
               errno == EINVAL,
           "maximum offset rejected");

    reg_write(&uart, UART_TX, 'X');
    reg_write(&uart, UART_TX, 0);
    reg_write(&uart, UART_TX, 'Y');
    close(fds[1]);
    tap_ok(read(fds[0], output, sizeof(output)) == 3 &&
               memcmp(output, "X\0Y", 3) == 0,
           "TX writes exact bytes; divisor and loopback writes stay off host "
           "output");
    close(fds[0]);
    uart.tx_fd = -1;
    data[0] = 'Z';
    tap_ok(serial8250_access(&uart, UART_TX, data, 1, true) < 0 &&
               errno == EBADF,
           "host write error propagated");
    tap_ok(accesses_ok, "all valid register accesses succeeded");
    return tap_done();
}
