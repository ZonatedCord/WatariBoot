// SPDX-License-Identifier: MIT
/* MADT rev 6: ACPI 6.5 sez. 5.2.12; GICC 5.2.12.14; GICD 5.2.12.15. */
#include <string.h>
#include "internal.h"

int64_t acpigen_build_madt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_cpu *cpus, size_t ncpus, const acpigen_gicd *gicd)
{
    if (!buf || !cpus || ncpus == 0)
        return ACPIGEN_E_INVAL;
    size_t total = sizeof(acpi_madt_header) + ncpus * sizeof(acpi_gicc) +
                   (gicd ? sizeof(acpi_gicd) : 0);
    if (total > cap)
        return ACPIGEN_E_SMALL;
    memset(buf, 0, total);
    acpi_madt_header mh;
    memset(&mh, 0, sizeof mh);
    memcpy(buf, &mh, sizeof mh);
    size_t off = sizeof mh;
    for (size_t i = 0; i < ncpus; i++) {
        acpi_gicc g;
        memset(&g, 0, sizeof g);
        g.type = ACPI_MADT_TYPE_GICC;
        g.length = sizeof g;
        g.cpu_interface_number = (uint32_t)i;
        g.acpi_processor_uid = cpus[i].uid;
        g.flags = cpus[i].enabled ? ACPI_GICC_ENABLED : 0;
        g.parking_protocol_version = cpus[i].parking_version;
        g.parked_address = cpus[i].parked_address;
        g.mpidr = cpus[i].mpidr;
        g.processor_power_efficiency_class = cpus[i].efficiency;
        memcpy(buf + off, &g, sizeof g);
        off += sizeof g;
    }
    if (gicd) {
        acpi_gicd d;
        memset(&d, 0, sizeof d);
        d.type = ACPI_MADT_TYPE_GICD;
        d.length = sizeof d;
        d.gic_id = gicd->gic_id;
        d.physical_base_address = gicd->physical_base_address;
        d.system_vector_base = gicd->system_vector_base;
        d.gic_version = gicd->gic_version;
        memcpy(buf + off, &d, sizeof d);
        off += sizeof d;
    }
    acpigen_finish(buf, off, "APIC", ACPI_MADT_REVISION, oem);
    return (int64_t)off;
}
