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
    mov     x1, #7                      // SRE | DFB | DIB
    msr     S3_0_C12_C12_5, x1
    isb
    mov     x1, #0xFF
    msr     S3_0_C4_C6_0, x1            // PMR = tutte le priorita'
    mov     x1, #1
    msr     S3_0_C12_C12_7, x1          // IGRPEN1 = 1
    isb
    ldr     w20, [x19, #0xFF0]          // ProcessorId atteso da questo core
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
    br      x2
ParkStubEnd:
