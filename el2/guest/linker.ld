/* SPDX-License-Identifier: MIT */
/* Watari EL2 test guest: flat position-independent image linked at 0 (no relocations, see start.S). The loader puts
 * it at any 4 KB aligned address; image_size in the arm64 header covers .bss and the stacks. */
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata : ALIGN(16) { *(.rodata .rodata.*) }
    .data : ALIGN(16) { *(.data .data.*) }
    .bss : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4096);
        __stacks = .;               /* 8 slots, slot n = core n's stack, top = __stacks + (n + 1) * STACK_SIZE */
        . += 0x2000 * 8;
        __bss_end = .;
    }
    __image_size = ABSOLUTE(__bss_end);
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash) *(.interp) }
}
