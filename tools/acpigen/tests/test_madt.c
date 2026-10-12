// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
/* Layout: ACPI 6.5 sez. 5.2.12 / Acpi65.h */
_Static_assert(sizeof(acpi_sdt_header) == 36, "hdr");
_Static_assert(sizeof(acpi_madt_header) == 44, "madt hdr");
_Static_assert(sizeof(acpi_gicc) == 82, "gicc");
_Static_assert(sizeof(acpi_gicd) == 24, "gicd");
_Static_assert(offsetof(acpi_gicc, cpu_interface_number) == 4, "");
_Static_assert(offsetof(acpi_gicc, acpi_processor_uid) == 8, "");
_Static_assert(offsetof(acpi_gicc, flags) == 12, "");
_Static_assert(offsetof(acpi_gicc, parking_protocol_version) == 16, "");
_Static_assert(offsetof(acpi_gicc, performance_interrupt_gsiv) == 20, "");
_Static_assert(offsetof(acpi_gicc, parked_address) == 24, "");
_Static_assert(offsetof(acpi_gicc, physical_base_address) == 32, "");
_Static_assert(offsetof(acpi_gicc, gicv) == 40, "");
_Static_assert(offsetof(acpi_gicc, gich) == 48, "");
_Static_assert(offsetof(acpi_gicc, vgic_maintenance_interrupt) == 56, "");
_Static_assert(offsetof(acpi_gicc, gicr_base_address) == 60, "");
_Static_assert(offsetof(acpi_gicc, mpidr) == 68, "");
_Static_assert(offsetof(acpi_gicc, processor_power_efficiency_class) == 76, "");
_Static_assert(offsetof(acpi_gicc, spe_overflow_interrupt) == 78, "");
_Static_assert(offsetof(acpi_gicc, trbe_interrupt) == 80, "");
_Static_assert(offsetof(acpi_gicd, gic_id) == 4, "");
_Static_assert(offsetof(acpi_gicd, physical_base_address) == 8, "");
_Static_assert(offsetof(acpi_gicd, system_vector_base) == 16, "");
_Static_assert(offsetof(acpi_gicd, gic_version) == 20, "");

int main(void)
{
    uint8_t b[2048];
    int64_t n = acpigen_build_madt(b, sizeof b, 0, t8103_cpus, T8103_NCPUS, 0);
    CHECK(n == 44 + 8 * 82);
    check_table(b, n, "APIC", 6);
    for (int i = 0; i < T8103_NCPUS; i++) {
        acpi_gicc g; memcpy(&g, b + 44 + i * 82, sizeof g);
        CHECK(g.type == 0x0B && g.length == 82);
        CHECK(g.cpu_interface_number == (uint32_t)i);
        CHECK(g.acpi_processor_uid == t8103_cpus[i].uid);
        CHECK(g.flags == 1);
        CHECK(g.mpidr == t8103_cpus[i].mpidr);
        CHECK(g.processor_power_efficiency_class == (i < 4 ? 0 : 1));
    }
    n = acpigen_build_madt(b, sizeof b, 0, t8103_cpus, T8103_NCPUS, &t8103_gicd);
    CHECK(n == 44 + 8 * 82 + 24);
    check_table(b, n, "APIC", 6);
    CHECK(b[44 + 8 * 82] == 0x0C && b[44 + 8 * 82 + 1] == 24);
    CHECK(acpigen_build_madt(b, sizeof b, 0, t8103_cpus, 0, 0) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_madt(b, 43, 0, t8103_cpus, 1, 0) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_madt(b, 44 + 8 * 82 - 1, 0, t8103_cpus, 8, 0) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_madt(b, 44 + 8 * 82, 0, t8103_cpus, 8, 0) == 44 + 8 * 82);
    TEST_END();
}
