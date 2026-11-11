/* SPDX-License-Identifier: MIT */
/* Watari EL2: linker script. Position-independent image (T6a): linked at address 0 as a PIE, relocated at run
 * time by start.S (R_AARCH64_RELATIVE only) to wherever the loader put it (QEMU -kernel: RAM base + the header's
 * text_offset 0x80000; the M1: where m1n1 puts it). Every symbol below is defined INSIDE an output section, so it
 * is section-relative (relocated with the image), never absolute (check-pie.sh verifies it). The load address must
 * be 16 KB aligned (start.S checks it): the stage-1 page permissions (stage1.c) split text/rodata at 16 KB
 * boundaries and the vector table needs 2 KB alignment of its run-time address. */
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text.boot : {
        __image_start = .;
        KEEP(*(.text.boot))
    }
    .text : {
        *(.text .text.*)
        . = ALIGN(16384);          /* page permissions of the EL2 stage-1 map (stage1.c): 16 KB pages on the M1 */
        __text_end = .;
    }
    .rela.dyn : ALIGN(8) {         /* read-only after the relocation loop; inside [__text_end, __rodata_end) */
        __rela_start = .;
        *(.rela.dyn)
        __rela_end = .;
    }
    .rodata : {
        *(.rodata .rodata.*)
        *(.data.rel.ro .data.rel.ro.*)   /* const data holding pointers: written by the relocation loop (MMU off), RO after */
        . = ALIGN(16384);
        __rodata_end = .;
    }
    .data : { *(.data .data.*) }
    .bss : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
        . = ALIGN(16384);
        __image_end = .;
        __el2_stacks = .;          /* 8 x 16 KB EL2 stacks, slot = wel2_core_index(MPIDR), see start.S */
        . += 0x4000 * 8;
        __el1_stack_top = . + 0x4000;   /* the EL1 test payload's stack: one more 16 KB slot */
        . += 0x4000;
        __layer_end = .;
    }
    /* image_size of the arm64 Image header: image, .bss and stacks (what the loader must keep free) */
    __image_size = ABSOLUTE(__layer_end - __image_start);
    ASSERT(__el1_stack_top - __el2_stacks == 9 * 0x4000, "stack area must be WEL2_NSTACKS * WEL2_STACK_SIZE (include/wel2_coreidx.h) + the EL1 slot")
    ASSERT(__layer_end <= 0x180000, "Watari EL2 image and stacks must fit the 2 MB carve-out after the 0x80000 text_offset (stage1.c)")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash) *(.interp) }
}
