/** @file
  Protocollo privato tra WatariParkingDxe e WatariAcpiOverride: indirizzi fisici delle mailbox del
  Multiprocessor Parking Protocol (una pagina da 4 KB per CPU, EfiReservedMemoryType).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef WATARI_PARKING_H_
#define WATARI_PARKING_H_

#define WATARI_PARKING_MAX_CPUS  8

//
// Layout della mailbox (specifica MS "Multi-processor Startup for ARM Platforms" v1, sez. 3.4).
//
#define WATARI_PARK_OFF_PROCESSOR_ID  0x000   // UINT32, 0xFFFFFFFF = core parcheggiato
#define WATARI_PARK_OFF_JUMP_ADDRESS  0x008   // UINT64, 0 = nessun salto richiesto
#define WATARI_PARK_OFF_CODE          0x800   // area firmware: stub di parcheggio
#define WATARI_PARK_OFF_PMODE         0xFEC   // area firmware: maschera di bit PMODE per lo stub (UINT32)
#define WATARI_PARK_OFF_EXPECT_ID     0xFF0   // area firmware: ProcessorId atteso da questo core (UINT32)

typedef struct {
  UINT32    Version;                               // versione del parking protocol (1)
  UINT32    CpuCount;                              // CPU descritte (BSP incluso, indice 0)
  UINT64    MailboxAddress[WATARI_PARKING_MAX_CPUS];
} WATARI_PARKING_INFO;

extern EFI_GUID  gWatariParkingProtocolGuid;

#endif
