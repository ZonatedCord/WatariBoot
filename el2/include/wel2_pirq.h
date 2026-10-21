/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - pirq: physical interrupt arrival -> pirq -> guest vINTID, and the SPI
 * life cycle (mask / unmask / route / trigger) in the other direction.
 *
 * One policy, many backends (ARCHITECTURE docs/design/watari-el2-devirq 3.3, 4.2, 7.4):
 * the QEMU physical GICv3 (today), the Apple AIC (#59, later) and the host-test fake all
 * implement struct pirq_ops; this module owns what must not differ between them:
 *  - physical source -> vINTID translation and default deny (backend intid_of);
 *  - level sources: masked at the source from the event until the guest deactivates,
 *    the line stays high in the vGIC model until then;
 *  - edge sources: a pulse latched pending; a backend whose event read masks the source
 *    (AIC) is unmasked at injection, a backend that does not (GIC) is left alone;
 *  - the guest's ICFGR trigger type, unless the backend's table already fixes it;
 *  - re-routing of SPIs away from a vCPU that goes CPU_OFF, restored on CPU_ON.
 * Pure state plus calls through the ops table: no hardware access here, host-testable.
 * Everything is called with the layer's big lock held.
 */
#ifndef WEL2_PIRQ_H
#define WEL2_PIRQ_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_intr_map.h"
#include "wel2_vgic.h"

struct pirq_ops {
	const char *name;
	/* The event read itself masks the source (AIC: "the delivered IRQ stays masked until
	 * MASK_CLR"; GICv3 does not: the layer has to disable a level SPI explicitly). */
	bool auto_masks;
	/* Level source that is still held by the guest (injected, not deactivated) must not be
	 * unmasked by a guest re-enable (it would re-fire while active). Off on the QEMU GIC:
	 * that is today's behaviour. */
	bool strict_level;
	void (*mask)(void *ctx, uint32_t phys);
	void (*unmask)(void *ctx, uint32_t phys);
	void (*set_target)(void *ctx, uint32_t phys, int vcpu);      /* physical routing to a vCPU's core */
	void (*set_trigger)(void *ctx, uint32_t phys, bool edge);
	/* physical source -> vINTID the guest may see, or INTR_INVALID (default deny) */
	uint32_t (*intid_of)(void *ctx, uint32_t phys);
	/* inverse: vINTID -> physical source, or INTR_INVALID (not exposed) */
	uint32_t (*phys_of)(void *ctx, uint32_t vintid);
	/* trigger fixed by the platform table for this source: INTR_TRIG_*; negative, or the
	 * pointer NULL: the guest's ICFGR decides (QEMU GIC, F-25). */
	int (*trigger_of)(void *ctx, uint32_t phys);
	void *ctx;
};

struct pirq_stats {
	uint32_t arrivals;      /* events turned into a vINTID */
	uint32_t denied;        /* events of sources that are not exposed (masked, dropped) */
	uint32_t retargets;     /* SPIs moved away from / back to a vCPU */
	uint32_t trig_ignored;  /* guest ICFGR writes overridden by the platform table */
};

#define PIRQ_NWORDS (VGIC_NR_IRQS / 32)

struct pirq {
	const struct pirq_ops *ops;
	struct vgic *v;
	unsigned nr_vcpus;
	uint32_t online;                       /* bit i = vCPU i is ON (can take interrupts) */
	uint32_t masked[PIRQ_NWORDS];          /* level SPI masked by the layer, to unmask at deactivate */
	uint32_t edge[PIRQ_NWORDS];            /* edge-triggered (ICFGR or platform table) */
	uint32_t held[PIRQ_NWORDS];            /* level SPI injected, not yet deactivated by the guest */
	int8_t guest_target[VGIC_NR_IRQS];     /* vCPU the guest routed it to (IROUTER), -1 none */
	int8_t phys_target[VGIC_NR_IRQS];      /* vCPU it is physically routed to now, -1 unset */
	struct pirq_stats st;
};

/* Reads trigger_of for every exposed source and programs it. No vCPU is online yet. */
void pirq_init(struct pirq *p, const struct pirq_ops *ops, struct vgic *v, unsigned nr_vcpus);

/* A physical interrupt of source phys arrived on vcpu (already acknowledged at the backend).
 * Returns the vINTID injected into the model, or INTR_INVALID when the source is denied
 * (masked, counted, nothing injected). */
uint32_t pirq_arrive(struct pirq *p, unsigned vcpu, uint32_t phys);

/* The guest has finished with a vINTID (level: deactivate). Two steps because the caller
 * has the model's EOI in between: lower the line, EOI in the vGIC, then unmask the source
 * if the layer masked it (a line that is still high fires again). */
void pirq_line_low(struct pirq *p, unsigned vcpu, uint32_t vintid);
void pirq_finish(struct pirq *p, uint32_t vintid);

/* vGIC effect hook (UNMASK_SPI, MASK_SPI, SET_TARGET, SET_TRIGGER). */
void pirq_effect(struct pirq *p, enum vgic_effect eff, uint32_t intid, int arg);

/* PSCI CPU_OFF / CPU_ON: re-route every SPI the guest sent to this vCPU to an online one /
 * back. Call pirq_vcpu_off before the core's interface is torn down, pirq_vcpu_on once the
 * core can take interrupts. */
void pirq_vcpu_off(struct pirq *p, unsigned vcpu);
void pirq_vcpu_on(struct pirq *p, unsigned vcpu);

bool pirq_is_masked(const struct pirq *p, uint32_t vintid);
bool pirq_is_edge(const struct pirq *p, uint32_t vintid);

#endif
