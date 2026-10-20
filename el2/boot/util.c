// SPDX-License-Identifier: MIT
// Watari EL2: output, locks and libc stubs shared by EL2 and by the EL1 test payload.
// Built with -mstrict-align on purpose: the payload runs at EL1 with the MMU off, where every
// data access is Device memory and an unaligned one faults (F-18). EL2 itself has a stage-1
// map with Normal memory since #53 (stage1.c) and is built without -mstrict-align.

#include <stdint.h>
#include "boot.h"

#define UART0_BASE   0x09000000UL
#define UART_DR      0x00
#define UART_FR      0x18
#define UART_FR_TXFF (1u << 5)

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

/* ---- UART (PL011; QEMU needs no init) ---------------------------------------
 * puts_() holds a lock for the whole string so that lines of different cores do
 * not interleave; the EL1 payload shares these functions. */
void putc_(char c)
{
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    volatile uint32_t *dr = (volatile uint32_t *)(UART0_BASE + UART_DR);
    while (*fr & UART_FR_TXFF)
        ;
    *dr = (uint32_t)c;
}

void puts_(const char *s)
{
    bool got = spin_lock(&g_uart, 1UL << 24);

    while (*s) {
        if (*s == '\n')
            putc_('\r');
        putc_(*s++);
    }
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
