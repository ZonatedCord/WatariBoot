/* SPDX-License-Identifier: MIT */
/* Watari EL2: block the layer writes into guest RAM and passes to the first guest in x0 (PLAN S14, DESIGN 8).
 * Without it (x0 is something else, e.g. the FDT of the QEMU chain build) the test guest assumes the QEMU virt
 * machine and discovers the cores from the redistributor frames. */
#ifndef WEL2_GUEST_INFO_H
#define WEL2_GUEST_INFO_H

#include <stdint.h>

#define WEL2_GINFO_MAGIC   0x464E49475A4C4557ull    /* "WEL2GINF" as a little-endian u64 */
#define WEL2_GINFO_VERSION 2u                       /* the version the layer writes when the framebuffer fields are valid */
#define WEL2_GINFO_VERSION_1 1u                     /* no framebuffer fields (written when nothing is handed over) */
#define WEL2_GINFO_MAX_CORES 8u

#define WEL2_GINFO_F_POWEROFF   (1u << 0)           /* end with SYSTEM_OFF instead of SYSTEM_RESET */
#define WEL2_GINFO_F_NO_PTIMER  (1u << 1)           /* the physical timer PPI 30 is not exposed: G8 is skipped */
#define WEL2_GINFO_F_PAUTH      (1u << 2)          /* the layer set HCR_EL2.API/APK: PAuth keys and PACIA/AUTIA do not trap */
#define WEL2_GINFO_F_FB         (1u << 3)           /* v2: fb_* describe a framebuffer the guest owns (wel2.fb=band|guest): check G12 */

struct wel2_guest_info {
    uint64_t magic;                                 /* WEL2_GINFO_MAGIC */
    uint32_t version;                               /* WEL2_GINFO_VERSION */
    uint32_t size;                                  /* sizeof(struct wel2_guest_info) */
    char     platform[16];                          /* NUL-terminated, e.g. "qemu-virt", "t8103" */
    uint64_t gicd_base;                             /* emulated GICD */
    uint64_t gicr_base;                             /* emulated GICR, frame of core 0 */
    uint64_t gicr_stride;                           /* bytes between two redistributors (0x20000) */
    uint32_t ncores;                                /* number of cores / redistributors */
    uint32_t checks;                                /* bit n = run check n (WEL2_CHK_* numbers); 0 = all */
    uint32_t cpuif_mode;                            /* WEL2_CPUIF_* */
    uint32_t flags;                                 /* WEL2_GINFO_F_* */
    uint64_t mpidr[WEL2_GINFO_MAX_CORES];           /* affinity of each core (Aff3..Aff0 as in MPIDR_EL1) */
    /* version 2 (E9); all zero in a version 1 block and whenever WEL2_GINFO_F_FB is clear */
    uint64_t fb_base;                               /* guest physical address of the first pixel the guest owns */
    uint32_t fb_width;                              /* pixels */
    uint32_t fb_height;                             /* rows the guest owns (the panel's height minus the layer's band) */
    uint32_t fb_stride;                             /* bytes per row */
    uint32_t fb_format;                             /* WEL2_GFB_X8R8G8B8 / WEL2_GFB_X2R10G10B10 (= enum wel2_fb_fmt) */
    uint32_t fb_mode;                               /* 1 band, 2 guest (enum wel2_fb_mode) */
    uint32_t fb_band_rows;                          /* rows the layer keeps above fb_base (0 in guest mode) */
};

#define WEL2_GFB_X8R8G8B8    1u                     /* X[31:24] R[23:16] G[15:8] B[7:0] */
#define WEL2_GFB_X2R10G10B10 2u                     /* X[31:30] R[29:20] G[19:10] B[9:0] */
#define WEL2_GFB_MODE_BAND   1u                     /* fb_mode values (= enum wel2_fb_mode) */
#define WEL2_GFB_MODE_GUEST  2u

#endif
