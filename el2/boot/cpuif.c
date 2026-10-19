// SPDX-License-Identifier: MIT
// Watari EL2 M1-2: virtual CPU interface (ICH_*) fed from the vGIC model.
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

#include "boot.h"

#define ICH_HCR_EN     (1ULL << 0)

#define LR_STATE_SHIFT 62
#define LR_STATE_MASK  (3ULL << LR_STATE_SHIFT)
#define LR_PENDING     (1ULL << LR_STATE_SHIFT)
#define LR_GROUP1      (1ULL << 60)
#define LR_EOI         (1ULL << 41)   /* HW=0: maintenance IRQ on deactivation */

#define MAX_LRS 16

static struct vgic *g_v;
static unsigned g_nlr;
static int16_t g_owner[MAX_LRS];       /* vINTID held by the LR, -1 = free */
static unsigned g_st_inject, g_st_maint, g_st_timer, g_st_eoi;
static bool g_vtimer_masked;           /* PPI 27 masked at the physical GIC */

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

void cpuif_init(struct vgic *v)
{
    uint64_t vtr;

    g_v = v;
    __asm__ volatile("mrs %0, ich_vtr_el2" : "=r"(vtr));
    g_nlr = (unsigned)(vtr & 0x1f) + 1;      /* ListRegs = number of LRs - 1 */
    if (g_nlr > MAX_LRS)
        g_nlr = MAX_LRS;
    put_kv("Watari EL2: ICH_VTR_EL2 = ", vtr);
    put_kv("Watari EL2: list registers (LRs) = ", g_nlr);
    put_kv("Watari EL2: ICH_VTR_EL2.PRIbits+1 = ", ((vtr >> 29) & 7) + 1);
    put_kv("Watari EL2: ICH_VTR_EL2.SEIS = ", (vtr >> 22) & 1);
    put_kv("Watari EL2: ICH_VTR_EL2.TDS = ", (vtr >> 19) & 1);

    for (unsigned i = 0; i < g_nlr; i++) {
        lr_write(i, 0);
        g_owner[i] = -1;
    }
    /* Virtual CPU interface on, no traps (HW mode). */
    __asm__ volatile("msr ich_hcr_el2, %0; isb" : : "r"(ICH_HCR_EN));

    /* Virtual timer: guest sees the system counter unscaled (CNTVOFF = 0) and
     * has EL1 access to the physical counter/timer (CNTHCTL_EL2.EL1PCTEN/PCEN). */
    __asm__ volatile("msr cntvoff_el2, xzr");
    __asm__ volatile("msr cnthctl_el2, %0" : : "r"(3ULL));
    __asm__ volatile("isb");
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
static void harvest(void)
{
    for (unsigned i = 0; i < g_nlr; i++) {
        if (g_owner[i] < 0)
            continue;
        if ((lr_read(i) & LR_STATE_MASK) != 0)
            continue;                        /* still pending and/or active */
        vgic_eoi(g_v, 0, (uint32_t)g_owner[i]);
        g_st_eoi++;
        lr_write(i, 0);                      /* also clears the EOI maintenance source */
        g_owner[i] = -1;
    }
}

void cpuif_flush(void)
{
    harvest();

    /* Level-sensitive virtual timer: the model line follows the timer. While
     * the line is high the physical PPI stays masked at EL2 (it would fire
     * again immediately); once the guest has silenced the timer, unmask. */
    bool line = vtimer_line();
    vgic_set_irq_level(g_v, 0, PPI_VTIMER, line);
    if (!line && g_vtimer_masked) {
        g_vtimer_masked = false;
        gic_phys_ppi_enable(PPI_VTIMER, true);
    }

    for (unsigned n = 0; n < g_nlr; n++) {
        uint32_t id;
        uint8_t prio;

        if (g_owner[n] >= 0)
            continue;
        if (!vgic_next_pending(g_v, 0, &id, &prio))
            break;
        vgic_ack(g_v, 0);                    /* picks the same interrupt */
        lr_write(n, LR_PENDING | LR_GROUP1 | LR_EOI | ((uint64_t)prio << 48) | id);
        g_owner[n] = (int16_t)id;
        g_st_inject++;
    }
}

/* Physical IRQ taken at EL2 (HCR_EL2.IMO = 1). */
void cpuif_irq(void)
{
    for (;;) {
        uint32_t id = gic_phys_ack();

        if (id >= 1020)
            break;                           /* spurious / none pending */
        if (id == PPI_VTIMER) {
            /* Mask the level PPI at the physical GIC, then deactivate. The
             * model line is refreshed from CNTV_CTL in cpuif_flush(). */
            g_st_timer++;
            g_vtimer_masked = true;
            gic_phys_ppi_enable(PPI_VTIMER, false);
        } else if (id == PPI_MAINT) {
            /* The maintenance line is level: free the LRs (which removes its
             * source) before the EOI, or it would re-fire immediately. */
            g_st_maint++;
            cpuif_flush();
        } else {
            put_kv("Watari EL2: unexpected physical INTID ", id);
        }
        gic_phys_eoi(id);
    }
    cpuif_flush();
}

/* Trapped MSR to ICC_SGI1R_EL1 / ICC_ASGI1R_EL1 / ICC_SGI0R_EL1.
 * ESR_EL2 ISS (Arm ARM D24.2, EC 0x18): Op0[21:20] Op2[19:17] Op1[16:14]
 * CRn[13:10] Rt[9:5] CRm[4:1] Direction[0] (0 = write).
 * ICC_SGI1R_EL1 = (3, 0, 12, 11, 5); ASGI1R = op2 6; SGI0R = op2 7. */
bool cpuif_sysreg_trap(struct frame *f)
{
    uint32_t iss = f->esr & 0x1ffffff;
    uint32_t op0 = (iss >> 20) & 3, op2 = (iss >> 17) & 7, op1 = (iss >> 14) & 7;
    uint32_t crn = (iss >> 10) & 15, rt = (iss >> 5) & 31, crm = (iss >> 1) & 15;
    uint32_t dir = iss & 1;

    if (dir || op0 != 3 || op1 != 0 || crn != 12 || crm != 11 || op2 < 5 || op2 > 7)
        return false;
    uint64_t val = rt == 31 ? 0 : f->x[rt];
    int n = vgic_sgi_send(g_v, 0, val);
    puts_("Watari EL2: ICC_SGI");
    putc_(op2 == 5 ? '1' : (op2 == 6 ? 'A' : '0'));
    put_kv("R_EL1 write trapped, value = ", val);
    put_kv("Watari EL2:   vCPUs targeted = ", (uint64_t)n);
    f->elr += 4;
    return true;
}

void cpuif_stats(void)
{
    put_kv("Watari EL2: stats: LR injections    = ", g_st_inject);
    put_kv("Watari EL2: stats: guest EOIs (vgic_eoi) = ", g_st_eoi);
    put_kv("Watari EL2: stats: physical PPI 27 irqs  = ", g_st_timer);
    put_kv("Watari EL2: stats: maintenance irqs (PPI 25) = ", g_st_maint);
}
