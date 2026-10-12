// SPDX-License-Identifier: MIT
/* CSRT rev 0: Microsoft "Core System Resources Table (CSRT)", 14/11/2011,
 * https://cdrdv2-public.intel.com/772760/csrt.pdf, sez. 2-4. */
#include <string.h>
#include "internal.h"

int64_t acpigen_build_csrt(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_csrt_cfg *c)
{
    if (!buf || !c || (c->shared_info_len && !c->shared_info) ||
        (c->payload_len && !c->payload) || c->res_uid == 0xFFFFFFFFu)
        return ACPIGEN_E_INVAL;
    size_t glen = sizeof(acpi_csrt_group_header) + c->shared_info_len +
                  sizeof(acpi_csrt_descriptor_header) + c->payload_len;
    size_t total = sizeof(acpi_sdt_header) + glen;
    if (total > cap)
        return ACPIGEN_E_SMALL;
    memset(buf, 0, total);
    size_t off = sizeof(acpi_sdt_header);
    acpi_csrt_group_header g;
    memset(&g, 0, sizeof g);
    g.length = (uint32_t)glen;
    g.vendor_id = c->vendor_id;
    g.subvendor_id = c->subvendor_id;
    g.device_id = c->device_id;
    g.subdevice_id = c->subdevice_id;
    g.revision = c->revision;
    g.shared_info_length = (uint32_t)c->shared_info_len;
    memcpy(buf + off, &g, sizeof g);
    off += sizeof g;
    if (c->shared_info_len)
        memcpy(buf + off, c->shared_info, c->shared_info_len);
    off += c->shared_info_len;
    acpi_csrt_descriptor_header d;
    d.length = (uint32_t)(sizeof d + c->payload_len);
    d.type = c->res_type;
    d.subtype = c->res_subtype;
    d.uid = c->res_uid;
    memcpy(buf + off, &d, sizeof d);
    off += sizeof d;
    if (c->payload_len)
        memcpy(buf + off, c->payload, c->payload_len);
    off += c->payload_len;
    acpigen_finish(buf, off, "CSRT", ACPI_CSRT_REVISION, oem);
    return (int64_t)off;
}
