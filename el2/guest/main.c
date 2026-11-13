// SPDX-License-Identifier: MIT
// Watari EL2 test guest (T7a): checks G1-G11 (and G12, E9: colour bars in the handed-over framebuffer) of docs/design/watari-el2-m1boot/DESIGN.md section 8.
// All output goes through the layer's vendor hypercalls (WEL2_LOG, WEL2_CHECKPOINT; include/wel2_hvc.h). Only when
// the layer answers NOT_SUPPORTED and no info block was passed (= QEMU chain build before the layer implements the
// calls) the log lines are written to the QEMU PL011 instead, so the same binary can be tested today.
// Every wait has a timeout: on expiry the check FAILs and the run continues, it never hangs silently.
// Register behaviour: GIC architecture specification IHI 0069 (ICC_*_EL1), Arm ARM (generic timer, PAuth), PSCI
// DEN0022F.b. Position independent: no function-pointer tables or initialised pointers (nothing is relocated).

#include "guest.h"

#define UART_BASE 0x09000000ull         /* QEMU virt PL011, fallback only */

u64 g_frq;
volatile u32 g_irq_count[MAX_CORES][32];
volatile u32 g_irq_last, g_irq_other, g_exc, g_svc, g_fiq;

extern void secondary_entry(void);
extern u64 secondary_entry_addr(void);
extern void guest_vectors(void);
extern unsigned char __stacks[];

/* ---- run state ---------------------------------------------------------------------------------------------- */
static struct wel2_guest_info g_info;
static bool g_have_info;
static bool g_uart_fallback_ok;         /* no info block: QEMU virt assumed, the PL011 may be used */
static bool g_is_apple;                 /* MIDR_EL1 implementer 0x61: never touch QEMU's MMIO map */
static bool g_log_works;                /* a WEL2_LOG call succeeded at least once */
static unsigned g_boot_core;            /* redistributor index of the core running guest_main */
static volatile u32 g_irq_entries, g_spur_run, g_storm;   /* D1: IRQ storm bounds, see irq_handler */
static bool g_uart_mode;                /* WEL2_LOG answered NOT_SUPPORTED */
static unsigned g_pass, g_fail;
static volatile u32 g_log_errs;         /* WEL2_LOG calls the layer rejected */
static volatile u32 g_chk_running;      /* the check in progress, for the exception handler's message */
static struct wel2_stats g_stats __attribute__((aligned(8)));
static bool g_stats_ok;

struct core_state {
    volatile u32 up;                    /* secondary: set once it ran its init and logged */
    volatile u32 round;                 /* times the core entered secondary_main */
    volatile u32 cmd;                   /* 0 idle, 1 = call CPU_OFF */
    volatile u32 fail;
    volatile u32 offs;                  /* times the secondary reached its CPU_OFF call */
};
static struct core_state g_core[MAX_CORES];

/* ---- output ------------------------------------------------------------------------------------------------ */
u64 ms_to_ticks(u64 ms) { return g_frq * ms / 1000; }

void b_init(struct buf *b) { b->n = 0; b->s[0] = 0; }

void b_str(struct buf *b, const char *s)
{
    while (*s && b->n < WEL2_LOG_MAX - 1)
        b->s[b->n++] = *s++;
    b->s[b->n] = 0;
}

void b_hex(struct buf *b, u64 v)
{
    char t[16];
    unsigned k = 0;

    b_str(b, "0x");
    do {
        t[k++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v);
    while (k && b->n < WEL2_LOG_MAX - 1)
        b->s[b->n++] = t[--k];
    b->s[b->n] = 0;
}

void b_dec(struct buf *b, u64 v)
{
    char t[20];
    unsigned k = 0;

    do {
        t[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (k && b->n < WEL2_LOG_MAX - 1)
        b->s[b->n++] = t[--k];
    b->s[b->n] = 0;
}

static void b_sdec(struct buf *b, int64_t v)
{
    if (v < 0) {
        b_str(b, "-");
        v = -v;
    }
    b_dec(b, (u64)v);
}

static void uart_puts(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            *(volatile u32 *)UART_BASE = '\r';
        *(volatile u32 *)UART_BASE = (unsigned char)*s;
    }
}

void b_flush(struct buf *b)
{
    if (!g_uart_mode) {
        int64_t r = (int64_t)hvc4(WEL2_LOG, (u64)(uintptr_t)b->s, b->n + 1, 0);

        if (r == 0) {
            g_log_works = true;
            return;
        }
        if (r == WEL2_HVC_NOT_SUPPORTED && g_uart_fallback_ok)
            g_uart_mode = true;         /* the layer has no LOG yet: use the PL011 from now on */
        else {
            if (r != WEL2_HVC_NOT_SUPPORTED)
                g_log_errs++;
            return;
        }
    }
    uart_puts("Watari EL1: ");
    uart_puts(b->s);
    uart_puts("\n");
}

static void say(const char *s)
{
    struct buf b;

    b_init(&b);
    b_str(&b, s);
    b_flush(&b);
}

static void checkpoint(unsigned n, unsigned state)
{
    hvc4(WEL2_CHECKPOINT, n, state, 0);         /* NOT_SUPPORTED in the fallback case: nothing to do */
}

static bool selected(unsigned n)
{
    return !g_have_info || g_info.checks == 0 || (g_info.checks >> n) & 1;
}

/* T17: the per-check summary printed last, right before the final reset/hold (see summary()). One slot per check;
 * `key` is the short result text (the detail's start, or what the check put into g_key). */
#define SUM_N   13
#define SUM_KEY 44
static const char g_sum_name[SUM_N][5] = { "G1", "G2", "G3", "G4", "G5a", "G5b", "G6", "G7", "G8", "G9", "G10", "G11", "G12" };
static struct { char verdict; char key[SUM_KEY]; } g_sum[SUM_N];     /* verdict: 0 not run, 'P', 'F', 'S' */
static struct buf g_key;                                              /* optional key text of the running check */

static void sum_put(const char *name, char verdict, const char *text)
{
    for (unsigned i = 0; i < SUM_N; i++) {
        unsigned k = 0;

        while (name[k] && name[k] == g_sum_name[i][k])
            k++;
        if (name[k] || g_sum_name[i][k])
            continue;
        g_sum[i].verdict = verdict;
        for (k = 0; text[k] && k < SUM_KEY - 1; k++)
            g_sum[i].key[k] = text[k];
        g_sum[i].key[k] = 0;
        return;
    }
}

static void begin(unsigned n)
{
    b_init(&g_key);
    g_chk_running = n;
    checkpoint(n, WEL2_CHKSTATE_RUNNING);
}

/* One result line "Gn PASS|FAIL <detail>" and the checkpoint square. */
static void finish(unsigned n, const char *name, bool ok, struct buf *detail)
{
    struct buf b;

    b_init(&b);
    b_str(&b, name);
    if (g_storm) {
        ok = false;
        g_storm = 0;
        b_str(detail, " IRQ-STORM(masked)");
    }
    b_str(&b, ok ? " PASS " : " FAIL ");
    b_str(&b, detail->s);
    b_flush(&b);
    sum_put(name, ok ? 'P' : 'F', g_key.n ? g_key.s : detail->s);
    checkpoint(n, ok ? WEL2_CHKSTATE_PASS : WEL2_CHKSTATE_FAIL);
    if (ok)
        g_pass++;
    else
        g_fail++;
    g_chk_running = 0;
}

/* A check that was not run: the line "Gn SKIP <why>" and, T6g, the grey square (WEL2_CHKSTATE_SKIP). */
static void skip(unsigned n, const char *name, const char *why)
{
    struct buf b;

    b_init(&b);
    b_str(&b, name);
    b_str(&b, " SKIP ");
    b_str(&b, why);
    b_flush(&b);
    sum_put(name, 'S', why);
    checkpoint(n, WEL2_CHKSTATE_SKIP);
}

/* ---- exceptions and interrupts --------------------------------------------------------------------------- */
static void timer_rearm(unsigned intid);

#define IRQ_ENTRY_LIMIT 5000u           /* IRQ entries allowed per wait window (a 10 ms timer gives ~5 in 50 ms) */
#define SPURIOUS_LIMIT  64u             /* consecutive acks of 1020-1023 */

static void timer_off(unsigned intid);

/* D1: a level interrupt that is never deactivated would re-enter forever. Past the limits the handler sets
 * SPSR_EL1.I so the eret returns masked, stops both timers and flags the running check as failed. */
static void irq_storm(const char *why)
{
    struct buf b;

    if (g_storm)
        return;
    g_storm = 1;
    SYSREG_WR(spsr_el1, SYSREG_RD(spsr_el1) | 0x80);
    timer_off(27);
    timer_off(30);
    b_init(&b);
    b_str(&b, "IRQ STORM: ");
    b_str(&b, why);
    b_str(&b, " during G");
    b_dec(&b, g_chk_running);
    b_str(&b, ", IRQ masked on return");
    b_flush(&b);
}

static void irq_handler(void)
{
    if (++g_irq_entries > IRQ_ENTRY_LIMIT) {
        irq_storm("too many IRQ entries in one wait window");
        return;
    }
    for (unsigned loop = 0; loop < 4; loop++) {
        u64 iar = SYSREG_RD(icc_iar1_el1);              /* virtual acknowledge */
        u32 id = (u32)(iar & 0xffffff);
        int c = gic_core_of(mpidr_now());

        if (id >= 1020) {                                /* 1020-1023: spurious / nothing left */
            if (++g_spur_run > SPURIOUS_LIMIT)
                irq_storm("endless spurious acknowledges");
            return;
        }
        g_spur_run = 0;
        g_irq_last = id;
        if (id < 32 && c >= 0)
            g_irq_count[c][id]++;
        else
            g_irq_other++;
        if (id == 27 || id == 30)
            timer_rearm(id);
        SYSREG_WR(icc_eoir1_el1, iar);
        /* EOImode = 1 (ICC_CTLR_EL1 bit 1) would need ICC_DIR_EL1 too; the checks leave it at 0 */
        if (SYSREG_RD(icc_ctlr_el1) & 2)
            SYSREG_WR(icc_dir_el1, iar);
        __asm__ volatile("isb");
    }
}

static void report_exception(unsigned idx, u64 esr, u64 elr, u64 far)
{
    struct buf b;

    b_init(&b);
    b_str(&b, "UNEXPECTED exception vector ");
    b_dec(&b, idx);
    b_str(&b, " EC ");
    b_hex(&b, esr >> 26);
    b_str(&b, " ESR ");
    b_hex(&b, esr);
    b_str(&b, " ELR ");
    b_hex(&b, elr);
    b_str(&b, " FAR ");
    b_hex(&b, far);
    b_str(&b, " during G");
    b_dec(&b, g_chk_running);
    b_flush(&b);
}

static volatile u32 g_fpac_expect, g_fpac_seen;          /* G3: a failed AUTIA may raise FEAT_FPAC's EC 0x1c exception */

void guest_exception(unsigned idx, void *frame)
{
    (void)frame;
    if (idx == 5) {                                      /* current EL, SPx, IRQ */
        irq_handler();
        return;
    }
    u64 esr = SYSREG_RD(esr_el1), elr = SYSREG_RD(elr_el1), far = SYSREG_RD(far_el1);

    if (idx == 4 && (esr >> 26) == 0x1c && g_fpac_expect) {   /* FEAT_FPAC: the authentication failure is an exception */
        g_fpac_seen++;
        SYSREG_WR(elr_el1, elr + 4);                     /* skip the AUTIA, as the check wants */
        return;
    }
    if (idx == 4 && (esr >> 26) == 0x15) {               /* SVC from the G1 round trip */
        g_svc++;
        return;
    }
    if (idx == 6) {                                      /* FIQ: group 0 virtual interrupt, ack so it cannot loop */
        u64 iar = SYSREG_RD(icc_iar0_el1);

        g_fiq++;
        if ((iar & 0xffffff) < 1020)
            SYSREG_WR(icc_eoir0_el1, iar);
        return;
    }
    g_exc++;
    report_exception(idx, esr, elr, far);
    if (g_exc > 8 || idx != 4 || (esr >> 26) == 0x16 || (esr >> 26) == 0x17) {
        say("too many or fatal exceptions: SYSTEM_RESET");
        hvc4(PSCI_SYSTEM_RESET, 0, 0, 0);
        for (;;)
            __asm__ volatile("wfe");
    }
    SYSREG_WR(elr_el1, elr + 4);                         /* skip the faulting instruction, the check will FAIL */
}

/* ---- generic timers (CNTV: PPI 27, CNTP: PPI 30), 10 ms period ---------------------------------------------- */
static u64 g_timer_ticks;
static volatile u32 g_timer_stop[2];                     /* [0] virtual, [1] physical: handler leaves the timer off */

static void timer_arm(unsigned intid)
{
    if (intid == 27) {
        SYSREG_WR(cntv_tval_el0, g_timer_ticks);
        SYSREG_WR(cntv_ctl_el0, 1);
    } else {
        SYSREG_WR(cntp_tval_el0, g_timer_ticks);
        SYSREG_WR(cntp_ctl_el0, 1);
    }
    __asm__ volatile("isb");
}

static void timer_off(unsigned intid)
{
    if (intid == 27)
        SYSREG_WR(cntv_ctl_el0, 0);
    else
        SYSREG_WR(cntp_ctl_el0, 0);
    __asm__ volatile("isb");
}

static void timer_rearm(unsigned intid)
{
    if (g_timer_stop[intid == 30])
        timer_off(intid);                                /* writing the new state also deasserts the level line */
    else
        timer_arm(intid);
}

/* ---- waiting with a timeout --------------------------------------------------------------------------------- */
/* Poll *p until it is >= want; interrupts are unmasked while waiting. Returns the elapsed ticks or ~0 on timeout. */
static u64 wait_ge(volatile u32 *p, u32 want, u64 ms)
{
    u64 t0 = cntvct(), end = t0 + ms_to_ticks(ms);

    g_irq_entries = 0;                                   /* new wait window (D1) */
    g_spur_run = 0;
    irq_on();
    while (*p < want) {
        if (cntvct() > end || g_storm) {
            irq_off();
            return ~0ull;
        }
        __asm__ volatile("nop");
    }
    irq_off();
    return cntvct() - t0;
}

/* ---- PSCI --------------------------------------------------------------------------------------------------- */
/* PSCI results are 32-bit signed values in w0 (DEN0022F.b 5.2): compare them as int32_t. */
static int64_t psci(u64 fid, u64 a1, u64 a2, u64 a3) { return (int64_t)(int32_t)hvc4(fid, a1, a2, a3); }

/* ---- the checks --------------------------------------------------------------------------------------------- */
static void g1(void)
{
    struct buf d;
    bool ok = true;
    u64 el = SYSREG_RD(CurrentEL) >> 2;
    u32 svc0 = g_svc;

    begin(WEL2_CHK_G1);
    b_init(&d);
    __asm__ volatile("svc #0" : : : "memory");           /* sync exception through our own vector table */
    b_str(&d, "EL");
    b_dec(&d, el);
    if (el != 1) ok = false;
    b_str(&d, " platform ");
    b_str(&d, g_have_info ? g_info.platform : "qemu-virt(assumed)");
    b_str(&d, g_have_info ? " info v" : " no info block");
    if (g_have_info)
        b_dec(&d, g_info.version);
    b_str(&d, " mpidr ");
    b_hex(&d, mpidr_now());
    b_str(&g_key, "MPIDR ");
    b_hex(&g_key, mpidr_now());
    b_str(&d, " vectors ");
    if (g_svc == svc0 + 1 && SYSREG_RD(vbar_el1) != 0) {
        b_str(&d, "ok(svc taken)");
    } else {
        b_str(&d, "svc NOT taken");
        ok = false;
    }
    b_str(&d, g_uart_mode ? " out=uart-fallback" : " out=hvc");
    /* DESIGN 8: the hand-over passes the info block. LOG working (the layer implements the calls) while x0 was no
     * valid info block is a hand-over failure; so is an Apple core without one (then nothing else is run). */
    if (!g_have_info && (g_log_works || g_is_apple)) {
        ok = false;
        b_str(&d, " NO valid info block in x0 (WEL2GINF magic/version/range)");
    }
    finish(WEL2_CHK_G1, "G1", ok, &d);
}

static void g2(void)
{
    struct buf d;
    bool ok = true;

    begin(WEL2_CHK_G2);
    b_init(&d);
    u64 v = (u64)psci(PSCI_VERSION, 0, 0, 0);
    b_str(&d, "version ");
    b_hex(&d, v);
    if ((v >> 16) < 1) ok = false;                       /* PSCI 1.0 or later */
    int64_t f1 = psci(PSCI_FEATURES, PSCI_CPU_ON_64, 0, 0);
    int64_t f2 = psci(PSCI_FEATURES, PSCI_SYSTEM_RESET, 0, 0);
    int64_t f3 = psci(PSCI_FEATURES, 0x8400003Fu, 0, 0);
    b_str(&d, " features cpu_on=");
    b_sdec(&d, f1);
    b_str(&d, " reset=");
    b_sdec(&d, f2);
    b_str(&d, f3 == PSCI_NOT_SUPPORTED ? " unknown=NOT_SUPPORTED" : " unknown=WRONG");
    if (f1 != 0 || f2 != 0 || f3 != PSCI_NOT_SUPPORTED) ok = false;
    int64_t aff = psci(PSCI_AFFINITY_64, mpidr_now(), 0, 0);
    b_str(&d, aff == 0 ? " self=ON" : " self=WRONG");
    if (aff != 0) ok = false;
    int64_t vu = (int64_t)hvc4(0xC6000FFFu, 0, 0, 0);    /* unknown id in the vendor range */
    b_str(&d, vu == PSCI_NOT_SUPPORTED ? " vendor-unknown=NOT_SUPPORTED" : " vendor-unknown=WRONG");
    if (vu != PSCI_NOT_SUPPORTED) ok = false;
    u64 wv = hvc4(WEL2_VERSION, 0, 0, 0);
    b_str(&d, " wel2_version=");
    if ((int64_t)wv == WEL2_HVC_NOT_SUPPORTED)
        b_str(&d, "n/a");
    else {
        b_hex(&d, wv);
        if (wv != ((WEL2_VERSION_TAG << 32) | WEL2_HVC_ABI_VERSION)) ok = false;
    }
    finish(WEL2_CHK_G2, "G2", ok, &d);
}

/* PAuth: the layer must not trap the key registers or PACIA/AUTIA (HCR_EL2.API/APK, DESIGN 5.1). */
static bool pauth_test(u64 *signed_out, bool *tamper_ok)
{
    u64 p = 0x0000000040281234ull, s = p, mod = 0x1234;
    u64 sctlr = SYSREG_RD(sctlr_el1);

    SYSREG_WR(sctlr_el1, sctlr | (1ull << 31));          /* EnIA */
    SYSREG_WR(APIAKeyLo_EL1, 0x0123456789abcdefull);
    SYSREG_WR(APIAKeyHi_EL1, 0xfedcba9876543210ull);
    __asm__ volatile("isb");
    __asm__ volatile(".arch_extension pauth\n pacia %0, %1" : "+r"(s) : "r"(mod));
    *signed_out = s;
    u64 a = s;
    __asm__ volatile(".arch_extension pauth\n autia %0, %1" : "+r"(a) : "r"(mod));
    u64 bad = s ^ (1ull << 51);                          /* corrupt the signature */
    u32 fpac0 = g_fpac_seen;
    g_fpac_expect = 1;                                   /* with FEAT_FPAC the failure is an EC 0x1c exception, else a poisoned pointer */
    __asm__ volatile(".arch_extension pauth\n autia %0, %1" : "+r"(bad) : "r"(mod));
    g_fpac_expect = 0;
    *tamper_ok = bad != p || g_fpac_seen != fpac0;       /* a failed authentication must not give the pointer back */
    SYSREG_WR(sctlr_el1, sctlr);
    __asm__ volatile("isb");
    return a == p && s != p;
}

/* T6g (PLAN S12): every allocated register of the ID group the layer may trap (HCR_EL2.TID3), printed as
 * "IDREG c<CRm>.<op2 base> v0 v1 v2 v3" (4 registers a line, "-" = not allocated). tools compare two runs, with and
 * without wel2.tid3=1 (run-tid3.sh): the lines must be identical (QEMU -cpu max reports GIC = 1 itself, so no
 * difference at all is expected there). The table copies vgic/idreg_tbl.c on purpose (no code shared with EL2). */
static const unsigned char g_idreg_known[8] = { 0x00, 0xff, 0xff, 0x77, 0x33, 0x33, 0x07, 0x07 };

#define IDRD(m, o) case (m) * 8 + (o): __asm__ volatile("mrs %0, S3_0_C0_C" #m "_" #o : "=r"(v)); break;
#define IDRD8(m) IDRD(m, 0) IDRD(m, 1) IDRD(m, 2) IDRD(m, 3) IDRD(m, 4) IDRD(m, 5) IDRD(m, 6) IDRD(m, 7)

static u64 idreg_read(unsigned crm, unsigned op2)
{
    u64 v = 0;

    switch (crm * 8 + op2) {
    IDRD8(1) IDRD8(2) IDRD8(3) IDRD8(4) IDRD8(5) IDRD8(6) IDRD8(7)
    default: break;
    }
    return v;
}

static void idreg_dump(void)
{
    for (unsigned crm = 1; crm <= 7; crm++) {
        for (unsigned half = 0; half < 8; half += 4) {
            struct buf b;

            b_init(&b);
            b_str(&b, "IDREG c");
            b_dec(&b, crm);
            b_str(&b, ".");
            b_dec(&b, half);
            for (unsigned o = half; o < half + 4; o++) {
                b_str(&b, " ");
                if ((g_idreg_known[crm] >> o) & 1)
                    b_hex(&b, idreg_read(crm, o));
                else
                    b_str(&b, "-");
            }
            b_flush(&b);
        }
    }
}

static void g3(void)
{
    struct buf d;
    bool ok = true;
    u64 pfr0 = SYSREG_RD(id_aa64pfr0_el1), isar1 = SYSREG_RD(id_aa64isar1_el1);
    u32 exc0 = g_exc;

    begin(WEL2_CHK_G3);
    b_init(&d);
    b_str(&d, "GIC field ");
    b_dec(&d, (pfr0 >> 24) & 15);
    if (((pfr0 >> 24) & 15) != 1) ok = false;            /* GICv3 system registers present */
    /* the ID group the layer may trap (HCR_EL2.TID3): all must read without a fault */
    u64 x = SYSREG_RD(id_aa64pfr1_el1) ^ SYSREG_RD(id_aa64dfr0_el1) ^ SYSREG_RD(id_aa64isar0_el1) ^
            SYSREG_RD(id_aa64mmfr0_el1) ^ SYSREG_RD(id_aa64mmfr1_el1) ^ SYSREG_RD(id_aa64mmfr2_el1) ^
            SYSREG_RD(midr_el1) ^ SYSREG_RD(id_aa64isar2_el1) ^ SYSREG_RD(id_aa64afr0_el1);
    (void)x;
    idreg_dump();
    if (g_exc != exc0) { ok = false; b_str(&d, " id-regs FAULTED"); }
    bool pa = ((isar1 >> 4) & 0xff) != 0 || ((isar1 >> 24) & 0xff) != 0;     /* APA/API or GPA/GPI */
    b_str(&d, " pauth ");
    if (pa && !g_have_info) {
        /* QEMU chain build before the layer announces PAuth: touching the keys would trap to an EL2 that does not
         * enable HCR_EL2.API/APK yet (fatal there), so the sign/authenticate part is not run. */
        b_str(&d, "implemented, sign/auth NOT RUN (no info block, layer PAuth unannounced)");
    } else if (pa && !(g_info.flags & WEL2_GINFO_F_PAUTH)) {
        b_str(&d, "implemented but the layer did not set API/APK (info flag PAUTH missing)");
        ok = false;
    } else if (pa) {
        u64 sgn;
        bool tamper;
        bool rt = pauth_test(&sgn, &tamper);

        b_str(&d, rt ? "sign+auth ok" : "sign/auth BAD (unsigned or mismatch)");
        b_str(&d, tamper ? " tamper detected" : " TAMPER NOT DETECTED");
        if (!rt || !tamper) ok = false;
        if (g_exc != exc0) { ok = false; b_str(&d, " faulted"); }
    } else {
        b_str(&d, "not implemented");
    }
    finish(WEL2_CHK_G3, "G3", ok, &d);
}

static void g4(void)
{
    struct buf d;
    bool ok = true;

    begin(WEL2_CHK_G4);
    b_init(&d);
    u32 typer = gic_rd32(g_gicd + 0x04), iidr = gic_rd32(g_gicd + 0x08);
    b_str(&d, "GICD_TYPER ");
    b_hex(&d, typer);
    b_str(&d, " IIDR ");
    b_hex(&d, iidr);
    gic_wr32(g_gicd + 0x00, (1u << 4) | (1u << 1));      /* ARE_NS | EnableGrp1NS */
    u32 ctlr = gic_rd32(g_gicd + 0x00);
    if ((ctlr & ((1u << 4) | (1u << 1))) != ((1u << 4) | (1u << 1))) { ok = false; b_str(&d, " CTLR-readback-BAD"); }
    unsigned n = gic_scan_cores();
    b_str(&d, " redistributors ");
    b_dec(&d, n);
    if (g_have_info && n != g_info.ncores) { ok = false; b_str(&d, "(info says "); b_dec(&d, g_info.ncores); b_str(&d, ")"); }
    int me = gic_core_of(mpidr_now());
    if (me < 0) { ok = false; b_str(&d, " own-MPIDR-not-found"); }
    u32 pidr2 = gic_rd32(g_gicr + 0xffe8);
    b_str(&d, " GICR_PIDR2.ArchRev ");
    b_dec(&d, (pidr2 >> 4) & 15);
    if (((pidr2 >> 4) & 15) != 3) ok = false;
    if (me >= 0) {
        bool w = gic_wake((unsigned)me);

        b_str(&d, w ? " wake ok" : " wake TIMEOUT");
        if (!w) ok = false;
    }
    finish(WEL2_CHK_G4, "G4", ok, &d);
}

static bool read_stats(void)
{
    g_stats_ok = (int64_t)hvc4(WEL2_STATS, (u64)(uintptr_t)&g_stats, sizeof(g_stats), 0) == 0;
    return g_stats_ok;
}

static u64 tall1_sum(const struct wel2_stats *s)
{
    return s->count[WEL2_STAT_ICC_IGRPEN1] + s->count[WEL2_STAT_ICC_BPR1];     /* ICC_SRE_EL1 is not a TALL1 register */
}
static u64 tc_sum(const struct wel2_stats *s)
{
    return s->count[WEL2_STAT_ICC_PMR] + s->count[WEL2_STAT_ICC_CTLR] + s->count[WEL2_STAT_ICC_RPR];
}

static void g5a(void)
{
    struct buf d;
    bool ok = true;
    struct wel2_stats before;

    begin(WEL2_CHK_G5A);
    b_init(&d);
    bool have = read_stats();
    before = g_stats;
    u64 sre = SYSREG_RD(icc_sre_el1);
    SYSREG_WR(icc_sre_el1, sre | 1);
    u64 sre2 = SYSREG_RD(icc_sre_el1);
    b_str(&d, "SRE ");
    b_hex(&d, sre2);
    if (!(sre2 & 1)) ok = false;
    SYSREG_WR(icc_igrpen1_el1, 1);
    u64 ig = SYSREG_RD(icc_igrpen1_el1);
    SYSREG_WR(icc_bpr1_el1, 7);
    u64 bp = SYSREG_RD(icc_bpr1_el1);
    b_str(&d, " IGRPEN1 ");
    b_dec(&d, ig);
    b_str(&d, " BPR1 ");
    b_dec(&d, bp);
    if (ig != 1 || bp != 7) { ok = false; b_str(&d, " readback-BAD"); }
    if (have && read_stats()) {
        u64 dn = tall1_sum(&g_stats) - tall1_sum(&before);
        bool trapping = g_stats.cpuif_mode != WEL2_CPUIF_HW;

        b_str(&d, " TALL1-traps +");
        b_dec(&d, dn);
        if (trapping && (g_stats.count[WEL2_STAT_ICC_IGRPEN1] == before.count[WEL2_STAT_ICC_IGRPEN1] ||
                         g_stats.count[WEL2_STAT_ICC_BPR1] == before.count[WEL2_STAT_ICC_BPR1])) {
            ok = false;
            b_str(&d, " TALL1 group (IGRPEN1/BPR1) did not trap");
        }
        if (trapping && g_stats.count[WEL2_STAT_ICC_SRE] != before.count[WEL2_STAT_ICC_SRE]) {
            ok = false;
            b_str(&d, " ICC_SRE_EL1 was trapped (TALL1 must not trap it)");
        }
    } else {
        b_str(&d, " counters n/a");
        if (g_have_info) {
            ok = false;
            b_str(&d, " (WEL2_STATS missing although the layer sent an info block)");
        }
    }
    finish(WEL2_CHK_G5A, "G5a", ok, &d);
}

static void g5b(void)
{
    struct buf d;
    bool ok = true;
    struct wel2_stats before;

    begin(WEL2_CHK_G5B);
    b_init(&d);
    bool have = read_stats();
    before = g_stats;
    SYSREG_WR(icc_pmr_el1, 0xf0);
    u64 pmr = SYSREG_RD(icc_pmr_el1);
    u64 ctlr = SYSREG_RD(icc_ctlr_el1);
    SYSREG_WR(icc_ctlr_el1, ctlr ^ 2);                   /* flip EOImode */
    u64 ctlr2 = SYSREG_RD(icc_ctlr_el1);
    SYSREG_WR(icc_ctlr_el1, ctlr);                       /* and back: the IRQ handler assumes the original */
    u64 ctlr3 = SYSREG_RD(icc_ctlr_el1);
    u64 rpr = SYSREG_RD(icc_rpr_el1);
    b_str(&d, "PMR ");
    b_hex(&d, pmr);
    b_str(&d, " CTLR ");
    b_hex(&d, ctlr);
    b_str(&d, " flipped ");
    b_hex(&d, ctlr2);
    b_str(&d, " RPR ");
    b_hex(&d, rpr);
    if (pmr != 0xf0) { ok = false; b_str(&d, " PMR-readback-BAD"); }
    if (((ctlr2 ^ ctlr) & 2) == 0 || ((ctlr3 ^ ctlr) & 2) != 0) { ok = false; b_str(&d, " CTLR.EOImode-BAD"); }
    if (have && read_stats()) {
        u64 dn = tc_sum(&g_stats) - tc_sum(&before);
        u32 mode = g_stats.cpuif_mode;

        b_str(&d, " mode ");
        b_str(&d, mode == WEL2_CPUIF_HW ? "hw" : mode == WEL2_CPUIF_TRAP ? "trap" : "trap-notc");
        b_str(&d, " TC-traps +");
        b_dec(&d, dn);
        if (mode == WEL2_CPUIF_TRAP && (g_stats.count[WEL2_STAT_ICC_PMR] == before.count[WEL2_STAT_ICC_PMR] ||
                                        g_stats.count[WEL2_STAT_ICC_CTLR] == before.count[WEL2_STAT_ICC_CTLR] ||
                                        g_stats.count[WEL2_STAT_ICC_RPR] == before.count[WEL2_STAT_ICC_RPR])) {
            ok = false;
            b_str(&d, " TC: expected traps, none counted (TC bit off?)");
        }
        if (mode != WEL2_CPUIF_TRAP && dn != 0) {
            ok = false;
            b_str(&d, " TC: counted traps although TC should be off");
        }
        if (g_have_info && g_info.cpuif_mode != mode) {
            ok = false;
            b_str(&d, " TC: layer reports a different mode than the info block");
        }
    } else {
        b_str(&d, " counters n/a");
        if (g_have_info) {
            ok = false;
            b_str(&d, " (WEL2_STATS missing although the layer sent an info block)");
        }
    }
    finish(WEL2_CHK_G5B, "G5b", ok, &d);
}

static void g6(void)
{
    struct buf d;
    bool ok = true;
    int me = gic_core_of(mpidr_now());

    begin(WEL2_CHK_G6);
    b_init(&d);
    if (me < 0) {
        b_str(&d, "no core index (G4 failed)");
        finish(WEL2_CHK_G6, "G6", false, &d);
        return;
    }
    gic_cpu_enable();
    gic_priv_enable((unsigned)me, 1);
    u64 spur = SYSREG_RD(icc_iar1_el1);                  /* nothing pending: must read 1023 */
    b_str(&d, "idle IAR ");
    b_dec(&d, spur & 0xffffff);
    if ((spur & 0xffffff) != 1023) ok = false;
    u32 c0 = g_irq_count[me][1];
    gic_send_sgi(1, mpidr_now());
    u64 t = wait_ge(&g_irq_count[me][1], c0 + 1, 500);
    if (t == ~0ull) {
        ok = false;
        b_str(&d, " SGI 1 not delivered within 500 ms");
    } else {
        b_str(&d, " SGI 1 taken, ack INTID ");
        b_dec(&d, g_irq_last);
        b_str(&d, " after ");
        b_dec(&d, t * 1000000 / g_frq);
        b_str(&d, " us");
        if (g_irq_last != 1) ok = false;
    }
    u64 rpr = SYSREG_RD(icc_rpr_el1);
    b_str(&d, " RPR after EOI ");
    b_hex(&d, rpr);
    if (rpr != 0xff) { ok = false; b_str(&d, " (expected idle 0xff)"); }
    finish(WEL2_CHK_G6, "G6", ok, &d);
}

static void timer_test(unsigned intid, unsigned chk, const char *name, bool soft)
{
    struct buf d;
    bool ok = true;
    int me = gic_core_of(mpidr_now());
    unsigned s = intid == 30;

    begin(chk);
    b_init(&d);
    if (me < 0 || !g_frq) {
        b_str(&d, "no core index or CNTFRQ=0");
        finish(chk, name, false, &d);
        return;
    }
    g_timer_stop[s] = 0;
    g_timer_ticks = g_frq / 100;                         /* 10 ms */
    gic_priv_enable((unsigned)me, intid);
    u32 c0 = g_irq_count[me][intid];
    timer_arm(intid);
    u64 t = wait_ge(&g_irq_count[me][intid], c0 + 5, 2000);
    g_timer_stop[s] = 1;
    irq_off();
    timer_off(intid);
    u32 got = g_irq_count[me][intid] - c0;
    b_str(&d, "INTID ");
    b_dec(&d, intid);
    b_str(&d, " ticks ");
    b_dec(&d, got);
    if ((t == ~0ull || got < 5) && soft && got == 0) {
        /* No info block = the QEMU chain build, whose layer injects only the virtual timer so far (the P gate is
         * a later step of PLAN S9): no INTID 30 at all is reported as SKIP, not as a guest failure. */
        skip(chk, name, "no INTID 30 delivered and no info block (layer exposes only PPI 27 so far)");
        g_chk_running = 0;
        return;
    }
    if (t == ~0ull || got < 5) {
        ok = false;
        b_str(&d, " TIMEOUT (5 expected in 2000 ms)");
    } else {
        b_str(&d, " in ");
        b_dec(&d, t * 1000 / g_frq);
        b_str(&d, " ms");
    }
    if (g_irq_other) { ok = false; b_str(&d, " other irqs seen"); }
    b_str(&g_key, "INTID ");
    b_dec(&g_key, intid);
    b_str(&g_key, " TICKS ");
    b_dec(&g_key, got);
    finish(chk, name, ok, &d);
}

static void g7(void) { timer_test(27, WEL2_CHK_G7, "G7", false); }

static void g8(void)
{
    if (g_have_info && (g_info.flags & WEL2_GINFO_F_NO_PTIMER)) {
        skip(WEL2_CHK_G8, "G8", "physical timer PPI 30 not exposed by the layer");
        return;
    }
    timer_test(30, WEL2_CHK_G8, "G8", !g_have_info);
}

/* Secondary core main (CPU_ON entry, C part). x0 = top of its stack. */
void secondary_main(u64 stack_top)
{
    int me = gic_core_of(mpidr_now());
    (void)stack_top;

    if (me < 0) {
        say("secondary: own MPIDR not found");
        for (;;)
            __asm__ volatile("wfe");
    }
    struct core_state *c = &g_core[me];
    struct buf b;

    c->round++;
    if (!gic_wake((unsigned)me))
        c->fail = 1;
    gic_cpu_enable();
    b_init(&b);
    b_str(&b, "cpu ");
    b_dec(&b, (u64)me);
    b_str(&b, " online round ");
    b_dec(&b, c->round);
    b_str(&b, " mpidr ");
    b_hex(&b, mpidr_now());
    b_str(&b, " EL");
    b_dec(&b, SYSREG_RD(CurrentEL) >> 2);
    b_flush(&b);
    gic_send_sgi(2, g_mpidr[g_boot_core]);                /* tell the boot core */
    c->up = 1;
    for (;;) {
        if (c->cmd == 1) {
            c->cmd = 0;
            c->up = 0;
            c->offs++;
            say("secondary: CPU_OFF");
            int64_t r = psci(PSCI_CPU_OFF, 0, 0, 0);     /* does not return on success */
            b_init(&b);
            b_str(&b, "secondary: CPU_OFF returned ");
            b_sdec(&b, r);
            b_flush(&b);
            c->fail = 1;
            c->up = 1;
        }
        __asm__ volatile("yield");
    }
}

static void g9(void)
{
    struct buf d;
    bool ok = true;
    int me = gic_core_of(mpidr_now());

    begin(WEL2_CHK_G9);
    b_init(&d);
    if (me < 0 || g_ncores < 2) {
        b_str(&d, g_ncores < 2 ? "only one core" : "no core index");
        finish(WEL2_CHK_G9, "G9", g_ncores < 2 && me >= 0, &d);
        return;
    }
    gic_priv_enable((unsigned)me, 2);                    /* SGI 2 = "I am up" from the secondaries */
    int64_t again = psci(PSCI_CPU_ON_64, mpidr_now(), secondary_entry_addr(), 0);
    b_str(&d, "CPU_ON(self)=");
    b_sdec(&d, again);
    if (again != PSCI_ALREADY_ON) ok = false;
    u32 sgi0 = g_irq_count[me][2];
    for (unsigned n = 0; n < g_ncores; n++) {
        if (n == (unsigned)me)
            continue;                                    /* every core except the one running this check */
        u64 top = (u64)(uintptr_t)__stacks + (u64)(n + 1) * STACK_SIZE;
        int64_t r = psci(PSCI_CPU_ON_64, g_mpidr[n], secondary_entry_addr(), top);

        if (r != 0) {
            ok = false;
            b_str(&d, " CPU_ON(");
            b_dec(&d, n);
            b_str(&d, ")=");
            b_sdec(&d, r);
            continue;
        }
        wait_ge(&g_core[n].up, 1, 5000);
        if (!g_core[n].up || g_core[n].fail) {
            ok = false;
            b_str(&d, " cpu ");
            b_dec(&d, n);
            b_str(&d, " did not report");
        }
    }
    u32 want = g_ncores - 1;
    if (wait_ge(&g_irq_count[me][2], sgi0 + want, 1000) == ~0ull) {
        ok = false;
        b_str(&d, " SGI 2 from secondaries: ");
        b_dec(&d, g_irq_count[me][2] - sgi0);
        b_str(&d, "/");
        b_dec(&d, want);
    } else {
        b_str(&d, " cores up ");
        b_dec(&d, want);
        b_str(&d, " SGI 2 received ");
        b_dec(&d, g_irq_count[me][2] - sgi0);
    }
    b_str(&g_key, "UP ");
    b_dec(&g_key, want);
    b_str(&g_key, " SGI2 ");
    b_dec(&g_key, g_irq_count[me][2] - sgi0);
    finish(WEL2_CHK_G9, "G9", ok, &d);
}

static void g10(void)
{
    struct buf d;
    bool ok = true;
    int me = gic_core_of(mpidr_now());

    begin(WEL2_CHK_G10);
    b_init(&d);
    if (g_ncores < 2 || me < 0) {
        b_str(&d, g_ncores < 2 ? "only one core" : "no core index");
        finish(WEL2_CHK_G10, "G10", g_ncores >= 2 ? false : true, &d);
        return;
    }
    unsigned n = me == 0 ? 1 : 0;                        /* a core other than the one running the checks */
    u32 round0 = g_core[n].round, offs0 = g_core[n].offs;
    g_core[n].cmd = 1;
    u64 end = cntvct() + ms_to_ticks(5000);
    int64_t st;
    do {
        st = psci(PSCI_AFFINITY_64, g_mpidr[n], 0, 0);
    } while (st != 1 && cntvct() < end);
    b_str(&d, "cpu ");
    b_dec(&d, n);
    b_str(&d, " AFFINITY_INFO=");
    b_sdec(&d, st);
    if (st != 1 || g_core[n].offs != offs0 + 1) {
        ok = false;
        b_str(&d, st != 1 ? " (never OFF)" : " (CPU_OFF was not issued by the core)");
        g_core[n].cmd = 0;                               /* no stale command for a later run */
    } else {
        u64 top = (u64)(uintptr_t)__stacks + (u64)(n + 1) * STACK_SIZE;

        g_core[n].cmd = 0;                               /* already consumed; make sure no stale one survives */
        g_core[n].up = 0;
        int64_t r = psci(PSCI_CPU_ON_64, g_mpidr[n], secondary_entry_addr(), top);

        b_str(&d, " CPU_ON again=");
        b_sdec(&d, r);
        if (r != 0) ok = false;
        wait_ge(&g_core[n].round, round0 + 1, 5000);
        wait_ge(&g_core[n].up, 1, 1000);
        if (!g_core[n].up || g_core[n].round < round0 + 1) {
            ok = false;
            b_str(&d, " did not come back");
        } else {
            b_str(&d, " back, round ");
            b_dec(&d, g_core[n].round);
        }
    }
    if (g_core[n].fail) { ok = false; b_str(&d, " core reported failure"); }
    finish(WEL2_CHK_G10, "G10", ok, &d);
}

/* T17: the last thing the guest prints (right after G11's verdict): one line per check, "Gn PASS|FAIL|SKIP <key>"
 * (key = tick count, MPIDR, counts...), then the MPIDR of every core, 4 per line. Short lines: with the layer's
 * "Watari EL1: " prefix they fit the 70 columns of the panel at scale 6. */
static void summary(void)
{
    struct buf b;
    unsigned cores = g_ncores < 8 ? g_ncores : 8;

    b_init(&b);
    b_str(&b, WEL2_SUMMARY_MARK " G1-G12 PASS ");
    b_dec(&b, g_pass);
    b_str(&b, " FAIL ");
    b_dec(&b, g_fail);
    b_flush(&b);
    for (unsigned i = 0; i < SUM_N; i++) {
        b_init(&b);
        b_str(&b, g_sum_name[i]);
        b_str(&b, g_sum[i].verdict == 'P' ? " PASS " : g_sum[i].verdict == 'F' ? " FAIL " :
                  g_sum[i].verdict == 'S' ? " SKIP " : " NOT RUN ");
        b_str(&b, g_sum[i].key);
        b_flush(&b);
    }
    for (unsigned n = 0; n < cores; n += 4) {
        b_init(&b);
        b_str(&b, "MPIDR");
        for (unsigned k = n; k < n + 4 && k < cores; k++) {
            b_str(&b, " ");
            b_hex(&b, g_mpidr[k]);
        }
        b_flush(&b);
    }
}

/* G12 (E9): the layer handed (part of) the panel over (info version 2, WEL2_GINFO_F_FB). Four vertical bars across the
 * whole guest part, each a quarter of the width: red, green, blue, yellow at full intensity in the layer's pixel format
 * (the layer's own palette has none of these four values, so the host can tell the guest's pixels from the layer's).
 * Written with the MMU off (Device memory) as aligned 32-bit stores, then read back at the first, middle and last row of
 * every bar. PASS = the read-back equals what was written. The host side (bench: QEMU monitor pmemsave of the fake panel)
 * checks the pixels from outside. */
static u32 g12_pix(unsigned format, unsigned bar)
{
    static const u32 p8[4] = { 0xff0000u, 0x00ff00u, 0x0000ffu, 0xffff00u };
    static const u32 p10[4] = { 0x3ff00000u, 0x000ffc00u, 0x000003ffu, 0x3ffffc00u };

    return format == WEL2_GFB_X2R10G10B10 ? p10[bar] : p8[bar];
}

static void g12(void)
{
    struct buf d;
    const struct wel2_guest_info *i = &g_info;
    volatile u32 *base;
    u32 w, h, stride, mask;
    bool ok = true;

    if (!g_have_info || !(i->flags & WEL2_GINFO_F_FB)) {
        skip(WEL2_CHK_G12, "G12", "no framebuffer handed over (wel2.fb=layer)");
        return;
    }
    begin(WEL2_CHK_G12);
    b_init(&d);
    w = i->fb_width;
    h = i->fb_height;
    stride = i->fb_stride;
    base = (volatile u32 *)(uintptr_t)i->fb_base;
    mask = i->fb_format == WEL2_GFB_X2R10G10B10 ? 0x3fffffffu : 0x00ffffffu;
    if ((i->fb_format != WEL2_GFB_X8R8G8B8 && i->fb_format != WEL2_GFB_X2R10G10B10) || w < 4 || !h || stride < 4 * w ||
        (stride & 3) || (i->fb_base & 3)) {
        b_str(&d, "bad framebuffer description");
        finish(WEL2_CHK_G12, "G12", false, &d);
        return;
    }
    for (u32 y = 0; y < h; y++) {
        volatile u32 *row = base + (u64)y * (stride / 4);

        for (u32 x = 0; x < w; x++)
            row[x] = g12_pix(i->fb_format, (u32)((u64)x * 4 / w));
    }
    __asm__ volatile("dsb sy" ::: "memory");
    for (unsigned bar = 0; bar < 4; bar++) {
        u32 x = (u32)(((u64)w * (2 * bar + 1)) / 8);
        u32 ys[3] = { 0, h / 2, h - 1 };

        for (unsigned k = 0; k < 3; k++) {
            u32 got = base[(u64)ys[k] * (stride / 4) + x] & mask;

            if (got != (g12_pix(i->fb_format, bar) & mask))
                ok = false;
        }
    }
    b_str(&d, i->fb_format == WEL2_GFB_X2R10G10B10 ? "x2r10g10b10 " : "x8r8g8b8 ");
    b_dec(&d, w);
    b_str(&d, "x");
    b_dec(&d, h);
    b_str(&d, i->fb_mode == WEL2_GFB_MODE_GUEST ? " guest" : " band ");
    if (i->fb_mode != WEL2_GFB_MODE_GUEST)
        b_dec(&d, i->fb_band_rows);
    b_str(&d, ok ? " bars RGBY read back" : " bars read back WRONG");
    b_init(&g_key);
    b_str(&g_key, i->fb_format == WEL2_GFB_X2R10G10B10 ? "X2R10G10B10 " : "X8R8G8B8 ");
    b_dec(&g_key, w);
    b_str(&g_key, "X");
    b_dec(&g_key, h);
    finish(WEL2_CHK_G12, "G12", ok, &d);
}

static void g11(void)
{
    struct buf d;
    bool off = g_have_info && (g_info.flags & WEL2_GINFO_F_POWEROFF);

    begin(WEL2_CHK_G11);
    b_init(&d);
    b_str(&d, "summary pass ");
    b_dec(&d, g_pass);
    b_str(&d, " fail ");
    b_dec(&d, g_fail);
    b_str(&d, " exceptions ");
    b_dec(&d, g_exc);
    b_str(&d, off ? " requesting SYSTEM_OFF" : " requesting SYSTEM_RESET");
    /* No return is expected, so the verdict is given before the call; the layer's own reset/off log line and the
     * reboot (or QEMU exiting) are the proof. */
    finish(WEL2_CHK_G11, "G11", g_exc == 0 && g_log_errs == 0, &d);
    summary();
    hvc4(off ? PSCI_SYSTEM_OFF : PSCI_SYSTEM_RESET, 0, 0, 0);
    checkpoint(WEL2_CHK_G11, WEL2_CHKSTATE_FAIL);        /* the call returned: the verdict above was wrong */
    say("G11 FAIL SYSTEM_RESET/OFF returned");
    for (;;)
        __asm__ volatile("wfe");
}

extern unsigned char _start[];

/* x0 is only dereferenced when it can be the layer's block: non-null, 8-byte aligned, and within 64 MB of this
 * image (the layer places the block next to the guest; QEMU's FDT hand-off buffer is 2.5 MB below it). Anything
 * else (e.g. an MMIO address) is never touched. */
static bool info_ptr_plausible(const struct wel2_guest_info *info)
{
    u64 p = (u64)(uintptr_t)info, base = (u64)(uintptr_t)_start, win = 64ull << 20;

    if (!p || (p & 7))
        return false;
    return p + sizeof(*info) > p && p + sizeof(*info) <= base + win && p + win >= base;
}

/* T12b/E7: one WEL2_CONSOLE_WRITE call, raw bytes with CR LF, to show the layer's byte stream sink (run-m1guest.sh looks for it). */
static void console_probe(void)
{
    static const char msg[] = "[WEL2_CONSOLE_WRITE] raw bytes from the EL1 test guest\r\n";
    int64_t r = (int64_t)hvc4(WEL2_CONSOLE_WRITE, (u64)(uintptr_t)msg, sizeof(msg) - 1, 0);

    if (r != 0 && r != WEL2_HVC_NOT_SUPPORTED)           /* NOT_SUPPORTED = an older layer: nothing to show */
        say("console write: unexpected result");
}

void guest_main(const struct wel2_guest_info *info)
{
    g_frq = SYSREG_RD(cntfrq_el0);
    g_is_apple = ((SYSREG_RD(midr_el1) >> 24) & 0xff) == 0x61;
    /* Version 1 (no framebuffer fields, size = offset of fb_base) and 2 are both understood; the layer's block may be shorter
     * than this struct (an older layer), then only what it has is copied and the framebuffer flag is cleared. */
    if (info_ptr_plausible(info) && info->magic == WEL2_GINFO_MAGIC &&
        (info->version == WEL2_GINFO_VERSION_1 || info->version == WEL2_GINFO_VERSION) &&
        info->size >= __builtin_offsetof(struct wel2_guest_info, fb_base)) {
        unsigned nw = (info->size < sizeof(g_info) ? info->size : sizeof(g_info)) / 8;

        for (unsigned i = 0; i < nw; i++)                      /* no memcpy: copy word by word */
            ((volatile u64 *)&g_info)[i] = ((const volatile u64 *)info)[i];
        if (info->version < 2 || info->size < sizeof(g_info))
            g_info.flags &= ~WEL2_GINFO_F_FB;
        g_info.platform[sizeof(g_info.platform) - 1] = 0;
        g_have_info = true;
        g_gicd = g_info.gicd_base;
        g_gicr = g_info.gicr_base;
        if (g_info.gicr_stride)
            g_gicr_stride = g_info.gicr_stride;
    }
    /* The PL011 / QEMU GIC addresses are used only on positive QEMU evidence: no info block and MIDR_EL1
     * implementer is not Apple (0x61). On an Apple core without an info block no MMIO is touched at all. */
    g_uart_fallback_ok = !g_have_info && !g_is_apple;

    say("Watari EL1 test guest: G1-G12");
    console_probe();
    if (!g_have_info && g_is_apple) {
        g1();                                            /* FAILs: no info block */
        g11();
    }
    if (selected(WEL2_CHK_G1)) g1();
    if (selected(WEL2_CHK_G2)) g2();
    if (selected(WEL2_CHK_G3)) g3();
    if (selected(WEL2_CHK_G4)) g4(); else gic_scan_cores();
    {
        int bc = gic_core_of(mpidr_now());

        g_boot_core = bc >= 0 ? (unsigned)bc : 0;
    }
    if (selected(WEL2_CHK_G5A)) g5a();
    if (selected(WEL2_CHK_G5B)) g5b();
    if (selected(WEL2_CHK_G6)) g6();
    if (selected(WEL2_CHK_G7)) g7();
    if (selected(WEL2_CHK_G8)) g8();
    if (selected(WEL2_CHK_G9)) g9();
    if (selected(WEL2_CHK_G10)) g10();
    if (selected(WEL2_CHK_G12)) g12();
    g11();
}
