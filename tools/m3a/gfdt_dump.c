// SPDX-License-Identifier: MIT
// gfdt_dump: host tool of the M3a day-3 bundle (PLAN E14, rung D0). Takes the FDT that m1n1 prepared (read back at rung
// L0), runs the layer's own M1 parser and the layer's own guest-FDT builder (el2/vgic/fdt_build.c, E5) on it, and
// prints the guest FDT the layer WOULD build. Nothing of the prepared blob is copied except what fdt_m1_parse extracts
// (RAM, reservations, cpu reg values, framebuffer geometry): no serial number, MAC address, seed or Apple node can
// reach the output. The result is also checked here: no node or property whose name or string value contains "apple",
// and none of the names wel2_fdtw_forbidden() lists; a hit exits 3.
//
//   gfdt_dump prepared.dtb [--layer 0xA] [--entry 0xE] [--image-size N] [--fb layer|band|guest] [-o guest.dtb]
//     --layer      address of the layer's 32 MB block (cut out of the guest RAM, as stage1.c does); default: none cut
//     --entry      wel2.guest=<entry> (EDK2's block). With it the placement of wel2_gfdt_place is checked and the
//                  range that holds the entry is listed first; without it the ranges are listed in address order
//     --image-size the EDK2 image's arm64 header image_size (default 0x2000000, the 32 MB block)
//     --fb         wel2.fb mode the loader will pass; "layer" (default) gives the guest no framebuffer node
//     -o           also write the guest FDT blob (for dtc -I dtb -O dts)
// Exit: 0 printed, 1 malformed input or the builder/placement refused, 2 usage or file error, 3 privacy self-check hit.
// Stdout is the tree. What the tool cannot know is said on stderr as NOTE lines.
// Build: tools/m3a/build-m1-payload.sh (prebuilt arm64 + source in the bundle) or tools/m3a/test_gfdt_dump.py.

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "fdt.h"
#include "fdt_m1.h"
#include "wel2_fdt_build.h"
#include "wel2_maps.h"

#define X64(v) ((uint64_t)(v))
/* el2/boot/plat.h PLAT_M1_GICD_BASE / PLAT_M1_GICR_BASE; boot.h WEL2_CARVE_SIZE_M1; wel2_v2m.h V2M_T8103_BASE */
#define M1_GICD       0xF00000000ULL
#define M1_GICR       0xF10000000ULL
#define M1_CARVE_SIZE 0x2000000ULL
#define M1_V2M_BASE   0xFFFFF000ULL

static bool str_ok(const uint8_t *v, uint32_t len)
{
	if (len == 0 || v[len - 1] != 0)
		return false;
	for (uint32_t i = 0; i + 1 < len; i++)
		if (v[i] == 0 ? (i == 0 || v[i - 1] == 0) : !isprint(v[i]))
			return false;
	return true;
}

static bool has_apple(const char *s)
{
	for (; *s; s++)
		if (!strncasecmp(s, "apple", 5))
			return true;
	return false;
}

/* case-insensitive scan of raw bytes (a value is not always a string) */
static bool bytes_have_apple(const uint8_t *v, uint32_t len)
{
	for (uint32_t i = 0; i + 5 <= len; i++)
		if (!strncasecmp((const char *)v + i, "apple", 5))
			return true;
	return false;
}

#ifndef GFDT_PLATFORM
#define GFDT_PLATFORM "t8103"            /* the unit test builds a variant with an "apple" platform to exercise the exit 3 */
#endif

struct walk {
	char last[80];
	unsigned last_depth;
	bool started;
	int hits;
};

static bool walk_cb(void *ctx, unsigned depth, const char *node, const char *prop, uint8_t *val, uint32_t len)
{
	struct walk *w = ctx;

	if (!w->started || w->last_depth != depth || strcmp(w->last, node)) {
		printf("%*s%s:\n", 2 * (int)depth, "", node[0] ? node : "/");
		snprintf(w->last, sizeof(w->last), "%s", node);
		w->last_depth = depth;
		w->started = true;
		if (has_apple(node)) {
			fprintf(stderr, "PRIVACY: node name contains 'apple': %s\n", node);
			w->hits++;
		}
	}
	printf("%*s%s", 2 * (int)depth + 2, "", prop);
	if (wel2_fdtw_forbidden(prop) || has_apple(prop)) {
		fprintf(stderr, "PRIVACY: forbidden or Apple property: %s\n", prop);
		w->hits++;
	}
	if (len && bytes_have_apple(val, len)) {
		fprintf(stderr, "PRIVACY: a value of %s contains 'apple' (raw byte scan)\n", prop);
		w->hits++;
	}
	if (len == 0) {
		printf(";\n");
	} else if (str_ok(val, len)) {
		printf(" = ");
		for (uint32_t i = 0; i < len; i += (uint32_t)strlen((char *)val + i) + 1) {
			printf("%s\"%s\"", i ? ", " : "", (char *)val + i);
			if (has_apple((char *)val + i)) {
				fprintf(stderr, "PRIVACY: string value contains 'apple' in %s\n", prop);
				w->hits++;
			}
		}
		printf(";\n");
	} else if (len % 4 == 0) {
		printf(" = <");
		for (uint32_t i = 0; i < len; i += 4)
			printf("%s0x%x", i ? " " : "",
			       (unsigned)(((uint32_t)val[i] << 24) | ((uint32_t)val[i + 1] << 16) | ((uint32_t)val[i + 2] << 8) | val[i + 3]));
		printf(">;\n");
	} else {
		printf(" = [");
		for (uint32_t i = 0; i < len; i++)
			printf("%s%02x", i ? " " : "", val[i]);
		printf("];\n");
	}
	return false;
}

static int usage(const char *a0)
{
	fprintf(stderr, "usage: %s prepared.dtb [--layer 0xA] [--entry 0xE] [--image-size N] [--fb layer|band|guest] [--gran 4k|16k] [-o guest.dtb]\n", a0);
	return 2;
}

int main(int argc, char **argv)
{
	const char *in_path = NULL, *out_path = NULL, *fbmode = "layer";
	uint64_t layer = 0, entry = 0, image_size = 0x2000000ULL;
	bool have_layer = false;
	unsigned gshift = 14;                                  /* WEL2_DEF_GRAN: 16 KB; --gran 4k for wel2.gran=4k */

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--layer") && i + 1 < argc)
			layer = strtoull(argv[++i], NULL, 0), have_layer = true;
		else if (!strcmp(argv[i], "--entry") && i + 1 < argc)
			entry = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--image-size") && i + 1 < argc)
			image_size = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--fb") && i + 1 < argc)
			fbmode = argv[++i];
		else if (!strcmp(argv[i], "--gran") && i + 1 < argc)
			gshift = !strcmp(argv[++i], "4k") ? 12 : !strcmp(argv[i], "16k") ? 14 : 0;
		else if (!strcmp(argv[i], "-o") && i + 1 < argc)
			out_path = argv[++i];
		else if (argv[i][0] != '-' && !in_path)
			in_path = argv[i];
		else
			return usage(argv[0]);
	}
	if (!gshift || !in_path || (strcmp(fbmode, "layer") && strcmp(fbmode, "band") && strcmp(fbmode, "guest")))
		return usage(argv[0]);

	FILE *f = fopen(in_path, "rb");
	if (!f) {
		perror(in_path);
		return 2;
	}
	size_t cap = 16u << 20, n;
	uint64_t *mem = calloc(cap / 8 + 1, 8);
	if (!mem) {
		fclose(f);
		return 2;
	}
	n = fread(mem, 1, cap, f);
	fclose(f);
	if (n < 40 || fdt_totalsize(mem) > n) {
		fprintf(stderr, "%s: too short or truncated blob\n", in_path);
		return 1;
	}
	static struct m1_platform p;
	if (fdt_m1_parse(mem, &p)) {
		fprintf(stderr, "%s: malformed device tree\n", in_path);
		return 1;
	}
	if (!p.is_m1)
		fprintf(stderr, "NOTE: no apple,aic node: this is not an M1 tree; the output is only the builder run on what was found\n");

	/* guest RAM exactly as stage1.c: usable[] minus the layer's 32 MB block, rounded inward to 32 MB */
	static struct wel2_range in[M1_MAX_USABLE], gram[WEL2_MAX_GUEST_RANGES];
	unsigned ngram = 0;
	for (unsigned i = 0; i < p.nusable; i++)
		in[i].base = p.usable[i].base, in[i].size = p.usable[i].size;
	if (wel2_ranges_inward(in, p.nusable, 0x2000000ULL, have_layer ? layer : 0, have_layer ? M1_CARVE_SIZE : 0, gram,
			       WEL2_MAX_GUEST_RANGES, &ngram) || !ngram) {
		fprintf(stderr, "the FDT leaves no guest RAM (or too many pieces)\n");
		return 1;
	}
	if (!have_layer)
		fprintf(stderr, "NOTE: no --layer: the layer's 32 MB block is not cut out of the guest RAM\n");

	static struct wel2_gfdt_place gp;
	const struct wel2_range *ordered = gram;
	unsigned nord = ngram;
	if (entry) {
		int rc = wel2_gfdt_place(gram, ngram, entry, image_size, WEL2_GFDT_MAX, &gp);
		if (rc != WEL2_GFP_OK) {
			fprintf(stderr, "placement refused by the layer's rule (wel2_gfdt_place code %d): the layer would stop with a FATAL line\n", rc);
			return 1;
		}
		ordered = gp.ordered;
		nord = gp.n;
		printf("// guest image at 0x%" PRIx64 ", image end 0x%" PRIx64 ", guest FDT at 0x%" PRIx64 "%s\n", X64(gp.entry),
		       X64(gp.image_end), X64(gp.fdt), gp.entry_2mb_aligned ? "" : " (entry not 2 MB aligned)");
	} else {
		fprintf(stderr, "NOTE: no --entry: the placement rules (first range >= 128 MB, FDT, 64 MB UEFI region) are not checked\n");
	}

	struct wel2_gfdt_in gi;
	struct wel2_gfdt_fb fb;
	memset(&gi, 0, sizeof(gi));
	gi.platform = GFDT_PLATFORM;
	gi.ranges = ordered;
	gi.nranges = nord;
	gi.gicd_base = M1_GICD;
	gi.gicr_base = M1_GICR;
	gi.gicr_stride = 0x20000;
	gi.ncores = p.ncpu < WEL2_GFDT_MAX_CORES ? p.ncpu : WEL2_GFDT_MAX_CORES;
	if (!gi.ncores) {
		fprintf(stderr, "the FDT lists no cpu\n");
		return 1;
	}
	for (unsigned i = 0; i < gi.ncores; i++)           /* plat_m1.c: aff = reg[23:0] | reg[39:32] << 24; main.c turns it back into reg */
		gi.mpidr[i] = p.cpu[i].reg & 0xff00ffffffULL;
	{
		bool in_ram = false;
		for (unsigned i = 0; i < nord; i++)
			if (M1_V2M_BASE >= ordered[i].base && M1_V2M_BASE < ordered[i].base + ordered[i].size)
				in_ram = true;
		gi.msi_frame_base = in_ram ? 0 : M1_V2M_BASE;          /* [hypothesis: g_v2m_on is "the page is not guest RAM"] */
	}
	/* E9: exactly main.c chain_fdt() / stage1.c: wel2_fb_plan with the stage-2 granule (16 KB by default on the M1); a plan
	 * that fails falls back to wel2.fb=layer (no node). */
	{
		unsigned mode = !strcmp(fbmode, "band") ? WEL2_FBM_BAND : !strcmp(fbmode, "guest") ? WEL2_FBM_GUEST : WEL2_FBM_LAYER;
		struct wel2_fb_plan plan;

		if (wel2_fb_plan(&p.fb, mode, gshift, &plan)) {
			printf("// wel2.fb=%s: the layer's plan fails (no framebuffer, not whole %u KB pages, or no room for the band): it keeps the panel, no node\n",
			       fbmode, 1u << (gshift - 10));
			wel2_fb_plan(&p.fb, WEL2_FBM_LAYER, gshift, &plan);
		}
		gi.fb_mode = "none";
		if (p.fb.fmt != WEL2_FB_NONE && plan.mode != WEL2_FBM_LAYER) {
			memset(&fb, 0, sizeof(fb));
			fb.base = plan.guest_base;
			fb.size = plan.guest_bytes;
			fb.width = p.fb.width;
			fb.height = plan.guest_rows;
			fb.stride = p.fb.stride;
			fb.format = p.fb.fmt == WEL2_FB_X2R10G10B10 ? "x2r10g10b10" : "x8r8g8b8";
			gi.fb = &fb;
			gi.fb_mode = plan.mode == WEL2_FBM_BAND ? "band" : "guest";
		}
		printf("// framebuffer plan (%u KB granule): mode %s, band rows %u, guest rows %u, guest base 0x%" PRIx64 ", guest bytes 0x%" PRIx64 "\n",
		       1u << (gshift - 10), gi.fb_mode, plan.band_rows, plan.guest_rows, X64(plan.guest_base), X64(plan.guest_bytes));
	}

	static uint64_t blob[WEL2_GFDT_MAX / 8 + 8];
	uint32_t size = 0;
	if (guest_fdt_build(blob, sizeof(blob), &gi, &size)) {
		fprintf(stderr, "the guest FDT builder refused the input or the result exceeds %u bytes\n", (unsigned)WEL2_GFDT_MAX);
		return 1;
	}
	printf("// guest FDT: %u bytes (limit %u), %u /memory range(s), %u core(s), wel2.fb=%s\n", (unsigned)size,
	       (unsigned)WEL2_GFDT_MAX, nord, gi.ncores, fbmode);
	if (p.fb.fmt != WEL2_FB_NONE)
		printf("// framebuffer of the prepared FDT: %ux%u stride=%u (%s 4 x width) format=%s\n", p.fb.width, p.fb.height,
		       p.fb.stride, p.fb.stride == 4u * p.fb.width ? "equals" : "DIFFERS from",
		       p.fb.fmt == WEL2_FB_X2R10G10B10 ? "x2r10g10b10" : "x8r8g8b8");
	if (out_path) {
		FILE *o = fopen(out_path, "wb");
		if (!o || fwrite(blob, 1, size, o) != size) {
			perror(out_path);
			return 2;
		}
		fclose(o);
	}
	struct walk w;
	memset(&w, 0, sizeof(w));
	if (fdt_walk_props(blob, walk_cb, &w)) {
		fprintf(stderr, "internal error: the built blob does not walk\n");
		return 1;
	}
	if (w.hits) {
		fprintf(stderr, "PRIVACY SELF-CHECK FAILED (%d hit(s)): do not use or share this output\n", w.hits);
		return 3;
	}
	free(mem);
	return 0;
}
