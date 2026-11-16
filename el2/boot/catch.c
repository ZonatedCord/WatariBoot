// SPDX-License-Identifier: MIT
// Watari EL2: catch mode (wel2.catch=1), the hardware side. The decisions (what is ours, the value of a read, the table, the
// text) are vgic/sysreg_catch.c (host-tested); this file sets the trap bits of MDCR_EL2 on every core, receives the EC 0x18
// traps, answers them (reads RAZ, writes ignored) and logs the first catch of each distinct encoding.
// Why: bootmgr took an UNDEFINED exception at EL1 on the M1 and the layer saw nothing; the layer now sees every access to the
// registers a firmware or OS may probe (PMU, debug, IMPLEMENTATION DEFINED, ACTLR_EL1) and says which one it was.

#include "boot.h"
#include "wel2_hvc.h"
#include "wel2_sysreg_catch.h"
#include "wel2_apple_sysreg.h"

uint64_t g_hacr;                        // wel2.hacr mask ORed into HACR_EL2 on every core (M1: bit 56, main.c); 0 = never touched
static uint64_t g_hacr_rb[MAX_CPUS];    // HACR_EL2 read back per core after the write (0 = not programmed)
static uint64_t g_mdcr_stuck;           // MDCR_EL2 trap bits (of the mask) that read back 0 on some core
static struct wel2_sysreg_enc g_last;   // the last access caught, for the status row
static bool g_have_last;
static uint64_t g_status_t;             // CNTPCT of the last status redraw
static uint32_t g_ec3f_logged;          // raw EC 0x3F lines printed (the first WEL2_EC3F_LOG_MAX)
#define WEL2_EC3F_LOG_MAX 8u

uint32_t g_catch_mask;                  // wel2.catch group mask (WEL2_CATCH_G_*); default on the M1: PMU only (main.c)
bool g_catch;                           // wel2.catch=1|0; default on the M1, off on QEMU (main.c)
static struct wel2_catch_tab g_tab;     // under the big lock (el2_exception)
static struct wel2_catch_cpu g_ccpu[MAX_CPUS];   // per-core emulation state (OS lock; the PMU too when wel2.vpmu=percore), same lock
static struct wel2_catch_cpu g_gpmu;             // the ONE virtual PMU of wel2.vpmu=global (all cores), same lock
uint32_t g_vpmu = WEL2_VPMU_GLOBAL;              // wel2.vpmu flags (main.c parses the argument)

// PMUSERENR_EL0 exists only with FEAT_PMUv3: ID_AA64DFR0_EL1.PMUVer [11:8] is 0 (none) or 0xF (IMPLEMENTATION DEFINED PMU)
// otherwise [verified: Arm ARM D24.2 ID_AA64DFR0_EL1]. On a core without it (the M1 [hypothesis]) an EL2 write of it would be
// UNDEFINED, so the mirror below is skipped there.
static bool hw_has_pmuv3(void)
{
    uint64_t d;

    __asm__ volatile("mrs %0, id_aa64dfr0_el1" : "=r"(d));
    d = (d >> 8) & 0xf;
    return d != 0 && d != 0xf;
}

// Every core, before it enters the guest: MDCR_EL2 |= the trap bits. Only ORed in: HPMN and everything else keep the value
// the firmware left. With HCR_EL2.E2H = 1 an EL2 access to MDCR_EL2 is MDCR_EL2 itself (only the *_EL1 names are redirected).
// The read-back shows which bits stuck: a trap bit that is RES0 on this CPU (e.g. TPM without a PMU) reads back as 0 [hypothesis].
static void status_draw(void)
{
    char line[WEL2_CATCH_LINE_MAX];

    wel2_catch_status_format(line, sizeof(line), g_mdcr_stuck, g_hacr_rb[this_cpu() < MAX_CPUS ? this_cpu() : 0], g_tab.total,
                             g_have_last ? &g_last : 0);
    wel2_fb_catch_status(&g_fb, line);
    puts_("Watari EL2: catch status: ");
    puts_(line);
    puts_("\n");
}

// HACR_EL2 |= g_hacr on this core, read back (apple_sysreg: HACR_EL2 is S3_4_C1_C1_7). Only called on a platform that has it (the M1).
static void hacr_cpu_init(bool log)
{
    unsigned me = this_cpu() < MAX_CPUS ? this_cpu() : 0;
    uint64_t before = apple_sysreg_real.rd(0, APPLE_SR_HACR_EL2), after;

    apple_sysreg_real.wr(0, APPLE_SR_HACR_EL2, before | g_hacr);
    after = apple_sysreg_real.rd(0, APPLE_SR_HACR_EL2);
    g_hacr_rb[me] = after;
    if (log || (after & g_hacr) != g_hacr) {
        puts_("Watari EL2: hacr: before=");
        puthex(before);
        puts_(" after=");
        puthex(after);
        puts_(" requested=");
        puthex(g_hacr);
        put_kn(" core ", me, "\n");
    }
}

void catch_cpu_init(bool log)
{
    uint64_t before, after;

    if (g_hacr)
        hacr_cpu_init(log);
    if (!g_catch) {                                  // HACR-only (wel2.catch=0): no MDCR / HCR trap bits, the status row still shows
        if (log)
            status_draw();
        return;
    }
    g_gpmu.vflags = g_vpmu;
    for (unsigned i = 0; i < MAX_CPUS; i++)
        g_ccpu[i].vflags = g_vpmu;
    if ((g_vpmu & WEL2_VPMU_UALL) && !(g_vpmu & WEL2_VPMU_NOUSER) && (g_catch_mask & WEL2_CATCH_G_PMU) && hw_has_pmuv3())
        __asm__ volatile("msr pmuserenr_el0, %0" : : "r"(0xfull));
    __asm__ volatile("mrs %0, mdcr_el2" : "=r"(before));
    __asm__ volatile("msr mdcr_el2, %0; isb" : : "r"(before | wel2_catch_mdcr_bits(g_catch_mask)));
    __asm__ volatile("mrs %0, mdcr_el2" : "=r"(after));
    g_mdcr_stuck |= (before | wel2_catch_mdcr_bits(g_catch_mask)) & ~after & wel2_catch_mdcr_bits(g_catch_mask);
    if (log) {
        char gs[32];

        wel2_catch_groups_str(gs, sizeof(gs), g_catch_mask);
        puts_("Watari EL2: catch: groups = ");
        puts_(gs);
        puts_(" (mask ");
        puthex(g_catch_mask);
        puts_(")\n");
        put_kv("Watari EL2: catch: vPMU flags (1 global, 2 nouser, 4 frq1, 8 n6) = ", g_vpmu);
        put_kv("Watari EL2: catch: MDCR_EL2 before = ", before);
        put_kv("Watari EL2: catch: MDCR_EL2 after  = ", after);
        put_kv("Watari EL2: catch: MDCR_EL2 trap bits that did not stick = ", g_mdcr_stuck);
        status_draw();
    }
}

// The groups answered: the mask, plus the PMU group whenever HACR routes those accesses to EL2 (the MDCR trap bits are best effort:
// on the M1 they are RES0, and the accesses reach us through HACR_EL2[56] instead).
static uint32_t eff_mask(void)
{
    return (g_catch ? g_catch_mask : 0) | (g_hacr ? WEL2_CATCH_G_PMU : 0);
}

// One decoded access: true = answered (ELR advanced), false = not ours (nothing touched).
static bool catch_handle(struct frame *f, const struct wel2_sysreg_enc *ep)
{
    struct wel2_sysreg_enc e = *ep;
    struct wel2_catch_cpu *cc = &g_ccpu[this_cpu() < MAX_CPUS ? this_cpu() : 0];
    unsigned idx = 0;
    char line[WEL2_CATCH_LINE_MAX];

    if (!wel2_catch_enabled(eff_mask(), wel2_catch_classify(&e)))
        return false;
    if ((g_vpmu & WEL2_VPMU_GLOBAL) && wel2_catch_classify(&e) == WEL2_CATCH_PMU)
        cc = &g_gpmu;                                // one PMU for all cores: a migrating thread sees one monotonic counter
    switch (wel2_catch_record(&g_tab, &e, f->elr, this_cpu(), &idx)) {
    case WEL2_CATCH_REC_NEW:
        wel2_catch_format(line, sizeof(line), "CATCH", &e, f->elr, this_cpu(), 1);
        break;
    case WEL2_CATCH_REC_OVERFLOW:                   // the table is full: still say it, rate limited (WEL2_CATCH_OVF_LINES)
        wel2_catch_format_overflow(line, sizeof(line), &e, f->elr, this_cpu());
        break;
    case WEL2_CATCH_REC_OVERFLOW_CAP:
        {
            const char *m = WEL2_CATCH_OVF_NOTICE;
            unsigned k = 0;

            while (m[k] && k + 1 < sizeof(line)) {
                line[k] = m[k];
                k++;
            }
            line[k] = 0;
        }
        break;
    default:
        line[0] = 0;
        break;
    }
    if (line[0]) {
        puts_(line);
        puts_("\n");
        wel2_fb_catch_note(&g_fb, line);
    }
    {
        uint64_t cnt, frq;

        __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(cnt));    // the shared physical counter: the cycle counter is derived from it
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
        if (e.rd) {
            if (e.rt != 31)
                f->x[e.rt] = wel2_catch_read(cc, &e, cnt, frq);
        } else {
            wel2_catch_write(cc, &e, e.rt == 31 ? 0 : f->x[e.rt], cnt, frq);
        }
    }
    g_last = e;
    g_have_last = true;
    {
        uint64_t now, frq;

        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
        if (line[0] || now - g_status_t >= frq / 2) {      // a new encoding at once, the rest at most twice a second
            g_status_t = now;
            status_draw();
        }
    }
    // The hardware PMUSERENR_EL0 of this core follows the virtual one: with EN = 0 an EL0 read of PMCCNTR_EL0 is UNDEFINED at EL1
    // (the process dies) even though the guest enabled it; with EN = 1 it reaches us through MDCR_EL2.TPM and is answered above.
    if (wel2_catch_classify(&e) == WEL2_CATCH_PMU && !(g_vpmu & (WEL2_VPMU_NOUSER | WEL2_VPMU_UALL)) && hw_has_pmuv3()) {
        uint64_t u = cc->pmuserenr;

        __asm__ volatile("msr pmuserenr_el0, %0" : : "r"(u));
    }
    f->elr += 4;
    return true;
}

// An EC 0x18 trap from the guest: true = it was one of ours and is answered (ELR advanced).
bool catch_trap(struct frame *f)
{
    struct wel2_sysreg_enc e;

    if (!wel2_catch_decode(f->esr & WEL2_AFSR1_ISS, &e))
        return false;
    return catch_handle(f, &e);
}

// An EC 0x3F (IMPLEMENTATION DEFINED) exception from a lower EL: with HACR_EL2[56] set the M1 reports the PMUv3 accesses this way
// [hypothesis]; the MSR/MRS ISS is expected in AFSR1_EL2[24:0]. The raw ESR / AFSR1 / ELR of the first lines go to the log, so the
// hardware run says what the layout really is. false = not a catch: the caller's fatal path prints ESR, AFSR1 and ELR.
bool catch_trap_ec3f(struct frame *f)
{
    struct wel2_sysreg_enc e;
    uint64_t afsr1;
    enum wel2_ec3f_src src;

    if (!g_hacr)
        return false;
    __asm__ volatile("mrs %0, afsr1_el2" : "=r"(afsr1));
    src = wel2_catch_ec3f_decode(f->esr, afsr1, &e);
    if (g_ec3f_logged < WEL2_EC3F_LOG_MAX) {
        g_ec3f_logged++;
        puts_("Watari EL2: ec3f: ESR=");
        puthex(f->esr);
        puts_(" AFSR1=");
        puthex(afsr1);
        puts_(" ELR=");
        puthex(f->elr);
        puts_(src == WEL2_EC3F_AFSR1 ? " iss=afsr1" : src == WEL2_EC3F_ESR ? " iss=esr" : " iss=none");
        put_kn(" core ", this_cpu(), "\n");
    }
    if (src == WEL2_EC3F_NONE)
        return false;
    return catch_handle(f, &e);
}

// The final statistics (cpuif_stats): the summary and one line per distinct encoding with its total count.
void catch_stats(void)
{
    char line[WEL2_CATCH_LINE_MAX];

    if (!g_catch && !g_hacr)
        return;
    if (g_hacr) {
        for (unsigned i = 0; i < MAX_CPUS; i++)
            if (g_hacr_rb[i]) {
                puts_("Watari EL2: stats: HACR_EL2 read back, core ");
                puthex(i);
                put_kv(" = ", g_hacr_rb[i]);
            }
        g_status_t = 0;
        status_draw();
    }
    wel2_catch_format_summary(line, sizeof(line), &g_tab);
    puts_("Watari EL2: stats: ");
    puts_(line);
    puts_("\n");
    for (unsigned i = 0; i < g_tab.n; i++) {
        const struct wel2_catch_ent *t = &g_tab.e[i];

        wel2_catch_format(line, sizeof(line), "CATCH TOTAL", &t->enc, t->first_pc, t->core, t->count);
        puts_("Watari EL2: stats: ");
        puts_(line);
        puts_("\n");
    }
}

void catch_stats_fill(struct wel2_stats *out)
{
    out->catch_distinct = g_tab.n;
    out->catch_total = g_tab.total;
    out->catch_overflow = g_tab.overflow;
}
