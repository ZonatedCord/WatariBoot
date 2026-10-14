// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
int main(void)
{
    uint8_t b[512];
    int64_t n = acpigen_build_fadt(b, sizeof b, 0, 0);
    CHECK(n == 276);
    check_table(b, n, "FACP", 6);
    uint32_t flags; memcpy(&flags, b + 112, 4);
    uint16_t boot; memcpy(&boot, b + 129, 2);
    CHECK(flags & (1u << 20));               /* HW_REDUCED_ACPI */
    CHECK(boot == 0);                        /* PSCI_COMPLIANT = 0 (coerente col parking) */
    CHECK(b[131] == 5);                      /* minor version */
    uint64_t xd; memcpy(&xd, b + 140, 8);
    CHECK(xd == 0);
    /* HW_REDUCED non si puo' spegnere; flag aggiuntivi conservati; PSCI esplicito */
    acpigen_fadt_cfg c = { .flags = 1u << 4, .arm_boot_arch = ACPI_FADT_ARM_PSCI_COMPLIANT,
                           .x_dsdt = 0x1234000 };
    n = acpigen_build_fadt(b, sizeof b, 0, &c);
    check_table(b, n, "FACP", 6);
    memcpy(&flags, b + 112, 4); memcpy(&boot, b + 129, 2); memcpy(&xd, b + 140, 8);
    CHECK(flags == ((1u << 4) | (1u << 20)) && boot == 1 && xd == 0x1234000);
    CHECK(acpigen_build_fadt(b, 275, 0, 0) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_fadt(0, 512, 0, 0) == ACPIGEN_E_INVAL);
    TEST_END();
}
