// SPDX-License-Identifier: MIT
/* Watari EL2 - polled console transmit for the PL011 and the Apple S5L UART. See wel2_uart.h. */
#include "wel2_uart.h"

bool wel2_uart_putc(struct wel2_uart *u, uint8_t c)
{
	unsigned st_off, st_bit, tx_off;
	bool ready_when_set;

	if (u->type == WEL2_UART_S5L) {
		st_off = S5L_UTRSTAT; st_bit = S5L_UTRSTAT_TXBE; tx_off = S5L_UTXH; ready_when_set = true;
	} else {
		st_off = PL011_FR; st_bit = PL011_FR_TXFF; tx_off = PL011_DR; ready_when_set = false;
	}

	uint32_t budget = u->limit;

	if (budget && u->fails >= WEL2_UART_MAX_FAILS)
		budget = 1;
	for (;;) {
		bool set = (u->io.rd(u->io.ctx, st_off) & st_bit) != 0;

		if (set == ready_when_set)
			break;
		if (budget && --budget == 0) {
			u->dropped++;
			u->fails++;
			return false;
		}
	}
	u->io.wr(u->io.ctx, tx_off, c);
	u->sent++;
	u->fails = 0;
	return true;
}

void wel2_uart_puts(struct wel2_uart *u, const char *s)
{
	for (; *s; s++) {
		if (*s == '\n')
			wel2_uart_putc(u, '\r');
		wel2_uart_putc(u, (uint8_t)*s);
	}
}
