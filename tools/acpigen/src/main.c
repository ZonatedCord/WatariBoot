// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>
#include "acpigen.h"

int main(int argc, char **argv)
{
    static uint8_t buf[4096];
    if (argc != 4 || strcmp(argv[2], "-o")) {
        fprintf(stderr, "uso: %s <madt|gtdt|pptt|csrt> -o file.bin\n", argv[0]);
        return 2;
    }
    int64_t n;
    if (!strcmp(argv[1], "madt"))
        n = acpigen_build_madt(buf, sizeof buf, 0, t8103_cpus, T8103_NCPUS, 0);
    else if (!strcmp(argv[1], "gtdt"))
        n = acpigen_build_gtdt(buf, sizeof buf, 0, &t8103_gtdt);
    else if (!strcmp(argv[1], "pptt"))
        n = acpigen_build_pptt(buf, sizeof buf, 0, t8103_cpus, T8103_NCPUS);
    else if (!strcmp(argv[1], "csrt"))
        n = acpigen_build_csrt(buf, sizeof buf, 0, &t8103_csrt);
    else {
        fprintf(stderr, "tabella sconosciuta: %s\n", argv[1]);
        return 2;
    }
    if (n < 0) {
        fprintf(stderr, "errore di generazione: %lld\n", (long long)n);
        return 1;
    }
    FILE *f = fopen(argv[3], "wb");
    if (!f || fwrite(buf, 1, (size_t)n, f) != (size_t)n || fclose(f)) {
        perror(argv[3]);
        return 1;
    }
    printf("%s: %lld byte\n", argv[1], (long long)n);
    return 0;
}
