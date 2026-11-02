// SPDX-License-Identifier: MIT
/* Watari EL2 - EL2 stage-1 and guest stage-2 maps. See wel2_maps.h.
 * Descriptor attributes from the Arm ARM (DDI 0487) D8.3 (stage 1 and stage 2 attribute fields). */
#include "wel2_maps.h"

/* Stage 1: lower attributes AttrIndx[4:2], AP[2:1] ([7:6]; AP[2] = read-only), SH[9:8], AF[10]; upper XN. */
#define S1_ATTR(i)   ((uint64_t)(i) << 2)
#define S1_AP1       (1ULL << 6)       /* AP[1]: RES1 in the EL2 regime, EL0 access in EL2&0 (left clear) */
#define S1_AP_RO     (1ULL << 7)
#define S1_SH_ISH    (3ULL << 8)
#define S1_AF        (1ULL << 10)
#define S1_XN        (1ULL << 54)      /* EL2 regime: XN; EL2&0: UXN */
#define S1_PXN       (1ULL << 53)      /* EL2&0 only */
#define S1_ATTR_DEV    0               /* MAIR_EL2 Attr0 = 0x00: Device-nGnRnE */
#define S1_ATTR_NORMAL 1               /* MAIR_EL2 Attr1 = 0xff: Normal, Inner+Outer WB, RW allocate */

/* Stage 2: MemAttr[5:2], S2AP[7:6], SH[9:8], AF[10]; XN[54]. */
#define S2_AF        (1ULL << 10)
#define S2_SH_ISH    (3ULL << 8)
#define S2_S2AP_RW   (3ULL << 6)
#define S2_XN        (1ULL << 54)
#define S2_ATTR_DEV    (0x0ULL << 2)   /* MemAttr 0b0000: Device-nGnRnE */
#define S2_ATTR_NORMAL (0xfULL << 2)   /* MemAttr 0b1111: Normal, Inner+Outer WB */

unsigned wel2_s1_va_bits(unsigned gshift)
{
	return gshift == WEL2_GRAN_4K ? 39 : 36;
}

static int map_clamped(struct wel2_pt *pt, uint64_t lo, uint64_t hi, uint64_t attrs)
{
	uint64_t limit = 1ULL << pt->va_bits;

	if (hi > limit)
		hi = limit;
	if (lo >= hi)
		return 0;
	return wel2_pt_map(pt, lo, lo, hi - lo, attrs);
}

int wel2_s1_build(struct wel2_pt *pt, const struct wel2_ram *ram, const struct wel2_s1_layout *l, bool e2h)
{
	uint64_t ap1 = e2h ? 0 : S1_AP1;
	uint64_t xn = e2h ? (S1_XN | S1_PXN) : S1_XN;
	uint64_t nrm = S1_AF | S1_SH_ISH | S1_ATTR(S1_ATTR_NORMAL) | ap1;
	uint64_t dev = S1_AF | S1_ATTR(S1_ATTR_DEV) | ap1 | xn;

	if (map_clamped(pt, 0, ram->base, dev) || map_clamped(pt, ram->base, ram->end, nrm | xn))
		return -1;
	if (l->text_hi > l->text_lo && map_clamped(pt, l->text_lo, l->text_hi, nrm | S1_AP_RO | (e2h ? S1_XN : 0)))
		return -1;
	if (l->ro_hi > l->text_hi && map_clamped(pt, l->text_hi, l->ro_hi, nrm | S1_AP_RO | xn))
		return -1;
	return 0;
}

int wel2_s2_build(struct wel2_pt *pt, const struct wel2_ram *ram, const struct wel2_range *holes, unsigned nholes)
{
	if (map_clamped(pt, 0, 1ULL << pt->va_bits, S2_AF | S2_S2AP_RW | S2_ATTR_DEV | S2_XN))
		return -1;
	if (map_clamped(pt, ram->base, ram->end, S2_AF | S2_S2AP_RW | S2_ATTR_NORMAL | S2_SH_ISH))
		return -1;
	for (unsigned i = 0; i < nholes; i++)
		if (holes[i].size && wel2_pt_unmap(pt, holes[i].base, holes[i].size))
			return -1;
	return 0;
}
