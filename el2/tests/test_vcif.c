/* SPDX-License-Identifier: MIT */
/* Host tests: virtual CPU interface model (LR overflow policy, ICC emulation). */
#include "test.h"
#include "wel2_vcif.h"

static struct wel2_vcif C;

static void sgi_prio(uint32_t id, uint8_t p) { wr8(S(0, 0x400 + id), p); }

/* One vCPU, 4 LRs, 5 priority bits, Group 1 on, PMR open. */
static void setup_vcif(void)
{
	setup(1);
	bring_up();
	vcif_init(&C, 4, 5, 5);
	vcif_igrpen_set(&C, 1, 1);
	vcif_pmr_set(&C, 0xf8);
}

static void pend_sgi(uint32_t id, uint8_t prio)
{
	enable_priv(0, id);
	sgi_prio(id, prio);
	vgic_set_pending(&F.v, 0, id);
}

/* The guest: ack the highest priority LR, EOI it (EOImode 0). */
static uint32_t guest_take(void)
{
	uint32_t id = vcif_iar1(&C);

	if (id < 1020)
		vcif_eoir1(&C, id);
	return id;
}

static void service(void)
{
	uint32_t freed[16];

	vcif_harvest(&C, &F.v, 0, freed, 16);
	vcif_fill(&C, &F.v, 0);
}

static void t_overflow_refill_in_priority_order(void)
{
	/* 8 SGIs, 4 LRs; priorities shuffled against INTID order. */
	static const uint8_t prio[8] = { 0x70, 0x30, 0x90, 0x10, 0x50, 0xb0, 0x20, 0xa0 };
	static const uint32_t want[8] = { 6, 9, 4, 7, 3, 5, 10, 8 }; /* by ascending prio */
	uint32_t got[8];
	unsigned n = 0;

	setup_vcif();
	for (unsigned i = 0; i < 8; i++)
		pend_sgi(3 + i, prio[i]);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.st_inject, 4);
	CHECK_EQ(vcif_valid_lrs(&C), 4);
	CHECK(vcif_overflow(&C, &F.v, 0));              /* 4 more wait in the model */
	while (n < 8) {
		uint32_t id = guest_take();

		if (id >= 1020) {                       /* LRs drained: refill */
			service();
			id = guest_take();
			if (id >= 1020)
				break;
		}
		got[n++] = id;
		service();                              /* EOI maintenance -> refill */
	}
	CHECK_EQ(n, 8);
	for (unsigned i = 0; i < n && i < 8; i++)
		CHECK_EQ(got[i], want[i]);
	CHECK_EQ(C.st_inject, 8);                       /* each exactly once */
	CHECK(!vcif_overflow(&C, &F.v, 0));
	CHECK_EQ(vcif_valid_lrs(&C), 0);
}

static void t_evict_pending_for_higher_priority(void)
{
	setup_vcif();
	pend_sgi(3, 0x80); pend_sgi(4, 0x90); pend_sgi(5, 0xa0); pend_sgi(6, 0xb0);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.st_inject, 4);
	pend_sgi(11, 0x08);                             /* better than everything in the LRs */
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.st_evict, 1);
	CHECK_EQ(C.st_inject, 5);
	/* the worst one (SGI 6, 0xb0) went back to the model, 11 took its place */
	CHECK_EQ(guest_take(), 11);
	CHECK_EQ(guest_take(), 3);
	CHECK_EQ(guest_take(), 4);
	CHECK_EQ(guest_take(), 5);
	CHECK_EQ(guest_take(), 1023);
	service();                                      /* harvest + refill: SGI 6 comes back */
	CHECK_EQ(guest_take(), 6);
	service();
	CHECK_EQ(guest_take(), 1023);
	CHECK_EQ(C.st_inject, 6);                       /* SGI 6 injected twice, delivered once */
}

static void t_no_evict_of_active_or_equal(void)
{
	uint32_t id;

	setup_vcif();
	pend_sgi(3, 0x80); pend_sgi(4, 0x80); pend_sgi(5, 0x80); pend_sgi(6, 0x80);
	vcif_fill(&C, &F.v, 0);
	pend_sgi(7, 0x80);                              /* equal priority: waits */
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.st_evict, 0);
	CHECK(vcif_overflow(&C, &F.v, 0));
	/* acknowledge one (now active); a better interrupt may evict only pending-only LRs */
	id = vcif_iar1(&C);
	CHECK_EQ(id, 3);
	pend_sgi(8, 0x20);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.st_evict, 1);                        /* one of the pending-only LRs, not the active one */
	CHECK_EQ(vcif_valid_lrs(&C), 4);
	/* all four LRs hold the active one + 8 + two of the 0x80 ones */
	CHECK_EQ(vcif_iar1(&C), 8);
}

static void t_all_active_blocks(void)
{
	setup_vcif();
	/* four nested priorities, each acknowledged before the next arrives */
	pend_sgi(3, 0x80);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 3);
	pend_sgi(4, 0x60);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 4);
	pend_sgi(5, 0x40);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 5);
	pend_sgi(6, 0x20);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 6);
	pend_sgi(7, 0x10);                              /* no free LR, none evictable */
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.st_evict, 0);
	CHECK(vcif_overflow(&C, &F.v, 0));
	CHECK_EQ(vcif_valid_lrs(&C), 4);
	/* unwind: EOI the innermost, refill brings 7 in, preempting nothing yet */
	vcif_eoir1(&C, 6);
	service();
	CHECK_EQ(vcif_iar1(&C), 7);                     /* 0x10 < running 0x40 */
	CHECK_EQ(vcif_rpr(&C), 0x10);
}

static void t_iar_masks(void)
{
	setup_vcif();
	pend_sgi(3, 0x80);
	vcif_fill(&C, &F.v, 0);

	vcif_pmr_set(&C, 0x80);                         /* priority 0x80 is not < PMR 0x80 */
	CHECK_EQ(vcif_iar1(&C), 1023);
	vcif_pmr_set(&C, 0x88);
	vcif_igrpen_set(&C, 1, 0);                      /* group disabled */
	CHECK_EQ(vcif_iar1(&C), 1023);
	CHECK_EQ(vcif_hppir1(&C), 1023);
	vcif_igrpen_set(&C, 1, 1);
	CHECK_EQ(vcif_hppir1(&C), 3);                   /* HPPIR does not look at PMR */
	CHECK_EQ(vcif_iar1(&C), 3);
	CHECK_EQ(vcif_rpr(&C), 0x80);
	CHECK_EQ(vcif_hppir1(&C), 1023);                /* nothing pending any more */
}

static void t_preemption_and_binary_point(void)
{
	setup_vcif();
	pend_sgi(3, 0x80);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 3);                     /* running group priority 0x80 (BPR1 min = 3: mask 0xf0) */
	pend_sgi(4, 0x88);                              /* same group (0x80 after masking) */
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 1023);                  /* subpriority only: no preemption */
	vcif_bpr1_set(&C, 0);                           /* below the minimum: clamped to 3 */
	CHECK_EQ(vcif_bpr1_get(&C), 3);
	vcif_bpr1_set(&C, 2);
	CHECK_EQ(vcif_bpr1_get(&C), 3);
	pend_sgi(5, 0x70);                              /* better group: preempts */
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 5);
	CHECK_EQ(vcif_rpr(&C), 0x70);
	vcif_eoir1(&C, 5);                              /* drop 0x70 and deactivate */
	CHECK_EQ(vcif_rpr(&C), 0x80);
	vcif_eoir1(&C, 3);
	CHECK_EQ(vcif_rpr(&C), 0xff);
	CHECK_EQ(C.eoicount, 0);
	/* now 4 (0x88) is signalled */
	CHECK_EQ(vcif_iar1(&C), 4);
}

static void t_eoimode_and_dir(void)
{
	uint32_t freed[4];

	setup_vcif();
	vcif_ctlr_set(&C, 2);                           /* EOImode = 1 */
	CHECK_EQ(vcif_ctlr_get(&C) & 3, 2);
	pend_sgi(3, 0x40);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 3);
	vcif_eoir1(&C, 3);                              /* priority drop only */
	CHECK_EQ(vcif_rpr(&C), 0xff);
	CHECK_EQ(vcif_harvest(&C, &F.v, 0, freed, 4), 0);  /* still active in the LR */
	vcif_dir(&C, 3);
	CHECK_EQ(vcif_harvest(&C, &F.v, 0, freed, 4), 1);
	CHECK_EQ(freed[0], 3);
	vcif_dir(&C, 3);                                /* nothing active: counted */
	CHECK_EQ(C.eoicount, 1);
	/* EOImode 0: DIR is ignored */
	vcif_ctlr_set(&C, 0);
	vcif_dir(&C, 3);
	CHECK_EQ(C.eoicount, 1);
}

static void t_eoir_special_and_unknown(void)
{
	setup_vcif();
	pend_sgi(3, 0x40);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 3);
	vcif_eoir1(&C, 1023);                           /* special: no priority drop */
	CHECK_EQ(vcif_rpr(&C), 0x40);
	vcif_eoir1(&C, 77);                             /* no matching LR: drop yes, EOIcount++ */
	CHECK_EQ(C.eoicount, 1);
	CHECK_EQ(vcif_rpr(&C), 0xff);
}

static void t_ctlr_pmr_bpr(void)
{
	setup_vcif();
	CHECK_EQ(vcif_ctlr_get(&C), (4u << 8) | (1u << 15));    /* PRIbits=4, A3V, SEIS 0, IDbits 0 */
	vcif_pmr_set(&C, 0xf7);                         /* unimplemented low bits read as 0 */
	CHECK_EQ(vcif_pmr_get(&C), 0xf0);
	CHECK_EQ(vcif_bpr0_get(&C), 2);
	CHECK_EQ(vcif_bpr1_get(&C), 3);
	vcif_ctlr_set(&C, 1);                           /* CBPR: Group 1 uses BPR0 + 1 */
	CHECK_EQ(vcif_bpr1_get(&C), 3);
	vcif_bpr1_set(&C, 7);                           /* ignored */
	vcif_bpr0_set(&C, 5);
	CHECK_EQ(vcif_bpr1_get(&C), 6);
	vcif_bpr0_set(&C, 7);
	CHECK_EQ(vcif_bpr1_get(&C), 7);                 /* saturated */
	vcif_igrpen_set(&C, 0, 1);
	CHECK_EQ(vcif_igrpen_get(&C, 0), 1);
	CHECK_EQ(vcif_igrpen_get(&C, 1), 1);            /* set in setup_vcif */
	vcif_ap_set(&C, 1, 0, 0x4);
	CHECK_EQ(vcif_ap_get(&C, 1, 0), 0x4);
	CHECK_EQ(vcif_ap_get(&C, 1, 1), 0);             /* PREbits 5: one register */
	CHECK_EQ(vcif_rpr(&C), 2u << 3);
}

static void t_level_requeue(void)
{
	uint32_t id = 0;

	setup_vcif();
	enable_spi(40);
	set_prio(40, 0x80);
	vgic_set_irq_level(&F.v, 0, 40, true);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.st_inject, 1);
	CHECK(!vgic_next_pending(&F.v, 0, &id, 0));     /* active in the model */
	/* give it back: a level line that is still high is pending again */
	CHECK(vgic_requeue(&F.v, 0, 40));
	CHECK(vgic_next_pending(&F.v, 0, &id, 0));
	CHECK_EQ(id, 40);
	CHECK(!vgic_requeue(&F.v, 0, 40));              /* not active any more */
	CHECK(!vgic_requeue(&F.v, 0, 5000));            /* bad INTID */
}

static void t_noeoi_knob_only_sgis(void)
{
	setup_vcif();
	C.no_eoi_maint = true;
	pend_sgi(3, 0x40);
	enable_spi(40);
	set_prio(40, 0x50);
	vgic_set_irq_level(&F.v, 0, 40, true);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.lr[0] & VCIF_LR_EOI, 0);             /* SGI 3: no EOI maintenance */
	CHECK(C.lr[1] & VCIF_LR_EOI);                   /* SPI 40: keeps it (level line) */
}

static void t_dirty_tracking(void)
{
	setup_vcif();
	C.lr_dirty = 0;
	C.vmcr_dirty = C.ap_dirty = false;
	pend_sgi(3, 0x40);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.lr_dirty, 1);                        /* only LR 0 */
	C.lr_dirty = 0;
	vcif_iar1(&C);
	CHECK_EQ(C.lr_dirty, 1);
	CHECK(C.ap_dirty);
	CHECK(!C.vmcr_dirty);
	vcif_pmr_set(&C, 0x10);
	CHECK(C.vmcr_dirty);
}

/* An LPI above 32767 (IDbits = 16 advertised): owner[] must not truncate it to "free LR". */
static void t_high_lpi_through_lr(void)
{
	uint32_t freed[16];

	setup_vcif();
	CHECK(vgic_lpi_inject(&F.v, 0, 40000, 0x40));
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.owner[0], 40000);
	CHECK_EQ(vcif_iar1(&C), 40000);
	vcif_eoir1(&C, 40000);
	CHECK_EQ(vcif_harvest(&C, &F.v, 0, freed, 16), 1);
	CHECK_EQ(freed[0], 40000);
	CHECK(C.owner[0] == -1);
	CHECK(!vgic_next_pending(&F.v, 0, &freed[1], NULL));   /* nothing stays in flight */
}

int main(void)
{
	RUN(t_overflow_refill_in_priority_order);
	RUN(t_evict_pending_for_higher_priority);
	RUN(t_no_evict_of_active_or_equal);
	RUN(t_all_active_blocks);
	RUN(t_iar_masks);
	RUN(t_preemption_and_binary_point);
	RUN(t_eoimode_and_dir);
	RUN(t_eoir_special_and_unknown);
	RUN(t_ctlr_pmr_bpr);
	RUN(t_level_requeue);
	RUN(t_noeoi_knob_only_sgis);
	RUN(t_dirty_tracking);
	RUN(t_high_lpi_through_lr);
	TESTS_DONE();
}
