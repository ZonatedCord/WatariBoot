/* SPDX-License-Identifier: MIT */
/* Host tests: multi-vCPU routing (SGI target masks, per-vCPU private state,
 * SPI routing to the right vCPU, per-vCPU reset used by PSCI CPU_OFF). */
#include "test.h"

static uint32_t next(unsigned cpu)
{
	uint32_t id = 0;
	return vgic_next_pending(&F.v, cpu, &id, NULL) ? id : VGIC_INTID_SPURIOUS;
}

static uint64_t sgi1r(uint32_t intid, uint32_t list, bool irm)
{
	return ((uint64_t)intid << 24) | list | ((uint64_t)irm << 40);
}

static void t_sgi_mask_list(void)
{
	setup(4);
	bring_up();
	CHECK_EQ(vgic_sgi_send_mask(&F.v, 0, sgi1r(2, 0x6, false)), 0x6);   /* cpu1, cpu2 */
	CHECK_EQ(vgic_sgi_send_mask(&F.v, 0, sgi1r(2, 0x8, false)), 0x8);   /* cpu3 */
	CHECK_EQ(vgic_sgi_send_mask(&F.v, 0, sgi1r(2, 0x30, false)), 0);    /* none exist */
}

static void t_sgi_mask_irm(void)
{
	setup(4);
	bring_up();
	CHECK_EQ(vgic_sgi_send_mask(&F.v, 1, sgi1r(2, 0, true)), 0xD);      /* all but cpu1 */
	CHECK_EQ(vgic_sgi_send_mask(&F.v, 9, sgi1r(2, 0, true)), 0);        /* bad sender */
	CHECK_EQ(vgic_sgi_send(&F.v, 3, sgi1r(2, 0, true)), 3);             /* count form */
}

static void t_private_state_is_per_vcpu(void)
{
	unsigned i;

	setup(4);
	bring_up();
	enable_priv(2, 27);                          /* only vCPU 2 enables PPI 27 */
	for (i = 0; i < 4; i++)
		vgic_set_irq_level(&F.v, i, 27, true);
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
	CHECK_EQ(next(1), VGIC_INTID_SPURIOUS);
	CHECK_EQ(next(2), 27);
	CHECK_EQ(next(3), VGIC_INTID_SPURIOUS);
	CHECK_EQ(vgic_ack(&F.v, 2), 27);
	/* the active state is private to vCPU 2 */
	enable_priv(3, 27);
	CHECK_EQ(next(3), 27);
}

static void t_sgi_delivered_only_to_target_and_enabled(void)
{
	unsigned i;

	setup(4);
	bring_up();
	for (i = 1; i < 4; i++)
		enable_priv(i, 2);
	vgic_sgi_send_mask(&F.v, 0, sgi1r(2, 0x4, false));      /* cpu2 only */
	CHECK_EQ(next(1), VGIC_INTID_SPURIOUS);
	CHECK_EQ(next(2), 2);
	CHECK_EQ(next(3), VGIC_INTID_SPURIOUS);
}

static void t_spi_routed_to_selected_vcpu(void)
{
	setup(4);
	bring_up();
	enable_spi(34);
	wr64(D(0x6000 + 8 * 34), 2);                 /* Aff0 = 2 */
	vgic_set_irq_level(&F.v, 0, 34, true);
	CHECK_EQ(vgic_route_target(&F.v, 34), 2);
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
	CHECK_EQ(next(2), 34);
	CHECK_EQ(F.last_eff, VGIC_EFFECT_SET_TARGET);
	CHECK_EQ(F.last_eff_arg, 2);
	/* re-route to vCPU 3 */
	wr64(D(0x6000 + 8 * 34), 3);
	CHECK_EQ(F.last_eff_arg, 3);
	CHECK_EQ(next(2), VGIC_INTID_SPURIOUS);
	CHECK_EQ(next(3), 34);
	/* route to an affinity that does not exist: nobody gets it */
	wr64(D(0x6000 + 8 * 34), 7);
	CHECK_EQ(F.last_eff_arg, (uint64_t)(int64_t)-1);
	CHECK_EQ(next(3), VGIC_INTID_SPURIOUS);
}

static void t_spi_enable_effects_order(void)
{
	setup(4);
	bring_up();
	F.effects = 0;
	enable_spi(40);
	CHECK_EQ(F.effects, 2);
	CHECK_EQ(F.last_eff, VGIC_EFFECT_UNMASK_SPI);   /* route first, then unmask */
	CHECK_EQ(F.last_eff_intid, 40);
	wr32(D(0x180 + 4), 1u << (40 - 32));            /* ICENABLER1 */
	CHECK_EQ(F.last_eff, VGIC_EFFECT_MASK_SPI);
}

static void t_spi_level_cycle_one_vcpu(void)
{
	setup(4);
	bring_up();
	enable_spi(34);
	wr64(D(0x6000 + 8 * 34), 1);
	vgic_set_irq_level(&F.v, 0, 34, true);
	CHECK_EQ(vgic_ack(&F.v, 1), 34);
	/* what cpuif.c does at guest EOI: drop the line, then deactivate */
	vgic_set_irq_level(&F.v, 1, 34, false);
	CHECK(vgic_eoi(&F.v, 1, 34));
	CHECK_EQ(next(1), VGIC_INTID_SPURIOUS);         /* line low: not pending again */
	vgic_set_irq_level(&F.v, 1, 34, true);          /* device asserts again */
	CHECK_EQ(next(1), 34);
}

static void t_waker_per_vcpu_gates_delivery(void)
{
	setup(3);
	wr32(D(0), 0x3);
	wr32(R(1, 0x14), 0);                         /* only vCPU 1 awake */
	enable_priv(1, 5);
	enable_priv(2, 5);
	vgic_sgi_send_mask(&F.v, 0, sgi1r(5, 0x6, false));
	CHECK_EQ(next(1), 5);
	CHECK_EQ(next(2), VGIC_INTID_SPURIOUS);      /* asleep redistributor */
	wr32(R(2, 0x14), 0);
	CHECK_EQ(next(2), 5);                        /* pending state was kept */
}

static void t_vcpu_reset_is_private(void)
{
	setup(3);
	bring_up();
	enable_priv(1, 3);
	enable_priv(2, 3);
	vgic_sgi_send_mask(&F.v, 0, sgi1r(3, 0x6, false));
	vgic_vcpu_reset(&F.v, 1);                    /* CPU_OFF of vCPU 1 */
	CHECK_EQ(rd32(R(1, 0x14)), 0x6);             /* asleep again */
	CHECK_EQ(rd32(S(1, 0x100)), 0);              /* enables cleared */
	CHECK_EQ(next(1), VGIC_INTID_SPURIOUS);
	CHECK_EQ(next(2), 3);                        /* vCPU 2 untouched */
	vgic_vcpu_reset(&F.v, 9);                    /* out of range: ignored */
}

int main(void)
{
	RUN(t_sgi_mask_list);
	RUN(t_sgi_mask_irm);
	RUN(t_private_state_is_per_vcpu);
	RUN(t_sgi_delivered_only_to_target_and_enabled);
	RUN(t_spi_routed_to_selected_vcpu);
	RUN(t_spi_enable_effects_order);
	RUN(t_spi_level_cycle_one_vcpu);
	RUN(t_waker_per_vcpu_gates_delivery);
	RUN(t_vcpu_reset_is_private);
	TESTS_DONE();
}
