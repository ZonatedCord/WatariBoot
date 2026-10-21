// SPDX-License-Identifier: MIT
// Watari EL2 M1-2/M1-3: physical GICv3 owned by EL2.
// Written from the GICv3/v4 architecture specification (IHI 0069H.b): GICD_CTLR
// (12.9.4), GICD_ISENABLER/ICENABLER/IGROUPR/IPRIORITYR/IROUTER (12.9), GICR_TYPER
// (12.11.x), GICR_WAKER (12.11.2), GICR_IGROUPR0/ISENABLER0/IPRIORITYR (12.12),
// ICC_SRE_EL2, ICC_PMR/IGRPEN1/IAR1/EOIR1/SGI1R. The guest never sees these
// frames: its GICD/GICR accesses are trapped by stage 2 and go to the vGIC model.
//
// M1-3: one redistributor per core, found by walking GICR_TYPER until the Last
// bit (GIC spec: redistributor discovery); every function that touches the
// SGI/PPI frame uses the frame of the calling core (TPIDR_EL2 = core index).

#include "boot.h"

#define GICD_CTLR        0x0000
#define GICD_IGROUPR     0x0080
#define GICD_ISENABLER   0x0100
#define GICD_ICENABLER   0x0180
#define GICD_IPRIORITYR  0x0400
#define GICD_ICFGR       0x0C00
#define GICD_IROUTER     0x6000
#define GICD_CTLR_ENGRP0 (1u << 0)
#define GICD_CTLR_ENGRP1 (1u << 1)
#define GICD_CTLR_ARE    (1u << 4)
#define GICD_CTLR_RWP    (1u << 31)

#define GICR_TYPER       0x0008
#define GICR_TYPER_LAST  (1u << 4)
#define GICR_WAKER       0x0014
#define GICR_WAKER_PSLEEP (1u << 1)
#define GICR_WAKER_CASLEEP (1u << 2)

#define SGI_OFF          0x10000
#define GICR_IGROUPR0    (SGI_OFF + 0x080)
#define GICR_ISENABLER0  (SGI_OFF + 0x100)
#define GICR_ICENABLER0  (SGI_OFF + 0x180)
#define GICR_IPRIORITYR  (SGI_OFF + 0x400)
#define GICR_ICFGR1      (SGI_OFF + 0xC04)

#define PRIO_EL2         0x40

static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uint64_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr64(uint64_t a, uint64_t v) { *(volatile uint64_t *)a = v; }
static inline void wr8(uint64_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

static inline uint64_t my_rd(void) { return g_pcpu[this_cpu()].gicr; }

unsigned cpu_from_mpidr(uint64_t mpidr)
{
    uint32_t aff = (uint32_t)(((mpidr >> 32) & 0xff) << 24) | (uint32_t)(mpidr & 0xffffff);

    for (unsigned i = 0; i < g_ncpu; i++)
        if (g_pcpu[i].aff == aff)
            return i;
    return ~0u;
}

/* Walk the redistributor frames (consecutive, GICR_STRIDE apart) until the Last
 * bit: one frame per core. GICR_TYPER[63:32] is Aff3.Aff2.Aff1.Aff0. */
unsigned gic_phys_discover(void)
{
    unsigned n = 0;

    for (uint64_t base = GICR_PHYS; n < MAX_CPUS; base += GICR_STRIDE) {
        uint64_t typer = rd64(base + GICR_TYPER);

        g_pcpu[n].aff = (uint32_t)(typer >> 32);
        g_pcpu[n].gicr = base;
        n++;
        if (typer & GICR_TYPER_LAST)
            break;
    }
    return n;
}

void gic_phys_ppi_enable(unsigned ppi, bool on)
{
    wr32(my_rd() + (on ? GICR_ISENABLER0 : GICR_ICENABLER0), 1u << ppi);
}

void gic_phys_init_dist(void)
{
    /* Distributor: affinity routing, both groups. Writing 0x13 is valid for
     * DS=1 (bit0 Grp0, bit1 Grp1, bit4 ARE) and harmless for DS=0. */
    wr32(GICD_PHYS + GICD_CTLR, GICD_CTLR_ARE | GICD_CTLR_ENGRP1 | GICD_CTLR_ENGRP0);
    unsigned spins = 0;
    while ((rd32(GICD_PHYS + GICD_CTLR) & GICD_CTLR_RWP) && ++spins < 100000)
        ;
    put_kv("Watari EL2: phys GICD_CTLR = ", rd32(GICD_PHYS + GICD_CTLR));
}

void gic_phys_init_cpu(void)
{
    uint64_t r = my_rd();

    /* ICC_SRE_EL2: SRE=1 (system register interface for EL2), DFB/DIB=1, and
     * Enable=1 so that EL1 may access ICC_SRE_EL1 and the other ICC registers
     * without trapping (GIC spec 12.2.24; erratum-free on QEMU). */
    __asm__ volatile("msr icc_sre_el2, %0; isb" : : "r"(0xfULL));

    /* Redistributor of this core: wake it up (clear ProcessorSleep, wait for
     * ChildrenAsleep=0). */
    wr32(r + GICR_WAKER, rd32(r + GICR_WAKER) & ~GICR_WAKER_PSLEEP);
    unsigned spins = 0;
    while ((rd32(r + GICR_WAKER) & GICR_WAKER_CASLEEP) && ++spins < 100000)
        ;
    if (spins >= 100000)
        put_kn("Watari EL2: cpu ", this_cpu(), " redistributor did not wake up\n");

    /* SGI/PPI frame: everything Group 1 NS, all disabled, the interrupts EL2
     * uses (kick SGI 15, maintenance PPI 25, virtual timer PPI 27)
     * level-triggered/priority 0x40, then enabled. */
    wr32(r + GICR_IGROUPR0, 0xffffffffu);
    wr32(r + GICR_ICENABLER0, 0xffffffffu);
    wr32(r + GICR_ICFGR1, 0);                /* PPIs 16-31 level-sensitive */
    wr8(r + GICR_IPRIORITYR + SGI_KICK, PRIO_EL2);
    wr8(r + GICR_IPRIORITYR + PPI_MAINT, PRIO_EL2);
    wr8(r + GICR_IPRIORITYR + PPI_VTIMER, PRIO_EL2);
    wr32(r + GICR_ISENABLER0, (1u << SGI_KICK) | (1u << PPI_MAINT) | (1u << PPI_VTIMER));

    /* Physical CPU interface at EL2: PMR open, Group 1 enabled. EOImode stays 0
     * (EOIR1 = priority drop + deactivate). With HCR_EL2.IMO=1 the same names
     * at EL1 are the virtual ICV_* registers, not these. */
    __asm__ volatile("msr icc_pmr_el1, %0" : : "r"(0xffULL));
    __asm__ volatile("msr icc_igrpen1_el1, %0; isb" : : "r"(1ULL));
}

/* Before PSCI CPU_OFF: CPU interface off, everything disabled, redistributor
 * asleep (GIC spec 11.1: ProcessorSleep handshake before power-down). */
void gic_phys_cpu_down(void)
{
    uint64_t r = my_rd();

    __asm__ volatile("msr icc_igrpen1_el1, xzr; isb");
    wr32(r + GICR_ICENABLER0, 0xffffffffu);
    wr32(r + GICR_WAKER, rd32(r + GICR_WAKER) | GICR_WAKER_PSLEEP);
    unsigned spins = 0;
    while (!(rd32(r + GICR_WAKER) & GICR_WAKER_CASLEEP) && ++spins < 100000)
        ;
}

uint32_t gic_phys_ack(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(v));
    return (uint32_t)(v & 0xffffff);
}

void gic_phys_eoi(uint32_t intid)
{
    __asm__ volatile("msr icc_eoir1_el1, %0; isb" : : "r"((uint64_t)intid));
}

/* Physical SGI_KICK (SGI 15, reserved for EL2: guest SGIs are virtual and never
 * reach the physical distributor) to another core: ICC_SGI1R_EL1 at EL2 is the
 * physical register (GIC spec 12.2.22): INTID[27:24], Aff1[23:16], Aff2[39:32],
 * Aff3[55:48], TargetList[15:0] for Aff0[3:0], RS[47:44] = Aff0 >> 4. */
void gic_phys_kick(unsigned cpu)
{
    uint32_t a = g_pcpu[cpu].aff;
    uint64_t v = ((uint64_t)(a >> 24) << 48) | ((uint64_t)((a >> 16) & 0xff) << 32) |
                 ((uint64_t)(SGI_KICK) << 24) | ((uint64_t)((a >> 8) & 0xff) << 16) |
                 ((uint64_t)((a & 0xff) >> 4) << 44) | (1ULL << (a & 0xf));

    __asm__ volatile("dsb ishst; msr icc_sgi1r_el1, %0; isb" : : "r"(v) : "memory");
}

/* ---- SPI forwarding ------------------------------------------------------- */
void gic_phys_spi_enable(uint32_t intid, bool on)
{
    uint64_t bit = 1u << (intid % 32), w = 4 * (intid / 32);

    if (on) {
        /* Group 1, priority 0x40 (above the guest's PMR), then enable. */
        wr32(GICD_PHYS + GICD_IGROUPR + w, rd32(GICD_PHYS + GICD_IGROUPR + w) | bit);
        wr8(GICD_PHYS + GICD_IPRIORITYR + intid, PRIO_EL2);
        wr32(GICD_PHYS + GICD_ISENABLER + w, bit);
    } else {
        wr32(GICD_PHYS + GICD_ICENABLER + w, bit);
    }
}

/* GICD_ICFGR (GIC spec 12.9.x): 2 bits per SPI, bit 1 = edge. The guest's trigger type is passed
 * through (a v2m/MSI SPI is a pulse: with a level configuration at the physical GIC it would be
 * lost). Changing it while the SPI is enabled is UNPREDICTABLE, so disable around the write. */
void gic_phys_spi_trigger(uint32_t intid, bool edge)
{
    uint64_t a = GICD_PHYS + GICD_ICFGR + 4ULL * (intid / 16);
    uint32_t sh = 2 * (intid % 16) + 1, v = rd32(a);
    uint64_t bit = 1u << (intid % 32), w = 4 * (intid / 32);
    bool en = rd32(GICD_PHYS + GICD_ISENABLER + w) & bit;

    v = (v & ~(1u << sh)) | ((uint32_t)edge << sh);
    if (en)
        wr32(GICD_PHYS + GICD_ICENABLER + w, bit);
    wr32(a, v);
    if (en)
        wr32(GICD_PHYS + GICD_ISENABLER + w, bit);
}

/* GICD_IROUTER<n> (GIC spec 12.9.x): Aff3 [39:32], Aff2 [23:16], Aff1 [15:8],
 * Aff0 [7:0], IRM = 0 (route to one PE). */
void gic_phys_spi_route(uint32_t intid, int vcpu)
{
    if (vcpu < 0 || (unsigned)vcpu >= g_ncpu)
        return;
    uint32_t a = g_pcpu[vcpu].aff;

    wr64(GICD_PHYS + GICD_IROUTER + 8ULL * intid,
         ((uint64_t)(a >> 24) << 32) | (a & 0xffffffu));
}

/* ---- LPI pass-through (M2) -------------------------------------------------
 * Windows needs MSI: with the QEMU virt GIC ITS present its ACPI describes a GITS and the
 * interrupt of NVMe/xHCI becomes an LPI (without it Windows stays on a level INTx that storms,
 * F-23). The ITS itself is not trapped (its frame is mapped in stage 2, guest IPA = PA); only
 * the per-core redistributor LPI registers are forwarded to the physical GICR of the same core
 * (GIC spec 12.11: GICR_CTLR.EnableLPIs, PROPBASER, PENDBASER, INVLPIR, INVALLR). */
bool gic_phys_has_lpis(void)
{
    return (rd32(GICD_PHYS + 4) >> 17) & 1u;
}

/* INTID bits of the physical GIC: GICD_TYPER.IDbits [23:19] + 1 (GIC spec 12.9.4). */
unsigned gic_phys_idbits(void)
{
    return ((rd32(GICD_PHYS + 4) >> 19) & 0x1f) + 1;
}

void gic_phys_lpi_reg(unsigned cpu, unsigned off, uint64_t val)
{
    uint64_t base = g_pcpu[cpu].gicr;

    switch (off) {
    case 0x0: /* GICR_CTLR.EnableLPIs; wait for RWP (bit 31) */
        wr32(base, (rd32(base) & ~1u) | (uint32_t)(val & 1u));
        for (unsigned n = 0; n < 1000000 && (rd32(base) >> 31); n++)
            ;
        break;
    case 0x70:
    case 0x78:
        wr64(base + off, val);
        break;
    case 0xA0:
    case 0xB0:
        wr64(base + off, val);
        for (unsigned n = 0; n < 1000000 && (rd32(base + 0xC0) & 1u); n++) /* SYNCR.Busy */
            ;
        break;
    default:
        break;
    }
}
