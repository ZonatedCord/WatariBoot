/* SPDX-License-Identifier: MIT */
/* Watari EL2: dense core index used to pick the per-core EL2 stack slot.
 *
 * index = Aff1 * 4 + Aff0, valid only if
 *   Aff0 < 4, Aff1 < 2, Aff2 == Aff1, Aff3 == 0.
 * Result 0..7 (WEL2_NSTACKS slots), or WEL2_CORE_INVALID.
 *
 * Why this shape:
 *  - QEMU virt: MPIDR Aff0 = 0..N-1, Aff1 = Aff2 = 0 (N <= 4 here), index = Aff0.
 *    [hypothesis: QEMU virt numbering for -smp <= 4; run-all.sh only exercises Aff0 <= 3]
 *  - Apple M1 (t8103): E-cores 0x80000000..3 (Aff2=0, Aff1=0, Aff0=0..3), P-cores
 *    0x80010100..3 (Aff2=1, Aff1=1, Aff0=0..3) -> indices 0..3 and 4..7
 *    [verified: re/m3a-ehw0-measurements.md]. Hence Aff2 must equal Aff1 (not be zero).
 *  - MPIDR bits outside Aff3:Aff0 (RES1 bit 31, U bit 30, MT bit 24) are ignored
 *    [verified: Arm ARM, MPIDR_EL1 field layout].
 * The asm in boot/start.S (secondary_entry) implements exactly this formula; keep both in step.
 * This index only selects the stack; the PSCI/vGIC cpu number is still the redistributor
 * discovery order (cpu_from_mpidr in gic_phys.c).
 */
#ifndef WEL2_COREIDX_H
#define WEL2_COREIDX_H

/* Stack geometry: the one place; start.S (preprocessed) and linker.ld (ASSERT) follow it. */
#define WEL2_NSTACKS      8
#define WEL2_STACK_SHIFT  14
#define WEL2_STACK_SIZE   (1 << WEL2_STACK_SHIFT)

#ifndef __ASSEMBLER__
#include <stdint.h>

#define WEL2_CORE_INVALID (~0u)

static inline unsigned wel2_core_index(uint64_t mpidr)
{
	unsigned aff0 = (unsigned)(mpidr & 0xff);
	unsigned aff1 = (unsigned)((mpidr >> 8) & 0xff);
	unsigned aff2 = (unsigned)((mpidr >> 16) & 0xff);
	unsigned aff3 = (unsigned)((mpidr >> 32) & 0xff);

	if (aff0 >= 4 || aff1 >= 2 || aff2 != aff1 || aff3 != 0)
		return WEL2_CORE_INVALID;
	return aff1 * 4 + aff0;
}
#endif /* __ASSEMBLER__ */

#endif
