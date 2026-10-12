// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
/* Layout: ACPI 6.5 sez. 5.2.30.1 / Acpi65.h */
_Static_assert(sizeof(acpi_pptt_processor) == 20, "node");
_Static_assert(offsetof(acpi_pptt_processor, flags) == 4, "");
_Static_assert(offsetof(acpi_pptt_processor, parent) == 8, "");
_Static_assert(offsetof(acpi_pptt_processor, acpi_processor_id) == 12, "");
_Static_assert(offsetof(acpi_pptt_processor, number_of_private_resources) == 16, "");

static acpi_pptt_processor node(const uint8_t *b, size_t off)
{ acpi_pptt_processor n; memcpy(&n, b + off, sizeof n); return n; }

int main(void)
{
    uint8_t b[512];
    int64_t n = acpigen_build_pptt(b, sizeof b, 0, t8103_cpus, T8103_NCPUS);
    CHECK(n == 36 + 20 * (1 + 2 + 8));
    check_table(b, n, "PPTT", 3);
    acpi_pptt_processor p = node(b, 36);
    CHECK(p.type == 0 && p.length == 20 && p.flags == 0x1 && p.parent == 0);
    for (int k = 0; k < 2; k++) {
        acpi_pptt_processor c = node(b, 56 + 20 * k);
        CHECK(c.flags == 0 && c.parent == 36);
    }
    for (int i = 0; i < 8; i++) {
        acpi_pptt_processor c = node(b, 96 + 20 * i);
        CHECK(c.flags == 0xA); /* id valid + leaf, non thread */
        CHECK(c.parent == (uint32_t)(56 + 20 * (i / 4)));
        CHECK(c.acpi_processor_id == (uint32_t)i);
        CHECK(c.number_of_private_resources == 0);
    }
    acpigen_cpu t = t8103_cpus[0]; t.is_thread = 1;
    CHECK(acpigen_build_pptt(b, sizeof b, 0, &t, 1) == 36 + 60);
    CHECK(node(b, 76).flags == 0xE);
    CHECK(acpigen_build_pptt(b, sizeof b, 0, t8103_cpus, 0) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_pptt(b, (size_t)n - 1, 0, t8103_cpus, 8) == ACPIGEN_E_SMALL);
    TEST_END();
}
