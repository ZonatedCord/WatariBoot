/* SPDX-License-Identifier: MIT */
/* Host tests: pending/priority selection, routing, ack/EOI, level semantics. */
#include "test.h"

static uint32_t next(unsigned cpu, uint8_t *prio)
{
	uint32_t id = 0;
	uint8_t p = 0;
	if (!vgic_next_pending(&F.v, cpu, &id, &p))
		return VGIC_INTID_SPURIOUS;
	if (prio)
		*prio = p;
	return id;
}

static void t_nothing_pending(void)
{
	setup(1);
	bring_up();
	CHECK_EQ(next(0, NULL), 1023);
	CHECK_EQ(vgic_ack(&F.v, 0), 1023);
}

static void t_needs_enable_and_pending(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	CHECK_EQ(next(0, NULL), 1023);               /* enabled, not pending */
	wr32(D(0x284), 0);
	vgic_set_pending(&F.v, 0, 41);               /* pending, not enabled */
	CHECK_EQ(next(0, NULL), 1023);
	vgic_set_pending(&F.v, 0, 40);
	CHECK_EQ(next(0, NULL), 40);
}

static void t_priority_selection(void)
{
	uint8_t p;
	setup(1);
	bring_up();
	enable_spi(40); enable_spi(41); enable_spi(42);
	set_prio(40, 0x80); set_prio(41, 0x20); set_prio(42, 0x50);
	vgic_set_pending(&F.v, 0, 40);
	vgic_set_pending(&F.v, 0, 42);
	CHECK_EQ(next(0, &p), 42);                   /* lower value = higher priority */
	CHECK_EQ(p, 0x50);
	vgic_set_pending(&F.v, 0, 41);
	CHECK_EQ(next(0, &p), 41);
	CHECK_EQ(p, 0x20);
}

static void t_priority_tie_lowest_intid(void)
{
	setup(1);
	bring_up();
	enable_spi(60); enable_spi(45);
	vgic_set_pending(&F.v, 0, 60);
	vgic_set_pending(&F.v, 0, 45);
	CHECK_EQ(next(0, NULL), 45);
}

static void t_private_beats_spi_by_priority(void)
{
	setup(1);
	bring_up();
	enable_priv(0, 27);
	enable_spi(40);
	wr8(S(0, 0x400 + 27), 0x10);
	set_prio(40, 0x40);
	vgic_set_pending(&F.v, 0, 40);
	vgic_set_pending(&F.v, 0, 27);
	CHECK_EQ(next(0, NULL), 27);
}

static void t_active_excluded(void)
{
	setup(1);
	bring_up();
	enable_spi(40); enable_spi(41);
	set_prio(40, 0x10); set_prio(41, 0x80);
	vgic_set_pending(&F.v, 0, 40); vgic_set_pending(&F.v, 0, 41);
	wr32(D(0x308 - 8 + 4), 1u << 8);             /* ISACTIVER1: INTID 40 active */
	CHECK_EQ(next(0, NULL), 41);
}

static void t_group_enable_gating(void)
{
	setup(1);
	wr32(R(0, 0x14), 0);
	enable_spi(40);                              /* Group 1 */
	vgic_set_pending(&F.v, 0, 40);
	CHECK_EQ(next(0, NULL), 1023);               /* GICD_CTLR enables both off */
	wr32(D(0), 0x1);                             /* only Group 0 */
	CHECK_EQ(next(0, NULL), 1023);
	wr32(D(0), 0x2);
	CHECK_EQ(next(0, NULL), 40);
}

static void t_group0_needs_enablegrp0(void)
{
	setup(1);
	wr32(R(0, 0x14), 0);
	wr32(D(0x104), 1u << 8);                     /* enable INTID 40, group 0 */
	vgic_set_pending(&F.v, 0, 40);
	wr32(D(0), 0x2);
	CHECK_EQ(next(0, NULL), 1023);
	wr32(D(0), 0x3);
	CHECK_EQ(next(0, NULL), 40);
}

static void t_routing_to_specific_vcpu(void)
{
	setup(4);
	bring_up();
	enable_spi(48);
	wr64(D(0x6000 + 8 * 48), 2);
	vgic_set_pending(&F.v, 0, 48);
	CHECK_EQ(next(0, NULL), 1023);
	CHECK_EQ(next(1, NULL), 1023);
	CHECK_EQ(next(2, NULL), 48);
	CHECK_EQ(next(3, NULL), 1023);
}

static void t_default_route_is_vcpu0(void)
{
	setup(2);
	bring_up();
	enable_spi(48);
	vgic_set_pending(&F.v, 0, 48);
	CHECK_EQ(next(0, NULL), 48);
	CHECK_EQ(next(1, NULL), 1023);
}

static void t_irm_picks_first_awake(void)
{
	setup(3);
	wr32(D(0), 0x3);
	enable_spi(48);
	wr64(D(0x6000 + 8 * 48), 1ull << 31);        /* IRM = 1 */
	vgic_set_pending(&F.v, 0, 48);
	CHECK_EQ(vgic_route_target(&F.v, 48), -1);   /* nobody awake yet */
	wr32(R(1, 0x14), 0);
	CHECK_EQ(vgic_route_target(&F.v, 48), 1);
	CHECK_EQ(next(1, NULL), 48);
	wr32(R(0, 0x14), 0);
	CHECK_EQ(vgic_route_target(&F.v, 48), 0);    /* vCPU 0 preferred when awake */
	CHECK_EQ(next(1, NULL), 1023);
}

static void t_sleeping_redistributor_blocks(void)
{
	setup(1);
	wr32(D(0), 0x3);
	enable_priv(0, 30);
	vgic_set_pending(&F.v, 0, 30);
	CHECK_EQ(next(0, NULL), 1023);               /* ProcessorSleep = 1 */
	wr32(R(0, 0x14), 0);
	CHECK_EQ(next(0, NULL), 30);
}

static void t_ack_edge_clears_pending_sets_active(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	vgic_set_pending(&F.v, 0, 40);
	CHECK_EQ(vgic_ack(&F.v, 0), 40);
	CHECK_EQ(rd32(D(0x204)) & (1u << 8), 0);     /* ISPENDR1 */
	CHECK_EQ(rd32(D(0x304)) & (1u << 8), 1u << 8); /* ISACTIVER1 */
	CHECK_EQ(vgic_ack(&F.v, 0), 1023);           /* nothing else */
}

static void t_eoi_deactivates(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	vgic_set_pending(&F.v, 0, 40);
	vgic_ack(&F.v, 0);
	CHECK(vgic_eoi(&F.v, 0, 40));
	CHECK_EQ(rd32(D(0x304)), 0);
	CHECK(!vgic_eoi(&F.v, 0, 5000));
	vgic_set_pending(&F.v, 0, 40);               /* can fire again */
	CHECK_EQ(vgic_ack(&F.v, 0), 40);
}

/* An MSI is a pulse (line high then low before the guest looks): it must stay pending. */
static void t_edge_pulse_stays_pending(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	wr32(D(0xC08), 2u << 16);
	vgic_set_irq_level(&F.v, 0, 40, true);
	vgic_set_irq_level(&F.v, 0, 40, false);
	CHECK_EQ(vgic_ack(&F.v, 0), 40);
	CHECK_EQ(vgic_ack(&F.v, 0), 1023);           /* one pulse = one interrupt */
	vgic_eoi(&F.v, 0, 40);
	vgic_set_irq_level(&F.v, 0, 40, true);       /* a second pulse while idle */
	vgic_set_irq_level(&F.v, 0, 40, false);
	CHECK_EQ(next(0, NULL), 40);
}

static void t_edge_line_pends_once(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	wr32(D(0xC08), 2u << 16);                    /* INTID 40 edge (field 8) */
	CHECK_EQ(vgic_set_irq_level(&F.v, 0, 40, true), 0);
	CHECK_EQ(vgic_ack(&F.v, 0), 40);
	vgic_eoi(&F.v, 0, 40);
	CHECK_EQ(next(0, NULL), 1023);               /* line still high: no re-pend */
	vgic_set_irq_level(&F.v, 0, 40, false);
	vgic_set_irq_level(&F.v, 0, 40, true);       /* new rising edge */
	CHECK_EQ(next(0, NULL), 40);
}

static void t_level_repends_after_eoi_while_high(void)
{
	setup(1);
	bring_up();
	enable_spi(40);                              /* SPIs default to level */
	vgic_set_irq_level(&F.v, 0, 40, true);
	CHECK_EQ(vgic_ack(&F.v, 0), 40);
	CHECK_EQ(next(0, NULL), 1023);               /* active: not re-delivered yet */
	vgic_eoi(&F.v, 0, 40);
	CHECK_EQ(next(0, NULL), 40);                 /* line still high -> pending again */
	CHECK_EQ(vgic_ack(&F.v, 0), 40);
}

static void t_level_no_repend_after_line_drops(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	vgic_set_irq_level(&F.v, 0, 40, true);
	vgic_ack(&F.v, 0);
	vgic_set_irq_level(&F.v, 0, 40, false);
	vgic_eoi(&F.v, 0, 40);
	CHECK_EQ(next(0, NULL), 1023);
}

static void t_level_pending_drops_with_line(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	vgic_set_irq_level(&F.v, 0, 40, true);
	CHECK_EQ(rd32(D(0x204)) & (1u << 8), 1u << 8);
	vgic_set_irq_level(&F.v, 0, 40, false);
	CHECK_EQ(rd32(D(0x204)) & (1u << 8), 0);
}

static void t_icpendr_does_not_clear_high_level_line(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	vgic_set_irq_level(&F.v, 0, 40, true);
	wr32(D(0x284), 1u << 8);
	CHECK_EQ(next(0, NULL), 40);
}

static void t_ppi_per_vcpu(void)
{
	setup(2);
	bring_up();
	enable_priv(0, 27);
	enable_priv(1, 27);
	vgic_set_irq_level(&F.v, 1, 27, true);
	CHECK_EQ(next(0, NULL), 1023);
	CHECK_EQ(next(1, NULL), 27);
	CHECK_EQ(vgic_ack(&F.v, 1), 27);
}

static void t_software_pend_via_mmio(void)
{
	setup(1);
	bring_up();
	enable_spi(40);
	wr32(D(0x204), 1u << 8);                     /* ISPENDR1 */
	CHECK_EQ(next(0, NULL), 40);
	wr32(D(0x284), 1u << 8);                     /* ICPENDR1 */
	CHECK_EQ(next(0, NULL), 1023);
}

static void t_sgi_pend_via_ispendr0(void)
{
	setup(2);
	bring_up();
	enable_priv(1, 3);
	wr32(S(1, 0x200), 1u << 3);
	CHECK_EQ(next(1, NULL), 3);
	CHECK_EQ(vgic_ack(&F.v, 1), 3);
	CHECK_EQ(rd32(S(1, 0x200)), 0);
	CHECK_EQ(vgic_set_irq_level(&F.v, 1, 3, true), -1); /* SGIs have no line */
}

static void t_set_level_rejects_bad_intid(void)
{
	setup(1);
	CHECK_EQ(vgic_set_irq_level(&F.v, 0, 960, true), -1);
	CHECK_EQ(vgic_set_irq_level(&F.v, 3, 20, true), -1);
}

int main(void)
{
	RUN(t_nothing_pending);
	RUN(t_needs_enable_and_pending);
	RUN(t_priority_selection);
	RUN(t_priority_tie_lowest_intid);
	RUN(t_private_beats_spi_by_priority);
	RUN(t_active_excluded);
	RUN(t_group_enable_gating);
	RUN(t_group0_needs_enablegrp0);
	RUN(t_routing_to_specific_vcpu);
	RUN(t_default_route_is_vcpu0);
	RUN(t_irm_picks_first_awake);
	RUN(t_sleeping_redistributor_blocks);
	RUN(t_ack_edge_clears_pending_sets_active);
	RUN(t_eoi_deactivates);
	RUN(t_edge_line_pends_once);
	RUN(t_level_repends_after_eoi_while_high);
	RUN(t_level_no_repend_after_line_drops);
	RUN(t_level_pending_drops_with_line);
	RUN(t_icpendr_does_not_clear_high_level_line);
	RUN(t_ppi_per_vcpu);
	RUN(t_edge_pulse_stays_pending);
	RUN(t_software_pend_via_mmio);
	RUN(t_sgi_pend_via_ispendr0);
	RUN(t_set_level_rejects_bad_intid);
	TESTS_DONE();
}
