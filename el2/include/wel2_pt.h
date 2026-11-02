/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - granule-parametric translation table builder (#58, host-testable).
 *
 * One builder for the EL2 stage 1 and the guest stage 2, 4 KB or 16 KB granule (VMSAv8-64, Arm ARM D8.2:
 * Table D8-26/D8-27 for 16 KB, the 4 KB equivalents for 4 KB). Tables come from a caller-provided pool
 * (static memory on the target, a buffer in the host tests); nothing here touches hardware or calls libc,
 * and every store is an aligned 64-bit one, so the code is safe with the MMU off (stage1.c).
 *
 * The caller passes the attribute bits of a leaf descriptor (everything except the type bits [1:0] and the
 * output address); the builder adds the type: block (01) at level 1/2, page (11) at level 3. A block is used
 * wherever an aligned range covers a whole entry, so a 16 KB map uses 32 MB blocks at level 2 (a 4 KB one 2 MB
 * blocks and, at level 1, 1 GB blocks; the 16 KB granule has no level-1 block without FEAT_LPA2, Table D8-27).
 * A finer map over an existing block splits it (a new table holding the equivalent descriptors); unmapping
 * part of a block splits it too. Unmapped = descriptor 0. Sub-tables that a larger block replaces are not
 * returned to the pool (the maps are built once at boot).
 */
#ifndef WEL2_PT_H
#define WEL2_PT_H

#include <stdbool.h>
#include <stdint.h>

#define WEL2_GRAN_4K   12u
#define WEL2_GRAN_16K  14u

struct wel2_pt {
	unsigned gshift;            /* 12 or 14: log2 of the page size */
	unsigned va_bits;           /* input address size: T0SZ = 64 - va_bits */
	unsigned start_level;       /* level of the root table (0..2) */
	unsigned root_entries;      /* entries of the root table the input address size really uses */
	uint64_t *root;             /* root table, taken from the pool by wel2_pt_init */
	uint8_t *pool;              /* first byte of the pool (aligned to the table size) */
	unsigned ntables, used;     /* pool capacity and tables taken, in units of one table (1 << gshift bytes) */
	unsigned max_used;          /* high-water mark, for sizing the pool */
};

/* Descriptor type bits and output address mask (bits [47:gshift]). */
#define WEL2_PTE_VALID  1ULL
#define WEL2_PTE_TABLE  2ULL        /* table at level 0-2, page at level 3 */
#define WEL2_PTE_OA_MASK(g)  (((1ULL << 48) - 1) & ~((1ULL << (g)) - 1))

/* Pool: pool_bytes / (1 << gshift) tables, the pool aligned to the table size. va_bits 15..48 (16 KB: the
 * root at level 1 or 2, the level-0 case needs 52-bit addressing and is refused). Returns 0, or -1 for a bad
 * geometry or alignment. The root table is taken from the pool (zeroed). */
int wel2_pt_init(struct wel2_pt *pt, void *pool, uint64_t pool_bytes, unsigned gshift, unsigned va_bits);

/* Map [va, va+size) to [pa, ...) with the leaf attributes `attrs`. va, pa, size must be multiples of the
 * granule and the range inside the input address space. Returns 0, or -1 (bad arguments or pool
 * exhausted: the tables are then partly built, the caller treats it as fatal). */
int wel2_pt_map(struct wel2_pt *pt, uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs);
/* Unmap [va, va+size). Same argument rules. */
int wel2_pt_unmap(struct wel2_pt *pt, uint64_t va, uint64_t size);

/* Walk the tables for one address. True if mapped: *pa gets the output address (with the page offset),
 * *attrs the leaf attribute bits, *level the level of the leaf (any pointer may be NULL). The tables hold
 * addresses as plain integers (identity on the target), so the host tests cast them back to pointers. */
bool wel2_pt_walk(const struct wel2_pt *pt, uint64_t va, uint64_t *pa, uint64_t *attrs, unsigned *level);

/* Address (= pointer value) of the root table, for TTBR0_EL2 / VTTBR_EL2. */
static inline uint64_t wel2_pt_root_addr(const struct wel2_pt *pt) { return (uint64_t)(uintptr_t)pt->root; }

/* Register values; `ps` is the 3-bit physical address size (PS / IPS). */

/* VTCR_EL2 (D24.2.223): RES1 bit 31, PS[18:16], TG0[15:14] (00 4 KB, 10 16 KB), SH0 = 3, ORGN0 = IRGN0 = 1
 * (WB WA), SL0[7:6] (4 KB: 2 - start level, 16 KB: 3 - start level), T0SZ[5:0]. */
uint64_t wel2_vtcr(const struct wel2_pt *pt, unsigned ps);

/* TCR_EL2 (D24.2.194). e2h = 0 ("E2H0 variant", one VA range): T0SZ, IRGN0 = ORGN0 = 1, SH0 = 3, TG0, PS[18:16],
 * RES1 bits 23 and 31. e2h = 1 (EL2&0, TCR_EL1-like): the same low fields, T1SZ[21:16] = T0SZ, EPD1[23] = 1 (no
 * TTBR1_EL2 walks), TG1[31:30] = the granule in TG1 encoding (01 16 KB, 10 4 KB), IPS[34:32]. */
uint64_t wel2_tcr_el2(const struct wel2_pt *pt, bool e2h, unsigned ps);

#endif
