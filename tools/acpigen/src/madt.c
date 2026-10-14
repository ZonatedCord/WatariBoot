// SPDX-License-Identifier: MIT
/* MADT rev 6: ACPI 6.5 sez. 5.2.12; GICC 5.2.12.14; GICD 5.2.12.15; GICR 5.2.12.17. */
#include <string.h>
#include "internal.h"

/* Validazione di parking, UID e cpu_interface_number. Restituisce 0 o E_INVAL. */
static int validate_cpus(const acpigen_cpu *cpus, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if ((uint64_t)i == ACPI_GICC_INTERFACE_INVALID)
            return ACPIGEN_E_INVAL;
        if (cpus[i].parking_version != 0) {
            if (cpus[i].parked_address == 0 || (cpus[i].parked_address & 0xFFFull))
                return ACPIGEN_E_INVAL;
        } else if (cpus[i].parked_address != 0) {
            return ACPIGEN_E_INVAL;
        }
        for (size_t j = 0; j < i; j++) {
            if (cpus[j].uid == cpus[i].uid)
                return ACPIGEN_E_INVAL;
            if (cpus[i].parked_address && cpus[j].parked_address == cpus[i].parked_address)
                return ACPIGEN_E_INVAL;
        }
    }
    return 0;
}

int64_t acpigen_build_madt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_cpu *cpus, size_t ncpus, const acpigen_gicd *gicd,
                           const acpigen_madt_opts *opts)
{
    static const acpigen_madt_opts no_opts;
    if (!buf || !cpus || ncpus == 0)
        return ACPIGEN_E_INVAL;
    if (!opts)
        opts = &no_opts;
    if (opts->boot_index >= ncpus)
        return ACPIGEN_E_INVAL;
    int rc = validate_cpus(cpus, ncpus);
    if (rc)
        return rc;
    size_t total = sizeof(acpi_madt_header) + ncpus * sizeof(acpi_gicc) +
                   (gicd ? sizeof(acpi_gicd) : 0) +
                   (opts->gicr_range_length ? sizeof(acpi_gicr) : 0);
    if (total > cap)
        return ACPIGEN_E_SMALL;
    memset(buf, 0, total);
    size_t off = sizeof(acpi_madt_header);
    for (size_t k = 0; k < ncpus; k++) {
        /* La CPU di boot per prima, le altre nell'ordine originale. */
        size_t i = (k == 0) ? opts->boot_index
                            : (k - 1 < opts->boot_index ? k - 1 : k);
        acpi_gicc g;
        memset(&g, 0, sizeof g);
        g.type = ACPI_MADT_TYPE_GICC;
        g.length = sizeof g;
        g.cpu_interface_number = (uint32_t)i;
        g.acpi_processor_uid = cpus[i].uid;
        g.flags = cpus[i].enabled ? ACPI_GICC_ENABLED : 0;
        g.parking_protocol_version = cpus[i].parking_version;
        g.parked_address = cpus[i].parked_address;
        g.gicr_base_address = opts->gicc_gicr_base;
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
    if (opts->gicr_range_length) {
        acpi_gicr r;
        memset(&r, 0, sizeof r);
        r.type = ACPI_MADT_TYPE_GICR;
        r.length = sizeof r;
        r.discovery_range_base = opts->gicr_range_base;
        r.discovery_range_length = opts->gicr_range_length;
        memcpy(buf + off, &r, sizeof r);
        off += sizeof r;
    }
    acpigen_finish(buf, off, "APIC", ACPI_MADT_REVISION, oem);
    return (int64_t)off;
}
