// SPDX-License-Identifier: MIT
// Watari EL2 M1-3: guest PSCI (Arm DEN0022F.b, version 1.1) over HVC.
// Written from the specification: function ids (Table 3 / section 5.1.1),
// return codes (5.1.x), CPU_OFF 5.5, CPU_ON 5.6, AFFINITY_INFO 5.7,
// MIGRATE_INFO_TYPE 5.9, SYSTEM_OFF 5.10, PSCI_FEATURES 5.16, entry state 6.4.3.
//
// The guest runs at EL1 and calls with HVC (INTERFACES section 9). Physical
// cores are started and stopped through the firmware conduit: QEMU virt with
// virtualization=on implements PSCI behind SMC, and EL2 is the caller.
//
// Core numbering: vCPU index == physical core index (g_pcpu[]). The MPIDR the
// guest sees (VMPIDR_EL2) equals the physical one, so target_cpu is passed
// through to the firmware unchanged once validated.

#include "boot.h"

#define PSCI_VERSION          0x84000000u
#define PSCI_CPU_SUSPEND_32   0x84000001u
#define PSCI_CPU_SUSPEND_64   0xC4000001u
#define PSCI_CPU_OFF          0x84000002u
#define PSCI_CPU_ON_32        0x84000003u
#define PSCI_CPU_ON_64        0xC4000003u
#define PSCI_AFFINITY_INFO_32 0x84000004u
#define PSCI_AFFINITY_INFO_64 0xC4000004u
#define PSCI_MIGRATE_INFO_TYPE 0x84000006u
#define PSCI_SYSTEM_OFF       0x84000008u
#define PSCI_SYSTEM_RESET     0x84000009u
#define PSCI_FEATURES         0x8400000Au

#define PSCI_VER_1_1          0x00010001u
#define PSCI_SUCCESS          0LL
#define PSCI_NOT_SUPPORTED    (-1LL)
#define PSCI_INVALID_PARAMETERS (-2LL)
#define PSCI_DENIED           (-3LL)
#define PSCI_ALREADY_ON       (-4LL)
#define PSCI_ON_PENDING       (-5LL)
#define PSCI_INTERNAL_FAILURE (-6LL)
#define PSCI_INVALID_ADDRESS  (-9LL)

/* Guest RAM = the Normal stage-2 blocks (main.c, boot.h). */

/* MPIDR as used by CPU_ON / AFFINITY_INFO (section 5.1.4): Aff3 [39:32],
 * Aff2 [23:16], Aff1 [15:8], Aff0 [7:0]; all other bits must be zero. */
#define PSCI_MPIDR_MASK 0x000000ff00ffffffULL

int64_t psci_smc(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3)
{
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;

    __asm__ volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    return (int64_t)x0;
}

void psci_system_off(void)
{
    psci_smc(PSCI_SYSTEM_OFF, 0, 0, 0);
    fatal_msg("SYSTEM_OFF returned");
}

static int64_t cpu_on(uint64_t target, uint64_t entry, uint64_t ctx, bool is64)
{
    if (!is64) {                                   /* SMC32 convention: 32-bit args */
        target &= 0xffffffffULL;
        entry &= 0xffffffffULL;
        ctx &= 0xffffffffULL;
    }
    if (target & ~PSCI_MPIDR_MASK)
        return PSCI_INVALID_PARAMETERS;
    unsigned idx = cpu_from_mpidr(target);
    if (idx == ~0u)
        return PSCI_INVALID_PARAMETERS;
    /* Entry point: a 4-byte aligned address inside guest RAM, else the
     * implementation knows it is unavailable to the caller (5.6.2). */
    if ((entry & 3) || entry < GUEST_RAM_BASE || entry >= GUEST_RAM_END)
        return PSCI_INVALID_ADDRESS;
    if (g_pcpu[idx].state == CPU_ST_ON)
        return PSCI_ALREADY_ON;
    if (g_pcpu[idx].state == CPU_ST_ON_PENDING)
        return PSCI_ON_PENDING;

    g_pcpu[idx].entry = entry;
    g_pcpu[idx].ctx = ctx;
    g_pcpu[idx].state = CPU_ST_ON_PENDING;

    /* Physical power-on through the firmware: the core starts at EL2 in
     * secondary_entry (start.S) and ends in el2_secondary_main(). A core that
     * went through CPU_OFF may still be shutting down: retry briefly on the
     * firmware's ALREADY_ON. */
    int64_t r = PSCI_ALREADY_ON;
    for (unsigned tries = 0; r == PSCI_ALREADY_ON && tries < 100000; tries++)
        r = psci_smc(PSCI_CPU_ON_64, target, (uint64_t)(uintptr_t)secondary_entry, 0);
    if (r != PSCI_SUCCESS) {
        g_pcpu[idx].state = CPU_ST_OFF;
        put_kv("Watari EL2: firmware CPU_ON failed, ret = ", (uint64_t)r);
        return PSCI_INTERNAL_FAILURE;
    }
    put_kn("Watari EL2: PSCI CPU_ON cpu ", idx, " accepted\n");
    return PSCI_SUCCESS;
}

/* CPU_OFF: the calling core powers itself down and does not return. DENIED for
 * the last core that is ON: the spec allows powering the system's last core
 * down, but nothing would ever wake it (F-14: no silent hang), so the guest
 * must use SYSTEM_OFF instead. */
static int64_t cpu_off(void)
{
    unsigned me = this_cpu(), others = 0;

    for (unsigned i = 0; i < g_ncpu; i++)
        if (i != me && g_pcpu[i].state != CPU_ST_OFF)
            others++;
    if (!others)
        return PSCI_DENIED;

    put_kn("Watari EL2: PSCI CPU_OFF cpu ", me, "\n");
    vgic_vcpu_reset(&g_vgic, me);      /* redistributor asleep, private IRQs dropped */
    uint32_t moved = g_pirq.st.retargets;

    pirq_vcpu_off(&g_pirq, me);        /* SPIs routed here move to an online core first */
    put_kv("Watari EL2: SPIs re-routed away from the core going off = ", g_pirq.st.retargets - moved);
    cpuif_cpu_down();
    gic_phys_cpu_down();
    g_pcpu[me].state = CPU_ST_OFF;
    big_unlock();                       /* this core never comes back to release it */
    __asm__ volatile("dsb sy" ::: "memory");
    int64_t r = psci_smc(PSCI_CPU_OFF, 0, 0, 0);
    put_kv("Watari EL2: firmware CPU_OFF returned, ret = ", (uint64_t)r);
    fatal_msg("CPU_OFF returned");
}

static int64_t affinity_info(uint64_t target, uint64_t level, bool is64)
{
    if (!is64) {
        target &= 0xffffffffULL;
        level &= 0xffffffffULL;
    }
    /* Levels above 0 are optional since PSCI 1.0 (5.7.1): not supported. */
    if (level != 0 || (target & ~PSCI_MPIDR_MASK))
        return PSCI_INVALID_PARAMETERS;
    unsigned idx = cpu_from_mpidr(target);
    if (idx == ~0u)
        return PSCI_INVALID_PARAMETERS;
    return g_pcpu[idx].state;           /* ON 0, OFF 1, ON_PENDING 2 */
}

static int64_t features(uint32_t fid)
{
    switch (fid) {
    case PSCI_VERSION:
    case PSCI_CPU_OFF:
    case PSCI_CPU_ON_32:
    case PSCI_CPU_ON_64:
    case PSCI_AFFINITY_INFO_32:
    case PSCI_AFFINITY_INFO_64:
    case PSCI_MIGRATE_INFO_TYPE:
    case PSCI_SYSTEM_OFF:
    case PSCI_SYSTEM_RESET:
    case PSCI_FEATURES:
        return 0;                       /* implemented, no feature flags */
    default:
        return PSCI_NOT_SUPPORTED;
    }
}

bool psci_handle(struct frame *f, uint32_t fid)
{
    int64_t ret;

    switch (fid) {
    case PSCI_VERSION:
        ret = PSCI_VER_1_1;
        break;
    case PSCI_CPU_ON_64:
        ret = cpu_on(f->x[1], f->x[2], f->x[3], true);
        break;
    case PSCI_CPU_ON_32:
        ret = cpu_on(f->x[1], f->x[2], f->x[3], false);
        break;
    case PSCI_CPU_OFF:
        ret = cpu_off();                /* returns only on error */
        break;
    case PSCI_AFFINITY_INFO_64:
        ret = affinity_info(f->x[1], f->x[2], true);
        break;
    case PSCI_AFFINITY_INFO_32:
        ret = affinity_info(f->x[1], f->x[2], false);
        break;
    case PSCI_MIGRATE_INFO_TYPE:
        ret = 2;                        /* MP or no Trusted OS: nothing to migrate (5.9.1) */
        break;
    case PSCI_FEATURES:
        ret = features((uint32_t)f->x[1]);
        break;
    case PSCI_SYSTEM_OFF:
        cpuif_stats();
        puts_("Watari EL2: PSCI SYSTEM_OFF -> forwarding to QEMU firmware (SMC)\n");
        psci_system_off();
    case PSCI_SYSTEM_RESET:                /* 5.11: EDK2 ResetSystem(); M1-4 */
        cpuif_stats();
        puts_("Watari EL2: PSCI SYSTEM_RESET -> forwarding to QEMU firmware (SMC)\n");
        psci_smc(PSCI_SYSTEM_RESET, 0, 0, 0);
        fatal_msg("SYSTEM_RESET returned");
    case PSCI_CPU_SUSPEND_32:
    case PSCI_CPU_SUSPEND_64:
    default:
        ret = PSCI_NOT_SUPPORTED;
        break;
    }
    /* SMCCC: result in x0, x1-x3 zeroed (INTERFACES section 9). */
    f->x[0] = (uint64_t)ret;
    f->x[1] = f->x[2] = f->x[3] = 0;
    return true;
}
