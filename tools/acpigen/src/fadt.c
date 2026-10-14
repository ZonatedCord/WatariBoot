// SPDX-License-Identifier: MIT
/* FADT rev 6 minor 5, 276 byte, solo Arm HW-reduced. Offset da ACPI 6.5 tab. 5.9 (a memoria, ipotesi
 * non confrontata con Acpi65.h): FLAGS 112, RESET_VALUE 128, ARM_BOOT_ARCH 129, FADT_MINOR 131,
 * X_DSDT 140, HypervisorVendorId 268. Gli altri campi restano zero (HW_REDUCED_ACPI li ignora). */
#include <string.h>
#include "internal.h"

#define OFF_FLAGS 112
#define OFF_ARM_BOOT_ARCH 129
#define OFF_MINOR 131
#define OFF_X_DSDT 140

int64_t acpigen_build_fadt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_fadt_cfg *cfg)
{
    if (!buf)
        return ACPIGEN_E_INVAL;
    if (cap < ACPI_FADT_LENGTH)
        return ACPIGEN_E_SMALL;
    uint32_t flags = ACPI_FADT_HW_REDUCED_ACPI;
    uint16_t boot = 0;
    uint64_t xdsdt = 0;
    if (cfg) {
        flags = cfg->flags ? cfg->flags : flags;
        boot = cfg->arm_boot_arch;
        xdsdt = cfg->x_dsdt;
    }
    /* HW-reduced e' obbligatorio per un Arm senza hardware ACPI legacy. */
    flags |= ACPI_FADT_HW_REDUCED_ACPI;
    memset(buf, 0, ACPI_FADT_LENGTH);
    memcpy(buf + OFF_FLAGS, &flags, 4);
    memcpy(buf + OFF_ARM_BOOT_ARCH, &boot, 2);
    buf[OFF_MINOR] = ACPI_FADT_MINOR_VERSION;
    memcpy(buf + OFF_X_DSDT, &xdsdt, 8);
    acpigen_finish(buf, ACPI_FADT_LENGTH, "FACP", ACPI_FADT_REVISION, oem);
    return ACPI_FADT_LENGTH;
}
