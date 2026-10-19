// SPDX-License-Identifier: MIT
// Watari EL2 M1-2/M1-3: virtual CPU interface (ICH_*) fed from the vGIC model.
// Written from the GICv3 specification IHI 0069H.b: ICH_HCR_EL2 (12.4.5),
// ICH_LR<n>_EL2 (12.4.6), ICH_VTR_EL2 (12.4.9), ICC_SGI1R_EL1 (12.2.22) and
// Arm ARM D24.2 (ESR_EL2 ISS for EC 0x18).
//
// Mode: HW list registers. The guest's ICC_IAR1/EOIR1/PMR/IGRPEN1/BPR1 are not
// trapped (ICH_HCR_EL2.TALL0/TALL1/TC = 0, HCR_EL2.IMO = 1 turns them into the
// ICV_* registers). Only the SGI generation registers trap, which IMO/FMO = 1
// does by itself (GIC spec 4.6.4 p.4-67). On Apple M1 the SEIS erratum will
// require TALL1/TC = 1 (TRAP_EMULATE, RISKS R-01); this file is the HW mode.
//
// Life cycle of one interrupt: vgic_next_pending() -> vgic_ack() (the model
// marks it active: the LR owns the pending/active state from now on) -> LR
// written Pending with EOI=1 -> guest acks (LR Active) and EOIs (LR Invalid,
// maintenance interrupt PPI 25) -> harvest: vgic_eoi() and LR freed. For a
// level line that is still high the model re-pends it and the next flush
// injects it again (level semantics).
//
// M1-3: everything here is per core (index = this_cpu() = vCPU index); the
// shared vGIC model is only touched with the big lock held (taken in
// el2_exception). SPIs: a physical SPI that the guest enabled arrives on the
// core its IROUTER selects; EL2 masks it at the physical GICD, EOIs it and
// raises the model line; at the guest's EOI the line is dropped and the SPI is
// unmasked again, so a level interrupt that is still asserted re-fires.

#include "boot.h"

#define ICH_HCR_EN     (1ULL << 0)

#define LR_STATE_SHIFT 62
#define LR_STATE_MASK  (3ULL << LR_STATE_SHIFT)
#define LR_PENDING     (1ULL << LR_STATE_SHIFT)
#define LR_GROUP1      (1ULL << 60)
#define LR_EOI         (1ULL << 41)   /* HW=0: maintenance IRQ on deactivation */

#define MAX_LRS 16

struct cif {
    int16_t owner[MAX_LRS];            /* vINTID held by the LR, -1 = free */
    bool vtimer_masked;                /* PPI 27 masked at the physical GIC */
    unsigned st_inject, st_maint, st_timer, st_eoi, st_spi, st_kick;
};

static struct vgic *g_v;
static unsigned g_nlr;
static struct cif g_cif[MAX_CPUS];
/* SPIs masked at the physical distributor while the guest owns them. */
static uint32_t g_spi_masked[VGIC_NR_IRQS / 32];

#define LR_CASE(i) case i: __asm__ volatile("mrs %0, ich_lr" #i "_el2" : "=r"(v)); break;
static uint64_t lr_read(unsigned n)
{
    uint64_t v = 0;
    switch (n) {
    LR_CASE(0) LR_CASE(1) LR_CASE(2) LR_CASE(3) LR_CASE(4) LR_CASE(5)
    LR_CASE(6) LR_CASE(7) LR_CASE(8) LR_CASE(9) LR_CASE(10) LR_CASE(11)
    LR_CASE(12) LR_CASE(13) LR_CASE(14) LR_CASE(15)
    }
    return v;
}
#undef LR_CASE

#define LR_CASE(i) case i: __asm__ volatile("msr ich_lr" #i "_el2, %0" : : "r"(v)); break;
static void lr_write(unsigned n, uint64_t v)
{
    switch (n) {
    LR_CASE(0) LR_CASE(1) LR_CASE(2) LR_CASE(3) LR_CASE(4) LR_CASE(5)
    LR_CASE(6) LR_CASE(7) LR_CASE(8) LR_CASE(9) LR_CASE(10) LR_CASE(11)
    LR_CASE(12) LR_CASE(13) LR_CASE(14) LR_CASE(15)
    }
    __asm__ volatile("isb");
}
#undef LR_CASE

/* Per-core part: list registers cleared, virtual CPU interface on, virtual
 * timer visible to EL1. Called on the boot core and on every secondary. */
void cpuif_init_cpu(void)
{
    struct cif *c = &g_cif[this_cpu()];
    uint64_t vtr;

    __asm__ volatile("mrs %0, ich_vtr_el2" : "=r"(vtr));
    g_nlr = (unsigned)(vtr & 0x1f) + 1;      /* ListRegs = number of LRs - 1 */
    if (g_nlr > MAX_LRS)
        g_nlr = MAX_LRS;
    for (unsigned i = 0; i < g_nlr; i++) {
        lr_write(i, 0);
        c->owner[i] = -1;
    }
    c->vtimer_masked = false;
    /* Virtual CPU interface on, no traps (HW mode). */
    __asm__ volatile("msr ich_hcr_el2, %0; isb" : : "r"(ICH_HCR_EN));

    /* Virtual timer: guest sees the system counter unscaled (CNTVOFF = 0) and
     * has EL1 access to the physical counter/timer (CNTHCTL_EL2.EL1PCTEN/PCEN). */
    __asm__ volatile("msr cntvoff_el2, xzr");
    __asm__ volatile("msr cnthctl_el2, %0" : : "r"(3ULL));
    __asm__ volatile("isb");
}

void cpuif_init(struct vgic *v)
{
    uint64_t vtr;

    g_v = v;
    __asm__ volatile("mrs %0, ich_vtr_el2" : "=r"(vtr));
    put_kv("Watari EL2: ICH_VTR_EL2 = ", vtr);
    put_kv("Watari EL2: list registers (LRs) = ", (vtr & 0x1f) + 1);
    put_kv("Watari EL2: ICH_VTR_EL2.PRIbits+1 = ", ((vtr >> 29) & 7) + 1);
    put_kv("Watari EL2: ICH_VTR_EL2.SEIS = ", (vtr >> 22) & 1);
    put_kv("Watari EL2: ICH_VTR_EL2.TDS = ", (vtr >> 19) & 1);
    cpuif_init_cpu();
}

/* CPU_OFF: this core's virtual interface goes away with its guest context. */
void cpuif_cpu_down(void)
{
    struct cif *c = &g_cif[this_cpu()];

    for (unsigned i = 0; i < g_nlr; i++) {
        if (c->owner[i] >= 32) {                 /* give an SPI back to the device */
            uint32_t id = (uint32_t)c->owner[i];

            vgic_set_irq_level(g_v, this_cpu(), id, false);
            vgic_eoi(g_v, this_cpu(), id);
            if (g_spi_masked[id / 32] & (1u << (id % 32))) {
                g_spi_masked[id / 32] &= ~(1u << (id % 32));
                gic_phys_spi_enable(id, true);
            }
        }
        lr_write(i, 0);
        c->owner[i] = -1;
    }
    c->vtimer_masked = false;
    __asm__ volatile("msr ich_hcr_el2, xzr; isb");
}

/* CNTV_CTL_EL0: ENABLE[0], IMASK[1], ISTATUS[2]. The timer interrupt line is
 * asserted while ENABLE && ISTATUS && !IMASK (Arm ARM D12 generic timer). */
static bool vtimer_line(void)
{
    uint64_t ctl;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(ctl));
    return (ctl & 5) == 5 && !(ctl & 2);
}

/* Free the LRs whose interrupt the guest has deactivated. */
static void harvest(unsigned me)
{
    struct cif *c = &g_cif[me];

    for (unsigned i = 0; i < g_nlr; i++) {
        uint32_t id;

        if (c->owner[i] < 0)
            continue;
        if ((lr_read(i) & LR_STATE_MASK) != 0)
            continue;                        /* still pending and/or active */
        id = (uint32_t)c->owner[i];
        if (id >= 32)                        /* SPI: the device decides again */
            vgic_set_irq_level(g_v, me, id, false);
        vgic_eoi(g_v, me, id);
        c->st_eoi++;
        lr_write(i, 0);                      /* also clears the EOI maintenance source */
        c->owner[i] = -1;
        if (id >= 32 && (g_spi_masked[id / 32] & (1u << (id % 32)))) {
            g_spi_masked[id / 32] &= ~(1u << (id % 32));
            gic_phys_spi_enable(id, true);   /* still asserted -> fires again */
        }
    }
}

void cpuif_flush(void)
{
    unsigned me = this_cpu();
    struct cif *c = &g_cif[me];

    harvest(me);

    /* Level-sensitive virtual timer: the model line follows the timer. While
     * the line is high the physical PPI stays masked at EL2 (it would fire
     * again immediately); once the guest has silenced the timer, unmask. */
    bool line = vtimer_line();
    vgic_set_irq_level(g_v, me, PPI_VTIMER, line);
    if (!line && c->vtimer_masked) {
        c->vtimer_masked = false;
        gic_phys_ppi_enable(PPI_VTIMER, true);
    }

    for (unsigned n = 0; n < g_nlr; n++) {
        uint32_t id;
        uint8_t prio;

        if (c->owner[n] >= 0)
            continue;
        if (!vgic_next_pending(g_v, me, &id, &prio))
            break;
        vgic_ack(g_v, me);                   /* picks the same interrupt */
        lr_write(n, LR_PENDING | LR_GROUP1 | LR_EOI | ((uint64_t)prio << 48) | id);
        c->owner[n] = (int16_t)id;
        c->st_inject++;
    }
}

/* After any change of the shared model made by this core (MMIO write, SGI
 * generation, SPI arrival): every other running core with a deliverable
 * interrupt gets a physical SGI_KICK; it takes the IRQ at EL2 and runs
 * cpuif_flush() for its own vCPU (it may be in WFI at EL1, or busy). */
void cpuif_kick_others(void)
{
    unsigned me = this_cpu();

    for (unsigned j = 0; j < g_ncpu; j++) {
        if (j == me || g_pcpu[j].state != CPU_ST_ON)
            continue;
        /* No free list register: a kick would be useless (and two cores in
         * that state would kick each other forever). The LR underflow
         * interrupt (ICH_HCR_EL2.UIE) is the proper fix, later. */
        bool room = false;

        for (unsigned n = 0; n < g_nlr; n++)
            room |= g_cif[j].owner[n] < 0;
        if (room && vgic_next_pending(g_v, j, 0, 0)) {
            g_cif[me].st_kick++;
            gic_phys_kick(j);
        }
    }
}

/* Physical IRQ taken at EL2 (HCR_EL2.IMO = 1). */
void cpuif_irq(void)
{
    unsigned me = this_cpu();
    struct cif *c = &g_cif[me];

    for (;;) {
        uint32_t id = gic_phys_ack();

        if (id >= 1020)
            break;                           /* spurious / none pending */
        if (id == PPI_VTIMER) {
            /* Mask the level PPI at the physical GIC, then deactivate. The
             * model line is refreshed from CNTV_CTL in cpuif_flush(). */
            c->st_timer++;
            c->vtimer_masked = true;
            gic_phys_ppi_enable(PPI_VTIMER, false);
        } else if (id == PPI_MAINT) {
            /* The maintenance line is level: free the LRs (which removes its
             * source) before the EOI, or it would re-fire immediately. */
            c->st_maint++;
            cpuif_flush();
        } else if (id == SGI_KICK) {
            /* Another core changed the model: the flush below delivers. */
        } else if (id >= 32) {
            /* Forwarded SPI: mask at the physical GICD, raise the model line. */
            c->st_spi++;
            g_spi_masked[id / 32] |= 1u << (id % 32);
            gic_phys_spi_enable(id, false);
            vgic_set_irq_level(g_v, me, id, true);
        } else {
            put_kv("Watari EL2: unexpected physical INTID ", id);
        }
        gic_phys_eoi(id);
    }
    cpuif_flush();
    cpuif_kick_others();
}

/* vGIC effects (INTERFACES section 5): wire the guest's SPI configuration to
 * the physical distributor. Called from inside vgic_mmio_*, big lock held. */
void cpuif_vgic_effect(enum vgic_effect eff, uint32_t intid, int arg)
{
    switch (eff) {
    case VGIC_EFFECT_SET_TARGET:
        gic_phys_spi_route(intid, arg);
        break;
    case VGIC_EFFECT_UNMASK_SPI:
        g_spi_masked[intid / 32] &= ~(1u << (intid % 32));
        gic_phys_spi_enable(intid, true);
        break;
    case VGIC_EFFECT_MASK_SPI:
        g_spi_masked[intid / 32] &= ~(1u << (intid % 32));
        gic_phys_spi_enable(intid, false);
        break;
    }
}

/* Trapped MSR to ICC_SGI1R_EL1 / ICC_ASGI1R_EL1 / ICC_SGI0R_EL1.
 * ESR_EL2 ISS (Arm ARM D24.2, EC 0x18): Op0[21:20] Op2[19:17] Op1[16:14]
 * CRn[13:10] Rt[9:5] CRm[4:1] Direction[0] (0 = write).
 * ICC_SGI1R_EL1 = (3, 0, 12, 11, 5); ASGI1R = op2 6; SGI0R = op2 7. */
bool cpuif_sysreg_trap(struct frame *f)
{
    static const char *const msg[3] = {
        "Watari EL2: ICC_SGI1R_EL1 write trapped, value = ",
        "Watari EL2: ICC_ASGI1R_EL1 write trapped, value = ",
        "Watari EL2: ICC_SGI0R_EL1 write trapped, value = ",
    };
    uint32_t iss = f->esr & 0x1ffffff;
    uint32_t op0 = (iss >> 20) & 3, op2 = (iss >> 17) & 7, op1 = (iss >> 14) & 7;
    uint32_t crn = (iss >> 10) & 15, rt = (iss >> 5) & 31, crm = (iss >> 1) & 15;
    uint32_t dir = iss & 1;

    if (dir || op0 != 3 || op1 != 0 || crn != 12 || crm != 11 || op2 < 5 || op2 > 7)
        return false;
    uint64_t val = rt == 31 ? 0 : f->x[rt];
    uint32_t mask = vgic_sgi_send_mask(g_v, this_cpu(), val);
    put_kv(msg[op2 - 5], val);
    put_kv("Watari EL2:   vCPU target mask = ", mask);
    f->elr += 4;
    return true;
}

void cpuif_stats(void)
{
    unsigned t[6] = { 0, 0, 0, 0, 0, 0 };

    for (unsigned i = 0; i < g_ncpu; i++) {
        t[0] += g_cif[i].st_inject;
        t[1] += g_cif[i].st_eoi;
        t[2] += g_cif[i].st_timer;
        t[3] += g_cif[i].st_maint;
        t[4] += g_cif[i].st_spi;
        t[5] += g_cif[i].st_kick;
    }
    put_kv("Watari EL2: stats: LR injections    = ", t[0]);
    put_kv("Watari EL2: stats: guest EOIs (vgic_eoi) = ", t[1]);
    put_kv("Watari EL2: stats: physical PPI 27 irqs  = ", t[2]);
    put_kv("Watari EL2: stats: maintenance irqs (PPI 25) = ", t[3]);
    put_kv("Watari EL2: stats: forwarded SPIs = ", t[4]);
    put_kv("Watari EL2: stats: cross-core kicks (SGI 15) = ", t[5]);
}
