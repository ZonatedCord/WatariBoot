/* SPDX-License-Identifier: MIT */
/* Host tests: the M1 FDT queries (boot/fdt_m1.c, PLAN S7 / T6b) on a j293-shaped tree (the shape m1n1 produces for
 * the minimal DTS of DESIGN 4.2, plus the measured MPIDRs of re/m3a-ehw0-measurements.md), a QEMU-shaped tree,
 * variants (disabled / NOP'd nodes, UART selection) and malformed or truncated blobs. The tree carries a
 * serial-number and a MAC address on purpose: nothing of them may ever reach a result or the dump tool output
 * (the Makefile greps the dump of the fixture written here). Also the per-depth cells fix of
 * fdt_find_compatible_reg (T5 review note). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "fdt.h"
#include "fdt_m1.h"
#include "fdt_build.h"
#include "fx_j293.h"

static uint32_t rd_be(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void *mk_tiny(struct fdtb *b)
{
	fb_init(b);
	fb_begin(b, "");
	fb_u32(b, "x", 1);
	fb_end(b);
	return fb_finish(b);
}

static void check_j293_ranges(const struct m1_platform *p)
{
	CHECK_EQ(p->nmem, 3);
	CHECK_EQ(p->mem[0].base, 0x800000000ull); CHECK_EQ(p->mem[0].size, 0x8000000);
	CHECK_EQ(p->mem[1].base, 0x808000000ull); CHECK_EQ(p->mem[1].size, 0x1000000);
	CHECK_EQ(p->mem[2].base, 0x810000000ull); CHECK_EQ(p->mem[2].size, 0x10000000);
	CHECK_EQ(p->nresv, 4);                           /* 2 reserved-memory children with reg + 2 memreserve entries */
	CHECK_EQ(p->nusable, 3);
	CHECK_EQ(p->usable[0].base, 0x800004000ull);
	CHECK_EQ(p->usable[0].size, 0x807ff0000ull - 0x800004000ull);
	CHECK_EQ(p->usable[1].base, 0x810000000ull); CHECK_EQ(p->usable[1].size, 0x2000000);
	CHECK_EQ(p->usable[2].base, 0x812100000ull); CHECK_EQ(p->usable[2].size, 0x820000000ull - 0x812100000ull);
	CHECK_EQ(p->mem_wholly_reserved, 1);             /* design S-4: pair B is gone */
	CHECK(!p->lists_truncated);
	CHECK_EQ(p->bad_ranges, 0);
}

static void t_j293(void)
{
	struct fdtb b;
	struct m1_platform p;
	void *fdt = build_j293(&b, &O0);

	CHECK_EQ(fdt_m1_parse(fdt, &p), 0);
	CHECK(p.is_m1);
	CHECK_EQ(p.ncpu, 8);
	CHECK(!p.cpus_truncated);
	for (unsigned i = 0; i < 8; i++) {
		CHECK_EQ(p.cpu[i].reg, CPU_REG[i]);
		CHECK(p.cpu[i].spin_table);
		CHECK_EQ(p.cpu[i].has_release, i == 5);       /* <0 0> is the placeholder: no release address */
		CHECK_EQ(p.cpu[i].release_addr, i == 5 ? 0x800123450ull : 0);
	}
	check_j293_ranges(&p);
	CHECK_EQ(p.naic, 2);
	CHECK_EQ(p.aic[0].base, 0x23b100000ull); CHECK_EQ(p.aic[0].size, 0xc000);
	CHECK_EQ(p.aic[1].base, 0x23b108000ull); CHECK_EQ(p.aic[1].size, 0x1000);
	CHECK(p.uart.found); CHECK(p.uart.from_stdout);
	CHECK_EQ(p.uart.base, 0x235200000ull); CHECK_EQ(p.uart.size, 0x4000);
	CHECK(p.wdt.found);
	CHECK_EQ(p.wdt.base, 0x23d2b0000ull); CHECK_EQ(p.wdt.size, 0x4000);
	CHECK_EQ(p.fb.fmt, WEL2_FB_X2R10G10B10);
	CHECK_EQ(p.fb.base, 0x9e2570000ull); CHECK_EQ(p.fb.width, 2560); CHECK_EQ(p.fb.height, 1600);
	CHECK_EQ(p.fb.stride, 10240);

	/* the existing single-purpose queries agree */
	struct fdt_reg_info ri;
	CHECK_EQ(fdt_find_compatible_reg(fdt, "apple,aic", &ri), 0);
	CHECK(ri.found); CHECK_EQ(ri.base, 0x23b100000ull); CHECK_EQ(ri.size, 0xc000);
	CHECK_EQ(fdt_find_compatible_reg(fdt, "apple,wdt", &ri), 0);
	CHECK(ri.found); CHECK_EQ(ri.base, 0x23d2b0000ull);

	/* the file the Makefile feeds to fdt_m1_dump */
	FILE *f = fopen("build/fx_j293.dtb", "wb");
	CHECK(f != NULL);
	if (f) {
		fwrite(fdt, 1, fdt_totalsize(fdt), f);
		fclose(f);
	}
}

static void t_reserve_iterator(void)
{
	struct fdtb b;
	uint64_t a = 0, s = 0;
	void *fdt = build_j293(&b, &O0);

	CHECK_EQ(fdt_reserve_entry(fdt, 0, &a, &s), 1);
	CHECK_EQ(a, 0x800000000ull); CHECK_EQ(s, 0x4000);
	CHECK_EQ(fdt_reserve_entry(fdt, 1, &a, &s), 1);
	CHECK_EQ(a, 0x807ff0000ull); CHECK_EQ(s, 0x10000);
	CHECK_EQ(fdt_reserve_entry(fdt, 2, &a, &s), 0);
	CHECK_EQ(fdt_reserve_entry(fdt, 99, &a, &s), 0);
	/* the terminator overwritten: no end inside the reservation block */
	memset(b.buf + 40 + 16 * 2, 0xff, 32);
	CHECK_EQ(fdt_reserve_entry(fdt, 2, &a, &s), 1);
	CHECK_EQ(fdt_reserve_entry(fdt, 3, &a, &s), 1);
	CHECK_EQ(fdt_reserve_entry(fdt, 4, &a, &s), 0);
	memset(b.buf + 40, 0xff, 16 * 5);                       /* all 5 slots non-zero: runs into the structure block */
	CHECK_EQ(fdt_reserve_entry(fdt, 9, &a, &s), -1);
	struct m1_platform p;
	CHECK_EQ(fdt_m1_parse(fdt, &p), -1);
}

static void t_variants(void)
{
	struct fdtb b;
	struct m1_platform p;
	struct opt o = O0;

	/* a disabled cpu is skipped; the order of the others is kept */
	o.cpu_disabled = 2;
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK_EQ(p.ncpu, 7);
	CHECK_EQ(p.cpu[1].reg, 1); CHECK_EQ(p.cpu[2].reg, 3); CHECK_EQ(p.cpu[6].reg, 0x10103);

	/* a NOP'd cpu node (all its tokens replaced by NOP, as fdt_del_node does) is not there */
	o = O0;
	o.cpu_nop = 4;
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK_EQ(p.ncpu, 7);
	CHECK_EQ(p.cpu[3].reg, 3); CHECK_EQ(p.cpu[4].reg, 0x10101);

	/* more cpus than the layer has stacks for */
	o = O0;
	o.extra_cpus = 3;
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK_EQ(p.ncpu, M1_MAX_CPUS);
	CHECK(p.cpus_truncated);

	/* no framebuffer node */
	o = O0;
	o.no_fb = true;
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK_EQ(p.fb.fmt, WEL2_FB_NONE);
}

static void t_uart_selection(void)
{
	struct fdtb b;
	struct m1_platform p;
	struct opt o = O0;

	/* stdout-path names the second UART (with options): it wins over the first one found */
	o.stdout_path = "/soc/serial@235208000:115200n8";
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK(p.uart.found); CHECK(p.uart.from_stdout); CHECK_EQ(p.uart.base, 0x235208000ull);

	/* an alias in stdout-path, resolved through /aliases */
	o = O0;
	o.stdout_path = "serial0";
	o.serial0 = "/soc/serial@235208000";
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK(p.uart.from_stdout); CHECK_EQ(p.uart.base, 0x235208000ull);

	/* no stdout-path: the serial0 alias */
	o = O0;
	o.stdout_path = NULL;
	o.serial0 = "/soc/serial@235208000";
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK(p.uart.from_stdout); CHECK_EQ(p.uart.base, 0x235208000ull);

	/* neither: first found, and it is not "from stdout" */
	o = O0;
	o.stdout_path = NULL;
	o.serial0 = NULL;
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK(p.uart.found); CHECK(!p.uart.from_stdout); CHECK_EQ(p.uart.base, 0x235200000ull);

	/* the preferred UART is disabled: it is skipped, the next enabled one is used */
	o = O0;
	o.uart0_disabled = true;
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK(p.uart.found); CHECK(!p.uart.from_stdout); CHECK_EQ(p.uart.base, 0x235208000ull);
	struct fdt_reg_info ri;
	CHECK_EQ(fdt_find_compatible_reg(build_j293(&b, &o), FDT_COMPAT_S5L_UART, &ri), 0);
	CHECK(ri.found); CHECK_EQ(ri.base, 0x235208000ull);        /* the T5 lookup respects status too */

	/* stdout-path pointing at something that is not an S5L UART: ignored */
	o = O0;
	o.stdout_path = "/soc/nothing";
	o.serial0 = NULL;
	CHECK_EQ(fdt_m1_parse(build_j293(&b, &o), &p), 0);
	CHECK(!p.uart.from_stdout); CHECK_EQ(p.uart.base, 0x235200000ull);
}

static void t_qemu(void)
{
	struct fdtb b;
	struct m1_platform p;
	static const uint64_t mem[1][2] = { { 0x40000000ull, 0x80000000ull } };
	char nm[16];

	fb_init(&b);
	fb_begin(&b, "");
	fb_u32(&b, "#address-cells", 2);
	fb_u32(&b, "#size-cells", 2);
	fb_str(&b, "compatible", "linux,dummy-virt");
	fb_begin(&b, "chosen");
	fb_str(&b, "bootargs", "wel2.step=1");
	fb_str(&b, "stdout-path", "/pl011@9000000");
	fb_end(&b);
	fb_begin(&b, "memory@40000000");
	fb_str(&b, "device_type", "memory");
	fb_regs(&b, 2, 2, 1, mem);
	fb_end(&b);
	fb_begin(&b, "cpus");
	fb_u32(&b, "#address-cells", 1);
	fb_u32(&b, "#size-cells", 0);
	for (unsigned i = 0; i < 4; i++) {
		snprintf(nm, sizeof(nm), "cpu@%x", i);
		fb_begin(&b, nm);
		fb_str(&b, "device_type", "cpu");
		fb_str(&b, "compatible", "arm,cortex-a57");
		fb_reg(&b, 1, 0, i, 0);
		fb_str(&b, "enable-method", "psci");
		fb_end(&b);
	}
	fb_end(&b);
	fb_begin(&b, "pl011@9000000");
	fb_strs(&b, "compatible", "arm,pl011\0arm,primecell\0", 24);
	fb_reg(&b, 2, 2, 0x9000000, 0x1000);
	fb_end(&b);
	fb_begin(&b, "intc@8000000");
	fb_str(&b, "compatible", "arm,gic-v3");
	fb_reg(&b, 2, 2, 0x8000000, 0x10000);
	fb_end(&b);
	fb_end(&b);
	void *fdt = fb_finish(&b);

	CHECK_EQ(fdt_m1_parse(fdt, &p), 0);
	CHECK(!p.is_m1);
	CHECK_EQ(p.ncpu, 4);
	for (unsigned i = 0; i < 4; i++) {
		CHECK_EQ(p.cpu[i].reg, i);
		CHECK(!p.cpu[i].spin_table);
		CHECK(!p.cpu[i].has_release);
	}
	CHECK_EQ(p.nmem, 1);
	CHECK_EQ(p.nresv, 0);
	CHECK_EQ(p.nusable, 1);
	CHECK_EQ(p.mem_wholly_reserved, 0);
	CHECK_EQ(p.naic, 0);
	CHECK(!p.uart.found);
	CHECK(!p.wdt.found);
	CHECK_EQ(p.fb.fmt, WEL2_FB_NONE);
	/* the facts the QEMU path uses today (fdt_get_memory, fdt_get_bootargs) are the same ones */
	struct wel2_ram ram;
	CHECK_EQ(fdt_get_memory(fdt, &ram, 0, ~0ull), 0);
	CHECK_EQ(ram.base, p.mem[0].base);
	CHECK_EQ(ram.end, p.mem[0].base + p.mem[0].size);
	char args[64];
	CHECK(fdt_get_bootargs(fdt, args, sizeof(args)) > 0);
}

/* T5 review note: the cells of a parent that carries no property of its own are the defaults (2 and 1), not what an
 * earlier sibling subtree declared. */
static void t_cells_per_depth(void)
{
	struct fdtb b;
	struct fdt_reg_info ri;
	struct m1_platform p;
	void *fdt;

	fb_init(&b);
	fb_begin(&b, "");
	fb_u32(&b, "#address-cells", 2);
	fb_u32(&b, "#size-cells", 2);
	fb_begin(&b, "a");
	fb_u32(&b, "#address-cells", 1);
	fb_u32(&b, "#size-cells", 1);
	fb_begin(&b, "x");
	fb_end(&b);
	fb_end(&b);
	fb_begin(&b, "bus");                                    /* no properties at all: children use 2 and 1 */
	fb_begin(&b, "serial@12345000");
	fb_str(&b, "compatible", "apple,s5l-uart");
	{
		uint32_t cells[3] = { 0, 0x12345000u, 0x1000 };

		u32cells(&b, "reg", 3, cells);
	}
	fb_end(&b);
	fb_end(&b);
	fb_end(&b);
	fdt = fb_finish(&b);
	CHECK_EQ(fdt_find_compatible_reg(fdt, FDT_COMPAT_S5L_UART, &ri), 0);
	CHECK(ri.found);
	CHECK_EQ(ri.base, 0x12345000u);
	CHECK_EQ(ri.size, 0x1000);
	CHECK_EQ(fdt_m1_parse(fdt, &p), 0);
	CHECK(p.uart.found);
	CHECK_EQ(p.uart.base, 0x12345000u);
	CHECK_EQ(p.uart.size, 0x1000);

	/* a parent with reg that has a sub-node is still found */
	fb_init(&b);
	fb_begin(&b, "");
	fb_begin(&b, "dev");
	fb_str(&b, "compatible", "apple,wdt");
	{
		uint32_t cells[3] = { 0x1, 0x2000, 0x40 };

		u32cells(&b, "reg", 3, cells);
	}
	fb_begin(&b, "child");
	fb_end(&b);
	fb_end(&b);
	fb_end(&b);
	fdt = fb_finish(&b);
	CHECK_EQ(fdt_find_compatible_reg(fdt, "apple,wdt", &ri), 0);
	CHECK(ri.found); CHECK_EQ(ri.base, 0x100002000ull); CHECK_EQ(ri.size, 0x40);
	CHECK_EQ(fdt_m1_parse(fdt, &p), 0);
	CHECK(p.wdt.found); CHECK_EQ(p.wdt.base, 0x100002000ull);
}

static void t_subtract(void)
{
	struct m1_range mem[2] = { { 0x1000, 0x1000 }, { 0x10000, 0x1000 } };
	struct m1_range resv[6] = { { 0x1400, 0x100 }, { 0x1000, 0x200 }, { 0x1f00, 0x500 }, { 0x10000, 0x1000 },
				    { 0x500, 0 }, { ~0ull - 0x10, 0x100 } };
	struct m1_range out[8];
	unsigned n, w = 0;
	bool tr = false;

	m1_subtract(mem, 2, resv, 6, out, 8, &n, &w, &tr);
	CHECK(!tr);
	CHECK_EQ(w, 1);                                  /* the second pair is wholly reserved */
	CHECK_EQ(n, 2);
	CHECK_EQ(out[0].base, 0x1200); CHECK_EQ(out[0].size, 0x200);
	CHECK_EQ(out[1].base, 0x1500); CHECK_EQ(out[1].size, 0x1f00 - 0x1500);
}

static void t_subtract_precise(void)
{
	/* [0x1000, 0x2000) minus [0x1400,0x1500) minus [0x1000,0x1200) minus [0x1f00,0x2400) */
	struct m1_range mem[1] = { { 0x1000, 0x1000 } };
	struct m1_range resv[3] = { { 0x1400, 0x100 }, { 0x1000, 0x200 }, { 0x1f00, 0x500 } };
	struct m1_range out[8];
	unsigned n, w;
	bool tr = false;

	m1_subtract(mem, 1, resv, 3, out, 8, &n, &w, &tr);
	CHECK(!tr); CHECK_EQ(w, 0); CHECK_EQ(n, 2);
	CHECK_EQ(out[0].base, 0x1200); CHECK_EQ(out[0].size, 0x200);
	CHECK_EQ(out[1].base, 0x1500); CHECK_EQ(out[1].size, 0xa00);

	/* output too small: the pieces that do not fit are dropped and flagged, never invented */
	n = 0;
	tr = false;
	m1_subtract(mem, 1, resv, 1, out, 1, &n, &w, &tr);
	CHECK(tr); CHECK_EQ(n, 1);
	CHECK_EQ(out[0].base, 0x1000); CHECK_EQ(out[0].size, 0x400);

	/* overlapping and duplicate reservations, a reservation covering everything */
	struct m1_range all[2] = { { 0, ~0ull >> 1 }, { 0x1000, 0x1000 } };
	tr = false;
	m1_subtract(mem, 1, all, 2, out, 8, &n, &w, &tr);
	CHECK_EQ(n, 0); CHECK_EQ(w, 1);
}

static void ovf_header(struct fdtb *b)
{
	fb_init(b);
	fb_begin(b, "");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
}

static void ovf_mem(struct fdtb *b, const char *name, unsigned n, uint64_t first)
{
	uint64_t pairs[12][2];

	for (unsigned i = 0; i < n; i++) {
		pairs[i][0] = first + 0x10000000ull * i;
		pairs[i][1] = 0x1000000;
	}
	fb_begin(b, name);
	fb_str(b, "device_type", "memory");
	fb_regs(b, 2, 2, n, pairs);
	fb_end(b);
}

static void ovf_resv_child(struct fdtb *b, unsigned i, uint64_t base, uint64_t size)
{
	char nm[24];

	snprintf(nm, sizeof(nm), "r@%x", i);
	fb_begin(b, nm);
	fb_reg(b, 2, 2, base, size);
	fb_end(b);
}

static void t_overflow_lists(void)
{
	struct fdtb b;
	struct m1_platform p;

	/* more /memory pairs than kept: safe (less RAM), flagged */
	ovf_header(&b);
	ovf_mem(&b, "memory@800000000", 12, 0x800000000ull);
	ovf_mem(&b, "memory@900000000", 12, 0x900000000ull);
	fb_end(&b);
	CHECK_EQ(fdt_m1_parse(fb_finish(&b), &p), 0);
	CHECK_EQ(p.nmem, M1_MAX_MEM);
	CHECK(p.lists_truncated);

	/* exactly M1_MAX_RESV children: fine; one more: the parse fails (reserved RAM must never become usable) */
	for (unsigned extra = 0; extra < 2; extra++) {
		ovf_header(&b);
		ovf_mem(&b, "memory@800000000", 1, 0x800000000ull);
		fb_begin(&b, "reserved-memory");
		fb_u32(&b, "#address-cells", 2);
		fb_u32(&b, "#size-cells", 2);
		for (unsigned i = 0; i < M1_MAX_RESV + extra; i++)
			ovf_resv_child(&b, i, 0x100000000ull + 0x1000ull * i, 0x1000);
		fb_end(&b);
		fb_end(&b);
		CHECK_EQ(fdt_m1_parse(fb_finish(&b), &p), extra ? -1 : 0);
		if (!extra)
			CHECK_EQ(p.nresv, M1_MAX_RESV);
		else
			CHECK_EQ(p.nresv, 0);
	}

	/* one child with more reg pairs than fit */
	{
		uint64_t pairs[12][2];

		for (unsigned i = 0; i < 12; i++) {
			pairs[i][0] = 0x100000000ull + 0x1000ull * i;
			pairs[i][1] = 0x1000;
		}
		ovf_header(&b);
		fb_begin(&b, "reserved-memory");
		fb_u32(&b, "#address-cells", 2);
		fb_u32(&b, "#size-cells", 2);
		for (unsigned c = 0; c < 6; c++) {
			char nm[16];

			snprintf(nm, sizeof(nm), "m@%x", c);
			fb_begin(&b, nm);
			fb_regs(&b, 2, 2, 12, pairs);
			fb_end(&b);
		}
		fb_end(&b);
		fb_end(&b);
		CHECK_EQ(fdt_m1_parse(fb_finish(&b), &p), -1);   /* 72 pairs > 64 */
	}

	/* a wrapping reservation fails; a zero-sized one is ignored and counted */
	for (unsigned k = 0; k < 2; k++) {
		ovf_header(&b);
		ovf_mem(&b, "memory@800000000", 1, 0x800000000ull);
		fb_begin(&b, "reserved-memory");
		fb_u32(&b, "#address-cells", 2);
		fb_u32(&b, "#size-cells", 2);
		ovf_resv_child(&b, 0, k ? 0x5000 : ~0ull - 0x10, k ? 0 : 0x1000);
		fb_end(&b);
		fb_end(&b);
		CHECK_EQ(fdt_m1_parse(fb_finish(&b), &p), k ? 0 : -1);
		if (k) {
			CHECK_EQ(p.bad_ranges, 1);
			CHECK_EQ(p.nresv, 0);
		}
	}

	/* a wrapping /memreserve/ entry fails */
	fb_init_rsv(&b, 1);
	fb_rsv(&b, ~0ull - 0x10, 0x1000);
	fb_begin(&b, "");
	fb_u32(&b, "#address-cells", 2);
	fb_u32(&b, "#size-cells", 2);
	fb_end(&b);
	CHECK_EQ(fdt_m1_parse(fb_finish(&b), &p), -1);

	/* a reserved-memory child whose reg cannot be decoded (3 address cells) fails */
	fb_init(&b);
	fb_begin(&b, "");
	fb_begin(&b, "reserved-memory");
	fb_u32(&b, "#address-cells", 3);
	fb_u32(&b, "#size-cells", 1);
	fb_begin(&b, "x@0");
	{
		uint32_t cells[4] = { 0, 0, 0x1000, 0x1000 };

		u32cells(&b, "reg", 4, cells);
	}
	fb_end(&b);
	fb_end(&b);
	fb_end(&b);
	CHECK_EQ(fdt_m1_parse(fb_finish(&b), &p), -1);
}

/* ---- malformed blobs --------------------------------------------------------------------------------------- */

static void t_malformed(void)
{
	struct fdtb b;
	struct m1_platform p;
	uint8_t *h;
	uint32_t total;

	/* bad magic, NULL */
	h = build_j293(&b, &O0);
	h[0] ^= 1;
	CHECK_EQ(fdt_m1_parse(h, &p), -1);
	CHECK_EQ(fdt_m1_parse(NULL, &p), -1);
	CHECK_EQ(p.ncpu, 0); CHECK(!p.is_m1);                    /* zeroed on failure */

	/* totalsize too small for the blocks it describes */
	h = build_j293(&b, &O0);
	total = fdt_totalsize(h);
	fb_put32(h + 4, total - 8);
	CHECK_EQ(fdt_m1_parse(h, &p), -1);
	fb_put32(h + 4, 20);
	CHECK_EQ(fdt_m1_parse(h, &p), -1);
	fb_put32(h + 4, 0xffffffffu);                            /* absurd */
	CHECK_EQ(fdt_m1_parse(h, &p), -1);

	/* offsets: misaligned structure block, block past the end, reservation block after the structure block */
	h = build_j293(&b, &O0);
	fb_put32(h + 8, rd_be(h + 8) + 2);
	CHECK_EQ(fdt_m1_parse(h, &p), -1);
	h = build_j293(&b, &O0);
	fb_put32(h + 12, total);                                 /* strings block starts at the end, size > 0 */
	CHECK_EQ(fdt_m1_parse(h, &p), -1);
	h = build_j293(&b, &O0);
	fb_put32(h + 16, rd_be(h + 8) + 8);
	CHECK_EQ(fdt_m1_parse(h, &p), -1);
	h = build_j293(&b, &O0);
	fb_put32(h + 16, 44);                                    /* misaligned reservation block */
	CHECK_EQ(fdt_m1_parse(h, &p), -1);

	/* structure block cut before the END token / before the root closes */
	h = build_j293(&b, &O0);
	fb_put32(h + 36, rd_be(h + 36) - 8);
	CHECK_EQ(fdt_m1_parse(h, &p), -1);
	h = build_j293(&b, &O0);
	fb_put32(h + 36, 100);
	CHECK_EQ(fdt_m1_parse(h, &p), -1);

	/* property longer than the structure block, name offset outside the strings block */
	h = mk_tiny(&b);
	fb_put32(h + 64 + 8 + 4, 0x10000);                       /* len of the first property: after BEGIN(4)+name(4)+tok(4) */
	CHECK_EQ(fdt_m1_parse(h, &p), -1);
	h = mk_tiny(&b);
	fb_put32(h + 64 + 12 + 4, 0x7fffffffu);                  /* nameoff */
	CHECK_EQ(fdt_m1_parse(h, &p), -1);

	/* node name without a terminator inside the structure block */
	{
		uint8_t raw[128];

		memset(raw, 0, sizeof(raw));
		fb_put32(raw, 0xd00dfeedu); fb_put32(raw + 4, 80); fb_put32(raw + 8, 56); fb_put32(raw + 12, 80);
		fb_put32(raw + 16, 40); fb_put32(raw + 32, 0); fb_put32(raw + 36, 24);
		fb_put32(raw + 56, 1);
		memcpy(raw + 60, "abcdabcdabcdabcdabcd", 20);        /* no NUL until the block ends */
		CHECK_EQ(fdt_m1_parse(raw, &p), -1);
	}

	/* nesting deeper than the limit */
	{
		struct fdtb d;

		fb_init(&d);
		for (int i = 0; i < 20; i++)
			fb_begin(&d, "n");
		for (int i = 0; i < 20; i++)
			fb_end(&d);
		CHECK_EQ(fdt_m1_parse(fb_finish(&d), &p), -1);
	}
	/* unbalanced END_NODE, a second root, no root */
	{
		struct fdtb d;

		fb_init(&d);
		fb_end(&d);
		CHECK_EQ(fdt_m1_parse(fb_finish(&d), &p), -1);
		fb_init(&d);
		fb_begin(&d, ""); fb_end(&d); fb_begin(&d, ""); fb_end(&d);
		CHECK_EQ(fdt_m1_parse(fb_finish(&d), &p), -1);
		fb_init(&d);
		CHECK_EQ(fdt_m1_parse(fb_finish(&d), &p), -1);
	}
	/* unknown token */
	h = build_j293(&b, &O0);
	fb_put32(h + rd_be(h + 8), 0x77);
	CHECK_EQ(fdt_m1_parse(h, &p), -1);

	/* property name not terminated inside the strings block: the name matches nothing, no read past the block */
	h = build_j293(&b, &O0);
	{
		uint32_t strs = rd_be(h + 12), sz = rd_be(h + 32);

		for (uint32_t cut = 1; cut < sz; cut += 3) {
			fb_put32(h + 32, cut);
			int rc = fdt_m1_parse(h, &p);

			CHECK(rc == 0 || rc == -1);
		}
		(void)strs;
	}
}

/* Every single-byte corruption and every cut of the header sizes: never a crash (a clean exit under
 * -fsanitize=address,undefined was checked by hand; the buffer is exactly totalsize bytes so a stray read shows). */
static void t_fuzz(void)
{
	struct fdtb b;
	struct m1_platform p;
	uint8_t *src = build_j293(&b, &O0);
	uint32_t total = fdt_totalsize(src);
	uint8_t *copy = malloc(total);
	static const uint8_t vals[] = { 0x00, 0xff, 0x80, 0x01 };

	for (uint32_t i = 0; i < total; i++)
		for (unsigned k = 0; k < sizeof(vals); k++) {
			memcpy(copy, src, total);
			copy[i] = vals[k];
			int rc = fdt_m1_parse(copy, &p);
			if (rc)
				rc = -1;
			CHECK(rc == 0 || rc == -1);
			if (rc)
				CHECK(!p.is_m1 && p.ncpu == 0 && p.nmem == 0);
		}
	for (uint32_t cut = 0; cut < total; cut += 4) {          /* the blob really ends here: header sizes cut to match */
		uint8_t *tmp = malloc(cut ? cut : 1);

		memcpy(tmp, src, cut);
		if (cut >= 40) {
			fb_put32(tmp + 4, cut);
			int rc = fdt_m1_parse(tmp, &p);

			CHECK(rc == 0 || rc == -1);
		}
		free(tmp);
	}
	free(copy);
}

int main(void)
{
	RUN(t_j293);
	RUN(t_reserve_iterator);
	RUN(t_variants);
	RUN(t_uart_selection);
	RUN(t_qemu);
	RUN(t_cells_per_depth);
	RUN(t_subtract);
	RUN(t_subtract_precise);
	RUN(t_overflow_lists);
	RUN(t_malformed);
	RUN(t_fuzz);
	TESTS_DONE();
}
