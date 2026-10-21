/* SPDX-License-Identifier: MIT */
/*
 * Host test: EOImode 1 on a level pirq source (ARCHITECTURE docs/design/watari-el2-devirq
 * 4.2: "unmask at deactivate, not at priority drop"; TEST_STRATEGY 2, test_pirq_level).
 * Chain under test: pirq_arrive -> vcif_fill (LR) -> guest IAR / EOIR / DIR (vcif ICC
 * emulation) -> vcif_harvest -> pirq_line_low + pirq_finish, exactly as boot/cpuif.c.
 */
#include "test.h"
#include "fake_pirq.h"
#include "wel2_vcif.h"

static struct fake FK;
static struct pirq P;
static struct wel2_vcif C;
static struct intr_map g_map;

static void eff_cb(void *ctx, enum vgic_effect e, uint32_t id, int arg)
{
	pirq_effect(ctx, e, id, arg);
}

static void setup_chain(bool aic)
{
	g_map = INTR_MAP_T8103;
	setup(1);
	bring_up();
	fake_init(&FK, aic ? &g_map : NULL);
	pirq_init(&P, &FK.ops, &F.v, 1);
	F.v.hooks.effect = eff_cb;
	F.v.hooks.ctx = &P;
	pirq_vcpu_on(&P, 0);
	vcif_init(&C, 4, 5, 5);
	vcif_igrpen_set(&C, 1, 1);
	vcif_pmr_set(&C, 0xf8);
}

/* The cpuif.c flush: free the LRs the guest deactivated, tell pirq. */
static unsigned flush_freed(uint32_t *last)
{
	uint32_t freed[4];
	unsigned n = vcif_harvest(&C, &F.v, 0, freed, 4);

	for (unsigned i = 0; i < n; i++) {
		pirq_line_low(&P, 0, freed[i]);
		pirq_finish(&P, freed[i]);
		*last = freed[i];
	}
	return n;
}

static void one_case(bool aic, uint32_t phys, uint32_t vintid, bool eoimode1)
{
	uint32_t ev, id, last = 0;

	setup_chain(aic);
	enable_spi(vintid);
	vcif_ctlr_set(&C, eoimode1 ? 2 : 0);
	fake_line(&FK, phys, true);
	CHECK(fake_event(&FK, &ev));
	CHECK_EQ(ev, phys);
	CHECK_EQ(pirq_arrive(&P, 0, ev), vintid);
	CHECK(FK.masked[phys]);

	vcif_fill(&C, &F.v, 0);
	id = vcif_iar1(&C);
	CHECK_EQ(id, vintid);
	CHECK_EQ(flush_freed(&last), 0);
	CHECK(FK.masked[phys]);

	vcif_eoir1(&C, id);                      /* EOIR: priority drop (+ deactivate in mode 0) */
	if (eoimode1) {
		/* priority dropped, still active: the source must stay masked */
		CHECK_EQ(flush_freed(&last), 0);
		CHECK(FK.masked[phys]);
		CHECK(pirq_is_masked(&P, vintid));
		vcif_dir(&C, id);                /* DIR: deactivate */
	}
	CHECK_EQ(flush_freed(&last), 1);
	CHECK_EQ(last, vintid);
	CHECK(!FK.masked[phys]);                 /* unmasked only now */
	CHECK(!pirq_is_masked(&P, vintid));
	/* line still high at the device: it fires again */
	CHECK(fake_event(&FK, &ev));
	CHECK_EQ(ev, phys);
}

static void t_level_eoimode0_gic(void)   { one_case(false, 40, 40, false); }
static void t_level_eoimode1_gic(void)   { one_case(false, 40, 40, true); }
static void t_level_eoimode0_aic(void)   { one_case(true, 590, 622, false); }
static void t_level_eoimode1_aic(void)   { one_case(true, 590, 622, true); }

/* EOImode 1: a guest enable between EOIR and DIR must not unmask a source it still holds
 * (AIC-like strict_level), and a DIR with a stray INTID must not unmask anything. */
static void t_eoimode1_reenable_between_eoir_and_dir(void)
{
	uint32_t ev, id, last = 0;

	setup_chain(true);
	enable_spi(622);
	vcif_ctlr_set(&C, 2);
	fake_line(&FK, 590, true);
	CHECK(fake_event(&FK, &ev));
	pirq_arrive(&P, 0, ev);
	vcif_fill(&C, &F.v, 0);
	id = vcif_iar1(&C);
	vcif_eoir1(&C, id);
	wr32(D(0x100 + 4 * (622 / 32)), 1u << (622 % 32));        /* ISENABLER write while held */
	CHECK(FK.masked[590]);
	vcif_dir(&C, 623);                                         /* DIR of something else */
	CHECK_EQ(flush_freed(&last), 0);
	CHECK(FK.masked[590]);
	vcif_dir(&C, id);
	CHECK_EQ(flush_freed(&last), 1);
	CHECK(!FK.masked[590]);
}

int main(void)
{
	RUN(t_level_eoimode0_gic);
	RUN(t_level_eoimode1_gic);
	RUN(t_level_eoimode0_aic);
	RUN(t_level_eoimode1_aic);
	RUN(t_eoimode1_reenable_between_eoir_and_dir);
	TESTS_DONE();
}
