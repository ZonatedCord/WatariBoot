// SPDX-License-Identifier: MIT
// Watari EL2: catch mode, the pure part (include/wel2_sysreg_catch.h). Written from the Arm ARM system register
// encodings (D24.2 ESR_EL2 ISS for EC 0x18; the register encodings of the PMU, debug and ID chapters), from memory:
// every encoding below is [hypothesis] until compared with a copy of the Arm ARM.
#include "wel2_sysreg_catch.h"

bool wel2_catch_decode(uint32_t iss, struct wel2_sysreg_enc *e)
{
	uint32_t op0 = (iss >> 20) & 3u;

	if (op0 < 2u)                                  /* SYS / SYSL (op0 1): never ours */
		return false;
	e->op0 = (uint8_t)op0;
	e->op2 = (uint8_t)((iss >> 17) & 7u);
	e->op1 = (uint8_t)((iss >> 14) & 7u);
	e->crn = (uint8_t)((iss >> 10) & 15u);
	e->rt = (uint8_t)((iss >> 5) & 31u);
	e->crm = (uint8_t)((iss >> 1) & 15u);
	e->rd = (uint8_t)(iss & 1u);
	return true;
}

enum wel2_catch_class wel2_catch_classify(const struct wel2_sysreg_enc *e)
{
	if (e->op0 == 2)
		return WEL2_CATCH_DEBUG;
	if (e->op0 != 3)
		return WEL2_CATCH_NONE;
	if (e->op1 == 0 && e->crn == 1 && e->crm == 0 && e->op2 == 1)
		return WEL2_CATCH_ACTLR;
	if (e->crn == 11 || e->crn == 15)
		return WEL2_CATCH_IMPDEF;
	/* PMINTENSET/CLR_EL1, PMMIR_EL1: op1 0, CRn 9, CRm 14; PMCR..PMOVSSET_EL0: op1 3, CRn 9, CRm 12..14 */
	if (e->crn == 9 && ((e->op1 == 0 && e->crm == 14) || (e->op1 == 3 && e->crm >= 12 && e->crm <= 14)))
		return WEL2_CATCH_PMU;
	/* PMEVCNTR<n>_EL0, PMEVTYPER<n>_EL0, PMCCFILTR_EL0: op1 3, CRn 14, CRm 8..15 */
	if (e->crn == 14 && e->op1 == 3 && e->crm >= 8)
		return WEL2_CATCH_PMU;
	return WEL2_CATCH_NONE;
}

// ---- names ----------------------------------------------------------------------------------------------------------
static void put_s(char *buf, unsigned cap, unsigned *n, const char *s)
{
	while (*s && *n + 1u < cap)
		buf[(*n)++] = *s++;
}

static void put_dec(char *buf, unsigned cap, unsigned *n, uint64_t v)
{
	char tmp[20];
	unsigned k = 0;

	do {
		tmp[k++] = (char)('0' + v % 10u);
		v /= 10u;
	} while (v && k < sizeof(tmp));
	while (k && *n + 1u < cap)
		buf[(*n)++] = tmp[--k];
}

static void put_hex(char *buf, unsigned cap, unsigned *n, uint64_t v)
{
	char tmp[16];
	unsigned k = 0;

	put_s(buf, cap, n, "0x");
	do {
		tmp[k++] = "0123456789abcdef"[v & 15u];
		v >>= 4;
	} while (v && k < sizeof(tmp));
	while (k && *n + 1u < cap)
		buf[(*n)++] = tmp[--k];
}

struct fixed_name {
	uint8_t op0, op1, crn, crm, op2;
	const char *name;
};

static const struct fixed_name fixed[] = {
	{ 3, 0, 1, 0, 1, "ACTLR_EL1" },
	{ 3, 0, 9, 14, 1, "PMINTENSET_EL1" }, { 3, 0, 9, 14, 2, "PMINTENCLR_EL1" }, { 3, 0, 9, 14, 6, "PMMIR_EL1" },
	{ 3, 3, 9, 12, 0, "PMCR_EL0" },       { 3, 3, 9, 12, 1, "PMCNTENSET_EL0" }, { 3, 3, 9, 12, 2, "PMCNTENCLR_EL0" },
	{ 3, 3, 9, 12, 3, "PMOVSCLR_EL0" },   { 3, 3, 9, 12, 4, "PMSWINC_EL0" },     { 3, 3, 9, 12, 5, "PMSELR_EL0" },
	{ 3, 3, 9, 12, 6, "PMCEID0_EL0" },    { 3, 3, 9, 12, 7, "PMCEID1_EL0" },
	{ 3, 3, 9, 13, 0, "PMCCNTR_EL0" },    { 3, 3, 9, 13, 1, "PMXEVTYPER_EL0" }, { 3, 3, 9, 13, 2, "PMXEVCNTR_EL0" },
	{ 3, 3, 9, 14, 0, "PMUSERENR_EL0" },  { 3, 3, 9, 14, 3, "PMOVSSET_EL0" },
	{ 3, 3, 14, 15, 7, "PMCCFILTR_EL0" },
	{ 2, 0, 0, 2, 0, "MDCCINT_EL1" },     { 2, 0, 0, 2, 2, "MDSCR_EL1" },       { 2, 0, 0, 0, 2, "OSDTRRX_EL1" },
	{ 2, 0, 0, 3, 2, "OSDTRTX_EL1" },     { 2, 0, 0, 6, 2, "OSECCR_EL1" },
	{ 2, 0, 1, 0, 0, "MDRAR_EL1" },       { 2, 0, 1, 0, 4, "OSLAR_EL1" },       { 2, 0, 1, 1, 4, "OSLSR_EL1" },
	{ 2, 0, 1, 3, 4, "OSDLR_EL1" },       { 2, 0, 1, 4, 4, "DBGPRCR_EL1" },
	{ 2, 0, 7, 8, 6, "DBGCLAIMSET_EL1" }, { 2, 0, 7, 9, 6, "DBGCLAIMCLR_EL1" }, { 2, 0, 7, 14, 6, "DBGAUTHSTATUS_EL1" },
	{ 2, 3, 0, 1, 0, "MDCCSR_EL0" },      { 2, 3, 0, 4, 0, "DBGDTR_EL0" },      { 2, 3, 0, 5, 0, "DBGDTRRX_EL0" },
};

unsigned wel2_catch_name(const struct wel2_sysreg_enc *e, char *buf, unsigned cap)
{
	unsigned n = 0;

	if (!cap)
		return 0;
	for (unsigned i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
		const struct fixed_name *f = &fixed[i];

		if (f->op0 == e->op0 && f->op1 == e->op1 && f->crn == e->crn && f->crm == e->crm && f->op2 == e->op2) {
			put_s(buf, cap, &n, f->name);
			buf[n] = 0;
			return n;
		}
	}
	if (e->op0 == 2 && e->op1 == 0 && e->crn == 0 && e->op2 >= 4) {            /* DBGBVR/BCR/WVR/WCR<n>_EL1, n = CRm */
		static const char *const pre[4] = { "DBGBVR", "DBGBCR", "DBGWVR", "DBGWCR" };

		put_s(buf, cap, &n, pre[e->op2 - 4]);
		put_dec(buf, cap, &n, e->crm);
		put_s(buf, cap, &n, "_EL1");
	} else if (e->op0 == 3 && e->op1 == 3 && e->crn == 14 && e->crm >= 8 && e->crm <= 11) {
		put_s(buf, cap, &n, "PMEVCNTR");
		put_dec(buf, cap, &n, (unsigned)(e->crm - 8) * 8u + e->op2);
		put_s(buf, cap, &n, "_EL0");
	} else if (e->op0 == 3 && e->op1 == 3 && e->crn == 14 && e->crm >= 12 && e->crm <= 14) {
		put_s(buf, cap, &n, "PMEVTYPER");
		put_dec(buf, cap, &n, (unsigned)(e->crm - 12) * 8u + e->op2);
		put_s(buf, cap, &n, "_EL0");
	}
	buf[n] = 0;
	return n;
}

uint64_t wel2_catch_value(const struct wel2_sysreg_enc *e)
{
	/* OSLSR_EL1: OSLM = 0b10 (OSLM[1] = bit 3, OSLM[0] = bit 0): OS Lock implemented; OSLK (bit 1) = 0: unlocked; nTT = 0.
	 * A guest that reads 0 here would believe there is no OS Lock; one that reads OSLK = 1 would wait for an unlock. */
	if (e->op0 == 2 && e->op1 == 0 && e->crn == 1 && e->crm == 1 && e->op2 == 4)
		return 8;
	return 0;
}

// ---- the table ------------------------------------------------------------------------------------------------------
enum wel2_catch_rec wel2_catch_record(struct wel2_catch_tab *t, const struct wel2_sysreg_enc *e, uint64_t pc,
				      unsigned core, unsigned *idx)
{
	t->total++;
	for (unsigned i = 0; i < t->n; i++) {
		const struct wel2_sysreg_enc *k = &t->e[i].enc;

		if (k->op0 == e->op0 && k->op1 == e->op1 && k->crn == e->crn && k->crm == e->crm && k->op2 == e->op2 &&
		    k->rd == e->rd) {
			t->e[i].count++;
			return WEL2_CATCH_REC_OLD;
		}
	}
	if (t->n >= WEL2_CATCH_TAB_N) {
		t->overflow++;
		return WEL2_CATCH_REC_OVERFLOW;
	}
	t->e[t->n] = (struct wel2_catch_ent){ .enc = *e, .core = (uint8_t)core, .count = 1, .first_pc = pc };
	if (idx)
		*idx = t->n;
	t->n++;
	return WEL2_CATCH_REC_NEW;
}

unsigned wel2_catch_format(char *buf, unsigned cap, const char *prefix, const struct wel2_sysreg_enc *e, uint64_t pc,
			   unsigned core, uint32_t count)
{
	char nm[WEL2_CATCH_NAME_MAX];
	unsigned n = 0;

	if (!cap)
		return 0;
	put_s(buf, cap, &n, prefix);
	put_s(buf, cap, &n, e->rd ? " R S" : " W S");
	put_dec(buf, cap, &n, e->op0);
	put_s(buf, cap, &n, "_");
	put_dec(buf, cap, &n, e->op1);
	put_s(buf, cap, &n, "_C");
	put_dec(buf, cap, &n, e->crn);
	put_s(buf, cap, &n, "_C");
	put_dec(buf, cap, &n, e->crm);
	put_s(buf, cap, &n, "_");
	put_dec(buf, cap, &n, e->op2);
	if (wel2_catch_name(e, nm, sizeof(nm))) {
		put_s(buf, cap, &n, " ");
		put_s(buf, cap, &n, nm);
	}
	put_s(buf, cap, &n, " PC=");
	put_hex(buf, cap, &n, pc);
	put_s(buf, cap, &n, " core=");
	put_dec(buf, cap, &n, core);
	put_s(buf, cap, &n, " count=");
	put_dec(buf, cap, &n, count);
	buf[n] = 0;
	return n;
}

unsigned wel2_catch_format_summary(char *buf, unsigned cap, const struct wel2_catch_tab *t)
{
	unsigned n = 0;

	if (!cap)
		return 0;
	put_s(buf, cap, &n, "CATCH SUMMARY distinct=");
	put_dec(buf, cap, &n, t->n);
	put_s(buf, cap, &n, " total=");
	put_dec(buf, cap, &n, t->total);
	put_s(buf, cap, &n, " overflow=");
	put_dec(buf, cap, &n, t->overflow);
	buf[n] = 0;
	return n;
}
