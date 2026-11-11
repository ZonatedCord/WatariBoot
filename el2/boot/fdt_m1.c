// SPDX-License-Identifier: MIT
// Watari EL2 T6b (PLAN S7): FDT queries for the Apple M1 boot, in one bounded pass.
// Written from the Devicetree Specification (flattened format, chapter 5; node and property conventions,
// chapters 2-3). Node shapes cross-checked against Linux arch/arm64/boot/dts/apple/t8103*.dts* v6.12
// (dual GPL-2.0+ OR MIT; cited, nothing copied) and DESIGN.md section 4.
//
// What is read, and nothing else (DESIGN 4.3): /memreserve/ block; /memory reg; /reserved-memory children reg;
// /cpus/cpu@* reg, status, enable-method, cpu-release-addr; nodes compatible with apple,aic / apple,s5l-uart /
// apple,wdt (compatible, status, reg); /chosen stdout-path and /aliases serial*; /chosen/framebuffer@* (fdt.c).
// Identifying properties (serial-number, smbios, MAC addresses, seeds) are never looked at, copied or printed.
//
// Safe with the MMU off: byte loads only, no unaligned access, no libc (build -mstrict-align -fno-builtin).
// Cells: a node's "reg" is decoded with the #address-cells/#size-cells of its parent, tracked per depth when the
// node opens (a parent without the property has the defaults 2 and 1, Devicetree Specification 2.3.5); no address
// translation (the /soc children asked for have identity "ranges", [verified: DESIGN 4.2]).

#include <stdint.h>
#include "fdt.h"
#include "fdt_m1.h"

#define T_BEGIN 1
#define T_END_NODE 2
#define T_PROP 3
#define T_NOP 4
#define T_END 9
#define MAXD 16
#define MAX_UART 8
#define MAX_ALIAS 4
#define MAX_FDT_SIZE 0x10000000u

static uint32_t rd32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t rd64(const uint8_t *p)
{
	return ((uint64_t)rd32(p) << 32) | rd32(p + 4);
}

static uint64_t rd_cells(const uint8_t *p, unsigned n)
{
	uint64_t v = 0;

	for (unsigned i = 0; i < n; i++)
		v = (v << 32) | rd32(p + 4 * i);
	return v;
}

static bool seq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

/* Node name matches `base` or `base@...`. */
static bool name_is(const char *name, const char *base)
{
	while (*base && *name == *base) {
		name++;
		base++;
	}
	return !*base && (*name == 0 || *name == '@');
}

static bool has_prefix(const char *s, const char *pre)
{
	while (*pre && *s == *pre) {
		s++;
		pre++;
	}
	return !*pre;
}

/* Does the NUL-separated list contain `s`? An unterminated tail makes the whole list invalid. */
static bool list_has(const uint8_t *val, uint32_t len, const char *s)
{
	uint32_t i = 0;
	bool found = false;

	while (i < len) {
		uint32_t j = i;

		while (j < len && val[j])
			j++;
		if (j >= len)
			return false;
		if (seq((const char *)val + i, s))
			found = true;
		i = j + 1;
	}
	return found;
}

static bool first_is(const uint8_t *val, uint32_t len, const char *s)
{
	uint32_t j = 0;

	while (j < len && val[j])
		j++;
	return j < len && seq((const char *)val, s);
}

/* absent or "okay"/"ok" = usable. `present` says whether a status property was seen. */
static bool status_ok(bool present, const uint8_t *val, uint32_t len)
{
	if (!present)
		return true;
	return len >= 1 && val[len - 1] == 0 && (seq((const char *)val, "okay") || seq((const char *)val, "ok"));
}

static void zero(void *p, unsigned long n)
{
	volatile uint8_t *b = p;

	while (n--)
		*b++ = 0;
}

/* ---- header and reservation block ------------------------------------------------------------------------ */

struct hdr {
	const uint8_t *h;
	uint32_t total, off_rsv, off_s, sz_s, off_str, sz_str;
};

static int hdr_check(const void *fdt, struct hdr *x)
{
	const uint8_t *h = fdt;

	if (!fdt || rd32(h) != 0xd00dfeedu)
		return -1;
	x->h = h;
	x->total = rd32(h + 4);
	x->off_s = rd32(h + 8);
	x->off_str = rd32(h + 12);
	x->off_rsv = rd32(h + 16);
	x->sz_str = rd32(h + 32);
	x->sz_s = rd32(h + 36);
	if (x->total < 40 || x->total > MAX_FDT_SIZE)
		return -1;
	if ((x->off_s & 3) || (x->sz_s & 3) || (x->off_rsv & 7))
		return -1;
	if ((uint64_t)x->off_s + x->sz_s > x->total || (uint64_t)x->off_str + x->sz_str > x->total)
		return -1;
	if (x->off_rsv < 40 || x->off_rsv > x->off_s || x->off_s < 40)
		return -1;
	return 0;
}

int fdt_reserve_entry(const void *fdt, unsigned idx, uint64_t *base, uint64_t *size)
{
	struct hdr x;

	if (hdr_check(fdt, &x))
		return -1;
	/* walk from the start: entries are 16 bytes, the list ends with an all-zero entry, and must end before
	 * the structure block */
	for (uint64_t off = x.off_rsv, i = 0; off + 16 <= x.off_s; off += 16, i++) {
		uint64_t a = rd64(x.h + off), s = rd64(x.h + off + 8);

		if (!a && !s)
			return 0;
		if (i == idx) {
			*base = a;
			*size = s;
			return 1;
		}
	}
	return -1;
}

/* ---- range arithmetic -------------------------------------------------------------------------------------- */

static bool range_ok(uint64_t base, uint64_t size)
{
	return size != 0 && base + size > base;
}

void m1_subtract(const struct m1_range *mem, unsigned nmem, const struct m1_range *resv, unsigned nresv,
		 struct m1_range *out, unsigned out_max, unsigned *nout, unsigned *wholly, bool *truncated)
{
	unsigned n = 0;

	*wholly = 0;
	for (unsigned m = 0; m < nmem; m++) {
		/* pieces of this one pair live in out[first..n) */
		unsigned first = n;

		if (!range_ok(mem[m].base, mem[m].size))
			continue;
		if (n >= out_max) {
			*truncated = true;
			continue;
		}
		out[n].base = mem[m].base;
		out[n].size = mem[m].size;
		n++;
		for (unsigned r = 0; r < nresv; r++) {
			uint64_t rb = resv[r].base, re = rb + resv[r].size;

			if (!range_ok(rb, resv[r].size))
				continue;
			unsigned end = n;

			for (unsigned i = first; i < end; i++) {
				uint64_t pb = out[i].base, pe = pb + out[i].size;

				if (re <= pb || rb >= pe)
					continue;
				/* overlap: keep the head [pb, rb) and the tail [re, pe) */
				bool head = rb > pb, tail = re < pe;

				if (head) {
					out[i].size = rb - pb;
					if (tail) {
						if (n < out_max) {
							out[n].base = re;
							out[n].size = pe - re;
							n++;
						} else {
							*truncated = true;     /* the tail is dropped: never extra RAM */
						}
					}
				} else if (tail) {
					out[i].base = re;
					out[i].size = pe - re;
				} else {
					out[i].size = 0;                /* wholly covered */
				}
			}
		}
		/* compact the pieces of this pair (drop the emptied ones) */
		unsigned w = first;

		for (unsigned i = first; i < n; i++)
			if (out[i].size) {
				out[w] = out[i];
				w++;
			}
		n = w;
		if (n == first)
			(*wholly)++;
	}
	*nout = n;
}

/* ---- the single pass --------------------------------------------------------------------------------------- */

struct lvl {
	const char *name;
	uint32_t ac, sc;            /* #address-cells / #size-cells declared by this node (defaults 2, 1) */
	uint32_t rac, rsc;          /* cells this node's own reg is decoded with: the parent's, as final at the opening */
	bool done;                  /* node already evaluated (on its first child or its end) */
};

struct cur {                        /* properties of the node being read */
	bool has_status, devtype_mem;
	uint32_t status_len;
	const uint8_t *status;
	bool c_aic, c_uart, c_wdt;
	const uint8_t *reg;
	uint32_t reg_len;
	const uint8_t *rel;
	uint32_t rel_len;
	bool spin;
};

struct ucand {
	uint64_t base, size;
	bool path_ok;
	char path[M1_PATH_MAX];
};

struct alias {
	char name[12];
	char path[M1_PATH_MAX];
};

struct ctx {
	struct m1_platform *p;
	struct lvl l[MAXD];
	struct cur c;
	struct ucand u[MAX_UART];
	unsigned nu;
	struct alias al[MAX_ALIAS];
	unsigned nal;
	char stdout_path[M1_PATH_MAX];
	bool have_stdout;
	bool aic_done, wdt_done;
};

static void add_range(struct m1_platform *p, struct m1_range *arr, unsigned *n, unsigned max, uint64_t base,
		      uint64_t size)
{
	if (!range_ok(base, size)) {
		p->bad_ranges++;
		return;
	}
	if (*n >= max) {
		p->lists_truncated = true;
		return;
	}
	arr[*n].base = base;
	arr[*n].size = size;
	(*n)++;
}

/* copy a string property (must be NUL-terminated inside len) bounded to max-1 characters; false if not */
static bool copy_str(char *out, unsigned max, const uint8_t *val, uint32_t len)
{
	uint32_t n = 0;

	while (n < len && val[n]) {
		if (n + 1 >= max)
			return false;
		out[n] = (char)val[n];
		n++;
	}
	if (n >= len)
		return false;
	out[n] = 0;
	return true;
}

/* "/a/b" built from the open nodes l[1..d] */
static bool build_path(const struct ctx *c, int d, char *out)
{
	unsigned n = 0;

	if (d == 0) {
		out[0] = '/';
		out[1] = 0;
		return true;
	}
	for (int i = 1; i <= d; i++) {
		if (n + 1 >= M1_PATH_MAX)
			return false;
		out[n++] = '/';
		for (const char *s = c->l[i].name; *s; s++) {
			if (n + 1 >= M1_PATH_MAX)
				return false;
			out[n++] = *s;
		}
	}
	out[n] = 0;
	return true;
}

/* decode all pairs of `reg` with (ac, sc); returns the number of pairs written */
static unsigned reg_pairs(const uint8_t *reg, uint32_t len, uint32_t ac, uint32_t sc, struct m1_range *out,
			  unsigned max)
{
	if (!reg || ac < 1 || ac > 2 || sc > 2)
		return 0;
	uint32_t pair = 4 * (ac + sc), n = 0;

	while (n < max && len >= (n + 1) * pair) {
		out[n].base = rd_cells(reg + n * pair, ac);
		out[n].size = sc ? rd_cells(reg + n * pair + 4 * ac, sc) : 0;
		n++;
	}
	return n;
}

/* The node at depth d (properties complete) is evaluated. */
static void finalize(struct ctx *x, int d)
{
	struct m1_platform *p = x->p;
	struct cur *c = &x->c;
	struct lvl *n = &x->l[d];

	if (n->done)
		return;
	n->done = true;
	if (d < 1 || !status_ok(c->has_status, c->status, c->status_len))
		return;

	struct m1_range pr[M1_MAX_MEM > M1_MAX_RESV ? M1_MAX_MEM : M1_MAX_RESV];
	bool parent_is_cpus = d == 2 && seq(x->l[1].name, "cpus");
	bool parent_is_resv = d == 2 && seq(x->l[1].name, "reserved-memory");

	if (d == 1 && (name_is(n->name, "memory") || c->devtype_mem)) {
		unsigned k = reg_pairs(c->reg, c->reg_len, n->rac, n->rsc, pr, M1_MAX_MEM);

		for (unsigned i = 0; i < k; i++)
			add_range(p, p->mem, &p->nmem, M1_MAX_MEM, pr[i].base, pr[i].size);
	} else if (parent_is_cpus && name_is(n->name, "cpu")) {
		if (reg_pairs(c->reg, c->reg_len, n->rac, n->rsc, pr, 1) == 1 && n->rsc == 0) {
			if (p->ncpu >= M1_MAX_CPUS) {
				p->cpus_truncated = true;
			} else {
				struct m1_cpu *u = &p->cpu[p->ncpu++];

				u->reg = pr[0].base;
				u->has_release = c->rel && c->rel_len == 8;
				u->release_addr = u->has_release ? rd64(c->rel) : 0;
				u->spin_table = c->spin;
			}
		}
	} else if (parent_is_resv) {
		unsigned k = reg_pairs(c->reg, c->reg_len, n->rac, n->rsc, pr, M1_MAX_RESV);

		for (unsigned i = 0; i < k; i++)
			add_range(p, p->resv, &p->nresv, M1_MAX_RESV, pr[i].base, pr[i].size);
	}

	if (c->c_aic && !x->aic_done) {
		p->naic = reg_pairs(c->reg, c->reg_len, n->rac, n->rsc, p->aic, M1_MAX_AIC);
		if (p->naic) {
			x->aic_done = true;
			p->is_m1 = true;
		}
	}
	if (c->c_wdt && !x->wdt_done && reg_pairs(c->reg, c->reg_len, n->rac, n->rsc, pr, 1) == 1) {
		x->wdt_done = true;
		p->wdt.found = true;
		p->wdt.base = pr[0].base;
		p->wdt.size = pr[0].size;
	}
	if (c->c_uart && x->nu < MAX_UART && reg_pairs(c->reg, c->reg_len, n->rac, n->rsc, pr, 1) == 1) {
		struct ucand *u = &x->u[x->nu++];

		u->base = pr[0].base;
		u->size = pr[0].size;
		u->path_ok = build_path(x, d, u->path);
	}
}

static void on_prop(struct ctx *x, int d, const char *prop, const uint8_t *val, uint32_t len)
{
	struct cur *c = &x->c;
	struct lvl *n = &x->l[d];

	if (n->done)
		return;                 /* a property after a child: not valid, ignored */
	if (len == 4 && seq(prop, "#address-cells")) {
		n->ac = rd32(val);
	} else if (len == 4 && seq(prop, "#size-cells")) {
		n->sc = rd32(val);
	} else if (seq(prop, "status")) {
		c->has_status = true;
		c->status = val;
		c->status_len = len;
	} else if (seq(prop, "reg")) {
		c->reg = val;
		c->reg_len = len;
	} else if (seq(prop, "compatible")) {
		c->c_aic = list_has(val, len, "apple,aic");
		c->c_uart = list_has(val, len, "apple,s5l-uart");
		c->c_wdt = list_has(val, len, "apple,wdt");
	} else if (seq(prop, "device_type")) {
		c->devtype_mem = first_is(val, len, "memory");
	} else if (seq(prop, "cpu-release-addr")) {
		c->rel = val;
		c->rel_len = len;
	} else if (seq(prop, "enable-method")) {
		c->spin = first_is(val, len, "spin-table");
	} else if (d == 1 && seq(x->l[1].name, "chosen") &&
		   (seq(prop, "stdout-path") || seq(prop, "linux,stdout-path"))) {
		x->have_stdout = copy_str(x->stdout_path, sizeof(x->stdout_path), val, len);
	} else if (d == 1 && seq(x->l[1].name, "aliases") && has_prefix(prop, "serial") && x->nal < MAX_ALIAS) {
		struct alias *a = &x->al[x->nal];
		unsigned k = 0;

		while (prop[k] && k + 1 < sizeof(a->name)) {
			a->name[k] = prop[k];
			k++;
		}
		a->name[k] = 0;
		if (!prop[k] && copy_str(a->path, sizeof(a->path), val, len))
			x->nal++;
	}
}

static const char *alias_path(const struct ctx *x, const char *name)
{
	for (unsigned i = 0; i < x->nal; i++)
		if (seq(x->al[i].name, name))
			return x->al[i].path;
	return 0;
}

/* stdout-path "target[:options]": a path, or an alias name */
static const char *resolve_target(const struct ctx *x, char *buf)
{
	unsigned k = 0;

	while (x->stdout_path[k] && x->stdout_path[k] != ':') {
		buf[k] = x->stdout_path[k];
		k++;
	}
	buf[k] = 0;
	if (buf[0] == '/')
		return buf;
	return alias_path(x, buf);
}

static const struct ucand *find_uart(const struct ctx *x, const char *path)
{
	for (unsigned i = 0; path && i < x->nu; i++)
		if (x->u[i].path_ok && seq(x->u[i].path, path))
			return &x->u[i];
	return 0;
}

static void pick_uart(struct ctx *x)
{
	struct m1_platform *p = x->p;
	const struct ucand *u = 0;
	bool stdo = false;
	char buf[M1_PATH_MAX];

	if (x->have_stdout)
		u = find_uart(x, resolve_target(x, buf));
	if (!u)
		u = find_uart(x, alias_path(x, "serial0"));
	if (u)
		stdo = true;
	else if (x->nu)
		u = &x->u[0];
	if (u) {
		p->uart.found = true;
		p->uart.base = u->base;
		p->uart.size = u->size;
		p->uart.from_stdout = stdo;
	}
}

int fdt_m1_parse(const void *fdt, struct m1_platform *p)
{
	struct hdr hd;
	struct ctx x;

	zero(p, sizeof(*p));
	if (hdr_check(fdt, &hd))
		return -1;
	zero(&x, sizeof(x));
	x.p = p;

	const uint8_t *h = hd.h;
	const char *strs = (const char *)h + hd.off_str;
	uint32_t off = hd.off_s, end = hd.off_s + hd.sz_s;
	int depth = -1;
	bool root_seen = false, ended = false;

	while (off + 4 <= end) {
		uint32_t tok = rd32(h + off);

		off += 4;
		if (tok == T_BEGIN) {
			const char *name = (const char *)h + off;
			uint32_t n = 0;

			while (off + n < end && name[n])
				n++;
			if (off + n >= end || depth + 1 >= MAXD || (depth < 0 && root_seen))
				goto bad;
			if (depth >= 0)
				finalize(&x, depth);    /* the parent's properties are complete */
			depth++;
			root_seen = true;
			x.l[depth].name = name;
			x.l[depth].ac = 2;
			x.l[depth].sc = 1;
			x.l[depth].rac = depth ? x.l[depth - 1].ac : 2;
			x.l[depth].rsc = depth ? x.l[depth - 1].sc : 1;
			x.l[depth].done = false;
			zero(&x.c, sizeof(x.c));
			off += (n + 1 + 3) & ~3u;
		} else if (tok == T_END_NODE) {
			if (depth < 0)
				goto bad;
			finalize(&x, depth);
			depth--;
		} else if (tok == T_PROP) {
			if (depth < 0 || off + 8 > end)
				goto bad;
			uint32_t len = rd32(h + off), nameoff = rd32(h + off + 4);

			off += 8;
			if (len > end - off || nameoff >= hd.sz_str)
				goto bad;
			char prop[24];
			uint32_t k = 0;

			/* name bounded by the strings block; a longer or unterminated name matches nothing looked for */
			while (k < sizeof(prop) - 1 && nameoff + k < hd.sz_str && strs[nameoff + k]) {
				prop[k] = strs[nameoff + k];
				k++;
			}
			prop[k] = 0;
			if (k == sizeof(prop) - 1 || nameoff + k >= hd.sz_str)
				prop[0] = 0;
			on_prop(&x, depth, prop, h + off, len);
			off += (len + 3) & ~3u;
		} else if (tok == T_NOP) {
			continue;
		} else if (tok == T_END) {
			ended = true;
			break;
		} else {
			goto bad;
		}
	}
	if (!ended || depth != -1 || !root_seen)
		goto bad;

	/* reservations */
	for (unsigned i = 0;; i++) {
		uint64_t b, s;
		int r = fdt_reserve_entry(fdt, i, &b, &s);

		if (r < 0)
			goto bad;
		if (!r)
			break;
		add_range(p, p->resv, &p->nresv, M1_MAX_RESV, b, s);
		if (i > 4 * M1_MAX_RESV) {      /* long list: stop reading, the rest cannot be kept */
			p->lists_truncated = true;
			break;
		}
	}
	pick_uart(&x);
	m1_subtract(p->mem, p->nmem, p->resv, p->nresv, p->usable, M1_MAX_USABLE, &p->nusable,
		    &p->mem_wholly_reserved, &p->lists_truncated);
	fdt_get_framebuffer((void *)(uintptr_t)fdt, &p->fb);
	return 0;
bad:
	zero(p, sizeof(*p));
	return -1;
}
