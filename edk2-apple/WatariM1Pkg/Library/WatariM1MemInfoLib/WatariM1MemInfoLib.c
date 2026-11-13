/** @file
  ArmVirtMemInfoLib for the Apple M1 platform (RAM + firmware volume, no peripheral window;
  a bench build adds the one page of its console UART).

  Written from the ArmVirtMemInfoLib interface (ArmVirtPkg/Include/Library/ArmVirtMemInfoLib.h) and
  the way MemoryInitPeiLib consumes it. Nothing of QemuVirtMemInfoLib's mach-virt window is kept.

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>

// RAM, FV, (bench UART), terminator.
#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS  4

/**
  Library constructor: publishes the size of the first RAM range (found by PrePi in the FDT) as
  a HOB, because MemoryPeim() and ArmVirtGetMemoryMap() read it from there.

  @return  Always RETURN_SUCCESS.
**/
RETURN_STATUS
EFIAPI
WatariM1MemInfoLibConstructor (
  VOID
  )
{
  UINT64  Size;
  VOID    *Hob;

  Size = PcdGet64 (PcdSystemMemorySize);
  Hob  = BuildGuidDataHob (&gArmVirtSystemMemorySizeGuid, &Size, sizeof Size);
  ASSERT (Hob != NULL);

  return RETURN_SUCCESS;
}

/**
  Return the virtual memory map used by MemoryInitPei to turn the MMU on.

  @param[out]  VirtualMemoryMap  Zero-terminated array of ARM_MEMORY_REGION_DESCRIPTOR.
**/
VOID
EFIAPI
ArmVirtGetMemoryMap (
  OUT ARM_MEMORY_REGION_DESCRIPTOR  **VirtualMemoryMap
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR  *VirtualMemoryTable;
  VOID                          *MemorySizeHob;
  UINTN                         Index;

  ASSERT (VirtualMemoryMap != NULL);

  MemorySizeHob = GetFirstGuidHob (&gArmVirtSystemMemorySizeGuid);
  ASSERT (MemorySizeHob != NULL);
  if (MemorySizeHob == NULL) {
    return;
  }

  VirtualMemoryTable = AllocatePool (
                         sizeof (ARM_MEMORY_REGION_DESCRIPTOR) *
                         MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS
                         );
  if (VirtualMemoryTable == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Error: Failed AllocatePool()\n", __func__));
    return;
  }

  // System RAM: the first range of the guest FDT (PrePi sets base and size).
  VirtualMemoryTable[0].PhysicalBase = PcdGet64 (PcdSystemMemoryBase);
  VirtualMemoryTable[0].VirtualBase  = VirtualMemoryTable[0].PhysicalBase;
  VirtualMemoryTable[0].Length       = *(UINT64 *)GET_GUID_HOB_DATA (MemorySizeHob);
  VirtualMemoryTable[0].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;

  DEBUG ((
    DEBUG_INFO,
    "%a: System RAM: PhysicalBase 0x%lX, Length 0x%lX\n",
    __func__,
    VirtualMemoryTable[0].PhysicalBase,
    VirtualMemoryTable[0].Length
    ));

  // The firmware volume, as normal executable memory.
  VirtualMemoryTable[1].PhysicalBase = PcdGet64 (PcdFvBaseAddress);
  VirtualMemoryTable[1].VirtualBase  = VirtualMemoryTable[1].PhysicalBase;
  VirtualMemoryTable[1].Length       = FixedPcdGet32 (PcdFvSize);
  VirtualMemoryTable[1].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_RO;

  Index = 2;

  // Bench only: the PL011 of the QEMU machine, one page. PcdWatariM1BenchUartBase is 0 in a
  // production build, so the map then holds RAM and the FV and nothing else.
  if (FixedPcdGet64 (PcdWatariM1BenchUartBase) != 0) {
    VirtualMemoryTable[Index].PhysicalBase = FixedPcdGet64 (PcdWatariM1BenchUartBase);
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = SIZE_4KB;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
    Index++;
  }

  // End of table.
  ZeroMem (&VirtualMemoryTable[Index], sizeof (ARM_MEMORY_REGION_DESCRIPTOR));

  *VirtualMemoryMap = VirtualMemoryTable;
}
