/* SPDX-License-Identifier: MIT */
/* Watari EL2 T16d (QEMU bench only, opt-in `wel2.vmpidr=m1`): the guest sees the 13" M1's eight MPIDRs on a QEMU `-smp 8`.
 *
 * Physical side (QEMU virt, gic-version=3, -smp 8): Aff0 = 0..7, Aff1 = Aff2 = Aff3 = 0 (one cluster)
 *   [hypothesis from QEMU's virt numbering; checked at start-up by wel2_vm1_phys_ok(), the layer stops otherwise].
 * Virtual side: the M1's, as the j293 MADT lists them and as the real machine reports them
 *   [verified: re/m3a-ehw0-measurements.md: E cores 0x80000000..3, P cores 0x80010100..3; tools/acpigen j293_model.c: GICC MPIDR
 *   0x0..0x3, 0x10100..0x10103, the affinity fields only]. VMPIDR_EL2 carries RES1 bit 31 like the real value, U = 0, MT = 0.
 * vCPU index i == physical core i (the redistributor order), as everywhere in the layer; only the guest-visible names differ.
 * The stack slot of a physical core is chosen by Aff0 alone on the bench (wel2_flat8_index; start.S core_sp, flag g_core_idx8).
 */
#ifndef WEL2_VMPIDR_H
#define WEL2_VMPIDR_H

#include <stdint.h>
#include <stdbool.h>

#define WEL2_VM1_NCORES 8u
#define WEL2_VMPIDR_RES1 (1ULL << 31)

/* Packed Aff3.Aff2.Aff1.Aff0 (as GICR_TYPER[63:32]) the guest sees for vCPU idx: E cores 0x0..0x3, P cores 0x10100..0x10103. */
static inline uint32_t wel2_vm1_aff(unsigned idx)
{
	return idx < 4 ? idx : 0x00010100u + (idx - 4);
}

/* VMPIDR_EL2 of vCPU idx. */
static inline uint64_t wel2_vm1_mpidr(unsigned idx)
{
	return WEL2_VMPIDR_RES1 | wel2_vm1_aff(idx);
}

/* vCPU of a guest PSCI target (affinity fields only: bits 31/30/24 already stripped), or ~0u. */
static inline unsigned wel2_vm1_find(uint64_t target)
{
	for (unsigned i = 0; i < WEL2_VM1_NCORES; i++)
		if (target == wel2_vm1_aff(i))
			return i;
	return ~0u;
}

/* Stack slot of a physical bench core: Aff0 when it is 0..7 and everything else is 0, else ~0u (start.S core_sp, same rule). */
static inline unsigned wel2_flat8_index(uint64_t mpidr)
{
	if ((mpidr & 0x000000ff00ffff00ULL) != 0 || (mpidr & 0xff) >= WEL2_VM1_NCORES)
		return ~0u;
	return (unsigned)(mpidr & 0xff);
}

/* Packed Aff3.Aff2.Aff1.Aff0 -> the 64-bit MPIDR affinity layout (Aff3 at [39:32]). */
static inline uint64_t wel2_aff_to_mpidr(uint32_t aff)
{
	return ((uint64_t)(aff >> 24) << 32) | (aff & 0xffffffu);
}

/* The physical cores (redistributor order) must be exactly Aff0 = 0..7 in one cluster for the map above to be a bijection. */
static inline bool wel2_vm1_phys_ok(const uint32_t *aff, unsigned n)
{
	if (n != WEL2_VM1_NCORES)
		return false;
	for (unsigned i = 0; i < n; i++)
		if (aff[i] != i)
			return false;
	return true;
}

#endif
