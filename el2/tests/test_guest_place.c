/* SPDX-License-Identifier: MIT */
/* Host tests: placement of the EL1 test guest and its info block on the M1 (T7b). */
#include "test.h"
#include "wel2_guest_place.h"
#include "wel2_guest_info.h"

#define MB 0x100000ULL

/* The j293 shape: RAM from 0x8_0000_0000, the layer's 32 MB block cut out of the middle, ranges rounded to 32 MB. */
static const struct wel2_range R[] = {
	{ 0x800000000ULL, 0x100000000ULL - 0x4000000ULL },       /* 4 GB up to a hole of 64 MB at its end */
	{ 0x900000000ULL, 0x1dfc00000ULL & ~0x1ffffffULL },      /* next piece */
};
#define NR (sizeof(R) / sizeof(R[0]))

static void t_ok_at_start_of_block(void)
{
	struct wel2_guest_place p;
	uint64_t entry = 0x808000000ULL;                          /* a 32 MB aligned block */

	CHECK_EQ(wel2_guest_place(R, NR, entry, 48056, sizeof(struct wel2_guest_info), &p), WEL2_GP_OK);
	CHECK_EQ(p.entry, entry);
	CHECK_EQ(p.image_end, entry + 48056);
	CHECK_EQ(p.info, entry + 0xc000);                         /* 48056 = 0xbbb8, rounded up to 4 KB */
	CHECK((p.info & 0xfff) == 0);
	CHECK(p.info >= p.image_end);
	CHECK(p.info + sizeof(struct wel2_guest_info) <= entry + WEL2_GUEST_MAX_IMAGE);   /* inside the guest's 32 MB block */
}

static void t_exact_page_size(void)
{
	struct wel2_guest_place p;

	CHECK_EQ(wel2_guest_place(R, NR, 0x808000000ULL, 0x4000, 0x100, &p), WEL2_GP_OK);
	CHECK_EQ(p.info, 0x808004000ULL);                         /* already aligned: directly above the image */
}

static void t_bad_entry(void)
{
	struct wel2_guest_place p;

	CHECK_EQ(wel2_guest_place(R, NR, 0, 0x1000, 0x100, &p), WEL2_GP_BAD_ENTRY);
	CHECK_EQ(wel2_guest_place(R, NR, 0x808000800ULL, 0x1000, 0x100, &p), WEL2_GP_BAD_ENTRY);
}

static void t_bad_size(void)
{
	struct wel2_guest_place p;

	CHECK_EQ(wel2_guest_place(R, NR, 0x808000000ULL, 0, 0x100, &p), WEL2_GP_BAD_SIZE);
	CHECK_EQ(wel2_guest_place(R, NR, 0x808000000ULL, WEL2_GUEST_MAX_IMAGE + 1, 0x100, &p), WEL2_GP_BAD_SIZE);
	CHECK_EQ(wel2_guest_place(R, NR, 0xfffffffffffff000ULL, 0x2000, 0x100, &p), WEL2_GP_BAD_SIZE);   /* wraps */
	CHECK_EQ(wel2_guest_place(R, NR, 0x808000000ULL, WEL2_GUEST_MAX_IMAGE, 0x100, &p), WEL2_GP_OK);   /* exactly 32 MB */
}

static void t_not_in_ram(void)
{
	struct wel2_guest_place p;

	CHECK_EQ(wel2_guest_place(R, NR, 0x40000000ULL, 0x1000, 0x100, &p), WEL2_GP_NOT_IN_RAM);          /* below RAM */
	CHECK_EQ(wel2_guest_place(R, NR, 0x8fc000000ULL, 0x1000, 0x100, &p), WEL2_GP_NOT_IN_RAM);         /* the hole (layer block) */
	CHECK_EQ(wel2_guest_place(R, NR, 0xb00000000ULL, 0x1000, 0x100, &p), WEL2_GP_NOT_IN_RAM);         /* above the last range */
	CHECK_EQ(wel2_guest_place(R, 0, 0x808000000ULL, 0x1000, 0x100, &p), WEL2_GP_NOT_IN_RAM);          /* no ranges */
}

static void t_straddles_end(void)
{
	struct wel2_guest_place p;
	uint64_t end0 = R[0].base + R[0].size;

	CHECK_EQ(wel2_guest_place(R, NR, end0 - 0x2000, 0x3000, 0x100, &p), WEL2_GP_IMAGE_OUT);           /* runs into the hole */
	CHECK_EQ(wel2_guest_place(R, NR, end0 - 0x2000, 0x2000, 0x100, &p), WEL2_GP_INFO_OUT);            /* image fits, info does not */
	CHECK_EQ(wel2_guest_place(R, NR, end0 - 0x3000, 0x2000, 0x100, &p), WEL2_GP_OK);                  /* info in the last page */
	CHECK_EQ(p.info, end0 - 0x1000);
	CHECK_EQ(wel2_guest_place(R, NR, end0 - 0x3000, 0x2000, 0x1001, &p), WEL2_GP_INFO_OUT);           /* needs 2 pages */
}

static void t_adjacent_ranges_are_not_joined(void)
{
	static const struct wel2_range adj[] = { { 0x800000000ULL, 0x2000000ULL }, { 0x802000000ULL, 0x2000000ULL } };
	struct wel2_guest_place p;

	/* two touching ranges are still two pieces (a reservation may sit between pieces in a real list): no crossing */
	CHECK_EQ(wel2_guest_place(adj, 2, 0x801fff000ULL, 0x2000, 0x100, &p), WEL2_GP_IMAGE_OUT);
	CHECK_EQ(wel2_guest_place(adj, 2, 0x802000000ULL, 0x1000, 0x100, &p), WEL2_GP_OK);
	CHECK_EQ(p.info, 0x802001000ULL);
}

static void t_second_range(void)
{
	struct wel2_guest_place p;

	CHECK_EQ(wel2_guest_place(R, NR, R[1].base, 0x10000, sizeof(struct wel2_guest_info), &p), WEL2_GP_OK);
	CHECK_EQ(p.info, R[1].base + 0x10000);
}

int main(void)
{
	RUN(t_ok_at_start_of_block);
	RUN(t_exact_page_size);
	RUN(t_bad_entry);
	RUN(t_bad_size);
	RUN(t_not_in_ram);
	RUN(t_straddles_end);
	RUN(t_adjacent_ranges_are_not_joined);
	RUN(t_second_range);
	TESTS_DONE();
}
