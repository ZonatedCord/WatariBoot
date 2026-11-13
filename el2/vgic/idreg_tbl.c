// SPDX-License-Identifier: MIT
// Watari EL2 T6g (PLAN S12): decode table and value policy of the TID3 ID-register emulation (include/wel2_idreg.h).
// Written from the Arm ARM register index as remembered [hypothesis: the list below is from memory, not re-read; a
// register missing from it reads as zero in the guest, which for a feature the CPU lacks is the architectural value].

#include "wel2_idreg.h"

// One bit per op2 (0..7) for each CRm 1..7: the registers the layer reads from the hardware.
//   CRm 1: ID_PFR0, ID_PFR1, ID_DFR0, ID_AFR0, ID_MMFR0, ID_MMFR1, ID_MMFR2, ID_MMFR3
//   CRm 2: ID_ISAR0..ID_ISAR5 (op2 0..5), ID_MMFR4 (6), ID_ISAR6 (7)
//   CRm 3: MVFR0, MVFR1, MVFR2 (op2 0..2), op2 3 reserved, ID_PFR2 (4), ID_DFR1 (5), ID_MMFR5 (6), op2 7 reserved
//   CRm 4: ID_AA64PFR0 (0), ID_AA64PFR1 (1), ID_AA64ZFR0 (4), ID_AA64SMFR0 (5)
//   CRm 5: ID_AA64DFR0 (0), ID_AA64DFR1 (1), ID_AA64AFR0 (4), ID_AA64AFR1 (5)
//   CRm 6: ID_AA64ISAR0 (0), ID_AA64ISAR1 (1), ID_AA64ISAR2 (2)
//   CRm 7: ID_AA64MMFR0 (0), ID_AA64MMFR1 (1), ID_AA64MMFR2 (2)
// Registers added by newer architecture revisions (ID_AA64PFR2, ID_AA64ISAR3, ID_AA64MMFR3/4, ...) are not listed:
// the M1 (Armv8.5-A class) has none of them [hypothesis], and the layer does not execute an `mrs` it is not sure of.
static const uint8_t g_known[8] = {
	0x00,       // CRm 0: not in the group
	0xff,       // CRm 1
	0xff,       // CRm 2
	0x77,       // CRm 3: op2 0-2, 4-6
	0x33,       // CRm 4: op2 0, 1, 4, 5
	0x33,       // CRm 5: op2 0, 1, 4, 5
	0x07,       // CRm 6: op2 0-2
	0x07,       // CRm 7: op2 0-2
};

bool wel2_idreg_decode(uint32_t iss, struct wel2_idreg_acc *out)
{
	uint32_t op0 = (iss >> 20) & 3, op2 = (iss >> 17) & 7, op1 = (iss >> 14) & 7;
	uint32_t crn = (iss >> 10) & 15, rt = (iss >> 5) & 31, crm = (iss >> 1) & 15;

	if (!(iss & 1))                 // Direction 0 = MSR: the ID group is read only, TID3 traps reads
		return false;
	if (op0 != 3 || op1 != 0 || crn != 0 || crm < WEL2_IDREG_CRM_FIRST || crm > WEL2_IDREG_CRM_LAST)
		return false;
	out->crm = (uint8_t)crm;
	out->op2 = (uint8_t)op2;
	out->rt = (uint8_t)rt;
	return true;
}

bool wel2_idreg_known(unsigned crm, unsigned op2)
{
	return crm >= WEL2_IDREG_CRM_FIRST && crm <= WEL2_IDREG_CRM_LAST && op2 < 8 && ((g_known[crm] >> op2) & 1);
}

unsigned wel2_idreg_count(void)
{
	unsigned n = 0;

	for (unsigned crm = WEL2_IDREG_CRM_FIRST; crm <= WEL2_IDREG_CRM_LAST; crm++)
		for (unsigned op2 = 0; op2 < 8; op2++)
			n += (g_known[crm] >> op2) & 1;
	return n;
}

bool wel2_idreg_nth(unsigned n, unsigned *crm, unsigned *op2)
{
	unsigned seen = 0;

	// One flat walk over (crm, op2) = 8 * crm + op2: simple enough that the optimiser keeps to a counted loop.
	for (unsigned i = 8 * WEL2_IDREG_CRM_FIRST; i < 8 * (WEL2_IDREG_CRM_LAST + 1); i++) {
		if (!wel2_idreg_known(i >> 3, i & 7))
			continue;
		if (seen == n) {
			*crm = i >> 3;
			*op2 = i & 7;
			return true;
		}
		seen++;
	}
	return false;
}

uint64_t wel2_idreg_value(wel2_idreg_read_fn rd, void *ctx, unsigned crm, unsigned op2)
{
	uint64_t v;

	if (!wel2_idreg_known(crm, op2))
		return 0;
	v = rd(ctx, crm, op2);
	if (crm == WEL2_IDREG_PFR0_CRM && op2 == WEL2_IDREG_PFR0_OP2)
		v = (v & ~WEL2_IDREG_GIC_MASK) | (1ULL << WEL2_IDREG_GIC_SHIFT);
	return v;
}
