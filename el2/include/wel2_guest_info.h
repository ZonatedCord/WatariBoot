/* SPDX-License-Identifier: MIT */
/* Watari EL2: block the layer writes into guest RAM and passes to the first guest in x0 (PLAN S14, DESIGN 8).
 * Without it (x0 is something else, e.g. the FDT of the QEMU chain build) the test guest assumes the QEMU virt
 * machine and discovers the cores from the redistributor frames. */
#ifndef WEL2_GUEST_INFO_H
#define WEL2_GUEST_INFO_H

#include <stdint.h>

#define WEL2_GINFO_MAGIC   0x464E49475A4C4557ull    /* "WEL2GINF" as a little-endian u64 */
#define WEL2_GINFO_VERSION 1u
#define WEL2_GINFO_MAX_CORES 8u

#define WEL2_GINFO_F_POWEROFF   (1u << 0)           /* end with SYSTEM_OFF instead of SYSTEM_RESET */
#define WEL2_GINFO_F_NO_PTIMER  (1u << 1)           /* the physical timer PPI 30 is not exposed: G8 is skipped */
#define WEL2_GINFO_F_PAUTH      (1u << 2)           /* the layer set HCR_EL2.API/APK: PAuth keys and PACIA/AUTIA do not trap */

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
};

#endif
