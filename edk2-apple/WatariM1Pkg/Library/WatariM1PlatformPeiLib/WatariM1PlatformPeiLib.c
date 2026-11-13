/** @file
  PlatformPeiLib for the Apple M1 platform: the FDT HOB and the FV HOB, nothing else.

  PrePi calls PlatformPeim() once the memory is set up. ArmVirtPkg's own PlatformPeiLib also
  looks for a PL011 and a TPM and reads fw_cfg for a debug level; none of that exists here.
  Written from the PlatformPeiLib interface (PrePi/PrePi.c) and the FDT HOB contract
  (Guid/FdtHob.h: the HOB carries the address of a copy of the device tree that DXE's
  FdtClientDxe reads).

  Bench build only (WATARI_M1_BENCH_PL011): the PL011 that QEMU describes in its device tree
  is published through gEarlyPL011BaseAddressGuid, which is how ArmVirtPkg's DXE-phase serial
  and debug libraries find their UART. Used to read the firmware's log on the QEMU bench.

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>

#include <Guid/FdtHob.h>

#ifdef WATARI_M1_BENCH_PL011
#include <Library/FdtSerialPortAddressLib.h>
#include <Guid/EarlyPL011BaseAddress.h>
#endif

EFI_STATUS
EFIAPI
PlatformPeim (
  VOID
  )
{
  VOID    *Base;
  VOID    *NewBase;
  UINTN   FdtSize;
  UINTN   FdtPages;
  UINT64  *FdtHobData;

  Base = (VOID *)(UINTN)PcdGet64 (PcdDeviceTreeInitialBaseAddress);
  ASSERT (Base != NULL);
  ASSERT (FdtCheckHeader (Base) == 0);

  FdtSize  = FdtTotalSize (Base) + PcdGet32 (PcdDeviceTreeAllocationPadding);
  FdtPages = EFI_SIZE_TO_PAGES (FdtSize);
  NewBase  = AllocatePages (FdtPages);
  ASSERT (NewBase != NULL);
  FdtOpenInto (Base, NewBase, EFI_PAGES_TO_SIZE (FdtPages));

  FdtHobData = BuildGuidHob (&gFdtHobGuid, sizeof *FdtHobData);
  ASSERT (FdtHobData != NULL);
  *FdtHobData = (UINTN)NewBase;

#ifdef WATARI_M1_BENCH_PL011
  {
    EARLY_PL011_BASE_ADDRESS  *UartHobData;
    FDT_SERIAL_PORTS          Ports;
    EFI_STATUS                Status;

    UartHobData = BuildGuidHob (&gEarlyPL011BaseAddressGuid, sizeof *UartHobData);
    ASSERT (UartHobData != NULL);
    SetMem (UartHobData, sizeof *UartHobData, 0);

    Status = FdtSerialGetPorts (Base, "arm,pl011", &Ports);
    if (!EFI_ERROR (Status) && (Ports.NumberOfPorts >= 1)) {
      // One UART on the QEMU virt machine: console and debug share it. DebugLevelSet stays FALSE,
      // so PcdDebugPrintErrorLevel applies unchanged.
      UartHobData->ConsoleAddress = Ports.BaseAddress[0];
      UartHobData->DebugAddress   = Ports.BaseAddress[0];
      DEBUG ((DEBUG_INFO, "%a: bench PL011 @ 0x%lx\n", __func__, UartHobData->ConsoleAddress));
    }
  }
#endif

  BuildFvHob (PcdGet64 (PcdFvBaseAddress), PcdGet32 (PcdFvSize));

  return EFI_SUCCESS;
}
