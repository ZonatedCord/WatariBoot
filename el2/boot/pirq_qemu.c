// SPDX-License-Identifier: MIT
// Watari EL2 M2.x-2: pirq backend on the QEMU virt physical GICv3 (the twin of the Apple
// AIC backend, #59: same struct pirq_ops, policy in ../vgic/pirq.c, host-tested).
//
// Behaviour is the one the layer had before the indirection (M1-3, F-25): physical INTID =
// vINTID (identity map for SPIs 32..959), a level SPI is disabled at the GICD when it fires
// and re-enabled at the guest's deactivate, ICFGR comes from the guest, and the event read
// (ICC_IAR1) does not mask anything (auto_masks = false).
//
// #42 (hybrid machine, QEMU patched with tools/qemu-watari "aic=on"): when the FDT has the
// Watari AIC node, the same table also drives the AIC for the devices wired to it. A source
// has one "physical" key in the pirq tables: a GIC SPI is its INTID (32..959), an AIC input
// is AIC_KEY | hwirq. The dispatch below sends each call to the GIC or the AIC backend. The
// AIC hwirq of a device equals the SPI number it would have on the GIC, and the vINTID the
// guest sees is 32 + hwirq (intr_map rule), so the numbers in the FDT / ACPI stay valid.

#include "boot.h"
#include "plat.h"
#include "fdt.h"
#include "wel2_aic.h"

struct pirq g_pirq;
bool g_aic_on;                          /* the hybrid backend is active */
struct pirq_aic g_aic;                  /* valid when g_aic_on */

static void q_mask(void *c, uint32_t p)               { (void)c; g_plat->spi_enable(p, false); }
static void q_unmask(void *c, uint32_t p)             { (void)c; g_plat->spi_enable(p, true); }
static void q_target(void *c, uint32_t p, int vcpu)   { (void)c; g_plat->spi_route(p, vcpu); }
static void q_trigger(void *c, uint32_t p, bool edge) { (void)c; g_plat->spi_trigger(p, edge); }

static uint32_t q_intid_of(void *c, uint32_t phys)
{
    (void)c;
    return phys >= 32 && phys < VGIC_NR_IRQS ? phys : INTR_INVALID;
}

static const struct pirq_ops g_qemu_ops = {
    .name = "pirq-gic-qemu",
    .auto_masks = false,
    .strict_level = false,
    .mask = q_mask,
    .unmask = q_unmask,
    .set_target = q_target,
    .set_trigger = q_trigger,
    .intid_of = q_intid_of,
    .phys_of = q_intid_of,          /* identity: the inverse is the same function */
    .trigger_of = 0,                /* the guest's ICFGR decides (F-25) */
    .ctx = 0,
};

void pirq_qemu_init(struct vgic *v, unsigned nr_vcpus)
{
    pirq_init(&g_pirq, &g_qemu_ops, v, nr_vcpus);
}

/* ---- hybrid: GIC + Watari AIC (#42) ---- */

#define AIC_KEY 0x10000u                /* phys key of an AIC input: AIC_KEY | hwirq */

static struct intr_map g_aic_map;
static struct intr_entry g_aic_allow[FDT_AIC_MAX_ROUTED];
static uint64_t g_aic_base;
static struct pirq_ops g_hyb_ops;

static bool is_aic(uint32_t phys)
{
    return phys != INTR_INVALID && (phys & AIC_KEY) != 0;
}

static uint32_t a_rd32(void *ctx, uint32_t off)
{
    (void)ctx;
    return *(volatile uint32_t *)(uintptr_t)(g_aic_base + off);
}

static void a_wr32(void *ctx, uint32_t off, uint32_t val)
{
    (void)ctx;
    *(volatile uint32_t *)(uintptr_t)(g_aic_base + off) = val;
}

static void h_mask(void *c, uint32_t p)
{
    if (is_aic(p))
        g_aic.ops.mask(g_aic.ops.ctx, p & 0xffff);
    else
        q_mask(c, p);
}

static void h_unmask(void *c, uint32_t p)
{
    if (is_aic(p))
        g_aic.ops.unmask(g_aic.ops.ctx, p & 0xffff);
    else
        q_unmask(c, p);
}

static void h_target(void *c, uint32_t p, int vcpu)
{
    if (is_aic(p))
        g_aic.ops.set_target(g_aic.ops.ctx, p & 0xffff, vcpu);
    else
        q_target(c, p, vcpu);
}

static void h_trigger(void *c, uint32_t p, bool edge)
{
    if (is_aic(p))
        g_aic.ops.set_trigger(g_aic.ops.ctx, p & 0xffff, edge);
    else
        q_trigger(c, p, edge);
}

static uint32_t h_intid_of(void *c, uint32_t phys)
{
    if (is_aic(phys))
        return g_aic.ops.intid_of(g_aic.ops.ctx, phys & 0xffff);
    return q_intid_of(c, phys);
}

/* An AIC-claimed vINTID wins over the GIC identity, so a device moved to the AIC has no GIC SPI. */
static uint32_t h_phys_of(void *c, uint32_t vintid)
{
    uint32_t h = g_aic.ops.phys_of(g_aic.ops.ctx, vintid);

    return h != INTR_INVALID ? (AIC_KEY | h) : q_intid_of(c, vintid);
}

static int h_trigger_of(void *c, uint32_t phys)
{
    (void)c;
    return is_aic(phys) ? g_aic.ops.trigger_of(g_aic.ops.ctx, phys & 0xffff) : -1;
}

/* Returns 0, or -1 with the reason in *why. Boot core, EL2 stage 1 on (the AIC window is Device memory). */
int pirq_qemu_init_aic(struct vgic *v, unsigned nr_vcpus, const struct fdt_aic_info *ai, const char **why)
{
    uint8_t cpu_of_vcpu[MAX_CPUS];

    if (ai->n_routed == 0 || ai->nr_irq > AIC_MAX_IRQS || ai->nr_irq == 0 || (ai->nr_irq & 31)) {
        *why = "AIC node has no routed IRQ or a bad num-irq";
        return -1;
    }
    for (unsigned i = 0; i < ai->n_routed; i++) {
        g_aic_allow[i].hwirq = ai->routed[i];
        g_aic_allow[i].edge = INTR_TRIG_LEVEL;      /* PL031 / PL011 lines are levels */
        g_aic_allow[i].name = "aic-routed";
    }
    g_aic_map = (struct intr_map){
        .wired_max = ai->nr_irq,
        .msi_hwirq_base = INTR_INVALID,
        .allow = g_aic_allow,
        .n_allow = ai->n_routed,
    };
    if (intr_map_validate(&g_aic_map) != INTR_OK) {
        *why = "interrupt map of the AIC node does not validate";
        return -1;
    }
    for (unsigned i = 0; i < nr_vcpus && i < MAX_CPUS; i++)
        cpu_of_vcpu[i] = (uint8_t)i;                  /* AIC core index = QEMU cpu index = vCPU index */
    g_aic_base = ai->base;
    if (pirq_aic_init(&g_aic, &(struct aic_io){ a_rd32, a_wr32, 0 }, &g_aic_map, cpu_of_vcpu, nr_vcpus)) {
        *why = "pirq_aic_init failed (INFO implausible?)";
        return -1;
    }
    g_hyb_ops = (struct pirq_ops){
        .name = "pirq-gic-qemu+aic",
        .auto_masks = false,            /* GIC rules; the AIC mask is redundant but harmless, edge: aic_irq() */
        .strict_level = false,
        .mask = h_mask,
        .unmask = h_unmask,
        .set_target = h_target,
        .set_trigger = h_trigger,
        .intid_of = h_intid_of,
        .phys_of = h_phys_of,
        .trigger_of = h_trigger_of,
        .ctx = 0,
    };
    g_aic_on = true;
    pirq_init(&g_pirq, &g_hyb_ops, v, nr_vcpus);
    return 0;
}

/* Per core, on that core, after its GIC interface is up. */
int pirq_qemu_init_aic_cpu(unsigned vcpu)
{
    return pirq_aic_init_cpu(&g_aic, vcpu);
}

/* Log of the drain below (the same lines as before the loop moved to pirq_aic_drain; run-m1-6 greps them). */
static void aic_note(void *ctx, const struct aic_event *ev, uint32_t id)
{
    unsigned me = (unsigned)(uintptr_t)ctx;

    if (ev->kind == AIC_EV_IRQ) {
#ifndef WEL2_CHAIN
        static unsigned n_log;

        if (n_log++ < 8) {           /* run-m1-6: which core took the AIC event, and as which vINTID */
            put_kn("Watari EL2: AIC event taken on cpu ", me, "\n");
            put_kv("Watari EL2:   AIC hwirq = ", ev->num);
            put_kv("Watari EL2:   injected as vINTID = ", id);
        }
#else
        (void)me; (void)id;
#endif
        if (g_trace_on && g_aic.st.events <= 100)
            put_kv("Watari EL2: TR AIC event, hwirq ", ev->num);
    } else if (g_aic.st.ipi_unexpected + g_aic.st.bad <= 8) {
        put_kv("Watari EL2: unexpected AIC event, raw ", ev->raw);
    }
}

/* Drain the AIC of the calling core after the physical GIC reported nothing more (pirq_aic_drain, wel2_aic.h). A
 * delivered hardware source is already masked by the EVENT read. */
void pirq_qemu_aic_irq(unsigned me)
{
    const struct pirq_aic_drain d = { AIC_KEY, 64, aic_note, (void *)(uintptr_t)me };

    pirq_aic_drain(&g_aic, &g_pirq, me, &d);
}
