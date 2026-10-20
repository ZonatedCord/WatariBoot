// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
/* Mirrors of EFI_ACPI_DEBUG_PORT_2_DESCRIPTION_TABLE and
 * EFI_ACPI_DBG2_DEBUG_DEVICE_INFORMATION_STRUCT (DebugPort2Table.h, packed) plus the 12-byte GAS
 * [verified: edk2-stable202608, issue #44]. */
typedef struct __attribute__((packed)) {
    acpi_sdt_header hdr;
    uint32_t OffsetDbgDeviceInfo;
    uint32_t NumberDbgDeviceInfo;
} edk2_dbg2_hdr;
typedef struct __attribute__((packed)) {
    uint8_t  Revision;
    uint16_t Length;
    uint8_t  NumberofGenericAddressRegisters;
    uint16_t NameSpaceStringLength;
    uint16_t NameSpaceStringOffset;
    uint16_t OemDataLength;
    uint16_t OemDataOffset;
    uint16_t PortType;
    uint16_t PortSubtype;
    uint8_t  Reserved[2];
    uint16_t BaseAddressRegisterOffset;
    uint16_t AddressSizeOffset;
} edk2_dbg2_dev;
typedef struct __attribute__((packed)) {
    uint8_t  AddressSpaceId, RegisterBitWidth, RegisterBitOffset, AccessSize;
    uint64_t Address;
} edk2_gas;
_Static_assert(sizeof(edk2_dbg2_hdr) == 44, "");
_Static_assert(offsetof(edk2_dbg2_hdr, OffsetDbgDeviceInfo) == 36, "");
_Static_assert(offsetof(edk2_dbg2_hdr, NumberDbgDeviceInfo) == 40, "");
_Static_assert(sizeof(edk2_dbg2_dev) == 22, "");
_Static_assert(offsetof(edk2_dbg2_dev, Length) == 1, "");
_Static_assert(offsetof(edk2_dbg2_dev, NumberofGenericAddressRegisters) == 3, "");
_Static_assert(offsetof(edk2_dbg2_dev, NameSpaceStringLength) == 4, "");
_Static_assert(offsetof(edk2_dbg2_dev, NameSpaceStringOffset) == 6, "");
_Static_assert(offsetof(edk2_dbg2_dev, OemDataLength) == 8, "");
_Static_assert(offsetof(edk2_dbg2_dev, OemDataOffset) == 10, "");
_Static_assert(offsetof(edk2_dbg2_dev, PortType) == 12, "");
_Static_assert(offsetof(edk2_dbg2_dev, PortSubtype) == 14, "");
_Static_assert(offsetof(edk2_dbg2_dev, Reserved) == 16, "");
_Static_assert(offsetof(edk2_dbg2_dev, BaseAddressRegisterOffset) == 18, "");
_Static_assert(offsetof(edk2_dbg2_dev, AddressSizeOffset) == 20, "");
_Static_assert(sizeof(edk2_gas) == 12, "");
_Static_assert(offsetof(edk2_gas, AccessSize) == 3, "");
_Static_assert(offsetof(edk2_gas, Address) == 4, "");
_Static_assert(ACPI_DBG2_PORT_SERIAL == 0x8000 && ACPI_DBG2_SUBTYPE_PL011 == 0x0003 &&
               ACPI_DBG2_SUBTYPE_16550 == 0x0000, "DBG2 port constants");

int main(void)
{
    uint8_t b[256];
    int64_t n = acpigen_build_dbg2(b, sizeof b, 0, &qemu_virt_dbg2);
    CHECK(n == 44 + 40);
    check_table(b, n, "DBG2", 0);
    uint32_t off, num; memcpy(&off, b + 36, 4); memcpy(&num, b + 40, 4);
    CHECK(off == 44 && num == 1);
    const uint8_t *d = b + off;
    uint16_t len, nsl, nso, ptype, psub, bar, asz;
    memcpy(&len, d + 1, 2); memcpy(&nsl, d + 4, 2); memcpy(&nso, d + 6, 2);
    memcpy(&ptype, d + 12, 2); memcpy(&psub, d + 14, 2);
    memcpy(&bar, d + 18, 2); memcpy(&asz, d + 20, 2);
    CHECK(d[0] == 0 && len == 40 && d[3] == 1);
    CHECK(ptype == 0x8000 && psub == 0x0003);          /* seriale, PL011 */
    CHECK(bar == 22 && asz == 34 && nso == 38 && nsl == 2);
    uint64_t base; uint32_t size;
    memcpy(&base, d + bar + 4, 8); memcpy(&size, d + asz, 4);
    CHECK(base == 0x09000000ull && size == 0x1000);
    CHECK(d[bar] == 0 && d[bar + 1] == 32 && d[bar + 3] == 3);  /* SystemMemory, 32 bit, DWORD */
    CHECK(d[nso] == '.' && d[nso + 1] == 0);
    /* Same fields read through the EDK2-layout structs. */
    const edk2_dbg2_hdr *h = (const edk2_dbg2_hdr *)b;
    const edk2_dbg2_dev *dv = (const edk2_dbg2_dev *)(b + h->OffsetDbgDeviceInfo);
    const edk2_gas *gas = (const edk2_gas *)((const uint8_t *)dv + dv->BaseAddressRegisterOffset);
    CHECK(h->NumberDbgDeviceInfo == 1 && dv->Revision == 0 && dv->Length == 40);
    CHECK(dv->NumberofGenericAddressRegisters == 1 && dv->OemDataLength == 0 && dv->OemDataOffset == 0);
    CHECK(dv->Reserved[0] == 0 && dv->Reserved[1] == 0);
    CHECK(gas->AddressSpaceId == 0 && gas->RegisterBitWidth == 32 && gas->RegisterBitOffset == 0 &&
          gas->AccessSize == 3 && gas->Address == 0x09000000ull);
    CHECK(acpigen_build_dbg2(b, 83, 0, &qemu_virt_dbg2) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_dbg2(b, sizeof b, 0, 0) == ACPIGEN_E_INVAL);
    acpigen_dbg2_cfg z = qemu_virt_dbg2; z.size = 0;
    CHECK(acpigen_build_dbg2(b, sizeof b, 0, &z) == ACPIGEN_E_INVAL);
    TEST_END();
}
