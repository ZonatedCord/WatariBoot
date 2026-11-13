// SPDX-License-Identifier: MIT
#include "wel2_coreidx.h"

// park_x0: stop this core with a distinctive code in x0, readable over the m1n1 proxy (README, "Position independence").
//   0xBAD00001 load address not 16 KB aligned     0xBAD00002 not the boot core (normal for the other cores)
//   0xBAD00003 not at EL2                         0xBAD00004 bad dynamic relocation (not R_AARCH64_RELATIVE)
//   0xBAD00005 secondary: not at EL2              0xBAD00006 secondary: MPIDR outside the stack slots (x1 = MPIDR | 1<<63)
    .macro park_x0 code
    movz    x0, #\code
    movk    x0, #0xBAD0, lsl #16
    b       park
    .endm

// adr_l: the run-time address of a symbol, PC-relative (adrp + add), usable before and after the relocation loop.
    .macro adr_l reg, sym
    adrp    \reg, \sym
    add     \reg, \reg, :lo12:\sym
    .endm
// Watari EL2 M0-C: entry, arm64 Image header, EL2 exception vectors.
// Written from the Arm Architecture Reference Manual (ARMv8-A) and the Linux
// arm64 boot protocol document (header layout only).

    .section .text.boot, "ax"
    .global _start
_start:
    b       reset                   // code0: branch over the header
    .long   0                       // code1
    .quad   0x80000                 // text_offset: where QEMU loads it (the image itself is position independent)
    .quad   __image_size            // image_size (linker.ld): image, .bss and stacks
    .quad   0xa                     // flags: LE, 4K pages, any placement
    .quad   0, 0, 0                 // reserved
    .ascii  "ARM\x64"               // magic at offset 56
    .long   0                       // reserved (PE header offset)

reset:
    msr     daifset, #0xf           // mask D, A, I, F before anything else (the firmware's state is not ours)
    // Only the boot CPU continues; others park (M0 runs with -smp 1).
    mrs     x1, mpidr_el1
    and     x1, x1, #0xffffff
    cbz     x1, 1f
    park_x0 2
1:  mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #2
    b.eq    1f
    park_x0 3                       // not at EL2: cannot proceed
1:  // x0 (the FDT address the loader passes) stays untouched until el2_main(fdt)

    // T6a: the image is a PIE linked at 0 and runs wherever the loader put it. x9 = the run-time base (the link
    // address of _start is 0). Everything up to the end of the relocation loop is position-independent by
    // construction: only adr/adrp, no literal pool, no GOT (check-pie.sh greps this file for pseudo-loads).
    adr     x9, _start
    tst     x9, #0x3fff             // the stage-1 page split and VBAR need a 16 KB aligned load address
    b.eq    1f
    park_x0 1
1:

    // Self-relocation, MMU and caches off, written from the ELF specification: every dynamic relocation must be
    // R_AARCH64_RELATIVE (type 1027), which means *(base + r_offset) = base + r_addend (Elf64_Rela: r_offset,
    // r_info, r_addend, 8 bytes each). Anything else cannot be resolved here: park with code 4 in x0 (nothing may
    // be printed yet: the console is not known; check-pie.sh makes this unreachable for a built image).
    adr_l   x1, __rela_start
    adr_l   x2, __rela_end
1:  cmp     x1, x2
    b.hs    2f
    ldp     x3, x4, [x1], #16       // r_offset, r_info
    ldr     x5, [x1], #8            // r_addend
    cmp     w4, #1027               // R_AARCH64_RELATIVE (low 32 bits of r_info = type)
    b.ne    reloc_fail
    lsr     x4, x4, #32
    cbnz    x4, reloc_fail          // symbol index must be 0
    add     x3, x3, x9
    add     x5, x5, x9
    str     x5, [x3]
    b       1b
reloc_fail:
    park_x0 4
2:
    dsb     sy

    adr_l   x1, __el2_stacks        // core 0 uses the first 16 KB stack
    add     x1, x1, #WEL2_STACK_SIZE
    mov     sp, x1

    // Zero .bss
    adr_l   x1, __bss_start
    adr_l   x2, __bss_end
1:  cmp     x1, x2
    b.hs    2f
    str     xzr, [x1], #8
    b       1b
2:
    // Cache maintenance for what the MMU-off stores above (the relocated .data / .data.rel.ro, .bss, and the stack
    // use that follows) wrote: with the MMU off the accesses go to memory, but stale lines of the same addresses
    // (left by the firmware / a previous run) may sit in the data cache and would be hit once the stage-1 map makes
    // the memory cacheable. DC IVAC by VA over the WHOLE layer block (image, .bss, stacks: the relocated data is
    // written MMU-off too), Arm ARM D7.5 cache maintenance; ordering by DSB, D7.5.9; line size from
    // CTR_EL0.DminLine. Nothing dirty is lost: the loader leaves the image in memory (m1n1 cleans what it copies),
    // and everything written here went straight to memory.
    // [hypothesis for the M1: QEMU TCG has no stale lines, so it cannot show the need]
    mrs     x2, ctr_el0
    ubfx    x2, x2, #16, #4
    mov     x3, #4
    lsl     x3, x3, x2              // data cache line size in bytes
    mov     x1, x9                  // _start
    adr_l   x4, __el1_stack_top     // end of the block (the last stack slot)
    sub     x5, x3, #1
    bic     x1, x1, x5
3:  dc      ivac, x1
    add     x1, x1, x3
    cmp     x1, x4
    b.lo    3b
    dsb     sy
    adr_l   x1, el2_vectors
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
// (M1-3). x0 = context given to the firmware (unused). MMU off, caches off. The image was relocated by the boot core
// (a CPU_ON comes from a guest the boot core started), so this code only uses PC-relative addressing.
// Stack: 16 KB slot selected like wel2_core_index() (include/wel2_coreidx.h; keep the formula identical):
//   idx = Aff1*4 + Aff0, valid iff Aff0 < 4, Aff1 < 2, Aff2 == Aff1, Aff3 == 0.
// A core outside that range must not touch a stack: it records its MPIDR (bit 63 set) in g_bad_mpidr
// (best effort: written with MMU/caches off, so the reader must invalidate its line first; the boot core
// reports it at the next CPU_ON) and parks.
    // core_sp: sp = top of this core's EL2 stack slot (x1..x5 clobbered); bad_core for an MPIDR outside the slots.
    .macro core_sp
    mrs     x1, mpidr_el1
    ubfx    x2, x1, #0, #8          // Aff0
    ubfx    x3, x1, #8, #8          // Aff1
    ubfx    x4, x1, #16, #8         // Aff2
    ubfx    x5, x1, #32, #8         // Aff3
    cmp     x2, #4
    b.hs    bad_core
    cmp     x3, #2
    b.hs    bad_core
    cmp     x4, x3
    b.ne    bad_core
    cbnz    x5, bad_core
    add     x1, x2, x3, lsl #2      // idx = Aff0 + 4*Aff1 (0..7)
    adr_l   x2, __el2_stacks
    add     x1, x1, #1
    lsl     x1, x1, #WEL2_STACK_SHIFT   // (idx + 1) * WEL2_STACK_SIZE
    add     sp, x2, x1
    .endm

    .text
    .global secondary_entry
secondary_entry:
    msr     daifset, #0xf
    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #2
    b.eq    1f
    park_x0 5                       // started below EL2: cannot continue
1:  core_sp
    // Insurance, per core [hypothesis: QEMU TCG has no stale lines, so it cannot show the need]: this core's caches may
    // hold stale lines of its own stack slot and of the stage-1 globals it reads (g_s1) from a previous run; drop
    // them (DC IVAC by VA, Arm ARM D7.5) before the stage-1 map makes this memory cacheable. The boot core cleaned
    // g_s1 to memory (stage1.c) and nothing else is dirty here.
    mrs     x3, ctr_el0
    ubfx    x3, x3, #16, #4
    mov     x4, #4
    lsl     x4, x4, x3              // line size
    sub     x5, x4, #1
    sub     x6, sp, #WEL2_STACK_SIZE
    mov     x7, sp
    bic     x6, x6, x5
3:  dc      ivac, x6
    add     x6, x6, x4
    cmp     x6, x7
    b.lo    3b
    adr_l   x6, g_s1
    add     x7, x6, #32
    bic     x6, x6, x5
4:  dc      ivac, x6
    add     x6, x6, x4
    cmp     x6, x7
    b.lo    4b
    dsb     sy
    adr_l   x1, el2_vectors
    msr     vbar_el2, x1
    isb
    bl      s1_enable               // same tables as the boot core (#53), then C code
    bl      el2_secondary_main      // never returns
    b       park

// secondary_rewake (T6f): a core that CPU_OFF parked inside the layer (M1: no firmware to hand it back to) has been
// woken by CPU_ON. It runs on a stack deep inside an exception handler: abandon it, start again from the top of the same
// slot, and enter el2_secondary_main like a first start. MMU, caches and VBAR are on already (no s1_enable); DAIF stays
// masked (el2_secondary_main takes the big lock and ends in enter_el1).
    .text
    .global secondary_rewake
secondary_rewake:
    msr     daifset, #0xf
    core_sp
    bl      el2_secondary_main      // never returns
    b       park

bad_core:
    // No stack here. MMU off: the store goes straight to memory.
    orr     x1, x1, #(1 << 63)
    adr_l   x2, g_bad_mpidr
    str     x1, [x2]
    dsb     sy
    park_x0 6

    // Own cache-line-sized slot (128 B covers the M1's line [hypothesis: 128 B] and 64 B parts) so that
    // invalidating it never discards neighbouring data.
    .bss
    .balign 128
    .global g_bad_mpidr
g_bad_mpidr:
    .quad   0
    .skip   120
    .text

// s1_enable: HCR_EL2.E2H, then MAIR/TCR/TTBR0_EL2 from g_s1 (stage1.c), invalidate TLB + I-cache, then
// SCTLR_EL2.M/C/I = 1 (A, WXN, EE cleared). Registers only (x0-x5), returns through x30.
// E2H comes first and per core: HCR_EL2 is not shared, and TCR_EL2/SCTLR_EL2 change layout with it
// (Arm ARM D8.13; written with the MMU off, as a change of the translation regime needs).
    .text
    .global s1_enable
s1_enable:
    adr_l   x0, g_s1
    ldp     x1, x2, [x0]            // mair, tcr
    ldr     x3, [x0, #16]           // ttbr
    ldr     x5, [x0, #24]           // e2h (the value that read back on the boot core)
    mrs     x4, hcr_el2
    bfi     x4, x5, #34, #1         // HCR_EL2.E2H
    msr     hcr_el2, x4
    isb
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
    // SCTLR_EL1: MMU/caches off, RES1 bits set (PSCI 6.4.3.4). With HCR_EL2.E2H = 1 (x3 bit 34) an EL2 access to
    // SCTLR_EL1 is redirected to SCTLR_EL2 (Arm ARM D8.13.2): the guest's register is SCTLR_EL12 there
    // (op0 3, op1 5, CRn 1, CRm 0, op2 0; the assembler's default architecture does not know the name).
    movz    x5, #0x0800
    movk    x5, #0x30d0, lsl #16
    tbnz    x3, #34, 1f
    msr     sctlr_el1, x5
    b       2f
1:  msr     S3_5_C1_C0_0, x5        // SCTLR_EL12
2:  isb
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
