// SPDX-License-Identifier: MIT
// Watari EL2: catch mode, the pure part (include/wel2_sysreg_catch.h). Written from the Arm ARM system register
// encodings (D24.2 ESR_EL2 ISS for EC 0x18; the register encodings of the PMU, debug and ID chapters), from memory:
// every encoding below is [hypothesis] until compared with a copy of the Arm ARM.
#include "wel2_sysreg_catch.h"

bool wel2_catch_decode(uint32_t iss, struct wel2_sysreg_enc *e)
{
	uint32_t op0 = (iss >> 20) & 3u;

	uint32_t crn = (iss >> 10) & 15u;

	/* SYS / SYSL (op0 1) are never ours (TLBI, DC, IC, AT, ...) except the IMPLEMENTATION DEFINED space CRn 11 / 15 (TIDCP) */
	if (op0 < 2u && !(op0 == 1u && (crn == 11u || crn == 15u)))
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
	if (e->op0 == 1)
		return (e->crn == 11 || e->crn == 15) ? WEL2_CATCH_IMPDEF : WEL2_CATCH_NONE;
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

// ---- groups ---------------------------------------------------------------------------------------------------------
uint32_t wel2_catch_group(enum wel2_catch_class c)
{
	switch (c) {
	case WEL2_CATCH_PMU:    return WEL2_CATCH_G_PMU;
	case WEL2_CATCH_DEBUG:  return WEL2_CATCH_G_DBG;
	case WEL2_CATCH_IMPDEF: return WEL2_CATCH_G_IMPDEF;
	case WEL2_CATCH_ACTLR:  return WEL2_CATCH_G_ACTLR;
	default:                return 0;
	}
}

bool wel2_catch_enabled(uint32_t mask, enum wel2_catch_class c)
{
	return (mask & wel2_catch_group(c)) != 0;
}

uint64_t wel2_catch_mdcr_bits(uint32_t mask)
{
	uint64_t b = 0;

	if (mask & WEL2_CATCH_G_PMU)
		b |= WEL2_MDCR_TPM | WEL2_MDCR_TPMCR;
	if (mask & WEL2_CATCH_G_DBG)
		b |= WEL2_MDCR_TDA | WEL2_MDCR_TDOSA | WEL2_MDCR_TDRA;
	return b;
}

uint64_t wel2_catch_hcr_bits(uint32_t mask)
{
	return ((mask & WEL2_CATCH_G_IMPDEF) ? WEL2_HCR_TIDCP : 0) | ((mask & WEL2_CATCH_G_ACTLR) ? WEL2_HCR_TACR : 0);
}

static bool tok_is(const char *p, unsigned n, const char *w)
{
	unsigned i = 0;

	while (w[i] && i < n && p[i] == w[i])
		i++;
	return !w[i] && i == n;
}

bool wel2_catch_parse(const char *val, uint32_t *mask, const char **end)
{
	const char *p = val;
	uint32_t m = 0;
	bool ok = true;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		unsigned nd = 0;

		p += 2;
		for (;; p++, nd++) {
			char c = *p;
			uint32_t d;

			if (c >= '0' && c <= '9')
				d = (uint32_t)(c - '0');
			else if (c >= 'a' && c <= 'f')
				d = (uint32_t)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				d = (uint32_t)(c - 'A' + 10);
			else
				break;
			m = (m << 4) | d;
		}
		ok = nd > 0 && nd <= 8 && (*p == 0 || *p == ' ');
		m &= WEL2_CATCH_G_ALL;
	} else {
		for (;;) {
			unsigned n = 0;

			while (p[n] && p[n] != ' ' && p[n] != ',')
				n++;
			if (tok_is(p, n, "0") || tok_is(p, n, "none"))
				m |= 0;
			else if (tok_is(p, n, "1") || tok_is(p, n, "all"))
				m |= WEL2_CATCH_G_ALL;
			else if (tok_is(p, n, "pmu"))
				m |= WEL2_CATCH_G_PMU;
			else if (tok_is(p, n, "dbg") || tok_is(p, n, "debug"))
				m |= WEL2_CATCH_G_DBG;
			else if (tok_is(p, n, "impdef"))
				m |= WEL2_CATCH_G_IMPDEF;
			else if (tok_is(p, n, "actlr"))
				m |= WEL2_CATCH_G_ACTLR;
			else {
				ok = false;
				break;
			}
			p += n;
			if (*p != ',')
				break;
			p++;
		}
	}
	if (end)
		*end = p;
	if (!ok)
		return false;
	*mask = m;
	return true;
}

unsigned wel2_catch_groups_str(char *buf, unsigned cap, uint32_t mask)
{
	static const struct { uint32_t bit; const char *name; } g[] = {
		{ WEL2_CATCH_G_PMU, "pmu" }, { WEL2_CATCH_G_DBG, "dbg" }, { WEL2_CATCH_G_IMPDEF, "impdef" }, { WEL2_CATCH_G_ACTLR, "actlr" },
	};
	unsigned n = 0;

	if (!cap)
		return 0;
	for (unsigned i = 0; i < 4; i++) {
		if (!(mask & g[i].bit))
			continue;
		if (n && n + 1u < cap)
			buf[n++] = ',';
		for (const char *s = g[i].name; *s && n + 1u < cap; s++)
			buf[n++] = *s;
	}
	if (!n)
		for (const char *s = "none"; *s && n + 1u < cap; s++)
			buf[n++] = *s;
	buf[n] = 0;
	return n;
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
	if (e->op0 == 1) {                                                       /* SYS / SYSL, CRn 11 / 15 */
		put_s(buf, cap, &n, "SYS-IMPDEF");
		buf[n] = 0;
		return n;
	}
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

// wel2.vpmu=: comma list of percore, global, nouser, frq1, n6 (see wel2_sysreg_catch.h). Default (empty) = global.
bool wel2_vpmu_parse(const char *val, uint32_t *flags, const char **end)
{
	const char *p = val;
	uint32_t f = 0;
	bool ok = true, mode = false;

	for (;;) {
		unsigned n = 0;

		while (p[n] && p[n] != ' ' && p[n] != ',')
			n++;
		if (tok_is(p, n, "global")) {
			f |= WEL2_VPMU_GLOBAL;
			mode = true;
		} else if (tok_is(p, n, "percore")) {
			f &= ~WEL2_VPMU_GLOBAL;
			mode = true;
		} else if (tok_is(p, n, "nouser")) {
			f |= WEL2_VPMU_NOUSER;
		} else if (tok_is(p, n, "frq1")) {
			f |= WEL2_VPMU_FRQ1;
		} else if (tok_is(p, n, "undef")) {
			f |= WEL2_VPMU_UNDEF;
		} else if (tok_is(p, n, "uall")) {
			f |= WEL2_VPMU_UALL;
		} else if (tok_is(p, n, "src=pmc0")) {
			f = (f & ~WEL2_VPMU_SRC_TIMER) | WEL2_VPMU_SRC_PMC0;
		} else if (tok_is(p, n, "src=timer")) {
			f = (f & ~WEL2_VPMU_SRC_PMC0) | WEL2_VPMU_SRC_TIMER;
		} else if (tok_is(p, n, "nosync")) {
			f |= WEL2_VPMU_NOSYNC;
		} else if (tok_is(p, n, "sync")) {
			f &= ~WEL2_VPMU_NOSYNC;
		} else if (tok_is(p, n, "hiwater")) {
			f |= WEL2_VPMU_HIWATER;
		} else if (tok_is(p, n, "run")) {
			f |= WEL2_VPMU_RUN;
		} else if (tok_is(p, n, "strict")) {
			f |= WEL2_VPMU_STRICT;
		} else if (tok_is(p, n, "n6")) {
			f |= WEL2_VPMU_N6;
		} else {
			ok = false;
			break;
		}
		p += n;
		if (*p != ',')
			break;
		p++;
	}
	if (end)
		*end = p;
	if (!ok)
		return false;
	if (!mode)
		f |= WEL2_VPMU_GLOBAL;
	*flags = f;
	return true;
}

// ---- virtual PMUv3 (cycle counter only) -----------------------------------------------------------------------------
static bool pmu_running(const struct wel2_catch_cpu *c)
{
	if (c->vflags & WEL2_VPMU_GLOBAL)       /* the global counter always runs: it is a clock, whatever PMCR / PMCNTENSET say */
		return true;
	return (c->pmcr & WEL2_PMCR_E) && (c->pmcnten & WEL2_PMU_CBIT);
}

// cnt * 1e9 / frq without 128-bit division (freestanding): q * 1e9 + (r * 1e9) / frq, r < frq so r * 1e9 fits for frq < 1.8e10.
static uint64_t pmu_scaled(const struct wel2_catch_cpu *c, uint64_t cnt, uint64_t frq)
{
	uint64_t q, r, v;

	if (c->vflags & WEL2_VPMU_FRQ1)          /* 1:1 with CNTPCT_EL0 (no PMCR.D divide either) */
		return cnt;
	if (!frq)
		return 0;
	q = cnt / frq;
	r = cnt % frq;
	v = q * 1000000000ull + (r * 1000000000ull) / frq;
	return ((c->pmcr & WEL2_PMCR_D) && !(c->vflags & WEL2_VPMU_GLOBAL)) ? v / 64u : v;
}

uint64_t wel2_catch_pmccntr(const struct wel2_catch_cpu *c, uint64_t cnt, uint64_t frq)
{
	return pmu_running(c) ? c->pmu_base + pmu_scaled(c, cnt, frq) : c->pmu_frozen;
}

// Set the counter to v keeping the running / frozen state of c as it is now.
static void pmu_set(struct wel2_catch_cpu *c, uint64_t v, uint64_t cnt, uint64_t frq)
{
	if (c->vflags & WEL2_VPMU_GLOBAL)       /* global: PMCCNTR = scaled(CNTPCT), writes (PMCCNTR, PMCR.C) are ignored: monotonic everywhere */
		return;
	c->pmu_frozen = v;
	c->pmu_base = v - pmu_scaled(c, cnt, frq);
}

// Change PMCR / PMCNTENSET (via fn) without a jump of the counter: the value before is the value after.
static void pmu_ctl(struct wel2_catch_cpu *c, uint32_t pmcr, uint32_t pmcnten, uint64_t cnt, uint64_t frq)
{
	uint64_t cur = wel2_catch_pmccntr(c, cnt, frq);

	c->pmcr = pmcr;
	c->pmcnten = pmcnten;
	pmu_set(c, cur, cnt, frq);
}

static bool is_pmreg(const struct wel2_sysreg_enc *e, unsigned op1, unsigned crm, unsigned op2)
{
	return e->op0 == 3 && e->op1 == op1 && e->crn == 9 && e->crm == crm && e->op2 == op2;
}

uint64_t wel2_catch_read(const struct wel2_catch_cpu *c, const struct wel2_sysreg_enc *e, uint64_t cnt, uint64_t frq)
{
	if (e->op0 == 2 && e->op1 == 0 && e->crn == 1 && e->crm == 1 && e->op2 == 4)
		return 8u | ((uint64_t)(c->oslk & 1u) << 1);
	if (is_pmreg(e, 3, 12, 0))
		return (c->pmcr & WEL2_PMCR_KEEP) | ((c->vflags & WEL2_VPMU_N6) ? 6u << 11 : 0);
	if (is_pmreg(e, 3, 12, 1) || is_pmreg(e, 3, 12, 2))
		return c->pmcnten;
	if (is_pmreg(e, 3, 12, 3) || is_pmreg(e, 3, 14, 3))
		return c->pmovs;
	if (is_pmreg(e, 0, 14, 1) || is_pmreg(e, 0, 14, 2))
		return c->pminten;
	if (is_pmreg(e, 3, 12, 5))
		return c->pmselr;
	if (is_pmreg(e, 3, 13, 0))
		return wel2_catch_pmccntr(c, cnt, frq);
	if (is_pmreg(e, 3, 14, 0))
		return (c->vflags & WEL2_VPMU_NOUSER) ? 0 : c->pmuserenr;
	if (e->op0 == 3 && e->op1 == 3 && e->crn == 14 && e->crm == 15 && e->op2 == 7)
		return c->pmccfiltr;
	return wel2_catch_value(e);
}

void wel2_catch_write(struct wel2_catch_cpu *c, const struct wel2_sysreg_enc *e, uint64_t wval, uint64_t cnt, uint64_t frq)
{
	if (e->op0 == 2 && e->op1 == 0 && e->crn == 1 && e->crm == 0 && e->op2 == 4) {   /* OSLAR_EL1 */
		c->oslk = (uint8_t)(wval & 1u);
		return;
	}
	if (is_pmreg(e, 3, 12, 0)) {                                                    /* PMCR_EL0 */
		pmu_ctl(c, (uint32_t)wval & WEL2_PMCR_KEEP, c->pmcnten, cnt, frq);
		if (wval & WEL2_PMCR_C)                                                      /* C: reset the cycle counter */
			pmu_set(c, 0, cnt, frq);
	} else if (is_pmreg(e, 3, 12, 1)) {                                             /* PMCNTENSET_EL0 */
		pmu_ctl(c, c->pmcr, c->pmcnten | ((uint32_t)wval & WEL2_PMU_CBIT), cnt, frq);
	} else if (is_pmreg(e, 3, 12, 2)) {                                             /* PMCNTENCLR_EL0 */
		pmu_ctl(c, c->pmcr, c->pmcnten & ~((uint32_t)wval & WEL2_PMU_CBIT), cnt, frq);
	} else if (is_pmreg(e, 3, 12, 3)) {                                             /* PMOVSCLR_EL0 */
		c->pmovs &= ~((uint32_t)wval & WEL2_PMU_CBIT);
	} else if (is_pmreg(e, 3, 14, 3)) {                                             /* PMOVSSET_EL0 */
		c->pmovs |= (uint32_t)wval & WEL2_PMU_CBIT;
	} else if (is_pmreg(e, 0, 14, 1)) {                                             /* PMINTENSET_EL1 */
		c->pminten |= (uint32_t)wval & WEL2_PMU_CBIT;
	} else if (is_pmreg(e, 0, 14, 2)) {                                             /* PMINTENCLR_EL1 */
		c->pminten &= ~((uint32_t)wval & WEL2_PMU_CBIT);
	} else if (is_pmreg(e, 3, 12, 5)) {                                             /* PMSELR_EL0 */
		c->pmselr = (uint32_t)wval & 31u;
	} else if (is_pmreg(e, 3, 13, 0)) {                                             /* PMCCNTR_EL0 */
		pmu_set(c, wval, cnt, frq);
	} else if (is_pmreg(e, 3, 14, 0)) {                                             /* PMUSERENR_EL0 */
		if (!(c->vflags & WEL2_VPMU_NOUSER))
			c->pmuserenr = (uint32_t)wval & 15u;
	} else if (e->op0 == 3 && e->op1 == 3 && e->crn == 14 && e->crm == 15 && e->op2 == 7) {   /* PMCCFILTR_EL0 */
		c->pmccfiltr = wval;
	}
	/* PMSWINC, PMXEVTYPER, PMXEVCNTR, PMEVCNTR<n>, PMEVTYPER<n>: N = 0, write ignored */
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
		uint32_t key = (uint32_t)e->op0 | (uint32_t)e->op1 << 2 | (uint32_t)e->crn << 5 | (uint32_t)e->crm << 9 |
			       (uint32_t)e->op2 << 13 | (uint32_t)e->rd << 16;

		t->overflow++;
		for (unsigned i = 0; i < t->ovf_n; i++)
			if (t->ovf_key[i] == key)
				return WEL2_CATCH_REC_OVERFLOW_OLD;
		if (t->ovf_n >= WEL2_CATCH_OVF_LINES) {
			if (t->ovf_capped)
				return WEL2_CATCH_REC_OVERFLOW_OLD;
			t->ovf_capped = true;
			return WEL2_CATCH_REC_OVERFLOW_CAP;
		}
		t->ovf_key[t->ovf_n++] = key;
		return WEL2_CATCH_REC_OVERFLOW;
	}
	t->e[t->n] = (struct wel2_catch_ent){ .enc = *e, .core = (uint8_t)core, .count = 1, .first_pc = pc };
	if (idx)
		*idx = t->n;
	t->n++;
	return WEL2_CATCH_REC_NEW;
}

static unsigned format_common(char *buf, unsigned cap, const char *prefix, const struct wel2_sysreg_enc *e, uint64_t pc,
			      unsigned core, uint32_t count, bool overflow)
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
	if (overflow)
		put_s(buf, cap, &n, "overflow");
	else
		put_dec(buf, cap, &n, count);
	buf[n] = 0;
	return n;
}

unsigned wel2_catch_format(char *buf, unsigned cap, const char *prefix, const struct wel2_sysreg_enc *e, uint64_t pc,
			   unsigned core, uint32_t count)
{
	return format_common(buf, cap, prefix, e, pc, core, count, false);
}

unsigned wel2_catch_format_overflow(char *buf, unsigned cap, const struct wel2_sysreg_enc *e, uint64_t pc, unsigned core)
{
	return format_common(buf, cap, "CATCH", e, pc, core, 0, true);
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

// ---- HACR_EL2[56] routing (EC 0x3F) ------------------------------------------------------------------------------------
// [hypothesis] An EC 0x3F exception from a lower EL (HACR_EL2[56]) is an MSR/MRS of 32 bits (ESR.IL = 1) and AFSR1_EL2[24:0] holds its ISS.
// AFSR1_EL2 is a firmware-owned auxiliary register that may be stale, so its low 25 bits are not trusted blindly:
//   - ESR.IL must be 1 (an MSR/MRS is a 32-bit instruction);
//   - AFSR1 bits [63:25] must be 0 (a real ISS is 25 bits wide; leftovers usually are not);
//   - AFSR1[24:0] must decode to an encoding this module owns;
//   - if ESR[24:0] ALSO decodes to an owned encoding and it differs from AFSR1's (op, CRn, CRm, Rt, direction), one of them is stale and
//     the architectural ESR wins.
// Anything else decodes to NONE: the caller reflects the exception to the guest as an UNDEF (its behaviour before HACR).
static bool same_enc(const struct wel2_sysreg_enc *a, const struct wel2_sysreg_enc *b)
{
	return a->op0 == b->op0 && a->op1 == b->op1 && a->crn == b->crn && a->crm == b->crm && a->op2 == b->op2 && a->rt == b->rt &&
	       a->rd == b->rd;
}

enum wel2_ec3f_src wel2_catch_ec3f_decode(uint64_t esr, uint64_t afsr1, struct wel2_sysreg_enc *e)
{
	struct wel2_sysreg_enc ta, te;
	bool a_ok, e_ok;

	if (!(esr & (1ULL << 25)))
		return WEL2_EC3F_NONE;
	a_ok = !(afsr1 >> 25) && wel2_catch_decode((uint32_t)(afsr1 & WEL2_AFSR1_ISS), &ta) && wel2_catch_classify(&ta) != WEL2_CATCH_NONE;
	e_ok = wel2_catch_decode((uint32_t)(esr & WEL2_AFSR1_ISS), &te) && wel2_catch_classify(&te) != WEL2_CATCH_NONE;
	if (a_ok && (!e_ok || same_enc(&ta, &te))) {
		*e = ta;
		return WEL2_EC3F_AFSR1;
	}
	if (e_ok) {
		*e = te;
		return WEL2_EC3F_ESR;
	}
	return WEL2_EC3F_NONE;
}

bool wel2_hacr_parse(const char *val, uint64_t *mask, const char **end)
{
	const char *p = val;
	uint64_t m = 0;
	unsigned nd = 0;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	for (;; p++, nd++) {
		char c = *p;
		uint64_t d;

		if (c >= '0' && c <= '9')
			d = (uint64_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			d = (uint64_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			d = (uint64_t)(c - 'A' + 10);
		else
			break;
		if (nd >= 16)
			break;
		m = (m << 4) | d;
	}
	if (end)
		*end = p;
	if (nd == 0 || nd > 16 || (*p != 0 && *p != ' '))
		return false;
	*mask = m;
	return true;
}

unsigned wel2_catch_status_format(char *buf, unsigned cap, uint64_t mdcr_stuck, uint64_t hacr, uint64_t n,
				  const struct wel2_sysreg_enc *last)
{
	char nm[WEL2_CATCH_NAME_MAX];
	unsigned k = 0;

	if (!cap)
		return 0;
	put_s(buf, cap, &k, "MDCR STUCK=");
	put_hex(buf, cap, &k, mdcr_stuck);
	put_s(buf, cap, &k, " HACR=");
	put_hex(buf, cap, &k, hacr);
	put_s(buf, cap, &k, " CATCH N=");
	put_dec(buf, cap, &k, n);
	put_s(buf, cap, &k, " LAST=");
	if (!last) {
		put_s(buf, cap, &k, "NONE");
	} else if (wel2_catch_name(last, nm, sizeof(nm))) {
		put_s(buf, cap, &k, nm);
	} else {
		put_s(buf, cap, &k, "S");
		put_dec(buf, cap, &k, last->op0);
		put_s(buf, cap, &k, "_");
		put_dec(buf, cap, &k, last->op1);
		put_s(buf, cap, &k, "_C");
		put_dec(buf, cap, &k, last->crn);
		put_s(buf, cap, &k, "_C");
		put_dec(buf, cap, &k, last->crm);
		put_s(buf, cap, &k, "_");
		put_dec(buf, cap, &k, last->op2);
	}
	buf[k] = 0;
	return k;
}
