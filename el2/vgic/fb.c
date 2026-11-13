// SPDX-License-Identifier: MIT
/* Watari EL2 - framebuffer checkpoints and text console. See wel2_fb.h.
 * Pixel formats: simple-framebuffer binding (x8r8g8b8, x2r10g10b10). Drawing is volatile aligned 32-bit stores
 * only, with every coordinate checked against width/height, so no store can leave [base, base + height * stride). */
#include "wel2_fb.h"
#include "font5x7.h"

#define RULE_H 4u                          /* grey rule between the error area and the scrolling text */
#define COL_BAND   0x14284bu               /* status band background */
#define COL_ERR    0xff4040u
#define COL_NOERR  0x909090u
#define COL_RULE   0x606060u
#define COL_OK     0x40ff40u
#define COL_RUN    0xffc020u
#define COL_FAILT  0xff6060u

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
	scale = width / 400u;
	scale = scale < 1u ? 1u : (scale > WEL2_FB_SCALE_MAX ? WEL2_FB_SCALE_MAX : scale);
	fb->base = base;
	fb->width = width;
	fb->height = height;
	fb->stride = stride;
	fb->fmt = fmt;
	fb->scale = scale;
	fb->cols = width > 2u * WEL2_FB_MARGIN ? (width - 2u * WEL2_FB_MARGIN) / ((FONT_W + 1u) * scale) : 0;
	fb->band_y = WEL2_FB_CPROW;
	fb->err_y = fb->band_y + 3u * (FONT_H + 2u) * scale;
	fb->text_y = fb->err_y + 2u * (FONT_H + 2u) * scale + RULE_H;
	fb->rows = height > fb->text_y ? (height - fb->text_y) / ((FONT_H + 2u) * scale) : 0;
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

/* One character cell at pixel (x0, y0), in colours fg on bg. */
static void draw_glyph(const struct wel2_fb *fb, uint32_t x0, uint32_t y0, char c, uint32_t fg, uint32_t bg)
{
	const uint8_t *g = glyph_rows(c);
	uint32_t s = fb->scale;

	fill(fb, x0, y0, (FONT_W + 1u) * s, (FONT_H + 2u) * s, wel2_fb_pixel(fb, bg));
	for (uint32_t r = 0; r < FONT_H; r++)
		for (uint32_t k = 0; k < FONT_W; k++)
			if (g[r] & (1u << (FONT_W - 1u - k)))
				fill(fb, x0 + k * s, y0 + r * s, s, s, wel2_fb_pixel(fb, fg));
}

/* ---- fixed lines (status band, error area): never scrolled ------------------------------------------------------ */
struct lw {                      /* a line writer: pieces are appended up to `cols` characters */
	const struct wel2_fb *fb;
	uint32_t y, col, fg, bg, maxcol;
};

static void lw_start(struct lw *w, const struct wel2_fb *fb, uint32_t y, uint32_t fg, uint32_t bg)
{
	w->fb = fb;
	w->y = y;
	w->col = 0;
	w->fg = fg;
	w->bg = bg;
	w->maxcol = fb->cols;
	fill(fb, 0, y, fb->width, (FONT_H + 2u) * fb->scale, wel2_fb_pixel(fb, bg));
}

static void lw_c(struct lw *w, char c)
{
	if (w->col >= w->maxcol)
		return;
	draw_glyph(w->fb, WEL2_FB_MARGIN + w->col * (FONT_W + 1u) * w->fb->scale, w->y, c, w->fg, w->bg);
	w->col++;
}

static void lw_s(struct lw *w, const char *s)
{
	while (*s)
		lw_c(w, *s++);
}

static void lw_n(struct lw *w, uint32_t v)
{
	char b[11];
	unsigned n = 0;

	do {
		b[n++] = (char)('0' + v % 10u);
		v /= 10u;
	} while (v);
	while (n)
		lw_c(w, b[--n]);
}

static unsigned popcnt(uint32_t v)
{
	unsigned n = 0;

	for (; v; v &= v - 1u)
		n++;
	return n;
}

unsigned wel2_fb_guest_pass(const struct wel2_fb *fb) { return popcnt(fb->pass_m); }
unsigned wel2_fb_guest_fail(const struct wel2_fb *fb) { return popcnt(fb->fail_m); }
unsigned wel2_fb_guest_skip(const struct wel2_fb *fb) { return popcnt(fb->skip_m); }

static const char *state_word(uint32_t st)
{
	return st == WEL2_CPS_OK ? "OK" : st == WEL2_CPS_FAIL ? "FAIL" : st == WEL2_CPS_SKIP ? "SKIP" :
	       st == WEL2_CPS_RUNNING ? "RUNNING" : "";
}

static void draw_band(const struct wel2_fb *fb)
{
	uint32_t chh = (FONT_H + 2u) * fb->scale, fgs;
	struct lw w;

	lw_start(&w, fb, fb->band_y, WEL2_RGB_WHITE, COL_BAND);
	lw_s(&w, "WATARI EL2  RUNG ");
	if (fb->rung1) {
		lw_s(&w, "L");
		lw_n(&w, fb->rung1 - 1u);
	} else {
		lw_s(&w, "-");
	}
	lw_s(&w, " STEP ");
	if (fb->step1)
		lw_n(&w, fb->step1 - 1u);
	else
		lw_s(&w, "-");

	fgs = fb->cp_state == WEL2_CPS_OK ? COL_OK : fb->cp_state == WEL2_CPS_FAIL ? COL_FAILT :
	      fb->cp_state == WEL2_CPS_RUNNING ? COL_RUN : WEL2_RGB_WHITE;
	lw_start(&w, fb, fb->band_y + chh, fgs, COL_BAND);
	lw_s(&w, "CHECKPOINT ");
	if (fb->cp < 0) {
		lw_s(&w, "NONE");
	} else {
		lw_n(&w, (uint32_t)fb->cp);
		lw_s(&w, " ");
		lw_s(&w, state_word(fb->cp_state));
		if ((uint32_t)fb->cp >= WEL2_FB_GUEST_BASE) {
			lw_s(&w, " (GUEST CHECK ");
			lw_n(&w, (uint32_t)fb->cp - WEL2_FB_GUEST_BASE);
			lw_s(&w, ")");
		}
	}
	if (fb->label[0]) {
		lw_s(&w, " ");
		lw_s(&w, fb->label);
	}

	lw_start(&w, fb, fb->band_y + 2u * chh, wel2_fb_guest_fail(fb) ? COL_FAILT : WEL2_RGB_WHITE, COL_BAND);
	lw_s(&w, "GUEST PASS ");
	lw_n(&w, wel2_fb_guest_pass(fb));
	lw_s(&w, " FAIL ");
	lw_n(&w, wel2_fb_guest_fail(fb));
	lw_s(&w, " SKIP ");
	lw_n(&w, wel2_fb_guest_skip(fb));
}

static void draw_error(const struct wel2_fb *fb)
{
	uint32_t chh = (FONT_H + 2u) * fb->scale;
	uint32_t fg = fb->err_kind ? COL_ERR : COL_NOERR;
	const char *p = fb->err;
	struct lw w;

	lw_start(&w, fb, fb->err_y, fg, WEL2_RGB_BLACK);
	lw_s(&w, "LAST ERROR: ");
	if (!fb->err_kind) {
		lw_s(&w, "NONE");
		lw_start(&w, fb, fb->err_y + chh, fg, WEL2_RGB_BLACK);
		return;
	}
	/* the text continues on the first line, then fills the second */
	while (*p && w.col < w.maxcol)
		lw_c(&w, *p++);
	lw_start(&w, fb, fb->err_y + chh, fg, WEL2_RGB_BLACK);
	while (*p && w.col < w.maxcol)
		lw_c(&w, *p++);
}

/* ---- scrolling text --------------------------------------------------------------------------------------------- */
/* The legend of the checkpoint squares: two lines of at most 52 characters, printed at the top of the text area when
 * it can hold them (56+ columns, 8+ rows) so that it never wraps. */
#define LEGEND1 "SQUARES 0-15: LAYER STEPS, 16+: GUEST G1-G11\n"
#define LEGEND2 "GREEN = OK  AMBER = RUNNING  RED = FAIL  GREY = SKIP\n"

static void putc_raw(struct wel2_fb *fb, char c);

static void legend(struct wel2_fb *fb)
{
	const char *p;

	if (fb->cols < 56u || fb->rows < 8u)
		return;
	for (p = LEGEND1; *p; p++)
		putc_raw(fb, *p);
	for (p = LEGEND2; *p; p++)
		putc_raw(fb, *p);
}

static void clear_text(struct wel2_fb *fb)
{
	fill(fb, 0, fb->text_y, fb->width, fb->height > fb->text_y ? fb->height - fb->text_y : 0,
	     wel2_fb_pixel(fb, WEL2_RGB_BLACK));
	fb->cx = fb->cy = 0;
}

static void newline(struct wel2_fb *fb)
{
	fb->cx = 0;
	if (++fb->cy >= fb->rows) {
		clear_text(fb);                 /* scroll by clearing; the band and the error line stay */
		legend(fb);
	}
}

static void putc_raw(struct wel2_fb *fb, char c)
{
	if (c == '\r')
		return;
	if (c == '\n') {
		newline(fb);
		return;
	}
	draw_glyph(fb, WEL2_FB_MARGIN + fb->cx * (FONT_W + 1u) * fb->scale,
		   fb->text_y + fb->cy * (FONT_H + 2u) * fb->scale, c, WEL2_RGB_WHITE, WEL2_RGB_BLACK);
	if (++fb->cx >= fb->cols)
		newline(fb);
}

static bool band_fits(const struct wel2_fb *fb)
{
	return fb->cols && fb->err_y + 2u * (FONT_H + 2u) * fb->scale <= fb->height;
}

/* First drawing call: wipe the whole screen (m1n1's last image), draw the rule, the band, the error area and the
 * legend. cleared is set first, so nothing below re-enters. */
static void ensure_init(struct wel2_fb *fb)
{
	if (fb->cleared)
		return;
	fb->cleared = 1;
	fill(fb, 0, 0, fb->width, fb->height, wel2_fb_pixel(fb, WEL2_RGB_BLACK));
	fb->cx = fb->cy = 0;
	if (band_fits(fb)) {
		fill(fb, 0, fb->text_y - RULE_H, fb->width, RULE_H, wel2_fb_pixel(fb, COL_RULE));
		draw_band(fb);
		draw_error(fb);
		if (fb->rows)
			legend(fb);
	}
	sync_stores();
}

static void refresh(struct wel2_fb *fb)
{
	if (band_fits(fb)) {
		draw_band(fb);
		draw_error(fb);
	}
	sync_stores();
}

/* ---- error text -------------------------------------------------------------------------------------------------- */
static void copy_text(char *dst, uint32_t max, const char *src)
{
	uint32_t i = 0;

	if (src)
		for (; src[i] && i + 1u < max; i++) {
			char c = src[i];

			dst[i] = (c < 0x20 || c > 0x7e) ? '?' : (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
		}
	dst[i] = 0;
}

/* Automatic error text "<a><n><b>"; never replaces an explicit one. */
static void err_auto(struct wel2_fb *fb, const char *a, uint32_t n, const char *b)
{
	char num[11];
	unsigned k = 0, m = 0;

	if (fb->err_kind == 2u)
		return;
	do {
		num[k++] = (char)('0' + n % 10u);
		n /= 10u;
	} while (n);
	while (*a && m + 1u < WEL2_FB_ERR_MAX)
		fb->err[m++] = *a++;
	while (k && m + 1u < WEL2_FB_ERR_MAX)
		fb->err[m++] = num[--k];
	while (*b && m + 1u < WEL2_FB_ERR_MAX)
		fb->err[m++] = *b++;
	fb->err[m] = 0;
	fb->err_kind = 1u;
}

void wel2_fb_set_error(struct wel2_fb *fb, const char *msg)
{
	if (!fb->fmt)
		return;
	copy_text(fb->err, WEL2_FB_ERR_MAX, msg);
	fb->err_kind = fb->err[0] ? 2u : 0u;
	ensure_init(fb);
	refresh(fb);
}

void wel2_fb_set_rung(struct wel2_fb *fb, unsigned rung, unsigned step)
{
	if (!fb->fmt)
		return;
	fb->rung1 = rung + 1u;
	fb->step1 = step + 1u;
	ensure_init(fb);
	refresh(fb);
}

void wel2_fb_checkpoint_text(struct wel2_fb *fb, const char *label)
{
	if (!fb->fmt)
		return;
	copy_text(fb->label, WEL2_FB_LABEL_MAX, label);
	ensure_init(fb);
	refresh(fb);
}

/* ---- checkpoints ------------------------------------------------------------------------------------------------- */
static uint32_t state_of(uint32_t rgb)
{
	return rgb == WEL2_RGB_GREEN ? WEL2_CPS_OK : rgb == WEL2_RGB_RED ? WEL2_CPS_FAIL :
	       rgb == WEL2_RGB_GREY ? WEL2_CPS_SKIP : WEL2_CPS_RUNNING;
}

void wel2_fb_checkpoint(struct wel2_fb *fb, unsigned index, uint32_t rgb)
{
	uint64_t x;
	uint32_t st = state_of(rgb);

	if (!fb->fmt)
		return;
	x = WEL2_FB_MARGIN + (uint64_t)index * WEL2_FB_CP_PITCH;
	if (x + WEL2_FB_CP_SIZE > fb->width || WEL2_FB_CP_SIZE > fb->height)
		return;
	ensure_init(fb);
	fill(fb, (uint32_t)x, 0, WEL2_FB_CP_SIZE, WEL2_FB_CP_SIZE, wel2_fb_pixel(fb, rgb));
	fb->cp = (int32_t)index;
	fb->cp_state = st;
	if (index >= WEL2_FB_GUEST_BASE && index < WEL2_FB_GUEST_BASE + WEL2_FB_GUEST_N) {
		uint32_t bit = 1u << (index - WEL2_FB_GUEST_BASE);

		fb->pass_m &= ~bit;
		fb->fail_m &= ~bit;
		fb->skip_m &= ~bit;
		if (st == WEL2_CPS_OK)
			fb->pass_m |= bit;
		else if (st == WEL2_CPS_FAIL)
			fb->fail_m |= bit;
		else if (st == WEL2_CPS_SKIP)
			fb->skip_m |= bit;
	}
	if (st == WEL2_CPS_FAIL)
		err_auto(fb, "CHECKPOINT ", index, " FAILED");
	refresh(fb);
}

void wel2_fb_checkpoint_fail(struct wel2_fb *fb)
{
	uint32_t n;

	if (!fb->fmt)
		return;
	n = fb->cp < 0 ? 0u : (uint32_t)fb->cp;
	wel2_fb_checkpoint(fb, n, WEL2_RGB_RED);
	if (fb->cp >= 0) {                  /* the square fitted on the screen */
		err_auto(fb, "FATAL AT CHECKPOINT ", n, "");
		refresh(fb);
	}
}

void wel2_fb_reserve(struct wel2_fb *fb, unsigned lines)
{
	if (!fb->fmt || !fb->cols || !fb->rows)
		return;
	ensure_init(fb);
	if (fb->cx)                         /* a block starts on its own row */
		putc_raw(fb, '\n');
	if (fb->cy + lines > fb->rows && lines + 2u <= fb->rows) {
		clear_text(fb);
		legend(fb);
	}
}

void wel2_fb_putc(struct wel2_fb *fb, char c)
{
	if (!fb->fmt || !fb->cols || !fb->rows)
		return;
	ensure_init(fb);
	putc_raw(fb, c);
	if (c == '\n')
		sync_stores();
}

void wel2_fb_puts(struct wel2_fb *fb, const char *s)
{
	if (!fb->fmt)
		return;
	while (*s)
		wel2_fb_putc(fb, *s++);
	sync_stores();
}
