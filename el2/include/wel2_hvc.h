/* SPDX-License-Identifier: MIT */
/* Watari EL2: vendor hypercall ABI shared by the layer and the EL1 test guest (T7, PLAN S14, DESIGN section 8).
 *
 * Convention: SMCCC fast call over `HVC #0`, SMC64 calling convention. Function id in w0, arguments x1-x3, result in
 * x0, x1-x3 returned as zero. The ids lie in the Vendor Specific Hypervisor range 0xC6000000-0xC600FFFF and avoid
 * the reserved general-query ids 0xC600FF00-0xC600FFFF [verified: R:docs/design/watari-el2/INTERFACES.md 9].
 * A guest must treat NOT_SUPPORTED as "the layer does not implement this call". */
#ifndef WEL2_HVC_H
#define WEL2_HVC_H

#include <stddef.h>
#include <stdint.h>

#define WEL2_HVC_VENDOR_FIRST 0xC6000000u
#define WEL2_HVC_VENDOR_LAST  0xC600FEFFu   /* 0xC600FF00-0xC600FFFF are the SMCCC general-query ids */

#define WEL2_VERSION          0xC6000000u   /* x0 = (WEL2_VERSION_TAG << 32) | WEL2_HVC_ABI_VERSION */
#define WEL2_STATS            0xC6000001u   /* x1 = guest PA of a buffer, x2 = its size in bytes */
#define WEL2_LOG              0xC6000002u   /* x1 = guest PA of text, x2 = length (<= WEL2_LOG_MAX) */
#define WEL2_CHECKPOINT       0xC6000003u   /* x1 = check number (WEL2_CHK_*), x2 = WEL2_CHKSTATE_* */
#define WEL2_CONSOLE_WRITE    0xC6000004u   /* x1 = guest PA of raw bytes, x2 = length (<= WEL2_CONSOLE_MAX) */

#define WEL2_HVC_ABI_VERSION  1u
#define WEL2_VERSION_TAG      0x57454C32ull /* 'WEL2' */

/* SMCCC return codes (DEN0028 5.2) */
#define WEL2_HVC_SUCCESS             0
#define WEL2_HVC_NOT_SUPPORTED       (-1)
#define WEL2_HVC_INVALID_PARAMETERS  (-2)

/* WEL2_LOG: one line, at most WEL2_LOG_MAX bytes, taken up to the first NUL (so x2 is an upper bound). x2 above the
 * limit, a zero x2, or a range outside guest RAM is INVALID_PARAMETERS and nothing is logged. */
#define WEL2_LOG_MAX          160u

/* T17: the guest's per-check summary block. Its first line starts with WEL2_SUMMARY_MARK; the layer then makes sure
 * WEL2_SUMMARY_LINES text rows are free on the panel (clearing the scrolling area first if they are not), so the block
 * is never cut by a scroll. Block = header + 13 checks (G1-G12, E9 added G12) + up to 2 MPIDR lines, plus slack for the layer's own last
 * lines (hold / reset message). */
#define WEL2_SUMMARY_MARK     "SUMMARY"
#define WEL2_SUMMARY_LINES    19u

/* WEL2_CONSOLE_WRITE (T12b/E7, DESIGN watari-edk2-m1 7): a raw byte stream, the console of a firmware without a UART. The bytes
 * go to the UART unchanged (CR, LF, escape sequences, 0x00-0xFF alike); the framebuffer text shows the printable ones
 * (0x20-0x7e) and LF only. At most WEL2_CONSOLE_MAX bytes per call: a longer x2 is INVALID_PARAMETERS and nothing is
 * written (the caller splits; no partial write). x2 = 0 is a no-op SUCCESS (the range is not looked at). A range
 * outside guest RAM is INVALID_PARAMETERS. SUCCESS returns x0 = 0. NOT_SUPPORTED = no such call in this layer: be quiet. */
#define WEL2_CONSOLE_MAX      160u

/* WEL2_CHECKPOINT numbers: the layer paints square WEL2_CHK_SQUARE_BASE + n and logs one line. */
#define WEL2_CHK_SQUARE_BASE  16u
#define WEL2_CHK_MAX          47u           /* largest accepted n (the checkpoint row has 64 squares) */
enum wel2_chk {
    WEL2_CHK_G1 = 1, WEL2_CHK_G2, WEL2_CHK_G3, WEL2_CHK_G4, WEL2_CHK_G5A, WEL2_CHK_G5B,
    WEL2_CHK_G6, WEL2_CHK_G7, WEL2_CHK_G8, WEL2_CHK_G9, WEL2_CHK_G10, WEL2_CHK_G11,
    WEL2_CHK_G12,                                   /* E9: four colour bars in the framebuffer the layer handed over; runs before G11 */
    WEL2_CHK_LAST = WEL2_CHK_G12
};
#define WEL2_CHKSTATE_RUNNING 0u
#define WEL2_CHKSTATE_PASS    1u
#define WEL2_CHKSTATE_FAIL    2u
#define WEL2_CHKSTATE_SKIP    3u            /* the check was not run (grey square, counted as SKIP in the status band) */

/* WEL2_STATS: the layer's ICC trap counters, written to the guest buffer (x2 must be >= sizeof(struct wel2_stats),
 * else INVALID_PARAMETERS; the buffer must lie in guest RAM and be 8-byte aligned). Counters are system wide. */
enum wel2_stat_reg {
    WEL2_STAT_ICC_SRE,                                                   /* NOT a TALL1 register: must stay 0 (G5a) */
    WEL2_STAT_ICC_IGRPEN1, WEL2_STAT_ICC_BPR1,                           /* TALL1 group (G5a) */
    WEL2_STAT_ICC_PMR, WEL2_STAT_ICC_CTLR, WEL2_STAT_ICC_RPR,            /* TC group (G5b) */
    WEL2_STAT_ICC_IAR1, WEL2_STAT_ICC_EOIR1, WEL2_STAT_ICC_DIR,          /* acknowledge path */
    WEL2_STAT_ICC_OTHER,
    WEL2_STAT_COUNT
};
#define WEL2_CPUIF_HW         0u            /* struct wel2_stats.cpuif_mode */
#define WEL2_CPUIF_TRAP       1u            /* TALL0 | TALL1 | TC | TDIR */
#define WEL2_CPUIF_TRAP_NOTC  2u            /* TALL0 | TALL1 | TDIR, no TC */
/* The buffer must hold at least WEL2_STATS_V1_SIZE bytes (the counters above); the layer fills as many bytes of
 * struct wel2_stats as the buffer holds, so a guest built against the older, shorter layout keeps working. */
struct wel2_stats {
    uint32_t version;                       /* WEL2_HVC_ABI_VERSION */
    uint32_t cpuif_mode;                    /* WEL2_CPUIF_* the layer runs with */
    uint64_t count[WEL2_STAT_COUNT];
    uint64_t console_calls;                 /* T12b: WEL2_CONSOLE_WRITE calls that wrote bytes (length > 0) */
    uint64_t console_bytes;                 /* ... and the bytes they wrote */
};
#define WEL2_STATS_V1_SIZE    (8u + 8u * WEL2_STAT_COUNT)

_Static_assert(offsetof(struct wel2_stats, console_calls) == WEL2_STATS_V1_SIZE, "the v1 stats layout must not move");

#endif
