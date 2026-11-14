/** @file
  Watari Boot (T16a), BENCH ONLY: a RAM disk over a disk image that the QEMU bench put into RAM
  with "-device loader,file=<image>,addr=<PcdWatariM1BenchRamDiskBase>,force-raw=on".

  The pages are claimed with AllocateAddress first, so nothing else uses them; if the claim fails
  or the first sector is all zero (no image was loaded) the driver does nothing. The disk then goes
  through PartitionDxe (GPT) and the FAT driver like any other disk, and the boot manager discovers
  it (BmEnumerateBootOptions).

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/RamDisk.h>
#include <Protocol/DevicePath.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS
EFIAPI
WatariM1BenchRamDiskEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                Status;
  EFI_RAM_DISK_PROTOCOL     *RamDisk;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_PHYSICAL_ADDRESS      Base;
  UINT64                    Size;
  UINTN                     Index;
  UINT8                     *Bytes;
  BOOLEAN                   Empty;

  Base = FixedPcdGet64 (PcdWatariM1BenchRamDiskBase);
  Size = FixedPcdGet64 (PcdWatariM1BenchRamDiskSize);
  if ((Base == 0) || (Size == 0)) {
    return EFI_SUCCESS;
  }

  Status = gBS->AllocatePages (AllocateAddress, EfiBootServicesData, EFI_SIZE_TO_PAGES (Size), &Base);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "WatariM1BenchRamDisk: cannot claim %lx..+%lx: %r\n", Base, Size, Status));
    return EFI_SUCCESS;
  }

  Bytes = (UINT8 *)(UINTN)Base;
  Empty = TRUE;
  for (Index = 0; Index < 512; Index++) {
    if (Bytes[Index] != 0) {
      Empty = FALSE;
      break;
    }
  }

  if (Empty) {
    DEBUG ((DEBUG_INFO, "WatariM1BenchRamDisk: no image at %lx\n", Base));
    gBS->FreePages (Base, EFI_SIZE_TO_PAGES (Size));
    return EFI_SUCCESS;
  }

  Status = gBS->LocateProtocol (&gEfiRamDiskProtocolGuid, NULL, (VOID **)&RamDisk);
  if (!EFI_ERROR (Status)) {
    Status = RamDisk->Register (Base, Size, &gEfiVirtualDiskGuid, NULL, &DevicePath);
  }

  DEBUG ((DEBUG_INFO, "WatariM1BenchRamDisk: image at %lx, %lx bytes registered: %r\n", Base, Size, Status));
  return EFI_SUCCESS;
}
