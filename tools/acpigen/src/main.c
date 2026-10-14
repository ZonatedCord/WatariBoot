// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>
#include "acpigen.h"

int main(int argc, char **argv)
{
    static uint8_t buf[4096];
    const char *mname = "t8103";
    if (argc == 6 && !strcmp(argv[2], "-m")) {
        mname = argv[3];
        argv[2] = argv[4];
        argv[3] = argv[5];
        argc = 4;
    }
    if (argc != 4 || strcmp(argv[2], "-o")) {
        fprintf(stderr, "uso: %s <madt|gtdt|pptt|csrt|fadt|dbg2> [-m t8103|qemu-virt] -o file.bin\n",
                argv[0]);
        return 2;
    }
    const acpigen_model *m = acpigen_find_model(mname);
    if (!m) {
        fprintf(stderr, "modello sconosciuto: %s\n", mname);
        return 2;
    }
    int64_t n;
    if (!strcmp(argv[1], "madt"))
        n = acpigen_build_madt(buf, sizeof buf, 0, m->cpus, m->ncpus, m->gicd, &m->madt_opts);
    else if (!strcmp(argv[1], "gtdt"))
        n = acpigen_build_gtdt(buf, sizeof buf, 0, m->gtdt);
    else if (!strcmp(argv[1], "pptt"))
        n = acpigen_build_pptt(buf, sizeof buf, 0, m->cpus, m->ncpus);
    else if (!strcmp(argv[1], "csrt"))
        n = acpigen_build_csrt(buf, sizeof buf, 0, m->csrt);
    else if (!strcmp(argv[1], "fadt"))
        n = acpigen_build_fadt(buf, sizeof buf, 0, m->fadt);
    else if (!strcmp(argv[1], "dbg2"))
        n = acpigen_build_dbg2(buf, sizeof buf, 0, m->dbg2);
    else {
        fprintf(stderr, "tabella sconosciuta: %s\n", argv[1]);
        return 2;
    }
    if (n < 0) {
        fprintf(stderr, "errore di generazione (%s per il modello %s): %lld\n", argv[1], m->name,
                (long long)n);
        return 1;
    }
    FILE *f = fopen(argv[3], "wb");
    if (!f || fwrite(buf, 1, (size_t)n, f) != (size_t)n || fclose(f)) {
        perror(argv[3]);
        return 1;
    }
    printf("%s (%s): %lld byte\n", argv[1], m->name, (long long)n);
    return 0;
}
