/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - pirq-guard. See include/wel2_pirq_guard.h. The numbers (budget, window,
 * cool-down) are the caller's policy; nothing here depends on a particular backend.
 */
#include "wel2_pirq_guard.h"

#define BIT_GET(a, i)  (((a)[(i) / 32] >> ((i) % 32)) & 1u)
#define BIT_SET(a, i)  ((a)[(i) / 32] |= 1u << ((i) % 32))
#define BIT_CLR(a, i)  ((a)[(i) / 32] &= ~(1u << ((i) % 32)))

/* The ops table pirq sees: ctx is the guard, everything forwards to the backend. */
static struct pirq_guard *gd(void *c) { return c; }

static void w_mask(void *c, uint32_t p) { gd(c)->inner->mask(gd(c)->inner->ctx, p); }

static void w_unmask(void *c, uint32_t p)
{
	struct pirq_guard *g = gd(c);

	if (p < PIRQ_GUARD_NSRC && BIT_GET(g->tripped, p)) {
		g->st.unmask_swallowed++;       /* the cool-down or a guest re-enable decides */
		return;
	}
	g->inner->unmask(g->inner->ctx, p);
}

static void w_target(void *c, uint32_t p, int v) { gd(c)->inner->set_target(gd(c)->inner->ctx, p, v); }
static void w_trigger(void *c, uint32_t p, bool e) { gd(c)->inner->set_trigger(gd(c)->inner->ctx, p, e); }
static uint32_t w_intid_of(void *c, uint32_t p) { return gd(c)->inner->intid_of(gd(c)->inner->ctx, p); }
static uint32_t w_phys_of(void *c, uint32_t v) { return gd(c)->inner->phys_of(gd(c)->inner->ctx, v); }
static int w_trigger_of(void *c, uint32_t p) { return gd(c)->inner->trigger_of(gd(c)->inner->ctx, p); }

void pirq_guard_init(struct pirq_guard *g, const struct pirq_ops *inner,
		     const struct pirq_guard_cfg *cfg)
{
	g->cfg = *cfg;
	g->inner = inner;
	g->ops = *inner;
	g->ops.mask = w_mask;
	g->ops.unmask = w_unmask;
	g->ops.set_target = w_target;
	g->ops.set_trigger = w_trigger;
	g->ops.intid_of = w_intid_of;
	g->ops.phys_of = w_phys_of;
	g->ops.trigger_of = inner->trigger_of ? w_trigger_of : (int (*)(void *, uint32_t))0;
	g->ops.ctx = g;
	g->st.trips = g->st.suppressed = g->st.denied_seen = g->st.reenabled = 0;
	g->st.unmask_swallowed = g->st.exempt = 0;
	for (unsigned i = 0; i < PIRQ_GUARD_NSRC / 32; i++)
		g->tripped[i] = g->logged_storm[i] = g->logged_deny[i] = g->guest_off[i] = 0;
	for (unsigned i = 0; i < PIRQ_GUARD_NSRC; i++) {
		g->win_count[i] = g->seen[i] = 0;
		g->win_start[i] = g->tripped_at[i] = 0;
	}
}

const struct pirq_ops *pirq_guard_ops(struct pirq_guard *g)
{
	return &g->ops;
}

static void log_once(struct pirq_guard *g, uint32_t *seen_bits, enum pirq_guard_reason why,
		     uint32_t phys)
{
	if (BIT_GET(seen_bits, phys))
		return;
	BIT_SET(seen_bits, phys);
	if (g->cfg.log)
		g->cfg.log(g->cfg.log_ctx, why, phys);
}

static void untrip(struct pirq_guard *g, uint32_t phys)
{
	BIT_CLR(g->tripped, phys);
	BIT_CLR(g->guest_off, phys);
	g->win_count[phys] = 0;                 /* a fresh budget */
	g->st.reenabled++;
}

uint32_t pirq_guard_arrive(struct pirq_guard *g, struct pirq *p, unsigned vcpu,
			   uint32_t phys, uint64_t now)
{
	uint32_t id = g->inner->intid_of(g->inner->ctx, phys);

	if (id != INTR_INVALID && id < INTR_VINTID_SPI0) {
		g->st.exempt++;                 /* PPI / SGI range: no budget, no bookkeeping */
		return pirq_arrive(p, vcpu, phys);
	}
	if (phys >= PIRQ_GUARD_NSRC)
		return pirq_arrive(p, vcpu, phys);      /* untracked: the pirq policy denies it */

	g->seen[phys]++;
	if (id == INTR_INVALID) {
		g->st.denied_seen++;
		log_once(g, g->logged_deny, PIRQ_GUARD_DENIED, phys);
		return pirq_arrive(p, vcpu, phys);      /* masks it and counts it in pirq stats */
	}
	if (BIT_GET(g->tripped, phys)) {
		/* Late or stray event of a source we hold masked: keep it quiet. */
		g->inner->mask(g->inner->ctx, phys);
		g->st.suppressed++;
		return INTR_INVALID;
	}
	if (g->win_count[phys] == 0 || now - g->win_start[phys] >= g->cfg.window) {
		g->win_start[phys] = now;
		g->win_count[phys] = 0;
	}
	if (++g->win_count[phys] > g->cfg.max_events) {
		g->inner->mask(g->inner->ctx, phys);
		BIT_SET(g->tripped, phys);
		g->tripped_at[phys] = now;
		g->st.trips++;
		g->st.suppressed++;
		log_once(g, g->logged_storm, PIRQ_GUARD_STORM, phys);
		return INTR_INVALID;
	}
	return pirq_arrive(p, vcpu, phys);
}

void pirq_guard_effect(struct pirq_guard *g, struct pirq *p, enum vgic_effect eff,
		       uint32_t intid, int arg)
{
	uint32_t phys = g->inner->phys_of(g->inner->ctx, intid);

	if (phys != INTR_INVALID && phys < PIRQ_GUARD_NSRC && BIT_GET(g->tripped, phys)) {
		if (eff == VGIC_EFFECT_MASK_SPI) {
			BIT_SET(g->guest_off, phys);    /* the guest took charge: no timed re-arm */
		} else if (eff == VGIC_EFFECT_UNMASK_SPI && BIT_GET(g->guest_off, phys)) {
			untrip(g, phys);                /* disabled then enabled again: forgiven */
		}
	}
	pirq_effect(p, eff, intid, arg);
}

unsigned pirq_guard_tick(struct pirq_guard *g, uint64_t now)
{
	unsigned n = 0;

	for (uint32_t i = 0; i < PIRQ_GUARD_NSRC; i++) {
		if (!BIT_GET(g->tripped, i) || BIT_GET(g->guest_off, i))
			continue;
		if (now - g->tripped_at[i] < g->cfg.cooldown)
			continue;
		untrip(g, i);
		g->inner->unmask(g->inner->ctx, i);
		n++;
	}
	return n;
}

bool pirq_guard_is_tripped(const struct pirq_guard *g, uint32_t phys)
{
	return phys < PIRQ_GUARD_NSRC && BIT_GET(g->tripped, phys);
}

uint32_t pirq_guard_seen(const struct pirq_guard *g, uint32_t phys)
{
	return phys < PIRQ_GUARD_NSRC ? g->seen[phys] : 0;
}
