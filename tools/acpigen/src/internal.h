// SPDX-License-Identifier: MIT
#ifndef ACPIGEN_INTERNAL_H
#define ACPIGEN_INTERNAL_H
#include "acpigen.h"

/* Scrive l'header SDT con length = len e checksum corretto. */
void acpigen_finish(uint8_t *buf, size_t len, const char sig[4], uint8_t rev,
                    const acpigen_oem *oem);
#endif
