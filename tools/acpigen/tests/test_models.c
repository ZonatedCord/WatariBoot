// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
int main(void)
{
    uint8_t b[2048];
    CHECK(acpigen_find_model("t8103") == &t8103_model);
    CHECK(acpigen_find_model("qemu-virt") == &qemu_virt_model);
    CHECK(acpigen_find_model("nope") == 0);
    const acpigen_model *ms[2] = { &t8103_model, &qemu_virt_model };
    for (int i = 0; i < 2; i++) {
        const acpigen_model *m = ms[i];
        int64_t n = acpigen_build_madt(b, sizeof b, 0, m->cpus, m->ncpus, m->gicd, &m->madt_opts);
        CHECK(n > 0); check_table(b, n, "APIC", 6);
        n = acpigen_build_gtdt(b, sizeof b, 0, m->gtdt);
        CHECK(n == 104); check_table(b, n, "GTDT", 3);
        n = acpigen_build_pptt(b, sizeof b, 0, m->cpus, m->ncpus);
        CHECK(n > 0); check_table(b, n, "PPTT", 3);
        n = acpigen_build_fadt(b, sizeof b, 0, m->fadt);
        CHECK(n == 276); check_table(b, n, "FACP", 6);
    }
    /* qemu-virt: valori dal DT di QEMU (verificato) */
    acpi_gtdt t;
    acpigen_build_gtdt(b, sizeof b, 0, qemu_virt_model.gtdt);
    memcpy(&t, b, sizeof t);
    CHECK(t.secure_pl1_gsiv == 29 && t.nonsecure_pl1_gsiv == 30);
    CHECK(t.virtual_timer_gsiv == 27 && t.nonsecure_pl2_gsiv == 26);
    CHECK(t.nonsecure_pl1_flags == ACPI_GTDT_TIMER_ALWAYS_ON);
    CHECK(qemu_virt_model.ncpus == 4);
    for (size_t i = 0; i < 4; i++)
        CHECK(qemu_virt_cpus[i].mpidr == i);
    /* T22 qemu-virt-bench: QEMU GIC addresses, the layer's MSI frame (0xFFFFF000, SPI 928, 32), PSCI over HVC, no DBG2 */
    const acpigen_model *bm = acpigen_find_model("qemu-virt-bench");
    CHECK(bm == &qemu_virt_bench_model && bm->ncpus == 4 && bm->dbg2 == 0);
    CHECK(bm->gicd->physical_base_address == 0x08000000ull && bm->madt_opts.gicr_range_base == 0x080A0000ull);
    CHECK(bm->madt_opts.msi_base == 0xFFFFF000ull && bm->madt_opts.msi_spi_base == 928 && bm->madt_opts.msi_spi_count == 32);
    CHECK(acpigen_build_madt(b, sizeof b, 0, bm->cpus, bm->ncpus, bm->gicd, &bm->madt_opts) > 0);
    CHECK(acpigen_build_fadt(b, sizeof b, 0, bm->fadt) == 276);
    /* T16b j293-e4: j293 addresses, four E cores, GICR range of four frames */
    const acpigen_model *em = acpigen_find_model("j293-e4");
    CHECK(em == &j293_e4_model && em->ncpus == 4 && em->gicd->physical_base_address == 0xF00000000ull);
    CHECK(em->madt_opts.gicr_range_base == 0xF10000000ull && em->madt_opts.gicr_range_length == 4 * 0x20000);
    CHECK(em->madt_opts.msi_base == 0xFFFFF000ull && em->fadt == &j293_fadt && em->dbg2 == 0);
    for (size_t i = 0; i < 4; i++)
        CHECK(em->cpus[i].mpidr == i && em->cpus[i].efficiency == 0);
    CHECK(acpigen_build_madt(b, sizeof b, 0, em->cpus, em->ncpus, em->gicd, &em->madt_opts) > 0);
    /* T16g j293-e1: boot core only, one GICR frame, everything else as j293 */
    const acpigen_model *om = acpigen_find_model("j293-e1");
    CHECK(om == &j293_e1_model && om->ncpus == 1 && om->cpus[0].mpidr == 0 && om->cpus[0].efficiency == 0);
    CHECK(om->gicd == &j293_gicd && om->madt_opts.gicr_range_base == 0xF10000000ull && om->madt_opts.gicr_range_length == 0x20000);
    CHECK(om->madt_opts.msi_base == 0xFFFFF000ull && om->fadt == &j293_fadt && om->gtdt == &j293_gtdt && om->dbg2 == 0);
    CHECK(acpigen_build_madt(b, sizeof b, 0, om->cpus, om->ncpus, om->gicd, &om->madt_opts) > 0);
    CHECK(acpigen_build_pptt(b, sizeof b, 0, om->cpus, om->ncpus) > 0);
    /* modelli senza CSRT/DBG2 */
    CHECK(acpigen_build_csrt(b, sizeof b, 0, qemu_virt_model.csrt) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_dbg2(b, sizeof b, 0, t8103_model.dbg2) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_csrt(b, sizeof b, 0, t8103_model.csrt) > 0);
    TEST_END();
}
