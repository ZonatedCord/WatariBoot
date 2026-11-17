/* SPDX-License-Identifier: MIT */
/* Host tests: the PMC0-backed virtual PMUv3 cycle counter (include/wel2_vpmu_pmc0.h) against the fake Apple register file. */
#include "test.h"
#include "fake_apple_sysreg.h"
#include "wel2_apple_fiq.h"
#include "wel2_vpmu_pmc0.h"

static struct fake_sysreg FS;
static struct apple_sysreg_io IO;
static struct wel2_vpmu_sh SH;
static struct wel2_vpmu_core C[4];
static struct wel2_catch_cpu G;     /* the global control state of the virtual PMU */

static uint64_t wait_stub(void *ctx)
{
	(void)ctx;
	return 24000;                   /* 1 ms at 24 MHz */
}

static void fresh_def(uint32_t flags)   /* default flags: the counter always runs */
{
	memset(&FS, 0, sizeof FS);
	memset(&SH, 0, sizeof SH);
	memset(C, 0, sizeof C);
	memset(&G, 0, sizeof G);
	IO = fs_io(&FS);
	FS.pmc0_step = 3000000;         /* 3 GHz: 3,000,000 cycles per read pair over the 1 ms stub */
	SH.flags = flags;
	G.vflags = WEL2_VPMU_GLOBAL | flags;
}

static void fresh(uint32_t flags)      /* strict: the architectural freeze, as the first tests assume */
{
	fresh_def(flags | WEL2_VPMU_STRICT);
}

static struct wel2_sysreg_enc ENC(unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rd)
{
	struct wel2_sysreg_enc e = { 3, (uint8_t)op1, (uint8_t)crn, (uint8_t)crm, (uint8_t)op2, (uint8_t)rd, 0 };
	return e;
}
#define E_PMCR     ENC(3, 9, 12, 0, 0)
#define E_CNTENSET ENC(3, 9, 12, 1, 0)
#define E_CNTENCLR ENC(3, 9, 12, 2, 0)
#define E_CCNTR_W  ENC(3, 9, 13, 0, 0)

static void wr_(struct wel2_vpmu_core *c, struct wel2_sysreg_enc e, uint64_t v, uint64_t p0, bool boot)
{
	(void)boot;
	wel2_vpmu_write(&SH, c, &G, &e, v, p0, 0, 24000000);
}

/* ---- hardware init -------------------------------------------------------------------------------------------- */

static void t_init_enables_and_preserves_fiq_bits(void)
{
	struct wel2_pmc0_init r;
	/* what the FIQ module owns: IMODE [10:8] = FIQ, IACT (bit 11); plus bit 20 and an unrelated bit that must survive */
	uint64_t keep = (4ull << PMCR0_IMODE_SHIFT) | PMCR0_IACT | (1ull << 20) | (1ull << 30);
	uint64_t keep1 = (1ull << 40) | 0x5ull;

	fresh(0);
	FS.r[APPLE_SR_PMCR0] = keep;
	FS.r[APPLE_SR_PMCR1] = keep1;
	FS.r[APPLE_SR_PMC0] = 1000;
	wel2_pmc0_hw_init(&IO, wait_stub, 0, &r);
	CHECK_EQ(r.state, WEL2_PMC0_OK);
	CHECK_EQ(r.pmcr0_before, keep);
	CHECK_EQ(r.pmcr0_after, keep | PMCR0_CTR0_EN);
	CHECK_EQ(FS.r[APPLE_SR_PMCR0], keep | PMCR0_CTR0_EN);      /* IMODE, IACT, bit 20 and bit 30 untouched */
	CHECK_EQ(FS.r[APPLE_SR_PMCR1], keep1 | PMCR1_CTR0_EL0 | PMCR1_CTR0_EL1);
	CHECK_EQ(FS.r[APPLE_SR_PMCR0] & PMCR0_IMODE_MASK, 4ull << PMCR0_IMODE_SHIFT);
	CHECK_EQ(r.s1 - r.s0, 3000000);
	CHECK_EQ(r.ticks, 24000);
	CHECK_EQ(wel2_pmc0_per_ms(r.s1 - r.s0, r.ticks, 24000000), 3000000);
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_PMC0), 0);              /* the counter itself is never written */
	/* a second call changes nothing: no write at all */
	memset(FS.nwr, 0, sizeof FS.nwr);
	wel2_pmc0_hw_init(&IO, wait_stub, 0, &r);
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_PMCR0), 0);
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_PMCR1), 0);
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_PMC0), 0);
}

static void t_init_with_fiq_module_both_orders(void)
{
	struct wel2_pmc0_init r;
	struct apple_fiq_cpu fc;

	/* FIQ module first (it switches the PMU interrupt off), then ours: bit 0 set, IMODE off */
	fresh(0);
	memset(&fc, 0, sizeof fc);
	fc.io = &IO;
	FS.r[APPLE_SR_PMCR0] = (4ull << PMCR0_IMODE_SHIFT) | PMCR0_IACT | (1ull << 20);
	apple_fiq_init_cpu(&fc);
	CHECK_EQ(FS.r[APPLE_SR_PMCR0] & (PMCR0_IMODE_MASK | PMCR0_IACT), 0);
	wel2_pmc0_hw_init(&IO, wait_stub, 0, &r);
	CHECK_EQ(FS.r[APPLE_SR_PMCR0], PMCR0_CTR0_EN | (1ull << 20));
	/* ours first, then the FIQ module clears a firing PMU interrupt: our enable bit survives its read-modify-write */
	fresh(0);
	memset(&fc, 0, sizeof fc);
	fc.io = &IO;
	FS.r[APPLE_SR_PMCR0] = (4ull << PMCR0_IMODE_SHIFT) | PMCR0_IACT;
	wel2_pmc0_hw_init(&IO, wait_stub, 0, &r);
	apple_fiq_init_cpu(&fc);
	CHECK(FS.r[APPLE_SR_PMCR0] & PMCR0_CTR0_EN);
	CHECK_EQ(FS.r[APPLE_SR_PMCR0] & (PMCR0_IMODE_MASK | PMCR0_IACT), 0);
	CHECK(FS.r[APPLE_SR_PMCR1] & PMCR1_CTR0_EL1);
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_PMC0), 0);
}

static void t_init_fallback(void)
{
	struct wel2_pmc0_init r;

	/* PMC0 does not move across the EL2 delay: PENDING, then FAILED at the guest read if it still has not moved */
	fresh(0);
	FS.stuck_pmc0 = true;
	FS.r[APPLE_SR_PMC0] = 777;
	wel2_pmc0_hw_init(&IO, wait_stub, 0, &r);
	CHECK_EQ(r.state, WEL2_PMC0_PENDING);
	wel2_vpmu_core_init(&SH, &C[0], true, r.s1, r.state);
	CHECK_EQ(wel2_vpmu_resolve(&C[0], 777), WEL2_PMC0_FAILED);
	CHECK_EQ(wel2_vpmu_resolve(&C[0], 999999), WEL2_PMC0_FAILED);     /* final */
	/* ... or has: it counts at EL1 even though it did not at EL2 */
	fresh(0);
	FS.stuck_pmc0 = true;
	FS.r[APPLE_SR_PMC0] = 777;
	wel2_pmc0_hw_init(&IO, wait_stub, 0, &r);
	wel2_vpmu_core_init(&SH, &C[0], true, r.s1, r.state);
	CHECK_EQ(wel2_vpmu_resolve(&C[0], 5000), WEL2_PMC0_OK);
	/* the enable does not stick: failed at once, the delay is not even run */
	fresh(0);
	FS.stuck_pmcr0 = true;
	wel2_pmc0_hw_init(&IO, wait_stub, 0, &r);
	CHECK_EQ(r.state, WEL2_PMC0_FAILED);
	CHECK_EQ(r.ticks, 0);
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_PMC0), 0);
}

/* ---- offsets, freeze / resume, reset, write ---------------------------------------------------------------- */

static void t_offset_math(void)
{
	/* boot core: PMC0 = 1000 at init; the counter starts frozen at 1000 */
	fresh(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 1000, WEL2_PMC0_OK);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 1500), 1000);       /* PMCR.E = 0: frozen */
	wr_(&C[0], E_CNTENSET, WEL2_PMU_CBIT, 2000, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 2100), 1000);       /* PMCNTENSET.C alone is not enough */
	wr_(&C[0], E_PMCR, WEL2_PMCR_E, 3000, true);                      /* enable: resumes from 1000 at PMC0 = 3000 */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 3000), 1000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 3500), 1500);
	/* freeze: the visible value is stored; PMC0 keeps going underneath */
	wr_(&C[0], E_CNTENCLR, WEL2_PMU_CBIT, 4000, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 9000), 2000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 99000), 2000);
	/* resume: continues from the frozen value, the frozen interval does not count */
	wr_(&C[0], E_CNTENSET, WEL2_PMU_CBIT, 100000, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 100000), 2000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 100250), 2250);
	/* PMCCNTR write: sets the visible value, this core's offset only */
	wr_(&C[0], E_CCNTR_W, 50, 200000, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 200000), 50);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 200100), 150);
	/* PMCR.C (with E kept): reset to 0 */
	wr_(&C[0], E_PMCR, WEL2_PMCR_E | WEL2_PMCR_C, 300000, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 300000), 0);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 300040), 40);
	/* write while frozen, then resume */
	wr_(&C[0], E_PMCR, 0, 310000, true);                              /* E off: freeze at 10040 - 40.. */
	wr_(&C[0], E_CCNTR_W, 7777, 320000, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 330000), 7777);
	wr_(&C[0], E_PMCR, WEL2_PMCR_E, 340000, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 340010), 7787);
	/* the hardware counter is never written: this module has no access to it at all, and the control state reads back as written */
	CHECK_EQ(G.pmcr & WEL2_PMCR_KEEP, WEL2_PMCR_E);
	CHECK_EQ(G.pmcnten, WEL2_PMU_CBIT);
}

static void t_run_token(void)
{
	fresh(WEL2_VPMU_RUN);
	wel2_vpmu_core_init(&SH, &C[0], true, 500, WEL2_PMC0_OK);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 500), 500);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 800), 800);          /* runs although PMCR.E / PMCNTENSET.C are 0 */
}

static void t_wraparound(void)
{
	fresh(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 0xfffffffffffffff0ull, WEL2_PMC0_OK);
	wr_(&C[0], E_CNTENSET, WEL2_PMU_CBIT, 0xfffffffffffffff0ull, true);
	wr_(&C[0], E_PMCR, WEL2_PMCR_E, 0xfffffffffffffff0ull, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 0xfffffffffffffff8ull), 0xfffffffffffffff8ull);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 8), 8);              /* modulo 2^64 (a 48-bit PMC0 is a [hypothesis] left to the hardware run) */
}

/* ---- several cores ------------------------------------------------------------------------------------------ */

static void enable_all(struct wel2_vpmu_core *c, uint64_t p0)
{
	wr_(c, E_CNTENSET, WEL2_PMU_CBIT, p0, false);
	wr_(c, E_PMCR, WEL2_PMCR_E, p0, false);
}

static void t_sync(void)
{
	/* the boot core's PMC0 is 100000 and it has been read at 100000; core 1's PMC0 is 7 (reset later): with sync it starts at 100000 */
	fresh(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 100000, WEL2_PMC0_OK);
	enable_all(&C[0], 100000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 150000), 150000);    /* published */
	wel2_vpmu_core_init(&SH, &C[1], false, 7, WEL2_PMC0_OK);
	enable_all(&C[1], 7);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 7), 150000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 107), 150100);
	/* nosync: each core starts at its own PMC0 */
	fresh(WEL2_VPMU_NOSYNC);
	wel2_vpmu_core_init(&SH, &C[0], true, 100000, WEL2_PMC0_OK);
	wel2_vpmu_core_init(&SH, &C[1], false, 7, WEL2_PMC0_OK);
	enable_all(&C[1], 7);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 7), 7);
}

static void t_clamp_per_core(void)
{
	fresh(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 1000, WEL2_PMC0_OK);
	enable_all(&C[0], 1000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 5000), 5000);
	/* a hardware reading below an earlier one (a counter that was reset behind our back) never makes the guest value go back */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 4000), 5000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 5001), 5001);
	/* an explicit guest write is not clamped: it may go down */
	wr_(&C[0], E_CCNTR_W, 10, 6000, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 6000), 10);
}

static void t_hiwater(void)
{
	/* P core fast, E core slow: with hiwater the E core never reads below what the P core returned (it jumps forward, never stalls) */
	fresh(WEL2_VPMU_HIWATER);
	wel2_vpmu_core_init(&SH, &C[0], true, 0, WEL2_PMC0_OK);
	wel2_vpmu_core_init(&SH, &C[1], false, 0, WEL2_PMC0_OK);
	enable_all(&C[0], 0);
	enable_all(&C[1], 0);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 3000), 3000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 1000), 3000);       /* jumped to the mark */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 1500), 3500);       /* and keeps counting at its own pace from there */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 3200), 3500);
	/* without it (and without sync) the two cores keep their own pace */
	fresh(WEL2_VPMU_NOSYNC);
	wel2_vpmu_core_init(&SH, &C[0], true, 0, WEL2_PMC0_OK);
	wel2_vpmu_core_init(&SH, &C[1], false, 0, WEL2_PMC0_OK);
	enable_all(&C[0], 0);
	enable_all(&C[1], 0);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 3000), 3000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 1000), 1000);
}

static void t_freeze_is_global_lazy_per_core(void)
{
	/* core 0 freezes the global gate; core 1 notices at its next access and stores the value it has then */
	fresh(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 0, WEL2_PMC0_OK);
	wel2_vpmu_core_init(&SH, &C[1], false, 0, WEL2_PMC0_OK);
	enable_all(&C[0], 0);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 100), 0);           /* a core resumes from its own frozen start value at its first access */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 300), 200);
	wr_(&C[0], E_CNTENCLR, WEL2_PMU_CBIT, 500, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 900), 800);         /* stored at the first access after the freeze (documented imprecision) */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 5000), 800);
}

/* ---- review fixes: always-running default, late re-sync, wrap / reset ----------------------------------------- */

static void t_default_always_runs(void)
{
	/* bootmgr's first PMU access is a PMCCNTR read before any PMCR / PMCNTENSET write: the value must advance */
	fresh_def(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 1000, WEL2_PMC0_OK);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 1000), 1000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 1500), 1500);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 9000), 9000);
	/* a secondary too, from the boot core's value, and a later enable / disable does not freeze it */
	wel2_vpmu_core_init(&SH, &C[1], false, 7, WEL2_PMC0_OK);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 7), 9000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 107), 9100);
	wr_(&C[1], E_PMCR, 0, 207, false);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 307), 9300);
	/* strict keeps the freeze */
	fresh(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 1000, WEL2_PMC0_OK);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 5000), 1000);
}

static void t_late_resync(void)
{
	/* strict: the boot core runs far ahead; a secondary that only now resumes is lifted to the boot core's published value */
	fresh(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 0, WEL2_PMC0_OK);
	wel2_vpmu_core_init(&SH, &C[1], false, 0, WEL2_PMC0_OK);
	enable_all(&C[0], 0);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 50000000), 50000000);   /* published */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 100), 50000000);       /* first read: no big backwards step */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 200), 50000100);
	/* freeze and a late resume on core 1 after core 0 moved on again */
	wr_(&C[1], E_CNTENCLR, WEL2_PMU_CBIT, 300, false);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 400), 50000200);
	fresh(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 0, WEL2_PMC0_OK);
	wel2_vpmu_core_init(&SH, &C[1], false, 0, WEL2_PMC0_OK);
	enable_all(&C[0], 0);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 10), 0);               /* strict: resumes from its frozen start value */
	wr_(&C[0], E_CNTENCLR, WEL2_PMU_CBIT, 20, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[1], &G, false, 30), 20);              /* frozen at 20 */
	wr_(&C[0], E_CNTENSET, WEL2_PMU_CBIT, 40, true);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 90000000), 90000000 - 40 + 20); /* core 0 resumed from its frozen value 20 at PMC0 40 */
	CHECK(wel2_vpmu_read(&SH, &C[1], &G, false, 100) >= SH.pub0);          /* the late resume is never below the boot core's published value */
}

static void t_wrap_and_reset(void)
{
	/* 48-bit wrap of PMC0: the value continues from last + 1, it does not stall */
	fresh_def(0);
	wel2_vpmu_core_init(&SH, &C[0], true, (1ull << 48) - 100, WEL2_PMC0_OK);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, (1ull << 48) - 50), (1ull << 48) - 50);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, (1ull << 48) - 1), (1ull << 48) - 1);
	{
		uint64_t a = wel2_vpmu_read(&SH, &C[0], &G, true, 10);            /* wrapped to 10 */
		uint64_t b = wel2_vpmu_read(&SH, &C[0], &G, true, 5010);
		CHECK_EQ(a, (1ull << 48));
		CHECK_EQ(b - a, 5000);                                             /* and keeps counting from there */
	}
	/* PMC0 reset to a small value in deep idle */
	fresh_def(0);
	wel2_vpmu_core_init(&SH, &C[0], true, 5000000, WEL2_PMC0_OK);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 9000000), 9000000);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 100), 9000001);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 600), 9000501);
	/* a small step back (jitter) is still clamped, not rebased */
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 500), 9000501);
	CHECK_EQ(wel2_vpmu_read(&SH, &C[0], &G, true, 700), 9000601);
}

/* ---- options and panel text --------------------------------------------------------------------------------- */

static void t_parse(void)
{
	uint32_t f = 0;
	const char *end;

	CHECK(wel2_vpmu_parse("global,src=pmc0", &f, &end)); CHECK_EQ(f, WEL2_VPMU_GLOBAL | WEL2_VPMU_SRC_PMC0);
	CHECK(wel2_vpmu_parse("src=timer", &f, &end)); CHECK_EQ(f, WEL2_VPMU_GLOBAL | WEL2_VPMU_SRC_TIMER);
	CHECK(wel2_vpmu_parse("global,src=timer,src=pmc0", &f, &end)); CHECK_EQ(f, WEL2_VPMU_GLOBAL | WEL2_VPMU_SRC_PMC0);
	CHECK(wel2_vpmu_parse("global,nosync,hiwater,run", &f, &end));
	CHECK_EQ(f, WEL2_VPMU_GLOBAL | WEL2_VPMU_NOSYNC | WEL2_VPMU_HIWATER | WEL2_VPMU_RUN);
	CHECK(wel2_vpmu_parse("global,strict", &f, &end)); CHECK_EQ(f, WEL2_VPMU_GLOBAL | WEL2_VPMU_STRICT);
	CHECK(wel2_vpmu_parse("global,nosync,sync", &f, &end)); CHECK_EQ(f, WEL2_VPMU_GLOBAL);
	CHECK(wel2_vpmu_parse("global", &f, &end)); CHECK_EQ(f, WEL2_VPMU_GLOBAL);              /* unchanged: sync on, source = platform default */
	CHECK(!wel2_vpmu_parse("src=cycles", &f, &end));
}

static void t_src_format(void)
{
	char b[64];

	wel2_vpmu_src_format(b, sizeof b, WEL2_PMC0_OK, true, 3204000);
	CHECK(!strcmp(b, "SRC=PMC0 D=3204000"));
	wel2_vpmu_src_format(b, sizeof b, WEL2_PMC0_OK, true, 0);
	CHECK(!strcmp(b, "SRC=PMC0 D=?"));
	wel2_vpmu_src_format(b, sizeof b, WEL2_PMC0_PENDING, true, 0);
	CHECK(!strcmp(b, "SRC=PMC0(PENDING) D=?"));
	wel2_vpmu_src_format(b, sizeof b, WEL2_PMC0_FAILED, true, 5);
	CHECK(!strcmp(b, "SRC=TIMER(fallback)"));
	wel2_vpmu_src_format(b, sizeof b, WEL2_PMC0_OFF, false, 0);
	CHECK(!strcmp(b, "SRC=TIMER"));
	wel2_vpmu_src_format(b, 8, WEL2_PMC0_OK, true, 3204000);          /* truncation keeps the NUL */
	CHECK_EQ(strlen(b), 7);
	/* the whole status row stays within the panel's width limit (WEL2_FB_CATCH_LEN 96) with the longest parts */
	{
		char row[WEL2_CATCH_LINE_MAX];
		struct wel2_sysreg_enc e = { 3, 3, 14, 15, 7, 1, 0 };     /* PMCCFILTR_EL0 */
		unsigned k = wel2_vpmu_src_format(row, sizeof row, WEL2_PMC0_OK, true, 3204000000ull);

		row[k++] = ' ';
		wel2_catch_status_format(row + k, sizeof row - k, 0x7e0, WEL2_HACR_PMU, 123456, &e);
		CHECK(strlen(row) < 96);
	}
}

int main(void)
{
	RUN(t_init_enables_and_preserves_fiq_bits);
	RUN(t_init_with_fiq_module_both_orders);
	RUN(t_init_fallback);
	RUN(t_offset_math);
	RUN(t_run_token);
	RUN(t_wraparound);
	RUN(t_sync);
	RUN(t_clamp_per_core);
	RUN(t_hiwater);
	RUN(t_freeze_is_global_lazy_per_core);
	RUN(t_default_always_runs);
	RUN(t_late_resync);
	RUN(t_wrap_and_reset);
	RUN(t_parse);
	RUN(t_src_format);
	TESTS_DONE();
}
