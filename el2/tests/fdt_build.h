/* SPDX-License-Identifier: MIT */
/* Host tests: a tiny flattened-device-tree builder (Devicetree Specification, flattened format, chapter 5).
 * Only what the tests need: nested nodes, u32 / cell / string-list properties. No bounds beyond the buffer. */
#ifndef WEL2_TEST_FDT_BUILD_H
#define WEL2_TEST_FDT_BUILD_H

#include <stdint.h>
#include <string.h>

struct fdtb {
	uint8_t buf[4096];
	uint32_t st, st_len;            /* structure block offset and length (bytes) */
	char strs[1024];
	uint32_t str_len;
};

static inline void fb_put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static inline void fb_tok(struct fdtb *b, uint32_t v)
{
	fb_put32(b->buf + b->st + b->st_len, v);
	b->st_len += 4;
}

static inline void fb_bytes(struct fdtb *b, const void *d, uint32_t n)
{
	if (n)
		memcpy(b->buf + b->st + b->st_len, d, n);
	memset(b->buf + b->st + b->st_len + n, 0, ((n + 3) & ~3u) - n);
	b->st_len += (n + 3) & ~3u;
}

static inline void fb_init(struct fdtb *b)
{
	memset(b, 0, sizeof(*b));
	b->st = 64;                     /* header (40) + empty reservation map (16 zero bytes), 8-aligned */
}

static inline void fb_begin(struct fdtb *b, const char *name)
{
	fb_tok(b, 1);
	fb_bytes(b, name, (uint32_t)strlen(name) + 1);
}

static inline void fb_end(struct fdtb *b)
{
	fb_tok(b, 2);
}

static inline void fb_prop(struct fdtb *b, const char *name, const void *val, uint32_t len)
{
	uint32_t off = 0;

	while (off < b->str_len && strcmp(b->strs + off, name))
		off += (uint32_t)strlen(b->strs + off) + 1;
	if (off >= b->str_len) {
		off = b->str_len;
		strcpy(b->strs + off, name);
		b->str_len += (uint32_t)strlen(name) + 1;
	}
	fb_tok(b, 3);
	fb_tok(b, len);
	fb_tok(b, off);
	fb_bytes(b, val, len);
}

static inline void fb_u32(struct fdtb *b, const char *name, uint32_t v)
{
	uint8_t t[4];

	fb_put32(t, v);
	fb_prop(b, name, t, 4);
}

/* reg = <addr size> with `ac` address cells and `sc` size cells */
static inline void fb_reg(struct fdtb *b, unsigned ac, unsigned sc, uint64_t addr, uint64_t size)
{
	uint8_t t[16];
	uint32_t n = 0;

	for (unsigned i = 0; i < ac; i++, n += 4)
		fb_put32(t + n, (uint32_t)(addr >> (32 * (ac - 1 - i))));
	for (unsigned i = 0; i < sc; i++, n += 4)
		fb_put32(t + n, (uint32_t)(size >> (32 * (sc - 1 - i))));
	fb_prop(b, "reg", t, n);
}

/* `list` is a NUL-separated list with the final NUL included in `len` ("a\0b\0"). */
static inline void fb_strs(struct fdtb *b, const char *name, const char *list, uint32_t len)
{
	fb_prop(b, name, list, len);
}

/* Close the tree (END token) and write the header; returns the blob. */
static inline void *fb_finish(struct fdtb *b)
{
	fb_tok(b, 9);
	uint32_t str_off = b->st + b->st_len;
	uint32_t total = str_off + b->str_len;

	memcpy(b->buf + str_off, b->strs, b->str_len);
	fb_put32(b->buf + 0, 0xd00dfeedu);
	fb_put32(b->buf + 4, total);
	fb_put32(b->buf + 8, b->st);
	fb_put32(b->buf + 12, str_off);
	fb_put32(b->buf + 16, 40);
	fb_put32(b->buf + 20, 17);
	fb_put32(b->buf + 24, 16);
	fb_put32(b->buf + 28, 0);
	fb_put32(b->buf + 32, b->str_len);
	fb_put32(b->buf + 36, b->st_len);
	return b->buf;
}

#endif
