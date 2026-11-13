// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * WatariM1AcpiDxe: publishes the ACPI tables of the Apple M1 platform (docs/design/watari-edk2-m1/
 * DESIGN.md 9.1). No QEMU table exists to replace (this firmware has no fw_cfg), so the driver only
 * calls EFI_ACPI_TABLE_PROTOCOL.InstallAcpiTable() for each blob:
 *
 *   FADT, MADT, GTDT, PPTT  built here by tools/acpigen for the model chosen at build time
 *                           ("j293" by default, "qemu-virt" for the bench
 *                           so that the GIC addresses in the MADT match QEMU's);
 *   DSDT                    the AML compiled from AcpiTables/Dsdt.asl, read from the firmware volume
 *                           (file gWatariM1DsdtFileGuid). AcpiTableDxe links it into the FADT
 *                           (X_DSDT) when it is installed [verified: E:MdeModulePkg/Universal/Acpi/
 *                           AcpiTableDxe/AcpiTableProtocol.c, "DSDT"/"X_DSDT" handling].
 *
 * No DBG2, SPCR, CSRT or MCFG (DESIGN 9.1). The driver also lists what is installed on the debug
 * log (the XSDT walk below), so that a bench run without a shell still shows the tables.
 */
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Protocol/AcpiTable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "acpigen.h"

// Model switch, set by the DSC (-D WATARI_M1_ACPIGEN_MODEL=qemu-virt adds this flag).
#ifdef WATARI_M1_MODEL_QEMU_VIRT
#define WATARI_M1_ACPIGEN_MODEL  "qemu-virt"
#else
#define WATARI_M1_ACPIGEN_MODEL  "j293"
#endif

#define WATARI_BUF_SIZE  2048

STATIC EFI_STATUS
InstallBlob (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable,
  IN CONST CHAR8              *Name,
  IN VOID                     *Table,
  IN UINTN                    Size
  )
{
  UINTN       Key;
  EFI_STATUS  Status;

  Status = AcpiTable->InstallAcpiTable (AcpiTable, Table, Size, &Key);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariM1AcpiDxe: InstallAcpiTable(%a, %lu bytes) failed: %r\n", Name, (UINT64)Size, Status));
  } else {
    DEBUG ((DEBUG_INFO, "WatariM1AcpiDxe: %a installed (%lu bytes, key %lu)\n", Name, (UINT64)Size, (UINT64)Key));
  }

  return Status;
}

/* Build one acpigen table into a fresh buffer and install it. Builder returns the length or < 0. */
#define BUILD_AND_INSTALL(Name, Call)                                                         \
  do {                                                                                        \
    ZeroMem (Buf, WATARI_BUF_SIZE);                                                           \
    Len = (Call);                                                                             \
    if (Len <= 0) {                                                                           \
      DEBUG ((DEBUG_ERROR, "WatariM1AcpiDxe: acpigen %a failed (%ld)\n", Name, Len));         \
      Failed = TRUE;                                                                          \
    } else if (EFI_ERROR (InstallBlob (AcpiTable, Name, Buf, (UINTN)Len))) {                  \
      Failed = TRUE;                                                                          \
    }                                                                                         \
  } while (0)

/* Debug listing of the installed tables, walked from the XSDT like an OS would. */
STATIC VOID
ListTables (
  VOID
  )
{
  EFI_ACPI_6_5_ROOT_SYSTEM_DESCRIPTION_POINTER                *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                                 *Xsdt;
  EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE                   *Fadt;
  UINT64                                                      *Entry;
  UINTN                                                       Count;
  UINTN                                                       I;
  EFI_STATUS                                                  Status;
  EFI_ACPI_DESCRIPTION_HEADER                                 *Hdr;
  CHAR8                                                       Sig[5];

  Status = EfiGetSystemConfigurationTable (&gEfiAcpiTableGuid, (VOID **)&Rsdp);
  if (EFI_ERROR (Status) || (Rsdp == NULL)) {
    DEBUG ((DEBUG_ERROR, "WatariM1AcpiDxe: no ACPI table in the configuration table\n"));
    return;
  }

  Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  DEBUG ((DEBUG_INFO, "WatariM1AcpiDxe: RSDP @ 0x%lx, XSDT @ 0x%lx (%u bytes)\n", (UINT64)(UINTN)Rsdp, (UINT64)(UINTN)Xsdt, Xsdt->Length));

  Count = (Xsdt->Length - sizeof (EFI_ACPI_DESCRIPTION_HEADER)) / sizeof (UINT64);
  Entry = (UINT64 *)(Xsdt + 1);
  Fadt  = NULL;
  for (I = 0; I < Count; I++) {
    Hdr = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)ReadUnaligned64 (&Entry[I]);
    CopyMem (Sig, &Hdr->Signature, 4);
    Sig[4] = 0;
    DEBUG ((DEBUG_INFO, "WatariM1AcpiDxe: table %a @ 0x%lx length %u rev %u\n", Sig, (UINT64)(UINTN)Hdr, Hdr->Length, Hdr->Revision));
    if (Hdr->Signature == EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
      Fadt = (EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE *)Hdr;
    }
  }

  if (Fadt != NULL) {
    Hdr = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Fadt->XDsdt;
    DEBUG ((DEBUG_INFO, "WatariM1AcpiDxe: FADT X_DSDT = 0x%lx\n", Fadt->XDsdt));
    if (Hdr != NULL) {
      CopyMem (Sig, &Hdr->Signature, 4);
      Sig[4] = 0;
      DEBUG ((DEBUG_INFO, "WatariM1AcpiDxe: table %a (via X_DSDT) @ 0x%lx length %u rev %u\n", Sig, (UINT64)(UINTN)Hdr, Hdr->Length, Hdr->Revision));
    }
  }
}

EFI_STATUS
EFIAPI
WatariM1AcpiDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  CONST acpigen_model      *Model;
  UINT8                    *Buf;
  VOID                     *Dsdt;
  UINTN                    DsdtSize;
  INT64                    Len;
  BOOLEAN                  Failed;
  EFI_STATUS               Status;

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Model = acpigen_find_model (WATARI_M1_ACPIGEN_MODEL);
  if (Model == NULL) {
    DEBUG ((DEBUG_ERROR, "WatariM1AcpiDxe: acpigen model \"%a\" not found\n", WATARI_M1_ACPIGEN_MODEL));
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_INFO, "WatariM1AcpiDxe: acpigen model \"%a\", %lu CPUs\n", WATARI_M1_ACPIGEN_MODEL, (UINT64)Model->ncpus));

  Buf = AllocateZeroPool (WATARI_BUF_SIZE);
  if (Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Failed = FALSE;
  BUILD_AND_INSTALL ("FADT", acpigen_build_fadt (Buf, WATARI_BUF_SIZE, NULL, Model->fadt));
  BUILD_AND_INSTALL (
    "MADT",
    acpigen_build_madt (Buf, WATARI_BUF_SIZE, NULL, Model->cpus, Model->ncpus, Model->gicd, &Model->madt_opts)
    );
  BUILD_AND_INSTALL ("GTDT", acpigen_build_gtdt (Buf, WATARI_BUF_SIZE, NULL, Model->gtdt));
  BUILD_AND_INSTALL ("PPTT", acpigen_build_pptt (Buf, WATARI_BUF_SIZE, NULL, Model->cpus, Model->ncpus));
  FreePool (Buf);

  // The DSDT (AML from AcpiTables/Dsdt.asl), stored in the firmware volume as a raw section.
  Dsdt     = NULL;
  DsdtSize = 0;
  Status   = GetSectionFromFv (&gWatariM1DsdtFileGuid, EFI_SECTION_RAW, 0, &Dsdt, &DsdtSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WatariM1AcpiDxe: DSDT not found in the firmware volume: %r\n", Status));
    Failed = TRUE;
  } else {
    if (EFI_ERROR (InstallBlob (AcpiTable, "DSDT", Dsdt, DsdtSize))) {
      Failed = TRUE;
    }

    FreePool (Dsdt);
  }

  ListTables ();

  return Failed ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
