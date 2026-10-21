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
	VGIC_EFFECT_SET_TARGET,      /* arg = target vCPU index, or -1 if none */
	VGIC_EFFECT_SET_TRIGGER      /* GICD_ICFGR of an SPI changed: arg = 1 edge, 0 level (M2.x-1: v2m MSIs are edge) */
};

struct vgic_hooks {
	void (*log)(void *ctx, enum vgic_log_kind kind, uint64_t addr,
		    unsigned size, bool is_write, uint64_t val);
	void (*effect)(void *ctx, enum vgic_effect eff, uint32_t intid, int arg);
	void *ctx;
	/* LPI pass-through (M2): the guest wrote a redistributor LPI register of vCPU cpu
	 * (off = GICR_CTLR 0x0, PROPBASER 0x70, PENDBASER 0x78, INVLPIR 0xA0, INVALLR 0xB0).
	 * Optional; the model keeps the value, the layer applies it to the physical GICR. */
	void (*lpi_reg)(void *ctx, unsigned cpu, unsigned off, uint64_t val);
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

#define VGIC_LPI_BASE    8192u   /* first LPI INTID (GICv3: 8192) */
#define VGIC_LPI_QUEUE   64u     /* LPIs the layer can hold per vCPU before it must drop one */

struct vgic_lpi {
	uint32_t id;                  /* 0 = free slot */
	uint8_t prio;
	bool inflight;                /* handed to the guest (vgic_ack), not yet EOI'd */
};

struct vgic_vcpu {
	uint32_t rd_ctlr;             /* GICR_CTLR.EnableLPIs (bit 0), LPI mode only */
	uint64_t propbaser, pendbaser;
	struct vgic_lpi lpi[VGIC_LPI_QUEUE];
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
	bool lpis;                               /* expose LPIs (GICD_TYPER.LPIS, GICR_TYPER.PLPIS) */
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
	bool lpis;
};

/* Returns 0 on success, -1 on invalid configuration. Leaves the vGIC at reset. */
int  vgic_init(struct vgic *v, const struct vgic_config *cfg);
void vgic_reset(struct vgic *v);
/* Reset the private (SGI/PPI) state and WAKER of one vCPU: PSCI CPU_OFF. */
void vgic_vcpu_reset(struct vgic *v, unsigned idx);
/* Hand-off to a guest that never does the GICR_WAKER handshake (EDK2's ArmGicV3
 * driver assumes awake redistributors, F-21): mark the vCPU awake. */
void vgic_vcpu_wake(struct vgic *v, unsigned idx);

/* addr is a guest-visible address inside the GICD or GICR window. */
int vgic_mmio_read(struct vgic *v, uint64_t addr, unsigned size, uint64_t *val);
int vgic_mmio_write(struct vgic *v, uint64_t addr, unsigned size, uint64_t val);

/* Drive an input line (PPI 16-31 on vcpu_idx, or SPI; vcpu_idx ignored for SPIs). */
int  vgic_set_irq_level(struct vgic *v, unsigned vcpu_idx, uint32_t intid, bool level);
/* Latch pending state (software set-pending; also used for SGIs). */
int  vgic_set_pending(struct vgic *v, unsigned vcpu_idx, uint32_t intid);

/*
 * Software-generated interrupt from a guest write of ICC_SGI1R_EL1 (GIC spec
 * IHI 0069H.b, 12.2.22): INTID [27:24], TargetList [15:0], Aff1 [23:16],
 * Aff2 [39:32], Aff3 [55:48], IRM [40], RS [47:44]. Latches the SGI pending on
 * every target vCPU (IRM=1: all except sender_idx). Returns the number of
 * vCPUs targeted; targets that do not exist are ignored. The sender is
 * reported to no one (GICv3 SGIs carry no source). Group is not checked
 * (DS=1 model). Delivery still depends on enable/priority/group enable.
 */
int  vgic_sgi_send(struct vgic *v, unsigned sender_idx, uint64_t sgi1r);
/* Same, but returns the bitmap of targeted vCPUs (bit i = vCPU index i), so the
 * caller can kick the physical cores that run them. */
uint32_t vgic_sgi_send_mask(struct vgic *v, unsigned sender_idx, uint64_t sgi1r);

/* vCPU an SPI is routed to (IROUTER), or -1. */
int  vgic_route_target(const struct vgic *v, uint32_t intid);

/* LPI pass-through: a physical LPI arrived for vCPU idx (property-table priority prio).
 * Queued until the guest acknowledges it; returns false (and drops it) when the queue is full. */
bool vgic_lpi_inject(struct vgic *v, unsigned vcpu_idx, uint32_t intid, uint8_t prio);
/* Value the guest last wrote to GICR_PROPBASER of vCPU idx (0 if never). */
/* GICR_CTLR.EnableLPIs of vCPU idx as the guest set it. */
bool vgic_lpi_enabled(const struct vgic *v, unsigned vcpu_idx);
uint64_t vgic_lpi_propbaser(const struct vgic *v, unsigned vcpu_idx);

/* Highest-priority pending+enabled+inactive interrupt deliverable to vcpu_idx. */
bool vgic_next_pending(const struct vgic *v, unsigned vcpu_idx,
		       uint32_t *intid, uint8_t *prio);

/* Acknowledge: returns the INTID (now active, latch cleared) or 1023 if none. */
uint32_t vgic_ack(struct vgic *v, unsigned vcpu_idx);
/* End of interrupt (priority drop + deactivate). Returns false for a bad INTID. */
bool vgic_eoi(struct vgic *v, unsigned vcpu_idx, uint32_t intid);
/* Take back an interrupt that vgic_ack() handed out but the guest has not
 * acknowledged yet (LR eviction, #54): active is cleared and, for SGIs and
 * edge-triggered interrupts, the pending latch that the ack consumed is
 * restored; a level interrupt becomes pending again through its line. Returns
 * false if the INTID is bad or not active in the model. */
bool vgic_requeue(struct vgic *v, unsigned vcpu_idx, uint32_t intid);

#endif
