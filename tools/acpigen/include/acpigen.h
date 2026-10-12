// SPDX-License-Identifier: MIT
/*
 * acpigen: generazione binaria di MADT, GTDT, PPTT e CSRT.
 *
 * Fonti dei layout (verificato = confrontato con la fonte):
 *  - Header SDT comune: ACPI 6.5 sez. 5.2.6; edk2 MdePkg/Include/IndustryStandard/Acpi65.h
 *    (EFI_ACPI_DESCRIPTION_HEADER).
 *  - MADT rev 6, GICC (tipo 0x0B, 82 byte) e GICD (tipo 0x0C, 24 byte): ACPI 6.5 sez. 5.2.12.14/.15;
 *    Acpi65.h (EFI_ACPI_6_5_GIC_STRUCTURE, EFI_ACPI_6_5_GIC_DISTRIBUTOR_STRUCTURE).
 *  - GTDT rev 3 (104 byte): ACPI 6.5 sez. 5.2.24; Acpi65.h (EFI_ACPI_6_5_GENERIC_TIMER_DESCRIPTION_TABLE).
 *  - PPTT rev 3, Processor Hierarchy Node (tipo 0, 20 byte senza risorse private):
 *    ACPI 6.5 sez. 5.2.30.1; Acpi65.h (EFI_ACPI_6_5_PPTT_STRUCTURE_PROCESSOR).
 *  - CSRT rev 0: "Core System Resources Table (CSRT)", Microsoft, 14 novembre 2011
 *    (https://cdrdv2-public.intel.com/772760/csrt.pdf, linkato da learn.microsoft.com
 *    "ACPI System Description Tables"): sez. 2 (header), 3 (Resource Group Header, 24 byte),
 *    4 (Resource Descriptor Header, 12 byte).
 */
#ifndef ACPIGEN_H
#define ACPIGEN_H

#include <stddef.h>
#include <stdint.h>

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "acpigen assume un host little-endian"
#endif

#define ACPIGEN_PACKED __attribute__((packed))

/* Codici di errore (valori di ritorno negativi delle funzioni build). */
#define ACPIGEN_E_SMALL (-1) /* buffer troppo piccolo */
#define ACPIGEN_E_INVAL (-2) /* parametri non validi (es. 0 CPU) */

/* Valore a 32 bit di un ID di 4 caratteri: "ABCD" -> 0x44434241 (CSRT sez. 3). */
#define ACPIGEN_ID4(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

/* ---- Header SDT (36 byte) ---- */
typedef struct ACPIGEN_PACKED {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header;

/* Campi OEM; puntatore NULL nelle API = valori predefiniti. */
typedef struct {
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpigen_oem;

/* ---- MADT ---- */
#define ACPI_MADT_REVISION 6
#define ACPI_MADT_TYPE_GICC 0x0B
#define ACPI_MADT_TYPE_GICD 0x0C
#define ACPI_GICC_ENABLED (1u << 0)

typedef struct ACPIGEN_PACKED {
    acpi_sdt_header header;
    uint32_t local_apic_address; /* 0 su Arm */
    uint32_t flags;
} acpi_madt_header;

typedef struct ACPIGEN_PACKED {
    uint8_t  type;   /* 0x0B */
    uint8_t  length; /* 82 */
    uint16_t reserved;
    uint32_t cpu_interface_number;
    uint32_t acpi_processor_uid;
    uint32_t flags;
    uint32_t parking_protocol_version;
    uint32_t performance_interrupt_gsiv;
    uint64_t parked_address;
    uint64_t physical_base_address;
    uint64_t gicv;
    uint64_t gich;
    uint32_t vgic_maintenance_interrupt;
    uint64_t gicr_base_address;
    uint64_t mpidr;
    uint8_t  processor_power_efficiency_class;
    uint8_t  reserved2;
    uint16_t spe_overflow_interrupt;
    uint16_t trbe_interrupt;
} acpi_gicc;

typedef struct ACPIGEN_PACKED {
    uint8_t  type;   /* 0x0C */
    uint8_t  length; /* 24 */
    uint16_t reserved1;
    uint32_t gic_id;
    uint64_t physical_base_address;
    uint32_t system_vector_base;
    uint8_t  gic_version;
    uint8_t  reserved2[3];
} acpi_gicd;

/* Descrizione di una CPU (condivisa da MADT e PPTT). */
typedef struct {
    uint32_t    uid;           /* ACPI Processor UID */
    uint64_t    mpidr;         /* affinita' MPIDR */
    uint32_t    cluster;       /* indice di cluster 0..n-1, contiguo */
    uint8_t     efficiency;    /* Processor Power Efficiency Class */
    uint32_t    parking_version;
    uint64_t    parked_address;
    int         enabled;
    int         is_thread;     /* PPTT: "Processor is a Thread" */
    const char *name;          /* solo informativo */
} acpigen_cpu;

typedef struct {
    uint64_t physical_base_address;
    uint32_t gic_id;
    uint32_t system_vector_base;
    uint8_t  gic_version;
} acpigen_gicd;

/* gicd NULL = nessuna GICD. Restituisce la lunghezza o un errore negativo. 0 CPU -> E_INVAL. */
int64_t acpigen_build_madt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_cpu *cpus, size_t ncpus, const acpigen_gicd *gicd);

/* ---- GTDT ---- */
#define ACPI_GTDT_REVISION 3
#define ACPI_GTDT_TIMER_EDGE (1u << 0)       /* 0 = level */
#define ACPI_GTDT_TIMER_ACTIVE_LOW (1u << 1) /* 0 = active high */
#define ACPI_GTDT_TIMER_ALWAYS_ON (1u << 2)

typedef struct ACPIGEN_PACKED {
    acpi_sdt_header header;
    uint64_t cnt_control_base;
    uint32_t reserved;
    uint32_t secure_pl1_gsiv;
    uint32_t secure_pl1_flags;
    uint32_t nonsecure_pl1_gsiv;
    uint32_t nonsecure_pl1_flags;
    uint32_t virtual_timer_gsiv;
    uint32_t virtual_timer_flags;
    uint32_t nonsecure_pl2_gsiv;
    uint32_t nonsecure_pl2_flags;
    uint64_t cnt_read_base;
    uint32_t platform_timer_count;
    uint32_t platform_timer_offset;
    uint32_t virtual_pl2_gsiv;
    uint32_t virtual_pl2_flags;
} acpi_gtdt;

typedef struct {
    uint64_t cnt_control_base; /* 0xFFFFFFFFFFFFFFFF se assente */
    uint64_t cnt_read_base;    /* idem */
    uint32_t secure_pl1_gsiv, secure_pl1_flags;
    uint32_t nonsecure_pl1_gsiv, nonsecure_pl1_flags;
    uint32_t virtual_gsiv, virtual_flags;
    uint32_t nonsecure_pl2_gsiv, nonsecure_pl2_flags;
    uint32_t virtual_pl2_gsiv, virtual_pl2_flags;
} acpigen_gtdt_cfg;

int64_t acpigen_build_gtdt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_gtdt_cfg *cfg);

/* ---- PPTT ---- */
#define ACPI_PPTT_REVISION 3
#define ACPI_PPTT_TYPE_PROCESSOR 0
#define ACPI_PPTT_PHYSICAL_PACKAGE (1u << 0)
#define ACPI_PPTT_ID_VALID (1u << 1)
#define ACPI_PPTT_IS_THREAD (1u << 2)
#define ACPI_PPTT_IS_LEAF (1u << 3)
#define ACPI_PPTT_IDENTICAL_IMPL (1u << 4)

typedef struct ACPIGEN_PACKED {
    uint8_t  type;   /* 0 */
    uint8_t  length; /* 20 + 4 * private resources */
    uint16_t reserved;
    uint32_t flags;
    uint32_t parent; /* offset dall'inizio della tabella, 0 = nessun padre */
    uint32_t acpi_processor_id;
    uint32_t number_of_private_resources;
} acpi_pptt_processor;

/* Gerarchia package -> cluster -> core. Il core ha ACPI Processor ID = cpu.uid. */
int64_t acpigen_build_pptt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_cpu *cpus, size_t ncpus);

/* ---- CSRT ---- */
#define ACPI_CSRT_REVISION 0

typedef struct ACPIGEN_PACKED {
    uint32_t length;
    uint32_t vendor_id;
    uint32_t subvendor_id;
    uint16_t device_id;
    uint16_t subdevice_id;
    uint16_t revision;
    uint16_t reserved;
    uint32_t shared_info_length;
} acpi_csrt_group_header;

typedef struct ACPIGEN_PACKED {
    uint32_t length;
    uint16_t type;
    uint16_t subtype;
    uint32_t uid;
} acpi_csrt_descriptor_header;

/* Tipi/sottotipi (CSRT tab. 4.2). */
#define ACPI_CSRT_TYPE_INTERRUPT 0x0001
#define ACPI_CSRT_TYPE_TIMER 0x0002
#define ACPI_CSRT_TYPE_DMA 0x0003

typedef struct {
    uint32_t vendor_id, subvendor_id;
    uint16_t device_id, subdevice_id, revision;
    const void *shared_info; size_t shared_info_len;
    uint16_t res_type, res_subtype;
    uint32_t res_uid; /* 0xFFFFFFFF riservato */
    const void *payload; size_t payload_len; /* info definita dal vendor */
} acpigen_csrt_cfg;

/* Un Resource Group con un Resource Descriptor. */
int64_t acpigen_build_csrt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_csrt_cfg *cfg);

/* ---- Modello di esempio t8103 (src/t8103_model.c) ---- */
#define T8103_NCPUS 8
extern const acpigen_cpu t8103_cpus[T8103_NCPUS];
extern const acpigen_gicd t8103_gicd;
extern const acpigen_gtdt_cfg t8103_gtdt;
extern const acpigen_csrt_cfg t8103_csrt;

/* Checksum: somma dei byte della tabella = 0 mod 256. Utile ai test. */
uint8_t acpigen_sum(const uint8_t *buf, size_t len);

#endif
