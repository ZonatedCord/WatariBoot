/* SPDX-License-Identifier: MIT */
/*
 * Host-test model of the Apple system registers used by the FIQ module (include/wel2_apple_sysreg.h): a register
 * file that records every access in order. Behaviour modelled: IPI_SR is write-1-to-clear (bit 0), UPMSR and
 * ICH_MISR are read-only (the test presets them); everything else stores what was written. ISTATUS in the timer
 * controls is whatever the test presets (the hardware computes it; the module never writes it meaningfully).
 */
#ifndef WEL2_FAKE_APPLE_SYSREG_H
#define WEL2_FAKE_APPLE_SYSREG_H

#include <string.h>
#include "wel2_apple_sysreg.h"

#define FS_LOG_MAX 256

struct fs_ev { bool write; enum apple_sr id; uint64_t val; };

struct fake_sysreg {
	uint64_t r[APPLE_SR_COUNT];
	struct fs_ev log[FS_LOG_MAX];
	unsigned nlog;
	unsigned ipi_rr_writes;
	uint64_t ipi_rr_last;
	unsigned nwr[APPLE_SR_COUNT];
	/* knobs: a source that does not go away when it is acknowledged / switched off */
	bool stuck_ipi, stuck_pmcr0;
	/* ICH_MISR model: when misr_cond is non-zero, MISR follows ICH_HCR: bit 0 (EOI) is unconditional (it has no
	 * enable), bits [7:1] need their enable bit in ICH_HCR. Otherwise MISR is whatever the test presets. */
	uint64_t misr_cond;
};

static inline void fs_misr_refresh(struct fake_sysreg *f)
{
	if (f->misr_cond)
		f->r[APPLE_SR_ICH_MISR] = (f->misr_cond & 1) | (f->misr_cond & f->r[APPLE_SR_ICH_HCR] & ICH_HCR_MAINT_EN);
}

static uint64_t fs_rd(void *ctx, enum apple_sr id)
{
	struct fake_sysreg *f = ctx;

	if (f->nlog < FS_LOG_MAX)
		f->log[f->nlog++] = (struct fs_ev){ false, id, f->r[id] };
	return id == APPLE_SR_IPI_RR_GLOBAL ? 0 : f->r[id];
}

static void fs_wr(void *ctx, enum apple_sr id, uint64_t v)
{
	struct fake_sysreg *f = ctx;

	if (f->nlog < FS_LOG_MAX)
		f->log[f->nlog++] = (struct fs_ev){ true, id, v };
	f->nwr[id]++;
	switch (id) {
	case APPLE_SR_IPI_SR:
		if (!f->stuck_ipi)
			f->r[id] &= ~(v & IPI_SR_PENDING);  /* write 1 to clear */
		break;
	case APPLE_SR_IPI_RR_GLOBAL:
		f->ipi_rr_writes++;
		f->ipi_rr_last = v;
		break;
	case APPLE_SR_UPMSR:
	case APPLE_SR_ICH_MISR:
		break;                                       /* read-only */
	case APPLE_SR_PMCR0:
		if (!f->stuck_pmcr0)
			f->r[id] = v;
		break;
	case APPLE_SR_ICH_HCR:
		f->r[id] = v;
		fs_misr_refresh(f);
		break;
	default:
		f->r[id] = v;
	}
}

static inline struct apple_sysreg_io fs_io(struct fake_sysreg *f)
{
	return (struct apple_sysreg_io){ fs_rd, fs_wr, f };
}

/* Index of the first logged write to `id` whose value satisfies (val & mask) == want, or -1. */
static inline int fs_first_write(const struct fake_sysreg *f, enum apple_sr id, uint64_t mask, uint64_t want)
{
	for (unsigned i = 0; i < f->nlog; i++)
		if (f->log[i].write && f->log[i].id == id && (f->log[i].val & mask) == want)
			return (int)i;
	return -1;
}

static inline unsigned fs_writes_to(const struct fake_sysreg *f, enum apple_sr id)
{
	return f->nwr[id];
}

#endif
