/* SPDX-License-Identifier: MIT */
/*
 * Host tests: interrupt map (hwirq <-> vINTID <-> GSIV, default-deny allow-list, MSI-pool
 * collision, trigger type) and the pirq policy on top of it (physical arrival -> vINTID,
 * level/edge semantics for a GIC-like and an AIC-like backend, re-target on CPU_OFF/CPU_ON).
 * Design: docs/design/watari-el2-devirq/ARCHITECTURE.md sections 4, 7.1 and TEST_STRATEGY 2.
 */
#include "test.h"
#include "fake_pirq.h"

#define T8103 (&INTR_MAP_T8103)

/* ---- pure map ----------------------------------------------------------------------- */

static void t_known_values(void)
{
	/* ARCHITECTURE section 6, "GSIV arithmetic example" */
	CHECK_EQ(intr_wired_vintid(T8103, 590), 622);   /* ANS NVMe */
	CHECK_EQ(intr_wired_vintid(T8103, 777), 809);   /* dwc3_0 */
	CHECK_EQ(intr_wired_vintid(T8103, 857), 889);   /* dwc3_1 */
	CHECK_EQ(intr_wired_vintid(T8103, 695), 727);   /* PCIe port 0 */
	CHECK_EQ(intr_wired_vintid(T8103, 0), 32);
	CHECK_EQ(intr_wired_vintid(T8103, 703), 735);
	CHECK_EQ(intr_wired_vintid(T8103, 736), 768);   /* first wired after the pool: still 32 + h */
	CHECK_EQ(intr_wired_vintid(T8103, 895), 927);
	CHECK_EQ(intr_wired_vintid(T8103, 896), INTR_INVALID);
	CHECK_EQ(intr_wired_vintid(T8103, 5000), INTR_INVALID);
	CHECK_EQ(intr_gsiv(T8103, 590), 622);
	CHECK_EQ(intr_gsiv(T8103, 704), 928);           /* a pool hwirq has a GSIV too, from the pool */
	CHECK_EQ(intr_gsiv(T8103, 896), INTR_INVALID);
}

static void t_collision_with_msi_pool(void)
{
	/* C4: 32 + pool hwirq (704..735) would be vINTID 736..767; nobody owns that hole. */
	for (uint32_t h = 704; h <= 735; h++) {
		CHECK_EQ(intr_wired_vintid(T8103, h), INTR_INVALID);
		CHECK_EQ(intr_msi_vintid(T8103, h), 928 + (h - 704));
	}
	CHECK_EQ(intr_msi_vintid(T8103, 703), INTR_INVALID);
	CHECK_EQ(intr_msi_vintid(T8103, 736), INTR_INVALID);
	for (uint32_t v = 736; v <= 767; v++)
		CHECK_EQ(intr_hwirq_of_vintid(T8103, v, NULL), INTR_INVALID);
}

static void t_inverse_boundaries(void)
{
	bool msi = true;

	CHECK_EQ(intr_hwirq_of_vintid(T8103, 31, &msi), INTR_INVALID);   /* PPI */
	CHECK(!msi);
	CHECK_EQ(intr_hwirq_of_vintid(T8103, 32, &msi), 0);
	CHECK(!msi);
	CHECK_EQ(intr_hwirq_of_vintid(T8103, 735, &msi), 703);
	CHECK_EQ(intr_hwirq_of_vintid(T8103, 768, &msi), 736);
	CHECK_EQ(intr_hwirq_of_vintid(T8103, 927, &msi), 895);
	CHECK(!msi);
	CHECK_EQ(intr_hwirq_of_vintid(T8103, 928, &msi), 704);
	CHECK(msi);
	CHECK_EQ(intr_hwirq_of_vintid(T8103, 959, &msi), 735);
	CHECK(msi);
	CHECK_EQ(intr_hwirq_of_vintid(T8103, 960, &msi), INTR_INVALID);
	CHECK(!msi);
	CHECK_EQ(intr_hwirq_of_vintid(T8103, 1023, NULL), INTR_INVALID);
}

/* Exhaustive: bijection between hwirq and vINTID, nobody shares a vINTID. */
static void t_bijection(void)
{
	static uint8_t owner_count[1100];
	uint32_t h, v;

	for (h = 0; h < 1024; h++) {
		bool msi;
		uint32_t w = intr_wired_vintid(T8103, h), m = intr_msi_vintid(T8103, h);

		CHECK(w == INTR_INVALID || m == INTR_INVALID);       /* a hwirq is wired xor pool */
		if (w != INTR_INVALID) {
			CHECK_EQ(intr_hwirq_of_vintid(T8103, w, &msi), h);
			CHECK(!msi);
			CHECK_EQ(intr_gsiv(T8103, h), w);
			CHECK(w >= 32 && w <= 927);
			owner_count[w]++;
		}
		if (m != INTR_INVALID) {
			CHECK_EQ(intr_hwirq_of_vintid(T8103, m, &msi), h);
			CHECK(msi);
			CHECK_EQ(intr_gsiv(T8103, h), m);
			owner_count[m]++;
		}
	}
	for (v = 0; v < 1100; v++) {
		CHECK(owner_count[v] <= 1);
		/* every vINTID in 32..959 has an owner except the hole 736..767 */
		if (v >= 32 && v <= 959)
			CHECK_EQ(owner_count[v], (v >= 736 && v <= 767) ? 0 : 1);
		else
			CHECK_EQ(owner_count[v], 0);
	}
	/* and from the vINTID side: every vINTID that maps back maps forward to itself */
	for (v = 0; v < 1100; v++) {
		bool msi;
		uint32_t hw = intr_hwirq_of_vintid(T8103, v, &msi);

		if (hw != INTR_INVALID)
			CHECK_EQ(msi ? intr_msi_vintid(T8103, hw) : intr_wired_vintid(T8103, hw), v);
	}
}

static void t_allow_list_default_deny(void)
{
	CHECK_EQ(intr_exposed_vintid(T8103, 590), 622);
	CHECK_EQ(intr_exposed_vintid(T8103, 777), 809);
	CHECK_EQ(intr_exposed_vintid(T8103, 857), 889);
	CHECK_EQ(intr_exposed_vintid(T8103, 583), INTR_INVALID);   /* ANS mailbox: not listed */
	CHECK_EQ(intr_exposed_vintid(T8103, 781), INTR_INVALID);   /* DART: not listed */
	CHECK_EQ(intr_exposed_vintid(T8103, 338), INTR_INVALID);   /* watchdog */
	CHECK_EQ(intr_exposed_vintid(T8103, 896), INTR_INVALID);
	CHECK_EQ(intr_exposed_vintid(T8103, 704), INTR_INVALID);   /* MSI pool: not exposed at level 1 */
	/* inverse */
	CHECK_EQ(intr_exposed_hwirq(T8103, 622), 590);
	CHECK_EQ(intr_exposed_hwirq(T8103, 623), INTR_INVALID);
	CHECK_EQ(intr_exposed_hwirq(T8103, 928), INTR_INVALID);
	/* exhaustive: exposure round-trips and equals the allow-list */
	unsigned n = 0;

	for (uint32_t h = 0; h < 1024; h++) {
		uint32_t v = intr_exposed_vintid(T8103, h);

		if (v == INTR_INVALID)
			continue;
		n++;
		CHECK_EQ(intr_exposed_hwirq(T8103, v), h);
		CHECK(intr_find(T8103, h) != NULL);
	}
	CHECK_EQ(n, T8103->n_allow);
}

static void t_msi_pool_exposed(void)
{
	struct intr_map m = INTR_MAP_T8103;

	m.msi_exposed = true;
	CHECK_EQ(intr_exposed_vintid(&m, 704), 928);
	CHECK_EQ(intr_exposed_vintid(&m, 735), 959);
	CHECK_EQ(intr_exposed_hwirq(&m, 940), 716);
	CHECK_EQ(intr_exposed_hwirq(&m, 750), INTR_INVALID);       /* the hole stays unowned */
	CHECK_EQ(intr_trigger(&m, 710), INTR_TRIG_EDGE);
	CHECK_EQ(intr_trigger(T8103, 710), -1);
}

static void t_trigger(void)
{
	CHECK_EQ(intr_trigger(T8103, 590), INTR_TRIG_LEVEL);
	CHECK_EQ(intr_trigger(T8103, 777), INTR_TRIG_LEVEL);
	CHECK_EQ(intr_trigger(T8103, 591), -1);                    /* not exposed */
	CHECK_EQ(intr_trigger(T8103, 900), -1);
	/* a table can carry an edge wired source */
	static const struct intr_entry e[] = { { 100, INTR_TRIG_EDGE, "x" }, { 101, INTR_TRIG_LEVEL, "y" } };
	struct intr_map m = INTR_MAP_T8103;

	m.allow = e;
	m.n_allow = 2;
	CHECK_EQ(intr_trigger(&m, 100), INTR_TRIG_EDGE);
	CHECK_EQ(intr_trigger(&m, 101), INTR_TRIG_LEVEL);
	CHECK_EQ(intr_trigger(&m, 590), -1);
}

static void t_validate(void)
{
	struct intr_map m = INTR_MAP_T8103;
	static const struct intr_entry hi[] = { { 896, 0, "hi" } };
	static const struct intr_entry pool[] = { { 710, 0, "pool" } };
	static const struct intr_entry dup[] = { { 5, 0, "a" }, { 6, 0, "b" }, { 5, 1, "c" } };

	CHECK_EQ(intr_map_validate(T8103), INTR_OK);
	m.allow = hi;   m.n_allow = 1;
	CHECK_EQ(intr_map_validate(&m), INTR_E_RANGE);
	m.allow = pool; m.n_allow = 1;
	CHECK_EQ(intr_map_validate(&m), INTR_E_POOL);
	m.allow = dup;  m.n_allow = 3;
	CHECK_EQ(intr_map_validate(&m), INTR_E_DUP);
	m = INTR_MAP_T8103;
	m.wired_max = 900;                                         /* wired range would reach the MSI SPIs */
	CHECK_EQ(intr_map_validate(&m), INTR_E_LAYOUT);
	m = INTR_MAP_T8103;
	m.msi_vintid_base = 940;                                   /* pool would end above INTID 959 */
	CHECK_EQ(intr_map_validate(&m), INTR_E_LAYOUT);
}

static void t_soc_without_msi_pool(void)
{
	static const struct intr_entry e[] = { { 10, 0, "dev" } };
	const struct intr_map m = { .wired_max = 500, .msi_hwirq_base = INTR_INVALID, .msi_count = 0,
				    .msi_vintid_base = 928, .msi_exposed = true, .allow = e, .n_allow = 1 };
	bool msi = true;

	CHECK_EQ(intr_map_validate(&m), INTR_OK);
	CHECK_EQ(intr_wired_vintid(&m, 704), INTR_INVALID);        /* above wired_max, no pool to claim it */
	CHECK_EQ(intr_wired_vintid(&m, 499), 531);
	CHECK_EQ(intr_msi_vintid(&m, 704), INTR_INVALID);
	CHECK_EQ(intr_hwirq_of_vintid(&m, 928, &msi), INTR_INVALID);
	CHECK(!msi);
	CHECK_EQ(intr_exposed_vintid(&m, 10), 42);
	CHECK_EQ(intr_exposed_vintid(&m, 11), INTR_INVALID);
}

/* ---- pirq on a vGIC ------------------------------------------------------------------ */

static struct fake FK;
static struct pirq P;
static struct intr_map g_map;          /* copy the AIC-like tests may edit */

static void eff_cb(void *ctx, enum vgic_effect e, uint32_t id, int arg)
{
	pirq_effect(ctx, e, id, arg);
}

static void setup_pirq(unsigned n, bool aic)
{
	g_map = INTR_MAP_T8103;
	setup(n);
	bring_up();
	fake_init(&FK, aic ? &g_map : NULL);
	pirq_init(&P, &FK.ops, &F.v, n);
	F.v.hooks.effect = eff_cb;
	F.v.hooks.ctx = &P;
	for (unsigned i = 0; i < n; i++)
		pirq_vcpu_on(&P, i);
}

static void set_edge_cfg(uint32_t id)
{
	wr32(D(0xC00 + 4 * (id / 16)), rd32(D(0xC00 + 4 * (id / 16))) | (2u << (2 * (id % 16))));
}

/* Take every pending physical event on vcpu; returns how many reached the model. */
static unsigned drain(unsigned vcpu)
{
	uint32_t phys;
	unsigned n = 0;

	while (fake_event(&FK, &phys))
		if (pirq_arrive(&P, vcpu, phys) != INTR_INVALID)
			n++;
	return n;
}

static uint32_t next(unsigned cpu)
{
	uint32_t id = 0;

	return vgic_next_pending(&F.v, cpu, &id, NULL) ? id : VGIC_INTID_SPURIOUS;
}

/* Guest side: acknowledge, then end of interrupt as cpuif.c does it (line low, EOI, unmask). */
static uint32_t guest_ack(unsigned cpu) { return vgic_ack(&F.v, cpu); }
static void guest_eoi(unsigned cpu, uint32_t id)
{
	pirq_line_low(&P, cpu, id);
	vgic_eoi(&F.v, cpu, id);
	pirq_finish(&P, id);
}

static void t_gic_level_cycle(void)
{
	setup_pirq(2, false);
	enable_spi(40);
	CHECK(!FK.masked[40]);                           /* guest enable unmasked the source */
	fake_line(&FK, 40, true);
	CHECK_EQ(drain(0), 1);
	CHECK(FK.masked[40]);                            /* GIC does not auto-mask: the layer did */
	CHECK(pirq_is_masked(&P, 40));
	CHECK_EQ(drain(0), 0);                           /* nothing while masked */
	CHECK_EQ(next(0), 40);
	CHECK_EQ(guest_ack(0), 40);
	/* line still high at the device: after deactivate it fires again */
	guest_eoi(0, 40);
	CHECK(!FK.masked[40]);
	CHECK_EQ(drain(0), 1);
	CHECK_EQ(next(0), 40);
	CHECK_EQ(guest_ack(0), 40);
	fake_line(&FK, 40, false);                       /* the ISR silenced the device */
	guest_eoi(0, 40);
	CHECK(!FK.masked[40]);
	CHECK_EQ(drain(0), 0);
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
}

static void t_gic_edge_never_masked(void)
{
	setup_pirq(2, false);
	enable_spi(80);
	set_edge_cfg(80);                                /* F-25: the guest's ICFGR reaches the controller */
	CHECK(FK.edge[80]);
	CHECK(pirq_is_edge(&P, 80));
	unsigned masks = FK.n_mask, unmasks = FK.n_unmask;

	fake_pulse(&FK, 80);
	CHECK_EQ(drain(0), 1);
	CHECK_EQ(FK.n_mask, masks);                      /* no mask, no unmask: a pulse is just latched */
	CHECK_EQ(FK.n_unmask, unmasks);
	CHECK_EQ(next(0), 80);
	CHECK_EQ(guest_ack(0), 80);
	fake_pulse(&FK, 80);                             /* second pulse while active */
	CHECK_EQ(drain(0), 1);
	guest_eoi(0, 80);
	CHECK_EQ(next(0), 80);                           /* pending+active became pending */
	CHECK_EQ(FK.n_mask, masks);
}

static void t_gic_identity_and_range(void)
{
	setup_pirq(1, false);
	CHECK_EQ(FK.ops.intid_of(&FK, 40), 40);
	CHECK_EQ(FK.ops.intid_of(&FK, 31), INTR_INVALID);
	CHECK_EQ(FK.ops.intid_of(&FK, 960), INTR_INVALID);
	/* a physical interrupt with no vINTID is quieted, not injected */
	fake_init(&FK, NULL);
	FK.masked[1000] = false;
	FK.line[1000] = true;
	CHECK_EQ(drain(0), 0);
	CHECK(FK.masked[1000]);
	CHECK_EQ(P.st.denied, 1);
}

static void t_aic_level_cycle(void)
{
	setup_pirq(2, true);
	enable_spi(622);                                 /* ANS: hwirq 590 */
	CHECK(!FK.masked[590]);
	CHECK(!FK.edge[590]);                            /* table said level */
	fake_line(&FK, 590, true);
	CHECK_EQ(drain(0), 1);
	CHECK(FK.masked[590]);                           /* the event read masked it */
	unsigned masks = FK.n_mask;

	CHECK_EQ(masks, 0);                              /* ... the layer did not have to */
	CHECK_EQ(next(0), 622);
	CHECK_EQ(guest_ack(0), 622);
	CHECK(FK.masked[590]);                           /* still masked while the guest works on it */
	guest_eoi(0, 622);                               /* line still high: unmask re-fires */
	CHECK(!FK.masked[590]);
	CHECK_EQ(drain(0), 1);
	CHECK_EQ(guest_ack(0), 622);
	fake_line(&FK, 590, false);
	guest_eoi(0, 622);
	CHECK(!FK.masked[590]);                          /* unmasked, quiet */
	CHECK_EQ(drain(0), 0);
	CHECK_EQ(P.st.arrivals, 2);
}

static void t_aic_edge_unmask_at_injection(void)
{
	g_map = INTR_MAP_T8103;
	setup_pirq(2, true);
	g_map.msi_exposed = true;                        /* level 2 on: pool hwirq 704 -> vINTID 928 */
	pirq_init(&P, &FK.ops, &F.v, 2);                 /* re-read the table */
	for (unsigned i = 0; i < 2; i++)
		pirq_vcpu_on(&P, i);
	CHECK(FK.edge[704]);                             /* trigger programmed from the table */
	CHECK(pirq_is_edge(&P, 928));
	enable_spi(928);
	set_edge_cfg(928);                               /* Windows programs ICFGR edge as ACPI said */
	CHECK_EQ(P.st.trig_ignored, 1);                  /* ... and the table already knew */
	CHECK(FK.edge[704]);
	fake_pulse(&FK, 704);
	CHECK_EQ(drain(0), 1);
	CHECK(!FK.masked[704]);                          /* unmasked at injection, not at deactivate */
	CHECK_EQ(next(0), 928);
	CHECK_EQ(guest_ack(0), 928);
	fake_pulse(&FK, 704);                            /* second edge while active */
	CHECK_EQ(drain(0), 1);
	CHECK(!FK.masked[704]);
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);          /* active: not deliverable yet */
	guest_eoi(0, 928);
	CHECK_EQ(next(0), 928);                          /* pending+active -> pending */
	/* a third pulse while still pending merges */
	fake_pulse(&FK, 704);
	CHECK_EQ(drain(0), 1);
	CHECK_EQ(guest_ack(0), 928);
	guest_eoi(0, 928);
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
}

static void t_aic_default_deny_storm(void)
{
	setup_pirq(2, true);
	/* ANS mailbox 583 is not allow-listed: the layer would never unmask it, but if it fires
	 * (the controller was left unmasked by firmware) it is quieted and counted. */
	FK.masked[583] = false;
	fake_line(&FK, 583, true);
	CHECK_EQ(drain(0), 0);
	CHECK(FK.masked[583]);
	CHECK_EQ(P.st.denied, 1);
	CHECK_EQ(next(0), VGIC_INTID_SPURIOUS);
	CHECK_EQ(drain(0), 0);                           /* stays quiet: no storm */
	CHECK_EQ(P.st.denied, 1);
	/* the guest cannot reach a source that is not exposed: enabling vINTID 615 is a no-op */
	unsigned un = FK.n_unmask, tg = FK.n_set_target;

	enable_spi(615);                                 /* 32 + 583 */
	CHECK_EQ(FK.n_unmask, un);
	CHECK_EQ(FK.n_set_target, tg);
	CHECK(FK.masked[583]);
}

static void t_strict_level_reenable_while_held(void)
{
	setup_pirq(1, true);
	enable_spi(622);
	fake_line(&FK, 590, true);
	CHECK_EQ(drain(0), 1);
	CHECK_EQ(guest_ack(0), 622);
	/* the guest disables and re-enables the SPI inside its ISR (ARCH 4.2: "on re-enable
	 * unmask only if no LR holds it") */
	wr32(D(0x180 + 4 * (622 / 32)), 1u << (622 % 32));
	CHECK(FK.masked[590]);
	wr32(D(0x100 + 4 * (622 / 32)), 1u << (622 % 32));
	CHECK(FK.masked[590]);                           /* held: not unmasked by the enable */
	fake_line(&FK, 590, false);
	guest_eoi(0, 622);
	CHECK(!FK.masked[590]);                          /* unmasked at deactivate */
	/* the GIC-like backend keeps today's behaviour: the enable unmasks at once */
	setup_pirq(1, false);
	enable_spi(40);
	fake_line(&FK, 40, true);
	CHECK_EQ(drain(0), 1);
	CHECK_EQ(guest_ack(0), 40);
	wr32(D(0x184), 1u << 8);                         /* INTID 40 = word 1, bit 8 */
	wr32(D(0x104), 1u << 8);
	CHECK(!FK.masked[40]);
}

static void t_trigger_table_vs_guest(void)
{
	/* GIC-like: the guest decides, both ways */
	setup_pirq(1, false);
	set_edge_cfg(90);
	CHECK(FK.edge[90]);
	wr32(D(0xC00 + 4 * (90 / 16)), 0);
	CHECK(!FK.edge[90]);
	CHECK(!pirq_is_edge(&P, 90));
	/* AIC-like: a level wired source stays level whatever ICFGR says */
	setup_pirq(1, true);
	set_edge_cfg(622);
	CHECK(!FK.edge[590]);
	CHECK(!pirq_is_edge(&P, 622));
	CHECK_EQ(P.st.trig_ignored, 1);
}

/* ---- re-target on CPU_OFF / CPU_ON ---------------------------------------------------- */

static void route_to(uint32_t id, unsigned cpu)
{
	wr64(D(0x6000 + 8 * id), cpu);                   /* affinity = vCPU index in the fixture */
}

static void t_retarget_off_on(void)
{
	setup_pirq(4, false);
	enable_spi(40);
	route_to(40, 3);
	CHECK_EQ(FK.target[40], 3);
	CHECK_EQ(P.phys_target[40], 3);
	unsigned before = FK.n_set_target;

	pirq_vcpu_off(&P, 3);                            /* PSCI CPU_OFF of vCPU 3 */
	CHECK_EQ(FK.target[40], 0);                      /* now on an online vCPU */
	CHECK_EQ(FK.n_set_target, before + 1);
	CHECK_EQ(P.st.retargets, 1);
	CHECK_EQ(P.guest_target[40], 3);                 /* the guest's wish is remembered */
	/* the guest's IROUTER still reads what it wrote */
	CHECK_EQ(rd64(D(0x6000 + 8 * 40)), 3);
	pirq_vcpu_on(&P, 3);                             /* CPU_ON, IROUTER still names it */
	CHECK_EQ(FK.target[40], 3);
	CHECK_EQ(P.st.retargets, 2);
}

static void t_retarget_untouched_and_idempotent(void)
{
	setup_pirq(4, false);
	enable_spi(40);
	enable_spi(41);
	route_to(40, 1);
	route_to(41, 2);
	pirq_vcpu_off(&P, 3);                            /* nobody is routed to vCPU 3 */
	CHECK_EQ(FK.target[40], 1);
	CHECK_EQ(FK.target[41], 2);
	CHECK_EQ(P.st.retargets, 0);
	pirq_vcpu_off(&P, 2);
	CHECK_EQ(FK.target[41], 0);
	CHECK_EQ(FK.target[40], 1);
	unsigned n = FK.n_set_target;

	pirq_vcpu_off(&P, 2);                            /* repeating changes nothing */
	CHECK_EQ(FK.n_set_target, n);
	pirq_vcpu_on(&P, 1);                             /* ON of a vCPU nobody needs back */
	CHECK_EQ(FK.target[40], 1);
	CHECK_EQ(FK.n_set_target, n);
}

static void t_route_while_off_then_on(void)
{
	setup_pirq(4, false);
	enable_spi(50);
	pirq_vcpu_off(&P, 2);
	route_to(50, 2);                                 /* the guest names a core that is off */
	CHECK_EQ(P.guest_target[50], 2);
	CHECK_EQ(FK.target[50], 0);                      /* physically somewhere that can take it */
	pirq_vcpu_on(&P, 2);
	CHECK_EQ(FK.target[50], 2);
	/* the guest re-routes away while the vCPU is off and it comes back: no stale restore */
	pirq_vcpu_off(&P, 2);
	route_to(50, 1);
	CHECK_EQ(FK.target[50], 1);
	pirq_vcpu_on(&P, 2);
	CHECK_EQ(FK.target[50], 1);
}

static void t_unrouted_spi_still_moves_on_cpu_off(void)
{
	setup_pirq(4, false);
	enable_spi(70);
	route_to(70, 2);
	CHECK_EQ(FK.target[70], 2);
	wr64(D(0x6000 + 8 * 70), 0x77);                  /* affinity that matches no vCPU */
	CHECK_EQ(P.guest_target[70], -1);
	CHECK_EQ(P.phys_target[70], 2);                  /* physical route unchanged */
	CHECK_EQ(FK.target[70], 2);
	pirq_vcpu_off(&P, 2);
	CHECK_EQ(FK.target[70], 0);                      /* not stranded on the core that left */
}

static void t_fallback_moves_again(void)
{
	setup_pirq(4, false);
	enable_spi(60);
	route_to(60, 3);
	pirq_vcpu_off(&P, 3);
	CHECK_EQ(FK.target[60], 0);                      /* fell back to vCPU 0 */
	pirq_vcpu_off(&P, 0);                            /* the fallback core goes off too */
	CHECK_EQ(FK.target[60], 1);
	pirq_vcpu_on(&P, 0);
	CHECK_EQ(FK.target[60], 1);                      /* still not at its home: 3 is off */
	pirq_vcpu_on(&P, 3);
	CHECK_EQ(FK.target[60], 3);
}

static void t_retarget_eight_vcpus_and_last_cpu(void)
{
	setup_pirq(8, false);
	for (uint32_t id = 40; id < 48; id++) {
		enable_spi(id);
		route_to(id, id - 40);                       /* one SPI per vCPU */
	}
	for (unsigned c = 7; c >= 1; c--)
		pirq_vcpu_off(&P, c);
	for (uint32_t id = 40; id < 48; id++)
		CHECK_EQ(FK.target[id], 0);
	CHECK_EQ(P.st.retargets, 7);
	/* the last online vCPU: PSCI refuses it; the model must not invent a target anyway */
	pirq_vcpu_off(&P, 0);
	CHECK_EQ(FK.target[40], 0);
	for (unsigned c = 0; c < 8; c++)
		pirq_vcpu_on(&P, c);
	for (uint32_t id = 40; id < 48; id++)
		CHECK_EQ(FK.target[id], (int)(id - 40));
}

static void t_retarget_aic_exposed_only(void)
{
	setup_pirq(4, true);
	enable_spi(622);                                 /* ANS */
	route_to(622, 2);
	CHECK_EQ(FK.target[590], 2);
	enable_spi(640);                                 /* 32 + 608: not exposed */
	route_to(640, 2);
	CHECK_EQ(FK.target[608], -1);                    /* the guest cannot route what it cannot see */
	pirq_vcpu_off(&P, 2);
	CHECK_EQ(FK.target[590], 0);
	CHECK_EQ(FK.target[608], -1);
	/* an interrupt that arrives for the vCPU that left is still delivered to the new one */
	fake_line(&FK, 590, true);
	CHECK_EQ(drain(0), 1);
	CHECK_EQ(vgic_route_target(&F.v, 622), 2);       /* the model keeps the guest's view */
	CHECK_EQ(next(2), 622);                          /* ... and delivers to the vCPU the guest chose */
}

int main(void)
{
	RUN(t_known_values);
	RUN(t_collision_with_msi_pool);
	RUN(t_inverse_boundaries);
	RUN(t_bijection);
	RUN(t_allow_list_default_deny);
	RUN(t_msi_pool_exposed);
	RUN(t_trigger);
	RUN(t_validate);
	RUN(t_soc_without_msi_pool);
	RUN(t_gic_level_cycle);
	RUN(t_gic_edge_never_masked);
	RUN(t_gic_identity_and_range);
	RUN(t_aic_level_cycle);
	RUN(t_aic_edge_unmask_at_injection);
	RUN(t_aic_default_deny_storm);
	RUN(t_strict_level_reenable_while_held);
	RUN(t_trigger_table_vs_guest);
	RUN(t_retarget_off_on);
	RUN(t_retarget_untouched_and_idempotent);
	RUN(t_route_while_off_then_on);
	RUN(t_unrouted_spi_still_moves_on_cpu_off);
	RUN(t_fallback_moves_again);
	RUN(t_retarget_eight_vcpus_and_last_cpu);
	RUN(t_retarget_aic_exposed_only);
	TESTS_DONE();
}
