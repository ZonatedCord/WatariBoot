/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - Apple FIQ module (PLAN S10 / T6e). See include/wel2_apple_fiq.h for the protocol and
 * docs/design/watari-el2-m1boot/DESIGN.md 5.1/5.2 for the design. Pure C, no system register access except through
 * struct apple_sysreg_io, so the host test drives it with a fake register file.
 *
 * Written from the register descriptions (encodings and bit meanings are facts; sources in wel2_apple_sysreg.h);
 * no code from Linux, m1n1 or KVM is copied.
 */
#include "wel2_apple_fiq.h"

static inline uint64_t rd(struct apple_fiq_cpu *c, enum apple_sr id) { return c->io->rd(c->io->ctx, id); }
static inline void wr(struct apple_fiq_cpu *c, enum apple_sr id, uint64_t v) { c->io->wr(c->io->ctx, id, v); }

uint64_t apple_ipi_value(uint64_t target_mpidr)
{
	uint64_t aff0 = target_mpidr & 0xff;
	uint64_t aff1 = (target_mpidr >> 8) & 0xff;

	/* Aff2 is not used: on the j293 the P cores have Aff2 = 1 only because of the MPIDR layout
	 * [verified: R:re/m3a-ehw0-measurements.md "Every core, measured"], and the cluster field is Aff1
	 * [verified: Linux v6.12 irq-apple-aic.c:765-772]. Type [29:28] = 0 = immediate. */
	return aff0 | (aff1 << 16);
}

void apple_ipi_send(struct apple_fiq_cpu *c, uint64_t target_mpidr)
{
	wr(c, APPLE_SR_IPI_RR_GLOBAL, apple_ipi_value(target_mpidr));
}

void apple_timer_mask(struct apple_fiq_cpu *c, unsigned ppi)
{
	uint64_t bit = ppi == APPLE_PPI_PTIMER ? VM_TMR_FIQ_P : VM_TMR_FIQ_V;

	wr(c, APPLE_SR_VM_TMR_FIQ_ENA, rd(c, APPLE_SR_VM_TMR_FIQ_ENA) & ~bit);
	if (ppi == APPLE_PPI_PTIMER)
		c->pmasked = true;
	else
		c->vmasked = true;
}

void apple_timer_unmask(struct apple_fiq_cpu *c, unsigned ppi)
{
	uint64_t bit = ppi == APPLE_PPI_PTIMER ? VM_TMR_FIQ_P : VM_TMR_FIQ_V;

	wr(c, APPLE_SR_VM_TMR_FIQ_ENA, rd(c, APPLE_SR_VM_TMR_FIQ_ENA) | bit);
	if (ppi == APPLE_PPI_PTIMER)
		c->pmasked = false;
	else
		c->vmasked = false;
}

void apple_fiq_close_gates(struct apple_fiq_cpu *c)
{
	wr(c, APPLE_SR_VM_TMR_FIQ_ENA, 0);
	c->vmasked = c->pmasked = false;       /* closed on purpose: resync must not reopen them */
}

static void quiesce_pmu(struct apple_fiq_cpu *c)
{
	uint64_t pm = rd(c, APPLE_SR_PMCR0);
	uint64_t up = rd(c, APPLE_SR_UPMCR0);

	wr(c, APPLE_SR_PMCR0, pm & ~(PMCR0_IMODE_MASK | PMCR0_IACT));
	wr(c, APPLE_SR_UPMCR0, up & ~UPMCR0_IMODE_MASK);
}

static void mask_own_timers(struct apple_fiq_cpu *c)
{
	wr(c, APPLE_SR_CNTHV_CTL, rd(c, APPLE_SR_CNTHV_CTL) | CNT_CTL_IMASK);
	wr(c, APPLE_SR_CNTHP_CTL, rd(c, APPLE_SR_CNTHP_CTL) | CNT_CTL_IMASK);
}

void apple_fiq_init_cpu(struct apple_fiq_cpu *c)
{
	/* Close the gates first: whatever state the core was left in, no guest-timer FIQ can be raised while the
	 * controls below are rewritten. */
	wr(c, APPLE_SR_VM_TMR_FIQ_ENA, 0);
	quiesce_pmu(c);
	wr(c, APPLE_SR_IPI_SR, IPI_SR_PENDING);          /* write 1 clears a pending fast IPI */
	mask_own_timers(c);
	/* Review M-3: the guest timer controls are reset BEFORE the gates open. m1n1 leaves them at 7, a core
	 * back from CPU_OFF carries its last guest context's value; an enabled, unmasked, expired timer behind an
	 * open gate would inject an interrupt the new guest context never asked for. */
	wr(c, APPLE_SR_CNTV_CTL_EL02, 0);
	wr(c, APPLE_SR_CNTP_CTL_EL02, 0);
	wr(c, APPLE_SR_VM_TMR_FIQ_ENA, VM_TMR_FIQ_V | VM_TMR_FIQ_P);
	c->vmasked = c->pmasked = false;
	c->spurious_run = 0;
}

static void storm_mask_everything(struct apple_fiq_cpu *c)
{
	apple_fiq_close_gates(c);
	quiesce_pmu(c);
	wr(c, APPLE_SR_IPI_SR, IPI_SR_PENDING);
	mask_own_timers(c);
	wr(c, APPLE_SR_ICH_HCR, rd(c, APPLE_SR_ICH_HCR) & ~(ICH_HCR_EN | ICH_HCR_MAINT_EN));
}

static unsigned guest_timer(struct apple_fiq_cpu *c, uint64_t gate, uint64_t gate_bit, enum apple_sr ctl_id,
			    unsigned ppi, unsigned cause)
{
	if (!(gate & gate_bit) || !CNT_CTL_FIRING(rd(c, ctl_id)))
		return 0;
	apple_timer_mask(c, ppi);                        /* the guest's control register stays untouched */
	vgic_set_irq_level(c->vgic, c->vcpu, ppi, true);
	return cause;
}

unsigned apple_fiq_handle(struct apple_fiq_cpu *c)
{
	unsigned causes = 0;
	uint64_t gate = rd(c, APPLE_SR_VM_TMR_FIQ_ENA);
	uint64_t v;
	unsigned budget = c->spurious_budget ? c->spurious_budget : APPLE_FIQ_SPURIOUS_BUDGET;

	c->st.fiq++;

	/* 1. Guest timers: only a timer whose gate is open can have raised the FIQ. */
	causes |= guest_timer(c, gate, VM_TMR_FIQ_V, APPLE_SR_CNTV_CTL_EL02, APPLE_PPI_VTIMER, APPLE_FIQ_VTIMER);
	causes |= guest_timer(c, gate, VM_TMR_FIQ_P, APPLE_SR_CNTP_CTL_EL02, APPLE_PPI_PTIMER, APPLE_FIQ_PTIMER);

	/* 2. Fast IPI: acknowledge before the work, so an IPI sent meanwhile stays pending. */
	if (rd(c, APPLE_SR_IPI_SR) & IPI_SR_PENDING) {
		wr(c, APPLE_SR_IPI_SR, IPI_SR_PENDING);
		if (c->on_ipi)
			c->on_ipi(c->ctx);
		causes |= APPLE_FIQ_IPI;
	}

	/* 3. vGIC maintenance: the flush the caller runs next harvests it; apple_fiq_after_flush() is the backstop. */
	if ((rd(c, APPLE_SR_ICH_HCR) & ICH_HCR_EN) && rd(c, APPLE_SR_ICH_MISR) != 0)
		causes |= APPLE_FIQ_MAINT;

	/* 4. PMU sources are never wanted: switch the source off. */
	v = rd(c, APPLE_SR_PMCR0);
	if (v & PMCR0_IACT) {
		wr(c, APPLE_SR_PMCR0, v & ~(PMCR0_IMODE_MASK | PMCR0_IACT));
		causes |= APPLE_FIQ_PMU;
	}
	v = rd(c, APPLE_SR_UPMCR0);
	if ((((v & UPMCR0_IMODE_MASK) >> UPMCR0_IMODE_SHIFT) == UPMCR0_IMODE_FIQ) && (rd(c, APPLE_SR_UPMSR) & UPMSR_IACT)) {
		wr(c, APPLE_SR_UPMCR0, v & ~UPMCR0_IMODE_MASK);
		causes |= APPLE_FIQ_UPMU;
	}

	/* 5. EL2's own timers: not used, write IMASK. */
	v = rd(c, APPLE_SR_CNTHV_CTL);
	if (CNT_CTL_FIRING(v)) {
		wr(c, APPLE_SR_CNTHV_CTL, v | CNT_CTL_IMASK);
		causes |= APPLE_FIQ_HVTIMER;
	}
	v = rd(c, APPLE_SR_CNTHP_CTL);
	if (CNT_CTL_FIRING(v)) {
		wr(c, APPLE_SR_CNTHP_CTL, v | CNT_CTL_IMASK);
		causes |= APPLE_FIQ_HPTIMER;
	}

	if (causes & APPLE_FIQ_VTIMER) c->st.vtimer++;
	if (causes & APPLE_FIQ_PTIMER) c->st.ptimer++;
	if (causes & APPLE_FIQ_IPI) c->st.ipi++;
	if (causes & APPLE_FIQ_MAINT) c->st.maint++;
	if (causes & APPLE_FIQ_PMU) c->st.pmu++;
	if (causes & APPLE_FIQ_UPMU) c->st.upmu++;
	if (causes & APPLE_FIQ_HVTIMER) c->st.hvtimer++;
	if (causes & APPLE_FIQ_HPTIMER) c->st.hptimer++;

	/* 6. Nothing found: spurious, with a budget (F-14: no silent storm). */
	if (!causes) {
		c->st.spurious++;
		if (++c->spurious_run > c->st.spurious_max_run)
			c->st.spurious_max_run = c->spurious_run;
		if (c->spurious_run > budget) {
			c->st.storms++;
			storm_mask_everything(c);
			return APPLE_FIQ_STORM;
		}
	} else {
		c->spurious_run = 0;
	}
	return causes;
}

bool apple_fiq_after_flush(struct apple_fiq_cpu *c)
{
	uint64_t h = rd(c, APPLE_SR_ICH_HCR);

	if (!(h & ICH_HCR_EN) || rd(c, APPLE_SR_ICH_MISR) == 0)
		return false;
	/* The flush could not remove the cause: drop the enables that produce it (never loop). */
	wr(c, APPLE_SR_ICH_HCR, h & ~ICH_HCR_MAINT_EN);
	c->st.maint_quenched++;
	return true;
}

static void resync_one(struct apple_fiq_cpu *c, unsigned ppi, enum apple_sr ctl_id, bool *masked)
{
	bool line = CNT_CTL_FIRING(rd(c, ctl_id));

	vgic_set_irq_level(c->vgic, c->vcpu, ppi, line);
	if (line && !*masked)
		apple_timer_mask(c, ppi);        /* the FIQ would only bounce: the model already has the line */
	else if (!line && *masked)
		apple_timer_unmask(c, ppi);
}

void apple_timer_resync(struct apple_fiq_cpu *c)
{
	resync_one(c, APPLE_PPI_VTIMER, APPLE_SR_CNTV_CTL_EL02, &c->vmasked);
	resync_one(c, APPLE_PPI_PTIMER, APPLE_SR_CNTP_CTL_EL02, &c->pmasked);
}
