/*
 * 16550A UART emulation (COM1, ports 0x3f8-0x3ff, IRQ 4).
 *
 * TODO: registers: RBR/THR, IER, IIR/FCR, LCR, MCR, LSR, MSR, SCR, DLL/DLM
 * TODO: TX -> stdout; RX from stdin via event loop; THRE/RDA interrupts
 * TODO: enough for earlyprintk=serial and console=ttyS0
 */

#ifndef MINIVMM_DEVICES_SERIAL8250_H
#define MINIVMM_DEVICES_SERIAL8250_H

struct serial8250;

#endif /* MINIVMM_DEVICES_SERIAL8250_H */
