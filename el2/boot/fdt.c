// SPDX-License-Identifier: MIT
// Watari EL2 M1-4: minimal in-place flattened-device-tree patcher.
// Written from the Devicetree Specification (flattened format, chapter 5);
// no third-party code. Only what the hand-off to EDK2 needs (INTERFACES 3.2):
//   /memory/reg      : trim the region the layer occupies at the start of RAM (same length)
//   /psci/method     : "smc" -> "hvc" (same length)
//   /timer/interrupts: read-only, reported so the log shows which PPIs EDK2 will use
// Nothing here changes the size of the blob, so the FDT is patched where QEMU put it.

#include <stdint.h>
#include "boot.h"
#include "fdt.h"

#define FDT_MAGIC       0xd00dfeedU
#define FDT_BEGIN_NODE  1
#define FDT_END_NODE    2
#define FDT_PROP        3
#define FDT_NOP         4
#define FDT_END         9
#define MAX_DEPTH       16

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint64_t be_cells(const uint8_t *p, unsigned cells)
{
    uint64_t v = 0;

    for (unsigned i = 0; i < cells; i++)
        v = (v << 32) | be32(p + 4 * i);
    return v;
}

static void put_be_cells(uint8_t *p, unsigned cells, uint64_t v)
{
    for (unsigned i = 0; i < cells; i++)
        put_be32(p + 4 * i, (uint32_t)(v >> (32 * (cells - 1 - i))));
}

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Node-name match ignoring the "@unit-address" suffix. */
static bool name_is(const char *name, const char *base)
{
    while (*base && *name == *base) {
        name++;
        base++;
    }
    return !*base && (*name == 0 || *name == '@');
}

static uint32_t align4(uint32_t v)
{
    return (v + 3) & ~3u;
}

int fdt_check(const void *fdt)
{
    const uint8_t *h = fdt;

    if (be32(h) != FDT_MAGIC)
        return -1;
    uint32_t total = be32(h + 4), off_s = be32(h + 8), off_str = be32(h + 12);
    uint32_t sz_str = be32(h + 32), sz_s = be32(h + 36);

    if (total < 40 || off_s > total || off_str > total ||
        sz_s > total - off_s || sz_str > total - off_str)
        return -1;
    return 0;
}

uint32_t fdt_totalsize(const void *fdt)
{
    return be32((const uint8_t *)fdt + 4);
}

/* Bytes that really carry data: QEMU leaves totalsize at the 1 MB it allocated. */
uint32_t fdt_used_size(const void *fdt)
{
    const uint8_t *h = fdt;
    uint32_t end = 40;                                  /* header */
    uint32_t e1 = be32(h + 8) + be32(h + 36);           /* structure block */
    uint32_t e2 = be32(h + 12) + be32(h + 32);          /* strings block */
    uint32_t e3 = be32(h + 16) + 16;                    /* memory reservation map terminator */

    if (e1 > end) end = e1;
    if (e2 > end) end = e2;
    if (e3 > end && be32(h + 16) < be32(h + 8)) end = e3 > be32(h + 8) ? be32(h + 8) : e3;
    return (end + 7) & ~7u;
}

void fdt_set_totalsize(void *fdt, uint32_t n)
{
    put_be32((uint8_t *)fdt + 4, n);
}

/* Walk every property; cb returns true to stop. Returns 0 if the walk ended
 * normally or by the callback, -1 for a malformed blob. */
int fdt_walk_props(void *fdt, fdt_prop_cb cb, void *ctx)
{
    uint8_t *h = fdt;

    if (fdt_check(fdt))
        return -1;
    uint32_t off = be32(h + 8), end = off + be32(h + 36);
    const char *strs = (const char *)h + be32(h + 12);
    uint32_t str_size = be32(h + 32);
    const char *stack[MAX_DEPTH];
    int depth = -1;

    while (off + 4 <= end) {
        uint32_t tok = be32(h + off);

        off += 4;
        if (tok == FDT_BEGIN_NODE) {
            const char *name = (const char *)h + off;
            uint32_t n = 0;

            while (off + n < end && name[n])
                n++;
            if (off + n >= end || depth + 1 >= MAX_DEPTH)
                return -1;
            stack[++depth] = name;
            off += align4(n + 1);
        } else if (tok == FDT_END_NODE) {
            if (depth < 0)
                return -1;
            depth--;
        } else if (tok == FDT_PROP) {
            if (off + 8 > end || depth < 0)
                return -1;
            uint32_t len = be32(h + off), nameoff = be32(h + off + 4);

            off += 8;
            if (len > end - off || nameoff >= str_size)
                return -1;
            if (cb(ctx, (unsigned)depth, stack[depth], strs + nameoff, h + off, len))
                return 0;
            off += align4(len);
        } else if (tok == FDT_NOP) {
            continue;
        } else if (tok == FDT_END) {
            return 0;
        } else {
            return -1;
        }
    }
    return -1;
}

struct patch_ctx {
    struct fdt_patch_info *info;
    uint64_t carve_base, carve_size, ram_end;
    unsigned addr_cells, size_cells;
};

static bool patch_cb(void *vctx, unsigned depth, const char *node, const char *prop,
                     uint8_t *val, uint32_t len)
{
    struct patch_ctx *c = vctx;
    struct fdt_patch_info *i = c->info;

    if (depth == 0 && len == 4) {
        if (str_eq(prop, "#address-cells"))
            c->addr_cells = be32(val);
        else if (str_eq(prop, "#size-cells"))
            c->size_cells = be32(val);
    } else if (depth == 1 && name_is(node, "memory") && str_eq(prop, "reg") &&
               !i->mem_found) {
        unsigned a = c->addr_cells, s = c->size_cells;

        if (a < 1 || a > 2 || s < 1 || s > 2 || len < 4 * (a + s))
            return false;
        uint64_t base = be_cells(val, a), size = be_cells(val + 4 * a, s);

        i->mem_found = true;
        i->mem_base_old = base;
        i->mem_size_old = size;
        if (base == c->carve_base && size > c->carve_size) {
            put_be_cells(val, a, base + c->carve_size);
            put_be_cells(val + 4 * a, s, size - c->carve_size);
            i->mem_patched = true;
            i->mem_base_new = base + c->carve_size;
            i->mem_size_new = size - c->carve_size;
            if (c->ram_end && i->mem_base_new < c->ram_end &&
                i->mem_base_new + i->mem_size_new > c->ram_end) {
                i->mem_size_new = c->ram_end - i->mem_base_new;    /* never hand out an unmapped tail */
                put_be_cells(val + 4 * a, s, i->mem_size_new);
                i->mem_trimmed = true;
            }
        }
    } else if (depth == 1 && name_is(node, "psci") && str_eq(prop, "method") && len == 4) {
        const char *m = (const char *)val;

        i->psci_found = true;
        if (m[0] == 's' && m[1] == 'm' && m[2] == 'c' && m[3] == 0) {
            val[0] = 'h';
            val[1] = 'v';
            val[2] = 'c';
            i->psci_patched = true;
        } else if (m[0] == 'h' && m[1] == 'v' && m[2] == 'c' && m[3] == 0) {
            i->psci_already_hvc = true;
        }
    } else if (depth == 1 && name_is(node, "timer") && str_eq(prop, "interrupts") &&
               len >= 12 * 3) {
        i->timer_found = true;
        for (unsigned k = 0; k < 4 && len >= 12 * (k + 1); k++) {
            /* <type number flags>: type 1 = PPI, INTID = 16 + number */
            uint32_t type = be32(val + 12 * k), num = be32(val + 12 * k + 4);
            i->timer_intid[k] = type == 1 ? 16 + num : num;
        }
    }
    return false;
}

int fdt_patch_for_guest(void *fdt, uint64_t carve_base, uint64_t carve_size, uint64_t ram_end,
                        struct fdt_patch_info *info)
{
    struct patch_ctx c = { info, carve_base, carve_size, ram_end, 2, 2 };

    *info = (struct fdt_patch_info){ 0 };
    return fdt_walk_props(fdt, patch_cb, &c);
}

struct bargs_ctx {
    char *out;
    unsigned max;
    int len;
};

static bool bargs_cb(void *vctx, unsigned depth, const char *node, const char *prop,
                     uint8_t *val, uint32_t len)
{
    struct bargs_ctx *c = vctx;

    if (depth == 1 && name_is(node, "chosen") && str_eq(prop, "bootargs")) {
        unsigned n = 0;

        while (n < len && val[n] && n + 1 < c->max) {
            c->out[n] = (char)val[n];
            n++;
        }
        c->out[n] = 0;
        c->len = (int)n;
        return true;
    }
    return false;
}

int fdt_get_bootargs(void *fdt, char *out, unsigned max)
{
    struct bargs_ctx c = { out, max, -1 };

    if (!max)
        return -1;
    out[0] = 0;
    if (fdt_walk_props(fdt, bargs_cb, &c))
        return -1;
    return c.len;
}

struct mem_ctx {
    struct wel2_ram *ram;
    uint64_t min_base, max_end;
    unsigned addr_cells, size_cells;
    bool found;
    int rc;
};

static bool mem_cb(void *vctx, unsigned depth, const char *node, const char *prop,
                   uint8_t *val, uint32_t len)
{
    struct mem_ctx *c = vctx;

    if (depth == 0 && len == 4) {
        if (str_eq(prop, "#address-cells"))
            c->addr_cells = be32(val);
        else if (str_eq(prop, "#size-cells"))
            c->size_cells = be32(val);
    } else if (depth == 1 && name_is(node, "memory") && str_eq(prop, "reg")) {
        c->found = true;
        c->rc = wel2_ram_from_reg(c->ram, val, len, c->addr_cells, c->size_cells,
                                  c->min_base, c->max_end);
        return true;
    }
    return false;
}

int fdt_get_memory(void *fdt, struct wel2_ram *ram, uint64_t min_base, uint64_t max_end)
{
    struct mem_ctx c = { ram, min_base, max_end, 2, 2, false, -1 };

    if (fdt_walk_props(fdt, mem_cb, &c))
        return -1;
    return c.found ? c.rc : -1;
}
