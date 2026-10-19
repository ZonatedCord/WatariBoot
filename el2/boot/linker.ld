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
    __el2_stacks = .;              /* 8 x 16 KB EL2 stacks, core i uses slot i */
    . += 0x4000 * 8;
    . += 0x4000;
    __el1_stack_top = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
