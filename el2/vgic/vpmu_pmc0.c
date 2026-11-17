// SPDX-License-Identifier: MIT
// Watari EL2 - virtual PMUv3 cycle counter on the M1's own cycle counter (PMC0). See include/wel2_vpmu_pmc0.h and
// docs/design/watari-el2/pmc0-vpmu.md. Written from the register facts listed in include/wel2_apple_sysreg.h only.
#include "wel2_vpmu_pmc0.h"

void wel2_pmc0_hw_init(const struct apple_sysreg_io *io, uint64_t (*wait)(void *), void *wctx, struct wel2_pmc0_init *r)
{
	uint64_t p0, p1;

	r->pmcr0_before = p0 = io->rd(io->ctx, APPLE_SR_PMCR0);
	r->pmcr1_before = p1 = io->rd(io->ctx, APPLE_SR_PMCR1);
	// Read-modify-write, only our bits ORed in, and only if something changes: IMODE / IACT (the FIQ module's bits) and every other bit stay.
	if ((p1 & (PMCR1_CTR0_EL0 | PMCR1_CTR0_EL1)) != (PMCR1_CTR0_EL0 | PMCR1_CTR0_EL1))
		io->wr(io->ctx, APPLE_SR_PMCR1, p1 | PMCR1_CTR0_EL0 | PMCR1_CTR0_EL1);
	if (!(p0 & PMCR0_CTR0_EN))
		io->wr(io->ctx, APPLE_SR_PMCR0, p0 | PMCR0_CTR0_EN);
	r->pmcr0_after = io->rd(io->ctx, APPLE_SR_PMCR0);
	r->pmcr1_after = io->rd(io->ctx, APPLE_SR_PMCR1);
	r->s0 = r->s1 = io->rd(io->ctx, APPLE_SR_PMC0);
	r->ticks = 0;
	if (!(r->pmcr0_after & PMCR0_CTR0_EN) ||
	    (r->pmcr1_after & (PMCR1_CTR0_EL0 | PMCR1_CTR0_EL1)) != (PMCR1_CTR0_EL0 | PMCR1_CTR0_EL1)) {
		r->state = WEL2_PMC0_FAILED;                 // the enables did not stick: the counter cannot be ours
		return;
	}
	r->ticks = wait ? wait(wctx) : 0;
	r->s1 = io->rd(io->ctx, APPLE_SR_PMC0);
	// The delay runs at EL2; whether PMC0 counts there is not known [hypothesis: the EL1 enable may not cover EL2]. A counter that did
	// not move is therefore PENDING, decided at the first guest read (the guest has run at EL1 / EL0 by then), not failed at once.
	r->state = r->s1 != r->s0 ? WEL2_PMC0_OK : WEL2_PMC0_PENDING;
}

uint64_t wel2_pmc0_per_ms(uint64_t delta, uint64_t ticks, uint64_t frq)
{
	if (!ticks || !frq)
		return 0;
	return (delta * (frq / 1000u)) / ticks;      // delta * (ticks per ms) / ticks; frq / 1000 is exact for 24 MHz
}

void wel2_vpmu_core_init(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, bool boot, uint64_t pmc0_now, enum wel2_pmc0_state st)
{
	uint64_t start = pmc0_now;

	if (!boot && sh->have_boot && !(sh->flags & WEL2_VPMU_NOSYNC))
		start = sh->pub0;
	c->state = (uint8_t)st;
	c->sample = pmc0_now;
	c->off = start - pmc0_now;
	c->last = c->frozen = start;
	c->running = !(sh->flags & WEL2_VPMU_STRICT) || (sh->flags & WEL2_VPMU_RUN);   // default: counting from the start
	c->synced = 0;
	if (boot || !sh->have_boot) {
		sh->have_boot = true;
		sh->pub0 = start;
	}
}

enum wel2_pmc0_state wel2_vpmu_resolve(struct wel2_vpmu_core *c, uint64_t pmc0_now)
{
	if (c->state == WEL2_PMC0_PENDING)
		c->state = pmc0_now != c->sample ? WEL2_PMC0_OK : WEL2_PMC0_FAILED;
	return (enum wel2_pmc0_state)c->state;
}

bool wel2_vpmu_running(const struct wel2_vpmu_sh *sh, const struct wel2_catch_cpu *g)
{
	if (!(sh->flags & WEL2_VPMU_STRICT) || (sh->flags & WEL2_VPMU_RUN))
		return true;
	return (g->pmcr & WEL2_PMCR_E) && (g->pmcnten & WEL2_PMU_CBIT);
}

static bool below(uint64_t a, uint64_t b)       // a < b, modulo 2^64
{
	return (int64_t)(a - b) < 0;
}

// Re-sync a core that was not running (resume) or has not read yet: never below its own last value, nor below the boot core's latest
// published value (sync) or the high-water mark (hiwater), so a thread that migrates here sees no big backwards step.
static void resync(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, bool boot, uint64_t pmc0_now)
{
	uint64_t v = pmc0_now + c->off, t = c->last;

	if (!(sh->flags & WEL2_VPMU_NOSYNC) && !boot && sh->have_boot && below(t, sh->pub0))
		t = sh->pub0;
	if ((sh->flags & WEL2_VPMU_HIWATER) && below(t, sh->hiwater))
		t = sh->hiwater;
	if (below(v, t))
		c->off += t - v;
}

// The visible value while running, with the per-core clamp (and the optional global high-water mark).
static uint64_t visible(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, bool boot, uint64_t pmc0_now)
{
	uint64_t v = pmc0_now + c->off;

	if (below(v, c->last)) {
		if (c->last - v > WEL2_PMC0_BACK_MAX) {  // PMC0 wrapped (48-bit?) or was reset (deep idle): continue from last + 1, do not stall
			c->off += c->last + 1 - v;
			v = c->last + 1;
		} else {
			v = c->last;                         // small step back: never below what this core already returned
		}
	}
	if (sh->flags & WEL2_VPMU_HIWATER) {
		if (below(v, sh->hiwater)) {
			c->off += sh->hiwater - v;           // a lagging core jumps forward to the mark (it never stalls)
			v = sh->hiwater;
		} else {
			sh->hiwater = v;
		}
	}
	c->last = v;
	if (boot)
		sh->pub0 = v;
	return v;
}

// Bring this core's view of the gate to `run` without a jump of the counter: freeze stores the value, resume continues from it.
static void gate(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, bool boot, uint64_t pmc0_now, bool run)
{
	if (run && !c->running) {
		c->off = c->frozen - pmc0_now;
		c->last = c->frozen;
		c->running = 1;
		resync(sh, c, boot, pmc0_now);
	} else if (!run && c->running) {
		c->frozen = visible(sh, c, boot, pmc0_now);
		c->running = 0;
	}
}

uint64_t wel2_vpmu_read(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, const struct wel2_catch_cpu *g, bool boot, uint64_t pmc0_now)
{
	gate(sh, c, boot, pmc0_now, wel2_vpmu_running(sh, g));
	if (c->running && !c->synced) {
		c->synced = 1;
		resync(sh, c, boot, pmc0_now);
	}
	return c->running ? visible(sh, c, boot, pmc0_now) : c->frozen;
}

static void set_value(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, uint64_t pmc0_now, uint64_t v)
{
	if (c->running)
		c->off = v - pmc0_now;
	else
		c->frozen = v;
	c->last = v;
	if (sh->flags & WEL2_VPMU_HIWATER)
		sh->hiwater = v;                         // an explicit write moves the mark with it (documented trade-off)
}

bool wel2_vpmu_is_ccntr(const struct wel2_sysreg_enc *e)
{
	return e->op0 == 3 && e->op1 == 3 && e->crn == 9 && e->crm == 13 && e->op2 == 0;
}

void wel2_vpmu_write(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, struct wel2_catch_cpu *g, const struct wel2_sysreg_enc *e,
		     uint64_t wval, uint64_t pmc0_now, uint64_t cnt, uint64_t frq)
{
	gate(sh, c, false, pmc0_now, wel2_vpmu_running(sh, g));   // bring this core up to date with the control state as it was
	wel2_catch_write(g, e, wval, cnt, frq);
	gate(sh, c, false, pmc0_now, wel2_vpmu_running(sh, g));   // PMCR.E / PMCNTENSET.C changed: freeze or resume
	if (wel2_vpmu_is_ccntr(e))
		set_value(sh, c, pmc0_now, wval);
	else if (e->op0 == 3 && e->op1 == 3 && e->crn == 9 && e->crm == 12 && e->op2 == 0 && (wval & WEL2_PMCR_C))
		set_value(sh, c, pmc0_now, 0);           // PMCR.C: reset the cycle counter (this core's offset only)
}

static void put_s(char *b, unsigned cap, unsigned *k, const char *s)
{
	while (*s && *k + 1 < cap)
		b[(*k)++] = *s++;
}

unsigned wel2_vpmu_src_format(char *buf, unsigned cap, enum wel2_pmc0_state st, bool wanted_pmc0, uint64_t per_ms)
{
	unsigned k = 0;

	if (!cap)
		return 0;
	if (st == WEL2_PMC0_OK || st == WEL2_PMC0_PENDING) {
		put_s(buf, cap, &k, st == WEL2_PMC0_OK ? "SRC=PMC0 D=" : "SRC=PMC0(PENDING) D=");
		if (per_ms) {
			char t[24];
			unsigned n = 0;

			while (per_ms && n < sizeof(t)) {
				t[n++] = (char)('0' + per_ms % 10u);
				per_ms /= 10u;
			}
			while (n && k + 1 < cap)
				buf[k++] = t[--n];
		} else {
			put_s(buf, cap, &k, "?");
		}
	} else {
		put_s(buf, cap, &k, wanted_pmc0 ? "SRC=TIMER(fallback)" : "SRC=TIMER");
	}
	buf[k] = 0;
	return k;
}
