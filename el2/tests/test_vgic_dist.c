/* SPDX-License-Identifier: MIT */
/* Host tests: GICD register model. */
#include "test.h"

static void t_reset_ctlr(void)
{
	setup(2);
	/* ARE and DS read as one (RAO), enables clear, RWP (bit31) zero. */
	CHECK_EQ(rd32(D(0)), 0x50);
}

static void t_ctlr_write_masks(void)
{
	setup(2);
	wr32(D(0), 0xFFFFFFFF);
	CHECK_EQ(rd32(D(0)), 0x53);          /* only EnableGrp0/1 stored */
	CHECK_EQ(rd32(D(0)) & 0x80000000u, 0); /* RWP never 1 */
	wr32(D(0), 0x12);                      /* ARE+EnableGrp1, as Windows does */
	CHECK_EQ(rd32(D(0)), 0x52);
}

static void t_typer(void)
{
	setup(2);
	CHECK_EQ(rd32(D(4)), 0x0148001D);
	wr32(D(4), 0);                         /* read-only */
	CHECK_EQ(rd32(D(4)), 0x0148001D);
}

static void t_iidr_typer2_statusr(void)
{
	setup(1);
	CHECK(rd32(D(8)) != 0);
	CHECK_EQ(rd32(D(0xC)), 0);
	wr32(D(0x10), 0xFFFFFFFF);
	CHECK_EQ(rd32(D(0x10)), 0);
	CHECK_EQ(F.logs, 0);
}

static void t_id_regs(void)
{
	setup(1);
	CHECK_EQ((rd32(D(0xFFE8)) >> 4) & 0xF, 3);   /* PIDR2.ArchRev */
	CHECK_EQ(rd32(D(0xFFF0)), 0x0D);
	CHECK_EQ(rd32(D(0xFFFC)), 0xB1);
	wr32(D(0xFFE8), 0);
	CHECK_EQ((rd32(D(0xFFE8)) >> 4) & 0xF, 3);
}

static void t_enable_set_clear(void)
{
	setup(1);
	wr32(D(0x104), 0x00000A00);                  /* INTID 41, 43 */
	CHECK_EQ(rd32(D(0x104)), 0xA00);
	CHECK_EQ(rd32(D(0x184)), 0xA00);             /* ICENABLER reads state */
	wr32(D(0x104), 0x00000100);                  /* set is OR, not overwrite */
	CHECK_EQ(rd32(D(0x104)), 0xB00);
	wr32(D(0x184), 0x00000200);
	CHECK_EQ(rd32(D(0x104)), 0x900);
}

static void t_gicd_word0_razwi(void)
{
	setup(1);
	wr32(D(0x100), 0xFFFFFFFF);                  /* INTID 0-31 live in the GICR */
	CHECK_EQ(rd32(D(0x100)), 0);
	CHECK_EQ(rd32(S(0, 0x100)), 0);
	CHECK_EQ(F.logs, 0);
}

static void t_pending_set_clear(void)
{
	setup(1);
	wr32(D(0x204), 0x3);
	CHECK_EQ(rd32(D(0x204)), 0x3);
	CHECK_EQ(rd32(D(0x284)), 0x3);
	wr32(D(0x284), 0x1);
	CHECK_EQ(rd32(D(0x204)), 0x2);
}

static void t_active_set_clear(void)
{
	setup(1);
	wr32(D(0x308), 0xF0);
	CHECK_EQ(rd32(D(0x308)), 0xF0);
	wr32(D(0x388), 0x30);
	CHECK_EQ(rd32(D(0x308)), 0xC0);
	CHECK_EQ(rd32(D(0x388)), 0xC0);
}

static void t_priority_word_masks_low_bits(void)
{
	setup(1);
	wr32(D(0x420), 0xFFFFFFFF);                  /* INTID 32-35 */
	CHECK_EQ(rd32(D(0x420)), 0xF8F8F8F8);        /* 5 implemented bits */
	wr32(D(0x420), 0x01234567);
	CHECK_EQ(rd32(D(0x420)), 0x00204060);
}

static void t_priority_byte_vs_word(void)
{
	setup(1);
	wr32(D(0x424), 0x80808080);
	wr8(D(0x425), 0x40);                         /* only INTID 37 changes */
	CHECK_EQ(rd32(D(0x424)), 0x80804080);
	CHECK_EQ(rd8(D(0x425)), 0x40);
	CHECK_EQ(rd8(D(0x427)), 0x80);
	wr8(D(0x426), 0xFF);
	CHECK_EQ(rd8(D(0x426)), 0xF8);
}

static void t_priority_bad_size(void)
{
	uint64_t v;
	setup(1);
	CHECK_EQ(vgic_mmio_write(&F.v, D(0x420), 2, 1), VGIC_ERR_ACCESS);
	CHECK_EQ(vgic_mmio_read(&F.v, D(0x420), 8, &v), VGIC_ERR_ACCESS);
	CHECK_EQ(F.last_kind, VGIC_LOG_BAD_ACCESS);
}

static void t_group_state(void)
{
	setup(1);
	CHECK_EQ(rd32(D(0x84)), 0);
	wr32(D(0x84), 0xFFFFFFFF);
	CHECK_EQ(rd32(D(0x84)), 0xFFFFFFFF);
	wr32(D(0x84), 0x0000FF00);                   /* group is a plain RW bit */
	CHECK_EQ(rd32(D(0x84)), 0x0000FF00);
}

static void t_icfgr_spi(void)
{
	setup(1);
	CHECK_EQ(rd32(D(0xC08)), 0);                 /* SPIs default to level */
	wr32(D(0xC08), 0xFFFFFFFF);                  /* INTID 32-47 */
	CHECK_EQ(rd32(D(0xC08)), 0xAAAAAAAA);        /* bit0 of each field RAZ */
	wr32(D(0xC08), 0x00000008);                  /* only INTID 33 edge (bit 3) */
	CHECK_EQ(rd32(D(0xC08)), 0x00000008);
}

static void t_icfgr_gicd_words01_razwi(void)
{
	setup(1);
	wr32(D(0xC00), 0xFFFFFFFF);
	wr32(D(0xC04), 0xFFFFFFFF);
	CHECK_EQ(rd32(D(0xC00)), 0);
	CHECK_EQ(rd32(D(0xC04)), 0);
}

static void t_irouter_roundtrip_and_mask(void)
{
	setup(4);
	wr64(D(0x6000 + 8 * 40), 0xFFFFFFFFFFFFFFFFull);
	CHECK_EQ(rd64(D(0x6000 + 8 * 40)), 0x000000FF80FFFFFFull);
	wr64(D(0x6000 + 8 * 40), 0x2);               /* Aff0 = 2 */
	CHECK_EQ(rd64(D(0x6000 + 8 * 40)), 2);
	CHECK_EQ(vgic_route_target(&F.v, 40), 2);
}

static void t_irouter_halves(void)
{
	setup(4);
	wr32(D(0x6000 + 8 * 50), 0x3);
	wr32(D(0x6000 + 8 * 50 + 4), 0x0);
	CHECK_EQ(rd32(D(0x6000 + 8 * 50)), 3);
	CHECK_EQ(rd32(D(0x6000 + 8 * 50 + 4)), 0);
	wr32(D(0x6000 + 8 * 50 + 4), 0x1);           /* Aff3 = 1: no such vCPU */
	CHECK_EQ(rd64(D(0x6000 + 8 * 50)), 0x100000003ull);
	CHECK_EQ(vgic_route_target(&F.v, 50), -1);
}

static void t_irouter_reserved_intids(void)
{
	setup(1);
	wr64(D(0x6000 + 8 * 5), 0x1);                /* INTID 5 is not an SPI */
	CHECK_EQ(rd64(D(0x6000 + 8 * 5)), 0);
	CHECK_EQ(F.logs, 0);
}

static void t_effects_on_enable_disable(void)
{
	setup(2);
	wr64(D(0x6000 + 8 * 57), 1);                 /* route to vCPU 1 */
	F.effects = 0;
	wr32(D(0x104), 1u << 25);                    /* INTID 57 */
	CHECK_EQ(F.effects, 2);                      /* set_target then unmask */
	CHECK_EQ(F.last_eff, VGIC_EFFECT_UNMASK_SPI);
	CHECK_EQ(F.last_eff_intid, 57);
	wr32(D(0x104), 1u << 25);                    /* already enabled: no effect */
	CHECK_EQ(F.effects, 2);
	wr32(D(0x184), 1u << 25);
	CHECK_EQ(F.effects, 3);
	CHECK_EQ(F.last_eff, VGIC_EFFECT_MASK_SPI);
}

static void t_effect_set_target_on_irouter_write(void)
{
	setup(2);
	F.effects = 0;
	wr64(D(0x6000 + 8 * 33), 1);
	CHECK_EQ(F.effects, 1);
	CHECK_EQ(F.last_eff, VGIC_EFFECT_SET_TARGET);
	CHECK_EQ(F.last_eff_arg, 1);
}

static void t_unimplemented_offset_logged(void)
{
	setup(1);
	CHECK_EQ(rd32(D(0x50)), 0);                  /* hole between CLRSPI and IGROUPR */
	CHECK_EQ(F.logs, 1);
	CHECK_EQ(F.last_kind, VGIC_LOG_UNIMPL);
	CHECK_EQ(F.last_addr, D(0x50));
	wr32(D(0x1000), 0xDEADBEEF);
	CHECK_EQ(F.logs, 2);
	CHECK_EQ(rd32(D(0x1000)), 0);
}

static void t_known_razwi_not_logged(void)
{
	setup(1);
	wr32(D(0x800), 1);                           /* ITARGETSR */
	wr32(D(0xD00), 1);                           /* IGRPMODR */
	wr32(D(0xE00), 1);                           /* NSACR */
	wr32(D(0xF10), 1);                           /* CPENDSGIR */
	CHECK_EQ(rd32(D(0x800)) | rd32(D(0xD00)) | rd32(D(0xE00)) | rd32(D(0xF10)), 0);
	CHECK_EQ(F.logs, 0);
}

static void t_setspi_sgir_ignored_logged(void)
{
	setup(1);
	wr32(D(0x40), 0x20);
	CHECK_EQ(F.logs, 1);
	CHECK_EQ(F.last_kind, VGIC_LOG_IGNORED);
	wr32(D(0xF00), 0x1);
	CHECK_EQ(F.logs, 2);
}

static void t_bad_size_and_alignment(void)
{
	uint64_t v;
	setup(1);
	CHECK_EQ(vgic_mmio_read(&F.v, D(0x100), 8, &v), VGIC_ERR_ACCESS);
	CHECK_EQ(vgic_mmio_read(&F.v, D(0x102), 4, &v), VGIC_ERR_ACCESS);
	CHECK_EQ(vgic_mmio_read(&F.v, D(0x100), 1, &v), VGIC_ERR_ACCESS);
	CHECK_EQ(vgic_mmio_read(&F.v, D(0x6004), 8, &v), VGIC_ERR_ACCESS);
	CHECK_EQ(F.logs, 4);
}

static void t_out_of_range_address(void)
{
	uint64_t v;
	setup(1);
	CHECK_EQ(vgic_mmio_read(&F.v, D(0x10000), 4, &v), VGIC_ERR_RANGE);
	CHECK_EQ(vgic_mmio_write(&F.v, 0x1000, 4, 0), VGIC_ERR_RANGE);
	CHECK_EQ(F.logs, 2);
}

static void t_beyond_nr_irqs_razwi(void)
{
	setup(1);
	wr32(D(0x100 + 4 * 31), 0xFFFFFFFF);         /* word 31 = INTID 992+ */
	CHECK_EQ(rd32(D(0x100 + 4 * 31)), 0);
	wr32(D(0x100 + 4 * 29), 0xFFFFFFFF);         /* word 29 = INTID 928-959 */
	CHECK_EQ(rd32(D(0x100 + 4 * 29)), 0xFFFFFFFF);
	CHECK_EQ(F.logs, 0);
}

static void t_init_validation(void)
{
	struct vgic_config c = { GICD, GICR, 0, NULL, { NULL, NULL, NULL } };
	CHECK_EQ(vgic_init(&F.v, &c), -1);
	c.nr_vcpus = VGIC_MAX_VCPUS + 1;
	CHECK_EQ(vgic_init(&F.v, &c), -1);
	c.nr_vcpus = 1;
	CHECK_EQ(vgic_init(&F.v, &c), 0);
	/* hooks are optional */
	wr32(D(0x50), 1);
	CHECK_EQ(rd32(D(0)), 0x50);
}

int main(void)
{
	RUN(t_reset_ctlr);
	RUN(t_ctlr_write_masks);
	RUN(t_typer);
	RUN(t_iidr_typer2_statusr);
	RUN(t_id_regs);
	RUN(t_enable_set_clear);
	RUN(t_gicd_word0_razwi);
	RUN(t_pending_set_clear);
	RUN(t_active_set_clear);
	RUN(t_priority_word_masks_low_bits);
	RUN(t_priority_byte_vs_word);
	RUN(t_priority_bad_size);
	RUN(t_group_state);
	RUN(t_icfgr_spi);
	RUN(t_icfgr_gicd_words01_razwi);
	RUN(t_irouter_roundtrip_and_mask);
	RUN(t_irouter_halves);
	RUN(t_irouter_reserved_intids);
	RUN(t_effects_on_enable_disable);
	RUN(t_effect_set_target_on_irouter_write);
	RUN(t_unimplemented_offset_logged);
	RUN(t_known_razwi_not_logged);
	RUN(t_setspi_sgir_ignored_logged);
	RUN(t_bad_size_and_alignment);
	RUN(t_out_of_range_address);
	RUN(t_beyond_nr_irqs_razwi);
	RUN(t_init_validation);
	TESTS_DONE();
}
