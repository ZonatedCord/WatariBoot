/* SPDX-License-Identifier: MIT */
/* Host tests: LPI pass-through (M2): GICD_TYPER/GICR_TYPER bits, GICR LPI registers forwarded
 * through the lpi_reg hook, the per-vCPU LPI queue (inject, priority order, ack, EOI, requeue). */
#include "test.h"

static int n_reg;
static unsigned last_cpu, last_off;
static uint64_t last_val;

static void lpi_cb(void *c, unsigned cpu, unsigned off, uint64_t val)
{
	(void)c;
	n_reg++; last_cpu = cpu; last_off = off; last_val = val;
}

static void setup_lpi(unsigned n)
{
	struct vgic_config cfg = { GICD, GICR, n, NULL, { fx_log, fx_eff, NULL, lpi_cb }, true, 0 };

	n_reg = 0;
	F.logs = F.effects = 0;
	if (vgic_init(&F.v, &cfg))
		printf("    vgic_init failed\n");
}

static void t_typer_bits(void)
{
	setup_lpi(2);
	CHECK(rd32(D(4)) & (1u << 17));                    /* GICD_TYPER.LPIS */
	CHECK_EQ((rd32(D(4)) >> 19) & 0x1f, 15);           /* IDbits = 16 bits */
	CHECK_EQ(rd32(R(0, 8)) & 1, 1);                    /* GICR_TYPER.PLPIS */
	CHECK_EQ(rd32(R(1, 8)) & 0x11, 0x11);              /* PLPIS + Last on the last vCPU */
	setup(2);                                          /* default: no LPIs */
	CHECK(!(rd32(D(4)) & (1u << 17)));
	CHECK_EQ(rd32(R(0, 8)) & 1, 0);
}

static void t_registers_forwarded(void)
{
	setup_lpi(2);
	wr64(R(1, 0x70), 0x40000000ull | 15);              /* PROPBASER, 64-bit */
	CHECK_EQ(n_reg, 1); CHECK_EQ(last_cpu, 1); CHECK_EQ(last_off, 0x70); CHECK_EQ(last_val, 0x4000000full);
	CHECK_EQ(rd64(R(1, 0x70)), 0x4000000full);
	CHECK_EQ(vgic_lpi_propbaser(&F.v, 1), 0x4000000full);
	CHECK_EQ(vgic_lpi_propbaser(&F.v, 0), 0);
	wr32(R(0, 0x78), 0x50000000u);                     /* PENDBASER as two 32-bit halves */
	wr32(R(0, 0x7C), 0x1);
	CHECK_EQ(last_off, 0x78); CHECK_EQ(last_val, 0x150000000ull);
	wr64(R(0, 0x78), (1ull << 62) | 0x60000000ull);    /* PTZ is write-only */
	CHECK_EQ(rd64(R(0, 0x78)), 0x60000000ull);
	wr32(R(0, 0), 1);                                  /* EnableLPIs */
	CHECK_EQ(last_off, 0); CHECK_EQ(last_val, 1); CHECK_EQ(rd32(R(0, 0)), 1);
	wr64(R(0, 0xA0), 0x2005);                          /* INVLPIR forwarded, SYNCR reads 0 */
	CHECK_EQ(last_off, 0xA0); CHECK_EQ(last_val, 0x2005);
	CHECK_EQ(rd32(R(0, 0xC0)), 0);
	vgic_vcpu_reset(&F.v, 0);                          /* CPU_OFF clears the LPI state */
	CHECK_EQ(rd32(R(0, 0)), 0);
}

static uint32_t next(unsigned cpu)
{
	uint32_t id = 0;
	return vgic_next_pending(&F.v, cpu, &id, NULL) ? id : VGIC_INTID_SPURIOUS;
}

static void t_queue_priority_and_lifecycle(void)
{
	setup_lpi(2);
	bring_up();
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
	CHECK(vgic_lpi_inject(&F.v, 0, 8200, 0xA0));
	CHECK(vgic_lpi_inject(&F.v, 0, 8300, 0x40));       /* better priority */
	CHECK(vgic_lpi_inject(&F.v, 0, 8200, 0xA0));       /* duplicate: still one pending bit */
	CHECK_EQ(next(1), VGIC_INTID_SPURIOUS);            /* per vCPU */
	CHECK_EQ(next(0), 8300);
	CHECK_EQ(vgic_ack(&F.v, 0), 8300);
	CHECK_EQ(next(0), 8200);                           /* the in-flight one is not offered twice */
	CHECK(vgic_requeue(&F.v, 0, 8300));                /* evicted LR: pending again */
	CHECK_EQ(next(0), 8300);
	CHECK_EQ(vgic_ack(&F.v, 0), 8300);
	CHECK(vgic_eoi(&F.v, 0, 8300));
	CHECK(!vgic_eoi(&F.v, 0, 8300));                   /* not in flight any more */
	CHECK_EQ(vgic_ack(&F.v, 0), 8200);
	CHECK(vgic_eoi(&F.v, 0, 8200));
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
	CHECK(!vgic_lpi_inject(&F.v, 0, 100, 0));          /* not an LPI */
}

static void t_queue_full_and_group_gate(void)
{
	unsigned i;

	setup_lpi(1);
	bring_up();
	for (i = 0; i < VGIC_LPI_QUEUE; i++)
		CHECK(vgic_lpi_inject(&F.v, 0, 8192 + i, 0x80));
	CHECK(!vgic_lpi_inject(&F.v, 0, 9000, 0x80));      /* full: reported, never silent */
	CHECK_EQ(next(0), 8192);                           /* ties: lowest INTID */
	wr32(D(0), 0x1);                                   /* Group 1 disabled: nothing deliverable */
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
}

/* #64: overflow of the LPI queue is counted per vCPU (and the stats line prints it). */
static void t_queue_overflow_counted(void)
{
	setup_lpi(2);
	bring_up();
	CHECK_EQ(vgic_lpi_dropped(&F.v, 0), 0);
	for (unsigned i = 0; i < VGIC_LPI_QUEUE; i++)
		CHECK(vgic_lpi_inject(&F.v, 0, 8192 + i, 0x80));
	CHECK(vgic_lpi_inject(&F.v, 0, 8192, 0x80));       /* duplicate of a queued one: not a drop */
	CHECK_EQ(vgic_lpi_dropped(&F.v, 0), 0);
	CHECK(!vgic_lpi_inject(&F.v, 0, 9000, 0x80));
	CHECK(!vgic_lpi_inject(&F.v, 0, 9001, 0x80));
	CHECK_EQ(vgic_lpi_dropped(&F.v, 0), 2);
	CHECK_EQ(vgic_lpi_dropped(&F.v, 1), 0);            /* per vCPU */
	CHECK(!vgic_lpi_inject(&F.v, 0, 100, 0x80));       /* below 8192: refused, not an overflow */
	CHECK_EQ(vgic_lpi_dropped(&F.v, 0), 2);
	CHECK_EQ(vgic_lpi_dropped(&F.v, 7), 0);            /* bad vCPU */
}

/* #64: IDbits comes from the physical GICD_TYPER (config), default 16 (GIC spec 12.9.4 GICD_TYPER.IDbits). */
static void t_idbits_from_config(void)
{
	struct vgic_config cfg = { GICD, GICR, 1, NULL, { fx_log, fx_eff, NULL, lpi_cb }, true, 24 };

	setup_lpi(1);
	CHECK_EQ(vgic_idbits(&F.v), 16);
	CHECK_EQ((rd32(D(4)) >> 19) & 0x1f, 15);
	CHECK_EQ(vgic_init(&F.v, &cfg), 0);
	CHECK_EQ(vgic_idbits(&F.v), 24);
	CHECK_EQ((rd32(D(4)) >> 19) & 0x1f, 23);
	CHECK(vgic_lpi_inject(&F.v, 0, 0xffffff, 0x80));   /* the largest 24-bit INTID */
	cfg.idbits = 14;                                   /* an LPI INTID needs 14 bits at least */
	CHECK_EQ(vgic_init(&F.v, &cfg), 0);
	CHECK_EQ(vgic_idbits(&F.v), 14);
	CHECK(vgic_lpi_inject(&F.v, 0, 16383, 0x80));
	CHECK(!vgic_lpi_inject(&F.v, 0, 16384, 0x80));     /* not representable: refused and counted */
	CHECK_EQ(vgic_lpi_dropped(&F.v, 0), 1);
	cfg.idbits = 40;                                   /* clamped */
	CHECK_EQ(vgic_init(&F.v, &cfg), 0);
	CHECK_EQ(vgic_idbits(&F.v), 24);
	cfg.idbits = 3;
	CHECK_EQ(vgic_init(&F.v, &cfg), 0);
	CHECK_EQ(vgic_idbits(&F.v), 14);
}

static void t_spi_beats_equal_priority_lpi(void)
{
	setup_lpi(1);
	bring_up();
	enable_spi(40);
	vgic_set_irq_level(&F.v, 0, 40, true);
	CHECK(vgic_lpi_inject(&F.v, 0, 8192, 0x00));       /* same priority as the SPI (reset value 0) */
	CHECK_EQ(next(0), 40);                             /* ties: lowest INTID, so the SPI first */
	CHECK_EQ(vgic_ack(&F.v, 0), 40);
	CHECK_EQ(next(0), 8192);
}

static void t_duplicates_bounded(void)
{
	unsigned i, pending = 0, inflight = 0;

	setup_lpi(1);
	bring_up();
	CHECK(vgic_lpi_inject(&F.v, 0, 8500, 0x40));
	CHECK_EQ(vgic_ack(&F.v, 0), 8500);                 /* in flight */
	for (i = 0; i < 1000; i++)                         /* a storming device */
		CHECK(vgic_lpi_inject(&F.v, 0, 8500, 0x40));
	for (i = 0; i < VGIC_LPI_QUEUE; i++) {
		if (F.v.vcpu[0].lpi[i].id == 8500)
			F.v.vcpu[0].lpi[i].inflight ? inflight++ : pending++;
	}
	CHECK_EQ(inflight, 1);
	CHECK_EQ(pending, 1);                              /* at most 1 + 1 */
	CHECK(vgic_lpi_inject(&F.v, 0, 9000, 0x40));       /* other ids still fit */
	/* eviction of the in-flight copy while a pending one exists: merge, no second pending copy */
	CHECK(vgic_requeue(&F.v, 0, 8500));
	pending = 0;
	for (i = 0; i < VGIC_LPI_QUEUE; i++)
		if (F.v.vcpu[0].lpi[i].id == 8500)
			pending++;
	CHECK_EQ(pending, 1);
}

static void t_base_writes_ignored_while_enabled(void)
{
	setup_lpi(1);
	wr64(R(0, 0x70), 0x40000000ull | 15);
	wr64(R(0, 0x78), 0x50000000ull);
	wr32(R(0, 0), 1);
	n_reg = 0;
	wr64(R(0, 0x70), 0x80000000ull);                   /* UNPREDICTABLE while enabled: ignored */
	wr32(R(0, 0x78), 0x60000000u);
	wr32(R(0, 0x74), 0x1);
	CHECK_EQ(n_reg, 0);
	CHECK_EQ(rd64(R(0, 0x70)), 0x4000000full);
	CHECK(vgic_lpi_enabled(&F.v, 0));
	wr32(R(0, 0), 0);
	CHECK(!vgic_lpi_enabled(&F.v, 0));
	wr64(R(0, 0x70), 0x80000000ull);                   /* accepted again once disabled */
	CHECK_EQ(n_reg, 2);                                /* the CTLR write and this one */
}

int main(void)
{
	RUN(t_typer_bits);
	RUN(t_registers_forwarded);
	RUN(t_queue_priority_and_lifecycle);
	RUN(t_queue_full_and_group_gate);
	RUN(t_spi_beats_equal_priority_lpi);
	RUN(t_duplicates_bounded);
	RUN(t_queue_overflow_counted);
	RUN(t_idbits_from_config);
	RUN(t_base_writes_ignored_while_enabled);
	TESTS_DONE();
}
