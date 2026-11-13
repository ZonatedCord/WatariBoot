/** @file
  Watari Boot (E12): the platform boot manager policy of the M1 firmware.

  - BeforeConsole: signal EndOfDxe, put the serial console (the hypercall UART behind SerialDxe
    and TerminalDxe) in ConIn/ConOut/ErrOut and every GOP handle in ConOut/ErrOut.
  - AfterConsole: a banner; the UEFI Shell is registered as an INACTIVE boot option (listed in
    the boot menu, never started by itself, no hotkey).
  - Never waits for a key: no hotkey is registered and PcdPlatformBootTimeOut is 0 in the DSC,
    so BdsDxe does not wait.
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

  EfiBootManagerConnectAll ();
  EfiBootManagerRefreshAllBootOption ();

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
