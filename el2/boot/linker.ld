/* SPDX-License-Identifier: MIT */
/* Watari EL2 M0-C skeleton: linker script. Loaded by QEMU -kernel at RAM base + text_offset. */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    __image_start = .;
    .text.boot : { KEEP(*(.text.boot)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(16);
    __bss_end = .;
    . = ALIGN(4096);
    __image_end = .;
    __image_size = __image_end - __image_start;
    . += 0x4000;
    __el2_stack_top = .;
    . += 0x4000;
    __el1_stack_top = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
