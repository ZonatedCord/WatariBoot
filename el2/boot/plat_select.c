// SPDX-License-Identifier: MIT
// Watari EL2 T6c: the platform switch (plat.h). Plain C: runs with the MMU off (stage1_build) and builds for the host.

#include "plat.h"

const struct wel2_platform *plat_select(int parse_rc, const struct m1_platform *p)
{
    return parse_rc == 0 && p->is_m1 ? &plat_m1 : &plat_qemu;
}

bool plat_vgic_windows(const struct wel2_platform *p, bool m1_ipa, uint64_t *gicd, uint64_t *gicr)
{
    bool move = m1_ipa && p->has_phys_gic && (p->vgic_gicd_base != PLAT_M1_GICD_BASE || p->vgic_gicr_base != PLAT_M1_GICR_BASE);

    *gicd = move ? PLAT_M1_GICD_BASE : p->vgic_gicd_base;
    *gicr = move ? PLAT_M1_GICR_BASE : p->vgic_gicr_base;
    return move;
}
