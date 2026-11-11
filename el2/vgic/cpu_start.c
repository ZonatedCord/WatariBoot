/* SPDX-License-Identifier: MIT */
/* Watari EL2 - M1 PSCI back end (spin-table start, park, watchdog reset). See wel2_cpu_start.h.
 * No smc anywhere in this file. */
#include "wel2_cpu_start.h"

#define MPIDR_AFF_MASK 0x000000ff00ffffffULL

unsigned wel2_cs_find(const struct wel2_cs *cs, uint64_t mpidr)
{
	mpidr &= MPIDR_AFF_MASK;
	for (unsigned i = 0; i < cs->n; i++)
		if (cs->core[i].mpidr == mpidr)
			return i;
	return ~0u;
}

int wel2_cs_init(struct wel2_cs *cs, const struct wel2_cs_ops *ops, uint64_t secondary_entry,
		 const struct wel2_cs_cpu *cpus, unsigned n, unsigned boot_idx)
{
	if (!cs || !ops || !cpus || !secondary_entry || n == 0 || n > WEL2_CS_MAX || boot_idx >= n)
		return -1;
	if (!ops->write64 || !ops->dc_cvac || !ops->dsb || !ops->sev || !ops->wfe || !ops->wake_store ||
	    !ops->wake_load || !ops->timers_reset || !ops->mmio_write32)
		return -1;
	cs->ops = ops;
	cs->secondary_entry = secondary_entry;
	cs->n = n;
	for (unsigned i = 0; i < n; i++) {
		struct wel2_cs_core *c = &cs->core[i];
		c->mpidr = cpus[i].mpidr & MPIDR_AFF_MASK;
		c->release_addr = cpus[i].release_addr;
		c->state = WEL2_CS_OFF;
		c->released = c->parked = false;
		c->entry = c->ctx = 0;
		for (unsigned j = 0; j < i; j++)
			if (cs->core[j].mpidr == c->mpidr)
				return -1;
	}
	cs->core[boot_idx].state = WEL2_CS_ON;
	cs->core[boot_idx].released = true;   /* already running: never started through the spin table */
	return 0;
}

int wel2_cs_cpu_on(struct wel2_cs *cs, uint64_t target_mpidr, uint64_t entry, uint64_t ctx)
{
	const struct wel2_cs_ops *o = cs->ops;
	unsigned idx;
	struct wel2_cs_core *c;

	if (target_mpidr & ~MPIDR_AFF_MASK)
		return WEL2_CS_INVALID_PARAMETERS;
	idx = wel2_cs_find(cs, target_mpidr);
	if (idx == ~0u)
		return WEL2_CS_INVALID_PARAMETERS;
	c = &cs->core[idx];
	if (c->state == WEL2_CS_ON)
		return WEL2_CS_ALREADY_ON;
	if (c->state == WEL2_CS_PENDING)
		return WEL2_CS_ON_PENDING;

	if (c->parked) {
		/* Wake path: the core is inside the layer, MMU on. Publish entry/context before the flag. */
		c->entry = entry;
		c->ctx = ctx;
		c->state = WEL2_CS_PENDING;
		c->parked = false;
		o->dsb(o->ctx);
		o->wake_store(o->ctx, idx, 1);
		o->dsb(o->ctx);
		o->sev(o->ctx);
		return WEL2_CS_SUCCESS;
	}

	/* First start through the spin table: the word must be 8-byte aligned (one cache line, one store). */
	if (c->released || !c->release_addr || (c->release_addr & 7))
		return WEL2_CS_INTERNAL_FAILURE;
	c->entry = entry;
	c->ctx = ctx;
	c->state = WEL2_CS_PENDING;
	c->released = true;
	o->write64(o->ctx, c->release_addr, cs->secondary_entry);
	o->dc_cvac(o->ctx, c->release_addr);
	o->dsb(o->ctx);
	o->sev(o->ctx);
	return WEL2_CS_SUCCESS;
}

int wel2_cs_cpu_off(struct wel2_cs *cs, unsigned me)
{
	unsigned others = 0;

	if (me >= cs->n || cs->core[me].state != WEL2_CS_ON)
		return WEL2_CS_INVALID_PARAMETERS;
	for (unsigned i = 0; i < cs->n; i++)
		if (i != me && cs->core[i].state != WEL2_CS_OFF)
			others++;
	if (!others)
		return WEL2_CS_DENIED;   /* nothing would ever wake the last core (F-14) */
	cs->core[me].state = WEL2_CS_OFF;
	cs->core[me].parked = true;
	return WEL2_CS_SUCCESS;
}

bool wel2_cs_park(struct wel2_cs *cs, unsigned me, unsigned max_wfe)
{
	const struct wel2_cs_ops *o = cs->ops;
	unsigned n = 0;

	while (!o->wake_load(o->ctx, me)) {
		if (max_wfe && n++ >= max_wfe)
			return false;
		o->wfe(o->ctx);
	}
	o->wake_store(o->ctx, me, 0);
	o->timers_reset(o->ctx);       /* before anything else (M-3) */
	return true;
}

bool wel2_cs_enter(struct wel2_cs *cs, unsigned me, uint64_t *entry, uint64_t *ctx)
{
	struct wel2_cs_core *c;

	if (me >= cs->n)
		return false;
	c = &cs->core[me];
	if (c->state != WEL2_CS_PENDING)
		return false;
	*entry = c->entry;
	*ctx = c->ctx;
	c->state = WEL2_CS_ON;
	return true;
}

int wel2_cs_affinity(const struct wel2_cs *cs, uint64_t mpidr)
{
	unsigned idx;

	if (mpidr & ~MPIDR_AFF_MASK)
		return WEL2_CS_INVALID_PARAMETERS;
	idx = wel2_cs_find(cs, mpidr);
	if (idx == ~0u)
		return WEL2_CS_INVALID_PARAMETERS;
	switch (cs->core[idx].state) {
	case WEL2_CS_ON:      return WEL2_CS_AFF_ON;
	case WEL2_CS_PENDING: return WEL2_CS_AFF_PENDING;
	default:              return WEL2_CS_AFF_OFF;
	}
}

int wel2_cs_system_reset(const struct wel2_cs_ops *o, uint64_t wdt_base)
{
	if (!wdt_base)
		return -1;
	o->mmio_write32(o->ctx, wdt_base + WEL2_WDT_ALARM, WEL2_WDT_ALARM_VALUE);
	o->mmio_write32(o->ctx, wdt_base + WEL2_WDT_COUNT, 0);
	o->mmio_write32(o->ctx, wdt_base + WEL2_WDT_CTL, WEL2_WDT_CTL_RESET);
	o->dsb(o->ctx);
	return 0;
}

void wel2_cs_wait_forever(const struct wel2_cs_ops *o, unsigned max)
{
	for (unsigned n = 0; !max || n < max; n++)
		o->wfe(o->ctx);
}
