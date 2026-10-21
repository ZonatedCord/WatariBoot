/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - pirq policy. See include/wel2_pirq.h.
 *
 * Level/edge rules: docs/design/watari-el2-devirq/ARCHITECTURE.md section 4.2 (the table
 * "Level source / Edge source"). The AIC claim "the delivered IRQ stays masked until
 * MASK_CLR" is [verified: Linux AIC flow, re/f0-qemu-aic-models.md section 5]; the edge
 * latch behaviour of the real chip is [hypothesis] until E-HW0. The GIC rules are the
 * architecture's (IHI 0069H.b) and the F-25 finding (edge SPIs of a v2m).
 */
#include "wel2_pirq.h"

#define BIT_GET(a, i)  (((a)[(i) / 32] >> ((i) % 32)) & 1u)
#define BIT_SET(a, i)  ((a)[(i) / 32] |= 1u << ((i) % 32))
#define BIT_CLR(a, i)  ((a)[(i) / 32] &= ~(1u << ((i) % 32)))

static bool spi_ok(uint32_t v)
{
	return v >= 32 && v < VGIC_NR_IRQS;
}

/* Lowest online vCPU, or -1. */
static int first_online(const struct pirq *p)
{
	for (unsigned i = 0; i < p->nr_vcpus; i++)
		if (p->online & (1u << i))
			return (int)i;
	return -1;
}

/* Where an SPI the guest routed to vCPU g is physically routed: g if it can take
 * interrupts, otherwise an online vCPU. Before any vCPU is online, g unchanged. */
static int effective_target(const struct pirq *p, int g)
{
	if (g >= 0 && (p->online & (1u << g)))
		return g;
	if (g < 0)
		return g;
	int f = first_online(p);

	return f >= 0 ? f : g;
}

void pirq_init(struct pirq *p, const struct pirq_ops *ops, struct vgic *v, unsigned nr_vcpus)
{
	p->ops = ops;
	p->v = v;
	p->nr_vcpus = nr_vcpus;
	p->online = 0;
	for (unsigned i = 0; i < PIRQ_NWORDS; i++)
		p->masked[i] = p->edge[i] = p->held[i] = 0;
	for (unsigned i = 0; i < VGIC_NR_IRQS; i++)
		p->guest_target[i] = p->phys_target[i] = -1;
	p->st.arrivals = p->st.denied = p->st.retargets = p->st.trig_ignored = 0;

	if (!ops->trigger_of)
		return;
	for (uint32_t id = 32; id < VGIC_NR_IRQS; id++) {
		uint32_t phys = ops->phys_of(ops->ctx, id);
		int t;

		if (phys == INTR_INVALID)
			continue;
		t = ops->trigger_of(ops->ctx, phys);
		if (t < 0)
			continue;
		if (t)
			BIT_SET(p->edge, id);
		ops->set_trigger(ops->ctx, phys, t != 0);
	}
}

uint32_t pirq_arrive(struct pirq *p, unsigned vcpu, uint32_t phys)
{
	const struct pirq_ops *o = p->ops;
	uint32_t id = o->intid_of(o->ctx, phys);

	if (!spi_ok(id)) {
		/* Default deny: keep the source quiet and tell nobody. */
		o->mask(o->ctx, phys);
		p->st.denied++;
		return INTR_INVALID;
	}
	p->st.arrivals++;
	if (BIT_GET(p->edge, id)) {
		/* A pulse: latch it pending. A backend whose event read masked the source gets it
		 * back at once (unmask at injection); the GIC never masked it. */
		vgic_set_irq_level(p->v, vcpu, id, true);
		vgic_set_irq_level(p->v, vcpu, id, false);
		if (o->auto_masks)
			o->unmask(o->ctx, phys);
	} else {
		/* Level: stay masked from the event until the guest deactivates. */
		if (!o->auto_masks)
			o->mask(o->ctx, phys);
		BIT_SET(p->masked, id);
		BIT_SET(p->held, id);
		vgic_set_irq_level(p->v, vcpu, id, true);
	}
	return id;
}

void pirq_line_low(struct pirq *p, unsigned vcpu, uint32_t vintid)
{
	vgic_set_irq_level(p->v, vcpu, vintid, false);
}

void pirq_finish(struct pirq *p, uint32_t vintid)
{
	uint32_t phys;

	if (!spi_ok(vintid))
		return;
	BIT_CLR(p->held, vintid);
	if (!BIT_GET(p->masked, vintid))
		return;
	BIT_CLR(p->masked, vintid);
	phys = p->ops->phys_of(p->ops->ctx, vintid);
	if (phys != INTR_INVALID)
		p->ops->unmask(p->ops->ctx, phys);      /* a line that is still high fires again */
}

void pirq_effect(struct pirq *p, enum vgic_effect eff, uint32_t intid, int arg)
{
	const struct pirq_ops *o = p->ops;
	uint32_t phys;

	if (!spi_ok(intid))
		return;
	phys = o->phys_of(o->ctx, intid);
	if (phys == INTR_INVALID)
		return;                         /* not exposed: the guest cannot touch the source */

	switch (eff) {
	case VGIC_EFFECT_SET_TARGET:
		p->guest_target[intid] = (int8_t)arg;
		if (arg < 0)
			break;                  /* routes nowhere: the physical route stays where it is */
		{
			int t = effective_target(p, arg);

			p->phys_target[intid] = (int8_t)t;
			o->set_target(o->ctx, phys, t);
		}
		break;
	case VGIC_EFFECT_SET_TRIGGER:
		if (o->trigger_of && o->trigger_of(o->ctx, phys) >= 0) {
			p->st.trig_ignored++;   /* the platform table decides, not the guest */
			break;
		}
		if (arg)
			BIT_SET(p->edge, intid);
		else
			BIT_CLR(p->edge, intid);
		o->set_trigger(o->ctx, phys, arg != 0);
		break;
	case VGIC_EFFECT_UNMASK_SPI:
		BIT_CLR(p->masked, intid);
		if (o->strict_level && BIT_GET(p->held, intid) && !BIT_GET(p->edge, intid)) {
			/* Held by the guest: unmask when it deactivates, not now. */
			BIT_SET(p->masked, intid);
			break;
		}
		o->unmask(o->ctx, phys);
		break;
	case VGIC_EFFECT_MASK_SPI:
		BIT_CLR(p->masked, intid);
		o->mask(o->ctx, phys);
		break;
	}
}

/* Move one SPI to where it should be now; calls the backend only when that changes. */
static void reroute(struct pirq *p, uint32_t id)
{
	uint32_t phys = p->ops->phys_of(p->ops->ctx, id);
	int g = p->guest_target[id], t;

	if (g < 0) {                            /* guest names no vCPU: move only if the old one left */
		t = p->phys_target[id];
		if (t >= 0 && !(p->online & (1u << t)))
			t = first_online(p);
	} else {
		t = effective_target(p, g);
	}
	if (t < 0)
		return;
	if (phys == INTR_INVALID || t == p->phys_target[id])
		return;
	p->phys_target[id] = (int8_t)t;
	p->ops->set_target(p->ops->ctx, phys, t);
	p->st.retargets++;
}

void pirq_vcpu_off(struct pirq *p, unsigned vcpu)
{
	p->online &= ~(1u << vcpu);
	if (!p->online)
		return;                         /* nothing to move to (PSCI denies the last CPU_OFF) */
	for (uint32_t id = 32; id < VGIC_NR_IRQS; id++)
		if (p->phys_target[id] == (int)vcpu || p->guest_target[id] == (int)vcpu)
			reroute(p, id);
}

void pirq_vcpu_on(struct pirq *p, unsigned vcpu)
{
	p->online |= 1u << vcpu;
	for (uint32_t id = 32; id < VGIC_NR_IRQS; id++)
		if (p->guest_target[id] == (int)vcpu)
			reroute(p, id);
}

bool pirq_is_masked(const struct pirq *p, uint32_t vintid)
{
	return spi_ok(vintid) && BIT_GET(p->masked, vintid);
}

bool pirq_is_edge(const struct pirq *p, uint32_t vintid)
{
	return spi_ok(vintid) && BIT_GET(p->edge, vintid);
}
