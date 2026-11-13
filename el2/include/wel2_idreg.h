/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 T6g (PLAN S12, DESIGN 5.4 "The guest's ID register"): emulation of the ID register group that
 * HCR_EL2.TID3 traps. Pure logic, no hardware: the real register is read through a callback, so the host test
 * (tests/test_idreg.c) checks the decode table and the returned values.
 *
 * The group. HCR_EL2.TID3 traps EL1 reads of the "ID group 3" registers: op0 = 3, op1 = 0, CRn = 0, CRm = 1..7, any
 * op2 (ID_PFR*, ID_DFR*, ID_AFR0, ID_MMFR*, ID_ISAR*, MVFR*, ID_AA64*) [hypothesis: Arm ARM, HCR_EL2.TID3 field,
 * from memory; MIDR/MPIDR/REVIDR (CRm 0) are TID1 and CTR/DCZID (op1 3) TID2, so they are not in the group]. The trap
 * is an EC 0x18 exception whose ISS carries the encoding (Arm ARM D24.2 ESR_EL2, ISS for MSR/MRS/system instruction).
 *
 * The policy (DESIGN 5.4): an encoding the architecture allocates a register to returns the real value, read at EL2,
 * except ID_AA64PFR0_EL1.GIC [27:24], which is forced to 1 (the guest sees the GICv3 system registers the layer
 * emulates; on the M1 the field is 0). Every other bit is unchanged, the pointer-authentication fields in
 * ID_AA64ISAR1/2_EL1 among them (DESIGN 5.1: the guest may use PAuth). An encoding in the group that is not in the
 * table below reads as zero and the callback is never invoked for it (the architecture makes unallocated encodings of
 * the group RAZ [hypothesis]; reading none of them at EL2 also means the layer never executes an `mrs` the hardware
 * might not accept).
 */
#ifndef WEL2_IDREG_H
#define WEL2_IDREG_H

#include <stdbool.h>
#include <stdint.h>

#define WEL2_IDREG_CRM_FIRST 1u
#define WEL2_IDREG_CRM_LAST  7u

/* ID_AA64PFR0_EL1 = op0 3, op1 0, CRn 0, CRm 4, op2 0; GIC field [27:24]. */
#define WEL2_IDREG_PFR0_CRM  4u
#define WEL2_IDREG_PFR0_OP2  0u
#define WEL2_IDREG_GIC_SHIFT 24
#define WEL2_IDREG_GIC_MASK  (0xfULL << WEL2_IDREG_GIC_SHIFT)

/* The ISS of an EC 0x18 trap, decoded. Returns false when the instruction is not a read of a register of the TID3
 * group (a write, another op0/op1/CRn, CRm outside 1..7); then nothing is filled in. rt is the guest register that
 * receives the value (31 = XZR: discard). */
struct wel2_idreg_acc {
	uint8_t crm, op2, rt;
};
bool wel2_idreg_decode(uint32_t iss, struct wel2_idreg_acc *out);

/* True when the architecture allocates a register at (crm, op2) of the group and the layer reads it. */
bool wel2_idreg_known(unsigned crm, unsigned op2);

/* Number of known registers, and the n-th one (0 <= n < count, in (crm, op2) order); used by the guest-side dump and
 * the tests. */
unsigned wel2_idreg_count(void);
bool wel2_idreg_nth(unsigned n, unsigned *crm, unsigned *op2);

/* The value the guest reads. rd(ctx, crm, op2) returns the real register and is called only for known encodings; it is not
 * called for the AArch32 ID group (CRm 1..3) when ID_AA64PFR0_EL1 shows AArch64-only EL0 and EL1, nor for ID_AA64ZFR0_EL1
 * when PFR0.SVE = 0 or ID_AA64SMFR0_EL1 when PFR1.SME = 0 (those read 0; a possible UNDEF at EL2 must not happen). The
 * decision itself reads ID_AA64PFR0_EL1 / PFR1_EL1 through rd. */
typedef uint64_t (*wel2_idreg_read_fn)(void *ctx, unsigned crm, unsigned op2);
uint64_t wel2_idreg_value(wel2_idreg_read_fn rd, void *ctx, unsigned crm, unsigned op2);

#endif
