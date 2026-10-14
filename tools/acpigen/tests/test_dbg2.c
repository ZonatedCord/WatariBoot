// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
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
    CHECK(acpigen_build_dbg2(b, 83, 0, &qemu_virt_dbg2) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_dbg2(b, sizeof b, 0, 0) == ACPIGEN_E_INVAL);
    acpigen_dbg2_cfg z = qemu_virt_dbg2; z.size = 0;
    CHECK(acpigen_build_dbg2(b, sizeof b, 0, &z) == ACPIGEN_E_INVAL);
    TEST_END();
}
