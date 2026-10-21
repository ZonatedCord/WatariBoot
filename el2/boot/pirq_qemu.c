// SPDX-License-Identifier: MIT
// Watari EL2 M2.x-2: pirq backend on the QEMU virt physical GICv3 (the twin of the future
// Apple AIC backend, #59: same struct pirq_ops, policy in ../vgic/pirq.c, host-tested).
//
// Behaviour is the one the layer had before the indirection (M1-3, F-25): physical INTID =
// vINTID (identity map for SPIs 32..959), a level SPI is disabled at the GICD when it fires
// and re-enabled at the guest's deactivate, ICFGR comes from the guest, and the event read
// (ICC_IAR1) does not mask anything (auto_masks = false).

#include "boot.h"

struct pirq g_pirq;

static void q_mask(void *c, uint32_t p)               { (void)c; gic_phys_spi_enable(p, false); }
static void q_unmask(void *c, uint32_t p)             { (void)c; gic_phys_spi_enable(p, true); }
static void q_target(void *c, uint32_t p, int vcpu)   { (void)c; gic_phys_spi_route(p, vcpu); }
static void q_trigger(void *c, uint32_t p, bool edge) { (void)c; gic_phys_spi_trigger(p, edge); }

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
