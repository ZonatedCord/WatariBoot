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
//   0x40200000-0x7fffffff  Normal WB inner-shareable RW XN, 2 MB blocks
//   0x80000000-0xbfffffff  Normal WB inner-shareable RW XN, one 1 GB block (RAM of -m 2G)
//   everything else        unmapped (an EL2 access faults; the vectors dump it)
// The layer image must lie in the first 2 MB of RAM (linker ASSERT).

#include "boot.h"

extern uint8_t __image_start[], __text_end[], __rodata_end[];

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

/* Boot core, MMU off. */
void stage1_build(void)
{
    uint64_t hcr, mmfr0;

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
        s1_l2ram[i] = (GUEST_RAM_BASE + ((uint64_t)i << 21)) | nrm | xn;
    }
    s1_l2ram[0] = (uint64_t)(uintptr_t)s1_l3ram | D_VALID | D_TABLE;
    for (unsigned i = 0; i < 512; i++)
        s1_l1[i] = 0;
    s1_l1[0] = (uint64_t)(uintptr_t)s1_l2dev | D_VALID | D_TABLE;
    s1_l1[1] = (uint64_t)(uintptr_t)s1_l2ram | D_VALID | D_TABLE;
    s1_l1[2] = 0x80000000ULL | nrm | xn;          /* 1 GB block: RAM of a -m 2G machine */

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
