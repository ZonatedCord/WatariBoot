/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - the virtual PMUv3 cycle counter backed by the M1's own cycle counter ("PMC0", wel2.vpmu=...,src=pmc0).
 * Design, data flow and every hypothesis: docs/design/watari-el2/pmc0-vpmu.md. Pure logic (no mrs/msr): the hardware is
 * reached through struct apple_sysreg_io, so tests/test_vpmu_pmc0.c drives it with the fake register file.
 *
 * Rules the code keeps (and the tests check):
 *   - the hardware PMC0 is only ever READ; it is never stopped, zeroed or written (PMCCNTR writes and PMCR.C move a per-core software offset);
 *   - PMCR0 / PMCR1 are changed by read-modify-write that only sets our own enable bits, so the IMODE / IACT bits the FIQ module
 *     (vgic/apple_fiq.c) relies on are never lost; the PMU interrupt mode is not touched (left as found, normally off).
 * Encodings are [hypothesis] until the hardware run (the init measures them: read-back of the enables + a liveness sample).
 */
#ifndef WEL2_VPMU_PMC0_H
#define WEL2_VPMU_PMC0_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_apple_sysreg.h"
#include "wel2_sysreg_catch.h"

enum wel2_pmc0_state {
	WEL2_PMC0_OFF = 0,       /* src=timer (or QEMU): this core never uses PMC0 */
	WEL2_PMC0_PENDING,       /* enabled, but PMC0 did not move across the EL2 delay (EL2 may not be counted): decided at the first guest read */
	WEL2_PMC0_OK,            /* PMC0 advances: it feeds PMCCNTR_EL0 on this core */
	WEL2_PMC0_FAILED,        /* PMC0 never moved / the enable did not stick: this core falls back to the timer source */
};

/* What the per-core hardware init saw (for the log and the panel). */
struct wel2_pmc0_init {
	uint64_t pmcr0_before, pmcr0_after, pmcr1_before, pmcr1_after;
	uint64_t s0, s1;         /* PMC0 before / after the delay */
	uint64_t ticks;          /* CNTPCT ticks the delay took (from the wait callback) */
	enum wel2_pmc0_state state;
};

/* Enable counter 0 for EL0 and EL1 and run the liveness check on this core. `wait` spins about 1 ms of CNTPCT and returns the ticks it
 * took. PMCR0 / PMCR1 are read, ORed with our bits and written only when something changes (so a second call writes nothing). */
void wel2_pmc0_hw_init(const struct apple_sysreg_io *io, uint64_t (*wait)(void *), void *wctx, struct wel2_pmc0_init *r);
/* Cycles per millisecond from a PMC0 delta over `ticks` CNTPCT ticks at `frq` Hz (0 when it cannot be computed). */
uint64_t wel2_pmc0_per_ms(uint64_t delta, uint64_t ticks, uint64_t frq);

/* Per-core software state. Zero-initialise. */
struct wel2_vpmu_core {
	uint8_t state;           /* enum wel2_pmc0_state */
	uint8_t running;         /* this core's view of the gate (PMCR.E && PMCNTENSET.C, or `run`) */
	uint64_t off;            /* PMCCNTR = PMC0 + off while running (modulo 2^64) */
	uint64_t last;           /* the last value this core returned or was set to (the clamp) */
	uint64_t frozen;         /* PMCCNTR while not running */
	uint64_t sample;         /* PMC0 at init (resolves PENDING) */
};
/* State shared by all cores (under the big lock). */
struct wel2_vpmu_sh {
	uint32_t flags;          /* WEL2_VPMU_* */
	bool have_boot;
	uint64_t pub0;           /* the boot core's latest published guest-visible value (starts the other cores) */
	uint64_t hiwater;        /* highest value any core returned (WEL2_VPMU_HIWATER) */
};

/* Start a core. `boot` = the first core. sync (the default): a non-boot core's counter starts at the boot core's last published value;
 * otherwise every core starts at its own PMC0. The counter starts frozen (PMCR.E is 0 at reset); the first enable resumes from there. */
void wel2_vpmu_core_init(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, bool boot, uint64_t pmc0_now, enum wel2_pmc0_state st);
/* PENDING -> OK (PMC0 moved since init) or FAILED (still the same). Returns the state after. Other states are returned unchanged. */
enum wel2_pmc0_state wel2_vpmu_resolve(struct wel2_vpmu_core *c, uint64_t pmc0_now);
/* The gate the global control state `g` implies: PMCR.E && PMCNTENSET.C, or always with WEL2_VPMU_RUN. */
bool wel2_vpmu_running(const struct wel2_vpmu_sh *sh, const struct wel2_catch_cpu *g);
/* PMCCNTR_EL0 as the guest sees it on this core now. */
uint64_t wel2_vpmu_read(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, const struct wel2_catch_cpu *g, bool boot, uint64_t pmc0_now);
/* A guest write of a register of the virtual PMU on this core (the caller resolved the state to OK). Updates the global control state
 * `g` through wel2_catch_write and applies what it means for the counter: PMCCNTR write = set, PMCR.C = set(0), PMCR.E / PMCNTEN
 * changes = freeze / resume without a jump. `cnt` / `frq` are only for the timer-side bookkeeping of g. */
void wel2_vpmu_write(struct wel2_vpmu_sh *sh, struct wel2_vpmu_core *c, struct wel2_catch_cpu *g, const struct wel2_sysreg_enc *e,
		     uint64_t wval, uint64_t pmc0_now, uint64_t cnt, uint64_t frq);
/* True when e is PMCCNTR_EL0 (the one register that carries the counter value). */
bool wel2_vpmu_is_ccntr(const struct wel2_sysreg_enc *e);

/* The panel prefix: "SRC=PMC0 D=<cycles per ms>", "SRC=PMC0 D=?" (no measurement), "SRC=TIMER" or "SRC=TIMER(fallback)". Returns the length. */
unsigned wel2_vpmu_src_format(char *buf, unsigned cap, enum wel2_pmc0_state st, bool wanted_pmc0, uint64_t per_ms);

#endif
