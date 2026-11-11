/* SPDX-License-Identifier: MIT */
/* Host tests: the Apple FIQ module (PLAN S10 / T6e) against a fake system-register file. */
#include "test.h"
#include "fake_apple_sysreg.h"
#include "wel2_apple_fiq.h"

static struct fake_sysreg FS;
static struct apple_sysreg_io IO;
static struct apple_fiq_cpu C;
static unsigned ipi_work;

static void on_ipi(void *ctx) { (void)ctx; ipi_work++; }

static void fresh(void)
{
	memset(&FS, 0, sizeof FS);
	memset(&C, 0, sizeof C);
	IO = fs_io(&FS);
	setup(1);
	bring_up();
	enable_priv(0, APPLE_PPI_VTIMER);
	enable_priv(0, APPLE_PPI_PTIMER);
	ipi_work = 0;
	C.io = &IO;
	C.vgic = &F.v;
	C.vcpu = 0;
	C.on_ipi = on_ipi;
}

/* Gates open and clean, as after apple_fiq_init_cpu(); clears the access log. */
static void ready(void)
{
	apple_fiq_init_cpu(&C);
	FS.nlog = 0;
	memset(FS.nwr, 0, sizeof FS.nwr);
}

static bool deliverable(uint32_t intid)
{
	uint32_t id = 0;
	uint8_t p = 0;

	return vgic_next_pending(&F.v, 0, &id, &p) && id == intid;
}

static bool any_deliverable(void)
{
	uint32_t id = 0;
	uint8_t p = 0;

	return vgic_next_pending(&F.v, 0, &id, &p);
}

/* ---- IPI send ---- */

static void t_ipi_value(void)
{
	/* measured j293 MPIDRs: E cores 0x80000000+n, P cores 0x80010100+n (Aff2 = 1, Aff1 = 1) */
	CHECK_EQ(apple_ipi_value(0x80000000), 0x00000);     /* E0 */
	CHECK_EQ(apple_ipi_value(0x80000002), 0x00002);     /* E2: cluster field 0 */
	CHECK_EQ(apple_ipi_value(0x80010100), 0x10000);     /* P0: cluster 1, core 0 (Aff0 alone would alias E0) */
	CHECK_EQ(apple_ipi_value(0x80010103), 0x10003);     /* P3 */
	CHECK_EQ(apple_ipi_value(0x80010103) & (3ull << 28), 0);   /* type = immediate */
	CHECK_EQ(apple_ipi_value(0x80FF01FF), (0x01ull << 16) | 0xFF);   /* Aff2/bit 31 are ignored */
}

static void t_ipi_send_one_write_global_only(void)
{
	fresh();
	apple_ipi_send(&C, 0x80010102);                     /* E -> P */
	CHECK_EQ(FS.ipi_rr_writes, 1);
	CHECK_EQ(FS.ipi_rr_last, 0x10002);
	CHECK_EQ(FS.nlog, 1);                               /* nothing else touched (no local form, no read) */
	apple_ipi_send(&C, 0x80000001);                     /* P -> E, E -> E look the same to the sender */
	CHECK_EQ(FS.ipi_rr_last, 0x00001);
	CHECK_EQ(FS.ipi_rr_writes, 2);
}

/* ---- init ---- */

static void t_init_resets_timers_before_gate(void)
{
	fresh();
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;                   /* enabled, firing, unmasked */
	FS.r[APPLE_SR_CNTP_CTL_EL02] = 7;
	FS.r[APPLE_SR_VM_TMR_FIQ_ENA] = 3;                  /* left open by a previous life of the core */
	FS.r[APPLE_SR_PMCR0] = (4ull << PMCR0_IMODE_SHIFT) | PMCR0_IACT | 0xFF;
	FS.r[APPLE_SR_UPMCR0] = (UPMCR0_IMODE_FIQ << UPMCR0_IMODE_SHIFT) | 0x1;
	FS.r[APPLE_SR_IPI_SR] = IPI_SR_PENDING;
	apple_fiq_init_cpu(&C);

	int gate_close = fs_first_write(&FS, APPLE_SR_VM_TMR_FIQ_ENA, ~0ull, 0);
	int gate_open = fs_first_write(&FS, APPLE_SR_VM_TMR_FIQ_ENA, VM_TMR_FIQ_V | VM_TMR_FIQ_P, VM_TMR_FIQ_V | VM_TMR_FIQ_P);
	int v0 = fs_first_write(&FS, APPLE_SR_CNTV_CTL_EL02, ~0ull, 0);
	int p0 = fs_first_write(&FS, APPLE_SR_CNTP_CTL_EL02, ~0ull, 0);

	CHECK(gate_close >= 0 && v0 >= 0 && p0 >= 0 && gate_open >= 0);
	CHECK(gate_close < v0);                             /* the gate is shut while the controls are rewritten */
	CHECK(v0 < gate_open);                              /* M-3: CNTV_CTL_EL02 = 0 before the gate opens */
	CHECK(p0 < gate_open);                              /* ... and CNTP_CTL_EL02 */

	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);
	CHECK_EQ(FS.r[APPLE_SR_CNTV_CTL_EL02], 0);
	CHECK_EQ(FS.r[APPLE_SR_CNTP_CTL_EL02], 0);
	CHECK_EQ(FS.r[APPLE_SR_PMCR0], 0xFF);               /* IMODE and IACT cleared, the rest kept */
	CHECK_EQ(FS.r[APPLE_SR_UPMCR0], 0x1);
	CHECK_EQ(FS.r[APPLE_SR_IPI_SR], 0);
	CHECK(FS.r[APPLE_SR_CNTHV_CTL] & CNT_CTL_IMASK);
	CHECK(FS.r[APPLE_SR_CNTHP_CTL] & CNT_CTL_IMASK);

	/* "no timer injection follows": the first FIQ after init finds nothing, and the vGIC has nothing pending */
	FS.nlog = 0;
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK(!any_deliverable());
	CHECK_EQ(C.st.vtimer, 0);
	CHECK_EQ(C.st.ptimer, 0);
}

static void t_init_after_park_reentry(void)
{
	fresh();
	ready();
	/* the guest context on this core programs a timer and the core is parked with it running */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	FS.r[APPLE_SR_CNTP_CTL_EL02] = 5;
	apple_fiq_close_gates(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);
	CHECK(!C.vmasked && !C.pmasked);
	FS.nlog = 0;
	apple_fiq_init_cpu(&C);                             /* the re-entry runs the init again */
	CHECK(fs_first_write(&FS, APPLE_SR_CNTV_CTL_EL02, ~0ull, 0) < fs_first_write(&FS, APPLE_SR_VM_TMR_FIQ_ENA, 3, 3));
	CHECK(fs_first_write(&FS, APPLE_SR_CNTP_CTL_EL02, ~0ull, 0) < fs_first_write(&FS, APPLE_SR_VM_TMR_FIQ_ENA, 3, 3));
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK(!any_deliverable());
}

/* ---- every cause alone ---- */

static void t_cause_vtimer(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_VTIMER);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], VM_TMR_FIQ_P);   /* V gate closed, P untouched */
	CHECK(C.vmasked && !C.pmasked);
	CHECK_EQ(FS.r[APPLE_SR_CNTV_CTL_EL02], 5);          /* the guest's control is not modified... */
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_CNTV_CTL_EL02), 0);   /* ... and not even written */
	CHECK(deliverable(APPLE_PPI_VTIMER));               /* vPPI 27 injected on this vCPU */
	CHECK_EQ(C.st.vtimer, 1);
}

static void t_cause_ptimer(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_CNTP_CTL_EL02] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_PTIMER);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], VM_TMR_FIQ_V);
	CHECK(C.pmasked && !C.vmasked);
	CHECK(deliverable(APPLE_PPI_PTIMER));               /* vPPI 30 */
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_CNTP_CTL_EL02), 0);
}

static void t_timer_not_a_cause(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 7;                   /* enabled, firing, but IMASK */
	FS.r[APPLE_SR_CNTP_CTL_EL02] = 1;                   /* enabled, not expired */
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);
	/* a firing timer whose gate is closed cannot have raised this FIQ */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	FS.r[APPLE_SR_VM_TMR_FIQ_ENA] = VM_TMR_FIQ_P;
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK(!any_deliverable());
}

static void t_cause_ipi(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_IPI_SR] = IPI_SR_PENDING;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_IPI);
	CHECK_EQ(FS.r[APPLE_SR_IPI_SR], 0);                 /* acknowledged */
	CHECK_EQ(ipi_work, 1);
	CHECK_EQ(C.st.ipi, 1);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);         /* the IPI leaves the gates alone */
	/* the ack comes before the work (an IPI sent during the work must stay pending) */
	int ack = fs_first_write(&FS, APPLE_SR_IPI_SR, 1, 1);
	CHECK(ack >= 0);
	/* a second FIQ with no IPI pending is spurious, not an IPI */
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK_EQ(ipi_work, 1);
}

static void t_cause_maintenance(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_ICH_HCR] = ICH_HCR_EN | 0x6;          /* En | UIE | LRENPIE */
	FS.misr_cond = 0x2;                                 /* an underflow condition the flush could not remove */
	fs_misr_refresh(&FS);
	CHECK(apple_maint_pending(&C));
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_MAINT);
	CHECK_EQ(C.st.maint, 1);
	/* the enables are cleared once, MISR follows, En stays, no loop */
	CHECK_EQ(apple_maint_backstop(&C), APPLE_MAINT_QUENCHED);
	CHECK_EQ(FS.r[APPLE_SR_ICH_HCR] & ICH_HCR_MAINT_EN, 0);
	CHECK(FS.r[APPLE_SR_ICH_HCR] & ICH_HCR_EN);
	CHECK_EQ(C.st.maint_quenched, 1);
	unsigned w = fs_writes_to(&FS, APPLE_SR_ICH_HCR);
	CHECK_EQ(apple_maint_backstop(&C), APPLE_MAINT_OK);   /* nothing left */
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_ICH_HCR), w);
	/* En clear: MISR is not looked at as a cause */
	FS.r[APPLE_SR_ICH_HCR] = 0;
	FS.misr_cond = 0;
	FS.r[APPLE_SR_ICH_MISR] = 0x2;
	CHECK(!apple_maint_pending(&C));
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK_EQ(apple_maint_backstop(&C), APPLE_MAINT_OK);
}

/* D-2: MISR.EOI (bit 0) is driven by the list registers and has no enable: clearing [7:1] cannot silence it. */
static void t_maintenance_eoi_only_is_fatal_not_a_loop(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_ICH_HCR] = ICH_HCR_EN | 0xFE | 0x400;   /* En | all enables | TC */
	FS.misr_cond = 0x1;                                 /* EOI only */
	fs_misr_refresh(&FS);
	CHECK_EQ(FS.r[APPLE_SR_ICH_MISR], 1);
	CHECK_EQ(apple_maint_backstop(&C), APPLE_MAINT_FATAL);
	CHECK_EQ(FS.r[APPLE_SR_ICH_HCR] & (ICH_HCR_EN | ICH_HCR_MAINT_EN), 0);   /* En = 0 as the last resort */
	CHECK(FS.r[APPLE_SR_ICH_HCR] & 0x400);              /* the trap bits are left alone */
	CHECK_EQ(C.st.maint_fatal, 1);
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_ICH_HCR), 2);   /* exactly: clear enables, then clear En; no loop */
	CHECK_EQ(apple_maint_backstop(&C), APPLE_MAINT_OK); /* En is off now: nothing more to do */
}

static void t_maintenance_resolved_by_flush(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_ICH_HCR] = ICH_HCR_EN | 0x2;
	FS.r[APPLE_SR_ICH_MISR] = 0x2;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_MAINT);
	FS.r[APPLE_SR_ICH_MISR] = 0;                        /* the flush harvested */
	CHECK_EQ(apple_maint_backstop(&C), APPLE_MAINT_OK);
	CHECK_EQ(FS.r[APPLE_SR_ICH_HCR], ICH_HCR_EN | 0x2); /* untouched */
}

/* D-1: the same check from the IRQ tail (vector 9): no FIQ handler involved. */
static void t_maintenance_from_irq_tail(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_ICH_HCR] = ICH_HCR_EN | 0x8;
	FS.misr_cond = 0x8;
	fs_misr_refresh(&FS);
	CHECK(apple_maint_pending(&C));
	CHECK_EQ(C.st.fiq, 0);
	CHECK_EQ(apple_maint_backstop(&C), APPLE_MAINT_QUENCHED);
	CHECK(!apple_maint_pending(&C));
	CHECK_EQ(C.st.fiq, 0);
}

static void t_cause_pmu(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_PMCR0] = (4ull << PMCR0_IMODE_SHIFT) | PMCR0_IACT | 0x3;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_PMU);
	CHECK_EQ(FS.r[APPLE_SR_PMCR0], 0x3);                /* switched off */
	CHECK_EQ(C.st.pmu, 1);
	CHECK_EQ(apple_fiq_handle(&C), 0);                  /* and quiet afterwards */
}

static void t_cause_uncore_pmu(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_UPMCR0] = (UPMCR0_IMODE_FIQ << UPMCR0_IMODE_SHIFT) | 0x1;
	FS.r[APPLE_SR_UPMSR] = UPMSR_IACT;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_UPMU);
	CHECK_EQ(FS.r[APPLE_SR_UPMCR0], 0x1);
	/* IACT without IMODE = FIQ, or IMODE = FIQ without IACT: not this source */
	FS.r[APPLE_SR_UPMCR0] = 2ull << UPMCR0_IMODE_SHIFT;
	CHECK_EQ(apple_fiq_handle(&C), 0);
	FS.r[APPLE_SR_UPMCR0] = UPMCR0_IMODE_FIQ << UPMCR0_IMODE_SHIFT;
	FS.r[APPLE_SR_UPMSR] = 0;
	CHECK_EQ(apple_fiq_handle(&C), 0);
}

static void t_cause_own_timers(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_CNTHV_CTL] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_HVTIMER);
	CHECK_EQ(FS.r[APPLE_SR_CNTHV_CTL], 7);              /* IMASK written */
	FS.r[APPLE_SR_CNTHP_CTL] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_HPTIMER);
	CHECK_EQ(FS.r[APPLE_SR_CNTHP_CTL], 7);
	CHECK_EQ(C.st.hvtimer, 1);
	CHECK_EQ(C.st.hptimer, 1);
	CHECK(!any_deliverable());                          /* EL2's own timers never reach the guest */
}

/* ---- combined and interleaved ---- */

static void t_combined_causes(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	FS.r[APPLE_SR_CNTP_CTL_EL02] = 5;
	FS.r[APPLE_SR_IPI_SR] = IPI_SR_PENDING;
	FS.r[APPLE_SR_PMCR0] = (4ull << PMCR0_IMODE_SHIFT) | PMCR0_IACT;
	FS.r[APPLE_SR_ICH_HCR] = ICH_HCR_EN | 0x2;
	FS.r[APPLE_SR_ICH_MISR] = 2;
	FS.r[APPLE_SR_CNTHP_CTL] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_VTIMER | APPLE_FIQ_PTIMER | APPLE_FIQ_IPI | APPLE_FIQ_MAINT |
				       APPLE_FIQ_PMU | APPLE_FIQ_HPTIMER);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);
	CHECK_EQ(ipi_work, 1);
	CHECK_EQ(C.spurious_run, 0);
	CHECK_EQ(C.st.fiq, 1);
	/* both vPPIs are pending in the model: 27 wins on INTID at equal priority */
	CHECK(deliverable(APPLE_PPI_VTIMER));
}

static void t_unmask_protocol(void)
{
	fresh();
	ready();
	/* 1. the timer fires: gate closed, line raised */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_VTIMER);
	CHECK(deliverable(APPLE_PPI_VTIMER));
	/* 2. flush while the guest has not touched the timer: the line stays high, the gate stays closed */
	apple_timer_resync(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA] & VM_TMR_FIQ_V, 0);
	CHECK(C.vmasked);
	CHECK(deliverable(APPLE_PPI_VTIMER));
	/* a spurious-looking second FIQ cannot happen with the gate closed: nothing is found */
	CHECK_EQ(apple_fiq_handle(&C), 0);
	/* 3. the guest handles it and re-arms (cval in the future: ISTATUS clear, still enabled and unmasked) */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 1;
	apple_timer_resync(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA] & VM_TMR_FIQ_V, VM_TMR_FIQ_V);   /* unmasked */
	CHECK(!C.vmasked);
	CHECK(!any_deliverable());                          /* the level line is low again */
	/* 4. it fires again: the cycle repeats */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_VTIMER);
	CHECK(deliverable(APPLE_PPI_VTIMER));
	CHECK_EQ(C.st.vtimer, 2);
	/* 5. the guest disables the timer instead */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 0;
	apple_timer_resync(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);
	CHECK(!any_deliverable());
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_CNTV_CTL_EL02), 0);   /* the module never wrote the guest's control */
}

static void t_resync_premasks_a_fired_timer(void)
{
	fresh();
	ready();
	/* the timer expired while EL2 was running (F masked): the flush sees the line high before the FIQ is taken */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	apple_timer_resync(&C);
	CHECK(deliverable(APPLE_PPI_VTIMER));
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], VM_TMR_FIQ_P);   /* no FIQ bounce is left behind */
	CHECK(C.vmasked);
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 3;                   /* guest masks it (IMASK) */
	apple_timer_resync(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);
}

static void t_interleaved_timers_and_ipi(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_VTIMER);
	/* P timer and an IPI arrive while V is masked */
	FS.r[APPLE_SR_CNTP_CTL_EL02] = 5;
	FS.r[APPLE_SR_IPI_SR] = IPI_SR_PENDING;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_PTIMER | APPLE_FIQ_IPI);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);
	/* V is silenced, P is not: only V reopens */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 0;
	apple_timer_resync(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], VM_TMR_FIQ_V);
	CHECK(C.pmasked && !C.vmasked);
	FS.r[APPLE_SR_CNTP_CTL_EL02] = 1;
	apple_timer_resync(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);
	CHECK(!any_deliverable());
	CHECK_EQ(ipi_work, 1);
	CHECK_EQ(C.st.fiq, 2);
	CHECK_EQ(C.st.spurious, 0);
}

static void t_mask_unmask_primitives(void)
{
	fresh();
	ready();
	apple_timer_mask(&C, APPLE_PPI_VTIMER);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], VM_TMR_FIQ_P);
	apple_timer_mask(&C, APPLE_PPI_PTIMER);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);
	apple_timer_unmask(&C, APPLE_PPI_PTIMER);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], VM_TMR_FIQ_P);
	apple_timer_unmask(&C, APPLE_PPI_VTIMER);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);
	CHECK(!C.vmasked && !C.pmasked);
}

/* ---- spurious budget ---- */

static void t_spurious_budget(void)
{
	fresh();
	ready();
	C.spurious_budget = 3;
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK_EQ(C.st.spurious, 3);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);         /* still tolerated */
	FS.r[APPLE_SR_ICH_HCR] = ICH_HCR_EN;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_STORM);    /* the fourth in a row */
	CHECK_EQ(C.st.storms, 1);
	CHECK_EQ(C.st.spurious_max_run, 4);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);         /* everything masked for the report */
	CHECK_EQ(FS.r[APPLE_SR_ICH_HCR] & ICH_HCR_EN, 0);
	CHECK(FS.r[APPLE_SR_CNTHV_CTL] & CNT_CTL_IMASK);
	CHECK_EQ(FS.r[APPLE_SR_PMCR0] & (PMCR0_IMODE_MASK | PMCR0_IACT), 0);
}

static void t_spurious_run_resets_on_a_real_cause(void)
{
	fresh();
	ready();
	C.spurious_budget = 3;
	for (int round = 0; round < 5; round++) {
		CHECK_EQ(apple_fiq_handle(&C), 0);
		CHECK_EQ(apple_fiq_handle(&C), 0);
		FS.r[APPLE_SR_IPI_SR] = IPI_SR_PENDING;
		CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_IPI);  /* a real cause ends the run */
	}
	CHECK_EQ(C.st.storms, 0);
	CHECK_EQ(C.st.spurious, 10);
	CHECK_EQ(C.st.spurious_max_run, 2);
}

static void t_spurious_default_budget(void)
{
	fresh();
	ready();
	for (unsigned i = 0; i < APPLE_FIQ_SPURIOUS_BUDGET; i++)
		CHECK_EQ(apple_fiq_handle(&C), 0);
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_STORM);
	/* init (re-entry) starts a new count */
	apple_fiq_init_cpu(&C);
	CHECK_EQ(C.spurious_run, 0);
	CHECK_EQ(apple_fiq_handle(&C), 0);
}

static void t_pmu_needs_imode_fiq(void)
{
	fresh();
	ready();
	FS.r[APPLE_SR_PMCR0] = PMCR0_IACT;                  /* IACT without IMODE = FIQ: not an FIQ source */
	CHECK_EQ(apple_fiq_handle(&C), 0);
	FS.r[APPLE_SR_PMCR0] = 4ull << PMCR0_IMODE_SHIFT;   /* IMODE = FIQ without IACT: nothing pending */
	CHECK_EQ(apple_fiq_handle(&C), 0);
	FS.r[APPLE_SR_PMCR0] = (2ull << PMCR0_IMODE_SHIFT) | PMCR0_IACT;   /* IMODE = AIC */
	CHECK_EQ(apple_fiq_handle(&C), 0);
}

static void t_stuck_ipi_is_a_storm(void)
{
	fresh();
	ready();
	C.spurious_budget = 3;
	FS.stuck_ipi = true;
	FS.r[APPLE_SR_IPI_SR] = IPI_SR_PENDING;             /* the ack does not clear it */
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_IPI);
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_IPI);
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_IPI);
	CHECK_EQ(C.st.ipi_stuck, 3);
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_STORM);    /* the fourth in a row */
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);
	/* a healthy IPI stream is not a storm, however long */
	fresh();
	ready();
	C.spurious_budget = 3;
	for (int i = 0; i < 500; i++) {
		FS.r[APPLE_SR_IPI_SR] = IPI_SR_PENDING;
		CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_IPI);
	}
	CHECK_EQ(C.st.storms, 0);
	CHECK_EQ(C.st.ipi_stuck, 0);
}

static void t_stuck_pmu_is_a_storm(void)
{
	fresh();
	ready();
	C.spurious_budget = 2;
	FS.stuck_pmcr0 = true;
	FS.r[APPLE_SR_PMCR0] = (4ull << PMCR0_IMODE_SHIFT) | PMCR0_IACT;   /* switching it off does not stick */
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_PMU);
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_PMU);
	CHECK_EQ(C.st.pmu_stuck, 2);
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_STORM);
	CHECK_EQ(C.st.storms, 1);
}

static void t_same_cause_budget(void)
{
	fresh();
	ready();
	C.same_budget = 4;
	/* EL2's own timer "fires" again after every IMASK write (a model of a source that cannot be quieted) */
	for (int i = 0; i < 4; i++) {
		FS.r[APPLE_SR_CNTHP_CTL] = 5;
		CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_HPTIMER);
	}
	FS.r[APPLE_SR_CNTHP_CTL] = 5;
	CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_STORM);
	/* a different cause in between restarts the count */
	fresh();
	ready();
	C.same_budget = 4;
	for (int i = 0; i < 20; i++) {
		FS.r[APPLE_SR_CNTHP_CTL] = 5;
		CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_HPTIMER);
		FS.r[APPLE_SR_CNTHV_CTL] = 5;
		CHECK_EQ(apple_fiq_handle(&C), APPLE_FIQ_HVTIMER);
	}
	CHECK_EQ(C.st.storms, 0);
}

static void t_closed_gates_stay_closed(void)
{
	fresh();
	ready();
	apple_fiq_close_gates(&C);                          /* park */
	unsigned gw = fs_writes_to(&FS, APPLE_SR_VM_TMR_FIQ_ENA);
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 5;                   /* a running timer on the parked core */
	apple_timer_resync(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);
	CHECK_EQ(fs_writes_to(&FS, APPLE_SR_VM_TMR_FIQ_ENA), gw);   /* resync wrote nothing */
	FS.r[APPLE_SR_CNTV_CTL_EL02] = 0;                   /* ... and silenced: still must not reopen */
	apple_timer_resync(&C);
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 0);
	CHECK(!any_deliverable());
	apple_fiq_init_cpu(&C);                             /* only the re-entry init reopens them */
	CHECK_EQ(FS.r[APPLE_SR_VM_TMR_FIQ_ENA], 3);
}

static void t_init_ich_hcr_off_first(void)
{
	fresh();
	FS.r[APPLE_SR_ICH_HCR] = ICH_HCR_EN | 0x6;
	apple_fiq_init_cpu(&C);
	CHECK_EQ(FS.r[APPLE_SR_ICH_HCR], 0);
	CHECK_EQ(FS.log[0].id, APPLE_SR_ICH_HCR);            /* the very first access is the write of 0 */
	CHECK(FS.log[0].write);
	CHECK_EQ(FS.log[0].val, 0);
}

int main(void)
{
	RUN(t_ipi_value);
	RUN(t_ipi_send_one_write_global_only);
	RUN(t_init_resets_timers_before_gate);
	RUN(t_init_after_park_reentry);
	RUN(t_cause_vtimer);
	RUN(t_cause_ptimer);
	RUN(t_timer_not_a_cause);
	RUN(t_cause_ipi);
	RUN(t_cause_maintenance);
	RUN(t_maintenance_eoi_only_is_fatal_not_a_loop);
	RUN(t_maintenance_resolved_by_flush);
	RUN(t_maintenance_from_irq_tail);
	RUN(t_pmu_needs_imode_fiq);
	RUN(t_stuck_ipi_is_a_storm);
	RUN(t_stuck_pmu_is_a_storm);
	RUN(t_same_cause_budget);
	RUN(t_closed_gates_stay_closed);
	RUN(t_init_ich_hcr_off_first);
	RUN(t_cause_pmu);
	RUN(t_cause_uncore_pmu);
	RUN(t_cause_own_timers);
	RUN(t_combined_causes);
	RUN(t_unmask_protocol);
	RUN(t_resync_premasks_a_fired_timer);
	RUN(t_interleaved_timers_and_ipi);
	RUN(t_mask_unmask_primitives);
	RUN(t_spurious_budget);
	RUN(t_spurious_run_resets_on_a_real_cause);
	RUN(t_spurious_default_budget);
	TESTS_DONE();
}
