/* SPDX-License-Identifier: MIT */
/* Host tests: guest RAM range from the FDT /memory node (wel2_ram.h, #60). */
#include "test.h"
#include "wel2_ram.h"

#define BASE  0x40000000ULL
#define MAXPA (512ULL << 30)                    /* 39-bit stage 1 */

static struct wel2_ram R;

static void t_qemu_machines(void)
{
	/* -m 512M: the first GB is partly RAM */
	CHECK_EQ(wel2_ram_set(&R, BASE, 0x20000000ULL, BASE, MAXPA), 0);
	CHECK_EQ(R.base, BASE); CHECK_EQ(R.end, 0x60000000ULL);
	CHECK_EQ(wel2_ram_cover(&R, 0), WEL2_RAM_NONE);
	CHECK_EQ(wel2_ram_cover(&R, 1), WEL2_RAM_PARTIAL);
	CHECK_EQ(wel2_ram_cover(&R, 2), WEL2_RAM_NONE);
	CHECK(wel2_ram_has_block(&R, 0x5fe00000ULL));
	CHECK(!wel2_ram_has_block(&R, 0x60000000ULL));
	/* -m 2G: two full GB (the old 2 GB map) */
	CHECK_EQ(wel2_ram_set(&R, BASE, 0x80000000ULL, BASE, MAXPA), 0);
	CHECK_EQ(R.end, 0xc0000000ULL);
	CHECK_EQ(wel2_ram_cover(&R, 1), WEL2_RAM_FULL);
	CHECK_EQ(wel2_ram_cover(&R, 2), WEL2_RAM_FULL);
	CHECK_EQ(wel2_ram_cover(&R, 3), WEL2_RAM_NONE);
	/* -m 4G (Windows): GB 1..4, ends at 0x13fffffff */
	CHECK_EQ(wel2_ram_set(&R, BASE, 0x100000000ULL, BASE, MAXPA), 0);
	CHECK_EQ(R.end, 0x140000000ULL);
	for (unsigned g = 1; g <= 4; g++)
		CHECK_EQ(wel2_ram_cover(&R, g), WEL2_RAM_FULL);
	CHECK_EQ(wel2_ram_cover(&R, 5), WEL2_RAM_NONE);
	CHECK(wel2_ram_contains(&R, 0x13fffffffULL));
	CHECK(!wel2_ram_contains(&R, 0x140000000ULL));
	CHECK(!wel2_ram_contains(&R, BASE - 1));
}

static void t_partial_tail_and_rounding(void)
{
	/* 3.5 GB: GB 1,2,3 full, GB 4 half */
	CHECK_EQ(wel2_ram_set(&R, BASE, 0xe0000000ULL, BASE, MAXPA), 0);
	CHECK_EQ(R.end, 0x120000000ULL);
	CHECK_EQ(wel2_ram_cover(&R, 3), WEL2_RAM_FULL);
	CHECK_EQ(wel2_ram_cover(&R, 4), WEL2_RAM_PARTIAL);
	/* odd sizes round inwards to 2 MB: 97 MB + 5 bytes */
	CHECK_EQ(wel2_ram_set(&R, BASE, 0x6100005ULL, BASE, MAXPA), 0);
	CHECK_EQ(R.end, BASE + 0x6000000ULL);
	/* an unaligned base rounds up */
	CHECK_EQ(wel2_ram_set(&R, BASE + 0x1000, 0x10000000ULL, BASE, MAXPA), 0);
	CHECK_EQ(R.base, BASE + 0x200000ULL);
	CHECK_EQ(R.end, BASE + 0x10000000ULL);
	/* RAM that starts in the middle of a GB and spans two */
	CHECK_EQ(wel2_ram_set(&R, 0x60000000ULL, 0x80000000ULL, BASE, MAXPA), 0);
	CHECK_EQ(wel2_ram_cover(&R, 1), WEL2_RAM_PARTIAL);
	CHECK_EQ(wel2_ram_cover(&R, 2), WEL2_RAM_FULL);
	CHECK_EQ(wel2_ram_cover(&R, 3), WEL2_RAM_PARTIAL);
}

static void t_rejects(void)
{
	struct wel2_ram keep = { 1, 2 };

	R = keep;
	CHECK_EQ(wel2_ram_set(&R, BASE, 0, BASE, MAXPA), -1);                /* empty */
	CHECK_EQ(wel2_ram_set(&R, 0, 0x80000000ULL, BASE, MAXPA), -1);       /* below the device window */
	CHECK_EQ(wel2_ram_set(&R, 0x3fe00000ULL, 0x80000000ULL, BASE, MAXPA), -1);
	CHECK_EQ(wel2_ram_set(&R, 0x3ff00000ULL, 0x80000000ULL, BASE, MAXPA), 0);   /* rounds up to the window */
	R = keep;
	CHECK_EQ(wel2_ram_set(&R, BASE, 0x100000ULL, BASE, MAXPA), -1);     /* rounds to nothing */
	CHECK_EQ(wel2_ram_set(&R, ~0ULL - 0xfffULL, 0x2000, BASE, ~0ULL), -1);   /* wraps */
	CHECK_EQ(wel2_ram_set(&R, MAXPA, 0x1000000, BASE, MAXPA), -1);       /* entirely above the limit */
	CHECK_EQ(R.base, 1); CHECK_EQ(R.end, 2);                             /* untouched on failure */
}

static void t_clamp(void)
{
	/* 1 TB of RAM on a 39-bit stage 1: clamped at 512 GB */
	CHECK_EQ(wel2_ram_set(&R, BASE, 1ULL << 40, BASE, MAXPA), 0);
	CHECK_EQ(R.end, MAXPA);
	CHECK_EQ(wel2_ram_cover(&R, 511), WEL2_RAM_FULL);
	CHECK_EQ(wel2_ram_cover(&R, 512), WEL2_RAM_NONE);                    /* beyond the table */
	CHECK_EQ(wel2_ram_set(&R, BASE, 0x100000000ULL, BASE, BASE + 0x3ff00000ULL), 0);
	CHECK_EQ(R.end, BASE + 0x3fe00000ULL);                               /* clamp also rounds */
}

static void t_from_reg(void)
{
	/* <0 0x40000000> <0 0x80000000>: 2 address cells, 2 size cells (QEMU virt) */
	static const uint8_t v22[16] = { 0, 0, 0, 0, 0x40, 0, 0, 0,  0, 0, 0, 0, 0x80, 0, 0, 0 };
	/* 1 + 1 cells */
	static const uint8_t v11[8]  = { 0x40, 0, 0, 0, 0x20, 0, 0, 0 };
	/* 2 + 2 cells, RAM above 4 GB: base 0x1_0000_0000, size 0x1_0000_0000 */
	static const uint8_t v22b[16] = { 0, 0, 0, 1, 0, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0 };
	/* two pairs: only the first counts */
	static const uint8_t vtwo[32] = { 0, 0, 0, 0, 0x40, 0, 0, 0,  0, 0, 0, 0, 0x40, 0, 0, 0,
					  0, 0, 0, 1, 0, 0, 0, 0,  0, 0, 0, 0, 0x40, 0, 0, 0 };

	CHECK_EQ(wel2_ram_from_reg(&R, v22, 16, 2, 2, BASE, MAXPA), 0);
	CHECK_EQ(R.base, BASE); CHECK_EQ(R.end, 0xc0000000ULL);
	CHECK_EQ(wel2_ram_from_reg(&R, v11, 8, 1, 1, BASE, MAXPA), 0);
	CHECK_EQ(R.base, BASE); CHECK_EQ(R.end, 0x60000000ULL);
	CHECK_EQ(wel2_ram_from_reg(&R, v22b, 16, 2, 2, BASE, MAXPA), 0);
	CHECK_EQ(R.base, 0x100000000ULL); CHECK_EQ(R.end, 0x200000000ULL);
	CHECK_EQ(wel2_ram_from_reg(&R, vtwo, 32, 2, 2, BASE, MAXPA), 0);
	CHECK_EQ(R.end, BASE + 0x40000000ULL);
	CHECK_EQ(wel2_ram_from_reg(&R, v22, 15, 2, 2, BASE, MAXPA), -1);     /* shorter than one pair */
	CHECK_EQ(wel2_ram_from_reg(&R, v22, 16, 0, 2, BASE, MAXPA), -1);     /* bad cell counts */
	CHECK_EQ(wel2_ram_from_reg(&R, v22, 16, 3, 1, BASE, MAXPA), -1);
	CHECK_EQ(wel2_ram_from_reg(&R, NULL, 16, 2, 2, BASE, MAXPA), -1);
}

int main(void)
{
	RUN(t_qemu_machines);
	RUN(t_partial_tail_and_rounding);
	RUN(t_rejects);
	RUN(t_clamp);
	RUN(t_from_reg);
	TESTS_DONE();
}
