/* SPDX-License-Identifier: MIT */
/* M1 PSCI back end: operation order, state machine, park/wake, watchdog (fake memory and events). */
#include <string.h>
#include "test.h"
#include "wel2_cpu_start.h"

enum { O_W64 = 1, O_CVAC, O_DSB, O_SEV, O_WFE, O_WSET, O_TIMERS, O_MMIO, O_LOG };
struct rec { int op; uint64_t a, b; };
static struct rec log_[256];
static unsigned nlog;
static uint64_t mem[0x1000 / 8];       /* "physical" 0x1000.. */
static uint32_t wake[WEL2_CS_MAX];
static unsigned wfe_until_wake, wfe_cnt;
static unsigned wake_idx_to_set;

static void rec(int op, uint64_t a, uint64_t b) { if (nlog < 256) log_[nlog++] = (struct rec){ op, a, b }; }
static void f_w64(void *c, uint64_t pa, uint64_t v) { (void)c; rec(O_W64, pa, v); mem[(pa - 0x1000) / 8] = v; }
static void f_cvac(void *c, uint64_t pa) { (void)c; rec(O_CVAC, pa, 0); }
static void f_dsb(void *c) { (void)c; rec(O_DSB, 0, 0); }
static void f_sev(void *c) { (void)c; rec(O_SEV, 0, 0); }
static void f_wfe(void *c)
{
	(void)c; rec(O_WFE, 0, 0);
	if (wfe_until_wake && ++wfe_cnt == wfe_until_wake)
		wake[wake_idx_to_set] = 1;   /* "another core" sets the flag during the wait */
}
static void f_wset(void *c, unsigned i, uint32_t v) { (void)c; rec(O_WSET, i, v); wake[i] = v; }
static uint32_t f_wget(void *c, unsigned i) { (void)c; return wake[i]; }
static void f_timers(void *c) { (void)c; rec(O_TIMERS, 0, 0); }
static void f_mmio(void *c, uint64_t pa, uint32_t v) { (void)c; rec(O_MMIO, pa, v); }

static void f_log(void *c, int ev, unsigned i, uint64_t a) { (void)c; rec(O_LOG, ((uint64_t)ev << 8) | i, a); }

static const struct wel2_cs_ops OPS = { f_w64, f_cvac, f_dsb, f_sev, f_wfe, f_wset, f_wget, f_timers, f_mmio, f_log, NULL };
static const uint64_t SECENTRY = 0x900000234000ull;
static const struct wel2_cs_cpu M1CPUS[8] = {
	{ 0x80000000, 0x1000 }, { 0x80000001, 0x1008 }, { 0x80000002, 0x1010 }, { 0x80000003, 0x1018 },
	{ 0x80010100, 0x1040 }, { 0x80010101, 0x1048 }, { 0x80010102, 0x1050 }, { 0x80010103, 0 /* none */ },
};
static struct wel2_cs CS;

static void fresh(unsigned n)
{
	nlog = 0; wfe_until_wake = wfe_cnt = 0;
	memset(mem, 0, sizeof mem); memset(wake, 0, sizeof wake);
	CHECK_EQ(wel2_cs_init(&CS, &OPS, SECENTRY, M1CPUS, n, 0), 0);
}

static void init_checks(void)
{
	struct wel2_cs c;
	CHECK_EQ(wel2_cs_init(&c, &OPS, SECENTRY, M1CPUS, 0, 0), -1);
	CHECK_EQ(wel2_cs_init(&c, &OPS, SECENTRY, M1CPUS, 9, 0), -1);
	CHECK_EQ(wel2_cs_init(&c, &OPS, SECENTRY, M1CPUS, 4, 4), -1);
	CHECK_EQ(wel2_cs_init(&c, &OPS, 0, M1CPUS, 4, 0), -1);
	CHECK_EQ(wel2_cs_init(&c, NULL, SECENTRY, M1CPUS, 4, 0), -1);
	struct wel2_cs_ops bad = OPS; bad.sev = NULL;
	CHECK_EQ(wel2_cs_init(&c, &bad, SECENTRY, M1CPUS, 4, 0), -1);
	bad = OPS; bad.log = NULL;
	CHECK_EQ(wel2_cs_init(&c, &bad, SECENTRY, M1CPUS, 4, 0), -1);
	struct wel2_cs_cpu dup[2] = { { 0x80000000, 0x1000 }, { 0x80000000, 0x1008 } };
	CHECK_EQ(wel2_cs_init(&c, &OPS, SECENTRY, dup, 2, 0), -1);
	fresh(8);
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00000000), WEL2_CS_AFF_ON);
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00000001), WEL2_CS_AFF_OFF);
	CHECK_EQ(wel2_cs_find(&CS, 0x00010101ull), 5);
}

/* store -> clean -> dsb -> sev, exactly, and the stored value is the run-time entry */
static void first_start_order(void)
{
	uint64_t e, x;
	fresh(8);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00010101, 0x40200000, 77), WEL2_CS_SUCCESS);
	CHECK_EQ(nlog, 5);
	CHECK_EQ(log_[4].op, O_LOG); CHECK_EQ(log_[4].a, (WEL2_CS_EV_ON_ACCEPT << 8) | 5); CHECK_EQ(log_[4].b, 0);
	CHECK_EQ(log_[0].op, O_W64); CHECK_EQ(log_[0].a, 0x1048); CHECK_EQ(log_[0].b, SECENTRY);
	CHECK_EQ(log_[1].op, O_CVAC); CHECK_EQ(log_[1].a, 0x1048);
	CHECK_EQ(log_[2].op, O_DSB);
	CHECK_EQ(log_[3].op, O_SEV);
	CHECK_EQ(mem[(0x1048 - 0x1000) / 8], SECENTRY);
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00010101), WEL2_CS_AFF_PENDING);
	nlog = 0;
	CHECK(wel2_cs_enter(&CS, 5, &e, &x));
	CHECK_EQ(nlog, 1);                           /* arrival is logged */
	CHECK_EQ(log_[0].op, O_LOG); CHECK_EQ(log_[0].a, (WEL2_CS_EV_ARRIVE << 8) | 5); CHECK_EQ(log_[0].b, 0x40200000);
	CHECK_EQ(e, 0x40200000); CHECK_EQ(x, 77);
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00010101), WEL2_CS_AFF_ON);
	nlog = 0;
	CHECK(!wel2_cs_enter(&CS, 5, &e, &x));       /* not PENDING any more: no log, caller must not eret */
	CHECK_EQ(nlog, 0);
	CHECK(!wel2_cs_enter(&CS, 7, &e, &x));       /* never started */
}

static void refusals(void)
{
	struct wel2_cs c;
	struct wel2_cs_cpu odd[2] = { { 0x80000000, 0x1000 }, { 0x80000001, 0x1004 } };

	fresh(8);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x80000001, 0x40000000, 0), WEL2_CS_INVALID_PARAMETERS);      /* RES1 bit 31: callers mask it (DEN0022F 5.1.4) */
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000007, 0x40000000, 0), WEL2_CS_INVALID_PARAMETERS);      /* unknown */
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x100000001ull, 0x40000000, 0), WEL2_CS_INVALID_PARAMETERS);  /* Aff3 */
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001ull | (1ull << 24), 0x40000000, 0), WEL2_CS_INVALID_PARAMETERS); /* stray bit */
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00010103, 0x40000000, 0), WEL2_CS_INTERNAL_FAILURE);        /* no release addr */
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000000, 0x40000000, 0), WEL2_CS_ALREADY_ON);              /* boot core */
	CHECK_EQ(nlog, 0);                                                                          /* nothing touched */
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00010103), WEL2_CS_AFF_OFF);                               /* failed start: OFF */
	CHECK_EQ(wel2_cs_affinity(&CS, 0x123), WEL2_CS_INVALID_PARAMETERS);
	CHECK_EQ(wel2_cs_init(&c, &OPS, SECENTRY, odd, 2, 0), 0);                                   /* unaligned word */
	CHECK_EQ(wel2_cs_cpu_on(&c, 0x00000001, 0x40000000, 0), WEL2_CS_INTERNAL_FAILURE);
	CHECK_EQ(nlog, 0);
}

static void double_on(void)
{
	uint64_t e, x;
	fresh(8);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40000000, 1), WEL2_CS_SUCCESS);
	nlog = 0;
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40000100, 2), WEL2_CS_ON_PENDING);
	CHECK(wel2_cs_enter(&CS, 1, &e, &x));
	CHECK_EQ(e, 0x40000000);                     /* the second call changed nothing */
	nlog = 0;
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40000100, 2), WEL2_CS_ALREADY_ON);
	CHECK_EQ(nlog, 0);                           /* neither refused call touched memory or events */
}

/* OFF then ON: wake flag + SEV only, the spin table is not written again; timers reset before ERET */
static void off_then_on(void)
{
	uint64_t e, x;
	fresh(8);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40000000, 1), WEL2_CS_SUCCESS);
	CHECK(wel2_cs_enter(&CS, 1, &e, &x));
	CHECK_EQ(wel2_cs_cpu_off(&CS, 1), WEL2_CS_SUCCESS);
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00000001), WEL2_CS_AFF_OFF);
	nlog = 0;
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40300000, 9), WEL2_CS_SUCCESS);
	for (unsigned i = 0; i < nlog; i++) { CHECK(log_[i].op != O_W64); CHECK(log_[i].op != O_CVAC); }
	CHECK_EQ(nlog, 5);
	CHECK_EQ(log_[4].op, O_LOG); CHECK_EQ(log_[4].b, 1);
	CHECK_EQ(log_[0].op, O_DSB);
	CHECK_EQ(log_[1].op, O_WSET); CHECK_EQ(log_[1].a, 1); CHECK_EQ(log_[1].b, 1);
	CHECK_EQ(log_[2].op, O_DSB);
	CHECK_EQ(log_[3].op, O_SEV);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40300000, 9), WEL2_CS_ON_PENDING);
	/* the parked core: flag already set -> no wfe, flag cleared, timers reset, then enter */
	nlog = 0;
	CHECK(wel2_cs_park(&CS, 1, 5));
	CHECK_EQ(nlog, 3);
	CHECK_EQ(log_[0].op, O_TIMERS);              /* at park entry */
	CHECK_EQ(log_[1].op, O_WSET); CHECK_EQ(log_[1].b, 0);
	CHECK_EQ(log_[2].op, O_TIMERS);              /* after the wake */
	CHECK_EQ(wake[1], 0);
	nlog = 0;
	CHECK(wel2_cs_enter(&CS, 1, &e, &x));
	CHECK_EQ(nlog, 1);
	CHECK_EQ(e, 0x40300000); CHECK_EQ(x, 9);
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00000001), WEL2_CS_AFF_ON);
	/* a second round works the same */
	CHECK_EQ(wel2_cs_cpu_off(&CS, 1), WEL2_CS_SUCCESS);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40400000, 3), WEL2_CS_SUCCESS);
	CHECK(wel2_cs_park(&CS, 1, 5));
	CHECK(wel2_cs_enter(&CS, 1, &e, &x));
	CHECK_EQ(e, 0x40400000);
}

/* park really waits in wfe until the flag appears; a bounded wait returns false */
static void park_waits(void)
{
	uint64_t e, x;
	fresh(8);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000002, 0x40000000, 0), WEL2_CS_SUCCESS);
	CHECK(wel2_cs_enter(&CS, 2, &e, &x));
	CHECK_EQ(wel2_cs_cpu_off(&CS, 2), WEL2_CS_SUCCESS);
	nlog = 0;
	CHECK(!wel2_cs_park(&CS, 2, 3));             /* nobody wakes it */
	CHECK_EQ(nlog, 4);
	CHECK_EQ(log_[0].op, O_TIMERS);              /* gates closed before the first wfe */
	for (unsigned i = 1; i < nlog; i++) CHECK_EQ(log_[i].op, O_WFE);   /* wfe only while parked */
	nlog = 0; wfe_cnt = 0; wfe_until_wake = 4; wake_idx_to_set = 2;
	CHECK(wel2_cs_park(&CS, 2, 100));
	CHECK_EQ(wfe_cnt, 4);
	CHECK_EQ(log_[nlog - 1].op, O_TIMERS);       /* the timer reset is the last thing before returning */
}

/* A CPU_ON that arrives between "state OFF" and the core entering wfe is not lost */
static void wake_before_park(void)
{
	uint64_t e, x;
	fresh(8);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000003, 0x40000000, 0), WEL2_CS_SUCCESS);
	CHECK(wel2_cs_enter(&CS, 3, &e, &x));
	CHECK_EQ(wel2_cs_cpu_off(&CS, 3), WEL2_CS_SUCCESS);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000003, 0x40500000, 5), WEL2_CS_SUCCESS);   /* before the core parks */
	nlog = 0;
	CHECK(wel2_cs_park(&CS, 3, 1));
	for (unsigned i = 0; i < nlog; i++) CHECK(log_[i].op != O_WFE);              /* flag seen first: no wait */
}

static void last_core_denied(void)
{
	uint64_t e, x;
	fresh(2);
	CHECK_EQ(wel2_cs_cpu_off(&CS, 0), WEL2_CS_DENIED);                           /* only core 0 is on */
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40000000, 0), WEL2_CS_SUCCESS);
	CHECK_EQ(wel2_cs_cpu_off(&CS, 0), WEL2_CS_DENIED);                           /* core 1 only PENDING: does not count */
	CHECK(wel2_cs_enter(&CS, 1, &e, &x));
	CHECK_EQ(wel2_cs_can_off(&CS, 0), WEL2_CS_SUCCESS);
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00000000), WEL2_CS_AFF_ON);                 /* can_off changed nothing */
	CHECK_EQ(wel2_cs_cpu_off(&CS, 0), WEL2_CS_SUCCESS);                          /* core 1 is ON */
	CHECK_EQ(wel2_cs_cpu_off(&CS, 1), WEL2_CS_DENIED);                           /* now core 1 is the last */
	CHECK_EQ(wel2_cs_cpu_off(&CS, 0), WEL2_CS_INVALID_PARAMETERS);               /* already off */
	CHECK_EQ(wel2_cs_cpu_off(&CS, 5), WEL2_CS_INVALID_PARAMETERS);
}

/* The boot core can be switched off and back on through the wake path (it was never in the spin table). */
static void boot_core_cycle(void)
{
	uint64_t e, x;
	fresh(2);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40000000, 0), WEL2_CS_SUCCESS);
	CHECK(wel2_cs_enter(&CS, 1, &e, &x));
	CHECK_EQ(wel2_cs_cpu_off(&CS, 0), WEL2_CS_SUCCESS);
	nlog = 0;
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000000, 0x40600000, 4), WEL2_CS_SUCCESS);
	for (unsigned i = 0; i < nlog; i++) CHECK(log_[i].op != O_W64 && log_[i].op != O_CVAC);
	CHECK(wel2_cs_park(&CS, 0, 2));
	CHECK(wel2_cs_enter(&CS, 0, &e, &x));
	CHECK_EQ(e, 0x40600000); CHECK_EQ(x, 4);
}

static void off_on_pending_core(void)
{
	fresh(2);
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40000000, 0), WEL2_CS_SUCCESS);
	CHECK_EQ(wel2_cs_cpu_off(&CS, 1), WEL2_CS_INVALID_PARAMETERS);   /* PENDING is not ON */
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00000001), WEL2_CS_AFF_PENDING);
}

/* OFF + released + not parked cannot be reached through the API; force it: a second spin-table start is refused. */
static void off_released_not_parked(void)
{
	fresh(2);
	CS.core[1].released = true;
	nlog = 0;
	CHECK_EQ(wel2_cs_cpu_on(&CS, 0x00000001, 0x40000000, 0), WEL2_CS_INTERNAL_FAILURE);
	CHECK_EQ(nlog, 0);
	CHECK_EQ(wel2_cs_affinity(&CS, 0x00000001), WEL2_CS_AFF_OFF);
}

static void watchdog(void)
{
	nlog = 0;
	CHECK_EQ(wel2_cs_system_reset(&OPS, 0), -1);
	CHECK_EQ(nlog, 0);                                                            /* no node: nothing written */
	CHECK_EQ(wel2_cs_system_reset(&OPS, 0x23d2b0000ull), 0);
	CHECK_EQ(nlog, 4);
	CHECK_EQ(log_[0].op, O_MMIO); CHECK_EQ(log_[0].a, 0x23d2b0014ull); CHECK_EQ(log_[0].b, 0x100000);   /* alarm */
	CHECK_EQ(log_[1].op, O_MMIO); CHECK_EQ(log_[1].a, 0x23d2b0010ull); CHECK_EQ(log_[1].b, 0);          /* count */
	CHECK_EQ(log_[2].op, O_MMIO); CHECK_EQ(log_[2].a, 0x23d2b001cull); CHECK_EQ(log_[2].b, 4);          /* ctl last */
	CHECK_EQ(log_[3].op, O_DSB);
	nlog = 0;
	wel2_cs_wait_forever(&OPS, 5);
	CHECK_EQ(nlog, 6);
	CHECK_EQ(log_[0].op, O_TIMERS);              /* a pending timer FIQ must not make the halt spin */
	for (unsigned i = 1; i < nlog; i++) CHECK_EQ(log_[i].op, O_WFE);
}

int main(void)
{
	RUN(init_checks);
	RUN(first_start_order);
	RUN(refusals);
	RUN(double_on);
	RUN(off_then_on);
	RUN(park_waits);
	RUN(wake_before_park);
	RUN(last_core_denied);
	RUN(boot_core_cycle);
	RUN(off_on_pending_core);
	RUN(off_released_not_parked);
	RUN(watchdog);
	TESTS_DONE();
}
