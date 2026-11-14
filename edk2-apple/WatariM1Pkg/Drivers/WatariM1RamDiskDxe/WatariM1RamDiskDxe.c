/** @file
  Watari Boot (T21): a RAM disk over a disk image that the loader put into guest RAM, described by the guest
  device tree node /reserved-memory/ramdisk@<base> { compatible = "watari,ramdisk"; reg = <base size>; no-map; }
  which Watari EL2 writes when it is started with wel2.ramdisk=0x<base>,0x<size>
  (docs/design/watari-winram/DESIGN.md sections 3 and 4). Production safe: without that node the driver returns at
  once and the firmware boots as before.

  Steps (DESIGN 4.1):
    1. read the node (base, size);
    2. make the range usable memory: the layer cut it out of /memory, so the GCD says "non existent": the same three
       calls OvmfPkg/Fdt/HighMemDxe makes for a /memory range (AddMemorySpace, SetMemorySpaceAttributes, then
       Cpu->SetMemoryAttributes, which builds the page-table mapping the early MMU map does not have); a range that is
       already system memory is accepted and logged (step 3 then decides), anything else is refused;
    3. AllocatePages (AllocateAddress, EfiBootServicesData): from now on nothing in boot services can take the pages;
       EfiBootServicesData (not Reserved) so that RamDiskDxe publishes neither an NFIT nor the NVDIMM SSDT, and
       Windows can reuse the memory after ExitBootServices (PcdWatariRamDiskType = 0 is the debug switch);
    4. the trailer (last 4 KiB of the range): magic "WRAMDSK1", image_bytes, CRC-32 of the image; a mismatch or a missing
       trailer is logged and the disk is NOT registered (PcdWatariRamDiskVerify = FALSE skips only the CRC compare);
    5. EFI_RAM_DISK_PROTOCOL.Register: Block I/O + Device Path, then PartitionDxe (GPT) and the FAT driver follow.

  Trailer (little endian, 4 KiB): +0 "WRAMDSK1", +8 u64 image_bytes, +16 u32 crc32 (the UEFI CalculateCrc32 polynomial =
  the standard CRC-32), +20 32 bytes SHA-256 of the image (informational), rest zero.

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/Cpu.h>
#include <Protocol/DevicePath.h>
#include <Protocol/FdtClient.h>
#include <Protocol/RamDisk.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define WATARI_RAMDISK_TRAILER_SIZE  0x1000
#define WATARI_RAMDISK_NODE          "watari,ramdisk"

STATIC
UINT64
ReadBe (
  IN CONST UINT32  *Cells,
  IN UINTN         Count
  )
{
  UINT64  V;
  UINTN   I;

  V = 0;
  for (I = 0; I < Count; I++) {
    V = (V << 32) | SwapBytes32 (Cells[I]);
  }

  return V;
}

EFI_STATUS
EFIAPI
WatariM1RamDiskEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                       Status;
  FDT_CLIENT_PROTOCOL              *FdtClient;
  EFI_CPU_ARCH_PROTOCOL            *Cpu;
  EFI_RAM_DISK_PROTOCOL            *RamDisk;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Gcd;
  INT32                            Node;
  CONST VOID                       *Prop;
  UINT32                           PropSize;
  UINT64                           Base;
  UINT64                           Size;
  UINT64                           Attributes;
  EFI_PHYSICAL_ADDRESS             Addr;
  CONST UINT8                      *Trailer;
  UINT64                           ImageBytes;
  UINT32                           Crc;
  UINT32                           CrcWant;
  EFI_MEMORY_TYPE                  Type;

  Status = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL, (VOID **)&FdtClient);
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  //
  // 1. The node. No node: nothing to do (the production firmware without wel2.ramdisk).
  //
  Status = FdtClient->FindCompatibleNode (FdtClient, WATARI_RAMDISK_NODE, &Node);
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  Status = FdtClient->GetNodeProperty (FdtClient, Node, "reg", &Prop, &PropSize);
  if (EFI_ERROR (Status) || (PropSize != 4 * sizeof (UINT32))) {
    DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: the node has no usable reg (%r, %u bytes)\n", Status, PropSize));
    return EFI_SUCCESS;
  }

  Base = ReadBe ((CONST UINT32 *)Prop, 2);
  Size = ReadBe ((CONST UINT32 *)Prop + 2, 2);
  if ((Base == 0) || (Size <= WATARI_RAMDISK_TRAILER_SIZE) || ((Base | Size) & (EFI_PAGE_SIZE - 1)) != 0 || (Base + Size < Base)) {
    DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: bad range %lx + %lx in the node\n", Base, Size));
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_INFO, "WatariM1RamDisk: node: image at %lx, %lx bytes (with the trailer)\n", Base, Size));

  //
  // 2. Make the range system memory, as HighMemDxe does for a /memory range beyond the first.
  //
  Status = gDS->GetMemorySpaceDescriptor (Base, &Gcd);
  if (EFI_ERROR (Status) || (Gcd.BaseAddress + Gcd.Length < Base + Size)) {
    DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: %lx..+%lx is not inside one GCD descriptor (%r): not registered\n", Base, Size, Status));
    return EFI_SUCCESS;
  }

  if (Gcd.GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
    Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: no CPU architectural protocol: %r\n", Status));
      return EFI_SUCCESS;
    }

    Status = gDS->AddMemorySpace (EfiGcdMemoryTypeSystemMemory, Base, Size, EFI_MEMORY_WB);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: AddMemorySpace failed: %r\n", Status));
      return EFI_SUCCESS;
    }

    Status = gDS->SetMemorySpaceAttributes (Base, Size, EFI_MEMORY_WB);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "WatariM1RamDisk: SetMemorySpaceAttributes: %r\n", Status));
    }

    //
    // The early MMU map has only the first /memory range and the FV; this installs the mapping for the rest
    // (no RO/XP in the GCD, only on the page tables, as HighMemDxe).
    //
    Attributes = EFI_MEMORY_WB;
    if ((PcdGet64 (PcdDxeNxMemoryProtectionPolicy) & (1U << (UINT32)EfiConventionalMemory)) != 0) {
      Attributes |= EFI_MEMORY_XP;
    }

    Status = Cpu->SetMemoryAttributes (Cpu, Base, Size, Attributes);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: Cpu->SetMemoryAttributes failed: %r: not registered\n", Status));
      return EFI_SUCCESS;
    }
  } else if (Gcd.GcdMemoryType == EfiGcdMemoryTypeSystemMemory) {
    DEBUG ((DEBUG_INFO, "WatariM1RamDisk: the range is already system memory (the layer did not cut it out of /memory?)\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: the range is GCD type %d: not registered\n", (INT32)Gcd.GcdMemoryType));
    return EFI_SUCCESS;
  }

  //
  // 3. Claim the pages: nothing in boot services can allocate them from now on.
  //
  Type = (EFI_MEMORY_TYPE)FixedPcdGet32 (PcdWatariRamDiskType);
  Addr = Base;
  Status = gBS->AllocatePages (AllocateAddress, Type, EFI_SIZE_TO_PAGES (Size), &Addr);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: cannot claim %lx..+%lx as memory type %d: %r: not registered\n", Base, Size, (INT32)Type, Status));
    return EFI_SUCCESS;
  }

  //
  // 4. The trailer.
  //
  Trailer = (CONST UINT8 *)(UINTN)(Base + Size - WATARI_RAMDISK_TRAILER_SIZE);
  if (CompareMem (Trailer, "WRAMDSK1", 8) != 0) {
    DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: no trailer (magic) at %lx: not registered\n", (UINT64)(UINTN)Trailer));
    return EFI_SUCCESS;
  }

  ImageBytes = ReadUnaligned64 ((CONST UINT64 *)(Trailer + 8));
  CrcWant    = ReadUnaligned32 ((CONST UINT32 *)(Trailer + 16));
  if ((ImageBytes < 512) || (ImageBytes > Size - WATARI_RAMDISK_TRAILER_SIZE) || ((ImageBytes & 511) != 0)) {
    DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: trailer image_bytes %lx does not fit the range: not registered\n", ImageBytes));
    return EFI_SUCCESS;
  }

  if (FixedPcdGetBool (PcdWatariRamDiskVerify)) {
    Crc    = 0;
    Status = gBS->CalculateCrc32 ((VOID *)(UINTN)Base, (UINTN)ImageBytes, &Crc);
    if (EFI_ERROR (Status) || (Crc != CrcWant)) {
      DEBUG ((DEBUG_ERROR, "WatariM1RamDisk: CRC-32 mismatch (computed %08x, trailer %08x, %r): not registered\n", Crc, CrcWant, Status));
      return EFI_SUCCESS;
    }

    DEBUG ((DEBUG_INFO, "WatariM1RamDisk: CRC-32 %08x of %lx bytes verified\n", Crc, ImageBytes));
  } else {
    DEBUG ((DEBUG_WARN, "WatariM1RamDisk: CRC-32 check skipped by PcdWatariRamDiskVerify\n"));
  }

  //
  // 5. Register the virtual disk.
  //
  Status = gBS->LocateProtocol (&gEfiRamDiskProtocolGuid, NULL, (VOID **)&RamDisk);
  if (!EFI_ERROR (Status)) {
    Status = RamDisk->Register ((UINT64)Base, ImageBytes, &gEfiVirtualDiskGuid, NULL, &DevicePath);
  }

  DEBUG ((DEBUG_INFO, "WatariM1RamDisk: image at %lx, %lx bytes registered: %r\n", Base, ImageBytes, Status));
  return EFI_SUCCESS;
}
