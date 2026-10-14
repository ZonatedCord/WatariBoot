// SPDX-License-Identifier: MIT
/*
 * Shim di <string.h> usato SOLO dentro la build EDK2 di WatariAcpiOverride (la cartella del
 * modulo e' nel percorso degli include solo per questo modulo). Permette di compilare senza
 * modifiche i sorgenti di tools/acpigen, che usano memcpy/memset/strcmp, appoggiandosi a
 * BaseMemoryLib/BaseLib. Non va incluso da nient'altro.
 */
#ifndef WATARI_SHIM_STRING_H
#define WATARI_SHIM_STRING_H

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define memcpy(d, s, n)  CopyMem((d), (s), (n))
#define memset(d, v, n)  SetMem((d), (n), (UINT8)(v))
#define strcmp(a, b)     AsciiStrCmp((a), (b))

#endif
