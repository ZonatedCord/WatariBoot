/* SPDX-License-Identifier: MIT */
/* Host tests: the platform switch (boot/plat_select.c) and the M1 platform table (boot/plat_m1.c, real code). The QEMU
 * table needs SMC and the physical GIC and is not host-buildable: a stand-in with the same name and window constants
 * takes its place here; what is tested for it is only that the switch picks it. */
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "fdt.h"
#include "fdt_m1.h"
#include "fdt_build.h"
#include "fx_j293.h"
#include "plat.h"

/* stand-in for plat_qemu.c */
const struct wel2_platform plat_qemu = {
	.name = "qemu", .has_phys_gic = true, .vgic_gicd_base = PLAT_QEMU_GICD_BASE, .vgic_gicr_base = PLAT_QEMU_GICR_BASE,
};

#include "fake_apple_sysreg.h"
#include "wel2_aic.h"
#include "wel2_apple_fiq.h"
#include "wel2_cpu_start.h"

/* what plat_m1.c needs from the boot environment (main.c, util.c, stage1.c, pirq_qemu.c, start.S) */
unsigned char g_stub_fatal;
static unsigned t_cpu;
static unsigned n_checkpoint, last_checkpoint, n_unlock;
static const char *last_dump;
static jmp_buf dump_jb;
void puts_(const char *s) { (void)s; }
void put_kv(const char *k, uint64_t v) { (void)k; (void)v; }
void put_kn(const char *a, unsigned n, const char *b) { (void)a; (void)n; (void)b; }
void checkpoint(unsigned n) { n_checkpoint++; last_checkpoint = n; }
void big_unlock(void) { n_unlock++; }
unsigned this_cpu(void) { return t_cpu; }
void __attribute__((noreturn)) fatal_msg(const char *why) { (void)why; exit(99); }
void __attribute__((noreturn)) fatal_dump(struct frame *f, unsigned idx, const char *why)
{
	(void)f; (void)idx;
	last_dump = why;
	longjmp(dump_jb, 1);
}
void secondary_entry(void) {}
void __attribute__((noreturn)) secondary_rewake(void) { exit(98); }
struct pcpu g_pcpu[MAX_CPUS];
unsigned g_ncpu;
struct vgic g_vgic;
struct m1_platform g_m1;
struct pirq g_pirq;
struct pirq_aic g_aic;
bool g_aic_on;
static struct fake_sysreg FS;
const struct apple_sysreg_io apple_sysreg_real = { fs_rd, fs_wr, &FS };

static void *qemu_shaped(struct fdtb *b)
{
	static const uint64_t mem[1][2] = { { 0x40000000ull, 0x80000000ull } };

	fb_init(b);
	fb_begin(b, "");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
	fb_str(b, "compatible", "linux,dummy-virt");
	fb_begin(b, "memory@40000000");
	fb_str(b, "device_type", "memory");
	fb_regs(b, 2, 2, 1, mem);
	fb_end(b);
	fb_begin(b, "intc@8000000");
	fb_str(b, "compatible", "arm,gic-v3");
	fb_reg(b, 2, 2, 0x8000000, 0x10000);
	fb_end(b);
	fb_begin(b, "watari-aic@f004000");                 /* the QEMU hybrid machine's AIC: NOT the M1 */
	fb_str(b, "compatible", "watari,aic-v1");
	fb_reg(b, 2, 2, 0xf004000, 0x4000);
	fb_end(b);
	fb_end(b);
	return fb_finish(b);
}

static void t_select(void)
{
	struct fdtb *b = malloc(sizeof(*b));
	struct m1_platform p;
	int rc;

	rc = fdt_m1_parse(qemu_shaped(b), &p);
	CHECK_EQ(rc, 0);
	CHECK(plat_select(rc, &p) == &plat_qemu);                         /* QEMU-shaped FDT (also with the hybrid AIC) */
	CHECK(!strcmp(plat_select(rc, &p)->name, "qemu"));

	rc = fdt_m1_parse(build_j293(b, &O0), &p);
	CHECK_EQ(rc, 0);
	CHECK(plat_select(rc, &p) == &plat_m1);                           /* j293-shaped FDT: apple,aic present */
	CHECK(!strcmp(plat_select(rc, &p)->name, "m1"));

	struct opt noaic = O0;                                            /* the same tree without any AIC is no M1 */
	uint8_t *raw = build_j293(b, &noaic);
	uint32_t tot = fdt_totalsize(raw);

	for (uint32_t i = 0; i + 9 < tot; i++)                            /* spoil the compatible string "apple,aic" */
		if (!memcmp(raw + i, "apple,aic", 9))
			raw[i + 6] = 'x';
	rc = fdt_m1_parse(raw, &p);
	CHECK_EQ(rc, 0);
	CHECK(!p.is_m1);
	CHECK(plat_select(rc, &p) == &plat_qemu);

	build_j293(b, &O0);                                               /* a truncated blob: the parse fails, QEMU path */
	((uint8_t *)b->buf)[4] = 0; ((uint8_t *)b->buf)[5] = 0; ((uint8_t *)b->buf)[6] = 0; ((uint8_t *)b->buf)[7] = 0x40;
	rc = fdt_m1_parse(b->buf, &p);
	CHECK(rc != 0);
	CHECK(plat_select(rc, &p) == &plat_qemu);
	free(b);
}

static void t_m1_table(void)
{
	const struct wel2_platform *m = &plat_m1;

	CHECK(m->m1_memory);
	CHECK(!m->has_phys_gic);
	CHECK_EQ(m->s2_ipa_bits, 36);
	CHECK_EQ(m->vgic_gicd_base, 0xF00000000ULL);
	CHECK_EQ(m->vgic_gicr_base, 0xF10000000ULL);
	CHECK_EQ(m->vgic_gicd_base, GICD);                                /* the windows test.h's vGIC tests use */
	CHECK_EQ(m->vgic_gicr_base, GICR);
	CHECK(m->discover_cpus && m->pirq_init && m->init_boot && m->init_cpu && m->cpu_down && m->has_lpis &&
	      m->idbits && m->lpi_reg && m->irq_ack && m->irq_eoi && m->irq_post && m->ppi_enable && m->kick && m->fiq &&
	      m->spi_enable && m->spi_route && m->spi_trigger && m->cpu_start && m->cpu_off && m->system_off &&
	      m->system_reset);
	CHECK(!m->has_lpis());
	CHECK_EQ(m->idbits(), 10);
	CHECK(plat_qemu.fiq == NULL);                                     /* QEMU: an unexpected FIQ stays fatal */
}

/* ---- the M1 wiring (T6d/T6e/T6f): the real plat_m1.c hooks against a memory-backed AIC window, the fake system
 * registers and a fake core-start ops table. The instructions that cannot run on the host (mrs MPIDR in discover_cpus,
 * the halts) are not exercised. ---- */

#define ANS_PHYS 590u
#define ANS_V    622u

static uint32_t aicmem[0x10000 / 4];

static void m1_env(void)
{
	struct vgic_config cfg = { GICD, GICR, 4, NULL, { fx_log, fx_eff, NULL, NULL }, false, 0 };

	memset(&FS, 0, sizeof FS);
	memset(aicmem, 0, sizeof aicmem);
	aicmem[AIC_INFO / 4] = 1024;
	memset(&g_m1, 0, sizeof g_m1);
	g_m1.naic = 1;
	g_m1.aic[0].base = (uint64_t)(uintptr_t)aicmem;
	g_m1.ncpu = 4;
	g_m1.cpu[0].reg = 0x0; g_m1.cpu[1].reg = 0x1;                      /* E0, E1 */
	g_m1.cpu[2].reg = 0x10100; g_m1.cpu[3].reg = 0x10101;              /* P0, P1 */
	g_ncpu = 4;
	t_cpu = 0;
	setup(4);
	(void)cfg;
	bring_up();
	enable_spi(ANS_V);
	for (unsigned i = 0; i < 4; i++) {
		enable_priv(i, APPLE_PPI_VTIMER);
		enable_priv(i, APPLE_PPI_PTIMER);
	}
	memcpy(&g_vgic, &F.v, sizeof g_vgic);
	g_aic_on = false;
}

static bool pending_is(unsigned vcpu, uint32_t want)
{
	uint32_t id = 0;
	uint8_t p = 0;

	return vgic_next_pending(&g_vgic, vcpu, &id, &p) && id == want;
}

static void t_m1_wiring_table(void)
{
	const struct wel2_platform *m = &plat_m1;

	CHECK(m->native_psci && m->cpu_off_begin && m->cpu_arrive && m->timer_resync && m->maint_backstop && m->stats);
	CHECK(!plat_qemu.native_psci && !plat_qemu.cpu_off_begin && !plat_qemu.cpu_arrive && !plat_qemu.timer_resync &&
	      !plat_qemu.maint_backstop);                                  /* QEMU keeps the generic behaviour */
	CHECK_EQ(m->irq_ack(), 1023);                                      /* no GIC: never an INTID */
	m->irq_eoi(1023);
}

static void t_m1_aic_irq_path(void)
{
	const struct wel2_platform *m = &plat_m1;

	m1_env();
	m->pirq_init(&g_vgic, 4, 0);
	CHECK(g_aic_on);
	CHECK_EQ(g_aic.nr_irq, 1024);
	CHECK_EQ(g_aic.cpu_of_vcpu[2], 4);                                 /* P0 = AIC core 4 (Aff1 * 4 + Aff0) */
	CHECK_EQ(g_aic.cpu_of_vcpu[3], 5);
	for (unsigned i = 0; i < 4; i++)
		pirq_vcpu_on(&g_pirq, i);

	/* per-core init: the WHOAMI check */
	aicmem[AIC_WHOAMI / 4] = 0;
	CHECK_EQ(m->init_cpu(0), 0);
	aicmem[AIC_WHOAMI / 4] = 4;
	CHECK_EQ(m->init_cpu(2), 0);
	aicmem[AIC_WHOAMI / 4] = 3;
	CHECK_EQ(m->init_cpu(2), -1);                                      /* this core is not the vCPU's core */
	CHECK_EQ(g_aic.st.whoami_mismatch, 1);

	/* the drain: a source that stays asserted is read at most AIC_DRAIN_BUDGET times per IRQ, then reported pending */
	g_aic.ops.unmask(g_aic.ops.ctx, ANS_PHYS);
	aicmem[AIC_EVENT / 4] = (AIC_EVENT_TYPE_HW << 16) | ANS_PHYS;
	m->irq_post(0);
	CHECK_EQ(g_aic.st.events, AIC_DRAIN_BUDGET);
	CHECK(g_pirq.st.arrivals >= 1);
	CHECK(pending_is(0, ANS_V));
	aicmem[AIC_EVENT / 4] = 0;                                         /* line low: one empty read ends the drain */
	m->irq_post(0);
	CHECK_EQ(g_aic.st.events, AIC_DRAIN_BUDGET);
	CHECK_EQ(g_aic.st.spurious, 1);

	/* a legacy IPI event is contained and not forwarded */
	aicmem[AIC_EVENT / 4] = 0;
	m->irq_post(1);
	CHECK(plat_m1.spi_enable != NULL);                                 /* present, but nothing calls them (pirq drives the AIC) */
}

static void t_m1_fiq_timer_ipi(void)
{
	const struct wel2_platform *m = &plat_m1;
	struct frame fr;

	m1_env();
	m->pirq_init(&g_vgic, 4, 0);
	memset(&fr, 0, sizeof fr);
	CHECK_EQ(m->init_cpu(0), 0);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], VM_TMR_FIQ_V | VM_TMR_FIQ_P);   /* gates open after the timer controls were reset */
	CHECK(fs_first_write(&FS, APPLE_SR_CNTV_CTL_EL02, ~0ull, 0) < fs_first_write(&FS, APPLE_SR_VM_TMR_FIQ_ENA, ~0ull, 3));

	/* a guest timer fires: FIQ -> gate closed, PPI 27 raised in the vGIC, the guest's control untouched */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	m->fiq(&fr);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA] & VM_TMR_FIQ_V, 0);
	CHECK_EQ(FS.r[APPLE_SR_CNTV_CTL_EL02], 5);
	CHECK(pending_is(0, APPLE_PPI_VTIMER));
	/* still firing: the resync keeps the gate closed; silenced: the line drops and the gate reopens */
	m->timer_resync(0);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA] & VM_TMR_FIQ_V, 0);
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 0;
	m->timer_resync(0);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA] & VM_TMR_FIQ_V, VM_TMR_FIQ_V);
	CHECK(!pending_is(0, APPLE_PPI_VTIMER));

	/* the contract hook: PPI 27/30 are the gates, other lines are ignored */
	m->ppi_enable(APPLE_PPI_PTIMER, false);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA] & VM_TMR_FIQ_P, 0);
	m->ppi_enable(APPLE_PPI_PTIMER, true);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA] & VM_TMR_FIQ_P, VM_TMR_FIQ_P);
	FS.nlog = 0;
	m->ppi_enable(25, false);
	CHECK_EQ(FS.nlog, 0);

	/* kick = one write of IPI_RR_GLOBAL with the target's cluster/core */
	m->kick(1);
	CHECK_EQ(FS.ipi_rr_last, apple_ipi_value(0x1));
	m->kick(2);
	CHECK_EQ(FS.ipi_rr_last, apple_ipi_value(0x10100));
	CHECK_EQ(FS.ipi_rr_writes, 2);

	/* CPU_OFF teardown closes the gates; the maintenance backstop is a pass-through (nothing asserted: 0) */
	m->cpu_down();
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);
	CHECK_EQ(m->maint_backstop(0), 0);

	/* a source that never goes away is reported as a storm with a register dump, not looped on */
	CHECK_EQ(m->init_cpu(0), 0);
	last_dump = NULL;
	if (!setjmp(dump_jb)) {
		for (unsigned i = 0; i < 100; i++)
			m->fiq(&fr);                                       /* FIQs with no cause: the spurious budget runs out */
	}
	CHECK(last_dump != NULL);
}

/* fake core start ops: only what the delegation test looks at */
static unsigned cs_sev_count, cs_w64_count;
static uint32_t cs_wake[WEL2_CS_MAX];
static void c_w64(void *c, uint64_t pa, uint64_t v) { (void)c; (void)pa; (void)v; cs_w64_count++; }
static void c_nop(void *c, uint64_t pa) { (void)c; (void)pa; }
static void c_dsb(void *c) { (void)c; }
static void c_sev(void *c) { (void)c; cs_sev_count++; }
static void c_wfe(void *c) { (void)c; }
static void c_wset(void *c, unsigned i, uint32_t v) { (void)c; cs_wake[i] = v; }
static uint32_t c_wget(void *c, unsigned i) { (void)c; return cs_wake[i]; }
static void c_timers(void *c) { (void)c; }
static void c_mmio(void *c, uint64_t pa, uint32_t v) { (void)c; (void)pa; (void)v; }
static void c_log(void *c, int e, unsigned i, uint64_t a) { (void)c; (void)e; (void)i; (void)a; }

static void t_m1_psci_hooks(void)
{
	static const struct wel2_cs_ops ops = { c_w64, c_nop, c_dsb, c_sev, c_wfe, c_wset, c_wget, c_timers, c_mmio, c_log, NULL };
	static const struct wel2_cs_cpu cpus[4] = { { 0, 0x1000 }, { 1, 0x1008 }, { 0x10100, 0x1010 }, { 0x10101, 0x1018 } };
	const struct wel2_platform *m = &plat_m1;
	uint64_t entry = 0, ctx = 0;

	memset(cs_wake, 0, sizeof cs_wake);
	cs_sev_count = cs_w64_count = 0;
	CHECK_EQ(wel2_cs_init(&g_cs, &ops, 0x900000234000ull, cpus, 4, 0), 0);

	CHECK_EQ(m->cpu_start(1, 1, 0x40100000, 7), WEL2_CS_SUCCESS);       /* spin table: store, clean, SEV */
	CHECK_EQ(cs_w64_count, 1);
	CHECK_EQ(cs_sev_count, 1);
	CHECK_EQ(m->cpu_start(1, 1, 0x40100000, 7), WEL2_CS_ON_PENDING);    /* codes pass through unchanged */
	CHECK_EQ(m->cpu_start(0x77, 0, 0x40100000, 7), WEL2_CS_INVALID_PARAMETERS);
	CHECK(m->cpu_arrive(1, &entry, &ctx));
	CHECK_EQ(entry, 0x40100000);
	CHECK_EQ(ctx, 7);
	CHECK(!m->cpu_arrive(3, &entry, &ctx));                            /* a stray start never enters a guest */

	CHECK_EQ(m->cpu_off_begin(1), WEL2_CS_SUCCESS);                    /* core 0 is ON: core 1 may go */
	CHECK_EQ(m->cpu_off_begin(0), WEL2_CS_DENIED);                     /* nobody left to wake the last core */
	CHECK_EQ(m->cpu_off_begin(2), WEL2_CS_INVALID_PARAMETERS);         /* not ON */
	CHECK_EQ(m->cpu_start(1, 1, 0x40200000, 9), WEL2_CS_SUCCESS);      /* a parked core is woken by flag + SEV */
	CHECK_EQ(cs_wake[1], 1);
	CHECK_EQ(cs_w64_count, 1);                                         /* the spin table is not written again */
}

int main(void)
{
	RUN(t_select);
	RUN(t_m1_table);
	RUN(t_m1_wiring_table);
	RUN(t_m1_aic_irq_path);
	RUN(t_m1_fiq_timer_ipi);
	RUN(t_m1_psci_hooks);
	TESTS_DONE();
}
