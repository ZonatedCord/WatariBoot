// SPDX-License-Identifier: MIT
// Watari EL2: the Apple M1 platform behind the seam (plat.h). The memory maps are real (stage1.c, from the FDT lists
// of fdt_m1.c); the hardware methods are the host-tested modules wired to the real accessors:
//   T6d: the AIC interrupt path. pirq_aic over the mapped AIC window (the FDT core list gives cpu_of_vcpu), the IRQ
//        drain loop pirq_aic_drain() in irq_post, the per-core WHOAMI check in init_cpu [verified: PLAN S9].
//   T6e: the Apple FIQ module (vgic/apple_fiq.c): fiq, timer gates (ppi_enable, timer_resync), the fast IPI (kick),
//        the maintenance backstop. One struct apple_fiq_cpu per core; the only mrs/msr are in apple_sysreg.c.
//   T6f: cores and PSCI (vgic/cpu_start.c): spin-table start, CPU_OFF as a park inside the layer, watchdog reset,
//        SYSTEM_OFF as a halt. THERE IS NO `smc` IN THIS FILE: the M1 has no EL3 [verified: ID_AA64PFR0_EL1.EL3 = 0,
//        re/m3a-ehw0-measurements.md]. Wiring notes: docs/design/watari-el2-m1boot/T6e-wiring.md, T6f-wiring.md.
// The stage-1 map already contains what these need: the AIC, UART and watchdog pages as Device, the cores'
// cpu-release-addr pages as Normal cacheable, identity mapped (stage1.c, wel2_s1_m1.rel).
// Everything below is plain C; the instructions that are not system-register accesses (DC CVAC, DSB, SEV, WFE) are the
// small inline functions of the cs_* ops. The host test (tests/test_plat.c) links this file with stand-ins for the boot
// environment and drives the hooks against a fake register file and a fake AIC.

#include "boot.h"
#include "plat.h"
#include "wel2_aic.h"
#include "wel2_apple_fiq.h"
#include "wel2_cpu_start.h"

/* Checkpoint squares of the PSCI events (the layer's own row, wel2_chk.h; S13 owns 0..8). */
#define CHK_CPU_ACCEPT  WEL2_LCHK_CPU_ACCEPT    /* a CPU_ON was accepted (the core has not arrived yet) */
#define CHK_CPU_ARRIVE  WEL2_LCHK_CPU_ARRIVE    /* a started core reached the layer and is about to eret to the guest */
#define CHK_HALT        WEL2_LCHK_HALT          /* SYSTEM_OFF / SYSTEM_RESET without a watchdog: the halt */

static void __attribute__((noreturn)) nyi(const char *hook)
{
    /* One buffer-free message: the hook name is appended by the two puts below, then the usual halt. */
    puts_("Watari EL2: M1 platform: not implemented yet: ");
    puts_(hook);
    puts_("\n");
    fatal_msg("M1 platform: not implemented yet");
}

/* ---- the AIC (T6d) ------------------------------------------------------------------------------------------ */

static uint64_t g_m1_aic_base;

static uint32_t m1_a_rd32(void *ctx, uint32_t off)
{
    (void)ctx;
    return *(volatile uint32_t *)(uintptr_t)(g_m1_aic_base + off);
}

static void m1_a_wr32(void *ctx, uint32_t off, uint32_t val)
{
    (void)ctx;
    *(volatile uint32_t *)(uintptr_t)(g_m1_aic_base + off) = val;
}

/* The AIC core index of vCPU i: E cores 0-3 = Aff1 0, P cores 4-7 = Aff1 1, the same formula as the EL2 stack slot
 * (wel2_coreidx.h). [verified: WHOAMI 0..7 with E cores 0-3 and P cores 4-7, re/m3a-ehw0-measurements.md] */
static void m1_pirq_init(struct vgic *v, unsigned ncpu, uint64_t fdt)
{
    uint8_t cpu_of_vcpu[MAX_CPUS];

    (void)fdt;
    if (!g_m1.naic)
        fatal_msg("M1: the FDT has no AIC reg");
    if (ncpu == 0 || ncpu > MAX_CPUS || ncpu > g_m1.ncpu)
        fatal_msg("M1: bad core count for the AIC");
    for (unsigned i = 0; i < ncpu; i++) {
        unsigned ix = wel2_core_index(g_m1.cpu[i].reg);

        if (ix == WEL2_CORE_INVALID)
            fatal_msg("M1: a core MPIDR has no AIC index");
        cpu_of_vcpu[i] = (uint8_t)ix;
    }
    if (intr_map_validate(&INTR_MAP_T8103) != INTR_OK)
        fatal_msg("M1: the interrupt map does not validate");
    g_m1_aic_base = g_m1.aic[0].base;
    if (pirq_aic_init(&g_aic, &(struct aic_io){ m1_a_rd32, m1_a_wr32, 0 }, &INTR_MAP_T8103, cpu_of_vcpu, ncpu))
        fatal_msg("M1: pirq_aic_init failed (AIC INFO implausible?)");
    pirq_init(&g_pirq, pirq_aic_ops(&g_aic), v, ncpu);
    g_aic_on = true;                                    /* cpuif_stats() prints the AIC counters */
}

static void m1_init_boot(void)
{
    put_kv("Watari EL2: AIC at ", g_m1_aic_base);
    put_kv("Watari EL2:   inputs = ", g_aic.nr_irq);
}

/* ---- per-core Apple FIQ state (T6e) ------------------------------------------------------------------------- */

static struct apple_fiq_cpu g_afc[MAX_CPUS];

/* Every core, before its virtual CPU interface is enabled (the quiesce writes ICH_HCR_EL2 = 0 first): the FIQ module's
 * per-core init (timer controls reset BEFORE the gates open, review M-3) and the AIC's WHOAMI check. 0 or -1. */
static int m1_init_cpu(unsigned cpu)
{
    struct apple_fiq_cpu *c = &g_afc[cpu];

    c->io = &apple_sysreg_real;
    c->vgic = &g_vgic;
    c->vcpu = cpu;
    c->on_ipi = 0;                                      /* the flush that follows every FIQ delivers */
    apple_fiq_init_cpu(c);
    return pirq_aic_init_cpu(&g_aic, cpu);
}

static void m1_cpu_down(void)
{
    apple_fiq_close_gates(&g_afc[this_cpu()]);          /* parked cores must not be woken by a guest timer */
}

/* PPI 27 / 30 are the guest-timer lines; the gate is the mask. Other lines (PPI 25, maintenance) have no gate. */
static void m1_ppi_enable(unsigned ppi, bool on)
{
    struct apple_fiq_cpu *c = &g_afc[this_cpu()];

    if (ppi != APPLE_PPI_VTIMER && ppi != APPLE_PPI_PTIMER)
        return;
    if (on)
        apple_timer_unmask(c, ppi);
    else
        apple_timer_mask(c, ppi);
}

static void m1_kick(unsigned cpu)
{
    apple_ipi_send(&g_afc[this_cpu()], g_m1.cpu[cpu].reg);
}

static void m1_fiq(struct frame *f)
{
    if (apple_fiq_handle(&g_afc[this_cpu()]) & APPLE_FIQ_STORM)
        fatal_dump(f, 10, "FIQ storm: a source keeps asserting, every FIQ source is masked now");
}

static void m1_timer_resync(unsigned me)
{
    apple_timer_resync(&g_afc[me]);
}

static int m1_maint_backstop(unsigned me)
{
    return (int)apple_maint_backstop(&g_afc[me]);
}

static void m1_stats(void)
{
    struct apple_fiq_stats t = { 0 };

    for (unsigned i = 0; i < g_ncpu; i++) {
        const struct apple_fiq_stats *s = &g_afc[i].st;

        t.fiq += s->fiq; t.vtimer += s->vtimer; t.ptimer += s->ptimer; t.ipi += s->ipi; t.maint += s->maint;
        t.maint_quenched += s->maint_quenched; t.maint_fatal += s->maint_fatal; t.pmu += s->pmu; t.upmu += s->upmu;
        t.spurious += s->spurious; t.storms += s->storms;
    }
    put_kv("Watari EL2: stats: M1 FIQs = ", t.fiq);
    put_kv("Watari EL2: stats:   guest virtual timer (PPI 27) = ", t.vtimer);
    put_kv("Watari EL2: stats:   guest physical timer (PPI 30) = ", t.ptimer);
    put_kv("Watari EL2: stats:   fast IPIs = ", t.ipi);
    put_kv("Watari EL2: stats:   vGIC maintenance = ", t.maint);
    put_kv("Watari EL2: stats:   maintenance quenched by the backstop = ", t.maint_quenched);
    put_kv("Watari EL2: stats:   PMU / uncore PMU = ", t.pmu + t.upmu);
    put_kv("Watari EL2: stats:   spurious = ", t.spurious);
    put_kv("Watari EL2: stats:   storms = ", t.storms);
}

/* ---- the IRQ path (T6d) ------------------------------------------------------------------------------------- */

/* There is no GIC: the physical IRQ line is the AIC's. irq_ack reports "none"; irq_post drains the AIC. */
static uint32_t m1_irq_ack(void) { return 1023; }
static void m1_irq_eoi(uint32_t id) { (void)id; }

static void m1_drain_note(void *ctx, const struct aic_event *ev, uint32_t vintid)
{
    static unsigned n_log;

    (void)ctx; (void)vintid;
    if (ev->kind != AIC_EV_IRQ && n_log++ < 8)           /* IPI / bad events: counted in g_aic.st, a few shown */
        put_kv("Watari EL2: unexpected AIC event, raw ", ev->raw);
}

static void m1_irq_post(unsigned me)
{
    const struct pirq_aic_drain d = { 0, 0, m1_drain_note, 0 };

    pirq_aic_drain(&g_aic, &g_pirq, me, &d);
}

/* The pirq backend drives the AIC directly (pirq_aic_ops); the GIC-style SPI hooks have no caller on this platform. */
static void m1_spi_enable(uint32_t id, bool on) { (void)id; (void)on; nyi("spi_enable (pirq drives the AIC)"); }
static void m1_spi_route(uint32_t id, int vcpu) { (void)id; (void)vcpu; nyi("spi_route (pirq drives the AIC)"); }
static void m1_spi_trigger(uint32_t id, bool edge) { (void)id; (void)edge; nyi("spi_trigger (pirq drives the AIC)"); }

/* Queries the vGIC needs: no LPIs, 10 ID bits (INTIDs 0..1023) [hypothesis: T6d may refine]. */
static bool m1_has_lpis(void) { return false; }
static unsigned m1_idbits(void) { return 10; }
static void m1_lpi_reg(unsigned cpu, unsigned off, uint64_t val) { (void)cpu; (void)off; (void)val; nyi("lpi_reg"); }

/* ---- cores and PSCI (T6f) ----------------------------------------------------------------------------------- */

struct wel2_cs g_cs;

/* One wake flag per core on its own cache line (128 B covers the M1's line): both sides run with the MMU on, the
 * memory is coherent, no cache maintenance. */
static struct { volatile uint32_t v; char pad[124]; } g_wake[MAX_CPUS] __attribute__((aligned(128)));

static void cs_write64(void *ctx, uint64_t pa, uint64_t v) { (void)ctx; *(volatile uint64_t *)(uintptr_t)pa = v; }
/* The reader of the spin table runs with MMU and caches off: clean to the PoC. With the identity stage-1 map of the
 * release page (Normal, write-back) the VA is the PA. */
static void cs_dc_cvac(void *ctx, uint64_t pa) { (void)ctx; __asm__ volatile("dc cvac, %0" : : "r"(pa) : "memory"); }
static void cs_dsb(void *ctx) { (void)ctx; __asm__ volatile("dsb sy" ::: "memory"); }
static void cs_sev(void *ctx) { (void)ctx; __asm__ volatile("sev" ::: "memory"); }
static void cs_wfe(void *ctx) { (void)ctx; __asm__ volatile("wfe" ::: "memory"); }
static void cs_wake_store(void *ctx, unsigned idx, uint32_t v) { (void)ctx; g_wake[idx].v = v; }
static uint32_t cs_wake_load(void *ctx, unsigned idx) { (void)ctx; return g_wake[idx].v; }
/* CNTV_CTL_EL02 = CNTP_CTL_EL02 = 0 through the same accessor the FIQ module uses (E2H is stuck at 1 on the M1). */
static void cs_timers_reset(void *ctx)
{
    (void)ctx;
    apple_sysreg_real.wr(apple_sysreg_real.ctx, APPLE_SR_CNTV_CTL_EL02, 0);
    apple_sysreg_real.wr(apple_sysreg_real.ctx, APPLE_SR_CNTP_CTL_EL02, 0);
}
static void cs_mmio_write32(void *ctx, uint64_t pa, uint32_t v) { (void)ctx; *(volatile uint32_t *)(uintptr_t)pa = v; }

static void cs_log(void *ctx, int event, unsigned idx, uint64_t arg)
{
    (void)ctx;
    if (event == WEL2_CS_EV_ON_ACCEPT) {
        checkpoint(CHK_CPU_ACCEPT);
        put_kn("Watari EL2: M1 CPU_ON cpu ", idx, arg ? " accepted (wake of a parked core)\n" : " accepted (spin table)\n");
    } else if (event == WEL2_CS_EV_ARRIVE) {
        checkpoint(CHK_CPU_ARRIVE);
        put_kn("Watari EL2: M1 cpu ", idx, " arrived, about to eret to the guest\n");
        put_kv("Watari EL2:   guest entry = ", arg);
    }
}

static const struct wel2_cs_ops g_cs_ops = {
    .write64 = cs_write64,
    .dc_cvac = cs_dc_cvac,
    .dsb = cs_dsb,
    .sev = cs_sev,
    .wfe = cs_wfe,
    .wake_store = cs_wake_store,
    .wake_load = cs_wake_load,
    .timers_reset = cs_timers_reset,
    .mmio_write32 = cs_mmio_write32,
    .log = cs_log,
    .ctx = 0,
};

/* The core list comes from the FDT (T6b): node order = vCPU index = PSCI index = stack index (DESIGN 3.3). */
static unsigned m1_discover_cpus(void)
{
    struct wel2_cs_cpu cpus[WEL2_CS_MAX];
    uint64_t mpidr, aff_mask = 0x000000ff00ffffffULL;
    unsigned n = g_m1.ncpu, boot = ~0u;

    if (n == 0 || n > MAX_CPUS || n > WEL2_CS_MAX)
        fatal_msg("M1: the FDT lists no usable cpu");
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    for (unsigned i = 0; i < n; i++) {
        uint64_t reg = g_m1.cpu[i].reg & aff_mask;

        g_pcpu[i].aff = (uint32_t)(reg & 0xffffff) | (uint32_t)(((reg >> 32) & 0xff) << 24);   /* as GICR_TYPER[63:32] */
        g_pcpu[i].gicr = 0;                              /* no redistributors: the guest's are emulated */
        cpus[i].mpidr = reg;
        cpus[i].release_addr = g_m1.cpu[i].has_release ? g_m1.cpu[i].release_addr : 0;
        if (reg == (mpidr & aff_mask))
            boot = i;
    }
    if (boot == ~0u)
        fatal_msg("M1: the boot core is not in the FDT cpu list");
    if (wel2_cs_init(&g_cs, &g_cs_ops, (uint64_t)(uintptr_t)secondary_entry, cpus, n, boot))
        fatal_msg("M1: wel2_cs_init failed (duplicate MPIDR or bad core list)");
    return n;
}

static int64_t m1_cpu_start(uint64_t target, unsigned idx, uint64_t entry, uint64_t ctx)
{
    (void)idx;
    return wel2_cs_cpu_on(&g_cs, target, entry, ctx);
}

/* Step 1 of CPU_OFF (big lock held, before the teardown): the module decides, and on SUCCESS the core is OFF. */
static int64_t m1_cpu_off_begin(unsigned me)
{
    return wel2_cs_cpu_off(&g_cs, me);
}

/* Step 2 (teardown done, big lock released): park in the layer until a CPU_ON wakes the core, then restart it on a
 * fresh stack (start.S secondary_rewake -> el2_secondary_main). Never returns: there is no firmware to return to. */
static int64_t m1_cpu_off(void)
{
    wel2_cs_park(&g_cs, this_cpu(), 0);
    secondary_rewake();
}

static bool m1_cpu_arrive(unsigned me, uint64_t *entry, uint64_t *ctx)
{
    return wel2_cs_enter(&g_cs, me, entry, ctx);
}

/* SYSTEM_OFF: no register switches a MacBook off. Halt with the lock released so that other cores that trap can still
 * print; the user holds the power button [hypothesis: the panel keeps the last image, DESIGN 7]. */
static void m1_system_off(void)
{
    puts_("Watari EL2: SYSTEM_OFF: halted, hold the power button to switch off\n");
    checkpoint(CHK_HALT);
    big_unlock();
    wel2_cs_wait_forever(&g_cs_ops, 0);
}

static void m1_system_reset(void)
{
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
    if (wel2_cs_system_reset(&g_cs_ops, g_m1.wdt.found ? g_m1.wdt.base : 0) < 0) {
        puts_("Watari EL2: SYSTEM_RESET: no watchdog node in the FDT, halting instead\n");
        checkpoint(CHK_HALT);
        big_unlock();                                    /* like system_off: other cores that trap can still print */
    }
    wel2_cs_wait_forever(&g_cs_ops, 0);
}

/* wel2.hold=1 (main.c layer_hold): the timers are reset first so a pending guest timer cannot turn the wfe into a spin. */
static void m1_park(void)
{
    wel2_cs_wait_forever(&g_cs_ops, 0);
}

const struct wel2_platform plat_m1 = {
    .name = "m1",
    .m1_memory = true,
    .s2_ipa_bits = 36,
    .has_phys_gic = false,
    .vgic_gicd_base = PLAT_M1_GICD_BASE,
    .vgic_gicr_base = PLAT_M1_GICR_BASE,
    .discover_cpus = m1_discover_cpus,
    .pirq_init = m1_pirq_init,
    .init_boot = m1_init_boot,
    .init_cpu = m1_init_cpu,
    .cpu_down = m1_cpu_down,
    .has_lpis = m1_has_lpis,
    .idbits = m1_idbits,
    .lpi_reg = m1_lpi_reg,
    .irq_ack = m1_irq_ack,
    .irq_eoi = m1_irq_eoi,
    .irq_post = m1_irq_post,
    .ppi_enable = m1_ppi_enable,
    .kick = m1_kick,
    .fiq = m1_fiq,
    .spi_enable = m1_spi_enable,
    .spi_route = m1_spi_route,
    .spi_trigger = m1_spi_trigger,
    .cpu_start = m1_cpu_start,
    .cpu_off = m1_cpu_off,
    .system_off = m1_system_off,
    .system_reset = m1_system_reset,
    .park = m1_park,
    .native_psci = true,
    .cpu_off_begin = m1_cpu_off_begin,
    .cpu_arrive = m1_cpu_arrive,
    .timer_resync = m1_timer_resync,
    .maint_backstop = m1_maint_backstop,
    .stats = m1_stats,
};
