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


/* #60 cross-core kick: vcif_fill_can_progress() tells another core whether a flush there would
 * change anything (free LR, or an evictable pending-only LR of strictly worse priority). */
static void t_kick_predicate(void)
{
	setup_vcif();
	CHECK(!vcif_fill_can_progress(&C, &F.v, 0));            /* nothing pending */
	pend_sgi(3, 0x80); pend_sgi(4, 0x90); pend_sgi(5, 0xa0);
	CHECK(vcif_fill_can_progress(&C, &F.v, 0));             /* free LRs */
	vcif_fill(&C, &F.v, 0);
	CHECK(!vcif_fill_can_progress(&C, &F.v, 0));            /* flushed: nothing left to do */
	pend_sgi(6, 0xb0);
	vcif_fill(&C, &F.v, 0);                                 /* all four LRs taken, all pending-only */
	pend_sgi(7, 0xb0);
	CHECK(!vcif_fill_can_progress(&C, &F.v, 0));            /* equal to the worst LR: waits */
	pend_sgi(11, 0x08);
	CHECK(vcif_fill_can_progress(&C, &F.v, 0));             /* better than the worst: evicts, kick */
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(C.st_evict, 1);
	CHECK(!vcif_fill_can_progress(&C, &F.v, 0));            /* no kick storm after the eviction */
}

static void t_kick_predicate_active_blocks(void)
{
	setup_vcif();
	pend_sgi(3, 0x80); pend_sgi(4, 0x90); pend_sgi(5, 0xa0); pend_sgi(6, 0xb0);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 3);                             /* 3 active */
	CHECK_EQ(vcif_iar1(&C), 1023);                          /* 4 is 0x90: not above running 0x80 */
	/* evictable LRs remain (4, 5, 6), so a better SGI would evict */
	pend_sgi(11, 0x08);
	CHECK(vcif_fill_can_progress(&C, &F.v, 0));
	vcif_fill(&C, &F.v, 0);
	/* make every LR active: nothing can be evicted any more */
	CHECK_EQ(vcif_iar1(&C), 11);
	CHECK_EQ(vcif_iar1(&C), 1023);
	for (unsigned i = 0; i < C.nlr; i++)
		C.lr[i] = (C.lr[i] & ~VCIF_LR_STATE_MASK) | VCIF_LR_ACTIVE;
	pend_sgi(12, 0x00);
	CHECK(!vcif_fill_can_progress(&C, &F.v, 0));
}

/* #60: ICC_HPPIR1_EL1 (GIC spec IHI 0069H.b 12.2.12 / 12.3.10) reports the highest priority
 * pending interrupt without the "of sufficient priority" qualifier that ICC_IAR1_EL1 carries
 * (12.2.10): neither PMR nor the running priority hide it. Only group enable / observability
 * does (special INTID 1023). */
static void t_hppir_ignores_pmr_and_running_priority(void)
{
	setup_vcif();
	pend_sgi(3, 0x40);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 3);                     /* running priority 0x40 */
	pend_sgi(4, 0x60);                              /* worse than running: not signalled */
	vcif_fill(&C, &F.v, 0);
	vcif_pmr_set(&C, 0x20);                         /* ... and masked by PMR as well */
	CHECK_EQ(vcif_iar1(&C), 1023);
	CHECK_EQ(vcif_hppir1(&C), 4);                   /* but HPPIR still names it */
	pend_sgi(5, 0x50);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_hppir1(&C), 5);                   /* highest priority wins */
	pend_sgi(6, 0x50);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_hppir1(&C), 5);                   /* tie: lowest INTID */
	vcif_igrpen_set(&C, 1, 0);
	CHECK_EQ(vcif_hppir1(&C), 1023);                /* group disabled: not observable */
}

/* #60: ICH_VTR_EL2.PREbits / PRIbits other than 5 (GIC spec 12.4.9; AP0R<n>/AP1R<n> exist for
 * n < 2^(PREbits-5), 12.4.1/12.4.2). */
static void setup_widths(unsigned pri, unsigned pre)
{
	setup(1);
	bring_up();
	vcif_init(&C, 4, pri, pre);
	vcif_igrpen_set(&C, 1, 1);
	vcif_pmr_set(&C, 0xff);
}

static void t_preemption_widths(void)
{
	/* ICH_VTR_EL2: PRIbits 5..7 (12.4.9), PREbits 5..PRIbits */
	static const unsigned pri[]  = { 5, 6, 7, 7, 6, 7 };
	static const unsigned pre[]  = { 5, 6, 6, 7, 5, 5 };
	static const unsigned nap[]  = { 1, 2, 2, 4, 1, 1 };

	for (unsigned k = 0; k < 6; k++) {
		unsigned P = pri[k], E = pre[k];
		uint32_t freed[4];

		setup_widths(P, E);
		/* register count: AP registers beyond 2^(PREbits-5) read 0 and ignore writes */
		for (unsigned n = 0; n < 4; n++) {
			vcif_ap_set(&C, 1, n, 0);
			vcif_ap_set(&C, 1, n, 0x80000000u);
			CHECK_EQ(vcif_ap_get(&C, 1, n), n < nap[k] ? 0x80000000u : 0u);
			vcif_ap_set(&C, 1, n, 0);
		}
		CHECK_EQ(vcif_ctlr_get(&C) >> 8 & 7, P - 1);
		CHECK_EQ(vcif_bpr0_get(&C), 7 - E);
		CHECK_EQ(vcif_bpr1_get(&C), 8 - E);

		/* the last (lowest) preemption level lives in the last implemented AP register */
		pend_sgi(3, (uint8_t)(((0xffu << (8 - P)) & 0xffu) - (1u << (8 - P))));   /* worst usable */
		vcif_fill(&C, &F.v, 0);
		uint32_t lr0prio = (uint32_t)(C.lr[0] >> 48) & 0xff;
		CHECK_EQ(vcif_iar1(&C), 3);
		unsigned gp = lr0prio & (0xffu << (vcif_bpr1_get(&C) + 1)) & 0xffu;
		unsigned level = gp >> (8 - E);
		CHECK_EQ(vcif_ap_get(&C, 1, level / 32), 1u << (level % 32));
		CHECK_EQ(vcif_rpr(&C), level << (8 - E));
		vcif_eoir1(&C, 3);
		CHECK_EQ(vcif_ap_get(&C, 1, level / 32), 0);
		CHECK_EQ(vcif_rpr(&C), 0xff);
		CHECK_EQ(vcif_harvest(&C, &F.v, 0, freed, 4), 1);

		/* a better level preempts one that is running: 0x80 running, 0x40 signalled */
		pend_sgi(4, 0x80); vcif_fill(&C, &F.v, 0);
		CHECK_EQ(vcif_iar1(&C), 4);
		pend_sgi(5, 0x40); vcif_fill(&C, &F.v, 0);
		CHECK_EQ(vcif_iar1(&C), 5);
		CHECK_EQ(vcif_rpr(&C), 0x40);
		vcif_eoir1(&C, 5);
		CHECK_EQ(vcif_rpr(&C), 0x80);
		vcif_eoir1(&C, 4);
		CHECK_EQ(vcif_rpr(&C), 0xff);
		service();
		CHECK_EQ(C.eoicount, 0);
	}
}

/* Levels in the upper AP registers (PREbits 6 and 7) are tracked in the right register
 * (12.4.2: register n holds the levels whose top bits are n). The Group 1 binary point cannot go
 * below BPR0min + 1 (12.3.4), so the group priority drops the lowest preemption bit. */
static void t_ap_upper_registers(void)
{
	/* PREbits 7: BPR1 min 1. The vGIC model stores 5 priority bits (granule 8), so the lowest
	 * priority it can queue is 0xf8 -> group priority 0xf8 -> level 124 = AP1R3 bit 28. */
	setup_widths(7, 7);
	CHECK_EQ(vcif_bpr1_get(&C), 1);
	pend_sgi(3, 0xfe);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 3);
	CHECK_EQ(vcif_ap_get(&C, 1, 3), 0x10000000u);
	CHECK_EQ(vcif_ap_get(&C, 1, 0), 0);
	CHECK_EQ(vcif_rpr(&C), 0xf8);
	/* priority 0x42 -> group priority 0x40 -> level 32 = AP1R1 bit 0 */
	pend_sgi(4, 0x42);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 4);
	CHECK_EQ(vcif_ap_get(&C, 1, 1), 1u);
	CHECK_EQ(vcif_rpr(&C), 0x40);
	vcif_eoir1(&C, 4);                              /* drops the lowest set bit = level 32 */
	CHECK_EQ(vcif_ap_get(&C, 1, 1), 0);
	CHECK_EQ(vcif_ap_get(&C, 1, 3), 0x10000000u);
	vcif_eoir1(&C, 3);
	CHECK_EQ(vcif_ap_get(&C, 1, 3), 0);

	/* PREbits 6: BPR1 min 2. Priority 0xfc -> 0xf8 -> level 62 = AP1R1 bit 30 */
	setup_widths(7, 6);
	CHECK_EQ(vcif_bpr1_get(&C), 2);
	pend_sgi(3, 0xfc);
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 3);
	CHECK_EQ(vcif_ap_get(&C, 1, 1), 0x40000000u);
	CHECK_EQ(vcif_ap_get(&C, 1, 2), 0);             /* does not exist */
	vcif_eoir1(&C, 3);
	CHECK_EQ(vcif_ap_get(&C, 1, 1), 0);
}

/* LR priorities below the model's 5-bit granule (hardware with PRIbits 6 or 7): the finer
 * bits count, bits beyond PRIbits are RAZ. LR written by hand. */
static void t_fine_lr_priorities(void)
{
	/* PRIbits 7, PREbits 7: priority 0x6b -> 0x6a, BPR1 1 -> group 0x68, level 52 = AP1R1 bit 20 */
	setup_widths(7, 7);
	C.lr[0] = VCIF_LR_PENDING | VCIF_LR_GROUP1 | (0x6bULL << 48) | 3;
	C.owner[0] = 3;
	CHECK_EQ(vcif_iar1(&C), 3);
	CHECK_EQ(vcif_ap_get(&C, 1, 1), 1u << 20);
	CHECK_EQ(vcif_rpr(&C), 0x68);
	/* PRIbits 6, PREbits 6: 0x6b -> 0x68, BPR1 2 -> group 0x68, level 26 = AP1R0 bit 26 */
	setup_widths(6, 6);
	C.lr[0] = VCIF_LR_PENDING | VCIF_LR_GROUP1 | (0x6bULL << 48) | 3;
	C.owner[0] = 3;
	CHECK_EQ(vcif_iar1(&C), 3);
	CHECK_EQ(vcif_ap_get(&C, 1, 0), 1u << 26);
	CHECK_EQ(vcif_rpr(&C), 0x68);
	/* PRIbits 5: PMR keeps only 5 bits too; 0x6b -> 0x68 */
	setup_widths(5, 5);
	vcif_pmr_set(&C, 0x6b);
	CHECK_EQ(vcif_pmr_get(&C), 0x68);
	vcif_pmr_set(&C, 0xff);
	CHECK_EQ(vcif_pmr_get(&C), 0xf8);
}

/* Group 0 shares the active-priority bit space: the running priority sees AP0R<n> too. */
static void t_ap0_upper_register_counts(void)
{
	setup_widths(7, 7);
	vcif_ap_set(&C, 0, 2, 1u << 5);                 /* level 69 -> priority 138 = 0x8a ... */
	CHECK_EQ(vcif_rpr(&C), (64u + 5) << 1);
	vcif_ap_set(&C, 0, 2, 0);
	vcif_ap_set(&C, 0, 3, 1u);                      /* level 96 */
	CHECK_EQ(vcif_rpr(&C), 96u << 1);
}

/* #64: ICC_CTLR_EL1.IDbits [13:11] (12.2.6): 0 = 16 bits, 1 = 24 bits; EOIR/DIR INTIDs are
 * masked to the implemented width. */
static void t_idbits(void)
{
	setup_vcif();
	CHECK_EQ(C.idbits, 16);
	CHECK_EQ((vcif_ctlr_get(&C) >> 11) & 7, 0);
	vcif_set_idbits(&C, 24);
	CHECK_EQ((vcif_ctlr_get(&C) >> 11) & 7, 1);
	CHECK_EQ(vcif_ctlr_get(&C) & 0x7ff, 4u << 8);          /* PRIbits unchanged */
	vcif_set_idbits(&C, 14);                                /* anything up to 16 bits reads as 16 */
	CHECK_EQ((vcif_ctlr_get(&C) >> 11) & 7, 0);
	vcif_set_idbits(&C, 24);
	{
		struct vgic_config cfg = { GICD, GICR, 1, NULL, { fx_log, fx_eff, NULL, NULL }, true, 24 };

		CHECK_EQ(vgic_init(&F.v, &cfg), 0);
		bring_up();
	}
	/* a 24-bit LPI survives EOIR (it would be truncated at 16 bits) */
	CHECK(vgic_lpi_inject(&F.v, 0, 0x123456, 0x40));
	vcif_fill(&C, &F.v, 0);
	CHECK_EQ(vcif_iar1(&C), 0x123456);
	vcif_eoir1(&C, 0x123456);
	CHECK_EQ(C.eoicount, 0);
	CHECK_EQ(vcif_valid_lrs(&C), 0);
}

/* The model clamps nonsense widths (PREbits above PRIbits does not exist in hardware). */
static void t_width_clamps(void)
{
	vcif_init(&C, 4, 9, 9);
	CHECK_EQ(C.pribits, 7);                         /* PRIbits is 5..7 (12.4.9) */
	CHECK_EQ(C.prebits, 7);
	vcif_init(&C, 4, 3, 3);
	CHECK_EQ(C.pribits, 5);
	CHECK_EQ(C.prebits, 5);
	vcif_init(&C, 4, 5, 7);                         /* PREbits <= PRIbits (12.4.9) */
	CHECK_EQ(C.prebits, 5);
	vcif_init(&C, 4, 6, 7);
	CHECK_EQ(C.prebits, 6);
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
	RUN(t_kick_predicate);
	RUN(t_kick_predicate_active_blocks);
	RUN(t_hppir_ignores_pmr_and_running_priority);
	RUN(t_preemption_widths);
	RUN(t_ap_upper_registers);
	RUN(t_ap0_upper_register_counts);
	RUN(t_fine_lr_priorities);
	RUN(t_width_clamps);
	RUN(t_idbits);
	TESTS_DONE();
}
