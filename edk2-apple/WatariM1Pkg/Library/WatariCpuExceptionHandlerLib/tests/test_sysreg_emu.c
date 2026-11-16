// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host test of WatariSysregEmu.h. Words are built from the Arm ARM C5.2 fields, not taken from any binary.
#define WATARI_SYSREG_HOST
#include <stdio.h>
#include "../WatariSysregEmu.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static UINT32 enc(int read, UINT32 op0, UINT32 op1, UINT32 crn, UINT32 crm, UINT32 op2, UINT32 rt)
{
  return (read ? 0xD5300000U : 0xD5100000U) | ((op0 - 2) << 19) | (op1 << 16) | (crn << 12) | (crm << 8) | (op2 << 5) | rt;
}

int main(void)
{
  UINT64 x[31]; UINT64 spsr; WATARI_SYSREG r; int i;
  #define RESET() do { for (i = 0; i < 31; i++) x[i] = 0xAAAA0000ULL + i; spsr = 0xB0000005ULL; } while (0)

  // Builder sanity against architecturally known words: MRS x0, MIDR_EL1 (S3_0_C0_C0_0) = D5380000;
  // MSR PMCR_EL0, x1 (S3_3_C9_C12_0) = D51B9C01.
  CHECK(enc(1, 3, 0, 0, 0, 0, 0) == 0xD5380000U);
  CHECK(enc(0, 3, 3, 9, 12, 0, 1) == 0xD51B9C01U);

  RESET(); CHECK(WatariSysregEmulate(enc(1, 3, 3, 9, 12, 0, 5), x, &spsr, &r));   // MRS x5, PMCR_EL0
  CHECK(x[5] == 0 && x[4] == 0xAAAA0004ULL && spsr == 0xB0000005ULL);
  CHECK(r.IsRead && r.Op0 == 3 && r.Op1 == 3 && r.Crn == 9 && r.Crm == 12 && r.Op2 == 0 && r.Rt == 5);

  RESET(); CHECK(WatariSysregEmulate(enc(0, 3, 3, 9, 12, 0, 5), x, &spsr, &r));   // MSR PMCR_EL0, x5
  CHECK(!r.IsRead && x[5] == 0xAAAA0005ULL);                                       // write: registers untouched

  RESET(); CHECK(WatariSysregEmulate(enc(1, 2, 0, 1, 1, 4, 3), x, &spsr, &r));    // MRS x3, OSLSR_EL1
  CHECK(x[3] == 0x8);
  RESET(); CHECK(WatariSysregEmulate(enc(1, 2, 0, 0, 2, 2, 3), x, &spsr, &r));    // MDSCR_EL1: other debug reads 0
  CHECK(x[3] == 0);
  RESET(); CHECK(WatariSysregEmulate(enc(1, 3, 3, 9, 12, 0, 31), x, &spsr, &r));  // Rt = 31: discarded
  for (i = 0; i < 31; i++) CHECK(x[i] == 0xAAAA0000ULL + i);

  RESET(); CHECK(WatariSysregEmulate(enc(1, 3, 3, 2, 4, 0, 7), x, &spsr, &r));    // MRS x7, RNDR
  CHECK(x[7] == 0 && (spsr & 0xF0000000ULL) == 0x40000000ULL && (spsr & 0x0FFFFFFFULL) == 0x00000005ULL);
  RESET(); CHECK(WatariSysregEmulate(enc(1, 3, 3, 2, 4, 1, 7), x, &spsr, &r));    // RNDRRS
  CHECK((spsr & 0xF0000000ULL) == 0x40000000ULL);
  RESET(); CHECK(!WatariSysregEmulate(enc(0, 3, 3, 2, 4, 0, 7), x, &spsr, &r));   // MSR to RNDR: not emulated
  RESET(); CHECK(WatariSysregEmulate(enc(1, 3, 3, 14, 15, 7, 1), x, &spsr, &r));  // PMCCFILTR_EL0
  RESET(); CHECK(WatariSysregEmulate(enc(1, 3, 3, 14, 8, 0, 1), x, &spsr, &r));   // PMEVCNTR0_EL0
  RESET(); CHECK(!WatariSysregEmulate(enc(1, 3, 3, 14, 3, 0, 1), x, &spsr, &r));  // CNTP_*: not PMU
  RESET(); CHECK(WatariSysregEmulate(enc(1, 3, 3, 13, 4, 0, 1), x, &spsr, &r));   // AMEVCNTR0
  RESET(); CHECK(!WatariSysregEmulate(enc(1, 3, 3, 13, 0, 2, 1), x, &spsr, &r));  // TPIDR_EL0: not AMU
  RESET(); CHECK(!WatariSysregEmulate(enc(1, 3, 0, 9, 12, 0, 1), x, &spsr, &r));  // op1 != 3: not PMU

  RESET(); CHECK(!WatariSysregEmulate(enc(1, 3, 0, 12, 12, 5, 1), x, &spsr, &r)); // ICC_SRE_EL1: NOT emulated
  RESET(); CHECK(!WatariSysregEmulate(enc(0, 3, 0, 12, 12, 5, 1), x, &spsr, &r));
  RESET(); CHECK(!WatariSysregEmulate(enc(1, 3, 0, 1, 0, 0, 1), x, &spsr, &r));   // SCTLR_EL1: NOT emulated
  RESET(); CHECK(!WatariSysregEmulate(0xD503201FU, x, &spsr, &r));                // NOP: not a sysreg word
  RESET(); CHECK(!WatariSysregEmulate(0xD5033FDFU, x, &spsr, &r));                // ISB
  RESET(); CHECK(!WatariSysregEmulate(0x00000000U, x, &spsr, &r));
  RESET(); CHECK(!WatariSysregEmulate(0xD5080000U, x, &spsr, &r));                // SYS (op0=1 space)
  for (i = 0; i < 31; i++) CHECK(x[i] == 0xAAAA0000ULL + i);
  CHECK(WatariSysregKey(&r) != 0);

  printf("test_sysreg_emu: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
