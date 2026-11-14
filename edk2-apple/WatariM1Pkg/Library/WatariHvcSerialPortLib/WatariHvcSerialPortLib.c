/** @file
  SerialPortLib over the Watari EL2 vendor hypercall WEL2_CONSOLE_WRITE.

  Calling convention: SMCCC fast call over HVC #0, SMC64 convention, function id in w0, arguments
  x1-x3, result in x0 (el2/include/wel2_hvc.h of this repository; the ids are repeated here
  because the layer's header is not part of the EDK2 tree).

    WEL2_VERSION        0xC6000000  x0 = (0x57454C32 << 32) | abi version
    WEL2_CONSOLE_WRITE  0xC6000004  x1 = guest PA of raw bytes, x2 = length (<= 160), x0 = 0 on success

  Why a guarded probe: on a machine without the layer, HVC from EL1 is UNDEFINED when EL2 is not
  implemented, and an unknown vendor id may not be answered as NOT_SUPPORTED by whoever sits at
  EL2 [hypothesis: PLAN E8]. The first SerialPortWrite therefore asks, in this order, and goes
  quiet for good unless every answer is as expected:
    1. CurrentEL is EL1 and ID_AA64PFR0_EL1.EL2 is non-zero (an HVC cannot be UNDEFINED);
    2. PSCI_VERSION (0x84000000) over HVC returns a version (not NOT_SUPPORTED);
    3. WEL2_VERSION returns the tag 'WEL2' in its upper half.

  The library holds one word of state, initialised to a non-zero value so that it lives in .data
  (not .bss: PrePi's image has no zero-fill area [hypothesis]). Before the MMU is on (PrePi) the
  buffer address is the physical address; afterwards EDK2 maps boot-time memory 1:1, so it still
  is [hypothesis: UEFI Arm binding, true for every boot-time module of ArmVirt]. Runtime drivers
  use BaseDebugLibNull instead of this library: after SetVirtualAddressMap the address would not
  be a guest physical address.

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/SerialPortLib.h>

#define WEL2_VERSION_ID        0xC6000000ULL
#define WEL2_CONSOLE_WRITE_ID  0xC6000004ULL
#define WEL2_CONSOLE_MAX       160U
#define WEL2_VERSION_TAG       0x57454C32ULL
#define PSCI_VERSION_ID        0x84000000ULL

#define PROBE_PENDING  1U     // non-zero on purpose: see the file comment
#define PROBE_ON       2U
#define PROBE_OFF      3U

STATIC volatile UINTN  mProbeState = PROBE_PENDING;

/**
  One SMCCC call over HVC #0. x4-x17 are declared clobbered (SMCCC 1.0 allows it).
**/
STATIC
UINT64
WatariHvc (
  IN UINT64  Id,
  IN UINT64  A1,
  IN UINT64  A2
  )
{
  register UINT64  X0 __asm__ ("x0") = Id;
  register UINT64  X1 __asm__ ("x1") = A1;
  register UINT64  X2 __asm__ ("x2") = A2;
  register UINT64  X3 __asm__ ("x3") = 0;

  __asm__ volatile (
    "hvc #0"
    : "+r" (X0), "+r" (X1), "+r" (X2), "+r" (X3)
    :
    : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
      "memory"
    );
  return X0;
}

/**
  The guarded probe. Returns TRUE only when the layer answered both calls.
**/
STATIC
BOOLEAN
WatariHvcProbe (
  VOID
  )
{
  UINT64  CurrentEl;
  UINT64  Pfr0;
  UINT64  Ret;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  if (((CurrentEl >> 2) & 3) != 1) {
    return FALSE;
  }

  __asm__ volatile ("mrs %0, ID_AA64PFR0_EL1" : "=r" (Pfr0));
  if (((Pfr0 >> 8) & 0xF) == 0) {
    return FALSE;                       // no EL2: HVC would be UNDEFINED
  }

  Ret = WatariHvc (PSCI_VERSION_ID, 0, 0) & 0xFFFFFFFFULL;
  if ((Ret == 0) || (Ret >= 0x80000000ULL)) {
    return FALSE;                       // NOT_SUPPORTED (-1) or an error code
  }

  Ret = WatariHvc (WEL2_VERSION_ID, 0, 0);
  return (BOOLEAN)((Ret >> 32) == WEL2_VERSION_TAG);
}

RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  return RETURN_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  UINTN   Done;
  UINTN   Chunk;
  UINT64  Ret;

  if (mProbeState == PROBE_PENDING) {
    mProbeState = WatariHvcProbe () ? PROBE_ON : PROBE_OFF;
  }

  if ((mProbeState != PROBE_ON) || (Buffer == NULL)) {
    return NumberOfBytes;               // quiet: the bytes are "written" so that callers do not loop
  }

  for (Done = 0; Done < NumberOfBytes; Done += Chunk) {
    Chunk = NumberOfBytes - Done;
    if (Chunk > WEL2_CONSOLE_MAX) {
      Chunk = WEL2_CONSOLE_MAX;
    }

    Ret = WatariHvc (WEL2_CONSOLE_WRITE_ID, (UINT64)(UINTN)(Buffer + Done), Chunk);
    if ((Ret & 0xFFFFFFFFULL) == 0xFFFFFFFFULL) {
      mProbeState = PROBE_OFF;          // NOT_SUPPORTED: be quiet from now on
      break;
    }
    // INVALID_PARAMETERS (-2: a range outside guest RAM): this chunk is lost, the next may be fine.
  }

  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  // EFI_SERIAL_OUTPUT_BUFFER_EMPTY (0x2000) | EFI_SERIAL_INPUT_BUFFER_EMPTY (0x0100): always ready to write, nothing to read.
  *Control = 0x2100;
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  // There is no line: the attributes are accepted as given.
  return RETURN_SUCCESS;
}
