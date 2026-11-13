// SPDX-License-Identifier: MIT
// Watari EL2 T7b: placement of the EL1 test guest and its info block in guest RAM (include/wel2_guest_place.h).

#include "wel2_guest_place.h"

int wel2_guest_place(const struct wel2_range *ranges, unsigned n, uint64_t entry, uint64_t image_size,
		     uint64_t info_size, struct wel2_guest_place *out)
{
	if (!entry || (entry & (WEL2_GUEST_ALIGN - 1)))
		return WEL2_GP_BAD_ENTRY;
	if (!image_size || image_size > WEL2_GUEST_MAX_IMAGE || entry + image_size < entry)
		return WEL2_GP_BAD_SIZE;
	for (unsigned i = 0; i < n; i++) {
		uint64_t base = ranges[i].base, size = ranges[i].size, end;

		if (size == 0 || base + size < base)
			continue;
		end = base + size;
		if (entry < base || entry >= end)
			continue;
		if (entry + image_size > end)
			return WEL2_GP_IMAGE_OUT;
		uint64_t info = (entry + image_size + (WEL2_GUEST_ALIGN - 1)) & ~(WEL2_GUEST_ALIGN - 1);

		if (info < entry + image_size || info + info_size < info || info + info_size > end)
			return WEL2_GP_INFO_OUT;
		out->entry = entry;
		out->image_end = entry + image_size;
		out->info = info;
		return WEL2_GP_OK;
	}
	return WEL2_GP_NOT_IN_RAM;
}

bool wel2_ranges_cover(const struct wel2_range *ranges, unsigned n, uint64_t pa, uint64_t len)
{
	if (!len || pa + len < pa)
		return false;
	for (unsigned i = 0; i < n; i++)
		if (pa >= ranges[i].base && pa - ranges[i].base < ranges[i].size && len <= ranges[i].size - (pa - ranges[i].base))
			return true;
	return false;
}
