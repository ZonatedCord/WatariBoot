/* SPDX-License-Identifier: MIT */
/*
 * Host-test backend for struct pirq_ops: behaves like the physical interrupt controller
 * the layer sits on, in two flavours.
 *  - AIC-like (auto_masks = true, sources and vINTIDs related by an intr_map, platform
 *    trigger table): a source whose event is read stays masked until the layer unmasks it
 *    (ARCHITECTURE devirq 4.2; [verified: Linux AIC flow, re/f0-qemu-aic-models.md 5] for
 *    the level case, edge latching while masked is [hypothesis] until E-HW0).
 *  - GIC-like (auto_masks = false, identity map, trigger set by the guest's ICFGR): an
 *    acknowledged level SPI stays enabled unless the layer disables it.
 * The test drives the "wires" (fake_line / fake_pulse) and reads events with fake_event().
 */
#ifndef WEL2_FAKE_PIRQ_H
#define WEL2_FAKE_PIRQ_H

#include <string.h>
#include "wel2_pirq.h"

#define FAKE_N 1024u

struct fake {
	struct pirq_ops ops;
	const struct intr_map *map;       /* NULL: identity (GIC-like) */
	bool line[FAKE_N];                /* level input */
	bool latch[FAKE_N];               /* edge latch */
	bool masked[FAKE_N];              /* AIC mask / GIC disabled */
	bool edge[FAKE_N];                /* trigger the controller is programmed with */
	int  target[FAKE_N];              /* physical routing (vCPU), -1 unset */
	unsigned n_mask, n_unmask, n_set_target, n_set_trigger;
	int last_target_phys, last_target;
};

static inline void fk_mask(void *c, uint32_t p) { struct fake *f = c; f->masked[p] = true; f->n_mask++; }
static inline void fk_unmask(void *c, uint32_t p) { struct fake *f = c; f->masked[p] = false; f->n_unmask++; }
static inline void fk_target(void *c, uint32_t p, int v)
{
	struct fake *f = c;

	f->target[p] = v; f->n_set_target++; f->last_target_phys = (int)p; f->last_target = v;
}
static inline void fk_trigger(void *c, uint32_t p, bool e) { struct fake *f = c; f->edge[p] = e; f->n_set_trigger++; }
static inline uint32_t fk_intid_of(void *c, uint32_t p)
{
	struct fake *f = c;

	if (f->map)
		return intr_exposed_vintid(f->map, p);
	return p >= 32 && p < VGIC_NR_IRQS ? p : INTR_INVALID;
}
static inline uint32_t fk_phys_of(void *c, uint32_t v)
{
	struct fake *f = c;

	if (f->map)
		return intr_exposed_hwirq(f->map, v);
	return v >= 32 && v < VGIC_NR_IRQS ? v : INTR_INVALID;
}
static inline int fk_trigger_of(void *c, uint32_t p)
{
	struct fake *f = c;

	return intr_trigger(f->map, p);
}

static inline void fake_init(struct fake *f, const struct intr_map *map)
{
	memset(f, 0, sizeof *f);
	f->map = map;
	for (unsigned i = 0; i < FAKE_N; i++) {
		f->masked[i] = true;
		f->target[i] = -1;
	}
	f->ops.name = map ? "fake-aic" : "fake-gic";
	f->ops.auto_masks = map != NULL;
	f->ops.strict_level = map != NULL;
	f->ops.mask = fk_mask;
	f->ops.unmask = fk_unmask;
	f->ops.set_target = fk_target;
	f->ops.set_trigger = fk_trigger;
	f->ops.intid_of = fk_intid_of;
	f->ops.phys_of = fk_phys_of;
	f->ops.trigger_of = map ? fk_trigger_of : NULL;
	f->ops.ctx = f;
}

static inline void fake_line(struct fake *f, uint32_t p, bool lvl) { f->line[p] = lvl; }
static inline void fake_pulse(struct fake *f, uint32_t p) { f->latch[p] = true; }

/* Next deliverable event: unmasked and pending (level: line high, edge: latched). Reading
 * it consumes an edge latch and, for an AIC-like controller, masks the source. */
static inline bool fake_event(struct fake *f, uint32_t *p)
{
	for (uint32_t i = 0; i < FAKE_N; i++) {
		bool pend = f->edge[i] ? f->latch[i] : f->line[i];

		if (f->masked[i] || !pend)
			continue;
		if (f->edge[i])
			f->latch[i] = false;
		if (f->ops.auto_masks)
			f->masked[i] = true;
		*p = i;
		return true;
	}
	return false;
}

#endif
