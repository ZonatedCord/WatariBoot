// SPDX-License-Identifier: MIT
/*
 * Modello di esempio MacBook Pro 13" M1 (t8103, j293).
 * Fonte CPU: AsahiLinux/linux arch/arm64/boot/dts/apple/t8103.dtsi, nodi cpus/cpu@N
 * (verificato): cpu@0..3 "apple,icestorm" reg=<0x0 0x0..0x3> (cluster0 in cpu-map);
 * cpu@10100..10103 "apple,firestorm" reg=<0x0 0x10100..0x10103> (cluster1 in cpu-map).
 * Il reg del DTS e' usato come MPIDR (verificato come valore DTS; coincidenza con il registro
 * MPIDR_EL1 reale: ipotesi).
 */
#include "acpigen.h"

#define CPU(n, u, m, cl, eff) \
    { .uid = (u), .mpidr = (m), .cluster = (cl), .efficiency = (eff), \
      .parking_version = 0, .parked_address = 0 /* ipotesi */, \
      .enabled = 1, .is_thread = 0, .name = (n) }

const acpigen_cpu t8103_cpus[T8103_NCPUS] = {
    CPU("icestorm0", 0, 0x0, 0, 0),     CPU("icestorm1", 1, 0x1, 0, 0),
    CPU("icestorm2", 2, 0x2, 0, 0),     CPU("icestorm3", 3, 0x3, 0, 0),
    CPU("firestorm0", 4, 0x10100, 1, 1), CPU("firestorm1", 5, 0x10101, 1, 1),
    CPU("firestorm2", 6, 0x10102, 1, 1), CPU("firestorm3", 7, 0x10103, 1, 1),
};
/* Efficiency class 0 = E-core, 1 = P-core: ipotesi (la specifica non fissa il significato per Apple).
 * Parking protocol version 0 e parked address 0: segnaposto, ipotesi.
 * GICD segnaposto, ipotesi (il t8103 usa AIC, non GIC). */
const acpigen_gicd t8103_gicd = {
    .physical_base_address = 0x0 /* ipotesi */, .gic_id = 0,
    .system_vector_base = 0, .gic_version = 3 /* ipotesi */,
};

/* GSIV timer: PPI standard Arm 29/30/27/26 come segnaposto, ipotesi
 * (il t8103 reale riceve i timer come FIQ via AIC). */
const acpigen_gtdt_cfg t8103_gtdt = {
    .cnt_control_base = 0xFFFFFFFFFFFFFFFFull, .cnt_read_base = 0xFFFFFFFFFFFFFFFFull,
    .secure_pl1_gsiv = 29, .secure_pl1_flags = ACPI_GTDT_TIMER_ACTIVE_LOW,
    .nonsecure_pl1_gsiv = 30, .nonsecure_pl1_flags = ACPI_GTDT_TIMER_ACTIVE_LOW,
    .virtual_gsiv = 27, .virtual_flags = ACPI_GTDT_TIMER_ACTIVE_LOW,
    .nonsecure_pl2_gsiv = 26, .nonsecure_pl2_flags = ACPI_GTDT_TIMER_ACTIVE_LOW,
    .virtual_pl2_gsiv = 0, .virtual_pl2_flags = 0,
};

/* CSRT: gruppo "APLE" segnaposto (ipotesi), un descrittore Interrupt controller. */
static const uint8_t csrt_payload[8] = {0}; /* ipotesi: contenuto definito dall'halext */
const acpigen_csrt_cfg t8103_csrt = {
    .vendor_id = ACPIGEN_ID4('A', 'P', 'L', 'E'), .subvendor_id = 0,
    .device_id = 0x8103, .subdevice_id = 0, .revision = 1,
    .shared_info = 0, .shared_info_len = 0,
    .res_type = ACPI_CSRT_TYPE_INTERRUPT, .res_subtype = 0x0001, .res_uid = 0,
    .payload = csrt_payload, .payload_len = sizeof csrt_payload,
};
