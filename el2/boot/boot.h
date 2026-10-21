/* SPDX-License-Identifier: MIT */
/* Watari EL2 boot spike: shared declarations (M1-3). */
#ifndef WEL2_BOOT_H
#define WEL2_BOOT_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_vgic.h"
#include "wel2_pirq.h"

/* QEMU virt GICv3 windows (hw/arm/virt.c memmap). */
#define GICD_PHYS   0x08000000UL
#define GICD_SIZE   0x10000UL
#define GICR_PHYS   0x080A0000UL   /* redistributor frame of CPU 0 (RD_base) */
#define GICR_STRIDE 0x20000UL      /* RD_base + SGI_base frames */
#define MAX_CPUS    VGIC_MAX_VCPUS
/* Guest RAM (QEMU virt, up to -m 2G) and, in the M1-4 chain build, the part of it
 * the layer keeps for itself: the first WEL2_CARVE_SIZE of RAM, cut off the FDT
 * /memory node (INTERFACES 3.2). Inside it, [RAM base, layer image) is the FDT
 * hand-off buffer (mapped, read by the guest) and [layer image, carve end) is the
 * layer (image + stacks, unmapped in stage 2).
 * The patched FDT is copied to the RAM base: EDK2's early DebugLib reads it at
 * PcdDeviceTreeInitialBaseAddress = 0x40000000 (F-21). EDK2 itself is loaded right
 * after the carve-out, at the address an arm64 Image with text_offset 0x80000 would
 * have there. */
#define GUEST_RAM_BASE   0x40000000ULL
#define GUEST_RAM_END    0x140000000ULL   /* M2: stage-2 maps RAM up to 5 GB (a -m 4G machine ends at 0x13fffffff) */
#define WEL2_CARVE_SIZE  0x200000ULL
#define EDK2_FDT_ADDR    GUEST_RAM_BASE
#define EDK2_ENTRY       (GUEST_RAM_BASE + WEL2_CARVE_SIZE + 0x80000ULL)
#define PL031_PHYS  0x09010000UL   /* QEMU virt RTC, SPI 2 = INTID 34 */

/* Exception frame saved by start.S (layout fixed there). */
struct frame {
    uint64_t x[31];
    uint64_t esr, elr, spsr, far;
};

/* Per physical core == per vCPU (the vCPU index is the core index). */
#define CPU_ST_ON         0   /* values are the PSCI AFFINITY_INFO results */
#define CPU_ST_OFF        1
#define CPU_ST_ON_PENDING 2
struct pcpu {
    volatile int state;      /* CPU_ST_* ; protected by the big lock */
    uint64_t entry, ctx;     /* guest entry point and context_id (CPU_ON) */
    uint32_t aff;            /* Aff3.Aff2.Aff1.Aff0 packed, as GICR_TYPER[63:32] */
    uint64_t gicr;           /* physical RD_base of this core's redistributor */
};
extern struct pcpu g_pcpu[MAX_CPUS];
extern unsigned g_ncpu;
extern struct vgic g_vgic;
extern uint64_t g_hcr;
extern bool g_cpuif_noeoi;         /* test knob wel2.test=noeoi: LRs without EOI maintenance */
bool gic_phys_has_lpis(void);
void gic_phys_lpi_reg(unsigned cpu, unsigned off, uint64_t val);
extern bool g_trace_on;          /* main.c: wel2.trace diagnostics */
extern bool g_cpuif_trap;          /* cpuif.c: TRAP_EMULATE (true) or HW list registers */

/* main.c: UART, locks and per-core helpers. */
void putc_(char c);
void puts_(const char *s);
void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
void dcache_clean_range(const void *p, unsigned long n);
void puthex(uint64_t v);
void put_kv(const char *k, uint64_t v);
void put_kn(const char *pre, unsigned n, const char *post);  /* one atomic line */
unsigned this_cpu(void);                     /* TPIDR_EL2 */
void big_lock(void);                         /* serialises all EL2 shared state */
void big_unlock(void);
void enter_el1(uint64_t entry, uint64_t sp, uint64_t spsr, uint64_t hcr, uint64_t x0)
    __attribute__((noreturn));
void stage1_build(void);                     /* stage1.c: boot core, MMU off */
void stage2_enable(void);                    /* VTCR/VTTBR of this core */
void fatal_msg(const char *why) __attribute__((noreturn));
void secondary_entry(void);                  /* start.S: EL2 entry of cores 1..n */
void el2_secondary_main(void) __attribute__((noreturn));
void el2_main(uint64_t fdt) __attribute__((noreturn));   /* x0 at reset = FDT from QEMU */

/* psci.c: guest PSCI over HVC (PSCI DEN0022F.b, SMC64 + SMC32 IDs). */
bool psci_handle(struct frame *f, uint32_t fid);   /* true: x0 holds the result */
int64_t psci_smc(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3);
void psci_system_off(void) __attribute__((noreturn));

/* payload.c (runs at EL1) */
void el1_main(void) __attribute__((noreturn));

/* gic_phys.c: EL2 owns the physical GICv3 (GIC spec IHI 0069H.b). */
#define PPI_MAINT  25u   /* GIC maintenance interrupt (QEMU virt: PPI 25) */
#define PPI_VTIMER 27u   /* EL1 virtual timer (CNTV) */
#define SGI_KICK   15u   /* physical SGI reserved for EL2: "re-evaluate your vGIC" */
unsigned gic_phys_discover(void);            /* fills g_pcpu[].aff/.gicr, returns count */
void     gic_phys_init_dist(void);           /* GICD, boot core only */
void     gic_phys_init_cpu(void);            /* redistributor + CPU interface of this core */
void     gic_phys_cpu_down(void);            /* redistributor to sleep, before CPU_OFF */
uint32_t gic_phys_ack(void);                 /* ICC_IAR1_EL1 at EL2 */
void     gic_phys_eoi(uint32_t intid);       /* ICC_EOIR1_EL1 at EL2 */
void     gic_phys_ppi_enable(unsigned ppi, bool on);   /* this core's redistributor */
unsigned gic_phys_idbits(void);              /* physical GICD_TYPER.IDbits + 1 */
void     gic_phys_kick(unsigned cpu);        /* physical SGI_KICK to another core */
void     gic_phys_spi_enable(uint32_t intid, bool on);
void     gic_phys_spi_route(uint32_t intid, int vcpu);
void     gic_phys_spi_trigger(uint32_t intid, bool edge);
unsigned cpu_from_mpidr(uint64_t mpidr);     /* index into g_pcpu, or ~0u */

/* pirq_qemu.c: SPI policy (wel2_pirq.h) on the QEMU physical GICv3. */
extern struct pirq g_pirq;
void pirq_qemu_init(struct vgic *v, unsigned nr_vcpus);   /* before the guest can touch the vGIC */

/* cpuif.c: virtual CPU interface (ICH_*) driven from the vGIC model. */
void cpuif_init(struct vgic *v);             /* boot core: shared + per-core init */
void cpuif_init_cpu(void);                   /* every core */
void cpuif_cpu_down(void);                   /* CPU_OFF: ICH off, LR state dropped */
void cpuif_flush(void);                      /* harvest LRs, fill from vGIC */
void cpuif_kick_others(void);                /* kick cores with a deliverable vIRQ */
void cpuif_irq(void);                        /* physical IRQ taken at EL2 */
void cpuif_stats(void);
bool cpuif_sysreg_trap(struct frame *f);     /* ICC_SGI*R_EL1 writes */
void cpuif_vgic_effect(enum vgic_effect eff, uint32_t intid, int arg);

#endif
