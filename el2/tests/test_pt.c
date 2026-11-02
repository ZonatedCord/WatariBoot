/* SPDX-License-Identifier: MIT */
/* Host tests: granule-parametric translation tables and the EL2 maps (wel2_pt.h, wel2_maps.h, #58). The tables
 * are built into a host buffer and walked for chosen addresses; the register values are compared with the
 * constants the layer used before the builder existed (4 KB). */
#include "test.h"
#include "wel2_maps.h"

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
	CHECK_EQ(wel2_pt_init(&pt, pool_small, sizeof(pool_small), WEL2_GRAN_16K, 48), -1);   /* level 0 needs LPA2 */
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
			CHECK_EQ(attrs_of(&pt, l.text_lo), nrm | (1ULL << 7));      /* code: RO, executable */
			CHECK_EQ(attrs_of(&pt, l.text_hi - 1), nrm | (1ULL << 7));
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
	RUN(t_s2_odd_ram_end);
	RUN(t_s1_map);
	TESTS_DONE();
}
