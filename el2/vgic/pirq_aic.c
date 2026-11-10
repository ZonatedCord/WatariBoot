/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - pirq backend for the Apple AIC (AICv1). See include/wel2_aic.h for the register
 * map and the verified / hypothesis marks. Policy (level/edge, unmask at injection or at
 * guest deactivate, re-target on CPU_OFF) lives in pirq.c; this file only moves bits.
 *
 * Written from the public hardware description (Asahi Linux docs, DT binding); nothing is
 * copied from the GPL Linux / m1n1 drivers.
 */
#include "wel2_aic.h"

static inline uint32_t rd(const struct pirq_aic *a, uint32_t off)
{
	return a->io.rd32(a->io.ctx, off);
}

static inline void wr(const struct pirq_aic *a, uint32_t off, uint32_t v)
{
	a->io.wr32(a->io.ctx, off, v);
}

static inline bool irq_ok(const struct pirq_aic *a, uint32_t n)
{
	return n < a->nr_irq;
}

/* ---- pirq_ops ---- */

static void aic_mask(void *c, uint32_t phys)
{
	struct pirq_aic *a = c;

	if (irq_ok(a, phys))
		wr(a, AIC_MASK_SET(phys), 1u << (phys % 32));
}

static void aic_unmask(void *c, uint32_t phys)
{
	struct pirq_aic *a = c;

	if (irq_ok(a, phys))
		wr(a, AIC_MASK_CLR(phys), 1u << (phys % 32));
}

/* vCPU -> physical core -> single-bit TARGET_CPU (ARCHITECTURE devirq 4.3). */
static void aic_set_target(void *c, uint32_t phys, int vcpu)
{
	struct pirq_aic *a = c;

	if (!irq_ok(a, phys))
		return;
	if (vcpu < 0 || (unsigned)vcpu >= a->nr_vcpus) {
		a->st.bad_vcpu++;
		return;
	}
	wr(a, AIC_TARGET_CPU(phys), 1u << a->cpu_of_vcpu[vcpu]);
}

/* The AIC has no trigger-type register: every source is a level input at the AIC. Whether a
 * source is edge for the guest comes from the platform table (pirq.c / trigger_of). */
static void aic_set_trigger(void *c, uint32_t phys, bool edge)
{
	struct pirq_aic *a = c;

	(void)phys; (void)edge;
	a->st.trigger_noop++;
}

static uint32_t aic_intid_of(void *c, uint32_t phys)
{
	struct pirq_aic *a = c;

	return intr_exposed_vintid(a->map, phys);
}

static uint32_t aic_phys_of(void *c, uint32_t vintid)
{
	struct pirq_aic *a = c;

	return intr_exposed_hwirq(a->map, vintid);
}

static int aic_trigger_of(void *c, uint32_t phys)
{
	struct pirq_aic *a = c;

	return intr_trigger(a->map, phys);
}

int pirq_aic_init(struct pirq_aic *a, const struct aic_io *io, const struct intr_map *map,
		  const uint8_t *cpu_of_vcpu, unsigned nr_vcpus)
{
	uint32_t n;

	if (!a || !io || !io->rd32 || !io->wr32 || !map || !cpu_of_vcpu ||
	    nr_vcpus == 0 || nr_vcpus > AIC_MAX_CPUS)
		return -1;
	for (unsigned i = 0; i < nr_vcpus; i++)
		if (cpu_of_vcpu[i] >= AIC_MAX_CPUS)
			return -1;

	a->io = *io;
	a->map = map;
	a->nr_vcpus = nr_vcpus;
	for (unsigned i = 0; i < AIC_MAX_CPUS; i++)
		a->cpu_of_vcpu[i] = i < nr_vcpus ? cpu_of_vcpu[i] : 0;
	a->st = (struct pirq_aic_stats){ 0 };
	for (unsigned i = 0; i < AIC_MAX_IRQS / 32; i++)
		a->sw_owned[i] = 0;

	n = AIC_INFO_NR_IRQ(rd(a, AIC_INFO));
	if (n == 0 || (n & 31))
		return -1;                      /* implausible: do not touch the block */
	a->nr_irq = n > AIC_MAX_IRQS ? AIC_MAX_IRQS : n;

	/* Default deny, quiet start: every source masked and low, routed to vCPU 0's core. */
	for (uint32_t i = 0; i < a->nr_irq; i += 32) {
		wr(a, AIC_MASK_SET(i), 0xFFFFFFFFu);
		wr(a, AIC_SW_CLR(i), 0xFFFFFFFFu);
	}
	for (uint32_t i = 0; i < a->nr_irq; i++)
		wr(a, AIC_TARGET_CPU(i), 1u << a->cpu_of_vcpu[0]);

	a->ops = (struct pirq_ops){
		.name = "pirq-aic-t8103",
		.auto_masks = true,             /* the EVENT read masks the delivered source */
		.strict_level = true,           /* a held level source is not unmasked by a guest enable */
		.mask = aic_mask,
		.unmask = aic_unmask,
		.set_target = aic_set_target,
		.set_trigger = aic_set_trigger,
		.intid_of = aic_intid_of,
		.phys_of = aic_phys_of,
		.trigger_of = aic_trigger_of,
		.ctx = a,
	};
	return 0;
}

int pirq_aic_init_cpu(struct pirq_aic *a, unsigned vcpu)
{
	int rc = 0;

	if (vcpu >= a->nr_vcpus)
		return -1;
	/* Quiet this core's IPIs whatever WHOAMI says: it is the calling core's own view. */
	wr(a, AIC_IPI_ACK, AIC_IPI_OTHER | AIC_IPI_SELF);
	wr(a, AIC_IPI_MASK_SET, AIC_IPI_OTHER | AIC_IPI_SELF);
	if (rd(a, AIC_WHOAMI) != a->cpu_of_vcpu[vcpu]) {
		a->st.whoami_mismatch++;
		rc = -1;
	}
	return rc;
}

const struct pirq_ops *pirq_aic_ops(struct pirq_aic *a)
{
	return &a->ops;
}

struct aic_event pirq_aic_poll(struct pirq_aic *a)
{
	struct aic_event ev = { AIC_EV_NONE, 0, 0, 0 };
	uint32_t raw = rd(a, AIC_EVENT);

	if (raw == 0) {
		a->st.spurious++;
		return ev;
	}
	ev.raw = raw;
	ev.num = AIC_EVENT_NUM(raw);
	ev.die = AIC_EVENT_DIE(raw);
	a->st.events++;

	if (ev.die != 0) {                      /* single die on the t8103: not ours to interpret */
		ev.kind = AIC_EV_BAD;
		a->st.bad++;
		return ev;
	}
	switch (AIC_EVENT_TYPE(raw)) {
	case AIC_EVENT_TYPE_HW:
		if (irq_ok(a, ev.num)) {
			ev.kind = AIC_EV_IRQ;
			/* A source the layer raised by software stays asserted after the EVENT read (SW_SET is
			 * level-like [verified: re/m3a-ehw0-measurements.md, Q-D5, j293 2026-11-10]). Clear it
			 * NOW, at take time, not at EOI:
			 *  - the EVENT read has just masked the source, so nothing can re-fire between the read
			 *    and this write, and the software "event" is consumed exactly once, like an edge;
			 *  - at EOI the source is unmasked, and a new sw_set issued while the guest handles the
			 *    first one would be erased by a late SW_CLR: clearing now loses nothing;
			 *  - only bits we set ourselves are cleared (sw_owned). SW_CLR touches the software bit
			 *    only, the hardware line and a latched edge are separate state, so a level or edge
			 *    HW source is never disturbed, and a firmware-set SW bit is not ours to drop.
			 * The guest's pending state lives in the vGIC and is unaffected. */
			if (__atomic_fetch_and(&a->sw_owned[ev.num / 32], ~(1u << (ev.num % 32)),
					       __ATOMIC_SEQ_CST) & (1u << (ev.num % 32))) {
				wr(a, AIC_SW_CLR(ev.num), 1u << (ev.num % 32));
				a->st.sw_cleared++;
			}
		} else {
			ev.kind = AIC_EV_BAD;
			a->st.bad++;
		}
		break;
	case AIC_EVENT_TYPE_IPI:
		/* t8103 IPIs are system registers; a legacy one is unexpected. The EVENT read masked
		 * it; ack it and leave it masked so it cannot re-assert. */
		ev.kind = AIC_EV_IPI;
		a->st.ipi_unexpected++;
		wr(a, AIC_IPI_ACK, ev.num == AIC_EVENT_IPI_SELF ? AIC_IPI_SELF : AIC_IPI_OTHER);
		break;
	default:
		ev.kind = AIC_EV_BAD;
		a->st.bad++;
		break;
	}
	return ev;
}

void pirq_aic_sw_set(struct pirq_aic *a, uint32_t irq)
{
	if (irq_ok(a, irq)) {
		/* Order: SW_SET first, owner bit second, both visible to a poll on another core in that
		 * order. A poll between the two sees the source with no owner and does not clear it; the
		 * owner bit is then set with the SW bit still up, so the NEXT take clears it (one coalesced,
		 * possibly late, event; never a SW bit that nobody will clear). The opposite order could let
		 * a poll consume the owner bit before SW_SET lands and strand the bit set. owned is atomic
		 * (no lock exists in el2/ yet); a stale owner bit with SW clear only costs one extra SW_CLR.
		 * Two sw_sets before a take are one event (the bit is level-like): intended coalescing. */
		wr(a, AIC_SW_SET(irq), 1u << (irq % 32));
		__atomic_fetch_or(&a->sw_owned[irq / 32], 1u << (irq % 32), __ATOMIC_SEQ_CST);
	}
}

void pirq_aic_sw_clr(struct pirq_aic *a, uint32_t irq)
{
	if (irq_ok(a, irq)) {
		__atomic_fetch_and(&a->sw_owned[irq / 32], ~(1u << (irq % 32)), __ATOMIC_SEQ_CST);
		wr(a, AIC_SW_CLR(irq), 1u << (irq % 32));
	}
}

bool pirq_aic_line(struct pirq_aic *a, uint32_t irq)
{
	return irq_ok(a, irq) && ((rd(a, AIC_HW_IRQ_MON(irq)) >> (irq % 32)) & 1u);
}

void pirq_aic_ipi_send(struct pirq_aic *a, uint32_t mask)
{
	wr(a, AIC_IPI_SEND, mask);
}

void pirq_aic_ipi_unmask(struct pirq_aic *a, uint32_t mask)
{
	wr(a, AIC_IPI_MASK_CLR, mask);
}
