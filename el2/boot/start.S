// SPDX-License-Identifier: MIT
// Watari EL2 M0-C: entry, arm64 Image header, EL2 exception vectors.
// Written from the Arm Architecture Reference Manual (ARMv8-A) and the Linux
// arm64 boot protocol document (header layout only).

    .section .text.boot, "ax"
    .global _start
_start:
    b       reset                   // code0: branch over the header
    .long   0                       // code1
    .quad   0x80000                 // text_offset (matches linker.ld)
    .quad   __image_size          // image_size (linker.ld)
    .quad   0xa                     // flags: LE, 4K pages, any placement
    .quad   0, 0, 0                 // reserved
    .ascii  "ARM\x64"               // magic at offset 56
    .long   0                       // reserved (PE header offset)

reset:
    // Only the boot CPU continues; others park (M0 runs with -smp 1).
    mrs     x1, mpidr_el1
    and     x1, x1, #0xffffff
    cbnz    x1, park

    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #2
    b.ne    park                    // not at EL2: cannot proceed
    // x0 (the FDT address QEMU passes) stays untouched until el2_main(fdt)

    ldr     x1, =__el2_stacks       // core 0 uses the first 16 KB stack
    add     x1, x1, #0x4000
    mov     sp, x1

    // Zero .bss
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
1:  cmp     x1, x2
    b.hs    2f
    str     xzr, [x1], #8
    b       1b
2:
    ldr     x1, =el2_vectors
    msr     vbar_el2, x1
    isb
    // #53: EL2 stage-1 identity map. The tables are built by C code that is still MMU-off
    // (stage1.c, -mstrict-align); s1_enable then switches MMU + caches on, without a stack
    // access in between. x19 (callee-saved) keeps the FDT pointer across the calls.
    mov     x19, x0
    bl      stage1_build            // x0 = FDT: the map covers the RAM it describes
    bl      s1_enable
    mov     x0, x19
    bl      el2_main                // never returns
park:
    wfe
    b       park

// Entry of cores 1..n at EL2, started by the firmware on guest PSCI CPU_ON
// (M1-3). x0 = context given to the firmware (unused). MMU off, caches off.
// Stack: 16 KB slot selected by MPIDR_EL1.Aff0 (QEMU virt numbering; real
// hardware needs a proper cpu index, see README).
    .text
    .global secondary_entry
secondary_entry:
    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #2
    b.ne    park                    // started below EL2: cannot continue
    mrs     x1, mpidr_el1
    and     x1, x1, #0xff
    cmp     x1, #8
    b.hs    park
    ldr     x2, =__el2_stacks
    add     x1, x1, #1
    lsl     x1, x1, #14             // (Aff0 + 1) * 0x4000
    add     sp, x2, x1
    ldr     x1, =el2_vectors
    msr     vbar_el2, x1
    isb
    bl      s1_enable               // same tables as the boot core (#53), then C code
    bl      el2_secondary_main      // never returns
    b       park

// s1_enable: MAIR/TCR/TTBR0_EL2 from g_s1 (stage1.c), invalidate TLB + I-cache, then
// SCTLR_EL2.M/C/I = 1 (A, WXN, EE cleared). Registers only (x0-x4), returns through x30.
    .text
    .global s1_enable
s1_enable:
    ldr     x0, =g_s1
    ldp     x1, x2, [x0]            // mair, tcr
    ldr     x3, [x0, #16]           // ttbr
    msr     mair_el2, x1
    msr     tcr_el2, x2
    msr     ttbr0_el2, x3
    isb
    dsb     sy                      // page tables written with the MMU off are visible
    tlbi    alle2
    ic      iallu
    dsb     nsh
    isb
    mrs     x4, sctlr_el2
    orr     x4, x4, #(1 << 0)       // M
    orr     x4, x4, #(1 << 2)       // C
    orr     x4, x4, #(1 << 12)      // I
    bic     x4, x4, #(1 << 1)       // A: unaligned Normal accesses are allowed
    bic     x4, x4, #(1 << 19)      // WXN
    bic     x4, x4, #(1 << 25)      // EE
    msr     sctlr_el2, x4
    isb
    ret

// void enter_el1(entry x0, el1_sp x1, spsr x2, hcr x3, x0_value x4)
    .text
    .global enter_el1
enter_el1:
    msr     hcr_el2, x3
    msr     elr_el2, x0
    msr     sp_el1, x1
    msr     spsr_el2, x2
    // SCTLR_EL1: MMU/caches off, RES1 bits set (PSCI 6.4.3.4)
    ldr     x5, =0x30d00800
    msr     sctlr_el1, x5
    isb
    mov     x0, x4                  // PSCI 6.4.3.7: context_id in X0; boot: FDT
    mov     x1, xzr                 // Linux boot protocol: x1..x3 = 0
    mov     x2, xzr
    mov     x3, xzr
    eret

// ---------------------------------------------------------------------------
// Exception vectors: 16 entries of 0x80 bytes, table aligned to 0x800.
// Each entry reserves a frame and calls el2_exception(frame, index).
// Frame (0x120 bytes): x0..x30 at 0x00..0xf0, ESR 0xf8, ELR 0x100,
// SPSR 0x108, FAR 0x110.
// ---------------------------------------------------------------------------
    .macro vec idx
    .balign 0x80
    sub     sp, sp, #0x120
    stp     x29, x30, [sp, #0xe8]
    mov     x29, #\idx
    b       exc_common
    .endm

    .section .text.vectors, "ax"
    .balign 0x800
    .global el2_vectors
el2_vectors:
    vec 0
    vec 1
    vec 2
    vec 3
    vec 4
    vec 5
    vec 6
    vec 7
    vec 8       // lower EL, AArch64, sync: the HVC path
    vec 9
    vec 10
    vec 11
    vec 12
    vec 13
    vec 14
    vec 15

    .text
exc_common:
    // x29 = vector index; x29/x30 already saved
    stp     x0, x1, [sp, #0x00]
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
    str     x28, [sp, #0xe0]
    mrs     x1, esr_el2
    mrs     x2, elr_el2
    stp     x1, x2, [sp, #0xf8]
    mrs     x1, spsr_el2
    mrs     x2, far_el2
    stp     x1, x2, [sp, #0x108]
    mov     x1, x29
    mov     x0, sp
    bl      el2_exception
    // Handled exception: restore (the handler may have edited x0-x3 and ELR)
    ldp     x1, x2, [sp, #0xf8]
    msr     elr_el2, x2
    ldr     x1, [sp, #0x108]
    msr     spsr_el2, x1
    ldp     x0, x1, [sp, #0x00]
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
    ldr     x28, [sp, #0xe0]
    ldp     x29, x30, [sp, #0xe8]
    add     sp, sp, #0x120
    eret
