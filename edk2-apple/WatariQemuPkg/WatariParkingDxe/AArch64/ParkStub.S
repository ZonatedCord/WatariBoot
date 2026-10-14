// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Stub di parcheggio (AArch64), copiato dal driver in <mailbox>+0x800. Posizione-indipendente.
// Ingresso (PSCI CPU_ON): MMU e cache spente, DAIF mascherati, X0 = indirizzo fisico della mailbox
// di questo core (context_id). Comportamento (spec MS parking v1, sez. 3.2, Fig. 2, scritto da noi):
// abilita la CPU interface GICv3, poi loop WFE: se ProcessorId (off 0x0) == valore atteso (off 0xFF0)
// e JumpAddress (off 0x8) != 0 -> JumpAddress = 0 (ack), DSB, X0 = mailbox, salto.
// Registri di sistema GICv3 con codifica numerica (S3_op1_Cn_Cm_op2) per non dipendere dall'assembler:
//   ICC_SRE_EL1 = S3_0_C12_C12_5, ICC_PMR_EL1 = S3_0_C4_C6_0, ICC_IGRPEN1_EL1 = S3_0_C12_C12_7.
//
    .text
    .balign 4
    .global ParkStubStart
    .global ParkStubEnd

ParkStubStart:
    mov     x19, x0
    ldr     w21, [x19, #0xFEC]          // PMODE: bit3 = non toccare la CPU interface GIC, bit5 = azzera i GPR al salto
    // diagnostica (tentativo A): stato del core all'ingresso, in +0xF00 (area firmware della mailbox)
    mrs     x1, CurrentEL
    str     x1, [x19, #0xF00]
    mrs     x1, SCTLR_EL1
    str     x1, [x19, #0xF08]
    mrs     x1, DAIF
    str     x1, [x19, #0xF10]
    mrs     x1, MPIDR_EL1
    str     x1, [x19, #0xF18]
    mrs     x1, S3_0_C12_C12_5          // ICC_SRE_EL1 (prima della scrittura)
    str     x1, [x19, #0xF20]
    mrs     x1, S3_0_C4_C6_0            // ICC_PMR_EL1
    str     x1, [x19, #0xF28]
    mrs     x1, S3_0_C12_C12_7          // ICC_IGRPEN1_EL1
    str     x1, [x19, #0xF30]
    tbnz    w21, #3, 4f
    mov     x1, #7                      // SRE | DFB | DIB
    msr     S3_0_C12_C12_5, x1
    isb
    mov     x1, #0xFF
    msr     S3_0_C4_C6_0, x1            // PMR = tutte le priorita'
    mov     x1, #1
    msr     S3_0_C12_C12_7, x1          // IGRPEN1 = 1
    isb
4:  ldr     w20, [x19, #0xFF0]          // ProcessorId atteso da questo core
1:  ldr     w1, [x19]                   // ProcessorId
    cmp     w1, w20
    b.ne    2f
    ldr     x2, [x19, #8]               // JumpAddress
    cbnz    x2, 3f
2:  wfe
    b       1b
3:  str     x2, [x19, #0xFF8]           // diagnostica (V3): copia del JumpAddress visto da questo core
    str     xzr, [x19, #8]              // ack: JumpAddress = 0
    dsb     sy
    mov     x0, x19                     // X0 = indirizzo della mailbox
    tbz     w21, #5, 5f
    mov     x16, x2                     // bit5: stato "pulito" come dopo un reset, solo X0 valido
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    mov     x8, xzr
    mov     x9, xzr
    mov     x10, xzr
    mov     x11, xzr
    mov     x12, xzr
    mov     x13, xzr
    mov     x14, xzr
    mov     x15, xzr
    mov     x17, xzr
    mov     x18, xzr
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr
    mov     x30, xzr
    br      x16
5:  br      x2
ParkStubEnd:
