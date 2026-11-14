/** @file
  Watari Boot (E12): the platform boot manager policy of the M1 firmware.

  - BeforeConsole: signal EndOfDxe, put the serial console (the hypercall UART behind SerialDxe
    and TerminalDxe) in ConIn/ConOut/ErrOut and every GOP handle in ConOut/ErrOut.
  - AfterConsole: a banner; the UEFI Shell is registered as an INACTIVE boot option (listed in
    the boot menu, never started by itself, no hotkey).
  - Never waits for a key: no hotkey is registered and PcdPlatformBootTimeOut is 0 in the DSC,
    so BdsDxe does not wait.
  - UnableToBoot: ConnectAll + RefreshAllBootOption, then every discovered active option outside
    the firmware volume, then (T16a) \EFI\Microsoft\Boot\bootmgfw.efi on every file system that
    has it (EDK2's own discovery only knows \EFI\BOOT\BOOTAA64.EFI, and the installer's Boot####
    variable is lost: variables are RAM-backed).
  - UnableToBoot (nothing bootable, which is the normal first boot): the bench build, which sets
    PcdUefiShellDefaultBootEnable, starts the Shell; every other build starts the built-in
    self-test (Application/WatariM1SelfTest), which ends in ResetSystem. Only if that fails does
    it fall back to the Boot Manager Menu, as ArmPkg's library does.

  The structure (and the serial console device path) follow EDK2's ArmPkg/Library/
  PlatformBootManagerLib; PCI, USB, capsule and boot-logo handling are left out.

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Guid/SerialPortLibVendor.h>
#include <Guid/TtyTerm.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformBootManagerLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define WATARI_WINDOWS_BOOTMGR  L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"

#define DP_NODE_LEN(Type)  { (UINT8)sizeof (Type), (UINT8)(sizeof (Type) >> 8) }

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH            SerialDxe;
  UART_DEVICE_PATH              Uart;
  VENDOR_DEFINED_DEVICE_PATH    TermType;
  EFI_DEVICE_PATH_PROTOCOL      End;
} PLATFORM_SERIAL_CONSOLE;
#pragma pack ()

STATIC PLATFORM_SERIAL_CONSOLE  mSerialConsole = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, DP_NODE_LEN (VENDOR_DEVICE_PATH) },
    EDKII_SERIAL_PORT_LIB_VENDOR_GUID
  },
  {
    { MESSAGING_DEVICE_PATH, MSG_UART_DP, DP_NODE_LEN (UART_DEVICE_PATH) },
    0,                                      // Reserved
    FixedPcdGet64 (PcdUartDefaultBaudRate), // BaudRate
    FixedPcdGet8 (PcdUartDefaultDataBits),  // DataBits
    FixedPcdGet8 (PcdUartDefaultParity),    // Parity
    FixedPcdGet8 (PcdUartDefaultStopBits)   // StopBits
  },
  {
    {
      MESSAGING_DEVICE_PATH, MSG_VENDOR_DP,
      DP_NODE_LEN (VENDOR_DEFINED_DEVICE_PATH)
    }
    // the GUID is filled in at run time
  },
  {
    END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
    DP_NODE_LEN (EFI_DEVICE_PATH_PROTOCOL)
  }
};

/**
  Add the device path of every handle that has a GOP to ConOut and ErrOut.
**/
STATIC
VOID
AddGraphicsOutputs (
  VOID
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                *Handles;
  UINTN                     NoHandles;
  UINTN                     Idx;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &NoHandles, &Handles);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "%a: no GOP handle (%r): serial console only\n", __func__, Status));
    return;
  }

  for (Idx = 0; Idx < NoHandles; Idx++) {
    DevicePath = DevicePathFromHandle (Handles[Idx]);
    if (DevicePath == NULL) {
      DEBUG ((DEBUG_ERROR, "%a: GOP handle %p has no device path\n", __func__, Handles[Idx]));
      continue;
    }

    Status = EfiBootManagerUpdateConsoleVariable (ConOut, DevicePath, NULL);
    if (!EFI_ERROR (Status)) {
      Status = EfiBootManagerUpdateConsoleVariable (ErrOut, DevicePath, NULL);
    }

    DEBUG ((
      EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO,
      "%a: GOP handle %p added to ConOut and ErrOut: %r\n",
      __func__,
      Handles[Idx],
      Status
      ));
  }

  FreePool (Handles);
}

/**
  The device path of a file of the firmware volume this image came from.
**/
STATIC
EFI_DEVICE_PATH_PROTOCOL *
FvFileDevicePath (
  IN CONST EFI_GUID  *FileGuid
  )
{
  EFI_STATUS                         Status;
  EFI_LOADED_IMAGE_PROTOCOL          *LoadedImage;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_DEVICE_PATH_PROTOCOL           *DevicePath;

  Status = gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  ASSERT_EFI_ERROR (Status);

  EfiInitializeFwVolDevicepathNode (&FileNode, FileGuid);
  DevicePath = DevicePathFromHandle (LoadedImage->DeviceHandle);
  ASSERT (DevicePath != NULL);
  return AppendDevicePathNode (DevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
}

/**
  Register an FV file as a boot option unless it is already one.
**/
STATIC
VOID
RegisterFvBootOption (
  IN CONST EFI_GUID  *FileGuid,
  IN CHAR16          *Description,
  IN UINT32          Attributes
  )
{
  EFI_STATUS                    Status;
  INTN                          OptionIndex;
  EFI_BOOT_MANAGER_LOAD_OPTION  NewOption;
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOptions;
  UINTN                         BootOptionCount;
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;

  DevicePath = FvFileDevicePath (FileGuid);
  ASSERT (DevicePath != NULL);

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             Attributes,
             Description,
             DevicePath,
             NULL,
             0
             );
  ASSERT_EFI_ERROR (Status);
  FreePool (DevicePath);

  BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);
  OptionIndex = EfiBootManagerFindLoadOption (&NewOption, BootOptions, BootOptionCount);
  if (OptionIndex == -1) {
    Status = EfiBootManagerAddLoadOptionVariable (&NewOption, MAX_UINTN);
    ASSERT_EFI_ERROR (Status);
  }

  EfiBootManagerFreeLoadOption (&NewOption);
  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
}

/**
  Start an FV file as a boot option now.
**/
STATIC
VOID
BootFvFile (
  IN CONST EFI_GUID  *FileGuid,
  IN CHAR16          *Description
  )
{
  EFI_STATUS                    Status;
  EFI_BOOT_MANAGER_LOAD_OPTION  NewOption;
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;

  DevicePath = FvFileDevicePath (FileGuid);
  ASSERT (DevicePath != NULL);

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_ACTIVE,
             Description,
             DevicePath,
             NULL,
             0
             );
  ASSERT_EFI_ERROR (Status);
  FreePool (DevicePath);

  EfiBootManagerBoot (&NewOption);
  EfiBootManagerFreeLoadOption (&NewOption);
}

/**
  TRUE if the device path points into a firmware volume (UiApp, the Shell, our self-test): those
  are the firmware's own applications, not something the user installed.
**/
STATIC
BOOLEAN
IsFirmwareVolumePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *Path
  )
{
  while (!IsDevicePathEnd (Path)) {
    if ((DevicePathType (Path) == MEDIA_DEVICE_PATH) &&
        ((DevicePathSubType (Path) == MEDIA_PIWG_FW_FILE_DP) || (DevicePathSubType (Path) == MEDIA_PIWG_FW_VOL_DP)))
    {
      return TRUE;
    }

    Path = NextDevicePathNode (Path);
  }

  return FALSE;
}

/**
  TRUE if the file system has the file.
**/
STATIC
BOOLEAN
VolumeHasFile (
  IN EFI_HANDLE  Handle,
  IN CHAR16      *FileName
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;

  Status = gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Status = Root->Open (Root, &File, FileName, EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR (Status)) {
    File->Close (File);
  }

  Root->Close (Root);
  return !EFI_ERROR (Status);
}

/**
  T16a: EfiBootManagerRefreshAllBootOption only creates options for the removable-media default
  file (\EFI\BOOT\BOOTAA64.EFI) [verified: MdeModulePkg/Library/UefiBootManagerLib/BmBoot.c
  BmEnumerateBootOptions 2289-2420 makes options holding the device path of a disk/volume, and
  BmExpandMediaDevicePath 1106-1230 (FileDevicePath with EFI_REMOVABLE_MEDIA_FILE_NAME at 1133
  and 1209, called from BmGetNextLoadOptionDevicePath 1746) expands it to that file only]. A Windows ESP written by the installer may carry only
  \EFI\Microsoft\Boot\bootmgfw.efi, and the installer's own Boot#### variable is gone (RAM-backed
  variables). So: on every file system that has bootmgfw.efi and that no option already points at
  (by exact file path), create an option and boot it.
**/
STATIC
VOID
BootWindowsBootManagers (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *BootOptions,
  IN UINTN                         BootOptionCount
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    *Handles;
  UINTN                         NoHandles;
  UINTN                         Idx;
  UINTN                         Opt;
  EFI_DEVICE_PATH_PROTOCOL      *FilePath;
  EFI_BOOT_MANAGER_LOAD_OPTION  NewOption;
  BOOLEAN                       Known;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &NoHandles, &Handles);
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Idx = 0; Idx < NoHandles; Idx++) {
    if (!VolumeHasFile (Handles[Idx], WATARI_WINDOWS_BOOTMGR)) {
      continue;
    }

    FilePath = FileDevicePath (Handles[Idx], WATARI_WINDOWS_BOOTMGR);
    if (FilePath == NULL) {
      continue;
    }

    if (IsFirmwareVolumePath (FilePath)) {
      FreePool (FilePath);
      continue;
    }

    Known = FALSE;
    for (Opt = 0; Opt < BootOptionCount; Opt++) {
      if ((GetDevicePathSize (BootOptions[Opt].FilePath) == GetDevicePathSize (FilePath)) &&
          (CompareMem (BootOptions[Opt].FilePath, FilePath, GetDevicePathSize (FilePath)) == 0))
      {
        Known = TRUE;
        break;
      }
    }

    if (Known) {
      FreePool (FilePath);
      continue;
    }

    Status = EfiBootManagerInitializeLoadOption (
               &NewOption,
               LoadOptionNumberUnassigned,
               LoadOptionTypeBoot,
               LOAD_OPTION_ACTIVE,
               L"Windows Boot Manager",
               FilePath,
               NULL,
               0
               );
    FreePool (FilePath);
    if (EFI_ERROR (Status)) {
      continue;
    }

    DEBUG ((DEBUG_INFO, "%a: trying %s on file system handle %p\n", __func__, NewOption.Description, Handles[Idx]));
    EfiBootManagerBoot (&NewOption);
    EfiBootManagerFreeLoadOption (&NewOption);
  }

  FreePool (Handles);
}

VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
  )
{
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);
  EfiBootManagerDispatchDeferredImages ();

  AddGraphicsOutputs ();

  //
  // The serial console (the hypercall UART): ConIn, ConOut and ErrOut.
  //
  CopyGuid (&mSerialConsole.TermType.Guid, &gEfiTtyTermGuid);
  EfiBootManagerUpdateConsoleVariable (ConIn, (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut, (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);
}

VOID
EFIAPI
PlatformBootManagerAfterConsole (
  VOID
  )
{
  Print (L"Watari M1 firmware (WatariM1Pkg) under Watari EL2\n");

  //
  // The Shell is listed in the boot menu but inactive: nothing starts it unasked, and no key
  // is ever waited for.
  //
  RegisterFvBootOption (&gUefiShellFileGuid, L"UEFI Shell", 0);
}

VOID
EFIAPI
PlatformBootManagerWaitCallback (
  UINT16  TimeoutRemain
  )
{
  //
  // PcdPlatformBootTimeOut is 0: BdsDxe never calls this.
  //
}

VOID
EFIAPI
PlatformBootManagerUnableToBoot (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_BOOT_MANAGER_LOAD_OPTION  BootManagerMenu;
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOptions;
  UINTN                         BootOptionCount;
  UINTN                         Index;

  EfiBootManagerConnectAll ();
  EfiBootManagerRefreshAllBootOption ();

  //
  // Variables are RAM-backed here, so the boot options are re-created at every boot: whatever
  // ConnectAll just discovered (a FAT volume with \EFI\BOOT\BOOTAA64.EFI, ...) must get its
  // chance before the self-test, or the self-test would reset the machine forever.
  //
  BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);
  for (Index = 0; Index < BootOptionCount; Index++) {
    if (((BootOptions[Index].Attributes & LOAD_OPTION_ACTIVE) == 0) || IsFirmwareVolumePath (BootOptions[Index].FilePath)) {
      continue;
    }

    DEBUG ((DEBUG_INFO, "%a: trying boot option %u (%s)\n", __func__, (UINT32)BootOptions[Index].OptionNumber, BootOptions[Index].Description));
    EfiBootManagerBoot (&BootOptions[Index]);
  }

  //
  // T16a: Windows' own boot manager where EDK2's discovery does not look.
  //
  BootWindowsBootManagers (BootOptions, BootOptionCount);

  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);

  if (FixedPcdGetBool (PcdUefiShellDefaultBootEnable)) {
    //
    // Bench build: the Shell is the default.
    //
    BootFvFile (&gUefiShellFileGuid, L"UEFI Shell (default)");
  } else {
    //
    // Nothing bootable and nobody to ask: prove the machine works (DESIGN 10.3). The
    // self-test resets the system when it passes and does not return.
    //
    BootFvFile (&gWatariM1SelfTestFileGuid, L"Watari M1 self-test");
  }

  //
  // The chosen application returned (it failed): the Boot Manager Menu, as ArmPkg does.
  //
  Status = EfiBootManagerGetBootManagerMenu (&BootManagerMenu);
  if (EFI_ERROR (Status)) {
    return;
  }

  for ( ; ;) {
    EfiBootManagerBoot (&BootManagerMenu);
  }
}
