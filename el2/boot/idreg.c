// SPDX-License-Identifier: MIT
// Watari EL2 T6g (PLAN S12): the hardware side of the TID3 ID-register emulation. The table and the value policy are
// vgic/idreg_tbl.c (host-tested); this file reads the real register at EL2 and answers the guest's trapped MRS.
// Written from the Arm ARM (ESR_EL2 ISS for EC 0x18, HCR_EL2.TID3, the system register encodings of the ID group).

#include "boot.h"
#include "wel2_idreg.h"

bool g_tid3;                    // HCR_EL2.TID3 is set (the M1 by default, wel2.tid3=1|0 overrides)
unsigned g_idreg_n;             // reads emulated, system wide (big lock held), printed by cpuif_stats()

// One mrs per allocated encoding, spelled with the generic S<op0>_<op1>_C<n>_C<m>_<op2> name so that no extension
// has to be enabled in the assembler. The reads happen at EL2: ID registers are not redirected by HCR_EL2.E2H.
#define RD(m, o) case (m) * 8 + (o): __asm__ volatile("mrs %0, S3_0_C0_C" #m "_" #o : "=r"(v)); break;
#define RD8(m) RD(m, 0) RD(m, 1) RD(m, 2) RD(m, 3) RD(m, 4) RD(m, 5) RD(m, 6) RD(m, 7)

static uint64_t idreg_hw_read(void *ctx, unsigned crm, unsigned op2)
{
    uint64_t v = 0;

    (void)ctx;
    switch (crm * 8 + op2) {
    RD8(1) RD8(2) RD8(3) RD8(4) RD8(5) RD8(6) RD8(7)
    default: break;
    }
    return v;
}

// A trapped read of the TID3 group (EC 0x18, sync from the lower EL): true = emulated, ELR advanced.
bool idreg_trap(struct frame *f)
{
    struct wel2_idreg_acc a;

    if (!g_tid3 || !wel2_idreg_decode(f->esr & 0x1ffffff, &a))
        return false;
    uint64_t v = wel2_idreg_value(idreg_hw_read, 0, a.crm, a.op2);

    if (a.rt != 31)
        f->x[a.rt] = v;
    g_idreg_n++;
    f->elr += 4;
    return true;
}
