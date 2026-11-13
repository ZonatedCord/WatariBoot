// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * Shim of <string.h> used ONLY inside the EDK2 build of WatariM1AcpiDxe (this directory is on the
 * include path of that module alone). It lets the sources of tools/acpigen, which use memcpy,
 * memset and strcmp, compile unchanged on top of BaseMemoryLib/BaseLib. Do not include it from
 * anything else.
 */
#ifndef WATARI_M1_SHIM_STRING_H
#define WATARI_M1_SHIM_STRING_H

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define memcpy(d, s, n)  CopyMem((d), (s), (n))
#define memset(d, v, n)  SetMem((d), (n), (UINT8)(v))
#define strcmp(a, b)     AsciiStrCmp((a), (b))

#endif
