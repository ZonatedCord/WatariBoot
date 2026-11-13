/* SPDX-License-Identifier: MIT */
/*
 * Host tests: the Apple AIC (AICv1) backend of pirq (issue #59) against the register-level
 * model in fake_aic.h. Covers ARCHITECTURE devirq 4.2 (level / edge / denied), 4.3 (TARGET_CPU
 * from the vCPU, CPU_OFF re-target), 3.3 (pirq-guard over the real backend) and 4.5 (IPI and
 * timer are not SPI events). The model's behaviour that is a hypothesis until E-HW0 is a knob.
 */
#include "test.h"
#include "fake_aic.h"
#include "wel2_pirq_guard.h"

#define ANS_PHYS   590u          /* vINTID 622, level */
#define ANS_V      622u
#define DWC_PHYS   777u          /* vINTID 809, level */
#define DWC_V      809u
#define MSI_PHYS   704u          /* vINTID 928, edge (pool) */
#define MSI_V      928u

static struct fake_aic FA;
static struct pirq_aic A;
static struct pirq P;
static struct pirq_guard G;
static struct intr_map g_map;
static bool guarded;
static int n_log;

/* vCPU i runs on AIC core cpu_of[i]: E0, E1, P0, P1 (the P-core indices are a hypothesis) */
static const uint8_t cpu_of[4] = { 0, 1, 4, 5 };

static void eff_cb(void *ctx, enum vgic_effect e, uint32_t id, int arg)
{
	(void)ctx;
	if (guarded)
		pirq_guard_effect(&G, &P, e, id, arg);
	else
		pirq_effect(&P, e, id, arg);
}

static void log_cb(void *c, enum pirq_guard_reason why, uint32_t phys)
{
	(void)c; (void)why; (void)phys;
	n_log++;
}

static void setup_aic(bool msi, bool guard)
{
	struct aic_io io;
	struct pirq_guard_cfg cfg = { 10, 1000, 5000, log_cb, NULL };

	g_map = INTR_MAP_T8103;
	g_map.msi_exposed = msi;
	setup(4);
	bring_up();
	fa_reset(&FA);
	io = fa_io(&FA);
	guarded = guard;
	n_log = 0;
	CHECK_EQ(pirq_aic_init(&A, &io, &g_map, cpu_of, 4), 0);
	if (guard) {
		pirq_guard_init(&G, pirq_aic_ops(&A), &cfg);
		pirq_init(&P, pirq_guard_ops(&G), &F.v, 4);
	} else {
		pirq_init(&P, pirq_aic_ops(&A), &F.v, 4);
	}
	F.v.hooks.effect = eff_cb;
	for (unsigned i = 0; i < 4; i++)
		pirq_vcpu_on(&P, i);
}

static void route(uint32_t id, unsigned vcpu)
{
	wr64(D(0x6000 + 8 * id), vcpu);        /* affinity = vCPU index in the fixture */
}

static void set_edge_cfg(uint32_t id)
{
	wr32(D(0xC00 + 4 * (id / 16)), rd32(D(0xC00 + 4 * (id / 16))) | (2u << (2 * (id % 16))));
}

static uint32_t arrive(struct aic_event ev, unsigned vcpu, uint64_t now)
{
	if (guarded)
		return pirq_guard_arrive(&G, &P, vcpu, ev.num, now);
	return pirq_arrive(&P, vcpu, ev.num);
}

static bool anything_pending(unsigned vcpu)
{
	uint32_t id;

	return vgic_next_pending(&F.v, vcpu, &id, NULL);
}

/* ---- init and ops table ---- */

static void t_init_state(void)
{
	struct aic_io io;

	setup_aic(false, false);
	for (unsigned i = 0; i < FA_WORDS; i++) {
		CHECK_EQ(FA.mask[i], 0xFFFFFFFFu);
		CHECK_EQ(FA.sw[i], 0);
	}
	CHECK_EQ(FA.target[ANS_PHYS], 1u << 0);                    /* vCPU 0's core */
	CHECK_EQ(FA.ipi_mask[0], AIC_IPI_OTHER | AIC_IPI_SELF);
	CHECK_EQ(A.nr_irq, 1024);
	CHECK(A.ops.auto_masks);
	CHECK(A.ops.strict_level);
	CHECK(A.ops.trigger_of != NULL);
	CHECK_EQ(pirq_aic_init_cpu(&A, 0), 0);
	CHECK_EQ(FA.ipi_mask[0], AIC_IPI_OTHER | AIC_IPI_SELF);

	/* bad arguments and an implausible INFO leave the block alone */
	io = fa_io(&FA);
	CHECK_EQ(pirq_aic_init(&A, &io, &g_map, cpu_of, 0), -1);
	CHECK_EQ(pirq_aic_init(&A, &io, &g_map, cpu_of, 9), -1);
	CHECK_EQ(pirq_aic_init(&A, &io, NULL, cpu_of, 4), -1);
	{
		static const uint8_t bad[1] = { 8 };

		CHECK_EQ(pirq_aic_init(&A, &io, &g_map, bad, 1), -1);
	}
	FA.nr_irq = 100;                                           /* not a multiple of 32 */
	FA.n_mask_set = 0;
	CHECK_EQ(pirq_aic_init(&A, &io, &g_map, cpu_of, 4), -1);
	CHECK_EQ(FA.n_mask_set, 0);
}

static void t_t8103_map(void)
{
	const struct pirq_ops *o;

	setup_aic(false, false);
	o = pirq_aic_ops(&A);
	CHECK_EQ(o->intid_of(o->ctx, ANS_PHYS), ANS_V);
	CHECK_EQ(o->phys_of(o->ctx, ANS_V), ANS_PHYS);
	CHECK_EQ(o->intid_of(o->ctx, DWC_PHYS), DWC_V);
	CHECK_EQ(o->phys_of(o->ctx, DWC_V), DWC_PHYS);
	CHECK_EQ(o->trigger_of(o->ctx, ANS_PHYS), INTR_TRIG_LEVEL);
	CHECK_EQ(o->intid_of(o->ctx, 583), INTR_INVALID);          /* ANS mailbox: not allow-listed */
	CHECK_EQ(o->intid_of(o->ctx, 0), INTR_INVALID);            /* AIC hwirq 0 is not the FIQ 0 */
	CHECK_EQ(o->intid_of(o->ctx, MSI_PHYS), INTR_INVALID);     /* pool not exposed yet */
	CHECK(o->trigger_of(o->ctx, 583) < 0);
	setup_aic(true, false);
	CHECK_EQ(pirq_aic_ops(&A)->intid_of(&A, MSI_PHYS), MSI_V);
	CHECK_EQ(pirq_aic_ops(&A)->trigger_of(&A, MSI_PHYS), INTR_TRIG_EDGE);
}

/* ---- level cycle ---- */

static void t_level_cycle(void)
{
	struct aic_event ev;
	uint32_t id;

	setup_aic(false, false);
	route(ANS_V, 2);                                           /* guest: SPI 622 -> vCPU 2 */
	CHECK_EQ(FA.target[ANS_PHYS], 1u << 4);                    /* vCPU 2 -> core P0 */
	enable_spi(ANS_V);                                         /* ISENABLER: AIC MASK_CLR */
	CHECK(!fa_bit(FA.mask, ANS_PHYS));
	CHECK(!pirq_is_masked(&P, ANS_V));

	fa_line(&FA, ANS_PHYS, true);
	FA.cpu = 0;
	ev = pirq_aic_poll(&A);                                    /* not this core's interrupt */
	CHECK_EQ(ev.kind, AIC_EV_NONE);
	FA.cpu = 4;
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(ev.num, ANS_PHYS);
	CHECK_EQ(AIC_EVENT_TYPE(ev.raw), AIC_EVENT_TYPE_HW);
	CHECK(fa_bit(FA.mask, ANS_PHYS));                          /* the read masked it */
	CHECK(pirq_aic_line(&A, ANS_PHYS));

	CHECK_EQ(arrive(ev, 2, 0), ANS_V);
	CHECK(fa_bit(FA.mask, ANS_PHYS));                          /* stays masked while injected */
	CHECK(pirq_is_masked(&P, ANS_V));
	CHECK(!fa_irq_out(&FA, 4));
	CHECK(anything_pending(2));
	CHECK_EQ(vgic_ack(&F.v, 2), ANS_V);

	/* the guest enables the line again while it holds it: strict level, no unmask */
	wr32(D(0x100 + 4 * (ANS_V / 32)), 1u << (ANS_V % 32));
	CHECK(fa_bit(FA.mask, ANS_PHYS));

	pirq_line_low(&P, 2, ANS_V);
	vgic_eoi(&F.v, 2, ANS_V);
	pirq_finish(&P, ANS_V);                                    /* deactivate: unmask */
	CHECK(!fa_bit(FA.mask, ANS_PHYS));
	CHECK(!pirq_is_masked(&P, ANS_V));
	/* the device still asserts the line: it fires again */
	CHECK(fa_irq_out(&FA, 4));
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(arrive(ev, 2, 1), ANS_V);

	/* now the device is quiet: finish, nothing fires */
	fa_line(&FA, ANS_PHYS, false);
	CHECK_EQ(vgic_ack(&F.v, 2), ANS_V);
	pirq_line_low(&P, 2, ANS_V);
	vgic_eoi(&F.v, 2, ANS_V);
	pirq_finish(&P, ANS_V);
	CHECK(!fa_irq_out(&FA, 4));
	CHECK(!pirq_aic_line(&A, ANS_PHYS));
	CHECK_EQ(P.st.arrivals, 2);
	CHECK_EQ(A.st.events, 2);
	(void)id;
}

static void t_second_source_dwc3(void)
{
	struct aic_event ev;

	setup_aic(false, false);
	enable_spi(DWC_V);
	fa_line(&FA, DWC_PHYS, true);
	fa_line(&FA, ANS_PHYS, true);                              /* masked at the AIC: invisible */
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.num, DWC_PHYS);
	CHECK_EQ(arrive(ev, 0, 0), DWC_V);
	CHECK_EQ(vgic_ack(&F.v, 0), DWC_V);
	CHECK_EQ(pirq_aic_poll(&A).kind, AIC_EV_NONE);
	CHECK(fa_bit(FA.mask, ANS_PHYS));
}

/* ---- edge ---- */

static void t_edge_unmask_at_injection(void)
{
	struct aic_event ev;

	setup_aic(true, false);
	set_edge_cfg(MSI_V);                                       /* ignored: the table says edge */
	enable_spi(MSI_V);
	CHECK(pirq_is_edge(&P, MSI_V));
	CHECK(!fa_bit(FA.mask, MSI_PHYS));

	fa_pulse(&FA, MSI_PHYS);
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(ev.num, MSI_PHYS);
	CHECK(fa_bit(FA.mask, MSI_PHYS));                          /* event read masked it */
	CHECK_EQ(arrive(ev, 0, 0), MSI_V);
	CHECK(!fa_bit(FA.mask, MSI_PHYS));                         /* unmasked at injection */
	CHECK(!pirq_is_masked(&P, MSI_V));
	CHECK_EQ(vgic_ack(&F.v, 0), MSI_V);

	/* a second pulse while the first is still active at the guest is delivered at once */
	fa_pulse(&FA, MSI_PHYS);
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.num, MSI_PHYS);
	CHECK_EQ(arrive(ev, 0, 1), MSI_V);
	CHECK(A.st.trigger_noop > 0);                              /* pirq_init programmed the table triggers */
}

/* Knowledge test, pending E-HW0: it documents the two possible chip behaviours, it does not
 * verify the chip. */
static void t_edge_pulse_while_masked(void)
{
	struct aic_event ev;

	/* hypothesis A (latch): a pulse that arrives while the source is masked fires on unmask */
	setup_aic(true, false);
	fa_pulse(&FA, MSI_PHYS);
	CHECK_EQ(pirq_aic_poll(&A).kind, AIC_EV_NONE);
	enable_spi(MSI_V);
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(arrive(ev, 0, 0), MSI_V);

	/* hypothesis B (no latch): the pulse is lost; the layer cannot recover it. E-HW0 decides
	 * which one is the chip; the window is the time between event read and unmask. */
	setup_aic(true, false);
	FA.latch_pulses = false;
	fa_pulse(&FA, MSI_PHYS);
	enable_spi(MSI_V);
	CHECK_EQ(pirq_aic_poll(&A).kind, AIC_EV_NONE);
}

/* ---- denied ---- */

static void t_denied_source(void)
{
	struct aic_event ev;

	setup_aic(false, false);
	FA.mask[583 / 32] &= ~(1u << (583 % 32));                  /* something unmasked it (firmware) */
	fa_line(&FA, 583, true);
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(ev.num, 583);
	CHECK_EQ(arrive(ev, 0, 0), INTR_INVALID);
	CHECK(fa_bit(FA.mask, 583));
	CHECK_EQ(P.st.denied, 1);
	CHECK_EQ(P.st.arrivals, 0);
	CHECK(!anything_pending(0));
	CHECK(!fa_irq_out(&FA, 0));

	/* the guest cannot reach it either: no vINTID, effects are ignored */
	enable_spi(615);                                           /* 583 + 32 */
	CHECK(fa_bit(FA.mask, 583));
}

static void t_guard_counts_denied(void)
{
	struct aic_event ev;

	setup_aic(false, true);
	for (unsigned i = 0; i < 3; i++) {
		FA.mask[583 / 32] &= ~(1u << (583 % 32));
		fa_line(&FA, 583, true);
		ev = pirq_aic_poll(&A);
		CHECK_EQ(arrive(ev, 0, i), INTR_INVALID);
		CHECK(fa_bit(FA.mask, 583));
	}
	CHECK_EQ(G.st.denied_seen, 3);
	CHECK_EQ(n_log, 1);
	CHECK_EQ(G.st.trips, 0);
}

/* ---- targets ---- */

static void t_retarget_cpu_off_on(void)
{
	setup_aic(false, false);
	route(ANS_V, 2);
	CHECK_EQ(FA.target[ANS_PHYS], 1u << 4);
	route(DWC_V, 3);
	CHECK_EQ(FA.target[DWC_PHYS], 1u << 5);
	CHECK_EQ(A.st.bad_vcpu, 0);

	pirq_vcpu_off(&P, 2);                                      /* PSCI CPU_OFF of vCPU 2 */
	CHECK_EQ(FA.target[ANS_PHYS], 1u << 0);                    /* first online vCPU's core */
	CHECK_EQ(FA.target[DWC_PHYS], 1u << 5);                    /* untouched */
	CHECK_EQ(P.st.retargets, 1);
	CHECK_EQ(rd64(D(0x6000 + 8 * ANS_V)), 2);                  /* the guest still reads its routing */

	pirq_vcpu_off(&P, 0);                                      /* the fallback leaves too */
	CHECK_EQ(FA.target[ANS_PHYS], 1u << 1);
	pirq_vcpu_on(&P, 2);                                       /* CPU_ON: back where the guest put it */
	CHECK_EQ(FA.target[ANS_PHYS], 1u << 4);
	pirq_vcpu_on(&P, 0);
	CHECK_EQ(FA.target[ANS_PHYS], 1u << 4);

	/* an event follows the new route: only the target core sees it */
	pirq_vcpu_off(&P, 2);
	enable_spi(ANS_V);
	fa_line(&FA, ANS_PHYS, true);
	FA.cpu = 4;
	CHECK(!fa_irq_out(&FA, 4));
	CHECK(fa_irq_out(&FA, 0));
}

static void t_set_target_bad_vcpu(void)
{
	setup_aic(false, false);
	pirq_aic_ops(&A)->set_target(&A, ANS_PHYS, 4);
	pirq_aic_ops(&A)->set_target(&A, ANS_PHYS, -1);
	CHECK_EQ(A.st.bad_vcpu, 2);
	CHECK_EQ(FA.target[ANS_PHYS], 1u << 0);
	pirq_aic_ops(&A)->set_target(&A, 5000, 1);                 /* beyond NR_IRQ: ignored */
	pirq_aic_ops(&A)->mask(&A, 5000);
	pirq_aic_ops(&A)->unmask(&A, 5000);
	CHECK_EQ(FA.n_unknown_wr, 0);
}

/* ---- guard over the AIC backend ---- */

static bool guard_round(uint64_t now)
{
	struct aic_event ev = pirq_aic_poll(&A);
	uint32_t id;

	if (ev.kind != AIC_EV_IRQ)
		return false;
	id = arrive(ev, 0, now);
	if (id == INTR_INVALID)
		return false;
	CHECK_EQ(vgic_ack(&F.v, 0), id);
	pirq_line_low(&P, 0, id);
	vgic_eoi(&F.v, 0, id);
	pirq_finish(&P, id);
	return true;
}

static void t_guard_storm(void)
{
	setup_aic(false, true);
	enable_spi(ANS_V);
	fa_line(&FA, ANS_PHYS, true);                              /* a device that never goes quiet */
	for (unsigned i = 0; i < 10; i++)
		CHECK(guard_round(100 + i));
	CHECK(!guard_round(110));                                  /* budget + 1: trip */
	CHECK(pirq_guard_is_tripped(&G, ANS_PHYS));
	CHECK(fa_bit(FA.mask, ANS_PHYS));                          /* masked at the AIC */
	CHECK_EQ(G.st.trips, 1);
	CHECK_EQ(n_log, 1);
	CHECK(!anything_pending(0));
	CHECK(!guard_round(111));                                  /* silent now */
	CHECK_EQ(pirq_guard_tick(&G, 110 + 5000), 1);
	CHECK(!fa_bit(FA.mask, ANS_PHYS));                         /* back after the cool-down */
	CHECK(guard_round(110 + 5001));
}

/* ---- IPI, timer and odd events do not reach the SPI path ---- */

static void t_ipi_contained(void)
{
	struct aic_event ev;

	setup_aic(false, false);
	enable_spi(ANS_V);
	FA.cpu = 0;
	pirq_aic_ipi_send(&A, 1u << 1);                            /* masked at reset: nothing */
	FA.cpu = 1;
	CHECK_EQ(pirq_aic_poll(&A).kind, AIC_EV_NONE);
	pirq_aic_ipi_unmask(&A, AIC_IPI_OTHER | AIC_IPI_SELF);     /* someone opened the IPI mask */
	CHECK(fa_irq_out(&FA, 1));
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IPI);
	CHECK_EQ(AIC_EVENT_TYPE(ev.raw), AIC_EVENT_TYPE_IPI);
	CHECK_EQ(ev.num, AIC_EVENT_IPI_OTHER);
	CHECK_EQ(A.st.ipi_unexpected, 1);
	CHECK_EQ(FA.ipi_pend[1] & AIC_IPI_OTHER, 0);               /* acked */
	CHECK(FA.ipi_mask[1] & AIC_IPI_OTHER);                     /* left masked */
	CHECK(!fa_irq_out(&FA, 1));
	CHECK_EQ(P.st.arrivals, 0);                                /* never an SPI */
	CHECK_EQ(P.st.denied, 0);
	CHECK(!anything_pending(1));

	pirq_aic_ipi_unmask(&A, AIC_IPI_SELF);                     /* self IPI */
	pirq_aic_ipi_send(&A, AIC_IPI_SELF);
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IPI);
	CHECK_EQ(ev.num, AIC_EVENT_IPI_SELF);
	CHECK_EQ(A.st.ipi_unexpected, 2);
	CHECK(!fa_irq_out(&FA, 1));
}

static void t_odd_events(void)
{
	struct aic_event ev;

	setup_aic(false, false);
	CHECK_EQ(pirq_aic_poll(&A).kind, AIC_EV_NONE);             /* spurious */
	CHECK_EQ(A.st.spurious, 1);

	FA.has_forced = true;
	FA.forced = (7u << 16) | 3;                                /* unknown type */
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_BAD);
	FA.has_forced = true;
	FA.forced = (AIC_EVENT_TYPE_HW << 16) | 2000;              /* number >= NR_IRQ */
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_BAD);
	CHECK_EQ(A.st.bad, 2);
	CHECK_EQ(A.st.events, 2);
	CHECK_EQ(P.st.arrivals + P.st.denied, 0);
}

/* This tests the interrupt map only (FIQ numbers 0..5 and PPIs 16..31 have no exposed source
 * or vINTID). It does not test FIQ separation: a hardware FIQ has no AIC register, so there is
 * nothing in this backend to exercise; that separation is a boot-path property (M3a). */
static void t_fiq_numbers_and_ppis_not_in_intr_map(void)
{
	setup_aic(false, false);
	for (uint32_t t = 0; t < 6; t++)                           /* FIQ numbers 0..5 of the binding */
		CHECK_EQ(pirq_aic_ops(&A)->intid_of(&A, t), INTR_INVALID);
	for (uint32_t ppi = 16; ppi < 32; ppi++)
		CHECK_EQ(pirq_aic_ops(&A)->phys_of(&A, ppi), INTR_INVALID);
}

/* ---- software set / clear ---- */

static void t_sw_set_clr(void)
{
	struct aic_event ev;

	setup_aic(false, false);
	CHECK(!FA.event_clears_sw);                                /* [verified] the M1 does not clear it */
	enable_spi(ANS_V);
	pirq_aic_sw_set(&A, ANS_PHYS);
	CHECK(fa_bit(FA.sw, ANS_PHYS));
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.num, ANS_PHYS);
	CHECK(!fa_bit(FA.sw, ANS_PHYS));                           /* cleared by the take (SW_CLR), not by the read */
	CHECK_EQ(A.st.sw_cleared, 1);
	CHECK_EQ(arrive(ev, 0, 0), ANS_V);
	CHECK_EQ(vgic_ack(&F.v, 0), ANS_V);
	pirq_line_low(&P, 0, ANS_V);
	vgic_eoi(&F.v, 0, ANS_V);
	pirq_finish(&P, ANS_V);
	CHECK(!fa_irq_out(&FA, 0));                                /* unmasked at EOI, nothing re-fires */
	CHECK_EQ(pirq_aic_poll(&A).kind, AIC_EV_NONE);

	pirq_aic_sw_set(&A, 9999);                                 /* out of range: ignored */
	CHECK_EQ(FA.n_unknown_wr, 0);
}

/* Q-D5: on the M1 SW_SET survives the EVENT read, so without the clear at take the source would
 * fire again at every unmask. The explicit mode switch checks the layer also works if a future
 * chip revision clears it itself. */
static void t_sw_cleared_at_take(void)
{
	struct aic_event ev;

	for (int clears = 0; clears <= 1; clears++) {
		setup_aic(false, false);
		FA.event_clears_sw = clears;
		enable_spi(ANS_V);
		pirq_aic_sw_set(&A, ANS_PHYS);
		ev = pirq_aic_poll(&A);
		CHECK_EQ(ev.kind, AIC_EV_IRQ);
		CHECK(!fa_bit(FA.sw, ANS_PHYS));
		CHECK_EQ(arrive(ev, 0, 0), ANS_V);
		CHECK_EQ(vgic_ack(&F.v, 0), ANS_V);
		pirq_line_low(&P, 0, ANS_V);
		vgic_eoi(&F.v, 0, ANS_V);
		pirq_finish(&P, ANS_V);
		CHECK(!fa_irq_out(&FA, 0));
		CHECK_EQ(A.st.sw_cleared, 1u);          /* the write is idempotent either way */
	}
}

/* A new sw_set between take and EOI must survive: that is why the clear is at take, not at EOI. */
static void t_sw_set_between_take_and_eoi(void)
{
	struct aic_event ev;

	setup_aic(false, false);
	enable_spi(ANS_V);
	pirq_aic_sw_set(&A, ANS_PHYS);
	ev = pirq_aic_poll(&A);
	CHECK_EQ(arrive(ev, 0, 0), ANS_V);
	CHECK_EQ(vgic_ack(&F.v, 0), ANS_V);
	pirq_aic_sw_set(&A, ANS_PHYS);                             /* second software event while the guest runs */
	CHECK(fa_bit(FA.sw, ANS_PHYS));
	pirq_line_low(&P, 0, ANS_V);
	vgic_eoi(&F.v, 0, ANS_V);
	pirq_finish(&P, ANS_V);                                    /* unmask: the second event fires */
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(ev.num, ANS_PHYS);
	CHECK(!fa_bit(FA.sw, ANS_PHYS));
	CHECK_EQ(A.st.sw_cleared, 2);
}

/* A hardware level line is not touched by the clear: it keeps firing after the take, and a SW bit
 * the layer did not set (firmware) is left alone. */
static void t_sw_clear_spares_hw_and_foreign(void)
{
	struct aic_event ev;

	setup_aic(false, false);
	enable_spi(ANS_V);
	enable_spi(DWC_V);
	fa_line(&FA, ANS_PHYS, true);                              /* HW level, no sw_set by us */
	FA.sw[DWC_PHYS / 32] |= 1u << (DWC_PHYS % 32);             /* foreign SW bit */
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(A.st.sw_cleared, 0);
	CHECK(fa_bit(FA.line, ANS_PHYS));
	/* both sources: one is ANS (line), one is DWC (foreign SW); neither was cleared */
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(A.st.sw_cleared, 0);
	CHECK(fa_bit(FA.sw, DWC_PHYS));
	/* SW and HW at once: the SW bit goes, the line stays and re-fires on unmask */
	pirq_aic_sw_set(&A, ANS_PHYS);
	pirq_aic_sw_clr(&A, ANS_PHYS);                             /* explicit clear drops ownership */
	CHECK(!fa_bit(FA.sw, ANS_PHYS));
	CHECK(fa_bit(FA.line, ANS_PHYS));
	pirq_aic_sw_set(&A, ANS_PHYS);
	FA.mask[ANS_PHYS / 32] &= ~(1u << (ANS_PHYS % 32));
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.num, ANS_PHYS);
	CHECK_EQ(A.st.sw_cleared, 1);
	CHECK(!fa_bit(FA.sw, ANS_PHYS));
	CHECK(fa_bit(FA.line, ANS_PHYS));
	FA.mask[ANS_PHYS / 32] &= ~(1u << (ANS_PHYS % 32));
	CHECK_EQ(pirq_aic_poll(&A).num, ANS_PHYS);                 /* the hardware line still fires */
}

/* DIE is [31:24], TYPE [23:16]: a type-1 event on die 1 is not an IRQ of ours, and a type
 * value with bits above 8 must not alias into the die field. */
static void t_event_die_field(void)
{
	struct aic_event ev;

	setup_aic(false, false);
	FA.has_forced = true;
	FA.forced = (1u << 24) | (AIC_EVENT_TYPE_HW << 16) | ANS_PHYS;
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_BAD);
	CHECK_EQ(ev.die, 1);
	CHECK_EQ(ev.num, ANS_PHYS);
	CHECK_EQ(A.st.bad, 1);
	FA.has_forced = true;
	FA.forced = (AIC_EVENT_TYPE_HW << 16) | ANS_PHYS;          /* die 0 */
	ev = pirq_aic_poll(&A);
	CHECK_EQ(ev.kind, AIC_EV_IRQ);
	CHECK_EQ(ev.die, 0);
	CHECK_EQ(AIC_EVENT_TYPE(0x01040001u), 4);
	CHECK_EQ(AIC_EVENT_DIE(0x01040001u), 1);
}

static void t_init_cpu_ipi_per_core(void)
{
	setup_aic(false, false);
	FA.ipi_mask[1] = 0;                                        /* core 1 left with IPIs open */
	FA.ipi_pend[1] = AIC_IPI_OTHER;
	FA.cpu = 1;
	CHECK_EQ(pirq_aic_init_cpu(&A, 1), 0);
	CHECK_EQ(FA.ipi_mask[1], AIC_IPI_OTHER | AIC_IPI_SELF);
	CHECK_EQ(FA.ipi_pend[1], 0);
	CHECK(!fa_irq_out(&FA, 1));
	CHECK_EQ(pirq_aic_init_cpu(&A, 4), -1);                    /* no such vCPU */
}

static void t_init_cpu_whoami(void)
{
	setup_aic(false, false);
	FA.cpu = 4;                                                /* vCPU 2 runs on core 4 */
	CHECK_EQ(pirq_aic_init_cpu(&A, 2), 0);
	CHECK_EQ(A.st.whoami_mismatch, 0);
	FA.cpu = 2;                                                /* P0 is not core 2 here */
	CHECK_EQ(pirq_aic_init_cpu(&A, 2), -1);
	CHECK_EQ(A.st.whoami_mismatch, 1);
	CHECK_EQ(FA.ipi_mask[2], AIC_IPI_OTHER | AIC_IPI_SELF);    /* still quieted */
}

/* ---- the drain loop of the IRQ path (T6d): pirq_aic_drain over the register-level model ---- */

static unsigned n_note, n_note_irq, n_note_other;
static uint32_t last_note_vintid;
static void note_cb(void *ctx, const struct aic_event *ev, uint32_t vintid)
{
	(void)ctx;
	n_note++;
	if (ev->kind == AIC_EV_IRQ) {
		n_note_irq++;
		last_note_vintid = vintid;
	} else {
		n_note_other++;
	}
}

static void t_drain_several_sources(void)
{
	struct pirq_aic_drain d = { 0, 0, note_cb, NULL };
	struct aic_drain_result r;

	setup_aic(true, false);
	n_note = n_note_irq = n_note_other = 0;
	enable_spi(ANS_V);
	enable_spi(DWC_V);
	enable_spi(MSI_V);
	fa_line(&FA, ANS_PHYS, true);                              /* level */
	fa_line(&FA, DWC_PHYS, true);                              /* level */
	fa_pulse(&FA, MSI_PHYS);                                   /* edge */
	FA.cpu = 0;
	CHECK(fa_irq_out(&FA, 0));

	r = pirq_aic_drain(&A, &P, 0, &d);
	CHECK_EQ(r.irqs, 3);
	CHECK_EQ(r.other, 0);
	CHECK_EQ(r.polls, 4);                                      /* three events and the empty read that ends it */
	CHECK(!r.budget_hit);
	CHECK_EQ(n_note, 3);
	CHECK_EQ(n_note_irq, 3);
	CHECK(!fa_irq_out(&FA, 0));                                /* the IRQ line is low again: nothing left */
	CHECK(fa_bit(FA.mask, ANS_PHYS) && fa_bit(FA.mask, DWC_PHYS));    /* levels stay masked until the guest EOIs */
	CHECK(pirq_is_masked(&P, ANS_V) && pirq_is_masked(&P, DWC_V));
	CHECK(!fa_bit(FA.mask, MSI_PHYS));                         /* the edge was unmasked at injection (auto_masks) */
	CHECK_EQ(P.st.arrivals, 3);
	CHECK(anything_pending(0));

	/* the second drain with nothing asserted: one empty read, nothing else */
	r = pirq_aic_drain(&A, &P, 0, &d);
	CHECK_EQ(r.polls, 1);
	CHECK_EQ(r.irqs + r.other, 0);
	CHECK_EQ(n_note, 3);
}

static void t_drain_spurious_zero_first(void)
{
	struct pirq_aic_drain d = { 0, 0, note_cb, NULL };
	struct aic_drain_result r;

	setup_aic(false, false);
	n_note = n_note_irq = n_note_other = 0;
	r = pirq_aic_drain(&A, &P, 0, &d);                         /* an IRQ taken for a source that has gone: EVENT = 0 */
	CHECK_EQ(r.polls, 1);
	CHECK_EQ(r.irqs, 0);
	CHECK(!r.budget_hit);
	CHECK_EQ(A.st.spurious, 1);
	CHECK_EQ(n_note, 0);
}

static void t_drain_bad_and_ipi_events(void)
{
	struct pirq_aic_drain d = { 0, 0, note_cb, NULL };
	struct aic_drain_result r;

	setup_aic(false, false);
	n_note = n_note_irq = n_note_other = 0;
	enable_spi(ANS_V);
	FA.has_forced = true;
	FA.forced = (7u << 16) | 3;                                /* unknown type: BAD, counted, nothing forwarded */
	fa_line(&FA, ANS_PHYS, true);                              /* a real source behind it */
	r = pirq_aic_drain(&A, &P, 0, &d);
	CHECK_EQ(r.other, 1);
	CHECK_EQ(r.irqs, 1);                                       /* the loop went on after the bad event */
	CHECK_EQ(A.st.bad, 1);
	CHECK_EQ(n_note_other, 1);
	CHECK_EQ(n_note_irq, 1);
	CHECK_EQ(last_note_vintid, ANS_V);

	n_note = n_note_irq = n_note_other = 0;
	FA.ipi_mask[0] &= ~AIC_IPI_OTHER;                          /* a legacy IPI nobody sends on the t8103 */
	FA.ipi_pend[0] |= AIC_IPI_OTHER;
	r = pirq_aic_drain(&A, &P, 0, &d);
	CHECK_EQ(r.other, 1);
	CHECK_EQ(r.irqs, 0);
	CHECK_EQ(A.st.ipi_unexpected, 1);
	CHECK(!fa_irq_out(&FA, 0));                                /* contained: acked and masked */
	CHECK_EQ(P.st.arrivals, 1);                                /* only the real source reached pirq */
}

static void t_drain_denied_source(void)
{
	struct pirq_aic_drain d = { 0, 0, note_cb, NULL };
	struct aic_drain_result r;

	setup_aic(false, false);
	n_note = n_note_irq = n_note_other = 0;
	FA.mask[583 / 32] &= ~(1u << (583 % 32));                  /* firmware left it unmasked */
	fa_line(&FA, 583, true);
	r = pirq_aic_drain(&A, &P, 0, &d);
	CHECK_EQ(r.irqs, 1);
	CHECK_EQ(last_note_vintid, INTR_INVALID);
	CHECK_EQ(P.st.denied, 1);
	CHECK(fa_bit(FA.mask, 583));                               /* quieted, not forwarded */
	CHECK(!anything_pending(0));
	CHECK(!fa_irq_out(&FA, 0));
}

static void t_drain_budget(void)
{
	struct pirq_aic_drain d = { 0, 2, note_cb, NULL };
	struct aic_drain_result r;

	setup_aic(false, false);
	n_note = n_note_irq = n_note_other = 0;
	enable_spi(ANS_V);
	enable_spi(DWC_V);
	fa_line(&FA, ANS_PHYS, true);
	fa_line(&FA, DWC_PHYS, true);
	r = pirq_aic_drain(&A, &P, 0, &d);                         /* budget 2: both reads found an event */
	CHECK_EQ(r.polls, 2);
	CHECK_EQ(r.irqs, 2);
	CHECK(r.budget_hit);                                       /* we cannot tell whether more is pending: say so */
	r = pirq_aic_drain(&A, &P, 0, &d);                         /* the next exception: nothing left */
	CHECK_EQ(r.polls, 1);
	CHECK(!r.budget_hit);
	CHECK_EQ(P.st.arrivals, 2);
}

/* A source that cannot be quieted keeps the line asserted: the loop is bounded and the next IRQ drains again. */
static void t_drain_stuck_source_bounded(void)
{
	struct pirq_aic_drain d = { 0, 0, NULL, NULL };           /* default budget */
	struct aic_drain_result r;

	setup_aic(true, false);
	set_edge_cfg(MSI_V);
	enable_spi(MSI_V);
	for (unsigned i = 0; i < 3; i++) {
		fa_pulse(&FA, MSI_PHYS);                               /* edge: unmasked at injection, so it can fire again */
		r = pirq_aic_drain(&A, &P, 0, &d);
		CHECK_EQ(r.irqs, 1);
		CHECK(!r.budget_hit);
	}
	CHECK_EQ(AIC_DRAIN_BUDGET, 64);                            /* the default budget (budget 0 above) */
}

/* The hybrid machine's backend does not report auto_masks: the drain unmasks an edge source itself (key AIC_KEY). */
static void t_drain_without_auto_masks(void)
{
	struct pirq_aic_drain d = { 0, 0, NULL, NULL };
	struct pirq_ops o;
	struct aic_drain_result r;

	setup_aic(true, false);
	o = A.ops;
	o.auto_masks = false;
	pirq_init(&P, &o, &F.v, 4);
	for (unsigned i = 0; i < 4; i++)
		pirq_vcpu_on(&P, i);
	enable_spi(MSI_V);
	fa_pulse(&FA, MSI_PHYS);
	r = pirq_aic_drain(&A, &P, 0, &d);
	CHECK_EQ(r.irqs, 1);
	CHECK(!fa_bit(FA.mask, MSI_PHYS));                         /* unmasked by the drain, not by pirq_arrive */
}

int main(void)
{
	RUN(t_drain_several_sources);
	RUN(t_drain_spurious_zero_first);
	RUN(t_drain_bad_and_ipi_events);
	RUN(t_drain_denied_source);
	RUN(t_drain_budget);
	RUN(t_drain_stuck_source_bounded);
	RUN(t_drain_without_auto_masks);
	RUN(t_init_state);
	RUN(t_t8103_map);
	RUN(t_level_cycle);
	RUN(t_second_source_dwc3);
	RUN(t_edge_unmask_at_injection);
	RUN(t_edge_pulse_while_masked);
	RUN(t_denied_source);
	RUN(t_guard_counts_denied);
	RUN(t_retarget_cpu_off_on);
	RUN(t_set_target_bad_vcpu);
	RUN(t_guard_storm);
	RUN(t_ipi_contained);
	RUN(t_odd_events);
	RUN(t_fiq_numbers_and_ppis_not_in_intr_map);
	RUN(t_event_die_field);
	RUN(t_init_cpu_ipi_per_core);
	RUN(t_init_cpu_whoami);
	RUN(t_sw_set_clr);
	RUN(t_sw_cleared_at_take);
	RUN(t_sw_set_between_take_and_eoi);
	RUN(t_sw_clear_spares_hw_and_foreign);
	TESTS_DONE();
}
