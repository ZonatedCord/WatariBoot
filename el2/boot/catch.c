// SPDX-License-Identifier: MIT
// Watari EL2: catch mode (wel2.catch=1), the hardware side. The decisions (what is ours, the value of a read, the table, the
// text) are vgic/sysreg_catch.c (host-tested); this file sets the trap bits of MDCR_EL2 on every core, receives the EC 0x18
// traps, answers them (reads RAZ, writes ignored) and logs the first catch of each distinct encoding.
// Why: bootmgr took an UNDEFINED exception at EL1 on the M1 and the layer saw nothing; the layer now sees every access to the
// registers a firmware or OS may probe (PMU, debug, IMPLEMENTATION DEFINED, ACTLR_EL1) and says which one it was.

#include "boot.h"
#include "wel2_hvc.h"
#include "wel2_sysreg_catch.h"

uint32_t g_catch_mask;                  // wel2.catch group mask (WEL2_CATCH_G_*); default on the M1: PMU only (main.c)
bool g_catch;                           // wel2.catch=1|0; default on the M1, off on QEMU (main.c)
static struct wel2_catch_tab g_tab;     // under the big lock (el2_exception)
static struct wel2_catch_cpu g_ccpu[MAX_CPUS];   // per-core emulation state (OS lock; the PMU too when wel2.vpmu=percore), same lock
static struct wel2_catch_cpu g_gpmu;             // the ONE virtual PMU of wel2.vpmu=global (all cores), same lock
uint32_t g_vpmu = WEL2_VPMU_GLOBAL;              // wel2.vpmu flags (main.c parses the argument)

// Every core, before it enters the guest: MDCR_EL2 |= the trap bits. Only ORed in: HPMN and everything else keep the value
// the firmware left. With HCR_EL2.E2H = 1 an EL2 access to MDCR_EL2 is MDCR_EL2 itself (only the *_EL1 names are redirected).
// The read-back shows which bits stuck: a trap bit that is RES0 on this CPU (e.g. TPM without a PMU) reads back as 0 [hypothesis].
void catch_cpu_init(bool log)
{
    uint64_t before, after;

    g_gpmu.vflags = g_vpmu;
    for (unsigned i = 0; i < MAX_CPUS; i++)
        g_ccpu[i].vflags = g_vpmu;
    if ((g_vpmu & WEL2_VPMU_UALL) && !(g_vpmu & WEL2_VPMU_NOUSER) && (g_catch_mask & WEL2_CATCH_G_PMU))
        __asm__ volatile("msr pmuserenr_el0, %0" : : "r"(0xfull));
    __asm__ volatile("mrs %0, mdcr_el2" : "=r"(before));
    __asm__ volatile("msr mdcr_el2, %0; isb" : : "r"(before | wel2_catch_mdcr_bits(g_catch_mask)));
    __asm__ volatile("mrs %0, mdcr_el2" : "=r"(after));
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
        put_kv("Watari EL2: catch: MDCR_EL2 trap bits that did not stick = ", (before | wel2_catch_mdcr_bits(g_catch_mask)) & ~after & wel2_catch_mdcr_bits(g_catch_mask));
    }
}

// An EC 0x18 trap from the guest: true = it was one of ours and is answered (ELR advanced).
bool catch_trap(struct frame *f)
{
    struct wel2_sysreg_enc e;
    struct wel2_catch_cpu *cc = &g_ccpu[this_cpu() < MAX_CPUS ? this_cpu() : 0];
    unsigned idx = 0;
    char line[WEL2_CATCH_LINE_MAX];

    if (!g_catch || !wel2_catch_decode(f->esr & 0x1ffffff, &e) || !wel2_catch_enabled(g_catch_mask, wel2_catch_classify(&e)))
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
    // The hardware PMUSERENR_EL0 of this core follows the virtual one: with EN = 0 an EL0 read of PMCCNTR_EL0 is UNDEFINED at EL1
    // (the process dies) even though the guest enabled it; with EN = 1 it reaches us through MDCR_EL2.TPM and is answered above.
    if (wel2_catch_classify(&e) == WEL2_CATCH_PMU && !(g_vpmu & (WEL2_VPMU_NOUSER | WEL2_VPMU_UALL))) {
        uint64_t u = cc->pmuserenr;

        __asm__ volatile("msr pmuserenr_el0, %0" : : "r"(u));
    }
    f->elr += 4;
    return true;
}

// The final statistics (cpuif_stats): the summary and one line per distinct encoding with its total count.
void catch_stats(void)
{
    char line[WEL2_CATCH_LINE_MAX];

    if (!g_catch)
        return;
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
