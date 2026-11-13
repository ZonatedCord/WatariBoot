// SPDX-License-Identifier: MIT
// Watari EL2 M0-C .. M1-4: boot skeleton (QEMU virt, TCG).
// Enters at EL2, installs vectors, builds stage 2, drops to a tiny EL1 payload
// and answers its HVCs (PSCI, see psci.c). M1-3: secondary cores are started by
// the guest's PSCI CPU_ON and enter through el2_secondary_main().
// Written from the Arm ARM and PSCI DEN0022; no third-party code copied.

#include <stdint.h>
#include "wel2_vgic.h"
#include "boot.h"
#include "plat.h"
#include "fdt.h"
#include "wel2_aic.h"
#include "wel2_maps.h"
#include "wel2_v2m.h"
#include "wel2_hvc_vendor.h"
#include "wel2_guest_info.h"
#include "wel2_guest_place.h"

#define HCR_VM   (1ULL << 0)
#define HCR_FMO  (1ULL << 3)
#define HCR_IMO  (1ULL << 4)
#define HCR_AMO  (1ULL << 5)
#define HCR_TID3 (1ULL << 18)   /* trap EL1 reads of the ID group 3 registers (EC 0x18), idreg.c */
#define HCR_TSC  (1ULL << 19)   /* trap EL1 SMC to EL2 (EC 0x17) */
#define HCR_RW   (1ULL << 31)
#define HCR_E2H  (1ULL << 34)   /* VHE: EL2 hosts (Arm ARM D8.13); TGE stays 0, the guest runs at EL1 */
#define HCR_APK  (1ULL << 40)   /* EL1 may use the pointer-authentication keys without a trap */
#define HCR_API  (1ULL << 41)   /* EL1 may execute the pointer-authentication instructions without a trap */

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

/* A layer checkpoint (wel2_chk.h, README "Checkpoints"): the square, the label in the status band and one log line, so a
 * QEMU run (no framebuffer) shows the same progress the panel does. Runs with the MMU on (the two stage-1 squares are
 * painted by stage1.c / el2_main). */
void layer_checkpoint(unsigned n, const char *name)
{
    checkpoint(n);
    wel2_fb_checkpoint_text(&g_fb, name);
    put_kn("Watari EL2: checkpoint ", n, " ");
    puts_(name);
    puts_("\n");
}

/* wel2.hold=1 (T6g PART 3): the hardware procedure reads results from photos, and a reset takes the screen away within
 * seconds. Where the layer would reset at the end of a step (wel2.step=2, the guest's final SYSTEM_RESET) it instead keeps
 * the final state on the panel, says so in the log and in the checkpoint line of the band, and parks this core with the lock
 * released (other cores can still trap and print). wel2.step=1 is not affected: it resets in stage1.c. The fatal paths halt
 * anyway; with the argument they print the same hold message. */
bool g_hold;
#define HOLD_MSG "Watari EL2: HOLD: take the photo, then hold the power button\n"

static void hold_notice(void)
{
    puts_(HOLD_MSG);
    wel2_fb_checkpoint_text(&g_fb, "HOLD: TAKE PHOTO");
}

void __attribute__((noreturn)) layer_reset(bool locked)
{
    if (g_hold) {
        __asm__ volatile("msr daifset, #0xf" ::: "memory");
        hold_notice();
        if (locked)
            big_unlock();
        if (g_plat->park)
            g_plat->park();
        for (;;)
            __asm__ volatile("wfe");
    }
    g_plat->system_reset();
    fatal_msg("system reset returned");
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
    checkpoint_fail();                  /* red square on the panel, even if the text path is the broken part */
    wel2_fb_set_error(&g_fb, why);      /* T6h: the reason stays in the last-error area after the log has scrolled */
    puts_("Watari EL2: FATAL: ");
    puts_(why);
    puts_("\nWatari EL2: halted\n");
    if (g_hold)
        hold_notice();
    for (;;)
        __asm__ volatile("wfe");
}

/* Never hang silently (lesson F-14): dump state, then halt. */
static void __attribute__((noreturn)) fatal(struct frame *f, unsigned idx)
{
    checkpoint_fail();
    if (g_fb.err_kind != 2)             /* fatal_dump() has set its reason already */
        wel2_fb_set_error(&g_fb, vec_names[idx & 15]);
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
    if (g_hold)
        hold_notice();
    for (;;)
        __asm__ volatile("wfe");
}

/* A platform module found a condition it cannot survive (e.g. an FIQ storm): say why, then the same dump as an
 * unexpected exception. */
void __attribute__((noreturn)) fatal_dump(struct frame *f, unsigned idx, const char *why)
{
    wel2_fb_set_error(&g_fb, why);
    puts_("Watari EL2: FATAL: ");
    puts_(why);
    puts_("\n");
    fatal(f, idx);
}

/* ---- Vendor hypercalls (T7, wel2_hvc.h): the test guest's log, checkpoints and counters ------------------------------
 * The logic is vgic/hvc_vendor.c (host-tested); this is its hardware side. handle_hvc holds the big lock, so the UART
 * and the framebuffer need no further locking. */
static bool hvc_range_ok(void *ctx, uint64_t pa, uint64_t len)
{
    (void)ctx;
    if (len == 0 || pa + len < pa)
        return false;
    uint64_t last = pa + len - 1;

    if (!guest_ram_contains(pa) || !guest_ram_contains(last))
        return false;
    /* QEMU: guest RAM is one span that includes the layer's carve-out; the layer itself is never readable. The M1
     * list (g_guest_ram) already excludes it. */
    if (!g_plat->m1_memory && pa < g_carve_base + g_carve_size && last >= g_layer_base)
        return false;
    return true;
}

/* The guest runs with MMU and caches off (its stores are in memory, EL2's cacheable map may hold stale lines) and reads
 * the same way: clean+invalidate around every access [hypothesis: needed on the M1, harmless on QEMU TCG]. */
static void hvc_clean_inv(uint64_t pa, uint64_t len)
{
    uint64_t ctr;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uint64_t line = 4ULL << ((ctr >> 16) & 0xf), a = pa & ~(line - 1);

    for (; a < pa + len; a += line)
        __asm__ volatile("dc civac, %0" : : "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

static void hvc_read_guest(void *ctx, uint64_t pa, void *dst, uint64_t len)
{
    (void)ctx;
    hvc_clean_inv(pa, len);
    memcpy(dst, (const void *)(uintptr_t)pa, len);
}

static void hvc_write_guest(void *ctx, uint64_t pa, const void *src, uint64_t len)
{
    (void)ctx;
    memcpy((void *)(uintptr_t)pa, src, len);
    hvc_clean_inv(pa, len);
}

static void hvc_log_line(void *ctx, const char *line, unsigned n)
{
    static const char pre[] = "Watari EL1: ";
    char buf[WEL2_LOG_MAX + sizeof(pre) + 2];
    unsigned k = 0;

    (void)ctx;
    for (unsigned i = 0; i < sizeof(pre) - 1; i++)
        buf[k++] = pre[i];
    for (unsigned i = 0; i < n && k < sizeof(buf) - 2; i++)
        buf[k++] = line[i];
    buf[k++] = '\n';
    buf[k] = 0;
    puts_(buf);
}

static void hvc_checkpoint(void *ctx, unsigned n, unsigned state)
{
    (void)ctx;
    checkpoint_state(WEL2_CHK_SQUARE_BASE + n, state);
}

static void hvc_stats_fill(void *ctx, struct wel2_stats *out)
{
    (void)ctx;
    cpuif_stats_fill(out);
}

static const struct wel2_hvc_ops g_hvc_ops = {
    .ctx = 0,
    .range_ok = hvc_range_ok,
    .read_guest = hvc_read_guest,
    .write_guest = hvc_write_guest,
    .log_line = hvc_log_line,
    .checkpoint = hvc_checkpoint,
    .stats_fill = hvc_stats_fill,
};

static unsigned g_hvc_log_budget = 8;

static void handle_hvc(struct frame *f)
{
    uint32_t imm = f->esr & 0xffff;
    uint32_t fid = (uint32_t)f->x[0];

    /* The guest's own vendor calls would fill the 8-line trace with noise: it shows the other HVCs. */
    if (g_hvc_log_budget && !(imm == 0 && wel2_hvc_is_vendor(f->x[0]))) {
        g_hvc_log_budget--;
        puts_("Watari EL2: HVC from EL1, imm=");
        puthex(imm);
        puts_(" fid=");
        puthex(fid);
        puts_("\n");
    }

    if (imm == 0 && wel2_hvc_vendor(&g_hvc_ops, f->x))   /* f->x[0..3] in/out; false = not a vendor id */
        return;
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
 * IPA space g_plat->s2_ipa_bits: 40 bits on QEMU (T0SZ=24; M1-4: EDK2 touches the PCIe high ECAM at 0x4010000000 and may use the high MMIO
 * window at 0x8000000000). 4 KB: start level 0 (SL0=2, 2 entries of 512 GB), 1 GB / 2 MB blocks. 16 KB: start
 * level 1 (SL0=2, 16 entries of 64 GB; no level-1 blocks without FEAT_LPA2), 32 MB blocks at level 2, 16 KB
 * pages at level 3 (Arm ARM D8.2.9, Table D8-27/D8-31). The M1 (36 bits, T0SZ=28): 16 KB start level 2 (SL0=1, one
 * table of 2048 32 MB entries), 4 KB start level 1 (SL0=1, 64 entries of 1 GB); wel2_vtcr derives both from the table
 * geometry (tests/test_pt.c). Contents:
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
struct fdt_aic_info g_aic_info;         /* the Watari AIC node (#42), found when g_aic_on */
static uint8_t s2_pool[WEL2_S2_POOL_BYTES] __attribute__((aligned(16384)));
static struct wel2_pt s2_pt;
static bool g_v2m_on;                   /* the page is a hole (no guest RAM there) */
static struct v2m g_v2m;                /* #74: the MSI frame at 0xFFFFF000, trapped as one 16 KB page */
static uint64_t g_vtcr;

/* T6c: on the M1 the guest stage 2 is RAM only (wel2_s2_build_ram): the real AIC, the UART, the watchdog and the
 * framebuffer are simply not mapped for the guest, so no hole list is needed there. The holes below serve the QEMU
 * map (Device blanket with holes). */
static void v2m_stray_log(void *ctx, uint32_t page_off, bool write, unsigned size)
{
    (void)ctx;
    puts_(write ? "Watari EL2: MSI page stray write, page offset " : "Watari EL2: MSI page stray read, page offset ");
    puthex(page_off);
    put_kv("  size = ", size);
}

static void v2m_bell_log(void *ctx, uint32_t val)
{
    (void)ctx;
    put_kv("Watari EL2: MSI doorbell write outside the pool, value ", val);
}

/* Capacity of the stage-2 hole list: GICD, GICR, MSI frame, AIC, console UART and the chain carve-out are six; two
 * spare for the M1 devices T6 adds (WDT, ...). */
#define HOLES_MAX 8

static void stage2_init(void)
{
    struct wel2_ram ram = { g_ram_base, g_ram_end };
    struct wel2_range holes[HOLES_MAX] = {
        { g_plat->vgic_gicd_base, GICD_SIZE },
        { g_plat->vgic_gicr_base, 8ULL * VGIC_GICR_FRAME_SIZE },
    };
    unsigned nholes = 2;

    /* #74: the GICv2m frame (4 KB at 0xFFFFF000) lives in the 16 KB page 0xFFFFC000-0xFFFFFFFF, trapped for
     * both granules (v2m_add_hole -> wel2_s2_add_hole). Nothing else is there on the t8103 [verified: ADT,
     * re/m3a-ehw0-measurements.md]; on the QEMU virt machine the range is unused. SPI pool 928..959. */
    if (v2m_init(&g_v2m, V2M_T8103_BASE, 928, 32, &g_vgic))
        fatal_msg("MSI frame: bad geometry");
    g_v2m.log = v2m_bell_log;
    g_v2m.log_stray = v2m_stray_log;
    /* QEMU virt with -m 4G or more has RAM at 0xFFFFC000: then there is no hole and no frame. */
    g_v2m_on = v2m_page_free(&g_v2m, g_ram_base, g_ram_end);
    if (!g_plat->m1_memory && g_v2m_on && v2m_add_hole(&g_v2m, holes, &nholes, HOLES_MAX, g_gran))
        fatal_msg("stage-2 holes: MSI frame page does not fit");
    if (!g_v2m_on)
        puts_("Watari EL2: MSI frame page 0xFFFFC000 is guest RAM here: not emulated\n");

    if (!g_plat->m1_memory && g_aic_on) {   /* #42: the AIC belongs to the layer, the guest faults there */
        if (wel2_s2_add_hole(holes, &nholes, HOLES_MAX, g_aic_info.base, g_aic_info.size, g_gran))
            fatal_msg("stage-2 holes: AIC window does not fit");
    }

    /* The layer's console UART (Apple S5L, from the FDT) is not the guest's: stage 2 maps every address Device by
     * default, so without this hole the guest could read and write the layer's log UART. (QEMU: the PL011 is the
     * guest's too and stays mapped: no S5L node, no hole.) */
    if (!g_plat->m1_memory && g_con_s5l_base && wel2_s2_add_hole(holes, &nholes, HOLES_MAX, g_con_s5l_base, g_con_s5l_size, g_gran))
        fatal_msg("stage-2 holes: console UART window does not fit");

    /* The framebuffer is the layer's too (checkpoints and log text): never given to the guest. The guest's stage 2
     * is RAM plus these holes; unmapping the framebuffer pages makes a guest access fault to EL2 and be reported. */
    if (!g_plat->m1_memory && g_fb.fmt && wel2_s2_add_hole(holes, &nholes, HOLES_MAX, g_fb.base, wel2_fb_bytes(&g_fb), g_gran))
        fatal_msg("stage-2 holes: framebuffer does not fit");

#ifdef WEL2_CHAIN
    uint64_t hide_lo = (uint64_t)(uintptr_t)__image_start & ~((1ULL << g_gran) - 1);

    if (!g_plat->m1_memory && wel2_s2_add_hole(holes, &nholes, HOLES_MAX, hide_lo, g_carve_base + g_carve_size - hide_lo, g_gran))
        fatal_msg("stage-2 holes: layer carve-out does not fit");
#endif
    if (wel2_pt_init(&s2_pt, s2_pool, sizeof(s2_pool), g_gran, g_plat->s2_ipa_bits) ||
        (g_plat->m1_memory ? wel2_s2_build_ram(&s2_pt, g_guest_ram, g_nguest_ram)
                           : wel2_s2_build(&s2_pt, &ram, holes, nholes)))
        fatal_msg("stage-2 tables: bad geometry or the table pool is too small");
    /* VTCR_EL2.PS = min(ID_AA64MMFR0_EL1.PARange, 0b010 = 40 bit): the M1 may implement fewer physical address
     * bits than the 40-bit IPA space the QEMU guest is given (an output address above PARange is an address size fault);
     * on the M1 the IPA space is 36 bits itself (g_plat->s2_ipa_bits), the PARange measured on the j293. */
    uint64_t mmfr0;

    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    g_vtcr = wel2_vtcr(&s2_pt, (mmfr0 & 0xf) < 2 ? (unsigned)(mmfr0 & 0xf) : 2u);
    put_kv("Watari EL2: stage-2 granule shift = ", g_gran);
    put_kv("Watari EL2:   start level = ", s2_pt.start_level);
    put_kv("Watari EL2:   tables used = ", s2_pt.used);
}

/* Per core: VTCR_EL2 (Arm ARM D24.2.223) from the builder: RES1 bit31, PS=min(PARange, 40 bit), TG0 (4 KB: 0,
 * 16 KB: 0b10), SH0=3, ORGN0=IRGN0=1 (WB WA), SL0 per granule and start level, T0SZ = 64 - g_plat->s2_ipa_bits (24 on QEMU, 28 on the M1). */
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
bool g_aic_allowed = true;   /* wel2.aic=off: ignore the AIC node of the QEMU aic=on machine */
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
    g_plat->lpi_reg(cpu, off, val);
}

static void vgic_setup(uint64_t fdt)
{
    uint32_t aff[MAX_CPUS];

    for (unsigned i = 0; i < g_ncpu; i++)
        aff[i] = g_pcpu[i].aff;
    struct vgic_config cfg = {
        .gicd_base = g_plat->vgic_gicd_base,
        .gicr_base = g_plat->vgic_gicr_base,
        .nr_vcpus = g_ncpu,
        .affinity = aff,
        .hooks = { vgic_log_cb, vgic_effect_cb, 0, lpi_reg_cb },
        .lpis = g_lpi && g_plat->has_lpis(),
        .idbits = g_plat->idbits(),
    };
    g_plat->pirq_init(&g_vgic, g_ncpu, fdt);    /* SPI policy first: vGIC effects reach it */
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
    int in_gic = (ipa >= g_plat->vgic_gicd_base && ipa < g_plat->vgic_gicd_base + GICD_SIZE) ||
                 (ipa >= g_plat->vgic_gicr_base && ipa < g_plat->vgic_gicr_base + 8 * VGIC_GICR_FRAME_SIZE);
    int in_v2m = g_v2m_on && v2m_contains(&g_v2m, ipa);        /* #74: the whole 16 KB page */

    if (!isv || !(in_gic || in_v2m) || (dfsc >> 2) != 1 /* translation fault, any level */) {
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
        rc = in_v2m ? v2m_mmio_write(&g_v2m, ipa, size, val) : vgic_mmio_write(&g_vgic, ipa, size, val);
    } else {
        rc = in_v2m ? v2m_mmio_read(&g_v2m, ipa, size, &val) : vgic_mmio_read(&g_vgic, ipa, size, &val);
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
        unsigned n = in_v2m ? 0 : ipa < g_plat->vgic_gicd_base + GICD_SIZE ? ++n_gicd : ++n_gicr;   /* v2m: no progress line */

        for (unsigned p = 1; p <= 1000000; p *= 10) {
            if (n == p) {
                put_kv(ipa < g_plat->vgic_gicd_base + GICD_SIZE ? "Watari EL2: vGIC GICD traps handled = "
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
    } else if (idx == 8 && ec == EC_SYSREG && idreg_trap(f)) {
        /* T6g: a read of the ID group 3 (HCR_EL2.TID3), decoded first: it is cheap and touches no ICH register */
    } else if (idx == 8 && ec == EC_SYSREG && cpuif_sysreg_trap(f)) {
        /* ICC_SGI*R_EL1 write: the SGI is now pending in the model */
    } else if (idx == 10 && g_plat->fiq) {
        g_plat->fiq(f);                      /* T6e: the Apple FIQ (timer, IPI, PMU); QEMU has none: fatal below */
        cpuif_flush();                       /* the FIQ may have raised a vGIC line (timer, IPI): deliver it */
        cpuif_maint_backstop();              /* the maintenance signal may be the FIQ (T6e D-1) */
        cpuif_kick_others();
        big_unlock();
        return;
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
/* T7: wel2.guest=1|<hex entry>. The first guest is the EL1 test guest (el2/guest), not EDK2: x0 is the guest info block
 * (wel2_guest_info.h) instead of the FDT, and the entry is EDK2_ENTRY (value 1) or the given address. */
static bool g_guest_mode;
static uint64_t g_guest_entry;


/* The block is written at guest-physical address `at` (QEMU: the end of the hand-off buffer, 4 KB below the layer; M1:
 * the first 4 KB boundary above the guest image, wel2_guest_place.h): guest RAM the guest can read, within 64 MB of the
 * guest image (which it checks before it dereferences x0). */
static uint64_t write_guest_info(uint64_t at)
{
    struct wel2_guest_info *gi = (struct wel2_guest_info *)(uintptr_t)at;
    const char *name = g_plat->name;

    /* The guest must be able to read it: inside guest RAM, never a pointer into the layer's carve-out. */
    if (g_plat->m1_memory && (!guest_ram_contains(at) || !guest_ram_contains(at + sizeof(*gi) - 1)))
        fatal_msg("guest info block does not lie in guest RAM");
    memset(gi, 0, sizeof(*gi));
    gi->magic = WEL2_GINFO_MAGIC;
    gi->version = WEL2_GINFO_VERSION;
    gi->size = sizeof(*gi);
    for (unsigned i = 0; name[i] && i < sizeof(gi->platform) - 1; i++)
        gi->platform[i] = name[i];
    gi->gicd_base = g_plat->vgic_gicd_base;
    gi->gicr_base = g_plat->vgic_gicr_base;
    gi->gicr_stride = 0x20000;
    gi->ncores = g_ncpu < WEL2_GINFO_MAX_CORES ? g_ncpu : WEL2_GINFO_MAX_CORES;
    /* Hardware ladder (DESIGN 9): L3 = G1-G6, L4 = G1-G8, L5 and no step = all (G11, the final reset, always runs). */
    gi->checks = g_step == 3 ? (2u << WEL2_CHK_G6) - 2u : g_step == 4 ? (2u << WEL2_CHK_G8) - 2u : 0;
    gi->cpuif_mode = cpuif_mode();
    /* PAuth only when the layer really lets the guest use the keys; PPI 30 only where the platform raises it. */
    if ((g_hcr & (HCR_API | HCR_APK)) == (HCR_API | HCR_APK))
        gi->flags |= WEL2_GINFO_F_PAUTH;
    if (!g_plat->timer_resync)
        gi->flags |= WEL2_GINFO_F_NO_PTIMER;
    for (unsigned i = 0; i < gi->ncores; i++)
        gi->mpidr[i] = ((uint64_t)(g_pcpu[i].aff >> 24) << 32) | (g_pcpu[i].aff & 0xffffff);
    dcache_clean_range(gi, sizeof(*gi));
    return (uint64_t)(uintptr_t)gi;
}

/* M1-4: hand the machine to EDK2 (ArmVirtQemuKernel) at EL1, the way a boot loader
 * hands over to an arm64 Linux kernel (Linux arm64 booting.rst, entry state):
 *   x0 = physical address of the FDT, x1 = x2 = x3 = 0, MMU off, D-cache off,
 *   PSTATE.DAIF masked, the image 4 KB aligned at its load address.
 * Differences from the plain protocol: EDK2 is entered at EL1 (the layer owns EL2),
 * and the FDT is patched first (fdt.c): /memory loses the layer's carve-out, the PSCI
 * conduit becomes "hvc". The entry is the start of the EDK2 image, whose header
 * (code0 = "adr x1, .") tells the relocatable PrePi where it was loaded. */
/* TODO(T12, EDK2 hand-off on the M1; the test guest's M1 hand-off is m1_enter_guest, T7b): fdt_patch_for_guest(), EDK2_FDT_ADDR (= g_carve_base) and EDK2_ENTRY (= carve end + 0x80000)
 * are QEMU conventions (RAM starts at the carve-out, the FDT is copied to the RAM base, EDK2 sits behind a 2 MB block).
 * They do not describe the M1, where the carve-out is a 32 MB block inside the RAM and the guest sees RAM ranges
 * (g_guest_ram[]); the chain build is not an M1 build until that hand-off is redone. */
/* T7b: the EL1 test guest on the M1. No FDT is patched or copied (the guest needs only the info block; the EDK2 guest FDT is
 * T12). The host loader put the guest image at the start of its own 32 MB block inside guest RAM and passed
 * wel2.guest=0x<entry>; the layer reads the arm64 Image header at the entry, checks the placement (wel2_guest_place.h:
 * image inside ONE guest RAM range, 4 KB aligned entry, room for the info block above the image), writes the info block,
 * and enters the guest at EL1 with x0 = the block. Secondaries are started by the guest's own PSCI CPU_ON through the
 * M1 path. */
static void __attribute__((noreturn)) m1_enter_guest(void)
{
    struct wel2_guest_place gp;

    if (!g_guest_mode || !g_guest_entry)
        fatal_msg("M1: wel2.guest=0x<entry> is required (the EDK2 hand-off is T12)");
    uint64_t entry = g_guest_entry;

    if (!guest_ram_contains(entry) || (entry & 0xfff))
        fatal_msg("wel2.guest: entry is not 4 KB aligned guest RAM");
    hvc_clean_inv(entry, 0x40);                          /* the loader wrote it through the proxy, past our caches */
    if (*(volatile uint32_t *)(uintptr_t)(entry + 0x38) != 0x644d5241)
        fatal_msg("wel2.guest: no arm64 Image header at the guest entry");
    uint64_t image_size = *(volatile uint64_t *)(uintptr_t)(entry + 0x10);
    int rc = wel2_guest_place(g_guest_ram, g_nguest_ram, entry, image_size, sizeof(struct wel2_guest_info), &gp);

    if (rc != WEL2_GP_OK) {
        static const char *const why[] = {
            "ok", "wel2.guest: bad entry", "wel2.guest: bad image_size in the Image header (0 or above 32 MB)",
            "wel2.guest: entry is not in guest RAM", "wel2.guest: the image does not end inside the RAM range it starts in",
            "wel2.guest: no room for the info block above the guest image",
        };
        fatal_msg(why[(unsigned)rc < sizeof(why) / sizeof(why[0]) ? (unsigned)rc : 0]);
    }
    uint64_t info = write_guest_info(gp.info);

    put_kv("Watari EL2: M1 guest image at ", gp.entry);
    put_kv("Watari EL2:   image end = ", gp.image_end);
    put_kv("Watari EL2: guest info block at ", info);
    layer_checkpoint(WEL2_LCHK_ENTER_GUEST, "ENTER_GUEST");
    put_kv("Watari EL2: eret to the EL1 test guest, entry = ", entry);
    enter_el1(entry, 0, SPSR_EL1H_MASKED, g_hcr, info);
}

static void __attribute__((noreturn)) chain_to_edk2(uint64_t fdt)
{
    struct fdt_patch_info pi;

    /* EDK2's ArmGicV3 driver never does the GICR_WAKER handshake (it expects awake
     * redistributors, as left by TF-A or QEMU); a vCPU asleep in the model forwards
     * nothing and the first timer tick would be lost for good (F-21). The boot vCPU
     * (here) and each vCPU PSCI starts (el2_secondary_main) are handed over awake. */
    vgic_vcpu_wake(&g_vgic, this_cpu());
    if (g_plat->m1_memory)
        m1_enter_guest();
    put_kv("Watari EL2: FDT from QEMU at ", fdt);
    if (!fdt || (fdt & 7) || fdt_check((void *)fdt))
        fatal_msg("x0 does not point to a valid FDT (is QEMU -kernel used?)");
    if (fdt_patch_for_guest((void *)fdt, g_carve_base, g_carve_size, g_ram_from_fdt ? g_ram_end : 0, &pi))
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
    if (total > ((uint64_t)(uintptr_t)__image_start - EDK2_FDT_ADDR - (g_guest_mode ? 0x1000 : 0)) || fdt < pi.mem_base_new)
        fatal_msg("FDT does not fit the hand-off buffer, or sits inside the layer's region");
    memcpy((void *)EDK2_FDT_ADDR, (void *)fdt, total);
    fdt_set_totalsize((void *)EDK2_FDT_ADDR, total);
    /* EL2 wrote through its cacheable map; the guest starts with MMU and caches off. */
    dcache_clean_range((void *)EDK2_FDT_ADDR, total);
    put_kv("Watari EL2: patched FDT copied to ", EDK2_FDT_ADDR);
    if (g_guest_mode) {
        uint64_t entry = g_guest_entry ? g_guest_entry : EDK2_ENTRY;

        if (!guest_ram_contains(entry) || (entry & 0xfff))
            fatal_msg("wel2.guest: entry is not 4 KB aligned guest RAM");
        if (*(volatile uint32_t *)(entry + 0x38) != 0x644d5241)
            fatal_msg("wel2.guest: no arm64 Image header at the guest entry");
        uint64_t info = write_guest_info((uint64_t)(uintptr_t)__image_start - 0x1000);

        put_kv("Watari EL2: guest info block at ", info);
        layer_checkpoint(WEL2_LCHK_ENTER_GUEST, "ENTER_GUEST");
        put_kv("Watari EL2: eret to the EL1 test guest, entry = ", entry);
        enter_el1(entry, 0, SPSR_EL1H_MASKED, g_hcr, info);
    }
    uint32_t magic = *(volatile uint32_t *)(EDK2_ENTRY + 0x38);
    if (magic != 0x644d5241) {               /* "ARM\x64" */
        put_kv("Watari EL2: no arm64 Image header at EDK2 entry ", EDK2_ENTRY);
        put_kv("Watari EL2: found ", magic);
        fatal_msg("EDK2 image missing: add -device loader,file=QEMU_EFI-kernel.fd,addr=<carve end + 0x80000, 0x40280000 on QEMU>,force-raw=on");
    }
    layer_checkpoint(WEL2_LCHK_ENTER_GUEST, "ENTER_GUEST");
    put_kv("Watari EL2: eret to EDK2 at EL1, entry = ", EDK2_ENTRY);
    enter_el1(EDK2_ENTRY, 0, SPSR_EL1H_MASKED, g_hcr, EDK2_FDT_ADDR);
}
#endif

/* Boot arguments (/chosen/bootargs, QEMU -append), space separated:
 *   wel2.cpuif=hw|trap|trap-notc   mode of the virtual CPU interface (build default WEL2_CPUIF_TRAP_DEFAULT);
 *                        trap-notc = trap without ICH_HCR_EL2.TC (T6g). The M1 accepts trap and trap-notc only.
 *   wel2.hold=1          do not reset at the end of a step or of the guest (wel2.step=2, the guest's SYSTEM_RESET): paint the final
 *                        state, print the HOLD message and park, so the screen can be photographed (not wel2.step=1)
 *   wel2.tid3=1|0        HCR_EL2.TID3: emulate the ID register group, GIC forced to 1 (default: on on the M1, off on QEMU)
 *   wel2.step=N          hardware ladder rung (DESIGN 9), read by stage1.c; 1 resets after the relocation, 2 after checkpoint 5 (CPU_INIT),
 *                        3..5 select the guest's checks
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
        if (arg_is(p, "wel2.cpuif=trap")) {
            g_cpuif_trap = true;
            g_cpuif_notc = false;
        } else if (arg_is(p, "wel2.cpuif=trap-notc")) {
            g_cpuif_trap = true;
            g_cpuif_notc = true;
        } else if (arg_is(p, "wel2.hold=1")) {
            g_hold = true;
        } else if (arg_is(p, "wel2.tid3=1")) {
            g_tid3 = true;
        } else if (arg_is(p, "wel2.tid3=0")) {
            g_tid3 = false;
        } else if (arg_is(p, "wel2.test=tcoff")) {
            g_test_tc = -1;
        } else if (arg_is(p, "wel2.test=tcon")) {
            g_test_tc = 1;
        } else if (arg_is(p, "wel2.lpi=off")) {
            g_lpi = false;
        } else if (arg_is(p, "wel2.aic=off")) {
            g_aic_allowed = false;
        } else if (arg_is(p, "wel2.cpuif=hw")) {
            /* DESIGN 5.4 / PLAN S12b: the hardware list-register mode is QEMU-only (the M1's ICH_VTR_EL2.SEIS = 1 erratum):
             * refused on the M1 with a log line, the mode stays trap. */
            if (g_plat->m1_memory)
                puts_("Watari EL2: wel2.cpuif=hw is refused on the M1 (SEIS erratum): the cpuif mode stays trap\n");
            else {
                g_cpuif_trap = false;
                g_cpuif_notc = false;
            }
        } else if (p[0] == 'w' && p[1] == 'e' && p[2] == 'l' && p[3] == '2' && p[4] == '.' && p[5] == 'c' &&
                   p[6] == 'p' && p[7] == 'u' && p[8] == 'i' && p[9] == 'f' && p[10] == '=') {
            /* trap, trap-notc and hw matched above; anything else is not silently ignored */
            puts_("Watari EL2: WARNING: unknown boot argument value, ignored: ");
            for (const char *q = p; *q && *q != ' '; q++)
                putc_(*q);
            puts_("\n");
        } else if (p[0] == 'w' && p[1] == 'e' && p[2] == 'l' && p[3] == '2' && p[4] == '.' && p[5] == 't' &&
                 p[6] == 'r' && p[7] == 'a' && p[8] == 'c' && p[9] == 'e' && p[10] == '=') {
            g_trace_on = true;
            g_trace_skip = 0;
            for (const char *q = p + 11; *q >= '0' && *q <= '9'; q++)
                g_trace_skip = g_trace_skip * 10 + (unsigned)(*q - '0');
        } else if (arg_is(p, "wel2.test=noeoi")) {
            g_cpuif_noeoi = true;
#ifdef WEL2_CHAIN
        } else if (p[0] == 'w' && p[1] == 'e' && p[2] == 'l' && p[3] == '2' && p[4] == '.' && p[5] == 'g' &&
                   p[6] == 'u' && p[7] == 'e' && p[8] == 's' && p[9] == 't' && p[10] == '=') {
            uint64_t v = 0;
            const char *q = p + 11;

            if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X'))
                q += 2;
            for (; (*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'f') || (*q >= 'A' && *q <= 'F'); q++)
                v = v * 16 + (uint64_t)(*q <= '9' ? *q - '0' : (*q | 0x20) - 'a' + 10);
            g_guest_mode = true;
            g_guest_entry = v == 1 ? 0 : v;          /* 1 = on, default entry */
            put_kv("Watari EL2: wel2.guest parsed, value = ", v);
#endif
        }
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
    if (g_plat->m1_memory)
        put_kv("Watari EL2: platform = M1, guest RAM ranges = ", g_nguest_ram);
    put_kv("Watari EL2: layer image (PIE, relocated) at ", g_layer_base);
    put_kv("Watari EL2:   carve-out base = ", g_carve_base);
    put_kv("Watari EL2:   carve-out size = ", g_carve_size);
    put_kv(g_ram_from_fdt ? "Watari EL2: RAM from the FDT /memory node, base " : "Watari EL2: no usable FDT /memory node: default RAM, base ", g_ram_base);
    put_kv("Watari EL2:   RAM end = ", g_ram_end);
}

/* Does the CPU implement pointer authentication? ID_AA64ISAR1_EL1: APA [7:4] / API [11:8] (address, architected /
 * implementation defined algorithm), GPA [27:24] / GPI [31:28] (generic); ID_AA64ISAR2_EL1: GPA3 [11:8] / APA3 [15:12]
 * [hypothesis: Arm ARM field positions from memory; the guest's G3 reads the same fields]. DESIGN 5.1 decision: when
 * it does, the guest gets the keys and the instructions (HCR_EL2.API | APK), on every platform. */
static bool cpu_has_pauth(void)
{
    uint64_t isar1, isar2;

    __asm__ volatile("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
    __asm__ volatile("mrs %0, id_aa64isar2_el1" : "=r"(isar2));
    return ((isar1 >> 4) & 0xff) || ((isar1 >> 24) & 0xff) || ((isar2 >> 8) & 0xff);   /* ISAR2: GPA3 [11:8], APA3 [15:12] */
}

/* ---- EL2 main ---------------------------------------------------------------- */
void __attribute__((noreturn)) el2_main(uint64_t fdt)
{
    uint64_t el, vbar;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, vbar_el2" : "=r"(vbar));

    checkpoint(WEL2_LCHK_STAGE1_ON);    /* checkpoint 1: s1_enable returned, the MMU and caches are on (before any text) */
    puts_("Watari EL2: hello from EL2\n");
    if (g_con_s5l_base) {
        put_kv("Watari EL2: console = Apple S5L UART at ", g_con_s5l_base);
        put_kv("Watari EL2:   window size = ", g_con_s5l_size);
    }
    if (g_fb.fmt) {                     /* geometry only; nothing else of the FDT is ever printed */
        put_kv("Watari EL2: framebuffer at ", g_fb.base);
        put_kn("Watari EL2:   width  = ", g_fb.width, " pixels\n");
        put_kn("Watari EL2:   height = ", g_fb.height, " pixels\n");
        put_kn("Watari EL2:   stride = ", g_fb.stride, " bytes\n");
        puts_(g_fb.fmt == WEL2_FB_X2R10G10B10 ? "Watari EL2:   format = x2r10g10b10\n"
                                               : "Watari EL2:   format = x8r8g8b8\n");
    }
    stage1_report();
    put_kv("Watari EL2: CurrentEL = ", el >> 2);
    put_kv("Watari EL2: VBAR_EL2  = ", vbar);
    if (g_step) {
        put_kv("Watari EL2: boot argument wel2.step = ", g_step);
        if (g_step == 1)                /* stage1.c would have reset: no watchdog node on this machine, so stop here */
            fatal_msg("wel2.step=1: the early reset did not happen (no watchdog node in the FDT?)");
    }
    layer_checkpoint(WEL2_LCHK_RELOCATED, "RELOCATED");      /* 0 and 1 were painted before the first text; logged now */
    layer_checkpoint(WEL2_LCHK_STAGE1_ON, "STAGE1_ON");
    layer_checkpoint(WEL2_LCHK_BANNER, "BANNER");

    /* Cores = redistributor frames (GICR_TYPER walk); vCPU i = core i. */
    regime_init_cpu();
    g_ncpu = g_plat->discover_cpus();
    bind_this_cpu();
    put_kv("Watari EL2: cores (redistributors) = ", g_ncpu);
    for (unsigned i = 0; i < g_ncpu; i++)
        g_pcpu[i].state = CPU_ST_OFF;
    g_pcpu[this_cpu()].state = CPU_ST_ON;      /* the cold-boot core is ON */
    layer_checkpoint(WEL2_LCHK_DISCOVER, "DISCOVER");

    /* HCR_EL2: RW=1 (EL1 is AArch64), IMO/FMO/AMO=1 (physical IRQ/FIQ/SError
     * routed to EL2, as designed). E2H as stage1.c left it on this core (1 on the M1; the guest then runs
     * with TGE=0 at EL1, its exceptions and the HVC/SMC/IRQ paths are the same); VM=1 since M1-1 (stage-2
     * with the GIC windows unmapped). M1-4 chain build: TSC=1 (EL1 SMC trapped). */
    g_hcr = HCR_RW | HCR_IMO | HCR_FMO | HCR_AMO | HCR_VM | (g_e2h ? HCR_E2H : 0);
#ifdef WEL2_CHAIN
    g_hcr |= HCR_TSC;
#endif
    if (g_plat->m1_memory) {
        /* DESIGN 5.4 / 5.1: the M1 runs a trap mode (SEIS erratum: trap or trap-notc) and needs VHE (E2H stuck at 1). */
        if (!g_e2h)
            fatal_msg("M1 platform without HCR_EL2.E2H: the layer cannot run here");
        g_cpuif_trap = true;
        g_tid3 = true;                          /* DESIGN 5.4: EDK2's GIC code and Windows read ID_AA64PFR0_EL1.GIC; wel2.tid3=0 overrides */
    }
    parse_bootargs(fdt);
    /* T6g: pointer authentication (DESIGN 5.1) and the ID register group (DESIGN 5.4). */
    if (cpu_has_pauth()) {
        g_hcr |= HCR_API | HCR_APK;
        puts_("Watari EL2: CPU implements pointer authentication: HCR_EL2.API|APK set, the guest owns the keys\n");
    }
    if (g_tid3) {
        g_hcr |= HCR_TID3;
        puts_("Watari EL2: HCR_EL2.TID3 set: ID registers emulated, ID_AA64PFR0_EL1.GIC presented as 1\n");
    }
    vgic_setup(fdt);
    pirq_vcpu_on(&g_pirq, this_cpu());          /* the cold-boot core can take SPIs */
    layer_checkpoint(WEL2_LCHK_IRQ_READY, "IRQ_READY");
    /* PLAN S13: platform init (the interrupt controller's shared part, the Apple FIQ module / GIC cpu interface, the
     * WHOAMI check) comes before the guest stage 2 (it needs none of it; it only was written after it) = 5. */
    g_plat->init_boot();
    if (g_plat->init_cpu(this_cpu()))
        fatal_msg("AIC: WHOAMI of the boot core differs from its index");
    layer_checkpoint(WEL2_LCHK_CPU_INIT, "CPU_INIT");
    if (g_step == 2) {                          /* rung L2 (DESIGN 9): the layer alone, then the watchdog */
        puts_("Watari EL2: wel2.step=2: platform and interrupt controller are up, resetting the machine\n");
        layer_reset(false);                     /* wel2.hold=1: stay on the screen instead */
    }
    stage2_init();
    stage2_enable();
    set_virtual_ids();
    puts_("Watari EL2: stage-2 on (identity, GICD/GICR unmapped), vGIC ready\n");
    layer_checkpoint(WEL2_LCHK_STAGE2_ON, "STAGE2_ON");
    cpuif_init(&g_vgic);
    puts_("Watari EL2: physical GIC owned by EL2, virtual CPU interface on\n");
    layer_checkpoint(WEL2_LCHK_CPUIF, "CPUIF");
#ifdef WEL2_CHAIN
    chain_to_edk2(fdt);
#else
    (void)fdt;
    layer_checkpoint(WEL2_LCHK_ENTER_GUEST, "ENTER_GUEST");
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
    if (g_plat->init_cpu(me))
        fatal_msg("AIC: WHOAMI of a secondary core differs from its index");
    cpuif_init_cpu();

    big_lock();
#ifdef WEL2_CHAIN
    vgic_vcpu_wake(&g_vgic, me);               /* same hand-off contract as the boot vCPU */
#endif
    uint64_t entry = g_pcpu[me].entry, ctx = g_pcpu[me].ctx;
    if (g_plat->cpu_arrive && !g_plat->cpu_arrive(me, &entry, &ctx)) {
        big_unlock();                          /* T6f: a core nobody CPU_ON'ed (stray start) must not enter a guest */
        fatal_msg("secondary core started without a CPU_ON (stray start)");
    }
    g_pcpu[me].state = CPU_ST_ON;
    pirq_vcpu_on(&g_pirq, me);                 /* SPIs the guest routed here come back */
    put_kn("Watari EL2: cpu ", me, " up at EL2, eret to the guest entry point\n");
    big_unlock();
    /* No flush needed here: interrupts sent while ON_PENDING are delivered on
     * this core's first trap/IRQ, and the guest enables its CPU interface only
     * after it runs. */
    enter_el1(entry, 0, SPSR_EL1H_MASKED, g_hcr, ctx);
}
