// SPDX-License-Identifier: MIT
// Watari EL2 #53: EL2 stage-1 identity map.
//
// Until now EL2 ran with SCTLR_EL2.M = 0: every data access was Device memory, which forced
// -mstrict-align (F-18) and put the SMP spin locks on exclusives to Device memory (F-20).
// Here the boot core builds the page tables with the MMU still off (this file is built with
// -mstrict-align and only does aligned 64-bit stores), then every core (boot and secondary)
// runs s1_enable (start.S) before any other C code.
//
// Written from the Arm ARM (DDI 0487): TCR_EL2 (D24.2.194), MAIR_EL2 (D24.2.127), SCTLR_EL2
// (D24.2.175), TTBR0_EL2 (D24.2.209), VMSAv8-64 stage 1 descriptors (D8). No third-party code.
//
// E2H: Watari EL2 runs with HCR_EL2.E2H = 0 today (main.c sets g_hcr without E2H), so the EL2
// translation regime has ONE VA range (TTBR0_EL2) and TCR_EL2 uses the "E2H0 variant" layout
// (PS[18:16], RES1 bits 31 and 23). The code reads E2H at build time and also knows the E2H = 1
// layout (EL2&0 regime: TCR_EL2 like TCR_EL1 with IPS[34:32] and EPD1, AP[1] = 0, UXN+PXN) because
// the real M1 is stuck at E2H = 1 (ARCHITECTURE 1.3). The E2H = 1 branch is NOT exercised
// (QEMU boots with E2H = 0 and nothing sets it) [hypothesis].
//
// Granule: 4 KB, VA = PA = 39 bits (T0SZ = 25, start level 1, 512 x 1 GB entries). Apple cores
// implement 16 KB only: the hardware port redoes these tables, the map logic stays.
//
//   0x00000000-0x3fffffff  Device-nGnRnE, XN   (MMIO: UART, GIC, RTC...), 2 MB blocks
//   0x40000000-0x401fffff  4 KB pages: [RAM base, __image_start) RW XN   (FDT hand-off buffer)
//                                      [__image_start, __text_end) RO X  (code + vectors)
//                                      [__text_end, __rodata_end)  RO XN
//                                      rest (data, bss, stacks)    RW XN
//   0x40200000-0x7fffffff  Normal WB inner-shareable RW XN, 2 MB blocks, where RAM exists
//   further 1 GB entries   Normal WB inner-shareable RW XN: one 1 GB block where the GB is all RAM,
//                          a level-2 table of 2 MB blocks for the last, partly filled GB
//   everything else        unmapped (an EL2 access faults; the vectors dump it)
// "Where RAM exists" (#60): the range comes from the FDT /memory node (wel2_ram.h), read here with the
// MMU still off. Without a usable node the old fixed map applies (0x40000000-0x13fffffff).
// The layer image must lie in the first 2 MB of RAM (linker ASSERT).

#include "boot.h"
#include "fdt.h"
#include "wel2_ram.h"

extern uint8_t __image_start[], __text_end[], __rodata_end[];

/* The RAM the maps cover; written once by the boot core with the MMU off, read by everyone after. */
uint64_t g_ram_base = GUEST_RAM_BASE;
uint64_t g_ram_end = GUEST_RAM_BASE + 0x100000000ULL;
bool g_ram_from_fdt;
#define S1_MAX_PA (512ULL << 30)     /* 39-bit VA = PA */

/* Read by s1_enable in start.S: offsets 0, 8, 16. */
struct s1_regs {
    uint64_t mair, tcr, ttbr;
};
struct s1_regs g_s1;

#define D_VALID    (1ULL << 0)
#define D_TABLE    (1ULL << 1)       /* table (L1, L2) or page (L3); a block has it clear */
#define D_ATTR(i)  ((uint64_t)(i) << 2)
#define D_AP_RO    (1ULL << 7)       /* AP[2] */
#define D_SH_ISH   (3ULL << 8)
#define D_AF       (1ULL << 10)

#define ATTR_DEV    0                /* MAIR_EL2 Attr0 = 0x00: Device-nGnRnE */
#define ATTR_NORMAL 1                /* MAIR_EL2 Attr1 = 0xff: Normal, Inner+Outer WB, RW allocate */
#define MAIR_VALUE  0xff00ULL

static uint64_t s1_l1[512] __attribute__((aligned(4096)));
static uint64_t s1_l2dev[512] __attribute__((aligned(4096)));
static uint64_t s1_l2ram[512] __attribute__((aligned(4096)));
static uint64_t s1_l3ram[512] __attribute__((aligned(4096)));
static uint64_t s1_l2tail[512] __attribute__((aligned(4096)));   /* the last GB when only part of it is RAM */

/* Boot core, MMU off. */
void stage1_build(uint64_t fdt)
{
    uint64_t hcr, mmfr0;
    struct wel2_ram ram;

    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    bool e2h = (hcr >> 34) & 1;
    /* AP[1] (bit 6): RES1 in the EL2 regime, EL0 access in EL2&0 (leave it clear there).
     * XN: bit 54 in the EL2 regime; UXN (54) + PXN (53) in EL2&0. */
    uint64_t ap1 = e2h ? 0 : (1ULL << 6);
    uint64_t xn = e2h ? ((1ULL << 53) | (1ULL << 54)) : (1ULL << 54);
    uint64_t nrm = D_VALID | D_AF | D_SH_ISH | D_ATTR(ATTR_NORMAL) | ap1;   /* block form */
    uint64_t dev = D_VALID | D_AF | D_ATTR(ATTR_DEV) | ap1 | xn;

    uint64_t text_lo = (uint64_t)(uintptr_t)__image_start;
    uint64_t text_hi = (uint64_t)(uintptr_t)__text_end;
    uint64_t ro_hi = (uint64_t)(uintptr_t)__rodata_end;

    /* RAM range: the FDT /memory node (must start at the layer's RAM base), else the old fixed map. */
    ram.base = g_ram_base;
    ram.end = g_ram_end;
    if (fdt && !(fdt & 7) && !fdt_check((void *)fdt) &&
        !fdt_get_memory((void *)fdt, &ram, GUEST_RAM_BASE, S1_MAX_PA) &&
        ram.base == GUEST_RAM_BASE && ram.end - ram.base >= WEL2_CARVE_SIZE) {
        g_ram_base = ram.base;
        g_ram_end = ram.end;
        g_ram_from_fdt = true;
    } else {
        ram.base = g_ram_base;
        ram.end = g_ram_end;
    }

    for (unsigned i = 0; i < 512; i++) {
        uint64_t pa = GUEST_RAM_BASE + ((uint64_t)i << 12);
        uint64_t d;

        if (pa >= text_lo && pa < text_hi)
            d = nrm | D_AP_RO;                    /* code: read-only, executable */
        else if (pa >= text_hi && pa < ro_hi)
            d = nrm | D_AP_RO | xn;               /* rodata */
        else
            d = nrm | xn;                         /* FDT buffer, data, bss, stacks */
        s1_l3ram[i] = pa | d | D_TABLE;
    }
    for (unsigned i = 0; i < 512; i++) {
        s1_l2dev[i] = ((uint64_t)i << 21) | dev;
        uint64_t pa = GUEST_RAM_BASE + ((uint64_t)i << 21);

        s1_l2ram[i] = wel2_ram_has_block(&ram, pa) ? (pa | nrm | xn) : 0;
    }
    s1_l2ram[0] = (uint64_t)(uintptr_t)s1_l3ram | D_VALID | D_TABLE;
    for (unsigned i = 0; i < 512; i++)
        s1_l1[i] = 0;
    s1_l1[0] = (uint64_t)(uintptr_t)s1_l2dev | D_VALID | D_TABLE;
    s1_l1[1] = (uint64_t)(uintptr_t)s1_l2ram | D_VALID | D_TABLE;
    /* The GBs after the first: all RAM = one 1 GB block, partly RAM = 2 MB blocks, else unmapped. */
    for (unsigned gb = (unsigned)(GUEST_RAM_BASE >> 30) + 1; gb < 512; gb++) {
        enum wel2_ram_cover cv = wel2_ram_cover(&ram, gb);

        if (cv == WEL2_RAM_FULL) {
            s1_l1[gb] = ((uint64_t)gb << 30) | nrm | xn;
        } else if (cv == WEL2_RAM_PARTIAL) {          /* only the last GB can be (the base is 1 GB aligned) */
            for (unsigned i = 0; i < 512; i++) {
                uint64_t pa = ((uint64_t)gb << 30) + ((uint64_t)i << 21);

                s1_l2tail[i] = wel2_ram_has_block(&ram, pa) ? (pa | nrm | xn) : 0;
            }
            s1_l1[gb] = (uint64_t)(uintptr_t)s1_l2tail | D_VALID | D_TABLE;
        }
    }

    /* TCR_EL2: T0SZ 25 (39-bit), IRGN0 = ORGN0 = 1 (WB WA), SH0 = 3 (ISH), TG0 = 0 (4 KB),
     * physical size = min(PARange, 40 bit). */
    uint64_t parange = mmfr0 & 0xf, ps = parange < 2 ? parange : 2;
    uint64_t tcr = 25ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12);

    if (!e2h)
        tcr |= (ps << 16) | (1ULL << 23) | (1ULL << 31);              /* PS, RES1, RES1 */
    else
        tcr |= (25ULL << 16) | (1ULL << 23) | (ps << 32);             /* T1SZ, EPD1, IPS */
    g_s1.mair = MAIR_VALUE;
    g_s1.tcr = tcr;
    g_s1.ttbr = (uint64_t)(uintptr_t)s1_l1;
    __asm__ volatile("dsb sy" ::: "memory");
}
