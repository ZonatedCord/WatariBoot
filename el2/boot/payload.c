// SPDX-License-Identifier: MIT
// Watari EL2 test payload, runs at EL1 (MMU off). M0: PSCI over HVC. M1-1: guest
// view of the vGIC MMIO. M1-2: an OS-like GICv3 bring-up through the vGIC,
// then virtual-timer interrupts (CNTV, PPI 27) and an SGI-to-self, both
// delivered as virtual IRQs through the GICv3 list registers.
// Register programming follows GIC spec IHI 0069H.b (GICD_CTLR, GICR_WAKER,
// ISENABLER0, IPRIORITYR, ICC_*_EL1) and the Arm ARM generic timer chapter.

#include "boot.h"

#define PSCI_VERSION     0x84000000u
#define PSCI_SYSTEM_OFF  0x84000008u

static uint64_t hvc(uint64_t fid)
{
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = 0;
    register uint64_t x2 __asm__("x2") = 0;
    register uint64_t x3 __asm__("x3") = 0;
    __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : : "memory");
    return x0;
}

static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uint64_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uint64_t a, uint8_t v) { *(volatile uint8_t *)a = v; }
static inline uint8_t rd8(uint64_t a) { return *(volatile uint8_t *)a; }

/* ---- M1-1 test: guest view of the virtual GIC MMIO ------------------------- */
static void el1_vgic_test(void)
{
    const uint64_t D = GICD_PHYS, R = GICR_PHYS;
    put_kv("Watari EL1: GICD_CTLR  = ", rd32(D + 0x0000));
    put_kv("Watari EL1: GICD_TYPER = ", rd32(D + 0x0004));
    put_kv("Watari EL1: GICD_IIDR  = ", rd32(D + 0x0008));
    put_kv("Watari EL1: GICR_TYPER = ", rd64(R + 0x0008));

    /* GICR_WAKER (RD frame +0x14): ProcessorSleep bit1, ChildrenAsleep bit2. */
    wr32(R + 0x0014, rd32(R + 0x0014) & ~2u);
    unsigned spins = 0;
    while ((rd32(R + 0x0014) & 4u) && ++spins < 1000)
        ;
    put_kv("Watari EL1: GICR_WAKER = ", rd32(R + 0x0014));
    puts_(spins < 1000 ? "Watari EL1: redistributor awake\n"
                       : "Watari EL1: redistributor STILL ASLEEP\n");

    /* SPI 33: GICD_ISENABLER1 bit 1 (0x104), priority byte at 0x400 + 33 */
    wr32(D + 0x0104, 1u << 1);
    put_kv("Watari EL1: GICD_ISENABLER1 = ", rd32(D + 0x0104));
    wr8(D + 0x0400 + 33, 0xa0);
    put_kv("Watari EL1: IPRIORITYR[33]  = ", rd8(D + 0x0400 + 33));
}

/* ---- M1-2 test: virtual interrupts ------------------------------------------ */
#define SGI_BASE (GICR_PHYS + 0x10000)

static volatile uint32_t g_timer_irqs;
static volatile uint32_t g_sgi_irqs;
static volatile uint32_t g_other_irqs;
static uint64_t g_timer_ticks;

static inline uint64_t read_cntfrq(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline void timer_arm(void)
{
    __asm__ volatile("msr cntv_tval_el0, %0" : : "r"(g_timer_ticks));
    __asm__ volatile("msr cntv_ctl_el0, %0; isb" : : "r"(1ULL));   /* ENABLE=1, IMASK=0 */
}

/* Called from the EL1 IRQ vector (payload.S) with IRQs masked. */
void el1_irq_handler(void)
{
    uint64_t iar;
    __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(iar));          /* virtual ack */
    uint32_t id = (uint32_t)(iar & 0xffffff);

    if (id >= 1020) {
        puts_("Watari EL1: spurious INTID\n");
        return;
    }
    if (id == 27) {
        g_timer_irqs++;
        put_kv("Watari EL1: irq INTID = ", id);
        timer_arm();                  /* new TVAL deasserts the level line */
    } else if (id == 1) {
        g_sgi_irqs++;
        put_kv("Watari EL1: irq INTID = ", id);
    } else {
        g_other_irqs++;
        put_kv("Watari EL1: unexpected irq INTID = ", id);
    }
    __asm__ volatile("msr icc_eoir1_el1, %0; isb" : : "r"((uint64_t)id));
}

void el1_bad(uint64_t idx)
{
    uint64_t esr, elr, far;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    put_kv("Watari EL1: UNEXPECTED EXCEPTION, vector ", idx);
    put_kv("  ESR_EL1 = ", esr);
    put_kv("  ELR_EL1 = ", elr);
    put_kv("  FAR_EL1 = ", far);
    for (;;)
        __asm__ volatile("wfe");
}

extern uint8_t el1_vectors[];

static void el1_irq_test(void)
{
    const uint64_t D = GICD_PHYS, R = GICR_PHYS;

    __asm__ volatile("msr vbar_el1, %0; isb" : : "r"((uint64_t)el1_vectors));

    /* ICC_SRE_EL1.SRE = 1 (system register interface), as an OS does first. */
    uint64_t sre;
    __asm__ volatile("mrs %0, icc_sre_el1" : "=r"(sre));
    __asm__ volatile("msr icc_sre_el1, %0; isb" : : "r"(sre | 1));
    __asm__ volatile("mrs %0, icc_sre_el1" : "=r"(sre));
    put_kv("Watari EL1: ICC_SRE_EL1 = ", sre);

    /* Distributor: ARE + EnableGrp1 (bit4, bit1). Redistributor already awake. */
    wr32(D + 0x0000, (1u << 4) | (1u << 1));
    put_kv("Watari EL1: GICD_CTLR now = ", rd32(D + 0x0000));
    wr32(R + 0x0014, rd32(R + 0x0014) & ~2u);

    /* SGI/PPI frame: Group 1, PPIs level, priorities, enable PPI 27 + SGI 1. */
    wr32(SGI_BASE + 0x080, 0xffffffffu);                 /* IGROUPR0 */
    wr32(SGI_BASE + 0xC04, 0);                           /* ICFGR1: PPIs level */
    wr8(SGI_BASE + 0x400 + 27, 0x80);                    /* IPRIORITYR */
    wr8(SGI_BASE + 0x400 + 1, 0x80);
    wr32(SGI_BASE + 0x100, (1u << 27) | (1u << 1));      /* ISENABLER0 */
    put_kv("Watari EL1: GICR_ISENABLER0 = ", rd32(SGI_BASE + 0x100));

    /* CPU interface: PMR open to priority < 0xF0, Group 1 on. */
    __asm__ volatile("msr icc_pmr_el1, %0" : : "r"(0xf0ULL));
    __asm__ volatile("msr icc_igrpen1_el1, %0; isb" : : "r"(1ULL));
    uint64_t pmr, grp;
    __asm__ volatile("mrs %0, icc_pmr_el1" : "=r"(pmr));
    __asm__ volatile("mrs %0, icc_igrpen1_el1" : "=r"(grp));
    put_kv("Watari EL1: ICC_PMR_EL1 = ", pmr);
    put_kv("Watari EL1: ICC_IGRPEN1_EL1 = ", grp);

    /* (a) virtual timer: 10 ms period, five interrupts. */
    uint64_t frq = read_cntfrq();
    put_kv("Watari EL1: CNTFRQ_EL0 = ", frq);
    g_timer_ticks = frq / 100;
    timer_arm();
    __asm__ volatile("msr daifclr, #2");                 /* unmask IRQ */
    for (unsigned spin = 0; g_timer_irqs < 5 && spin < 2000000; spin++)
        __asm__ volatile("wfi");
    __asm__ volatile("msr daifset, #2");
    __asm__ volatile("msr cntv_ctl_el0, xzr; isb");      /* timer off */
    if (g_timer_irqs == 5)
        puts_("Watari EL1: timer irq 27 x5\n");
    else
        put_kv("Watari EL1: TIMER FAILED, count = ", g_timer_irqs);

    /* (b) SGI 1 to self: INTID[27:24] = 1, TargetList = 1 (Aff0 = 0). */
    __asm__ volatile("msr daifclr, #2");
    __asm__ volatile("msr icc_sgi1r_el1, %0; isb" : : "r"((1ULL << 24) | 1ULL));
    for (unsigned spin = 0; !g_sgi_irqs && spin < 1000000; spin++)
        __asm__ volatile("nop");
    __asm__ volatile("msr daifset, #2");
    if (g_sgi_irqs == 1 && !g_other_irqs)
        puts_("Watari EL1: sgi 1 received\n");
    else
        put_kv("Watari EL1: SGI FAILED, count = ", g_sgi_irqs);
}

void __attribute__((noreturn)) el1_main(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    puts_("Watari EL1: hello from EL1 (CurrentEL=");
    putc_('0' + (char)((el >> 2) & 3));
    puts_(")\n");

    uint64_t v = hvc(PSCI_VERSION);
    puts_("Watari EL1: PSCI_VERSION returned ");
    putc_('0' + (char)((v >> 16) & 0xf));
    putc_('.');
    putc_('0' + (char)(v & 0xf));
    puts_("\n");

    el1_vgic_test();
    el1_irq_test();

    puts_("Watari EL1: calling PSCI SYSTEM_OFF\n");
    hvc(PSCI_SYSTEM_OFF);
    puts_("Watari EL1: SYSTEM_OFF returned?!\n");
    for (;;)
        __asm__ volatile("wfe");
}
