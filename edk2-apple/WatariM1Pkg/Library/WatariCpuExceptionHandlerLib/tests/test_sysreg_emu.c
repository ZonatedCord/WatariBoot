// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test of WatariSysregEmu.h. Words are built from the Arm ARM C5.2 fields, not taken from any binary.
#define WATARI_SYSREG_HOST
#include <stdio.h>
#include "../WatariSysregEmu.h"

static int fails, checks;
static WATARI_PMU pmu;
static UINT64 now_cnt;
#define F 24000000ULL
static BOOLEAN EMU(UINT32 w, UINT64 *x, UINT64 *sp, WATARI_SYSREG *r) { return WatariSysregEmulate(w, x, sp, &pmu, now_cnt, F, r); }
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static UINT32 enc(int read, UINT32 op0, UINT32 op1, UINT32 crn, UINT32 crm, UINT32 op2, UINT32 rt)
{
  return (read ? 0xD5300000U : 0xD5100000U) | ((op0 - 2) << 19) | (op1 << 16) | (crn << 12) | (crm << 8) | (op2 << 5) | rt;
}

int main(void)
{
  UINT64 saved;
  UINT64 x[31]; UINT64 spsr; WATARI_SYSREG r; int i;
  #define RESET() do { for (i = 0; i < 31; i++) x[i] = 0xAAAA0000ULL + i; spsr = 0xB0000005ULL; } while (0)

  // Builder sanity against architecturally known words: MRS x0, MIDR_EL1 (S3_0_C0_C0_0) = D5380000;
  // MSR PMCR_EL0, x1 (S3_3_C9_C12_0) = D51B9C01.
  CHECK(enc(1, 3, 0, 0, 0, 0, 0) == 0xD5380000U);
  CHECK(enc(0, 3, 3, 9, 12, 0, 1) == 0xD51B9C01U);

  RESET(); CHECK(EMU(enc(1, 3, 3, 9, 12, 0, 5), x, &spsr, &r));   // MRS x5, PMCR_EL0
  CHECK(x[5] == 0 && x[4] == 0xAAAA0004ULL && spsr == 0xB0000005ULL);
  CHECK(r.IsRead && r.Op0 == 3 && r.Op1 == 3 && r.Crn == 9 && r.Crm == 12 && r.Op2 == 0 && r.Rt == 5);

  RESET(); CHECK(EMU(enc(0, 3, 3, 9, 12, 0, 5), x, &spsr, &r));   // MSR PMCR_EL0, x5
  CHECK(!r.IsRead && x[5] == 0xAAAA0005ULL);                                       // write: registers untouched

  RESET(); CHECK(EMU(enc(1, 2, 0, 1, 1, 4, 3), x, &spsr, &r));    // MRS x3, OSLSR_EL1
  CHECK(x[3] == 0x8);
  RESET(); CHECK(EMU(enc(1, 2, 0, 0, 2, 2, 3), x, &spsr, &r));    // MDSCR_EL1: other debug reads 0
  CHECK(x[3] == 0);
  RESET(); CHECK(EMU(enc(1, 3, 3, 9, 12, 0, 31), x, &spsr, &r));  // Rt = 31: discarded
  for (i = 0; i < 31; i++) CHECK(x[i] == 0xAAAA0000ULL + i);

  RESET(); CHECK(EMU(enc(1, 3, 3, 2, 4, 0, 7), x, &spsr, &r));    // MRS x7, RNDR
  CHECK(x[7] == 0 && (spsr & 0xF0000000ULL) == 0x40000000ULL && (spsr & 0x0FFFFFFFULL) == 0x00000005ULL);
  RESET(); CHECK(EMU(enc(1, 3, 3, 2, 4, 1, 7), x, &spsr, &r));    // RNDRRS
  CHECK((spsr & 0xF0000000ULL) == 0x40000000ULL);
  RESET(); CHECK(!EMU(enc(0, 3, 3, 2, 4, 0, 7), x, &spsr, &r));   // MSR to RNDR: not emulated
  RESET(); CHECK(EMU(enc(1, 3, 3, 14, 15, 7, 1), x, &spsr, &r));  // PMCCFILTR_EL0
  RESET(); CHECK(EMU(enc(1, 3, 3, 14, 8, 0, 1), x, &spsr, &r));   // PMEVCNTR0_EL0
  RESET(); CHECK(!EMU(enc(1, 3, 3, 14, 3, 0, 1), x, &spsr, &r));  // CNTP_*: not PMU
  RESET(); CHECK(EMU(enc(1, 3, 3, 13, 4, 0, 1), x, &spsr, &r));   // AMEVCNTR0
  RESET(); CHECK(!EMU(enc(1, 3, 3, 13, 0, 2, 1), x, &spsr, &r));  // TPIDR_EL0: not AMU
  RESET(); CHECK(!EMU(enc(1, 3, 0, 9, 12, 0, 1), x, &spsr, &r));  // op1 != 3: not PMU

  RESET(); CHECK(!EMU(enc(1, 3, 0, 12, 12, 5, 1), x, &spsr, &r)); // ICC_SRE_EL1: NOT emulated
  RESET(); CHECK(!EMU(enc(0, 3, 0, 12, 12, 5, 1), x, &spsr, &r));
  RESET(); CHECK(!EMU(enc(1, 3, 0, 1, 0, 0, 1), x, &spsr, &r));   // SCTLR_EL1: NOT emulated
  RESET(); CHECK(!EMU(0xD503201FU, x, &spsr, &r));                // NOP: not a sysreg word
  RESET(); CHECK(!EMU(0xD5033FDFU, x, &spsr, &r));                // ISB
  RESET(); CHECK(!EMU(0x00000000U, x, &spsr, &r));
  RESET(); CHECK(!EMU(0xD5080000U, x, &spsr, &r));                // SYS (op0=1 space)
  for (i = 0; i < 31; i++) CHECK(x[i] == 0xAAAA0000ULL + i);
  CHECK(WatariSysregKey(&r) != 0);


  // ---- virtual PMU cycle counter (bootmgr's sequence, then a running counter) ----
  #define MSRX(op1,crm,op2,val) do { RESET(); x[1] = (val); now_cnt = NOW; CHECK(EMU(enc(0, 3, op1, 9, crm, op2, 1), x, &spsr, &r)); } while (0)
  #define MRSX(op1,crm,op2) do { RESET(); now_cnt = NOW; CHECK(EMU(enc(1, 3, op1, 9, crm, op2, 2), x, &spsr, &r)); } while (0)
  pmu = (WATARI_PMU){ 0 };
  #define NOW (100ULL)
  MRSX(3, 13, 0); CHECK(x[2] == 0);                                  // MRS PMCCNTR_EL0 first: 0
  MRSX(3, 12, 0); CHECK(x[2] == 0);                                  // PMCR: N = 0
  MSRX(3, 12, 2, 0xFFFFFFFFULL);                                     // PMCNTENCLR
  MSRX(3, 12, 3, 0xFFFFFFFFULL);                                     // PMOVSCLR
  MSRX(0, 14, 2, 0xFFFFFFFFULL);                                     // PMINTENCLR_EL1
  MSRX(3, 12, 0, 0);                                                 // PMCR
  RESET(); x[1] = 0x80000000ULL; CHECK(EMU(enc(0, 3, 3, 14, 15, 7, 1), x, &spsr, &r));   // PMCCFILTR
  MSRX(3, 12, 1, 0x8000000FULL);                                     // PMCNTENSET: only bit 31
  MRSX(3, 12, 1); CHECK(x[2] == 0x80000000ULL);
  MRSX(3, 12, 2); CHECK(x[2] == 0x80000000ULL);
  MSRX(3, 14, 0, 0xFF);                                              // PMUSERENR
  MRSX(3, 14, 0); CHECK(x[2] == 15);
  #undef NOW
  #define NOW (5 * F)
  MRSX(3, 13, 0); CHECK(x[2] == 0);                                  // E still clear: frozen
  #undef NOW
  #define NOW (1 * F)
  MSRX(3, 12, 0, 0x41);                                              // E | LC: starts
  MRSX(3, 12, 0); CHECK(x[2] == 0x41);
  MRSX(3, 13, 0); CHECK(x[2] == 0);
  #undef NOW
  #define NOW (2 * F)
  MRSX(3, 13, 0); CHECK(x[2] == 1000000000ULL);                      // one second = 1e9 cycles
  #undef NOW
  saved = 0;
  for (now_cnt = F; now_cnt < 40 * F; now_cnt += 12345677ULL) {       // strictly increasing
    RESET(); CHECK(EMU(enc(1, 3, 3, 9, 13, 0, 2), x, &spsr, &r));
    CHECK(x[2] >= saved); if (now_cnt > F) CHECK(x[2] > saved);
    saved = x[2];
  }
  #define NOW (3 * F)
  MSRX(3, 12, 2, 0x80000000ULL);                                     // PMCNTENCLR: freeze
  MRSX(3, 13, 0); CHECK(x[2] == 2000000000ULL);
  #undef NOW
  #define NOW (9 * F)
  MRSX(3, 13, 0); CHECK(x[2] == 2000000000ULL);
  #undef NOW
  #define NOW (10 * F)
  MSRX(3, 12, 1, 0x80000000ULL);                                     // enable: continues
  MRSX(3, 13, 0); CHECK(x[2] == 2000000000ULL);
  #undef NOW
  #define NOW (11 * F)
  MRSX(3, 13, 0); CHECK(x[2] == 3000000000ULL);
  MSRX(3, 13, 0, 5000);                                              // write PMCCNTR
  MRSX(3, 13, 0); CHECK(x[2] == 5000);
  #undef NOW
  #define NOW (12 * F)
  MRSX(3, 13, 0); CHECK(x[2] == 5000 + 1000000000ULL);
  MSRX(3, 12, 0, 0x45);                                              // C: reset, P/C read 0
  MRSX(3, 13, 0); CHECK(x[2] == 0);
  MRSX(3, 12, 0); CHECK(x[2] == 0x41);
  MSRX(3, 12, 0, 0x49);                                              // D: /64, no jump
  MRSX(3, 12, 0); CHECK(x[2] == 0x49);
  #undef NOW
  #define NOW (13 * F)
  MRSX(3, 13, 0); CHECK(x[2] == 1000000000ULL / 64);
  MSRX(3, 14, 3, 0xFFFFFFFFULL);                                     // PMOVSSET / PMOVSCLR
  MRSX(3, 12, 3); CHECK(x[2] == 0x80000000ULL);
  MSRX(3, 12, 3, 0x80000000ULL);
  MRSX(3, 14, 3); CHECK(x[2] == 0);
  MSRX(0, 14, 1, 0xFFFFFFFFULL);                                     // PMINTENSET_EL1
  MRSX(0, 14, 1); CHECK(x[2] == 0x80000000ULL);
  MSRX(0, 14, 2, 0x80000000ULL);
  MRSX(0, 14, 2); CHECK(x[2] == 0);
  MSRX(3, 12, 5, 0xFFFF);                                            // PMSELR: 5 bits
  MRSX(3, 12, 5); CHECK(x[2] == 31);
  MRSX(3, 12, 6); CHECK(x[2] == 0);                                  // PMCEID0
  MRSX(0, 14, 6); CHECK(x[2] == 0);                                  // PMMIR_EL1
  MSRX(3, 13, 2, 0xFFFF); MRSX(3, 13, 2); CHECK(x[2] == 0);          // PMXEVCNTR: RAZ/WI
  RESET(); now_cnt = NOW; CHECK(EMU(enc(1, 3, 3, 14, 9, 2, 2), x, &spsr, &r)); CHECK(x[2] == 0);   // PMEVCNTR9
  MRSX(3, 12, 0); CHECK(((x[2] >> 11) & 31) == 0);                   // N = 0
  #undef NOW
  RESET(); now_cnt = 0; CHECK(!EMU(enc(1, 3, 0, 9, 13, 0, 2), x, &spsr, &r));   // op1 0 CRm 13: not PMU
  now_cnt = 30ULL * 86400 * F;                                        // 30 days: no 64-bit overflow
  pmu = (WATARI_PMU){ 0 };
  { UINT64 t = now_cnt; now_cnt = 0; RESET(); x[1] = 1; EMU(enc(0, 3, 3, 9, 12, 0, 1), x, &spsr, &r);
    x[1] = 0x80000000ULL; EMU(enc(0, 3, 3, 9, 12, 1, 1), x, &spsr, &r); now_cnt = t; EMU(enc(1, 3, 3, 9, 13, 0, 2), x, &spsr, &r);
    CHECK(x[2] == 30ULL * 86400 * 1000000000ULL); }

  printf("test_sysreg_emu: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
