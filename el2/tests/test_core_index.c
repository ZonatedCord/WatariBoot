/* SPDX-License-Identifier: MIT */
/* wel2_core_index(): stack-slot index from MPIDR_EL1. */
#include "test.h"
#include "wel2_coreidx.h"

static void qemu_plain(void)
{
	for (unsigned i = 0; i < 8; i++) {
		/* QEMU with more than 4 cores per cluster would use Aff0 >= 4: not supported. */
		unsigned want = i < 4 ? i : WEL2_CORE_INVALID;
		CHECK_EQ(wel2_core_index(i), want);
	}
}

static void qemu_with_res1(void)
{
	for (unsigned i = 0; i < 4; i++)
		CHECK_EQ(wel2_core_index(0x80000000ull | i), i);
	CHECK_EQ(wel2_core_index(0x80000004ull), WEL2_CORE_INVALID);
	/* U and MT bits are ignored */
	CHECK_EQ(wel2_core_index(0xC1000002ull), 2);
}

static void m1_all(void)
{
	static const uint64_t m[8] = { 0x80000000, 0x80000001, 0x80000002, 0x80000003,
				       0x80010100, 0x80010101, 0x80010102, 0x80010103 };
	unsigned seen = 0;
	for (unsigned i = 0; i < 8; i++) {
		unsigned idx = wel2_core_index(m[i]);
		CHECK_EQ(idx, i);
		CHECK(idx < WEL2_NSTACKS);
		CHECK((seen & (1u << idx)) == 0);
		seen |= 1u << idx;
	}
	CHECK_EQ(seen, 0xff);
}

static void out_of_range(void)
{
	CHECK_EQ(wel2_core_index(0x80000004ull), WEL2_CORE_INVALID);   /* Aff0 >= 4 */
	CHECK_EQ(wel2_core_index(0x80010104ull), WEL2_CORE_INVALID);   /* Aff0 >= 4, P cluster */
	CHECK_EQ(wel2_core_index(0x80020200ull), WEL2_CORE_INVALID);   /* Aff1 = Aff2 = 2 */
	CHECK_EQ(wel2_core_index(0x80000200ull), WEL2_CORE_INVALID);   /* Aff1 = 2, Aff2 = 0 */
	CHECK_EQ(wel2_core_index(0x80010000ull), WEL2_CORE_INVALID);   /* Aff2=1, Aff1=0 */
	CHECK_EQ(wel2_core_index(0x80000100ull), WEL2_CORE_INVALID);   /* Aff2=0, Aff1=1 */
	CHECK_EQ(wel2_core_index(0x80010200ull), WEL2_CORE_INVALID);   /* Aff2 != Aff1 */
	CHECK_EQ(wel2_core_index(0x180000000ull), WEL2_CORE_INVALID);  /* Aff3 != 0 */
	CHECK_EQ(wel2_core_index(0x180010101ull), WEL2_CORE_INVALID);  /* Aff3 != 0, else valid */
	CHECK_EQ(wel2_core_index(0xffull), WEL2_CORE_INVALID);
}

int main(void)
{
	RUN(qemu_plain);
	RUN(qemu_with_res1);
	RUN(m1_all);
	RUN(out_of_range);
	TESTS_DONE();
}
