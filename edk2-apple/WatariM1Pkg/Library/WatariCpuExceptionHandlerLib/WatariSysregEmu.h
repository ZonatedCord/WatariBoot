/** @file
  Pure decode/classify/emulate logic for the EL1 system-register safety net of
  WatariCpuExceptionHandlerLib. No hardware access, no EDK2 services: it works on a 32-bit word
  and a saved register image, so it is also compiled by the host test
  (tests/test_sysreg_emu.c, `make -C <this directory>/tests`).

  Scope (decision by Marco, 2026-11-16, docs/adr/draft-f01-interop-reading.md "Runtime emulation"):
  the word at ELR is looked at ONLY to decide whether it is an MRS/MSR (register) it can emulate.
  Nothing but the decoded encoding (op0, op1, CRn, CRm, op2, direction) and the PC leaves this
  file: the raw word is never stored, logged or printed.

  Encodings, Arm ARM (DDI 0487) C5.2 "System instruction encodings" / C6.2.228 MRS, C6.2.246 MSR:
    bits 31:22 = 1101 0101 00, bit 21 = L (1 = MRS read, 0 = MSR write), bit 20 = 1,
    bit 19 = o0 (op0 = 2 + o0), bits 18:16 = op1, 15:12 = CRn, 11:8 = CRm, 7:5 = op2, 4:0 = Rt.
    So (word & 0xFFF00000) == 0xD5300000 is MRS and == 0xD5100000 is MSR (register). [verified:
    derived field by field from the Arm ARM encoding table; checked by the host test]
  Register groups, Arm ARM D (AArch64 system registers), by encoding range:
    PMU     S3_3_C9_C12..C14_*  (PMCR..PMOVSSET, PMCCNTR, PMXEVTYPER/CNTR, PMUSERENR, ...),
            S3_3_C14_C8..C15_*  (PMEVCNTR<n>, PMEVTYPER<n>, PMCCFILTR_EL0 = C14_C15_7).
    Debug   op0 = 2 (all: OSLAR/OSLSR/OSDLR, DBG*VR/CR, MDSCR, ...).
    AMU     S3_3_C13_C2..C7_*.
    RNG     RNDR = S3_3_C2_C4_0, RNDRRS = S3_3_C2_C4_1 (read-only).
  PMU: a virtual cycle counter (see WATARI_PMU below); the other PMU registers are RAZ / WI (N = 0).
  OSLSR_EL1 = S2_0_C1_C1_4; value 0x8 = OSLM[1:0] 0b10 ("OS lock implemented", unlocked).
  [hypothesis: field layout of OSLSR_EL1 recalled from the Arm ARM, not re-read]
  RNDR on failure: "PSTATE.NZCV is set to 0b0100 and the register is 0" (Arm ARM, RNDR). The saved
  SPSR keeps NZCV in bits 31:28 (N=31 Z=30 C=29 V=28), so the saved NZCV becomes Z only.
  [verified: Arm ARM RNDR pseudocode "PSTATE.<N,Z,C,V> = '0100'"; wording recalled]

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef WATARI_SYSREG_EMU_H_
#define WATARI_SYSREG_EMU_H_

#ifdef WATARI_SYSREG_HOST
  #include <stdint.h>
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int       BOOLEAN;
  #define TRUE   1
  #define FALSE  0
  #define STATIC  static
  #define IN
  #define OUT
  #define CONST  const
  #define VOID    void
#endif

#define WSE_MRS_MASK  0xFFF00000U
#define WSE_MRS_BITS  0xD5300000U
#define WSE_MSR_BITS  0xD5100000U

#define WSE_SPSR_NZCV_MASK  0xF0000000ULL
#define WSE_SPSR_Z          0x40000000ULL

typedef struct {
  UINT32     Op0;
  UINT32     Op1;
  UINT32     Crn;
  UINT32     Crm;
  UINT32     Op2;
  UINT32     Rt;
  BOOLEAN    IsRead;
} WATARI_SYSREG;

/**
  Decodes an MRS/MSR (register) word. Returns FALSE for anything else; Out is then untouched.
**/
STATIC
BOOLEAN
WatariSysregDecode (
  IN  UINT32         Word,
  OUT WATARI_SYSREG  *Out
  )
{
  UINT32  Top;

  Top = Word & WSE_MRS_MASK;
  if ((Top != WSE_MRS_BITS) && (Top != WSE_MSR_BITS)) {
    return FALSE;
  }

  Out->IsRead = (Top == WSE_MRS_BITS);
  Out->Op0    = 2 + ((Word >> 19) & 1);
  Out->Op1    = (Word >> 16) & 7;
  Out->Crn    = (Word >> 12) & 0xF;
  Out->Crm    = (Word >> 8) & 0xF;
  Out->Op2    = (Word >> 5) & 7;
  Out->Rt     = Word & 0x1F;
  return TRUE;
}

/**
  Whether the register belongs to an emulated group. RNDR/RNDRRS are reads only.
**/
STATIC
BOOLEAN
WatariSysregEmulated (
  IN CONST WATARI_SYSREG  *R
  )
{
  if (R->Op0 == 2) {
    return TRUE;                                              // debug: all
  }

  if ((R->Op1 == 0) && (R->Crn == 9) && (R->Crm == 14)) {
    return TRUE;                                              // PMINTENSET/CLR_EL1, PMMIR_EL1
  }

  if (R->Op1 != 3) {
    return FALSE;
  }

  if ((R->Crn == 9) && (R->Crm >= 12) && (R->Crm <= 14)) {
    return TRUE;                                              // PMU
  }

  if ((R->Crn == 14) && (R->Crm >= 8)) {
    return TRUE;                                              // PMEVCNTR/PMEVTYPER/PMCCFILTR
  }

  if ((R->Crn == 13) && (R->Crm >= 2) && (R->Crm <= 7)) {
    return TRUE;                                              // AMU
  }

  if ((R->Crn == 2) && (R->Crm == 4) && (R->Op2 <= 1) && R->IsRead) {
    return TRUE;                                              // RNDR / RNDRRS
  }

  return FALSE;
}

//
// Virtual PMUv3, cycle counter only (N = 0 event counters). Same semantics as the Watari EL2 catch mode
// (el2/vgic/sysreg_catch.c, docs/design/watari-el2/INTERFACES.md 7.5): PMCCNTR_EL0 is a running counter derived from
// the shared system counter at a nominal 1 GHz: it is ONE global clock that ALWAYS runs, whatever PMCR.E / PMCNTENSET.C /
// PMCR.D say (so it is monotonic on every core), writes to PMCCNTR_EL0 and PMCR.C are ignored, and the control registers
// read back what was written (wel2.vpmu=global of the layer). A guest that calibrates against the cycle counter
// (bootmgr right after start) then sees it advance instead of reading 0 forever.
// State: ONE global instance, not per core (the handler runs on the boot core while bootmgr runs; secondary cores run
// firmware code only until Windows installs its own VBAR_EL1) [hypothesis]. Racing cores can at worst see a stale
// value, never fault.
//
#define WSE_PMCR_E     (1U << 0)
#define WSE_PMCR_C     (1U << 2)
#define WSE_PMCR_D     (1U << 3)
#define WSE_PMCR_KEEP  0xF9U          // E | D | X | DP | LC | LP; P and C are write-only actions; N, IDCODE read 0
#define WSE_PMU_CBIT   0x80000000U

typedef struct {
  UINT32    Pmcr;
  UINT32    PmCntEn;                  // bit 31 only
  UINT32    PmOvs;                    // bit 31 only
  UINT32    PmIntEn;                  // bit 31 only
  UINT32    PmUserEnr;                // 4 bits
  UINT32    PmSelr;                   // 5 bits
  UINT64    PmCcFiltr;
  UINT64    Base;                     // running: PMCCNTR = Base + scaled(now), modulo 2^64
  UINT64    Frozen;                   // not running: PMCCNTR
} WATARI_PMU;

STATIC
BOOLEAN
WatariPmuRunning (
  IN CONST WATARI_PMU  *P
  )
{
  (VOID)P;
  return TRUE;                           // global: always running
}

//
// Cnt * 1e9 / Frq without a 64-bit by 128-bit division: Q * 1e9 + (R * 1e9) / Frq with R < Frq (fits for Frq < 1.8e10).
//
STATIC
UINT64
WatariPmuScaled (
  IN CONST WATARI_PMU  *P,
  IN UINT64            Cnt,
  IN UINT64            Frq
  )
{
  UINT64  V;

  if (Frq == 0) {
    return 0;
  }

  V = (Cnt / Frq) * 1000000000ULL + ((Cnt % Frq) * 1000000000ULL) / Frq;
  (VOID)P;
  return V;                              // PMCR.D does not slow the global clock
}

STATIC
UINT64
WatariPmuCycles (
  IN CONST WATARI_PMU  *P,
  IN UINT64            Cnt,
  IN UINT64            Frq
  )
{
  return WatariPmuRunning (P) ? P->Base + WatariPmuScaled (P, Cnt, Frq) : P->Frozen;
}

STATIC
VOID
WatariPmuSet (
  IN OUT WATARI_PMU  *P,
  IN UINT64          V,
  IN UINT64          Cnt,
  IN UINT64          Frq
  )
{
  (VOID)P;
  (VOID)V;
  (VOID)Cnt;
  (VOID)Frq;                             // global: writes (PMCCNTR_EL0, PMCR.C) are ignored
}

// Change PMCR / PMCNTENSET without a jump of the counter value.
STATIC
VOID
WatariPmuCtl (
  IN OUT WATARI_PMU  *P,
  IN UINT32          Pmcr,
  IN UINT32          PmCntEn,
  IN UINT64          Cnt,
  IN UINT64          Frq
  )
{
  UINT64  Cur;

  Cur         = WatariPmuCycles (P, Cnt, Frq);
  P->Pmcr     = Pmcr;
  P->PmCntEn  = PmCntEn;
  WatariPmuSet (P, Cur, Cnt, Frq);
}

STATIC
BOOLEAN
WatariIsPm (
  IN CONST WATARI_SYSREG  *R,
  IN UINT32               Op1,
  IN UINT32               Crm,
  IN UINT32               Op2
  )
{
  return (R->Op0 == 3) && (R->Op1 == Op1) && (R->Crn == 9) && (R->Crm == Crm) && (R->Op2 == Op2);
}

/**
  Value a read returns. OSLSR_EL1 = 0x8, the virtual PMU registers their state, everything else 0 (RAZ).
**/
STATIC
UINT64
WatariSysregReadValue (
  IN CONST WATARI_SYSREG  *R,
  IN CONST WATARI_PMU     *P,
  IN UINT64               Cnt,
  IN UINT64               Frq
  )
{
  if ((R->Op0 == 2) && (R->Op1 == 0) && (R->Crn == 1) && (R->Crm == 1) && (R->Op2 == 4)) {
    return 0x8;
  }

  if (WatariIsPm (R, 3, 12, 0)) {
    return P->Pmcr & WSE_PMCR_KEEP;
  }

  if (WatariIsPm (R, 3, 12, 1) || WatariIsPm (R, 3, 12, 2)) {
    return P->PmCntEn;
  }

  if (WatariIsPm (R, 3, 12, 3) || WatariIsPm (R, 3, 14, 3)) {
    return P->PmOvs;
  }

  if (WatariIsPm (R, 0, 14, 1) || WatariIsPm (R, 0, 14, 2)) {
    return P->PmIntEn;
  }

  if (WatariIsPm (R, 3, 12, 5)) {
    return P->PmSelr;
  }

  if (WatariIsPm (R, 3, 13, 0)) {
    return WatariPmuCycles (P, Cnt, Frq);
  }

  if (WatariIsPm (R, 3, 14, 0)) {
    return P->PmUserEnr;
  }

  if ((R->Op0 == 3) && (R->Op1 == 3) && (R->Crn == 14) && (R->Crm == 15) && (R->Op2 == 7)) {
    return P->PmCcFiltr;
  }

  return 0;
}

//
// The effect of a write (Value = the register written, 0 for XZR).
//
STATIC
VOID
WatariSysregWriteValue (
  IN CONST WATARI_SYSREG  *R,
  IN OUT WATARI_PMU       *P,
  IN UINT64               Value,
  IN UINT64               Cnt,
  IN UINT64               Frq
  )
{
  UINT32  V32;

  V32 = (UINT32)Value;
  if (WatariIsPm (R, 3, 12, 0)) {
    WatariPmuCtl (P, V32 & WSE_PMCR_KEEP, P->PmCntEn, Cnt, Frq);
    if ((V32 & WSE_PMCR_C) != 0) {
      WatariPmuSet (P, 0, Cnt, Frq);
    }
  } else if (WatariIsPm (R, 3, 12, 1)) {
    WatariPmuCtl (P, P->Pmcr, P->PmCntEn | (V32 & WSE_PMU_CBIT), Cnt, Frq);
  } else if (WatariIsPm (R, 3, 12, 2)) {
    WatariPmuCtl (P, P->Pmcr, P->PmCntEn & ~(V32 & WSE_PMU_CBIT), Cnt, Frq);
  } else if (WatariIsPm (R, 3, 12, 3)) {
    P->PmOvs &= ~(V32 & WSE_PMU_CBIT);
  } else if (WatariIsPm (R, 3, 14, 3)) {
    P->PmOvs |= V32 & WSE_PMU_CBIT;
  } else if (WatariIsPm (R, 0, 14, 1)) {
    P->PmIntEn |= V32 & WSE_PMU_CBIT;
  } else if (WatariIsPm (R, 0, 14, 2)) {
    P->PmIntEn &= ~(V32 & WSE_PMU_CBIT);
  } else if (WatariIsPm (R, 3, 12, 5)) {
    P->PmSelr = V32 & 31U;
  } else if (WatariIsPm (R, 3, 13, 0)) {
    WatariPmuSet (P, Value, Cnt, Frq);
  } else if (WatariIsPm (R, 3, 14, 0)) {
    P->PmUserEnr = V32 & 15U;
  } else if ((R->Op0 == 3) && (R->Op1 == 3) && (R->Crn == 14) && (R->Crm == 15) && (R->Op2 == 7)) {
    P->PmCcFiltr = Value;
  }

  // PMSWINC, PMXEVTYPER, PMXEVCNTR, PMEVCNTR<n>, PMEVTYPER<n>, debug, AMU: N = 0 / not implemented, write ignored
}

/**
  Emulates the access on a saved register image and returns TRUE (the caller then advances ELR by 4
  and resumes), or returns FALSE with nothing changed.

  @param[in]      Word      The instruction word. Only decoded here; never kept.
  @param[in,out]  X         Saved X0..X30 (31 entries; X[Rt] for Rt < 31).
  @param[in,out]  Spsr      Saved SPSR (RNDR failure sets NZCV = 0b0100).
  @param[in,out]  Pmu       The virtual PMU state.
  @param[in]      Cnt       The system counter now (CNTVCT_EL0), Frq its frequency (CNTFRQ_EL0, Hz).
  @param[out]     Decoded   The decoded encoding, for the one-line log. Valid if TRUE.
**/
STATIC
BOOLEAN
WatariSysregEmulate (
  IN     UINT32         Word,
  IN OUT UINT64         *X,
  IN OUT UINT64         *Spsr,
  IN OUT WATARI_PMU     *Pmu,
  IN     UINT64         Cnt,
  IN     UINT64         Frq,
  OUT    WATARI_SYSREG  *Decoded
  )
{
  if (!WatariSysregDecode (Word, Decoded) || !WatariSysregEmulated (Decoded)) {
    return FALSE;
  }

  if (Decoded->IsRead) {
    if (Decoded->Rt < 31) {
      X[Decoded->Rt] = WatariSysregReadValue (Decoded, Pmu, Cnt, Frq);
    }

    if ((Decoded->Op0 == 3) && (Decoded->Crn == 2)) {         // RNDR / RNDRRS: report "failure"
      *Spsr = (*Spsr & ~WSE_SPSR_NZCV_MASK) | WSE_SPSR_Z;
    }
  } else {
    WatariSysregWriteValue (Decoded, Pmu, (Decoded->Rt < 31) ? X[Decoded->Rt] : 0, Cnt, Frq);
  }

  return TRUE;
}

/**
  Key of an encoding for the seen-table (direction included). Never 0 for a valid encoding.
**/
STATIC
UINT32
WatariSysregKey (
  IN CONST WATARI_SYSREG  *R
  )
{
  return 0x80000000U | ((R->IsRead ? 1U : 0U) << 17) | ((R->Op0 & 1) << 16) | (R->Op1 << 13) |
         (R->Crn << 9) | (R->Crm << 5) | (R->Op2 << 2);
}

#endif
