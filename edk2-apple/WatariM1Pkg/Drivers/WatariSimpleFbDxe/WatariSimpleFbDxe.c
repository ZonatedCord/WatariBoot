/** @file
  Watari Boot (E11): GOP on the guest device tree's "simple-framebuffer" node.

  Watari EL2 hands the guest (all or the lower part of) the panel and describes it as
  /chosen/framebuffer@<base> { compatible = "simple-framebuffer"; reg; width; height; stride;
  format } (DESIGN 8.2-8.4). This driver turns that node into one GOP mode:

    format "x8r8g8b8"    -> PixelBlueGreenRedReserved8BitPerColor
    format "x2r10g10b10" -> PixelBitMask, red 0x3FF00000, green 0x000FFC00, blue 0x000003FF,
                            reserved 0xC0000000 (the truthful description of the M1 panel), or,
                            with PcdWatariFbReportBgr8, PixelBlueGreenRedReserved8BitPerColor
                            (untruthful: the colours of an 8-bit writer come out wrong, shapes
                            stay visible; Microsoft's boot requirement is 8-bit BGR, DESIGN 8.5)
    anything else        -> no GOP (a wrong picture is worse than none); the firmware boots on.

  Blt goes through FrameBufferBltLib, which handles bit masks wider than 8 bits (it derives one
  shift per channel from the highest set bit of the mask): an 8-bit channel value v lands in the
  top eight bits of a 10-bit field, so 0xFF becomes 0x3FC, not 0x3FF.

  Written from the UEFI specification (Graphics Output Protocol) and the simple-framebuffer
  device-tree binding; the structure of EDK2's own OvmfPkg/QemuRamfbDxe is the model. No code of
  other projects' SimpleFb drivers is used.

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Protocol/DevicePath.h>
#include <Protocol/FdtClient.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define FB_BYTES_PER_PIXEL  4

STATIC EFI_HANDLE                            mGopHandle;
STATIC FRAME_BUFFER_CONFIGURE                *mBltConfigure;
STATIC UINTN                                 mBltConfigureSize;
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mModeInfo;

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE  mMode = {
  1,                                              // MaxMode
  0,                                              // Mode
  &mModeInfo,                                     // Info
  sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),  // SizeOfInfo
};

STATIC
EFI_STATUS
EFIAPI
GopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  if ((Info == NULL) || (SizeOfInfo == NULL) || (ModeNumber != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocateCopyPool (sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION), &mModeInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GopBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer  OPTIONAL,
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN  UINTN                              SourceX,
  IN  UINTN                              SourceY,
  IN  UINTN                              DestinationX,
  IN  UINTN                              DestinationY,
  IN  UINTN                              Width,
  IN  UINTN                              Height,
  IN  UINTN                              Delta
  )
{
  return FrameBufferBlt (
           mBltConfigure,
           BltBuffer,
           BltOperation,
           SourceX,
           SourceY,
           DestinationX,
           DestinationY,
           Width,
           Height,
           Delta
           );
}

STATIC
EFI_STATUS
EFIAPI
GopSetMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN  UINT32                        ModeNumber
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black;

  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }

  //
  // The one mode is always set; SetMode clears the screen to black (UEFI spec).
  //
  ZeroMem (&Black, sizeof (Black));
  return FrameBufferBlt (
           mBltConfigure,
           &Black,
           EfiBltVideoFill,
           0,
           0,
           0,
           0,
           mModeInfo.HorizontalResolution,
           mModeInfo.VerticalResolution,
           0
           );
}

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  mGop = {
  GopQueryMode,
  GopSetMode,
  GopBlt,
  &mMode,
};

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  ACPI_ADR_DEVICE_PATH        Adr;
  EFI_DEVICE_PATH_PROTOCOL    End;
} WATARI_FB_DEVICE_PATH;
#pragma pack ()

/**
  Read the big-endian cells of the node's "reg" (base, size), whatever the parent's cell counts
  were. /chosen of the layer's tree has no #address-cells/#size-cells of its own, so the count is
  taken from the length: 16 bytes = 2+2 cells, 12 = 2+1, 8 = 1+1.
**/
STATIC
EFI_STATUS
ParseReg (
  IN  CONST VOID  *Reg,
  IN  UINT32      RegSize,
  OUT UINT64      *Base,
  OUT UINT64      *Size
  )
{
  CONST UINT32  *Cells;

  Cells = Reg;
  switch (RegSize) {
    case 16:
      *Base = LShiftU64 (SwapBytes32 (Cells[0]), 32) | SwapBytes32 (Cells[1]);
      *Size = LShiftU64 (SwapBytes32 (Cells[2]), 32) | SwapBytes32 (Cells[3]);
      return EFI_SUCCESS;
    case 12:
      *Base = LShiftU64 (SwapBytes32 (Cells[0]), 32) | SwapBytes32 (Cells[1]);
      *Size = SwapBytes32 (Cells[2]);
      return EFI_SUCCESS;
    case 8:
      *Base = SwapBytes32 (Cells[0]);
      *Size = SwapBytes32 (Cells[1]);
      return EFI_SUCCESS;
    default:
      return EFI_UNSUPPORTED;
  }
}

STATIC
EFI_STATUS
GetU32Property (
  IN  FDT_CLIENT_PROTOCOL  *FdtClient,
  IN  INT32                Node,
  IN  CONST CHAR8          *Name,
  OUT UINT32               *Value
  )
{
  EFI_STATUS  Status;
  CONST VOID  *Prop;
  UINT32      Size;

  Status = FdtClient->GetNodeProperty (FdtClient, Node, Name, &Prop, &Size);
  if (EFI_ERROR (Status) || (Size != sizeof (UINT32))) {
    DEBUG ((DEBUG_WARN, "WatariSimpleFb: property '%a' missing or not a u32\n", Name));
    return EFI_NOT_FOUND;
  }

  *Value = SwapBytes32 (*(CONST UINT32 *)Prop);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
WatariSimpleFbEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS             Status;
  FDT_CLIENT_PROTOCOL    *FdtClient;
  INT32                  Node;
  CONST VOID             *Prop;
  UINT32                 PropSize;
  UINT64                 Base;
  UINT64                 RegSize;
  UINT32                 Width;
  UINT32                 Height;
  UINT32                 Stride;
  CONST CHAR8            *Format;
  BOOLEAN                Is10Bit;
  UINT64                 NeedBytes;
  WATARI_FB_DEVICE_PATH  *DevicePath;
  EFI_PHYSICAL_ADDRESS   MapBase;
  UINT64                 MapLen;

  Status = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL, (VOID **)&FdtClient);
  ASSERT_EFI_ERROR (Status);

  Status = FdtClient->FindCompatibleNode (FdtClient, "simple-framebuffer", &Node);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "WatariSimpleFb: no simple-framebuffer node, no GOP\n"));
    return EFI_NOT_FOUND;
  }

  //
  // The node: reg, width, height, stride, format. Anything missing or odd: no GOP.
  //
  Status = FdtClient->GetNodeProperty (FdtClient, Node, "reg", &Prop, &PropSize);
  if (EFI_ERROR (Status) || EFI_ERROR (ParseReg (Prop, PropSize, &Base, &RegSize))) {
    DEBUG ((DEBUG_WARN, "WatariSimpleFb: bad or missing 'reg', no GOP\n"));
    return EFI_UNSUPPORTED;
  }

  if (EFI_ERROR (GetU32Property (FdtClient, Node, "width", &Width)) ||
      EFI_ERROR (GetU32Property (FdtClient, Node, "height", &Height)) ||
      EFI_ERROR (GetU32Property (FdtClient, Node, "stride", &Stride)))
  {
    return EFI_UNSUPPORTED;
  }

  Status = FdtClient->GetNodeProperty (FdtClient, Node, "format", &Prop, &PropSize);
  if (EFI_ERROR (Status) || (PropSize == 0) || (((CONST CHAR8 *)Prop)[PropSize - 1] != '\0')) {
    DEBUG ((DEBUG_WARN, "WatariSimpleFb: bad or missing 'format', no GOP\n"));
    return EFI_UNSUPPORTED;
  }

  Format = Prop;
  if (AsciiStrCmp (Format, "x8r8g8b8") == 0) {
    Is10Bit = FALSE;
  } else if (AsciiStrCmp (Format, "x2r10g10b10") == 0) {
    Is10Bit = TRUE;
  } else {
    DEBUG ((DEBUG_WARN, "WatariSimpleFb: unknown format '%a', no GOP\n", Format));
    return EFI_UNSUPPORTED;
  }

  NeedBytes = (UINT64)Stride * Height;
  if ((Width == 0) || (Height == 0) || (Width > 16384) || (Height > 16384) ||
      ((Stride % FB_BYTES_PER_PIXEL) != 0) || (Stride < (UINT64)Width * FB_BYTES_PER_PIXEL) ||
      (Base == 0) || ((Base & EFI_PAGE_MASK) != 0) || (RegSize < NeedBytes) ||
      (Base + RegSize < Base) || (Base + RegSize > MAX_UINTN))
  {
    DEBUG ((
      DEBUG_WARN,
      "WatariSimpleFb: inconsistent node (base 0x%lx size 0x%lx %ux%u stride %u), no GOP\n",
      Base,
      RegSize,
      Width,
      Height,
      Stride
      ));
    return EFI_UNSUPPORTED;
  }

  //
  // The panel is not RAM of the firmware's memory map: add it to the GCD as memory-mapped I/O and
  // make it write-combining (Normal non-cacheable), which also maps it in the DXE page tables.
  //
  MapBase = Base;
  MapLen  = ALIGN_VALUE (NeedBytes, EFI_PAGE_SIZE);
  Status  = gDS->AddMemorySpace (
                   EfiGcdMemoryTypeMemoryMappedIo,
                   MapBase,
                   MapLen,
                   EFI_MEMORY_UC | EFI_MEMORY_WC
                   );
  if (EFI_ERROR (Status) && (Status != EFI_ACCESS_DENIED)) {
    DEBUG ((DEBUG_ERROR, "WatariSimpleFb: AddMemorySpace: %r, no GOP\n", Status));
    return Status;
  }

  Status = gDS->SetMemorySpaceAttributes (MapBase, MapLen, EFI_MEMORY_WC);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariSimpleFb: SetMemorySpaceAttributes(WC): %r, no GOP\n", Status));
    return Status;
  }

  //
  // The one mode.
  //
  mModeInfo.Version              = 0;
  mModeInfo.HorizontalResolution = Width;
  mModeInfo.VerticalResolution   = Height;
  mModeInfo.PixelsPerScanLine    = Stride / FB_BYTES_PER_PIXEL;
  if (!Is10Bit || FixedPcdGetBool (PcdWatariFbReportBgr8)) {
    mModeInfo.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  } else {
    mModeInfo.PixelFormat                   = PixelBitMask;
    mModeInfo.PixelInformation.RedMask      = 0x3FF00000;
    mModeInfo.PixelInformation.GreenMask    = 0x000FFC00;
    mModeInfo.PixelInformation.BlueMask     = 0x000003FF;
    mModeInfo.PixelInformation.ReservedMask = 0xC0000000;
  }

  mMode.FrameBufferBase = Base;
  mMode.FrameBufferSize = NeedBytes;

  Status = FrameBufferBltConfigure ((VOID *)(UINTN)Base, &mModeInfo, mBltConfigure, &mBltConfigureSize);
  if (Status == RETURN_BUFFER_TOO_SMALL) {
    mBltConfigure = AllocatePool (mBltConfigureSize);
    if (mBltConfigure == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = FrameBufferBltConfigure ((VOID *)(UINTN)Base, &mModeInfo, mBltConfigure, &mBltConfigureSize);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariSimpleFb: FrameBufferBltConfigure: %r, no GOP\n", Status));
    return Status;
  }

  //
  // Device path (Vendor / ADR) so that the boot manager can put the handle in ConOut and
  // GraphicsConsoleDxe can bind to it.
  //
  DevicePath = AllocateZeroPool (sizeof (*DevicePath));
  if (DevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  DevicePath->Vendor.Header.Type    = HARDWARE_DEVICE_PATH;
  DevicePath->Vendor.Header.SubType = HW_VENDOR_DP;
  SetDevicePathNodeLength (&DevicePath->Vendor.Header, sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&DevicePath->Vendor.Guid, &gWatariSimpleFbDevicePathGuid);
  DevicePath->Adr.Header.Type    = ACPI_DEVICE_PATH;
  DevicePath->Adr.Header.SubType = ACPI_ADR_DP;
  DevicePath->Adr.ADR            = ACPI_DISPLAY_ADR (1, 0, 0, 1, 0, ACPI_ADR_DISPLAY_TYPE_EXTERNAL_DIGITAL, 0, 0);
  SetDevicePathNodeLength (&DevicePath->Adr.Header, sizeof (ACPI_ADR_DEVICE_PATH));
  SetDevicePathEndNode (&DevicePath->End);

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mGopHandle,
                  &gEfiDevicePathProtocolGuid,
                  DevicePath,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGop,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariSimpleFb: installing the GOP: %r\n", Status));
    FreePool (DevicePath);
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "WatariSimpleFb: GOP %ux%u, stride %u bytes, buffer 0x%lx size 0x%lx, node format %a, reported as %a\n",
    Width,
    Height,
    Stride,
    Base,
    NeedBytes,
    Format,
    (mModeInfo.PixelFormat == PixelBitMask) ? "PixelBitMask R3FF00000 G000FFC00 B000003FF" : "PixelBlueGreenRedReserved8BitPerColor"
    ));
  return EFI_SUCCESS;
}
