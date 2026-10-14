// SPDX-License-Identifier: MIT
/*
 * Modello QEMU "virt" (GICv3, 4 CPU). Valori ricavati in locale (verificato) con
 *   qemu-system-aarch64 -M virt,gic-version=3 -cpu max -smp 4 -machine dumpdtb=... + dtc:
 *  - cpu@0..3 reg = 0..3 (MPIDR Aff0), compatible arm,cortex-a57 (con -cpu max il DT indica a57);
 *  - intc@8000000 arm,gic-v3, reg GICD 0x08000000 (0x10000), GICR 0x080A0000 (0xF60000);
 *  - timer: interrupts PPI 13,14,11,10 = GSIV 16+PPI = 29,30,27,26, flag DT 4 (level, active high)
 *    e "always-on";
 *  - pl011@9000000 (0x1000).
 * Confronto con le tabelle ACPI originali di QEMU (dump dalla RAM del guest, re/f11-acpi-comparison.md):
 *  - GTDT: flag come l'originale (solo NS EL1 ALWAYS_ON; trigger level, polarita' 0 in ACPI);
 *  - MADT: parking 0 (PSCI, come la FADT originale), GICD per prima, GICC con PMU GSIV 23 (PPI 7),
 *    GICR Structure e GIC MSI Frame (0x0D, v2m a 0x08020000, SPI 0x50..0x8F). La MSI Frame MANCANTE
 *    era la causa di F-11 (Windows si blocca senza MSI per NVMe/xHCI).
 * Pagine di parking: non servono con PSCI.
 */
#include <string.h>
#include "acpigen.h"

#define QCPU(n, i) \
    { .uid = (i), .mpidr = (i), .cluster = 0, .efficiency = 0, .parking_version = 0, \
      .parked_address = 0, .perf_gsiv = 23, .enabled = 1, .is_thread = 0, .name = (n) }

const acpigen_cpu qemu_virt_cpus[QEMU_VIRT_NCPUS] = {
    QCPU("virt0", 0), QCPU("virt1", 1), QCPU("virt2", 2), QCPU("virt3", 3),
};

const acpigen_gicd qemu_virt_gicd = {
    .physical_base_address = 0x08000000ull, .gic_id = 0, .system_vector_base = 0, .gic_version = 3,
};

/* Flag come nella GTDT originale di QEMU (dump F-11): solo il timer NS EL1 ha ALWAYS_ON. */
const acpigen_gtdt_cfg qemu_virt_gtdt = {
    .cnt_control_base = 0xFFFFFFFFFFFFFFFFull, .cnt_read_base = 0xFFFFFFFFFFFFFFFFull,
    .secure_pl1_gsiv = 29, .secure_pl1_flags = 0,
    .nonsecure_pl1_gsiv = 30, .nonsecure_pl1_flags = ACPI_GTDT_TIMER_ALWAYS_ON,
    .virtual_gsiv = 27, .virtual_flags = 0,
    .nonsecure_pl2_gsiv = 26, .nonsecure_pl2_flags = 0,
    .virtual_pl2_gsiv = 0, .virtual_pl2_flags = 0,
};

const acpigen_dbg2_cfg qemu_virt_dbg2 = {
    .base = 0x09000000ull, .size = 0x1000, .port_subtype = ACPI_DBG2_SUBTYPE_PL011,
    .access_size = 3, .bit_width = 32,
};

const acpigen_model qemu_virt_model = {
    .name = "qemu-virt", .cpus = qemu_virt_cpus, .ncpus = QEMU_VIRT_NCPUS,
    .gicd = &qemu_virt_gicd,
    .madt_opts = { .boot_index = 0, .gicc_gicr_base = 0, .gicd_first = 1,
                   .gicr_range_base = 0x080A0000ull, .gicr_range_length = 0xF60000,
                   .msi_base = 0x08020000ull, .msi_flags = 1, .msi_spi_count = 0x40, .msi_spi_base = 0x50 },
    .gtdt = &qemu_virt_gtdt, .csrt = 0, .dbg2 = &qemu_virt_dbg2, .fadt = 0,
};

/* V2 (G2): identico a qemu-virt ma SENZA la GICD Structure (GICC + GICR + MSI Frame). Serve a
 * verificare se Windows richiede la GICD nella MADT (rischio R-02). */
const acpigen_model qemu_virt_nogicd_model = {
    .name = "qemu-virt-nogicd", .cpus = qemu_virt_cpus, .ncpus = QEMU_VIRT_NCPUS,
    .gicd = 0,
    .madt_opts = { .boot_index = 0, .gicc_gicr_base = 0, .gicd_first = 1,
                   .gicr_range_base = 0x080A0000ull, .gicr_range_length = 0xF60000,
                   .msi_base = 0x08020000ull, .msi_flags = 1, .msi_spi_count = 0x40, .msi_spi_base = 0x50 },
    .gtdt = &qemu_virt_gtdt, .csrt = 0, .dbg2 = &qemu_virt_dbg2, .fadt = 0,
};

const acpigen_model *acpigen_find_model(const char *name)
{
    if (name && !strcmp(name, t8103_model.name))
        return &t8103_model;
    if (name && !strcmp(name, qemu_virt_model.name))
        return &qemu_virt_model;
    if (name && !strcmp(name, qemu_virt_nogicd_model.name))
        return &qemu_virt_nogicd_model;
    return 0;
}
