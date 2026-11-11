/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - Apple FIQ module (PLAN S10 / T6e, DESIGN 5.1 and 5.2).
 *
 * On the M1 the guest timers, fast IPIs, PMU and the vGIC maintenance line all arrive as FIQ, and no register
 * names the cause, so the handler looks at every source. All hardware access goes through struct apple_sysreg_io
 * (wel2_apple_sysreg.h); the module is plain C and runs on the host in tests/test_apple_fiq.c.
 *
 * Guest-timer protocol ("gate + level", DESIGN 5.2 / ARCHITECTURE 6.2):
 *   1. FIQ with the V (P) gate open and the guest timer control ENABLE & ISTATUS & !IMASK:
 *      close that gate bit, remember "masked", raise the vGIC line PPI 27 (PPI 30) on this vCPU.
 *      The guest's CNTV_CTL / CNTP_CTL is never modified.
 *   2. apple_timer_resync() (call it from cpuif_flush(), i.e. on every exit and every trapped guest EOI) makes
 *      the line follow the timer control; when the line is low and the gate is masked, the gate is reopened.
 *      So "unmask" happens at the first flush after the guest silenced or re-armed its timer, never earlier,
 *      and never while the timer still fires (which would be an FIQ storm).
 */
#ifndef WEL2_APPLE_FIQ_H
#define WEL2_APPLE_FIQ_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_apple_sysreg.h"
#include "wel2_vgic.h"

#define APPLE_PPI_VTIMER 27u   /* EL1 virtual timer  [verified: R:docs/design/watari-el2/INTERFACES.md 4.1] */
#define APPLE_PPI_PTIMER 30u   /* EL1 physical timer */

/* Causes found by one apple_fiq_handle() call (bit mask). */
#define APPLE_FIQ_VTIMER   (1u << 0)
#define APPLE_FIQ_PTIMER   (1u << 1)
#define APPLE_FIQ_IPI      (1u << 2)
#define APPLE_FIQ_MAINT    (1u << 3)
#define APPLE_FIQ_PMU      (1u << 4)
#define APPLE_FIQ_UPMU     (1u << 5)
#define APPLE_FIQ_HVTIMER  (1u << 6)
#define APPLE_FIQ_HPTIMER  (1u << 7)
#define APPLE_FIQ_STORM    (1u << 31)   /* spurious budget exhausted: everything is masked, caller must report */

#define APPLE_FIQ_SPURIOUS_BUDGET 16u

struct apple_fiq_stats {
	unsigned fiq, vtimer, ptimer, ipi, maint, maint_quenched, pmu, upmu, hvtimer, hptimer;
	unsigned spurious, spurious_max_run, storms;
};

struct apple_fiq_cpu {
	const struct apple_sysreg_io *io;
	struct vgic *vgic;
	unsigned vcpu;                       /* index of this core's vCPU in the vGIC model */
	void (*on_ipi)(void *ctx);           /* the layer's IPI work (kick); the flush that follows delivers */
	void *ctx;
	unsigned spurious_budget;            /* consecutive spurious FIQs tolerated; 0 = APPLE_FIQ_SPURIOUS_BUDGET */
	/* state */
	bool vmasked, pmasked;               /* gate closed because the line is high */
	unsigned spurious_run;
	struct apple_fiq_stats st;
};

/* IPI_RR_GLOBAL_EL1 value for a target MPIDR: core = Aff0 in [7:0], cluster = Aff1 in [23:16], type 0 (immediate)
 * [verified: Linux v6.12 irq-apple-aic.c:168-176, 765-772 (cluster = MPIDR affinity level 1)]. */
uint64_t apple_ipi_value(uint64_t target_mpidr);
/* One write to IPI_RR_GLOBAL_EL1 (the local form is not used; review L-4). */
void apple_ipi_send(struct apple_fiq_cpu *c, uint64_t target_mpidr);

/* Per-core init, before the eret (boot core, every secondary, every re-entry from the park loop): quiesce all
 * sources, write 0 to CNTV_CTL_EL02 and CNTP_CTL_EL02, and only then open the V|P gates (review M-3). */
void apple_fiq_init_cpu(struct apple_fiq_cpu *c);
/* Close both gates (CPU_OFF park, fatal path). */
void apple_fiq_close_gates(struct apple_fiq_cpu *c);

/* FIQ taken at EL2 from the guest (vector index 10). Returns the causes found; APPLE_FIQ_STORM means the
 * module has masked every source and the caller must report with a register dump. Caller runs cpuif_flush()
 * afterwards (always), then apple_fiq_after_flush(). */
unsigned apple_fiq_handle(struct apple_fiq_cpu *c);
/* After the flush: if the maintenance condition is still asserted, clear the maintenance enables (never loops).
 * Returns true if it had to. */
bool apple_fiq_after_flush(struct apple_fiq_cpu *c);

/* Make the vGIC timer lines follow the guest timer controls and reopen a closed gate whose line went low.
 * Replaces vtimer_line() + gic_phys_ppi_enable(PPI_VTIMER, true) in cpuif_flush() on the M1. */
void apple_timer_resync(struct apple_fiq_cpu *c);
/* The two primitives of the timer_src_ops contract (INTERFACES 8.3): close / reopen one gate (ppi = APPLE_PPI_*). */
void apple_timer_mask(struct apple_fiq_cpu *c, unsigned ppi);
void apple_timer_unmask(struct apple_fiq_cpu *c, unsigned ppi);

#endif
