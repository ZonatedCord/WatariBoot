/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - virtual CPU interface model (pure state, host-testable).
 *
 * Mirrors what the GICv3 virtualization extension keeps per vCPU: the list
 * registers ICH_LR<n>_EL2, ICH_VMCR_EL2 and the active-priority registers
 * ICH_AP0R/AP1R<n>_EL2. The boot layer loads the hardware registers into this
 * struct, calls the functions below and writes back what changed (dirty
 * masks); the host tests drive the very same code without hardware.
 *
 * Two jobs (written from GIC spec IHI 0069H.b, 4.6 virtual CPU interface and
 * 12.3/12.4 register descriptions; no third-party code):
 *  1. List-register policy (#54): fill free LRs in priority order, take back
 *     a pending-only LR when a higher priority interrupt waits (eviction),
 *     harvest LRs the guest has deactivated, tell the caller when interrupts
 *     overflow the LRs so that the underflow maintenance interrupt is armed.
 *  2. TRAP_EMULATE (#52): the guest's ICC_* accesses (IAR1/EOIR1/DIR/HPPIR1/
 *     RPR/PMR/BPR1/CTLR/IGRPEN1, and the Group 0 ones) executed against the
 *     LR / VMCR / AP state, i.e. what the ICV_* hardware would have done.
 */
#ifndef WEL2_VCIF_H
#define WEL2_VCIF_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_vgic.h"

#define VCIF_MAX_LRS 16

/* ICH_LR<n>_EL2 (GIC spec 12.4.6): State [63:62], HW 61, Group 60,
 * Priority [55:48], bit 41 = EOI maintenance request (HW=0), vINTID [31:0]. */
#define VCIF_LR_STATE_SHIFT 62
#define VCIF_LR_STATE_MASK  (3ULL << VCIF_LR_STATE_SHIFT)
#define VCIF_LR_PENDING     (1ULL << VCIF_LR_STATE_SHIFT)
#define VCIF_LR_ACTIVE      (2ULL << VCIF_LR_STATE_SHIFT)
#define VCIF_LR_PENDACT     (3ULL << VCIF_LR_STATE_SHIFT)
#define VCIF_LR_GROUP1      (1ULL << 60)
#define VCIF_LR_EOI         (1ULL << 41)

/* ICH_VMCR_EL2 (12.4.8). */
#define VCIF_VMCR_VENG0     (1u << 0)
#define VCIF_VMCR_VENG1     (1u << 1)
#define VCIF_VMCR_VCBPR     (1u << 4)
#define VCIF_VMCR_VEOIM     (1u << 9)
#define VCIF_VMCR_VBPR1_SH  18
#define VCIF_VMCR_VBPR0_SH  21
#define VCIF_VMCR_VPMR_SH   24

struct wel2_vcif {
	unsigned nlr;                 /* 1..VCIF_MAX_LRS */
	unsigned pribits;             /* ICH_VTR_EL2.PRIbits + 1 (5..8) */
	unsigned prebits;             /* ICH_VTR_EL2.PREbits + 1 (5..7) */
	uint64_t lr[VCIF_MAX_LRS];
	int32_t  owner[VCIF_MAX_LRS]; /* vINTID the LR was filled for, -1 = free */
	uint32_t vmcr;
	uint32_t ap0r[4], ap1r[4];
	uint32_t lr_dirty;            /* LRs to write back */
	bool     vmcr_dirty, ap_dirty;
	uint32_t eoicount;            /* EOIR/DIR that matched no active LR */
	/* Test knob: leave the EOI-maintenance bit out of the LRs of SGIs so that only the
	 * underflow interrupt (UIE) refills them (exercises #54 on its own). Not for level
	 * interrupts: their physical line stays masked until the EOI is noticed. */
	bool     no_eoi_maint;
	/* statistics */
	unsigned st_inject, st_evict, st_eoi;
};

void vcif_init(struct wel2_vcif *c, unsigned nlr, unsigned pribits, unsigned prebits);
/* VMCR reset: everything off, binary points at their minimum, PMR 0. */
void vcif_reset_vmcr(struct wel2_vcif *c);
unsigned vcif_valid_lrs(const struct wel2_vcif *c);        /* LRs with State != 0 */

/* ---- list-register policy ---------------------------------------------- */
/* LRs whose interrupt the guest has deactivated (State 0, owner set): the
 * model gets vgic_eoi(), the LR is freed, its INTID is stored in freed[]
 * (up to max). Returns how many are stored. */
unsigned vcif_harvest(struct wel2_vcif *c, struct vgic *v, unsigned vcpu,
		      uint32_t *freed, unsigned max);
/* Move deliverable interrupts of the model into LRs, highest priority first.
 * With no free LR a pending-only LR of strictly lower priority is evicted
 * (given back with vgic_requeue()). */
void vcif_fill(struct wel2_vcif *c, struct vgic *v, unsigned vcpu);
/* True if, after vcif_fill(), the model still holds a deliverable interrupt:
 * the caller arms ICH_HCR_EL2.UIE (only meaningful with >= 2 valid LRs). */
bool vcif_overflow(const struct wel2_vcif *c, const struct vgic *v, unsigned vcpu);

/* ---- ICC_* emulation (TRAP_EMULATE) --------------------------------------- */
uint32_t vcif_iar1(struct wel2_vcif *c);
void     vcif_eoir1(struct wel2_vcif *c, uint32_t intid);
void     vcif_dir(struct wel2_vcif *c, uint32_t intid);
uint32_t vcif_hppir1(const struct wel2_vcif *c);
uint32_t vcif_rpr(const struct wel2_vcif *c);
uint32_t vcif_pmr_get(const struct wel2_vcif *c);
void     vcif_pmr_set(struct wel2_vcif *c, uint32_t v);
uint32_t vcif_bpr1_get(const struct wel2_vcif *c);
void     vcif_bpr1_set(struct wel2_vcif *c, uint32_t v);
uint32_t vcif_bpr0_get(const struct wel2_vcif *c);
void     vcif_bpr0_set(struct wel2_vcif *c, uint32_t v);
uint32_t vcif_igrpen_get(const struct wel2_vcif *c, unsigned group);
void     vcif_igrpen_set(struct wel2_vcif *c, unsigned group, uint32_t v);
/* ICC_CTLR_EL1 as the guest sees it (SEIS hidden, IDbits = 16). */
uint32_t vcif_ctlr_get(const struct wel2_vcif *c);
void     vcif_ctlr_set(struct wel2_vcif *c, uint32_t v);
uint32_t vcif_ap_get(const struct wel2_vcif *c, unsigned group, unsigned n);
void     vcif_ap_set(struct wel2_vcif *c, unsigned group, unsigned n, uint32_t v);

#endif
