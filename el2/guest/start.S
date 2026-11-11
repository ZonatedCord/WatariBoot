// SPDX-License-Identifier: MIT
// Watari EL2 test guest (T7a): arm64 Image header, entry, EL1 vector table, secondary entry.
// Entered by the layer at EL1h, MMU and caches off, DAIF masked, x0 = struct wel2_guest_info * (or, in the QEMU
// chain build until the layer writes the block, the patched FDT). The binary is position independent: linked at 0,
// no relocations (check in the Makefile), every reference is adr/adrp-relative, so it runs wherever the loader put
// it as long as that address is 4 KB aligned. The arm64 Image header makes the layer's chain path accept it.

    .macro adr_l reg, sym
    adrp    \reg, \sym
    add     \reg, \reg, :lo12:\sym
    .endm

    .section .text.boot, "ax"
    .global _start
_start:
    b       reset                   // code0
    .long   0                       // code1
    .quad   0x80000                 // text_offset (unused: position independent)
    .quad   __image_size            // image_size: image, .bss and stacks
    .quad   0xa                     // flags: LE, 4K pages, any placement
    .quad   0, 0, 0
    .ascii  "ARM\x64"               // magic at offset 56
    .long   0

reset:
    msr     daifset, #0xf
    mov     x19, x0                 // info block (callee-saved across the calls below)
    adr_l   x1, guest_vectors
    msr     vbar_el1, x1
    isb
    adr_l   x1, __bss_start         // zero .bss (the loader image ends before it)
    adr_l   x2, __bss_end
1:  cmp     x1, x2
    b.hs    2f
    stp     xzr, xzr, [x1], #16
    b       1b
2:  adr_l   x1, __stacks
    mov     x2, #STACK_SIZE
    add     sp, x1, x2              // core 0 uses slot 0
    mov     x0, x19
    bl      guest_main
3:  wfe                             // guest_main never returns
    b       3b

// PSCI CPU_ON entry (x0 = context_id = top of this core's stack), MMU off, DAIF masked.
    .global secondary_entry
secondary_entry:
    msr     daifset, #0xf
    adr_l   x1, guest_vectors
    msr     vbar_el1, x1
    isb
    mov     sp, x0
    bl      secondary_main
3:  wfe
    b       3b

// Run-time address of secondary_entry for the PSCI call.
    .global secondary_entry_addr
secondary_entry_addr:
    adr_l   x0, secondary_entry
    ret

// ---- EL1 vector table: 16 slots of 0x80 bytes; every slot saves the frame and calls guest_exception(idx, frame) --
    .macro vec idx
    .balign 0x80
    sub     sp, sp, #0x110
    stp     x0, x1, [sp, #0]
    mov     x0, #\idx
    b       exc_common
    .endm

    .balign 0x800
    .global guest_vectors
guest_vectors:
    vec 0                           // current EL, SP0: sync, irq, fiq, serror
    vec 1
    vec 2
    vec 3
    vec 4                           // current EL, SPx (this guest runs on SP_EL1): sync
    vec 5                           //                                              IRQ
    vec 6                           //                                              FIQ
    vec 7                           //                                              SError
    vec 8                           // lower EL AArch64
    vec 9
    vec 10
    vec 11
    vec 12                          // lower EL AArch32
    vec 13
    vec 14
    vec 15

exc_common:
    stp     x2, x3, [sp, #0x10]
    stp     x4, x5, [sp, #0x20]
    stp     x6, x7, [sp, #0x30]
    stp     x8, x9, [sp, #0x40]
    stp     x10, x11, [sp, #0x50]
    stp     x12, x13, [sp, #0x60]
    stp     x14, x15, [sp, #0x70]
    stp     x16, x17, [sp, #0x80]
    stp     x18, x19, [sp, #0x90]
    stp     x20, x21, [sp, #0xa0]
    stp     x22, x23, [sp, #0xb0]
    stp     x24, x25, [sp, #0xc0]
    stp     x26, x27, [sp, #0xd0]
    stp     x28, x29, [sp, #0xe0]
    str     x30, [sp, #0xf0]
    mov     x1, sp
    bl      guest_exception
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #0x10]
    ldp     x4, x5, [sp, #0x20]
    ldp     x6, x7, [sp, #0x30]
    ldp     x8, x9, [sp, #0x40]
    ldp     x10, x11, [sp, #0x50]
    ldp     x12, x13, [sp, #0x60]
    ldp     x14, x15, [sp, #0x70]
    ldp     x16, x17, [sp, #0x80]
    ldp     x18, x19, [sp, #0x90]
    ldp     x20, x21, [sp, #0xa0]
    ldp     x22, x23, [sp, #0xb0]
    ldp     x24, x25, [sp, #0xc0]
    ldp     x26, x27, [sp, #0xd0]
    ldp     x28, x29, [sp, #0xe0]
    ldr     x30, [sp, #0xf0]
    add     sp, sp, #0x110
    eret
