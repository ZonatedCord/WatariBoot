// SPDX-License-Identifier: MIT
// GSIVs of device interrupts come from the layer's shared interrupt map
// (el2/include/wel2_intr_map.h), not from numbers typed in acpigen: the value ACPI hands to
// Windows must equal the vINTID the layer injects (docs/design/watari-el2-devirq, FR-D10,
// milestone M2.x-3). acpigen has no DSDT/device-entry generator yet (see the TODO in the
// README); this test pins the contract such a generator must use: gsiv = intr_gsiv(map, hwirq)
// for every allow-listed source, level/edge from the same table.
#include <string.h>
#include "test.h"
#include "wel2_intr_map.h"

int main(void)
{
    const struct intr_map *m = &INTR_MAP_T8103;

    CHECK(intr_map_validate(m) == INTR_OK);
    /* j293 numbers of ARCHITECTURE section 6 (AIC hwirq -> GSIV) */
    CHECK(intr_gsiv(m, 590) == 622);   /* ANS NVMe */
    CHECK(intr_gsiv(m, 777) == 809);   /* dwc3_0 xHCI */
    CHECK(intr_gsiv(m, 857) == 889);   /* dwc3_1 xHCI */
    CHECK(intr_gsiv(m, 695) == 727);   /* PCIe port 0 INTx */
    CHECK(intr_gsiv(m, 698) == 730);
    CHECK(intr_gsiv(m, 701) == 733);
    /* every allow-listed device: GSIV = 32 + hwirq, level for all t8103 wired sources, and
     * the layer's inverse maps it back */
    for (uint32_t i = 0; i < m->n_allow; i++) {
        uint32_t h = m->allow[i].hwirq, g = intr_gsiv(m, h);

        CHECK(g == 32 + h);
        CHECK(intr_exposed_vintid(m, h) == g);
        CHECK(intr_exposed_hwirq(m, g) == h);
        CHECK(intr_trigger(m, h) == INTR_TRIG_LEVEL);
    }
    /* sources that are not allow-listed get no GSIV in the ACPI tables */
    CHECK(intr_exposed_vintid(m, 583) == INTR_INVALID);
    CHECK(intr_exposed_vintid(m, 781) == INTR_INVALID);
    /* the MSI pool is not a wired range: the GSIV of its vectors is the v2m frame's SPI */
    CHECK(intr_gsiv(m, 704) == 928 && intr_gsiv(m, 735) == 959);
    /* consistency with the (vestigial) GIC MSI Frame the MADT already carries for QEMU:
     * the layer's frame SPIs 928-959 are exactly the pool's vINTIDs */
    CHECK(m->msi_vintid_base == 928 && m->msi_count == 32);
    TEST_END();
}
