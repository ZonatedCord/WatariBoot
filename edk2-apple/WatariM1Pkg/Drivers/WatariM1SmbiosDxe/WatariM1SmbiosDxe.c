/** @file
  Watari Boot (E12): minimal SMBIOS for the M1 platform. Fixed strings, deliberately no serial
  number, asset tag or UUID (a serial number is personal data and nothing here needs one).

    Type 0  BIOS Information       vendor "Watari Boot", version of this firmware, fixed date
    Type 1  System Information     the machine; serial number and UUID absent
    Type 2  Baseboard Information
    Type 3  System Enclosure       notebook

  MdeModulePkg's SmbiosDxe adds the end-of-table record (type 127) and the entry point.
  The model identifier is the one of the 13-inch M1 MacBook Pro, from memory
  [hypothesis: MacBookPro17,1 is j293's identifier; not verified from a public source, no network use].

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>
#include <Protocol/Smbios.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define MAX_STRINGS  6

/**
  Add one record: the formal part (FormalSize bytes at Formal) followed by up to MAX_STRINGS
  strings (string i of the table is Strings[i-1]; the formal part refers to them by number).
**/
STATIC
EFI_STATUS
AddRecord (
  IN EFI_SMBIOS_PROTOCOL  *Smbios,
  IN VOID                 *Formal,
  IN UINTN                FormalSize,
  IN CONST CHAR8          *Strings[MAX_STRINGS]
  )
{
  EFI_STATUS             Status;
  EFI_SMBIOS_HANDLE      Handle;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  UINTN                  Total;
  UINTN                  Index;
  UINT8                  *Cursor;
  UINTN                  Len;

  Total = FormalSize;
  for (Index = 0; Index < MAX_STRINGS; Index++) {
    if (Strings[Index] != NULL) {
      Total += AsciiStrLen (Strings[Index]) + 1;
    }
  }

  Total += 2;   // string-set terminator (or, with no strings, the two zero bytes)

  Record = AllocateZeroPool (Total);
  if (Record == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (Record, Formal, FormalSize);
  Cursor = (UINT8 *)Record + FormalSize;
  for (Index = 0; Index < MAX_STRINGS; Index++) {
    if (Strings[Index] != NULL) {
      Len = AsciiStrLen (Strings[Index]) + 1;
      CopyMem (Cursor, Strings[Index], Len);
      Cursor += Len;
    }
  }

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Status = Smbios->Add (Smbios, NULL, &Handle, Record);
  FreePool (Record);
  return Status;
}

EFI_STATUS
EFIAPI
WatariM1SmbiosEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                Status;
  EFI_SMBIOS_PROTOCOL       *Smbios;
  SMBIOS_TABLE_TYPE0        T0;
  SMBIOS_TABLE_TYPE1        T1;
  SMBIOS_TABLE_TYPE2        T2;
  SMBIOS_TABLE_TYPE3        T3;
  CONST CHAR8               *S[MAX_STRINGS];
  UINTN                     Index;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  ASSERT_EFI_ERROR (Status);

  //
  // Type 0: BIOS Information.
  //
  ZeroMem (&T0, sizeof (T0));
  T0.Hdr.Type                            = SMBIOS_TYPE_BIOS_INFORMATION;
  T0.Hdr.Length                          = sizeof (T0);
  T0.Hdr.Handle                          = 0;
  T0.Vendor                              = 1;
  T0.BiosVersion                         = 2;
  T0.BiosSegment                         = 0;
  T0.BiosReleaseDate                     = 3;
  T0.BiosSize                            = 0xFF;                       // 16 MB or more: see ExtendedBiosSize
  T0.BiosCharacteristics.BiosCharacteristicsNotSupported = 1;
  T0.BIOSCharacteristicsExtensionBytes[1] = 0x08;                      // bit 3: UEFI specification supported (not the VM bit)
  T0.SystemBiosMajorRelease              = 0;
  T0.SystemBiosMinorRelease              = 1;
  T0.EmbeddedControllerFirmwareMajorRelease = 0xFF;
  T0.EmbeddedControllerFirmwareMinorRelease = 0xFF;
  T0.ExtendedBiosSize.Size               = 3;                          // 3 MB image
  T0.ExtendedBiosSize.Unit               = 0;                          // MB
  for (Index = 0; Index < MAX_STRINGS; Index++) {
    S[Index] = NULL;
  }

  S[0]   = "Watari Boot";
  S[1]   = "WatariM1Pkg 0.1";
  S[2]   = "11/13/2026";
  Status = AddRecord (Smbios, &T0, sizeof (T0), S);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariM1Smbios: type 0: %r\n", Status));
    return Status;
  }

  //
  // Type 1: System Information. No serial number (index 0), no UUID (all zero = not present).
  //
  ZeroMem (&T1, sizeof (T1));
  T1.Hdr.Type      = SMBIOS_TYPE_SYSTEM_INFORMATION;
  T1.Hdr.Length    = sizeof (T1);
  T1.Manufacturer  = 1;
  T1.ProductName   = 2;
  T1.Version       = 3;
  T1.SerialNumber  = 0;
  T1.WakeUpType    = SystemWakeupTypeUnknown;
  T1.SKUNumber     = 0;
  T1.Family        = 4;
  for (Index = 0; Index < MAX_STRINGS; Index++) {
    S[Index] = NULL;
  }

  S[0]   = "Apple Inc.";
  S[1]   = "MacBookPro17,1";
  S[2]   = "Watari EL2 + WatariM1Pkg";
  S[3]   = "Apple Silicon";
  Status = AddRecord (Smbios, &T1, sizeof (T1), S);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariM1Smbios: type 1: %r\n", Status));
    return Status;
  }

  //
  // Type 2: Baseboard Information (board type "motherboard", no serial number, no asset tag).
  //
  ZeroMem (&T2, sizeof (T2));
  T2.Hdr.Type             = SMBIOS_TYPE_BASEBOARD_INFORMATION;
  T2.Hdr.Length           = sizeof (T2);
  T2.Manufacturer         = 1;
  T2.ProductName          = 2;
  T2.Version              = 3;
  T2.SerialNumber         = 0;
  T2.AssetTag             = 0;
  T2.FeatureFlag.Motherboard = 1;
  T2.FeatureFlag.Replaceable = 0;
  T2.LocationInChassis    = 0;
  T2.ChassisHandle        = 0;
  T2.BoardType            = BaseBoardTypeMotherBoard;
  T2.NumberOfContainedObjectHandles = 0;
  for (Index = 0; Index < MAX_STRINGS; Index++) {
    S[Index] = NULL;
  }

  S[0]   = "Apple Inc.";
  S[1]   = "j293";
  S[2]   = "t8103";
  Status = AddRecord (Smbios, &T2, sizeof (T2), S);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariM1Smbios: type 2: %r\n", Status));
    return Status;
  }

  //
  // Type 3: System Enclosure (notebook).
  //
  ZeroMem (&T3, sizeof (T3));
  T3.Hdr.Type          = SMBIOS_TYPE_SYSTEM_ENCLOSURE;
  T3.Hdr.Length        = sizeof (T3);
  T3.Manufacturer      = 1;
  T3.Type              = MiscChassisTypeNotebook;
  T3.Version           = 0;
  T3.SerialNumber      = 0;
  T3.AssetTag          = 0;
  T3.BootupState       = ChassisStateSafe;
  T3.PowerSupplyState  = ChassisStateSafe;
  T3.ThermalState      = ChassisStateSafe;
  T3.SecurityStatus    = ChassisSecurityStatusNone;
  for (Index = 0; Index < MAX_STRINGS; Index++) {
    S[Index] = NULL;
  }

  S[0]   = "Apple Inc.";
  Status = AddRecord (Smbios, &T3, sizeof (T3), S);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariM1Smbios: type 3: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "WatariM1Smbios: types 0, 1, 2, 3 added (no serial number, no UUID)\n"));
  return EFI_SUCCESS;
}
