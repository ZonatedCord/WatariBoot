// SPDX-License-Identifier: MIT
/* Watari EL2 - EL2 stage-1 and guest stage-2 maps. See wel2_maps.h.
 * Descriptor attributes from the Arm ARM (DDI 0487) D8.3 (stage 1 and stage 2 attribute fields). */
#include "wel2_maps.h"

/* Stage 1: lower attributes AttrIndx[4:2], AP[2:1] ([7:6]; AP[2] = read-only), SH[9:8], AF[10]; upper XN. */
#define S1_ATTR(i)   ((uint64_t)(i) << 2)
#define S1_AP1       (1ULL << 6)       /* AP[1]: RES1 in the EL2 regime, EL0 access in EL2&0 (left clear) */
#define S1_AP_RO     (1ULL << 7)
#define S1_SH_ISH    (3ULL << 8)
#define S1_SH_OSH    (2ULL << 8)
#define S1_AF        (1ULL << 10)
#define S1_XN        (1ULL << 54)      /* EL2 regime: XN; EL2&0: UXN */
#define S1_PXN       (1ULL << 53)      /* EL2&0 only */
#define S1_ATTR_DEV    0               /* MAIR_EL2 Attr0 = 0x00: Device-nGnRnE */
#define S1_ATTR_NC     WEL2_S1_ATTR_NC /* MAIR_EL2 Attr2 = 0x44: Normal, Inner+Outer Non-cacheable */
#define S1_ATTR_NORMAL 1              /* MAIR_EL2 Attr1 = 0xff: Normal, Inner+Outer WB, RW allocate */

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

int wel2_s1_map_device(struct wel2_pt *pt, const struct wel2_ram *ram, uint64_t base, uint64_t size, bool e2h)
{
	uint64_t g = 1ULL << pt->gshift;
	uint64_t lo = base & ~(g - 1), hi = (base + size + g - 1) & ~(g - 1);
	uint64_t ap1 = e2h ? 0 : S1_AP1;
	uint64_t xn = e2h ? (S1_XN | S1_PXN) : S1_XN;

	if (size == 0 || hi <= lo)
		return 0;
	if (hi > (1ULL << pt->va_bits))
		return -1;
	if (lo < ram->base)                         /* [0, RAM base) is Device already: drop the part below */
		lo = ram->base;
	if (hi <= lo)
		return 0;
	if (lo < ram->end)                          /* overlaps the RAM map: a device must not sit in RAM */
		return -1;
	return map_clamped(pt, lo, hi, S1_AF | S1_ATTR(S1_ATTR_DEV) | ap1 | xn);
}

int wel2_s1_map_fb(struct wel2_pt *pt, const struct wel2_ram *ram, uint64_t base, uint64_t size, bool e2h)
{
	uint64_t g = 1ULL << pt->gshift;
	uint64_t lo = base & ~(g - 1), hi = (base + size + g - 1) & ~(g - 1);
	uint64_t ap1 = e2h ? 0 : S1_AP1;
	uint64_t xn = e2h ? (S1_XN | S1_PXN) : S1_XN;

	if (size == 0 || hi <= lo || hi < base)
		return -1;
	if (hi > (1ULL << pt->va_bits))
		return -1;
	if (lo < ram->end)                          /* inside the RAM map (cacheable), or below RAM base where the
						 * Device map of [0, RAM base) would be remapped: refused */
		return -1;
	/* Normal non-cacheable (S1_ATTR_NC); for Non-cacheable memory the shareability field is ignored by the
	 * hardware (treated as Outer Shareable), set to Outer Shareable (0b10) for clarity. */
	return map_clamped(pt, lo, hi, S1_AF | S1_SH_OSH | S1_ATTR(S1_ATTR_NC) | ap1 | xn);
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

int wel2_s2_add_hole(struct wel2_range *holes, unsigned *n, unsigned cap, uint64_t base, uint64_t size,
		     unsigned gshift)
{
	uint64_t g = 1ULL << gshift, lo, hi;
	unsigned w = 0;

	if (!size)
		return 0;
	if (base + size < base || base + size > ~(g - 1))       /* wraps, or rounding up would wrap */
		return -1;
	lo = base & ~(g - 1);
	hi = (base + size + g - 1) & ~(g - 1);
	for (unsigned i = 0; i < *n; i++)
		if (lo >= holes[i].base && hi <= holes[i].base + holes[i].size)
			return 0;                               /* already covered */
	for (unsigned i = 0; i < *n; i++)                       /* survivors: holes the new range does not contain */
		if (!(holes[i].base >= lo && holes[i].base + holes[i].size <= hi))
			w++;
	if (w >= cap)
		return -1;                                      /* full: nothing modified */
	w = 0;
	for (unsigned i = 0; i < *n; i++)
		if (!(holes[i].base >= lo && holes[i].base + holes[i].size <= hi))
			holes[w++] = holes[i];
	holes[w].base = lo;
	holes[w].size = hi - lo;
	*n = w + 1;
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

/* ---- Apple M1 maps (T6c) ---------------------------------------------------------------------------------------- */

int wel2_ranges_inward(const struct wel2_range *in, unsigned n, uint64_t align, uint64_t cut_base, uint64_t cut_size,
		       struct wel2_range *out, unsigned max, unsigned *nout)
{
	uint64_t cl = 0, ch = 0;
	unsigned w = 0;

	if (cut_size) {
		if (cut_base + cut_size < cut_base || cut_base + cut_size > ~(align - 1))
			return -1;
		cl = cut_base & ~(align - 1);
		ch = (cut_base + cut_size + align - 1) & ~(align - 1);
	}
	for (unsigned i = 0; i < n; i++) {
		uint64_t lo, hi, end = in[i].base + in[i].size;

		if (end < in[i].base)
			return -1;
		if (end - in[i].base < align)
			continue;
		lo = (in[i].base + align - 1) & ~(align - 1);
		hi = end & ~(align - 1);
		if (lo < in[i].base || lo >= hi)
			continue;
		if (cut_size && lo < ch && hi > cl) {           /* overlaps the cut: keep the pieces on either side */
			if (lo < cl) {
				if (w >= max)
					return -1;
				out[w].base = lo;
				out[w].size = cl - lo;
				w++;
			}
			if (hi > ch) {
				if (w >= max)
					return -1;
				out[w].base = ch;
				out[w].size = hi - ch;
				w++;
			}
			continue;
		}
		if (w >= max)
			return -1;
		out[w].base = lo;
		out[w].size = hi - lo;
		w++;
	}
	*nout = w;
	return 0;
}

/* Maps [base, base+size) rounded outward to `attrs`, but only if none of its pages is mapped yet. */
static int map_fresh(struct wel2_pt *pt, uint64_t base, uint64_t size, uint64_t attrs)
{
	uint64_t g = 1ULL << pt->gshift, lo, hi;

	if (!size || base + size < base || base + size > ~(g - 1))
		return -1;
	lo = base & ~(g - 1);
	hi = (base + size + g - 1) & ~(g - 1);
	if (hi > (1ULL << pt->va_bits))
		return -1;
	for (uint64_t a = lo; a < hi; a += g)
		if (wel2_pt_walk(pt, a, 0, 0, 0))
			return -1;
	return wel2_pt_map(pt, lo, lo, hi - lo, attrs);
}

int wel2_s1_build_m1(struct wel2_pt *pt, const struct wel2_s1_m1 *in, bool e2h)
{
	uint64_t ap1 = e2h ? 0 : S1_AP1;
	uint64_t xn = e2h ? (S1_XN | S1_PXN) : S1_XN;
	uint64_t nrm = S1_AF | S1_SH_ISH | S1_ATTR(S1_ATTR_NORMAL) | ap1;
	uint64_t limit = 1ULL << pt->va_bits;
	uint64_t g = 1ULL << pt->gshift;

	for (unsigned i = 0; i < in->nram; i++) {
		uint64_t lo = (in->ram[i].base + WEL2_M1_S1_ALIGN - 1) & ~(WEL2_M1_S1_ALIGN - 1);
		uint64_t end = in->ram[i].base + in->ram[i].size, hi = end & ~(WEL2_M1_S1_ALIGN - 1);

		if (end < in->ram[i].base)
			return -1;
		if (lo >= hi || lo < in->ram[i].base)
			continue;
		if (hi > limit)
			return -1;
		if (wel2_pt_map(pt, lo, lo, hi - lo, nrm | xn))
			return -1;
	}
	if (in->carve.size) {
		uint64_t lo = in->carve.base & ~(g - 1), hi = (in->carve.base + in->carve.size + g - 1) & ~(g - 1);

		if (hi > limit || hi <= lo || wel2_pt_map(pt, lo, lo, hi - lo, nrm | xn))
			return -1;
	}
	if (in->layout.text_hi > in->layout.text_lo &&
	    map_clamped(pt, in->layout.text_lo, in->layout.text_hi, nrm | S1_AP_RO | (e2h ? S1_XN : 0)))
		return -1;
	if (in->layout.ro_hi > in->layout.text_hi &&
	    map_clamped(pt, in->layout.text_hi, in->layout.ro_hi, nrm | S1_AP_RO | xn))
		return -1;
	if (in->fdt.size) {
		uint64_t lo = in->fdt.base & ~(g - 1), hi = (in->fdt.base + in->fdt.size + g - 1) & ~(g - 1);

		if (in->fdt.base + in->fdt.size < in->fdt.base || hi > limit)
			return -1;
		/* Only the pages that are not mapped yet, one by one: a page that is already RAM or carve-out keeps its
		 * read-write mapping (it is never downgraded to read-only). */
		for (uint64_t a = lo; a < hi; a += g)
			if (!wel2_pt_walk(pt, a, 0, 0, 0) && wel2_pt_map(pt, a, a, g, nrm | S1_AP_RO | xn))
				return -1;
	}
	for (unsigned i = 0; i < in->nrel; i++) {
		uint64_t lo = in->rel[i].base & ~(g - 1), hi = (in->rel[i].base + in->rel[i].size + g - 1) & ~(g - 1);

		if (!in->rel[i].size)
			continue;
		if (in->rel[i].base + in->rel[i].size < in->rel[i].base || hi > limit)
			return -1;
		for (uint64_t a = lo; a < hi; a += g) {
			uint64_t at;

			if (wel2_pt_walk(pt, a, 0, &at, 0)) {
				if (((at >> 2) & 7) != S1_ATTR_NORMAL || (at & S1_AP_RO))
					return -1;      /* the word sits in a device, non-cacheable or read-only (FDT) page: refused */
				continue;
			}
			if (wel2_pt_map(pt, a, a, g, nrm | xn))
				return -1;
		}
	}
	for (unsigned i = 0; i < in->ndev; i++) {
		uint64_t lo = in->dev[i].base & ~(g - 1), hi = (in->dev[i].base + in->dev[i].size + g - 1) & ~(g - 1);

		if (!in->dev[i].size)
			continue;
		if (in->dev[i].base + in->dev[i].size < in->dev[i].base || hi > limit)
			return -1;
		/* two windows may share a page (the t8103 AIC lists reg[1] inside reg[0]): an already Device page is fine,
		 * a RAM page is not. */
		for (uint64_t a = lo; a < hi; a += g) {
			uint64_t at;

			if (wel2_pt_walk(pt, a, 0, &at, 0) && ((at >> 2) & 7) != S1_ATTR_DEV)
				return -1;
		}
		if (wel2_pt_map(pt, lo, lo, hi - lo, S1_AF | S1_ATTR(S1_ATTR_DEV) | ap1 | xn))
			return -1;
	}
	return 0;
}

int wel2_s1_map_fb_free(struct wel2_pt *pt, uint64_t base, uint64_t size, bool e2h)
{
	uint64_t ap1 = e2h ? 0 : S1_AP1;
	uint64_t xn = e2h ? (S1_XN | S1_PXN) : S1_XN;

	return map_fresh(pt, base, size, S1_AF | S1_SH_OSH | S1_ATTR(S1_ATTR_NC) | ap1 | xn);
}

int wel2_s2_build_ram(struct wel2_pt *pt, const struct wel2_range *ranges, unsigned n)
{
	for (unsigned i = 0; i < n; i++)
		if (ranges[i].size &&
		    wel2_pt_map(pt, ranges[i].base, ranges[i].base, ranges[i].size,
				S2_AF | S2_S2AP_RW | S2_ATTR_NORMAL | S2_SH_ISH))
			return -1;
	return 0;
}
