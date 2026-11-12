// SPDX-License-Identifier: MIT
// Watari EL2 T6c: the platform switch (plat.h). Plain C: runs with the MMU off (stage1_build) and builds for the host.

#include "plat.h"

const struct wel2_platform *plat_select(int parse_rc, const struct m1_platform *p)
{
    return parse_rc == 0 && p->is_m1 ? &plat_m1 : &plat_qemu;
}
