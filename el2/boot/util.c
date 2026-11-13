// SPDX-License-Identifier: MIT
// Watari EL2: output, locks and libc stubs shared by EL2 and by the EL1 test payload.
// Built with -mstrict-align on purpose: the payload runs at EL1 with the MMU off, where every
// data access is Device memory and an unaligned one faults (F-18). EL2 itself has a stage-1
// map with Normal memory since #53 (stage1.c) and is built without -mstrict-align.

#include <stdint.h>
#include "boot.h"
#include "wel2_uart.h"
#include "wel2_hvc.h"

#define UART0_BASE   0x09000000UL      /* QEMU virt PL011 */

/* ---- Locks ----------------------------------------------------------------
 * At EL2 these are exclusives (ldaxr/stlxr) on Normal Inner-Shareable memory: the
 * stage-1 map of stage1.c is on before any of this runs (#53, F-20). The EL1 test
 * payload shares the functions with its MMU off (Device memory, cache-incoherent
 * with EL2's view of the same word): only QEMU TCG accepts that, and only the
 * default test image has a payload; the chain image (M1-4) has none. Spins are
 * bounded and report, never hang silently (F-14). */
static volatile uint32_t g_big, g_uart;

static bool spin_lock(volatile uint32_t *l, unsigned long limit)
{
    for (unsigned long n = 0; n < limit; n++) {
        if (!__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE))
            return true;
        __asm__ volatile("yield");
    }
    return false;
}

static void spin_unlock(volatile uint32_t *l)
{
    __atomic_store_n(l, 0, __ATOMIC_RELEASE);
}

void big_lock(void)
{
    if (!spin_lock(&g_big, 1UL << 28)) {
        puts_("Watari EL2: big lock timeout, stealing it\n");
        g_big = 1;
    }
}

void big_unlock(void)
{
    spin_unlock(&g_big);
}

/* ---- Console UART -----------------------------------------------------------
 * One global console (g_con, wel2_uart.h): the PL011 of the QEMU virt machine until con_select_s5l() switches it to
 * the Apple S5L UART whose "apple,s5l-uart" node the FDT has (stage1_build, before any other output). QEMU needs no
 * init; the S5L UART is left as iBoot/m1n1 set it up. Initialised data (not a run-time constructor), so the EL1 test
 * payload that shares putc_/puts_ with its MMU off sees the PL011 setup from the image itself.
 * puts_() holds a lock for the whole string so that lines of different cores do not interleave. */
static uint32_t mmio_rd(void *ctx, unsigned off)
{
    return *(volatile uint32_t *)((uintptr_t)ctx + off);
}

static void mmio_wr(void *ctx, unsigned off, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)ctx + off) = val;
}

struct wel2_uart g_con = {
    WEL2_UART_PL011, { mmio_rd, mmio_wr, (void *)UART0_BASE }, 0, 0, 0, 0,
};

/* T6c: the null console, for the M1 before (or without) a usable S5L UART: no register is ever touched. */
void con_select_none(void)
{
    g_con = (struct wel2_uart){ WEL2_UART_NONE, { mmio_rd, mmio_wr, 0 }, 0, 0, 0, 0 };
}

void con_select_s5l(uint64_t base)
{
    g_con = (struct wel2_uart){
        WEL2_UART_S5L, { mmio_rd, mmio_wr, (void *)(uintptr_t)base }, WEL2_UART_S5L_LIMIT, 0, 0, 0,
    };
}

/* ---- Framebuffer mirror (T5b) -------------------------------------------------------------------------------
 * g_fb stays all-zero (format NONE) unless stage1_build found /chosen/framebuffer@* in the FDT; then every byte that
 * goes to the UART is also drawn as text, and checkpoint() paints the squares of the boot stages. Aligned 32-bit
 * stores only (fb.c), so all of it works with the MMU off too. */
struct wel2_fb g_fb;
/* T17: while set, text goes to the UART only (the panel keeps what it shows). Set around the final statistics when the
 * guest's SUMMARY block is on the panel (psci.c); plain flag, no allocation, MMU-safe. */
bool g_fb_mute;
bool g_summary_on_panel;

void checkpoint(unsigned n)
{
    wel2_fb_checkpoint(&g_fb, n, WEL2_RGB_GREEN);
}

/* state: 0 running (amber), 1 pass (green), 3 skipped (grey), anything else fail (red) */
void checkpoint_state(unsigned n, unsigned state)
{
    wel2_fb_checkpoint(&g_fb, n, state == 0 ? WEL2_RGB_AMBER : state == 1 ? WEL2_RGB_GREEN :
                                 state == 3 ? WEL2_RGB_GREY : WEL2_RGB_RED);
}

void checkpoint_fail(void)
{
    wel2_fb_checkpoint_fail(&g_fb);
}

void putc_(char c)
{
    if (!g_fb_mute)
        wel2_fb_putc(&g_fb, c);              /* T6c: the panel first, a stuck UART must not delay or lose it */
    wel2_uart_putc(&g_con, (uint8_t)c);
}

void puts_(const char *s)
{
    bool got = spin_lock(&g_uart, 1UL << 24);

    if (!g_fb_mute)
        wel2_fb_puts(&g_fb, s);
    wel2_uart_puts(&g_con, s);
    if (got)
        spin_unlock(&g_uart);
}

/* WEL2_CONSOLE_WRITE (T12b/E7): a guest's raw byte stream. The UART gets every byte unchanged (no CR insertion, no
 * filtering: the guest's own CR/LF and escape sequences reach the terminal as sent); the framebuffer text gets the
 * printable ASCII bytes and LF only (a ticker, not a terminal). One lock for the whole call so that it does not
 * interleave with the layer's own lines. */
void console_bytes(const uint8_t *b, unsigned n)
{
    char text[WEL2_CONSOLE_MAX + 1];
    unsigned k = 0;
    bool got = spin_lock(&g_uart, 1UL << 24);

    for (unsigned i = 0; i < n; i++) {
        if (((b[i] >= 0x20 && b[i] <= 0x7e) || b[i] == '\n') && k < WEL2_CONSOLE_MAX)
            text[k++] = (char)b[i];
        wel2_uart_putc(&g_con, b[i]);
    }
    text[k] = 0;
    wel2_fb_puts(&g_fb, text);
    if (got)
        spin_unlock(&g_uart);
}

static char *fmt_hex(char *p, uint64_t v)
{
    *p++ = '0';
    *p++ = 'x';
    for (int i = 60; i >= 0; i -= 4)
        *p++ = "0123456789abcdef"[(v >> i) & 0xf];
    return p;
}

static char *fmt_str(char *p, const char *s, char *end)
{
    while (*s && p < end)
        *p++ = *s++;
    return p;
}

void puthex(uint64_t v)
{
    char b[19];

    *fmt_hex(b, v) = 0;
    puts_(b);
}

/* "<k>0x<v>\n" as one atomic write. */
void put_kv(const char *k, uint64_t v)
{
    char b[160], *p = fmt_str(b, k, b + 120);

    p = fmt_hex(p, v);
    *p++ = '\n';
    *p = 0;
    puts_(b);
}

/* "<pre><decimal n><post>" as one atomic write (post carries the newline). */
void put_kn(const char *pre, unsigned n, const char *post)
{
    char b[160], t[12], *p = fmt_str(b, pre, b + 100);
    unsigned k = 0;

    do {
        t[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n && k < sizeof(t));
    while (k)
        *p++ = t[--k];
    p = fmt_str(p, post, b + 150);
    *p = 0;
    puts_(b);
}

/* libc stubs: the compiler may lower loops to these. */
void *memset(void *d, int c, unsigned long n)
{
    uint8_t *p = d;
    while (n--)
        *p++ = (uint8_t)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}


/* Clean (write back to the point of coherency) the data cache lines of [p, p+n): memory that
 * EL2 filled through its cacheable map and a guest with its MMU off must read (FDT hand-off). */
void dcache_clean_range(const void *p, unsigned long n)
{
    uint64_t ctr;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uintptr_t line = 4UL << ((ctr >> 16) & 0xf);           /* CTR_EL0.DminLine: log2 of words */
    uintptr_t a = (uintptr_t)p & ~(line - 1), end = (uintptr_t)p + n;

    for (; a < end; a += line)
        __asm__ volatile("dc cvac, %0" : : "r"(a) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
}
