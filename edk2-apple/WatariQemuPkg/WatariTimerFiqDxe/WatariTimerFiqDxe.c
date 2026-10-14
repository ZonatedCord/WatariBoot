/** @file
  Watari Boot, V6 metodo A (docs/design/watariboot/ESPERIMENTO-F0.md sez. 4.4): configura i PPI
  del timer ARM (INTID 27 = timer virtuale, INTID 30 = timer fisico NS EL1) in Group 0, abilita
  GICD_CTLR.EnableGrp0 e ICC_IGRPEN0_EL1, cosi' che il GICv3 li segnali come FIQ e non come IRQ.

  Scelta del momento. Il Depex richiede gHardwareInterruptProtocolGuid, prodotto da ArmGicDxe:
  il driver parte quindi DOPO la configurazione del GIC (che con GICD_CTLR.DS=1 mette tutto in
  Group 1 NS). Ma la modifica NON si applica subito: il TimerDxe di UEFI usa il PPI come IRQ e il
  firmware non ha un gestore di FIQ utile, quindi un tick in Group 0 durante DXE andrebbe perso o
  finirebbe nel gestore di eccezioni di default. La si applica nell'evento ExitBootServices, quando
  il firmware ha finito con il timer e l'OS sta per prendere il controllo.
  Si disabilitano prima i due PPI (GICR_ICENABLER0), poi si cambia gruppo; lo stato di abilitazione
  e' lasciato all'OS, che riprogramma comunque il GIC (ipotesi: Windows puo' rimetterli in Group 1).

  Solo boot core. Il redistributor e ICC_IGRPEN0_EL1 sono per-CPU e questo codice gira solo sul core
  che esegue ExitBootServices. Per una prima prova basta: i core secondari partono dal reset del
  redistributor, dove gicr_igroupr0 = 0 (Group 0) [ipotesi, ESPERIMENTO-F0 4.4.1], ma IGRPEN0 non
  e' impostato da nessuno su di loro. Per l'estensione ai core secondari servira' il codice di
  parcheggio (G4).

  Fonti:
   - GICv3 Architecture Specification (Arm IHI 0069): GICR_IGROUPR0 (frame SGI, offset 0x0080),
     GICR_ICENABLER0 (0x0180), GICR_TYPER (0x0008), GICD_CTLR (0x0000: bit0 EnableGrp0,
     bit1 EnableGrp1NS con DS=1, bit6 DS), ICC_IGRPEN0_EL1 = S3_0_C12_C12_6, bit0 Enable.
   - EDK2 ArmPkg/Include/Library/ArmGicLib.h: ARM_GIC_ICDDCR (GICD_CTLR), ARM_GIC_ICDISR (0x80,
     che sul frame SGI e' IGROUPR0), ARM_GICR_ICENABLER, ARM_GICR_TYPER, dimensioni dei frame.
   - EDK2 ArmPkg/Drivers/ArmGicDxe/GicV3/ArmGicV3Dxe.c: scansione dei redistributor per MPIDR.

  Copyright (c) Watari Boot contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/ArmLib.h>
#include <Library/ArmGicLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define GICD_CTLR_ENABLE_GRP0  BIT0
#define PPI_VIRT_TIMER         27
#define PPI_PHYS_NS_TIMER      30
#define TIMER_PPI_MASK         ((UINT32)(BIT27 | BIT30))

STATIC EFI_EVENT  mExitBsEvent;

/** Legge ICC_IGRPEN0_EL1 (S3_0_C12_C12_6). */
STATIC
UINT64
ReadIgrpen0 (
  VOID
  )
{
  UINT64  V;

  __asm__ volatile ("mrs %0, S3_0_C12_C12_6" : "=r" (V));
  return V;
}

/** Scrive ICC_IGRPEN0_EL1. */
STATIC
VOID
WriteIgrpen0 (
  UINT64  V
  )
{
  __asm__ volatile ("msr S3_0_C12_C12_6, %0\n\tisb" : : "r" (V) : "memory");
}

/** Base del frame di controllo del redistributor del core corrente, 0 se non trovato. */
STATIC
UINTN
FindMyRedistributor (
  VOID
  )
{
  UINTN   Base;
  UINT64  Mpidr;
  UINT32  MyAff;
  UINT64  Typer;

  Mpidr = ArmReadMpidr ();
  // GICR_TYPER[63:32] = Aff3.Aff2.Aff1.Aff0; MPIDR: Aff0 [7:0], Aff1 [15:8], Aff2 [23:16], Aff3 [39:32].
  MyAff = (UINT32)((Mpidr & 0x00FFFFFF) | (((Mpidr >> 32) & 0xFF) << 24));

  Base = (UINTN)PcdGet64 (PcdGicRedistributorsBase);
  for ( ; ;) {
    Typer = MmioRead64 (Base + ARM_GICR_TYPER);
    if ((UINT32)ARM_GICR_TYPER_GET_AFFINITY (Typer) == MyAff) {
      return Base;
    }

    if ((Typer & ARM_GICR_TYPER_LAST) != 0) {
      return 0;
    }

    Base += ARM_GICR_CTLR_FRAME_SIZE + ARM_GICR_SGI_PPI_FRAME_SIZE;
    if ((Typer & ARM_GICR_TYPER_VLPIS) != 0) {
      Base += ARM_GICR_SGI_VLPI_FRAME_SIZE + ARM_GICR_SGI_RESERVED_FRAME_SIZE;
    }
  }
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN   Gicd;
  UINTN   Gicr;
  UINTN   Sgi;
  UINT32  Ctlr;
  UINT32  IgroupBefore;
  UINT32  IgroupAfter;
  UINT32  EnabledBefore;
  UINT64  IgrpenBefore;

  Gicd = (UINTN)PcdGet64 (PcdGicDistributorBase);
  Gicr = FindMyRedistributor ();
  if (Gicr == 0) {
    DEBUG ((DEBUG_ERROR, "WatariTimerFiq: redistributor del core non trovato (MPIDR=0x%lx)\n", ArmReadMpidr ()));
    return;
  }

  Sgi = Gicr + ARM_GICR_CTLR_FRAME_SIZE;   // frame SGI/PPI

  EnabledBefore = MmioRead32 (Sgi + 0x0100);   // GICR_ISENABLER0 (lettura: bit abilitati)
  IgroupBefore  = MmioRead32 (Sgi + ARM_GIC_ICDISR);
  Ctlr          = MmioRead32 (Gicd + ARM_GIC_ICDDCR);
  IgrpenBefore  = ReadIgrpen0 ();

  // 1. Spegne i PPI del timer prima di cambiare gruppo.
  MmioWrite32 (Sgi + ARM_GICR_ICENABLER, TIMER_PPI_MASK);

  // 2. IGROUPR0 bit 27 e 30 a 0 = Group 0 (con DS=1, vedi 4.4.1).
  MmioWrite32 (Sgi + ARM_GIC_ICDISR, IgroupBefore & ~TIMER_PPI_MASK);
  IgroupAfter = MmioRead32 (Sgi + ARM_GIC_ICDISR);

  // 3. GICD_CTLR.EnableGrp0 (read-modify-write: non si toccano ARE, DS, EnableGrp1NS).
  MmioWrite32 (Gicd + ARM_GIC_ICDDCR, Ctlr | GICD_CTLR_ENABLE_GRP0);

  // 4. ICC_IGRPEN0_EL1.Enable.
  WriteIgrpen0 (1);

  DEBUG ((
    DEBUG_INFO,
    "WatariTimerFiq: GICR=0x%lx GICD=0x%lx PPI %d,%d -> Group 0\n"
    "WatariTimerFiq: IGROUPR0 0x%08x -> 0x%08x (letto dopo), ISENABLER0 prima 0x%08x (PPI spenti)\n"
    "WatariTimerFiq: GICD_CTLR 0x%08x -> 0x%08x (letto dopo), ICC_IGRPEN0_EL1 0x%lx -> 0x%lx\n",
    Gicr,
    Gicd,
    PPI_VIRT_TIMER,
    PPI_PHYS_NS_TIMER,
    IgroupBefore,
    IgroupAfter,
    EnabledBefore,
    Ctlr,
    MmioRead32 (Gicd + ARM_GIC_ICDDCR),
    IgrpenBefore,
    ReadIgrpen0 ()
    ));
}

EFI_STATUS
EFIAPI
WatariTimerFiqEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBsEvent
                  );
  DEBUG ((DEBUG_INFO, "WatariTimerFiq: registrato su ExitBootServices (%r)\n", Status));
  return Status;
}
