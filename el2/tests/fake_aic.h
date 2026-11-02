/* SPDX-License-Identifier: MIT */
/*
 * Host-test model of the Apple AIC (AICv1) at register level, for pirq_aic.c.
 * Offsets and the documented behaviour: include/wel2_aic.h. Behaviour that is a hypothesis
 * until E-HW0 on hardware is a knob here so the tests can exercise both answers:
 *   latch_pulses  an edge pulse is remembered while the source is masked (hypothesis)
 *   event_clears_sw  the EVENT read clears the SW_SET bit of the delivered source (hypothesis;
 *                    default off: the conservative reading, SW_CLR is explicit)
 * The model acts as one core at a time: set fa.cpu to the core that reads the registers.
 * "Wires": fa_line() drives a level input, fa_pulse() an edge pulse.
 */
#ifndef WEL2_FAKE_AIC_H
#define WEL2_FAKE_AIC_H

#include <string.h>
#include "wel2_aic.h"

#define FA_N      AIC_MAX_IRQS
#define FA_WORDS  (FA_N / 32)

struct fake_aic {
	uint32_t nr_irq;
	uint32_t mask[FA_WORDS];          /* 1 = masked; reset: all masked */
	uint32_t sw[FA_WORDS];
	uint32_t line[FA_WORDS];
	uint32_t latch[FA_WORDS];
	uint32_t target[FA_N];            /* bit n = core n; reset: core 0 */
	uint32_t ipi_pend[AIC_MAX_CPUS];  /* bit 0 other, bit 31 self, per core */
	uint32_t ipi_mask[AIC_MAX_CPUS];  /* reset: masked */
	unsigned cpu;                     /* core issuing the access ("this CPU" view) */
	bool latch_pulses;
	bool event_clears_sw;
	unsigned n_event, n_mask_set, n_mask_clr, n_target_wr;
	unsigned n_unknown_wr;            /* writes to offsets the model does not know */
	bool has_forced;                  /* the next EVENT read returns `forced` (odd events) */
	uint32_t forced;
};

static inline bool fa_bit(const uint32_t *a, uint32_t n) { return (a[n / 32] >> (n % 32)) & 1u; }

static inline void fa_reset(struct fake_aic *f)
{
	memset(f, 0, sizeof *f);
	f->nr_irq = 896 + 32 + 96;        /* 1024 */
	f->latch_pulses = true;
	for (unsigned i = 0; i < FA_WORDS; i++)
		f->mask[i] = 0xFFFFFFFFu;
	for (unsigned i = 0; i < FA_N; i++)
		f->target[i] = 1;
	for (unsigned c = 0; c < AIC_MAX_CPUS; c++)
		f->ipi_mask[c] = AIC_IPI_OTHER | AIC_IPI_SELF;
}

static inline void fa_line(struct fake_aic *f, uint32_t n, bool lvl)
{
	if (lvl)
		f->line[n / 32] |= 1u << (n % 32);
	else
		f->line[n / 32] &= ~(1u << (n % 32));
}

/* An edge pulse: remembered only if the model latches (and the source is masked or not, the
 * pending state is the same: it fires once when unmasked and targeted). */
static inline void fa_pulse(struct fake_aic *f, uint32_t n)
{
	bool was_masked = fa_bit(f->mask, n);

	if (f->latch_pulses || !was_masked)
		f->latch[n / 32] |= 1u << (n % 32);
}

static inline bool fa_irq_pending(const struct fake_aic *f, unsigned cpu, uint32_t *irq)
{
	for (uint32_t n = 0; n < f->nr_irq; n++) {
		uint32_t w = (f->line[n / 32] | f->sw[n / 32] | f->latch[n / 32]);

		if (((w >> (n % 32)) & 1u) && !fa_bit(f->mask, n) && (f->target[n] & (1u << cpu))) {
			if (irq)
				*irq = n;
			return true;
		}
	}
	return false;
}

/* The IRQ output of a core: unmasked pending IRQ or IPI. */
static inline bool fa_irq_out(const struct fake_aic *f, unsigned cpu)
{
	return fa_irq_pending(f, cpu, NULL) || (f->ipi_pend[cpu] & ~f->ipi_mask[cpu]);
}

static inline uint32_t fa_event(struct fake_aic *f)
{
	uint32_t n;
	uint32_t ipi = f->ipi_pend[f->cpu] & ~f->ipi_mask[f->cpu];

	f->n_event++;
	if (f->has_forced) {
		f->has_forced = false;
		return f->forced;
	}
	if (ipi) {                        /* order vs IRQs: hypothesis, IPIs first */
		uint32_t bit = (ipi & AIC_IPI_OTHER) ? AIC_IPI_OTHER : AIC_IPI_SELF;

		f->ipi_mask[f->cpu] |= bit;       /* the read masks it */
		return (AIC_EVENT_TYPE_IPI << 16) |
		       (bit == AIC_IPI_OTHER ? AIC_EVENT_IPI_OTHER : AIC_EVENT_IPI_SELF);
	}
	if (!fa_irq_pending(f, f->cpu, &n))
		return 0;
	f->mask[n / 32] |= 1u << (n % 32);        /* auto-mask */
	f->latch[n / 32] &= ~(1u << (n % 32));
	if (f->event_clears_sw)
		f->sw[n / 32] &= ~(1u << (n % 32));
	return (AIC_EVENT_TYPE_HW << 16) | n;
}

static inline uint32_t fa_rd32(void *ctx, uint32_t off)
{
	struct fake_aic *f = ctx;

	if (off == AIC_INFO)
		return f->nr_irq;
	if (off == AIC_WHOAMI)
		return f->cpu;
	if (off == AIC_EVENT)
		return fa_event(f);
	if (off >= 0x3000 && off < 0x3000 + 4 * FA_N)
		return f->target[(off - 0x3000) / 4];
	if (off >= 0x4000 && off < 0x4000 + 4 * FA_WORDS)
		return f->sw[(off - 0x4000) / 4];
	if (off >= 0x4100 && off < 0x4100 + 4 * FA_WORDS)
		return f->mask[(off - 0x4100) / 4];
	if (off >= 0x4180 && off < 0x4180 + 4 * FA_WORDS)
		return f->mask[(off - 0x4180) / 4];     /* Apple SET/CLR style: reads the state */
	if (off >= 0x4200 && off < 0x4200 + 4 * FA_WORDS)
		return f->line[(off - 0x4200) / 4];
	if (off == AIC_IPI_MASK_SET || off == AIC_IPI_MASK_CLR)
		return f->ipi_mask[f->cpu];
	return 0;
}

static inline void fa_wr32(void *ctx, uint32_t off, uint32_t v)
{
	struct fake_aic *f = ctx;

	if (off >= 0x3000 && off < 0x3000 + 4 * FA_N) {
		f->target[(off - 0x3000) / 4] = v;
		f->n_target_wr++;
	} else if (off >= 0x4000 && off < 0x4000 + 4 * FA_WORDS) {
		f->sw[(off - 0x4000) / 4] |= v;
	} else if (off >= 0x4080 && off < 0x4080 + 4 * FA_WORDS) {
		f->sw[(off - 0x4080) / 4] &= ~v;
	} else if (off >= 0x4100 && off < 0x4100 + 4 * FA_WORDS) {
		f->mask[(off - 0x4100) / 4] |= v;
		f->n_mask_set++;
	} else if (off >= 0x4180 && off < 0x4180 + 4 * FA_WORDS) {
		f->mask[(off - 0x4180) / 4] &= ~v;
		f->n_mask_clr++;
	} else if (off == AIC_IPI_SEND) {
		for (unsigned c = 0; c < AIC_MAX_CPUS; c++)
			if (v & (1u << c))
				f->ipi_pend[c] |= AIC_IPI_OTHER;
		if (v & AIC_IPI_SELF)
			f->ipi_pend[f->cpu] |= AIC_IPI_SELF;
	} else if (off == AIC_IPI_ACK) {
		f->ipi_pend[f->cpu] &= ~v;
	} else if (off == AIC_IPI_MASK_SET) {
		f->ipi_mask[f->cpu] |= v;
	} else if (off == AIC_IPI_MASK_CLR) {
		f->ipi_mask[f->cpu] &= ~v;
	} else {
		f->n_unknown_wr++;
	}
}

static inline struct aic_io fa_io(struct fake_aic *f)
{
	struct aic_io io = { fa_rd32, fa_wr32, f };

	return io;
}

#endif
