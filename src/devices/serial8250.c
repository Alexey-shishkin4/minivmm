#include "devices/serial8250.h"

#include <errno.h>
#include <linux/serial_reg.h>
#include <stdint.h>
#include <unistd.h>

void serial8250_init(struct serial8250 *uart, int tx_fd)
{
    *uart = (struct serial8250){
        .tx_fd = tx_fd,
        .lcr = UART_LCR_WLEN8,
        .dll = 1,
        .thre_pending = true,
    };
}

static uint8_t interrupt_id(const struct serial8250 *uart)
{
    if ((uart->ier & UART_IER_RLSI) && uart->lsr_errors) {
        return UART_IIR_RLSI;
    }
    if ((uart->ier & UART_IER_RDI) && uart->rx_ready) {
        return UART_IIR_RDI;
    }
    if ((uart->ier & UART_IER_THRI) && uart->thre_pending) {
        return UART_IIR_THRI;
    }
    if ((uart->ier & UART_IER_MSI) && uart->msr_delta) {
        return UART_IIR_MSI;
    }
    return UART_IIR_NO_INT;
}

static int update_irq(struct serial8250 *uart)
{
    const bool level = (uart->mcr & UART_MCR_OUT2) != 0 &&
                       interrupt_id(uart) != UART_IIR_NO_INT;

    if (uart->set_irq != NULL && level != uart->irq_level) {
        if (uart->set_irq(uart->irq_opaque, level) < 0) {
            return -1;
        }
    }
    uart->irq_level = level;
    return 0;
}

int serial8250_set_irq(struct serial8250 *uart, serial8250_irq_fn fn,
                       void *opaque)
{
    uart->set_irq = fn;
    uart->irq_opaque = opaque;
    uart->irq_level = false;
    return update_irq(uart);
}

static uint8_t modem_inputs(const struct serial8250 *uart)
{
    if (!(uart->mcr & UART_MCR_LOOP)) {
        return UART_MSR_CTS | UART_MSR_DSR | UART_MSR_DCD;
    }

    return (uint8_t)(((uart->mcr & UART_MCR_RTS) ? UART_MSR_CTS : 0) |
                     ((uart->mcr & UART_MCR_DTR) ? UART_MSR_DSR : 0) |
                     ((uart->mcr & UART_MCR_OUT1) ? UART_MSR_RI : 0) |
                     ((uart->mcr & UART_MCR_OUT2) ? UART_MSR_DCD : 0));
}

static void write_mcr(struct serial8250 *uart, uint8_t value)
{
    const uint8_t old_inputs = modem_inputs(uart);
    uint8_t new_inputs;

    uart->mcr = value & 0x1fU;
    new_inputs = modem_inputs(uart);

    /* CTS, DSR and DCD record either edge; RI records only a falling edge. */
    uart->msr_delta |=
        (uint8_t)(((old_inputs ^ new_inputs) &
                   (UART_MSR_CTS | UART_MSR_DSR | UART_MSR_DCD)) >>
                  4U);

    if ((old_inputs & UART_MSR_RI) && !(new_inputs & UART_MSR_RI)) {
        uart->msr_delta |= UART_MSR_TERI;
    }
}

static int transmit(struct serial8250 *uart, uint8_t value)
{
    if (uart->mcr & UART_MCR_LOOP) {
        if (uart->rx_ready) {
            /* Retain the unread byte and report overrun. */
            uart->lsr_errors |= UART_LSR_OE;
        } else {
            uart->rx_byte = value;
            uart->rx_ready = true;
        }
        return 0;
    }

    for (;;) {
        const ssize_t n = write(uart->tx_fd, &value, 1);

        if (n == 1) {
            return 0;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n == 0) {
            errno = EIO;
        }
        return -1;
    }
}

static int write_register(struct serial8250 *uart, uint64_t offset,
                          uint8_t value)
{
    switch (offset) {
    case UART_TX:
        if (transmit(uart, value) < 0) {
            return -1;
        }
        /* Transmission completes synchronously; THR is empty again. */
        uart->thre_pending = true;
        break;

    case UART_IER:
        if (!(uart->ier & UART_IER_THRI) && (value & UART_IER_THRI)) {
            uart->thre_pending = true;
        }
        uart->ier = value & 0x0fU;
        break;

    case UART_FCR:
        /* No FIFO: ignore writes and do not advertise FIFO bits in IIR. */
        break;

    case UART_LCR:
        uart->lcr = value;
        break;

    case UART_MCR:
        write_mcr(uart, value);
        break;

    case UART_SCR:
        uart->scr = value;
        break;

    default:
        /* Writes to the read-only LSR and MSR have no effect. */
        break;
    }

    return 0;
}

static uint8_t read_register(struct serial8250 *uart, uint64_t offset)
{
    uint8_t value;

    switch (offset) {
    case UART_RX:
        value = uart->rx_ready ? uart->rx_byte : 0;
        uart->rx_ready = false;
        return value;

    case UART_IER:
        return uart->ier;

    case UART_IIR:
        value = interrupt_id(uart);
        if (value == UART_IIR_THRI) {
            uart->thre_pending = false;
        }
        return value;

    case UART_LCR:
        return uart->lcr;

    case UART_MCR:
        return uart->mcr;

    case UART_LSR:
        value = (uint8_t)(UART_LSR_THRE | UART_LSR_TEMT | uart->lsr_errors |
                          (uart->rx_ready ? UART_LSR_DR : 0));
        uart->lsr_errors = 0;
        return value;

    case UART_MSR:
        value = (uint8_t)(modem_inputs(uart) | uart->msr_delta);
        uart->msr_delta = 0;
        return value;

    case UART_SCR:
        return uart->scr;

    default:
        return 0;
    }
}

int serial8250_access(void *opaque, uint64_t offset, void *data, size_t len,
                      bool is_write)
{
    struct serial8250 *uart = opaque;
    uint8_t *byte = data;

    if (uart == NULL || data == NULL || len != 1 ||
        offset >= SERIAL8250_REG_COUNT) {
        errno = EINVAL;
        return -1;
    }

    /* DLAB remaps offsets 0 and 1 to the divisor latches. */
    if ((uart->lcr & UART_LCR_DLAB) && offset <= UART_DLM) {
        uint8_t *latch = offset == UART_DLL ? &uart->dll : &uart->dlm;

        if (is_write) {
            *latch = *byte;
        } else {
            *byte = *latch;
        }
        return 0;
    }

    if (is_write) {
        if (write_register(uart, offset, *byte) < 0) {
            return -1;
        }
    } else {
        *byte = read_register(uart, offset);
    }
    return update_irq(uart);
}
