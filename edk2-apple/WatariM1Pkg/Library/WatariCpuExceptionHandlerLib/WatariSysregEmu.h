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

/**
  Value a read returns. OSLSR_EL1 = 0x8, everything else 0 (RAZ).
**/
STATIC
UINT64
WatariSysregReadValue (
  IN CONST WATARI_SYSREG  *R
  )
{
  if ((R->Op0 == 2) && (R->Op1 == 0) && (R->Crn == 1) && (R->Crm == 1) && (R->Op2 == 4)) {
    return 0x8;
  }

  return 0;
}

/**
  Emulates the access on a saved register image and returns TRUE (the caller then advances ELR by 4
  and resumes), or returns FALSE with nothing changed.

  @param[in]      Word      The instruction word. Only decoded here; never kept.
  @param[in,out]  X         Saved X0..X30 (31 entries; X[Rt] for Rt < 31).
  @param[in,out]  Spsr      Saved SPSR (RNDR failure sets NZCV = 0b0100).
  @param[out]     Decoded   The decoded encoding, for the one-line log. Valid if TRUE.
**/
STATIC
BOOLEAN
WatariSysregEmulate (
  IN     UINT32         Word,
  IN OUT UINT64         *X,
  IN OUT UINT64         *Spsr,
  OUT    WATARI_SYSREG  *Decoded
  )
{
  if (!WatariSysregDecode (Word, Decoded) || !WatariSysregEmulated (Decoded)) {
    return FALSE;
  }

  if (Decoded->IsRead) {
    if (Decoded->Rt < 31) {
      X[Decoded->Rt] = WatariSysregReadValue (Decoded);
    }

    if ((Decoded->Op0 == 3) && (Decoded->Crn == 2)) {         // RNDR / RNDRRS: report "failure"
      *Spsr = (*Spsr & ~WSE_SPSR_NZCV_MASK) | WSE_SPSR_Z;
    }
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
