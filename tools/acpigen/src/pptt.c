// SPDX-License-Identifier: MIT
/* PPTT rev 3, Processor Hierarchy Node: ACPI 6.5 sez. 5.2.30.1; Acpi65.h
 * EFI_ACPI_6_5_PPTT_STRUCTURE_PROCESSOR. Package -> cluster -> core. */
#include <string.h>
#include "internal.h"

static void put(uint8_t *buf, size_t *off, uint32_t flags, uint32_t parent, uint32_t id)
{
    acpi_pptt_processor n;
    memset(&n, 0, sizeof n);
    n.type = ACPI_PPTT_TYPE_PROCESSOR;
    n.length = sizeof n;
    n.flags = flags;
    n.parent = parent;
    n.acpi_processor_id = id;
    memcpy(buf + *off, &n, sizeof n);
    *off += sizeof n;
}

int64_t acpigen_build_pptt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_cpu *cpus, size_t ncpus)
{
    if (!buf || !cpus || ncpus == 0)
        return ACPIGEN_E_INVAL;
    size_t ncl = 0;
    for (size_t i = 0; i < ncpus; i++)
        if (cpus[i].cluster + 1u > ncl)
            ncl = cpus[i].cluster + 1u;
    if (ncl > ncpus)
        return ACPIGEN_E_INVAL;
    for (size_t k = 0; k < ncl; k++) { /* ogni cluster deve avere almeno un core */
        size_t n = 0;
        for (size_t i = 0; i < ncpus; i++)
            n += cpus[i].cluster == k;
        if (!n)
            return ACPIGEN_E_INVAL;
    }
    size_t nsz = sizeof(acpi_pptt_processor);
    size_t total = sizeof(acpi_sdt_header) + nsz * (1 + ncl + ncpus);
    if (total > cap)
        return ACPIGEN_E_SMALL;
    memset(buf, 0, total);
    size_t off = sizeof(acpi_sdt_header);
    uint32_t pkg = (uint32_t)off;
    /* package: fisico, id non valido, non foglia */
    put(buf, &off, ACPI_PPTT_PHYSICAL_PACKAGE, 0, 0);
    uint32_t clbase = (uint32_t)off;
    /* cluster: non fisico, id non valido, non foglia, padre = package */
    for (size_t k = 0; k < ncl; k++)
        put(buf, &off, 0, pkg, (uint32_t)k);
    /* core: id valido (= UID MADT), foglia, padre = cluster */
    for (size_t i = 0; i < ncpus; i++) {
        uint32_t fl = ACPI_PPTT_ID_VALID | ACPI_PPTT_IS_LEAF |
                      (cpus[i].is_thread ? ACPI_PPTT_IS_THREAD : 0);
        put(buf, &off, fl, clbase + (uint32_t)(cpus[i].cluster * nsz), cpus[i].uid);
    }
    acpigen_finish(buf, off, "PPTT", ACPI_PPTT_REVISION, oem);
    return (int64_t)off;
}
