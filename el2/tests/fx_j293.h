/* SPDX-License-Identifier: MIT */
/* Host tests: the j293-shaped FDT fixture (the shape m1n1 produces for the minimal DTS of DESIGN 4.2, plus the measured
 * MPIDRs of re/m3a-ehw0-measurements.md), shared by test_fdt_m1.c (T6b) and test_pt.c (T6c). Needs fdt_build.h. The
 * tree carries a serial-number and a MAC address on purpose: nothing of them may ever reach a result. */
#ifndef WEL2_TEST_FX_J293_H
#define WEL2_TEST_FX_J293_H

#include <stdio.h>
#include "fdt_build.h"

#define FT_NOP 4u

struct opt {
	bool uart0_disabled;
	const char *stdout_path;        /* /chosen/stdout-path or NULL */
	const char *serial0;            /* /aliases/serial0 or NULL */
	int cpu_disabled;               /* index of a cpu with status = "disabled", -1 none */
	int cpu_nop;                    /* index of a cpu node whose tokens are all NOP, -1 none */
	unsigned extra_cpus;            /* additional cpu@100+n nodes */
	bool no_fb;
	bool big;                       /* T6c: the full 8 GB map with ten reserved children (BIG_RESV) */
};

/* T6c: reserved-memory children of the big tree (m1n1-style scattered carve-outs, one holds a cpu-release-addr) */
static const __attribute__((unused)) uint64_t BIG_RESV[10][2] = {
	{ 0x800000000ull, 0x4000 }, { 0x804000000ull, 0x100000 }, { 0x82f4c8000ull, 0x78000 }, { 0x850000000ull, 0x2c00000 },
	{ 0x8a0123000ull, 0x10000 }, { 0x900000000ull, 0x4000000 }, { 0x93ffc0000ull, 0x80000 }, { 0x9a0000000ull, 0x4000 },
	{ 0x9d0000000ull, 0x1000000 }, { 0x9df000000ull, 0x600000 },
};
static const __attribute__((unused)) uint64_t CPU_REG[8] = { 0x0, 0x1, 0x2, 0x3, 0x10100, 0x10101, 0x10102, 0x10103 };

static inline __attribute__((unused)) void u32cells(struct fdtb *b, const char *name, unsigned n, const uint32_t *v)
{
	uint8_t t[32];

	for (unsigned i = 0; i < n; i++)
		fb_put32(t + 4 * i, v[i]);
	fb_prop(b, name, t, 4 * n);
}

static inline __attribute__((unused)) void cpu_node(struct fdtb *b, uint64_t reg, bool disabled, uint64_t rel)
{
	char nm[24];

	snprintf(nm, sizeof(nm), "cpu@%llx", (unsigned long long)reg);
	fb_begin(b, nm);
	fb_str(b, "device_type", "cpu");
	fb_str(b, "compatible", reg < 4 ? "apple,icestorm" : "apple,firestorm");
	fb_reg(b, 2, 0, reg, 0);
	fb_str(b, "enable-method", "spin-table");
	fb_u64(b, "cpu-release-addr", rel);
	if (disabled)
		fb_str(b, "status", "disabled");
	fb_end(b);
}

static inline __attribute__((unused)) void *build_j293(struct fdtb *b, const struct opt *o)
{
	fb_init_rsv(b, 4);
	fb_rsv(b, 0x800000000ull, 0x4000);              /* head of RAM pair A */
	fb_rsv(b, 0x807ff0000ull, 0x10000);             /* tail of RAM pair A */
	fb_begin(b, "");
	fb_str(b, "compatible", "apple,j293");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
	fb_str(b, "serial-number", "WATARI-SECRET-SN");

	fb_begin(b, "aliases");
	if (o->serial0)
		fb_str(b, "serial0", o->serial0);
	fb_end(b);

	fb_begin(b, "chosen");
	fb_str(b, "bootargs", "wel2.step=1");
	if (o->stdout_path)
		fb_str(b, "stdout-path", o->stdout_path);
	fb_str(b, "local-mac-address", "SECRET-MAC");
	if (!o->no_fb) {
		fb_begin(b, "framebuffer@9e2570000");
		fb_str(b, "compatible", "simple-framebuffer");
		fb_reg(b, 2, 2, 0x9e2570000ull, 0x1000000);
		fb_u32(b, "width", 2560);
		fb_u32(b, "height", 1600);
		fb_u32(b, "stride", 10240);
		fb_str(b, "format", "x2r10g10b10");
		fb_end(b);
	}
	fb_end(b);

	fb_begin(b, "memory@800000000");
	fb_str(b, "device_type", "memory");
	if (o->big) {                                            /* the measured 8 GB map: RAM top 0x9df610000 */
		static const uint64_t one[1][2] = { { 0x800000000ull, 0x1df610000ull } };

		fb_regs(b, 2, 2, 1, one);
	} else {
		static const uint64_t pairs[3][2] = {
			{ 0x800000000ull, 0x8000000 },          /* A: 128 MB */
			{ 0x808000000ull, 0x1000000 },          /* B: 16 MB, wholly inside a reserved child */
			{ 0x810000000ull, 0x10000000 },         /* C: 256 MB, a reserved child cuts it in two */
		};
		fb_regs(b, 2, 2, 3, pairs);
	}
	fb_end(b);

	fb_begin(b, "reserved-memory");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
	fb_prop(b, "ranges", "", 0);
	if (o->big) {
		for (unsigned i = 0; i < sizeof(BIG_RESV) / sizeof(BIG_RESV[0]); i++) {
			char nm[32];

			snprintf(nm, sizeof(nm), "r%u@%llx", i, (unsigned long long)BIG_RESV[i][0]);
			fb_begin(b, nm);
			fb_reg(b, 2, 2, BIG_RESV[i][0], BIG_RESV[i][1]);
			fb_end(b);
		}
	} else {
		fb_begin(b, "tz@808000000");
		fb_reg(b, 2, 2, 0x808000000ull, 0x1000000);
		fb_prop(b, "no-map", "", 0);
		fb_end(b);
		fb_begin(b, "adt@812000000");
		fb_reg(b, 2, 2, 0x812000000ull, 0x100000);
		fb_end(b);
	}
	fb_begin(b, "dyn");                                      /* size only, no reg: allocated at run time, no range */
	fb_u32(b, "size", 0x1000);
	fb_end(b);
	fb_end(b);

	fb_begin(b, "cpus");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 0);
	for (int i = 0; i < 8; i++) {
		uint32_t span = b->st_len;

		cpu_node(b, CPU_REG[i], i == o->cpu_disabled, i == 5 ? (o->big ? 0x9a0000100ull : 0x800123450ull) : 0);
		if (i == o->cpu_nop)
			for (uint32_t w = span; w < b->st_len; w += 4)
				fb_put32(b->buf + b->st + w, FT_NOP);
	}
	for (unsigned i = 0; i < o->extra_cpus; i++)
		cpu_node(b, 0x20000 + i, false, 0);
	fb_end(b);

	fb_begin(b, "soc");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
	fb_prop(b, "ranges", "", 0);
	fb_begin(b, "interrupt-controller@23b100000");
	{
		static const uint64_t pairs[2][2] = { { 0x23b100000ull, 0xc000 }, { 0x23b108000ull, 0x1000 } };

		fb_strs(b, "compatible", "apple,t8103-aic\0apple,aic\0", 26);
		fb_regs(b, 2, 2, 2, pairs);
	}
	fb_prop(b, "interrupt-controller", "", 0);
	fb_begin(b, "affinities");                               /* a child: the AIC node must still be recognised */
	fb_end(b);
	fb_end(b);
	fb_begin(b, "serial@235200000");
	fb_str(b, "compatible", "apple,s5l-uart");
	fb_reg(b, 2, 2, 0x235200000ull, 0x4000);
	if (o->uart0_disabled)
		fb_str(b, "status", "disabled");
	fb_end(b);
	fb_begin(b, "serial@235208000");
	fb_str(b, "compatible", "apple,s5l-uart");
	fb_reg(b, 2, 2, 0x235208000ull, 0x4000);
	fb_end(b);
	fb_begin(b, "watchdog@23d2b0000");
	fb_strs(b, "compatible", "apple,t8103-wdt\0apple,wdt\0", 26);
	fb_reg(b, 2, 2, 0x23d2b0000ull, 0x4000);
	fb_end(b);
	fb_end(b);
	fb_end(b);
	return fb_finish(b);
}

static const __attribute__((unused)) struct opt O0 = { false, "serial0:115200", "/soc/serial@235200000", -1, -1, 0, false, false };


#endif
