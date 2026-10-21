/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - pirq-guard: storm limiter around pirq arrivals.
 *
 * Design: docs/design/watari-el2-devirq/ARCHITECTURE.md 3.3 and 4.2 ("Source not allow-listed,
 * or more than N events/s: pirq-guard: log once, keep masked, count"), TEST_STRATEGY 2
 * (test_pirq_guard). A decorator: it wraps a backend's struct pirq_ops (so the logic is
 * written and tested once for the QEMU GIC, the AIC and the fake) and fronts pirq_arrive().
 *
 * Policy (fixed window per physical source; time = the caller's generic timer count, this
 * module reads no clock):
 *  - more than max_events arrivals of one source inside `window` ticks: the source is
 *    masked at the backend, the event is NOT injected, the trip is counted and logged once
 *    per source;
 *  - while tripped the guard swallows the unmasks the pirq policy issues (end of interrupt,
 *    guest enable), so the source cannot be re-armed behind its back;
 *  - it comes back (a) after `cooldown` ticks, at the next pirq_guard_tick(), unless the
 *    guest disabled it in the meantime, or (b) when the guest re-enables it (that forgives
 *    the trip: a driver that disabled and enabled the line is handling it);
 *  - a source the backend does not expose (default deny) is counted and logged once; the
 *    pirq policy has already masked it;
 *  - never applied to the timer PPI, SGIs or any other INTID below 32: those are not
 *    physical SPI sources, and one that did arrive here is passed straight through.
 * Pure state plus calls through the ops table, host-testable. Big lock held by the caller.
 */
#ifndef WEL2_PIRQ_GUARD_H
#define WEL2_PIRQ_GUARD_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_pirq.h"

#define PIRQ_GUARD_NSRC 1024u          /* physical sources tracked (hwirq / SPI < 1024) */

enum pirq_guard_reason {
	PIRQ_GUARD_STORM = 1,          /* rate budget exceeded */
	PIRQ_GUARD_DENIED = 2,         /* source not exposed to the guest */
};

struct pirq_guard_cfg {
	uint32_t max_events;           /* events allowed per window (more than this trips) */
	uint64_t window;               /* window length, generic timer ticks */
	uint64_t cooldown;             /* ticks a tripped source stays masked */
	/* Called once per source and reason; may be NULL. */
	void (*log)(void *ctx, enum pirq_guard_reason why, uint32_t phys);
	void *log_ctx;
};

struct pirq_guard_stats {
	uint32_t trips;                /* sources masked for exceeding the budget */
	uint32_t suppressed;           /* events dropped because the source was tripped or tripping */
	uint32_t denied_seen;          /* events of sources that are not exposed */
	uint32_t reenabled;            /* sources that came back (cool-down or guest re-enable) */
	uint32_t unmask_swallowed;     /* pirq unmasks held back while tripped */
	uint32_t exempt;               /* INTIDs below 32 passed through untouched */
};

struct pirq_guard {
	struct pirq_guard_cfg cfg;
	const struct pirq_ops *inner;
	struct pirq_ops ops;           /* what pirq_init() gets: inner with the guard in the way */
	struct pirq_guard_stats st;
	uint32_t tripped[PIRQ_GUARD_NSRC / 32];
	uint32_t logged_storm[PIRQ_GUARD_NSRC / 32];
	uint32_t logged_deny[PIRQ_GUARD_NSRC / 32];
	uint32_t guest_off[PIRQ_GUARD_NSRC / 32];   /* guest disabled it while tripped */
	uint32_t win_count[PIRQ_GUARD_NSRC];
	uint32_t seen[PIRQ_GUARD_NSRC];             /* all events per source, denied included */
	uint64_t win_start[PIRQ_GUARD_NSRC];
	uint64_t tripped_at[PIRQ_GUARD_NSRC];
};

void pirq_guard_init(struct pirq_guard *g, const struct pirq_ops *inner,
		     const struct pirq_guard_cfg *cfg);
/* The ops table to give pirq_init(); valid as long as g is. */
const struct pirq_ops *pirq_guard_ops(struct pirq_guard *g);

/* pirq_arrive() with the budget applied; now = generic timer count. Same return value:
 * the vINTID injected, or INTR_INVALID (denied, tripped). */
uint32_t pirq_guard_arrive(struct pirq_guard *g, struct pirq *p, unsigned vcpu,
			   uint32_t phys, uint64_t now);
/* vGIC effect hook: use instead of pirq_effect (a guest re-enable forgives a trip). */
void pirq_guard_effect(struct pirq_guard *g, struct pirq *p, enum vgic_effect eff,
		       uint32_t intid, int arg);
/* Periodic: bring back sources whose cool-down has elapsed. Returns how many. */
unsigned pirq_guard_tick(struct pirq_guard *g, uint64_t now);

bool pirq_guard_is_tripped(const struct pirq_guard *g, uint32_t phys);
uint32_t pirq_guard_seen(const struct pirq_guard *g, uint32_t phys);

#endif
