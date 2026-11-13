/* SPDX-License-Identifier: MIT */
/* Force-included in every translation unit of the layer image (Makefile: -include pie.h). clang applies
 * -fvisibility=hidden to definitions only, so an extern function or object declared in a header (and defined in
 * assembly or in another file) would still be reached through the GOT in a PIE. Hidden declarations are direct
 * (adrp + add): the image needs no GOT, so nothing depends on a table that is only valid after the relocation
 * loop of start.S. */
#pragma GCC visibility push(hidden)
