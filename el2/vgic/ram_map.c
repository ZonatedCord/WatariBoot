// SPDX-License-Identifier: MIT
/* Watari EL2 - guest RAM range helper. See wel2_ram.h. */
#include "wel2_ram.h"

int wel2_ram_set(struct wel2_ram *r, uint64_t base, uint64_t size, uint64_t min_base, uint64_t max_end)
{
	uint64_t end, b;

	if (!size || base + size < base)
		return -1;                              /* empty, or wraps around 2^64 */
	end = base + size;
	b = (base + WEL2_RAM_BLOCK - 1) & ~(WEL2_RAM_BLOCK - 1);
	if (b < base)
		return -1;                              /* rounding up wrapped */
	end &= ~(WEL2_RAM_BLOCK - 1);
	if (end > max_end)
		end = max_end & ~(WEL2_RAM_BLOCK - 1);
	if (b < min_base || end <= b)
		return -1;
	r->base = b;
	r->end = end;
	return 0;
}

static uint64_t be_cells(const uint8_t *p, unsigned cells)
{
	uint64_t v = 0;

	for (unsigned i = 0; i < cells; i++) {
		const uint8_t *c = p + 4 * i;

		v = (v << 32) | ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) | ((uint32_t)c[2] << 8) | c[3];
	}
	return v;
}

int wel2_ram_from_reg(struct wel2_ram *r, const uint8_t *val, uint32_t len,
		      unsigned addr_cells, unsigned size_cells, uint64_t min_base, uint64_t max_end)
{
	if (!val || addr_cells < 1 || addr_cells > 2 || size_cells < 1 || size_cells > 2 ||
	    len < 4u * (addr_cells + size_cells))
		return -1;
	return wel2_ram_set(r, be_cells(val, addr_cells), be_cells(val + 4 * addr_cells, size_cells),
			    min_base, max_end);
}

bool wel2_ram_contains(const struct wel2_ram *r, uint64_t pa)
{
	return pa >= r->base && pa < r->end;
}

bool wel2_ram_has_block(const struct wel2_ram *r, uint64_t pa)
{
	return pa >= r->base && pa + WEL2_RAM_BLOCK <= r->end;
}

enum wel2_ram_cover wel2_ram_cover(const struct wel2_ram *r, unsigned gb)
{
	uint64_t lo = (uint64_t)gb * WEL2_RAM_GB, hi = lo + WEL2_RAM_GB;

	if (r->end <= lo || r->base >= hi)
		return WEL2_RAM_NONE;
	return (r->base <= lo && r->end >= hi) ? WEL2_RAM_FULL : WEL2_RAM_PARTIAL;
}
