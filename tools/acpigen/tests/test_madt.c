// SPDX-License-Identifier: MIT
#include <string.h>
#include "test.h"
/* Layout: ACPI 6.5 sez. 5.2.12 / Acpi65.h */
_Static_assert(sizeof(acpi_sdt_header) == 36, "hdr");
_Static_assert(sizeof(acpi_madt_header) == 44, "madt hdr");
_Static_assert(sizeof(acpi_gicc) == 82, "gicc");
_Static_assert(sizeof(acpi_gicd) == 24, "gicd");
_Static_assert(offsetof(acpi_gicc, cpu_interface_number) == 4, "");
_Static_assert(offsetof(acpi_gicc, acpi_processor_uid) == 8, "");
_Static_assert(offsetof(acpi_gicc, flags) == 12, "");
_Static_assert(offsetof(acpi_gicc, parking_protocol_version) == 16, "");
_Static_assert(offsetof(acpi_gicc, performance_interrupt_gsiv) == 20, "");
_Static_assert(offsetof(acpi_gicc, parked_address) == 24, "");
_Static_assert(offsetof(acpi_gicc, physical_base_address) == 32, "");
_Static_assert(offsetof(acpi_gicc, gicv) == 40, "");
_Static_assert(offsetof(acpi_gicc, gich) == 48, "");
_Static_assert(offsetof(acpi_gicc, vgic_maintenance_interrupt) == 56, "");
_Static_assert(offsetof(acpi_gicc, gicr_base_address) == 60, "");
_Static_assert(offsetof(acpi_gicc, mpidr) == 68, "");
_Static_assert(offsetof(acpi_gicc, processor_power_efficiency_class) == 76, "");
_Static_assert(offsetof(acpi_gicc, spe_overflow_interrupt) == 78, "");
_Static_assert(offsetof(acpi_gicc, trbe_interrupt) == 80, "");
_Static_assert(offsetof(acpi_gicd, gic_id) == 4, "");
_Static_assert(offsetof(acpi_gicd, physical_base_address) == 8, "");
_Static_assert(offsetof(acpi_gicd, system_vector_base) == 16, "");
_Static_assert(offsetof(acpi_gicd, gic_version) == 20, "");

_Static_assert(sizeof(acpi_gicr) == 16, "gicr");

static acpi_gicc gicc_at(const uint8_t *b, size_t k)
{
    acpi_gicc g; memcpy(&g, b + 44 + k * 82, sizeof g); return g;
}

int main(void)
{
    uint8_t b[2048];
    const acpigen_model *m = &t8103_model;
    int64_t n = acpigen_build_madt(b, sizeof b, 0, m->cpus, m->ncpus, m->gicd, &m->madt_opts);
    CHECK(n == 44 + 8 * 82);   /* niente GICD segnaposto di default */
    CHECK(m->gicd == 0);
    check_table(b, n, "APIC", 6);
    for (int i = 0; i < T8103_NCPUS; i++) {
        acpi_gicc g = gicc_at(b, (size_t)i);
        CHECK(g.type == 0x0B && g.length == 82);
        CHECK(g.cpu_interface_number == (uint32_t)i);
        CHECK(g.acpi_processor_uid == t8103_cpus[i].uid);
        CHECK(g.flags == 1);
        CHECK(g.mpidr == t8103_cpus[i].mpidr);
        CHECK(g.processor_power_efficiency_class == (i < 4 ? 0 : 1));
        /* parking: versione 1, indirizzo non zero, allineato 4K, distinto */
        CHECK(g.parking_protocol_version == 1);
        CHECK(g.parked_address != 0 && (g.parked_address & 0xFFF) == 0);
        for (int j = 0; j < i; j++)
            CHECK(g.parked_address != gicc_at(b, (size_t)j).parked_address);
        CHECK(g.gicr_base_address == 1);
    }
    /* GICD opzionale quando richiesta esplicitamente */
    n = acpigen_build_madt(b, sizeof b, 0, t8103_cpus, T8103_NCPUS, &t8103_gicd, 0);
    CHECK(n == 44 + 8 * 82 + 24);
    check_table(b, n, "APIC", 6);
    CHECK(b[44 + 8 * 82] == 0x0C && b[44 + 8 * 82 + 1] == 24);
    /* opts NULL = nessun GICR, boot 0 */
    CHECK(gicc_at(b, 0).gicr_base_address == 0);
    CHECK(acpigen_build_madt(b, sizeof b, 0, t8103_cpus, 0, 0, 0) == ACPIGEN_E_INVAL);
    CHECK(acpigen_build_madt(b, 43, 0, t8103_cpus, 1, 0, 0) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_madt(b, 44 + 8 * 82 - 1, 0, t8103_cpus, 8, 0, 0) == ACPIGEN_E_SMALL);
    CHECK(acpigen_build_madt(b, 44 + 8 * 82, 0, t8103_cpus, 8, 0, 0) == 44 + 8 * 82);

    /* CPU di boot per prima: boot_index 5 -> ordine 5,0,1,2,3,4,6,7 */
    acpigen_madt_opts o = { .boot_index = 5 };
    n = acpigen_build_madt(b, sizeof b, 0, t8103_cpus, T8103_NCPUS, 0, &o);
    CHECK(n == 44 + 8 * 82);
    check_table(b, n, "APIC", 6);
    static const uint32_t order[8] = {5, 0, 1, 2, 3, 4, 6, 7};
    for (size_t k = 0; k < 8; k++) {
        acpi_gicc g = gicc_at(b, k);
        CHECK(g.cpu_interface_number == order[k]);
        CHECK(g.acpi_processor_uid == t8103_cpus[order[k]].uid);
        CHECK(g.mpidr == t8103_cpus[order[k]].mpidr);
    }
    o.boot_index = 7;
    n = acpigen_build_madt(b, sizeof b, 0, t8103_cpus, T8103_NCPUS, 0, &o);
    CHECK(gicc_at(b, 0).cpu_interface_number == 7 && gicc_at(b, 7).cpu_interface_number == 6);
    o.boot_index = 8;
    CHECK(acpigen_build_madt(b, sizeof b, 0, t8103_cpus, T8103_NCPUS, 0, &o) == ACPIGEN_E_INVAL);

    /* validazione parking */
    acpigen_cpu c[3];
    memcpy(c, t8103_cpus, sizeof c);
    CHECK(acpigen_build_madt(b, sizeof b, 0, c, 3, 0, 0) > 0);
    c[1].parked_address += 0x10;                       /* non allineato */
    CHECK(acpigen_build_madt(b, sizeof b, 0, c, 3, 0, 0) == ACPIGEN_E_INVAL);
    memcpy(c, t8103_cpus, sizeof c);
    c[2].parked_address = c[0].parked_address;         /* duplicato */
    CHECK(acpigen_build_madt(b, sizeof b, 0, c, 3, 0, 0) == ACPIGEN_E_INVAL);
    memcpy(c, t8103_cpus, sizeof c);
    c[1].parked_address = 0;                           /* versione 1 con indirizzo 0 */
    CHECK(acpigen_build_madt(b, sizeof b, 0, c, 3, 0, 0) == ACPIGEN_E_INVAL);
    memcpy(c, t8103_cpus, sizeof c);
    c[1].parking_version = 0;                          /* versione 0 con indirizzo != 0 */
    CHECK(acpigen_build_madt(b, sizeof b, 0, c, 3, 0, 0) == ACPIGEN_E_INVAL);
    c[1].parked_address = 0;                           /* PSCI puro: ammesso */
    CHECK(acpigen_build_madt(b, sizeof b, 0, c, 3, 0, 0) > 0);
    memcpy(c, t8103_cpus, sizeof c);
    c[2].uid = c[0].uid;                               /* UID duplicato */
    CHECK(acpigen_build_madt(b, sizeof b, 0, c, 3, 0, 0) == ACPIGEN_E_INVAL);

    /* qemu-virt: GICD + GICR Structure, 4 GICC, GICC.GICR = 0 */
    m = &qemu_virt_model;
    n = acpigen_build_madt(b, sizeof b, 0, m->cpus, m->ncpus, m->gicd, &m->madt_opts);
    CHECK(n == 44 + 4 * 82 + 24 + 16 + 24);
    check_table(b, n, "APIC", 6);
    /* ordine di QEMU: GICD, 4 GICC, GICR (F-11 H2) */
    acpi_gicd d; memcpy(&d, b + 44, sizeof d);
    CHECK(d.type == 0x0C && d.physical_base_address == 0x08000000ull && d.gic_version == 3);
    acpi_gicr r; memcpy(&r, b + 44 + 24 + 4 * 82, sizeof r);
    CHECK(r.type == 0x0E && r.length == 16);
    CHECK(r.discovery_range_base == 0x080A0000ull && r.discovery_range_length == 0xF60000);
    acpi_gic_msi ms; memcpy(&ms, b + 44 + 24 + 4 * 82 + 16, sizeof ms);
    CHECK(ms.type == 0x0D && ms.length == 24 && ms.physical_base_address == 0x08020000ull);
    CHECK(ms.flags == 1 && ms.spi_count == 0x40 && ms.spi_base == 0x50);
    for (size_t k = 0; k < 4; k++) {
        acpi_gicc g = gicc_at(b + 24, k);
        CHECK(g.mpidr == k && g.cpu_interface_number == k && g.gicr_base_address == 0);
        CHECK(g.parking_protocol_version == 0 && g.parked_address == 0); /* PSCI (F-11 H1) */
        CHECK(g.performance_interrupt_gsiv == 23);
    }

    /* qemu-virt-nogicd (V2): niente GICD, 4 GICC + GICR + MSI Frame */
    m = acpigen_find_model("qemu-virt-nogicd");
    CHECK(m && m->gicd == 0);
    n = acpigen_build_madt(b, sizeof b, 0, m->cpus, m->ncpus, m->gicd, &m->madt_opts);
    CHECK(n == 44 + 4 * 82 + 16 + 24);
    check_table(b, n, "APIC", 6);
    for (size_t off = 44; off < (size_t)n; ) {
        CHECK(b[off] != 0x0C);                      /* nessuna GICD Structure */
        off += b[off + 1];
    }
    CHECK(b[44] == 0x0B && b[44 + 4 * 82] == 0x0E && b[44 + 4 * 82 + 16] == 0x0D);
    TEST_END();
}
