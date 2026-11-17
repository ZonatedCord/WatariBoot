/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - Apple M1 system registers behind an accessor (PLAN S10, DESIGN 5.1/5.2).
 *
 * The FIQ module (vgic/apple_fiq.c) never executes mrs/msr itself: it calls rd()/wr() of a struct
 * apple_sysreg_io. On the M1 the table is `apple_sysreg_real` (boot/apple_sysreg.c, one small function per
 * register and direction, generic S3_x_Cx_Cx_x names so that the assembler needs no extension); the host test
 * (tests/fake_apple_sysreg.h) supplies a fake register file that records every access.
 *
 * Encodings (op0, op1, CRn, CRm, op2); sources are facts only (register numbers and bit meanings), no code reused:
 *   VM_TMR_FIQ_ENA_EL2  3,5,15,1,3  bit0 = V gate, bit1 = P gate  [verified: Linux v6.12 irq-apple-aic.c:183-185]
 *   IPI_RR_GLOBAL_EL1   3,5,15,0,1  [7:0] core, [23:16] cluster, [29:28] type  [verified: irq-apple-aic.c:168-176]
 *   IPI_SR_EL1          3,5,15,1,1  bit0 pending, write 1 clears  [verified: irq-apple-aic.c:179-180]
 *   PMCR0_EL1           3,1,15,0,0  IMODE [10:8], IACT bit 11  [verified: Linux v6.12 arch/arm64/include/asm/apple_m1_pmu.h:22-30]
 *   PMCR1_EL1           3,1,15,1,0  per-counter EL0/EL1 count enables: bit 8+n (A64 EL0), bit 16+n (A64 EL1) [hypothesis: task brief; no copy in our notes, measured by the layer's own init read-back]
 *   PMC0_EL1            3,2,15,0,0  IMPLEMENTATION DEFINED cycle counter of this core ("PMC0") [hypothesis: task brief; liveness is measured at run time, docs/design/watari-el2/pmc0-vpmu.md]
 *   UPMCR0_EL1          3,7,15,0,4  IMODE [18:16], 4 = FIQ  [verified: irq-apple-aic.c:191-196]
 *   UPMSR_EL1           3,7,15,6,4  bit0 IACT  [verified: irq-apple-aic.c:199-200]
 *   CNTV_CTL_EL02       3,5,14,3,1  [verified: Linux v6.12 arch/arm64/include/asm/sysreg.h:653; R:el2/boot/README.md]
 *   CNTP_CTL_EL02       3,5,14,2,1  [verified: Linux v6.12 sysreg.h:650] (the "by analogy" hypothesis of PLAN S10 is settled)
 *   CNTHV_CTL_EL2       3,4,14,3,1  EL2's own virtual timer (what CNTV_CTL_EL0 means at EL2 with E2H = 1)
 *   CNTHP_CTL_EL2       3,4,14,2,1  EL2's own physical timer (CNTP_CTL_EL0 with E2H = 1)
 *                       [verified: llvm-mc encodes 'mrs x0, cnthp_ctl_el2' / 'cnthv_ctl_el2' (+vh) as these; Arm ARM D8.13.2]
 *   ICH_HCR_EL2         3,4,12,11,0  [verified: Linux v6.12 sysreg.h:576]
 *   ICH_MISR_EL2        3,4,12,11,2  [verified: Linux v6.12 sysreg.h:578]
 *   HACR_EL2            3,4,1,1,7    [verified: llvm-mc encodes 'mrs x0, hacr_el2' as S3_4_C1_C1_7 = 0xd53c11e0; Arm ARM D24.2 HACR_EL2]. NOT
 *                       S3_4_C15_C1_7 (that one is another IMPLEMENTATION DEFINED register); the IMPLEMENTATION DEFINED part is the bit meaning.
 * The independent copy of this table that the build check compares the object code against is in
 * boot/check-apple-enc.sh (it does not include this header on purpose).
 */
#ifndef WEL2_APPLE_SYSREG_H
#define WEL2_APPLE_SYSREG_H

#include <stdint.h>

enum apple_sr {
	APPLE_SR_VM_TMR_FIQ_ENA,   /* VM_TMR_FIQ_ENA_EL2 */
	APPLE_SR_CNTV_CTL_EL02,    /* guest virtual timer control */
	APPLE_SR_CNTP_CTL_EL02,    /* guest (EL1) physical timer control */
	APPLE_SR_CNTHV_CTL,        /* EL2's own virtual timer control */
	APPLE_SR_CNTHP_CTL,        /* EL2's own physical timer control */
	APPLE_SR_IPI_SR,           /* IPI status (rd, wr) */
	APPLE_SR_IPI_RR_GLOBAL,    /* IPI request, global form (wr only) */
	APPLE_SR_PMCR0,
	APPLE_SR_UPMCR0,
	APPLE_SR_UPMSR,            /* rd only */
	APPLE_SR_ICH_HCR,
	APPLE_SR_ICH_MISR,         /* rd only */
	APPLE_SR_PMCR1,            /* PMCR1_EL1: per-counter EL0/EL1 enables */
	APPLE_SR_PMC0,             /* PMC0: the core's cycle counter (the layer never writes it) */
	APPLE_SR_HACR_EL2,         /* HACR_EL2 (architectural encoding, Apple-defined bits: bit 56 routes the PMUv3 accesses to EL2) */
	APPLE_SR_COUNT
};

#define VM_TMR_FIQ_V      (1ull << 0)
#define VM_TMR_FIQ_P      (1ull << 1)

#define CNT_CTL_ENABLE    (1ull << 0)
#define CNT_CTL_IMASK     (1ull << 1)
#define CNT_CTL_ISTATUS   (1ull << 2)
/* The timer interrupt line is asserted while ENABLE && ISTATUS && !IMASK (Arm ARM, generic timer). */
#define CNT_CTL_FIRING(c) (((c) & (CNT_CTL_ENABLE | CNT_CTL_ISTATUS | CNT_CTL_IMASK)) == (CNT_CTL_ENABLE | CNT_CTL_ISTATUS))

#define IPI_SR_PENDING    (1ull << 0)

#define PMCR0_IMODE_SHIFT 8
#define PMCR0_IMODE_MASK  (7ull << PMCR0_IMODE_SHIFT)
#define PMCR0_IMODE_FIQ   4ull
#define PMCR0_IACT        (1ull << 11)
#define PMCR0_CTR0_EN     (1ull << 0)                 /* counter 0 (cycles) enable [hypothesis] */
#define PMCR1_CTR0_EL0    (1ull << 8)                 /* counter 0 counts at EL0 (AArch64) [hypothesis] */
#define PMCR1_CTR0_EL1    (1ull << 16)                /* counter 0 counts at EL1 (AArch64) [hypothesis] */
#define UPMCR0_IMODE_SHIFT 16
#define UPMCR0_IMODE_MASK (7ull << UPMCR0_IMODE_SHIFT)
#define UPMCR0_IMODE_FIQ  4ull
#define UPMSR_IACT        (1ull << 0)

/* ICH_HCR_EL2: En [0]; the maintenance-cause enables UIE, LRENPIE, NPIE, VGrp0EIE, VGrp0DIE, VGrp1EIE, VGrp1DIE
 * are bits [7:1] [verified: Linux v6.12 include/linux/irqchip/arm-gic-v3.h ICH_HCR_* (the bit numbers as listed there)]. */
#define ICH_HCR_EN        (1ull << 0)
#define ICH_HCR_MAINT_EN  (0xFEull)

struct apple_sysreg_io {
	uint64_t (*rd)(void *ctx, enum apple_sr id);
	void     (*wr)(void *ctx, enum apple_sr id, uint64_t val);
	void     *ctx;
};

/* The real accessors (mrs/msr); defined in boot/apple_sysreg.c, AArch64 only. */
extern const struct apple_sysreg_io apple_sysreg_real;

#endif
