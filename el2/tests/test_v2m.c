/* SPDX-License-Identifier: MIT */
/*
 * Host tests: GICv2m-style MSI frame model (include/wel2_v2m.h) wired to the vGIC model, and
 * its interaction with the interrupt map's MSI pool (928..959) and the pirq edge path.
 * Design: docs/design/watari-el2-devirq/ARCHITECTURE.md 5 (frame at 0xFFFFF000 on the t8103),
 * TEST_STRATEGY 2 (test_msi_frame).
 */
#include "test.h"
#include "fake_pirq.h"
#include "wel2_v2m.h"

#define FRAME   0xFFFFF000ull
#define T8103   (&INTR_MAP_T8103)

static struct v2m M;
static int n_log;
static uint32_t log_val;

static void log_cb(void *c, uint32_t v) { (void)c; n_log++; log_val = v; }

static void setup_frame(void)
{
	setup(2);
	bring_up();
	CHECK_EQ(v2m_init_from_map(&M, FRAME, T8103, &F.v), 0);
	M.log = log_cb;
	n_log = 0;
}

static uint32_t fr(uint32_t off)
{
	uint64_t v = ~0ull;

	CHECK_EQ(v2m_mmio_read(&M, FRAME + off, 4, &v), VGIC_OK);
	return (uint32_t)v;
}

static int bell(uint64_t val) { return v2m_mmio_write(&M, FRAME + V2M_SETSPI_NS, 4, val); }

static bool pending(uint32_t id)
{
	return (rd32(D(0x200 + 4 * (id / 32))) >> (id % 32)) & 1;
}

static bool active(uint32_t id)
{
	return (rd32(D(0x300 + 4 * (id / 32))) >> (id % 32)) & 1;
}

static void t_registers(void)
{
	setup_frame();
	CHECK_EQ(fr(V2M_TYPER), (928u << 16) | 32u);
	CHECK_EQ(fr(V2M_TYPER) & 0xE0000000u, 0);           /* Valid/CLR/SR = 0: nothing optional */
	CHECK_EQ(fr(V2M_IIDR), V2M_IIDR_VALUE);
	CHECK_EQ(fr(V2M_SETSPI_NS), 0);                      /* WO reads as zero */
	CHECK_EQ(fr(0x048), 0);                              /* no CLRSPI */
	CHECK_EQ(fr(0x000), 0);
	CHECK_EQ(fr(0xFD0), 0);                              /* ID registers: RAZ */
	CHECK_EQ(fr(0xFFC), 0);
	/* read-only registers ignore writes */
	CHECK_EQ(v2m_mmio_write(&M, FRAME + V2M_TYPER, 4, 0), VGIC_OK);
	CHECK_EQ(v2m_mmio_write(&M, FRAME + V2M_IIDR, 4, 0xFFFFFFFFu), VGIC_OK);
	CHECK_EQ(fr(V2M_TYPER), (928u << 16) | 32u);
	CHECK_EQ(fr(V2M_IIDR), V2M_IIDR_VALUE);
	/* the same frame as QEMU's MADT in F-25: base 0x08020000, SPI 80, count 64 */
	CHECK_EQ(v2m_init(&M, 0x08020000ull, 80, 64, &F.v), 0);
	{
		uint64_t v = 0;

		CHECK_EQ(v2m_mmio_read(&M, 0x08020000ull + V2M_TYPER, 4, &v), VGIC_OK);
		CHECK_EQ(v, (80u << 16) | 64u);
	}
	CHECK_EQ(v2m_doorbell_addr(&M), 0x08020040ull);
}

static void t_access_rules(void)
{
	uint64_t v;

	setup_frame();
	CHECK(v2m_contains(&M, FRAME));
	CHECK(v2m_contains(&M, FRAME + 0xFFF));
	/* the trap window is the whole 16 KB page 0xFFFFC000-0xFFFFFFFF (#74) */
	CHECK(v2m_contains(&M, 0xFFFFC000ull));
	CHECK(v2m_contains(&M, FRAME - 1));
	CHECK(!v2m_contains(&M, 0xFFFFBFFFull));
	CHECK(!v2m_contains(&M, FRAME + 0x1000));
	CHECK_EQ(v2m_mmio_read(&M, 0xFFFFBFFCull, 4, &v), VGIC_ERR_RANGE);
	CHECK_EQ(v2m_mmio_write(&M, FRAME + 0x1000, 4, 0), VGIC_ERR_RANGE);
	CHECK_EQ(v2m_mmio_write(&M, 0xFFFFBFFCull, 4, 0), VGIC_ERR_RANGE);
	CHECK_EQ(v2m_mmio_read(&M, FRAME + V2M_TYPER, 8, &v), VGIC_ERR_ACCESS);   /* 64-bit */
	CHECK_EQ(v2m_mmio_read(&M, FRAME + V2M_TYPER, 1, &v), VGIC_ERR_ACCESS);
	CHECK_EQ(v2m_mmio_read(&M, FRAME + V2M_TYPER + 2, 4, &v), VGIC_ERR_ACCESS);   /* misaligned */
	CHECK_EQ(v2m_mmio_write(&M, FRAME + V2M_SETSPI_NS, 8, 930), VGIC_ERR_ACCESS);
	CHECK_EQ(v2m_mmio_write(&M, FRAME + V2M_SETSPI_NS, 1, 930), VGIC_ERR_ACCESS);
	CHECK(!pending(930));
	CHECK_EQ(M.st.bad_access, 5);
	/* 16-bit write of the low half is supported (spec 12.9.43) */
	CHECK_EQ(v2m_mmio_write(&M, FRAME + V2M_SETSPI_NS, 2, 931), VGIC_OK);
	CHECK(pending(931));
	/* ... and only the low 16 bits are used */
	CHECK_EQ(v2m_mmio_write(&M, FRAME + V2M_SETSPI_NS, 2, 0x10000u | 932u), VGIC_OK);
	CHECK(pending(932));
}

static void t_doorbell_range(void)
{
	setup_frame();
	/* every INTID in range sets exactly its own SPI pending */
	for (uint32_t id = 928; id <= 959; id++) {
		CHECK_EQ(bell(id), VGIC_OK);
		CHECK(pending(id));
		for (uint32_t o = 928; o < id; o++)
			CHECK(pending(o));
		if (id < 959)
			CHECK(!pending(id + 1));
	}
	CHECK_EQ(M.st.doorbells, 32);
	CHECK_EQ(M.st.ignored, 0);
	CHECK_EQ(n_log, 0);

	/* outside the pool: ignored, counted, logged once; nothing else changes */
	setup_frame();
	static const uint32_t bad[] = { 0, 1, 31, 32, 927, 960, 1019, 1020, 1023, 8192 };

	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		CHECK_EQ(bell(bad[i]), VGIC_OK);
	}
	CHECK_EQ(M.st.doorbells, 0);
	CHECK_EQ(M.st.ignored, sizeof bad / sizeof bad[0]);
	for (uint32_t id = 32; id < 960; id++)
		CHECK(!pending(id));
	/* bits [31:13] are RES0: masked off, not a reason to reject (8192 + 928 is vector 0) */
	CHECK_EQ(bell(8192u + 928u), VGIC_OK);
	CHECK(pending(928));
	CHECK_EQ(M.st.doorbells, 1);
}

/* ---- #74: the 16 KB trap page around the frame ------------------------------------- */

static int n_stray;
static uint32_t stray_off[16];
static bool stray_wr[16];

static void stray_cb(void *c, uint32_t off, bool wr, unsigned sz)
{
	(void)c; (void)sz;
	if (n_stray < 16) {
		stray_off[n_stray] = off;
		stray_wr[n_stray] = wr;
	}
	n_stray++;
}

static void t_page_layout(void)
{
	uint64_t v;

	setup_frame();
	CHECK_EQ(M.page_base, 0xFFFFC000ull);
	CHECK_EQ(FRAME - M.page_base, 0x3000ull);                  /* the frame is the last 4 KB of the page */
	CHECK_EQ(v2m_doorbell_addr(&M), 0xFFFFF040ull);
	/* correct registers at page offset 0x3000 + register offset */
	CHECK_EQ(v2m_mmio_read(&M, 0xFFFFC000ull + 0x3008, 4, &v), VGIC_OK);
	CHECK_EQ(v, (928u << 16) | 32u);
	CHECK_EQ(v2m_mmio_read(&M, 0xFFFFC000ull + 0x3000 + 0xFCC, 4, &v), VGIC_OK);
	CHECK_EQ(v, V2M_IIDR_VALUE);
	CHECK_EQ(v2m_mmio_write(&M, 0xFFFFC000ull + 0x3040, 4, 930), VGIC_OK);
	CHECK(pending(930));
	CHECK_EQ(M.st.stray, 0);
	/* the same register numbers at the other 4 KB frames of the page are NOT the registers */
	for (uint32_t fr_off = 0; fr_off < 0x3000; fr_off += 0x1000) {
		CHECK_EQ(v2m_mmio_read(&M, 0xFFFFC000ull + fr_off + V2M_TYPER, 4, &v), VGIC_OK);
		CHECK_EQ(v, 0);
		CHECK_EQ(v2m_mmio_read(&M, 0xFFFFC000ull + fr_off + V2M_IIDR, 4, &v), VGIC_OK);
		CHECK_EQ(v, 0);
		CHECK_EQ(v2m_mmio_write(&M, 0xFFFFC000ull + fr_off + V2M_SETSPI_NS, 4, 931), VGIC_OK);
	}
	CHECK(!pending(931));
	CHECK_EQ(M.st.doorbells, 1);
}

static void t_page_raz_wi(void)
{
	uint64_t v;
	static const unsigned sizes[] = { 1, 2, 4, 8 };

	setup_frame();
	M.log_stray = stray_cb;
	n_stray = 0;
	/* every 4-byte word in 0x0..0x2FFC reads zero and ignores writes (nothing pending, nothing refused) */
	for (uint32_t off = 0; off < 0x3000; off += 4) {
		v = ~0ull;
		CHECK_EQ(v2m_mmio_read(&M, M.page_base + off, 4, &v), VGIC_OK);
		CHECK_EQ(v, 0);
		CHECK_EQ(v2m_mmio_write(&M, M.page_base + off, 4, 0xFFFFFFFFu), VGIC_OK);
	}
	/* any size or alignment is accepted outside the frame: refusing would kill the guest at EL2 */
	for (unsigned i = 0; i < 4; i++) {
		v = ~0ull;
		CHECK_EQ(v2m_mmio_read(&M, M.page_base + 0x1001, sizes[i], &v), VGIC_OK);
		CHECK_EQ(v, 0);
		CHECK_EQ(v2m_mmio_write(&M, M.page_base + 0x2FFC, sizes[i], 930), VGIC_OK);
	}
	CHECK_EQ(M.st.stray, 2 * 0xC00 + 8);
	CHECK_EQ(M.st.bad_access, 0);
	CHECK_EQ(M.st.doorbells, 0);
	for (uint32_t id = 32; id < 960; id++)
		CHECK(!pending(id));
	/* the log is rate-limited: only the first V2M_STRAY_LOG_MAX, with page-relative offsets */
	CHECK_EQ(n_stray, V2M_STRAY_LOG_MAX);
	CHECK_EQ(stray_off[0], 0);
	CHECK(!stray_wr[0]);
	CHECK_EQ(stray_off[1], 0);
	CHECK(stray_wr[1]);
	CHECK_EQ(stray_off[2], 4);
	/* the frame itself still behaves after that */
	CHECK_EQ(v2m_mmio_read(&M, FRAME + V2M_TYPER, 4, &v), VGIC_OK);
	CHECK_EQ(v, (928u << 16) | 32u);
}

static void t_page_free_of_ram(void)
{
	setup_frame();
	CHECK(v2m_page_free(&M, 0x800000000ull, 0xA00000000ull));        /* M1: RAM at 32 GB */
	CHECK(v2m_page_free(&M, 0x40000000ull, 0xC0000000ull));          /* QEMU -m 2G ends below the page */
	CHECK(v2m_page_free(&M, 0x40000000ull, 0xFFFFC000ull));          /* ends exactly at the page */
	CHECK(!v2m_page_free(&M, 0x40000000ull, 0xFFFFC001ull));
	CHECK(!v2m_page_free(&M, 0x40000000ull, 0x140000000ull));        /* QEMU -m 4G: RAM there */
	CHECK(v2m_page_free(&M, 0x100000000ull, 0x140000000ull));        /* starts right after the page */
	CHECK(v2m_page_free(&M, 0, 0));
}

static void t_log_once(void)
{
	setup_frame();
	bell(927);
	bell(960);
	bell(0);
	CHECK_EQ(n_log, 1);
	CHECK_EQ(log_val, 927);
	CHECK_EQ(M.st.ignored, 3);
}

static void t_edge_pending_semantics(void)
{
	uint32_t id = 0;

	setup_frame();
	enable_spi(930);
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));
	bell(930);
	CHECK(vgic_next_pending(&F.v, 0, &id, NULL));
	CHECK_EQ(id, 930);
	bell(930);                                         /* already pending: merges */
	CHECK_EQ(vgic_ack(&F.v, 0), 930);
	CHECK(active(930));
	CHECK(!pending(930));                              /* the ack consumed the latch */
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));     /* one delivery for two doorbells */
	/* a doorbell while active: active and pending */
	bell(930);
	CHECK(active(930));
	CHECK(pending(930));
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));     /* not deliverable while active */
	vgic_eoi(&F.v, 0, 930);
	CHECK(vgic_next_pending(&F.v, 0, &id, NULL));      /* pending survived the deactivate */
	CHECK_EQ(id, 930);
	CHECK_EQ(vgic_ack(&F.v, 0), 930);
	vgic_eoi(&F.v, 0, 930);
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));
}

static void t_disabled_spi_stays_pending(void)
{
	uint32_t id = 0;

	setup_frame();
	bell(940);                                         /* SPI not enabled by the guest */
	CHECK(pending(940));
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));
	enable_spi(940);
	CHECK(vgic_next_pending(&F.v, 0, &id, NULL));
	CHECK_EQ(id, 940);
}

static void t_level_configured_spi_still_edge(void)
{
	uint32_t id = 0;

	setup_frame();
	enable_spi(931);                                   /* ICFGR default: level */
	bell(931);
	CHECK_EQ(vgic_ack(&F.v, 0), 931);
	vgic_eoi(&F.v, 0, 931);
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));     /* no line behind it: gone after the ack */
}

static void t_init_validation(void)
{
	struct v2m m;

	setup(1);
	CHECK_EQ(v2m_init(&m, FRAME, 928, 32, &F.v), 0);
	CHECK_EQ(v2m_init(&m, FRAME, 928, 0, &F.v), -1);         /* empty */
	CHECK_EQ(v2m_init(&m, FRAME, 31, 4, &F.v), -1);          /* starts below the SPIs */
	CHECK_EQ(v2m_init(&m, FRAME, 928, 33, &F.v), -1);        /* beyond INTID 959 (model size) */
	CHECK_EQ(v2m_init(&m, FRAME, 960, 1, &F.v), -1);
	CHECK_EQ(v2m_init(&m, FRAME, 32, 1024, &F.v), -1);       /* count does not fit TYPER[9:0] */
	CHECK_EQ(v2m_init(&m, FRAME, 32, 927, &F.v), 0);
	CHECK_EQ(v2m_init(&m, FRAME + 0x800, 928, 32, &F.v), -1);   /* the frame is 4 KB aligned */
	/* a SoC without an MSI pool has no frame to model */
	{
		struct intr_map nopool = INTR_MAP_T8103;

		nopool.msi_hwirq_base = INTR_INVALID;
		CHECK_EQ(v2m_init_from_map(&m, FRAME, &nopool, &F.v), -1);
	}
}

/* ---- the frame and the interrupt map's MSI pool ------------------------------------- */

static struct fake FK;
static struct pirq P;
static struct intr_map g_map;

static void eff_cb(void *ctx, enum vgic_effect e, uint32_t id, int arg)
{
	pirq_effect(ctx, e, id, arg);
}

static void setup_pool(bool exposed)
{
	g_map = INTR_MAP_T8103;
	g_map.msi_exposed = exposed;
	setup(2);
	bring_up();
	fake_init(&FK, &g_map);
	pirq_init(&P, &FK.ops, &F.v, 2);
	F.v.hooks.effect = eff_cb;
	F.v.hooks.ctx = &P;
	for (unsigned i = 0; i < 2; i++)
		pirq_vcpu_on(&P, i);
	CHECK_EQ(v2m_init_from_map(&M, FRAME, &g_map, &F.v), 0);
}

static void t_frame_matches_map_pool(void)
{
	setup_pool(true);
	CHECK_EQ(M.spi_base, g_map.msi_vintid_base);
	CHECK_EQ(M.spi_count, g_map.msi_count);
	for (uint32_t k = 0; k < 32; k++) {
		uint32_t hw = g_map.msi_hwirq_base + k;

		/* the vINTID a doorbell write for vector k sets is the vINTID the pool maps hwirq to */
		CHECK_EQ(intr_msi_vintid(&g_map, hw), M.spi_base + k);
		CHECK_EQ(intr_exposed_vintid(&g_map, hw), M.spi_base + k);
		CHECK_EQ(intr_trigger(&g_map, hw), INTR_TRIG_EDGE);
		CHECK_EQ(intr_gsiv(&g_map, hw), M.spi_base + k);
	}
	/* nothing in the wired range is reachable through the frame */
	for (uint32_t v = 32; v < 928; v++)
		CHECK(bell(v) == VGIC_OK && !pending(v));
	CHECK_EQ(M.st.ignored, 928 - 32);
}

static void t_doorbell_and_pirq_edge_merge(void)
{
	uint32_t ev, id = 0;

	setup_pool(true);
	enable_spi(933);                                   /* vector 5 */
	CHECK(pirq_is_edge(&P, 933));
	wr32(D(0xC00 + 4 * (933 / 16)), 2u << (2 * (933 % 16)));   /* the guest sets ICFGR edge */
	/* the Apple PCIe path: AIC edge on hwirq 709 -> pirq -> vINTID 933 */
	fake_pulse(&FK, 709);
	FK.masked[709] = false;
	CHECK(fake_event(&FK, &ev));
	CHECK_EQ(pirq_arrive(&P, 0, ev), 933);
	CHECK(pending(933));
	/* the CPU-write path sets the same SPI: one delivery for the two */
	CHECK_EQ(v2m_mmio_write(&M, v2m_doorbell_addr(&M), 4, 933), VGIC_OK);
	CHECK_EQ(vgic_ack(&F.v, 0), 933);
	vgic_eoi(&F.v, 0, 933);
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));
	/* and the other way round */
	bell(933);
	fake_pulse(&FK, 709);
	FK.masked[709] = false;
	CHECK(fake_event(&FK, &ev));
	CHECK_EQ(pirq_arrive(&P, 1, ev), 933);
	CHECK_EQ(vgic_ack(&F.v, 0), 933);
	vgic_eoi(&F.v, 0, 933);
	CHECK(!vgic_next_pending(&F.v, 0, &id, NULL));
}

static void t_frame_independent_of_exposure(void)
{
	uint32_t ev;

	setup_pool(false);                                 /* level 1: the pool is not exposed */
	enable_spi(928);
	CHECK_EQ(fr(V2M_TYPER), (928u << 16) | 32u);       /* the frame answers all the same */
	bell(928);
	CHECK(pending(928));                               /* the CPU write works */
	FK.masked[704] = false;                            /* ... but a physical MSI is denied */
	FK.edge[704] = true;
	fake_pulse(&FK, 704);
	CHECK(fake_event(&FK, &ev));
	CHECK_EQ(pirq_arrive(&P, 0, ev), INTR_INVALID);
	CHECK_EQ(P.st.denied, 1);
}

int main(void)
{
	RUN(t_registers);
	RUN(t_access_rules);
	RUN(t_doorbell_range);
	RUN(t_page_layout);
	RUN(t_page_raz_wi);
	RUN(t_page_free_of_ram);
	RUN(t_log_once);
	RUN(t_edge_pending_semantics);
	RUN(t_disabled_spi_stays_pending);
	RUN(t_level_configured_spi_still_edge);
	RUN(t_init_validation);
	RUN(t_frame_matches_map_pool);
	RUN(t_doorbell_and_pirq_edge_merge);
	RUN(t_frame_independent_of_exposure);
	TESTS_DONE();
}
