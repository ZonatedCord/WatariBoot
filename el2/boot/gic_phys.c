// SPDX-License-Identifier: MIT
// Watari EL2 M1-2: physical GICv3 owned by EL2.
// Written from the GICv3/v4 architecture specification (IHI 0069H.b): GICD_CTLR
// (12.9.4), GICR_WAKER (12.11.2), GICR_IGROUPR0/ISENABLER0/IPRIORITYR (12.12),
// ICC_SRE_EL2 (12.5.x), ICC_PMR/IGRPEN1/IAR1/EOIR1. The guest never sees these
// frames: its GICD/GICR accesses are trapped by stage 2 and go to the vGIC model.

#include "boot.h"

#define GICD_CTLR        0x0000
#define GICD_CTLR_ENGRP0 (1u << 0)
#define GICD_CTLR_ENGRP1 (1u << 1)
#define GICD_CTLR_ARE    (1u << 4)
#define GICD_CTLR_RWP    (1u << 31)

#define GICR_WAKER       0x0014
#define GICR_WAKER_PSLEEP (1u << 1)
#define GICR_WAKER_CASLEEP (1u << 2)

#define SGI_BASE         (GICR_PHYS + 0x10000)
#define GICR_IGROUPR0    (SGI_BASE + 0x080)
#define GICR_ISENABLER0  (SGI_BASE + 0x100)
#define GICR_ICENABLER0  (SGI_BASE + 0x180)
#define GICR_IPRIORITYR  (SGI_BASE + 0x400)
#define GICR_ICFGR1      (SGI_BASE + 0xC04)

static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uint64_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

void gic_phys_ppi_enable(unsigned ppi, bool on)
{
    wr32(on ? GICR_ISENABLER0 : GICR_ICENABLER0, 1u << ppi);
}

void gic_phys_init(void)
{
    /* ICC_SRE_EL2: SRE=1 (system register interface for EL2), DFB/DIB=1, and
     * Enable=1 so that EL1 may access ICC_SRE_EL1 and the other ICC registers
     * without trapping (GIC spec 12.2.24; erratum-free on QEMU). */
    __asm__ volatile("msr icc_sre_el2, %0; isb" : : "r"(0xfULL));

    /* Distributor: affinity routing, both groups. Writing 0x13 is valid for
     * DS=1 (bit0 Grp0, bit1 Grp1, bit4 ARE) and harmless for DS=0. */
    wr32(GICD_PHYS + GICD_CTLR, GICD_CTLR_ARE | GICD_CTLR_ENGRP1 | GICD_CTLR_ENGRP0);
    unsigned spins = 0;
    while ((rd32(GICD_PHYS + GICD_CTLR) & GICD_CTLR_RWP) && ++spins < 100000)
        ;
    put_kv("Watari EL2: phys GICD_CTLR = ", rd32(GICD_PHYS + GICD_CTLR));

    /* Redistributor of CPU 0: wake it up (clear ProcessorSleep, wait for
     * ChildrenAsleep=0). */
    wr32(GICR_PHYS + GICR_WAKER, rd32(GICR_PHYS + GICR_WAKER) & ~GICR_WAKER_PSLEEP);
    spins = 0;
    while ((rd32(GICR_PHYS + GICR_WAKER) & GICR_WAKER_CASLEEP) && ++spins < 100000)
        ;
    put_kv("Watari EL2: phys GICR_WAKER = ", rd32(GICR_PHYS + GICR_WAKER));

    /* SGI/PPI frame: everything Group 1 NS, all disabled, the PPIs EL2 uses
     * (maintenance 25, virtual timer 27) level-triggered with priority 0x40,
     * then enabled. */
    wr32(GICR_IGROUPR0, 0xffffffffu);
    wr32(GICR_ICENABLER0, 0xffffffffu);
    wr32(GICR_ICFGR1, 0);                    /* PPIs 16-31 level-sensitive */
    wr8(GICR_IPRIORITYR + PPI_MAINT, 0x40);
    wr8(GICR_IPRIORITYR + PPI_VTIMER, 0x40);
    gic_phys_ppi_enable(PPI_MAINT, true);
    gic_phys_ppi_enable(PPI_VTIMER, true);

    /* Physical CPU interface at EL2: PMR open, Group 1 enabled. EOImode stays 0
     * (EOIR1 = priority drop + deactivate). With HCR_EL2.IMO=1 the same names
     * at EL1 are the virtual ICV_* registers, not these. */
    __asm__ volatile("msr icc_pmr_el1, %0" : : "r"(0xffULL));
    __asm__ volatile("msr icc_igrpen1_el1, %0; isb" : : "r"(1ULL));
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
