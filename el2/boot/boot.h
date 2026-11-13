/* SPDX-License-Identifier: MIT */
/* Watari EL2 boot spike: shared declarations (M1-3). */
#ifndef WEL2_BOOT_H
#define WEL2_BOOT_H

#include "wel2_coreidx.h"
#include <stdbool.h>
#include <stdint.h>
#include "wel2_vgic.h"
#include "wel2_pirq.h"
#include "wel2_fb.h"
#include "wel2_maps.h"
#include "fdt_m1.h"
#include "wel2_chk.h"

/* QEMU virt GICv3 windows (hw/arm/virt.c memmap). */
#define GICD_PHYS   0x08000000UL
#define GICD_SIZE   0x10000UL
#define GICR_PHYS   0x080A0000UL   /* redistributor frame of CPU 0 (RD_base) */
#define GICR_STRIDE 0x20000UL      /* RD_base + SGI_base frames */
#define MAX_CPUS    VGIC_MAX_VCPUS
_Static_assert(MAX_CPUS == WEL2_NSTACKS, "one EL2 stack slot per vCPU/core (wel2_coreidx.h)");
/* Guest RAM (QEMU virt, up to -m 2G) and, in the M1-4 chain build, the part of it
 * the layer keeps for itself: the first g_carve_size of RAM, cut off the FDT
 * /memory node (INTERFACES 3.2). Inside it, [RAM base, layer image) is the FDT
 * hand-off buffer (mapped, read by the guest) and [layer image, carve end) is the
 * layer (image + stacks, unmapped in stage 2).
 * The patched FDT is copied to the RAM base: EDK2's early DebugLib reads it at
 * PcdDeviceTreeInitialBaseAddress = 0x40000000 (F-21). EDK2 itself is loaded right
 * after the carve-out, at the address an arm64 Image with text_offset 0x80000 would
 * have there. */
/* T6a: the image is position independent, so nothing here is a link-time constant any more. stage1_build() (MMU off,
 * boot core) fills these from the run-time address of _start; read by everyone after. QEMU: the layer is loaded at
 * RAM base + 0x80000, the carve-out is the 2 MB block that contains it. The M1 uses a larger block (design D 2.3). */
#define WEL2_CARVE_SIZE_QEMU 0x200000ULL    /* default g_carve_size */
#define WEL2_CARVE_SIZE_M1   0x2000000ULL   /* the M1 (T6c): one 32 MB block, 32 MB aligned (design D 2.3), anywhere in the RAM */
extern uint64_t g_layer_base;               /* run-time address of _start (16 KB aligned) */
extern uint64_t g_carve_base, g_carve_size; /* [g_carve_base, +g_carve_size): FDT hand-off buffer + the layer */
#define EDK2_FDT_ADDR    (g_carve_base)
#define EDK2_ENTRY       (g_carve_base + g_carve_size + 0x80000ULL)
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
extern bool g_trace_on;          /* main.c: wel2.trace diagnostics */
extern bool g_cpuif_trap;          /* cpuif.c: TRAP_EMULATE (true) or HW list registers */
unsigned cpuif_mode(void);         /* cpuif.c: WEL2_CPUIF_HW / _TRAP / _TRAP_NOTC */
extern int g_test_tc;              /* cpuif.c: test knob, see there */
extern bool g_cpuif_notc;          /* cpuif.c: with g_cpuif_trap, the TC registers (PMR, CTLR, RPR) go to the hardware (trap-notc) */
extern unsigned g_step;            /* stage1.c: wel2.step=N (0 = none): the hardware ladder rung, DESIGN 9 */
extern bool g_tid3;                /* idreg.c: HCR_EL2.TID3 set */
extern unsigned g_idreg_n;         /* idreg.c: ID-register reads emulated */
bool idreg_trap(struct frame *f);  /* idreg.c: a trapped read of the TID3 group (EC 0x18) */

/* main.c: UART, locks and per-core helpers. */
void putc_(char c);
struct wel2_uart;
extern struct wel2_uart g_con;       /* util.c: the console (PL011 until con_select_s5l) */
void con_select_s5l(uint64_t base);
void con_select_none(void);                  /* util.c: null console (the M1 without a UART node) */
extern uint64_t g_con_s5l_base, g_con_s5l_size;   /* stage1.c: S5L UART window from the FDT, 0 = none */  /* switch the console to the Apple S5L UART at base (MMU may be off) */
void puts_(const char *s);
void console_bytes(const uint8_t *b, unsigned n);   /* util.c: WEL2_CONSOLE_WRITE sink: raw to the UART, printable to the framebuffer */
/* Framebuffer output (T5b, util.c): g_fb is set by stage1_build from the FDT (fmt NONE = no framebuffer, every call
 * a no-op). putc_/puts_ mirror to it. checkpoint(n) paints square n green (reached), checkpoint_state(n, s) with
 * s = 0 running / 1 pass / 2 fail, checkpoint_fail() repaints the current one red. All use aligned 32-bit stores
 * only and need no stack-heavy or lock-taking code, so they run with the MMU off. */
extern struct wel2_fb g_fb;
extern bool g_fb_mute;                       /* util.c: text to the UART only, not to the panel */
extern bool g_summary_on_panel;              /* util.c: the guest's SUMMARY block is on the panel (main.c sets it) */
void checkpoint(unsigned n);
void checkpoint_state(unsigned n, unsigned state);
void checkpoint_fail(void);
extern struct wel2_fb_plan g_fb_plan;        /* stage1.c: E9, what the guest gets of the panel (mode layer = nothing) */
extern uint64_t g_fake_fb_lo;                /* stage1.c: bench fake panel [lo, RAM end), 0 = none */
extern bool g_fb_fake;
void fb_handover(void);                      /* stage1.c: E9, the switch at the eret (no-op for wel2.fb=layer) */
extern bool g_hold;                           /* main.c: wel2.hold=1, stop on the screen instead of resetting */
void layer_reset(bool locked) __attribute__((noreturn));   /* main.c: the guest's / the ladder's reset, or the hold with wel2.hold=1 */
void layer_checkpoint(unsigned n, const char *name);   /* main.c: paint square n and log one line (MMU on) */
#if !__STDC_HOSTED__                          /* util.c provides these for the layer; the host tests have libc */
void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
#endif
void dcache_clean_range(const void *p, unsigned long n);
void puthex(uint64_t v);
void put_kv(const char *k, uint64_t v);
void put_kn(const char *pre, unsigned n, const char *post);  /* one atomic line */
unsigned this_cpu(void);                     /* TPIDR_EL2 */
void big_lock(void);                         /* serialises all EL2 shared state */
void big_unlock(void);
void enter_el1(uint64_t entry, uint64_t sp, uint64_t spsr, uint64_t hcr, uint64_t x0)
    __attribute__((noreturn));
/* Build defaults of the two M1 switches, overridden by the boot arguments wel2.e2h=0|1 and wel2.gran=4k|16k
 * (stage1.c). 1 / 16 KB is what the real M1 needs (E2H stuck at 1, 16 KB translation granule only). */
#ifndef WEL2_DEF_E2H
#define WEL2_DEF_E2H 1
#endif
#ifndef WEL2_DEF_GRAN
#define WEL2_DEF_GRAN 14              /* 12 = 4 KB, 14 = 16 KB */
#endif
extern unsigned g_gran;                      /* translation granule shift of the stage-1 and stage-2 maps */
extern bool g_e2h;                           /* HCR_EL2.E2H the layer runs with */
extern bool g_e2h_stuck;                     /* wel2.e2h=0 was asked and the bit reads 1 (the M1) */
extern bool g_gran_fallback;                 /* the requested granule is not implemented, the other is used */
void stage1_build(uint64_t fdt);             /* stage1.c: boot core, MMU off; fdt = x0 from the loader */
/* Guest RAM [g_ram_base, g_ram_end) as the FDT /memory node describes it (stage1.c). Without a
 * usable node: the old fixed map, 0x40000000-0x13fffffff (a -m 4G machine). g_ram_from_fdt says which. */
extern uint64_t g_ram_base, g_ram_end;
extern bool g_ram_from_fdt;
/* T6c: the FDT lists the platform switch was taken from (stage1.c; valid when g_plat is plat_m1) and the guest's RAM
 * as ranges. M1: usable[] minus the layer's block, rounded inward to 32 MB (WEL2_M1_S2_ALIGN): the guest stage 2 maps
 * exactly these. QEMU: empty, the guest RAM is the single range [g_ram_base, g_ram_end). g_ram_base / g_ram_end are
 * then the lowest start and the highest end of the list. */
_Static_assert(WEL2_MAX_GUEST_RANGES == M1_MAX_USABLE + 2, "one cut can split one more piece, plus slack");
extern struct m1_platform g_m1;
extern struct wel2_range g_guest_ram[WEL2_MAX_GUEST_RANGES];
extern unsigned g_nguest_ram;
unsigned guest_ranges(struct wel2_range *out);   /* stage1.c: the ranges the guest FDT lists, out[WEL2_MAX_GUEST_RANGES] */
bool guest_ram_contains(uint64_t pa);        /* pa is guest RAM (M1: inside one of g_guest_ram[]) */
struct fdt_aic_info;
extern struct fdt_aic_info g_aic_info;       /* main.c: the QEMU hybrid machine's Watari AIC node (#42) */
extern bool g_aic_allowed;                   /* wel2.aic=off clears it */
void stage2_enable(void);                    /* VTCR/VTTBR of this core */
void fatal_msg(const char *why) __attribute__((noreturn));
void fatal_dump(struct frame *f, unsigned idx, const char *why) __attribute__((noreturn));   /* main.c: why + register dump + halt */
void secondary_entry(void);                  /* start.S: EL2 entry of cores 1..n */
/* start.S: a core that CPU_OFF parked is woken: new EL2 stack (top of its slot), then el2_secondary_main(). MMU and
 * caches are on already; the exception frames of the old stack are abandoned. */
void secondary_rewake(void) __attribute__((noreturn));
void el2_secondary_main(void) __attribute__((noreturn));
void el2_main(uint64_t fdt) __attribute__((noreturn));   /* x0 at reset = FDT from QEMU */

/* psci.c: guest PSCI over HVC (PSCI DEN0022F.b, SMC64 + SMC32 IDs). */
bool psci_handle(struct frame *f, uint32_t fid);   /* true: x0 holds the result */

/* payload.c (runs at EL1) */
void el1_main(void) __attribute__((noreturn));

/* Physical interrupt INTIDs the generic code knows (QEMU virt GICv3 numbering; the platform maps them, plat.h).
 * The QEMU GICv3 itself is behind g_plat (plat.h, gic_phys.h). */
#define PPI_MAINT  25u   /* GIC maintenance interrupt (QEMU virt: PPI 25) */
#define PPI_VTIMER 27u   /* EL1 virtual timer (CNTV) */
#define SGI_KICK   15u   /* physical SGI reserved for EL2: "re-evaluate your vGIC" */
extern volatile uint64_t g_bad_mpidr;           /* MPIDR|bit63 of a core parked by start.S bad_core, else 0; written
                                                 * with caches off: read it via psci.c report_bad_core() */
unsigned cpu_from_mpidr(uint64_t mpidr);     /* index into g_pcpu, or ~0u */

/* pirq_qemu.c: SPI policy (wel2_pirq.h) on the QEMU physical GICv3. */
extern struct pirq g_pirq;
void pirq_qemu_init(struct vgic *v, unsigned nr_vcpus);   /* before the guest can touch the vGIC */
/* #42: the hybrid machine (QEMU patched with aic=on) also has a Watari AIC that owns some devices' lines. */
struct fdt_aic_info;
struct pirq_aic;
extern bool g_aic_on;                        /* the AIC backend is active (FDT node found and accepted) */
extern struct pirq_aic g_aic;
int  pirq_qemu_init_aic(struct vgic *v, unsigned nr_vcpus, const struct fdt_aic_info *ai, const char **why);
int  pirq_qemu_init_aic_cpu(unsigned vcpu);  /* every core, after its GIC interface is up */
void pirq_qemu_aic_irq(unsigned me);         /* drain this core's AIC events (IRQ branch) */

/* cpuif.c: virtual CPU interface (ICH_*) driven from the vGIC model. */
void cpuif_init(struct vgic *v);             /* boot core: shared + per-core init */
void cpuif_init_cpu(void);                   /* every core */
void cpuif_cpu_down(void);                   /* CPU_OFF: ICH off, LR state dropped */
void cpuif_flush(void);                      /* harvest LRs, fill from vGIC */
void cpuif_kick_others(void);                /* kick cores with a deliverable vIRQ */
void cpuif_irq(void);                        /* physical IRQ taken at EL2 */
void cpuif_maint_backstop(void);             /* IRQ and FIQ exit tails, after cpuif_flush(): g_plat->maint_backstop (M1) */
void cpuif_stats(void);
struct wel2_stats;
void cpuif_stats_fill(struct wel2_stats *out);   /* WEL2_STATS: trap counters per ICC register, system wide */
bool cpuif_sysreg_trap(struct frame *f);     /* ICC_SGI*R_EL1 writes */
void cpuif_vgic_effect(enum vgic_effect eff, uint32_t intid, int arg);

#endif
