// SPDX-License-Identifier: MIT
// Watari EL2: post-mortem log ring (T16j), see include/wel2_pmlog.h. Pure code: the cleaning is a callback, the lock an
// atomic word, so the layer and the host tests share it. Header and data are written with ordinary stores.

#include "wel2_pmlog.h"

#ifndef WEL2_PMLOG_SPIN
#define WEL2_PMLOG_SPIN (1UL << 20)       /* bounded: a core that died holding the lock must not stop the others */
#endif

uint32_t wel2_pmlog_crc32(const void *p, unsigned long n)
{
	const uint8_t *b = p;
	uint32_t c = 0xffffffffu;

	for (unsigned long i = 0; i < n; i++) {
		c ^= b[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
	}
	return ~c;
}

static void hdr_seal(struct wel2_pmlog_hdr *h)
{
	h->crc = wel2_pmlog_crc32(h, WEL2_PMLOG_HDR - 4);
}

static void clean(struct wel2_pmlog *r, const void *p, unsigned long n)
{
	if (r->clean && n)
		r->clean(p, n);
}

static int hdr_valid(const struct wel2_pmlog_hdr *h, uint32_t size)
{
	if (h->magic != WEL2_PMLOG_MAGIC)
		return -1;
	if (h->crc != wel2_pmlog_crc32(h, WEL2_PMLOG_HDR - 4))
		return -2;
	if (h->version != WEL2_PMLOG_VERSION || h->hdr_size != WEL2_PMLOG_HDR || h->capacity != size - WEL2_PMLOG_HDR ||
	    h->wraps != h->total / h->capacity)
		return -3;
	return 0;
}

int wel2_pmlog_init(struct wel2_pmlog *r, void *base, uint32_t size, wel2_pmlog_clean_fn clean_fn)
{
	struct wel2_pmlog_hdr *h = base;
	uint32_t boots = 1;

	if (!base || size < WEL2_PMLOG_MIN || size > WEL2_PMLOG_MAX || (size & 0x3fff))
		return -1;
	if (hdr_valid(h, size) == 0)
		boots = h->boots + 1;
	r->base = base;
	r->size = size;
	r->cap = size - WEL2_PMLOG_HDR;
	r->dropped = 0;
	r->lock = 0;
	r->clean = clean_fn;
	r->spin_limit = 0;
	r->stuck = 0;
	*h = (struct wel2_pmlog_hdr){ WEL2_PMLOG_MAGIC, WEL2_PMLOG_VERSION, WEL2_PMLOG_HDR, r->cap, boots, 0, 0, 0, { 0 }, 0 };
	hdr_seal(h);
	clean(r, h, WEL2_PMLOG_HDR);
	return 0;
}

void wel2_pmlog_write(struct wel2_pmlog *r, const char *s, unsigned long n)
{
	struct wel2_pmlog_hdr *h = (struct wel2_pmlog_hdr *)r->base;
	uint8_t *data = r->base + WEL2_PMLOG_HDR;
	unsigned long limit = r->spin_limit ? r->spin_limit : WEL2_PMLOG_SPIN, pos, first;
	uint64_t skip = 0;
	int got = 0;

	if (!n)
		return;
	if (r->stuck && limit > 64)     /* a dead holder: do not pay the full bound on every line of a fatal dump */
		limit = 64;
	for (unsigned long i = 0; i < limit; i++) {
		if (!__atomic_exchange_n(&r->lock, 1, __ATOMIC_ACQUIRE)) {
			got = 1;
			break;
		}
		__asm__ volatile("" ::: "memory");
	}
	if (!got) {
		__atomic_fetch_add(&r->dropped, 1, __ATOMIC_RELAXED);
		r->stuck = 1;
		return;
	}
	r->stuck = 0;
	if (n > r->cap) {                              /* keep the tail: the newest bytes matter most */
		skip = n - r->cap;
		s += skip;
		n = r->cap;
	}
	pos = (unsigned long)((h->total + skip) % r->cap);
	first = r->cap - pos < n ? r->cap - pos : n;
	for (unsigned long i = 0; i < first; i++)
		data[pos + i] = (uint8_t)s[i];
	for (unsigned long i = first; i < n; i++)
		data[i - first] = (uint8_t)s[i];
	clean(r, data + pos, first);
	if (first < n)
		clean(r, data, n - first);
	h->total += skip + n;
	h->wraps = (uint32_t)(h->total / r->cap);
	h->dropped += __atomic_exchange_n(&r->dropped, 0, __ATOMIC_RELAXED);
	hdr_seal(h);
	clean(r, h, WEL2_PMLOG_HDR);
	__atomic_store_n(&r->lock, 0, __ATOMIC_RELEASE);
}

int wel2_pmlog_read(const void *region, uint32_t size, uint8_t *out, uint32_t *n, struct wel2_pmlog_hdr *hdr)
{
	const struct wel2_pmlog_hdr *h = region;
	const uint8_t *data = (const uint8_t *)region + WEL2_PMLOG_HDR;
	int rc;

	if (size < WEL2_PMLOG_MIN)
		return -1;
	rc = hdr_valid(h, size);
	if (hdr)
		*hdr = *h;
	if (rc)
		return rc;
	if (h->total <= h->capacity) {
		for (uint32_t i = 0; i < (uint32_t)h->total; i++)
			out[i] = data[i];
		*n = (uint32_t)h->total;
	} else {
		uint32_t pos = (uint32_t)(h->total % h->capacity);

		for (uint32_t i = 0; i < h->capacity; i++)
			out[i] = data[(pos + i) % h->capacity];
		*n = h->capacity;
	}
	return 0;
}
