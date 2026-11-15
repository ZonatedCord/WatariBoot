/* SPDX-License-Identifier: MIT */
/* T16d host tests: the bench's virtual M1 topology (wel2.vmpidr=m1): MPIDR table, PSCI target lookup, physical stack slot rule,
 * and a vGIC built on the virtual affinities (GICR_TYPER, ICC_SGI1R targeting, GICD_IROUTER). */
#include "test.h"
#include "wel2_vmpidr.h"

static void table(void)
{
	static const uint64_t want[8] = { 0x80000000, 0x80000001, 0x80000002, 0x80000003,
					  0x80010100, 0x80010101, 0x80010102, 0x80010103 };   /* [verified: re/m3a-ehw0-measurements.md] */
	for (unsigned i = 0; i < 8; i++) {
		CHECK_EQ(wel2_vm1_mpidr(i), want[i]);
		CHECK_EQ(wel2_vm1_find(want[i] & 0x000000ff00ffffffull), i);     /* PSCI target = affinity fields only */
		CHECK_EQ(wel2_aff_to_mpidr(wel2_vm1_aff(i)), want[i] & 0xffffff);
	}
	CHECK_EQ(wel2_vm1_find(0x4), ~0u);            /* QEMU's Aff0 4 is not an M1 core */
	CHECK_EQ(wel2_vm1_find(0x10104), ~0u);
	CHECK_EQ(wel2_vm1_find(0x100), ~0u);
	CHECK_EQ(wel2_vm1_find(0x80000000), ~0u);     /* bit 31 must have been stripped by the caller */
	CHECK_EQ(wel2_vm1_find(0x100000000ull), ~0u);
}

static void phys_rule(void)
{
	uint32_t ok[8], bad[8];

	for (unsigned i = 0; i < 8; i++)
		ok[i] = bad[i] = i;
	CHECK(wel2_vm1_phys_ok(ok, 8));
	CHECK(!wel2_vm1_phys_ok(ok, 4));              /* -smp 4 */
	bad[5] = 0x100;                               /* Aff1 = 1 */
	CHECK(!wel2_vm1_phys_ok(bad, 8));
	bad[5] = 6;                                   /* duplicate */
	CHECK(!wel2_vm1_phys_ok(bad, 8));
	for (unsigned i = 0; i < 8; i++)
		CHECK_EQ(wel2_flat8_index(0x80000000ull | i), i);   /* QEMU MPIDR with RES1 bit 31 */
	CHECK_EQ(wel2_flat8_index(8), ~0u);
	CHECK_EQ(wel2_flat8_index(0x100), ~0u);
	CHECK_EQ(wel2_flat8_index(0x10000), ~0u);
	CHECK_EQ(wel2_flat8_index(0x100000000ull), ~0u);
}

static void vm1_setup(void)
{
	uint32_t aff[8];
	struct vgic_config cfg = { GICD, GICR, 8, aff, { fx_log, fx_eff, NULL, NULL }, false, 0 };

	for (unsigned i = 0; i < 8; i++)
		aff[i] = wel2_vm1_aff(i);
	F.logs = F.effects = 0;
	if (vgic_init(&F.v, &cfg))
		printf("    vgic_init failed\n");
	bring_up();
}

static uint32_t next(unsigned cpu)
{
	uint32_t id = 0;
	return vgic_next_pending(&F.v, cpu, &id, NULL) ? id : VGIC_INTID_SPURIOUS;
}

static void typer(void)
{
	vm1_setup();
	for (unsigned i = 0; i < 8; i++) {
		uint64_t t = rd64(R(i, 0x8));
		CHECK_EQ(t >> 32, wel2_vm1_aff(i));                       /* Affinity_Value as the MADT/MPIDR say */
		CHECK_EQ((t >> 8) & 0xffff, i);                           /* Processor_Number */
		CHECK_EQ((t >> 4) & 1, i == 7);                           /* Last only on the final frame */
	}
}

/* ICC_SGI1R_EL1: INTID [27:24], Aff1 [23:16], Aff2 [39:32], TargetList [15:0]. */
static uint64_t sgi1r(uint32_t intid, uint32_t list, uint64_t aff1, uint64_t aff2)
{
	return ((uint64_t)intid << 24) | list | (aff1 << 16) | (aff2 << 32);
}

static void sgi_routing(void)
{
	vm1_setup();
	for (unsigned i = 0; i < 8; i++)
		enable_priv(i, 3);
	/* P cluster (Aff2 = 1, Aff1 = 1), Aff0 targets 0 and 2: vCPUs 4 and 6 */
	CHECK_EQ(vgic_sgi_send(&F.v, 0, sgi1r(3, 0x5, 1, 1)), 2);
	for (unsigned i = 0; i < 8; i++)
		CHECK_EQ(next(i), (i == 4 || i == 6) ? 3 : VGIC_INTID_SPURIOUS);
	for (unsigned i = 0; i < 8; i++)
		if (next(i) == 3)
			vgic_ack(&F.v, i);
	/* E cluster (Aff1 = 0): Aff0 1 and 3: vCPUs 1 and 3 */
	CHECK_EQ(vgic_sgi_send(&F.v, 4, sgi1r(3, 0xa, 0, 0)), 2);
	CHECK_EQ(next(1), 3);
	CHECK_EQ(next(3), 3);
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
	/* QEMU's own naming of the same cores matches nothing: Aff1 = 0 list 0x10 (Aff0 = 4) hits no vCPU */
	CHECK_EQ(vgic_sgi_send(&F.v, 0, sgi1r(3, 0x10, 0, 0)), 0);
	/* Aff1 = 1 with Aff2 = 0 is not an M1 core either */
	CHECK_EQ(vgic_sgi_send(&F.v, 0, sgi1r(3, 0x1, 1, 0)), 0);
}

static void irouter(void)
{
	vm1_setup();
	wr64(D(0x6000 + 8 * 40), 0x10102);            /* Aff2 = 1, Aff1 = 1, Aff0 = 2 */
	CHECK_EQ(vgic_route_target(&F.v, 40), 6);
	wr64(D(0x6000 + 8 * 41), 0x3);                /* E core 3 */
	CHECK_EQ(vgic_route_target(&F.v, 41), 3);
	wr64(D(0x6000 + 8 * 42), 0x4);                /* QEMU's Aff0 4 does not exist for the guest */
	CHECK_EQ(vgic_route_target(&F.v, 42), -1);
}

int main(void)
{
	RUN(table);
	RUN(phys_rule);
	RUN(typer);
	RUN(sgi_routing);
	RUN(irouter);
	TESTS_DONE();
}
