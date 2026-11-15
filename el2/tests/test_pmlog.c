/* SPDX-License-Identifier: MIT */
/* Host tests: the post-mortem log ring (wel2_pmlog.h): header and CRC, wrap, truncation of a write longer than the ring,
 * the clean order (data lines first, header last), the lock timeout, the boot counter, and four writer threads. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "wel2_pmlog.h"

#define SZ 0x4000u                      /* the smallest region: 16 KB, capacity 16320 */

static uint8_t g_reg[0x10000] __attribute__((aligned(16384)));
static struct wel2_pmlog g_r;
static uint8_t g_out[0x10000];

/* the clean callback records what it was asked to clean */
static struct { const void *p; unsigned long n; } g_cl[64];
static unsigned g_ncl;
static void rec_clean(const void *p, unsigned long n)
{
	if (g_ncl < 64) {
		g_cl[g_ncl].p = p;
		g_cl[g_ncl].n = n;
		g_ncl++;
	}
}

static void fresh(uint32_t size)
{
	memset(g_reg, 0, sizeof(g_reg));
	g_ncl = 0;
	CHECK_EQ(wel2_pmlog_init(&g_r, g_reg, size, rec_clean), 0);
}

static void put(const char *s)
{
	wel2_pmlog_write(&g_r, s, strlen(s));
}

static int rd(uint32_t size, uint32_t *n, struct wel2_pmlog_hdr *h)
{
	return wel2_pmlog_read(g_reg, size, g_out, n, h);
}

static void test_header(void)
{
	struct wel2_pmlog_hdr h;
	uint32_t n = 99;

	CHECK_EQ(sizeof(struct wel2_pmlog_hdr), 64);
	fresh(SZ);
	CHECK_EQ(g_ncl, 1);                                  /* init cleaned exactly the header */
	CHECK(g_cl[0].p == g_reg && g_cl[0].n == 64);
	CHECK(memcmp(g_reg, "WEL2PMLG", 8) == 0);            /* the magic is the documented byte string */
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(n, 0);
	CHECK_EQ(h.version, 1);
	CHECK_EQ(h.hdr_size, 64);
	CHECK_EQ(h.capacity, SZ - 64);
	CHECK_EQ(h.boots, 1);
	CHECK_EQ(h.total, 0);
	CHECK_EQ(h.crc, wel2_pmlog_crc32(g_reg, 60));
	CHECK_EQ(wel2_pmlog_crc32("123456789", 9), 0xcbf43926u);   /* the standard CRC-32 check value */
	/* bad sizes and a NULL base are refused and leave the region alone */
	CHECK_EQ(wel2_pmlog_init(&g_r, g_reg, 0x3000, 0), -1);
	CHECK_EQ(wel2_pmlog_init(&g_r, g_reg, 0x4001, 0), -1);
	CHECK_EQ(wel2_pmlog_init(&g_r, g_reg, WEL2_PMLOG_MAX + 0x4000, 0), -1);
	CHECK_EQ(wel2_pmlog_init(&g_r, 0, SZ, 0), -1);
}

static void test_basic_and_clean(void)
{
	uint32_t n;
	struct wel2_pmlog_hdr h;

	fresh(SZ);
	g_ncl = 0;
	put("Watari EL2: hello\n");
	CHECK_EQ(g_ncl, 2);                                  /* data first, header last */
	CHECK(g_cl[0].p == g_reg + 64 && g_cl[0].n == 18);
	CHECK(g_cl[1].p == g_reg && g_cl[1].n == 64);
	put("second\n");
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(n, 25);
	CHECK(memcmp(g_out, "Watari EL2: hello\nsecond\n", 25) == 0);
	CHECK_EQ(h.total, 25);
	CHECK_EQ(h.wraps, 0);
	put("");                                             /* an empty write changes nothing */
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(h.total, 25);
}

static void test_wrap(void)
{
	char line[40];
	uint32_t n, cap = SZ - 64, total = 0;
	struct wel2_pmlog_hdr h;

	fresh(SZ);
	for (int i = 0; i < 1000; i++) {                     /* 1000 lines of 20 bytes: 20000 > 16320, one wrap */
		snprintf(line, sizeof(line), "line %011d xx\n", i);   /* 5 + 11 + 4 = 20 bytes */
		CHECK_EQ(strlen(line), 20);
		put(line);
		total += 20;
	}
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(h.total, total);
	CHECK_EQ(h.wraps, total / cap);
	CHECK_EQ(h.wraps, 1);
	CHECK_EQ(n, cap);
	/* the newest 16320 bytes, oldest first: the last byte is the last line's newline and the tail of the text is exact */
	CHECK_EQ(g_out[n - 1], '\n');
	snprintf(line, sizeof(line), "line %011d xx\n", 999);
	CHECK(memcmp(g_out + n - 20, line, 20) == 0);
	for (int k = 1; k < 100; k++) {                      /* every line wholly inside the kept tail is intact */
		int idx = 999 - k;

		snprintf(line, sizeof(line), "line %011d xx\n", idx);
		CHECK(memcmp(g_out + n - 20 * (k + 1), line, 20) == 0);
	}
	/* a write that straddles the end of the data area is cleaned in two pieces, then the header */
	fresh(SZ);
	for (unsigned i = 0; i < 3263; i++)                  /* 3263 * 5 = 16315 = cap - 5 */
		put("abcde");
	g_ncl = 0;
	put("0123456789");
	CHECK_EQ(g_ncl, 3);
	CHECK(g_cl[0].p == g_reg + 64 + 16315 && g_cl[0].n == 5);
	CHECK(g_cl[1].p == g_reg + 64 && g_cl[1].n == 5);
	CHECK(g_cl[2].p == g_reg && g_cl[2].n == 64);
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK(memcmp(g_out + n - 10, "0123456789", 10) == 0);
}

static void test_truncation(void)
{
	static char big[3 * SZ];
	uint32_t n, cap = SZ - 64;
	struct wel2_pmlog_hdr h;

	fresh(SZ);
	put("start\n");
	for (unsigned i = 0; i < sizeof(big); i++)
		big[i] = (char)('a' + i % 26);
	wel2_pmlog_write(&g_r, big, sizeof(big));
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(n, cap);
	CHECK_EQ(h.total, 6 + sizeof(big));                  /* the count keeps what was offered, the data what fits */
	CHECK_EQ(h.wraps, h.total / cap);
	/* the kept bytes are the last cap bytes of the big write */
	CHECK(memcmp(g_out, big + sizeof(big) - cap, cap) == 0);
	put("tail\n");
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK(memcmp(g_out + n - 5, "tail\n", 5) == 0);
}

static void test_crc_and_validation(void)
{
	uint32_t n;
	struct wel2_pmlog_hdr h;

	fresh(SZ);
	put("x\n");
	CHECK_EQ(rd(SZ, &n, &h), 0);
	g_reg[0x18] ^= 1;                                    /* a torn header: the total changed, the CRC did not */
	CHECK_EQ(rd(SZ, &n, &h), -2);
	g_reg[0x18] ^= 1;
	CHECK_EQ(rd(SZ, &n, &h), 0);
	g_reg[3] ^= 0xff;                                    /* the magic */
	CHECK_EQ(rd(SZ, &n, &h), -1);
	g_reg[3] ^= 0xff;
	/* a region of another size than the header says: inconsistent */
	CHECK_EQ(rd(SZ + 0x4000, &n, &h), -3);
	CHECK_EQ(rd(0x1000, &n, &h), -1);
	/* inconsistent but CRC-valid fields (wraps) */
	struct wel2_pmlog_hdr *hh = (struct wel2_pmlog_hdr *)g_reg;
	hh->wraps = 7;
	hh->crc = wel2_pmlog_crc32(hh, 60);
	CHECK_EQ(rd(SZ, &n, &h), -3);
}

static void test_boot_counter(void)
{
	uint32_t n;
	struct wel2_pmlog_hdr h;

	fresh(SZ);
	put("first boot\n");
	CHECK_EQ(wel2_pmlog_init(&g_r, g_reg, SZ, rec_clean), 0);   /* a valid header is found: boots 2, the ring restarts */
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(h.boots, 2);
	CHECK_EQ(h.total, 0);
	CHECK_EQ(wel2_pmlog_init(&g_r, g_reg, SZ, rec_clean), 0);
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(h.boots, 3);
	g_reg[8] ^= 1;                                       /* a damaged header counts as fresh */
	CHECK_EQ(wel2_pmlog_init(&g_r, g_reg, SZ, rec_clean), 0);
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(h.boots, 1);
}

static void test_lock_timeout(void)
{
	uint32_t n;
	struct wel2_pmlog_hdr h;

	fresh(SZ);
	g_r.spin_limit = 100;
	g_r.lock = 1;                                        /* a core died holding it */
	put("lost\n");
	put("lost too\n");
	CHECK_EQ(g_r.dropped, 2);
	g_r.lock = 0;
	put("kept\n");
	CHECK_EQ(rd(SZ, &n, &h), 0);
	CHECK_EQ(n, 5);
	CHECK(memcmp(g_out, "kept\n", 5) == 0);
	CHECK_EQ(h.dropped, 2);
	CHECK_EQ(g_r.dropped, 0);
}

/* four threads, each writing whole lines "T<id> <seq>\n" with a lot of padding so that a torn write would show */
#define THREADS 4
#define PER     400
static void *writer(void *arg)
{
	long id = (long)arg;
	char line[48];

	for (int i = 0; i < PER; i++) {
		int len = snprintf(line, sizeof(line), "T%ld %05d ---------------------------\n", id, i);

		wel2_pmlog_write(&g_r, line, (unsigned long)len);
	}
	return 0;
}

static void test_threads(void)
{
	pthread_t t[THREADS];
	uint32_t n, size = 0x40000;
	struct wel2_pmlog_hdr h;
	static uint8_t big_reg[0x40000] __attribute__((aligned(16384)));
	static uint8_t big_out[0x40000];
	int seen[THREADS] = { 0 }, next[THREADS] = { 0 }, bad = 0;

	memset(big_reg, 0, sizeof(big_reg));
	CHECK_EQ(wel2_pmlog_init(&g_r, big_reg, size, 0), 0);
	for (long i = 0; i < THREADS; i++)
		pthread_create(&t[i], 0, writer, (void *)i);
	for (int i = 0; i < THREADS; i++)
		pthread_join(t[i], 0);
	CHECK_EQ(wel2_pmlog_read(big_reg, size, big_out, &n, &h), 0);
	CHECK_EQ(h.dropped, 0);
	CHECK_EQ(h.wraps, 0);                                /* 1600 lines of 37 bytes = 59200 < the 262080 capacity */
	for (uint32_t i = 0; i < n;) {
		long id;
		int seq, used = 0;
		char *nl = memchr(big_out + i, '\n', n - i);

		if (!nl || sscanf((char *)big_out + i, "T%ld %d %n", &id, &seq, &used) < 2 || id < 0 || id >= THREADS ||
		    seq != next[id] || nl - (char *)(big_out + i) != 36) {
			bad++;
			break;
		}
		next[id]++;
		seen[id]++;
		i = (uint32_t)(nl - (char *)big_out) + 1;
	}
	CHECK_EQ(bad, 0);                                    /* every line intact, per-thread order kept */
	for (int i = 0; i < THREADS; i++)
		CHECK_EQ(seen[i], PER);
}

int main(void)
{
	RUN(test_header);
	RUN(test_basic_and_clean);
	RUN(test_wrap);
	RUN(test_truncation);
	RUN(test_crc_and_validation);
	RUN(test_boot_counter);
	RUN(test_lock_timeout);
	RUN(test_threads);
	TESTS_DONE();
}
