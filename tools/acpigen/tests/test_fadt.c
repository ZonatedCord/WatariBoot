// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
/* Mirror of EFI_ACPI_6_5_FIXED_ACPI_DESCRIPTION_TABLE (Acpi65.h) used to pin the field offsets
 * the generator writes to. Offsets computed by compiling Acpi65.h [verified: edk2-stable202608,
 * issue #44]. Only the fields acpigen sets are named; the rest are padding. */
typedef struct __attribute__((packed)) {
    acpi_sdt_header hdr;
    uint8_t  pad0[112 - 36];
    uint32_t Flags;             /* 112 */
    uint8_t  ResetReg[12];      /* 116 (GAS) */
    uint8_t  ResetValue;        /* 128 */
    uint16_t ArmBootArch;       /* 129 */
    uint8_t  MinorVersion;      /* 131 */
    uint64_t XFirmwareCtrl;     /* 132 */
    uint64_t XDsdt;             /* 140 */
    uint8_t  XBlocks[10][12];   /* 148..267: XPm1aEvtBlk .. SleepStatusReg */
    uint64_t HypervisorVendorIdentity; /* 268 */
} edk2_fadt;
_Static_assert(sizeof(edk2_fadt) == 276, "FADT size");
_Static_assert(sizeof(edk2_fadt) == ACPI_FADT_LENGTH, "ACPI_FADT_LENGTH");
_Static_assert(offsetof(edk2_fadt, Flags) == 112, "");
_Static_assert(offsetof(edk2_fadt, ResetReg) == 116, "");
_Static_assert(offsetof(edk2_fadt, ResetValue) == 128, "");
_Static_assert(offsetof(edk2_fadt, ArmBootArch) == 129, "");
_Static_assert(offsetof(edk2_fadt, MinorVersion) == 131, "");
_Static_assert(offsetof(edk2_fadt, XFirmwareCtrl) == 132, "");
_Static_assert(offsetof(edk2_fadt, XDsdt) == 140, "");
_Static_assert(offsetof(edk2_fadt, XBlocks[9]) == 256, "SleepStatusReg");
_Static_assert(offsetof(edk2_fadt, HypervisorVendorIdentity) == 268, "");

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
    /* The generator writes through the same offsets as the EDK2 layout. */
    const edk2_fadt *f = (const edk2_fadt *)b;
    CHECK(f->Flags == flags && f->ArmBootArch == 1 && f->MinorVersion == 5 && f->XDsdt == 0x1234000);
    CHECK(f->ResetValue == 0 && f->XFirmwareCtrl == 0 && f->HypervisorVendorIdentity == 0);
    CHECK(acpigen_build_fadt(b, 275, 0, 0) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_fadt(0, 512, 0, 0) == ACPIGEN_E_INVAL);
    TEST_END();
}
