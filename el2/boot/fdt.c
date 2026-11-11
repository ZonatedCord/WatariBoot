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

struct aic_ctx {
    struct fdt_aic_info *info;
    unsigned addr_cells, size_cells;
    bool compat;
};

/* The node "watari-aic@..." QEMU adds with aic=on (patch 0002 of tools/qemu-watari): compatible
 * "watari,aic-v1", reg, watari,num-irq, watari,num-cpu, watari,routed-irqs (AIC input numbers of
 * the devices wired to the AIC). Only a node at depth 1 with the right compatible string counts. */
static bool aic_cb(void *vctx, unsigned depth, const char *node, const char *prop,
                   uint8_t *val, uint32_t len)
{
    struct aic_ctx *c = vctx;
    struct fdt_aic_info *i = c->info;

    if (depth == 0 && len == 4) {
        if (str_eq(prop, "#address-cells"))
            c->addr_cells = be32(val);
        else if (str_eq(prop, "#size-cells"))
            c->size_cells = be32(val);
    } else if (depth == 1 && name_is(node, "watari-aic")) {
        if (str_eq(prop, "compatible")) {
            c->compat = len == 14 && str_eq((const char *)val, "watari,aic-v1");
        } else if (str_eq(prop, "reg")) {
            unsigned a = c->addr_cells, s = c->size_cells;

            if (a >= 1 && a <= 2 && s >= 1 && s <= 2 && len >= 4 * (a + s)) {
                i->base = be_cells(val, a);
                i->size = be_cells(val + 4 * a, s);
                i->has_reg = true;
            }
        } else if (str_eq(prop, "watari,num-irq") && len == 4) {
            i->nr_irq = be32(val);
        } else if (str_eq(prop, "watari,num-cpu") && len == 4) {
            i->nr_cpu = be32(val);
        } else if (str_eq(prop, "watari,routed-irqs")) {
            for (unsigned k = 0; k + 4 <= len && i->n_routed < FDT_AIC_MAX_ROUTED; k += 4)
                i->routed[i->n_routed++] = be32(val + k);
        }
    }
    return false;
}

int fdt_find_aic(void *fdt, struct fdt_aic_info *info)
{
    struct aic_ctx c = { info, 2, 2, false };

    *info = (struct fdt_aic_info){ 0 };
    if (fdt_walk_props(fdt, aic_cb, &c))
        return -1;
    info->found = c.compat && info->has_reg && info->nr_irq != 0;
    return 0;
}

/* ---- First node compatible with a given string, and its "reg" -------------------------------------------------
 * Own structure walk, so that the cells of every parent are known when a node OPENS, whether or not the node (or its
 * ancestors) carries any property of its own (T5 review note: the former callback-based version only reset its
 * per-depth state when a property was seen). Per depth: the cells the node declares for its children (defaults 2 and
 * 1, Devicetree Specification 2.3.5) and the cells its own reg is decoded with (the parent's, final at the opening
 * because properties precede sub-nodes, 2.2.1). A node with a status other than okay/ok is skipped. A node is
 * evaluated when its first child opens or when it ends. Ranges/address translation is not applied: the nodes asked
 * for (/soc children of the Apple SoC, "ranges;" identity) need none
 * [verified: t8103 /soc has ranges and #address-cells = 2, DESIGN section 4.2; hypothesis for other SoCs]. */
struct compat_node {
    bool match, has_reg, disabled;
    uint64_t base, size;
};

static bool stringlist_has(const uint8_t *val, uint32_t len, const char *s)
{
    uint32_t i = 0;

    while (i < len) {
        uint32_t j = i;

        while (j < len && val[j])
            j++;
        if (j >= len)                   /* not terminated: malformed */
            return false;
        if (str_eq((const char *)val + i, s))
            return true;
        i = j + 1;
    }
    return false;
}

int fdt_find_compatible_reg(void *fdt, const char *compat, struct fdt_reg_info *info)
{
    uint8_t *h = fdt;
    struct compat_node n = { 0 };
    uint32_t a[MAX_DEPTH], s[MAX_DEPTH], ra[MAX_DEPTH], rs[MAX_DEPTH];
    int depth = -1;
    bool pending = false;               /* the node at `depth` has not been evaluated yet */

    info->found = false;
    info->base = info->size = 0;
    if (fdt_check(fdt))
        return -1;
    uint32_t off = be32(h + 8), end = off + be32(h + 36);
    const char *strs = (const char *)h + be32(h + 12);
    uint32_t str_size = be32(h + 32);

    while (off + 4 <= end) {
        uint32_t tok = be32(h + off);

        off += 4;
        if (tok == FDT_BEGIN_NODE || tok == FDT_END_NODE) {
            if (pending && depth >= 1 && n.match && n.has_reg && !n.disabled) {
                info->found = true;             /* the first usable node wins */
                info->base = n.base;
                info->size = n.size;
                return 0;
            }
            pending = false;
        }
        if (tok == FDT_BEGIN_NODE) {
            const char *name = (const char *)h + off;
            uint32_t len = 0;

            while (off + len < end && name[len])
                len++;
            if (off + len >= end || depth + 1 >= MAX_DEPTH)
                return -1;
            depth++;
            a[depth] = 2;                               /* defaults for this node's children */
            s[depth] = 1;
            ra[depth] = depth ? a[depth - 1] : 2;       /* cells for this node's own reg: the parent's */
            rs[depth] = depth ? s[depth - 1] : 1;
            n = (struct compat_node){ 0 };
            pending = true;
            off += align4(len + 1);
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
            char prop[24];
            uint32_t pn = 0;

            while (pn < sizeof(prop) - 1 && nameoff + pn < str_size && strs[nameoff + pn]) {
                prop[pn] = strs[nameoff + pn];
                pn++;
            }
            prop[pn] = 0;
            const uint8_t *v = h + off;

            if (len == 4 && str_eq(prop, "#address-cells")) {
                a[depth] = be32(v);
            } else if (len == 4 && str_eq(prop, "#size-cells")) {
                s[depth] = be32(v);
            } else if (pending && depth >= 1 && str_eq(prop, "compatible")) {
                n.match = stringlist_has(v, len, compat);
            } else if (pending && depth >= 1 && str_eq(prop, "status")) {
                n.disabled = !(len >= 1 && v[len - 1] == 0 &&
                               (str_eq((const char *)v, "okay") || str_eq((const char *)v, "ok")));
            } else if (pending && depth >= 1 && str_eq(prop, "reg")) {
                if (ra[depth] >= 1 && ra[depth] <= 2 && rs[depth] <= 2 && len >= 4 * (ra[depth] + rs[depth])) {
                    n.has_reg = true;
                    n.base = be_cells(v, ra[depth]);
                    n.size = rs[depth] ? be_cells(v + 4 * ra[depth], rs[depth]) : 0;
                }
            }
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

/* ---- /chosen/framebuffer@*: the simple-framebuffer node the loader (m1n1) fills ----------------------------
 * Own structure walk (not fdt_walk_props) because the parent of the node must be known even when /chosen carries no
 * property of its own. Accepted: a node at depth 2 named "framebuffer" or "framebuffer@..." under "chosen", with a
 * "compatible" list containing "simple-framebuffer", no status "disabled" (the template m1n1 fills has it
 * disabled until it has written reg, then deletes it), and all of reg, width, height, stride, format. "reg" is
 * decoded with the #address-cells/#size-cells of /chosen if it has them, else of the root, else 2 and 1 (the DT
 * defaults). The first usable node wins; later ones are ignored. Only these properties are read: nothing else of the
 * blob (serial number, MAC addresses...) is looked at, let alone kept. */
struct fbnode {
    bool compat, disabled, sized, has_reg, has_w, has_h, has_s, has_f;
    uint64_t base, size;
    uint32_t w, h, s;
    const char *fmt;
    uint32_t flen;
};

static void fb_prop(struct fbnode *n, unsigned ac, unsigned sc, const char *prop, const uint8_t *val, uint32_t len)
{
    if (str_eq(prop, "compatible")) {
        n->compat = stringlist_has(val, len, "simple-framebuffer");
    } else if (str_eq(prop, "status")) {
        /* absent or "okay"/"ok" = usable; anything else (disabled, reserved, fail, garbage) = not */
        n->disabled = !(len >= 1 && val[len - 1] == 0 &&
                        (str_eq((const char *)val, "okay") || str_eq((const char *)val, "ok")));
    } else if (str_eq(prop, "reg")) {
        if (ac >= 1 && ac <= 2 && sc <= 2 && len >= 4 * (ac + sc)) {
            n->base = be_cells(val, ac);
            n->size = sc ? be_cells(val + 4 * ac, sc) : 0;
            n->sized = sc > 0;
            n->has_reg = true;
        }
    } else if (str_eq(prop, "width") && len == 4) {
        n->w = be32(val);
        n->has_w = true;
    } else if (str_eq(prop, "height") && len == 4) {
        n->h = be32(val);
        n->has_h = true;
    } else if (str_eq(prop, "stride") && len == 4) {
        n->s = be32(val);
        n->has_s = true;
    } else if (str_eq(prop, "format") && len >= 1 && val[len - 1] == 0) {
        n->fmt = (const char *)val;
        n->flen = len;
        n->has_f = true;
    }
}

int fdt_get_framebuffer(void *fdt, struct wel2_fb *fb)
{
    uint8_t *h = fdt;
    struct fbnode n = { 0 };
    const char *stack[MAX_DEPTH];
    uint32_t a[MAX_DEPTH], s[MAX_DEPTH];
    int depth = -1;
    bool in_fb = false;

    *fb = (struct wel2_fb){ 0 };
    fb->cp = -1;
    if (fdt_check(fdt))
        return -1;
    uint32_t off = be32(h + 8), end = off + be32(h + 36);
    const char *strs = (const char *)h + be32(h + 12);
    uint32_t str_size = be32(h + 32);

    while (off + 4 <= end) {
        uint32_t tok = be32(h + off);

        off += 4;
        if (tok == FDT_BEGIN_NODE) {
            const char *name = (const char *)h + off;
            uint32_t len = 0;

            while (off + len < end && name[len])
                len++;
            if (off + len >= end || depth + 1 >= MAX_DEPTH)
                return -1;
            stack[++depth] = name;
            a[depth] = 0xffffffffu;             /* "not defined at this node" */
            s[depth] = 0xffffffffu;
            off += align4(len + 1);
            in_fb = depth == 2 && name_is(name, "framebuffer") && name_is(stack[1], "chosen");
            if (in_fb)
                n = (struct fbnode){ 0 };
        } else if (tok == FDT_END_NODE) {
            if (depth < 0)
                return -1;
            if (in_fb && depth == 2 && n.compat && !n.disabled && n.has_reg && n.has_w && n.has_h && n.has_s &&
                n.has_f &&
                (!n.sized || n.size >= (uint64_t)n.h * n.s) &&      /* reg must cover the whole screen */
                wel2_fb_setup(fb, n.base, n.w, n.h, n.s, n.fmt, n.flen) == 0)
                return 0;                       /* the first usable node wins */
            in_fb = false;
            depth--;
        } else if (tok == FDT_PROP) {
            if (off + 8 > end || depth < 0)
                return -1;
            uint32_t len = be32(h + off), nameoff = be32(h + off + 4);

            off += 8;
            if (len > end - off || nameoff >= str_size)
                return -1;
            /* the property name, copied with a bound by the strings block (a name not terminated inside it is
             * cut at 23 characters, which matches none of the names looked for) */
            char prop[24];
            uint32_t pn = 0;

            while (pn < sizeof(prop) - 1 && nameoff + pn < str_size && strs[nameoff + pn]) {
                prop[pn] = strs[nameoff + pn];
                pn++;
            }
            prop[pn] = 0;

            if (len == 4 && str_eq(prop, "#address-cells"))
                a[depth] = be32(h + off);
            else if (len == 4 && str_eq(prop, "#size-cells"))
                s[depth] = be32(h + off);
            else if (in_fb && depth == 2) {
                /* /chosen's own cells, else the root's, else the DT defaults */
                unsigned ac = a[1] != 0xffffffffu ? a[1] : (a[0] != 0xffffffffu ? a[0] : 2);
                unsigned sc = s[1] != 0xffffffffu ? s[1] : (s[0] != 0xffffffffu ? s[0] : 1);

                fb_prop(&n, ac, sc, prop, h + off, len);
            }
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
