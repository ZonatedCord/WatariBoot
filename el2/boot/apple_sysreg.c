// SPDX-License-Identifier: MIT
/*
 * Watari EL2 - the real Apple system-register accessors (PLAN S10). One small function per register and
 * direction, named apple_sr_rd_<reg> / apple_sr_wr_<reg>, each containing exactly one mrs or msr with the generic
 * S3_<op1>_C<n>_C<m>_<op2> name. boot/check-apple-enc.sh disassembles this object and compares every function's
 * instruction word with an independent table (review L-5, risk R-17): a mistyped encoding fails the build.
 * Encodings and sources: include/wel2_apple_sysreg.h.
 *
 * Not yet linked into the layer image (T6c adds it to boot/Makefile OBJS); it builds and is checked on its own.
 */
#include "wel2_apple_sysreg.h"

#define SR_RD(name, enc) \
	__attribute__((noinline)) uint64_t apple_sr_rd_##name(void) { uint64_t v; __asm__ volatile("mrs %0, " enc : "=r"(v)); return v; }
/* The isb makes a write take effect before the next instruction (the gate writes need it: DESIGN 5.2). */
#define SR_WR(name, enc) \
	__attribute__((noinline)) void apple_sr_wr_##name(uint64_t v) { __asm__ volatile("msr " enc ", %0\n\tisb" : : "r"(v) : "memory"); }

SR_RD(vm_tmr_fiq_ena, "S3_5_C15_C1_3")
SR_WR(vm_tmr_fiq_ena, "S3_5_C15_C1_3")
SR_RD(cntv_ctl_el02,  "S3_5_C14_C3_1")
SR_WR(cntv_ctl_el02,  "S3_5_C14_C3_1")
SR_RD(cntp_ctl_el02,  "S3_5_C14_C2_1")
SR_WR(cntp_ctl_el02,  "S3_5_C14_C2_1")
SR_RD(cnthv_ctl,      "S3_4_C14_C3_1")
SR_WR(cnthv_ctl,      "S3_4_C14_C3_1")
SR_RD(cnthp_ctl,      "S3_4_C14_C2_1")
SR_WR(cnthp_ctl,      "S3_4_C14_C2_1")
SR_RD(ipi_sr,         "S3_5_C15_C1_1")
SR_WR(ipi_sr,         "S3_5_C15_C1_1")
SR_WR(ipi_rr_global,  "S3_5_C15_C0_1")      /* write only */
SR_RD(pmcr0,          "S3_1_C15_C0_0")
SR_WR(pmcr0,          "S3_1_C15_C0_0")
SR_RD(upmcr0,         "S3_7_C15_C0_4")
SR_WR(upmcr0,         "S3_7_C15_C0_4")
SR_RD(upmsr,          "S3_7_C15_C6_4")      /* read only */
SR_RD(ich_hcr,        "S3_4_C12_C11_0")
SR_WR(ich_hcr,        "S3_4_C12_C11_0")
SR_RD(ich_misr,       "S3_4_C12_C11_2")     /* read only */

uint64_t apple_sr_rd_vm_tmr_fiq_ena(void);
uint64_t apple_sr_rd_cntv_ctl_el02(void);
uint64_t apple_sr_rd_cntp_ctl_el02(void);
uint64_t apple_sr_rd_cnthv_ctl(void);
uint64_t apple_sr_rd_cnthp_ctl(void);
uint64_t apple_sr_rd_ipi_sr(void);
uint64_t apple_sr_rd_pmcr0(void);
uint64_t apple_sr_rd_upmcr0(void);
uint64_t apple_sr_rd_upmsr(void);
uint64_t apple_sr_rd_ich_hcr(void);
uint64_t apple_sr_rd_ich_misr(void);

static uint64_t real_rd(void *ctx, enum apple_sr id)
{
	(void)ctx;
	switch (id) {
	case APPLE_SR_VM_TMR_FIQ_ENA: return apple_sr_rd_vm_tmr_fiq_ena();
	case APPLE_SR_CNTV_CTL_EL02:  return apple_sr_rd_cntv_ctl_el02();
	case APPLE_SR_CNTP_CTL_EL02:  return apple_sr_rd_cntp_ctl_el02();
	case APPLE_SR_CNTHV_CTL:      return apple_sr_rd_cnthv_ctl();
	case APPLE_SR_CNTHP_CTL:      return apple_sr_rd_cnthp_ctl();
	case APPLE_SR_IPI_SR:         return apple_sr_rd_ipi_sr();
	case APPLE_SR_PMCR0:          return apple_sr_rd_pmcr0();
	case APPLE_SR_UPMCR0:         return apple_sr_rd_upmcr0();
	case APPLE_SR_UPMSR:          return apple_sr_rd_upmsr();
	case APPLE_SR_ICH_HCR:        return apple_sr_rd_ich_hcr();
	case APPLE_SR_ICH_MISR:       return apple_sr_rd_ich_misr();
	default:                      return 0;     /* IPI_RR_GLOBAL is write-only */
	}
}

static void real_wr(void *ctx, enum apple_sr id, uint64_t v)
{
	(void)ctx;
	switch (id) {
	case APPLE_SR_VM_TMR_FIQ_ENA: apple_sr_wr_vm_tmr_fiq_ena(v); break;
	case APPLE_SR_CNTV_CTL_EL02:  apple_sr_wr_cntv_ctl_el02(v); break;
	case APPLE_SR_CNTP_CTL_EL02:  apple_sr_wr_cntp_ctl_el02(v); break;
	case APPLE_SR_CNTHV_CTL:      apple_sr_wr_cnthv_ctl(v); break;
	case APPLE_SR_CNTHP_CTL:      apple_sr_wr_cnthp_ctl(v); break;
	case APPLE_SR_IPI_SR:         apple_sr_wr_ipi_sr(v); break;
	case APPLE_SR_IPI_RR_GLOBAL:  apple_sr_wr_ipi_rr_global(v); break;
	case APPLE_SR_PMCR0:          apple_sr_wr_pmcr0(v); break;
	case APPLE_SR_UPMCR0:         apple_sr_wr_upmcr0(v); break;
	case APPLE_SR_ICH_HCR:        apple_sr_wr_ich_hcr(v); break;
	default:                      break;        /* UPMSR and ICH_MISR are read-only */
	}
}

const struct apple_sysreg_io apple_sysreg_real = { real_rd, real_wr, 0 };
