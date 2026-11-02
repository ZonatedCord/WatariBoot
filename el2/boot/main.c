// SPDX-License-Identifier: MIT
// Watari EL2 M0-C .. M1-4: boot skeleton (QEMU virt, TCG).
// Enters at EL2, installs vectors, builds stage 2, drops to a tiny EL1 payload
// and answers its HVCs (PSCI, see psci.c). M1-3: secondary cores are started by
// the guest's PSCI CPU_ON and enter through el2_secondary_main().
// Written from the Arm ARM and PSCI DEN0022; no third-party code copied.

#include <stdint.h>
#include "wel2_vgic.h"
#include "boot.h"
#include "fdt.h"
#include "wel2_maps.h"

#define HCR_VM   (1ULL << 0)
#define HCR_FMO  (1ULL << 3)
#define HCR_IMO  (1ULL << 4)
#define HCR_AMO  (1ULL << 5)
#define HCR_TSC  (1ULL << 19)   /* trap EL1 SMC to EL2 (EC 0x17) */
#define HCR_RW   (1ULL << 31)
#define HCR_E2H  (1ULL << 34)   /* VHE: EL2 hosts (Arm ARM D8.13); TGE stays 0, the guest runs at EL1 */

#define SPSR_EL1H_MASKED 0x3c5ULL   /* M=EL1h, DAIF all masked */

#define EC_HVC64 0x16
#define EC_SMC64 0x17

#define EC_DABT_LOWER 0x24
#define EC_SYSREG     0x18   /* trapped MSR/MRS/system instruction (Arm ARM D24.2) */

#ifndef WEL2_CHAIN
extern uint8_t __el1_stack_top[];
#else
extern uint8_t __image_start[];
#endif

struct pcpu g_pcpu[MAX_CPUS];
unsigned g_ncpu;
struct vgic g_vgic;
uint64_t g_hcr;

unsigned this_cpu(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, tpidr_el2" : "=r"(v));
    return (unsigned)v;
}

/* ---- Exception handling -------------------------------------------------- */
static const char *const vec_names[16] = {
    "cur_el_sp0 sync", "cur_el_sp0 irq", "cur_el_sp0 fiq", "cur_el_sp0 serror",
    "cur_el_spx sync", "cur_el_spx irq", "cur_el_spx fiq", "cur_el_spx serror",
    "lower_a64 sync", "lower_a64 irq", "lower_a64 fiq", "lower_a64 serror",
    "lower_a32 sync", "lower_a32 irq", "lower_a32 fiq", "lower_a32 serror",
};

void __attribute__((noreturn)) fatal_msg(const char *why)
{
    puts_("Watari EL2: FATAL: ");
    puts_(why);
    puts_("\nWatari EL2: halted\n");
    for (;;)
        __asm__ volatile("wfe");
}

/* Never hang silently (lesson F-14): dump state, then halt. */
static void __attribute__((noreturn)) fatal(struct frame *f, unsigned idx)
{
    put_kn("Watari EL2: cpu ", this_cpu(), " UNEXPECTED EXCEPTION: \n");
    puts_(vec_names[idx & 15]);
    puts_("\n");
    put_kv("  ESR_EL2  = ", f->esr);
    put_kv("  ELR_EL2  = ", f->elr);
    put_kv("  FAR_EL2  = ", f->far);
    put_kv("  SPSR_EL2 = ", f->spsr);
    for (unsigned i = 0; i < 31; i++) {            /* guest registers at the fault */
        char nm[12] = "  x";
        unsigned k = 3;
        if (i >= 10)
            nm[k++] = (char)('0' + i / 10);
        nm[k++] = (char)('0' + i % 10);
        nm[k++] = ' ';
        nm[k++] = '=';
        nm[k++] = ' ';
        nm[k] = 0;
        put_kv(nm, f->x[i]);
    }
    puts_("Watari EL2: halted\n");
    for (;;)
        __asm__ volatile("wfe");
}

static unsigned g_hvc_log_budget = 8;

static void handle_hvc(struct frame *f)
{
    uint32_t imm = f->esr & 0xffff;
    uint32_t fid = (uint32_t)f->x[0];

    if (g_hvc_log_budget) {
        g_hvc_log_budget--;
        puts_("Watari EL2: HVC from EL1, imm=");
        puthex(imm);
        puts_(" fid=");
        puthex(fid);
        puts_("\n");
    }

    if (imm == 0 && psci_handle(f, fid))
        return;
    /* Unknown conduit/immediate: SMCCC NOT_SUPPORTED. ELR already points past
     * the HVC. */
    f->x[0] = (uint64_t)-1LL;
    f->x[1] = f->x[2] = f->x[3] = 0;
}

/* HCR_EL2.TSC=1 (INTERFACES 8, 9): an SMC at EL1 traps here. The FDT handed to EDK2
 * says "hvc", but the ACPI FADT that QEMU builds for Windows says SMC (QEMU virt with
 * virtualization=on uses the SMC conduit: M2, F-23), so Windows calls PSCI with SMC.
 * Both conduits reach the same PSCI handler (PSCI DEN0022F.b 5.2 allows either; the
 * conduit only selects the instruction). Anything that is not PSCI gets SMCCC
 * NOT_SUPPORTED. The preferred return address of a trapped SMC is the SMC itself
 * (Arm ARM D24.2.40, EC 0x17), so ELR must be advanced by hand. */
static unsigned g_smc_log_budget = 8;

static void handle_smc(struct frame *f)
{
    uint32_t fid = (uint32_t)f->x[0];

    f->elr += 4;
    if (g_trace_on) {
        static unsigned n_smc;
        uint64_t x1 = f->x[1], x2 = f->x[2], x3 = f->x[3];
        bool ok = psci_handle(f, fid);

        if (n_smc++ < 80) {
            put_kv("Watari EL2: TR SMC fid ", fid);
            put_kv("Watari EL2: TR    x1 ", x1);
            put_kv("Watari EL2: TR    x2 ", x2);
            put_kv("Watari EL2: TR    x3 ", x3);
            put_kv("Watari EL2: TR    -> x0 ", f->x[0]);
        }
        if (ok)
            return;
    } else if (psci_handle(f, fid)) {
        return;
    }
    if (g_smc_log_budget) {
        g_smc_log_budget--;
        puts_("Watari EL2: non-PSCI SMC from EL1, fid=");
        puthex(fid);
        puts_(" -> NOT_SUPPORTED\n");
    }
    f->x[0] = (uint64_t)-1LL;
    f->x[1] = f->x[2] = f->x[3] = 0;
}

/* ---- Stage-2 translation (identity map, 4 KB or 16 KB granule) ---------------
 * The tables are built by the granule-parametric builder (wel2_pt.h, wel2_maps.h: host-tested in
 * tests/test_pt.c) with the granule stage1.c chose (g_gran, boot argument wel2.gran=4k|16k, #58). Apple cores
 * implement only 16 KB, so 16 KB is the M1 case; the map logic (identity, device attributes, GIC holes) does
 * not depend on the granule.
 *
 * IPA space 40 bits (T0SZ=24; M1-4: EDK2 touches the PCIe high ECAM at 0x4010000000 and may use the high MMIO
 * window at 0x8000000000). 4 KB: start level 0 (SL0=2, 2 entries of 512 GB), 1 GB / 2 MB blocks. 16 KB: start
 * level 1 (SL0=2, 16 entries of 64 GB; no level-1 blocks without FEAT_LPA2), 32 MB blocks at level 2, 16 KB
 * pages at level 3 (Arm ARM D8.2.9, Table D8-27/D8-31). Contents:
 *   everything             : Device-nGnRnE blocks (MMIO, PCIe windows)
 *   RAM (g_ram_base..g_ram_end, from the FDT /memory node, #60; default 0x40000000-0x13fffffff):
 *                            Normal WB blocks, the largest the granule and the alignment allow (the RAM helper
 *                            rounds to 2 MB, so a RAM end that is not 32 MB aligned costs 16 KB pages)
 *   GICD (64 KB) and the first 8 GICR frames (8 x 128 KB): UNMAPPED, so the guest faults to EL2 there; all are
 *                            multiples of 16 KB, so the holes are exact for both granules (the ITS and GICv2m
 *                            frames next to them stay mapped as device).
 * Chain build (M1-4): the layer (image + stacks, from __image_start to the end of the 2 MB carve-out) is also
 * unmapped: a guest access takes a stage-2 translation fault and EL2 dumps it (F-14). The FDT hand-off buffer
 * below __image_start stays mapped. __image_start is 16 KB aligned (text_offset 0x80000).
 * Note: without the chain build EL2's own image sits in the RAM block, i.e. it is visible to the guest.
 * M1-3: all cores share these tables (one VTTBR value, VMID 0).
 */
static uint8_t s2_pool[WEL2_S2_POOL_BYTES] __attribute__((aligned(16384)));
static struct wel2_pt s2_pt;
static uint64_t g_vtcr;

static void stage2_init(void)
{
    struct wel2_ram ram = { g_ram_base, g_ram_end };
    struct wel2_range holes[3] = {
        { GICD_PHYS, GICD_SIZE },
        { GICR_PHYS, 8ULL * VGIC_GICR_FRAME_SIZE },
        { 0, 0 },
    };
    unsigned nholes = 2;

#ifdef WEL2_CHAIN
    uint64_t hide_lo = (uint64_t)(uintptr_t)__image_start & ~((1ULL << g_gran) - 1);

    holes[nholes].base = hide_lo;
    holes[nholes].size = GUEST_RAM_BASE + WEL2_CARVE_SIZE - hide_lo;
    nholes++;
#endif
    if (wel2_pt_init(&s2_pt, s2_pool, sizeof(s2_pool), g_gran, WEL2_S2_IPA_BITS) ||
        wel2_s2_build(&s2_pt, &ram, holes, nholes))
        fatal_msg("stage-2 tables: bad geometry or the table pool is too small");
    /* VTCR_EL2.PS = min(ID_AA64MMFR0_EL1.PARange, 0b010 = 40 bit): the M1 may implement fewer physical address
     * bits than the 40-bit IPA space the guest is given (an output address above PARange is an address size fault). */
    uint64_t mmfr0;

    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    g_vtcr = wel2_vtcr(&s2_pt, (mmfr0 & 0xf) < 2 ? (unsigned)(mmfr0 & 0xf) : 2u);
    put_kv("Watari EL2: stage-2 granule shift = ", g_gran);
    put_kv("Watari EL2:   start level = ", s2_pt.start_level);
    put_kv("Watari EL2:   tables used = ", s2_pt.used);
}

/* Per core: VTCR_EL2 (Arm ARM D24.2.223) from the builder: RES1 bit31, PS=min(PARange, 40 bit), TG0 (4 KB: 0,
 * 16 KB: 0b10), SH0=3, ORGN0=IRGN0=1 (WB WA), SL0 per granule and start level, T0SZ=24 (40-bit IPA). */
void stage2_enable(void)
{
    __asm__ volatile("msr vtcr_el2, %0" : : "r"(g_vtcr));
    __asm__ volatile("msr vttbr_el2, %0" : : "r"(wel2_pt_root_addr(&s2_pt)));
    /* The tables were written by this core with the stage-1 MMU on (Normal WB inner-shareable), and the walk
     * attributes of VTCR_EL2 are WB, inner-shareable: coherent with the walker, so DSB ISHST before the TLBI is
     * enough. A walker that is not coherent would need DC CVAC over the pool here [hypothesis for the M1]. */
    __asm__ volatile("dsb ishst; tlbi vmalls12e1is; dsb ish; isb" : : : "memory");
}

/* ---- Virtual GIC wiring --------------------------------------------------- */
static unsigned g_log_budget = 16;

/* M2 diagnostics (boot argument wel2.trace=SKIP): log every trapped GICD/GICR access after the first
 * SKIP ones (EDK2 does ~1000 of them), at most 3000 lines. Off by default. */
bool g_trace_on;
static bool g_lpi = true;       /* wel2.lpi=off: hide LPIs from the guest (GICD_TYPER.LPIS = 0) */
static unsigned g_trace_skip, g_trace_left = 3000;

static unsigned idx_cpu(void)
{
    uint64_t i;

    __asm__ volatile("mrs %0, tpidr_el2" : "=r"(i));
    return (unsigned)i;
}

static void vgic_log_cb(void *ctx, enum vgic_log_kind kind, uint64_t addr,
                        unsigned size, bool is_write, uint64_t val)
{
    (void)ctx;
    if (!g_log_budget)
        return;
    g_log_budget--;
    puts_("Watari EL2: vgic log kind=");
    puthex(kind);
    puts_(is_write ? " W addr=" : " R addr=");
    puthex(addr);
    puts_(" size=");
    puthex(size);
    puts_(" val=");
    puthex(val);
    puts_("\n");
}

static unsigned g_eff_log_budget = 12;

static void vgic_effect_cb(void *ctx, enum vgic_effect eff, uint32_t intid, int arg)
{
    (void)ctx;
    if (g_eff_log_budget) {
        g_eff_log_budget--;
        puts_("Watari EL2: vgic effect=");
        puthex(eff);
        puts_(" intid=");
        puthex(intid);
        puts_(" arg=");
        puthex((uint64_t)(int64_t)arg);
        puts_("\n");
    }
    cpuif_vgic_effect(eff, intid, arg);        /* M1-3: apply to the physical GICD */
}

static void lpi_reg_cb(void *ctx, unsigned cpu, unsigned off, uint64_t val)
{
    (void)ctx;
    if (g_trace_on) {
        put_kv("Watari EL2: TR LPI register write, offset ", off);
        put_kv("Watari EL2: TR    cpu ", cpu);
        put_kv("Watari EL2: TR    value ", val);
    }
    gic_phys_lpi_reg(cpu, off, val);
}

static void vgic_setup(void)
{
    uint32_t aff[MAX_CPUS];

    for (unsigned i = 0; i < g_ncpu; i++)
        aff[i] = g_pcpu[i].aff;
    struct vgic_config cfg = {
        .gicd_base = GICD_PHYS,
        .gicr_base = GICR_PHYS,
        .nr_vcpus = g_ncpu,
        .affinity = aff,
        .hooks = { vgic_log_cb, vgic_effect_cb, 0, lpi_reg_cb },
        .lpis = g_lpi && gic_phys_has_lpis(),
        .idbits = gic_phys_idbits(),
    };
    pirq_qemu_init(&g_vgic, g_ncpu);            /* SPI policy first: vGIC effects reach it */
    if (vgic_init(&g_vgic, &cfg) != 0)
        fatal_msg("vgic_init failed");
}

/* Guest data abort at stage 2 (Arm ARM D24.2.40 ESR_EL2, "ISS encoding for an
 * exception from a Data Abort"): ISV[24] SAS[23:22] SSE[21] SRT[20:16] SF[15]
 * WnR[6] DFSC[5:0]. IPA = HPFAR_EL2.FIPA[43:4] << 8 | FAR_EL2[11:0]. */
static void handle_dabt(struct frame *f, unsigned idx)
{
    uint64_t hpfar;
    __asm__ volatile("mrs %0, hpfar_el2" : "=r"(hpfar));
    uint64_t ipa = (((hpfar >> 4) & 0xfffffffffULL) << 12) | (f->far & 0xfff);
    uint32_t iss = f->esr & 0x1ffffff;
    int isv = (iss >> 24) & 1;
    unsigned size = 1u << ((iss >> 22) & 3);
    int sse = (iss >> 21) & 1;
    unsigned srt = (iss >> 16) & 31;
    int wnr = (iss >> 6) & 1;
    uint32_t dfsc = iss & 0x3f;
    int in_gic = (ipa >= GICD_PHYS && ipa < GICD_PHYS + GICD_SIZE) ||
                 (ipa >= GICR_PHYS && ipa < GICR_PHYS + 8 * VGIC_GICR_FRAME_SIZE);

    if (!isv || !in_gic || (dfsc >> 2) != 1 /* translation fault, any level */) {
        puts_("Watari EL2: unhandled guest data abort\n");
        put_kv("  HPFAR_EL2 = ", hpfar);
        put_kv("  IPA       = ", ipa);
        put_kv("  ISV       = ", (uint64_t)isv);
        fatal(f, idx);
    }

    uint64_t val = 0;
    int rc;
    if (wnr) {
        val = srt == 31 ? 0 : f->x[srt];
        if (size < 8)
            val &= (1ULL << (size * 8)) - 1;
        rc = vgic_mmio_write(&g_vgic, ipa, size, val);
    } else {
        rc = vgic_mmio_read(&g_vgic, ipa, size, &val);
        if (rc == VGIC_OK) {
            if (sse && size < 8 && ((val >> (size * 8 - 1)) & 1))
                val |= ~0ULL << (size * 8);
            if (!((iss >> 15) & 1))        /* SF=0: 32-bit destination */
                val &= 0xffffffffULL;
            if (srt != 31)
                f->x[srt] = val;
        }
    }
    if (rc != VGIC_OK) {
        puts_("Watari EL2: vgic rejected access (rc=");
        puthex((uint64_t)(int64_t)rc);
        puts_(")\n");
        put_kv("  IPA       = ", ipa);
        fatal(f, idx);
    }
    if (g_trace_on) {
        static unsigned n_seen;

        if ((f->elr >> 48) == 0xffff && ++n_seen > g_trace_skip && g_trace_left) {
            g_trace_left--;
            puts_("Watari EL2: TR cpu=");
            puthex(idx_cpu());
            puts_(wnr ? " W " : " R ");
            puthex(ipa);
            puts_(" sz=");
            puthex(size);
            puts_(" v=");
            puthex(wnr ? val : (srt == 31 ? 0 : f->x[srt]));
            puts_(" pc=");
            puthex(f->elr);
            puts_("\n");
        }
    }
    f->elr += 4;   /* AArch64 guest: ISV=1 implies a 4-byte instruction */
#ifdef WEL2_CHAIN
    /* M1-4: progress evidence for a firmware that does thousands of GIC accesses. */
    {
        static unsigned n_gicd, n_gicr;
        unsigned n = ipa < GICD_PHYS + GICD_SIZE ? ++n_gicd : ++n_gicr;

        for (unsigned p = 1; p <= 1000000; p *= 10) {
            if (n == p) {
                put_kv(ipa < GICD_PHYS + GICD_SIZE ? "Watari EL2: vGIC GICD traps handled = "
                                                   : "Watari EL2: vGIC GICR traps handled = ", n);
            }
        }
    }
#endif
}

void el2_exception(struct frame *f, unsigned idx)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;

    big_lock();
    if (idx == 8 && ec == EC_HVC64) {
        handle_hvc(f);
    } else if (idx == 8 && ec == EC_SMC64) {
        handle_smc(f);
    } else if (idx == 8 && ec == EC_DABT_LOWER) {
        handle_dabt(f, idx);
    } else if (idx == 8 && ec == EC_SYSREG && cpuif_sysreg_trap(f)) {
        /* ICC_SGI*R_EL1 write: the SGI is now pending in the model */
    } else if (idx == 9) {
        cpuif_irq();                         /* physical IRQ: timer / maintenance / kick / SPI */
        big_unlock();
        return;
    } else {
        fatal(f, idx);
    }
    /* Any guest-visible state change (vGIC MMIO write, SGI, ...) may have made
     * an interrupt deliverable, here or on another core. */
    cpuif_flush();
    cpuif_kick_others();
    big_unlock();
}

/* Identify this core among the redistributors and publish its index. */
static void bind_this_cpu(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    unsigned idx = cpu_from_mpidr(mpidr);

    if (idx == ~0u)
        fatal_msg("MPIDR not found among the redistributors");
    __asm__ volatile("msr tpidr_el2, %0" : : "r"((uint64_t)idx));
}

/* What the guest reads in MIDR_EL1 / MPIDR_EL1 (VPIDR_EL2 / VMPIDR_EL2): the
 * physical values, so a PSCI MPIDR means the same to both sides. */
static void set_virtual_ids(void)
{
    uint64_t midr, mpidr;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    __asm__ volatile("msr vpidr_el2, %0; msr vmpidr_el2, %1; isb" : : "r"(midr), "r"(mpidr));
}

#ifdef WEL2_CHAIN
/* M1-4: hand the machine to EDK2 (ArmVirtQemuKernel) at EL1, the way a boot loader
 * hands over to an arm64 Linux kernel (Linux arm64 booting.rst, entry state):
 *   x0 = physical address of the FDT, x1 = x2 = x3 = 0, MMU off, D-cache off,
 *   PSTATE.DAIF masked, the image 4 KB aligned at its load address.
 * Differences from the plain protocol: EDK2 is entered at EL1 (the layer owns EL2),
 * and the FDT is patched first (fdt.c): /memory loses the layer's carve-out, the PSCI
 * conduit becomes "hvc". The entry is the start of the EDK2 image, whose header
 * (code0 = "adr x1, .") tells the relocatable PrePi where it was loaded. */
static void __attribute__((noreturn)) chain_to_edk2(uint64_t fdt)
{
    struct fdt_patch_info pi;

    /* EDK2's ArmGicV3 driver never does the GICR_WAKER handshake (it expects awake
     * redistributors, as left by TF-A or QEMU); a vCPU asleep in the model forwards
     * nothing and the first timer tick would be lost for good (F-21). The boot vCPU
     * (here) and each vCPU PSCI starts (el2_secondary_main) are handed over awake. */
    vgic_vcpu_wake(&g_vgic, this_cpu());
    put_kv("Watari EL2: FDT from QEMU at ", fdt);
    if (!fdt || (fdt & 7) || fdt_check((void *)fdt))
        fatal_msg("x0 does not point to a valid FDT (is QEMU -kernel used?)");
    if (fdt_patch_for_guest((void *)fdt, GUEST_RAM_BASE, WEL2_CARVE_SIZE, g_ram_from_fdt ? g_ram_end : 0, &pi))
        fatal_msg("FDT is malformed, cannot patch it");
    if (!pi.mem_found || !pi.mem_patched)
        fatal_msg("FDT /memory not found or does not start at the layer's carve-out");
    if (!pi.psci_found)
        fatal_msg("FDT has no /psci/method");
    put_kv("Watari EL2: FDT /memory base now ", pi.mem_base_new);
    put_kv("Watari EL2: FDT /memory size now ", pi.mem_size_new);
    if (pi.mem_trimmed)
        put_kv("Watari EL2: FDT /memory trimmed to the mapped RAM end ", g_ram_end);
    puts_(pi.psci_patched ? "Watari EL2: FDT /psci method \"smc\" patched to \"hvc\"\n"
                           : "Watari EL2: FDT /psci method already \"hvc\"\n");
    if (pi.timer_found) {
        put_kv("Watari EL2: FDT timer INTID secure phys     = ", pi.timer_intid[0]);
        put_kv("Watari EL2: FDT timer INTID non-secure phys = ", pi.timer_intid[1]);
        put_kv("Watari EL2: FDT timer INTID virtual         = ", pi.timer_intid[2]);
    }
    /* Copy the patched FDT to the RAM base (EDK2_FDT_ADDR): EDK2 PrePi's debug
     * output reads the FDT there before it learns the real address (F-21). */
    uint32_t total = fdt_used_size((void *)fdt);
    put_kv("Watari EL2: FDT used bytes = ", total);
    if (total > ((uint64_t)(uintptr_t)__image_start - EDK2_FDT_ADDR) || fdt < pi.mem_base_new)
        fatal_msg("FDT does not fit the hand-off buffer, or sits inside the layer's region");
    memcpy((void *)EDK2_FDT_ADDR, (void *)fdt, total);
    fdt_set_totalsize((void *)EDK2_FDT_ADDR, total);
    /* EL2 wrote through its cacheable map; the guest starts with MMU and caches off. */
    dcache_clean_range((void *)EDK2_FDT_ADDR, total);
    put_kv("Watari EL2: patched FDT copied to ", EDK2_FDT_ADDR);
    uint32_t magic = *(volatile uint32_t *)(EDK2_ENTRY + 0x38);
    if (magic != 0x644d5241) {               /* "ARM\x64" */
        put_kv("Watari EL2: no arm64 Image header at EDK2 entry ", EDK2_ENTRY);
        put_kv("Watari EL2: found ", magic);
        fatal_msg("EDK2 image missing: add -device loader,file=QEMU_EFI-kernel.fd,addr=0x40280000,force-raw=on");
    }
    put_kv("Watari EL2: eret to EDK2 at EL1, entry = ", EDK2_ENTRY);
    enter_el1(EDK2_ENTRY, 0, SPSR_EL1H_MASKED, g_hcr, EDK2_FDT_ADDR);
}
#endif

/* Boot arguments (/chosen/bootargs, QEMU -append), space separated:
 *   wel2.cpuif=hw|trap   mode of the virtual CPU interface (build default WEL2_CPUIF_TRAP_DEFAULT)
 *   wel2.lpi=off         do not expose LPIs/ITS (default: exposed when the physical GIC has them)
 *   wel2.trace=SKIP      log trapped GICD/GICR accesses after the first SKIP (M2 diagnostics)
 *   wel2.test=noeoi      test knob: LRs without EOI maintenance, only UIE refills them (#54) */
static bool arg_is(const char *p, const char *key)
{
    unsigned k = 0;

    while (key[k] && p[k] == key[k])
        k++;
    return !key[k] && (p[k] == 0 || p[k] == ' ');
}

static void parse_bootargs(uint64_t fdt)
{
    char args[160];

    if (!fdt || (fdt & 7) || fdt_check((void *)fdt) || fdt_get_bootargs((void *)fdt, args, sizeof(args)) < 0)
        return;
    for (const char *p = args; *p; p++) {
        if (p != args && p[-1] != ' ')
            continue;
        if (arg_is(p, "wel2.cpuif=trap"))
            g_cpuif_trap = true;
        else if (arg_is(p, "wel2.lpi=off"))
            g_lpi = false;
        else if (arg_is(p, "wel2.cpuif=hw"))
            g_cpuif_trap = false;
        else if (p[0] == 'w' && p[1] == 'e' && p[2] == 'l' && p[3] == '2' && p[4] == '.' && p[5] == 't' &&
                 p[6] == 'r' && p[7] == 'a' && p[8] == 'c' && p[9] == 'e' && p[10] == '=') {
            g_trace_on = true;
            g_trace_skip = 0;
            for (const char *q = p + 11; *q >= '0' && *q <= '9'; q++)
                g_trace_skip = g_trace_skip * 10 + (unsigned)(*q - '0');
        } else if (arg_is(p, "wel2.test=noeoi"))
            g_cpuif_noeoi = true;
    }
}

/* Per-core EL2 regime registers that change layout with HCR_EL2.E2H (Arm ARM D8.13, D24.2.37).
 * CPTR_EL2 with E2H = 1 uses the CPACR_EL1 layout: FPEN[21:20] = 0b11 (no FP/SIMD trap of EL1 and EL0; the
 * reset value is UNKNOWN and 0 would trap the guest's first FP instruction), ZEN[17:16] / SMEN[25:24] = 0b11
 * only where SVE / SME exist (the fields are RES0 otherwise); TCPAC, TAM, TTA = 0. With E2H = 0 the register is
 * left alone, as it always was (QEMU resets it to the no-trap value). */
static void regime_init_cpu(void)
{
    uint64_t pfr0, pfr1, cptr;

    if (!g_e2h)
        return;
    __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
    __asm__ volatile("mrs %0, id_aa64pfr1_el1" : "=r"(pfr1));
    cptr = 3ULL << 20;
    if ((pfr0 >> 32) & 0xf)                      /* SVE */
        cptr |= 3ULL << 16;
    if ((pfr1 >> 24) & 0xf)                      /* SME */
        cptr |= 3ULL << 24;
    __asm__ volatile("msr cptr_el2, %0; isb" : : "r"(cptr));
}

/* #53: the stage-1 map is already on (start.S). Report the regime so the log proves it, and refuse to
 * continue without it (the locks and the unaligned-capable code depend on Normal memory). */
static void stage1_report(void)
{
    uint64_t sctlr, tcr, hcr;

    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    __asm__ volatile("mrs %0, tcr_el2" : "=r"(tcr));
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
    if ((sctlr & 0x1005) != 0x1005)
        fatal_msg("EL2 stage-1 MMU/caches are not on (SCTLR_EL2.M/C/I)");
    put_kv(g_gran == WEL2_GRAN_16K ? "Watari EL2: stage-1 on (identity, 16 KB, 36-bit), SCTLR_EL2 = "
                                   : "Watari EL2: stage-1 on (identity, 4 KB, 39-bit), SCTLR_EL2 = ", sctlr);
    put_kv("Watari EL2:   TCR_EL2 = ", tcr);
    put_kv("Watari EL2:   HCR_EL2.E2H = ", (hcr >> 34) & 1);
    if (((hcr >> 34) & 1) != (g_e2h ? 1u : 0u))
        fatal_msg("HCR_EL2.E2H on this core differs from the value the stage-1 map was built for");
    if (g_e2h_stuck)
        puts_("Watari EL2:   E2H = 0 was requested but the bit is stuck at 1 (the M1 case): running with E2H = 1\n");
    if (g_gran_fallback)
        puts_("Watari EL2:   the requested translation granule is not implemented: using the other one\n");
    put_kv(g_ram_from_fdt ? "Watari EL2: RAM from the FDT /memory node, base " : "Watari EL2: no usable FDT /memory node: default RAM, base ", g_ram_base);
    put_kv("Watari EL2:   RAM end = ", g_ram_end);
}

/* ---- EL2 main ---------------------------------------------------------------- */
void __attribute__((noreturn)) el2_main(uint64_t fdt)
{
    uint64_t el, vbar;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, vbar_el2" : "=r"(vbar));

    puts_("Watari EL2: hello from EL2\n");
    stage1_report();
    put_kv("Watari EL2: CurrentEL = ", el >> 2);
    put_kv("Watari EL2: VBAR_EL2  = ", vbar);

    /* Cores = redistributor frames (GICR_TYPER walk); vCPU i = core i. */
    regime_init_cpu();
    g_ncpu = gic_phys_discover();
    bind_this_cpu();
    put_kv("Watari EL2: cores (redistributors) = ", g_ncpu);
    for (unsigned i = 0; i < g_ncpu; i++)
        g_pcpu[i].state = CPU_ST_OFF;
    g_pcpu[this_cpu()].state = CPU_ST_ON;      /* the cold-boot core is ON */

    /* HCR_EL2: RW=1 (EL1 is AArch64), IMO/FMO/AMO=1 (physical IRQ/FIQ/SError
     * routed to EL2, as designed). E2H as stage1.c left it on this core (1 on the M1; the guest then runs
     * with TGE=0 at EL1, its exceptions and the HVC/SMC/IRQ paths are the same); VM=1 since M1-1 (stage-2
     * with the GIC windows unmapped). M1-4 chain build: TSC=1 (EL1 SMC trapped). */
    g_hcr = HCR_RW | HCR_IMO | HCR_FMO | HCR_AMO | HCR_VM | (g_e2h ? HCR_E2H : 0);
#ifdef WEL2_CHAIN
    g_hcr |= HCR_TSC;
#endif
    parse_bootargs(fdt);
    vgic_setup();
    pirq_vcpu_on(&g_pirq, this_cpu());          /* the cold-boot core can take SPIs */
    stage2_init();
    stage2_enable();
    set_virtual_ids();
    puts_("Watari EL2: stage-2 on (identity, GICD/GICR unmapped), vGIC ready\n");
    gic_phys_init_dist();
    gic_phys_init_cpu();
    cpuif_init(&g_vgic);
    puts_("Watari EL2: physical GIC owned by EL2, virtual CPU interface on\n");
#ifdef WEL2_CHAIN
    chain_to_edk2(fdt);
#else
    (void)fdt;
    puts_("Watari EL2: eret to EL1h payload\n");
    enter_el1((uint64_t)el1_main, (uint64_t)__el1_stack_top, SPSR_EL1H_MASKED, g_hcr, 0);
#endif
}

/* Cores 1..n: started by PSCI CPU_ON through the firmware (secondary_entry in
 * start.S, EL2, MMU off). Same per-core setup as the boot core, then ERET to
 * the entry point the guest asked for, x0 = context_id (PSCI 6.4.3). */
void __attribute__((noreturn)) el2_secondary_main(void)
{
    bind_this_cpu();
    unsigned me = this_cpu();

    {
        uint64_t sctlr;

        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
        if ((sctlr & 0x1005) != 0x1005)
            fatal_msg("secondary core: EL2 stage-1 MMU/caches are not on");
    }
    regime_init_cpu();
    stage2_enable();
    set_virtual_ids();
    gic_phys_init_cpu();
    cpuif_init_cpu();

    big_lock();
#ifdef WEL2_CHAIN
    vgic_vcpu_wake(&g_vgic, me);               /* same hand-off contract as the boot vCPU */
#endif
    uint64_t entry = g_pcpu[me].entry, ctx = g_pcpu[me].ctx;
    g_pcpu[me].state = CPU_ST_ON;
    pirq_vcpu_on(&g_pirq, me);                 /* SPIs the guest routed here come back */
    put_kn("Watari EL2: cpu ", me, " up at EL2, eret to the guest entry point\n");
    big_unlock();
    /* No flush needed here: interrupts sent while ON_PENDING are delivered on
     * this core's first trap/IRQ, and the guest enables its CPU interface only
     * after it runs. */
    enter_el1(entry, 0, SPSR_EL1H_MASKED, g_hcr, ctx);
}
