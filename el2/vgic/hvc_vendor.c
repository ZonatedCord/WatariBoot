// SPDX-License-Identifier: MIT
// Watari EL2: vendor hypercalls (WEL2_VERSION / STATS / LOG / CHECKPOINT), see include/wel2_hvc.h.
// Logic only; the memory accessors and the sinks are callbacks (include/wel2_hvc_vendor.h).

#include "wel2_hvc_vendor.h"

bool wel2_hvc_is_vendor(uint64_t x0)
{
    uint32_t fid = (uint32_t)x0;

    return fid >= WEL2_HVC_VENDOR_FIRST && fid <= WEL2_HVC_VENDOR_LAST;
}

const char *wel2_hvc_state_name(unsigned state)
{
    switch (state) {
    case WEL2_CHKSTATE_RUNNING: return "running";
    case WEL2_CHKSTATE_PASS:    return "PASS";
    case WEL2_CHKSTATE_FAIL:    return "FAIL";
    case WEL2_CHKSTATE_SKIP:    return "SKIP";
    default:                    return "?";
    }
}

/* Append a string / a decimal number to buf at *n (capacity cap, always leaves room for no NUL: lines are counted). */
static void put_s(char *buf, unsigned cap, unsigned *n, const char *s)
{
    while (*s && *n < cap)
        buf[(*n)++] = *s++;
}

static void put_dec(char *buf, unsigned cap, unsigned *n, uint64_t v)
{
    char tmp[20];
    unsigned k = 0;

    do {
        tmp[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && k < sizeof(tmp));
    while (k && *n < cap)
        buf[(*n)++] = tmp[--k];
}

static int64_t do_log(const struct wel2_hvc_ops *ops, uint64_t pa, uint64_t len)
{
    char line[WEL2_LOG_MAX];
    unsigned n = 0;

    if (!ops->read_guest || !ops->log_line || !ops->range_ok)
        return WEL2_HVC_NOT_SUPPORTED;
    if (len == 0 || len > WEL2_LOG_MAX)
        return WEL2_HVC_INVALID_PARAMETERS;
    /* The whole claimed range must be guest RAM, even when the text ends earlier (a NUL cannot be assumed). */
    if (pa + len < pa || !ops->range_ok(ops->ctx, pa, len))
        return WEL2_HVC_INVALID_PARAMETERS;
    ops->read_guest(ops->ctx, pa, line, len);
    while (n < len && line[n] != '\0')
        n++;
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))      /* the sink adds its own newline */
        n--;
    for (unsigned i = 0; i < n; i++)
        if ((unsigned char)line[i] < 0x20 || (unsigned char)line[i] > 0x7e)
            line[i] = '?';
    ops->log_line(ops->ctx, line, n);
    return WEL2_HVC_SUCCESS;
}

static int64_t do_checkpoint(const struct wel2_hvc_ops *ops, uint64_t n, uint64_t state)
{
    char line[WEL2_LOG_MAX];
    unsigned k = 0;

    if (!ops->log_line)
        return WEL2_HVC_NOT_SUPPORTED;
    if (n == 0 || n > WEL2_CHK_MAX || state > WEL2_CHKSTATE_SKIP)
        return WEL2_HVC_INVALID_PARAMETERS;
    put_s(line, sizeof(line), &k, "guest checkpoint ");
    put_dec(line, sizeof(line), &k, n);
    put_s(line, sizeof(line), &k, " ");
    put_s(line, sizeof(line), &k, wel2_hvc_state_name((unsigned)state));
    ops->log_line(ops->ctx, line, k);
    if (ops->checkpoint)
        ops->checkpoint(ops->ctx, (unsigned)n, (unsigned)state);
    return WEL2_HVC_SUCCESS;
}

static int64_t do_stats(const struct wel2_hvc_ops *ops, uint64_t pa, uint64_t len)
{
    struct wel2_stats st;

    if (!ops->stats_fill || !ops->write_guest || !ops->range_ok)
        return WEL2_HVC_NOT_SUPPORTED;
    if (len < sizeof(st) || (pa & 7) || pa + sizeof(st) < pa || !ops->range_ok(ops->ctx, pa, sizeof(st)))
        return WEL2_HVC_INVALID_PARAMETERS;
    for (unsigned i = 0; i < sizeof(st); i++)
        ((volatile char *)&st)[i] = 0;
    st.version = WEL2_HVC_ABI_VERSION;
    ops->stats_fill(ops->ctx, &st);
    ops->write_guest(ops->ctx, pa, &st, sizeof(st));
    return WEL2_HVC_SUCCESS;
}

bool wel2_hvc_vendor(const struct wel2_hvc_ops *ops, uint64_t x[4])
{
    int64_t ret;

    if (!wel2_hvc_is_vendor(x[0]))
        return false;
    switch ((uint32_t)x[0]) {
    case WEL2_VERSION:
        ret = (int64_t)((WEL2_VERSION_TAG << 32) | WEL2_HVC_ABI_VERSION);
        break;
    case WEL2_STATS:
        ret = do_stats(ops, x[1], x[2]);
        break;
    case WEL2_LOG:
        ret = do_log(ops, x[1], x[2]);
        break;
    case WEL2_CHECKPOINT:
        ret = do_checkpoint(ops, x[1], x[2]);
        break;
    default:
        ret = WEL2_HVC_NOT_SUPPORTED;
        break;
    }
    x[0] = (uint64_t)ret;
    x[1] = x[2] = x[3] = 0;
    return true;
}
