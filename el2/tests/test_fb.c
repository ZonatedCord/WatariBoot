/* SPDX-License-Identifier: MIT */
/* Host tests: the framebuffer back end (wel2_fb.h, vgic/fb.c): pixel formats, bounds, checkpoint squares, 5 x 7 text
 * (glyph patterns, scaling, wrap, scroll by clearing), the /chosen/framebuffer@* lookup (fdt_get_framebuffer) on
 * synthetic FDTs, and the Normal non-cacheable stage-1 mapping (wel2_s1_map_fb). The drawing runs on a host buffer
 * surrounded by canary words: a store outside [base, base + height * stride) or into the line padding fails. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "wel2_fb.h"
#include "wel2_maps.h"
#include "fdt.h"
#include "fdt_build.h"

#define CANARY   0xdeadbeefu
#define GUARD    64u                    /* canary words before and after the pixels */

struct screen {
	uint32_t *mem;                  /* GUARD words, pixels, GUARD words */
	uint32_t *pix;
	uint32_t w, h, stride_words;
	struct wel2_fb fb;
};

static const char *fmt_str(unsigned fmt)
{
	return fmt == WEL2_FB_X2R10G10B10 ? "x2r10g10b10" : "x8r8g8b8";
}

/* stride_pad: extra words at the end of every line (stride > 4 * width) */
static void scr_new(struct screen *s, unsigned fmt, uint32_t w, uint32_t h, uint32_t stride_pad)
{
	size_t n = (size_t)(w + stride_pad) * h;

	s->w = w;
	s->h = h;
	s->stride_words = w + stride_pad;
	s->mem = malloc((n + 2 * GUARD) * 4);
	for (size_t i = 0; i < n + 2 * GUARD; i++)
		s->mem[i] = CANARY;
	s->pix = s->mem + GUARD;
	CHECK_EQ(wel2_fb_setup(&s->fb, (uint64_t)(uintptr_t)s->pix, w, h, s->stride_words * 4, fmt_str(fmt),
			       (uint32_t)strlen(fmt_str(fmt)) + 1), 0);
}

static void scr_free(struct screen *s)
{
	free(s->mem);
}

/* All canaries intact: guards, line padding. Pixels may be anything. */
static bool scr_canaries_ok(const struct screen *s)
{
	size_t n = (size_t)s->stride_words * s->h;

	for (unsigned i = 0; i < GUARD; i++)
		if (s->mem[i] != CANARY || s->mem[GUARD + n + i] != CANARY)
			return false;
	for (uint32_t y = 0; y < s->h; y++)
		for (uint32_t x = s->w; x < s->stride_words; x++)
			if (s->pix[(size_t)y * s->stride_words + x] != CANARY)
				return false;
	return true;
}

static uint32_t at(const struct screen *s, uint32_t x, uint32_t y)
{
	return s->pix[(size_t)y * s->stride_words + x];
}

/* Fill every pixel (not the padding) with `v`, as the previous screen content (m1n1's last image). */
static void scr_paint(struct screen *s, uint32_t v)
{
	for (uint32_t y = 0; y < s->h; y++)
		for (uint32_t x = 0; x < s->w; x++)
			s->pix[(size_t)y * s->stride_words + x] = v;
}

static const unsigned FMTS[2] = { WEL2_FB_X8R8G8B8, WEL2_FB_X2R10G10B10 };

/* ---- pixel formats --------------------------------------------------------------------------------------------- */
static void t_pixel_formats(void)
{
	struct wel2_fb a, b;

	CHECK_EQ(wel2_fb_setup(&a, 0x1000, 64, 64, 256, "x8r8g8b8", 9), 0);
	CHECK_EQ(wel2_fb_setup(&b, 0x1000, 64, 64, 256, "x2r10g10b10", 12), 0);
	CHECK_EQ(wel2_fb_pixel(&a, 0xff0000), 0x00ff0000u);
	CHECK_EQ(wel2_fb_pixel(&a, 0x00ff00), 0x0000ff00u);
	CHECK_EQ(wel2_fb_pixel(&a, 0x0000ff), 0x000000ffu);
	CHECK_EQ(wel2_fb_pixel(&a, 0x123456), 0x00123456u);
	CHECK_EQ(wel2_fb_pixel(&b, 0xff0000), 0x3ff00000u);          /* R at [29:20] */
	CHECK_EQ(wel2_fb_pixel(&b, 0x00ff00), 0x000ffc00u);          /* G at [19:10] */
	CHECK_EQ(wel2_fb_pixel(&b, 0x0000ff), 0x000003ffu);          /* B at [9:0] */
	CHECK_EQ(wel2_fb_pixel(&b, 0xffffff), 0x3fffffffu);          /* X (bits 31:30) stays 0 */
	CHECK_EQ(wel2_fb_pixel(&b, 0x000000), 0u);
	CHECK_EQ(wel2_fb_pixel(&b, 0x808080), (0x202u << 20) | (0x202u << 10) | 0x202u);   /* 0x80 -> 10 bits: 0x202 */
}

static void t_setup_validation(void)
{
	struct wel2_fb f;

	CHECK_EQ(wel2_fb_setup(&f, 0x9e2570000ULL, 2560, 1600, 10240, "x2r10g10b10", 12), 0);
	CHECK_EQ(f.fmt, WEL2_FB_X2R10G10B10);
	CHECK_EQ(f.scale, 3);                                        /* 2560 / 800 */
	CHECK_EQ(f.cols, (2560 - 16) / 18);
	CHECK_EQ(f.rows, (1600 - 48) / 27);
	CHECK_EQ(wel2_fb_bytes(&f), 1600ull * 10240);
	/* every refusal leaves an all-zero structure (format NONE, nothing drawable) */
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 0, 100, 400, "x8r8g8b8", 9), -1);
	CHECK_EQ(f.fmt, WEL2_FB_NONE); CHECK_EQ(wel2_fb_bytes(&f), 0);
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 100, 0, 400, "x8r8g8b8", 9), -1);
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 100, 100, 399, "x8r8g8b8", 9), -1);     /* stride < 4 * width */
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 100, 100, 402, "x8r8g8b8", 9), -1);     /* stride not a multiple of 4 */
	CHECK_EQ(wel2_fb_setup(&f, 0x1002, 100, 100, 400, "x8r8g8b8", 9), -1);     /* base not 4-aligned */
	CHECK_EQ(wel2_fb_setup(&f, ~0ull - 0x100, 100, 100, 400, "x8r8g8b8", 9), -1);   /* wraps */
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 100000, 100, 400000, "x8r8g8b8", 9), -1);    /* oversized */
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 100, 100, 400, "a8r8g8b8", 9), -1);          /* unknown format */
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 100, 100, 400, "x8r8g8b8", 8), -1);          /* no NUL inside the property */
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 100, 100, 400, "r5g6b5", 7), -1);            /* 16 bpp: not supported */
	CHECK_EQ(wel2_fb_setup(&f, 0x1000, 100, 100, 400, NULL, 0), -1);
	CHECK_EQ(f.fmt, WEL2_FB_NONE);
}

/* ---- checkpoints ------------------------------------------------------------------------------------------------ */
static void t_checkpoint(void)
{
	for (int k = 0; k < 2; k++) {
		struct screen s;
		uint32_t green, red;

		scr_new(&s, FMTS[k], 400, 120, 8);
		scr_paint(&s, 0x11111111u);
		green = wel2_fb_pixel(&s.fb, WEL2_RGB_GREEN);
		red = wel2_fb_pixel(&s.fb, WEL2_RGB_RED);
		wel2_fb_checkpoint(&s.fb, 0, WEL2_RGB_GREEN);
		wel2_fb_checkpoint(&s.fb, 3, WEL2_RGB_GREEN);
		/* square n: x in [8 + 40n, 8 + 40n + 32), y in [0, 32) */
		for (uint32_t y = 0; y < 120; y++)
			for (uint32_t x = 0; x < 400; x++) {
				bool in0 = x >= 8 && x < 40 && y < 32;
				bool in3 = x >= 128 && x < 160 && y < 32;

				CHECK_EQ(at(&s, x, y), in0 || in3 ? green : 0x11111111u);
			}
		CHECK_EQ(s.fb.cp, 3);
		wel2_fb_checkpoint_fail(&s.fb);                              /* the current one (3) turns red */
		CHECK_EQ(at(&s, 128, 0), red); CHECK_EQ(at(&s, 159, 31), red);
		CHECK_EQ(at(&s, 8, 0), green);                               /* others unchanged */
		CHECK_EQ(at(&s, 160, 0), 0x11111111u);
		/* the last square that fits: x = 8 + 40 * 9 = 368, ends at 400; index 10 would leave the screen */
		wel2_fb_checkpoint(&s.fb, 9, WEL2_RGB_AMBER);
		CHECK_EQ(at(&s, 368, 0), wel2_fb_pixel(&s.fb, WEL2_RGB_AMBER)); CHECK_EQ(at(&s, 399, 31), wel2_fb_pixel(&s.fb, WEL2_RGB_AMBER));
		wel2_fb_checkpoint(&s.fb, 10, WEL2_RGB_RED);
		wel2_fb_checkpoint(&s.fb, 100000, WEL2_RGB_RED);
		wel2_fb_checkpoint(&s.fb, 0xffffffffu, WEL2_RGB_RED);
		CHECK_EQ(s.fb.cp, 9);                                        /* refused calls do not move the current one */
		CHECK(scr_canaries_ok(&s));
		scr_free(&s);
	}
}

static void t_checkpoint_fail_without_any(void)
{
	struct screen s;

	scr_new(&s, WEL2_FB_X8R8G8B8, 200, 100, 0);
	wel2_fb_checkpoint_fail(&s.fb);                                  /* nothing painted yet: square 0 */
	CHECK_EQ(at(&s, 8, 0), wel2_fb_pixel(&s.fb, WEL2_RGB_RED));
	CHECK(scr_canaries_ok(&s));
	scr_free(&s);
}

static void t_checkpoint_screen_too_small(void)
{
	struct screen s;

	scr_new(&s, WEL2_FB_X8R8G8B8, 100, 20, 0);                       /* shorter than a square */
	scr_paint(&s, 0x22222222u);
	wel2_fb_checkpoint(&s.fb, 0, WEL2_RGB_GREEN);
	CHECK_EQ(at(&s, 8, 0), 0x22222222u);
	CHECK(scr_canaries_ok(&s));
	scr_free(&s);
}

/* ---- no framebuffer -------------------------------------------------------------------------------------------- */
static void t_no_fb_is_noop(void)
{
	struct wel2_fb f;

	memset(&f, 0, sizeof(f));
	f.cp = -1;
	wel2_fb_checkpoint(&f, 0, WEL2_RGB_GREEN);
	wel2_fb_checkpoint_fail(&f);
	wel2_fb_putc(&f, 'A');
	wel2_fb_puts(&f, "hello\n");
	CHECK_EQ(f.fmt, WEL2_FB_NONE); CHECK_EQ(f.cp, -1); CHECK_EQ(f.cx, 0); CHECK_EQ(f.cy, 0); CHECK_EQ(f.cleared, 0);
}

/* ---- text ------------------------------------------------------------------------------------------------------- */
/* Expected 5 x 7 patterns written out independently of the font table (X = lit). */
static const char *GLYPH_I[7] = { ".XXX.", "..X..", "..X..", "..X..", "..X..", "..X..", ".XXX." };
static const char *GLYPH_L[7] = { "X....", "X....", "X....", "X....", "X....", "X....", "XXXXX" };
static const char *GLYPH_7[7] = { "XXXXX", "....X", "...X.", "..X..", ".X...", ".X...", ".X..." };
static const char *GLYPH_DOT[7] = { ".....", ".....", ".....", ".....", ".....", ".XX..", ".XX.." };

/* Does the cell (col, row) show the pattern at integer scale `sc`? Also checks the sixth column and the two rows
 * below the glyph (the gap of the 6 x 9 cell) are background. */
static bool cell_shows(const struct screen *s, uint32_t col, uint32_t row, const char *const pat[7])
{
	uint32_t sc = s->fb.scale, x0 = 8 + col * 6 * sc, y0 = 48 + row * 9 * sc;
	uint32_t fg = wel2_fb_pixel(&s->fb, WEL2_RGB_WHITE), bg = wel2_fb_pixel(&s->fb, WEL2_RGB_BLACK);

	for (uint32_t cy = 0; cy < 9 * sc; cy++)
		for (uint32_t cx = 0; cx < 6 * sc; cx++) {
			uint32_t gx = cx / sc, gy = cy / sc;
			bool lit = gx < 5 && gy < 7 && pat[gy][gx] == 'X';

			if (at(s, x0 + cx, y0 + cy) != (lit ? fg : bg))
				return false;
		}
	return true;
}

static bool cell_blank(const struct screen *s, uint32_t col, uint32_t row)
{
	static const char *const blank[7] = { ".....", ".....", ".....", ".....", ".....", ".....", "....." };

	return cell_shows(s, col, row, blank);
}

static void t_glyphs_scale1(void)
{
	for (int k = 0; k < 2; k++) {
		struct screen s;

		scr_new(&s, FMTS[k], 640, 200, 4);                           /* 640 / 800 = scale 1 */
		scr_paint(&s, 0x33333333u);                                  /* old image: must be wiped by the text */
		CHECK_EQ(s.fb.scale, 1);
		wel2_fb_puts(&s.fb, "IL7.i ~\n");                            /* lower case i = I; '~' not in the font */
		CHECK(cell_shows(&s, 0, 0, GLYPH_I));
		CHECK(cell_shows(&s, 1, 0, GLYPH_L));
		CHECK(cell_shows(&s, 2, 0, GLYPH_7));
		CHECK(cell_shows(&s, 3, 0, GLYPH_DOT));
		CHECK(cell_shows(&s, 4, 0, GLYPH_I));
		CHECK(cell_blank(&s, 5, 0));                                 /* space */
		CHECK(cell_blank(&s, 6, 0));                                 /* unknown character -> blank */
		CHECK_EQ(s.fb.cx, 0); CHECK_EQ(s.fb.cy, 1);
		/* the band above the text and the old image above it are untouched; the text area was wiped */
		CHECK_EQ(at(&s, 0, 0), 0x33333333u); CHECK_EQ(at(&s, 639, 47), 0x33333333u);
		CHECK_EQ(at(&s, 639, 199), wel2_fb_pixel(&s.fb, WEL2_RGB_BLACK));
		CHECK_EQ(at(&s, 0, 48), wel2_fb_pixel(&s.fb, WEL2_RGB_BLACK));
		CHECK(scr_canaries_ok(&s));
		scr_free(&s);
	}
}

static void t_glyphs_scale2_and_3(void)
{
	static const uint32_t widths[2] = { 1600, 2400 };               /* scale 2 and 3 */

	for (int k = 0; k < 2; k++) {
		struct screen s;

		scr_new(&s, WEL2_FB_X2R10G10B10, widths[k], 300, 0);
		CHECK_EQ(s.fb.scale, 2 + k);
		wel2_fb_puts(&s.fb, "L\nI");
		CHECK(cell_shows(&s, 0, 0, GLYPH_L));
		CHECK(cell_shows(&s, 0, 1, GLYPH_I));
		CHECK(cell_blank(&s, 1, 0));
		/* one lit pixel of the 'L' at scale 2/3 covers a scale x scale block, and nothing leaks to the next block */
		uint32_t sc = s.fb.scale, fg = wel2_fb_pixel(&s.fb, WEL2_RGB_WHITE);
		CHECK_EQ(at(&s, 8, 48), fg); CHECK_EQ(at(&s, 8 + sc - 1, 48 + sc - 1), fg);
		CHECK(at(&s, 8 + sc, 48) != fg);
		CHECK(scr_canaries_ok(&s));
		scr_free(&s);
	}
}

static void t_cr_ignored_and_lf_newline(void)
{
	struct screen s;

	scr_new(&s, WEL2_FB_X8R8G8B8, 640, 200, 0);
	wel2_fb_puts(&s.fb, "I\r\nL\r\n");                               /* what the UART path sends for "\n" */
	CHECK(cell_shows(&s, 0, 0, GLYPH_I));
	CHECK(cell_shows(&s, 0, 1, GLYPH_L));
	CHECK_EQ(s.fb.cx, 0); CHECK_EQ(s.fb.cy, 2);
	scr_free(&s);
}

static void t_wrap(void)
{
	struct screen s;

	scr_new(&s, WEL2_FB_X8R8G8B8, 100, 200, 3);                      /* cols = (100 - 16) / 6 = 14 */
	CHECK_EQ(s.fb.cols, 14);
	for (int i = 0; i < 14; i++)
		wel2_fb_putc(&s.fb, 'L');
	CHECK_EQ(s.fb.cx, 0); CHECK_EQ(s.fb.cy, 1);                      /* the 14th character wrapped the cursor */
	wel2_fb_putc(&s.fb, 'I');
	CHECK(cell_shows(&s, 13, 0, GLYPH_L));
	CHECK(cell_shows(&s, 0, 1, GLYPH_I));
	CHECK_EQ(s.fb.cx, 1); CHECK_EQ(s.fb.cy, 1);
	/* the last cell ends at x = 8 + 14 * 6 = 92 <= 100: nothing was drawn past the right edge */
	CHECK(scr_canaries_ok(&s));
	scr_free(&s);
}

static void t_scroll_by_clearing(void)
{
	struct screen s;
	uint32_t bg, fg;

	scr_new(&s, WEL2_FB_X8R8G8B8, 200, 48 + 9 * 3, 0);                // three text rows
	bg = wel2_fb_pixel(&s.fb, WEL2_RGB_BLACK);
	fg = wel2_fb_pixel(&s.fb, WEL2_RGB_WHITE);
	CHECK_EQ(s.fb.rows, 3);
	wel2_fb_puts(&s.fb, "L\nL\nL\n");                                // fills rows 0..2, the third newline overflows
	CHECK_EQ(s.fb.cy, 0); CHECK_EQ(s.fb.cx, 0);
	for (uint32_t y = 48; y < 48 + 27; y++)                          // the text area was cleared
		for (uint32_t x = 0; x < 200; x++)
			CHECK_EQ(at(&s, x, y), bg);
	wel2_fb_putc(&s.fb, 'I');                                        // and output restarts at the top
	CHECK(cell_shows(&s, 0, 0, GLYPH_I));
	CHECK_EQ(at(&s, 8 + 2, 48), fg);
	CHECK(scr_canaries_ok(&s));
	scr_free(&s);
}

static void t_checkpoints_survive_text(void)
{
	struct screen s;

	scr_new(&s, WEL2_FB_X2R10G10B10, 640, 120, 0);
	wel2_fb_checkpoint(&s.fb, 1, WEL2_RGB_GREEN);
	wel2_fb_puts(&s.fb, "HELLO\nWORLD\nAGAIN\nAND AGAIN\nAND AGAIN\n");   // forces a scroll
	CHECK_EQ(at(&s, 48, 0), wel2_fb_pixel(&s.fb, WEL2_RGB_GREEN));    // the band is never part of the text area
	CHECK_EQ(at(&s, 79, 31), wel2_fb_pixel(&s.fb, WEL2_RGB_GREEN));
	CHECK(scr_canaries_ok(&s));
	scr_free(&s);
}

/* A long fuzz-like run over odd geometries: no store outside the buffer, whatever the text. */
static void t_bounds_odd_geometry(void)
{
	static const uint32_t dims[][3] = { { 17, 50, 0 }, { 40, 49, 5 }, { 60, 60, 1 }, { 641, 130, 3 }, { 9, 9, 0 } };

	for (unsigned d = 0; d < sizeof(dims) / sizeof(dims[0]); d++)
		for (int k = 0; k < 2; k++) {
			struct screen s;

			scr_new(&s, FMTS[k], dims[d][0], dims[d][1], dims[d][2]);
			for (int i = 0; i < 3000; i++)
				wel2_fb_putc(&s.fb, (char)(i * 7 + (i >> 3)));       // all byte values, including > 127
			for (unsigned c = 0; c < 40; c++)
				wel2_fb_checkpoint(&s.fb, c, WEL2_RGB_GREEN);
			wel2_fb_checkpoint_fail(&s.fb);
			CHECK(scr_canaries_ok(&s));
			scr_free(&s);
		}
}

/* Looked at by eye: WEL2_FB_PPM=<file> make -C el2 test writes a 8-bit PPM of a sample screen (not a check). */
static void t_ppm_sample(void)
{
	const char *path = getenv("WEL2_FB_PPM");
	struct screen s;
	FILE *f;

	if (!path)
		return;
	scr_new(&s, WEL2_FB_X2R10G10B10, 800, 160, 0);
	for (unsigned i = 0; i < 6; i++)
		wel2_fb_checkpoint(&s.fb, i, i == 5 ? WEL2_RGB_RED : WEL2_RGB_GREEN);
	wel2_fb_puts(&s.fb, "WATARI EL2: HELLO FROM EL2\nABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789\n.:=-_X/,;()[]<>+*!?'\"%#|@\\\n");
	f = fopen(path, "wb");
	CHECK(f != NULL);
	if (f) {
		fprintf(f, "P6\n%u %u\n255\n", s.w, s.h);
		for (uint32_t y = 0; y < s.h; y++)
			for (uint32_t x = 0; x < s.w; x++) {
				uint32_t p = at(&s, x, y);
				unsigned char rgb[3] = { (unsigned char)(p >> 22), (unsigned char)(p >> 12), (unsigned char)(p >> 2) };

				fwrite(rgb, 1, 3, f);
			}
		fclose(f);
	}
	scr_free(&s);
}

/* ---- FDT lookup ------------------------------------------------------------------------------------------------- */
enum { FB_OK, FB_DISABLED, FB_NO_COMPAT, FB_NO_REG, FB_NO_FORMAT, FB_BAD_FORMAT, FB_UNDER_SOC, FB_NO_CHOSEN };

/* j293-shaped: root cells 2/2, /chosen with the node m1n1 fills. `serial` adds the property the code must never
 * look at. */
static void *fb_tree(struct fdtb *b, int variant, const char *format, bool serial, unsigned chosen_cells)
{
	uint8_t fmtlen = (uint8_t)(strlen(format) + 1);

	fb_init(b);
	fb_begin(b, "");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
	fb_strs(b, "compatible", "apple,j293\0apple,t8103\0", 24);
	if (serial)
		fb_strs(b, "serial-number", "F5K0000SECRET\0", 14);
	fb_begin(b, "soc");
	fb_u32(b, "#address-cells", 2);
	fb_u32(b, "#size-cells", 2);
	if (variant == FB_UNDER_SOC) {
		fb_begin(b, "framebuffer@9e2570000");
		fb_strs(b, "compatible", "simple-framebuffer\0", 19);
		fb_reg(b, 2, 2, 0x9e2570000ull, 0xfa0000);
		fb_u32(b, "width", 2560); fb_u32(b, "height", 1600); fb_u32(b, "stride", 10240);
		fb_strs(b, "format", format, fmtlen);
		fb_end(b);
	}
	fb_end(b);
	fb_begin(b, "chosen");
	if (chosen_cells) {
		fb_u32(b, "#address-cells", chosen_cells);
		fb_u32(b, "#size-cells", chosen_cells);
	}
	if (variant != FB_UNDER_SOC && variant != FB_NO_CHOSEN) {
		fb_strs(b, "bootargs", "wel2.step=1\0", 12);
		fb_begin(b, "framebuffer@9e2570000");
		if (variant != FB_NO_COMPAT)
			fb_strs(b, "compatible", "simple-framebuffer\0", 19);
		else
			fb_strs(b, "compatible", "simple-bus\0", 11);
		if (variant == FB_DISABLED)
			fb_strs(b, "status", "disabled\0", 9);
		if (variant != FB_NO_REG) {
			if (chosen_cells == 1)
				fb_reg(b, 1, 1, 0xe2570000ull, 0xfa0000);
			else
				fb_reg(b, 2, 2, 0x9e2570000ull, 0xfa0000);
		}
		fb_u32(b, "width", 2560);
		fb_u32(b, "height", 1600);
		fb_u32(b, "stride", 10240);
		if (variant == FB_BAD_FORMAT)
			fb_strs(b, "format", "a8r8g8b8\0", 9);
		else if (variant != FB_NO_FORMAT)
			fb_strs(b, "format", format, fmtlen);
		fb_end(b);
	}
	fb_end(b);
	fb_end(b);
	return fb_finish(b);
}

static void t_fdt_present(void)
{
	struct fdtb b;
	struct wel2_fb f;
	void *fdt = fb_tree(&b, FB_OK, "x2r10g10b10", true, 0);

	CHECK_EQ(fdt_get_framebuffer(fdt, &f), 0);
	CHECK_EQ(f.fmt, WEL2_FB_X2R10G10B10);
	CHECK_EQ(f.base, 0x9e2570000ull);
	CHECK_EQ(f.width, 2560); CHECK_EQ(f.height, 1600); CHECK_EQ(f.stride, 10240);
	CHECK_EQ(wel2_fb_bytes(&f), 0xfa0000);                          /* 16 384 000: the node's own reg size */
	CHECK_EQ(f.cp, -1);

	fdt = fb_tree(&b, FB_OK, "x8r8g8b8", false, 0);
	CHECK_EQ(fdt_get_framebuffer(fdt, &f), 0);
	CHECK_EQ(f.fmt, WEL2_FB_X8R8G8B8);

	/* /chosen with its own 1/1 cells: reg is <0xe2570000 0xfa0000> */
	fdt = fb_tree(&b, FB_OK, "x8r8g8b8", false, 1);
	CHECK_EQ(fdt_get_framebuffer(fdt, &f), 0);
	CHECK_EQ(f.fmt, WEL2_FB_X8R8G8B8); CHECK_EQ(f.base, 0xe2570000ull);
}

static void t_fdt_absent_or_unusable(void)
{
	static const int variants[] = { FB_DISABLED, FB_NO_COMPAT, FB_NO_REG, FB_NO_FORMAT, FB_BAD_FORMAT, FB_UNDER_SOC,
					FB_NO_CHOSEN };
	struct fdtb b;
	struct wel2_fb f;

	for (unsigned i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
		void *fdt = fb_tree(&b, variants[i], "x2r10g10b10", false, 0);

		memset(&f, 0xaa, sizeof(f));
		CHECK_EQ(fdt_get_framebuffer(fdt, &f), 0);                  // walked fine, nothing usable
		CHECK_EQ(f.fmt, WEL2_FB_NONE);
		CHECK_EQ(wel2_fb_bytes(&f), 0);
		CHECK_EQ(f.cp, -1);
	}
}

/* QEMU-shaped tree: no /chosen/framebuffer. */
static void t_fdt_qemu_shape(void)
{
	struct fdtb b;
	struct wel2_fb f;

	fb_init(&b);
	fb_begin(&b, "");
	fb_u32(&b, "#address-cells", 2);
	fb_u32(&b, "#size-cells", 2);
	fb_begin(&b, "chosen");
	fb_strs(&b, "bootargs", "wel2.e2h=0\0", 11);
	fb_end(&b);
	fb_begin(&b, "memory@40000000");
	fb_reg(&b, 2, 2, 0x40000000ull, 0x40000000ull);
	fb_end(&b);
	fb_end(&b);
	CHECK_EQ(fdt_get_framebuffer(fb_finish(&b), &f), 0);
	CHECK_EQ(f.fmt, WEL2_FB_NONE);
}

static void t_fdt_malformed(void)
{
	struct fdtb b;
	struct wel2_fb f;
	uint8_t *p;

	/* bad magic */
	p = fb_tree(&b, FB_OK, "x8r8g8b8", false, 0);
	p[0] ^= 0xff;
	memset(&f, 0xaa, sizeof(f));
	CHECK_EQ(fdt_get_framebuffer(p, &f), -1);
	CHECK_EQ(f.fmt, WEL2_FB_NONE);

	/* structure block cut in the middle of the framebuffer node (size fields claim less than is needed) */
	p = fb_tree(&b, FB_OK, "x8r8g8b8", false, 0);
	uint32_t st_len = (uint32_t)(p[36] << 24 | p[37] << 16 | p[38] << 8 | p[39]);
	for (uint32_t cut = 8; cut < st_len; cut += 36) {
		p = fb_tree(&b, FB_OK, "x8r8g8b8", false, 0);
		fb_put32(p + 36, st_len - cut);
		memset(&f, 0xaa, sizeof(f));
		int r = fdt_get_framebuffer(p, &f);
		CHECK(r == -1 || r == 0);                                   // never a crash or an out-of-bounds read
		if (r == -1)
			CHECK_EQ(f.fmt, WEL2_FB_NONE);
	}

	/* unknown structure token */
	p = fb_tree(&b, FB_OK, "x8r8g8b8", false, 0);
	fb_put32(p + 64 + 8, 0x77);                                      // overwrites the first token after the root name
	CHECK_EQ(fdt_get_framebuffer(p, &f), -1);

	/* a property whose length runs past the structure block */
	p = fb_tree(&b, FB_OK, "x8r8g8b8", false, 0);
	for (uint32_t off = 64; off + 12 < st_len + 64; off += 4)
		if (p[off + 3] == 3 && p[off] == 0 && p[off + 1] == 0 && p[off + 2] == 0) {   // a FDT_PROP token
			fb_put32(p + off + 4, 0x7fffffff);
			break;
		}
	CHECK_EQ(fdt_get_framebuffer(p, &f), -1);

	/* absurd geometry from a well-formed blob is refused by the validation, not drawn */
	p = fb_tree(&b, FB_OK, "x8r8g8b8", false, 0);
	for (uint32_t off = 64; off + 12 < st_len + 64; off += 4)        // find "width" value: 2560 = 0x0a00
		if (p[off] == 0 && p[off + 1] == 0 && p[off + 2] == 0x0a && p[off + 3] == 0x00) {
			fb_put32(p + off, 0x10000000u);
			break;
		}
	CHECK_EQ(fdt_get_framebuffer(p, &f), 0);
	CHECK_EQ(f.fmt, WEL2_FB_NONE);
}

/* ---- stage-1 mapping -------------------------------------------------------------------------------------------- */
static uint8_t pool[WEL2_S1_POOL_BYTES] __attribute__((aligned(16384)));

/* The j293 numbers: RAM at 0x8_0000_0000 up to 0x9_df61_0000 (2 MB-rounded: 0x9df600000), framebuffer 16 384 000
 * bytes at 0x9_e257_0000, which lies above the RAM. 16 KB granule, 36-bit input range. */
static void t_s1_map_fb_j293(void)
{
	for (int e2h = 0; e2h <= 1; e2h++) {
		struct wel2_pt pt;
		struct wel2_ram ram;
		struct wel2_s1_layout lay = { 0, 0, 0 };
		const uint64_t fbase = 0x9e2570000ull, fsize = 1600ull * 10240;
		uint64_t pa, at_, ap1 = e2h ? 0 : (1ULL << 6);
		unsigned lv, before;

		CHECK_EQ(wel2_ram_set(&ram, 0x800000000ull, 0x9df610000ull - 0x800000000ull, 0x800000000ull,
				      1ULL << 36), 0);
		CHECK_EQ(wel2_pt_init(&pt, pool, sizeof(pool), WEL2_GRAN_16K, wel2_s1_va_bits(WEL2_GRAN_16K)), 0);
		CHECK_EQ(wel2_s1_build(&pt, &ram, &lay, e2h != 0), 0);
		before = pt.used;
		CHECK_EQ(wel2_s1_map_fb(&pt, &ram, fbase, fsize, e2h != 0), 0);
		printf("    fb map e2h %d: %u tables before, %u after, pool %u\n", e2h, before, pt.max_used,
		       WEL2_S1_POOL_BYTES >> WEL2_GRAN_16K);
		CHECK(pt.max_used * (1ULL << WEL2_GRAN_16K) <= WEL2_S1_POOL_BYTES);

		/* first and last page: Normal non-cacheable (AttrIndx 2), accessed, outer shareable, XN */
		uint64_t xn = e2h ? ((1ULL << 53) | (1ULL << 54)) : (1ULL << 54);
		uint64_t want = (1ULL << 10) | (2ULL << 8) | ((uint64_t)WEL2_S1_ATTR_NC << 2) | ap1 | xn;

		CHECK(wel2_pt_walk(&pt, fbase & ~0x3fffULL, &pa, &at_, &lv));
		CHECK_EQ(at_, want);
		CHECK(wel2_pt_walk(&pt, fbase + fsize - 4, &pa, &at_, &lv));
		CHECK_EQ(pa, (fbase + fsize - 4)); CHECK_EQ(at_, want);
		CHECK(!wel2_pt_walk(&pt, ((fbase + fsize + 0x3fff) & ~0x3fffULL), NULL, NULL, NULL));   // page after: unmapped
		CHECK(!wel2_pt_walk(&pt, (fbase & ~0x3fffULL) - 0x4000, NULL, NULL, NULL));
		CHECK_EQ(WEL2_MAIR_EL2_VALUE & 0xff0000, 0x440000);           // Attr2 = 0x44
		CHECK_EQ((WEL2_MAIR_EL2_VALUE >> 8) & 0xff, 0xff);            // Attr1 unchanged
		CHECK_EQ(WEL2_MAIR_EL2_VALUE & 0xff, 0x00);                   // Attr0 unchanged

		/* refusals: inside the RAM, straddling its end, below it, beyond the input range, empty */
		CHECK_EQ(wel2_s1_map_fb(&pt, &ram, 0x900000000ull, 0x100000, e2h != 0), -1);
		CHECK_EQ(wel2_s1_map_fb(&pt, &ram, ram.end - 0x1000, 0x100000, e2h != 0), -1);
		CHECK_EQ(wel2_s1_map_fb(&pt, &ram, 0x700000000ull, 0x100000, e2h != 0), -1);
		CHECK_EQ(wel2_s1_map_fb(&pt, &ram, 1ULL << 40, 0x100000, e2h != 0), -1);
		CHECK_EQ(wel2_s1_map_fb(&pt, &ram, fbase, 0, e2h != 0), -1);
	}
}

static void t_s1_map_fb_both_granules(void)
{
	for (unsigned g = WEL2_GRAN_4K; g <= WEL2_GRAN_16K; g += 2) {
		struct wel2_pt pt;
		struct wel2_ram ram;
		struct wel2_s1_layout lay = { 0, 0, 0 };
		uint64_t at_;

		CHECK_EQ(wel2_ram_set(&ram, 0x40000000ull, 0x40000000ull, 0x40000000ull, 1ULL << wel2_s1_va_bits(g)), 0);
		CHECK_EQ(wel2_pt_init(&pt, pool, sizeof(pool), g, wel2_s1_va_bits(g)), 0);
		CHECK_EQ(wel2_s1_build(&pt, &ram, &lay, true), 0);
		CHECK_EQ(wel2_s1_map_fb(&pt, &ram, 0x80000000ull + 0x1230, 0x4000, true), 0);   // unaligned base: rounded out
		CHECK(wel2_pt_walk(&pt, 0x80001230ull & ~((1ull << g) - 1), NULL, &at_, NULL));   // rounded out to the page
		CHECK_EQ((at_ >> 2) & 7, WEL2_S1_ATTR_NC);
	}
}

int main(void)
{
	RUN(t_pixel_formats);
	RUN(t_setup_validation);
	RUN(t_checkpoint);
	RUN(t_checkpoint_fail_without_any);
	RUN(t_checkpoint_screen_too_small);
	RUN(t_no_fb_is_noop);
	RUN(t_glyphs_scale1);
	RUN(t_glyphs_scale2_and_3);
	RUN(t_cr_ignored_and_lf_newline);
	RUN(t_wrap);
	RUN(t_scroll_by_clearing);
	RUN(t_checkpoints_survive_text);
	RUN(t_bounds_odd_geometry);
	RUN(t_ppm_sample);
	RUN(t_fdt_present);
	RUN(t_fdt_absent_or_unusable);
	RUN(t_fdt_qemu_shape);
	RUN(t_fdt_malformed);
	RUN(t_s1_map_fb_j293);
	RUN(t_s1_map_fb_both_granules);
	TESTS_DONE();
}
