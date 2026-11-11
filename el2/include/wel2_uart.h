/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - console UART back ends behind a register accessor, so that the polled transmit logic can be
 * tested on the host with a fake register file (tests/test_uart_s5l.c), the way pirq_aic.c is.
 *
 *  PL011 (QEMU virt):   FR at 0x18, bit 5 TXFF = transmit FIFO full; DR at 0x00.
 *                       [verified: Arm PrimeCell UART (PL011) TRM, DDI 0183, register summary]
 *  S5L (Apple, compatible "apple,s5l-uart", a Samsung-style UART):
 *                       UTRSTAT at 0x10, bit 1 (TXBE) = transmit buffer empty; UTXH at 0x20 = transmit byte.
 *                       [verified: register map and bit meaning as listed in m1n1 src/uart_regs.h (facts only,
 *                       no code reused); Linux drivers/tty/serial/samsung_tty.c uses the same offsets for the
 *                       "apple,s5l-uart" type; docs/design/watari-el2-m1boot/DESIGN.md section 7]
 *                       The layer does not program baud rate, clocks or FIFOs: iBoot/m1n1 already set the
 *                       debug UART up (115200 8N1) [verified: DESIGN section 7]. That TXBE behaves as a
 *                       "room for one byte" flag on the real M1 is [hypothesis] until day 2; the bounded wait
 *                       makes a stuck bit lose bytes (counted) instead of hanging the layer.
 */
#ifndef WEL2_UART_H
#define WEL2_UART_H

#include <stdbool.h>
#include <stdint.h>

#define PL011_DR        0x00u
#define PL011_FR        0x18u
#define PL011_FR_TXFF   (1u << 5)

#define S5L_UTRSTAT      0x10u
#define S5L_UTRSTAT_TXBE (1u << 1)
#define S5L_UTXH         0x20u

/* Polls of the status register per byte before a byte is dropped (S5L). */
#define WEL2_UART_S5L_LIMIT  (1u << 22)
/* After this many dropped bytes in a row a byte is tried once only (a dead UART must not slow every line). */
#define WEL2_UART_MAX_FAILS  4u

enum wel2_uart_type { WEL2_UART_PL011, WEL2_UART_S5L };

struct wel2_uart_io {
	uint32_t (*rd)(void *ctx, unsigned off);
	void (*wr)(void *ctx, unsigned off, uint32_t val);
	void *ctx;
};

struct wel2_uart {
	enum wel2_uart_type type;
	struct wel2_uart_io io;
	uint32_t limit;              /* status polls per byte; 0 = wait for ever (PL011, as before) */
	uint32_t fails;              /* bytes dropped in a row */
	uint64_t sent, dropped;
};

/* One raw byte. Returns true if it was written, false if the wait timed out and it was dropped. */
bool wel2_uart_putc(struct wel2_uart *u, uint8_t c);
/* A NUL-terminated string, '\n' sent as "\r\n". */
void wel2_uart_puts(struct wel2_uart *u, const char *s);

#endif
