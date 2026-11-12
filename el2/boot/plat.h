/* SPDX-License-Identifier: MIT */
/* Watari EL2 T6c (PLAN S8): the platform seam. Everything the layer does to platform hardware (the interrupt
 * controller, the PSCI firmware, the reset/power controls) is a method of the one table g_plat points to; the
 * generic code (main.c, cpuif.c, psci.c, pirq_qemu.c) never touches the QEMU GICv3 or issues an SMC itself.
 *   plat_qemu (plat_qemu.c): exactly the behaviour the layer always had on the QEMU virt machine (physical GICv3,
 *                            PSCI by SMC to QEMU's firmware).
 *   plat_m1   (plat_m1.c):   the Apple M1 (AIC, FIQ timer/IPI, spin-table cores, watchdog). The methods the later
 *                            tasks fill (T6d AIC IRQ path, T6e FIQ module, T6f cores/PSCI) stop with
 *                            "M1 platform: not implemented yet: <hook>".
 * The table lives in .data and holds function pointers, so it is relocated by start.S like every other pointer of the
 * PIE image; nothing here computes an absolute address before the relocation has run. Selection happens once, in
 * stage1_build (MMU off, boot core), from the FDT: plat_select(). */
#ifndef WEL2_PLAT_H
#define WEL2_PLAT_H

#include <stdbool.h>
#include <stdint.h>
#include "boot.h"
#include "fdt_m1.h"

/* The guest-visible vGIC windows (INTERFACES 4). QEMU: the virt machine's GICv3 addresses, as always. M1: an
 * address range no device of the SoC uses, above the RAM. */
#define PLAT_QEMU_GICD_BASE  0x08000000ULL
#define PLAT_QEMU_GICR_BASE  0x080A0000ULL
#define PLAT_M1_GICD_BASE    0xF00000000ULL
#define PLAT_M1_GICR_BASE    0xF10000000ULL

struct wel2_platform {
	const char *name;
	bool m1_memory;                 /* memory maps come from the FDT lists (RAM-only guest stage 2, no Device blanket) */
	unsigned s2_ipa_bits;           /* guest IPA size: QEMU 40 (EDK2 uses the high PCIe windows), M1 36 (the measured PARange; the
	                                 * vGIC windows at 0xF_xxxx_xxxx lie inside 64 GB) */
	bool has_phys_gic;              /* a physical GICv3 is owned by EL2 (false: AIC) */
	uint64_t vgic_gicd_base;        /* guest-visible vGIC distributor window */
	uint64_t vgic_gicr_base;        /* guest-visible vGIC redistributor window of CPU 0 */

	/* ---- boot ---- */
	unsigned (*discover_cpus)(void);                    /* fills g_pcpu[].aff/.gicr, returns the core count */
	void (*pirq_init)(struct vgic *v, unsigned ncpu, uint64_t fdt);   /* SPI policy backend, before vgic_init */
	void (*init_boot)(void);                            /* boot core: the interrupt controller's shared part */
	int  (*init_cpu)(unsigned cpu);                     /* every core: its own interface; 0, or -1 (per-core check failed) */
	void (*cpu_down)(void);                             /* CPU_OFF: this core's interface to sleep */
	bool (*has_lpis)(void);                             /* vGIC capability queries */
	unsigned (*idbits)(void);
	void (*lpi_reg)(unsigned cpu, unsigned off, uint64_t val);   /* guest ITS/redistributor LPI programming */

	/* ---- interrupts (EL2 IRQ/FIQ, big lock held) ---- */
	uint32_t (*irq_ack)(void);                          /* next pending INTID, 1020..1023 = none */
	void (*irq_eoi)(uint32_t intid);
	void (*irq_post)(unsigned me);                      /* after irq_ack reported nothing: drain a second source (AIC); may be NULL */
	void (*ppi_enable)(unsigned ppi, bool on);          /* mask/unmask a private line of this core (timer, maintenance) */
	void (*kick)(unsigned cpu);                         /* IPI to another core: "re-evaluate your vGIC" */
	void (*fiq)(struct frame *f);                       /* vector 10 (guest FIQ); NULL = unexpected, fatal */
	void (*spi_enable)(uint32_t intid, bool on);        /* shared lines, for the SPI policy (pirq) */
	void (*spi_route)(uint32_t intid, int vcpu);
	void (*spi_trigger)(uint32_t intid, bool edge);

	/* ---- power and reset (the platform side of guest PSCI) ---- */
	/* psci.c has checked target and entry; entry/ctx are the guest's (QEMU: the firmware gets secondary_entry and the
	 * layer keeps entry/ctx in g_pcpu[]; M1: the platform stores them itself, T6f). Returns a PSCI code (QEMU:
	 * ALREADY_ON = retry, M1: passed to the guest as is). */
	int64_t (*cpu_start)(uint64_t target_mpidr, unsigned idx, uint64_t entry, uint64_t ctx);
	int64_t (*cpu_off)(void);                           /* this core powers down; returns only on failure */
	void (*system_off)(void);                           /* returns only on failure */
	void (*system_reset)(void);                         /* returns only on failure */

	/* ---- T6d/T6e/T6f wiring; every one of these may be NULL (QEMU: NULL, the generic behaviour applies) ---- */
	bool native_psci;                                   /* no PSCI firmware (M1): psci.c uses the hooks below, strips MPIDR bits 31/30/24 */
	/* CPU_OFF step 1, big lock held, BEFORE any teardown: SUCCESS (the platform now regards the core as OFF), or the
	 * PSCI error to return to the guest with nothing torn down. NULL: the generic "last core" rule of psci.c. */
	int64_t (*cpu_off_begin)(unsigned me);
	/* A secondary core is about to eret (big lock held): the entry point and context of its start. false = the core
	 * was not started by a CPU_ON (stray): it must not eret. NULL: g_pcpu[me].entry/.ctx. */
	bool (*cpu_arrive)(unsigned me, uint64_t *entry, uint64_t *ctx);
	/* cpuif_flush(): make the vGIC timer lines follow the guest timers and reopen a closed gate (M1). NULL: the QEMU
	 * body (vtimer_line() + physical PPI 27). */
	void (*timer_resync)(unsigned me);
	/* After cpuif_flush() on the IRQ and FIQ exit tails: the vGIC maintenance backstop. Returns 0 (nothing to do),
	 * 1 (the maintenance enables were quenched: the cached ICH_HCR_EL2 is stale) or 2 (not clearable: fatal). */
	int (*maint_backstop)(unsigned me);
	void (*stats)(void);                                /* cpuif_stats(): platform counters */
};

/* The M1 PSCI state (plat_m1.c), used by psci.c when g_plat->native_psci. */
struct wel2_cs;
extern struct wel2_cs g_cs;

extern const struct wel2_platform plat_qemu;
extern const struct wel2_platform plat_m1;
extern const struct wel2_platform *g_plat;            /* set by stage1_build, read by everyone after */

/* The platform for a parsed FDT: plat_m1 when fdt_m1_parse() returned 0 and found the AIC node (p->is_m1), plat_qemu
 * otherwise, also when the parse failed (rc != 0): a blob the layer cannot read is not recognised as an M1. */
const struct wel2_platform *plat_select(int parse_rc, const struct m1_platform *p);

#endif
