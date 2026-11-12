/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - pirq backend for the Apple AIC (AICv1, t8103), issue #59.
 *
 * Register-level code behind a small MMIO accessor, so the same file runs on the host
 * against tests/fake_aic.h and on the M1 against the real block (the boot path supplies
 * an accessor over the mapped AIC window; it is NOT wired in yet, QEMU has a GIC: M3a).
 *
 * Register map (AIC base 0x23b100000 on the t8103; the ADT gives reg[0] size 0xc000 and reg[1]
 * 0x23b108000 size 0x1000, INFO = 0x000a0380, NR_IRQ 896: measured on j293 2026-11-10,
 * re/m3a-ehw0-measurements.md; this header used to say window 0x8000). Every offset is a hardware
 * fact taken from public documentation only (Asahi Linux docs "Apple Interrupt Controller",
 * Linux DT binding apple,aic.yaml, our notes re/f0-m1-fiq-irq.md and re/f0-qemu-aic-models.md);
 * no driver code is copied. Status marks are per row:
 *
 *   0x0004 INFO         NR_IRQ in [15:0]                      [offset: Asahi doc, which marks it
 *                                                              "NR_IRQ?"; field position: Linux
 *                                                              driver register definitions]
 *   0x2000 WHOAMI       index of the reading core             [verified: Linux driver register
 *                                                              definitions; used by its CPU check]
 *   0x2004 EVENT        read = die[31:24] | type[23:16] | number[15:0], 0 = nothing pending;
 *                       the delivered source is masked by the read
 *                       [verified: Asahi doc "HW irq flow" for the masking; field layout and
 *                        the die field: Linux driver register definitions, hardware facts]
 *   0x2008 IPI_SEND     legacy IPI: bit n = "other" IPI to core n, bit 31 = "self"
 *   0x200c IPI_ACK      bit 0 other, bit 31 self              [verified: Asahi doc "IPI flow"]
 *   0x2024 IPI_MASK_SET / 0x2028 IPI_MASK_CLR                 [verified: Asahi doc]
 *   0x3000 TARGET_CPU   one 32-bit register per IRQ, bit n = deliver to core n
 *                                                             [verified: Asahi doc "IRQ_TGT"]
 *   0x4000 SW_SET / 0x4080 SW_CLR   one bit per IRQ, ORed with the hardware line
 *                                                             [verified: Asahi doc "SW_GEN_*"]
 *   0x4100 MASK_SET / 0x4180 MASK_CLR  one bit per IRQ; write 1 = set/clear
 *                                                             [verified: Asahi doc "IRQ_MASK_*"]
 *                       a MASK_SET read returns the current mask state [verified: measured
 *                       on j293 2026-11-10], never relied on by this code
 *   0x4200 HW_IRQ_MON   current hardware line state           [verified offset, meaning is a
 *                                                              "?" in the Asahi doc: hypothesis]
 *   EVENT type 1 = hardware IRQ, 4 = legacy IPI (number 1 other, 2 self)
 *                                  [verified: Linux driver defines, as hardware facts]
 *   IPI_ACK / IPI_MASK_SET / IPI_MASK_CLR are per-core views: run pirq_aic_init_cpu() on every core.
 *
 * Measured on j293 2026-11-10 (re/m3a-ehw0-measurements.md, day 2 section), IRQ 891 routed to
 * core 7: SW_SET on a masked source delivers nothing; after MASK_CLR the EVENT read returns
 * 0x0001037b (die 0, type 1, number 891) and re-masks the source; the EVENT read does NOT clear
 * SW_SET, so the source fires again on the next unmask until SW_CLR (level-like); no spurious
 * event after SW_CLR + unmask; MASK_SET and SW_SET reads return the current state; WHOAMI = 0..7
 * with E cores 0-3 and P cores 4-7; TARGET_CPU and MASK reset values seen: 0 (or 1 for IRQ 0) and
 * all masked.
 * Still not verified: whether a hardware edge pulse is latched while the source is masked (needs
 * a real edge source), order of IPI vs IRQ events, the INFO field above bit 15.
 *
 * What does NOT go through here (ARCHITECTURE devirq 4.5): the guest timers and the PMU are
 * FIQ sources, the fast IPI is a system register on the t8103; none of them is an AIC EVENT.
 * A legacy-IPI event is unexpected on this layer: it is contained (acked, left masked) and
 * counted, never forwarded.
 */
#ifndef WEL2_AIC_H
#define WEL2_AIC_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_intr_map.h"
#include "wel2_pirq.h"

/* AIC windows of the t8103 as the ADT lists them [verified: re/m3a-ehw0-measurements.md row 1,
 * measured on j293 2026-11-10]: reg[0] = 0x23b100000 size 0xc000, reg[1] = 0x23b108000 size 0x1000.
 * Range 1 lies INSIDE range 0 (0x23b108000 + 0x1000 <= 0x23b100000 + 0xc000), so the union is the
 * single window [0x23b100000, 0x23b10c000): the layer maps it Device for itself (EL2 stage 1 maps
 * everything below RAM as Device) and the guest stage 2 gets ONE hole for it; a second hole would be
 * redundant, and a 4 KB hole cannot even be expressed with the 16 KB granule (wel2_s2_add_hole()
 * rounds outwards and drops a range already covered). */
#define AIC_T8103_BASE      0x23b100000ull
#define AIC_T8103_SIZE0     0xc000ull
#define AIC_T8103_BASE1     0x23b108000ull
#define AIC_T8103_SIZE1     0x1000ull

#define AIC_INFO            0x0004u
#define AIC_INFO_NR_IRQ(v)  ((v) & 0xFFFFu)
#define AIC_WHOAMI          0x2000u
#define AIC_EVENT           0x2004u
#define AIC_IPI_SEND        0x2008u
#define AIC_IPI_ACK         0x200cu
#define AIC_IPI_MASK_SET    0x2024u
#define AIC_IPI_MASK_CLR    0x2028u
#define AIC_TARGET_CPU(n)   (0x3000u + 4u * (n))
#define AIC_SW_SET(n)       (0x4000u + 4u * ((n) / 32))
#define AIC_SW_CLR(n)       (0x4080u + 4u * ((n) / 32))
#define AIC_MASK_SET(n)     (0x4100u + 4u * ((n) / 32))
#define AIC_MASK_CLR(n)     (0x4180u + 4u * ((n) / 32))
#define AIC_HW_IRQ_MON(n)   (0x4200u + 4u * ((n) / 32))

#define AIC_EVENT_TYPE(e)   (((e) >> 16) & 0xFFu)
#define AIC_EVENT_DIE(e)    (((e) >> 24) & 0xFFu)
#define AIC_EVENT_NUM(e)    ((e) & 0xFFFFu)
#define AIC_EVENT_TYPE_HW   1u
#define AIC_EVENT_TYPE_IPI  4u
#define AIC_EVENT_IPI_OTHER 1u
#define AIC_EVENT_IPI_SELF  2u

#define AIC_IPI_OTHER       (1u << 0)
#define AIC_IPI_SELF        (1u << 31)

#define AIC_MAX_IRQS        1024u          /* tracked sources (t8103: 896 wired + pool is [hypothesis], design number) */
#define AIC_MAX_CPUS        8u             /* t8103: E0-E3, P0-P3 */

/* The only way this module touches hardware. off is relative to the AIC base. */
struct aic_io {
	uint32_t (*rd32)(void *ctx, uint32_t off);
	void (*wr32)(void *ctx, uint32_t off, uint32_t val);
	void *ctx;
};

enum aic_ev_kind {
	AIC_EV_NONE = 0,       /* EVENT read 0: nothing pending (also a spurious wake) */
	AIC_EV_IRQ,            /* hardware IRQ number in .num, source now masked by the AIC */
	AIC_EV_IPI,            /* legacy IPI: unexpected, already contained */
	AIC_EV_BAD,            /* unknown type, or IRQ number >= NR_IRQ: counted, nothing to do */
};

struct aic_event {
	enum aic_ev_kind kind;
	uint32_t raw;          /* the EVENT value */
	uint32_t num;          /* IRQ number (AIC_EV_IRQ), IPI number (AIC_EV_IPI) */
	uint32_t die;          /* die field of the event (0 on the t8103; non-zero is AIC_EV_BAD) */
};

struct pirq_aic_stats {
	uint32_t events;       /* EVENT reads that returned something */
	uint32_t spurious;     /* EVENT reads that returned 0 */
	uint32_t ipi_unexpected;
	uint32_t bad;          /* unknown type or number out of range */
	uint32_t trigger_noop; /* set_trigger calls (the AIC has no trigger register) */
	uint32_t bad_vcpu;     /* set_target with a vCPU that has no core */
	uint32_t whoami_mismatch; /* pirq_aic_init_cpu: WHOAMI differs from the expected core */
	uint32_t sw_cleared;   /* SW_CLR written at take time for a source the layer had SW_SET */
};

struct pirq_aic {
	struct aic_io io;
	const struct intr_map *map;
	uint32_t nr_irq;                          /* from INFO, clamped to AIC_MAX_IRQS */
	unsigned nr_vcpus;
	uint8_t cpu_of_vcpu[AIC_MAX_CPUS];        /* vCPU index -> AIC core index (plat.pcpu_of_vcpu) */
	struct pirq_ops ops;
	struct pirq_aic_stats st;
	/* Sources the layer itself raised with pirq_aic_sw_set() and has not cleared yet (bit n = IRQ n).
	 * pirq_aic_poll() writes SW_CLR for exactly these when it takes them, see pirq_aic.c. */
	uint32_t sw_owned[AIC_MAX_IRQS / 32];
};

/* Reads INFO, masks every source, clears every software bit, points every TARGET_CPU at the
 * core of vCPU 0. cpu_of_vcpu[] has nr_vcpus entries (a core index < AIC_MAX_CPUS each).
 * Returns 0, or -1 on a bad argument or an implausible INFO. Per-core state (the IPI views)
 * is NOT touched here: call pirq_aic_init_cpu() on every core. */
int pirq_aic_init(struct pirq_aic *a, const struct aic_io *io, const struct intr_map *map,
		  const uint8_t *cpu_of_vcpu, unsigned nr_vcpus);
/* Per-core init, run ON the core that runs vCPU `vcpu`: acks and masks this core's legacy IPIs
 * (IPI_ACK / IPI_MASK_* are per-core views), and checks WHOAMI against cpu_of_vcpu[vcpu]
 * (design: E0-3 = 0-3, P0-3 = 4-7, [hypothesis] until #55). Returns 0, or -1 on a mismatch
 * (counted in st.whoami_mismatch; the TARGET_CPU bits derived from the table would then point
 * at the wrong core, so the caller must not go on) or a bad vCPU. */
int pirq_aic_init_cpu(struct pirq_aic *a, unsigned vcpu);
/* The ops table for pirq_init() / pirq_guard_init(); valid as long as a is. */
const struct pirq_ops *pirq_aic_ops(struct pirq_aic *a);

/* One EVENT read on the calling core. A legacy IPI is acked and left masked here. */
struct aic_event pirq_aic_poll(struct pirq_aic *a);

/* The drain loop of an IRQ taken on the calling core (T6d): EVENT reads until the AIC reports nothing, each hardware
 * source handed to pirq_arrive() as phys = d->key | hwirq. Shared by the M1 platform (key 0) and the QEMU hybrid
 * machine (key AIC_KEY, pirq_qemu.c). A delivered source is already masked by the EVENT read; pirq_arrive() decides
 * the rest. When the pirq backend does not report auto_masks (the hybrid machine's GIC rules), an edge source is
 * unmasked here, as the backend would. At most d->budget EVENT reads (0 = AIC_DRAIN_BUDGET) per call: a source that
 * keeps asserting keeps the CPU IRQ line high and the next exception drains again; budget_hit says it happened. */
#define AIC_DRAIN_BUDGET 64u
struct pirq_aic_drain {
	uint32_t key;                       /* ORed into the hwirq to form the pirq key */
	unsigned budget;
	/* Optional: called for every event that is not AIC_EV_NONE, after it was handled. vintid = what pirq_arrive()
	 * returned for AIC_EV_IRQ (INTR_INVALID: denied), 0 otherwise. */
	void (*note)(void *ctx, const struct aic_event *ev, uint32_t vintid);
	void *ctx;
};
struct aic_drain_result {
	unsigned polls;                     /* EVENT reads, including the one that returned nothing */
	unsigned irqs;                      /* hardware sources handed to pirq_arrive() */
	unsigned other;                     /* IPI / bad events (counted in a->st, nothing forwarded) */
	bool budget_hit;
};
struct aic_drain_result pirq_aic_drain(struct pirq_aic *a, struct pirq *p, unsigned vcpu,
				       const struct pirq_aic_drain *d);

/* Software generate / clear a source (tests, irq_set_irqchip_state-like uses). A source raised with
 * sw_set is cleared by pirq_aic_poll() when it is taken (the EVENT read does not clear SW_SET on the
 * M1, so the source would re-fire at every unmask). Callers on different cores need no lock:
 * sw_owned is updated with atomics in a fixed order (see pirq_aic.c); two sw_sets before a take are
 * one event (intended coalescing). */
void pirq_aic_sw_set(struct pirq_aic *a, uint32_t irq);
void pirq_aic_sw_clr(struct pirq_aic *a, uint32_t irq);
/* Current hardware line of a source (HW_IRQ_MON), for diagnostics. */
bool pirq_aic_line(struct pirq_aic *a, uint32_t irq);

/* Hooks for the legacy IPI path, kept only as clean entry points: the t8103 uses the fast IPI
 * system registers, which are a separate design item (#46). */
void pirq_aic_ipi_send(struct pirq_aic *a, uint32_t mask);
void pirq_aic_ipi_unmask(struct pirq_aic *a, uint32_t mask);

#endif
