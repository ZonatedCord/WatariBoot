// SPDX-License-Identifier: MIT
#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <stdlib.h>
#include "acpigen.h"
static int t_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); t_fail++; } } while (0)
#define OFF(t, f) offsetof(t, f)
/* Verifica comune: lunghezza header = byte scritti, checksum = 0, signature, revisione. */
static inline void check_table(const uint8_t *b, int64_t n, const char *sig, uint8_t rev)
{
    CHECK(n >= 36);
    CHECK(memcmp(b, sig, 4) == 0);
    uint32_t len; memcpy(&len, b + 4, 4);
    CHECK((int64_t)len == n);
    CHECK(b[8] == rev);
    CHECK(acpigen_sum(b, (size_t)n) == 0);
}
#define TEST_END() do { printf("%s: %s\n", __FILE__, t_fail ? "FALLITO" : "ok"); return t_fail ? 1 : 0; } while (0)
#endif
