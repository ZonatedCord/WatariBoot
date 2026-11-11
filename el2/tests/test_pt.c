/* SPDX-License-Identifier: MIT */
/* Host tests: granule-parametric translation tables and the EL2 maps (wel2_pt.h, wel2_maps.h, #58). The tables
 * are built into a host buffer and walked for chosen addresses; the register values are compared with the
 * constants the layer used before the builder existed (4 KB). */
#include "test.h"
#include "wel2_maps.h"
#include "wel2_aic.h"
#include "wel2_v2m.h"

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
	TESTS_DONE();
}
