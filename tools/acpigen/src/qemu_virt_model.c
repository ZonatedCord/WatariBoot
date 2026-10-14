// SPDX-License-Identifier: MIT
/*
 * Modello QEMU "virt" (GICv3, 4 CPU). Valori ricavati in locale (verificato) con
 *   qemu-system-aarch64 -M virt,gic-version=3 -cpu max -smp 4 -machine dumpdtb=... + dtc:
 *  - cpu@0..3 reg = 0..3 (MPIDR Aff0), compatible arm,cortex-a57 (con -cpu max il DT indica a57);
 *  - intc@8000000 arm,gic-v3, reg GICD 0x08000000 (0x10000), GICR 0x080A0000 (0xF60000);
 *  - timer: interrupts PPI 13,14,11,10 = GSIV 16+PPI = 29,30,27,26, flag DT 4 (level, active high)
 *    e "always-on";
 *  - pl011@9000000 (0x1000).
 * Flag GTDT: il DT indica level/ACTIVE-HIGH, quindi qui NON si imposta ACTIVE_LOW (diverge dalla
 * richiesta del task, che parlava di active-low): verificato sul DT; cosa metta QEMU nella sua
 * GTDT ACPI e' ipotesi (ricordo: solo ALWAYS_ON).
 * Parking: QEMU reale usa PSCI; qui si usa il parking per provare il percorso Watari (PSCI=0 in FADT).
 * Pagine di parking a 0x4F000000 + 0x1000*i (RAM di virt parte a 0x40000000, verificato dal DT
 * memory@40000000): base IPOTESI, va riservata nell'EDK2.
 */
#include <string.h>
#include "acpigen.h"

#define QPARK 0x4F000000ull
#define QCPU(n, i) \
    { .uid = (i), .mpidr = (i), .cluster = 0, .efficiency = 0, .parking_version = 1, \
      .parked_address = QPARK + 0x1000ull * (i), .enabled = 1, .is_thread = 0, .name = (n) }

const acpigen_cpu qemu_virt_cpus[QEMU_VIRT_NCPUS] = {
    QCPU("virt0", 0), QCPU("virt1", 1), QCPU("virt2", 2), QCPU("virt3", 3),
};

const acpigen_gicd qemu_virt_gicd = {
    .physical_base_address = 0x08000000ull, .gic_id = 0, .system_vector_base = 0, .gic_version = 3,
};

#define QF ACPI_GTDT_TIMER_ALWAYS_ON
const acpigen_gtdt_cfg qemu_virt_gtdt = {
    .cnt_control_base = 0xFFFFFFFFFFFFFFFFull, .cnt_read_base = 0xFFFFFFFFFFFFFFFFull,
    .secure_pl1_gsiv = 29, .secure_pl1_flags = QF,
    .nonsecure_pl1_gsiv = 30, .nonsecure_pl1_flags = QF,
    .virtual_gsiv = 27, .virtual_flags = QF,
    .nonsecure_pl2_gsiv = 26, .nonsecure_pl2_flags = QF,
    .virtual_pl2_gsiv = 0, .virtual_pl2_flags = 0,
};

const acpigen_dbg2_cfg qemu_virt_dbg2 = {
    .base = 0x09000000ull, .size = 0x1000, .port_subtype = ACPI_DBG2_SUBTYPE_PL011,
    .access_size = 3, .bit_width = 32,
};

const acpigen_model qemu_virt_model = {
    .name = "qemu-virt", .cpus = qemu_virt_cpus, .ncpus = QEMU_VIRT_NCPUS,
    .gicd = &qemu_virt_gicd,
    .madt_opts = { .boot_index = 0, .gicc_gicr_base = 0,
                   .gicr_range_base = 0x080A0000ull, .gicr_range_length = 0xF60000 },
    .gtdt = &qemu_virt_gtdt, .csrt = 0, .dbg2 = &qemu_virt_dbg2, .fadt = 0,
};

const acpigen_model *acpigen_find_model(const char *name)
{
    if (name && !strcmp(name, t8103_model.name))
        return &t8103_model;
    if (name && !strcmp(name, qemu_virt_model.name))
        return &qemu_virt_model;
    return 0;
}
