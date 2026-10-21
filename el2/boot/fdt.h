/* SPDX-License-Identifier: MIT */
/* Watari EL2 M1-4: minimal in-place FDT patcher (see fdt.c). */
#ifndef WEL2_FDT_H
#define WEL2_FDT_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_ram.h"

struct fdt_patch_info {
    bool mem_found, mem_patched;
    uint64_t mem_base_old, mem_size_old, mem_base_new, mem_size_new;
    bool psci_found, psci_patched, psci_already_hvc;
    bool timer_found;
    uint32_t timer_intid[4];       /* secure phys, non-secure phys, virtual, hyp (INTIDs) */
};

typedef bool (*fdt_prop_cb)(void *ctx, unsigned depth, const char *node, const char *prop,
                            uint8_t *val, uint32_t len);

int fdt_check(const void *fdt);                      /* 0 = header and bounds sane */
uint32_t fdt_totalsize(const void *fdt);
uint32_t fdt_used_size(const void *fdt);             /* end of the last block, 8-aligned */
void fdt_set_totalsize(void *fdt, uint32_t n);
int fdt_walk_props(void *fdt, fdt_prop_cb cb, void *ctx);

/* Hand-off patch (INTERFACES 3.2): trims [carve_base, carve_base+carve_size), which must
 * be the head of /memory, off it; /psci/method "smc" -> "hvc", reads /timer/interrupts. All edits
 * keep the blob size. Returns 0, or -1 if the blob is malformed. */
int fdt_patch_for_guest(void *fdt, uint64_t carve_base, uint64_t carve_size,
                        struct fdt_patch_info *info);

/* First /memory "reg" pair as a RAM range (wel2_ram_from_reg: rounded to 2 MB, end clamped to max_end).
 * 0 = found and usable, -1 = no /memory, unusable cells, or the blob is malformed. Reads only, and
 * is safe with the MMU off (stage1.c calls it before the EL2 map exists: this file is built with
 * -mstrict-align). */
int fdt_get_memory(void *fdt, struct wel2_ram *ram, uint64_t min_base, uint64_t max_end);

/* /chosen/bootargs (QEMU -append) copied into out (NUL-terminated, truncated to max-1).
 * Returns the string length, or -1 if there is none or the blob is malformed. */
int fdt_get_bootargs(void *fdt, char *out, unsigned max);

#endif
