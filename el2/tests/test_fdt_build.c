/* SPDX-License-Identifier: MIT */
/* Host tests (T12b/E5, E6): the FDT writer, the guest tree of DESIGN watari-edk2-m1 4.2, and the placement / ordering of
 * the guest's RAM ranges. The output is parsed back with the layer's own reader (boot/fdt.c), and with dtc when it is
 * installed. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "fdt.h"
#include "wel2_fdt_build.h"

#define MB 0x100000ULL
#define GB 0x40000000ULL

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t be64(const uint8_t *p)
{
	return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

/* ---- property collector over fdt_walk_props ---- */
struct pr {
	unsigned depth;
	char node[64], name[48];
	uint8_t val[96];
	uint32_t len;
};
struct col {
	struct pr p[256];
	unsigned n;
};

static bool collect_cb(void *ctx, unsigned depth, const char *node, const char *prop, uint8_t *val, uint32_t len)
{
	struct col *c = ctx;
	struct pr *p = &c->p[c->n++];

	p->depth = depth;
	strncpy(p->node, node, sizeof(p->node) - 1);
	strncpy(p->name, prop, sizeof(p->name) - 1);
	p->len = len;
	memcpy(p->val, val, len < sizeof(p->val) ? len : sizeof(p->val));
	return false;
}

static int collect(void *fdt, struct col *c)
{
	memset(c, 0, sizeof(*c));
	return fdt_walk_props(fdt, collect_cb, c);
}

/* nth property `name` of the node whose name starts with `node` (prefix, so "memory@" matches every memory node) */
static const struct pr *find(const struct col *c, const char *node, const char *name, unsigned nth)
{
	for (unsigned i = 0; i < c->n; i++)
		if (!strncmp(c->p[i].node, node, strlen(node)) && !strcmp(c->p[i].name, name) && nth-- == 0)
			return &c->p[i];
	return NULL;
}

static bool contains(const uint8_t *buf, uint32_t n, const char *s)
{
	size_t k = strlen(s);

	for (uint32_t i = 0; i + k <= n; i++)
		if (!memcmp(buf + i, s, k))
			return true;
	return false;
}

/* ---- the writer ---- */

static uint8_t B[WEL2_GFDT_MAX + 64];

static void build_small(struct wel2_fdtw *w)
{
	static const char *const compat[] = { "a,b", "c,d" };
	static const uint8_t raw[5] = { 1, 2, 3, 4, 5 };

	wel2_fdtw_begin(w, "");
	wel2_fdtw_prop_u32(w, "#address-cells", 2);
	wel2_fdtw_begin(w, "n@10");
	wel2_fdtw_prop_u32(w, "x", 0x11223344);
	wel2_fdtw_prop_u64(w, "y", 0x1122334455667788ULL);
	wel2_fdtw_prop_str(w, "s", "hello");
	wel2_fdtw_prop_strlist(w, "l", compat, 2);
	wel2_fdtw_prop(w, "raw", raw, sizeof(raw));
	wel2_fdtw_prop_empty(w, "flag");
	wel2_fdtw_end(w);
	wel2_fdtw_begin(w, "m");
	wel2_fdtw_prop_u32(w, "x", 7);              /* same name as above: de-duplicated */
	wel2_fdtw_end(w);
	wel2_fdtw_end(w);
}

static void t_writer_roundtrip(void)
{
	struct wel2_fdtw w;
	uint32_t size = 0;
	struct col c;

	wel2_fdtw_init(&w, B, sizeof(B));
	build_small(&w);
	CHECK_EQ(wel2_fdtw_finish(&w, &size), 0);
	CHECK_EQ(be32(B), 0xd00dfeedu);
	CHECK_EQ(be32(B + 4), size);
	CHECK_EQ(size & 7, 0);
	CHECK_EQ(be32(B + 20), 17);
	CHECK_EQ(be32(B + 24), 16);
	CHECK_EQ(be32(B + 16), 40);                 /* off_mem_rsvmap */
	CHECK_EQ(be32(B + 8), 56);                  /* off_dt_struct */
	CHECK_EQ(be32(B + 12), 56 + be32(B + 36));  /* strings follow the structure block */
	CHECK_EQ(fdt_check(B), 0);
	CHECK_EQ(fdt_totalsize(B), size);
	CHECK_EQ(fdt_used_size(B), size);
	/* the empty reservation map */
	for (unsigned i = 40; i < 56; i++)
		CHECK_EQ(B[i], 0);
	/* de-duplication: "x" once. Names: #address-cells, x, y, s, l, raw, flag */
	CHECK_EQ(be32(B + 32), strlen("#address-cells") + 1 + 2 + 2 + 2 + 2 + 4 + 5);

	CHECK_EQ(collect(B, &c), 0);
	CHECK_EQ(c.n, 8);
	const struct pr *p;

	CHECK((p = find(&c, "", "#address-cells", 0)) != NULL && p->depth == 0 && p->len == 4 && be32(p->val) == 2);
	CHECK((p = find(&c, "n@10", "x", 0)) != NULL && p->depth == 1 && be32(p->val) == 0x11223344);
	CHECK((p = find(&c, "n@10", "y", 0)) != NULL && p->len == 8 && be64(p->val) == 0x1122334455667788ULL);
	CHECK((p = find(&c, "n@10", "s", 0)) != NULL && p->len == 6 && !strcmp((const char *)p->val, "hello"));
	CHECK((p = find(&c, "n@10", "l", 0)) != NULL && p->len == 8 && !memcmp(p->val, "a,b\0c,d\0", 8));
	CHECK((p = find(&c, "n@10", "raw", 0)) != NULL && p->len == 5 && !memcmp(p->val, "\1\2\3\4\5", 5));
	CHECK((p = find(&c, "n@10", "flag", 0)) != NULL && p->len == 0);
	CHECK((p = find(&c, "m", "x", 0)) != NULL && be32(p->val) == 7);
}

static void t_writer_overflow(void)
{
	struct wel2_fdtw w;
	uint32_t need = 0, size;
	static uint8_t big[4096], small[4096 + 16];

	wel2_fdtw_init(&w, big, sizeof(big));
	build_small(&w);
	CHECK_EQ(wel2_fdtw_finish(&w, &need), 0);
	/* every capacity below the need is an error and writes nothing past the buffer; the exact need works */
	for (uint32_t cap = 0; cap < need; cap++) {
		memset(small, 0xA5, sizeof(small));
		wel2_fdtw_init(&w, small, cap);
		build_small(&w);
		int rc = wel2_fdtw_finish(&w, &size);

		CHECK(rc < 0);
		if (rc >= 0)
			break;
		for (uint32_t i = cap; i < sizeof(small); i++)
			if (small[i] != 0xA5) {
				CHECK(0 && "write past the buffer");
				break;
			}
	}
	memset(small, 0xA5, sizeof(small));
	wel2_fdtw_init(&w, small, need);
	build_small(&w);
	CHECK_EQ(wel2_fdtw_finish(&w, &size), 0);
	CHECK_EQ(size, need);
	CHECK(small[need] == 0xA5);
	CHECK(!memcmp(small, big, need));
	/* the name table is bounded too: many distinct property names */
	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "");
	for (unsigned i = 0; i < 200 && !w.err; i++) {
		char nm[16];

		snprintf(nm, sizeof(nm), "prop-number-%u", i);
		wel2_fdtw_prop_u32(&w, nm, i);
	}
	CHECK_EQ(w.err, WEL2_FDTW_E_OVERFLOW);
}

static void t_writer_state(void)
{
	struct wel2_fdtw w;
	uint32_t size;

	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_end(&w);                          /* end without begin */
	CHECK_EQ(w.err, WEL2_FDTW_E_STATE);
	wel2_fdtw_begin(&w, "");                    /* sticky: nothing more is accepted */
	CHECK_EQ(w.err, WEL2_FDTW_E_STATE);
	CHECK(wel2_fdtw_finish(&w, &size) < 0);

	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_prop_u32(&w, "x", 1);             /* property outside a node */
	CHECK_EQ(w.err, WEL2_FDTW_E_STATE);

	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "");
	wel2_fdtw_end(&w);
	wel2_fdtw_begin(&w, "");                    /* a second root */
	CHECK_EQ(w.err, WEL2_FDTW_E_STATE);

	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "");
	wel2_fdtw_begin(&w, "a");                   /* finish with an open node */
	CHECK_EQ(wel2_fdtw_finish(&w, &size), -WEL2_FDTW_E_STATE);

	wel2_fdtw_init(&w, B, sizeof(B));
	CHECK_EQ(wel2_fdtw_finish(&w, &size), -WEL2_FDTW_E_STATE);   /* nothing written */

	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "");
	for (unsigned i = 0; i < WEL2_FDTW_MAX_DEPTH + 1 && !w.err; i++)
		wel2_fdtw_begin(&w, "d");
	CHECK_EQ(w.err, WEL2_FDTW_E_DEPTH);

	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "x");                   /* the root is "" */
	CHECK_EQ(w.err, WEL2_FDTW_E_NAME);
	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "");
	wel2_fdtw_begin(&w, "");                    /* only the root may be unnamed */
	CHECK_EQ(w.err, WEL2_FDTW_E_NAME);
	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "");
	wel2_fdtw_begin(&w, "a/b");
	CHECK_EQ(w.err, WEL2_FDTW_E_NAME);
	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "");
	wel2_fdtw_prop_u32(&w, "has space", 1);
	CHECK_EQ(w.err, WEL2_FDTW_E_NAME);
	wel2_fdtw_init(&w, B, sizeof(B));
	wel2_fdtw_begin(&w, "");
	wel2_fdtw_prop_u32(&w, "", 1);
	CHECK_EQ(w.err, WEL2_FDTW_E_NAME);

	wel2_fdtw_init(&w, NULL, 100);
	CHECK(w.err != 0);
}

static void t_forbidden(void)
{
	static const char *const bad[] = { "serial-number", "local-mac-address", "kaslr-seed", "rng-seed",
					   "mac-address", "linux,rng-seed" };
	struct wel2_fdtw w;

	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(wel2_fdtw_forbidden(bad[i]));
		wel2_fdtw_init(&w, B, sizeof(B));
		wel2_fdtw_begin(&w, "");
		wel2_fdtw_prop_u32(&w, bad[i], 1);
		CHECK_EQ(w.err, WEL2_FDTW_E_FORBIDDEN);
		wel2_fdtw_init(&w, B, sizeof(B));
		wel2_fdtw_begin(&w, "");
		wel2_fdtw_prop_str(&w, bad[i], "x");
		CHECK_EQ(w.err, WEL2_FDTW_E_FORBIDDEN);
		wel2_fdtw_init(&w, B, sizeof(B));
		wel2_fdtw_begin(&w, "");
		wel2_fdtw_prop(&w, bad[i], "ab", 2);
		CHECK_EQ(w.err, WEL2_FDTW_E_FORBIDDEN);
		wel2_fdtw_init(&w, B, sizeof(B));
		wel2_fdtw_begin(&w, "");
		wel2_fdtw_prop_empty(&w, bad[i]);
		CHECK_EQ(w.err, WEL2_FDTW_E_FORBIDDEN);
		wel2_fdtw_init(&w, B, sizeof(B));
		wel2_fdtw_begin(&w, "");
		wel2_fdtw_prop_cells(&w, bad[i], (const uint32_t[]){ 1, 2 }, 2);
		CHECK_EQ(w.err, WEL2_FDTW_E_FORBIDDEN);
	}
	CHECK(!wel2_fdtw_forbidden("serial"));          /* a prefix is not the name */
	CHECK(!wel2_fdtw_forbidden("serial-numbers"));
	CHECK(!wel2_fdtw_forbidden("compatible"));
}

/* ---- the guest tree ---- */

/* j293-shaped: the guest ranges the layer computes from the day-1 map (usable RAM from 0x8_0000_0000 minus the layer's
 * 32 MB block and the reserved ranges, rounded inward to 32 MB), 8 cores with the measured MPIDRs, the vGIC windows of
 * plat_m1, the MSI frame, a 2560 x 1600 x2r10g10b10 framebuffer. */
static const struct wel2_range J293[] = {
	{ 0x804000000ULL, 0x2c000000ULL },
	{ 0x832000000ULL, 0x4e000000ULL },
	{ 0x8a2000000ULL, 0x5e000000ULL },
	{ 0x908000000ULL, 0x98000000ULL },
	{ 0x9b0000000ULL, 0x2e000000ULL },
};
static const uint64_t J293_MPIDR[8] = { 0x0, 0x1, 0x2, 0x3, 0x10100, 0x10101, 0x10102, 0x10103 };
static const struct wel2_gfdt_fb J293_FB = { 0x9e2570000ULL, 2560u * 1600u * 4u, 2560, 1600, 2560 * 4, "x2r10g10b10" };

static void fill_j293(struct wel2_gfdt_in *in, const struct wel2_range *ordered, unsigned n)
{
	memset(in, 0, sizeof(*in));
	in->platform = "t8103";
	in->ranges = ordered;
	in->nranges = n;
	in->gicd_base = 0xF00000000ULL;
	in->gicr_base = 0xF10000000ULL;
	in->gicr_stride = 0x20000;
	in->ncores = 8;
	memcpy(in->mpidr, J293_MPIDR, sizeof(J293_MPIDR));
	in->msi_frame_base = 0xFFFFF000ULL;
	in->fb_mode = "band";
	in->fb = &J293_FB;
}

static void check_no_identifying(const uint8_t *fdt, uint32_t size)
{
	CHECK(!contains(fdt, size, "serial-number"));
	CHECK(!contains(fdt, size, "local-mac-address"));
	CHECK(!contains(fdt, size, "mac-address"));
	CHECK(!contains(fdt, size, "kaslr-seed"));
	CHECK(!contains(fdt, size, "rng-seed"));
}

static void t_guest_j293(void)
{
	struct wel2_gfdt_in in;
	uint32_t size = 0;
	struct col c;
	const struct pr *p;
	struct wel2_ram ram;
	struct fdt_reg_info ri;
	struct wel2_fb fb;

	fill_j293(&in, J293, 5);
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), 0);
	printf("     j293 guest FDT: %u bytes\n", (unsigned)size);
	CHECK(size > 0 && size <= WEL2_GFDT_MAX);
	CHECK_EQ(fdt_check(B), 0);
	CHECK_EQ(collect(B, &c), 0);
	check_no_identifying(B, size);

	CHECK((p = find(&c, "", "#address-cells", 0)) && be32(p->val) == 2);
	CHECK((p = find(&c, "", "#size-cells", 0)) && be32(p->val) == 2);
	CHECK((p = find(&c, "", "compatible", 0)) && !strcmp((const char *)p->val, "watari,el2-guest"));
	CHECK(find(&c, "", "model", 0) == NULL);
	/* memory nodes: order and values */
	for (unsigned i = 0; i < 5; i++) {
		CHECK((p = find(&c, "memory@", "reg", i)) != NULL);
		CHECK(p && p->len == 16 && be64(p->val) == J293[i].base && be64(p->val + 8) == J293[i].size);
		CHECK((p = find(&c, "memory@", "device_type", i)) && !strcmp((const char *)p->val, "memory"));
	}
	CHECK(find(&c, "memory@", "reg", 5) == NULL);
	CHECK(!strcmp(find(&c, "memory@", "reg", 0)->node, "memory@804000000"));
	/* the layer's own reader: the first memory node is the image's range */
	CHECK_EQ(fdt_get_memory(B, &ram, 0, 1ULL << 40), 0);
	CHECK_EQ(ram.base, J293[0].base);
	CHECK_EQ(ram.end, J293[0].base + J293[0].size);
	/* psci */
	CHECK((p = find(&c, "psci", "method", 0)) && !strcmp((const char *)p->val, "hvc"));
	CHECK((p = find(&c, "psci", "compatible", 0)) && p->len == 26 && !memcmp(p->val, "arm,psci-1.0\0arm,psci-0.2\0", 26));
	/* intc: GICD 64 KB, GICR 8 x 128 KB; the reader finds it by its compatible */
	CHECK_EQ(fdt_find_compatible_reg(B, "arm,gic-v3", &ri), 0);
	CHECK(ri.found && ri.base == 0xF00000000ULL && ri.size == 0x10000);
	CHECK((p = find(&c, "intc@f00000000", "reg", 0)) && p->len == 32);
	CHECK(p && be64(p->val + 16) == 0xF10000000ULL && be64(p->val + 24) == 8 * 0x20000ULL);
	CHECK((p = find(&c, "intc@", "#interrupt-cells", 0)) && be32(p->val) == 3);
	CHECK(find(&c, "intc@", "interrupt-controller", 0) != NULL);
	CHECK((p = find(&c, "intc@", "phandle", 0)) && be32(p->val) == 1);
	/* timer: PPIs 13, 14, 11, 10, level low, parent = the intc */
	CHECK((p = find(&c, "timer", "interrupts", 0)) && p->len == 48);
	if (p) {
		static const uint32_t want[12] = { 1, 13, 4, 1, 14, 4, 1, 11, 4, 1, 10, 4 };

		for (unsigned i = 0; i < 12; i++)
			CHECK_EQ(be32(p->val + 4 * i), want[i]);
	}
	CHECK((p = find(&c, "timer", "interrupt-parent", 0)) && be32(p->val) == 1);
	CHECK(find(&c, "timer", "always-on", 0) != NULL);
	/* cpus */
	for (unsigned i = 0; i < 8; i++) {
		CHECK((p = find(&c, "cpu@", "reg", i)) && p->len == 8);
		CHECK(p && be64(p->val) == J293_MPIDR[i]);
		CHECK((p = find(&c, "cpu@", "enable-method", i)) && !strcmp((const char *)p->val, "psci"));
	}
	CHECK(find(&c, "cpu@", "reg", 8) == NULL);
	CHECK(!strcmp(find(&c, "cpu@", "reg", 4)->node, "cpu@10100"));
	/* the framebuffer, through the reader the layer uses for m1n1's node */
	CHECK_EQ(fdt_get_framebuffer(B, &fb), 0);
	CHECK(fb.fmt == WEL2_FB_X2R10G10B10 && fb.base == 0x9e2570000ULL && fb.width == 2560 && fb.height == 1600 &&
	      fb.stride == 2560 * 4);
	/* /chosen/watari,el2 */
	CHECK((p = find(&c, "watari,el2", "compatible", 0)) && !strcmp((const char *)p->val, "watari,el2"));
	CHECK((p = find(&c, "watari,el2", "version", 0)) && be32(p->val) == 1);
	CHECK((p = find(&c, "watari,el2", "platform", 0)) && !strcmp((const char *)p->val, "t8103"));
	CHECK((p = find(&c, "watari,el2", "hvc-vendor-base", 0)) && be32(p->val) == 0xC6000000u);
	CHECK((p = find(&c, "watari,el2", "gicd-base", 0)) && be64(p->val) == 0xF00000000ULL);
	CHECK((p = find(&c, "watari,el2", "gicr-base", 0)) && be64(p->val) == 0xF10000000ULL);
	CHECK((p = find(&c, "watari,el2", "gicr-stride", 0)) && be32(p->val) == 0x20000);
	CHECK((p = find(&c, "watari,el2", "msi-frame-base", 0)) && be64(p->val) == 0xFFFFF000ULL);
	CHECK((p = find(&c, "watari,el2", "fb-mode", 0)) && !strcmp((const char *)p->val, "band"));
	/* nothing of the device world of the host */
	for (unsigned i = 0; i < c.n; i++) {
		CHECK(strncmp(c.p[i].node, "aic", 3) && strncmp(c.p[i].node, "serial", 6) && strncmp(c.p[i].node, "uart", 4));
		CHECK(strcmp(c.p[i].name, "interrupt-controller") || !strncmp(c.p[i].node, "intc@", 5));
	}
}

static void t_guest_qemu(void)
{
	/* QEMU-shaped (-m 2G, wel2.split=3): the carve-out end, 32 MB holes between the pieces, 4 cores, QEMU's GIC, no MSI
	 * frame, no framebuffer. */
	static const struct wel2_range Q[] = {
		{ 0x40200000ULL, 0x20000000ULL - 0x200000ULL },
		{ 0x62000000ULL, 0x20000000ULL - 0x2000000ULL },
		{ 0x82000000ULL, 0x3e000000ULL },
	};
	struct wel2_gfdt_in in;
	uint32_t size = 0;
	struct col c;
	const struct pr *p;
	struct wel2_fb fb;

	memset(&in, 0, sizeof(in));
	in.platform = "qemu-virt";
	in.ranges = Q;
	in.nranges = 3;
	in.gicd_base = 0x08000000ULL;
	in.gicr_base = 0x080A0000ULL;
	in.ncores = 4;
	for (unsigned i = 0; i < 4; i++)
		in.mpidr[i] = i;
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), 0);
	printf("     QEMU guest FDT: %u bytes\n", (unsigned)size);
	CHECK(size <= WEL2_GFDT_MAX);
	CHECK_EQ(collect(B, &c), 0);
	check_no_identifying(B, size);
	CHECK((p = find(&c, "memory@", "reg", 0)) && be64(p->val) == 0x40200000ULL);
	CHECK((p = find(&c, "memory@", "reg", 2)) && be64(p->val) == 0x82000000ULL);
	CHECK((p = find(&c, "intc@8000000", "reg", 0)) && be64(p->val + 16) == 0x080A0000ULL && be64(p->val + 24) == 4 * 0x20000ULL);
	CHECK(find(&c, "watari,el2", "msi-frame-base", 0) == NULL);        /* none given: none emitted */
	CHECK((p = find(&c, "watari,el2", "fb-mode", 0)) && !strcmp((const char *)p->val, "none"));
	CHECK((p = find(&c, "watari,el2", "gicr-stride", 0)) && be32(p->val) == 0x20000);   /* 0 = default */
	CHECK_EQ(fdt_get_framebuffer(B, &fb), 0);
	CHECK(fb.fmt == WEL2_FB_NONE);                                      /* no node */
	CHECK(find(&c, "framebuffer@", "reg", 0) == NULL);
	CHECK((p = find(&c, "cpu@", "reg", 3)) && be64(p->val) == 3);
	CHECK(!strcmp(find(&c, "cpu@", "reg", 0)->node, "cpu@0"));
}

static void t_guest_bounds(void)
{
	/* 8 cores and 8 ranges stay far below 32 KB - 64 B; the largest legal input is bounded by the same constant. */
	struct wel2_range r[WEL2_MAX_GUEST_RANGES];
	struct wel2_gfdt_in in;
	uint32_t size = 0;

	for (unsigned i = 0; i < WEL2_MAX_GUEST_RANGES; i++) {
		r[i].base = 0x800000000ULL + (uint64_t)i * 0x4000000ULL;
		r[i].size = 0x2000000ULL;
	}
	fill_j293(&in, r, 8);
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), 0);
	CHECK(size <= WEL2_GFDT_MAX - 1024);        /* real headroom, not a squeak */
	printf("     8 cores, 8 ranges, framebuffer: %u bytes\n", (unsigned)size);
	fill_j293(&in, r, WEL2_MAX_GUEST_RANGES);
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), 0);
	CHECK(size <= WEL2_GFDT_MAX);
	printf("     8 cores, %u ranges, framebuffer: %u bytes\n", (unsigned)WEL2_MAX_GUEST_RANGES, (unsigned)size);

	/* the buffer bound: any capacity below the need is an error, never a truncation */
	uint32_t need = size;
	static uint8_t tiny[WEL2_GFDT_MAX];

	for (uint32_t cap = 0; cap < need; cap += 97)
		CHECK_EQ(guest_fdt_build(tiny, cap, &in, &size), -1);
	CHECK_EQ(guest_fdt_build(tiny, need - 1, &in, &size), -1);
	CHECK_EQ(guest_fdt_build(tiny, need, &in, &size), 0);
	CHECK_EQ(size, need);
	/* a huge buffer does not raise the bound: the result is the same blob */
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), 0);
	CHECK_EQ(size, need);
}

static void t_guest_bad_input(void)
{
	struct wel2_gfdt_in in;
	uint32_t size;
	struct wel2_gfdt_fb fb = J293_FB;

	fill_j293(&in, J293, 5);
	CHECK_EQ(guest_fdt_build(B, sizeof(B), NULL, &size), -1);
	in.ncores = 0;
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), -1);
	in.ncores = 9;
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), -1);
	fill_j293(&in, J293, 0);
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), -1);
	fill_j293(&in, J293, 5);
	in.fb_mode = "everything";
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), -1);
	fill_j293(&in, J293, 5);
	fb.format = "r5g6b5";
	in.fb = &fb;
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), -1);
	fill_j293(&in, J293, 5);
	in.platform = NULL;
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), -1);
	{
		struct wel2_range z[1] = { { 0x800000000ULL, 0 } };

		fill_j293(&in, z, 1);
		CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), -1);
	}
}

static void t_dtc(void)
{
	struct wel2_gfdt_in in;
	uint32_t size = 0;
	FILE *f;
	char cmd[256], line[512];
	bool saw_mem = false, saw_psci = false, saw_timer = false, saw_serial = false;

	if (system("command -v dtc >/dev/null 2>&1") != 0) {
		printf("     SKIP dtc round trip: dtc is not installed\n");
		return;
	}
	fill_j293(&in, J293, 5);
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), 0);
	f = fopen("build/gfdt_j293.dtb", "wb");
	CHECK(f != NULL);
	if (!f)
		return;
	fwrite(B, 1, size, f);
	fclose(f);
	/* dtc must accept it and say the things we put in (warnings on stderr are not failures) */
	snprintf(cmd, sizeof(cmd), "dtc -I dtb -O dts build/gfdt_j293.dtb 2>/dev/null");
	f = popen(cmd, "r");
	CHECK(f != NULL);
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		saw_mem |= strstr(line, "memory@804000000 {") != NULL;
		saw_psci |= strstr(line, "method = \"hvc\"") != NULL;
		saw_timer |= strstr(line, "arm,armv8-timer") != NULL;
		saw_serial |= strstr(line, "serial-number") != NULL || strstr(line, "mac-address") != NULL ||
			      strstr(line, "seed") != NULL;
	}
	CHECK_EQ(pclose(f), 0);
	CHECK(saw_mem && saw_psci && saw_timer);
	CHECK(!saw_serial);
	/* and re-encodes it into a blob our own reader accepts, with the same properties */
	CHECK_EQ(system("dtc -I dtb -O dtb -o build/gfdt_j293.re.dtb build/gfdt_j293.dtb 2>/dev/null"), 0);
	f = fopen("build/gfdt_j293.re.dtb", "rb");
	CHECK(f != NULL);
	if (f) {
		static uint8_t re[65536];
		size_t n = fread(re, 1, sizeof(re), f);
		struct col a, b;

		fclose(f);
		CHECK(n > 0);
		CHECK_EQ(fdt_check(re), 0);
		CHECK_EQ(collect(B, &a), 0);
		CHECK_EQ(collect(re, &b), 0);
		CHECK_EQ(a.n, b.n);
		for (unsigned i = 0; i < a.n && i < b.n; i++)
			CHECK(!strcmp(a.p[i].name, b.p[i].name) && a.p[i].len == b.p[i].len &&
			      !memcmp(a.p[i].val, b.p[i].val, a.p[i].len < sizeof(a.p[i].val) ? a.p[i].len : sizeof(a.p[i].val)));
	}
}

/* ---- placement and ordering ---- */

#define FDTMAX WEL2_GFDT_MAX

static void t_place_qemu_bench(void)
{
	/* the QEMU bench, -m 512M: [0x40200000, 0x60000000), EDK2 at the carve end + 0x80000 */
	static const struct wel2_range R[] = { { 0x40200000ULL, 0x1fe00000ULL } };
	struct wel2_gfdt_place p;

	CHECK_EQ(wel2_gfdt_place(R, 1, 0x40280000ULL, 0x200000, FDTMAX, &p), WEL2_GFP_OK);
	CHECK_EQ(p.fdt, 0x40480000ULL);                    /* 0x40280000 + 0x200000, already 16 KB aligned */
	CHECK((p.fdt & (WEL2_GFDT_ALIGN - 1)) == 0);
	CHECK_EQ(p.fdt_end, p.fdt + FDTMAX);
	CHECK_EQ(p.n, 1);
	CHECK(!p.entry_2mb_aligned);
	CHECK_EQ(wel2_gfdt_place(R, 1, 0x40400000ULL, 0x200001, FDTMAX, &p), WEL2_GFP_OK);
	CHECK(p.entry_2mb_aligned);
	CHECK_EQ(p.image_end, 0x40600001ULL);
	CHECK_EQ(p.fdt, 0x40604000ULL);                    /* rounded up to 16 KB */
}

static void t_place_first_range_holds_everything(void)
{
	/* The invariant of PLAN E6: the first /memory range holds the image, the FDT and PrePi's 64 MB UEFI region. */
	struct wel2_gfdt_place p;
	const struct wel2_range *rs = J293;

	for (unsigned k = 0; k < 5; k++) {
		/* entry at the start of each range's first 32 MB block */
		uint64_t entry = rs[k].base;

		int rc = wel2_gfdt_place(rs, 5, entry, 0x300000, FDTMAX, &p);

		if (rs[k].size < WEL2_GFDT_MIN_RANGE || rs[k].size < 0x300000 + FDTMAX + WEL2_GFDT_UEFI_REGION)
			continue;
		CHECK_EQ(rc, WEL2_GFP_OK);
		if (rc)
			continue;
		CHECK_EQ(p.n, 5);
		CHECK(p.ordered[0].base == rs[k].base && p.ordered[0].size == rs[k].size);
		CHECK(p.entry >= p.ordered[0].base);
		CHECK(p.image_end <= p.ordered[0].base + p.ordered[0].size);
		CHECK(p.fdt >= p.image_end && p.fdt_end <= p.ordered[0].base + p.ordered[0].size);
		/* the UEFI region at the top of the range is disjoint from the image and the FDT */
		CHECK(p.fdt_end <= p.ordered[0].base + p.ordered[0].size - WEL2_GFDT_UEFI_REGION);
		/* every other range is listed once, in its original order, none lost */
		unsigned seen = 0;

		for (unsigned j = 1; j < p.n; j++)
			for (unsigned i = 0; i < 5; i++)
				if (rs[i].base == p.ordered[j].base && rs[i].size == p.ordered[j].size) {
					CHECK(i != k);
					seen++;
				}
		CHECK_EQ(seen, 4);
	}
	/* ordering of the rest: original order minus the first */
	CHECK_EQ(wel2_gfdt_place(J293, 5, J293[2].base, 0x100000, FDTMAX, &p), WEL2_GFP_OK);
	CHECK(p.ordered[0].base == J293[2].base);
	CHECK(p.ordered[1].base == J293[0].base && p.ordered[2].base == J293[1].base);
	CHECK(p.ordered[3].base == J293[3].base && p.ordered[4].base == J293[4].base);
	/* and the result feeds the builder: the first /memory node is the image's range */
	struct wel2_gfdt_in in;
	uint32_t size;
	struct wel2_ram ram;

	fill_j293(&in, p.ordered, p.n);
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), 0);
	CHECK_EQ(fdt_get_memory(B, &ram, 0, 1ULL << 40), 0);
	CHECK_EQ(ram.base, J293[2].base);
}

static void t_place_above_4gb(void)
{
	/* the -m 6G run: RAM from 0x40000000 to 0x1_C000_0000; the image goes above 4 GB explicitly */
	static const struct wel2_range R[] = {
		{ 0x40200000ULL, 0xc0000000ULL - 0x200000ULL },      /* 0x40200000 .. 0x1_0000_0000 */
		{ 0x100000000ULL, 0xc0000000ULL },                    /* 0x1_0000_0000 .. 0x1_C000_0000 */
	};
	struct wel2_gfdt_place p;

	CHECK_EQ(wel2_gfdt_place(R, 2, 0x100000000ULL, 0x200000, FDTMAX, &p), WEL2_GFP_OK);
	CHECK_EQ(p.ordered[0].base, 0x100000000ULL);
	CHECK_EQ(p.ordered[1].base, 0x40200000ULL);
	CHECK_EQ(p.fdt, 0x100200000ULL);
	CHECK(p.fdt > 0xffffffffULL);
	CHECK_EQ(wel2_gfdt_place(R, 2, 0x180000000ULL, 0x200000, FDTMAX, &p), WEL2_GFP_OK);
	/* the 64-bit values survive the tree */
	struct wel2_gfdt_in in;
	uint32_t size;
	struct col c;
	const struct pr *q;

	memset(&in, 0, sizeof(in));
	in.platform = "qemu-virt";
	in.ranges = p.ordered;
	in.nranges = p.n;
	in.gicd_base = 0x08000000ULL;
	in.gicr_base = 0x080A0000ULL;
	in.ncores = 1;
	CHECK_EQ(guest_fdt_build(B, sizeof(B), &in, &size), 0);
	CHECK_EQ(collect(B, &c), 0);
	CHECK((q = find(&c, "memory@", "reg", 0)) && be64(q->val) == 0x100000000ULL && be64(q->val + 8) == 0xc0000000ULL);
	CHECK(!strcmp(find(&c, "memory@", "reg", 0)->node, "memory@100000000"));
}

static void t_place_refusals(void)
{
	static const struct wel2_range R[] = {
		{ 0x40200000ULL, 0x4000000ULL },                      /* 64 MB: too small */
		{ 0x80000000ULL, 0x8000000ULL },                      /* exactly 128 MB */
		{ 0x100000000ULL, 0xc0000000ULL },
	};
	struct wel2_gfdt_place p;

	CHECK_EQ(wel2_gfdt_place(R, 3, 0x40200000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_SMALL_RANGE);
	/* 128 MB exactly: the image + FDT + 64 MB fit at the bottom */
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x80000000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_OK);
	/* ... but not high in the range: the UEFI region would overlap the image */
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x80000000ULL + 0x4000000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_UEFI_REGION);
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x80000000ULL + 0x3ff0000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_UEFI_REGION);
	/* image past the end of its range */
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x80000000ULL + 0x7f00000ULL, 0x200000, FDTMAX, &p), WEL2_GFP_IMAGE_OUT);
	/* the image just fits, the FDT does not */
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x80000000ULL + 0x8000000ULL - 0x100000, 0x100000, FDTMAX, &p), WEL2_GFP_IMAGE_OUT);
	/* entry not in RAM, in a hole, below, above */
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x70000000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_NOT_IN_RAM);
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x20000000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_NOT_IN_RAM);
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x300000000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_NOT_IN_RAM);
	/* alignment, size, wrap, no ranges */
	CHECK_EQ(wel2_gfdt_place(R, 3, 0, 0x100000, FDTMAX, &p), WEL2_GFP_BAD_ENTRY);
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x100000800ULL, 0x100000, FDTMAX, &p), WEL2_GFP_BAD_ENTRY);
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x100000000ULL, 0, FDTMAX, &p), WEL2_GFP_BAD_SIZE);
	CHECK_EQ(wel2_gfdt_place(R, 3, 0x100000000ULL, 0x2000001ULL, FDTMAX, &p), WEL2_GFP_BAD_SIZE);
	CHECK_EQ(wel2_gfdt_place(R, 3, 0xfffffffffffff000ULL, 0x2000, FDTMAX, &p), WEL2_GFP_BAD_SIZE);
	CHECK_EQ(wel2_gfdt_place(R, 0, 0x100000000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_NOT_IN_RAM);
	CHECK_EQ(wel2_gfdt_place(R, WEL2_MAX_GUEST_RANGES + 1, 0x100000000ULL, 0x100000, FDTMAX, &p), WEL2_GFP_TOO_MANY);
	/* a range that wraps is ignored, not trusted */
	static const struct wel2_range W[] = { { 0xfffffffffff00000ULL, 0x200000ULL }, { 0x100000000ULL, 0xc0000000ULL } };

	CHECK_EQ(wel2_gfdt_place(W, 2, 0xfffffffffff00000ULL, 0x1000, FDTMAX, &p), WEL2_GFP_NOT_IN_RAM);
	CHECK_EQ(wel2_gfdt_place(W, 2, 0x100000000ULL, 0x1000, FDTMAX, &p), WEL2_GFP_OK);
	CHECK_EQ(p.n, 1);                                  /* the wrapping range is dropped from the order */
}

int main(void)
{
	RUN(t_writer_roundtrip);
	RUN(t_writer_overflow);
	RUN(t_writer_state);
	RUN(t_forbidden);
	RUN(t_guest_j293);
	RUN(t_guest_qemu);
	RUN(t_guest_bounds);
	RUN(t_guest_bad_input);
	RUN(t_dtc);
	RUN(t_place_qemu_bench);
	RUN(t_place_first_range_holds_everything);
	RUN(t_place_above_4gb);
	RUN(t_place_refusals);
	TESTS_DONE();
}
