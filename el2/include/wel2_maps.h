/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - the two address maps, built with the granule-parametric builder (wel2_pt.h). Host-testable:
 * the boot code (stage1.c, main.c) only supplies the platform numbers (RAM range, image layout, holes).
 *
 *  EL2 stage 1 (identity): [0, RAM base) Device-nGnRnE XN, the RAM Normal WB inner-shareable RW XN, with the
 *     layer's code RO X and its read-only data RO XN (page granularity). Everything else is unmapped.
 *     Input size 39 bits with the 4 KB granule, 36 bits with 16 KB (root at level 2, one table).
 *  Guest stage 2 (identity, IPA size per platform: 40 bits on QEMU, 36 on the M1): everything Device-nGnRnE XN, the RAM Normal WB inner-shareable RW,
 *     then the holes unmapped (GICD/GICR, which the vGIC emulates by trapping, and the layer's own carve-out).
 */
#ifndef WEL2_MAPS_H
#define WEL2_MAPS_H

#include "wel2_pt.h"
#include "wel2_ram.h"

#define WEL2_S2_IPA_BITS 40u

/* Table pools (bytes), enough for the QEMU virt maps with either granule (test_pt.c prints the real use):
 * stage 2 with 16 KB needs a level-2 table per 64 GB of the 1 TB device window, stage 1 three tables.
 * T6c: the M1 stage 1 is the larger user. The measured j293 layout (8 GB, ten scattered reserved ranges, framebuffer,
 * cpu-release page) takes 18 tables with 16 KB (two level-3 tables per odd range edge, 17 with 4 KB); 40 tables
 * hold about 20 reserved ranges. The guest stage 2 of the M1 (32 MB blocks, RAM only) takes 2 tables with 16 KB and
 * 9 with 4 KB. The QEMU maps need at most 21 stage-2 tables (six holes, 16 KB), hence 24; the pools make the layer 0x160000 bytes.
 * A layout that does not fit stops the boot with a message (stage1.c), it never maps less silently. */
#define WEL2_S1_POOL_BYTES  (40u * 16384u)
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

/* Maps the device window [base, base+size) (rounded outwards to the granule) Device-nGnRnE XN in the EL2 stage 1
 * when it lies above the RAM (on the M1 the console UART is below the RAM, already covered by [0, RAM base): then
 * nothing is added). The part of the window below RAM base is dropped; a window overlapping the RAM range is
 * refused. Call after wel2_s1_build. 0, or -1 (overlaps RAM, pool exhausted, beyond the input range). */
int wel2_s1_map_device(struct wel2_pt *pt, const struct wel2_ram *ram, uint64_t base, uint64_t size, bool e2h);

/* MAIR_EL2: Attr0 = 0x00 Device-nGnRnE, Attr1 = 0xff Normal WB RW allocate, Attr2 = 0x44 Normal Inner and Outer
 * Non-cacheable [verified: Arm ARM D24.2.127 MAIR_EL2 Attr encoding, 0b0100_0100 = Normal, Outer NC, Inner NC]. */
#define WEL2_S1_ATTR_NC      2
#define WEL2_MAIR_EL2_VALUE  0x44ff00ULL

/* Maps the framebuffer [base, base+size) (rounded outwards to the granule) Normal non-cacheable XN in the EL2
 * stage 1 (Device memory would fault on merged or unaligned stores, cacheable memory would need flushing). It must
 * lie wholly above the RAM range (on the j293: base 0x9e2570000, RAM top 0x9df610000); a framebuffer that overlaps
 * or sits below the RAM, or beyond the input range, is refused. Call after
 * wel2_s1_build. 0, or -1 (refused, pool exhausted). */
int wel2_s1_map_fb(struct wel2_pt *pt, const struct wel2_ram *ram, uint64_t base, uint64_t size, bool e2h);

/* Add [base, base+size) to holes[0..*n) (capacity cap) for the granule 1 << gshift. The range is rounded
 * OUTWARDS to the granule (a stage-2 hole cannot be smaller than a page; the guest losing the rest of that page is
 * the safe direction for a device window). A range inside an existing hole is dropped (the t8103 AIC lists reg[1]
 * inside reg[0]: wel2_aic.h); existing holes inside the new, wider range are removed; partial overlaps are kept
 * as separate holes (unmapping twice is harmless). Returns 0 (also for a size-0 range: nothing to add), or -1 if
 * the range wraps past 2^64 or the array is full; *n is unchanged on error. */
int wel2_s2_add_hole(struct wel2_range *holes, unsigned *n, unsigned cap, uint64_t base, uint64_t size,
		     unsigned gshift);

/* ---- Apple M1 maps (T6c, PLAN S8) -------------------------------------------------------------------------------
 * The M1 has no blanket Device map and no 4 GB default: both maps are built from explicit range lists (the FDT's
 * usable[] after the reservations, T6b) and everything else stays unmapped, so a stray access faults to EL2 and is
 * reported. Pure arithmetic over struct wel2_range arrays: host-tested with the j293 fixture (tests/test_pt.c). */

#define WEL2_MAX_GUEST_RANGES 66              /* M1_MAX_USABLE (fdt_m1.h) + 2 */
#define WEL2_M1_S1_ALIGN  WEL2_RAM_BLOCK       /* EL2 stage 1: RAM rounded inward to 2 MB (T6b note) */
#define WEL2_M1_S2_ALIGN  0x2000000ULL         /* guest stage 2: 32 MB = one level-2 block of the 16 KB granule (D 4.3) */

/* out[] = the pieces of in[0..n) rounded INWARD to `align` (a power of two); pieces that vanish are dropped, overlaps
 * are kept as they are (mapping twice is harmless). If cut_size is non-zero, [cut_base, cut_base+cut_size) rounded
 * OUTWARD to `align` is then removed from every piece. 0 and *nout, or -1 if out[] (capacity max) is too small or a
 * range wraps. */
int wel2_ranges_inward(const struct wel2_range *in, unsigned n, uint64_t align, uint64_t cut_base, uint64_t cut_size,
		       struct wel2_range *out, unsigned max, unsigned *nout);

struct wel2_s1_m1 {
	const struct wel2_range *ram;     /* usable RAM, exact (rounded inward to WEL2_M1_S1_ALIGN here) */
	unsigned nram;
	struct wel2_range carve;          /* the layer's block: always mapped (image, .bss, stacks, table pools) */
	struct wel2_range fdt;            /* the loader's FDT: its pages that are not mapped yet go in read-only, mapped ones are left as they are; size 0 = none */
	const struct wel2_range *dev;     /* device windows (AIC, UART, WDT), rounded outward, Device-nGnRnE XN */
	unsigned ndev;
	const struct wel2_range *rel;     /* spin-table cpu-release-addr words (8 bytes each): their pages 1:1 Normal cacheable RW XN
					   * unless already mapped (T6f: the layer stores there and does DC CVAC by VA) */
	unsigned nrel;
	struct wel2_s1_layout layout;     /* code RO X / rodata RO XN inside the carve-out */
};

/* EL2 stage 1 of the M1: RAM Normal (rounded inward to 2 MB) + the carve-out + the FDT + the devices, nothing else.
 * A device window that overlaps a non-device page is refused (-1), so is any pool exhaustion or a range outside the
 * input address space. */
int wel2_s1_build_m1(struct wel2_pt *pt, const struct wel2_s1_m1 *in, bool e2h);

/* Framebuffer of the M1: Normal non-cacheable, rounded outward, refused (-1) when any of its pages is already mapped
 * (inside the RAM map) or beyond the input range. Call after wel2_s1_build_m1. */
int wel2_s1_map_fb_free(struct wel2_pt *pt, uint64_t base, uint64_t size, bool e2h);

/* Guest stage 2 of the M1: exactly the ranges (already rounded) as Normal WB, no device, nothing else. */
int wel2_s2_build_ram(struct wel2_pt *pt, const struct wel2_range *ranges, unsigned n);

/* 0, or -1 on bad arguments / pool exhausted. */
int wel2_s2_build(struct wel2_pt *pt, const struct wel2_ram *ram, const struct wel2_range *holes, unsigned nholes);

#endif
