// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
/* Layout: ACPI 6.5 sez. 5.2.24 / Acpi65.h */
_Static_assert(sizeof(acpi_gtdt) == 104, "gtdt");
_Static_assert(offsetof(acpi_gtdt, cnt_control_base) == 36, "");
_Static_assert(offsetof(acpi_gtdt, secure_pl1_gsiv) == 48, "");
_Static_assert(offsetof(acpi_gtdt, nonsecure_pl1_gsiv) == 56, "");
_Static_assert(offsetof(acpi_gtdt, virtual_timer_gsiv) == 64, "");
_Static_assert(offsetof(acpi_gtdt, nonsecure_pl2_gsiv) == 72, "");
_Static_assert(offsetof(acpi_gtdt, cnt_read_base) == 80, "");
_Static_assert(offsetof(acpi_gtdt, platform_timer_count) == 88, "");
_Static_assert(offsetof(acpi_gtdt, virtual_pl2_gsiv) == 96, "");

int main(void)
{
    uint8_t b[256];
    int64_t n = acpigen_build_gtdt(b, sizeof b, 0, &t8103_gtdt);
    CHECK(n == 104);
    check_table(b, n, "GTDT", 3);
    acpi_gtdt t; memcpy(&t, b, sizeof t);
    CHECK(t.secure_pl1_gsiv == 29 && t.nonsecure_pl1_gsiv == 30);
    CHECK(t.virtual_timer_gsiv == 27 && t.nonsecure_pl2_gsiv == 26);
    CHECK(t.platform_timer_count == 0);
    CHECK(acpigen_build_gtdt(b, 103, 0, &t8103_gtdt) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_gtdt(b, sizeof b, 0, 0) == ACPIGEN_E_INVAL);
    TEST_END();
}
