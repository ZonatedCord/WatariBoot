/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - framebuffer output: checkpoint squares and a small text console (task T5b, PLAN S4/S5).
 *
 * The framebuffer is the one the loader (m1n1) describes in the FDT as /chosen/framebuffer@<base> with
 * compatible "simple-framebuffer" and the properties reg, width, height, stride (bytes) and format
 * [verified: DESIGN.md section 7 citing m1n1 src/kboot.c:180-240, 218-232]. On the j293: 2560 x 1600, base
 * 0x9e2570000, format "x2r10g10b10" [verified: re/m3a-ehw0-measurements.md, HW boot log]. The simple-framebuffer
 * binding also defines "x8r8g8b8" [verified: the Devicetree simple-framebuffer binding text; no code reused].
 *
 * Every call is a no-op while fb->fmt == WEL2_FB_NONE (QEMU has no framebuffer). All pixel stores are aligned,
 * volatile 32-bit stores (base and stride are required to be multiples of 4), so the drawing runs with the MMU
 * off (Device memory: unaligned or merged accesses would fault) and under the Normal non-cacheable stage-1 map
 * alike. Nothing here allocates or takes a lock; the caller serialises text output (puts_ does).
 *
 * Screen layout: a checkpoint band of WEL2_FB_BAND pixels at the top (squares of WEL2_FB_CP_SIZE pixels every
 * WEL2_FB_CP_PITCH pixels, square n at x = WEL2_FB_MARGIN + n * pitch), then the text area below it.
 * Text is white on black, 5 x 7 font in a 6 x 9 cell, scaled by an integer (scale 3 on a 2560-wide screen).
 */
#ifndef WEL2_FB_H
#define WEL2_FB_H

#include <stdbool.h>
#include <stdint.h>

enum wel2_fb_fmt {
	WEL2_FB_NONE = 0,
	WEL2_FB_X8R8G8B8,        /* 32 bit: X[31:24] R[23:16] G[15:8] B[7:0] */
	WEL2_FB_X2R10G10B10,     /* 32 bit: X[31:30] R[29:20] G[19:10] B[9:0] (depth 30) */
};

#define WEL2_FB_MARGIN    8u
#define WEL2_FB_CP_SIZE   32u
#define WEL2_FB_CP_PITCH  40u
#define WEL2_FB_BAND      48u      /* text starts at this y */

/* Largest geometry accepted (a sanity bound on what the FDT says). */
#define WEL2_FB_MAX_DIM     16384u
#define WEL2_FB_MAX_STRIDE  65536u

/* Checkpoint colours, 0xRRGGBB. */
#define WEL2_RGB_GREEN   0x00c800u    /* reached / passed */
#define WEL2_RGB_RED     0xe00000u    /* fatal error / failed */
#define WEL2_RGB_AMBER   0xe0a000u    /* running */
#define WEL2_RGB_BLACK   0x000000u
#define WEL2_RGB_WHITE   0xffffffu

struct wel2_fb {
	uint64_t base;            /* physical address = address used by the layer (identity map) */
	uint32_t width, height;   /* pixels */
	uint32_t stride;          /* bytes per line */
	uint32_t fmt;             /* enum wel2_fb_fmt */
	/* text console state */
	uint32_t scale, cols, rows, cx, cy;
	uint32_t cleared;         /* text area blanked yet (done lazily by the first character) */
	int32_t cp;               /* last checkpoint painted, -1 = none */
};

/* Fill fb from the values the FDT gave. Returns 0 and sets everything, or -1 and leaves fb all-zero (fmt NONE)
 * when the geometry is unusable: zero or oversized dimensions, stride < 4 * width or not a multiple of 4,
 * base not 4-aligned, base + size wrapping, or an unknown format. `format` is the property string as it
 * appears in the FDT ("x8r8g8b8", "x2r10g10b10"), `flen` its length including the NUL. */
int wel2_fb_setup(struct wel2_fb *fb, uint64_t base, uint32_t width, uint32_t height, uint32_t stride,
		  const char *format, uint32_t flen);

/* Bytes of the framebuffer (height * stride), 0 when absent. */
uint64_t wel2_fb_bytes(const struct wel2_fb *fb);

/* Pixel value for 8-bit-per-channel 0xRRGGBB in the framebuffer's format. */
uint32_t wel2_fb_pixel(const struct wel2_fb *fb, uint32_t rgb);

/* Paint checkpoint square `index` in `rgb`. Out of range (the square would leave the screen): no-op. Records
 * `index` as the current checkpoint. Aligned 32-bit stores only; safe with the MMU off. */
void wel2_fb_checkpoint(struct wel2_fb *fb, unsigned index, uint32_t rgb);
/* Repaint the current checkpoint red (square 0 when none was painted yet). */
void wel2_fb_checkpoint_fail(struct wel2_fb *fb);

/* Text: '\n' starts a new line, '\r' is ignored, a line wraps at the right edge, and when the last line is full
 * the text area is cleared and output restarts at the top. Lower case is drawn as upper case; a character the
 * font does not have is drawn as a blank cell. */
void wel2_fb_putc(struct wel2_fb *fb, char c);
void wel2_fb_puts(struct wel2_fb *fb, const char *s);

#endif
