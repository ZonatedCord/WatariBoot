// SPDX-License-Identifier: MIT
// Watari EL2 T12b/E5: flattened-device-tree writer, the guest tree of DESIGN watari-edk2-m1 4.2, and the placement /
// ordering of the guest's RAM ranges (include/wel2_fdt_build.h). Written from the Devicetree Specification (chapter 5,
// flattened format); no third-party code. No libc calls: the layer links this file freestanding.

#include "wel2_fdt_build.h"

#define FDT_MAGIC       0xd00dfeedu
#define FDT_BEGIN_NODE  1u
#define FDT_END_NODE    2u
#define FDT_PROP        3u
#define FDT_END         9u
#define FDT_HDR_SIZE    40u
#define FDT_STRUCT_OFF  56u          /* header + one terminating (zero) memory-reservation entry of 16 bytes */

static uint32_t align4(uint32_t v)
{
	return (v + 3u) & ~3u;
}

static uint32_t slen(const char *s)
{
	uint32_t n = 0;

	while (s[n])
		n++;
	return n;
}

static bool seq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void st32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

bool wel2_fdtw_forbidden(const char *name)
{
	static const char *const bad[] = {
		"serial-number", "local-mac-address", "mac-address", "kaslr-seed", "rng-seed", "linux,rng-seed",
	};

	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		if (seq(name, bad[i]))
			return true;
	return false;
}

static void fail(struct wel2_fdtw *w, int err)
{
	if (!w->err)
		w->err = err;
}

void wel2_fdtw_init(struct wel2_fdtw *w, void *buf, uint32_t cap)
{
	w->buf = buf;
	w->cap = cap;
	w->len = FDT_STRUCT_OFF;
	w->depth = 0;
	w->root_done = false;
	w->err = WEL2_FDTW_OK;
	w->strs_len = 0;
	if (!buf || cap < FDT_STRUCT_OFF + 8u)       /* the header, the reservation block, BEGIN + END at least */
		w->err = WEL2_FDTW_E_OVERFLOW;
}

/* Room for n more bytes of the structure block; keeps 4 bytes back for the END token. */
static bool room(struct wel2_fdtw *w, uint32_t n)
{
	if (w->err)
		return false;
	if (n > w->cap || w->len > w->cap - n || w->len + n > w->cap - 4u) {
		fail(w, WEL2_FDTW_E_OVERFLOW);
		return false;
	}
	return true;
}

static void put_tok(struct wel2_fdtw *w, uint32_t v)
{
	st32(w->buf + w->len, v);
	w->len += 4;
}

/* n bytes of data padded with zeros to a multiple of 4 (room() was checked by the caller for the padded size). */
static void put_data(struct wel2_fdtw *w, const uint8_t *d, uint32_t n)
{
	uint32_t padded = align4(n);

	for (uint32_t i = 0; i < n; i++)
		w->buf[w->len + i] = d[i];
	for (uint32_t i = n; i < padded; i++)
		w->buf[w->len + i] = 0;
	w->len += padded;
}

/* Node names: letters, digits and , . _ + - @ ; property names: the same plus ? and #. */
static bool name_ok(const char *s, bool is_prop)
{
	uint32_t n = slen(s);

	if (n == 0 || n > WEL2_FDTW_NAME_MAX)
		return false;
	for (uint32_t i = 0; i < n; i++) {
		char c = s[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ',' || c == '.' ||
			  c == '_' || c == '+' || c == '-' || (is_prop ? (c == '?' || c == '#') : c == '@');

		if (!ok)
			return false;
	}
	return true;
}

void wel2_fdtw_begin(struct wel2_fdtw *w, const char *name)
{
	uint32_t n;

	if (w->err)
		return;
	if (w->depth == 0 && w->root_done) {            /* one root only */
		fail(w, WEL2_FDTW_E_STATE);
		return;
	}
	if (w->depth >= WEL2_FDTW_MAX_DEPTH) {
		fail(w, WEL2_FDTW_E_DEPTH);
		return;
	}
	if (w->depth == 0 ? name[0] != 0 : !name_ok(name, false)) {   /* the root is "" and every other node has a name */
		fail(w, WEL2_FDTW_E_NAME);
		return;
	}
	n = slen(name) + 1u;
	if (!room(w, 4u + align4(n)))
		return;
	put_tok(w, FDT_BEGIN_NODE);
	put_data(w, (const uint8_t *)name, n);
	w->depth++;
	w->root_done = true;
}

void wel2_fdtw_end(struct wel2_fdtw *w)
{
	if (w->err)
		return;
	if (w->depth == 0) {
		fail(w, WEL2_FDTW_E_STATE);
		return;
	}
	if (!room(w, 4u))
		return;
	put_tok(w, FDT_END_NODE);
	w->depth--;
}

/* Offset of `name` in the strings block, appended when new (property names are de-duplicated); ~0u = full. */
static uint32_t str_off(struct wel2_fdtw *w, const char *name)
{
	uint32_t n = slen(name) + 1u, off = 0;

	while (off < w->strs_len) {
		if (seq(w->strs + off, name))
			return off;
		off += slen(w->strs + off) + 1u;
	}
	if (w->strs_len + n > WEL2_FDTW_STRS_CAP)
		return ~0u;
	for (uint32_t i = 0; i < n; i++)
		w->strs[w->strs_len + i] = name[i];
	off = w->strs_len;
	w->strs_len += n;
	return off;
}

static bool prop_head(struct wel2_fdtw *w, const char *name, uint32_t len)
{
	uint32_t off;

	if (w->err)
		return false;
	if (w->depth == 0) {
		fail(w, WEL2_FDTW_E_STATE);
		return false;
	}
	if (wel2_fdtw_forbidden(name)) {
		fail(w, WEL2_FDTW_E_FORBIDDEN);
		return false;
	}
	if (!name_ok(name, true)) {
		fail(w, WEL2_FDTW_E_NAME);
		return false;
	}
	if (len > w->cap || !room(w, 12u + align4(len)))
		return false;
	off = str_off(w, name);
	if (off == ~0u) {
		fail(w, WEL2_FDTW_E_OVERFLOW);
		return false;
	}
	put_tok(w, FDT_PROP);
	put_tok(w, len);
	put_tok(w, off);
	return true;
}

void wel2_fdtw_prop(struct wel2_fdtw *w, const char *name, const void *val, uint32_t len)
{
	if (len && !val) {
		fail(w, WEL2_FDTW_E_STATE);
		return;
	}
	if (!prop_head(w, name, len))
		return;
	put_data(w, (const uint8_t *)val, len);
}

void wel2_fdtw_prop_empty(struct wel2_fdtw *w, const char *name)
{
	wel2_fdtw_prop(w, name, 0, 0);
}

void wel2_fdtw_prop_u32(struct wel2_fdtw *w, const char *name, uint32_t v)
{
	wel2_fdtw_prop_cells(w, name, &v, 1);
}

void wel2_fdtw_prop_u64(struct wel2_fdtw *w, const char *name, uint64_t v)
{
	uint32_t c[2] = { (uint32_t)(v >> 32), (uint32_t)v };

	wel2_fdtw_prop_cells(w, name, c, 2);
}

void wel2_fdtw_prop_cells(struct wel2_fdtw *w, const char *name, const uint32_t *cells, unsigned n)
{
	if (n > 0x1000u) {                               /* a sanity bound, far above any real property */
		fail(w, WEL2_FDTW_E_OVERFLOW);
		return;
	}
	if (!prop_head(w, name, 4u * n))
		return;
	for (unsigned i = 0; i < n; i++)
		put_tok(w, cells[i]);
}

void wel2_fdtw_prop_str(struct wel2_fdtw *w, const char *name, const char *s)
{
	wel2_fdtw_prop(w, name, s, slen(s) + 1u);
}

void wel2_fdtw_prop_strlist(struct wel2_fdtw *w, const char *name, const char *const *strs, unsigned n)
{
	uint32_t total = 0;

	for (unsigned i = 0; i < n; i++)
		total += slen(strs[i]) + 1u;
	if (!prop_head(w, name, total))
		return;
	for (unsigned i = 0; i < n; i++) {
		uint32_t k = slen(strs[i]) + 1u;

		for (uint32_t j = 0; j < k; j++)
			w->buf[w->len + j] = (uint8_t)strs[i][j];
		w->len += k;
	}
	for (uint32_t pad = align4(total) - total; pad; pad--)
		w->buf[w->len++] = 0;
}

int wel2_fdtw_finish(struct wel2_fdtw *w, uint32_t *size)
{
	uint32_t struct_len, total;

	if (w->err)
		return -w->err;
	if (w->depth != 0 || !w->root_done)
		return -WEL2_FDTW_E_STATE;
	/* totalsize is the used bytes rounded up to 8 (the layer's reader, fdt_used_size, reports it that way). */
	total = (w->len + 4u + w->strs_len + 7u) & ~7u;
	if (total > w->cap || total < w->len)
		return -WEL2_FDTW_E_OVERFLOW;
	put_tok(w, FDT_END);
	struct_len = w->len - FDT_STRUCT_OFF;
	for (uint32_t i = 0; i < w->strs_len; i++)
		w->buf[w->len + i] = (uint8_t)w->strs[i];
	for (uint32_t i = w->len + w->strs_len; i < total; i++)
		w->buf[i] = 0;
	for (uint32_t i = 0; i < FDT_STRUCT_OFF; i++)          /* header and the empty reservation map first */
		w->buf[i] = 0;
	st32(w->buf + 0, FDT_MAGIC);
	st32(w->buf + 4, total);
	st32(w->buf + 8, FDT_STRUCT_OFF);
	st32(w->buf + 12, FDT_STRUCT_OFF + struct_len);
	st32(w->buf + 16, FDT_HDR_SIZE);                       /* off_mem_rsvmap */
	st32(w->buf + 20, 17);                                 /* version */
	st32(w->buf + 24, 16);                                 /* last_comp_version */
	st32(w->buf + 28, 0);                                  /* boot_cpuid_phys */
	st32(w->buf + 32, w->strs_len);
	st32(w->buf + 36, struct_len);
	if (size)
		*size = total;
	return 0;
}

/* ---- the guest tree ---------------------------------------------------------------------------------------------- */

/* "<prefix><lowercase hex of v>" into out (cap bytes); the unit-address form of the devicetree spec (no 0x, no padding). */
static void unit_name(char *out, unsigned cap, const char *prefix, uint64_t v)
{
	unsigned n = 0, digits = 1;
	uint64_t t = v >> 4;

	while (*prefix && n + 1 < cap)
		out[n++] = *prefix++;
	while (t) {
		digits++;
		t >>= 4;
	}
	if (n + digits + 1 > cap) {
		out[0] = 0;
		return;
	}
	for (unsigned i = 0; i < digits; i++)
		out[n + i] = "0123456789abcdef"[(v >> (4 * (digits - 1 - i))) & 0xf];
	out[n + digits] = 0;
}

static bool str_in(const char *s, const char *const *set, unsigned n)
{
	for (unsigned i = 0; i < n; i++)
		if (seq(s, set[i]))
			return true;
	return false;
}

int guest_fdt_build(void *buf, uint32_t cap, const struct wel2_gfdt_in *in, uint32_t *size)
{
	static const char *const fb_formats[] = { "x8r8g8b8", "x2r10g10b10" };
	static const char *const fb_modes[] = { "none", "band", "guest" };
	struct wel2_fdtw w;
	char name[48];
	uint32_t total = 0, stride = in ? (in->gicr_stride ? in->gicr_stride : 0x20000u) : 0;
	const char *mode = in && in->fb_mode ? in->fb_mode : "none";

	if (!in || !in->platform || !in->ranges || in->nranges == 0 || in->nranges > WEL2_MAX_GUEST_RANGES ||
	    in->ncores == 0 || in->ncores > WEL2_GFDT_MAX_CORES || !str_in(mode, fb_modes, 3))
		return -1;
	if (in->fb && (!in->fb->format || !str_in(in->fb->format, fb_formats, 2) || !in->fb->width || !in->fb->height ||
		       !in->fb->stride || !in->fb->size || in->fb->base + in->fb->size < in->fb->base))
		return -1;
	for (unsigned i = 0; i < in->nranges; i++)
		if (in->ranges[i].size == 0 || in->ranges[i].base + in->ranges[i].size < in->ranges[i].base)
			return -1;
	if (cap > WEL2_GFDT_MAX)                         /* the same bound for every caller, whatever buffer it passes */
		cap = WEL2_GFDT_MAX;
	wel2_fdtw_init(&w, buf, cap);

	wel2_fdtw_begin(&w, "");
	wel2_fdtw_prop_u32(&w, "#address-cells", 2);
	wel2_fdtw_prop_u32(&w, "#size-cells", 2);
	wel2_fdtw_prop_str(&w, "compatible", "watari,el2-guest");

	/* One /memory node per range, the image's range first (PrePi reads the first; HighMemDxe adds the others). */
	for (unsigned i = 0; i < in->nranges; i++) {
		uint32_t reg[4] = { (uint32_t)(in->ranges[i].base >> 32), (uint32_t)in->ranges[i].base,
				    (uint32_t)(in->ranges[i].size >> 32), (uint32_t)in->ranges[i].size };

		unit_name(name, sizeof(name), "memory@", in->ranges[i].base);
		wel2_fdtw_begin(&w, name);
		wel2_fdtw_prop_str(&w, "device_type", "memory");
		wel2_fdtw_prop_cells(&w, "reg", reg, 4);
		wel2_fdtw_end(&w);
	}

	wel2_fdtw_begin(&w, "psci");
	{
		static const char *const compat[] = { "arm,psci-1.0", "arm,psci-0.2" };

		wel2_fdtw_prop_strlist(&w, "compatible", compat, 2);
		wel2_fdtw_prop_str(&w, "method", "hvc");
	}
	wel2_fdtw_end(&w);

	/* The interrupt controller: GICD and the redistributor window of ncores frames. */
	unit_name(name, sizeof(name), "intc@", in->gicd_base);
	wel2_fdtw_begin(&w, name);
	{
		uint64_t gicr_size = (uint64_t)stride * in->ncores;
		uint32_t reg[8] = { (uint32_t)(in->gicd_base >> 32), (uint32_t)in->gicd_base, 0, 0x10000,
				    (uint32_t)(in->gicr_base >> 32), (uint32_t)in->gicr_base,
				    (uint32_t)(gicr_size >> 32), (uint32_t)gicr_size };

		wel2_fdtw_prop_str(&w, "compatible", "arm,gic-v3");
		wel2_fdtw_prop_empty(&w, "interrupt-controller");
		wel2_fdtw_prop_u32(&w, "#interrupt-cells", 3);
		wel2_fdtw_prop_cells(&w, "reg", reg, 8);
		wel2_fdtw_prop_u32(&w, "phandle", 1);
	}
	wel2_fdtw_end(&w);

	/* The architected timer: PPIs 13, 14, 11, 10 = INTIDs 29, 30, 27, 26 (secure phys, phys, virt, hyp), level, active low. */
	wel2_fdtw_begin(&w, "timer");
	{
		static const uint32_t irq[12] = { 1, 13, 4, 1, 14, 4, 1, 11, 4, 1, 10, 4 };

		wel2_fdtw_prop_str(&w, "compatible", "arm,armv8-timer");
		wel2_fdtw_prop_u32(&w, "interrupt-parent", 1);
		wel2_fdtw_prop_cells(&w, "interrupts", irq, 12);
		wel2_fdtw_prop_empty(&w, "always-on");
	}
	wel2_fdtw_end(&w);

	wel2_fdtw_begin(&w, "cpus");
	wel2_fdtw_prop_u32(&w, "#address-cells", 2);
	wel2_fdtw_prop_u32(&w, "#size-cells", 0);
	for (unsigned i = 0; i < in->ncores; i++) {
		uint64_t aff = in->mpidr[i] & 0xff00ffffffULL;
		uint32_t reg[2] = { (uint32_t)(aff >> 32), (uint32_t)aff };

		unit_name(name, sizeof(name), "cpu@", aff);
		wel2_fdtw_begin(&w, name);
		wel2_fdtw_prop_str(&w, "device_type", "cpu");
		wel2_fdtw_prop_str(&w, "compatible", "arm,armv8");
		wel2_fdtw_prop_cells(&w, "reg", reg, 2);
		wel2_fdtw_prop_str(&w, "enable-method", "psci");
		wel2_fdtw_end(&w);
	}
	wel2_fdtw_end(&w);

	wel2_fdtw_begin(&w, "chosen");
	if (in->fb) {
		uint32_t reg[4] = { (uint32_t)(in->fb->base >> 32), (uint32_t)in->fb->base,
				    (uint32_t)(in->fb->size >> 32), (uint32_t)in->fb->size };

		unit_name(name, sizeof(name), "framebuffer@", in->fb->base);
		wel2_fdtw_begin(&w, name);
		wel2_fdtw_prop_str(&w, "compatible", "simple-framebuffer");
		wel2_fdtw_prop_cells(&w, "reg", reg, 4);
		wel2_fdtw_prop_u32(&w, "width", in->fb->width);
		wel2_fdtw_prop_u32(&w, "height", in->fb->height);
		wel2_fdtw_prop_u32(&w, "stride", in->fb->stride);
		wel2_fdtw_prop_str(&w, "format", in->fb->format);
		wel2_fdtw_end(&w);
	}
	wel2_fdtw_begin(&w, "watari,el2");
	wel2_fdtw_prop_str(&w, "compatible", "watari,el2");
	wel2_fdtw_prop_u32(&w, "version", 1);
	wel2_fdtw_prop_str(&w, "platform", in->platform);
	wel2_fdtw_prop_u32(&w, "hvc-vendor-base", 0xC6000000u);
	wel2_fdtw_prop_u64(&w, "gicd-base", in->gicd_base);
	wel2_fdtw_prop_u64(&w, "gicr-base", in->gicr_base);
	wel2_fdtw_prop_u32(&w, "gicr-stride", stride);
	if (in->msi_frame_base)
		wel2_fdtw_prop_u64(&w, "msi-frame-base", in->msi_frame_base);
	wel2_fdtw_prop_str(&w, "fb-mode", mode);
	wel2_fdtw_end(&w);
	wel2_fdtw_end(&w);                               /* /chosen */

	wel2_fdtw_end(&w);                               /* root */
	if (wel2_fdtw_finish(&w, &total))
		return -1;
	if (size)
		*size = total;
	return 0;
}

/* ---- placement and ordering -------------------------------------------------------------------------------------- */

int wel2_gfdt_place(const struct wel2_range *ranges, unsigned n, uint64_t entry, uint64_t image_size, uint32_t fdt_max,
		    struct wel2_gfdt_place *out)
{
	unsigned hit = n;
	uint64_t base, end;

	if (n > WEL2_MAX_GUEST_RANGES)
		return WEL2_GFP_TOO_MANY;
	if (!entry || (entry & 0xfff))
		return WEL2_GFP_BAD_ENTRY;
	if (!image_size || image_size > 0x2000000ULL || entry + image_size < entry)
		return WEL2_GFP_BAD_SIZE;
	for (unsigned i = 0; i < n; i++) {
		if (ranges[i].size == 0 || ranges[i].base + ranges[i].size < ranges[i].base)
			continue;
		if (entry >= ranges[i].base && entry < ranges[i].base + ranges[i].size) {
			hit = i;
			break;
		}
	}
	if (hit == n)
		return WEL2_GFP_NOT_IN_RAM;
	base = ranges[hit].base;
	end = base + ranges[hit].size;
	if (ranges[hit].size < WEL2_GFDT_MIN_RANGE)
		return WEL2_GFP_SMALL_RANGE;
	out->entry = entry;
	out->image_end = entry + image_size;
	out->fdt = (out->image_end + (WEL2_GFDT_ALIGN - 1)) & ~(uint64_t)(WEL2_GFDT_ALIGN - 1);
	out->fdt_end = out->fdt + fdt_max;
	if (out->fdt < out->image_end || out->fdt_end < out->fdt || out->fdt_end > end)
		return WEL2_GFP_IMAGE_OUT;
	if (out->fdt_end + WEL2_GFDT_UEFI_REGION > end)      /* PrePi's 64 MB region sits at the top of the range */
		return WEL2_GFP_UEFI_REGION;
	out->entry_2mb_aligned = (entry & 0x1fffff) == 0;
	out->n = 0;
	out->ordered[out->n++] = ranges[hit];
	for (unsigned i = 0; i < n; i++)
		if (i != hit && ranges[i].size && ranges[i].base + ranges[i].size >= ranges[i].base)
			out->ordered[out->n++] = ranges[i];
	return WEL2_GFP_OK;
}
