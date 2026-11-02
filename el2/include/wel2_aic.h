/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - pirq backend for the Apple AIC (AICv1, t8103), issue #59.
 *
 * Register-level code behind a small MMIO accessor, so the same file runs on the host
 * against tests/fake_aic.h and on the M1 against the real block (the boot path supplies
 * an accessor over the mapped AIC window; it is NOT wired in yet, QEMU has a GIC: M3a).
 *
 * Register map (AIC base 0x23b100000 on the t8103, window 0x8000). Every offset is a hardware
 * fact taken from public documentation only (Asahi Linux docs "Apple Interrupt Controller",
 * Linux DT binding apple,aic.yaml, our notes re/f0-m1-fiq-irq.md and re/f0-qemu-aic-models.md);
 * no driver code is copied. Status marks are per row:
 *
 *   0x0004 INFO         NR_IRQ in [15:0]                      [verified: re/f0-qemu-aic-models 5;
 *                                                              Asahi doc lists it as "NR_IRQ?"]
 *   0x2000 WHOAMI       index of the reading core             [verified: re/f0-m1-fiq-irq 4.1]
 *   0x2004 EVENT        read = type[31:16] | number[15:0], 0 = nothing pending; the delivered
 *                       source is masked by the read          [verified: Asahi doc "HW irq flow"]
 *   0x2008 IPI_SEND     legacy IPI: bit n = "other" IPI to core n, bit 31 = "self"
 *   0x200c IPI_ACK      bit 0 other, bit 31 self              [verified: Asahi doc "IPI flow"]
 *   0x2024 IPI_MASK_SET / 0x2028 IPI_MASK_CLR                 [verified: Asahi doc]
 *   0x3000 TARGET_CPU   one 32-bit register per IRQ, bit n = deliver to core n
 *                                                             [verified: Asahi doc "IRQ_TGT"]
 *   0x4000 SW_SET / 0x4080 SW_CLR   one bit per IRQ, ORed with the hardware line
 *                                                             [verified: Asahi doc "SW_GEN_*"]
 *   0x4100 MASK_SET / 0x4180 MASK_CLR  one bit per IRQ; write 1 = set/clear, SET reads the state
 *                                                             [verified: Asahi doc "IRQ_MASK_*"]
 *   0x4200 HW_IRQ_MON   current hardware line state           [verified offset, meaning is a
 *                                                              "?" in the Asahi doc: hypothesis]
 *   EVENT type 1 = hardware IRQ, 4 = legacy IPI (number 1 other, 2 self)   [verified: re notes]
 *
 * Not verified (E-HW0, M3a): reset value of MASK/TARGET_CPU, whether the EVENT read clears a
 * SW_SET bit, whether an edge pulse is latched while the source is masked, order of IPI vs IRQ
 * events, WHOAMI vs MPIDR numbering of the P cores, the exact INFO field widths.
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

#define AIC_EVENT_TYPE(e)   (((e) >> 16) & 0xFFFFu)
#define AIC_EVENT_NUM(e)    ((e) & 0xFFFFu)
#define AIC_EVENT_TYPE_HW   1u
#define AIC_EVENT_TYPE_IPI  4u
#define AIC_EVENT_IPI_OTHER 1u
#define AIC_EVENT_IPI_SELF  2u

#define AIC_IPI_OTHER       (1u << 0)
#define AIC_IPI_SELF        (1u << 31)

#define AIC_MAX_IRQS        1024u          /* tracked sources (t8103 has 896 wired + MSI pool) */
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
};

struct pirq_aic_stats {
	uint32_t events;       /* EVENT reads that returned something */
	uint32_t spurious;     /* EVENT reads that returned 0 */
	uint32_t ipi_unexpected;
	uint32_t bad;          /* unknown type or number out of range */
	uint32_t trigger_noop; /* set_trigger calls (the AIC has no trigger register) */
	uint32_t bad_vcpu;     /* set_target with a vCPU that has no core */
};

struct pirq_aic {
	struct aic_io io;
	const struct intr_map *map;
	uint32_t nr_irq;                          /* from INFO, clamped to AIC_MAX_IRQS */
	unsigned nr_vcpus;
	uint8_t cpu_of_vcpu[AIC_MAX_CPUS];        /* vCPU index -> AIC core index (plat.pcpu_of_vcpu) */
	struct pirq_ops ops;
	struct pirq_aic_stats st;
};

/* Reads INFO, masks every source, clears every software bit, points every TARGET_CPU at the
 * core of vCPU 0, masks and acks the legacy IPIs. cpu_of_vcpu[] has nr_vcpus entries (a core
 * index < AIC_MAX_CPUS each). Returns 0, or -1 on a bad argument or an implausible INFO. */
int pirq_aic_init(struct pirq_aic *a, const struct aic_io *io, const struct intr_map *map,
		  const uint8_t *cpu_of_vcpu, unsigned nr_vcpus);
/* The ops table for pirq_init() / pirq_guard_init(); valid as long as a is. */
const struct pirq_ops *pirq_aic_ops(struct pirq_aic *a);

/* One EVENT read on the calling core. A legacy IPI is acked and left masked here. */
struct aic_event pirq_aic_poll(struct pirq_aic *a);

/* Software generate / clear a source (tests, irq_set_irqchip_state-like uses). */
void pirq_aic_sw_set(struct pirq_aic *a, uint32_t irq);
void pirq_aic_sw_clr(struct pirq_aic *a, uint32_t irq);
/* Current hardware line of a source (HW_IRQ_MON), for diagnostics. */
bool pirq_aic_line(struct pirq_aic *a, uint32_t irq);

/* Hooks for the legacy IPI path, kept only as clean entry points: the t8103 uses the fast IPI
 * system registers, which are a separate design item (#46). */
void pirq_aic_ipi_send(struct pirq_aic *a, uint32_t mask);
void pirq_aic_ipi_unmask(struct pirq_aic *a, uint32_t mask);

#endif
