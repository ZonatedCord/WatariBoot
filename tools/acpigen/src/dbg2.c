// SPDX-License-Identifier: MIT
/* DBG2 con un solo dispositivo seriale MMIO. Layout (ACPI debug port table 2, Microsoft Learn;
 * verified against DebugPort2Table.h, issue #44): header 36 + OffsetDbgDeviceInfo u32 +
 * NumberDbgDeviceInfo u32; device info rev 0 di 22 byte, poi GAS (12), AddressSize (4),
 * NamespaceString (".\0"). */
#include <string.h>
#include "internal.h"

#define DEV_HDR 22u
#define DEV_LEN (DEV_HDR + 12u + 4u + 2u)

int64_t acpigen_build_dbg2(uint8_t *buf, size_t cap, const acpigen_oem *oem,
                           const acpigen_dbg2_cfg *c)
{
    if (!buf || !c || c->size == 0)
        return ACPIGEN_E_INVAL;
    size_t total = 44 + DEV_LEN;
    if (cap < total)
        return ACPIGEN_E_SMALL;
    memset(buf, 0, total);
    uint32_t v = 44;
    memcpy(buf + 36, &v, 4); /* OffsetDbgDeviceInfo */
    v = 1;
    memcpy(buf + 40, &v, 4); /* NumberDbgDeviceInfo */
    uint8_t *d = buf + 44;
    uint16_t w;
    d[0] = 0;                                  /* Revision */
    w = DEV_LEN;            memcpy(d + 1, &w, 2);  /* Length */
    d[3] = 1;                                  /* NumberOfGenericAddressRegisters */
    w = 2;                  memcpy(d + 4, &w, 2);  /* NamespaceStringLength */
    w = DEV_HDR + 12 + 4;   memcpy(d + 6, &w, 2);  /* NamespaceStringOffset */
    w = 0;                  memcpy(d + 8, &w, 2);  /* OemDataLength */
                            memcpy(d + 10, &w, 2); /* OemDataOffset */
    w = ACPI_DBG2_PORT_SERIAL; memcpy(d + 12, &w, 2); /* PortType */
    w = c->port_subtype;    memcpy(d + 14, &w, 2); /* PortSubtype */
                                                   /* d+16: Reserved */
    w = DEV_HDR;            memcpy(d + 18, &w, 2); /* BaseAddressRegisterOffset */
    w = DEV_HDR + 12;       memcpy(d + 20, &w, 2); /* AddressSizeOffset */
    uint8_t *gas = d + DEV_HDR;
    gas[0] = 0;             /* SystemMemory */
    gas[1] = c->bit_width;
    gas[2] = 0;
    gas[3] = c->access_size;
    memcpy(gas + 4, &c->base, 8);
    memcpy(d + DEV_HDR + 12, &c->size, 4);
    d[DEV_HDR + 16] = '.';  /* namespace string "." = nessun oggetto ACPI */
    d[DEV_HDR + 17] = 0;
    acpigen_finish(buf, total, "DBG2", ACPI_DBG2_REVISION, oem);
    return (int64_t)total;
}
