// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
/* Layout: Microsoft CSRT (14/11/2011) sez. 3 e 4 */
_Static_assert(sizeof(acpi_csrt_group_header) == 24, "");
_Static_assert(offsetof(acpi_csrt_group_header, vendor_id) == 4, "");
_Static_assert(offsetof(acpi_csrt_group_header, subvendor_id) == 8, "");
_Static_assert(offsetof(acpi_csrt_group_header, device_id) == 12, "");
_Static_assert(offsetof(acpi_csrt_group_header, subdevice_id) == 14, "");
_Static_assert(offsetof(acpi_csrt_group_header, revision) == 16, "");
_Static_assert(offsetof(acpi_csrt_group_header, reserved) == 18, "");
_Static_assert(offsetof(acpi_csrt_group_header, shared_info_length) == 20, "");
_Static_assert(sizeof(acpi_csrt_descriptor_header) == 12, "");
_Static_assert(offsetof(acpi_csrt_descriptor_header, type) == 4, "");
_Static_assert(offsetof(acpi_csrt_descriptor_header, subtype) == 6, "");
_Static_assert(offsetof(acpi_csrt_descriptor_header, uid) == 8, "");

int main(void)
{
    uint8_t b[256];
    int64_t n = acpigen_build_csrt(b, sizeof b, 0, &t8103_csrt);
    CHECK(n == 36 + 24 + 12 + 8);
    check_table(b, n, "CSRT", 0);
    acpi_csrt_group_header g; memcpy(&g, b + 36, sizeof g);
    CHECK(g.length == 44 && g.vendor_id == 0x454C5041 /* "APLE" LE */);
    CHECK(g.device_id == 0x8103 && g.revision == 1 && g.reserved == 0 && g.shared_info_length == 0);
    acpi_csrt_descriptor_header d; memcpy(&d, b + 60, sizeof d);
    CHECK(d.length == 20 && d.type == 1 && d.subtype == 1 && d.uid == 0);
    /* con shared info */
    uint8_t sh[4] = {1, 2, 3, 4};
    acpigen_csrt_cfg c = t8103_csrt; c.shared_info = sh; c.shared_info_len = 4;
    n = acpigen_build_csrt(b, sizeof b, 0, &c);
    CHECK(n == 36 + 24 + 4 + 12 + 8);
    check_table(b, n, "CSRT", 0);
    CHECK(memcmp(b + 60, sh, 4) == 0);
    c = t8103_csrt; c.res_uid = 0xFFFFFFFFu;
    CHECK(acpigen_build_csrt(b, sizeof b, 0, &c) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_csrt(b, 79, 0, &t8103_csrt) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_csrt(b, 80, 0, &t8103_csrt) == 80);
    TEST_END();
}
