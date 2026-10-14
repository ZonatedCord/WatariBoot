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
    /* modelli senza CSRT/DBG2 */
    CHECK(acpigen_build_csrt(b, sizeof b, 0, qemu_virt_model.csrt) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_dbg2(b, sizeof b, 0, t8103_model.dbg2) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_csrt(b, sizeof b, 0, t8103_model.csrt) > 0);
    TEST_END();
}
