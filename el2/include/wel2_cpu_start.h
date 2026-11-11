/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - PSCI back end for the Apple M1 (no EL3, no PSCI firmware): secondary-core start through the
 * spin table, CPU_OFF as a park inside the layer, SYSTEM_RESET through the watchdog, SYSTEM_OFF as a halt.
 * Pure logic behind an ops table (memory store, cache clean, barriers, events, timer reset, MMIO), so the
 * order of the operations is tested on the host (tests/test_cpu_start.c).
 *
 * THIS FILE AND vgic/cpu_start.c CONTAIN NO `smc`: the M1 has no EL3 [verified: re/m3a-ehw0-measurements.md,
 * ID_AA64PFR0_EL1.EL3 = 0], so the instruction would be UNDEFINED [hypothesis: Arm ARM, SMC without EL3].
 * The ops table is the only way to touch hardware; the real ops (plat_m1.c) use plain stores, DC CVAC, DSB,
 * SEV, WFE and MSR to CNT*_CTL_EL02 only.
 *
 * Facts used (no code reused):
 *  - spin table: the core waits (wfe) at m1n1's `struct spin_table.target`; whoever stores a non-zero address
 *    there releases it and the core branches to it with MMU and caches off [verified: m1n1 v1.6.1
 *    src/smp.c:30-36, 80-106; DESIGN section 3.2]. cpu-release-addr in the FDT points at that field
 *    [verified: m1n1 v1.6.1 src/kboot.c:541-610].
 *  - writer has caches on, reader off: store, clean the line to PoC, DSB, then SEV [verified: Linux v6.12
 *    arch/arm64/kernel/smp_spin_table.c:85-100 (write, clean+invalidate, sev); DESIGN section 3.3].
 *  - watchdog reset: write ALARM (+0x14) = 0x100000, COUNT (+0x10) = 0, CTL (+0x1c) = 4 [verified: m1n1 v1.6.1
 *    src/wdt.c:51-59 (order alarm, count, ctl, read through the GitHub API); Linux drivers/watchdog/apple_wdt.c
 *    uses the same register block]. Base = reg of the FDT node "apple,wdt" [verified: DESIGN section 6].
 */
#ifndef WEL2_CPU_START_H
#define WEL2_CPU_START_H

#include <stdbool.h>
#include <stdint.h>

#define WEL2_CS_MAX 8u

/* Return codes: the PSCI values of Arm DEN0022F.b section 5.1 (the caller passes them through). */
#define WEL2_CS_SUCCESS             0
#define WEL2_CS_INVALID_PARAMETERS (-2)
#define WEL2_CS_DENIED             (-3)
#define WEL2_CS_ALREADY_ON         (-4)
#define WEL2_CS_ON_PENDING         (-5)
#define WEL2_CS_INTERNAL_FAILURE   (-6)

/* AFFINITY_INFO values (5.7) */
#define WEL2_CS_AFF_ON       0
#define WEL2_CS_AFF_OFF      1
#define WEL2_CS_AFF_PENDING  2

#define WEL2_WDT_COUNT 0x10u
#define WEL2_WDT_ALARM 0x14u
#define WEL2_WDT_CTL   0x1cu
#define WEL2_WDT_ALARM_VALUE 0x100000u   /* about 44 ms at 24 MHz [verified: DESIGN section 6] */
#define WEL2_WDT_CTL_RESET   4u

struct wel2_cs_ops {
	/* 64-bit store to a physical address, as seen by a core with caches on (the spin-table word). */
	void (*write64)(void *ctx, uint64_t pa, uint64_t v);
	/* DC CVAC on the line holding pa. */
	void (*dc_cvac)(void *ctx, uint64_t pa);
	void (*dsb)(void *ctx);
	void (*sev)(void *ctx);
	void (*wfe)(void *ctx);
	/* Per-core wake flag of the parked core (ordinary memory, MMU on: no cache maintenance). */
	void (*wake_store)(void *ctx, unsigned idx, uint32_t v);
	uint32_t (*wake_load)(void *ctx, unsigned idx);
	/* CNTV_CTL_EL02 = 0 and CNTP_CTL_EL02 = 0 on the calling core (DESIGN section 5.2, M-3). */
	void (*timers_reset)(void *ctx);
	/* 32-bit MMIO store (watchdog). */
	void (*mmio_write32)(void *ctx, uint64_t pa, uint32_t v);
	/* Diagnostics: one line / checkpoint per event (WEL2_CS_EV_*), idx = core index, arg = event specific. */
	void (*log)(void *ctx, int event, unsigned idx, uint64_t arg);
	void *ctx;
};

/* Events passed to ops->log */
#define WEL2_CS_EV_ON_ACCEPT 1   /* CPU_ON accepted for core idx; arg = 0 spin-table start, 1 wake of a parked core */
#define WEL2_CS_EV_ARRIVE    2   /* core idx reached wel2_cs_enter() and is about to eret; arg = entry */

enum wel2_cs_state { WEL2_CS_OFF = 0, WEL2_CS_PENDING, WEL2_CS_ON };

struct wel2_cs_core {
	uint64_t mpidr;          /* affinity fields only, as in PSCI CPU_ON */
	uint64_t release_addr;   /* cpu-release-addr; 0 = none */
	enum wel2_cs_state state;
	bool released;           /* the spin table was used: the core is inside the layer for good */
	bool parked;             /* CPU_OFF done: the core waits (or is about to) in wel2_cs_park() */
	uint64_t entry, ctx;     /* guest entry point and context id for the next start */
};

struct wel2_cs {
	const struct wel2_cs_ops *ops;
	uint64_t secondary_entry;   /* run-time (relocated) physical address of secondary_entry */
	unsigned n;
	struct wel2_cs_core core[WEL2_CS_MAX];
};

/* One entry of the core list (from the FDT /cpus nodes, node order = core index). */
struct wel2_cs_cpu {
	uint64_t mpidr;
	uint64_t release_addr;
};

/*
 * LOCKING (every function below): called with the layer's big lock held, except wel2_cs_park() (the core has
 * released the lock; it touches only its own wake flag through the ops), wel2_cs_wait_forever() and
 * wel2_cs_system_reset() (see their comments). wel2_cs_find/affinity only read state: the lock is held too, so
 * they never see a half-updated core.
 *
 * boot_idx = the core that is already running the layer (state ON). Returns 0, or -1 for bad arguments
 * (n = 0 or > WEL2_CS_MAX, missing ops, secondary_entry = 0, boot_idx out of range, duplicate MPIDR). */
int wel2_cs_init(struct wel2_cs *cs, const struct wel2_cs_ops *ops, uint64_t secondary_entry,
		 const struct wel2_cs_cpu *cpus, unsigned n, unsigned boot_idx);

/* Index of the core with this MPIDR (Aff3:Aff0 only), or ~0u. */
unsigned wel2_cs_find(const struct wel2_cs *cs, uint64_t mpidr);

/*
 * CPU_ON for a core: the caller (psci.c) has already checked the guest entry address. Must run under the
 * layer's big lock like the rest of psci.c. First start: store the secondary entry at the release address,
 * clean the line, DSB, SEV. Later start of a core that CPU_OFF parked: entry/context, DSB, wake flag = 1,
 * DSB, SEV (the spin table is not touched again). Errors: INVALID_PARAMETERS (unknown MPIDR),
 * ALREADY_ON, ON_PENDING, INTERNAL_FAILURE (no release address, or not 8-byte aligned).
 */
int wel2_cs_cpu_on(struct wel2_cs *cs, uint64_t target_mpidr, uint64_t entry, uint64_t ctx);

/* Would CPU_OFF succeed for `me`? SUCCESS, DENIED (no other core is ON: only cores in state ON count, a PENDING
 * core has not arrived yet and cannot wake anybody; nothing would ever wake the last core, F-14) or
 * INVALID_PARAMETERS (me out of range or not ON). No state change. */
int wel2_cs_can_off(const struct wel2_cs *cs, unsigned me);

/*
 * CPU_OFF bookkeeping for the calling core `me`. CONTRACT: psci.c calls this FIRST, under the big lock, and
 * tears the core down (vGIC reset, pirq_vcpu_off, cpuif_cpu_down, gic_phys_cpu_down) ONLY when it returns
 * SUCCESS; on DENIED / INVALID_PARAMETERS nothing was changed and nothing must be torn down. After SUCCESS the
 * core is OFF and parked: the caller finishes the teardown, releases the big lock, and calls wel2_cs_park().
 * A CPU_ON arriving between the unlock and the park is safe (see wel2_cs_park).
 */
int wel2_cs_cpu_off(struct wel2_cs *cs, unsigned me);

/*
 * The park loop of core `me` (big lock NOT held). Per-core timers are reset FIRST (CNTV_CTL_EL02 =
 * CNTP_CTL_EL02 = 0, via ops->timers_reset) so that a pending timer FIQ cannot turn wfe into a busy spin, then
 * `while (wake flag == 0) wfe`, then the flag is cleared and the per-core timers
 * are reset again before anything else (the guest context of the previous run may have left a timer on, M-3). Returns true once woken.
 * max_wfe = 0 means wait forever (the real call); a bound exists for tests and returns false when reached.
 * The woken core continues with wel2_cs_enter().
 * Lost wake-ups: the flag is read before every wfe, SEV sets the event register of a core that is not yet in
 * wfe, and the flag is cleared only after it was seen, so a CPU_ON that races with the core entering the loop
 * is never lost [hypothesis: Arm ARM event-register semantics; not tested on hardware].
 */
bool wel2_cs_park(struct wel2_cs *cs, unsigned me, unsigned max_wfe);

/* The core is about to eret to the guest: take the entry point and context, state ON, ops->log(ARRIVE).
 * Also used for the first start (called from el2_secondary_main), big lock held. A false return (core not
 * PENDING: stray start) means the core must NOT eret: it paints a checkpoint, logs, and parks/fatal()s. */
bool wel2_cs_enter(struct wel2_cs *cs, unsigned me, uint64_t *entry, uint64_t *ctx);

/* AFFINITY_INFO: WEL2_CS_AFF_*; INVALID_PARAMETERS for an unknown MPIDR. */
int wel2_cs_affinity(const struct wel2_cs *cs, uint64_t mpidr);

/* SYSTEM_RESET (big lock held on entry; recommended: keep it, other cores are going away anyway, but the
 * caller masks DAIF first and does not return): three watchdog stores (alarm, count, ctl), DSB; then the caller waits (wel2_cs_wait_forever).
 * Returns -1 if wdt_base is 0 (no watchdog node: the caller must report and halt). */
int wel2_cs_system_reset(const struct wel2_cs_ops *ops, uint64_t wdt_base);

/* Halt: resets the timers first (a pending timer FIQ would make wfe spin), then a wfe loop; max = 0 forever
 * (reset wait and SYSTEM_OFF halt), a bound for tests. For SYSTEM_OFF release the big lock BEFORE calling, so
 * other cores that trap can still print their diagnostics; for SYSTEM_RESET either way. */
void wel2_cs_wait_forever(const struct wel2_cs_ops *ops, unsigned max);

#endif
