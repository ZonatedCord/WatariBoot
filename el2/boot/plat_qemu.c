// SPDX-License-Identifier: MIT
// Watari EL2 T6c: the QEMU virt platform behind the seam (plat.h). This is exactly what the generic code did
// directly before the seam existed: the physical GICv3 of gic_phys.c, the SPI policy of pirq_qemu.c (including the
// hybrid machine's Watari AIC, #42), and PSCI by SMC to QEMU's firmware (virtualization=on puts PSCI behind SMC).
// PSCI function ids: Arm DEN0022F.b Table 3.

#include "boot.h"
#include "plat.h"
#include "gic_phys.h"
#include "fdt.h"
#include "wel2_aic.h"

_Static_assert(PLAT_QEMU_GICD_BASE == GICD_PHYS && PLAT_QEMU_GICR_BASE == GICR_PHYS, "vGIC windows as boot.h");

#define PSCI_CPU_OFF       0x84000002u
#define PSCI_CPU_ON_64     0xC4000003u
#define PSCI_SYSTEM_OFF    0x84000008u
#define PSCI_SYSTEM_RESET  0x84000009u

static int64_t psci_smc(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3)
{
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;

    __asm__ volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    return (int64_t)x0;
}

/* SPI policy first (vGIC effects reach it); #42: the QEMU machine with aic=on describes its Apple-style AIC in the
 * FDT, the layer then owns it too. */
static void q_pirq_init(struct vgic *v, unsigned ncpu, uint64_t fdt)
{
    pirq_qemu_init(v, ncpu);
    if (g_aic_allowed && fdt && !(fdt & 7) && !fdt_check((void *)fdt) &&
        fdt_find_aic((void *)fdt, &g_aic_info) == 0 && g_aic_info.found) {
        const char *why = "";

        if (g_aic_info.nr_cpu != g_ncpu)
            fatal_msg("AIC node: num-cpu differs from the cores found");
        if (pirq_qemu_init_aic(v, ncpu, &g_aic_info, &why))
            fatal_msg(why);
        put_kv("Watari EL2: AIC at ", g_aic_info.base);
        put_kv("Watari EL2:   inputs = ", g_aic.nr_irq);
        put_kv("Watari EL2:   devices routed to the AIC = ", g_aic_info.n_routed);
    }
}

static int q_init_cpu(unsigned cpu)
{
    gic_phys_init_cpu();
    if (g_aic_on && pirq_qemu_init_aic_cpu(cpu))
        return -1;
    return 0;
}

/* #42: the CPU IRQ line is the OR of the GIC and the AIC; the AIC is drained once the GIC reports nothing. */
static void q_irq_post(unsigned me)
{
    if (g_aic_on)
        pirq_qemu_aic_irq(me);
}

static int64_t q_cpu_start(uint64_t target, unsigned idx)
{
    (void)idx;
    return psci_smc(PSCI_CPU_ON_64, target, (uint64_t)(uintptr_t)secondary_entry, 0);
}

static int64_t q_cpu_off(void)
{
    return psci_smc(PSCI_CPU_OFF, 0, 0, 0);
}

static void q_system_off(void)
{
    psci_smc(PSCI_SYSTEM_OFF, 0, 0, 0);
}

static void q_system_reset(void)
{
    psci_smc(PSCI_SYSTEM_RESET, 0, 0, 0);
}

const struct wel2_platform plat_qemu = {
    .name = "qemu",
    .m1_memory = false,
    .has_phys_gic = true,
    .vgic_gicd_base = PLAT_QEMU_GICD_BASE,
    .vgic_gicr_base = PLAT_QEMU_GICR_BASE,
    .discover_cpus = gic_phys_discover,
    .pirq_init = q_pirq_init,
    .init_boot = gic_phys_init_dist,
    .init_cpu = q_init_cpu,
    .cpu_down = gic_phys_cpu_down,
    .has_lpis = gic_phys_has_lpis,
    .idbits = gic_phys_idbits,
    .lpi_reg = gic_phys_lpi_reg,
    .irq_ack = gic_phys_ack,
    .irq_eoi = gic_phys_eoi,
    .irq_post = q_irq_post,
    .ppi_enable = gic_phys_ppi_enable,
    .kick = gic_phys_kick,
    .fiq = 0,
    .spi_enable = gic_phys_spi_enable,
    .spi_route = gic_phys_spi_route,
    .spi_trigger = gic_phys_spi_trigger,
    .cpu_start = q_cpu_start,
    .cpu_off = q_cpu_off,
    .system_off = q_system_off,
    .system_reset = q_system_reset,
};

const struct wel2_platform *g_plat = &plat_qemu;
