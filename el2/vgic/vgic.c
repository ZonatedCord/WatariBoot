/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - virtual GICv3 model. See wel2_vgic.h.
 * Written from the architecture description in INTERFACES.md sections 5-6.
 */
#include "wel2_vgic.h"

/* ---- constants -------------------------------------------------------- */

/*
 * HYPOTHESIS: GICD_TYPER = ITLinesNumber 29 | IDbits 15 (<<19) | A3V (<<24)
 * = 0x0148001D: IDbits=9 (no LPIs => at most 10 INTID bits, GIC IHI 0069H.b §2.2.2 p.2-37; corrected in M0-A). To be confirmed against the QEMU oracle.
 */
#define GICD_TYPER_VALUE   0x0148001Du
/*
 * HYPOTHESIS: IIDR is a placeholder (ARM implementer JEP106 0x43B); the real
 * value must be copied from the QEMU oracle (INTERFACES section 5).
 */
#define GIC_IIDR_VALUE     0x0000043Bu

/*
 * HYPOTHESIS: GICD_CTLR layout with DS=1: bit0 EnableGrp0, bit1 EnableGrp1,
 * bit4 ARE (RAO/WI), bit6 DS (RAO/WI), bit7 E1NWF RAZ/WI, bit31 RWP always 0.
 */
#define GICD_CTLR_ENGRP0   0x01u
#define GICD_CTLR_ENGRP1   0x02u
#define GICD_CTLR_ARE      0x10u
#define GICD_CTLR_DS       0x40u

/*
 * HYPOTHESIS: GICR_WAKER bit1 ProcessorSleep (RW), bit2 ChildrenAsleep mirrors
 * ProcessorSleep immediately; reset value 0x6.
 */
#define GICR_WAKER_SLEEP   0x2u
#define GICR_WAKER_CASLEEP 0x4u
/* HYPOTHESIS: GICR_TYPER.Last is bit 4, Processor_Number bits 23:8. */
#define GICR_TYPER_LAST    (1u << 4)

/* Implemented priority bits: top 5 (32 levels), INTERFACES section 5. */
#define PRIO_MASK          0xF8u

/* IROUTER: IRM bit31, Aff3 [39:32], Aff2 [23:16], Aff1 [15:8], Aff0 [7:0]. */
#define IROUTER_IRM        (1ull << 31)
#define IROUTER_MASK       0x000000FF80FFFFFFull

#define IN(o, lo, n) ((o) >= (lo) && (o) < (lo) + (n))

enum { W_OK = 0, W_UNIMPL, W_IGNORED };
enum { K_DIST = 0, K_RD, K_SGI };
enum { B_GROUP, B_ENSET, B_ENCLR, B_PENDSET, B_PENDCLR, B_ACTSET, B_ACTCLR };

/* ---- helpers ---------------------------------------------------------- */

static void zero(void *p, size_t n)
{
	uint8_t *b = p;
	while (n--)
		*b++ = 0;
}

static struct vgic_irq *irq_get(const struct vgic *v, int vcpu, uint32_t intid)
{
	if (intid < 32) {
		if (vcpu < 0 || (unsigned)vcpu >= v->nr_vcpus)
			return NULL;
		return (struct vgic_irq *)&v->vcpu[vcpu].priv[intid];
	}
	if (intid >= VGIC_NR_IRQS)
		return NULL;
	return (struct vgic_irq *)&v->spi[intid - 32];
}

static bool irq_pending(const struct vgic_irq *i)
{
	/* Level lines keep an interrupt pending only while it is not active. */
	return (i->flags & VGIC_F_PENDING) ||
	       (!(i->flags & VGIC_F_EDGE) && (i->flags & VGIC_F_LINE) &&
		!(i->flags & VGIC_F_ACTIVE));
}

static void do_log(struct vgic *v, enum vgic_log_kind k, uint64_t addr,
		   unsigned size, bool wr, uint64_t val)
{
	if (v->hooks.log)
		v->hooks.log(v->hooks.ctx, k, addr, size, wr, val);
}

static void do_effect(struct vgic *v, enum vgic_effect e, uint32_t intid, int arg)
{
	if (v->hooks.effect)
		v->hooks.effect(v->hooks.ctx, e, intid, arg);
}

static uint32_t aff_from_irouter(uint64_t r)
{
	return (uint32_t)(((r >> 32) & 0xFF) << 24) | (uint32_t)(r & 0xFFFFFF);
}

int vgic_route_target(const struct vgic *v, uint32_t intid)
{
	uint64_t r;
	unsigned i;

	if (intid < 32 || intid >= VGIC_NR_IRQS)
		return -1;
	r = v->irouter[intid - 32];
	if (r & IROUTER_IRM) {
		/* vCPU 0 if awake, otherwise the first awake vCPU. */
		for (i = 0; i < v->nr_vcpus; i++)
			if (!v->vcpu[i].sleep)
				return (int)i;
		return -1;
	}
	for (i = 0; i < v->nr_vcpus; i++)
		if (v->vcpu[i].aff == aff_from_irouter(r))
			return (int)i;
	return -1;
}

static void emit_target(struct vgic *v, uint32_t intid)
{
	do_effect(v, VGIC_EFFECT_SET_TARGET, intid, vgic_route_target(v, intid));
}

/* ---- init / reset ----------------------------------------------------- */

void vgic_vcpu_reset(struct vgic *v, unsigned idx)
{
	unsigned j;

	if (idx >= v->nr_vcpus)
		return;
	zero(v->vcpu[idx].priv, sizeof(v->vcpu[idx].priv));
	for (j = 0; j < 16; j++)
		v->vcpu[idx].priv[j].flags = VGIC_F_EDGE; /* SGIs: always edge */
	v->vcpu[idx].sleep = true;                        /* WAKER reset value 0x6 */
	v->vcpu[idx].rd_ctlr = 0;
	v->vcpu[idx].propbaser = v->vcpu[idx].pendbaser = 0;
	zero(v->vcpu[idx].lpi, sizeof(v->vcpu[idx].lpi));
}

void vgic_vcpu_wake(struct vgic *v, unsigned idx)
{
	if (idx < v->nr_vcpus)
		v->vcpu[idx].sleep = false;
}

void vgic_reset(struct vgic *v)
{
	unsigned i;

	v->ctlr = 0;
	zero(v->irouter, sizeof(v->irouter));
	zero(v->spi, sizeof(v->spi));
	for (i = 0; i < v->nr_vcpus; i++)
		vgic_vcpu_reset(v, i);
}

int vgic_init(struct vgic *v, const struct vgic_config *cfg)
{
	unsigned i;

	if (!v || !cfg || cfg->nr_vcpus < 1 || cfg->nr_vcpus > VGIC_MAX_VCPUS)
		return -1;
	zero(v, sizeof(*v));
	v->gicd_base = cfg->gicd_base;
	v->gicr_base = cfg->gicr_base;
	v->nr_vcpus = cfg->nr_vcpus;
	v->hooks = cfg->hooks;
	v->lpis = cfg->lpis;
	v->idbits = cfg->idbits ? cfg->idbits : 16;
	if (v->idbits < 14)
		v->idbits = 14;                  /* an LPI INTID (8192 and up) needs 14 bits */
	if (v->idbits > 24)
		v->idbits = 24;
	for (i = 0; i < v->nr_vcpus; i++)
		v->vcpu[i].aff = cfg->affinity ? cfg->affinity[i] : i;
	vgic_reset(v);
	return 0;
}

/* ---- bitmap registers (IGROUPR, ISENABLER, ...) ----------------------- */

static uint32_t bm_read(struct vgic *v, int vcpu, int kind, uint32_t base)
{
	uint32_t r = 0, i;

	for (i = 0; i < 32; i++) {
		const struct vgic_irq *q = irq_get(v, vcpu, base + i);
		bool b;

		if (!q)
			continue;
		switch (kind) {
		case B_GROUP:   b = q->flags & VGIC_F_GROUP1; break;
		case B_ENSET:
		case B_ENCLR:   b = q->flags & VGIC_F_ENABLED; break;
		case B_PENDSET:
		case B_PENDCLR: b = irq_pending(q); break;
		default:        b = q->flags & VGIC_F_ACTIVE; break;
		}
		if (b)
			r |= 1u << i;
	}
	return r;
}

static void flag_assign(struct vgic_irq *q, uint8_t f, bool on)
{
	if (on)
		q->flags |= f;
	else
		q->flags &= (uint8_t)~f;
}

static void bm_write(struct vgic *v, int vcpu, int kind, uint32_t base, uint32_t val)
{
	uint32_t i;

	for (i = 0; i < 32; i++) {
		uint32_t id = base + i;
		struct vgic_irq *q = irq_get(v, vcpu, id);
		bool bit = (val >> i) & 1u;

		if (!q)
			continue;
		if (kind == B_GROUP) {
			flag_assign(q, VGIC_F_GROUP1, bit);
			continue;
		}
		if (!bit)
			continue;
		switch (kind) {
		case B_ENSET:
			if (!(q->flags & VGIC_F_ENABLED)) {
				q->flags |= VGIC_F_ENABLED;
				if (id >= 32) {
					emit_target(v, id);
					do_effect(v, VGIC_EFFECT_UNMASK_SPI, id, 0);
				}
			}
			break;
		case B_ENCLR:
			if (q->flags & VGIC_F_ENABLED) {
				q->flags &= (uint8_t)~VGIC_F_ENABLED;
				if (id >= 32)
					do_effect(v, VGIC_EFFECT_MASK_SPI, id, 0);
			}
			break;
		case B_PENDSET:  q->flags |= VGIC_F_PENDING; break;
		case B_PENDCLR:  q->flags &= (uint8_t)~VGIC_F_PENDING; break;
		case B_ACTSET:   q->flags |= VGIC_F_ACTIVE; break;
		default:         q->flags &= (uint8_t)~VGIC_F_ACTIVE; break;
		}
	}
}

static int bm_access(struct vgic *v, int vcpu, int kind, uint32_t base,
		     bool wr, uint32_t *val)
{
	if (wr)
		bm_write(v, vcpu, kind, base, *val);
	else
		*val = bm_read(v, vcpu, kind, base);
	return W_OK;
}

/* ICFGR: 2 bits per INTID, 16 INTIDs per word; bit1 = edge, bit0 RAZ. */
static int cfg_access(struct vgic *v, int vcpu, uint32_t base, bool wr, uint32_t *val)
{
	uint32_t i, r = 0;

	for (i = 0; i < 16; i++) {
		struct vgic_irq *q = irq_get(v, vcpu, base + i);

		if (!q)
			continue;
		if (wr && base + i >= 16) { /* SGIs are RAO/WI */
			bool edge = (*val >> (2 * i + 1)) & 1u;
			bool was = q->flags & VGIC_F_EDGE;

			flag_assign(q, VGIC_F_EDGE, edge);
			if (base + i >= 32 && edge != was)
				do_effect(v, VGIC_EFFECT_SET_TRIGGER, base + i, edge);
		}
		if (q->flags & VGIC_F_EDGE)
			r |= 2u << (2 * i);
	}
	if (!wr)
		*val = r;
	return W_OK;
}

/* IPRIORITYR: byte granular (size 1 or 4). */
static void prio_access(struct vgic *v, int vcpu, uint32_t base, unsigned size,
			bool wr, uint64_t *val)
{
	uint64_t r = 0;
	unsigned i;

	for (i = 0; i < size; i++) {
		struct vgic_irq *q = irq_get(v, vcpu, base + i);

		if (!q)
			continue;
		if (wr)
			q->prio = (uint8_t)((*val >> (8 * i)) & PRIO_MASK);
		else
			r |= (uint64_t)q->prio << (8 * i);
	}
	if (!wr)
		*val = r;
}

/* ---- ID registers ----------------------------------------------------- */

static uint32_t id_reg(uint32_t off)
{
	switch (off) {
	/* HYPOTHESIS: PIDR2 = ArchRev 3 (bits 7:4) | usesJEPcode | JEP id 3. */
	case 0xFFE8: return 0x3B;
	case 0xFFF0: return 0x0D; /* CIDR0..3: standard component ID preamble */
	case 0xFFF4: return 0xF0;
	case 0xFFF8: return 0x05;
	case 0xFFFC: return 0xB1;
	default:     return 0;
	}
}

/* ---- GICD ------------------------------------------------------------- */

static int gicd_word(struct vgic *v, uint32_t off, bool wr, uint32_t *val)
{
	if (IN(off, 0x80, 0x80))
		return bm_access(v, -1, B_GROUP, 32 * ((off - 0x80) / 4), wr, val);
	if (IN(off, 0x100, 0x80))
		return bm_access(v, -1, B_ENSET, 32 * ((off - 0x100) / 4), wr, val);
	if (IN(off, 0x180, 0x80))
		return bm_access(v, -1, B_ENCLR, 32 * ((off - 0x180) / 4), wr, val);
	if (IN(off, 0x200, 0x80))
		return bm_access(v, -1, B_PENDSET, 32 * ((off - 0x200) / 4), wr, val);
	if (IN(off, 0x280, 0x80))
		return bm_access(v, -1, B_PENDCLR, 32 * ((off - 0x280) / 4), wr, val);
	if (IN(off, 0x300, 0x80))
		return bm_access(v, -1, B_ACTSET, 32 * ((off - 0x300) / 4), wr, val);
	if (IN(off, 0x380, 0x80))
		return bm_access(v, -1, B_ACTCLR, 32 * ((off - 0x380) / 4), wr, val);
	if (IN(off, 0xC00, 0x100))
		return cfg_access(v, -1, 16 * ((off - 0xC00) / 4), wr, val);
	/* RAZ/WI (ARE=1 / DS=1): ITARGETSR, IGRPMODR, NSACR, CPENDSGIR, SPENDSGIR */
	if (IN(off, 0x800, 0x400) || IN(off, 0xD00, 0x80) || IN(off, 0xE00, 0x100) ||
	    IN(off, 0xF10, 0x20)) {
		if (!wr)
			*val = 0;
		return W_OK;
	}
	if (IN(off, 0xFFD0, 0x30)) {
		if (!wr)
			*val = id_reg(off);
		return W_OK;
	}
	switch (off) {
	case 0x0000:
		if (wr)
			v->ctlr = *val & (GICD_CTLR_ENGRP0 | GICD_CTLR_ENGRP1);
		else
			*val = v->ctlr | GICD_CTLR_ARE | GICD_CTLR_DS; /* RWP = 0 */
		return W_OK;
	case 0x0004:
		if (!wr)
			*val = v->lpis ? ((GICD_TYPER_VALUE & ~(0x1fu << 19)) | ((v->idbits - 1) << 19) | (1u << 17))
				       : GICD_TYPER_VALUE;     /* LPIS bit 17, IDbits = physical IDbits */
		return W_OK;
	case 0x0008:
		if (!wr)
			*val = GIC_IIDR_VALUE;
		return W_OK;
	case 0x000C: /* TYPER2 */
	case 0x0010: /* STATUSR */
		if (!wr)
			*val = 0;
		return W_OK;
	case 0x0040: /* SETSPI_NSR (MBIS=0) */
	case 0x0048: /* CLRSPI_NSR */
	case 0x0F00: /* SGIR (GICv2 legacy) */
		if (!wr)
			*val = 0;
		return wr ? W_IGNORED : W_UNIMPL;
	default:
		if (!wr)
			*val = 0;
		return W_UNIMPL;
	}
}

static void irouter_access(struct vgic *v, uint32_t off, unsigned size, bool wr,
			   uint64_t *val)
{
	uint32_t intid = (off - 0x6000) / 8;
	uint64_t *r;
	uint64_t nv;

	if (intid < 32 || intid >= VGIC_NR_IRQS) { /* reserved: RAZ/WI */
		if (!wr)
			*val = 0;
		return;
	}
	r = &v->irouter[intid - 32];
	if (!wr) {
		*val = size == 8 ? *r : ((off & 4) ? *r >> 32 : *r & 0xFFFFFFFFu);
		return;
	}
	if (size == 8)
		nv = *val;
	else if (off & 4)
		nv = (*r & 0xFFFFFFFFull) | (*val << 32);
	else
		nv = (*r & ~0xFFFFFFFFull) | (*val & 0xFFFFFFFFull);
	*r = nv & IROUTER_MASK;
	emit_target(v, intid);
}

/* ---- GICR ------------------------------------------------------------- */

static int rd_word(struct vgic *v, unsigned cpu, uint32_t off, bool wr, uint32_t *val)
{
	struct vgic_vcpu *c = &v->vcpu[cpu];

	if (IN(off, 0xFFD0, 0x30)) {
		if (!wr)
			*val = id_reg(off);
		return W_OK;
	}
	switch (off) {
	case 0x0000: /* CTLR: EnableLPIs (LPI mode only, else RAZ/WI), RWP = 0, UWP = 0 */
		if (v->lpis) {
			if (wr) {
				c->rd_ctlr = *val & 1u;
				if (v->hooks.lpi_reg)
					v->hooks.lpi_reg(v->hooks.ctx, cpu, 0x0, *val & 1u);
			} else {
				*val = c->rd_ctlr;
			}
			return W_OK;
		}
		if (!wr)
			*val = 0;
		return W_OK;
	case 0x00C0: /* SYNCR: nothing is ever in flight in the model (LPI mode) */
		if (!wr)
			*val = 0;
		return v->lpis ? W_OK : W_UNIMPL;
	case 0x0010: /* STATUSR */
		if (!wr)
			*val = 0;
		return W_OK;
	case 0x0004:
		if (!wr)
			*val = GIC_IIDR_VALUE;
		return W_OK;
	case 0x0008: /* TYPER low: Last, Processor_Number; PLPIS/VLPIS/DirectLPI = 0 */
		if (!wr)
			*val = (cpu << 8) | (cpu == v->nr_vcpus - 1 ? GICR_TYPER_LAST : 0) | (v->lpis ? 1u : 0);
		return W_OK;
	case 0x000C: /* TYPER high: Affinity_Value */
		if (!wr)
			*val = c->aff;
		return W_OK;
	case 0x0014: /* WAKER */
		if (wr)
			c->sleep = (*val & GICR_WAKER_SLEEP) != 0;
		else
			*val = c->sleep ? (GICR_WAKER_SLEEP | GICR_WAKER_CASLEEP) : 0;
		return W_OK;
	default:
		if (!wr)
			*val = 0;
		return W_UNIMPL;
	}
}

static int sgi_word(struct vgic *v, unsigned cpu, uint32_t off, bool wr, uint32_t *val)
{
	int c = (int)cpu;

	switch (off) {
	case 0x080: return bm_access(v, c, B_GROUP, 0, wr, val);
	case 0x100: return bm_access(v, c, B_ENSET, 0, wr, val);
	case 0x180: return bm_access(v, c, B_ENCLR, 0, wr, val);
	case 0x200: return bm_access(v, c, B_PENDSET, 0, wr, val);
	case 0x280: return bm_access(v, c, B_PENDCLR, 0, wr, val);
	case 0x300: return bm_access(v, c, B_ACTSET, 0, wr, val);
	case 0x380: return bm_access(v, c, B_ACTCLR, 0, wr, val);
	case 0xC00: return cfg_access(v, c, 0, wr, val);
	case 0xC04: return cfg_access(v, c, 16, wr, val);
	case 0xD00: /* IGRPMODR0 */
	case 0xE00: /* NSACR */
		if (!wr)
			*val = 0;
		return W_OK;
	default:
		if (!wr)
			*val = 0;
		return W_UNIMPL;
	}
}

/* ---- MMIO entry points ------------------------------------------------ */

static int word_dispatch(struct vgic *v, int kind, unsigned cpu, uint32_t off,
			 bool wr, uint32_t *val)
{
	if (kind == K_DIST)
		return gicd_word(v, off, wr, val);
	if (kind == K_RD)
		return rd_word(v, cpu, off, wr, val);
	return sgi_word(v, cpu, off, wr, val);
}

static int mmio(struct vgic *v, uint64_t addr, unsigned size, bool wr, uint64_t *val)
{
	int kind;
	unsigned cpu = 0;
	uint32_t off;
	uint64_t orig = wr ? *val : 0;

	if (!wr)
		*val = 0;

	if (addr >= v->gicd_base && addr < v->gicd_base + VGIC_GICD_SIZE) {
		kind = K_DIST;
		off = (uint32_t)(addr - v->gicd_base);
	} else if (addr >= v->gicr_base &&
		   addr < v->gicr_base + (uint64_t)VGIC_MAX_VCPUS * VGIC_GICR_FRAME_SIZE) {
		uint64_t rel = addr - v->gicr_base;

		cpu = (unsigned)(rel / VGIC_GICR_FRAME_SIZE);
		off = (uint32_t)(rel % VGIC_GICR_FRAME_SIZE);
		if (cpu >= v->nr_vcpus) { /* frame of a non-existent vCPU: RAZ/WI */
			do_log(v, VGIC_LOG_UNIMPL, addr, size, wr, orig);
			return VGIC_OK;
		}
		kind = off >= VGIC_GICR_SGI_OFFSET ? K_SGI : K_RD;
		off &= VGIC_GICR_SGI_OFFSET - 1;
	} else {
		do_log(v, VGIC_LOG_BAD_ACCESS, addr, size, wr, orig);
		return VGIC_ERR_RANGE;
	}

	if (kind == K_DIST && IN(off, 0x6000, 0x2000)) {
		if ((size != 4 && size != 8) || (off % size)) {
			do_log(v, VGIC_LOG_BAD_ACCESS, addr, size, wr, orig);
			return VGIC_ERR_ACCESS;
		}
		irouter_access(v, off, size, wr, val);
		return VGIC_OK;
	}
	if ((kind == K_DIST && IN(off, 0x400, 0x400)) ||
	    (kind == K_SGI && IN(off, 0x400, 0x20))) {
		if ((size != 1 && size != 4) || (off % size)) {
			do_log(v, VGIC_LOG_BAD_ACCESS, addr, size, wr, orig);
			return VGIC_ERR_ACCESS;
		}
		prio_access(v, kind == K_DIST ? -1 : (int)cpu, off - 0x400, size, wr, val);
		return VGIC_OK;
	}
	if (kind == K_RD && off == 0x8 && size == 8) { /* GICR_TYPER, 64-bit, RO */
		uint32_t lo = 0, hi = 0;

		rd_word(v, cpu, 0x8, false, &lo);
		rd_word(v, cpu, 0xC, false, &hi);
		if (!wr)
			*val = ((uint64_t)hi << 32) | lo;
		return VGIC_OK;
	}
	if (kind == K_RD && v->lpis && (off == 0x70 || off == 0x78 || off == 0xA0 || off == 0xB0) &&
	    (size == 8 || size == 4)) {
		/* LPI configuration registers (64-bit, accessed as one word or two halves) */
		struct vgic_vcpu *c = &v->vcpu[cpu];
		uint64_t *r = off == 0x70 ? &c->propbaser : &c->pendbaser;
		uint64_t nv;

		if (off >= 0xA0) { /* INVLPIR / INVALLR: write-only, forwarded */
			if (wr && v->hooks.lpi_reg)
				v->hooks.lpi_reg(v->hooks.ctx, cpu, off, size == 8 ? orig : orig & 0xFFFFFFFFu);
			return VGIC_OK;
		}
		if (!wr) {
			*val = (size == 8 ? *r : *r & 0xFFFFFFFFull) & ~(1ull << 62);   /* PTZ reads as zero */
			return VGIC_OK;
		}
		if (c->rd_ctlr & 1u)            /* UNPREDICTABLE while EnableLPIs == 1: ignored, not forwarded */
			return VGIC_OK;
		nv = size == 8 ? orig : ((*r & ~0xFFFFFFFFull) | (orig & 0xFFFFFFFFull));
		*r = nv;
		if (v->hooks.lpi_reg)
			v->hooks.lpi_reg(v->hooks.ctx, cpu, off, nv);
		return VGIC_OK;
	}
	if (kind == K_RD && v->lpis && (off == 0x74 || off == 0x7C) && size == 4) { /* high halves */
		struct vgic_vcpu *c = &v->vcpu[cpu];
		uint64_t *r = off == 0x74 ? &c->propbaser : &c->pendbaser;

		if (!wr) {
			*val = (uint32_t)(*r >> 32) & ~(1u << 30);
			return VGIC_OK;
		}
		if (c->rd_ctlr & 1u)
			return VGIC_OK;
		*r = (*r & 0xFFFFFFFFull) | (orig << 32);
		if (v->hooks.lpi_reg)
			v->hooks.lpi_reg(v->hooks.ctx, cpu, off - 4, *r);
		return VGIC_OK;
	}
	if (size != 4 || (off & 3)) {
		do_log(v, VGIC_LOG_BAD_ACCESS, addr, size, wr, orig);
		return VGIC_ERR_ACCESS;
	}
	{
		uint32_t w = wr ? (uint32_t)orig : 0;
		int res = word_dispatch(v, kind, cpu, off, wr, &w);

		if (res == W_UNIMPL)
			do_log(v, VGIC_LOG_UNIMPL, addr, size, wr, orig);
		else if (res == W_IGNORED)
			do_log(v, VGIC_LOG_IGNORED, addr, size, wr, orig);
		if (!wr)
			*val = w;
	}
	return VGIC_OK;
}

int vgic_mmio_read(struct vgic *v, uint64_t addr, unsigned size, uint64_t *val)
{
	return mmio(v, addr, size, false, val);
}

int vgic_mmio_write(struct vgic *v, uint64_t addr, unsigned size, uint64_t val)
{
	return mmio(v, addr, size, true, &val);
}

/* ---- injection and delivery ------------------------------------------- */

int vgic_set_irq_level(struct vgic *v, unsigned vcpu_idx, uint32_t intid, bool level)
{
	struct vgic_irq *q;

	if (intid < 16)
		return -1; /* SGIs have no input line */
	q = irq_get(v, (int)vcpu_idx, intid);
	if (!q)
		return -1;
	if (level && !(q->flags & VGIC_F_LINE) && (q->flags & VGIC_F_EDGE))
		q->flags |= VGIC_F_PENDING; /* rising edge */
	flag_assign(q, VGIC_F_LINE, level);
	return 0;
}

int vgic_set_pending(struct vgic *v, unsigned vcpu_idx, uint32_t intid)
{
	struct vgic_irq *q = irq_get(v, (int)vcpu_idx, intid);

	if (!q)
		return -1;
	q->flags |= VGIC_F_PENDING;
	return 0;
}

uint32_t vgic_sgi_send_mask(struct vgic *v, unsigned sender_idx, uint64_t sgi1r)
{
	uint32_t intid = (uint32_t)((sgi1r >> 24) & 0xF);
	uint32_t mask = 0;
	unsigned i;

	if (sender_idx >= v->nr_vcpus)
		return 0;
	if (sgi1r & (1ULL << 40)) { /* IRM: all PEs but the sender; Aff/TargetList RES0 */
		for (i = 0; i < v->nr_vcpus; i++) {
			if (i == sender_idx)
				continue;
			v->vcpu[i].priv[intid].flags |= VGIC_F_PENDING;
			mask |= 1u << i;
		}
	} else {
		uint32_t list = (uint32_t)(sgi1r & 0xFFFF);
		uint32_t rs = (uint32_t)((sgi1r >> 44) & 0xF); /* RES0 unless RSS; harmless */
		uint32_t pfx = (uint32_t)(((sgi1r >> 48) & 0xFF) << 24) |
			       (uint32_t)(((sgi1r >> 32) & 0xFF) << 16) |
			       (uint32_t)(((sgi1r >> 16) & 0xFF) << 8);

		for (i = 0; i < v->nr_vcpus; i++) {
			uint32_t a = v->vcpu[i].aff;
			uint32_t a0 = a & 0xFF;

			if ((a & 0xFFFFFF00u) != pfx || a0 / 16 != rs)
				continue;
			if (!(list & (1u << (a0 % 16))))
				continue;
			v->vcpu[i].priv[intid].flags |= VGIC_F_PENDING;
			mask |= 1u << i;
		}
	}
	return mask;
}

int vgic_sgi_send(struct vgic *v, unsigned sender_idx, uint64_t sgi1r)
{
	uint32_t m = vgic_sgi_send_mask(v, sender_idx, sgi1r);
	int n = 0;

	for (; m; m &= m - 1)
		n++;
	return n;
}

bool vgic_next_pending(const struct vgic *v, unsigned vcpu_idx,
		       uint32_t *intid, uint8_t *prio)
{
	uint32_t id;
	bool found = false;
	uint8_t best = 0xFF;

	if (vcpu_idx >= v->nr_vcpus || v->vcpu[vcpu_idx].sleep)
		return false; /* a sleeping redistributor forwards nothing */

	for (id = 0; id < VGIC_NR_IRQS; id++) {
		const struct vgic_irq *q = irq_get(v, (int)vcpu_idx, id);
		uint32_t grp_en;

		if (!(q->flags & VGIC_F_ENABLED) || (q->flags & VGIC_F_ACTIVE) ||
		    !irq_pending(q))
			continue;
		grp_en = (q->flags & VGIC_F_GROUP1) ? GICD_CTLR_ENGRP1 : GICD_CTLR_ENGRP0;
		if (!(v->ctlr & grp_en))
			continue;
		if (id >= 32 && vgic_route_target(v, id) != (int)vcpu_idx)
			continue;
		if (!found || q->prio < best) { /* ties: lowest INTID (ascending scan) */
			found = true;
			best = q->prio;
			if (intid)
				*intid = id;
			if (prio)
				*prio = q->prio;
		}
	}
	if (v->ctlr & GICD_CTLR_ENGRP1) {
		const struct vgic_vcpu *c = &v->vcpu[vcpu_idx];

		for (unsigned i = 0; i < VGIC_LPI_QUEUE; i++) {
			if (!c->lpi[i].id || c->lpi[i].inflight)
				continue;
			if (!found || c->lpi[i].prio < best ||
			    (c->lpi[i].prio == best && intid && *intid >= VGIC_LPI_BASE && c->lpi[i].id < *intid)) {
				found = true;
				best = c->lpi[i].prio;
				if (intid)
					*intid = c->lpi[i].id;
				if (prio)
					*prio = c->lpi[i].prio;
			}
		}
	}
	return found;
}

/* Entry with this id (0 = free slot) in the given state: in flight or not. */
static struct vgic_lpi *lpi_find(struct vgic *v, unsigned vcpu_idx, uint32_t intid, bool inflight)
{
	struct vgic_vcpu *c = &v->vcpu[vcpu_idx];

	for (unsigned i = 0; i < VGIC_LPI_QUEUE; i++)
		if (c->lpi[i].id == intid && c->lpi[i].inflight == inflight)
			return &c->lpi[i];
	return NULL;
}

bool vgic_lpi_inject(struct vgic *v, unsigned vcpu_idx, uint32_t intid, uint8_t prio)
{
	struct vgic_lpi *e;

	if (vcpu_idx >= v->nr_vcpus || intid < VGIC_LPI_BASE || intid >= (1u << v->idbits))
		return false;
	if (lpi_find(v, vcpu_idx, intid, false))
		return true;                     /* already pending (the pending bit is idempotent) */
	e = lpi_find(v, vcpu_idx, 0, false);     /* new, or a second occurrence while one is in flight */
	if (!e) {
		v->vcpu[vcpu_idx].lpi_dropped++;
		return false;
	}
	e->id = intid;
	e->prio = prio;
	e->inflight = false;
	return true;
}

unsigned vgic_idbits(const struct vgic *v) { return v->idbits; }

uint32_t vgic_lpi_dropped(const struct vgic *v, unsigned vcpu_idx)
{
	return vcpu_idx < v->nr_vcpus ? v->vcpu[vcpu_idx].lpi_dropped : 0;
}

bool vgic_lpi_enabled(const struct vgic *v, unsigned vcpu_idx)
{
	return vcpu_idx < v->nr_vcpus && v->lpis && (v->vcpu[vcpu_idx].rd_ctlr & 1u);
}

uint64_t vgic_lpi_propbaser(const struct vgic *v, unsigned vcpu_idx)
{
	return vcpu_idx < v->nr_vcpus ? v->vcpu[vcpu_idx].propbaser : 0;
}

uint32_t vgic_ack(struct vgic *v, unsigned vcpu_idx)
{
	uint32_t id;
	struct vgic_irq *q;

	if (!vgic_next_pending(v, vcpu_idx, &id, NULL))
		return VGIC_INTID_SPURIOUS;
	if (id >= VGIC_LPI_BASE) {
		struct vgic_vcpu *c = &v->vcpu[vcpu_idx];

		for (unsigned i = 0; i < VGIC_LPI_QUEUE; i++)
			if (c->lpi[i].id == id && !c->lpi[i].inflight) {
				c->lpi[i].inflight = true;
				return id;
			}
		return VGIC_INTID_SPURIOUS;
	}
	q = irq_get(v, (int)vcpu_idx, id);
	q->flags &= (uint8_t)~VGIC_F_PENDING;
	q->flags |= VGIC_F_ACTIVE;
	return id;
}

bool vgic_eoi(struct vgic *v, unsigned vcpu_idx, uint32_t intid)
{
	struct vgic_irq *q;

	if (intid >= VGIC_LPI_BASE) {            /* LPI: forget the in-flight entry */
		struct vgic_vcpu *c = &v->vcpu[vcpu_idx];

		for (unsigned i = 0; i < VGIC_LPI_QUEUE; i++)
			if (c->lpi[i].id == intid && c->lpi[i].inflight) {
				c->lpi[i].id = 0;
				c->lpi[i].inflight = false;
				return true;
			}
		return false;
	}
	q = irq_get(v, (int)vcpu_idx, intid);
	if (!q)
		return false;
	q->flags &= (uint8_t)~VGIC_F_ACTIVE;
	return true;
}

bool vgic_requeue(struct vgic *v, unsigned vcpu_idx, uint32_t intid)
{
	struct vgic_irq *q;

	if (intid >= VGIC_LPI_BASE) {            /* LPI: the pending bit is simply restored */
		struct vgic_vcpu *c = &v->vcpu[vcpu_idx];

		for (unsigned i = 0; i < VGIC_LPI_QUEUE; i++)
			if (c->lpi[i].id == intid && c->lpi[i].inflight) {
				if (lpi_find(v, vcpu_idx, intid, false)) {   /* a pending copy exists: merge */
					c->lpi[i].id = 0;
					c->lpi[i].inflight = false;
				} else {
					c->lpi[i].inflight = false;
				}
				return true;
			}
		return false;
	}
	q = irq_get(v, (int)vcpu_idx, intid);
	if (!q || !(q->flags & VGIC_F_ACTIVE))
		return false;
	q->flags &= (uint8_t)~VGIC_F_ACTIVE;
	if (intid < 16 || (q->flags & VGIC_F_EDGE))
		q->flags |= VGIC_F_PENDING;
	return true;
}
