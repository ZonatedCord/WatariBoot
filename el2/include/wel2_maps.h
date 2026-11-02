/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - the two address maps, built with the granule-parametric builder (wel2_pt.h). Host-testable:
 * the boot code (stage1.c, main.c) only supplies the platform numbers (RAM range, image layout, holes).
 *
 *  EL2 stage 1 (identity): [0, RAM base) Device-nGnRnE XN, the RAM Normal WB inner-shareable RW XN, with the
 *     layer's code RO X and its read-only data RO XN (page granularity). Everything else is unmapped.
 *     Input size 39 bits with the 4 KB granule, 36 bits with 16 KB (root at level 2, one table).
 *  Guest stage 2 (identity, 40-bit IPA): everything Device-nGnRnE XN, the RAM Normal WB inner-shareable RW,
 *     then the holes unmapped (GICD/GICR, which the vGIC emulates by trapping, and the layer's own carve-out).
 */
#ifndef WEL2_MAPS_H
#define WEL2_MAPS_H

#include "wel2_pt.h"
#include "wel2_ram.h"

#define WEL2_S2_IPA_BITS 40u

/* Table pools (bytes), enough for the QEMU virt maps with either granule (test_pt.c prints the real use):
 * stage 2 with 16 KB needs a level-2 table per 64 GB of the 1 TB device window, stage 1 three tables. */
#define WEL2_S1_POOL_BYTES  (8u * 16384u)
#define WEL2_S2_POOL_BYTES  (24u * 16384u)

struct wel2_range {
	uint64_t base, size;
};

/* Input address size of the EL2 stage-1 map for a granule (so that the root is a single table at most). */
unsigned wel2_s1_va_bits(unsigned gshift);

struct wel2_s1_layout {
	uint64_t text_lo, text_hi, ro_hi;   /* [text_lo, text_hi) code, [text_hi, ro_hi) read-only data; granule aligned */
};

/* e2h selects the descriptor bits of the EL2&0 regime (AP[1] = 0, UXN + PXN) instead of the EL2 regime
 * (AP[1] = RES1, XN). 0, or -1 on bad arguments / pool exhausted. */
int wel2_s1_build(struct wel2_pt *pt, const struct wel2_ram *ram, const struct wel2_s1_layout *l, bool e2h);

/* 0, or -1 on bad arguments / pool exhausted. */
int wel2_s2_build(struct wel2_pt *pt, const struct wel2_ram *ram, const struct wel2_range *holes, unsigned nholes);

#endif
