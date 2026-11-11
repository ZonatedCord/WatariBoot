/* SPDX-License-Identifier: MIT */
/* Host tests: vendor hypercalls (wel2_hvc_vendor.h) against a fake guest RAM and recording sinks. */
#include <stdbool.h>
#include <string.h>
#include "test.h"
#include "wel2_hvc_vendor.h"

#define RAM_BASE 0x40000000ull
#define RAM_SIZE 0x1000ull
#define HOLE_LO  (RAM_BASE + 0x800)     /* a hole (the layer's carve-out) inside the fake RAM */
#define HOLE_HI  (RAM_BASE + 0x900)

static uint8_t g_ram[RAM_SIZE];
static char g_lines[8][WEL2_LOG_MAX + 1];
static unsigned g_nlines, g_ncp, g_cp_n, g_cp_state, g_stats_calls, g_reads, g_writes;
static bool g_have_stats = true;

static bool f_range_ok(void *c, uint64_t pa, uint64_t len)
{
    (void)c;
    if (pa < RAM_BASE || pa + len < pa || pa + len > RAM_BASE + RAM_SIZE)
        return false;
    return pa + len <= HOLE_LO || pa >= HOLE_HI;
}
static void f_read(void *c, uint64_t pa, void *dst, uint64_t len)
{
    (void)c; g_reads++;
    memcpy(dst, &g_ram[pa - RAM_BASE], len);
}
static void f_write(void *c, uint64_t pa, const void *src, uint64_t len)
{
    (void)c; g_writes++;
    memcpy(&g_ram[pa - RAM_BASE], src, len);
}
static void f_log(void *c, const char *l, unsigned n)
{
    (void)c;
    if (g_nlines < 8) {
        memcpy(g_lines[g_nlines], l, n);
        g_lines[g_nlines][n] = 0;
    }
    g_nlines++;
}
static void f_cp(void *c, unsigned n, unsigned st) { (void)c; g_ncp++; g_cp_n = n; g_cp_state = st; }
static void f_stats(void *c, struct wel2_stats *o)
{
    (void)c; g_stats_calls++;
    o->cpuif_mode = WEL2_CPUIF_TRAP;
    o->count[WEL2_STAT_ICC_PMR] = 42;
    o->count[WEL2_STAT_ICC_SRE] = 7;
}

static struct wel2_hvc_ops ops(void)
{
    struct wel2_hvc_ops o = { 0 };
    o.range_ok = f_range_ok; o.read_guest = f_read; o.write_guest = f_write;
    o.log_line = f_log; o.checkpoint = f_cp;
    o.stats_fill = g_have_stats ? f_stats : NULL;
    return o;
}

static void reset(void)
{
    memset(g_ram, 0, sizeof(g_ram));
    g_nlines = g_ncp = g_stats_calls = g_reads = g_writes = 0;
    g_have_stats = true;
}

static int64_t call(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3, bool *handled)
{
    struct wel2_hvc_ops o = ops();
    uint64_t x[4] = { fid, a1, a2, a3 };

    *handled = wel2_hvc_vendor(&o, x);
    if (*handled)
        CHECK(x[1] == 0 && x[2] == 0 && x[3] == 0);     /* SMCCC: unused results are zero */
    return (int64_t)x[0];
}
static int64_t callf(uint64_t fid, uint64_t a1, uint64_t a2)
{
    bool h;
    int64_t r = call(fid, a1, a2, 0, &h);
    CHECK(h);
    return r;
}
static void put_text(uint64_t pa, const char *s) { memcpy(&g_ram[pa - RAM_BASE], s, strlen(s) + 1); }

static void t_range_of_ids(void)
{
    bool h;

    CHECK(wel2_hvc_is_vendor(WEL2_VERSION));
    CHECK(wel2_hvc_is_vendor(WEL2_LOG));
    CHECK(wel2_hvc_is_vendor(0xC600FEFFu));
    CHECK(!wel2_hvc_is_vendor(0xC600FF00u));            /* SMCCC general-query range */
    CHECK(!wel2_hvc_is_vendor(0xC5FFFFFFu));
    CHECK(!wel2_hvc_is_vendor(0xC4000003u));            /* PSCI CPU_ON */
    CHECK(!wel2_hvc_is_vendor(0x84000000u));
    CHECK(wel2_hvc_is_vendor(0xFFFFFFFF00000000ull | WEL2_LOG));   /* only the low 32 bits count */
    /* not ours: untouched and reported as not handled */
    struct wel2_hvc_ops o = ops();
    uint64_t x[4] = { 0xC4000003u, 1, 2, 3 };
    CHECK(!wel2_hvc_vendor(&o, x));
    CHECK_EQ(x[0], 0xC4000003u); CHECK_EQ(x[1], 1); CHECK_EQ(x[3], 3);
    /* in the range, not implemented */
    reset();
    CHECK_EQ(call(0xC6000004u, 0, 0, 0, &h), WEL2_HVC_NOT_SUPPORTED); CHECK(h);
    CHECK_EQ(call(0xC600FEFFu, 0, 0, 0, &h), WEL2_HVC_NOT_SUPPORTED); CHECK(h);
    CHECK_EQ(g_nlines, 0);
}

static void t_version(void)
{
    reset();
    CHECK_EQ(callf(WEL2_VERSION, 0, 0), (0x57454C32ull << 32) | WEL2_HVC_ABI_VERSION);
}

static void t_log_ok(void)
{
    reset();
    put_text(RAM_BASE + 0x100, "G1 PASS hello");
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x100, 32), 0);
    CHECK_EQ(g_nlines, 1);
    CHECK(!strcmp(g_lines[0], "G1 PASS hello"));          /* stops at the NUL, not at the length */
    /* exact length, no NUL inside the range */
    memcpy(&g_ram[0x200], "ABCDE", 5);
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x200, 5), 0);
    CHECK(!strcmp(g_lines[1], "ABCDE"));
    /* maximum length accepted */
    memset(&g_ram[0x300], 'x', WEL2_LOG_MAX);
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x300, WEL2_LOG_MAX), 0);
    CHECK_EQ(strlen(g_lines[2]), WEL2_LOG_MAX);
}

static void t_log_sanitised(void)
{
    reset();
    put_text(RAM_BASE + 0x100, "line\r\n");
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x100, 16), 0);
    CHECK(!strcmp(g_lines[0], "line"));                   /* trailing CR/LF dropped, the sink adds its own */
    g_ram[0x180] = 'a'; g_ram[0x181] = 0x1b; g_ram[0x182] = (uint8_t)0xe9; g_ram[0x183] = 'b'; g_ram[0x184] = 0;
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x180, 8), 0);
    CHECK(!strcmp(g_lines[1], "a??b"));                   /* control and non-ASCII bytes become '?' */
    g_ram[0x1c0] = 0;
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x1c0, 4), 0);    /* empty text: an empty line, still a success */
    CHECK_EQ(strlen(g_lines[2]), 0);
}

static void t_log_rejects(void)
{
    reset();
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x100, 0), WEL2_HVC_INVALID_PARAMETERS);               /* zero length */
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x100, WEL2_LOG_MAX + 1), WEL2_HVC_INVALID_PARAMETERS); /* too long */
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + 0x100, ~0ull), WEL2_HVC_INVALID_PARAMETERS);
    CHECK_EQ(callf(WEL2_LOG, 0x1000, 16), WEL2_HVC_INVALID_PARAMETERS);                        /* below RAM */
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + RAM_SIZE, 16), WEL2_HVC_INVALID_PARAMETERS);           /* above RAM */
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE + RAM_SIZE - 8, 16), WEL2_HVC_INVALID_PARAMETERS);       /* straddles the end */
    CHECK_EQ(callf(WEL2_LOG, RAM_BASE - 8, 16), WEL2_HVC_INVALID_PARAMETERS);                  /* straddles the start */
    CHECK_EQ(callf(WEL2_LOG, HOLE_LO + 4, 16), WEL2_HVC_INVALID_PARAMETERS);                   /* inside the carve-out */
    CHECK_EQ(callf(WEL2_LOG, HOLE_LO - 8, 16), WEL2_HVC_INVALID_PARAMETERS);                   /* straddles a hole */
    CHECK_EQ(callf(WEL2_LOG, ~0ull - 4, 16), WEL2_HVC_INVALID_PARAMETERS);                     /* address wraps */
    CHECK_EQ(g_nlines, 0);
    CHECK_EQ(g_reads, 0);                                  /* nothing was read from rejected addresses */
}

static void t_checkpoint(void)
{
    reset();
    CHECK_EQ(callf(WEL2_CHECKPOINT, WEL2_CHK_G6, WEL2_CHKSTATE_PASS), 0);
    CHECK_EQ(g_ncp, 1); CHECK_EQ(g_cp_n, WEL2_CHK_G6); CHECK_EQ(g_cp_state, WEL2_CHKSTATE_PASS);
    CHECK_EQ(g_nlines, 1);
    CHECK(!strcmp(g_lines[0], "guest checkpoint 7 PASS"));
    CHECK_EQ(callf(WEL2_CHECKPOINT, WEL2_CHK_G1, WEL2_CHKSTATE_RUNNING), 0);
    CHECK(!strcmp(g_lines[1], "guest checkpoint 1 running"));
    CHECK_EQ(callf(WEL2_CHECKPOINT, WEL2_CHK_MAX, WEL2_CHKSTATE_FAIL), 0);
    CHECK(!strcmp(g_lines[2], "guest checkpoint 47 FAIL"));
    CHECK_EQ(g_cp_state, WEL2_CHKSTATE_FAIL);
    unsigned before = g_ncp;
    CHECK_EQ(callf(WEL2_CHECKPOINT, 0, 1), WEL2_HVC_INVALID_PARAMETERS);
    CHECK_EQ(callf(WEL2_CHECKPOINT, WEL2_CHK_MAX + 1, 1), WEL2_HVC_INVALID_PARAMETERS);
    CHECK_EQ(callf(WEL2_CHECKPOINT, 1, 3), WEL2_HVC_INVALID_PARAMETERS);
    CHECK_EQ(callf(WEL2_CHECKPOINT, 1ull << 32 | 1, 1), WEL2_HVC_INVALID_PARAMETERS);
    CHECK_EQ(g_ncp, before);                               /* rejected calls paint nothing */
    CHECK_EQ(g_nlines, 3);
}

static void t_stats(void)
{
    struct wel2_stats st;

    reset();
    CHECK_EQ(callf(WEL2_STATS, RAM_BASE + 0x400, sizeof(st)), 0);
    memcpy(&st, &g_ram[0x400], sizeof(st));
    CHECK_EQ(st.version, WEL2_HVC_ABI_VERSION);
    CHECK_EQ(st.cpuif_mode, WEL2_CPUIF_TRAP);
    CHECK_EQ(st.count[WEL2_STAT_ICC_PMR], 42);
    CHECK_EQ(st.count[WEL2_STAT_ICC_SRE], 7);
    CHECK_EQ(st.count[WEL2_STAT_ICC_DIR], 0);
    CHECK_EQ(g_stats_calls, 1);
    CHECK_EQ(callf(WEL2_STATS, RAM_BASE + 0x400, sizeof(st) - 8), WEL2_HVC_INVALID_PARAMETERS);   /* buffer too small */
    CHECK_EQ(callf(WEL2_STATS, RAM_BASE + 0x404, sizeof(st)), WEL2_HVC_INVALID_PARAMETERS);       /* misaligned */
    CHECK_EQ(callf(WEL2_STATS, RAM_BASE + RAM_SIZE - 16, sizeof(st)), WEL2_HVC_INVALID_PARAMETERS); /* end of RAM */
    CHECK_EQ(callf(WEL2_STATS, HOLE_LO, sizeof(st)), WEL2_HVC_INVALID_PARAMETERS);
    CHECK_EQ(g_stats_calls, 1);
    CHECK_EQ(g_writes, 1);
    g_have_stats = false;
    CHECK_EQ(callf(WEL2_STATS, RAM_BASE + 0x400, sizeof(st)), WEL2_HVC_NOT_SUPPORTED);   /* no counters wired */
}

static void t_missing_sinks(void)
{
    struct wel2_hvc_ops o = { 0 };
    uint64_t x[4] = { WEL2_LOG, RAM_BASE, 8, 0 };

    reset();
    CHECK(wel2_hvc_vendor(&o, x));                         /* no callbacks at all: NOT_SUPPORTED, no crash */
    CHECK_EQ(x[0], (uint64_t)WEL2_HVC_NOT_SUPPORTED);
    uint64_t y[4] = { WEL2_CHECKPOINT, 1, 1, 0 };
    CHECK(wel2_hvc_vendor(&o, y));
    CHECK_EQ(y[0], (uint64_t)WEL2_HVC_NOT_SUPPORTED);
    o.log_line = f_log;                                    /* checkpoint callback is optional */
    uint64_t z[4] = { WEL2_CHECKPOINT, 1, 1, 0 };
    CHECK(wel2_hvc_vendor(&o, z));
    CHECK_EQ(z[0], 0);
    CHECK_EQ(g_nlines, 1);
}

int main(void)
{
    RUN(t_range_of_ids);
    RUN(t_version);
    RUN(t_log_ok);
    RUN(t_log_sanitised);
    RUN(t_log_rejects);
    RUN(t_checkpoint);
    RUN(t_stats);
    RUN(t_missing_sinks);
    TESTS_DONE();
}
