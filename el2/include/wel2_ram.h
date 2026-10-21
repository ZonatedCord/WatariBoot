/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - guest RAM range from the FDT /memory node (host-testable helper, #60).
 *
 * The EL2 stage-1 and the stage-2 maps used to cover a fixed 0x40000000-0x13fffffff. The range is
 * now computed from what the firmware reports in /memory/reg (Devicetree Specification, 3.4: "reg"
 * is <address size> in #address-cells / #size-cells cells, big-endian), rounded to 2 MB blocks and
 * clamped to the translation limits. The page-table builders ask this module which 1 GB entries
 * are fully RAM (one 1 GB block), partly RAM (a level-2 table of 2 MB blocks) or not RAM.
 * Pure arithmetic, no hardware access.
 */
#ifndef WEL2_RAM_H
#define WEL2_RAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEL2_RAM_BLOCK 0x200000ULL      /* 2 MB: the map granule */
#define WEL2_RAM_GB    0x40000000ULL

struct wel2_ram {
	uint64_t base, end;             /* [base, end), both 2 MB aligned */
};

enum wel2_ram_cover { WEL2_RAM_NONE = 0, WEL2_RAM_PARTIAL, WEL2_RAM_FULL };

/* RAM = [base, base+size) rounded inwards to 2 MB, end clamped to max_end. Returns -1 (r untouched)
 * if size is 0, base+size wraps, the rounded base is below min_base or the range is empty. */
int wel2_ram_set(struct wel2_ram *r, uint64_t base, uint64_t size, uint64_t min_base, uint64_t max_end);

/* The same from a /memory "reg" property: the first <address size> pair, big-endian cells. Returns -1
 * for unusable cell counts (1 or 2 each), a property shorter than one pair, or what wel2_ram_set rejects. */
int wel2_ram_from_reg(struct wel2_ram *r, const uint8_t *val, uint32_t len,
		      unsigned addr_cells, unsigned size_cells, uint64_t min_base, uint64_t max_end);

/* How much of the 1 GB entry `gb` (physical address gb << 30) is RAM. */
enum wel2_ram_cover wel2_ram_cover(const struct wel2_ram *r, unsigned gb);
/* True if the 2 MB block at physical address pa (2 MB aligned) is RAM. */
bool wel2_ram_has_block(const struct wel2_ram *r, uint64_t pa);
/* True if pa lies in RAM. */
bool wel2_ram_contains(const struct wel2_ram *r, uint64_t pa);

#endif
