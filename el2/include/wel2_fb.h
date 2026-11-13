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
 * Screen layout, top to bottom (T6h: readable from a phone photo of the panel; S = text scale = width / 400, 1..6,
 * so S = 6 and about 70 columns on the 2560-wide j293; cell = 6S x 9S pixels, the 5 x 7 font drawn at S x S):
 *   1. the checkpoint row, WEL2_FB_CPROW pixels high: squares of WEL2_FB_CP_SIZE pixels every WEL2_FB_CP_PITCH
 *      pixels, square n at x = WEL2_FB_MARGIN + n * pitch (0-15 the layer's steps, 16+ the guest's checks);
 *   2. the status band, 3 text lines on a dark blue background, never scrolled:
 *        "WATARI EL2  RUNG L<n> STEP <s>", "CHECKPOINT <n> <STATE>", "GUEST PASS <p> FAIL <f> SKIP <k>";
 *   3. the last-error area, 2 text lines: "LAST ERROR: <text>" in red, or "LAST ERROR: NONE" in grey; never scrolled;
 *   4. a 4 pixel grey rule, then the scrolling text area (white on black; when it fills it is cleared and restarts at
 *      the top; the legend of the squares is printed at the top of it, again after each clear when the area is
 *      big enough).
 * The first call of any drawing function wipes the whole screen to black and draws all of the above (lazily, so a
 * panel with no calls is left alone). Everything is drawn with aligned volatile 32-bit stores, clipped to the screen.
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
#define WEL2_FB_CP_SIZE   24u
#define WEL2_FB_CP_PITCH  28u
#define WEL2_FB_CPROW     32u      /* height of the checkpoint row; the status band starts at this y */
#define WEL2_FB_SCALE_MAX 6u
#define WEL2_FB_ERR_MAX   128u     /* bytes kept of the last-error text (including the NUL) */
#define WEL2_FB_LABEL_MAX 32u
#define WEL2_FB_GUEST_BASE 16u     /* squares from here on are guest checks (WEL2_CHK_SQUARE_BASE) */
#define WEL2_FB_GUEST_N    32u     /* guest checks counted: squares 16..47 */

/* Largest geometry accepted (a sanity bound on what the FDT says). */
#define WEL2_FB_MAX_DIM     16384u
#define WEL2_FB_MAX_STRIDE  65536u

/* Checkpoint colours, 0xRRGGBB. */
#define WEL2_RGB_GREEN   0x00c800u    /* reached / passed */
#define WEL2_RGB_RED     0xe00000u    /* fatal error / failed */
#define WEL2_RGB_AMBER   0xe0a000u    /* running */
#define WEL2_RGB_GREY    0x808080u    /* skipped */
#define WEL2_RGB_BLACK   0x000000u
#define WEL2_RGB_WHITE   0xffffffu

struct wel2_fb {
	uint64_t base;            /* physical address = address used by the layer (identity map) */
	uint32_t width, height;   /* pixels */
	uint32_t stride;          /* bytes per line */
	uint32_t fmt;             /* enum wel2_fb_fmt */
	/* text console state */
	uint32_t scale, cols, rows, cx, cy;
	uint32_t cleared;         /* screen wiped and band drawn yet (done lazily by the first drawing call) */
	int32_t cp;               /* last checkpoint painted, -1 = none */
	/* layout (pixels), set by setup */
	uint32_t band_y, err_y, text_y;
	/* status band; the all-zero value of every field means "nothing yet" (stage1 re-zeroes the struct) */
	uint32_t cp_state;        /* enum wel2_fb_cpstate of the last checkpoint */
	uint32_t rung1, step1;    /* rung / step + 1, 0 = not set */
	uint32_t pass_m, fail_m, skip_m;   /* guest checks (square - 16) by outcome */
	uint32_t err_kind;        /* 0 none, 1 automatic text (may be replaced by the next automatic one), 2 explicit */
	char label[WEL2_FB_LABEL_MAX];
	char err[WEL2_FB_ERR_MAX];
};

enum wel2_fb_cpstate { WEL2_CPS_NONE = 0, WEL2_CPS_RUNNING, WEL2_CPS_OK, WEL2_CPS_FAIL, WEL2_CPS_SKIP };

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
 * `index` as the current checkpoint and updates the status band: the colour gives the state (WEL2_RGB_AMBER running,
 * GREEN ok, RED fail, GREY skip; anything else counts as running), and for squares 16..47 the guest counters
 * (each square counts once, in its latest state; running is not counted). A red square also sets the automatic
 * error text "CHECKPOINT <n> FAILED" unless an explicit one is set. Aligned 32-bit stores only; safe with the MMU
 * off. */
void wel2_fb_checkpoint(struct wel2_fb *fb, unsigned index, uint32_t rgb);
/* Repaint the current checkpoint red (square 0 when none was painted yet) and show "FATAL AT CHECKPOINT <n>" in the
 * error area unless an explicit text was set with wel2_fb_set_error. */
void wel2_fb_checkpoint_fail(struct wel2_fb *fb);

/* Status band (T6h). `rung` is the hardware ladder rung 0..5 (L0..L5), `step` the wel2.step value / boot step;
 * shown as "RUNG L<rung> STEP <step>". Boot code should call it once the values are known. */
void wel2_fb_set_rung(struct wel2_fb *fb, unsigned rung, unsigned step);
/* Extra words after the checkpoint line ("CHECKPOINT 3 OK <label>"), kept until replaced; NULL or "" clears it.
 * Upper case, at most WEL2_FB_LABEL_MAX - 1 characters are kept. Not touched by later checkpoints. */
void wel2_fb_checkpoint_text(struct wel2_fb *fb, const char *label);
/* The last-error area: `msg` (NULL or "" clears it) in red, up to 2 lines; explicit text is never replaced by an
 * automatic one. Call it from fatal()/fatal_msg() with the message. */
void wel2_fb_set_error(struct wel2_fb *fb, const char *msg);
/* Guest counters shown in the band. */
unsigned wel2_fb_guest_pass(const struct wel2_fb *fb);
unsigned wel2_fb_guest_fail(const struct wel2_fb *fb);
unsigned wel2_fb_guest_skip(const struct wel2_fb *fb);

/* Text: '\n' starts a new line, '\r' is ignored, a line wraps at the right edge, and when the last line is full
 * the scrolling text area is cleared and output restarts at its top. Lower case is drawn as upper case; a character the
 * font does not have is drawn as a blank cell. */
void wel2_fb_putc(struct wel2_fb *fb, char c);
/* T17: make sure `lines` text rows are free below the cursor: when fewer are left (and the area can hold them at all,
 * lines + 2 legend rows <= rows), the scrolling area is cleared and the legend reprinted first, so the next `lines`
 * lines are never cut by a scroll. Call it at the start of a line (cx == 0). */
void wel2_fb_reserve(struct wel2_fb *fb, unsigned lines);
void wel2_fb_puts(struct wel2_fb *fb, const char *s);

#endif
