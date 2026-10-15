/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - virtual GICv3 distributor/redistributor model (pure state).
 *
 * Written from the GICv3 architecture description in
 * docs/design/watari-el2/INTERFACES.md sections 5-6. No hardware access:
 * the model only keeps architectural state and reports side effects through
 * the optional hooks below. Freestanding-compatible (stdint/stdbool/stddef).
 */
#ifndef WEL2_VGIC_H
#define WEL2_VGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VGIC_MAX_VCPUS        8
#define VGIC_NR_IRQS          960u   /* ITLinesNumber = 29 -> 32 * 30 INTIDs */
#define VGIC_NR_SPIS          (VGIC_NR_IRQS - 32u)
#define VGIC_INTID_SPURIOUS   1023u

#define VGIC_GICD_SIZE        0x10000u
#define VGIC_GICR_FRAME_SIZE  0x20000u  /* RD_base 64 KB + SGI_base 64 KB */
#define VGIC_GICR_SGI_OFFSET  0x10000u

/* Return codes of the MMIO entry points. */
#define VGIC_OK               0
#define VGIC_ERR_ACCESS      (-1)  /* misaligned or disallowed size (guest bug) */
#define VGIC_ERR_RANGE       (-2)  /* address outside GICD and the GICR window */

enum vgic_log_kind {
	VGIC_LOG_UNIMPL = 1,   /* unimplemented offset: read as 0 / write ignored */
	VGIC_LOG_IGNORED,      /* known register whose writes are ignored (log-once candidate) */
	VGIC_LOG_BAD_ACCESS    /* misaligned / illegal size / out of range */
};

enum vgic_effect {
	VGIC_EFFECT_UNMASK_SPI = 1,  /* SPI enabled by the guest (arg unused) */
	VGIC_EFFECT_MASK_SPI,        /* SPI disabled by the guest (arg unused) */
	VGIC_EFFECT_SET_TARGET       /* arg = target vCPU index, or -1 if none */
};

struct vgic_hooks {
	void (*log)(void *ctx, enum vgic_log_kind kind, uint64_t addr,
		    unsigned size, bool is_write, uint64_t val);
	void (*effect)(void *ctx, enum vgic_effect eff, uint32_t intid, int arg);
	void *ctx;
};

/* Per-interrupt state flags. */
#define VGIC_F_ENABLED  0x01u
#define VGIC_F_PENDING  0x02u  /* latched pending (edge / software) */
#define VGIC_F_ACTIVE   0x04u
#define VGIC_F_LINE     0x08u  /* current input level */
#define VGIC_F_EDGE     0x10u  /* ICFGR: edge-triggered */
#define VGIC_F_GROUP1   0x20u

struct vgic_irq {
	uint8_t prio;
	uint8_t flags;
};

struct vgic_vcpu {
	uint32_t aff;                 /* Aff3.Aff2.Aff1.Aff0 packed in 32 bits */
	bool sleep;                   /* GICR_WAKER.ProcessorSleep */
	struct vgic_irq priv[32];     /* SGIs 0-15 and PPIs 16-31 */
};

struct vgic_config {
	uint64_t gicd_base;
	uint64_t gicr_base;
	unsigned nr_vcpus;                       /* 1..VGIC_MAX_VCPUS */
	const uint32_t *affinity;                /* per vCPU, or NULL: aff = index */
	struct vgic_hooks hooks;
};

struct vgic {
	uint64_t gicd_base;
	uint64_t gicr_base;
	unsigned nr_vcpus;
	uint32_t ctlr;                           /* GICD_CTLR stored bits (EnableGrp0/1) */
	uint64_t irouter[VGIC_NR_SPIS];
	struct vgic_irq spi[VGIC_NR_SPIS];
	struct vgic_vcpu vcpu[VGIC_MAX_VCPUS];
	struct vgic_hooks hooks;
};

/* Returns 0 on success, -1 on invalid configuration. Leaves the vGIC at reset. */
int  vgic_init(struct vgic *v, const struct vgic_config *cfg);
void vgic_reset(struct vgic *v);

/* addr is a guest-visible address inside the GICD or GICR window. */
int vgic_mmio_read(struct vgic *v, uint64_t addr, unsigned size, uint64_t *val);
int vgic_mmio_write(struct vgic *v, uint64_t addr, unsigned size, uint64_t val);

/* Drive an input line (PPI 16-31 on vcpu_idx, or SPI; vcpu_idx ignored for SPIs). */
int  vgic_set_irq_level(struct vgic *v, unsigned vcpu_idx, uint32_t intid, bool level);
/* Latch pending state (software set-pending; also used for SGIs). */
int  vgic_set_pending(struct vgic *v, unsigned vcpu_idx, uint32_t intid);

/* vCPU an SPI is routed to (IROUTER), or -1. */
int  vgic_route_target(const struct vgic *v, uint32_t intid);

/* Highest-priority pending+enabled+inactive interrupt deliverable to vcpu_idx. */
bool vgic_next_pending(const struct vgic *v, unsigned vcpu_idx,
		       uint32_t *intid, uint8_t *prio);

/* Acknowledge: returns the INTID (now active, latch cleared) or 1023 if none. */
uint32_t vgic_ack(struct vgic *v, unsigned vcpu_idx);
/* End of interrupt (priority drop + deactivate). Returns false for a bad INTID. */
bool vgic_eoi(struct vgic *v, unsigned vcpu_idx, uint32_t intid);

#endif
