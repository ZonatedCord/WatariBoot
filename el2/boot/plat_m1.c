// SPDX-License-Identifier: MIT
// Watari EL2 T6c: the Apple M1 platform behind the seam (plat.h). The memory maps are real (stage1.c, from the FDT
// lists of fdt_m1.c); the hardware methods are stubs that stop with "M1 platform: not implemented yet: <hook>" until
// the tasks that own them fill them in:
//   TODO(T6d): AIC interrupt path (pirq_init, init_boot, init_cpu, irq_ack/irq_eoi/irq_post, spi_*)
//   TODO(T6e): Apple FIQ module (fiq, ppi_enable = timer gate, kick = fast IPI)
//   TODO(T6f): cores and PSCI (discover_cpus from the FDT cpu list, cpu_start by spin table, cpu_off, system_off,
//              system_reset by the watchdog)
// The stage-1 map already contains what those tasks need: the AIC, UART and watchdog pages as Device, the cores'
// cpu-release-addr pages as Normal cacheable (stage1.c, wel2_s1_m1.rel).
// Nothing here is built for the host except the constants and the table: the stubs only call fatal_msg().

#include "boot.h"
#include "plat.h"

static void __attribute__((noreturn)) nyi(const char *hook)
{
    /* One buffer-free message: the hook name is appended by the two puts below, then the usual halt. */
    puts_("Watari EL2: M1 platform: not implemented yet: ");
    puts_(hook);
    puts_("\n");
    fatal_msg("M1 platform: not implemented yet");
}

static unsigned m1_discover_cpus(void) { nyi("discover_cpus (TODO T6f)"); }
static void m1_pirq_init(struct vgic *v, unsigned n, uint64_t fdt) { (void)v; (void)n; (void)fdt; nyi("pirq_init (TODO T6d)"); }
static void m1_init_boot(void) { nyi("init_boot (TODO T6d)"); }
static int m1_init_cpu(unsigned cpu) { (void)cpu; nyi("init_cpu (TODO T6d/T6e)"); }
static void m1_cpu_down(void) { nyi("cpu_down (TODO T6f)"); }
/* Queries the vGIC needs before any hardware exists: no LPIs, 10 ID bits (INTIDs 0..1023) [hypothesis: T6d may refine]. */
static bool m1_has_lpis(void) { return false; }
static unsigned m1_idbits(void) { return 10; }
static void m1_lpi_reg(unsigned cpu, unsigned off, uint64_t val) { (void)cpu; (void)off; (void)val; nyi("lpi_reg"); }
static uint32_t m1_irq_ack(void) { nyi("irq_ack (TODO T6d)"); }
static void m1_irq_eoi(uint32_t id) { (void)id; nyi("irq_eoi (TODO T6d)"); }
static void m1_irq_post(unsigned me) { (void)me; nyi("irq_post (TODO T6d)"); }
static void m1_ppi_enable(unsigned ppi, bool on) { (void)ppi; (void)on; nyi("ppi_enable (TODO T6e)"); }
static void m1_kick(unsigned cpu) { (void)cpu; nyi("kick (TODO T6e)"); }
static void m1_fiq(struct frame *f) { (void)f; nyi("fiq (TODO T6e)"); }
static void m1_spi_enable(uint32_t id, bool on) { (void)id; (void)on; nyi("spi_enable (TODO T6d)"); }
static void m1_spi_route(uint32_t id, int vcpu) { (void)id; (void)vcpu; nyi("spi_route (TODO T6d)"); }
static void m1_spi_trigger(uint32_t id, bool edge) { (void)id; (void)edge; nyi("spi_trigger (TODO T6d)"); }
static int64_t m1_cpu_start(uint64_t t, unsigned idx) { (void)t; (void)idx; nyi("cpu_start (TODO T6f)"); }
static int64_t m1_cpu_off(void) { nyi("cpu_off (TODO T6f)"); }
static void m1_system_off(void) { nyi("system_off (TODO T6f)"); }
static void m1_system_reset(void) { nyi("system_reset (TODO T6f)"); }

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
};
