// SPDX-License-Identifier: MIT
/*
 * Watari EL2 - virtual CPU interface model. See wel2_vcif.h.
 *
 * Priority arithmetic (GIC spec 4.6, "Priority grouping"): the LR carries an
 * 8-bit priority of which the top `pribits` bits are implemented; with binary
 * point b the group priority is bits [7:b+1]; the active-priority registers
 * hold one bit per preemption level, level = group priority >> (8 - prebits),
 * bit (level % 32) of register (level / 32).
 */
#include "wel2_vcif.h"

static unsigned lr_state(uint64_t lr) { return (unsigned)(lr >> VCIF_LR_STATE_SHIFT) & 3; }
static uint32_t lr_id(uint64_t lr)    { return (uint32_t)lr; }

static unsigned pmask(const struct wel2_vcif *c)
{
	return (0xffu << (8 - c->pribits)) & 0xffu;
}

static unsigned lr_prio(const struct wel2_vcif *c, uint64_t lr)
{
	return (unsigned)(lr >> 48) & pmask(c);
}

static unsigned min_bpr0(const struct wel2_vcif *c) { return 7 - c->prebits; }
static unsigned min_bpr1(const struct wel2_vcif *c) { return 7 - c->prebits + 1; }

static unsigned vbpr0(const struct wel2_vcif *c) { return (c->vmcr >> VCIF_VMCR_VBPR0_SH) & 7; }
static unsigned vbpr1(const struct wel2_vcif *c) { return (c->vmcr >> VCIF_VMCR_VBPR1_SH) & 7; }
static unsigned vpmr(const struct wel2_vcif *c)  { return (c->vmcr >> VCIF_VMCR_VPMR_SH) & 0xff; }

/* Binary point that applies to Group 1 (VCBPR=1: Group 0's plus one). */
static unsigned bpr1_eff(const struct wel2_vcif *c)
{
	unsigned b;

	if (c->vmcr & VCIF_VMCR_VCBPR) {
		b = vbpr0(c) + 1;
		return b > 7 ? 7 : b;
	}
	return vbpr1(c);
}

static unsigned group_prio(const struct wel2_vcif *c, unsigned prio)
{
	return prio & (0xffu << (bpr1_eff(c) + 1)) & 0xffu;
}

static unsigned nlevels(const struct wel2_vcif *c) { return 1u << c->prebits; }

/* Running priority: lowest set level of AP0R/AP1R, 0xff when idle. */
static unsigned running_prio(const struct wel2_vcif *c)
{
	for (unsigned l = 0; l < nlevels(c); l++)
		if (((c->ap0r[l / 32] | c->ap1r[l / 32]) >> (l % 32)) & 1)
			return l << (8 - c->prebits);
	return 0xff;
}

void vcif_init(struct wel2_vcif *c, unsigned nlr, unsigned pribits, unsigned prebits)
{
	*c = (struct wel2_vcif){ 0 };
	c->nlr = nlr > VCIF_MAX_LRS ? VCIF_MAX_LRS : nlr;
	c->pribits = pribits < 5 ? 5 : (pribits > 8 ? 8 : pribits);
	c->prebits = prebits < 5 ? 5 : (prebits > 7 ? 7 : prebits);
	for (unsigned i = 0; i < VCIF_MAX_LRS; i++)
		c->owner[i] = -1;
	c->lr_dirty = (1u << c->nlr) - 1;
	vcif_reset_vmcr(c);
	c->ap_dirty = true;
}

void vcif_reset_vmcr(struct wel2_vcif *c)
{
	c->vmcr = (min_bpr0(c) << VCIF_VMCR_VBPR0_SH) | (min_bpr1(c) << VCIF_VMCR_VBPR1_SH);
	c->vmcr_dirty = true;
}

unsigned vcif_valid_lrs(const struct wel2_vcif *c)
{
	unsigned n = 0;

	for (unsigned i = 0; i < c->nlr; i++)
		n += lr_state(c->lr[i]) != 0;
	return n;
}

/* ---- list-register policy ---------------------------------------------- */

unsigned vcif_harvest(struct wel2_vcif *c, struct vgic *v, unsigned vcpu,
		      uint32_t *freed, unsigned max)
{
	unsigned n = 0;

	for (unsigned i = 0; i < c->nlr; i++) {
		uint32_t id;

		if (c->owner[i] < 0 || lr_state(c->lr[i]) != 0)
			continue;                 /* free, or still pending/active */
		id = (uint32_t)c->owner[i];
		vgic_eoi(v, vcpu, id);
		c->st_eoi++;
		c->lr[i] = 0;                     /* also clears the EOI maintenance source */
		c->owner[i] = -1;
		c->lr_dirty |= 1u << i;
		if (n < max)
			freed[n++] = id;
	}
	return n;
}

void vcif_fill(struct wel2_vcif *c, struct vgic *v, unsigned vcpu)
{
	for (;;) {
		uint32_t id;
		uint8_t prio;
		int slot = -1;

		if (!vgic_next_pending(v, vcpu, &id, &prio))
			return;
		prio &= (uint8_t)pmask(c);
		for (unsigned n = 0; n < c->nlr; n++) {
			if (c->owner[n] < 0) {
				slot = (int)n;
				break;
			}
		}
		if (slot < 0) {
			/* Full: the worst pending-only LR (the guest has not touched it)
			 * makes room if the newcomer has a strictly better priority. */
			unsigned worst = 0;

			for (unsigned n = 0; n < c->nlr; n++) {
				unsigned p = lr_prio(c, c->lr[n]);

				if (lr_state(c->lr[n]) == 1 && (slot < 0 || p > worst)) {
					slot = (int)n;
					worst = p;
				}
			}
			if (slot < 0 || worst <= prio)
				return;
			vgic_requeue(v, vcpu, (uint32_t)c->owner[slot]);
			c->owner[slot] = -1;
			c->lr[slot] = 0;
			c->st_evict++;
		}
		vgic_ack(v, vcpu);                /* picks the same interrupt */
		c->lr[slot] = VCIF_LR_PENDING | VCIF_LR_GROUP1 |
			      ((c->no_eoi_maint && id < 16) ? 0 : VCIF_LR_EOI) |
			      ((uint64_t)prio << 48) | id;
		c->owner[slot] = (int32_t)id;
		c->lr_dirty |= 1u << slot;
		c->st_inject++;
	}
}

bool vcif_overflow(const struct wel2_vcif *c, const struct vgic *v, unsigned vcpu)
{
	(void)c;
	return vgic_next_pending(v, vcpu, 0, 0);
}

/* ---- ICC emulation ------------------------------------------------------- */

/* Highest-priority pending LR (State pending or pending+active), lowest INTID
 * on a tie. Returns the LR index or -1. */
static int hppi_lr(const struct wel2_vcif *c)
{
	int best = -1;

	for (unsigned i = 0; i < c->nlr; i++) {
		unsigned s = lr_state(c->lr[i]);

		if (s != 1 && s != 3)
			continue;
		if (best < 0 || lr_prio(c, c->lr[i]) < lr_prio(c, c->lr[best]) ||
		    (lr_prio(c, c->lr[i]) == lr_prio(c, c->lr[best]) &&
		     lr_id(c->lr[i]) < lr_id(c->lr[best])))
			best = (int)i;
	}
	return best;
}

uint32_t vcif_iar1(struct wel2_vcif *c)
{
	int i = hppi_lr(c);
	unsigned p, gp, level;

	if (i < 0 || !(c->lr[i] & VCIF_LR_GROUP1) || !(c->vmcr & VCIF_VMCR_VENG1))
		return VGIC_INTID_SPURIOUS;
	p = lr_prio(c, c->lr[i]);
	gp = group_prio(c, p);
	/* Signalled only if above the mask and above the running group priority. */
	if (p >= vpmr(c) || gp >= running_prio(c))
		return VGIC_INTID_SPURIOUS;
	c->lr[i] = (c->lr[i] & ~VCIF_LR_STATE_MASK) | VCIF_LR_ACTIVE;
	c->lr_dirty |= 1u << i;
	level = gp >> (8 - c->prebits);
	c->ap1r[level / 32] |= 1u << (level % 32);
	c->ap_dirty = true;
	return lr_id(c->lr[i]);
}

uint32_t vcif_hppir1(const struct wel2_vcif *c)
{
	int i = hppi_lr(c);

	if (i < 0 || !(c->lr[i] & VCIF_LR_GROUP1) || !(c->vmcr & VCIF_VMCR_VENG1))
		return VGIC_INTID_SPURIOUS;
	return lr_id(c->lr[i]);
}

uint32_t vcif_rpr(const struct wel2_vcif *c) { return running_prio(c); }

static bool deactivate(struct wel2_vcif *c, uint32_t id)
{
	for (unsigned i = 0; i < c->nlr; i++) {
		unsigned s = lr_state(c->lr[i]);

		if ((s == 2 || s == 3) && lr_id(c->lr[i]) == id) {
			c->lr[i] &= ~VCIF_LR_STATE_MASK;
			if (s == 3)
				c->lr[i] |= VCIF_LR_PENDING;      /* pending+active -> pending */
			c->lr_dirty |= 1u << i;
			return true;
		}
	}
	return false;
}

/* Priority drop: clear the lowest set (= highest priority) bit of AP1R. */
static void priority_drop(struct wel2_vcif *c)
{
	for (unsigned l = 0; l < nlevels(c); l++) {
		if ((c->ap1r[l / 32] >> (l % 32)) & 1) {
			c->ap1r[l / 32] &= ~(1u << (l % 32));
			c->ap_dirty = true;
			return;
		}
	}
}

void vcif_eoir1(struct wel2_vcif *c, uint32_t intid)
{
	intid &= 0xffff;                          /* IDbits = 16 */
	if (intid >= 1020 && intid <= 1023)
		return;                           /* special INTIDs: no priority drop */
	priority_drop(c);
	if (!(c->vmcr & VCIF_VMCR_VEOIM) && !deactivate(c, intid))
		c->eoicount++;
}

void vcif_dir(struct wel2_vcif *c, uint32_t intid)
{
	intid &= 0xffff;
	if (!(c->vmcr & VCIF_VMCR_VEOIM))
		return;                           /* UNPREDICTABLE with EOImode 0: ignored */
	if (intid >= 1020 || !deactivate(c, intid))
		c->eoicount++;
}

uint32_t vcif_pmr_get(const struct wel2_vcif *c) { return vpmr(c); }

void vcif_pmr_set(struct wel2_vcif *c, uint32_t v)
{
	c->vmcr = (c->vmcr & 0x00ffffffu) | ((v & pmask(c) & 0xffu) << VCIF_VMCR_VPMR_SH);
	c->vmcr_dirty = true;
}

uint32_t vcif_bpr1_get(const struct wel2_vcif *c) { return bpr1_eff(c); }

void vcif_bpr1_set(struct wel2_vcif *c, uint32_t v)
{
	unsigned b = v & 7;

	if (c->vmcr & VCIF_VMCR_VCBPR)
		return;                           /* ignored while the Group 0 point is shared */
	if (b < min_bpr1(c))
		b = min_bpr1(c);
	c->vmcr = (c->vmcr & ~(7u << VCIF_VMCR_VBPR1_SH)) | (b << VCIF_VMCR_VBPR1_SH);
	c->vmcr_dirty = true;
}

uint32_t vcif_bpr0_get(const struct wel2_vcif *c) { return vbpr0(c); }

void vcif_bpr0_set(struct wel2_vcif *c, uint32_t v)
{
	unsigned b = v & 7;

	if (b < min_bpr0(c))
		b = min_bpr0(c);
	c->vmcr = (c->vmcr & ~(7u << VCIF_VMCR_VBPR0_SH)) | (b << VCIF_VMCR_VBPR0_SH);
	c->vmcr_dirty = true;
}

uint32_t vcif_igrpen_get(const struct wel2_vcif *c, unsigned group)
{
	return (c->vmcr >> (group ? 1 : 0)) & 1;
}

void vcif_igrpen_set(struct wel2_vcif *c, unsigned group, uint32_t v)
{
	uint32_t bit = group ? VCIF_VMCR_VENG1 : VCIF_VMCR_VENG0;

	c->vmcr = (v & 1) ? (c->vmcr | bit) : (c->vmcr & ~bit);
	c->vmcr_dirty = true;
}

/* ICC_CTLR_EL1 (12.2.6): CBPR 0, EOImode 1, PRIbits [10:8], IDbits [13:11],
 * SEIS 14, A3V 15. SEIS is reported 0 on purpose (the SEIS erratum is the
 * reason this path exists: the guest must not think it has to handle SEIS);
 * IDbits = 0 (16 bits) as the vGIC model has 960 INTIDs; A3V = 1. */
uint32_t vcif_ctlr_get(const struct wel2_vcif *c)
{
	return ((c->vmcr & VCIF_VMCR_VCBPR) ? 1u : 0u) |
	       ((c->vmcr & VCIF_VMCR_VEOIM) ? 2u : 0u) |
	       ((c->pribits - 1) << 8) | (1u << 15);
}

void vcif_ctlr_set(struct wel2_vcif *c, uint32_t v)
{
	c->vmcr = (c->vmcr & ~(VCIF_VMCR_VCBPR | VCIF_VMCR_VEOIM)) |
		  ((v & 1) ? VCIF_VMCR_VCBPR : 0) | ((v & 2) ? VCIF_VMCR_VEOIM : 0);
	c->vmcr_dirty = true;
}

uint32_t vcif_ap_get(const struct wel2_vcif *c, unsigned group, unsigned n)
{
	if (n >= (nlevels(c) + 31) / 32)
		return 0;
	return group ? c->ap1r[n] : c->ap0r[n];
}

void vcif_ap_set(struct wel2_vcif *c, unsigned group, unsigned n, uint32_t v)
{
	if (n >= (nlevels(c) + 31) / 32)
		return;
	if (group)
		c->ap1r[n] = v;
	else
		c->ap0r[n] = v;
	c->ap_dirty = true;
}
