// SPDX-License-Identifier: MIT
// Watari EL2 M1-2..M2 prep: virtual CPU interface (ICH_*) fed from the vGIC model.
// Written from the GICv3 specification IHI 0069H.b: ICH_HCR_EL2 (12.4.5),
// ICH_LR<n>_EL2 (12.4.6), ICH_VMCR_EL2 (12.4.8), ICH_VTR_EL2 (12.4.9),
// ICH_AP0R/AP1R<n>_EL2, ICH_MISR_EL2, ICC_SGI1R_EL1 (12.2.22) and Arm ARM D24.2
// (ESR_EL2 ISS for EC 0x18). The policy (which interrupt goes into which list
// register, what an emulated ICC access does) lives in ../vgic/vcif.c and is
// host-tested; this file only moves the registers between hardware and that model.
//
// Two modes, chosen at boot (g_cpuif_trap; build default WEL2_CPUIF_MODE, boot
// argument "wel2.cpuif=hw|trap" in /chosen/bootargs overrides):
//  * HW: the guest's ICC_IAR1/EOIR1/PMR/IGRPEN1/BPR1 are not trapped
//    (ICH_HCR_EL2.TALL0/TALL1/TC = 0, HCR_EL2.IMO = 1 turns them into ICV_*);
//    the hardware runs them against the list registers.
//  * TRAP_EMULATE (#52, RISKS R-01): ICH_HCR_EL2.TALL0/TALL1/TC (+TDIR) = 1; every
//    guest access to those registers traps (EC 0x18) and cpuif_sysreg_trap()
//    executes it with vcif_*() on a copy of LR / VMCR / AP state, then writes the
//    result back. Needed on the Apple M1 because of the SEIS erratum (guest GIC
//    register accesses must never reach the hardware); QEMU has ICH_VTR_EL2.SEIS=0
//    so only the logic is exercised, not the erratum (F-19).
// ICC_SGI*R_EL1 writes trap in both modes (IMO/FMO = 1, GIC spec 4.6.4 p.4-67).
//
// Life cycle of one interrupt: vgic_next_pending() -> vgic_ack() (the model
// marks it active: the LR owns the pending/active state from now on) -> LR
// written Pending with EOI=1 -> guest acks (LR Active) and EOIs (LR Invalid,
// maintenance interrupt PPI 25) -> harvest: vgic_eoi() and LR freed. For a
// level line that is still high the model re-pends it and the next flush
// injects it again (level semantics).
//
// LR overflow (#54): more pending interrupts than LRs wait in the model; they are
// refilled (a) at every guest EOI by the EOI maintenance interrupt of each LR
// and (b) by the underflow interrupt ICH_HCR_EL2.UIE (zero or one LR valid),
// armed only while interrupts overflow and at least two LRs are valid (so it
// cannot assert at once). NPIE is not used: it stays asserted while every valid
// LR is active, a condition EL2 cannot clear. A pending-only LR is evicted when
// a strictly higher priority interrupt waits (vcif_fill); a core with full LRs whose pending-only
// LR would be evicted is kicked by the core that queued the interrupt (cpuif_kick_others, #60).
//
// M1-3: everything here is per core (index = this_cpu() = vCPU index); the
// shared vGIC model is only touched with the big lock held (taken in
// el2_exception). SPIs: a physical SPI that the guest enabled arrives on the
// core its IROUTER selects; EL2 masks it at the physical GICD, EOIs it and
// raises the model line; at the guest's EOI the line is dropped and the SPI is
// unmasked again, so a level interrupt that is still asserted re-fires.

#include "boot.h"
#include "wel2_vcif.h"

#define ICH_HCR_EN     (1ULL << 0)
#define ICH_HCR_UIE    (1ULL << 1)
#define ICH_HCR_TC     (1ULL << 10)
#define ICH_HCR_TALL0  (1ULL << 11)
#define ICH_HCR_TALL1  (1ULL << 12)
#define ICH_HCR_TDIR   (1ULL << 14)
#define ICH_MISR_U     (1ULL << 1)

#ifndef WEL2_CPUIF_TRAP_DEFAULT
#define WEL2_CPUIF_TRAP_DEFAULT 0
#endif

bool g_cpuif_trap = WEL2_CPUIF_TRAP_DEFAULT;
bool g_cpuif_noeoi;

struct cif {
    struct wel2_vcif vc;               /* mirror of LR / VMCR / AP, policy + ICC emulation */
    bool vtimer_masked;                /* PPI 27 masked at the physical GIC */
    uint64_t hcr_base, hcr_cur;        /* ICH_HCR_EL2 without / as currently written */
    unsigned nap;                      /* number of ICH_AP<g>R<n> registers */
    unsigned st_timer, st_spi, st_kick, st_kick_evict, st_maint, st_uie, st_lpi;
    unsigned st_iar, st_eoir, st_dir, st_pmr_w, st_pmr_r, st_other;
    unsigned pmr_reported;
};

static struct vgic *g_v;
static struct cif g_cif[MAX_CPUS];
/* SPI life cycle (mask while the guest owns a level source, edge pulses, ICFGR, routing,
 * re-target on CPU_OFF) lives in g_pirq (../vgic/pirq.c, host-tested); pirq_qemu.c is its
 * backend on the physical GICv3. */

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
}
#undef LR_CASE

/* ICH_AP0R<n>_EL2 / ICH_AP1R<n>_EL2, n = 0..3 (one register per 32 preemption levels). */
static uint32_t ap_read(unsigned grp, unsigned n)
{
    uint64_t v = 0;

    if (grp == 0) {
        switch (n) {
        case 0: __asm__ volatile("mrs %0, ich_ap0r0_el2" : "=r"(v)); break;
        case 1: __asm__ volatile("mrs %0, ich_ap0r1_el2" : "=r"(v)); break;
        case 2: __asm__ volatile("mrs %0, ich_ap0r2_el2" : "=r"(v)); break;
        case 3: __asm__ volatile("mrs %0, ich_ap0r3_el2" : "=r"(v)); break;
        }
    } else {
        switch (n) {
        case 0: __asm__ volatile("mrs %0, ich_ap1r0_el2" : "=r"(v)); break;
        case 1: __asm__ volatile("mrs %0, ich_ap1r1_el2" : "=r"(v)); break;
        case 2: __asm__ volatile("mrs %0, ich_ap1r2_el2" : "=r"(v)); break;
        case 3: __asm__ volatile("mrs %0, ich_ap1r3_el2" : "=r"(v)); break;
        }
    }
    return (uint32_t)v;
}

static void ap_write(unsigned grp, unsigned n, uint32_t val)
{
    uint64_t v = val;

    if (grp == 0) {
        switch (n) {
        case 0: __asm__ volatile("msr ich_ap0r0_el2, %0" : : "r"(v)); break;
        case 1: __asm__ volatile("msr ich_ap0r1_el2, %0" : : "r"(v)); break;
        case 2: __asm__ volatile("msr ich_ap0r2_el2, %0" : : "r"(v)); break;
        case 3: __asm__ volatile("msr ich_ap0r3_el2, %0" : : "r"(v)); break;
        }
    } else {
        switch (n) {
        case 0: __asm__ volatile("msr ich_ap1r0_el2, %0" : : "r"(v)); break;
        case 1: __asm__ volatile("msr ich_ap1r1_el2, %0" : : "r"(v)); break;
        case 2: __asm__ volatile("msr ich_ap1r2_el2, %0" : : "r"(v)); break;
        case 3: __asm__ volatile("msr ich_ap1r3_el2, %0" : : "r"(v)); break;
        }
    }
}

/* Hardware -> mirror. The LRs always (the guest may have changed them in HW mode);
 * VMCR and AP only when an ICC access is going to be emulated. */
static void hw_in(struct cif *c, bool full)
{
    struct wel2_vcif *v = &c->vc;

    for (unsigned i = 0; i < v->nlr; i++)
        v->lr[i] = lr_read(i);
    v->lr_dirty = 0;
    if (full) {
        uint64_t m;

        __asm__ volatile("mrs %0, ich_vmcr_el2" : "=r"(m));
        v->vmcr = (uint32_t)m;
        for (unsigned n = 0; n < c->nap; n++) {
            v->ap0r[n] = ap_read(0, n);
            v->ap1r[n] = ap_read(1, n);
        }
        v->vmcr_dirty = v->ap_dirty = false;
    }
}

/* Mirror -> hardware: only what changed. */
static void hw_out(struct cif *c)
{
    struct wel2_vcif *v = &c->vc;

    for (unsigned i = 0; i < v->nlr; i++)
        if (v->lr_dirty & (1u << i))
            lr_write(i, v->lr[i]);
    v->lr_dirty = 0;
    if (v->vmcr_dirty) {
        __asm__ volatile("msr ich_vmcr_el2, %0" : : "r"((uint64_t)v->vmcr));
        v->vmcr_dirty = false;
    }
    if (v->ap_dirty) {
        for (unsigned n = 0; n < c->nap; n++) {
            ap_write(0, n, v->ap0r[n]);
            ap_write(1, n, v->ap1r[n]);
        }
        v->ap_dirty = false;
    }
    __asm__ volatile("isb");
}

static void hcr_set(struct cif *c, bool uie)
{
    uint64_t want = c->hcr_base | (uie ? ICH_HCR_UIE : 0);

    if (want != c->hcr_cur) {
        c->hcr_cur = want;
        __asm__ volatile("msr ich_hcr_el2, %0; isb" : : "r"(want));
    }
}

/* Per-core part: list registers cleared, virtual CPU interface on (with or
 * without the ICC traps), virtual timer visible to EL1. Called on the boot core
 * and on every secondary. */
void cpuif_init_cpu(void)
{
    struct cif *c = &g_cif[this_cpu()];
    uint64_t vtr;

    __asm__ volatile("mrs %0, ich_vtr_el2" : "=r"(vtr));
    unsigned nlr = (unsigned)(vtr & 0x1f) + 1;           /* ListRegs = number of LRs - 1 */
    unsigned pribits = (unsigned)((vtr >> 29) & 7) + 1;  /* PRIbits */
    unsigned prebits = (unsigned)((vtr >> 26) & 7) + 1;  /* PREbits */
    bool tds = (vtr >> 19) & 1;                          /* ICC_DIR trap supported */

    struct cif keep = *c;                                /* statistics survive CPU_OFF -> CPU_ON */

    *c = (struct cif){ 0 };
    c->st_timer = keep.st_timer; c->st_spi = keep.st_spi; c->st_lpi = keep.st_lpi; c->st_kick = keep.st_kick; c->st_kick_evict = keep.st_kick_evict;
    c->st_maint = keep.st_maint; c->st_uie = keep.st_uie; c->st_iar = keep.st_iar;
    c->st_eoir = keep.st_eoir; c->st_dir = keep.st_dir; c->st_pmr_w = keep.st_pmr_w;
    c->st_pmr_r = keep.st_pmr_r; c->st_other = keep.st_other; c->pmr_reported = keep.pmr_reported;
    vcif_init(&c->vc, nlr, pribits, prebits);
    c->vc.st_inject = keep.vc.st_inject; c->vc.st_evict = keep.vc.st_evict;
    c->vc.st_eoi = keep.vc.st_eoi; c->vc.eoicount = keep.vc.eoicount;
    c->vc.no_eoi_maint = g_cpuif_noeoi;
    vcif_set_idbits(&c->vc, vgic_idbits(g_v));
    c->nap = 1u << (c->vc.prebits - 5);
    hw_out(c);                                           /* LRs 0, VMCR reset, AP 0 */

    c->hcr_base = ICH_HCR_EN;
    if (g_cpuif_trap)
        c->hcr_base |= ICH_HCR_TALL0 | ICH_HCR_TALL1 | ICH_HCR_TC | (tds ? ICH_HCR_TDIR : 0);
    c->hcr_cur = ~0ULL;                                  /* force the write */
    hcr_set(c, false);

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
    put_kv("Watari EL2: ICH_VTR_EL2.PREbits+1 = ", ((vtr >> 26) & 7) + 1);
    put_kv("Watari EL2: ICH_VTR_EL2.SEIS = ", (vtr >> 22) & 1);
    put_kv("Watari EL2: ICH_VTR_EL2.TDS = ", (vtr >> 19) & 1);
    if (g_cpuif_noeoi)
        puts_("Watari EL2: TEST knob wel2.test=noeoi: list registers without EOI maintenance (UIE refills)\n");
    puts_(g_cpuif_trap
              ? "Watari EL2: cpuif mode = TRAP_EMULATE (TALL0|TALL1|TC|TDIR, ICC_* emulated in EL2)\n"
              : "Watari EL2: cpuif mode = HW (list registers, no ICC traps)\n");
    cpuif_init_cpu();
}

/* CPU_OFF: this core's virtual interface goes away with its guest context. */
void cpuif_cpu_down(void)
{
    unsigned me = this_cpu();
    struct cif *c = &g_cif[me];
    struct wel2_vcif *v = &c->vc;

    for (unsigned i = 0; i < v->nlr; i++) {
        if (v->owner[i] >= (int)VGIC_LPI_BASE) {      /* an LPI in flight: just forget it */
            vgic_eoi(g_v, me, (uint32_t)v->owner[i]);
        } else if (v->owner[i] >= 32) {          /* give an SPI back to the device */
            uint32_t id = (uint32_t)v->owner[i];

            pirq_line_low(&g_pirq, me, id);
            vgic_eoi(g_v, me, id);
            pirq_finish(&g_pirq, id);
        }
        v->lr[i] = 0;
        v->owner[i] = -1;
        v->lr_dirty |= 1u << i;
    }
    vcif_reset_vmcr(v);                          /* next CPU_ON starts from a clean interface */
    for (unsigned n = 0; n < 4; n++)
        v->ap0r[n] = v->ap1r[n] = 0;
    v->ap_dirty = true;
    hw_out(c);
    c->vtimer_masked = false;
    __asm__ volatile("msr ich_hcr_el2, xzr; isb");
    c->hcr_cur = ~0ULL;
}

/* CNTV_CTL_EL0: ENABLE[0], IMASK[1], ISTATUS[2]. The timer interrupt line is
 * asserted while ENABLE && ISTATUS && !IMASK (Arm ARM D12 generic timer). */
static bool vtimer_line(void)
{
    uint64_t ctl;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(ctl));
    return (ctl & 5) == 5 && !(ctl & 2);
}

void cpuif_flush(void)
{
    unsigned me = this_cpu();
    struct cif *c = &g_cif[me];
    struct wel2_vcif *v = &c->vc;
    uint32_t freed[VCIF_MAX_LRS];

    hw_in(c, false);

    /* LRs the guest deactivated are freed (and the model told). SPIs: the device
     * decides again; a masked SPI is unmasked, so a still-asserted level fires again. */
    unsigned nfreed = vcif_harvest(v, g_v, me, freed, VCIF_MAX_LRS);
    for (unsigned i = 0; i < nfreed; i++) {
        uint32_t id = freed[i];

        if (id < 32 || id >= VGIC_LPI_BASE)
            continue;
        if (g_trace_on) {
            static unsigned n_hv;

            if (n_hv++ < 100)
                put_kv("Watari EL2: TR guest finished SPI, INTID ", id);
        }
        pirq_line_low(&g_pirq, me, id);
        pirq_finish(&g_pirq, id);            /* a level line still asserted fires again */
    }

    /* Level-sensitive virtual timer: the model line follows the timer. While
     * the line is high the physical PPI stays masked at EL2 (it would fire
     * again immediately); once the guest has silenced the timer, unmask. */
    bool line = vtimer_line();
    vgic_set_irq_level(g_v, me, PPI_VTIMER, line);
    if (!line && c->vtimer_masked) {
        c->vtimer_masked = false;
        gic_phys_ppi_enable(PPI_VTIMER, true);
    }

    vcif_fill(v, g_v, me);                   /* priority order, evicting if need be */
    hw_out(c);

    /* Underflow interrupt only while interrupts overflow the LRs and >= 2 LRs
     * are valid (with fewer it would assert immediately and EL2 could not clear it). */
    hcr_set(c, v->nlr >= 2 && vcif_valid_lrs(v) >= 2 && vcif_overflow(v, g_v, me));
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
        /* Kick only when the flush there would change something: a free list register, or
         * (#60) a pending-only LR of strictly worse priority that the waiting interrupt
         * evicts (vcif_fill_can_progress). The kicked core's own flush makes the predicate
         * false again, so two cores with full LRs cannot kick each other forever (the
         * kick-storm guard of F-20); a core whose LRs are all active or at least as good
         * is not kicked and refills by its EOI / underflow maintenance interrupts. */
        struct wel2_vcif *vj = &g_cif[j].vc;

        if (vcif_fill_can_progress(vj, g_v, j)) {
            bool room = false;

            for (unsigned n = 0; n < vj->nlr; n++)
                room |= vj->owner[n] < 0;
            g_cif[me].st_kick++;
            if (!room)
                g_cif[me].st_kick_evict++;
            gic_phys_kick(j);
        }
    }
}

/* Trapped-ICC progress evidence: PMR write counts feed #43 / RISKS R-05. Cores
 * report from the virtual-timer tick of core 0 (every 100th tick, only if the
 * count changed) and at SYSTEM_OFF via cpuif_stats(). */
static unsigned pmr_total(void)
{
    unsigned t = 0;

    for (unsigned i = 0; i < g_ncpu; i++)
        t += g_cif[i].st_pmr_w;
    return t;
}

static unsigned __attribute__((unused)) icc_total(void)          /* all trapped ICC accesses, to detect activity */
{
    unsigned t = 0;

    for (unsigned i = 0; i < g_ncpu; i++)
        t += g_cif[i].st_pmr_w + g_cif[i].st_pmr_r + g_cif[i].st_iar + g_cif[i].st_eoir +
             g_cif[i].st_dir + g_cif[i].st_other;
    return t;
}

static void icc_report(void)
{
    unsigned t = pmr_total(), a = 0, e = 0, r = 0, o = 0;

    for (unsigned i = 0; i < g_ncpu; i++) {
        a += g_cif[i].st_iar;
        e += g_cif[i].st_eoir + g_cif[i].st_dir;
        r += g_cif[i].st_pmr_r;
        o += g_cif[i].st_other;
    }
    uint64_t cnt, frq;

    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cnt));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    put_kv("Watari EL2:   report at CNTVCT = ", cnt);       /* M2: rate = delta writes / (delta CNTVCT / CNTFRQ) */
    put_kv("Watari EL2:   CNTFRQ = ", frq);
    put_kv("Watari EL2:   timer irqs on cpu 0 = ", g_cif[0].st_timer);
    put_kv("Watari EL2: trapped ICC_PMR_EL1 writes so far = ", t);
    put_kv("Watari EL2:   trapped ICC_PMR_EL1 reads = ", r);
    put_kv("Watari EL2:   trapped ICC_IAR1_EL1 reads = ", a);
    put_kv("Watari EL2:   trapped ICC_EOIR1/DIR writes = ", e);
    put_kv("Watari EL2:   other trapped ICC accesses = ", o);
}

/* Physical IRQ taken at EL2 (HCR_EL2.IMO = 1). */
void cpuif_irq(void)
{
    unsigned me = this_cpu();
    struct cif *c = &g_cif[me];

    for (;;) {
        uint32_t id = gic_phys_ack();

        if (id >= 1020 && id <= 1023)
            break;                           /* spurious / none pending */
        if (id == PPI_VTIMER) {
            /* Mask the level PPI at the physical GIC, then deactivate. The
             * model line is refreshed from CNTV_CTL in cpuif_flush(). */
            c->st_timer++;
            c->vtimer_masked = true;
            gic_phys_ppi_enable(PPI_VTIMER, false);
#ifdef WEL2_CHAIN
            /* M1-4: a firmware ticks every 10 ms; log only the first and every 10x. */
            for (unsigned p = 1; p <= 100000; p *= 10) {
                if (c->st_timer == p) {
                    put_kn("Watari EL2: cpu ", me, " virtual timer PPI 27 taken, injecting into the guest\n");
                    put_kv("Watari EL2:   timer irqs on this cpu so far = ", c->st_timer);
                    put_kv("Watari EL2:   vIRQ injections (this cpu's LRs) so far = ", c->vc.st_inject);
                }
            }
            if (g_cpuif_trap && me == 0 && c->st_timer % 100 == 0 && icc_total() != c->pmr_reported) {
                c->pmr_reported = icc_total();
                icc_report();
            }
#endif
        } else if (id == PPI_MAINT) {
            /* The maintenance line is level: free the LRs (which removes its
             * source) before the EOI, or it would re-fire immediately. */
            uint64_t misr;

            __asm__ volatile("mrs %0, ich_misr_el2" : "=r"(misr));
            c->st_maint++;
            if (misr & ICH_MISR_U)
                c->st_uie++;
            cpuif_flush();
        } else if (id == SGI_KICK) {
            /* Another core changed the model: the flush below delivers. */
        } else if (id >= VGIC_LPI_BASE) {
            /* Forwarded LPI (M2): the guest's ITS/redistributor programming reached the physical
             * GIC unchanged (gic_phys_lpi_reg), so an LPI arrives here as a physical interrupt.
             * Its priority is the byte of the LPI property table the guest set up (GIC spec 6.1.1:
             * bits [7:2] priority, bit 0 enable); queue it in the model. LPIs are edge: nothing
             * to mask, the physical EOI below completes it. */
            uint64_t prop = vgic_lpi_propbaser(g_v, me);
            uint8_t prio = 0xa0, cfg = 1;       /* default if the guest has not set the table up */

            if (vgic_lpi_enabled(g_v, me) && prop) {
                uint64_t a = (prop & 0x000FFFFFFFFFF000ULL) + (id - VGIC_LPI_BASE);

                cfg = *(volatile uint8_t *)a;
                prio = (uint8_t)(cfg & 0xfc);
            }

            c->st_lpi++;
            if (g_trace_on && c->st_lpi <= 100)
                put_kv("Watari EL2: TR physical LPI taken, INTID ", id);
            if (!(cfg & 1u))
                ;                               /* LPI disabled in the property table: just EOI */
            else if (!vgic_lpi_inject(g_v, me, id, prio))
                put_kv("Watari EL2: LPI queue full, dropped INTID ", id);
        } else if (id >= 1024) {
            put_kv("Watari EL2: ignoring unexpected physical INTID ", id);   /* 1024..8191: reserved */
        } else if (id >= 32) {
            /* Forwarded SPI: mask at the physical GICD, raise the model line. */
            c->st_spi++;
#ifndef WEL2_CHAIN
            {
                static unsigned n_log;

                if (n_log++ < 8) {       /* M2.x-2 re-target test: which core took the physical SPI */
                    put_kn("Watari EL2: physical SPI ", id, "");
                    put_kn(" taken on cpu ", me, "\n");
                }
            }
#endif
            if (g_trace_on && c->st_spi <= 200)
                put_kv("Watari EL2: TR physical SPI taken, INTID ", id);
            /* pirq: edge (v2m MSI) = a latched pulse, level = masked until the guest
             * deactivates, source not exposed = quieted. */
            pirq_arrive(&g_pirq, me, id);
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
    static unsigned n_tr;

    if (g_trace_on && intid >= 32 && (eff != VGIC_EFFECT_SET_TARGET || n_tr < 40) && n_tr < 600) {
        n_tr++;
        put_kv("Watari EL2: TR SPI effect (1=unmask 2=mask 3=target 4=trigger) ", (uint64_t)eff);
        put_kv("Watari EL2: TR    intid ", intid);
        put_kv("Watari EL2: TR    arg   ", (uint64_t)(int64_t)arg);
    }
    pirq_effect(&g_pirq, eff, intid, arg);
}

/* ---- Trapped system registers ----------------------------------------------
 * ESR_EL2 ISS (Arm ARM D24.2, EC 0x18): Op0[21:20] Op2[19:17] Op1[16:14]
 * CRn[13:10] Rt[9:5] CRm[4:1] Direction[0] (1 = read, MRS; 0 = write, MSR).
 * ICC_SGI1R_EL1 = (3, 0, 12, 11, 5); ASGI1R = op2 6; SGI0R = op2 7.
 * ICC registers emulated in TRAP_EMULATE mode (GIC spec 12.2; op0 = 3, op1 = 0):
 *   CRn 12 CRm 12: IAR1 0 (r), EOIR1 1 (w), HPPIR1 2 (r), BPR1 3, CTLR 4, IGRPEN0 6, IGRPEN1 7
 *   CRn 12 CRm  8: IAR0 0 (r), EOIR0 1 (w), HPPIR0 2 (r), BPR0 3, AP0R<n> 4+n
 *   CRn 12 CRm  9: AP1R<n> n
 *   CRn 12 CRm 11: DIR 1 (w), RPR 3 (r)
 *   CRn  4 CRm  6: PMR 0
 */
static bool icc_emulate(struct frame *f, uint32_t crn, uint32_t crm, uint32_t op2,
                        uint32_t rt, bool rd)
{
    unsigned me = this_cpu();
    struct cif *c = &g_cif[me];
    struct wel2_vcif *v = &c->vc;
    uint64_t val = (!rd && rt != 31) ? f->x[rt] : 0, ret = 0;
    bool known = true;

    hw_in(c, true);
    if (crn == 4 && crm == 6 && op2 == 0) {                     /* ICC_PMR_EL1 */
        if (rd) {
            c->st_pmr_r++;
            ret = vcif_pmr_get(v);
        } else {
            c->st_pmr_w++;
            vcif_pmr_set(v, (uint32_t)val);
        }
    } else if (crn == 12 && crm == 12) {
        switch (op2) {
        case 0: if (!rd) { known = false; break; }              /* ICC_IAR1_EL1 */
                c->st_iar++; ret = vcif_iar1(v); break;
        case 1: if (rd) { known = false; break; }               /* ICC_EOIR1_EL1 */
                c->st_eoir++; vcif_eoir1(v, (uint32_t)val); break;
        case 2: if (!rd) { known = false; break; }              /* ICC_HPPIR1_EL1 */
                c->st_other++; ret = vcif_hppir1(v); break;
        case 3: c->st_other++;                                  /* ICC_BPR1_EL1 */
                if (rd) ret = vcif_bpr1_get(v); else vcif_bpr1_set(v, (uint32_t)val);
                break;
        case 4: c->st_other++;                                  /* ICC_CTLR_EL1 */
                if (rd) ret = vcif_ctlr_get(v); else vcif_ctlr_set(v, (uint32_t)val);
                break;
        case 6: c->st_other++;                                  /* ICC_IGRPEN0_EL1 */
                if (rd) ret = vcif_igrpen_get(v, 0); else vcif_igrpen_set(v, 0, (uint32_t)val);
                break;
        case 7: c->st_other++;                                  /* ICC_IGRPEN1_EL1 */
                if (rd) ret = vcif_igrpen_get(v, 1); else vcif_igrpen_set(v, 1, (uint32_t)val);
                break;
        default: known = false;
        }
    } else if (crn == 12 && crm == 8) {                         /* Group 0: none of our LRs is Group 0 */
        switch (op2) {
        case 0: case 2: if (!rd) { known = false; break; }      /* IAR0, HPPIR0 */
                c->st_other++; ret = VGIC_INTID_SPURIOUS; break;
        case 1: if (rd) { known = false; break; }               /* EOIR0 */
                c->st_other++; break;
        case 3: c->st_other++;                                  /* ICC_BPR0_EL1 */
                if (rd) ret = vcif_bpr0_get(v); else vcif_bpr0_set(v, (uint32_t)val);
                break;
        case 4: case 5: case 6: case 7:                         /* ICC_AP0R<n>_EL1 */
                c->st_other++;
                if (rd) ret = vcif_ap_get(v, 0, op2 - 4); else vcif_ap_set(v, 0, op2 - 4, (uint32_t)val);
                break;
        default: known = false;
        }
    } else if (crn == 12 && crm == 9 && op2 < 4) {              /* ICC_AP1R<n>_EL1 */
        c->st_other++;
        if (rd) ret = vcif_ap_get(v, 1, op2); else vcif_ap_set(v, 1, op2, (uint32_t)val);
    } else if (crn == 12 && crm == 11 && op2 == 1 && !rd) {     /* ICC_DIR_EL1 */
        c->st_dir++;
        vcif_dir(v, (uint32_t)val);
    } else if (crn == 12 && crm == 11 && op2 == 3 && rd) {      /* ICC_RPR_EL1 */
        c->st_other++;
        ret = vcif_rpr(v);
    } else {
        known = false;
    }
    if (!known)
        return false;
    if (g_trace_on && crn == 12) {
        static unsigned n_icc;
        bool iar = crm == 12 && op2 == 0 && rd, eoi = (crm == 12 && op2 == 1 && !rd) || (crm == 11 && op2 == 1 && !rd);

        if ((iar && ret >= 32) || (eoi && val >= 32)) {
            if (n_icc++ < 150) {
                put_kv(iar ? "Watari EL2: TR guest IAR1 -> " : (crm == 11 ? "Watari EL2: TR guest DIR  <- " : "Watari EL2: TR guest EOIR1 <- "),
                       iar ? ret : val);
                put_kv("Watari EL2: TR    at guest pc ", f->elr);
            }
        }
    }
    hw_out(c);
    if (rd && rt != 31)
        f->x[rt] = ret;
    f->elr += 4;
    return true;
}

/* Trapped MSR/MRS: ICC_SGI*R_EL1 writes in both modes; the other ICC registers
 * in TRAP_EMULATE mode. Returns false for anything else (the caller dumps and halts). */
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

    if (op0 != 3 || op1 != 0)
        return false;
    if (!dir && crn == 12 && crm == 11 && op2 >= 5 && op2 <= 7) {
        uint64_t val = rt == 31 ? 0 : f->x[rt];
        uint32_t mask = vgic_sgi_send_mask(g_v, this_cpu(), val);
        put_kv(msg[op2 - 5], val);
        put_kv("Watari EL2:   vCPU target mask = ", mask);
        f->elr += 4;
        return true;
    }
    if (!g_cpuif_trap)
        return false;
    return icc_emulate(f, crn, crm, op2, rt, dir);
}

static unsigned lpi_total(void)
{
    unsigned n = 0;

    for (unsigned i = 0; i < g_ncpu; i++)
        n += g_cif[i].st_lpi;
    return n;
}

static unsigned kick_evict_total(void)
{
    unsigned n = 0;

    for (unsigned i = 0; i < g_ncpu; i++)
        n += g_cif[i].st_kick_evict;
    return n;
}

static unsigned lpi_dropped_total(void)
{
    unsigned n = 0;

    for (unsigned i = 0; i < g_ncpu; i++)
        n += vgic_lpi_dropped(g_v, i);
    return n;
}

void cpuif_stats(void)
{
    unsigned t[9] = { 0 };

    for (unsigned i = 0; i < g_ncpu; i++) {
        t[0] += g_cif[i].vc.st_inject;
        t[1] += g_cif[i].vc.st_eoi;
        t[2] += g_cif[i].st_timer;
        t[3] += g_cif[i].st_maint;
        t[4] += g_cif[i].st_spi;
        t[5] += g_cif[i].st_kick;
        t[6] += g_cif[i].vc.st_evict;
        t[7] += g_cif[i].st_uie;
        t[8] += g_cif[i].vc.eoicount;
    }
    put_kv("Watari EL2: stats: LR injections    = ", t[0]);
    put_kv("Watari EL2: stats: guest EOIs (vgic_eoi) = ", t[1]);
    put_kv("Watari EL2: stats: physical PPI 27 irqs  = ", t[2]);
    put_kv("Watari EL2: stats: maintenance irqs (PPI 25) = ", t[3]);
    put_kv("Watari EL2: stats: underflow maintenance irqs (UIE) = ", t[7]);
    put_kv("Watari EL2: stats: LR evictions (higher priority arrived) = ", t[6]);
    put_kv("Watari EL2: stats: forwarded SPIs = ", t[4]);
    put_kv("Watari EL2: stats: forwarded LPIs = ", lpi_total());
    put_kv("Watari EL2: stats: LPI queue overflows (LPIs dropped) = ", lpi_dropped_total());
    put_kv("Watari EL2: stats: cross-core kicks (SGI 15) = ", t[5]);
    put_kv("Watari EL2: stats: cross-core kicks to evict a worse pending LR = ", kick_evict_total());
    if (g_cpuif_trap) {
        icc_report();
        put_kv("Watari EL2: stats: EOIR/DIR without a matching active LR = ", t[8]);
    }
}
