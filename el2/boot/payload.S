// SPDX-License-Identifier: MIT
// Watari EL2 M1-2: EL1 exception vector table of the test payload.
// Only "current EL with SPx, IRQ" (offset 0x280) is expected; every other
// entry reports its index through el1_bad() (never a silent hang, lesson F-14).

    .macro bad idx
    .balign 0x80
    mov     x0, #\idx
    b       el1_bad
    .endm

    .section .text.el1vec, "ax"
    .balign 0x800
    .global el1_vectors
el1_vectors:
    bad 0
    bad 1
    bad 2
    bad 3
    bad 4                       // current EL, SPx, sync
    .balign 0x80
    b       el1_irq_entry       // current EL, SPx, IRQ
    bad 6
    bad 7
    bad 8
    bad 9
    bad 10
    bad 11
    bad 12
    bad 13
    bad 14
    bad 15

    .text
el1_irq_entry:
    sub     sp, sp, #0xa0
    stp     x0, x1, [sp, #0x00]
    stp     x2, x3, [sp, #0x10]
    stp     x4, x5, [sp, #0x20]
    stp     x6, x7, [sp, #0x30]
    stp     x8, x9, [sp, #0x40]
    stp     x10, x11, [sp, #0x50]
    stp     x12, x13, [sp, #0x60]
    stp     x14, x15, [sp, #0x70]
    stp     x16, x17, [sp, #0x80]
    stp     x18, x30, [sp, #0x90]
    bl      el1_irq_handler
    ldp     x0, x1, [sp, #0x00]
    ldp     x2, x3, [sp, #0x10]
    ldp     x4, x5, [sp, #0x20]
    ldp     x6, x7, [sp, #0x30]
    ldp     x8, x9, [sp, #0x40]
    ldp     x10, x11, [sp, #0x50]
    ldp     x12, x13, [sp, #0x60]
    ldp     x14, x15, [sp, #0x70]
    ldp     x16, x17, [sp, #0x80]
    ldp     x18, x30, [sp, #0x90]
    add     sp, sp, #0xa0
    eret
