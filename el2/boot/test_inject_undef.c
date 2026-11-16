// SPDX-License-Identifier: MIT
// Host test of the vector-offset and SPSR logic of boot/inject_undef.c.
#include <stdint.h>
#include <stdio.h>
int wel2_undef_vector_offset(uint64_t spsr);
uint64_t wel2_undef_new_spsr(uint64_t sctlr);
static int fails;
#define CHK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void)
{
    CHK(wel2_undef_vector_offset(0x0) == 0x400);
    CHK(wel2_undef_vector_offset(0x3c0) == 0x400);
    CHK(wel2_undef_vector_offset(0x4) == 0x000);
    CHK(wel2_undef_vector_offset(0x5 | 0x3c0 | 0xf0000000ULL) == 0x200);
    CHK(wel2_undef_vector_offset(0x9) == -1);      // EL2h
    CHK(wel2_undef_vector_offset(0x8) == -1);      // EL2t
    CHK(wel2_undef_vector_offset(0xd) == -1);      // EL3h
    CHK(wel2_undef_vector_offset(0x10) == -1);     // AArch32 user
    CHK(wel2_undef_vector_offset(0x1f) == -1);     // AArch32 system
    CHK(wel2_undef_new_spsr(0) == (0x5ULL | 0x3c0 | (1ULL << 22)));          // SPAN = 0 -> PAN set
    CHK(wel2_undef_new_spsr(1ULL << 23) == (0x5ULL | 0x3c0));                // SPAN = 1 -> PAN clear
    CHK(wel2_undef_new_spsr((1ULL << 23) | (1ULL << 24)) == (0x5ULL | 0x3c0 | (1ULL << 24)));
    CHK(wel2_undef_new_spsr((1ULL << 23) | (1ULL << 44)) == (0x5ULL | 0x3c0 | (1ULL << 12)));
    CHK(wel2_undef_new_spsr(0) >> 28 == 0);        // NZCV clear
    puts(fails ? "inject_undef: FAILED" : "ok   inject_undef");
    return fails != 0;
}
