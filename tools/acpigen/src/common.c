// SPDX-License-Identifier: MIT
#include <string.h>
#include "internal.h"

uint8_t acpigen_sum(const uint8_t *buf, size_t len)
{
    uint8_t s = 0;
    for (size_t i = 0; i < len; i++)
        s = (uint8_t)(s + buf[i]);
    return s;
}

static const acpigen_oem default_oem = {
    .oem_id = {'W', 'A', 'T', 'A', 'R', 'I'},
    .oem_table_id = {'W', 'A', 'T', 'A', 'R', 'I', 'B', 'T'},
    .oem_revision = 1,
    .creator_id = ACPIGEN_ID4('W', 'T', 'R', 'B'),
    .creator_revision = 1,
};

void acpigen_finish(uint8_t *buf, size_t len, const char sig[4], uint8_t rev,
                    const acpigen_oem *oem)
{
    acpi_sdt_header h;
    if (!oem)
        oem = &default_oem;
    memcpy(h.signature, sig, 4);
    h.length = (uint32_t)len;
    h.revision = rev;
    h.checksum = 0;
    memcpy(h.oem_id, oem->oem_id, 6);
    memcpy(h.oem_table_id, oem->oem_table_id, 8);
    h.oem_revision = oem->oem_revision;
    h.creator_id = oem->creator_id;
    h.creator_revision = oem->creator_revision;
    memcpy(buf, &h, sizeof h);
    ((acpi_sdt_header *)(void *)buf)->checksum = (uint8_t)(0u - acpigen_sum(buf, len));
}
