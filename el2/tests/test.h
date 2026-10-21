/* SPDX-License-Identifier: MIT */
/* Minimal host test harness and vGIC fixture. */
#ifndef WEL2_TEST_H
#define WEL2_TEST_H

#include <inttypes.h>
#include <stdio.h>
#include "wel2_vgic.h"

static int t_tests, t_checks, t_fail;

#define CHECK(c) do { t_checks++; if (!(c)) { t_fail++; \
	printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b); t_checks++; \
	if (_a != _b) { t_fail++; printf("    FAIL %s:%d: %s == %s (0x%" PRIx64 " vs 0x%" PRIx64 ")\n", \
	__FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define RUN(fn) do { int _b = t_fail; t_tests++; fn(); \
	printf("%s %s\n", t_fail == _b ? "ok  " : "FAIL", #fn); } while (0)
#define TESTS_DONE() do { printf("%s: %d tests, %d checks, %d failed\n", __FILE__, \
	t_tests, t_checks, t_fail); return t_fail ? 1 : 0; } while (0)

#define GICD 0xF00000000ull
#define GICR 0xF10000000ull
#define D(off)       (GICD + (off))
#define R(cpu, off)  (GICR + 0x20000ull * (cpu) + (off))
#define S(cpu, off)  (GICR + 0x20000ull * (cpu) + 0x10000ull + (off))

struct fx {
	struct vgic v;
	int logs, last_kind;
	uint64_t last_addr;
	int effects, last_eff, last_eff_arg;
	uint32_t last_eff_intid;
};
static struct fx F;

static void fx_log(void *c, enum vgic_log_kind k, uint64_t a, unsigned s, bool w, uint64_t val)
{
	(void)c; (void)s; (void)w; (void)val;
	F.logs++; F.last_kind = k; F.last_addr = a;
}
static void fx_eff(void *c, enum vgic_effect e, uint32_t id, int arg)
{
	(void)c;
	F.effects++; F.last_eff = e; F.last_eff_intid = id; F.last_eff_arg = arg;
}

static inline void setup(unsigned nvcpu)
{
	struct vgic_config cfg = { GICD, GICR, nvcpu, NULL, { fx_log, fx_eff, NULL, NULL }, false, 0 };
	F.logs = F.effects = 0;
	if (vgic_init(&F.v, &cfg))
		printf("    vgic_init failed\n");
}

static inline uint32_t rd32(uint64_t a) { uint64_t v = ~0ull; vgic_mmio_read(&F.v, a, 4, &v); return (uint32_t)v; }
static inline uint32_t rd8(uint64_t a)  { uint64_t v = ~0ull; vgic_mmio_read(&F.v, a, 1, &v); return (uint32_t)v; }
static inline uint64_t rd64(uint64_t a) { uint64_t v = ~0ull; vgic_mmio_read(&F.v, a, 8, &v); return v; }
static inline void wr32(uint64_t a, uint32_t v) { vgic_mmio_write(&F.v, a, 4, v); }
static inline void wr8(uint64_t a, uint32_t v)  { vgic_mmio_write(&F.v, a, 1, v); }
static inline void wr64(uint64_t a, uint64_t v) { vgic_mmio_write(&F.v, a, 8, v); }

/* Enable both groups and wake all redistributors. */
static inline void bring_up(void)
{
	unsigned i;
	wr32(D(0), 0x3);
	for (i = 0; i < F.v.nr_vcpus; i++)
		wr32(R(i, 0x14), 0);
}
/* Enable an SPI as Group 1. */
static inline void enable_spi(uint32_t id)
{
	wr32(D(0x80 + 4 * (id / 32)), rd32(D(0x80 + 4 * (id / 32))) | (1u << (id % 32)));
	wr32(D(0x100 + 4 * (id / 32)), 1u << (id % 32));
}
/* Enable an SGI/PPI on one vCPU as Group 1. */
static inline void enable_priv(unsigned cpu, uint32_t id)
{
	wr32(S(cpu, 0x80), rd32(S(cpu, 0x80)) | (1u << id));
	wr32(S(cpu, 0x100), 1u << id);
}
static inline void set_prio(uint32_t id, uint8_t p) { wr8(D(0x400 + id), p); }

#endif
