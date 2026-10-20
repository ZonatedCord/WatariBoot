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

#define HCR_VM   (1ULL << 0)
#define HCR_FMO  (1ULL << 3)
#define HCR_IMO  (1ULL << 4)
#define HCR_AMO  (1ULL << 5)
#define HCR_TSC  (1ULL << 19)   /* trap EL1 SMC to EL2 (EC 0x17) */
#define HCR_RW   (1ULL << 31)

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

/* HCR_EL2.TSC=1 (INTERFACES 8, 9): an SMC at EL1 traps here. The PSCI conduit the
 * guest was told is HVC (FDT patched, see chain_to_edk2), so an SMC is a stray:
 * log it and answer SMCCC NOT_SUPPORTED. The preferred return address of a
 * trapped SMC is the SMC itself (Arm ARM D24.2.40, EC 0x17), so ELR must be
 * advanced by hand. */
static unsigned g_smc_log_budget = 8;

static void handle_smc(struct frame *f)
{
    if (g_smc_log_budget) {
        g_smc_log_budget--;
        puts_("Watari EL2: stray SMC from EL1 (conduit is HVC), fid=");
        puthex((uint32_t)f->x[0]);
        puts_(" -> NOT_SUPPORTED\n");
    }
    f->x[0] = (uint64_t)-1LL;
    f->x[1] = f->x[2] = f->x[3] = 0;
    f->elr += 4;
}

/* ---- Stage-2 translation (identity map, 4 KB granule) ------------------------
 * Choice: 4 KB granule for the spike (Arm ARM D8, VMSAv8-64 stage 2). Apple
 * cores implement only 16 KB, so the hardware port must redo the tables with
 * TG0=16 KB; the map logic (identity, device attributes, GIC holes) is
 * granule independent. [hypothesis for M1 HW]
 *
 * IPA space 40 bits (T0SZ=24), start level 0 (SL0=2): 2 entries of 512 GB (M1-4: EDK2
 * touches the PCIe high ECAM at 0x4010000000 and may use the high MMIO window at
 * 0x8000000000). Level 1 tables hold 1 GB entries:
 *   0x00000000-0x3fffffff : level 2 table of 2 MB device blocks (MMIO), except
 *                           the 2 MB block at 0x08000000, a level-3 table of
 *                           4 KB device pages with the GICD (64 KB) and the
 *                           first 8 GICR frames (8 x 128 KB) UNMAPPED, so the
 *                           guest faults to EL2 there.
 *   0x40000000-0x7fffffff : 1 GB Normal WB block (QEMU virt RAM).
 *   0x80000000-0xbfffffff : 1 GB Normal WB block (RAM of a -m 2G machine, M1-4).
 *   0xc0000000-0xffffffffff : 1 GB device blocks (unused on virt, PCIe high windows).
 * Chain build (M1-4): the first GB is split into 2 MB blocks; in the first block
 * (the layer's carve-out) 4 KB pages hide EL2 (image + stacks).
 * Note: EL2's own image sits in the RAM block, i.e. it is visible to the
 * guest. Isolating EL2 memory is a later hardening step.
 * M1-3: all cores share these tables (one VTTBR value, VMID 0).
 */
#define S2_VALID   (1ULL << 0)
#define S2_TABLE   (1ULL << 1)         /* table (levels 0-2) / page (level 3) */
#define S2_AF      (1ULL << 10)
#define S2_SH_ISH  (3ULL << 8)
#define S2_S2AP_RW (3ULL << 6)
#define S2_XN      (1ULL << 54)        /* XN[1]=1: execute-never */
#define S2_ATTR_DEV    (0x0ULL << 2)   /* MemAttr 0b0000: Device-nGnRnE */
#define S2_ATTR_NORMAL (0xfULL << 2)   /* MemAttr 0b1111: Normal, Inner+Outer WB */

#define S2_BLOCK_DEV    (S2_VALID | S2_AF | S2_S2AP_RW | S2_ATTR_DEV | S2_XN)
#define S2_BLOCK_NORMAL (S2_VALID | S2_AF | S2_S2AP_RW | S2_ATTR_NORMAL | S2_SH_ISH)

static uint64_t s2_l0[512] __attribute__((aligned(4096)));   /* 2 used: 0-512 GB, 512 GB-1 TB */
static uint64_t s2_l1[512] __attribute__((aligned(4096)));
static uint64_t s2_l1hi[512] __attribute__((aligned(4096)));
static uint64_t s2_l2[512] __attribute__((aligned(4096)));
static uint64_t s2_l3[512] __attribute__((aligned(4096)));
#ifdef WEL2_CHAIN
static uint64_t s2_l2ram[512] __attribute__((aligned(4096)));
static uint64_t s2_l3ram[512] __attribute__((aligned(4096)));
#endif

static void stage2_init(void)
{
    /* level 3 for 0x08000000-0x081fffff: 512 x 4 KB device pages */
    for (unsigned i = 0; i < 512; i++)
        s2_l3[i] = (GICD_PHYS + ((uint64_t)i << 12)) | S2_BLOCK_DEV | S2_TABLE;
    unsigned first = 0, n = GICD_SIZE >> 12;                       /* GICD */
    for (unsigned i = first; i < first + n; i++)
        s2_l3[i] = 0;
    first = (GICR_PHYS - GICD_PHYS) >> 12;                         /* GICR x8 */
    n = (8 * VGIC_GICR_FRAME_SIZE) >> 12;
    for (unsigned i = first; i < first + n; i++)
        s2_l3[i] = 0;

    for (unsigned i = 0; i < 512; i++)
        s2_l2[i] = ((uint64_t)i << 21) | S2_BLOCK_DEV;
    s2_l2[GICD_PHYS >> 21] = (uint64_t)(uintptr_t)s2_l3 | S2_VALID | S2_TABLE;

    s2_l1[0] = (uint64_t)(uintptr_t)s2_l2 | S2_VALID | S2_TABLE;
    s2_l1[1] = 0x40000000ULL | S2_BLOCK_NORMAL;
    s2_l1[2] = 0x80000000ULL | S2_BLOCK_NORMAL;   /* RAM of a -m 2G machine */
#ifdef WEL2_CHAIN
    /* M1-4: the layer (image + stacks, from __image_start to the end of the carve-out)
     * is NOT mapped: a guest access takes a stage-2 translation fault and EL2 dumps it
     * (F-14). The first 2 MB are 4 KB pages, mapped below __image_start (FDT hand-off
     * buffer) and unmapped above; the rest of the first GB is 2 MB Normal WB blocks. */
    uint64_t hide_lo = (uint64_t)(uintptr_t)__image_start & ~0xfffULL;
    for (unsigned i = 0; i < 512; i++) {
        uint64_t pa = GUEST_RAM_BASE + ((uint64_t)i << 12);
        s2_l3ram[i] = (pa >= hide_lo && pa < GUEST_RAM_BASE + WEL2_CARVE_SIZE)
                          ? 0 : (pa | S2_BLOCK_NORMAL | S2_TABLE);
    }
    for (unsigned i = 0; i < 512; i++)
        s2_l2ram[i] = (GUEST_RAM_BASE + ((uint64_t)i << 21)) | S2_BLOCK_NORMAL;
    s2_l2ram[0] = (uint64_t)(uintptr_t)s2_l3ram | S2_VALID | S2_TABLE;
    s2_l1[1] = (uint64_t)(uintptr_t)s2_l2ram | S2_VALID | S2_TABLE;
#endif
    for (unsigned i = 3; i < 512; i++)
        s2_l1[i] = ((uint64_t)i << 30) | S2_BLOCK_DEV;
    for (unsigned i = 0; i < 512; i++)
        s2_l1hi[i] = ((512ULL << 30) + ((uint64_t)i << 30)) | S2_BLOCK_DEV;
    s2_l0[0] = (uint64_t)(uintptr_t)s2_l1 | S2_VALID | S2_TABLE;
    s2_l0[1] = (uint64_t)(uintptr_t)s2_l1hi | S2_VALID | S2_TABLE;
}

/* Per core: VTCR_EL2 (Arm ARM D24.2.223): RES1 bit31, PS=0b010 (40 bit), TG0=0
 * (4 KB), SH0=3, ORGN0=IRGN0=1 (WB WA), SL0=2 (start level 0), T0SZ=24 (40-bit IPA). */
void stage2_enable(void)
{
    uint64_t vtcr = (1ULL << 31) | (2ULL << 16) | (3ULL << 12) | (1ULL << 10) |
                    (1ULL << 8) | (2ULL << 6) | 24ULL;
    __asm__ volatile("msr vtcr_el2, %0" : : "r"(vtcr));
    __asm__ volatile("msr vttbr_el2, %0" : : "r"((uint64_t)(uintptr_t)s2_l0));
    /* Tables were written with the MMU off; on real HW add cache maintenance
     * if the stage-2 walker is cacheable. */
    __asm__ volatile("dsb ishst; tlbi vmalls12e1is; dsb ish; isb" : : : "memory");
}

/* ---- Virtual GIC wiring --------------------------------------------------- */
static unsigned g_log_budget = 16;

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
        .hooks = { vgic_log_cb, vgic_effect_cb, 0 },
    };
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
    if (fdt_patch_for_guest((void *)fdt, GUEST_RAM_BASE, WEL2_CARVE_SIZE, &pi))
        fatal_msg("FDT is malformed, cannot patch it");
    if (!pi.mem_found || !pi.mem_patched)
        fatal_msg("FDT /memory not found or does not start at the layer's carve-out");
    if (!pi.psci_found)
        fatal_msg("FDT has no /psci/method");
    put_kv("Watari EL2: FDT /memory base now ", pi.mem_base_new);
    put_kv("Watari EL2: FDT /memory size now ", pi.mem_size_new);
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
        else if (arg_is(p, "wel2.cpuif=hw"))
            g_cpuif_trap = false;
        else if (arg_is(p, "wel2.test=noeoi"))
            g_cpuif_noeoi = true;
    }
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
    put_kv("Watari EL2: stage-1 on (identity, 4 KB, 39-bit), SCTLR_EL2 = ", sctlr);
    put_kv("Watari EL2:   TCR_EL2 = ", tcr);
    put_kv("Watari EL2:   HCR_EL2.E2H = ", (hcr >> 34) & 1);
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
    g_ncpu = gic_phys_discover();
    bind_this_cpu();
    put_kv("Watari EL2: cores (redistributors) = ", g_ncpu);
    for (unsigned i = 0; i < g_ncpu; i++)
        g_pcpu[i].state = CPU_ST_OFF;
    g_pcpu[this_cpu()].state = CPU_ST_ON;      /* the cold-boot core is ON */

    /* HCR_EL2: RW=1 (EL1 is AArch64), IMO/FMO/AMO=1 (physical IRQ/FIQ/SError
     * routed to EL2, as designed). E2H=0 so far; VM=1 since M1-1 (stage-2 with the
     * GIC windows unmapped). M1-4 chain build: TSC=1 (EL1 SMC trapped). VHE later. */
    g_hcr = HCR_RW | HCR_IMO | HCR_FMO | HCR_AMO | HCR_VM;
#ifdef WEL2_CHAIN
    g_hcr |= HCR_TSC;
#endif
    parse_bootargs(fdt);
    vgic_setup();
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
    put_kn("Watari EL2: cpu ", me, " up at EL2, eret to the guest entry point\n");
    big_unlock();
    /* No flush needed here: interrupts sent while ON_PENDING are delivered on
     * this core's first trap/IRQ, and the guest enables its CPU interface only
     * after it runs. */
    enter_el1(entry, 0, SPSR_EL1H_MASKED, g_hcr, ctx);
}
