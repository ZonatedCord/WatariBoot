/* SPDX-License-Identifier: MIT */
/* Host tests: granule-parametric translation tables and the EL2 maps (wel2_pt.h, wel2_maps.h, #58). The tables
 * are built into a host buffer and walked for chosen addresses; the register values are compared with the
 * constants the layer used before the builder existed (4 KB). */
#include "test.h"
#include "wel2_maps.h"
#include "wel2_aic.h"
#include "wel2_v2m.h"
#include <stdlib.h>
#include <string.h>
#include "fdt.h"
#include "fdt_m1.h"
#include "fdt_build.h"
#include "fx_j293.h"

#define BASE   0x40000000ULL
#define GICD_P 0x08000000ULL
#define GICR_P 0x080A0000ULL

static uint8_t pool_s1[WEL2_S1_POOL_BYTES] __attribute__((aligned(16384)));
static uint8_t pool_s2[WEL2_S2_POOL_BYTES] __attribute__((aligned(16384)));
static uint8_t pool_small[16384 * 4] __attribute__((aligned(16384)));

static uint64_t attrs_of(const struct wel2_pt *pt, uint64_t va)
{
	uint64_t pa = 0, at = 0;
	unsigned lv = 9;

	CHECK(wel2_pt_walk(pt, va, &pa, &at, &lv));
	return at;
}

static void t_geometry(void)
{
	struct wel2_pt pt;

	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_4K, 39), 0);
	CHECK_EQ(pt.start_level, 1); CHECK_EQ(pt.root_entries, 512);
	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_4K, 40), 0);
	CHECK_EQ(pt.start_level, 0); CHECK_EQ(pt.root_entries, 2);
	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 40), 0);
	CHECK_EQ(pt.start_level, 1); CHECK_EQ(pt.root_entries, 16);       /* 4 bits at level 1 */
	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(pt.start_level, 2); CHECK_EQ(pt.root_entries, 2048);     /* 64 GB in one table */
	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 47), 0);
	CHECK_EQ(pt.start_level, 1); CHECK_EQ(pt.root_entries, 2048);
	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 48), 0);    /* stage 1, DS = 0: level 0 */
	CHECK_EQ(pt.start_level, 0); CHECK_EQ(pt.root_entries, 2);
	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 49), -1);
	CHECK_EQ(wel2_pt_init(&pt, pool_small + 4096, sizeof(pool_small) - 4096, WEL2_GRAN_16K, 36), -1);   /* misaligned */
	CHECK_EQ(wel2_pt_init(&pt, pool_small + 4096, sizeof(pool_small) - 4096, WEL2_GRAN_4K, 39), 0);
	CHECK_EQ(wel2_pt_init(&pt, pool_small, 8192, WEL2_GRAN_16K, 36), -1);   /* smaller than one table */
	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), 13, 36), -1);   /* 8 KB is not a granule */
}

static void t_registers(void)
{
	struct wel2_pt pt;

	/* The values the layer wrote before the builder (4 KB): VTCR_EL2 40-bit/SL0 2, TCR_EL2 39-bit, both variants. */
	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_4K, 40);
	CHECK_EQ(wel2_vtcr(&pt, 2), (1ULL << 31) | (2ULL << 16) | (3ULL << 12) | (1ULL << 10) | (1ULL << 8) | (2ULL << 6) | 24ULL);
	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_4K, 39);
	CHECK_EQ(wel2_tcr_el2(&pt, false, 2),
		 25ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (2ULL << 16) | (1ULL << 23) | (1ULL << 31));
	CHECK_EQ(wel2_tcr_el2(&pt, true, 2),
		 25ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (25ULL << 16) | (1ULL << 23) | (2ULL << 30) | (2ULL << 32));
	/* 16 KB: TG0 = 0b10, SL0 = 0b10 (level 1) for the 40-bit IPA, T0SZ 24; stage 1 36-bit: T0SZ 28, TG1 = 0b01 */
	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 40);
	CHECK_EQ(wel2_vtcr(&pt, 2), (1ULL << 31) | (2ULL << 16) | (2ULL << 14) | (3ULL << 12) | (1ULL << 10) | (1ULL << 8) | (2ULL << 6) | 24ULL);
	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 36);
	CHECK_EQ(wel2_vtcr(&pt, 2) >> 6 & 3, 1);                         /* start level 2: SL0 = 0b01 */
	CHECK_EQ(wel2_tcr_el2(&pt, false, 2),
		 28ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (2ULL << 14) | (2ULL << 16) | (1ULL << 23) | (1ULL << 31));
	CHECK_EQ(wel2_tcr_el2(&pt, true, 1),
		 28ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (2ULL << 14) | (28ULL << 16) | (1ULL << 23) | (1ULL << 30) | (1ULL << 32));
}

static void t_blocks_and_pages(void)
{
	struct wel2_pt pt;
	uint64_t pa, at;
	unsigned lv;

	/* 16 KB: a 32 MB aligned range is one level-2 block; no level-1 block exists */
	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 36);
	CHECK_EQ(wel2_pt_map(&pt, 0x40000000ULL, 0x40000000ULL, 0x2000000ULL, 0x400), 0);
	CHECK(wel2_pt_walk(&pt, 0x40123456ULL, &pa, &at, &lv));
	CHECK_EQ(pa, 0x40123456ULL); CHECK_EQ(at, 0x400); CHECK_EQ(lv, 2);
	CHECK(!wel2_pt_walk(&pt, 0x42000000ULL, &pa, &at, &lv));
	CHECK(!wel2_pt_walk(&pt, 0x3fffffffULL, &pa, &at, &lv));
	CHECK_EQ(pt.used, 1);                                           /* the root only */
	/* 2 MB inside it unmapped: the block splits into 16 KB pages in one level-3 table */
	CHECK_EQ(wel2_pt_unmap(&pt, 0x40080000ULL, 0x180000ULL), 0);
	CHECK_EQ(pt.used, 2);
	CHECK(wel2_pt_walk(&pt, 0x4007c000ULL, &pa, &at, &lv));
	CHECK_EQ(lv, 3); CHECK_EQ(pa, 0x4007c000ULL); CHECK_EQ(at, 0x400);
	CHECK(!wel2_pt_walk(&pt, 0x40080000ULL, &pa, &at, &lv));
	CHECK(!wel2_pt_walk(&pt, 0x401fffffULL, &pa, &at, &lv));
	CHECK(wel2_pt_walk(&pt, 0x40200000ULL, &pa, &at, &lv));
	CHECK_EQ(lv, 3); CHECK_EQ(pa, 0x40200000ULL);
	/* a remap with a different physical address and attributes */
	CHECK_EQ(wel2_pt_map(&pt, 0x40080000ULL, 0x90000000ULL, 0x8000ULL, 0x480), 0);
	CHECK(wel2_pt_walk(&pt, 0x40085678ULL, &pa, &at, &lv));
	CHECK_EQ(pa, 0x90005678ULL); CHECK_EQ(at, 0x480); CHECK_EQ(lv, 3);
	/* a range the size of a block but not aligned in PA cannot be a block */
	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 36);
	CHECK_EQ(wel2_pt_map(&pt, 0x40000000ULL, 0x40004000ULL, 0x2000000ULL, 0x400), 0);
	CHECK(wel2_pt_walk(&pt, 0x40000000ULL, &pa, &at, &lv));
	CHECK_EQ(lv, 3); CHECK_EQ(pa, 0x40004000ULL);

	/* 4 KB: a 1 GB aligned range is one level-1 block, 2 MB ones at level 2 */
	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_4K, 39);
	CHECK_EQ(wel2_pt_map(&pt, 0x80000000ULL, 0x80000000ULL, 0x40000000ULL, 0x400), 0);
	CHECK(wel2_pt_walk(&pt, 0x8abcdef0ULL, &pa, &at, &lv));
	CHECK_EQ(lv, 1); CHECK_EQ(pa, 0x8abcdef0ULL);
	CHECK_EQ(wel2_pt_unmap(&pt, 0x80201000ULL, 0x1000ULL), 0);        /* one 4 KB page out of the GB */
	CHECK_EQ(pt.used, 3);                                           /* root + level 2 + level 3 */
	CHECK(!wel2_pt_walk(&pt, 0x80201000ULL, &pa, &at, &lv));
	CHECK(wel2_pt_walk(&pt, 0x80200000ULL, &pa, &at, &lv));
	CHECK_EQ(lv, 3);
	CHECK(wel2_pt_walk(&pt, 0x80400000ULL, &pa, &at, &lv));
	CHECK_EQ(lv, 2);                                                /* the other 2 MB blocks stay blocks */
}

static void t_bad_arguments_and_exhaustion(void)
{
	struct wel2_pt pt;

	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 36);
	CHECK_EQ(wel2_pt_map(&pt, 0x1000, 0x1000, 0x4000, 0), -1);          /* 4 KB aligned: not a 16 KB granule */
	CHECK_EQ(wel2_pt_map(&pt, 0x4000, 0x4000, 0x1000, 0), -1);
	CHECK_EQ(wel2_pt_map(&pt, 0x4000, 0x1000, 0x4000, 0), -1);
	CHECK_EQ(wel2_pt_map(&pt, 0x4000, 0x4000, 0, 0), -1);
	CHECK_EQ(wel2_pt_map(&pt, 1ULL << 36, 0, 0x4000, 0), -1);          /* outside the input address space */
	CHECK_EQ(wel2_pt_map(&pt, (1ULL << 36) - 0x4000, 0, 0x8000, 0), -1);
	/* pool of 4 tables: root + 3; scattered single pages need one level-3 table per 32 MB */
	wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 36);
	CHECK_EQ(wel2_pt_map(&pt, 0x04000000ULL, 0x04000000ULL, 0x4000, 0), 0);
	CHECK_EQ(wel2_pt_map(&pt, 0x08000000ULL, 0x08000000ULL, 0x4000, 0), 0);
	CHECK_EQ(wel2_pt_map(&pt, 0x0c000000ULL, 0x0c000000ULL, 0x4000, 0), 0);
	CHECK_EQ(pt.used, 4);
	CHECK_EQ(wel2_pt_map(&pt, 0x10000000ULL, 0x10000000ULL, 0x4000, 0), -1);   /* pool exhausted */
}

static void ram_4g(struct wel2_ram *ram)
{
	CHECK_EQ(wel2_ram_set(ram, BASE, 0x100000000ULL, BASE, 1ULL << 36), 0);
}

#define ATTR_NORMAL_S2 ((1ULL << 10) | (3ULL << 6) | (0xfULL << 2) | (3ULL << 8))
#define ATTR_DEV_S2    ((1ULL << 10) | (3ULL << 6) | (1ULL << 54))

static void check_s2(unsigned gshift)
{
	struct wel2_pt pt;
	struct wel2_ram ram;
	struct wel2_range holes[3] = {
		{ GICD_P, 0x10000 }, { GICR_P, 8 * 0x20000 }, { BASE + 0x80000, 0x200000 - 0x80000 },
	};
	uint64_t pa, at;
	unsigned lv;
	const unsigned blk_gb = gshift == WEL2_GRAN_4K ? 1 : 2;       /* level of a fully covered GB of RAM */

	ram_4g(&ram);
	CHECK_EQ(wel2_pt_init(&pt, pool_s2, sizeof(pool_s2), gshift, WEL2_S2_IPA_BITS), 0);
	CHECK_EQ(wel2_s2_build(&pt, &ram, holes, 3), 0);
	CHECK(pt.max_used * (1ULL << gshift) <= WEL2_S2_POOL_BYTES);
	printf("    s2 granule %u KB: %u tables, %u KB\n", 1u << (gshift - 10), pt.max_used, pt.max_used << (gshift - 10));

	/* RAM: identity, Normal, and the 2 MB layer carve-out of the first 2 MB unmapped */
	CHECK(wel2_pt_walk(&pt, BASE, &pa, &at, &lv));
	CHECK_EQ(pa, BASE); CHECK_EQ(at, ATTR_NORMAL_S2);
	CHECK(wel2_pt_walk(&pt, BASE + 0x7ffff, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, BASE + 0x80000, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, BASE + 0x1fffff, NULL, NULL, NULL));
	CHECK(wel2_pt_walk(&pt, BASE + 0x200000, &pa, &at, &lv));         /* the guest RAM base of the chain build */
	CHECK_EQ(pa, BASE + 0x200000); CHECK_EQ(at, ATTR_NORMAL_S2);
	CHECK(wel2_pt_walk(&pt, 0x80000000ULL, &pa, &at, &lv));
	CHECK_EQ(lv, blk_gb);                                           /* 16 KB: a 32 MB block; 4 KB: a 1 GB block */
	CHECK(wel2_pt_walk(&pt, 0x13ffffff8ULL, &pa, &at, &lv));
	CHECK_EQ(pa, 0x13ffffff8ULL);
	CHECK(attrs_of(&pt, 0x140000000ULL) == ATTR_DEV_S2);   /* just past the RAM: device (4 GB machine) */
	/* devices: UART, RTC, ITS/v2m area, PCIe ECAM/MMIO windows, the high ECAM and high MMIO of virt */
	CHECK_EQ(attrs_of(&pt, 0x09000000ULL), ATTR_DEV_S2);
	CHECK_EQ(attrs_of(&pt, 0x08080000ULL), ATTR_DEV_S2);             /* GIC ITS frame */
	CHECK_EQ(attrs_of(&pt, 0x08020000ULL), ATTR_DEV_S2);             /* GICv2m frame */
	CHECK_EQ(attrs_of(&pt, 0x3eff0000ULL), ATTR_DEV_S2);
	CHECK_EQ(attrs_of(&pt, 0x4010000000ULL), ATTR_DEV_S2);
	CHECK_EQ(attrs_of(&pt, 0x8000001000ULL), ATTR_DEV_S2);
	CHECK_EQ(attrs_of(&pt, (1ULL << 40) - 0x1000), ATTR_DEV_S2);
	/* GICD (64 KB) and the eight redistributor frames (1 MB) trap; their neighbours do not */
	CHECK(wel2_pt_walk(&pt, GICD_P - 1, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, GICD_P, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, GICD_P + 0xffff, NULL, NULL, NULL));
	CHECK(wel2_pt_walk(&pt, GICD_P + 0x10000, NULL, NULL, NULL));
	CHECK(wel2_pt_walk(&pt, GICR_P - 1, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, GICR_P, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, GICR_P + 8 * 0x20000 - 1, NULL, NULL, NULL));
	CHECK(wel2_pt_walk(&pt, GICR_P + 8 * 0x20000, NULL, NULL, NULL));
	/* every 4 KB frame of the holes is unmapped: the vGIC sees all of its faults, whatever the granule */
	for (uint64_t a = GICD_P; a < GICD_P + 0x10000; a += 0x1000)
		CHECK(!wel2_pt_walk(&pt, a, NULL, NULL, NULL));
	for (uint64_t a = GICR_P; a < GICR_P + 8 * 0x20000; a += 0x1000)
		CHECK(!wel2_pt_walk(&pt, a, NULL, NULL, NULL));
}

static void t_s2_map(void)
{
	check_s2(WEL2_GRAN_4K);
	check_s2(WEL2_GRAN_16K);
}

/* The t8103 AIC (wel2_aic.h): reg[0] 0x23b100000 + 0xc000, reg[1] 0x23b108000 + 0x1000 inside it.
 * The guest must not see either range (stage-2 hole); EL2 stage 1 maps both as Device for the layer.
 * M1-like layout: 8 GB of RAM at 0x800000000, 16 KB granule is the real case, 4 KB checked too. */
static void check_aic_maps(unsigned gshift)
{
	const uint64_t rb = 0x800000000ULL;
	struct wel2_range holes[4];
	struct wel2_pt pt;
	struct wel2_ram ram;
	struct wel2_s1_layout l = { rb + 0x80000, rb + 0x84000, rb + 0x88000 };
	unsigned n = 0, m = 0;

	CHECK_EQ(wel2_s2_add_hole(holes, &n, 4, AIC_T8103_BASE, AIC_T8103_SIZE0, gshift), 0);
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 4, AIC_T8103_BASE1, AIC_T8103_SIZE1, gshift), 0);
	CHECK_EQ(n, 1);                                                    /* reg[1] is inside reg[0]: one hole */
	CHECK_EQ(holes[0].base, AIC_T8103_BASE); CHECK_EQ(holes[0].size, AIC_T8103_SIZE0);
	/* reg[1] alone is rounded outwards to a page (the sub-range is not expressible otherwise) */
	CHECK_EQ(wel2_s2_add_hole(holes + 1, &m, 3, AIC_T8103_BASE1, AIC_T8103_SIZE1, gshift), 0);
	CHECK_EQ(m, 1);
	CHECK((holes[1].base & ((1ULL << gshift) - 1)) == 0 && holes[1].base <= AIC_T8103_BASE1);
	CHECK(holes[1].base + holes[1].size >= AIC_T8103_BASE1 + AIC_T8103_SIZE1);

	CHECK_EQ(wel2_ram_set(&ram, rb, 0x200000000ULL, rb, 1ULL << 36), 0);
	/* stage 2 */
	CHECK_EQ(wel2_pt_init(&pt, pool_s2, sizeof(pool_s2), gshift, WEL2_S2_IPA_BITS), 0);
	CHECK_EQ(wel2_s2_build(&pt, &ram, holes, n), 0);
	for (uint64_t a = AIC_T8103_BASE; a < AIC_T8103_BASE + AIC_T8103_SIZE0; a += 0x1000)
		CHECK(!wel2_pt_walk(&pt, a, NULL, NULL, NULL));
	for (uint64_t a = AIC_T8103_BASE1; a < AIC_T8103_BASE1 + AIC_T8103_SIZE1; a += 0x1000)
		CHECK(!wel2_pt_walk(&pt, a, NULL, NULL, NULL));
	CHECK(wel2_pt_walk(&pt, AIC_T8103_BASE - 1, NULL, NULL, NULL));                    /* neighbours stay mapped */
	CHECK(wel2_pt_walk(&pt, AIC_T8103_BASE + AIC_T8103_SIZE0, NULL, NULL, NULL));
	CHECK_EQ(attrs_of(&pt, AIC_T8103_BASE - 0x1000), ATTR_DEV_S2);
	/* stage 1 of the layer: both ranges present, Device, whichever regime */
	for (int e2h = 0; e2h <= 1; e2h++) {
		uint64_t ap1 = e2h ? 0 : (1ULL << 6);
		uint64_t xn = e2h ? ((1ULL << 53) | (1ULL << 54)) : (1ULL << 54);
		uint64_t pa = 0;

		CHECK_EQ(wel2_ram_set(&ram, rb, 0x200000000ULL, rb, 1ULL << wel2_s1_va_bits(gshift)), 0);
		CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), gshift, wel2_s1_va_bits(gshift)), 0);
		CHECK_EQ(wel2_s1_build(&pt, &ram, &l, e2h != 0), 0);
		for (uint64_t a = AIC_T8103_BASE; a < AIC_T8103_BASE + AIC_T8103_SIZE0; a += 0x1000) {
			CHECK(wel2_pt_walk(&pt, a, &pa, NULL, NULL));
			CHECK_EQ(pa, a);
			CHECK_EQ(attrs_of(&pt, a), (1ULL << 10) | ap1 | xn);             /* AttrIndx 0 = Device-nGnRnE */
		}
		CHECK_EQ(attrs_of(&pt, AIC_T8103_BASE1), (1ULL << 10) | ap1 | xn);
	}
}

static void t_s2_aic_hole(void)
{
	check_aic_maps(WEL2_GRAN_16K);
	check_aic_maps(WEL2_GRAN_4K);
}

/* T5: the Apple S5L console UART (0x2_3520_0000, 16 KB window on the t8103) is the layer's: it sits below the M1
 * RAM, so the EL2 stage 1 already maps it Device (nothing added); the guest stage 2 gets a hole for it, next to the
 * five other windows, and the table pool still suffices with either granule. A UART above the RAM is added by
 * wel2_s1_map_device. */
#define UART_S5L 0x235200000ULL

static void check_uart_maps(unsigned gshift)
{
	const uint64_t rb = 0x800000000ULL, re = rb + 0x200000000ULL;
	struct wel2_range holes[8] = { { GICD_P, 0x10000 }, { GICR_P, 8 * 0x20000 } };
	unsigned n = 2;
	struct v2m m;
	struct wel2_pt pt;
	struct wel2_ram ram;
	struct wel2_s1_layout lay = { 0, 0, 0 };

	CHECK_EQ(wel2_ram_set(&ram, rb, re - rb, rb, 1ULL << 36), 0);
	/* stage 1: below the RAM -> already Device; the helper adds nothing and does not fail */
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), gshift, wel2_s1_va_bits(gshift)), 0);
	CHECK_EQ(wel2_s1_build(&pt, &ram, &lay, true), 0);
	unsigned used = pt.used;
	CHECK_EQ(wel2_s1_map_device(&pt, &ram, UART_S5L, 0x4000, true), 0);
	CHECK_EQ(pt.used, used);
	CHECK(wel2_pt_walk(&pt, UART_S5L, NULL, NULL, NULL));
	CHECK(wel2_pt_walk(&pt, UART_S5L + 0x3000, NULL, NULL, NULL));
	/* a window overlapping the RAM is refused, a window above it is mapped Device */
	CHECK_EQ(wel2_s1_map_device(&pt, &ram, rb + 0x1000, 0x4000, true), -1);
	CHECK_EQ(wel2_s1_map_device(&pt, &ram, 1ULL << 40, 0x4000, true), -1);   /* beyond the 36/39-bit input range */
	CHECK_EQ(wel2_s1_map_device(&pt, &ram, 0, 0, true), 0);
	CHECK_EQ(wel2_s1_map_device(&pt, &ram, re + 0x10000000ULL, 0x4000, true), 0);
	CHECK_EQ(attrs_of(&pt, re + 0x10000000ULL), attrs_of(&pt, UART_S5L));        /* the same Device attributes */

	/* stage 2: all six windows of the M1 layer (GICD, GICR, MSI page, AIC, console UART, carve-out) */
	CHECK_EQ(v2m_init(&m, V2M_T8103_BASE, 928, 32, NULL), 0);
	CHECK_EQ(v2m_add_hole(&m, holes, &n, 8, gshift), 0);
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 8, AIC_T8103_BASE, AIC_T8103_SIZE0, gshift), 0);
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 8, UART_S5L, 0x4000, gshift), 0);
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 8, rb + 0x10000000ULL, 0x2000000ULL, gshift), 0);
	CHECK_EQ(n, 6);
	CHECK_EQ(wel2_pt_init(&pt, pool_s2, sizeof(pool_s2), gshift, WEL2_S2_IPA_BITS), 0);
	CHECK_EQ(wel2_s2_build(&pt, &ram, holes, n), 0);
	printf("    s2 six holes, granule %u KB: %u tables of %u\n", 1u << (gshift - 10), pt.used, WEL2_S2_POOL_BYTES >> gshift);
	for (uint64_t a = UART_S5L; a < UART_S5L + 0x4000; a += 0x1000)
		CHECK(!wel2_pt_walk(&pt, a, NULL, NULL, NULL));
	CHECK_EQ(attrs_of(&pt, UART_S5L - 0x4000), ATTR_DEV_S2);          /* the neighbours stay device */
	CHECK_EQ(attrs_of(&pt, UART_S5L + 0x4000), ATTR_DEV_S2);
	CHECK(!wel2_pt_walk(&pt, AIC_T8103_BASE, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, 0xFFFFC000ULL, NULL, NULL, NULL));
	CHECK_EQ(attrs_of(&pt, rb), ATTR_NORMAL_S2);
	/* a seventh and an eighth window still fit the list of eight */
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 8, 0x23d280000ULL, 0x4000, gshift), 0);
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 8, 0x2a0000000ULL, 0x4000, gshift), 0);
	CHECK_EQ(n, 8);
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 8, 0x2b0000000ULL, 0x4000, gshift), -1);
}

/* T6a: the layer is a position-independent image, so its carve-out is wherever the loader put it. The M1 case: RAM
 * at 0x8_0000_0000, the layer loaded high inside it, the carve-out the 32 MB aligned block holding the layer (design
 * D 2.3). Stage 1 maps the RAM, with the layer's text read-only executable and the rodata read-only at that address;
 * stage 2 unmaps [layer, carve end) (main.c: hide_lo = layer rounded down to the granule, size to the carve end),
 * the FDT hand-off area below the layer stays mapped, and the 32 MB block next to it too. */
static void check_high_carve(unsigned gshift, uint64_t layer)
{
	const uint64_t rb = 0x800000000ULL, re = rb + 0x200000000ULL, csize = 0x2000000ULL;
	const uint64_t cbase = layer & ~(csize - 1), cend = cbase + csize, g = 1ULL << gshift;
	const uint64_t text_lo = layer, text_hi = layer + 0x14000, ro_hi = text_hi + 0x8000;   /* 16 KB multiples */
	struct wel2_range holes[8] = { { GICD_P, 0x10000 }, { GICR_P, 8 * 0x20000 } };
	unsigned n = 2;
	struct wel2_pt pt;
	struct wel2_ram ram;
	struct wel2_s1_layout lay = { text_lo, text_hi, ro_hi };
	uint64_t pa, at;
	unsigned lv;

	CHECK_EQ(cbase % csize, 0);
	CHECK(cbase >= rb && cend <= re);
	CHECK_EQ(wel2_ram_set(&ram, rb, re - rb, rb, 1ULL << 36), 0);
	/* stage 1 */
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), gshift, wel2_s1_va_bits(gshift)), 0);
	CHECK_EQ(wel2_s1_build(&pt, &ram, &lay, true), 0);
	CHECK(wel2_pt_walk(&pt, layer, &pa, &at, &lv));
	CHECK_EQ(pa, layer);
	CHECK(wel2_pt_walk(&pt, text_hi - 8, &pa, &at, NULL));
	CHECK(attrs_of(&pt, text_lo) != attrs_of(&pt, text_hi));             /* code RO+X, rodata RO+XN */
	CHECK(attrs_of(&pt, text_hi) != attrs_of(&pt, ro_hi));               /* rodata RO, data RW */
	CHECK_EQ(attrs_of(&pt, ro_hi), attrs_of(&pt, rb));                   /* RW data = the plain RAM attributes */
	CHECK_EQ(attrs_of(&pt, cbase), attrs_of(&pt, rb));                   /* the FDT hand-off area below the layer */
	/* stage 2 */
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 8, layer & ~(g - 1), cend - (layer & ~(g - 1)), gshift), 0);
	CHECK_EQ(n, 3);
	CHECK_EQ(wel2_pt_init(&pt, pool_s2, sizeof(pool_s2), gshift, WEL2_S2_IPA_BITS), 0);
	CHECK_EQ(wel2_s2_build(&pt, &ram, holes, n), 0);
	for (uint64_t a = layer & ~(g - 1); a < cend; a += 0x1000)
		CHECK(!wel2_pt_walk(&pt, a, NULL, NULL, NULL));
	CHECK(wel2_pt_walk(&pt, (layer & ~(g - 1)) - 1, NULL, NULL, NULL));      /* the byte before: guest RAM */
	CHECK(wel2_pt_walk(&pt, cbase, &pa, &at, &lv));                           /* FDT hand-off buffer */
	CHECK_EQ(pa, cbase); CHECK_EQ(at, ATTR_NORMAL_S2);
	CHECK(wel2_pt_walk(&pt, cend, &pa, &at, &lv));                            /* the block after the carve-out */
	CHECK_EQ(pa, cend); CHECK_EQ(at, ATTR_NORMAL_S2);
	CHECK(wel2_pt_walk(&pt, cend + csize, &pa, &at, &lv));
	CHECK_EQ(at, ATTR_NORMAL_S2);
	CHECK(wel2_pt_walk(&pt, rb, &pa, &at, &lv));                              /* RAM base: untouched */
	CHECK_EQ(at, ATTR_NORMAL_S2);
	CHECK(pt.max_used * (1ULL << gshift) <= WEL2_S2_POOL_BYTES);
}

static void t_high_carve(void)
{
	/* aligned 32 MB block inside the RAM, the layer at 0x80000 into it (QEMU text_offset) and 1 MB into it */
	check_high_carve(WEL2_GRAN_16K, 0x810000000ULL + 0x80000);
	check_high_carve(WEL2_GRAN_4K, 0x810000000ULL + 0x80000);
	check_high_carve(WEL2_GRAN_16K, 0x81a000000ULL + 0x100000);
	check_high_carve(WEL2_GRAN_4K, 0x81a000000ULL + 0x100000);
}

static void t_s5l_uart_maps(void)
{
	check_uart_maps(WEL2_GRAN_16K);
	check_uart_maps(WEL2_GRAN_4K);
}

/* #74: the GICv2m frame (4 KB at 0xFFFFF000) is trapped as the whole 16 KB page 0xFFFFC000-0xFFFFFFFF with
 * either granule. The RAM is the M1 layout (above 4 GB), so the page is plain device space around the hole. */
static void check_v2m_hole(unsigned gshift)
{
	const uint64_t rb = 0x800000000ULL, page = 0xFFFFC000ULL;
	struct wel2_range holes[6] = { { GICD_P, 0x10000 }, { GICR_P, 8 * 0x20000 } };
	unsigned n = 2;
	struct vgic *none = NULL;
	struct v2m m;
	struct wel2_pt pt;
	struct wel2_ram ram;

	CHECK_EQ(v2m_init(&m, V2M_T8103_BASE, 928, 32, none), 0);
	CHECK_EQ(v2m_add_hole(&m, holes, &n, 6, gshift), 0);
	CHECK_EQ(n, 3);
	CHECK_EQ(holes[2].base, page);
	CHECK_EQ(holes[2].size, 0x4000);
	CHECK(v2m_page_free(&m, rb, rb + 0x200000000ULL));
	CHECK_EQ(wel2_ram_set(&ram, rb, rb + 0x200000000ULL, rb, 1ULL << 36), 0);
	CHECK_EQ(wel2_pt_init(&pt, pool_s2, sizeof(pool_s2), gshift, WEL2_S2_IPA_BITS), 0);
	CHECK_EQ(wel2_s2_build(&pt, &ram, holes, n), 0);
	/* every 4 KB of the page is unmapped: the layer sees all the faults, whatever the granule */
	for (uint64_t a = page; a < page + 0x4000; a += 0x1000)
		CHECK(!wel2_pt_walk(&pt, a, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, 0xFFFFFFFFULL, NULL, NULL, NULL));
	/* neighbours are device blocks/pages: below the page and the 4 GB boundary above it */
	CHECK_EQ(attrs_of(&pt, page - 0x1000), ATTR_DEV_S2);
	CHECK_EQ(attrs_of(&pt, page - 0x4000), ATTR_DEV_S2);
	CHECK_EQ(attrs_of(&pt, 0x100000000ULL), ATTR_DEV_S2);
	CHECK_EQ(attrs_of(&pt, 0x100000000ULL + 0x3000), ATTR_DEV_S2);
	CHECK(wel2_pt_walk(&pt, 0xFFFF8000ULL, NULL, NULL, NULL));
	/* the other holes are still there, and RAM is intact */
	CHECK(!wel2_pt_walk(&pt, GICD_P, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&pt, GICR_P, NULL, NULL, NULL));
	CHECK_EQ(attrs_of(&pt, rb), ATTR_NORMAL_S2);
	/* idempotent: adding the same page again, or only the frame, changes nothing */
	CHECK_EQ(v2m_add_hole(&m, holes, &n, 6, gshift), 0);
	CHECK_EQ(n, 3);
	CHECK_EQ(wel2_s2_add_hole(holes, &n, 6, V2M_T8103_BASE, V2M_FRAME_SIZE, gshift), 0);
	CHECK_EQ(n, 3);
}

static void t_s2_v2m_hole(void)
{
	check_v2m_hole(WEL2_GRAN_16K);
	check_v2m_hole(WEL2_GRAN_4K);
}

static void t_s2_add_hole_rules(void)
{
	struct wel2_range h[4];
	unsigned n = 0;

	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x1000, 0, WEL2_GRAN_4K), 0);            /* empty: nothing added */
	CHECK_EQ(n, 0);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, ~0ULL - 0xff, 0x1000, WEL2_GRAN_4K), -1);   /* wraps */
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0xffffffffffffe000ULL, 0x2000, WEL2_GRAN_4K), -1);   /* ends at 2^64 */
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0xffffffffffffe001ULL, 0x1000, WEL2_GRAN_16K), -1);  /* rounding wraps */
	CHECK_EQ(n, 0);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x23b108000ULL, 0x1000, WEL2_GRAN_16K), 0);   /* 4 KB range, 16 KB granule */
	CHECK_EQ(n, 1); CHECK_EQ(h[0].base, 0x23b108000ULL); CHECK_EQ(h[0].size, 0x4000);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x23b10a000ULL, 0x1000, WEL2_GRAN_16K), 0);   /* same page: dropped */
	CHECK_EQ(n, 1);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x23b10c000ULL, 0x4000, WEL2_GRAN_16K), 0);   /* adjacent: its own hole */
	CHECK_EQ(n, 2);

	/* superset first, subset second: the subset is dropped */
	n = 0;
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x10000, 0x10000, WEL2_GRAN_4K), 0);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x12000, 0x2000, WEL2_GRAN_4K), 0);
	CHECK_EQ(n, 1); CHECK_EQ(h[0].base, 0x10000); CHECK_EQ(h[0].size, 0x10000);
	/* subset first, superset second: the old hole is removed, not duplicated */
	n = 0;
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x12000, 0x2000, WEL2_GRAN_4K), 0);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x30000, 0x1000, WEL2_GRAN_4K), 0);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x10000, 0x10000, WEL2_GRAN_4K), 0);
	CHECK_EQ(n, 2);
	CHECK_EQ(h[0].base, 0x30000); CHECK_EQ(h[1].base, 0x10000); CHECK_EQ(h[1].size, 0x10000);
	/* partial overlap: kept as two holes (unmapping the overlap twice is harmless) */
	n = 0;
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x10000, 0x8000, WEL2_GRAN_4K), 0);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0x14000, 0x8000, WEL2_GRAN_4K), 0);
	CHECK_EQ(n, 2);
	/* highest valid page */
	n = 0;
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0xffffffffffffc000ULL, 0x4000, WEL2_GRAN_16K), -1);   /* ends at 2^64 */
	CHECK_EQ(wel2_s2_add_hole(h, &n, 4, 0xffffffffffff8000ULL, 0x4000, WEL2_GRAN_16K), 0);
	CHECK_EQ(n, 1);
	/* capacity: full array is an error and leaves the list untouched */
	n = 0;
	CHECK_EQ(wel2_s2_add_hole(h, &n, 2, 0x10000, 0x1000, WEL2_GRAN_4K), 0);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 2, 0x20000, 0x1000, WEL2_GRAN_4K), 0);
	CHECK_EQ(wel2_s2_add_hole(h, &n, 2, 0x30000, 0x1000, WEL2_GRAN_4K), -1);
	CHECK_EQ(n, 2); CHECK_EQ(h[0].base, 0x10000); CHECK_EQ(h[1].base, 0x20000);
	/* a wider range that absorbs both fits even in a full list */
	CHECK_EQ(wel2_s2_add_hole(h, &n, 2, 0x0, 0x40000, WEL2_GRAN_4K), 0);
	CHECK_EQ(n, 1); CHECK_EQ(h[0].size, 0x40000);
}

static void t_s2_odd_ram_end(void)
{
	/* RAM that ends inside a 32 MB block: 16 KB pages (the RAM helper rounds to 2 MB) / 2 MB blocks */
	struct wel2_pt pt;
	struct wel2_ram ram;
	uint64_t pa, at;
	unsigned lv;

	CHECK_EQ(wel2_ram_set(&ram, BASE, 0x20000000ULL + 0x600000ULL, BASE, 1ULL << 36), 0);
	CHECK_EQ(wel2_pt_init(&pt, pool_s2, sizeof(pool_s2), WEL2_GRAN_16K, WEL2_S2_IPA_BITS), 0);
	CHECK_EQ(wel2_s2_build(&pt, &ram, NULL, 0), 0);
	CHECK(wel2_pt_walk(&pt, ram.end - 0x4000, &pa, &at, &lv));
	CHECK_EQ(at, ATTR_NORMAL_S2); CHECK_EQ(lv, 3);
	CHECK_EQ(attrs_of(&pt, ram.end), ATTR_DEV_S2);
}

static void t_s1_map(void)
{
	for (int e2h = 0; e2h <= 1; e2h++) {
		for (unsigned g = WEL2_GRAN_4K; g <= WEL2_GRAN_16K; g += 2) {
			struct wel2_pt pt;
			struct wel2_ram ram;
			struct wel2_s1_layout l = { BASE + 0x80000, BASE + 0x84000, BASE + 0x88000 };
			uint64_t ap1 = e2h ? 0 : (1ULL << 6);
			uint64_t xn = e2h ? ((1ULL << 53) | (1ULL << 54)) : (1ULL << 54);
			uint64_t nrm = (1ULL << 10) | (3ULL << 8) | (1ULL << 2) | ap1;
			uint64_t pa, at;
			unsigned lv;

			CHECK_EQ(wel2_ram_set(&ram, BASE, 0x100000000ULL, BASE, 1ULL << wel2_s1_va_bits(g)), 0);
			CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), g, wel2_s1_va_bits(g)), 0);
			CHECK_EQ(wel2_s1_build(&pt, &ram, &l, e2h != 0), 0);
			printf("    s1 granule %u KB e2h %d: %u tables\n", 1u << (g - 10), e2h, pt.max_used);
			CHECK(pt.max_used * (1ULL << g) <= WEL2_S1_POOL_BYTES);
			CHECK(wel2_pt_walk(&pt, 0x09000000ULL, &pa, &at, &lv));    /* UART: device, XN */
			CHECK_EQ(pa, 0x09000000ULL); CHECK_EQ(at, (1ULL << 10) | ap1 | xn);
			CHECK_EQ(attrs_of(&pt, BASE), nrm | xn);                    /* FDT buffer: RW XN */
			uint64_t uxn = e2h ? (1ULL << 54) : 0;                          /* EL2&0: UXN on code (EL0 never runs it) */
			CHECK_EQ(attrs_of(&pt, l.text_lo), nrm | (1ULL << 7) | uxn);      /* code: RO, executable at EL2 */
			CHECK_EQ(attrs_of(&pt, l.text_hi - 1), nrm | (1ULL << 7) | uxn);
			CHECK_EQ(attrs_of(&pt, l.text_hi), nrm | (1ULL << 7) | xn); /* rodata: RO XN */
			CHECK_EQ(attrs_of(&pt, l.ro_hi), nrm | xn);                 /* data, bss, stacks */
			CHECK_EQ(attrs_of(&pt, 0x13fffffffULL), nrm | xn);
			CHECK(!wel2_pt_walk(&pt, 0x140000000ULL, NULL, NULL, NULL));  /* past the RAM: faults */
			CHECK(!wel2_pt_walk(&pt, 0x200000000ULL, NULL, NULL, NULL));
		}
	}
}

/* ---- T6c: the M1 maps from the j293 fixture (PLAN S8) ------------------------------------------------------------
 * The inputs are derived from the FDT exactly as stage1_build does it (usable[] / aic / uart / wdt / cpu-release-addr /
 * framebuffer), the maps are built into the real table pools in both granules and with both E2H settings, and every
 * 1 MB step of the first 40 GB plus every region edge is compared with an independent model of what must be mapped. */

struct m1_in {
	struct m1_platform p;
	struct wel2_range ram[M1_MAX_USABLE];
	unsigned nram;
	struct wel2_range dev[M1_MAX_AIC + 2];
	unsigned ndev;
	struct wel2_range rel[M1_MAX_CPUS];
	unsigned nrel;
	struct wel2_range guest[WEL2_MAX_GUEST_RANGES];
	unsigned nguest;
	struct wel2_range carve, fdt, fb;
	struct wel2_s1_layout layout;
};

static void m1_prepare(const struct opt *o, struct m1_in *in, uint64_t carve_base, uint64_t fdt_base)
{
	struct fdtb *b = malloc(sizeof(*b));
	void *fdt = build_j293(b, o);

	CHECK_EQ(fdt_m1_parse(fdt, &in->p), 0);
	CHECK(in->p.is_m1);
	in->nram = in->p.nusable;
	for (unsigned i = 0; i < in->nram; i++)
		in->ram[i] = (struct wel2_range){ in->p.usable[i].base, in->p.usable[i].size };
	in->carve = (struct wel2_range){ carve_base, WEL2_M1_S2_ALIGN };
	CHECK_EQ(wel2_ranges_inward(in->ram, in->nram, WEL2_M1_S2_ALIGN, carve_base, WEL2_M1_S2_ALIGN, in->guest,
				    WEL2_MAX_GUEST_RANGES, &in->nguest), 0);
	in->ndev = 0;
	for (unsigned i = 0; i < in->p.naic; i++)
		in->dev[in->ndev++] = (struct wel2_range){ in->p.aic[i].base, in->p.aic[i].size };
	in->dev[in->ndev++] = (struct wel2_range){ in->p.uart.base, in->p.uart.size };
	in->dev[in->ndev++] = (struct wel2_range){ in->p.wdt.base, in->p.wdt.size };
	in->nrel = 0;
	for (unsigned i = 0; i < in->p.ncpu; i++)
		if (in->p.cpu[i].has_release)
			in->rel[in->nrel++] = (struct wel2_range){ in->p.cpu[i].release_addr, 8 };
	in->fdt = (struct wel2_range){ fdt_base, fdt_totalsize(fdt) };
	in->fb = (struct wel2_range){ in->p.fb.base, wel2_fb_bytes(&in->p.fb) };
	in->layout = (struct wel2_s1_layout){ carve_base + 0x4000, carve_base + 0x20000, carve_base + 0x28000 };
	free(b);
}

static bool in_pages(uint64_t a, uint64_t base, uint64_t size, uint64_t g)
{
	return size && a >= (base & ~(g - 1)) && a < ((base + size + g - 1) & ~(g - 1));
}

/* 0 unmapped, 1 Normal WB, 2 Device, 3 Normal NC */
static int s1_model(const struct m1_in *in, uint64_t a, uint64_t g)
{
	const uint64_t A = WEL2_M1_S1_ALIGN;

	for (unsigned i = 0; i < in->nram; i++) {
		uint64_t lo = (in->ram[i].base + A - 1) & ~(A - 1), hi = (in->ram[i].base + in->ram[i].size) & ~(A - 1);

		if (lo < hi && a >= lo && a < hi)
			return 1;
	}
	if (a >= in->carve.base && a < in->carve.base + in->carve.size)
		return 1;
	if (in_pages(a, in->fdt.base, in->fdt.size, g))
		return 1;
	for (unsigned i = 0; i < in->nrel; i++)
		if (in_pages(a, in->rel[i].base, in->rel[i].size, g))
			return 1;
	for (unsigned i = 0; i < in->ndev; i++)
		if (in_pages(a, in->dev[i].base, in->dev[i].size, g))
			return 2;
	if (in_pages(a, in->fb.base, in->fb.size, g))
		return 3;
	return 0;
}

static bool s2_model(const struct m1_in *in, uint64_t a)
{
	const uint64_t B = WEL2_M1_S2_ALIGN, blk = a & ~(B - 1);

	if (blk < in->carve.base + in->carve.size && blk + B > in->carve.base)
		return false;
	for (unsigned i = 0; i < in->nram; i++)
		if (blk >= in->ram[i].base && blk + B <= in->ram[i].base + in->ram[i].size)
			return true;
	return false;
}

/* the M1 guest IPA size (plat_m1.s2_ipa_bits, plat.h) */
#define plat_m1_ipa 36u

static void m1_check_maps(const struct opt *o, unsigned gshift, bool e2h, uint64_t carve_base, uint64_t fdt_base,
			  const char *tag)
{
	static struct m1_in in;
	struct wel2_pt s1, s2;
	uint64_t g = 1ULL << gshift, at;
	unsigned bad1 = 0, bad2 = 0;

	memset(&in, 0, sizeof(in));
	m1_prepare(o, &in, carve_base, fdt_base);
	CHECK_EQ(wel2_pt_init(&s1, pool_s1, sizeof(pool_s1), gshift, wel2_s1_va_bits(gshift)), 0);
	CHECK_EQ(wel2_s1_build_m1(&s1, &(struct wel2_s1_m1){ in.ram, in.nram, in.carve, in.fdt, in.dev, in.ndev,
							     in.rel, in.nrel, in.layout }, e2h), 0);
	CHECK_EQ(wel2_s1_map_fb_free(&s1, in.fb.base, in.fb.size, e2h), 0);
	CHECK_EQ(wel2_pt_init(&s2, pool_s2, sizeof(pool_s2), gshift, plat_m1_ipa), 0);
	CHECK_EQ(wel2_s2_build_ram(&s2, in.guest, in.nguest), 0);
	printf("    %s gran %uk e2h %d: stage 1 uses %u of %u tables, stage 2 uses %u of %u\n", tag, 1u << (gshift - 10),
	       e2h, s1.used, s1.ntables, s2.used, s2.ntables);

	/* VTCR_EL2 of the 36-bit guest space (Arm ARM D24.2.223, Tables D8-27 and D8-31): T0SZ 28; 16 KB: start level 2 =
	 * SL0 0b01, TG0 0b10; 4 KB: start level 1 = SL0 0b01, TG0 0b00; PS 1 (36-bit PARange) */
	{
		uint64_t v = wel2_vtcr(&s2, 1);

		CHECK_EQ(v & 0x3f, 28);
		CHECK_EQ((v >> 6) & 3, 1);
		CHECK_EQ((v >> 14) & 3, gshift == WEL2_GRAN_16K ? 2 : 0);
		CHECK_EQ((v >> 16) & 7, 1);
		CHECK_EQ(s2.start_level, gshift == WEL2_GRAN_16K ? 2 : 1);
		CHECK_EQ(s2.root_entries, gshift == WEL2_GRAN_16K ? 2048 : 64);
	}
	/* the FDT: read-only where it had to be mapped, untouched where it lies in the RAM map (never downgraded) */
	CHECK(wel2_pt_walk(&s1, in.fdt.base, NULL, &at, NULL));
	if (in.fdt.base == 0x802000100ULL)
		CHECK(!(at & (1ULL << 7)));
	else
		CHECK(at & (1ULL << 7));
	/* every 1 MB of the first 40 GB, then every region edge +-1 granule, against the model */
	uint64_t edges[512];
	unsigned ne = 0;
#define EDGE(x) do { if (ne < 512) edges[ne++] = (x); } while (0)
	for (unsigned i = 0; i < in.nram; i++) { EDGE(in.ram[i].base); EDGE(in.ram[i].base + in.ram[i].size); }
	EDGE(in.carve.base); EDGE(in.carve.base + in.carve.size);
	EDGE(in.layout.text_lo); EDGE(in.layout.text_hi); EDGE(in.layout.ro_hi);
	EDGE(in.fdt.base); EDGE(in.fdt.base + in.fdt.size); EDGE(in.fb.base); EDGE(in.fb.base + in.fb.size);
	for (unsigned i = 0; i < in.nrel; i++) { EDGE(in.rel[i].base); EDGE(in.rel[i].base + 8); }
	for (unsigned i = 0; i < in.ndev; i++) { EDGE(in.dev[i].base); EDGE(in.dev[i].base + in.dev[i].size); }
	for (unsigned i = 0; i < in.nguest; i++) { EDGE(in.guest[i].base); EDGE(in.guest[i].base + in.guest[i].size); }
	for (uint64_t a = 0; a < 0xa00000000ULL + 0x100000ULL * ne * 3; a += 0x100000ULL) {
		uint64_t x = a;

		if (a >= 0xa00000000ULL) {          /* the edge probes */
			unsigned k = (unsigned)((a - 0xa00000000ULL) / 0x100000ULL);

			x = (edges[k / 3] & ~(g - 1)) + (uint64_t)((int64_t)(int)(k % 3) - 1) * g;
		}
		int want = s1_model(&in, x, g);
		bool m = wel2_pt_walk(&s1, x, NULL, &at, NULL);
		int got = m ? (int)((at >> 2) & 7) == 1 ? 1 : (int)((at >> 2) & 7) == 0 ? 2 : (int)((at >> 2) & 7) == 2 ? 3 : 9 : 0;

		if (want != got && bad1++ < 5)
			printf("    s1 mismatch at 0x%" PRIx64 ": want %d got %d\n", x, want, got);
		bool w2 = s2_model(&in, x);

		if (w2 != wel2_pt_walk(&s2, x, NULL, NULL, NULL) && bad2++ < 5)
			printf("    s2 mismatch at 0x%" PRIx64 "\n", x);
	}
	CHECK_EQ(bad1, 0);
	CHECK_EQ(bad2, 0);
	/* guest blocks: start, middle, last page of each 32 MB block of the first 64 GB */
	for (uint64_t blk = 0; blk < 0x1000000000ULL; blk += WEL2_M1_S2_ALIGN) {
		bool w = s2_model(&in, blk);

		if (wel2_pt_walk(&s2, blk, NULL, NULL, NULL) != w || wel2_pt_walk(&s2, blk + 0x1000000, NULL, NULL, NULL) != w ||
		    wel2_pt_walk(&s2, blk + WEL2_M1_S2_ALIGN - g, NULL, NULL, NULL) != w)
			bad2++;
	}
	CHECK_EQ(bad2, 0);

	/* named facts (the model above must not be the only witness) */
	uint64_t xn = e2h ? (3ULL << 53) : (1ULL << 54);
	CHECK(wel2_pt_walk(&s1, in.p.aic[0].base, NULL, &at, NULL)); CHECK_EQ((at >> 2) & 7, 0); CHECK(at & xn);
	CHECK(wel2_pt_walk(&s1, in.p.uart.base, NULL, &at, NULL));   CHECK_EQ((at >> 2) & 7, 0);
	CHECK(wel2_pt_walk(&s1, in.p.wdt.base, NULL, &at, NULL));    CHECK_EQ((at >> 2) & 7, 0);   /* SYSTEM_RESET target */
	CHECK(wel2_pt_walk(&s1, in.p.fb.base, NULL, &at, NULL));     CHECK_EQ((at >> 2) & 7, WEL2_S1_ATTR_NC);
	CHECK(wel2_pt_walk(&s1, in.p.cpu[5].release_addr, NULL, &at, NULL)); CHECK_EQ((at >> 2) & 7, 1);   /* Normal WB */
	CHECK(wel2_pt_walk(&s1, in.layout.text_lo, NULL, &at, NULL)); CHECK(at & (1ULL << 7));            /* code RO */
	CHECK(!(at & (1ULL << 53)));                                                                      /* ... and executable */
	CHECK(wel2_pt_walk(&s1, in.layout.ro_hi, NULL, &at, NULL));   CHECK(!(at & (1ULL << 7)));         /* data RW */
	CHECK(!wel2_pt_walk(&s1, 0x0, NULL, NULL, NULL));                        /* no blanket device map below the RAM */
	CHECK(!wel2_pt_walk(&s1, 0x09000000ULL, NULL, NULL, NULL));              /* the QEMU PL011 is not there */
	CHECK(!wel2_pt_walk(&s1, 0xF00000000ULL, NULL, NULL, NULL));             /* the vGIC window is guest-only */
	CHECK(!wel2_pt_walk(&s2, in.carve.base, NULL, NULL, NULL));              /* the layer's block is not the guest's */
	CHECK(!wel2_pt_walk(&s2, in.carve.base + in.carve.size - g, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&s2, in.p.aic[0].base, NULL, NULL, NULL));           /* no device for the guest */
	CHECK(!wel2_pt_walk(&s2, in.p.uart.base, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&s2, in.p.wdt.base, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&s2, in.p.fb.base, NULL, NULL, NULL));
	CHECK(!wel2_pt_walk(&s2, 0xF00000000ULL, NULL, NULL, NULL));             /* vGIC GICD / GICR fault to EL2 */
	CHECK(!wel2_pt_walk(&s2, 0xF10000000ULL, NULL, NULL, NULL));
	CHECK(wel2_pt_walk(&s2, in.guest[0].base, NULL, &at, NULL));  CHECK_EQ((at >> 2) & 15, 0xf);      /* Normal WB */
	CHECK(!wel2_pt_walk(&s2, in.guest[0].base - g, NULL, NULL, NULL));

	/* E9: the framebuffer hand-over on the j293 panel (2560 x 1600, stride 10240): the guest's part is mapped Normal
	 * non-cacheable (MemAttr 0b0101), read/write, XN, accessed; the band (and everything outside the panel) is not. */
	for (unsigned mode = WEL2_FBM_BAND; mode <= WEL2_FBM_GUEST; mode++) {
		struct wel2_fb_plan pl;
		uint64_t a2, end = in.p.fb.base + wel2_fb_bytes(&in.p.fb);

		CHECK_EQ(wel2_fb_plan(&in.p.fb, mode, gshift, &pl), 0);
		CHECK_EQ(pl.band_bytes + pl.guest_bytes, wel2_fb_bytes(&in.p.fb));
		CHECK_EQ(pl.guest_base, in.p.fb.base + pl.band_bytes);
		CHECK_EQ(pl.band_bytes & (g - 1), 0);
		CHECK_EQ(pl.guest_base & (g - 1), 0);
		if (mode == WEL2_FBM_BAND)
			CHECK(pl.band_rows >= wel2_fb_band_min_rows(&in.p.fb) && pl.band_rows < 1600);
		else
			CHECK_EQ(pl.band_rows, 0);
		CHECK_EQ(wel2_pt_init(&s2, pool_s2, sizeof(pool_s2), gshift, plat_m1_ipa), 0);
		CHECK_EQ(wel2_s2_build_ram(&s2, in.guest, in.nguest), 0);
		CHECK_EQ(wel2_s2_map_fb(&s2, pl.guest_base, pl.guest_bytes), 0);
		printf("    %s fb mode %u gran %uk: band %u rows, guest part %u rows, stage 2 uses %u tables\n", tag, mode,
		       1u << (gshift - 10), pl.band_rows, pl.guest_rows, s2.used);
		for (a2 = in.p.fb.base; a2 < end; a2 += g) {                   /* every page: band unmapped, the rest NC XN RW */
			bool guest_page = a2 >= pl.guest_base;
			bool mapped = wel2_pt_walk(&s2, a2, NULL, &at, NULL);

			CHECK_EQ(mapped, guest_page);
			if (mapped) {
				CHECK_EQ((at >> 2) & 15, 0x5);                  /* Normal Inner+Outer Non-cacheable */
				CHECK_EQ((at >> 6) & 3, 3);                     /* S2AP read/write */
				CHECK(at & (1ULL << 10));                       /* AF */
				CHECK(at & (1ULL << 54));                       /* XN */
			}
		}
		CHECK(!wel2_pt_walk(&s2, in.p.fb.base - g, NULL, NULL, NULL));
		CHECK(!wel2_pt_walk(&s2, end, NULL, NULL, NULL));
		CHECK(wel2_pt_walk(&s2, in.guest[0].base, NULL, &at, NULL));    /* the RAM map is untouched */
		CHECK_EQ((at >> 2) & 15, 0xf);
	}
}

static void t_m1_maps(void)
{
	static const struct opt big = { false, "serial0:115200", "/soc/serial@235200000", -1, -1, 0, false, true };
	const unsigned grans[2] = { WEL2_GRAN_16K, WEL2_GRAN_4K };

	for (unsigned gi = 0; gi < 2; gi++)
		for (unsigned e = 0; e < 2; e++) {
			m1_check_maps(&O0, grans[gi], e, 0x804000000ULL, 0x9f0000000ULL, "small j293");
			m1_check_maps(&O0, grans[gi], e, 0x804000000ULL, 0x802000100ULL, "small j293 (FDT in RAM)");
			m1_check_maps(&big, grans[gi], e, 0x884000000ULL, 0x9f0000000ULL, "8 GB j293");
			m1_check_maps(&big, grans[gi], e, 0x9e0000000ULL - 0x2000000ULL, 0x9f0000000ULL, "8 GB j293 (layer at the top)");
		}
}

static void t_m1_refusals(void)
{
	struct wel2_pt pt;
	static struct wel2_range ram[1] = { { 0x800000000ULL, 0x10000000ULL } };
	struct wel2_s1_m1 in = { .ram = ram, .nram = 1, .carve = { 0x800000000ULL, 0x2000000ULL } };
	struct wel2_range dev = { 0x800400000ULL, 0x4000 };

	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), 0);
	CHECK_EQ(wel2_s1_map_fb_free(&pt, 0x800400000ULL, 0x4000, true), -1);      /* inside the RAM map */
	CHECK_EQ(wel2_s1_map_fb_free(&pt, 0x1000000000ULL, 0x4000, true), -1);     /* beyond the 36-bit input range */
	CHECK_EQ(wel2_s1_map_fb_free(&pt, 0x900000000ULL, 0x10000, true), 0);
	CHECK_EQ(wel2_s1_map_fb_free(&pt, 0x900008000ULL, 0x4000, true), -1);      /* overlaps the previous one */
	in.dev = &dev;
	in.ndev = 1;
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), -1);                             /* a device on a RAM page */
	struct wel2_range devs[2] = { { 0x900000000ULL, 0xc000 }, { 0x900008000ULL, 0x1000 } };   /* reg[1] inside reg[0] */

	in.dev = devs;
	in.ndev = 2;
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), 0);
	struct wel2_range rel = { 0x900000000ULL, 8 };                              /* a release word in a Device page */

	in.dev = NULL; in.ndev = 0; in.rel = &rel; in.nrel = 1;
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), 0);                              /* alone it is mapped Normal */
	in.dev = devs; in.ndev = 2;
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), -1);                             /* both: refused, never silently Device */
}

static void t_m1_fdt_pages(void)
{
	struct wel2_pt pt;
	static struct wel2_range ram[1] = { { 0x800000000ULL, 0x10000000ULL } };
	struct wel2_s1_m1 in = { .ram = ram, .nram = 1, .carve = { 0x800000000ULL, 0x2000000ULL } };
	struct wel2_range rel = { 0x900000010ULL, 8 };
	uint64_t at;

	in.fdt = (struct wel2_range){ 0x900000000ULL, 0x8000 };                /* outside the RAM: read-only pages */
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), 0);
	CHECK(wel2_pt_walk(&pt, 0x900004000ULL, NULL, &at, NULL)); CHECK(at & (1ULL << 7));
	in.rel = &rel; in.nrel = 1;                                           /* a release word in the FDT's page */
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), -1);
	in.fdt = (struct wel2_range){ 0x800400000ULL, 0x8000 };                /* inside the RAM map: stays read-write */
	in.rel = NULL; in.nrel = 0;
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), 0);
	CHECK(wel2_pt_walk(&pt, 0x800400000ULL, NULL, &at, NULL)); CHECK(!(at & (1ULL << 7)));
	in.fdt = (struct wel2_range){ 0x80fffc000ULL, 0x8000 };                /* straddles the RAM end: only the outside page RO */
	CHECK_EQ(wel2_pt_init(&pt, pool_s1, sizeof(pool_s1), WEL2_GRAN_16K, 36), 0);
	CHECK_EQ(wel2_s1_build_m1(&pt, &in, true), 0);
	CHECK(wel2_pt_walk(&pt, 0x80fffc000ULL, NULL, &at, NULL)); CHECK(!(at & (1ULL << 7)));
	CHECK(wel2_pt_walk(&pt, 0x810000000ULL, NULL, &at, NULL)); CHECK(at & (1ULL << 7));
}

static void t_ranges_inward(void)
{
	struct wel2_range in[3] = { { 0x1000, 0x5000000 }, { 0x10000000, 0x1ffffff }, { 0x20000000, 0x8000000 } }, out[8];
	unsigned n;

	CHECK_EQ(wel2_ranges_inward(in, 3, 0x2000000, 0, 0, out, 8, &n), 0);
	CHECK_EQ(n, 2);                                                              /* the middle piece is 1 byte short */
	CHECK_EQ(out[0].base, 0x2000000); CHECK_EQ(out[0].size, 0x2000000);          /* [0x1000, 0x5001000) -> one block */
	CHECK_EQ(out[1].base, 0x20000000); CHECK_EQ(out[1].size, 0x8000000);
	CHECK_EQ(wel2_ranges_inward(in, 3, 0x2000000, 0x23000000, 0x1000, out, 8, &n), 0);   /* a cut rounds outward */
	CHECK_EQ(n, 3);
	CHECK_EQ(out[1].base, 0x20000000); CHECK_EQ(out[1].size, 0x2000000);
	CHECK_EQ(out[2].base, 0x24000000); CHECK_EQ(out[2].size, 0x4000000);
	CHECK_EQ(wel2_ranges_inward(in, 3, 0x2000000, 0x23000000, 0x1000, out, 2, &n), -1);   /* too small: refused */
	CHECK_EQ(wel2_ranges_inward(in, 3, 0x2000000, 0xfffffffffffff000ULL, 0x2000, out, 8, &n), -1);   /* wrapping cut */
}

int main(void)
{
	RUN(t_geometry);
	RUN(t_registers);
	RUN(t_blocks_and_pages);
	RUN(t_bad_arguments_and_exhaustion);
	RUN(t_s2_map);
	RUN(t_s2_aic_hole);
	RUN(t_s2_v2m_hole);
	RUN(t_s5l_uart_maps);
	RUN(t_high_carve);
	RUN(t_s2_add_hole_rules);
	RUN(t_s2_odd_ram_end);
	RUN(t_s1_map);
	RUN(t_ranges_inward);
	RUN(t_m1_refusals);
	RUN(t_m1_fdt_pages);
	RUN(t_m1_maps);
	TESTS_DONE();
}
