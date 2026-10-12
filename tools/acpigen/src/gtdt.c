// SPDX-License-Identifier: MIT
/* GTDT rev 3: ACPI 6.5 sez. 5.2.24, Acpi65.h EFI_ACPI_6_5_GENERIC_TIMER_DESCRIPTION_TABLE. */
#include <string.h>
#include "internal.h"

int64_t acpigen_build_gtdt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_gtdt_cfg *c)
{
    if (!buf || !c)
        return ACPIGEN_E_INVAL;
    if (cap < sizeof(acpi_gtdt))
        return ACPIGEN_E_SMALL;
    acpi_gtdt t;
    memset(&t, 0, sizeof t);
    t.cnt_control_base = c->cnt_control_base;
    t.secure_pl1_gsiv = c->secure_pl1_gsiv;
    t.secure_pl1_flags = c->secure_pl1_flags;
    t.nonsecure_pl1_gsiv = c->nonsecure_pl1_gsiv;
    t.nonsecure_pl1_flags = c->nonsecure_pl1_flags;
    t.virtual_timer_gsiv = c->virtual_gsiv;
    t.virtual_timer_flags = c->virtual_flags;
    t.nonsecure_pl2_gsiv = c->nonsecure_pl2_gsiv;
    t.nonsecure_pl2_flags = c->nonsecure_pl2_flags;
    t.cnt_read_base = c->cnt_read_base;
    t.platform_timer_count = 0;
    t.platform_timer_offset = 0;
    t.virtual_pl2_gsiv = c->virtual_pl2_gsiv;
    t.virtual_pl2_flags = c->virtual_pl2_flags;
    memcpy(buf, &t, sizeof t);
    acpigen_finish(buf, sizeof t, "GTDT", ACPI_GTDT_REVISION, oem);
    return (int64_t)sizeof t;
}
