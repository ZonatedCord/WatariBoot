/* SPDX-License-Identifier: MIT */
/* Host tests: ICC_SGI1R_EL1 decoding (vgic_sgi_send). */
#include "test.h"

static uint32_t next(unsigned cpu)
{
	uint32_t id = 0;
	return vgic_next_pending(&F.v, cpu, &id, NULL) ? id : VGIC_INTID_SPURIOUS;
}

static uint64_t sgi1r(uint32_t intid, uint32_t list, uint64_t aff1, bool irm)
{
	return ((uint64_t)intid << 24) | list | (aff1 << 16) | ((uint64_t)irm << 40);
}

static void t_sgi_to_self(void)
{
	setup(1);
	bring_up();
	enable_priv(0, 1);
	CHECK_EQ(next(0), 1023);
	CHECK_EQ(vgic_sgi_send(&F.v, 0, sgi1r(1, 0x1, 0, false)), 1);
	CHECK_EQ(next(0), 1);
	CHECK_EQ(vgic_ack(&F.v, 0), 1);
	CHECK_EQ(next(0), 1023);
}

static void t_sgi_not_enabled_stays_pending(void)
{
	setup(1);
	bring_up();
	CHECK_EQ(vgic_sgi_send(&F.v, 0, sgi1r(2, 0x1, 0, false)), 1);
	CHECK_EQ(next(0), 1023);                     /* pending but disabled */
	enable_priv(0, 2);
	CHECK_EQ(next(0), 2);
}

static void t_sgi_target_list(void)
{
	unsigned i;

	setup(4);
	bring_up();
	for (i = 0; i < 4; i++)
		enable_priv(i, 5);
	CHECK_EQ(vgic_sgi_send(&F.v, 0, sgi1r(5, 0x6, 0, false)), 2); /* cpu1, cpu2 */
	CHECK_EQ(next(0), 1023);
	CHECK_EQ(next(1), 5);
	CHECK_EQ(next(2), 5);
	CHECK_EQ(next(3), 1023);
}

static void t_sgi_irm_excludes_sender(void)
{
	unsigned i;

	setup(3);
	bring_up();
	for (i = 0; i < 3; i++)
		enable_priv(i, 7);
	CHECK_EQ(vgic_sgi_send(&F.v, 1, sgi1r(7, 0xffff, 9, true)), 2);
	CHECK_EQ(next(0), 7);
	CHECK_EQ(next(1), 1023);
	CHECK_EQ(next(2), 7);
}

static void t_sgi_wrong_affinity_ignored(void)
{
	setup(2);
	bring_up();
	enable_priv(0, 3);
	enable_priv(1, 3);
	CHECK_EQ(vgic_sgi_send(&F.v, 0, sgi1r(3, 0x3, 1, false)), 0);  /* Aff1=1: none */
	CHECK_EQ(vgic_sgi_send(&F.v, 0, sgi1r(3, 0x8, 0, false)), 0);  /* Aff0=3: none */
	CHECK_EQ(next(0), 1023);
	CHECK_EQ(next(1), 1023);
	CHECK_EQ(vgic_sgi_send(&F.v, 5, sgi1r(3, 0x1, 0, false)), 0);  /* bad sender */
}

static void t_sgi_priority_and_pend_visible(void)
{
	uint8_t p = 0;
	uint32_t id = 0;

	setup(1);
	bring_up();
	enable_priv(0, 4);
	wr8(S(0, 0x400 + 4), 0x40);
	vgic_sgi_send(&F.v, 0, sgi1r(4, 1, 0, false));
	CHECK_EQ(rd32(S(0, 0x200)) & (1u << 4), 1u << 4);   /* ISPENDR0 shows it */
	CHECK(vgic_next_pending(&F.v, 0, &id, &p));
	CHECK_EQ(p, 0x40);
}

int main(void)
{
	RUN(t_sgi_to_self);
	RUN(t_sgi_not_enabled_stays_pending);
	RUN(t_sgi_target_list);
	RUN(t_sgi_irm_excludes_sender);
	RUN(t_sgi_wrong_affinity_ignored);
	RUN(t_sgi_priority_and_pend_visible);
	TESTS_DONE();
}
