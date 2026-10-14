// SPDX-License-Identifier: MIT
/*
 * WatariAcpiOverride: sostituisce MADT ("APIC") e GTDT installate da QEMU/EDK2 con quelle
 * generate da acpigen per il modello qemu-virt.
 *
 * Quando: l'AcpiPlatformDxe di OvmfPkg installa le tabelle di QEMU dopo il nostro avvio, quindi
 * il lavoro si fa in un evento ReadyToBoot (tutte le tabelle ci sono, il sistema operativo non
 * e' ancora partito). Se i protocolli ACPI non ci sono, si registra un avviso e si esce.
 * Come: EFI_ACPI_SDT_PROTOCOL.GetAcpiTable per trovare la tabella e la sua TableKey,
 * EFI_ACPI_TABLE_PROTOCOL.UninstallAcpiTable(key), poi InstallAcpiTable con la nuova.
 */
#include <Uefi.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/EventGroup.h>

#include "acpigen.h"

#define WATARI_BUF_SIZE 1024

STATIC EFI_EVENT mReadyToBootEvent;
STATIC BOOLEAN   mDone;

/* Cerca la prima tabella con la firma data; restituisce la chiave oppure FALSE. */
STATIC BOOLEAN
FindTable (
  IN  EFI_ACPI_SDT_PROTOCOL  *Sdt,
  IN  UINT32                 Signature,
  OUT UINTN                  *Key,
  OUT UINT32                 *OldLength
  )
{
  UINTN                        Index;
  EFI_ACPI_SDT_HEADER          *Table;
  EFI_ACPI_TABLE_VERSION       Version;

  for (Index = 0; ; Index++) {
    if (EFI_ERROR (Sdt->GetAcpiTable (Index, &Table, &Version, Key))) {
      return FALSE;
    }
    if (Table->Signature == Signature) {
      *OldLength = Table->Length;
      return TRUE;
    }
  }
}

STATIC EFI_STATUS
ReplaceTable (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable,
  IN EFI_ACPI_SDT_PROTOCOL    *Sdt,
  IN CONST CHAR8              *Name,
  IN UINT32                   Signature,
  IN VOID                     *NewTable,
  IN UINTN                    NewSize
  )
{
  UINTN       Key;
  UINT32      OldLength;
  UINTN       NewKey;
  EFI_STATUS  Status;

  if (FindTable (Sdt, Signature, &Key, &OldLength)) {
    DEBUG ((DEBUG_INFO, "WatariAcpiOverride: %a esistente (%u byte, chiave %lu): la rimuovo\n",
            Name, OldLength, (UINT64)Key));
    Status = AcpiTable->UninstallAcpiTable (AcpiTable, Key);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "WatariAcpiOverride: UninstallAcpiTable(%a) fallita: %r\n", Name, Status));
      return Status;
    }
  } else {
    DEBUG ((DEBUG_WARN, "WatariAcpiOverride: %a non presente, la installo comunque\n", Name));
  }

  Status = AcpiTable->InstallAcpiTable (AcpiTable, NewTable, NewSize, &NewKey);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariAcpiOverride: InstallAcpiTable(%a) fallita: %r\n", Name, Status));
    return Status;
  }
  DEBUG ((DEBUG_INFO, "WatariAcpiOverride: %a acpigen installata (%lu byte, chiave %lu)\n",
          Name, (UINT64)NewSize, (UINT64)NewKey));
  return EFI_SUCCESS;
}

STATIC VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  EFI_ACPI_SDT_PROTOCOL    *Sdt;
  CONST acpigen_model      *Model;
  UINT8                    *Buf;
  INT64                    Len;
  EFI_STATUS               Status;

  if (mDone) {
    return;
  }
  mDone = TRUE;

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTable);
  if (!EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&Sdt);
  }
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "WatariAcpiOverride: protocolli ACPI non disponibili (%r): niente da fare\n", Status));
    return;
  }

  Model = acpigen_find_model ("qemu-virt");
  if (Model == NULL) {
    DEBUG ((DEBUG_ERROR, "WatariAcpiOverride: modello qemu-virt non trovato\n"));
    return;
  }

  Buf = AllocateZeroPool (WATARI_BUF_SIZE);
  if (Buf == NULL) {
    return;
  }

  Len = acpigen_build_madt (Buf, WATARI_BUF_SIZE, NULL, Model->cpus, Model->ncpus,
                            Model->gicd, &Model->madt_opts);
  if (Len > 0) {
    DEBUG ((DEBUG_INFO, "WatariAcpiOverride: MADT generata da acpigen: %ld byte, %lu CPU\n",
            Len, (UINT64)Model->ncpus));
    ReplaceTable (AcpiTable, Sdt, "MADT", SIGNATURE_32 ('A', 'P', 'I', 'C'), Buf, (UINTN)Len);
  } else {
    DEBUG ((DEBUG_ERROR, "WatariAcpiOverride: acpigen_build_madt errore %ld\n", Len));
  }

  ZeroMem (Buf, WATARI_BUF_SIZE);
  Len = acpigen_build_gtdt (Buf, WATARI_BUF_SIZE, NULL, Model->gtdt);
  if (Len > 0) {
    DEBUG ((DEBUG_INFO, "WatariAcpiOverride: GTDT generata da acpigen: %ld byte\n", Len));
    ReplaceTable (AcpiTable, Sdt, "GTDT", SIGNATURE_32 ('G', 'T', 'D', 'T'), Buf, (UINTN)Len);
  } else {
    DEBUG ((DEBUG_ERROR, "WatariAcpiOverride: acpigen_build_gtdt errore %ld\n", Len));
  }

  FreePool (Buf);
}

EFI_STATUS
EFIAPI
WatariAcpiOverrideEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "WatariAcpiOverride: caricato, attendo ReadyToBoot\n"));
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OnReadyToBoot, NULL,
                               &gEfiEventReadyToBootGuid, &mReadyToBootEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariAcpiOverride: CreateEventEx fallita: %r\n", Status));
  }
  return EFI_SUCCESS;
}
