// SPDX-License-Identifier: MIT
// Watari EL2 M0-C: first bootable skeleton (QEMU virt, TCG).
// Enters at EL2, installs vectors, drops to a tiny EL1 payload, answers its
// HVC (PSCI_VERSION / SYSTEM_OFF) per INTERFACES section 9.
// Written from the Arm ARM and PSCI DEN0022; no third-party code copied.

#include <stdint.h>
#include "wel2_vgic.h"

#define UART0_BASE   0x09000000UL
#define UART_DR      0x00
#define UART_FR      0x18
#define UART_FR_TXFF (1u << 5)

#define HCR_VM   (1ULL << 0)
#define HCR_FMO  (1ULL << 3)
#define HCR_IMO  (1ULL << 4)
#define HCR_AMO  (1ULL << 5)
#define HCR_RW   (1ULL << 31)

#define SPSR_EL1H_MASKED 0x3c5ULL   /* M=EL1h, DAIF all masked */

#define EC_HVC64 0x16
#define EC_SMC64 0x17

#define PSCI_VERSION     0x84000000u
#define PSCI_SYSTEM_OFF  0x84000008u
#define PSCI_VER_1_1     0x00010001u
#define PSCI_NOT_SUPPORTED (-1LL)

/* QEMU virt GICv3 windows (hw/arm/virt.c memmap). */
#define GICD_PHYS   0x08000000UL
#define GICD_SIZE   0x10000UL
#define GICR_PHYS   0x080A0000UL
#define NR_VCPUS    1u

#define EC_DABT_LOWER 0x24

struct frame {
    uint64_t x[31];
    uint64_t esr, elr, spsr, far;
};

extern uint8_t __el1_stack_top[];
void enter_el1(uint64_t entry, uint64_t sp, uint64_t spsr, uint64_t hcr)
    __attribute__((noreturn));

/* ---- UART (PL011; QEMU needs no init) ---------------------------------- */
static void putc_(char c)
{
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    volatile uint32_t *dr = (volatile uint32_t *)(UART0_BASE + UART_DR);
    while (*fr & UART_FR_TXFF)
        ;
    *dr = (uint32_t)c;
}

static void puts_(const char *s)
{
    while (*s) {
        if (*s == '\n')
            putc_('\r');
        putc_(*s++);
    }
}

static void puthex(uint64_t v)
{
    puts_("0x");
    for (int i = 60; i >= 0; i -= 4)
        putc_("0123456789abcdef"[(v >> i) & 0xf]);
}

static void put_kv(const char *k, uint64_t v)
{
    puts_(k);
    puthex(v);
    puts_("\n");
}

/* ---- Exception handling -------------------------------------------------- */
static const char *const vec_names[16] = {
    "cur_el_sp0 sync", "cur_el_sp0 irq", "cur_el_sp0 fiq", "cur_el_sp0 serror",
    "cur_el_spx sync", "cur_el_spx irq", "cur_el_spx fiq", "cur_el_spx serror",
    "lower_a64 sync", "lower_a64 irq", "lower_a64 fiq", "lower_a64 serror",
    "lower_a32 sync", "lower_a32 irq", "lower_a32 fiq", "lower_a32 serror",
};

/* Never hang silently (lesson F-14): dump state, then halt. */
static void __attribute__((noreturn)) fatal(struct frame *f, unsigned idx)
{
    puts_("Watari EL2: UNEXPECTED EXCEPTION: ");
    puts_(vec_names[idx & 15]);
    puts_("\n");
    put_kv("  ESR_EL2  = ", f->esr);
    put_kv("  ELR_EL2  = ", f->elr);
    put_kv("  FAR_EL2  = ", f->far);
    put_kv("  SPSR_EL2 = ", f->spsr);
    puts_("Watari EL2: halted\n");
    for (;;)
        __asm__ volatile("wfe");
}

static void __attribute__((noreturn)) psci_system_off(void)
{
    /* QEMU virt with virtualization=on uses the SMC conduit for PSCI. */
    register uint64_t x0 __asm__("x0") = PSCI_SYSTEM_OFF;
    __asm__ volatile("smc #0" : "+r"(x0) : : "memory");
    puts_("Watari EL2: SYSTEM_OFF returned?!\n");
    for (;;)
        __asm__ volatile("wfe");
}

static void handle_hvc(struct frame *f)
{
    uint32_t imm = f->esr & 0xffff;
    uint32_t fid = (uint32_t)f->x[0];

    puts_("Watari EL2: HVC from EL1, imm=");
    puthex(imm);
    puts_(" fid=");
    puthex(fid);
    puts_("\n");

    int64_t ret = PSCI_NOT_SUPPORTED;
    if (imm == 0) {
        switch (fid) {
        case PSCI_VERSION:
            ret = PSCI_VER_1_1;
            break;
        case PSCI_SYSTEM_OFF:
            puts_("Watari EL2: PSCI SYSTEM_OFF -> forwarding to QEMU firmware (SMC)\n");
            psci_system_off();
        default:
            break;
        }
    }
    /* SMCCC: result in x0, x1-x3 zeroed (INTERFACES section 9). ELR already
     * points past the HVC. */
    f->x[0] = (uint64_t)ret;
    f->x[1] = f->x[2] = f->x[3] = 0;
}


/* libc stubs: the compiler may lower loops to these. */
void *memset(void *d, int c, unsigned long n)
{
    uint8_t *p = d;
    while (n--)
        *p++ = (uint8_t)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}

/* ---- Stage-2 translation (identity map, 4 KB granule) ------------------------
 * Choice: 4 KB granule for the spike (Arm ARM D8, VMSAv8-64 stage 2). Apple
 * cores implement only 16 KB, so the hardware port must redo the tables with
 * TG0=16 KB; the map logic (identity, device attributes, GIC holes) is
 * granule independent. [hypothesis for M1 HW]
 *
 * IPA space 32 bits (T0SZ=32), start level 1 (SL0=1): 4 entries of 1 GB.
 *   0x00000000-0x3fffffff : level 2 table of 2 MB device blocks (MMIO), except
 *                           the 2 MB block at 0x08000000, a level-3 table of
 *                           4 KB device pages with the GICD (64 KB) and the
 *                           first 8 GICR frames (8 x 128 KB) UNMAPPED, so the
 *                           guest faults to EL2 there.
 *   0x40000000-0x7fffffff : 1 GB Normal WB block (QEMU virt RAM).
 *   0x80000000-0xffffffff : 1 GB device blocks (unused on virt).
 * Note: EL2's own image sits in the RAM block, i.e. it is visible to the
 * guest. Isolating EL2 memory is a later hardening step.
 */
#define S2_VALID   (1ULL << 0)
#define S2_TABLE   (1ULL << 1)         /* table (levels 0-2) / page (level 3) */
#define S2_AF      (1ULL << 10)
#define S2_SH_ISH  (3ULL << 8)
#define S2_S2AP_RW (3ULL << 6)
#define S2_XN      (1ULL << 54)        /* XN[1]=1: execute-never */
#define S2_ATTR_DEV    (0x0ULL << 2)   /* MemAttr 0b0000: Device-nGnRnE */
#define S2_ATTR_NORMAL (0xfULL << 2)   /* MemAttr 0b1111: Normal, Inner+Outer WB */

#define S2_BLOCK_DEV    (S2_VALID | S2_AF | S2_S2AP_RW | S2_ATTR_DEV | S2_XN)
#define S2_BLOCK_NORMAL (S2_VALID | S2_AF | S2_S2AP_RW | S2_ATTR_NORMAL | S2_SH_ISH)

static uint64_t s2_l1[4] __attribute__((aligned(4096)));
static uint64_t s2_l2[512] __attribute__((aligned(4096)));
static uint64_t s2_l3[512] __attribute__((aligned(4096)));

static void stage2_init(void)
{
    /* level 3 for 0x08000000-0x081fffff: 512 x 4 KB device pages */
    for (unsigned i = 0; i < 512; i++)
        s2_l3[i] = (GICD_PHYS + ((uint64_t)i << 12)) | S2_BLOCK_DEV | S2_TABLE;
    unsigned first = 0, n = GICD_SIZE >> 12;                       /* GICD */
    for (unsigned i = first; i < first + n; i++)
        s2_l3[i] = 0;
    first = (GICR_PHYS - GICD_PHYS) >> 12;                         /* GICR x8 */
    n = (8 * VGIC_GICR_FRAME_SIZE) >> 12;
    for (unsigned i = first; i < first + n; i++)
        s2_l3[i] = 0;

    for (unsigned i = 0; i < 512; i++)
        s2_l2[i] = ((uint64_t)i << 21) | S2_BLOCK_DEV;
    s2_l2[GICD_PHYS >> 21] = (uint64_t)(uintptr_t)s2_l3 | S2_VALID | S2_TABLE;

    s2_l1[0] = (uint64_t)(uintptr_t)s2_l2 | S2_VALID | S2_TABLE;
    s2_l1[1] = 0x40000000ULL | S2_BLOCK_NORMAL;
    s2_l1[2] = 0x80000000ULL | S2_BLOCK_DEV;
    s2_l1[3] = 0xc0000000ULL | S2_BLOCK_DEV;

    /* VTCR_EL2 (Arm ARM D24.2.223): RES1 bit31, PS=0b010 (40 bit), TG0=0
     * (4 KB), SH0=3, ORGN0=IRGN0=1 (WB WA), SL0=1 (start level 1), T0SZ=32. */
    uint64_t vtcr = (1ULL << 31) | (2ULL << 16) | (3ULL << 12) | (1ULL << 10) |
                    (1ULL << 8) | (1ULL << 6) | 32ULL;
    __asm__ volatile("msr vtcr_el2, %0" : : "r"(vtcr));
    __asm__ volatile("msr vttbr_el2, %0" : : "r"((uint64_t)(uintptr_t)s2_l1));
    /* Tables were written with the MMU off; on real HW add cache maintenance
     * if the stage-2 walker is cacheable. */
    __asm__ volatile("dsb ishst; tlbi vmalls12e1is; dsb ish; isb" : : : "memory");
}

/* ---- Virtual GIC wiring --------------------------------------------------- */
static struct vgic g_vgic;
static unsigned g_log_budget = 16;

static void vgic_log_cb(void *ctx, enum vgic_log_kind kind, uint64_t addr,
                        unsigned size, bool is_write, uint64_t val)
{
    (void)ctx;
    if (!g_log_budget)
        return;
    g_log_budget--;
    puts_("Watari EL2: vgic log kind=");
    puthex(kind);
    puts_(is_write ? " W addr=" : " R addr=");
    puthex(addr);
    puts_(" size=");
    puthex(size);
    puts_(" val=");
    puthex(val);
    puts_("\n");
}

static void vgic_effect_cb(void *ctx, enum vgic_effect eff, uint32_t intid, int arg)
{
    (void)ctx;
    /* M1-1: the physical GIC is not touched; effects are only reported. */
    puts_("Watari EL2: vgic effect=");
    puthex(eff);
    puts_(" intid=");
    puthex(intid);
    puts_(" arg=");
    puthex((uint64_t)(int64_t)arg);
    puts_("\n");
}

static void vgic_setup(void)
{
    struct vgic_config cfg = {
        .gicd_base = GICD_PHYS,
        .gicr_base = GICR_PHYS,
        .nr_vcpus = NR_VCPUS,
        .affinity = 0,
        .hooks = { vgic_log_cb, vgic_effect_cb, 0 },
    };
    if (vgic_init(&g_vgic, &cfg) != 0) {
        puts_("Watari EL2: vgic_init failed\n");
        for (;;)
            __asm__ volatile("wfe");
    }
}

/* Guest data abort at stage 2 (Arm ARM D24.2.40 ESR_EL2, "ISS encoding for an
 * exception from a Data Abort"): ISV[24] SAS[23:22] SSE[21] SRT[20:16] SF[15]
 * WnR[6] DFSC[5:0]. IPA = HPFAR_EL2.FIPA[43:4] << 8 | FAR_EL2[11:0]. */
static void handle_dabt(struct frame *f, unsigned idx)
{
    uint64_t hpfar;
    __asm__ volatile("mrs %0, hpfar_el2" : "=r"(hpfar));
    uint64_t ipa = (((hpfar >> 4) & 0xfffffffffULL) << 12) | (f->far & 0xfff);
    uint32_t iss = f->esr & 0x1ffffff;
    int isv = (iss >> 24) & 1;
    unsigned size = 1u << ((iss >> 22) & 3);
    int sse = (iss >> 21) & 1;
    unsigned srt = (iss >> 16) & 31;
    int wnr = (iss >> 6) & 1;
    uint32_t dfsc = iss & 0x3f;
    int in_gic = (ipa >= GICD_PHYS && ipa < GICD_PHYS + GICD_SIZE) ||
                 (ipa >= GICR_PHYS && ipa < GICR_PHYS + 8 * VGIC_GICR_FRAME_SIZE);

    if (!isv || !in_gic || (dfsc >> 2) != 1 /* translation fault, any level */) {
        puts_("Watari EL2: unhandled guest data abort\n");
        put_kv("  HPFAR_EL2 = ", hpfar);
        put_kv("  IPA       = ", ipa);
        put_kv("  ISV       = ", (uint64_t)isv);
        fatal(f, idx);
    }

    uint64_t val = 0;
    int rc;
    if (wnr) {
        val = srt == 31 ? 0 : f->x[srt];
        if (size < 8)
            val &= (1ULL << (size * 8)) - 1;
        rc = vgic_mmio_write(&g_vgic, ipa, size, val);
    } else {
        rc = vgic_mmio_read(&g_vgic, ipa, size, &val);
        if (rc == VGIC_OK) {
            if (sse && size < 8 && ((val >> (size * 8 - 1)) & 1))
                val |= ~0ULL << (size * 8);
            if (!((iss >> 15) & 1))        /* SF=0: 32-bit destination */
                val &= 0xffffffffULL;
            if (srt != 31)
                f->x[srt] = val;
        }
    }
    if (rc != VGIC_OK) {
        puts_("Watari EL2: vgic rejected access (rc=");
        puthex((uint64_t)(int64_t)rc);
        puts_(")\n");
        put_kv("  IPA       = ", ipa);
        fatal(f, idx);
    }
    f->elr += 4;   /* AArch64 guest: ISV=1 implies a 4-byte instruction */
}

void el2_exception(struct frame *f, unsigned idx)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    if (idx == 8 && ec == EC_HVC64) {
        handle_hvc(f);
        return;
    }
    if (idx == 8 && ec == EC_DABT_LOWER) {
        handle_dabt(f, idx);
        return;
    }
    fatal(f, idx);
}

/* ---- EL1 payload ---------------------------------------------------------- */
static uint64_t hvc(uint64_t fid)
{
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = 0;
    register uint64_t x2 __asm__("x2") = 0;
    register uint64_t x3 __asm__("x3") = 0;
    __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : : "memory");
    return x0;
}


/* ---- M1-1 test: guest view of the virtual GIC ------------------------------ */
static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uint64_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uint64_t a, uint8_t v) { *(volatile uint8_t *)a = v; }
static inline uint8_t rd8(uint64_t a) { return *(volatile uint8_t *)a; }

static void el1_vgic_test(void)
{
    const uint64_t D = GICD_PHYS, R = GICR_PHYS;
    put_kv("Watari EL1: GICD_CTLR  = ", rd32(D + 0x0000));
    put_kv("Watari EL1: GICD_TYPER = ", rd32(D + 0x0004));
    put_kv("Watari EL1: GICD_IIDR  = ", rd32(D + 0x0008));
    put_kv("Watari EL1: GICR_TYPER = ", rd64(R + 0x0008));

    /* GICR_WAKER (RD frame +0x14): ProcessorSleep bit1, ChildrenAsleep bit2.
     * GIC spec IHI 0069H.b, redistributor power management. */
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

static void __attribute__((noreturn)) el1_main(void)
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

    puts_("Watari EL1: calling PSCI SYSTEM_OFF\n");
    hvc(PSCI_SYSTEM_OFF);
    puts_("Watari EL1: SYSTEM_OFF returned?!\n");
    for (;;)
        __asm__ volatile("wfe");
}

/* ---- EL2 main ---------------------------------------------------------------- */
void __attribute__((noreturn)) el2_main(void)
{
    uint64_t el, vbar;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, vbar_el2" : "=r"(vbar));

    puts_("Watari EL2: hello from EL2\n");
    put_kv("Watari EL2: CurrentEL = ", el >> 2);
    put_kv("Watari EL2: VBAR_EL2  = ", vbar);

    /* HCR_EL2: RW=1 (EL1 is AArch64), IMO/FMO/AMO=1 (physical IRQ/FIQ/SError
     * routed to EL2, as designed). E2H=0, TSC=0 so far; VM=1 since M1-1 (stage-2
     * with the GIC windows unmapped). VHE and SMC trapping come later. */
    uint64_t hcr = HCR_RW | HCR_IMO | HCR_FMO | HCR_AMO | HCR_VM;
    vgic_setup();
    stage2_init();
    puts_("Watari EL2: stage-2 on (identity, GICD/GICR unmapped), vGIC ready\n");
    puts_("Watari EL2: eret to EL1h payload\n");
    enter_el1((uint64_t)el1_main, (uint64_t)__el1_stack_top, SPSR_EL1H_MASKED, hcr);
}
