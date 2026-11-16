// SPDX-License-Identifier: MIT
// Watari EL2: catch mode (wel2.catch=1), the hardware side. The decisions (what is ours, the value of a read, the table, the
// text) are vgic/sysreg_catch.c (host-tested); this file sets the trap bits of MDCR_EL2 on every core, receives the EC 0x18
// traps, answers them (reads RAZ, writes ignored) and logs the first catch of each distinct encoding.
// Why: bootmgr took an UNDEFINED exception at EL1 on the M1 and the layer saw nothing; the layer now sees every access to the
// registers a firmware or OS may probe (PMU, debug, IMPLEMENTATION DEFINED, ACTLR_EL1) and says which one it was.

#include "boot.h"
#include "wel2_hvc.h"
#include "wel2_sysreg_catch.h"

bool g_catch;                           // wel2.catch=1|0; default on the M1, off on QEMU (main.c)
static struct wel2_catch_tab g_tab;     // under the big lock (el2_exception)

// Every core, before it enters the guest: MDCR_EL2 |= the trap bits. Only ORed in: HPMN and everything else keep the value
// the firmware left. With HCR_EL2.E2H = 1 an EL2 access to MDCR_EL2 is MDCR_EL2 itself (only the *_EL1 names are redirected).
// The read-back shows which bits stuck: a trap bit that is RES0 on this CPU (e.g. TPM without a PMU) reads back as 0 [hypothesis].
void catch_cpu_init(bool log)
{
    uint64_t before, after;

    __asm__ volatile("mrs %0, mdcr_el2" : "=r"(before));
    __asm__ volatile("msr mdcr_el2, %0; isb" : : "r"(before | WEL2_MDCR_CATCH));
    __asm__ volatile("mrs %0, mdcr_el2" : "=r"(after));
    if (log) {
        put_kv("Watari EL2: catch: MDCR_EL2 before = ", before);
        put_kv("Watari EL2: catch: MDCR_EL2 after  = ", after);
        put_kv("Watari EL2: catch: MDCR_EL2 trap bits that did not stick = ", (before | WEL2_MDCR_CATCH) & ~after & WEL2_MDCR_CATCH);
    }
}

// An EC 0x18 trap from the guest: true = it was one of ours and is answered (ELR advanced).
bool catch_trap(struct frame *f)
{
    struct wel2_sysreg_enc e;
    unsigned idx = 0;

    if (!g_catch || !wel2_catch_decode(f->esr & 0x1ffffff, &e) || wel2_catch_classify(&e) == WEL2_CATCH_NONE)
        return false;
    if (wel2_catch_record(&g_tab, &e, f->elr, this_cpu(), &idx) == WEL2_CATCH_REC_NEW) {
        char line[WEL2_CATCH_LINE_MAX];

        wel2_catch_format(line, sizeof(line), "CATCH", &e, f->elr, this_cpu(), 1);
        puts_(line);
        puts_("\n");
        wel2_fb_catch_note(&g_fb, line);
    }
    if (e.rd && e.rt != 31)
        f->x[e.rt] = wel2_catch_value(&e);
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
