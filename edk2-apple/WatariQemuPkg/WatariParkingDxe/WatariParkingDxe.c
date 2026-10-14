/** @file
  WatariParkingDxe: Multiprocessor Parking Protocol per QEMU virt (-smp N).

  - all'avvio: alloca una pagina da 4 KB EfiReservedMemoryType per CPU (anche per il BSP, indice 0),
    scrive ProcessorId = 0xFFFFFFFF e JumpAddress = 0, copia lo stub in +0x800, pubblica gli
    indirizzi con gWatariParkingProtocolGuid (usato da WatariAcpiOverride per MADT/FADT);
  - a ReadyToBoot: sveglia ogni redistributor, poi accende i core 1..N-1 con PSCI CPU_ON
    (function id 0xC4000003, conduit scelto da ArmMonitorLib dal DTB) puntati allo stub, con
    context_id = indirizzo della mailbox.
  ProcessorId atteso = indice di CPU (= GICC.CPUInterfaceNumber generato da acpigen).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/ArmMonitorLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/EventGroup.h>
#include <Protocol/WatariParking.h>

#ifndef WATARI_PMODE
#define WATARI_PMODE 0
#endif
#define PSCI_CPU_ON_64     0xC4000003ULL
#define PARKING_NCPUS      4            // deve coincidere con -smp (e con qemu_virt_model in acpigen)
#define GICR_FRAME_STRIDE  0x20000      // GICv3: RD_base + SGI_base = 2 x 64 KB per redistributor
#define GICR_WAKER         0x14

extern CONST UINT8  ParkStubStart[];
extern CONST UINT8  ParkStubEnd[];

STATIC WATARI_PARKING_INFO  mInfo;
STATIC EFI_EVENT            mReadyToBootEvent;
STATIC BOOLEAN              mStarted;

STATIC VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN             Cpu;
  UINTN             Waker;
  UINT32            V;
  UINTN             Tries;
  ARM_MONITOR_ARGS  Args;

  if (mStarted) {
    return;
  }
  mStarted = TRUE;

  for (Cpu = 1; Cpu < mInfo.CpuCount; Cpu++) {
    // 1. redistributor del core: ProcessorSleep = 0 (puo' farlo il BSP via MMIO).
    Waker = (UINTN)PcdGet64 (PcdGicRedistributorsBase) + Cpu * GICR_FRAME_STRIDE + GICR_WAKER;
    V     = MmioRead32 (Waker);
#if !(WATARI_PMODE & 16)
    MmioWrite32 (Waker, V & ~0x2U);
    for (Tries = 0; Tries < 100000 && (MmioRead32 (Waker) & 0x4U) != 0; Tries++) {
    }
#else
    Tries = 0;  // PMODE bit 4: redistributor lasciato a Windows
    (VOID)V;
    (VOID)Tries;
#endif
#if WATARI_PMODE & 4
    continue;   // PMODE bit 2: nessun CPU_ON (controllo: i core restano spenti per PSCI di Windows)
#endif

    // 2. PSCI CPU_ON(target = MPIDR Aff0 = indice, entry = stub, context = mailbox).
    ZeroMem (&Args, sizeof (Args));
    Args.Arg0 = (UINTN)PSCI_CPU_ON_64;
    Args.Arg1 = Cpu;
    Args.Arg2 = (UINTN)(mInfo.MailboxAddress[Cpu] + WATARI_PARK_OFF_CODE);
    Args.Arg3 = (UINTN)mInfo.MailboxAddress[Cpu];
    ArmMonitorCall (&Args);
    DEBUG ((
      DEBUG_INFO,
      "WatariParking: CPU_ON cpu%lu mailbox=0x%lx entry=0x%lx -> PSCI %ld (waker=0x%x)\n",
      (UINT64)Cpu,
      (UINT64)mInfo.MailboxAddress[Cpu],
      (UINT64)Args.Arg2,
      (INT64)Args.Arg0,
      MmioRead32 (Waker)
      ));
  }
}

EFI_STATUS
EFIAPI
WatariParkingEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Page;
  UINTN                 Cpu;
  UINT8                 *P;
  UINTN                 StubSize;
  EFI_HANDLE            Handle;

  StubSize = (UINTN)(ParkStubEnd - ParkStubStart);
  ASSERT (StubSize < (WATARI_PARK_OFF_EXPECT_ID - WATARI_PARK_OFF_CODE));

  mInfo.Version  = 1;
  mInfo.CpuCount = PARKING_NCPUS;
  for (Cpu = 0; Cpu < mInfo.CpuCount; Cpu++) {
    Status = gBS->AllocatePages (AllocateAnyPages, EfiReservedMemoryType, 1, &Page);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "WatariParking: AllocatePages cpu%lu: %r\n", (UINT64)Cpu, Status));
      return Status;
    }

    P = (UINT8 *)(UINTN)Page;
    ZeroMem (P, SIZE_4KB);
    *(UINT32 *)(P + WATARI_PARK_OFF_PROCESSOR_ID) = 0xFFFFFFFFU;
    *(UINT64 *)(P + WATARI_PARK_OFF_JUMP_ADDRESS) = 0;
    *(UINT32 *)(P + WATARI_PARK_OFF_EXPECT_ID)    = (UINT32)Cpu;
    *(UINT32 *)(P + WATARI_PARK_OFF_PMODE)        = (UINT32)WATARI_PMODE;
    if (Cpu != 0) {
      CopyMem (P + WATARI_PARK_OFF_CODE, ParkStubStart, StubSize);
    }

    // I core partono con MMU e cache spente: la pagina deve essere in memoria, non nella cache.
    WriteBackInvalidateDataCacheRange (P, SIZE_4KB);
    InvalidateInstructionCacheRange (P, SIZE_4KB);  // la variante senza Range e' un ASSERT(FALSE) in ArmCacheMaintenanceLib
    mInfo.MailboxAddress[Cpu] = Page;
    DEBUG ((DEBUG_INFO, "WatariParking: cpu%lu mailbox 0x%lx\n", (UINT64)Cpu, (UINT64)Page));
  }

  Handle = NULL;
  Status = gBS->InstallProtocolInterface (&Handle, &gWatariParkingProtocolGuid, EFI_NATIVE_INTERFACE, &mInfo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariParking: CreateEventEx: %r\n", Status));
  }

  return EFI_SUCCESS;
}
