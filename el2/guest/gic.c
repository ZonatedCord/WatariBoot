// SPDX-License-Identifier: MIT
// Watari EL2 test guest: GICv3 access helpers. Register layout from the GIC architecture specification IHI 0069
// (GICD/GICR frames, GICR_WAKER bits 1-2, GICR_TYPER Last bit 4 and Affinity [63:32], ICC_SGI1R_EL1 fields).
#include "guest.h"

u64 g_gicd = 0x08000000ull, g_gicr = 0x080A0000ull, g_gicr_stride = 0x20000ull;
unsigned g_ncores;
u64 g_mpidr[MAX_CORES];

u32 gic_rd32(u64 a) { return *(volatile u32 *)a; }
void gic_wr32(u64 a, u32 v) { *(volatile u32 *)a = v; }
static u64 rd64(u64 a) { return *(volatile u64 *)a; }
static void wr8(u64 a, unsigned v) { *(volatile unsigned char *)a = (unsigned char)v; }

static u64 rd_base(unsigned core) { return g_gicr + (u64)core * g_gicr_stride; }
static u64 sgi_base(unsigned core) { return rd_base(core) + 0x10000; }

/* GICR_TYPER Affinity_Value [63:32] = Aff3.Aff2.Aff1.Aff0; MPIDR layout puts Aff3 at [39:32]. */
static u64 aff_to_mpidr(u64 typer)
{
    u64 aff = typer >> 32;

    return (aff & 0xffffff) | (((aff >> 24) & 0xff) << 32);
}

unsigned gic_scan_cores(void)
{
    g_ncores = 0;
    for (unsigned i = 0; i < MAX_CORES; i++) {
        u64 t = rd64(rd_base(i) + 0x08);

        g_mpidr[i] = aff_to_mpidr(t);
        g_ncores++;
        if (t & (1u << 4))                      /* Last */
            break;
    }
    return g_ncores;
}

int gic_core_of(u64 mpidr)
{
    for (unsigned i = 0; i < g_ncores; i++)
        if (g_mpidr[i] == (mpidr & 0xff00ffffffull))
            return (int)i;
    return -1;
}

bool gic_wake(unsigned core)
{
    u64 w = rd_base(core) + 0x14;
    u64 end = cntvct() + ms_to_ticks(500);

    gic_wr32(w, gic_rd32(w) & ~2u);              /* ProcessorSleep = 0 */
    while (gic_rd32(w) & 4u)                     /* ChildrenAsleep */
        if (cntvct() > end)
            return false;
    return true;
}

void gic_cpu_enable(void)
{
    u64 sre = SYSREG_RD(icc_sre_el1);

    SYSREG_WR(icc_sre_el1, sre | 1);
    __asm__ volatile("isb");
    SYSREG_WR(icc_pmr_el1, 0xf0);
    SYSREG_WR(icc_bpr1_el1, 7);
    SYSREG_WR(icc_igrpen1_el1, 1);
    __asm__ volatile("isb");
}

void gic_priv_enable(unsigned core, unsigned intid)
{
    u64 s = sgi_base(core);

    gic_wr32(s + 0x080, gic_rd32(s + 0x080) | (1u << intid));   /* IGROUPR0: Group 1 */
    wr8(s + 0x400 + intid, 0x80);                                /* IPRIORITYR */
    gic_wr32(s + 0x100, 1u << intid);                            /* ISENABLER0 */
}

void gic_send_sgi(unsigned intid, u64 t)
{
    u64 aff0 = t & 0xff, aff1 = (t >> 8) & 0xff, aff2 = (t >> 16) & 0xff, aff3 = (t >> 32) & 0xff;
    u64 v = ((u64)intid << 24) | (aff1 << 16) | (aff2 << 32) | (aff3 << 48) | ((aff0 >> 4) << 44) |
            (1ull << (aff0 & 15));

    SYSREG_WR(icc_sgi1r_el1, v);
    __asm__ volatile("isb");
}
