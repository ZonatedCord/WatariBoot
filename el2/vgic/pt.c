// SPDX-License-Identifier: MIT
/* Watari EL2 - granule-parametric translation table builder. See wel2_pt.h.
 * Written from the Arm ARM (DDI 0487) D8.2 (translation process, 4 KB and 16 KB tables) and the
 * TCR_EL2 / VTCR_EL2 register descriptions. */
#include "wel2_pt.h"

static unsigned bits_per_level(unsigned gshift)
{
	return gshift - 3;
}

/* Size of the range one entry of `level` covers. */
static uint64_t entry_size(const struct wel2_pt *pt, unsigned level)
{
	return 1ULL << (pt->gshift + bits_per_level(pt->gshift) * (3 - level));
}

/* A block descriptor is allowed at level 2 (always) and level 1 (4 KB granule only: with 16 KB it needs
 * FEAT_LPA2, Table D8-27); level 3 holds pages. */
static bool leaf_allowed(const struct wel2_pt *pt, unsigned level)
{
	return level == 3 || level == 2 || (level == 1 && pt->gshift == WEL2_GRAN_4K);
}

/* Take a zeroed table from the pool. */
static uint64_t *table_alloc(struct wel2_pt *pt)
{
	uint64_t *t;
	unsigned n = 1u << (pt->gshift - 3);

	if (pt->used >= pt->ntables)
		return 0;
	t = (uint64_t *)(void *)(pt->pool + ((uint64_t)pt->used << pt->gshift));
	pt->used++;
	if (pt->used > pt->max_used)
		pt->max_used = pt->used;
	for (unsigned i = 0; i < n; i++)
		t[i] = 0;
	return t;
}

int wel2_pt_init(struct wel2_pt *pt, void *pool, uint64_t pool_bytes, unsigned gshift, unsigned va_bits)
{
	uint64_t tsz = 1ULL << gshift;
	unsigned b, levels;

	if ((gshift != WEL2_GRAN_4K && gshift != WEL2_GRAN_16K) || !pool || ((uintptr_t)pool & (tsz - 1)) ||
	    pool_bytes < tsz)
		return -1;
	b = bits_per_level(gshift);
	if (va_bits <= gshift || va_bits > 48)
		return -1;
	levels = (va_bits - gshift + b - 1) / b;
	if (levels > 4)
		return -1;
	pt->gshift = gshift;
	pt->va_bits = va_bits;
	pt->start_level = 4 - levels;
	pt->root_entries = 1u << (va_bits - (gshift + b * (levels - 1)));
	pt->pool = pool;
	pt->ntables = (unsigned)(pool_bytes / tsz);
	pt->used = 0;
	pt->max_used = 0;
	pt->root = table_alloc(pt);
	return 0;
}

static bool is_table(unsigned level, uint64_t d)
{
	return level < 3 && (d & 3) == 3;
}

static uint64_t leaf_desc(unsigned level, uint64_t pa, uint64_t attrs)
{
	return pa | attrs | (level == 3 ? 3ULL : 1ULL);
}

/* The bits of a leaf descriptor that are attributes (neither type nor output address). */
static uint64_t attr_of(const struct wel2_pt *pt, uint64_t d)
{
	return d & ~WEL2_PTE_OA_MASK(pt->gshift) & ~3ULL;
}

/* Replace the block at entry *e (level `level`) by a table of equivalent descriptors one level down. */
static int split_block(struct wel2_pt *pt, unsigned level, uint64_t *e)
{
	uint64_t d = *e, oa = d & WEL2_PTE_OA_MASK(pt->gshift), attrs = attr_of(pt, d);
	uint64_t sub = entry_size(pt, level + 1);
	uint64_t *t = table_alloc(pt);
	unsigned n = 1u << (pt->gshift - 3);

	if (!t)
		return -1;
	for (unsigned i = 0; i < n; i++)
		t[i] = leaf_desc(level + 1, oa + (uint64_t)i * sub, attrs);
	*e = (uint64_t)(uintptr_t)t | 3ULL;
	return 0;
}

/* Map (or unmap) the part of [va, va+size) inside the table at `level`, whose first byte covers tbase. */
static int walk_range(struct wel2_pt *pt, unsigned level, uint64_t *table, uint64_t tbase, unsigned nent,
		      uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs, bool do_map)
{
	uint64_t esz = entry_size(pt, level);
	uint64_t lo = va, hi = va + size;

	for (unsigned i = 0; i < nent; i++) {
		uint64_t e_lo = tbase + (uint64_t)i * esz, e_hi = e_lo + esz;
		uint64_t *e = &table[i];
		uint64_t s, t;

		if (e_hi <= lo || e_lo >= hi)
			continue;
		s = lo > e_lo ? lo : e_lo;              /* the overlap with this entry */
		t = hi < e_hi ? hi : e_hi;

		if (s == e_lo && t == e_hi && leaf_allowed(pt, level) &&
		    (!do_map || !((pa + (e_lo - va)) & (esz - 1)))) {
			*e = do_map ? leaf_desc(level, pa + (e_lo - va), attrs) : 0;   /* a sub-table, if any, is dropped */
			continue;
		}
		if (level == 3)
			return -1;                      /* cannot happen with granule-aligned arguments */
		if (!(*e & 1)) {
			uint64_t *nt;

			if (!do_map)
				continue;               /* nothing mapped here */
			nt = table_alloc(pt);
			if (!nt)
				return -1;
			*e = (uint64_t)(uintptr_t)nt | 3ULL;
		} else if (!is_table(level, *e)) {
			if (split_block(pt, level, e))
				return -1;
		}
		if (walk_range(pt, level + 1, (uint64_t *)(uintptr_t)(*e & WEL2_PTE_OA_MASK(pt->gshift)), e_lo,
			       1u << (pt->gshift - 3), s, pa + (s - va), t - s, attrs, do_map))
			return -1;
	}
	return 0;
}

static int do_range(struct wel2_pt *pt, uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs, bool do_map)
{
	uint64_t g = 1ULL << pt->gshift, limit = 1ULL << pt->va_bits;

	if (!pt->root || !size || ((va | size) & (g - 1)) || (do_map && ((pa | size) & (g - 1))))
		return -1;
	if (va >= limit || size > limit - va || (do_map && pa + size < pa))
		return -1;
	return walk_range(pt, pt->start_level, pt->root, 0, pt->root_entries, va, pa, size,
			  attrs & ~(WEL2_PTE_OA_MASK(pt->gshift) | 3ULL), do_map);
}

int wel2_pt_map(struct wel2_pt *pt, uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs)
{
	return do_range(pt, va, pa, size, attrs, true);
}

int wel2_pt_unmap(struct wel2_pt *pt, uint64_t va, uint64_t size)
{
	return do_range(pt, va, 0, size, 0, false);
}

bool wel2_pt_walk(const struct wel2_pt *pt, uint64_t va, uint64_t *pa, uint64_t *attrs, unsigned *level)
{
	const uint64_t *table = pt->root;
	unsigned lv = pt->start_level;
	uint64_t base = 0;

	if (!table || va >= (1ULL << pt->va_bits))
		return false;
	for (;;) {
		uint64_t esz = entry_size(pt, lv);
		unsigned idx = (unsigned)((va - base) / esz);
		uint64_t d = table[idx];

		if (!(d & 1))
			return false;
		if (is_table(lv, d)) {
			table = (const uint64_t *)(const void *)(uintptr_t)(d & WEL2_PTE_OA_MASK(pt->gshift));
			base += (uint64_t)idx * esz;
			lv++;
			continue;
		}
		if (lv == 3 && (d & 3) != 3)
			return false;                   /* reserved encoding at level 3 */
		if (pa)
			*pa = (d & WEL2_PTE_OA_MASK(pt->gshift)) + ((va - base) & (esz - 1));
		if (attrs)
			*attrs = attr_of(pt, d);
		if (level)
			*level = lv;
		return true;
	}
}

uint64_t wel2_vtcr(const struct wel2_pt *pt, unsigned ps)
{
	unsigned sl0 = (pt->gshift == WEL2_GRAN_4K ? 2u : 3u) - pt->start_level;
	uint64_t tg0 = pt->gshift == WEL2_GRAN_4K ? 0ULL : 2ULL;

	return (1ULL << 31) | ((uint64_t)(ps & 7) << 16) | (tg0 << 14) | (3ULL << 12) | (1ULL << 10) |
	       (1ULL << 8) | ((uint64_t)sl0 << 6) | (uint64_t)(64 - pt->va_bits);
}

uint64_t wel2_tcr_el2(const struct wel2_pt *pt, bool e2h, unsigned ps)
{
	uint64_t t0sz = 64 - pt->va_bits;
	uint64_t tg0 = pt->gshift == WEL2_GRAN_4K ? 0ULL : 2ULL;
	uint64_t tcr = t0sz | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (tg0 << 14);

	if (!e2h)
		return tcr | ((uint64_t)(ps & 7) << 16) | (1ULL << 23) | (1ULL << 31);
	return tcr | (t0sz << 16) | (1ULL << 23) | ((pt->gshift == WEL2_GRAN_4K ? 2ULL : 1ULL) << 30) |
	       ((uint64_t)(ps & 7) << 32);
}
