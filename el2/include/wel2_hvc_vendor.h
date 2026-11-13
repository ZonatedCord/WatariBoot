/* SPDX-License-Identifier: MIT */
/* Watari EL2: layer side of the vendor hypercalls (wel2_hvc.h). Pure logic, no hardware: the guest memory accessors
 * and the sinks are callbacks, so the host tests drive it with fakes and the layer wires it to its own console,
 * framebuffer and counters (docs/design/watari-el2-m1boot/T7-wiring.md). */
#ifndef WEL2_HVC_VENDOR_H
#define WEL2_HVC_VENDOR_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_hvc.h"

/* Counters the dispatcher keeps itself (WEL2_STATS reports them). The layer owns the storage; the big lock serialises. */
struct wel2_hvc_counters {
    uint64_t console_calls, console_bytes;
};

struct wel2_hvc_ops {
    void *ctx;
    /* [pa, pa + len) lies entirely in guest RAM (the Normal stage-2 blocks, not the layer's carve-out, no MMIO hole).
     * Must be overflow safe. Required. */
    bool (*range_ok)(void *ctx, uint64_t pa, uint64_t len);
    /* Copy len bytes out of / into guest memory; only called after range_ok() said yes. The caller of the layer is
     * a guest with its caches off: the implementation cleans+invalidates the range first when it reads. Required
     * by LOG (read) and STATS (write). */
    void (*read_guest)(void *ctx, uint64_t pa, void *dst, uint64_t len);
    void (*write_guest)(void *ctx, uint64_t pa, const void *src, uint64_t len);
    /* One sanitised line (no NUL inside, no CR/LF, printable ASCII only, len <= WEL2_LOG_MAX), to every sink:
     * UART, framebuffer text, log ring. The sink adds its own prefix and newline. Required. */
    void (*log_line)(void *ctx, const char *line, unsigned len);
    /* A check changed state (n = WEL2_CHK_*, state = WEL2_CHKSTATE_*): paint square WEL2_CHK_SQUARE_BASE + n. The
     * hvc_vendor code has already called log_line() with a summary line. May be NULL. */
    void (*checkpoint)(void *ctx, unsigned n, unsigned state);
    /* WEL2_CONSOLE_WRITE: the raw bytes, 1..WEL2_CONSOLE_MAX of them, to every console sink: the UART gets all of them
     * unchanged, the framebuffer text only the printable ones (sinks decide). Required by CONSOLE_WRITE (else
     * NOT_SUPPORTED); read_guest and range_ok too. */
    void (*console_write)(void *ctx, const uint8_t *bytes, unsigned len);
    /* The dispatcher's own counters (console_calls / console_bytes); may be NULL (then WEL2_STATS reports 0 for them). */
    struct wel2_hvc_counters *counters;
    /* Fill the layer's counters. May be NULL: WEL2_STATS then answers NOT_SUPPORTED. */
    void (*stats_fill)(void *ctx, struct wel2_stats *out);
};

/* True when the function id belongs to the vendor hypervisor range handled here (including ids that are in the
 * range but not implemented: those answer NOT_SUPPORTED). Only the low 32 bits of x0 count (SMCCC). */
bool wel2_hvc_is_vendor(uint64_t x0);

/* Handle one vendor call. x[0..3] are the guest's x0-x3 on entry; on return x[0] = result, x[1..3] = 0.
 * Returns false (x untouched) when x0 is not in the vendor range, so the caller falls through to PSCI etc. */
bool wel2_hvc_vendor(const struct wel2_hvc_ops *ops, uint64_t x[4]);

/* Text of a check state for the log line ("running", "PASS", "FAIL"). Never NULL. */
const char *wel2_hvc_state_name(unsigned state);

#endif
