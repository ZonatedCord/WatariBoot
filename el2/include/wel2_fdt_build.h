/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 T12b/E5: a flattened-device-tree WRITER and the builder of the guest's tree (DESIGN watari-edk2-m1 4.1, 4.2).
 * Written from the Devicetree Specification, flattened format (chapter 5); no third-party code, no libfdt.
 *
 * Why a new tree and not a patched copy of the loader's: m1n1's prepared FDT carries the serial number, MAC addresses and
 * seeds, and Apple nodes an EDK2 FDT client cannot read; a guest never gets them. The writer therefore REFUSES, whatever
 * the caller asks, the property names in wel2_fdtw_forbidden() (asserted by tests/test_fdt_build.c), and the builder only
 * emits what DESIGN 4.2 lists. Pure logic, no hardware: host-tested, and built freestanding into the layer.
 *
 * Blob layout (version 17, last compatible 16): header (40 bytes), an empty memory-reservation block (16 zero bytes),
 * the structure block, the strings block. totalsize is exactly the bytes used. The strings block de-duplicates property
 * names. The destination buffer is bounded: a write that does not fit is an error (sticky), never a truncation.
 */
#ifndef WEL2_FDT_BUILD_H
#define WEL2_FDT_BUILD_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_maps.h"            /* struct wel2_range */

/* ---- the writer ------------------------------------------------------------------------------------------------ */

#define WEL2_FDTW_MAX_DEPTH    8u
#define WEL2_FDTW_STRS_CAP     1024u     /* property-name table (de-duplicated), bytes */
#define WEL2_FDTW_NAME_MAX     63u       /* node name (with unit address) and property name, without the NUL */

enum wel2_fdtw_err {
	WEL2_FDTW_OK = 0,
	WEL2_FDTW_E_OVERFLOW,      /* the buffer (or the name table) is full */
	WEL2_FDTW_E_STATE,         /* end without begin, property outside a node, a second root, finish with an open node */
	WEL2_FDTW_E_DEPTH,         /* nested deeper than WEL2_FDTW_MAX_DEPTH */
	WEL2_FDTW_E_NAME,          /* empty / too long / illegal characters in a node or property name */
	WEL2_FDTW_E_FORBIDDEN,     /* identifying property: see wel2_fdtw_forbidden() */
};

struct wel2_fdtw {
	uint8_t *buf;
	uint32_t cap;              /* bytes available at buf */
	uint32_t len;              /* end of the structure block so far (offset from buf) */
	uint32_t depth;
	bool root_done;
	int err;                   /* first error, WEL2_FDTW_* (sticky: later calls do nothing) */
	uint32_t strs_len;
	char strs[WEL2_FDTW_STRS_CAP];
};

/* True for the property names a guest must never get (identifying data and entropy of the host machine):
 * "serial-number", "local-mac-address", "mac-address", "kaslr-seed", "rng-seed", "linux,rng-seed". */
bool wel2_fdtw_forbidden(const char *name);

void wel2_fdtw_init(struct wel2_fdtw *w, void *buf, uint32_t cap);
void wel2_fdtw_begin(struct wel2_fdtw *w, const char *name);          /* the root node is "" (written first, once) */
void wel2_fdtw_end(struct wel2_fdtw *w);
void wel2_fdtw_prop(struct wel2_fdtw *w, const char *name, const void *val, uint32_t len);
void wel2_fdtw_prop_empty(struct wel2_fdtw *w, const char *name);
void wel2_fdtw_prop_u32(struct wel2_fdtw *w, const char *name, uint32_t v);
void wel2_fdtw_prop_u64(struct wel2_fdtw *w, const char *name, uint64_t v);
void wel2_fdtw_prop_cells(struct wel2_fdtw *w, const char *name, const uint32_t *cells, unsigned n);
void wel2_fdtw_prop_str(struct wel2_fdtw *w, const char *name, const char *s);
/* A string list: the strings back to back, each with its NUL ("a\0b\0"); n = how many. */
void wel2_fdtw_prop_strlist(struct wel2_fdtw *w, const char *name, const char *const *strs, unsigned n);
/* Closes the blob (END token, strings block, header). 0 and *size = totalsize, or -err (the first error). */
int wel2_fdtw_finish(struct wel2_fdtw *w, uint32_t *size);

/* ---- the guest tree (DESIGN 4.2) --------------------------------------------------------------------------------- */

#define WEL2_GFDT_MAX_CORES   8u
#define WEL2_GFDT_MAX         (32768u - 64u)   /* PrePi copies the FDT into the 32 KB gap behind the 64-byte image header */
#define WEL2_GFDT_ALIGN       0x4000u          /* the guest FDT is placed 16 KB aligned */
#define WEL2_GFDT_MIN_RANGE   0x8000000ULL     /* the first /memory range must be at least 128 MB */
#define WEL2_GFDT_UEFI_REGION 0x4000000ULL     /* PrePi's UEFI region (PcdSystemMemoryUefiRegionSize), at the top of that range */

struct wel2_gfdt_fb {
	uint64_t base;             /* guest physical address of the first pixel the guest may use */
	uint64_t size;             /* bytes */
	uint32_t width, height, stride;
	const char *format;        /* "x8r8g8b8" or "x2r10g10b10" */
};

struct wel2_gfdt_in {
	const char *platform;                          /* /chosen/watari,el2 platform: "qemu-virt", "t8103" */
	const struct wel2_range *ranges;               /* guest RAM, the range that holds the image FIRST (wel2_gfdt_place) */
	unsigned nranges;
	uint64_t gicd_base;                            /* emulated GICD (size 0x10000) */
	uint64_t gicr_base;                            /* emulated GICR, frame of core 0 */
	uint32_t gicr_stride;                          /* 0 = 0x20000 */
	unsigned ncores;                               /* 1..WEL2_GFDT_MAX_CORES */
	uint64_t mpidr[WEL2_GFDT_MAX_CORES];           /* Aff3.Aff2.Aff1.Aff0 as the guest reads them (bits 39:0 used) */
	uint64_t msi_frame_base;                       /* 0 = none */
	const char *fb_mode;                           /* "none" (NULL too), "band" or "guest" */
	const struct wel2_gfdt_fb *fb;                 /* NULL = no /chosen/framebuffer node */
};

/* Builds the guest FDT into buf (cap bytes). 0 and *size, or -1: bad input, buffer too small, or the result would
 * exceed WEL2_GFDT_MAX (the same bound for every caller). */
int guest_fdt_build(void *buf, uint32_t cap, const struct wel2_gfdt_in *in, uint32_t *size);

/* ---- placement and ordering (E6) -------------------------------------------------------------------------------- */

enum wel2_gfdt_place_err {
	WEL2_GFP_OK = 0,
	WEL2_GFP_BAD_ENTRY,        /* zero or not 4 KB aligned */
	WEL2_GFP_BAD_SIZE,         /* image_size zero, above 32 MB, or wrapping */
	WEL2_GFP_NOT_IN_RAM,       /* entry is in no guest RAM range */
	WEL2_GFP_IMAGE_OUT,        /* image + FDT do not end inside the range that holds the entry */
	WEL2_GFP_SMALL_RANGE,      /* that range is below WEL2_GFDT_MIN_RANGE (128 MB) */
	WEL2_GFP_UEFI_REGION,      /* image + FDT reach into the top WEL2_GFDT_UEFI_REGION bytes of that range */
	WEL2_GFP_TOO_MANY,         /* more than WEL2_MAX_GUEST_RANGES ranges */
};

struct wel2_gfdt_place {
	uint64_t entry;
	uint64_t image_end;        /* entry + image_size */
	uint64_t fdt;              /* guest physical address of the guest FDT: image_end rounded up to WEL2_GFDT_ALIGN */
	uint64_t fdt_end;          /* fdt + fdt_max */
	struct wel2_range ordered[WEL2_MAX_GUEST_RANGES];   /* the ranges, the one that holds the entry first, the rest in order */
	unsigned n;
	bool entry_2mb_aligned;    /* the arm64 boot protocol asks for 2 MB; 4 KB is accepted [hypothesis: PrePi needs only 4 KB] */
};

/* Where the guest FDT goes and in which order the ranges are listed. The first /memory node must hold the image, the
 * FDT (fdt_max bytes reserved for it) and PrePi's 64 MB UEFI region without overlap, and be at least 128 MB: PrePi takes
 * its system memory from that node only (DESIGN 3.3). Ranges above 4 GB are fine (all 64 bit). */
int wel2_gfdt_place(const struct wel2_range *ranges, unsigned n, uint64_t entry, uint64_t image_size, uint32_t fdt_max,
		    struct wel2_gfdt_place *out);

#endif
