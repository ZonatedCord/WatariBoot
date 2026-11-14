/** @file
  Watari Boot (E12, DESIGN 10.3): the built-in self-test.

  With no boot device and no keyboard, a first boot on the real machine must prove things by
  itself. In order:
    1. print the memory-map summary and the GOP mode (console: serial and, with a GOP, the panel);
    2. draw four full-height colour bars through the GOP (red, green, blue, white), leaving the
       bottom STRIP_ROWS rows for status text, so that the channel order of the panel's format
       is visible to the eye, to a camera and to a pixel check on the bench;
    3. count ten one-second periodic timer events (interrupt delivery to the firmware);
    4. report SELFTEST PASS and call ResetSystem (EfiResetCold): PSCI SYSTEM_RESET through
       Watari EL2, the visible end of a successful run.

  Text printed after the bars would be drawn over them by the graphics console, so the progress
  and the verdict go to the debug log (the hypercall console of the production build, the PL011
  of the bench build) and, as ten boxes that turn white one by one, to the status strip. A
  RELEASE build has no debug log: its verdict is only the ten white boxes.
  Without a GOP (unknown framebuffer format) the test still runs: no bars, same ticks, same reset.

  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define TICKS_WANTED  10
#define STRIP_ROWS    64          // bottom rows kept for status text (no bars there)
#define TICK_100NS    10000000ULL // one second
#define BOX_X0        16          // status strip: ten boxes, box i at x = BOX_X0 + i * BOX_PITCH,
#define BOX_PITCH     56          // y = H - STRIP_ROWS + 12, BOX_SIZE square
#define BOX_SIZE      40

STATIC
VOID
MemorySummary (
  VOID
  )
{
  EFI_STATUS             Status;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINT64                 Conventional;
  UINT64                 Total;

  MapSize = 0;
  Map     = NULL;
  Status  = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return;
  }

  MapSize += 4 * DescSize;
  Map      = AllocatePool (MapSize);
  if (Map == NULL) {
    return;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (!EFI_ERROR (Status)) {
    Conventional = 0;
    Total        = 0;
    for (Desc = Map; (UINT8 *)Desc < (UINT8 *)Map + MapSize; Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescSize)) {
      Total += Desc->NumberOfPages;
      if (Desc->Type == EfiConventionalMemory) {
        Conventional += Desc->NumberOfPages;
      }
    }

    Print (
      L"memory map: %lu descriptors, %lu MB described, %lu MB conventional\n",
      (UINT64)(MapSize / DescSize),
      RShiftU64 (Total * EFI_PAGE_SIZE, 20),
      RShiftU64 (Conventional * EFI_PAGE_SIZE, 20)
      );
    DEBUG ((
      DEBUG_INFO,
      "SELFTEST memory map: %lu descriptors, %lu MB described, %lu MB conventional\n",
      (UINT64)(MapSize / DescSize),
      RShiftU64 (Total * EFI_PAGE_SIZE, 20),
      RShiftU64 (Conventional * EFI_PAGE_SIZE, 20)
      ));
  }

  FreePool (Map);
}

STATIC
VOID
DrawBars (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Colors[4] = {
    { 0x00, 0x00, 0xFF, 0x00 },   // red   (Blue, Green, Red, Reserved)
    { 0x00, 0xFF, 0x00, 0x00 },   // green
    { 0xFF, 0x00, 0x00, 0x00 },   // blue
    { 0xFF, 0xFF, 0xFF, 0x00 },   // white
  };
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL         Black;
  UINTN                                 W;
  UINTN                                 H;
  UINTN                                 BarRows;
  UINTN                                 Index;
  UINTN                                 X0;
  UINTN                                 X1;

  W = Gop->Mode->Info->HorizontalResolution;
  H = Gop->Mode->Info->VerticalResolution;

  ZeroMem (&Black, sizeof (Black));
  Gop->Blt (Gop, &Black, EfiBltVideoFill, 0, 0, 0, 0, W, H, 0);

  BarRows = (H > 2 * STRIP_ROWS) ? H - STRIP_ROWS : H;
  for (Index = 0; Index < 4; Index++) {
    X0 = (Index * W) / 4;
    X1 = ((Index + 1) * W) / 4;
    Gop->Blt (Gop, &Colors[Index], EfiBltVideoFill, 0, 0, X0, 0, X1 - X0, BarRows, 0);
  }
}

/**
  One of ten boxes in the status strip: dark grey = tick not yet seen, white = tick seen. Boxes
  rather than text: PrintXY returned 0 and drew nothing on this platform (cause not
  investigated), and ten filling boxes are easier to read on a photograph anyway.
**/
STATIC
VOID
StripBox (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINTN                         Index,
  IN BOOLEAN                       Lit
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color;
  UINTN                          W;
  UINTN                          H;

  if (Gop == NULL) {
    return;
  }

  W = Gop->Mode->Info->HorizontalResolution;
  H = Gop->Mode->Info->VerticalResolution;
  if ((H <= 2 * STRIP_ROWS) || (W < BOX_X0 + TICKS_WANTED * BOX_PITCH)) {
    return;
  }

  SetMem (&Color, sizeof (Color), Lit ? 0xFF : 0x40);
  Color.Reserved = 0;
  Gop->Blt (Gop, &Color, EfiBltVideoFill, 0, 0, BOX_X0 + Index * BOX_PITCH, H - STRIP_ROWS + 12, BOX_SIZE, BOX_SIZE, 0);
}

/**
  One BufferToVideo + VideoToBlt round trip on a pixel of the status strip (below the boxes,
  outside every checked area), compared in software. With PixelBitMask the library's conversion
  is lossy in the low bits, so each channel may differ by up to 4.
**/
STATIC
BOOLEAN
RoundTrip (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Out;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  In;
  UINTN                          X;
  UINTN                          Y;
  EFI_STATUS                     Status;
  BOOLEAN                        Ok;

  Out.Blue     = 0x20;
  Out.Green    = 0x40;
  Out.Red      = 0x80;
  Out.Reserved = 0;
  ZeroMem (&In, sizeof (In));
  X = Gop->Mode->Info->HorizontalResolution - 8;
  Y = Gop->Mode->Info->VerticalResolution - 8;

  Status = Gop->Blt (Gop, &Out, EfiBltBufferToVideo, 0, 0, X, Y, 1, 1, 0);
  if (!EFI_ERROR (Status)) {
    Status = Gop->Blt (Gop, &In, EfiBltVideoToBltBuffer, X, Y, 0, 0, 1, 1, 0);
  }

  Ok = !EFI_ERROR (Status) &&
       (In.Red >= Out.Red - 4) && (In.Red <= Out.Red + 4) &&
       (In.Green >= Out.Green - 4) && (In.Green <= Out.Green + 4) &&
       (In.Blue >= Out.Blue - 4) && (In.Blue <= Out.Blue + 4);
  DEBUG ((
    DEBUG_INFO,
    "SELFTEST roundtrip %a: wrote R%02x G%02x B%02x, read R%02x G%02x B%02x (%r)\n",
    Ok ? "OK" : "FAIL",
    Out.Red, Out.Green, Out.Blue, In.Red, In.Green, In.Blue, Status
    ));
  return Ok;
}

EFI_STATUS
EFIAPI
WatariM1SelfTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_EVENT                     Timer;
  UINTN                         Index;
  UINTN                         Ticks;

  Print (L"Watari M1 self-test\n");
  DEBUG ((DEBUG_INFO, "SELFTEST start\n"));
  MemorySummary ();

  Gop    = NULL;
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  }

  if (EFI_ERROR (Status)) {
    Gop = NULL;
    Print (L"GOP: none\n");
    DEBUG ((DEBUG_INFO, "SELFTEST GOP none\n"));
  } else {
    Print (
      L"GOP: %ux%u, format %u, %u pixels per scan line\n",
      Gop->Mode->Info->HorizontalResolution,
      Gop->Mode->Info->VerticalResolution,
      (UINTN)Gop->Mode->Info->PixelFormat,
      Gop->Mode->Info->PixelsPerScanLine
      );
    DEBUG ((
      DEBUG_INFO,
      "SELFTEST GOP %ux%u pixel format %d scanline %u\n",
      Gop->Mode->Info->HorizontalResolution,
      Gop->Mode->Info->VerticalResolution,
      (INT32)Gop->Mode->Info->PixelFormat,
      Gop->Mode->Info->PixelsPerScanLine
      ));
    DrawBars (Gop);
    DEBUG ((DEBUG_INFO, "SELFTEST bars drawn\n"));
    if (!RoundTrip (Gop)) {
      DEBUG ((DEBUG_ERROR, "SELFTEST FAIL: Blt round trip\n"));
      return EFI_DEVICE_ERROR;
    }
    for (Ticks = 0; Ticks < TICKS_WANTED; Ticks++) {
      StripBox (Gop, Ticks, FALSE);
    }
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &Timer);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (Timer, TimerPeriodic, TICK_100NS);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SELFTEST FAIL: timer event: %r\n", Status));
    return Status;
  }

  for (Ticks = 1; Ticks <= TICKS_WANTED; Ticks++) {
    Status = gBS->WaitForEvent (1, &Timer, &Index);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "SELFTEST FAIL: WaitForEvent: %r\n", Status));
      return Status;
    }

    DEBUG ((DEBUG_INFO, "SELFTEST tick %u/%u\n", Ticks, TICKS_WANTED));
    StripBox (Gop, Ticks - 1, TRUE);
  }

  gBS->SetTimer (Timer, TimerCancel, 0);
  gBS->CloseEvent (Timer);

  DEBUG ((DEBUG_INFO, "SELFTEST PASS\n"));
  if (Gop == NULL) {
    Print (L"SELFTEST PASS\n");
  }

  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
