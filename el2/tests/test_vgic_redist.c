/* SPDX-License-Identifier: MIT */
/* Host tests: GICR (RD_base and SGI_base frames). */
#include "test.h"

static void t_typer_last_bit(void)
{
	setup(4);
	CHECK_EQ(rd32(R(0, 8)) & 0x10, 0);
	CHECK_EQ(rd32(R(2, 8)) & 0x10, 0);
	CHECK_EQ(rd32(R(3, 8)) & 0x10, 0x10);        /* only the last vCPU */
}

static void t_typer_last_single_vcpu(void)
{
	setup(1);
	CHECK_EQ(rd64(R(0, 8)) & 0x10, 0x10);
}

static void t_typer_processor_number_and_affinity(void)
{
	static const uint32_t aff[3] = { 0x00000000, 0x00000100, 0x01020304 };
	struct vgic_config c = { GICD, GICR, 3, aff, { fx_log, fx_eff, NULL } };

	CHECK_EQ(vgic_init(&F.v, &c), 0);
	CHECK_EQ((rd64(R(1, 8)) >> 8) & 0xFFFF, 1);
	CHECK_EQ(rd64(R(1, 8)) >> 32, 0x100);
	CHECK_EQ((rd64(R(2, 8)) >> 8) & 0xFFFF, 2);
	CHECK_EQ(rd64(R(2, 8)) >> 32, 0x01020304);
	CHECK_EQ(rd64(R(2, 8)) & 0x3, 0);            /* PLPIS = VLPIS = 0 */
}

static void t_typer_32bit_halves(void)
{
	setup(2);
	CHECK_EQ(rd32(R(1, 0xC)), 1);                /* Aff0 = 1 */
	CHECK_EQ(rd32(R(1, 8)), (1u << 8) | 0x10);
	wr64(R(1, 8), 0);                            /* read-only */
	CHECK_EQ(rd32(R(1, 8)), (1u << 8) | 0x10);
}

static void t_ctlr_statusr_iidr(void)
{
	setup(1);
	wr32(R(0, 0), 0xFFFFFFFF);                   /* EnableLPIs RAZ/WI */
	CHECK_EQ(rd32(R(0, 0)), 0);
	CHECK_EQ(rd32(R(0, 0x10)), 0);
	CHECK_EQ(rd32(R(0, 4)), rd32(D(8)));         /* GICR_IIDR == GICD_IIDR */
	CHECK_EQ(F.logs, 0);
}

static void t_redist_id_regs(void)
{
	setup(1);
	CHECK_EQ((rd32(R(0, 0xFFE8)) >> 4) & 0xF, 3);
	CHECK_EQ(rd32(R(0, 0xFFE8)), rd32(D(0xFFE8)));
}

static void t_waker_reset(void)
{
	setup(2);
	CHECK_EQ(rd32(R(0, 0x14)), 0x6);             /* ProcessorSleep + ChildrenAsleep */
	CHECK_EQ(rd32(R(1, 0x14)), 0x6);
}

static void t_waker_handshake(void)
{
	setup(2);
	wr32(R(0, 0x14), 0);                         /* clear ProcessorSleep */
	CHECK_EQ(rd32(R(0, 0x14)) & 0x4, 0);         /* ChildrenAsleep drops immediately */
	CHECK_EQ(rd32(R(0, 0x14)), 0);
	CHECK_EQ(rd32(R(1, 0x14)), 0x6);             /* other vCPU unaffected */
	wr32(R(0, 0x14), 0x2);
	CHECK_EQ(rd32(R(0, 0x14)), 0x6);
	wr32(R(0, 0x14), 0xFFFFFFFF);                /* other bits are WI */
	CHECK_EQ(rd32(R(0, 0x14)), 0x6);
}

static void t_sgi_enable_set_clear(void)
{
	setup(1);
	wr32(S(0, 0x180), 0xFFFF00FF);               /* what Windows writes first */
	CHECK_EQ(rd32(S(0, 0x100)), 0);
	wr32(S(0, 0x100), 0x0000FFFF);
	CHECK_EQ(rd32(S(0, 0x100)), 0xFFFF);
	wr32(S(0, 0x180), 0xFFFF00FF);
	CHECK_EQ(rd32(S(0, 0x100)), 0x0000FF00);
	CHECK_EQ(rd32(S(0, 0x180)), 0x0000FF00);
}

static void t_sgi_enable_has_no_effect_callbacks(void)
{
	setup(1);
	wr32(S(0, 0x100), 0xFFFFFFFF);               /* SGIs/PPIs are not AIC SPIs */
	CHECK_EQ(F.effects, 0);
}

static void t_sgi_igroupr0_persists(void)
{
	setup(2);
	wr32(S(0, 0x80), 0xFFFFFFFF);
	CHECK_EQ(rd32(S(0, 0x80)), 0xFFFFFFFF);
	CHECK_EQ(rd32(S(1, 0x80)), 0);               /* banked per vCPU */
	wr32(R(0, 0x14), 0);                         /* waking up does not reset it */
	CHECK_EQ(rd32(S(0, 0x80)), 0xFFFFFFFF);
}

static void t_sgi_pending_active_state(void)
{
	setup(1);
	wr32(S(0, 0x200), 0x5);
	CHECK_EQ(rd32(S(0, 0x200)), 0x5);
	wr32(S(0, 0x280), 0x1);
	CHECK_EQ(rd32(S(0, 0x280)), 0x4);
	wr32(S(0, 0x300), 0x8);
	CHECK_EQ(rd32(S(0, 0x300)), 0x8);
	wr32(S(0, 0x380), 0x8);
	CHECK_EQ(rd32(S(0, 0x300)), 0);
}

static void t_sgi_icfgr(void)
{
	setup(1);
	CHECK_EQ(rd32(S(0, 0xC00)), 0xAAAAAAAA);     /* SGIs always edge (RAO) */
	wr32(S(0, 0xC00), 0);
	CHECK_EQ(rd32(S(0, 0xC00)), 0xAAAAAAAA);
	CHECK_EQ(rd32(S(0, 0xC04)), 0);              /* PPIs default to level */
	wr32(S(0, 0xC04), 0xFFFFFFFF);
	CHECK_EQ(rd32(S(0, 0xC04)), 0xAAAAAAAA);
	wr32(S(0, 0xC04), 0);
	CHECK_EQ(rd32(S(0, 0xC04)), 0);
}

static void t_sgi_priority(void)
{
	setup(1);
	wr32(S(0, 0x41C), 0xFFFFFFFF);               /* INTID 28-31 */
	CHECK_EQ(rd32(S(0, 0x41C)), 0xF8F8F8F8);
	wr8(S(0, 0x400), 0x20);
	CHECK_EQ(rd8(S(0, 0x400)), 0x20);
	CHECK_EQ(rd32(D(0x400)), 0);                 /* GICD copy of INTID 0-3 is RAZ */
}

static void t_banking_between_vcpus(void)
{
	setup(2);
	wr32(S(1, 0x100), 0x1u << 27);
	CHECK_EQ(rd32(S(0, 0x100)), 0);
	CHECK_EQ(rd32(S(1, 0x100)), 1u << 27);
	wr8(S(1, 0x41B), 0x50);
	CHECK_EQ(rd8(S(0, 0x41B)), 0);
	CHECK_EQ(rd8(S(1, 0x41B)), 0x50);
}

static void t_razwi_sgi_frame_regs(void)
{
	setup(1);
	wr32(S(0, 0xD00), 0xFFFFFFFF);               /* IGRPMODR0 */
	wr32(S(0, 0xE00), 0xFFFFFFFF);               /* NSACR */
	CHECK_EQ(rd32(S(0, 0xD00)) | rd32(S(0, 0xE00)), 0);
	CHECK_EQ(F.logs, 0);
}

static void t_unimplemented_rd_offset_logged(void)
{
	setup(1);
	CHECK_EQ(rd32(R(0, 0x70)), 0);               /* e.g. GICR_PROPBASER: absent (no LPIs) */
	CHECK_EQ(F.logs, 1);
	CHECK_EQ(F.last_kind, VGIC_LOG_UNIMPL);
	wr32(S(0, 0x500), 0x1234);
	CHECK_EQ(F.logs, 2);
	CHECK_EQ(F.last_addr, S(0, 0x500));
}

static void t_nonexistent_vcpu_frame_logged(void)
{
	setup(2);
	CHECK_EQ(rd32(R(5, 8)), 0);
	CHECK_EQ(F.logs, 1);
	wr32(S(7, 0x100), 1);
	CHECK_EQ(F.logs, 2);
}

static void t_redist_bad_access(void)
{
	uint64_t v;
	setup(1);
	CHECK_EQ(vgic_mmio_read(&F.v, R(0, 0x14), 8, &v), VGIC_ERR_ACCESS);
	CHECK_EQ(vgic_mmio_read(&F.v, R(0, 0x14), 1, &v), VGIC_ERR_ACCESS);
	CHECK_EQ(vgic_mmio_read(&F.v, R(0, 0xA), 8, &v), VGIC_ERR_ACCESS);
}

static void t_vcpu_wake_for_firmware(void)
{
	uint32_t id = 0;

	setup(2);
	wr32(D(0), 2);                               /* EnableGrp1 */
	wr32(S(0, 0x80), 1u << 27);                  /* IGROUPR0: PPI 27 group 1 */
	wr32(S(0, 0x100), 1u << 27);                 /* ISENABLER0 */
	vgic_set_irq_level(&F.v, 0, 27, true);
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));       /* asleep: nothing forwarded */
	vgic_vcpu_wake(&F.v, 0);
	CHECK_EQ(rd32(R(0, 0x14)) & 6, 0);                    /* WAKER: awake, children awake */
	CHECK(vgic_next_pending(&F.v, 0, &id, NULL));
	CHECK_EQ(id, 27);
	CHECK_EQ(rd32(R(1, 0x14)) & 2, 2);                    /* vCPU 1 untouched */
	vgic_vcpu_wake(&F.v, 9);                              /* out of range: ignored */
}

int main(void)
{
	RUN(t_vcpu_wake_for_firmware);
	RUN(t_typer_last_bit);
	RUN(t_typer_last_single_vcpu);
	RUN(t_typer_processor_number_and_affinity);
	RUN(t_typer_32bit_halves);
	RUN(t_ctlr_statusr_iidr);
	RUN(t_redist_id_regs);
	RUN(t_waker_reset);
	RUN(t_waker_handshake);
	RUN(t_sgi_enable_set_clear);
	RUN(t_sgi_enable_has_no_effect_callbacks);
	RUN(t_sgi_igroupr0_persists);
	RUN(t_sgi_pending_active_state);
	RUN(t_sgi_icfgr);
	RUN(t_sgi_priority);
	RUN(t_banking_between_vcpus);
	RUN(t_razwi_sgi_frame_regs);
	RUN(t_unimplemented_rd_offset_logged);
	RUN(t_nonexistent_vcpu_frame_logged);
	RUN(t_redist_bad_access);
	TESTS_DONE();
}
