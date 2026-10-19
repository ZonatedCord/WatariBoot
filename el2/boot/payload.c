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

/* M1-3 shared state (all guest data is Device memory here: MMU off). */
static volatile uint32_t g_tirq[MAX_CPUS];      /* timer irqs of secondaries */
static volatile uint32_t g_sgi2[MAX_CPUS];      /* SGI 2 received per cpu */
static volatile uint32_t g_spi_cpu;             /* 1 + cpu that took SPI 34 */
#define RTC_SPI 34u

static inline unsigned my_cpu(void)
{
    uint64_t m;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(m));   /* = VMPIDR_EL2 */
    return (unsigned)(m & 0xff);
}

/* Called from the EL1 IRQ vector (payload.S) with IRQs masked. */
void el1_irq_handler(void)
{
    uint64_t iar;
    __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(iar));          /* virtual ack */
    uint32_t id = (uint32_t)(iar & 0xffffff);
    unsigned cpu = my_cpu();

    if (id >= 1020) {
        puts_("Watari EL1: spurious INTID\n");
        return;
    }
    if (id == 27 && cpu != 0) {
        g_tirq[cpu]++;
        timer_arm();
    } else if (id == 27) {
        g_timer_irqs++;
        put_kv("Watari EL1: irq INTID = ", id);
        timer_arm();                  /* new TVAL deasserts the level line */
    } else if (id == 1) {
        g_sgi_irqs++;
        put_kv("Watari EL1: irq INTID = ", id);
    } else if (id == 2) {
        g_sgi2[cpu]++;
        put_kn("Watari EL1: cpu ", cpu, " sgi 2\n");
    } else if (id == RTC_SPI) {
        /* PL031: clear the interrupt at the device (RTCICR) and mask it, or the
         * level line would stay asserted. */
        wr32(PL031_PHYS + 0x1C, 1);
        wr32(PL031_PHYS + 0x10, 0);
        g_spi_cpu = cpu + 1;
        put_kn("Watari EL1: cpu ", cpu, " spi 34 received\n");
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

/* ---- M1-3 test: SMP bring-up, SGI between cores, forwarded SPI, CPU_OFF ------ */
#define PSCI_CPU_OFF         0x84000002u
#define PSCI_CPU_ON_64       0xC4000003u
#define PSCI_AFFINITY_INFO   0xC4000004u
#define PSCI_FEATURES        0x8400000Au

static int64_t psci(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3)
{
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;
    __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : : "memory");
    return (int64_t)x0;
}

extern void el1_secondary_entry(void);                 /* payload.S */
static uint8_t g_sec_stack[MAX_CPUS][4096] __attribute__((aligned(16)));
static volatile uint32_t g_online[MAX_CPUS];           /* "cpu N online" printed */
static volatile uint32_t g_ready[MAX_CPUS];            /* 3 timer irqs done */
static volatile uint32_t g_rounds[MAX_CPUS];           /* times the cpu entered */
static volatile uint32_t g_go_off;
static volatile uint32_t g_fail;

static inline uint64_t cnt(void)
{
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

/* Wait (IRQs masked around the check so that WFI cannot miss its wake-up)
 * until *flag >= want or `secs` seconds pass. */
static bool wait_flag(volatile uint32_t *flag, uint32_t want, unsigned secs, bool sleep)
{
    uint64_t end = cnt() + (uint64_t)secs * read_cntfrq();

    for (;;) {
        __asm__ volatile("msr daifset, #2");
        if (*flag >= want) {
            __asm__ volatile("msr daifclr, #2");
            return true;
        }
        if (cnt() > end) {
            __asm__ volatile("msr daifclr, #2");
            return false;
        }
        if (sleep)       /* only with a guaranteed wake-up source (timer, SGI) */
            __asm__ volatile("wfi");
        __asm__ volatile("msr daifclr, #2");      /* take the interrupt, if any */
    }
}

static void expect_ret(const char *what, int64_t got, int64_t want)
{
    if (got != want) {
        puts_("Watari EL1: PSCI CHECK FAILED: ");
        puts_(what);
        put_kv(", got ", (uint64_t)got);
        g_fail = 1;
    }
}

/* Every secondary core starts here from PSCI CPU_ON (x0 = stack top = context_id).
 * Register programming follows GIC spec IHI 0069H.b (GICR_WAKER, IGROUPR0,
 * ISENABLER0, IPRIORITYR, ICC_*_EL1) like el1_irq_test(). */
void el1_secondary_main(void)
{
    unsigned n = my_cpu();
    const uint64_t R = GICR_PHYS + (uint64_t)n * 0x20000, S = R + 0x10000;
    uint32_t round = ++g_rounds[n];

    __asm__ volatile("msr vbar_el1, %0; isb" : : "r"((uint64_t)el1_vectors));
    if (round > 1) {
        put_kn("Watari EL1: cpu ", n, " online again after CPU_OFF\n");
        g_online[n] = 2;
        for (;;)
            __asm__ volatile("wfe");
    }
    put_kn("Watari EL1: cpu ", n, " online\n");
    g_online[n] = 1;

    uint64_t sre;
    __asm__ volatile("mrs %0, icc_sre_el1" : "=r"(sre));
    __asm__ volatile("msr icc_sre_el1, %0; isb" : : "r"(sre | 1));
    wr32(R + 0x0014, rd32(R + 0x0014) & ~2u);            /* GICR_WAKER.ProcessorSleep = 0 */
    unsigned spins = 0;
    while ((rd32(R + 0x0014) & 4u) && ++spins < 1000)
        ;
    wr32(S + 0x080, 0xffffffffu);                        /* IGROUPR0: Group 1 */
    wr32(S + 0xC04, 0);                                  /* PPIs level */
    wr8(S + 0x400 + 27, 0x80);
    wr8(S + 0x400 + 2, 0x80);
    wr32(S + 0x100, (1u << 27) | (1u << 2));             /* ISENABLER0: timer + SGI 2 */
    __asm__ volatile("msr icc_pmr_el1, %0" : : "r"(0xf0ULL));
    __asm__ volatile("msr icc_igrpen1_el1, %0; isb" : : "r"(1ULL));

    timer_arm();                                         /* 10 ms period */
    if (!wait_flag(&g_tirq[n], 3, 5, true))
        put_kn("Watari EL1: cpu ", n, " TIMER FAILED\n");
    put_kn("Watari EL1: cpu ", n, " timer irq 27 x3\n");
    g_ready[n] = 1;

    if (!wait_flag(&g_sgi2[n], 1, 10, true))
        put_kn("Watari EL1: cpu ", n, " SGI FAILED\n");
    wait_flag(&g_go_off, 1, 30, true);
    __asm__ volatile("msr cntv_ctl_el0, xzr; isb");      /* timer off */

    /* Offline sequence of an OS: interrupts off, CPU interface off, redistributor
     * asleep, then PSCI CPU_OFF (5.5.2: no wake-ups may arrive afterwards). */
    __asm__ volatile("msr daifset, #2");
    __asm__ volatile("msr icc_igrpen1_el1, xzr; isb");
    wr32(S + 0x180, 0xffffffffu);                        /* ICENABLER0 */
    wr32(R + 0x0014, rd32(R + 0x0014) | 2u);
    put_kn("Watari EL1: cpu ", n, " going off (CPU_OFF)\n");
    int64_t r = psci(PSCI_CPU_OFF, 0, 0, 0);
    put_kv("Watari EL1: CPU_OFF RETURNED (FAILED), ret = ", (uint64_t)r);
    for (;;)
        __asm__ volatile("wfe");
}

static void smp_test(unsigned ncpu)
{
    const uint64_t D = GICD_PHYS;
    const uint64_t entry = (uint64_t)el1_secondary_entry;
    unsigned i;

    put_kv("Watari EL1: cores visible in the vGIC = ", ncpu);

    /* PSCI contract checks (PSCI DEN0022F.b 5.6, 5.7, 5.5, 5.16). */
    expect_ret("FEATURES(CPU_ON)", psci(PSCI_FEATURES, PSCI_CPU_ON_64, 0, 0), 0);
    expect_ret("FEATURES(CPU_SUSPEND)", psci(PSCI_FEATURES, 0xC4000001u, 0, 0), -1);
    expect_ret("CPU_ON self -> ALREADY_ON", psci(PSCI_CPU_ON_64, 0, entry, 0), -4);
    expect_ret("CPU_ON Aff1=1 -> INVALID_PARAMETERS", psci(PSCI_CPU_ON_64, 0x100, entry, 0), -2);
    expect_ret("CPU_ON bit31 -> INVALID_PARAMETERS", psci(PSCI_CPU_ON_64, 0x80000001u, entry, 0), -2);
    expect_ret("CPU_ON unaligned -> INVALID_ADDRESS", psci(PSCI_CPU_ON_64, 1, entry + 1, 0), -9);
    expect_ret("CPU_ON outside RAM -> INVALID_ADDRESS", psci(PSCI_CPU_ON_64, 1, 0x1000, 0), -9);
    expect_ret("AFFINITY_INFO(0) -> ON", psci(PSCI_AFFINITY_INFO, 0, 0, 0), 0);
    expect_ret("AFFINITY_INFO(1) -> OFF", psci(PSCI_AFFINITY_INFO, 1, 0, 0), 1);
    expect_ret("AFFINITY_INFO level 1", psci(PSCI_AFFINITY_INFO, 0, 1, 0), -2);
    expect_ret("AFFINITY_INFO unknown cpu", psci(PSCI_AFFINITY_INFO, 0x100, 0, 0), -2);
    expect_ret("CPU_OFF on the only ON core -> DENIED", psci(PSCI_CPU_OFF, 0, 0, 0), -3);
    puts_(g_fail ? "Watari EL1: psci contract FAILED\n" : "Watari EL1: psci contract ok\n");

    /* Bring up cores 1..n-1; context_id = the core's stack top. */
    for (i = 1; i < ncpu; i++) {
        int64_t r = psci(PSCI_CPU_ON_64, i, entry, (uint64_t)&g_sec_stack[i][4096]);
        if (r)
            put_kv("Watari EL1: CPU_ON FAILED, ret = ", (uint64_t)r);
        else {
            int64_t r2 = psci(PSCI_CPU_ON_64, i, entry, 0);
            if (r2 != -4 && r2 != -5)
                put_kv("Watari EL1: repeated CPU_ON not ALREADY_ON/ON_PENDING: ", (uint64_t)r2);
        }
    }
    for (i = 1; i < ncpu; i++)
        if (!wait_flag(&g_ready[i], 1, 20, false))
            put_kn("Watari EL1: cpu ", i, " did not become ready\n");

    /* SGI 2 from cpu 0: cpu 1 by itself, then the rest with one target list. */
    __asm__ volatile("msr icc_sgi1r_el1, %0; isb" : : "r"((2ULL << 24) | 2ULL));
    if (ncpu > 2)
        __asm__ volatile("msr icc_sgi1r_el1, %0; isb"
                         : : "r"((2ULL << 24) | (((1ULL << ncpu) - 1) & ~3ULL)));
    for (i = 1; i < ncpu; i++)
        if (!wait_flag(&g_sgi2[i], 1, 10, false))
            put_kn("Watari EL1: cpu ", i, " NEVER GOT SGI 2\n");

    /* SPI: PL031 RTC alarm (QEMU virt SPI 2 = INTID 34), routed to the last core.
     * GICD registers: IGROUPR1 +0x84, IPRIORITYR +0x400, IROUTER +0x6000, ISENABLER1 +0x104. */
    unsigned tgt = ncpu - 1;
    wr32(D + 0x84, rd32(D + 0x84) | (1u << (RTC_SPI - 32)));
    wr8(D + 0x400 + RTC_SPI, 0x80);
    *(volatile uint64_t *)(D + 0x6000 + 8ULL * RTC_SPI) = tgt;           /* Aff0 = tgt */
    wr32(D + 0x104, 1u << (RTC_SPI - 32));
    uint32_t now = rd32(PL031_PHYS + 0x00);                              /* RTCDR */
    wr32(PL031_PHYS + 0x04, now + 2);                                    /* RTCMR */
    wr32(PL031_PHYS + 0x10, 1);                                          /* RTCIMSC */
    put_kv("Watari EL1: RTC alarm armed, SPI 34 routed to cpu ", tgt);
    if (wait_flag(&g_spi_cpu, 1, 10, false)) {
        if (g_spi_cpu != tgt + 1)
            put_kv("Watari EL1: SPI delivered to the WRONG cpu: ", g_spi_cpu - 1);
    } else {
        puts_("Watari EL1: SPI FAILED (no RTC interrupt)\n");
    }

    /* Power the secondaries off: each one calls CPU_OFF itself. */
    g_go_off = 1;
    for (i = 1; i < ncpu; i++) {
        uint64_t end = cnt() + 10 * read_cntfrq();
        int64_t st = 0;

        while (cnt() < end && (st = psci(PSCI_AFFINITY_INFO, i, 0, 0)) != 1)
            ;
        if (st == 1)
            put_kn("Watari EL1: cpu ", i, " is OFF (AFFINITY_INFO = 1)\n");
        else
            put_kn("Watari EL1: cpu ", i, " DID NOT GO OFF\n");
    }
    /* A core that went OFF can be started again (restart is a fresh entry). */
    {
        int64_t r = psci(PSCI_CPU_ON_64, 1, entry, (uint64_t)&g_sec_stack[1][4096]);
        if (r)
            put_kv("Watari EL1: second CPU_ON FAILED, ret = ", (uint64_t)r);
        else if (!wait_flag(&g_online[1], 2, 10, false))
            puts_("Watari EL1: cpu 1 did not come back\n");
    }
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

    /* More than one redistributor in the vGIC (GICR_TYPER.Last clear on frame 0):
     * run the SMP test. */
    unsigned ncpu = 1;
    while (ncpu < MAX_CPUS && !(rd64(GICR_PHYS + (ncpu - 1) * 0x20000UL + 8) & 0x10))
        ncpu++;
    if (ncpu > 1)
        smp_test(ncpu);

    puts_("Watari EL1: calling PSCI SYSTEM_OFF\n");
    hvc(PSCI_SYSTEM_OFF);
    puts_("Watari EL1: SYSTEM_OFF returned?!\n");
    for (;;)
        __asm__ volatile("wfe");
}
