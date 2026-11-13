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
  of the bench build) and, as pixels, to the status strip through PrintXY. A RELEASE build has no
  debug log: its verdict is only on the panel.
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

STATIC
VOID
StripText (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN CONST CHAR16                  *Text,
  IN UINTN                         Number
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  White;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black;
  UINTN                          H;
  UINTN                          Count;

  if (Gop == NULL) {
    return;
  }

  H = Gop->Mode->Info->VerticalResolution;
  if (H <= 2 * STRIP_ROWS) {
    return;
  }

  SetMem (&White, sizeof (White), 0xFF);
  ZeroMem (&Black, sizeof (Black));
  Count = PrintXY (16, H - 40, &White, &Black, L"%-40s %u/%u", Text, Number, TICKS_WANTED);
  if (Count == 0) {
    DEBUG ((DEBUG_WARN, "SELFTEST: PrintXY drew nothing\n"));
  }
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
    StripText (Gop, L"SELFTEST running, timer ticks", 0);
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &Timer);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (Timer, TimerPeriodic, TICK_100NS);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SELFTEST FAIL: timer event: %r\n", Status));
    StripText (Gop, L"SELFTEST FAIL: no timer event", 0);
    return Status;
  }

  for (Ticks = 1; Ticks <= TICKS_WANTED; Ticks++) {
    Status = gBS->WaitForEvent (1, &Timer, &Index);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "SELFTEST FAIL: WaitForEvent: %r\n", Status));
      StripText (Gop, L"SELFTEST FAIL: WaitForEvent", Ticks);
      return Status;
    }

    DEBUG ((DEBUG_INFO, "SELFTEST tick %u/%u\n", Ticks, TICKS_WANTED));
    StripText (Gop, L"SELFTEST running, timer ticks", Ticks);
  }

  gBS->SetTimer (Timer, TimerCancel, 0);
  gBS->CloseEvent (Timer);

  DEBUG ((DEBUG_INFO, "SELFTEST PASS\n"));
  StripText (Gop, L"SELFTEST PASS, timer ticks", TICKS_WANTED);
  if (Gop == NULL) {
    Print (L"SELFTEST PASS\n");
  }

  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
