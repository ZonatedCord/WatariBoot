// SPDX-License-Identifier: MIT
// Watari EL2 #53 / #58: EL2 stage-1 identity map, granule- and E2H-parametric.
//
// Until #53 EL2 ran with SCTLR_EL2.M = 0: every data access was Device memory, which forced
// -mstrict-align (F-18) and put the SMP spin locks on exclusives to Device memory (F-20).
// Here the boot core builds the page tables with the MMU still off (this file and the builder it calls are
// built with -mstrict-align and only do aligned 64-bit stores), then every core (boot and secondary)
// runs s1_enable (start.S) before any other C code.
//
// Written from the Arm ARM (DDI 0487): TCR_EL2 (D24.2.194), MAIR_EL2 (D24.2.127), SCTLR_EL2
// (D24.2.175), TTBR0_EL2 (D24.2.209), VMSAv8-64 stage 1 descriptors (D8). No third-party code.
//
// E2H (HCR_EL2.E2H, Arm ARM D8.13 Virtualization Host Extensions). The real M1 has E2H stuck at 1, so 1 is the
// configuration the layer is designed around; 0 stays selectable for comparison on QEMU. The boot argument
// wel2.e2h=0|1 (default WEL2_DEF_E2H) is applied here, before the EL2 MMU is on, and every core repeats it
// in s1_enable (HCR_EL2 is per core and QEMU resets it to 0): the value that reads back after the write is the
// one the tables are built for (a stuck bit reads 1 whatever was asked). With E2H = 1 the EL2&0 regime has two
// VA ranges (TTBR0_EL2 and TTBR1_EL2) and TCR_EL2 takes the TCR_EL1 layout (T1SZ, EPD1, TG1, IPS); the
// descriptors lose AP[1] (RES1 in the EL2 regime, EL0 access in EL2&0) and use UXN + PXN instead of XN.
// Only TTBR0_EL2 is used: EPD1 = 1.
//
// Granule (#58): the boot argument wel2.gran=4k|16k (default WEL2_DEF_GRAN) picks the translation granule
// of this map and of the stage-2 map (main.c). The M1 implements 16 KB only (ID_AA64MMFR0_EL1.TGran4 = 0b1111),
// so 16 KB is the one the code is designed around; a request for a granule the CPU lacks falls back to the
// other one (ID_AA64MMFR0_EL1.TGran4/TGran16 for stage 1, TGran4_2/TGran16_2 for stage 2). VA = PA = 39 bits
// with 4 KB (T0SZ 25, start level 1) and 36 bits with 16 KB (T0SZ 28, start level 2: one 16 KB table of 2048
// 32 MB entries), see wel2_s1_va_bits.
//
//   0x00000000-0x3fffffff  Device-nGnRnE, XN   (MMIO: UART, GIC, RTC...)
//   0x40000000-0x401fffff  pages: [RAM base, __image_start) RW XN   (FDT hand-off buffer)
//                                 [__image_start, __text_end) RO X  (code + vectors)
//                                 [__text_end, __rodata_end)  RO XN
//                                 rest (data, bss, stacks)    RW XN
//   RAM                    Normal WB inner-shareable RW XN, in the largest blocks the granule allows
//   everything else        unmapped (an EL2 access faults; the vectors dump it)
// "Where RAM exists" (#60): the range comes from the FDT /memory node (wel2_ram.h), read here with the
// MMU still off. Without a usable node the old fixed map applies (0x40000000-0x13fffffff).
// The layer image (a PIE, T6a) must lie in the carve-out block that contains its run-time start (QEMU: the first 2 MB
// of RAM; linker ASSERT on the size); its text/rodata boundaries are 16 KB aligned (linker.ld) and so is the load
// address (start.S), so the page permissions are the same for both granules.

#include "boot.h"
#include "plat.h"
#include "fdt.h"
#include "wel2_ram.h"
#include "wel2_maps.h"
#include "wel2_uart.h"

extern uint8_t _start[], __image_start[], __text_end[], __rodata_end[], __layer_end[];

/* The RAM the maps cover; written once by the boot core with the MMU off, read by everyone after. */
uint64_t g_ram_base, g_ram_end;      /* default (no usable FDT /memory): the carve-out block up to 4 GB above it */
uint64_t g_layer_base;               /* T6a: run-time address of _start */
uint64_t g_carve_base, g_carve_size; /* T6a: the block the layer keeps for itself, see boot.h */
bool g_ram_from_fdt;

/* T6c: what the platform switch read (valid for plat_m1) and the guest RAM as ranges (M1). Written once by the boot
 * core with the MMU off, cleaned to memory below, read by everyone after. */
struct m1_platform g_m1;
struct wel2_range g_guest_ram[WEL2_MAX_GUEST_RANGES];
unsigned g_nguest_ram;

bool guest_ram_contains(uint64_t pa)
{
    if (!g_plat->m1_memory)
        return pa >= g_ram_base && pa < g_ram_end;
    for (unsigned i = 0; i < g_nguest_ram; i++)
        if (pa >= g_guest_ram[i].base && pa - g_guest_ram[i].base < g_guest_ram[i].size)
            return true;
    return false;
}

/* The S5L console UART window found in the FDT (0 = none: PL011). Read by stage2_init to keep it from the guest. */
uint64_t g_con_s5l_base, g_con_s5l_size;

/* What stage1_build decided (MMU off, boot core), read by everyone after. */
unsigned g_gran = WEL2_DEF_GRAN;     /* granule shift of the stage-1 and stage-2 maps: 12 or 14 */
bool g_e2h;                          /* HCR_EL2.E2H the layer runs with (the value that read back) */
bool g_e2h_stuck;                    /* asked for 0, the bit reads 1 (the M1) */
bool g_gran_fallback;                /* the requested granule is not implemented: the other one is used */
unsigned g_step;                     /* T6g: boot argument wel2.step=N (0 = not given): the hardware ladder rung, DESIGN 9 */

/* Read by s1_enable in start.S: offsets 0, 8, 16, 24. */
struct s1_regs {
    uint64_t mair, tcr, ttbr, e2h;
};
struct s1_regs g_s1;

#define MAIR_VALUE  WEL2_MAIR_EL2_VALUE   /* Attr0 Device-nGnRnE, Attr1 Normal WB RW allocate, Attr2 Normal NC (framebuffer) */
#define HCR_E2H_BIT (1ULL << 34)

static uint8_t s1_pool[WEL2_S1_POOL_BYTES] __attribute__((aligned(16384)));
static struct wel2_pt s1_pt;

/* Message with the MMU off: putc_ only. puts_ takes a spin lock (exclusives), which need Normal memory. */
static void s1_msg(const char *s)
{
    while (*s) {
        if (*s == '\n')
            putc_('\r');
        putc_(*s++);
    }
}

/* Clean and invalidate [p, p+n) to the point of coherency by VA (DC CIVAC, Arm ARM D7.5): what this core wrote
 * with the MMU off must reach memory, where the secondaries (MMU and caches off) read it, and no stale line of it
 * may remain here once the MMU makes the memory cacheable. DSB SY completes it. [hypothesis for the M1: QEMU TCG
 * cannot show the need] */
static void clean_inval(const void *p, unsigned long n)
{
    uint64_t ctr, line, a, end;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line = 4ULL << ((ctr >> 16) & 0xf);
    a = (uint64_t)(uintptr_t)p & ~(line - 1);
    end = (uint64_t)(uintptr_t)p + n;
    for (; a < end; a += line)
        __asm__ volatile("dc civac, %0" : : "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

/* Option "key" (e.g. "wel2.e2h=1") present as a whole word in the boot arguments? */
static bool has_arg(const char *args, const char *key)
{
    for (const char *p = args; *p; p++) {
        unsigned k = 0;

        if (p != args && p[-1] != ' ')
            continue;
        while (key[k] && p[k] == key[k])
            k++;
        if (!key[k] && (p[k] == 0 || p[k] == ' '))
            return true;
    }
    return false;
}

/* wel2.step=<decimal> as a whole word: its value, 0 when absent or not a number. */
static unsigned parse_step(const char *args)
{
    static const char key[] = "wel2.step=";

    for (const char *p = args; *p; p++) {
        unsigned k = 0, v = 0;

        if (p != args && p[-1] != ' ')
            continue;
        while (key[k] && p[k] == key[k])
            k++;
        if (key[k])
            continue;
        const char *q = p + k;

        for (; *q >= '0' && *q <= '9' && v < 1000; q++)
            v = v * 10 + (unsigned)(*q - '0');
        if (q != p + k && (*q == 0 || *q == ' '))
            return v;
    }
    return 0;
}

/* ID_AA64MMFR0_EL1 (D24.2.84): TGran16 [23:20] (0 = no), TGran4 [31:28] (0b1111 = no), and for stage 2
 * TGran16_2 [35:32] / TGran4_2 [43:40] (0 = as stage 1, 1 = no, 2/3 = yes). */
static bool gran_ok(uint64_t mmfr0, unsigned gshift)
{
    unsigned s1 = gshift == WEL2_GRAN_4K ? (unsigned)((mmfr0 >> 28) & 0xf) : (unsigned)((mmfr0 >> 20) & 0xf);
    unsigned s2 = gshift == WEL2_GRAN_4K ? (unsigned)((mmfr0 >> 40) & 0xf) : (unsigned)((mmfr0 >> 32) & 0xf);
    bool s1_yes = gshift == WEL2_GRAN_4K ? s1 != 0xf : s1 != 0;

    return s1_yes && (s2 == 0 || s2 == 2 || s2 == 3);
}

/* Boot core, MMU off. */
void stage1_build(uint64_t fdt)
{
    uint64_t hcr, mmfr0;
    struct wel2_ram ram;
    char args[160];
    unsigned want_gran = WEL2_DEF_GRAN;
    bool want_e2h = WEL2_DEF_E2H;

    /* T6a: the run-time layout. The image is a PIE (start.S relocated it): everything that used to be the fixed link
     * address 0x40080000 derives from where _start actually is. The carve-out is the g_carve_size aligned block that
     * holds the image (QEMU: the 2 MB block at the RAM base); RAM defaults to 4 GB from its start. */
    g_layer_base = (uint64_t)(uintptr_t)_start;
    /* The platform switch (T6c): an enabled "apple,aic" node in the loader's FDT = the M1; anything else (also a blob
     * the parser rejects) = the QEMU virt machine, exactly as before. The parser works on the stack (about 3 KB of
     * the 16 KB boot stack, fdt_m1.h) and fills the static g_m1; every access is a byte read or an aligned store, so
     * it runs with the MMU off. */
    int m1rc = -1;

    if (fdt && !(fdt & 7) && !fdt_check((void *)fdt))
        m1rc = fdt_m1_parse((const void *)fdt, &g_m1);
    g_plat = plat_select(m1rc, &g_m1);
    bool m1 = g_plat->m1_memory;

    if (m1)
        con_select_none();      /* T6c: the M1 never touches the PL011 (unmapped there); the S5L UART replaces this below */

    /* Boot arguments (/chosen/bootargs, QEMU -append); the FDT is read-only here and the MMU is off. */
    args[0] = 0;
    bool have_args = fdt && !(fdt & 7) && !fdt_check((void *)fdt) && fdt_get_bootargs((void *)fdt, args, sizeof(args)) >= 0;

    if (!have_args)
        args[0] = 0;
    g_step = parse_step(args);
    /* T6g, rung L1 (DESIGN 9, PLAN S13): wel2.step=1 resets the machine right here, after the relocation and the platform
     * switch and before anything else: no console, no framebuffer, no stage 1. Needs only the FDT lookups above (the
     * watchdog node on the M1). Without a watchdog node the reset cannot be made: continue, el2_main stops with a message. */
    if (g_step == 1 && (!m1 || g_m1.wdt.found)) {
        g_plat->system_reset();
        for (;;)
            __asm__ volatile("wfe");
    }

    g_carve_size = m1 ? WEL2_CARVE_SIZE_M1 : WEL2_CARVE_SIZE_QEMU;
    g_carve_base = g_layer_base & ~(g_carve_size - 1);
    g_ram_base = g_carve_base;
    g_ram_end = g_carve_base + 0x100000000ULL;

    /* Console first: a node compatible with "apple,s5l-uart" (the M1 under m1n1) switches putc_/puts_ from the
     * QEMU PL011 to it, so that every message below, and every core after, goes to the right UART. No such node
     * (QEMU): nothing changes. Written with the MMU off, cleaned to memory below with the other globals. */
    struct fdt_reg_info uart = { 0 };

    if (m1 && g_m1.uart.found) {                /* the M1: stdout-path / serial0 first (fdt_m1.c) */
        con_select_s5l(g_m1.uart.base);
        g_con_s5l_base = g_m1.uart.base;
        g_con_s5l_size = g_m1.uart.size ? g_m1.uart.size : 0x1000;
    } else if (!m1 && fdt && !(fdt & 7) && !fdt_check((void *)fdt) &&
        fdt_find_compatible_reg((void *)fdt, FDT_COMPAT_S5L_UART, &uart) == 0 && uart.found) {
        con_select_s5l(uart.base);
        g_con_s5l_base = uart.base;
        g_con_s5l_size = uart.size ? uart.size : 0x1000;
    }

    /* Framebuffer (T5b): /chosen/framebuffer@* of the loader's FDT. Found = putc_/puts_ mirror to it and checkpoint()
     * can paint (all with aligned 32-bit stores, so this works with the MMU off). None (QEMU): g_fb stays zero and
     * every call is a no-op. The stage-1 mapping (Normal non-cacheable) follows with the other maps below. */
    if (fdt && !(fdt & 7) && !fdt_check((void *)fdt))
        fdt_get_framebuffer((void *)fdt, &g_fb);
    if (g_step)                                   /* status band: "RUNG L<n> STEP <s>" (T6h); rungs L1..L5 are steps 1..5 */
        wel2_fb_set_rung(&g_fb, g_step > 5 ? 5 : g_step, g_step);
    checkpoint(WEL2_LCHK_RELOCATED);              /* checkpoint 0: relocated, framebuffer looked up, MMU still off */

    /* The whole layer block (image, .bss, stacks: _start to __layer_end) must lie inside the carve-out, whatever
     * address the loader chose; the linker only checks the size. MMU off: s1_msg only; it runs after the console and the
     * framebuffer are known, so the M1 can still say (or paint red) why it stopped. */
    if (g_layer_base + (uint64_t)(uintptr_t)__layer_end - (uint64_t)(uintptr_t)_start > g_carve_base + g_carve_size) {
        s1_msg("Watari EL2: FATAL: the layer image and stacks do not fit the carve-out block at this load address, halted\n");
        checkpoint_fail();
        for (;;)
            __asm__ volatile("wfe");
    }

    if (have_args) {
        if (has_arg(args, "wel2.e2h=1"))
            want_e2h = true;
        else if (has_arg(args, "wel2.e2h=0"))
            want_e2h = false;
        if (has_arg(args, "wel2.gran=16k"))
            want_gran = WEL2_GRAN_16K;
        else if (has_arg(args, "wel2.gran=4k"))
            want_gran = WEL2_GRAN_4K;
    }

    /* E2H first: it changes the layout of TCR_EL2 and of the descriptors. The value that reads back wins. */
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
    hcr = (hcr & ~HCR_E2H_BIT) | (want_e2h ? HCR_E2H_BIT : 0);
    __asm__ volatile("msr hcr_el2, %0; isb" : : "r"(hcr));
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
    g_e2h = (hcr & HCR_E2H_BIT) != 0;
    g_e2h_stuck = g_e2h && !want_e2h;
    if (want_e2h && !g_e2h)
        s1_msg("Watari EL2: WARNING: E2H=1 requested but HCR_EL2.E2H reads 0 (no FEAT_VHE?): running with E2H=0\n");

    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    g_gran = want_gran;
    if (!gran_ok(mmfr0, want_gran) && gran_ok(mmfr0, want_gran == WEL2_GRAN_4K ? WEL2_GRAN_16K : WEL2_GRAN_4K)) {
        g_gran = want_gran == WEL2_GRAN_4K ? WEL2_GRAN_16K : WEL2_GRAN_4K;
        g_gran_fallback = true;
    }
    unsigned va_bits = wel2_s1_va_bits(g_gran);

    /* RAM range: the FDT /memory node (must start at the layer's RAM base), else the old fixed map. */
    ram.base = g_ram_base;
    ram.end = g_ram_end;
    struct wel2_range ram_in[M1_MAX_USABLE];
    unsigned nram_in = 0;

    if (m1) {
        /* The M1 (T6c): RAM is wherever the FDT says (0x8_0000_0000 and up), the layer sits anywhere inside it, so the
         * "RAM starts at the carve-out" rule of the QEMU path does not apply. The guest gets usable[] minus the
         * layer's 32 MB block, rounded inward to 32 MB; g_ram_base / g_ram_end are the bounding box of that list. */
        for (unsigned i = 0; i < g_m1.nusable; i++) {
            ram_in[i].base = g_m1.usable[i].base;
            ram_in[i].size = g_m1.usable[i].size;
        }
        nram_in = g_m1.nusable;
        if (wel2_ranges_inward(ram_in, nram_in, WEL2_M1_S2_ALIGN, g_carve_base, g_carve_size, g_guest_ram,
                               WEL2_MAX_GUEST_RANGES, &g_nguest_ram) || !g_nguest_ram) {
            s1_msg("Watari EL2: FATAL: the FDT leaves no guest RAM (or too many pieces), halted\n");
            checkpoint_fail();
            for (;;)
                __asm__ volatile("wfe");
        }
        g_ram_base = g_guest_ram[0].base;
        g_ram_end = g_guest_ram[0].base + g_guest_ram[0].size;
        for (unsigned i = 1; i < g_nguest_ram; i++) {
            if (g_guest_ram[i].base < g_ram_base)
                g_ram_base = g_guest_ram[i].base;
            if (g_guest_ram[i].base + g_guest_ram[i].size > g_ram_end)
                g_ram_end = g_guest_ram[i].base + g_guest_ram[i].size;
        }
        g_ram_from_fdt = true;
        ram.base = g_ram_base;
        ram.end = g_ram_end;
    } else if (fdt && !(fdt & 7) && !fdt_check((void *)fdt) &&
        !fdt_get_memory((void *)fdt, &ram, g_carve_base, 1ULL << va_bits) &&
        ram.base == g_carve_base && ram.end - ram.base >= g_carve_size) {
        g_ram_base = ram.base;
        g_ram_end = ram.end;
        g_ram_from_fdt = true;
    } else {
        ram.base = g_ram_base;
        ram.end = g_ram_end;
    }

    struct wel2_s1_layout layout = {
        (uint64_t)(uintptr_t)__image_start, (uint64_t)(uintptr_t)__text_end, (uint64_t)(uintptr_t)__rodata_end,
    };

    /* Pool exhaustion or a bad layout: the host tests (test_pt.c) size the pool for the QEMU maps; a failure
     * here prints a line on the UART (no lock, MMU off) and parks the core. */
    /* The M1 stage 1 (T6c): RAM (usable[] rounded inward to 2 MB), the layer's block, the loader's FDT (still read
     * with the MMU on), the AIC / UART / watchdog windows as Device, the cores' cpu-release-addr words as Normal
     * cacheable; the framebuffer follows below. Everything else stays unmapped. */
    struct wel2_range dev[M1_MAX_AIC + 2], rel[M1_MAX_CPUS];
    unsigned ndev = 0, nrel = 0;
    struct wel2_s1_m1 m1in;
    bool built_bad;

    if (m1) {
        for (unsigned i = 0; i < g_m1.naic; i++, ndev++) {
            dev[ndev].base = g_m1.aic[i].base;
            dev[ndev].size = g_m1.aic[i].size;
        }
        if (g_m1.uart.found) {
            dev[ndev].base = g_m1.uart.base;
            dev[ndev].size = g_con_s5l_size;
            ndev++;
        }
        if (g_m1.wdt.found) {
            dev[ndev].base = g_m1.wdt.base;
            dev[ndev].size = g_m1.wdt.size;
            ndev++;
        }
        for (unsigned i = 0; i < g_m1.ncpu; i++)
            if (g_m1.cpu[i].has_release) {
                rel[nrel].base = g_m1.cpu[i].release_addr;
                rel[nrel].size = 8;
                nrel++;
            }
        m1in = (struct wel2_s1_m1){
            .ram = ram_in, .nram = nram_in,
            .carve = { g_carve_base, g_carve_size },
            .fdt = { fdt, fdt_totalsize((void *)fdt) },
            .dev = dev, .ndev = ndev, .rel = rel, .nrel = nrel,
            .layout = layout,
        };
        built_bad = wel2_pt_init(&s1_pt, s1_pool, sizeof(s1_pool), g_gran, va_bits) ||
                    wel2_s1_build_m1(&s1_pt, &m1in, g_e2h);
    } else {
        built_bad = wel2_pt_init(&s1_pt, s1_pool, sizeof(s1_pool), g_gran, va_bits) ||
                    wel2_s1_build(&s1_pt, &ram, &layout, g_e2h);
    }
    if (built_bad) {
        s1_msg("Watari EL2: FATAL: stage-1 tables: bad geometry/layout or the table pool is too small, halted\n");
        checkpoint_fail();
        for (;;)
            __asm__ volatile("wfe");
    }

    /* The S5L UART: below the RAM it is covered by the Device map of [0, RAM base) (the M1: 0x2_3520_0000 under
     * the RAM at 0x8_0000_0000); above it, it is added here. A window the map cannot take falls back to the
     * (unreachable on the M1) PL011: the first output would fault otherwise, with nothing to say why. */
    if (!m1 && g_con_s5l_base && wel2_s1_map_device(&s1_pt, &ram, g_con_s5l_base, g_con_s5l_size, g_e2h)) {
        g_con_s5l_base = 0;
        g_con = (struct wel2_uart){ WEL2_UART_PL011, g_con.io, 0, 0, 0, 0 };
        g_con.io.ctx = (void *)0x09000000UL;
        s1_msg("Watari EL2: WARNING: the S5L UART window does not fit the stage-1 map, console back on the PL011\n");
    }

    /* The framebuffer: above the RAM on the M1 (0x9e2570000 vs the RAM top 0x9df610000), mapped Normal non-cacheable
     * (MAIR Attr2). A framebuffer the map cannot take (inside the RAM, beyond the input range, pool full) is
     * switched off: drawing into unmapped or cacheable memory would fault or never reach the panel. */
    if (g_fb.fmt && (m1 ? wel2_s1_map_fb_free(&s1_pt, g_fb.base, wel2_fb_bytes(&g_fb), g_e2h)
                        : wel2_s1_map_fb(&s1_pt, &ram, g_fb.base, wel2_fb_bytes(&g_fb), g_e2h))) {
        g_fb = (struct wel2_fb){ 0 };
        g_fb.cp = -1;
        s1_msg("Watari EL2: WARNING: the framebuffer does not fit the stage-1 map, no framebuffer output\n");
    }

    /* TCR_EL2 physical size = min(PARange, 40 bit). */
    uint64_t parange = mmfr0 & 0xf, ps = parange < 2 ? parange : 2;

    g_s1.mair = MAIR_VALUE;
    g_s1.tcr = wel2_tcr_el2(&s1_pt, g_e2h, (unsigned)ps);
    g_s1.ttbr = wel2_pt_root_addr(&s1_pt);
    g_s1.e2h = g_e2h;
    /* Everything written with the MMU off: the used part of the table pool and the globals the other cores read
     * (g_s1 is read by s1_enable with the MMU off; g_ram_*, g_gran, g_e2h by everyone after the MMU is on). */
    clean_inval(s1_pool, (unsigned long)s1_pt.used << g_gran);
    clean_inval(&g_s1, sizeof(g_s1));
    clean_inval(&g_con, sizeof(g_con));
    clean_inval(&g_fb, sizeof(g_fb));
    clean_inval(&g_con_s5l_base, sizeof(g_con_s5l_base));
    clean_inval(&g_con_s5l_size, sizeof(g_con_s5l_size));
    clean_inval(&g_m1, sizeof(g_m1));
    clean_inval(&g_plat, sizeof(g_plat));
    clean_inval(g_guest_ram, sizeof(g_guest_ram));
    clean_inval(&g_nguest_ram, sizeof(g_nguest_ram));
    clean_inval(&g_layer_base, sizeof(g_layer_base));
    clean_inval(&g_carve_base, sizeof(g_carve_base));
    clean_inval(&g_carve_size, sizeof(g_carve_size));
    clean_inval(&g_ram_base, sizeof(g_ram_base));
    clean_inval(&g_ram_end, sizeof(g_ram_end));
    clean_inval(&g_ram_from_fdt, sizeof(g_ram_from_fdt));
    clean_inval(&g_gran, sizeof(g_gran));
    clean_inval(&g_e2h, sizeof(g_e2h));
    clean_inval(&g_e2h_stuck, sizeof(g_e2h_stuck));
    clean_inval(&g_gran_fallback, sizeof(g_gran_fallback));
    clean_inval(&g_step, sizeof(g_step));
    clean_inval(&s1_pt, sizeof(s1_pt));
    __asm__ volatile("dsb sy" ::: "memory");
}
