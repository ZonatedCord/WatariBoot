/* SPDX-License-Identifier: MIT */
/* Host tests: the platform switch (boot/plat_select.c) and the M1 platform table (boot/plat_m1.c, real code). The QEMU
 * table needs SMC and the physical GIC and is not host-buildable: a stand-in with the same name and window constants
 * takes its place here; what is tested for it is only that the switch picks it. */
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

/* what plat_m1.c's stubs call */
unsigned char g_stub_fatal;
void puts_(const char *s) { (void)s; }
void __attribute__((noreturn)) fatal_msg(const char *why) { (void)why; exit(99); }

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

int main(void)
{
	RUN(t_select);
	RUN(t_m1_table);
	TESTS_DONE();
}
