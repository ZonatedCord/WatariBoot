// SPDX-License-Identifier: MIT
// Watari EL2: bench-only "wel2.vpmu=undef". A trapped PMU access is NOT emulated but reflected to the guest as the exception the
// real M1 raises: a synchronous exception to EL1 with ESR_EL1.EC = 0x00 (Unknown reason), IL = 1, ISS = 0. QEMU with its normal
// PMU traps the access to EL2 (MDCR_EL2.TPM), and this turns it into the M1's behaviour, so an EL1 handler (the EDK2 safety net)
// is exercised on the bench. Written from the Arm ARM (DDI 0487) D1.10 "Exception entry" / D1.10.2 "Exception vector table":
//   - vector offset: 0x000 current EL with SP_EL0 (EL1t), 0x200 current EL with SP_ELx (EL1h), 0x400 lower EL using AArch64
//     (guest at EL0); the synchronous entry is the first of each group of four. AArch32 guests / EL2 sources are not injected.
//   - ESR_EL1 = EC<<26 | IL<<25 | ISS = 0x02000000; ELR_EL1 = the preferred return address = the address of the faulting
//     instruction (an UNDEFINED instruction is not skipped); SPSR_EL1 = PSTATE at the exception; FAR_EL1 is not written
//     (UNKNOWN for EC 0x00).
//   - PSTATE on entry to EL1 (D1.10.x): M = EL1h, DAIF = 1111, NZCV/SS/IL/BTYPE = 0, PAN = 1 unless SCTLR_EL1.SPAN = 1, UAO = 0,
//     DIT = SCTLR_EL1.DIT, SSBS = SCTLR_EL1.DSSBS. TCO is left 0 here [hypothesis: not modelled, harmless for the safety net].
//     [verified: from the Arm ARM text recalled, not re-read; the host test checks the arithmetic, not the ARM wording]
#include <stdbool.h>
#include <stdint.h>

#define INJ_SPSR_M_MASK  0xfULL
#define INJ_SPSR_DAIF    0x3c0ULL
#define INJ_SPSR_PAN     (1ULL << 22)
#define INJ_SPSR_DIT     (1ULL << 24)
#define INJ_SPSR_SSBS    (1ULL << 12)
#define INJ_SCTLR_SPAN   (1ULL << 23)
#define INJ_SCTLR_DIT    (1ULL << 24)
#define INJ_SCTLR_DSSBS  (1ULL << 44)
#define INJ_ESR_UNKNOWN  0x02000000ULL

// The vector offset for a guest PSTATE, or -1 when it cannot be injected (AArch32, EL2/EL3).
int wel2_undef_vector_offset(uint64_t spsr)
{
    if (spsr & 0x10)                     // M[4] = 1: AArch32
        return -1;
    switch (spsr & INJ_SPSR_M_MASK) {
    case 0x0: return 0x400;              // EL0t: lower EL, AArch64
    case 0x4: return 0x000;              // EL1t: current EL, SP_EL0
    case 0x5: return 0x200;              // EL1h: current EL, SP_ELx
    default:  return -1;
    }
}

// The SPSR_EL2 to return with (PSTATE the guest runs the vector with).
uint64_t wel2_undef_new_spsr(uint64_t sctlr_el1)
{
    uint64_t s = 0x5 | INJ_SPSR_DAIF;    // EL1h, all of DAIF masked, NZCV = 0

    if (!(sctlr_el1 & INJ_SCTLR_SPAN))
        s |= INJ_SPSR_PAN;
    if (sctlr_el1 & INJ_SCTLR_DIT)
        s |= INJ_SPSR_DIT;
    if (sctlr_el1 & INJ_SCTLR_DSSBS)
        s |= INJ_SPSR_SSBS;
    return s;
}

#ifndef WEL2_INJECT_HOST
#include "boot.h"

// Reflects the trapped access to guest EL1 as an UNDEFINED instruction. false = not injectable (the caller then answers it).
bool wel2_inject_undef(struct frame *f)
{
    uint64_t vbar, sctlr, esr = INJ_ESR_UNKNOWN;
    int off = wel2_undef_vector_offset(f->spsr);

    if (off < 0)
        return false;
    uint64_t hcr;

    // With HCR_EL2.E2H = 1 (the layer's regime, always on the M1) the *_EL1 names at EL2 are redirected to the EL2 registers:
    // the guest's EL1 registers are reached through the *_EL12 encodings (Arm ARM D8.13).
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
    if (hcr & (1ULL << 34)) {
        __asm__ volatile("mrs %0, S3_5_C12_C0_0" : "=r"(vbar));       // VBAR_EL12
        __asm__ volatile("mrs %0, S3_5_C1_C0_0" : "=r"(sctlr));       // SCTLR_EL12
        __asm__ volatile("msr S3_5_C5_C2_0, %0" : : "r"(esr));        // ESR_EL12
        __asm__ volatile("msr S3_5_C4_C0_1, %0" : : "r"(f->elr));     // ELR_EL12
        __asm__ volatile("msr S3_5_C4_C0_0, %0" : : "r"(f->spsr));    // SPSR_EL12
    } else {
        __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        __asm__ volatile("msr esr_el1, %0" : : "r"(esr));
        __asm__ volatile("msr elr_el1, %0" : : "r"(f->elr));
        __asm__ volatile("msr spsr_el1, %0" : : "r"(f->spsr));
    }
    {
        static int n;                    // the first 4 injections: where from, where to (bench diagnostics)

        if (n++ < 4) {
            put_kv("Watari EL2: inject UNDEF: pc = ", f->elr);
            put_kv("Watari EL2: inject UNDEF: spsr = ", f->spsr);
            put_kv("Watari EL2: inject UNDEF: VBAR_EL1 = ", vbar);
        }
    }
    f->spsr = wel2_undef_new_spsr(sctlr);
    f->elr = (vbar & ~0x7ffULL) + (uint64_t)off;
    return true;
}
#endif
