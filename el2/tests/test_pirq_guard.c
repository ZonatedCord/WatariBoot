/* SPDX-License-Identifier: MIT */
/*
 * Host tests: pirq-guard (storm limiter) on the fake backends.
 * Design: docs/design/watari-el2-devirq/ARCHITECTURE.md 3.3 / 4.2, TEST_STRATEGY 2.
 */
#include "test.h"
#include "fake_pirq.h"
#include "wel2_pirq_guard.h"

#define ANS_PHYS  590u           /* vINTID 622, level */
#define NVME_V    622u
#define WINDOW    1000u
#define BUDGET    10u
#define COOL      5000u

static struct fake FK;
static struct pirq P;
static struct pirq_guard G;
static struct intr_map g_map;
static int n_log, last_why;
static uint32_t last_phys;

static void log_cb(void *c, enum pirq_guard_reason why, uint32_t phys)
{
	(void)c; n_log++; last_why = why; last_phys = phys;
}

static void eff_cb(void *ctx, enum vgic_effect e, uint32_t id, int arg)
{
	(void)ctx;
	pirq_guard_effect(&G, &P, e, id, arg);
}

static void setup_guard(bool aic)
{
	struct pirq_guard_cfg cfg = { BUDGET, WINDOW, COOL, log_cb, NULL };

	g_map = INTR_MAP_T8103;
	setup(2);
	bring_up();
	fake_init(&FK, aic ? &g_map : NULL);
	n_log = 0;
	pirq_guard_init(&G, &FK.ops, &cfg);
	pirq_init(&P, pirq_guard_ops(&G), &F.v, 2);
	F.v.hooks.effect = eff_cb;
	for (unsigned i = 0; i < 2; i++)
		pirq_vcpu_on(&P, i);
}

static void guest_enable(uint32_t id)
{
	wr32(D(0x80 + 4 * (id / 32)), rd32(D(0x80 + 4 * (id / 32))) | (1u << (id % 32)));
	wr32(D(0x100 + 4 * (id / 32)), 1u << (id % 32));
}

static bool anything_pending(void)
{
	uint32_t id;

	return vgic_next_pending(&F.v, 0, &id, NULL);
}

/* One interrupt of a source the guest handles at once: event, arrive, ack, EOI, finish.
 * Returns true if it reached the guest. */
static bool one_round(uint32_t phys, uint64_t now)
{
	uint32_t ev, id;

	while (fake_event(&FK, &ev)) {
		if (ev != phys)
			continue;
		id = pirq_guard_arrive(&G, &P, 0, ev, now);
		if (id == INTR_INVALID)
			return false;
		CHECK_EQ(vgic_ack(&F.v, 0), id);
		/* the device stays asserted: finish unmasks the source and it fires again */
		pirq_line_low(&P, 0, id);
		vgic_eoi(&F.v, 0, id);
		pirq_finish(&P, id);
		return true;
	}
	return false;
}

static void t_normal_source_untouched(void)
{
	setup_guard(true);
	guest_enable(NVME_V);
	fake_line(&FK, ANS_PHYS, true);
	/* exactly the budget in every window: never trips */
	for (unsigned w = 0; w < 5; w++)
		for (unsigned i = 0; i < BUDGET; i++)
			CHECK(one_round(ANS_PHYS, w * WINDOW + i));
	CHECK_EQ(G.st.trips, 0);
	CHECK_EQ(G.st.suppressed, 0);
	CHECK(!pirq_guard_is_tripped(&G, ANS_PHYS));
	CHECK_EQ(n_log, 0);
	CHECK(!FK.masked[ANS_PHYS] || FK.n_unmask > 0);
	CHECK_EQ(pirq_guard_seen(&G, ANS_PHYS), 5 * BUDGET);
}

static void t_storm_level_masks_and_comes_back(void)
{
	setup_guard(true);
	guest_enable(NVME_V);
	fake_line(&FK, ANS_PHYS, true);          /* a device that never goes quiet */
	for (unsigned i = 0; i < BUDGET; i++)
		CHECK(one_round(ANS_PHYS, 100 + i));
	CHECK(!pirq_guard_is_tripped(&G, ANS_PHYS));
	CHECK(!one_round(ANS_PHYS, 110));        /* budget + 1 inside the window: trip */
	CHECK(pirq_guard_is_tripped(&G, ANS_PHYS));
	CHECK(FK.masked[ANS_PHYS]);
	CHECK_EQ(G.st.trips, 1);
	CHECK_EQ(G.st.suppressed, 1);
	CHECK_EQ(n_log, 1);
	CHECK_EQ(last_why, PIRQ_GUARD_STORM);
	CHECK_EQ(last_phys, ANS_PHYS);
	CHECK(!anything_pending());              /* nothing injected for the tripped event */

	/* masked: no more events until the cool-down is processed */
	CHECK(!one_round(ANS_PHYS, 200));
	CHECK_EQ(pirq_guard_tick(&G, 110 + COOL - 1), 0);
	CHECK(FK.masked[ANS_PHYS]);
	CHECK_EQ(pirq_guard_tick(&G, 110 + COOL), 1);
	CHECK(!FK.masked[ANS_PHYS]);
	CHECK(!pirq_guard_is_tripped(&G, ANS_PHYS));
	CHECK_EQ(G.st.reenabled, 1);
	/* it is delivered again, with a fresh budget */
	CHECK(one_round(ANS_PHYS, 110 + COOL + 1));
	CHECK_EQ(G.st.trips, 1);
	/* a second storm trips again but is logged only once */
	for (unsigned i = 0; i < BUDGET; i++)
		one_round(ANS_PHYS, 110 + COOL + 2 + i);
	CHECK(pirq_guard_is_tripped(&G, ANS_PHYS));
	CHECK_EQ(G.st.trips, 2);
	CHECK_EQ(n_log, 1);
}

static void t_unmask_swallowed_while_tripped(void)
{
	setup_guard(true);
	guest_enable(NVME_V);
	fake_line(&FK, ANS_PHYS, true);
	for (unsigned i = 0; i <= BUDGET; i++)
		one_round(ANS_PHYS, i);
	CHECK(pirq_guard_is_tripped(&G, ANS_PHYS));
	unsigned unm = FK.n_unmask;

	pirq_finish(&P, NVME_V);                 /* the end-of-interrupt path: nothing held, nothing to do */
	pirq_guard_ops(&G)->unmask(pirq_guard_ops(&G)->ctx, ANS_PHYS);   /* an unmask the policy asks for */
	CHECK_EQ(FK.n_unmask, unm);
	CHECK(FK.masked[ANS_PHYS]);
	CHECK_EQ(G.st.unmask_swallowed, 1);
	pirq_guard_ops(&G)->unmask(pirq_guard_ops(&G)->ctx, 591);        /* another source: passes */
	CHECK_EQ(FK.n_unmask, unm + 1);
}

static void t_guest_reenable_forgives(void)
{
	setup_guard(true);
	guest_enable(NVME_V);
	fake_line(&FK, ANS_PHYS, true);
	for (unsigned i = 0; i <= BUDGET; i++)
		one_round(ANS_PHYS, i);
	CHECK(pirq_guard_is_tripped(&G, ANS_PHYS));
	/* the driver disables the line (ICENABLER): the cool-down no longer re-arms it */
	wr32(D(0x180 + 4 * (NVME_V / 32)), 1u << (NVME_V % 32));
	CHECK_EQ(pirq_guard_tick(&G, 10 + COOL), 0);
	CHECK(FK.masked[ANS_PHYS]);
	CHECK(pirq_guard_is_tripped(&G, ANS_PHYS));
	/* ... and re-enables it (ISENABLER): forgiven at once */
	wr32(D(0x100 + 4 * (NVME_V / 32)), 1u << (NVME_V % 32));
	CHECK(!pirq_guard_is_tripped(&G, ANS_PHYS));
	CHECK(!FK.masked[ANS_PHYS]);
	CHECK_EQ(G.st.reenabled, 1);
}

static void t_storm_edge_gic(void)
{
	uint32_t ev;

	setup_guard(false);                      /* GIC-like, identity map */
	guest_enable(80);
	wr32(D(0xC00 + 4 * (80 / 16)), rd32(D(0xC00 + 4 * (80 / 16))) | (2u << (2 * (80 % 16))));
	for (unsigned i = 0; i < BUDGET; i++) {
		fake_pulse(&FK, 80);
		CHECK(fake_event(&FK, &ev));
		CHECK_EQ(pirq_guard_arrive(&G, &P, 0, ev, i), 80);
	}
	fake_pulse(&FK, 80);
	CHECK(fake_event(&FK, &ev));
	CHECK_EQ(pirq_guard_arrive(&G, &P, 0, ev, 50), INTR_INVALID);
	CHECK(FK.masked[80]);
	CHECK(pirq_guard_is_tripped(&G, 80));
	/* a stray event of a tripped source is still dropped and counted */
	CHECK_EQ(pirq_guard_arrive(&G, &P, 0, 80, 60), INTR_INVALID);
	CHECK_EQ(G.st.suppressed, 2);
	CHECK_EQ(G.st.trips, 1);
	CHECK_EQ(pirq_guard_seen(&G, 80), BUDGET + 2);
}

static void t_window_boundary(void)
{
	setup_guard(false);
	guest_enable(80);
	/* BUDGET events in one window and BUDGET in the next: legal. (A fixed window starts at
	 * its first event, so a burst straddling two windows can reach 2 * BUDGET: accepted.) */
	for (unsigned i = 0; i < BUDGET; i++)
		CHECK_EQ(pirq_guard_arrive(&G, &P, 0, 80, i), 80);
	for (unsigned i = 0; i < BUDGET; i++)
		CHECK_EQ(pirq_guard_arrive(&G, &P, 0, 80, WINDOW + i), 80);
	CHECK_EQ(G.st.trips, 0);
	/* window is half-open: an event at start + window opens a new window */
	setup_guard(false);
	guest_enable(80);
	for (unsigned i = 0; i < BUDGET; i++)
		CHECK_EQ(pirq_guard_arrive(&G, &P, 0, 80, 5), 80);
	CHECK_EQ(pirq_guard_arrive(&G, &P, 0, 80, 5 + WINDOW), 80);
	CHECK_EQ(G.st.trips, 0);
}

static void t_denied_counted_logged_once(void)
{
	setup_guard(true);
	for (unsigned i = 0; i < 3; i++) {
		FK.line[583] = true;                 /* ANS mailbox: not allow-listed */
		FK.masked[583] = false;
		CHECK_EQ(pirq_guard_arrive(&G, &P, 0, 583, i), INTR_INVALID);
		CHECK(FK.masked[583]);
	}
	CHECK_EQ(G.st.denied_seen, 3);
	CHECK_EQ(P.st.denied, 3);
	CHECK_EQ(pirq_guard_seen(&G, 583), 3);
	CHECK_EQ(n_log, 1);
	CHECK_EQ(last_why, PIRQ_GUARD_DENIED);
	CHECK_EQ(last_phys, 583);
	CHECK_EQ(G.st.trips, 0);                 /* denied is not a storm trip */
	CHECK(!anything_pending());
	/* another denied source logs once more */
	CHECK_EQ(pirq_guard_arrive(&G, &P, 0, 584, 9), INTR_INVALID);
	CHECK_EQ(n_log, 2);
}

static uint32_t ppi_intid_of(void *c, uint32_t p)
{
	(void)c;
	return p == 27 ? 27 : INTR_INVALID;
}

static void t_exempt_below_32(void)
{
	struct pirq_ops o;
	struct pirq_guard g2;
	struct pirq p2;
	struct pirq_guard_cfg cfg = { 1, WINDOW, COOL, log_cb, NULL };

	setup_guard(false);
	o = FK.ops;
	o.intid_of = ppi_intid_of;
	pirq_guard_init(&g2, &o, &cfg);
	pirq_init(&p2, pirq_guard_ops(&g2), &F.v, 2);
	n_log = 0;
	for (unsigned i = 0; i < 50; i++)        /* far above the budget of 1 */
		CHECK_EQ(pirq_guard_arrive(&g2, &p2, 0, 27, i), INTR_INVALID);   /* pirq denies SPI-less INTIDs */
	CHECK_EQ(g2.st.exempt, 50);
	CHECK_EQ(g2.st.trips, 0);
	CHECK(!pirq_guard_is_tripped(&g2, 27));
	CHECK_EQ(n_log, 0);
	/* out of the table: denied, not tracked */
	CHECK_EQ(pirq_guard_arrive(&G, &P, 0, 5000, 0), INTR_INVALID);
	CHECK_EQ(pirq_guard_seen(&G, 5000), 0);
}

static void t_ops_passthrough(void)
{
	const struct pirq_ops *o;

	setup_guard(true);
	o = pirq_guard_ops(&G);
	CHECK_EQ(o->auto_masks, FK.ops.auto_masks);
	CHECK_EQ(o->strict_level, FK.ops.strict_level);
	CHECK_EQ(o->intid_of(o->ctx, 590), 622);
	CHECK_EQ(o->phys_of(o->ctx, 622), 590);
	CHECK(o->trigger_of != NULL);
	CHECK_EQ(o->trigger_of(o->ctx, 590), INTR_TRIG_LEVEL);
	o->set_target(o->ctx, 590, 1);
	CHECK_EQ(FK.target[590], 1);
	o->mask(o->ctx, 590);
	CHECK(FK.masked[590]);
	setup_guard(false);
	CHECK(pirq_guard_ops(&G)->trigger_of == NULL);
}

int main(void)
{
	RUN(t_normal_source_untouched);
	RUN(t_storm_level_masks_and_comes_back);
	RUN(t_unmask_swallowed_while_tripped);
	RUN(t_guest_reenable_forgives);
	RUN(t_storm_edge_gic);
	RUN(t_window_boundary);
	RUN(t_denied_counted_logged_once);
	RUN(t_exempt_below_32);
	RUN(t_ops_passthrough);
	TESTS_DONE();
}
