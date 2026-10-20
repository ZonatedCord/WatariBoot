/* SPDX-License-Identifier: MIT */
/* Watari EL2 skeleton: linker script. Loaded by QEMU -kernel at RAM base + text_offset. */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    __image_start = .;
    .text.boot : { KEEP(*(.text.boot)) }
    .text : { *(.text .text.*) }
    . = ALIGN(4096);               /* page permissions of the EL2 stage-1 map (stage1.c) */
    __text_end = .;
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    __rodata_end = .;
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
    ASSERT(. <= 0x40200000, "Watari EL2 image and stacks must fit the first 2 MB of RAM (stage1.c)")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
