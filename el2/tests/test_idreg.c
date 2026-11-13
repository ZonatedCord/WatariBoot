/* SPDX-License-Identifier: MIT */
/* Host tests: TID3 ID-register emulation (T6g, PLAN S12): which encodings are trapped and what the guest reads. */
#include "test.h"
#include "wel2_idreg.h"

/* An ESR_EL2 ISS for an MRS/MSR trap: op0 [21:20] op2 [19:17] op1 [16:14] CRn [13:10] Rt [9:5] CRm [4:1] dir [0]. */
static uint32_t iss(unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt, unsigned rd)
{
	return (op0 << 20) | (op2 << 17) | (op1 << 14) | (crn << 10) | (rt << 5) | (crm << 1) | rd;
}

static unsigned g_calls;
static uint64_t g_fake[8][8];
static bool g_read[8][8];

static uint64_t fake_rd(void *ctx, unsigned crm, unsigned op2)
{
	(void)ctx;
	g_calls++;
	g_read[crm][op2] = true;
	return g_fake[crm][op2];
}

static void t_decode_group(void)
{
	struct wel2_idreg_acc a;

	/* every (CRm 1..7, op2 0..7) read is in the group, with Rt and the position carried over */
	for (unsigned crm = 1; crm <= 7; crm++)
		for (unsigned op2 = 0; op2 < 8; op2++) {
			CHECK(wel2_idreg_decode(iss(3, 0, 0, crm, op2, 9, 1), &a));
			CHECK_EQ(a.crm, crm); CHECK_EQ(a.op2, op2); CHECK_EQ(a.rt, 9);
		}
	CHECK(wel2_idreg_decode(iss(3, 0, 0, 4, 0, 31, 1), &a));         /* ID_AA64PFR0_EL1 into XZR */
	CHECK_EQ(a.rt, 31);
	/* outside the group */
	CHECK(!wel2_idreg_decode(iss(3, 0, 0, 0, 0, 1, 1), &a));          /* MIDR_EL1 (CRm 0): TID1, not TID3 */
	CHECK(!wel2_idreg_decode(iss(3, 0, 0, 0, 5, 1, 1), &a));          /* MPIDR_EL1 */
	CHECK(!wel2_idreg_decode(iss(3, 0, 0, 8, 0, 1, 1), &a));          /* CRm 8 */
	CHECK(!wel2_idreg_decode(iss(3, 0, 0, 15, 0, 1, 1), &a));
	CHECK(!wel2_idreg_decode(iss(3, 3, 0, 0, 1, 1, 1), &a));          /* CTR_EL0 (op1 3): TID2 */
	CHECK(!wel2_idreg_decode(iss(3, 1, 0, 0, 0, 1, 1), &a));          /* CCSIDR_EL1 (op1 1) */
	CHECK(!wel2_idreg_decode(iss(3, 0, 12, 12, 0, 1, 1), &a));        /* ICC_IAR1_EL1: the cpuif traps handle it */
	CHECK(!wel2_idreg_decode(iss(3, 0, 4, 6, 0, 1, 1), &a));          /* ICC_PMR_EL1 */
	CHECK(!wel2_idreg_decode(iss(2, 0, 0, 4, 0, 1, 1), &a));          /* op0 2 */
	CHECK(!wel2_idreg_decode(iss(3, 0, 0, 4, 0, 1, 0), &a));          /* a write (MSR) is not emulated here */
}

static void t_known_table(void)
{
	/* the registers named in DESIGN 5.4 / 5.1 are there */
	CHECK(wel2_idreg_known(4, 0));        /* ID_AA64PFR0_EL1 */
	CHECK(wel2_idreg_known(4, 1));        /* ID_AA64PFR1_EL1 */
	CHECK(wel2_idreg_known(5, 0));        /* ID_AA64DFR0_EL1 */
	CHECK(wel2_idreg_known(6, 0));        /* ID_AA64ISAR0_EL1 */
	CHECK(wel2_idreg_known(6, 1));        /* ID_AA64ISAR1_EL1 (PAuth) */
	CHECK(wel2_idreg_known(6, 2));        /* ID_AA64ISAR2_EL1 */
	CHECK(wel2_idreg_known(7, 0));        /* ID_AA64MMFR0_EL1 */
	CHECK(wel2_idreg_known(7, 1)); CHECK(wel2_idreg_known(7, 2));
	CHECK(wel2_idreg_known(1, 0));        /* ID_PFR0_EL1 */
	CHECK(wel2_idreg_known(3, 0));        /* MVFR0_EL1 */
	/* reserved slots */
	CHECK(!wel2_idreg_known(4, 2)); CHECK(!wel2_idreg_known(4, 3)); CHECK(!wel2_idreg_known(4, 6)); CHECK(!wel2_idreg_known(4, 7));
	CHECK(!wel2_idreg_known(3, 3)); CHECK(!wel2_idreg_known(3, 7));
	CHECK(!wel2_idreg_known(6, 4)); CHECK(!wel2_idreg_known(7, 7));
	CHECK(!wel2_idreg_known(0, 0)); CHECK(!wel2_idreg_known(8, 0)); CHECK(!wel2_idreg_known(1, 8));
	/* count and enumeration agree */
	unsigned n = wel2_idreg_count(), seen = 0, crm, op2, pcrm = 0, pop2 = 0;

	CHECK_EQ(n, 8 + 8 + 6 + 4 + 4 + 3 + 3);
	for (unsigned i = 0; wel2_idreg_nth(i, &crm, &op2); i++) {
		CHECK(wel2_idreg_known(crm, op2));
		CHECK(i == 0 || crm > pcrm || (crm == pcrm && op2 > pop2));     /* (crm, op2) order, no repeat */
		pcrm = crm; pop2 = op2;
		seen++;
	}
	CHECK_EQ(seen, n);
	CHECK(!wel2_idreg_nth(n, &crm, &op2));
}

static void t_gic_forced_rest_unchanged(void)
{
	/* The real registers are all ones in turn, then the M1-like value with GIC = 0 and PAuth set. */
	for (unsigned i = 0; i < 8; i++)
		for (unsigned j = 0; j < 8; j++)
			g_fake[i][j] = 0x1122334455667788ull ^ (i * 0x0101010101010101ull) ^ j;
	/* a core with AArch32 at EL0, SVE and SME: everything is read */
	g_fake[4][0] = (g_fake[4][0] & ~0xffULL & ~(0xfULL << 32)) | 0x12ULL | (1ULL << 32);
	g_fake[4][1] = (g_fake[4][1] & ~(0xfULL << 24)) | (1ULL << 24);
	g_calls = 0;
	for (unsigned crm = 1; crm <= 7; crm++)
		for (unsigned op2 = 0; op2 < 8; op2++) {
			uint64_t v = wel2_idreg_value(fake_rd, 0, crm, op2);

			if (!wel2_idreg_known(crm, op2))
				CHECK_EQ(v, 0);
			else if (crm == 4 && op2 == 0)
				CHECK_EQ(v, (g_fake[4][0] & ~(0xfULL << 24)) | (1ULL << 24));
			else
				CHECK_EQ(v, g_fake[crm][op2]);       /* unchanged, bit for bit */
		}
	CHECK(g_calls >= wel2_idreg_count());                /* every known register was read, plus PFR0/PFR1 for the checks */

	/* ID_AA64PFR0_EL1 of the M1 shape: GIC 0 -> 1, every other field untouched (AdvSIMD, FP, EL0..EL3, ...) */
	g_fake[4][0] = 0x0000000011112222ull;
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 4, 0), 0x0000000011112222ull | (1ULL << 24));
	/* GIC already 1 (QEMU -cpu max): same value; GIC 3 (a future GICv4.1 CPU field) is reduced to 1 */
	g_fake[4][0] = 0x0000000011112222ull | (1ULL << 24);
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 4, 0), g_fake[4][0]);
	g_fake[4][0] = 0x00000000f1112222ull | (3ULL << 24);
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 4, 0), 0x00000000f1112222ull | (1ULL << 24));
	/* the high half (e.g. the SVE, RAS, MPAM fields of PFR0) is carried over */
	g_fake[4][0] = 0xfedcba9800000000ull | (0xaULL << 24) | 0x1234;
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 4, 0), 0xfedcba9800000000ull | (1ULL << 24) | 0x1234);
}

static void t_pauth_fields_pass_through(void)
{
	/* ID_AA64ISAR1_EL1 APA [7:4], API [11:8], GPA [27:24], GPI [31:28]; ISAR2 APA3 [15:12], GPA3 [11:8] */
	g_fake[6][1] = 0x0000000011110aa0ull | 0x00000000f0f00000ull;
	g_fake[6][2] = 0x000000000000ff00ull;
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 6, 1), g_fake[6][1]);
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 6, 2), g_fake[6][2]);
}

static void clear_read(void)
{
	for (unsigned i = 0; i < 8; i++)
		for (unsigned j = 0; j < 8; j++)
			g_read[i][j] = false;
	g_calls = 0;
}

static void all_values(void)
{
	for (unsigned crm = 1; crm <= 7; crm++)
		for (unsigned op2 = 0; op2 < 8; op2++)
			(void)wel2_idreg_value(fake_rd, 0, crm, op2);
}

/* The M1 shape: AArch64 only (PFR0 EL0 = EL1 = 1), no SVE, no SME. */
static void t_not_read_when_absent(void)
{
	for (unsigned i = 0; i < 8; i++)
		for (unsigned j = 0; j < 8; j++)
			g_fake[i][j] = 0xdeadbeef00000000ull | (i * 8 + j + 1);
	g_fake[4][0] = 0x0000000000001011ull;                 /* EL0 1, EL1 1, SVE 0 */
	g_fake[4][1] = 0x0000000000000020ull;                 /* SME 0 */
	clear_read();
	all_values();
	for (unsigned crm = 1; crm <= 3; crm++)
		for (unsigned op2 = 0; op2 < 8; op2++) {
			CHECK(!g_read[crm][op2]);                    /* never read at EL2 */
			CHECK_EQ(wel2_idreg_value(fake_rd, 0, crm, op2), 0);
		}
	CHECK(!g_read[4][4]); CHECK(!g_read[4][5]);          /* ZFR0, SMFR0 */
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 4, 4), 0);
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 4, 5), 0);
	CHECK(g_read[4][0] && g_read[4][1] && g_read[5][0] && g_read[6][1] && g_read[7][0]);   /* the 64-bit ones are */
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 6, 1), g_fake[6][1]);
	CHECK_EQ(wel2_idreg_value(fake_rd, 0, 4, 0), 0x0000000000001011ull | (1ULL << 24));
}

static void t_read_when_present(void)
{
	for (unsigned i = 0; i < 8; i++)
		for (unsigned j = 0; j < 8; j++)
			g_fake[i][j] = 0x77ull + i * 8 + j;
	/* AArch32 at EL0 only (EL0 = 2, EL1 = 1): the AArch32 group is read; SVE present, SME absent */
	g_fake[4][0] = 0x0000000100000012ull;
	g_fake[4][1] = 0;
	clear_read();
	all_values();
	CHECK(g_read[1][0] && g_read[2][7] && g_read[3][6]);
	CHECK(g_read[4][4]);                                  /* SVE: ZFR0 read */
	CHECK(!g_read[4][5]);                                 /* no SME: SMFR0 not */
	/* EL1 AArch32 as well */
	g_fake[4][0] = 0x0000000000000022ull;
	clear_read();
	all_values();
	CHECK(g_read[1][0] && g_read[3][0]);
	CHECK(!g_read[4][4]);
}

static void t_unallocated_never_read(void)
{
	g_calls = 0;
	for (unsigned crm = 0; crm <= 9; crm++)
		for (unsigned op2 = 0; op2 < 8; op2++)
			if (!wel2_idreg_known(crm, op2))
				CHECK_EQ(wel2_idreg_value(fake_rd, 0, crm, op2), 0);
	CHECK_EQ(g_calls, 0);
}

int main(void)
{
	RUN(t_decode_group);
	RUN(t_known_table);
	RUN(t_gic_forced_rest_unchanged);
	RUN(t_pauth_fields_pass_through);
	RUN(t_not_read_when_absent);
	RUN(t_read_when_present);
	RUN(t_unallocated_never_read);
	TESTS_DONE();
}
