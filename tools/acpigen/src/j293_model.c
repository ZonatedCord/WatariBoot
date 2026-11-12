// SPDX-License-Identifier: MIT
/*
 * Model j293: 13" M1 MacBook Pro (t8103) running Windows ARM64 on top of Watari EL2 (the layer).
 * Unlike t8103_model.c (a plan-A placeholder: parking protocol, no GIC), this model describes what the
 * *layer* shows the guest: an emulated GICv3 plus the Arm generic timer, PSCI over HVC.
 *
 * Sources:
 *  - MPIDRs [verified: re/m3a-ehw0-measurements.md row 2, measured on j293 2026-11-10]: cpu0-3 Icestorm
 *    (E) 0x80000000..3, cpu4-7 Firestorm (P) 0x80010100..3. GICC.MPIDR carries only the affinity
 *    fields (Aff3:Aff2:Aff1:Aff0 = 0x0, 0x10100), so RES1 bit 31 is dropped [hypothesis: ACPI 6.5
 *    5.2.12.14 "MPIDR" field is the affinity value as in GICR_TYPER; the existing t8103 model and
 *    Linux's MPIDR_HWID_BITMASK agree]. The layer hands the guest the physical MPIDR as VMPIDR_EL2
 *    (el2/boot/main.c set_virtual_ids) and builds the vGIC GICR_TYPER affinity from the same value
 *    (el2/boot/main.c vgic_setup), in the dense order E0-3 then P0-3 (el2/include/wel2_coreidx.h).
 *    NOTE: docs/design/watari-el2/INTERFACES.md 5 still says "flat Aff0 = i"; the code and the
 *    measurement win (see README "j293").
 *  - GICD 0xF00000000, GICR 0xF10000000 (128 KB per core, 8 cores = 0x100000), MSI frame at
 *    0xFFFFF000: el2/boot/plat.h PLAT_M1_*_BASE, docs/design/watari-el2/INTERFACES.md 4,
 *    el2/include/wel2_v2m.h; SPI base 928 / count 32: el2/vgic/intr_map.c INTR_MAP_T8103.
 *  - Timer PPIs: 27 virtual, 30 EL1 physical (el2/include/wel2_apple_fiq.h); 29 / 26 are the
 *    standard Arm secure / EL2 PPIs, listed because the GTDT has the fields [hypothesis: Windows
 *    ignores them; QEMU's own GTDT, which Windows boots with, lists them identically].
 */
#include "acpigen.h"

/* PSCI over the layer: no parking protocol (parking_version 0, address 0). Efficiency class 0 = E, 1 = P
 * [hypothesis: the specification does not fix the meaning; higher = more performant is the usual use].
 * perf_gsiv 0: the layer does not expose a PMU interrupt on the M1 (the Apple PMU is a FIQ source,
 * wel2_apple_fiq.h) [hypothesis: Windows tolerates a missing PMU GSIV; QEMU's 23 is not available]. */
#define JCPU(n, u, m, cl, eff) \
    { .uid = (u), .mpidr = (m), .cluster = (cl), .efficiency = (eff), .parking_version = 0, \
      .parked_address = 0, .perf_gsiv = 0, .enabled = 1, .is_thread = 0, .name = (n) }

const acpigen_cpu j293_cpus[J293_NCPUS] = {
    JCPU("icestorm0", 0, 0x0, 0, 0),      JCPU("icestorm1", 1, 0x1, 0, 0),
    JCPU("icestorm2", 2, 0x2, 0, 0),      JCPU("icestorm3", 3, 0x3, 0, 0),
    JCPU("firestorm0", 4, 0x10100, 1, 1), JCPU("firestorm1", 5, 0x10101, 1, 1),
    JCPU("firestorm2", 6, 0x10102, 1, 1), JCPU("firestorm3", 7, 0x10103, 1, 1),
};

const acpigen_gicd j293_gicd = {
    .physical_base_address = 0xF00000000ull, .gic_id = 0, .system_vector_base = 0, .gic_version = 3,
};

/* PPIs are level, active high in the vGIC (GICR ICFGR, INTERFACES 6); ACPI flag bit 0 = 0 level, bit 1 = 0
 * active high. ALWAYS_ON on the NS EL1 physical timer only, exactly as QEMU's GTDT (dump F-11, the Windows
 * control that boots) [hypothesis: Apple's counter keeps running in sleep states; the layer has no sleep]. */
const acpigen_gtdt_cfg j293_gtdt = {
    .cnt_control_base = 0xFFFFFFFFFFFFFFFFull, .cnt_read_base = 0xFFFFFFFFFFFFFFFFull,
    .secure_pl1_gsiv = 29, .secure_pl1_flags = 0,
    .nonsecure_pl1_gsiv = 30, .nonsecure_pl1_flags = ACPI_GTDT_TIMER_ALWAYS_ON,
    .virtual_gsiv = 27, .virtual_flags = 0,
    .nonsecure_pl2_gsiv = 26, .nonsecure_pl2_flags = 0,
    .virtual_pl2_gsiv = 0, .virtual_pl2_flags = 0,
};

/* FADT: HW-reduced (always), PSCI_COMPLIANT | PSCI_USE_HVC: the layer implements PSCI at EL2 and the guest
 * calls it with HVC (ACPI 6.5 table 5.10, ARM_BOOT_ARCH bits 0 and 1; el2/boot/psci.c). */
const acpigen_fadt_cfg j293_fadt = {
    .flags = 0, .arm_boot_arch = ACPI_FADT_ARM_PSCI_COMPLIANT | ACPI_FADT_ARM_PSCI_USE_HVC, .x_dsdt = 0,
};

/* No DBG2/SPCR and no CSRT: see README "j293" for the console decision. */
const acpigen_model j293_model = {
    .name = "j293", .cpus = j293_cpus, .ncpus = J293_NCPUS,
    .gicd = &j293_gicd,
    .madt_opts = { .boot_index = 0, .gicc_gicr_base = 0, .gicd_first = 1,
                   .gicr_range_base = 0xF10000000ull, .gicr_range_length = J293_NCPUS * 0x20000,
                   .msi_base = 0xFFFFF000ull, .msi_flags = 1, .msi_spi_count = 32, .msi_spi_base = 928 },
    .gtdt = &j293_gtdt, .csrt = 0, .dbg2 = 0, .fadt = &j293_fadt,
};
