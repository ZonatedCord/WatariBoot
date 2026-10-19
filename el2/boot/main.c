// SPDX-License-Identifier: MIT
// Watari EL2 M0-C .. M1-3: boot skeleton (QEMU virt, TCG).
// Enters at EL2, installs vectors, builds stage 2, drops to a tiny EL1 payload
// and answers its HVCs (PSCI, see psci.c). M1-3: secondary cores are started by
// the guest's PSCI CPU_ON and enter through el2_secondary_main().
// Written from the Arm ARM and PSCI DEN0022; no third-party code copied.

#include <stdint.h>
#include "wel2_vgic.h"
#include "boot.h"

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

#define EC_DABT_LOWER 0x24
#define EC_SYSREG     0x18   /* trapped MSR/MRS/system instruction (Arm ARM D24.2) */

extern uint8_t __el1_stack_top[];

struct pcpu g_pcpu[MAX_CPUS];
unsigned g_ncpu;
struct vgic g_vgic;
uint64_t g_hcr;

/* ---- Locks ----------------------------------------------------------------
 * EL2 runs with the MMU off, so these exclusives hit Device memory: fine in
 * QEMU TCG; real hardware needs an EL2 stage-1 map with Normal memory first
 * (same prerequisite as F-18). Spins are bounded and report, never hang
 * silently (F-14). */
static volatile uint32_t g_big, g_uart;

static bool spin_lock(volatile uint32_t *l, unsigned long limit)
{
    for (unsigned long n = 0; n < limit; n++) {
        if (!__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE))
            return true;
        __asm__ volatile("yield");
    }
    return false;
}

static void spin_unlock(volatile uint32_t *l)
{
    __atomic_store_n(l, 0, __ATOMIC_RELEASE);
}

void big_lock(void)
{
    if (!spin_lock(&g_big, 1UL << 28)) {
        puts_("Watari EL2: big lock timeout, stealing it\n");
        g_big = 1;
    }
}

void big_unlock(void)
{
    spin_unlock(&g_big);
}

unsigned this_cpu(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, tpidr_el2" : "=r"(v));
    return (unsigned)v;
}

/* ---- UART (PL011; QEMU needs no init) ---------------------------------------
 * puts_() holds a lock for the whole string so that lines of different cores do
 * not interleave; the EL1 payload shares these functions. */
void putc_(char c)
{
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    volatile uint32_t *dr = (volatile uint32_t *)(UART0_BASE + UART_DR);
    while (*fr & UART_FR_TXFF)
        ;
    *dr = (uint32_t)c;
}

void puts_(const char *s)
{
    bool got = spin_lock(&g_uart, 1UL << 24);

    while (*s) {
        if (*s == '\n')
            putc_('\r');
        putc_(*s++);
    }
    if (got)
        spin_unlock(&g_uart);
}

static char *fmt_hex(char *p, uint64_t v)
{
    *p++ = '0';
    *p++ = 'x';
    for (int i = 60; i >= 0; i -= 4)
        *p++ = "0123456789abcdef"[(v >> i) & 0xf];
    return p;
}

static char *fmt_str(char *p, const char *s, char *end)
{
    while (*s && p < end)
        *p++ = *s++;
    return p;
}

void puthex(uint64_t v)
{
    char b[19];

    *fmt_hex(b, v) = 0;
    puts_(b);
}

/* "<k>0x<v>\n" as one atomic write. */
void put_kv(const char *k, uint64_t v)
{
    char b[160], *p = fmt_str(b, k, b + 120);

    p = fmt_hex(p, v);
    *p++ = '\n';
    *p = 0;
    puts_(b);
}

/* "<pre><decimal n><post>" as one atomic write (post carries the newline). */
void put_kn(const char *pre, unsigned n, const char *post)
{
    char b[160], t[12], *p = fmt_str(b, pre, b + 100);
    unsigned k = 0;

    do {
        t[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n && k < sizeof(t));
    while (k)
        *p++ = t[--k];
    p = fmt_str(p, post, b + 150);
    *p = 0;
    puts_(b);
}

/* ---- Exception handling -------------------------------------------------- */
static const char *const vec_names[16] = {
    "cur_el_sp0 sync", "cur_el_sp0 irq", "cur_el_sp0 fiq", "cur_el_sp0 serror",
    "cur_el_spx sync", "cur_el_spx irq", "cur_el_spx fiq", "cur_el_spx serror",
    "lower_a64 sync", "lower_a64 irq", "lower_a64 fiq", "lower_a64 serror",
    "lower_a32 sync", "lower_a32 irq", "lower_a32 fiq", "lower_a32 serror",
};

void __attribute__((noreturn)) fatal_msg(const char *why)
{
    puts_("Watari EL2: FATAL: ");
    puts_(why);
    puts_("\nWatari EL2: halted\n");
    for (;;)
        __asm__ volatile("wfe");
}

/* Never hang silently (lesson F-14): dump state, then halt. */
static void __attribute__((noreturn)) fatal(struct frame *f, unsigned idx)
{
    put_kn("Watari EL2: cpu ", this_cpu(), " UNEXPECTED EXCEPTION: \n");
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

static unsigned g_hvc_log_budget = 8;

static void handle_hvc(struct frame *f)
{
    uint32_t imm = f->esr & 0xffff;
    uint32_t fid = (uint32_t)f->x[0];

    if (g_hvc_log_budget) {
        g_hvc_log_budget--;
        puts_("Watari EL2: HVC from EL1, imm=");
        puthex(imm);
        puts_(" fid=");
        puthex(fid);
        puts_("\n");
    }

    if (imm == 0 && psci_handle(f, fid))
        return;
    /* Unknown conduit/immediate: SMCCC NOT_SUPPORTED. ELR already points past
     * the HVC. */
    f->x[0] = (uint64_t)-1LL;
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
 * M1-3: all cores share these tables (one VTTBR value, VMID 0).
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
}

/* Per core: VTCR_EL2 (Arm ARM D24.2.223): RES1 bit31, PS=0b010 (40 bit), TG0=0
 * (4 KB), SH0=3, ORGN0=IRGN0=1 (WB WA), SL0=1 (start level 1), T0SZ=32. */
void stage2_enable(void)
{
    uint64_t vtcr = (1ULL << 31) | (2ULL << 16) | (3ULL << 12) | (1ULL << 10) |
                    (1ULL << 8) | (1ULL << 6) | 32ULL;
    __asm__ volatile("msr vtcr_el2, %0" : : "r"(vtcr));
    __asm__ volatile("msr vttbr_el2, %0" : : "r"((uint64_t)(uintptr_t)s2_l1));
    /* Tables were written with the MMU off; on real HW add cache maintenance
     * if the stage-2 walker is cacheable. */
    __asm__ volatile("dsb ishst; tlbi vmalls12e1is; dsb ish; isb" : : : "memory");
}

/* ---- Virtual GIC wiring --------------------------------------------------- */
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

static unsigned g_eff_log_budget = 12;

static void vgic_effect_cb(void *ctx, enum vgic_effect eff, uint32_t intid, int arg)
{
    (void)ctx;
    if (g_eff_log_budget) {
        g_eff_log_budget--;
        puts_("Watari EL2: vgic effect=");
        puthex(eff);
        puts_(" intid=");
        puthex(intid);
        puts_(" arg=");
        puthex((uint64_t)(int64_t)arg);
        puts_("\n");
    }
    cpuif_vgic_effect(eff, intid, arg);        /* M1-3: apply to the physical GICD */
}

static void vgic_setup(void)
{
    uint32_t aff[MAX_CPUS];

    for (unsigned i = 0; i < g_ncpu; i++)
        aff[i] = g_pcpu[i].aff;
    struct vgic_config cfg = {
        .gicd_base = GICD_PHYS,
        .gicr_base = GICR_PHYS,
        .nr_vcpus = g_ncpu,
        .affinity = aff,
        .hooks = { vgic_log_cb, vgic_effect_cb, 0 },
    };
    if (vgic_init(&g_vgic, &cfg) != 0)
        fatal_msg("vgic_init failed");
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

    big_lock();
    if (idx == 8 && ec == EC_HVC64) {
        handle_hvc(f);
    } else if (idx == 8 && ec == EC_DABT_LOWER) {
        handle_dabt(f, idx);
    } else if (idx == 8 && ec == EC_SYSREG && cpuif_sysreg_trap(f)) {
        /* ICC_SGI*R_EL1 write: the SGI is now pending in the model */
    } else if (idx == 9) {
        cpuif_irq();                         /* physical IRQ: timer / maintenance / kick / SPI */
        big_unlock();
        return;
    } else {
        fatal(f, idx);
    }
    /* Any guest-visible state change (vGIC MMIO write, SGI, ...) may have made
     * an interrupt deliverable, here or on another core. */
    cpuif_flush();
    cpuif_kick_others();
    big_unlock();
}

/* Identify this core among the redistributors and publish its index. */
static void bind_this_cpu(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    unsigned idx = cpu_from_mpidr(mpidr);

    if (idx == ~0u)
        fatal_msg("MPIDR not found among the redistributors");
    __asm__ volatile("msr tpidr_el2, %0" : : "r"((uint64_t)idx));
}

/* What the guest reads in MIDR_EL1 / MPIDR_EL1 (VPIDR_EL2 / VMPIDR_EL2): the
 * physical values, so a PSCI MPIDR means the same to both sides. */
static void set_virtual_ids(void)
{
    uint64_t midr, mpidr;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    __asm__ volatile("msr vpidr_el2, %0; msr vmpidr_el2, %1; isb" : : "r"(midr), "r"(mpidr));
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

    /* Cores = redistributor frames (GICR_TYPER walk); vCPU i = core i. */
    g_ncpu = gic_phys_discover();
    bind_this_cpu();
    put_kv("Watari EL2: cores (redistributors) = ", g_ncpu);
    for (unsigned i = 0; i < g_ncpu; i++)
        g_pcpu[i].state = CPU_ST_OFF;
    g_pcpu[this_cpu()].state = CPU_ST_ON;      /* the cold-boot core is ON */

    /* HCR_EL2: RW=1 (EL1 is AArch64), IMO/FMO/AMO=1 (physical IRQ/FIQ/SError
     * routed to EL2, as designed). E2H=0, TSC=0 so far; VM=1 since M1-1 (stage-2
     * with the GIC windows unmapped). VHE and SMC trapping come later. */
    g_hcr = HCR_RW | HCR_IMO | HCR_FMO | HCR_AMO | HCR_VM;
    vgic_setup();
    stage2_init();
    stage2_enable();
    set_virtual_ids();
    puts_("Watari EL2: stage-2 on (identity, GICD/GICR unmapped), vGIC ready\n");
    gic_phys_init_dist();
    gic_phys_init_cpu();
    cpuif_init(&g_vgic);
    puts_("Watari EL2: physical GIC owned by EL2, virtual CPU interface on\n");
    puts_("Watari EL2: eret to EL1h payload\n");
    enter_el1((uint64_t)el1_main, (uint64_t)__el1_stack_top, SPSR_EL1H_MASKED, g_hcr, 0);
}

/* Cores 1..n: started by PSCI CPU_ON through the firmware (secondary_entry in
 * start.S, EL2, MMU off). Same per-core setup as the boot core, then ERET to
 * the entry point the guest asked for, x0 = context_id (PSCI 6.4.3). */
void __attribute__((noreturn)) el2_secondary_main(void)
{
    bind_this_cpu();
    unsigned me = this_cpu();

    stage2_enable();
    set_virtual_ids();
    gic_phys_init_cpu();
    cpuif_init_cpu();

    big_lock();
    uint64_t entry = g_pcpu[me].entry, ctx = g_pcpu[me].ctx;
    g_pcpu[me].state = CPU_ST_ON;
    put_kn("Watari EL2: cpu ", me, " up at EL2, eret to the guest entry point\n");
    big_unlock();
    /* No flush needed here: interrupts sent while ON_PENDING are delivered on
     * this core's first trap/IRQ, and the guest enables its CPU interface only
     * after it runs. */
    enter_el1(entry, 0, SPSR_EL1H_MASKED, g_hcr, ctx);
}
