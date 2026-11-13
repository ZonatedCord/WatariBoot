/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 T7b: where the EL1 test guest lives on the M1 and where its info block goes (pure arithmetic, host-tested by
 * tests/test_guest_place.c; the layer's M1 hand-off in main.c calls it).
 *
 * The host loader (tools/m3a/watari-load.py) puts the guest image at the START of its own 32 MB block inside guest RAM and
 * passes wel2.guest=0x<entry> in the boot arguments. The layer needs from the guest only its arm64 Image header: the
 * entry (header magic at +0x38) and image_size (u64 at +0x10; the guest's value covers the image, .bss and its stacks).
 * Contract, all of it checked here:
 *   - entry is 4 KB aligned (the guest is position independent and runs MMU off);
 *   - 0 < image_size <= WEL2_GUEST_MAX_IMAGE (one 32 MB block);
 *   - [entry, entry + image_size) lies inside ONE guest RAM range (the ranges the guest's stage 2 maps: the usable RAM
 *     minus the layer's block, rounded inward to 32 MB, so a block that touches the layer or a hole is rejected);
 *   - the info block (info_size bytes) goes at the first 4 KB boundary at or above entry + image_size, and must also
 *     lie inside that range. The guest finds it in x0; it checks that the pointer is within 64 MB of its own start
 *     (guest/main.c info_ptr_plausible), which any placement here satisfies.
 */
#ifndef WEL2_GUEST_PLACE_H
#define WEL2_GUEST_PLACE_H

#include <stdint.h>
#include "wel2_maps.h"

#define WEL2_GUEST_MAX_IMAGE 0x2000000ULL     /* 32 MB */
#define WEL2_GUEST_ALIGN     0x1000ULL

enum wel2_guest_place_err {
	WEL2_GP_OK = 0,
	WEL2_GP_BAD_ENTRY,         /* zero or not 4 KB aligned */
	WEL2_GP_BAD_SIZE,          /* image_size zero, above 32 MB, or the range wraps */
	WEL2_GP_NOT_IN_RAM,        /* entry is not in any guest RAM range */
	WEL2_GP_IMAGE_OUT,         /* the image does not end inside the range that holds its start */
	WEL2_GP_INFO_OUT,          /* no room for the info block above the image inside the range */
};

struct wel2_guest_place {
	uint64_t entry;
	uint64_t image_end;        /* entry + image_size */
	uint64_t info;             /* guest-physical address of the info block (4 KB aligned) */
};

int wel2_guest_place(const struct wel2_range *ranges, unsigned n, uint64_t entry, uint64_t image_size,
		     uint64_t info_size, struct wel2_guest_place *out);

#endif
