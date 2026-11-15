/* SPDX-License-Identifier: MIT */
/* Watari EL2: post-mortem log ring (T16j). A byte ring in a reserved RAM region that the layer fills with every console line
 * and the host reads back over the m1n1 proxy after a reset (tools/m3a/pmlog_parse.py is the reader, same layout).
 *
 * Region layout (little endian, size a multiple of 16 KB):
 *   0x00 u64 magic "WEL2PMLG" (bytes 57 45 4c 32 50 4d 4c 47)
 *   0x08 u32 version = 1
 *   0x0c u32 header size = 64
 *   0x10 u32 capacity (data bytes = region size - 64)
 *   0x14 u32 boot counter (1 on a fresh region, previous + 1 when init finds a valid header)
 *   0x18 u64 total (bytes ever written; the newest byte is at data[(total - 1) % capacity])
 *   0x20 u32 wraps (total / capacity)
 *   0x24 u32 dropped (writes lost because the ring lock timed out)
 *   0x28 u8[20] reserved (zero)
 *   0x3c u32 CRC-32 (IEEE, zlib) of bytes 0x00..0x3b
 *   0x40 data[capacity]
 * One writer at a time (an internal spin lock, bounded: a stuck lock drops the write and counts it). After every write the
 * touched data lines, then the header line, are cleaned to the point of coherency through the clean callback, so a reset
 * that follows finds the text in DRAM [hypothesis: DRAM contents survive the M1's reset, Monday check]. */
#ifndef WEL2_PMLOG_H
#define WEL2_PMLOG_H

#include <stdint.h>

#define WEL2_PMLOG_MAGIC   0x474c4d50324c4557ULL      /* "WEL2PMLG" little endian */
#define WEL2_PMLOG_VERSION 1u
#define WEL2_PMLOG_HDR     64u
#define WEL2_PMLOG_MIN     0x4000u                     /* smallest region: 16 KB */
#define WEL2_PMLOG_MAX     0x4000000u                  /* largest: 64 MB (the capacity is a u32 and a block is 32 MB) */

struct wel2_pmlog_hdr {
	uint64_t magic;
	uint32_t version, hdr_size, capacity, boots;
	uint64_t total;
	uint32_t wraps, dropped;
	uint8_t reserved[20];
	uint32_t crc;
};

typedef void (*wel2_pmlog_clean_fn)(const void *p, unsigned long n);

struct wel2_pmlog {
	uint8_t *base;
	uint32_t size, cap;
	uint32_t dropped;                 /* pending, written into the header by the next successful write */
	volatile uint32_t lock;
	wel2_pmlog_clean_fn clean;        /* NULL = no cleaning (host tests) */
	unsigned long spin_limit;         /* 0 = the default */
};

uint32_t wel2_pmlog_crc32(const void *p, unsigned long n);
/* 0 on success; -1 for a bad size (not a multiple of 16 KB, outside MIN..MAX) or a NULL base. Writes the header, boots + 1 when
 * the region already holds a valid header of this version. The data area is not touched. */
int wel2_pmlog_init(struct wel2_pmlog *r, void *base, uint32_t size, wel2_pmlog_clean_fn clean);
/* Append n bytes. A write longer than the capacity keeps its last `capacity` bytes. Never blocks for long, never fails. */
void wel2_pmlog_write(struct wel2_pmlog *r, const char *s, unsigned long n);
/* Reader (host tests, same rules as the Python one): 0 ok, -1 no magic / short, -2 header CRC bad, -3 inconsistent fields.
 * Copies the log, oldest byte first, to out (room for capacity bytes); *n gets its length. */
int wel2_pmlog_read(const void *region, uint32_t size, uint8_t *out, uint32_t *n, struct wel2_pmlog_hdr *hdr);

#endif
