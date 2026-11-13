// SPDX-License-Identifier: MIT
// Model j293 (M1 under Watari EL2): every number the guest reads from ACPI is cross-checked with the
// layer's own headers/tables (core index, timer PPIs, interrupt map) and with the measurements.
#include <string.h>
#include "test.h"
#include "wel2_intr_map.h"
#include "wel2_coreidx.h"
#include "wel2_apple_fiq.h"

/* The layer's platform windows (el2/boot/plat.h PLAT_M1_*, not includable on the host: boot.h needs the
 * freestanding build). The Makefile greps plat.h and wel2_vgic.h for the same literals in `make test`. */
#define LAYER_GICD 0xF00000000ull
#define LAYER_GICR 0xF10000000ull
#define LAYER_GICR_STRIDE 0x20000ull /* VGIC_GICR_FRAME_SIZE in wel2_vgic.h */

static acpi_gicc gicc_at(const uint8_t *b, size_t off)
{ acpi_gicc g; memcpy(&g, b + off, sizeof g); return g; }

int main(void)
{
    uint8_t b[1024];
    const acpigen_model *m = &j293_model;
    const struct intr_map *im = &INTR_MAP_T8103;
    CHECK(acpigen_find_model("j293") == m);
    CHECK(m->ncpus == 8 && J293_NCPUS == 8);

    /* ---- MADT: GICD, 8 GICC, GICR, MSI frame ---- */
    int64_t n = acpigen_build_madt(b, sizeof b, 0, m->cpus, m->ncpus, m->gicd, &m->madt_opts);
    CHECK(n == 44 + 24 + 8 * 82 + 16 + 24);
    check_table(b, n, "APIC", 6);
    acpi_madt_header mh; memcpy(&mh, b, sizeof mh);
    CHECK(mh.local_apic_address == 0 && mh.flags == 0);
    acpi_gicd d; memcpy(&d, b + 44, sizeof d);
    CHECK(d.type == 0x0C && d.length == 24 && d.gic_version == 3);
    CHECK(d.physical_base_address == LAYER_GICD && d.gic_id == 0 && d.system_vector_base == 0);
    for (size_t k = 0; k < 8; k++) {
        acpi_gicc g = gicc_at(b, 44 + 24 + k * 82);
        uint64_t mp = k < 4 ? 0x80000000ull + k : 0x80010100ull + (k - 4); /* measured, full MPIDR_EL1 */
        CHECK(g.type == 0x0B && g.length == 82 && g.flags == ACPI_GICC_ENABLED);
        CHECK(g.mpidr == (mp & 0xFF00FFFFFFull)); /* ACPI carries the affinity fields only */
        CHECK(g.acpi_processor_uid == k && g.cpu_interface_number == k);
        /* the layer's dense index (stack slot / vCPU order) equals the GICC order and the UID */
        CHECK(wel2_core_index(mp) == (unsigned)k);
        CHECK(wel2_core_index(g.mpidr) == (unsigned)k);
        CHECK(g.processor_power_efficiency_class == (k < 4 ? 0 : 1));
        CHECK(g.parking_protocol_version == 0 && g.parked_address == 0); /* PSCI, no parking */
        CHECK(g.gicr_base_address == 0 && g.performance_interrupt_gsiv == 0);
        CHECK(g.physical_base_address == 0 && g.gicv == 0 && g.gich == 0 && g.vgic_maintenance_interrupt == 0);
    }
    /* boot core = cpu0 (measured MPIDR 0x80000000) is the first GICC */
    CHECK(gicc_at(b, 44 + 24).mpidr == 0 && gicc_at(b, 44 + 24).acpi_processor_uid == 0);
    acpi_gicr r; memcpy(&r, b + 44 + 24 + 8 * 82, sizeof r);
    CHECK(r.type == 0x0E && r.length == 16 && r.discovery_range_base == LAYER_GICR);
    CHECK(r.discovery_range_length == 8 * LAYER_GICR_STRIDE && r.discovery_range_length == 0x100000);
    CHECK(r.discovery_range_base + r.discovery_range_length <= (1ull << 36)); /* inside the 36-bit PA (measured PARange) */
    acpi_gic_msi ms; memcpy(&ms, b + 44 + 24 + 8 * 82 + 16, sizeof ms);
    CHECK(ms.type == 0x0D && ms.length == 24 && ms.physical_base_address == 0xFFFFF000ull);
    CHECK(ms.flags == 1 && ms.msi_frame_id == 0); /* SPI Count/Base Select */
    CHECK(ms.spi_base == im->msi_vintid_base && ms.spi_count == im->msi_count);
    CHECK(ms.spi_base == 928 && ms.spi_count == 32);
    /* and the frame's SPIs are exactly the vINTIDs of the pool hwirq 704..735 */
    CHECK(intr_msi_vintid(im, im->msi_hwirq_base) == ms.spi_base);
    CHECK(intr_msi_vintid(im, im->msi_hwirq_base + ms.spi_count - 1) == (uint32_t)ms.spi_base + ms.spi_count - 1);
    CHECK(ms.spi_base + ms.spi_count <= 960); /* GICD_TYPER ITLinesNumber = 29 */

    /* ---- GTDT: PPIs as the layer injects them ---- */
    n = acpigen_build_gtdt(b, sizeof b, 0, m->gtdt);
    CHECK(n == 104);
    check_table(b, n, "GTDT", 3);
    acpi_gtdt t; memcpy(&t, b, sizeof t);
    CHECK(t.virtual_timer_gsiv == APPLE_PPI_VTIMER && t.virtual_timer_gsiv == 27);
    CHECK(t.nonsecure_pl1_gsiv == APPLE_PPI_PTIMER && t.nonsecure_pl1_gsiv == 30);
    CHECK(t.secure_pl1_gsiv == 29 && t.nonsecure_pl2_gsiv == 26 && t.virtual_pl2_gsiv == 0);
    /* level (bit 0 = 0), active high (bit 1 = 0): the vGIC PPIs are level-high */
    CHECK(!(t.virtual_timer_flags & (ACPI_GTDT_TIMER_EDGE | ACPI_GTDT_TIMER_ACTIVE_LOW)));
    CHECK(!(t.nonsecure_pl1_flags & (ACPI_GTDT_TIMER_EDGE | ACPI_GTDT_TIMER_ACTIVE_LOW)));
    CHECK(t.nonsecure_pl1_flags == ACPI_GTDT_TIMER_ALWAYS_ON);
    CHECK(t.cnt_control_base == ~0ull && t.cnt_read_base == ~0ull);
    CHECK(t.platform_timer_count == 0 && t.platform_timer_offset == 0);
    /* PPIs are INTIDs 16..31 */
    CHECK(t.virtual_timer_gsiv >= 16 && t.virtual_timer_gsiv < 32 && t.nonsecure_pl1_gsiv >= 16);

    /* ---- FADT: HW-reduced, PSCI over HVC ---- */
    n = acpigen_build_fadt(b, sizeof b, 0, m->fadt);
    CHECK(n == 276);
    check_table(b, n, "FACP", 6);
    uint32_t flags; uint16_t boot;
    memcpy(&flags, b + 112, 4); memcpy(&boot, b + 129, 2);
    CHECK(flags & ACPI_FADT_HW_REDUCED_ACPI);
    CHECK(boot == (ACPI_FADT_ARM_PSCI_COMPLIANT | ACPI_FADT_ARM_PSCI_USE_HVC) && boot == 3);
    CHECK(b[131] == 5);

    /* ---- PPTT: package -> E cluster (4) / P cluster (4) -> 8 cores, no caches ---- */
    n = acpigen_build_pptt(b, sizeof b, 0, m->cpus, m->ncpus);
    CHECK(n == 36 + 20 * (1 + 2 + 8));
    check_table(b, n, "PPTT", 3);
    for (size_t i = 0; i < 8; i++) {
        acpi_pptt_processor c; memcpy(&c, b + 96 + 20 * i, sizeof c);
        CHECK(c.flags == (ACPI_PPTT_ID_VALID | ACPI_PPTT_IS_LEAF));
        CHECK(c.acpi_processor_id == (uint32_t)i); /* = MADT UID */
        CHECK(c.parent == (uint32_t)(56 + 20 * (i / 4))); /* E = cluster 0, P = cluster 1 */
        CHECK(c.number_of_private_resources == 0);
    }

    /* ---- DBG2/SPCR/CSRT intentionally absent ---- */
    CHECK(m->dbg2 == 0 && m->csrt == 0);
    CHECK(acpigen_build_dbg2(b, sizeof b, 0, m->dbg2) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_csrt(b, sizeof b, 0, m->csrt) == ACPIGEN_E_INVAL);
    TEST_END();
}
