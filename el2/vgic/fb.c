// SPDX-License-Identifier: MIT
/* Watari EL2 - framebuffer checkpoints and text console. See wel2_fb.h.
 * Pixel formats: simple-framebuffer binding (x8r8g8b8, x2r10g10b10). Drawing is volatile aligned 32-bit stores
 * only, with every coordinate checked against width/height, so no store can leave [base, base + height * stride). */
#include "wel2_fb.h"
#include "font5x7.h"

/* Make the stores above reach the panel's memory before anything else happens (a halt right after a red square). */
static void sync_stores(void)
{
#if defined(__aarch64__)
	__asm__ volatile("dsb st" ::: "memory");
#endif
}

/* `s` (len bytes, from the FDT) equals the literal including its terminating NUL. */
static bool str_is(const char *s, uint32_t len, const char *lit)
{
	uint32_t i = 0;

	while (lit[i]) {
		if (i >= len || s[i] != lit[i])
			return false;
		i++;
	}
	return i < len && s[i] == 0;
}

int wel2_fb_setup(struct wel2_fb *fb, uint64_t base, uint32_t width, uint32_t height, uint32_t stride,
		  const char *format, uint32_t flen)
{
	uint32_t fmt = WEL2_FB_NONE, scale;

	*fb = (struct wel2_fb){ 0 };
	fb->cp = -1;
	if (!format)
		return -1;
	if (str_is(format, flen, "x8r8g8b8"))
		fmt = WEL2_FB_X8R8G8B8;
	else if (str_is(format, flen, "x2r10g10b10"))
		fmt = WEL2_FB_X2R10G10B10;
	if (fmt == WEL2_FB_NONE || !width || !height || width > WEL2_FB_MAX_DIM || height > WEL2_FB_MAX_DIM ||
	    stride > WEL2_FB_MAX_STRIDE || stride < 4u * width || (stride & 3u) || (base & 3u) ||
	    base + (uint64_t)height * stride < base)
		return -1;
	scale = width / 800u;
	scale = scale < 1u ? 1u : (scale > 4u ? 4u : scale);
	fb->base = base;
	fb->width = width;
	fb->height = height;
	fb->stride = stride;
	fb->fmt = fmt;
	fb->scale = scale;
	fb->cols = width > 2u * WEL2_FB_MARGIN ? (width - 2u * WEL2_FB_MARGIN) / ((FONT_W + 1u) * scale) : 0;
	fb->rows = height > WEL2_FB_BAND ? (height - WEL2_FB_BAND) / ((FONT_H + 2u) * scale) : 0;
	return 0;
}

uint64_t wel2_fb_bytes(const struct wel2_fb *fb)
{
	return fb->fmt ? (uint64_t)fb->height * fb->stride : 0;
}

static uint32_t ch10(uint32_t v8)
{
	return (v8 << 2) | (v8 >> 6);       /* replicate the top bits: 0xff -> 0x3ff, 0 -> 0 */
}

uint32_t wel2_fb_pixel(const struct wel2_fb *fb, uint32_t rgb)
{
	uint32_t r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;

	if (fb->fmt == WEL2_FB_X2R10G10B10)
		return (ch10(r) << 20) | (ch10(g) << 10) | ch10(b);
	return (r << 16) | (g << 8) | b;
}

/* Fill the rectangle [x0, x0+w) x [y0, y0+h), clipped to the screen. */
static void fill(const struct wel2_fb *fb, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t pix)
{
	if (x0 >= fb->width || y0 >= fb->height)
		return;
	if (w > fb->width - x0)
		w = fb->width - x0;
	if (h > fb->height - y0)
		h = fb->height - y0;
	for (uint32_t y = y0; y < y0 + h; y++) {
		volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(fb->base + (uint64_t)y * fb->stride) + x0;

		for (uint32_t x = 0; x < w; x++)
			p[x] = pix;
	}
}

void wel2_fb_checkpoint(struct wel2_fb *fb, unsigned index, uint32_t rgb)
{
	uint64_t x;

	if (!fb->fmt)
		return;
	x = WEL2_FB_MARGIN + (uint64_t)index * WEL2_FB_CP_PITCH;
	if (x + WEL2_FB_CP_SIZE > fb->width || WEL2_FB_CP_SIZE > fb->height)
		return;
	fill(fb, (uint32_t)x, 0, WEL2_FB_CP_SIZE, WEL2_FB_CP_SIZE, wel2_fb_pixel(fb, rgb));
	fb->cp = (int32_t)index;
	sync_stores();
}

void wel2_fb_checkpoint_fail(struct wel2_fb *fb)
{
	if (fb->fmt)
		wel2_fb_checkpoint(fb, fb->cp < 0 ? 0u : (unsigned)fb->cp, WEL2_RGB_RED);
}

static const uint8_t *glyph_rows(char c)
{
	static const uint8_t blank[FONT_H];

	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	for (unsigned i = 0; i < sizeof(font5x7) / sizeof(font5x7[0]); i++)
		if (font5x7[i].c == c)
			return font5x7[i].row;
	return blank;
}

static void clear_text(struct wel2_fb *fb)
{
	fill(fb, 0, WEL2_FB_BAND, fb->width, fb->height - WEL2_FB_BAND, wel2_fb_pixel(fb, WEL2_RGB_BLACK));
	fb->cx = fb->cy = 0;
	fb->cleared = 1;
}

static void newline(struct wel2_fb *fb)
{
	fb->cx = 0;
	if (++fb->cy >= fb->rows)
		clear_text(fb);                 /* scroll by clearing */
}

static void draw_cell(const struct wel2_fb *fb, uint32_t col, uint32_t row, char c)
{
	const uint8_t *g = glyph_rows(c);
	uint32_t s = fb->scale, cw = (FONT_W + 1u) * s, chh = (FONT_H + 2u) * s;
	uint32_t x0 = WEL2_FB_MARGIN + col * cw, y0 = WEL2_FB_BAND + row * chh;
	uint32_t fg = wel2_fb_pixel(fb, WEL2_RGB_WHITE), bg = wel2_fb_pixel(fb, WEL2_RGB_BLACK);

	fill(fb, x0, y0, cw, chh, bg);
	for (uint32_t r = 0; r < FONT_H; r++)
		for (uint32_t k = 0; k < FONT_W; k++)
			if (g[r] & (1u << (FONT_W - 1u - k)))
				fill(fb, x0 + k * s, y0 + r * s, s, s, fg);
}

void wel2_fb_putc(struct wel2_fb *fb, char c)
{
	if (!fb->fmt || !fb->cols || !fb->rows)
		return;
	if (!fb->cleared)
		clear_text(fb);
	if (c == '\r')
		return;
	if (c == '\n') {
		newline(fb);
		sync_stores();
		return;
	}
	draw_cell(fb, fb->cx, fb->cy, c);
	if (++fb->cx >= fb->cols)
		newline(fb);
}

void wel2_fb_puts(struct wel2_fb *fb, const char *s)
{
	if (!fb->fmt)
		return;
	while (*s)
		wel2_fb_putc(fb, *s++);
	sync_stores();
}
