/* SPDX-License-Identifier: MIT */
/* Host tests: console UART back ends (wel2_uart.h) against a fake register file, and the FDT lookup that selects the
 * Apple S5L UART (fdt_find_compatible_reg). The register offsets and bits are the ones listed in wel2_uart.h. */
#include <stdbool.h>
#include <string.h>
#include "test.h"
#include "wel2_uart.h"
#include "fdt.h"
#include "fdt_build.h"

/* ---- fake register file ---------------------------------------------------------------------------------------- */
struct fake {
	enum wel2_uart_type type;
	unsigned busy_polls;        /* after each byte: status reads that report "not ready" before one reports ready */
	unsigned left;              /* countdown for the current byte */
	bool stuck;                 /* never ready */
	uint8_t out[256];
	unsigned n;
	unsigned reads, bad_access; /* bad_access: an offset the driver has no business touching */
};

static uint32_t f_rd(void *ctx, unsigned off)
{
	struct fake *f = ctx;
	bool ready;

	f->reads++;
	if (f->type == WEL2_UART_S5L && off == S5L_UTRSTAT) {
		ready = !f->stuck && f->left == 0;
		if (!ready && f->left)
			f->left--;
		return ready ? S5L_UTRSTAT_TXBE : 0x4;          /* bit 2 (transmitter empty) must not matter */
	}
	if (f->type == WEL2_UART_PL011 && off == PL011_FR) {
		ready = !f->stuck && f->left == 0;
		if (!ready && f->left)
			f->left--;
		return ready ? 0x90 : (PL011_FR_TXFF | 0x90);   /* other FR bits set on purpose */
	}
	f->bad_access++;
	return 0;
}

static void f_wr(void *ctx, unsigned off, uint32_t v)
{
	struct fake *f = ctx;

	if ((f->type == WEL2_UART_S5L && off == S5L_UTXH) || (f->type == WEL2_UART_PL011 && off == PL011_DR)) {
		if (f->n < sizeof(f->out))
			f->out[f->n++] = (uint8_t)v;
		f->left = f->busy_polls;
	} else {
		f->bad_access++;
	}
}

static struct wel2_uart mk(struct fake *f, enum wel2_uart_type t, uint32_t limit)
{
	memset(f, 0, sizeof(*f));
	f->type = t;
	return (struct wel2_uart){ t, { f_rd, f_wr, f }, limit, 0, 0, 0 };
}

static void t_s5l_tx(void)
{
	struct fake f;
	struct wel2_uart u = mk(&f, WEL2_UART_S5L, 1000);

	CHECK(wel2_uart_putc(&u, 'W'));
	CHECK(wel2_uart_putc(&u, 'x'));
	CHECK_EQ(f.n, 2);
	CHECK_EQ(f.out[0], 'W');
	CHECK_EQ(f.out[1], 'x');
	CHECK_EQ(u.sent, 2);
	CHECK_EQ(u.dropped, 0);
	CHECK_EQ(f.bad_access, 0);
}

static void t_s5l_waits_for_txbe(void)
{
	struct fake f;
	struct wel2_uart u = mk(&f, WEL2_UART_S5L, 1000);

	f.busy_polls = 7;                    /* after each byte: 7 polls "full", then ready */
	CHECK(wel2_uart_putc(&u, 'a'));
	unsigned r0 = f.reads;
	CHECK(wel2_uart_putc(&u, 'b'));      /* must poll 8 times (7 not ready + 1 ready) before the write */
	CHECK_EQ(f.reads - r0, 8);
	CHECK_EQ(f.n, 2);
	CHECK_EQ(u.dropped, 0);
	CHECK_EQ(f.bad_access, 0);
}

static void t_s5l_timeout_drops_and_counts(void)
{
	struct fake f;
	struct wel2_uart u = mk(&f, WEL2_UART_S5L, 50);

	f.stuck = true;
	CHECK(!wel2_uart_putc(&u, 'a'));
	CHECK_EQ(f.reads, 50);               /* the budget, no more */
	CHECK_EQ(f.n, 0);
	CHECK_EQ(u.dropped, 1);
	CHECK_EQ(u.sent, 0);
	/* after WEL2_UART_MAX_FAILS drops in a row a byte costs one poll only */
	for (unsigned i = 1; i < WEL2_UART_MAX_FAILS; i++)
		CHECK(!wel2_uart_putc(&u, 'a'));
	unsigned r = f.reads;
	CHECK(!wel2_uart_putc(&u, 'a'));
	CHECK_EQ(f.reads - r, 1);
	/* the UART comes back: the next byte goes out and the failure streak is over */
	f.stuck = false;
	CHECK(wel2_uart_putc(&u, 'z'));
	CHECK_EQ(f.n, 1);
	CHECK_EQ(f.out[0], 'z');
	CHECK_EQ(u.fails, 0);
	CHECK_EQ(u.dropped, WEL2_UART_MAX_FAILS + 1);
}

static void t_newline_cr_lf(void)
{
	struct fake f;
	struct wel2_uart u = mk(&f, WEL2_UART_S5L, 1000);

	wel2_uart_puts(&u, "ab\ncd\n");
	CHECK_EQ(f.n, 8);
	CHECK(!memcmp(f.out, "ab\r\ncd\r\n", 8));
	f.n = 0;                             /* a bare "\r" in the text is passed through unchanged */
	wel2_uart_puts(&u, "x\ry");
	CHECK_EQ(f.n, 3);
	CHECK(!memcmp(f.out, "x\ry", 3));
}

static void t_pl011_unchanged(void)
{
	struct fake f;
	struct wel2_uart u = mk(&f, WEL2_UART_PL011, 0);    /* limit 0: wait for ever, as before */

	f.busy_polls = 3;
	wel2_uart_puts(&u, "hi\n");
	CHECK_EQ(f.n, 4);
	CHECK(!memcmp(f.out, "hi\r\n", 4));
	CHECK_EQ(f.bad_access, 0);
	CHECK_EQ(u.dropped, 0);
	/* TXFF set: FR is read until it clears; the other FR bits are ignored */
	f.n = 0;
	f.left = 40;
	unsigned r = f.reads;
	CHECK(wel2_uart_putc(&u, 'q'));
	CHECK_EQ(f.reads - r, 41);
	CHECK_EQ(f.out[0], 'q');
}

/* ---- FDT selection ----------------------------------------------------------------------------------------------- */
/* A j293-shaped tree: /soc (2 + 2 cells) with the AIC and the UART as in the day-1 ADT. */
static void *m1_tree(struct fdtb *b, bool with_uart, bool reg_first)
{
	fb_init(b);
	fb_begin(b, "");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
	fb_strs(b, "compatible", "apple,j293\0apple,t8103\0apple,arm-platform\0", 42);
	fb_begin(b, "soc");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
	fb_prop(b, "ranges", 0, 0);
	fb_begin(b, "aic@23b100000");
	fb_strs(b, "compatible", "apple,t8103-aic\0apple,aic\0", 26);
	fb_reg(b, 2, 2, 0x23b100000ull, 0xc000);
	fb_end(b);
	if (with_uart) {
		fb_begin(b, "serial@235200000");
		if (reg_first) {
			fb_reg(b, 2, 2, 0x235200000ull, 0x4000);
			fb_strs(b, "compatible", "apple,s5l-uart\0", 15);
		} else {
			fb_strs(b, "compatible", "apple,s5l-uart\0", 15);
			fb_reg(b, 2, 2, 0x235200000ull, 0x4000);
		}
		fb_end(b);
	}
	fb_end(b);
	fb_end(b);
	return fb_finish(b);
}

static void t_fdt_finds_s5l(void)
{
	static struct fdtb b;
	struct fdt_reg_info i;
	void *fdt = m1_tree(&b, true, false);

	CHECK_EQ(fdt_check(fdt), 0);
	CHECK_EQ(fdt_find_compatible_reg(fdt, FDT_COMPAT_S5L_UART, &i), 0);
	CHECK(i.found);
	CHECK_EQ(i.base, 0x235200000ull);
	CHECK_EQ(i.size, 0x4000);
	fdt = m1_tree(&b, true, true);       /* "reg" before "compatible" in the node */
	CHECK_EQ(fdt_find_compatible_reg(fdt, FDT_COMPAT_S5L_UART, &i), 0);
	CHECK(i.found);
	CHECK_EQ(i.base, 0x235200000ull);
	/* the AIC node is found by the second string of its list */
	CHECK_EQ(fdt_find_compatible_reg(fdt, "apple,aic", &i), 0);
	CHECK(i.found);
	CHECK_EQ(i.base, 0x23b100000ull);
	CHECK_EQ(i.size, 0xc000);
}

static void t_fdt_without_node(void)
{
	static struct fdtb b;
	struct fdt_reg_info i;
	void *fdt = m1_tree(&b, false, false);

	CHECK_EQ(fdt_find_compatible_reg(fdt, FDT_COMPAT_S5L_UART, &i), 0);
	CHECK(!i.found);

	/* QEMU virt: a PL011 node, 2 + 2 cells at the root, no Apple node: the PL011 stays */
	fb_init(&b);
	fb_begin(&b, "");
	fb_u32(&b, "#address-cells", 2);
	fb_u32(&b, "#size-cells", 2);
	fb_begin(&b, "pl011@9000000");
	fb_strs(&b, "compatible", "arm,pl011\0arm,primecell\0", 24);
	fb_reg(&b, 2, 2, 0x9000000ull, 0x1000);
	fb_end(&b);
	fb_end(&b);
	fdt = fb_finish(&b);
	CHECK_EQ(fdt_find_compatible_reg(fdt, FDT_COMPAT_S5L_UART, &i), 0);
	CHECK(!i.found);
}

static void t_fdt_cells_and_edge_cases(void)
{
	static struct fdtb b;
	struct fdt_reg_info i;

	/* 1 + 1 cells; a string that only starts with the wanted one does not match; a compatible without reg
	 * does not count; the match may be the second string of the list */
	fb_init(&b);
	fb_begin(&b, "");
	fb_u32(&b, "#address-cells", 1);
	fb_u32(&b, "#size-cells", 1);
	fb_begin(&b, "serial@1000");
	fb_strs(&b, "compatible", "apple,s5l-uart-x\0", 17);
	fb_reg(&b, 1, 1, 0x1000, 0x100);
	fb_end(&b);
	fb_begin(&b, "serial@2000");
	fb_strs(&b, "compatible", "apple,s5l-uart\0", 15);     /* no reg */
	fb_end(&b);
	fb_begin(&b, "serial@3000");
	fb_strs(&b, "compatible", "foo,bar\0apple,s5l-uart\0", 23);
	fb_reg(&b, 1, 1, 0x3000, 0x200);
	fb_end(&b);
	fb_end(&b);
	void *fdt = fb_finish(&b);

	CHECK_EQ(fdt_find_compatible_reg(fdt, FDT_COMPAT_S5L_UART, &i), 0);
	CHECK(i.found);
	CHECK_EQ(i.base, 0x3000);
	CHECK_EQ(i.size, 0x200);

	/* a blob whose structure block is cut inside a node name is malformed, never read past its end */
	fb_init(&b);
	fb_begin(&b, "");
	fb_begin(&b, "serial@1000");
	fb_strs(&b, "compatible", "apple,s5l-uart\0", 15);
	fdt = fb_finish(&b);
	CHECK_EQ(fdt_find_compatible_reg(fdt, FDT_COMPAT_S5L_UART, &i), 0);   /* END token ends the walk: no reg */
	CHECK(!i.found);
	fb_put32((uint8_t *)fdt + 36, 6);
	CHECK_EQ(fdt_find_compatible_reg(fdt, FDT_COMPAT_S5L_UART, &i), -1);
	CHECK(!i.found);
}

int main(void)
{
	RUN(t_s5l_tx);
	RUN(t_s5l_waits_for_txbe);
	RUN(t_s5l_timeout_drops_and_counts);
	RUN(t_newline_cr_lf);
	RUN(t_pl011_unchanged);
	RUN(t_fdt_finds_s5l);
	RUN(t_fdt_without_node);
	RUN(t_fdt_cells_and_edge_cases);
	TESTS_DONE();
}
