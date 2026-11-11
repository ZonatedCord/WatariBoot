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
    bool mem_trimmed;              /* size cut so that /memory ends at ram_end */
    bool timer_found;
    uint32_t timer_intid[4];       /* secure phys, non-secure phys, virtual, hyp (INTIDs) */
};

#define FDT_AIC_MAX_ROUTED 16
/* The Watari AIC node of the QEMU "aic=on" machine (tools/qemu-watari). */
struct fdt_aic_info {
    bool found;                    /* node with compatible "watari,aic-v1", a reg and a num-irq */
    bool has_reg;
    uint64_t base, size;           /* MMIO window */
    uint32_t nr_irq, nr_cpu;
    uint32_t routed[FDT_AIC_MAX_ROUTED];   /* AIC inputs of the devices wired to it */
    unsigned n_routed;
};

typedef bool (*fdt_prop_cb)(void *ctx, unsigned depth, const char *node, const char *prop,
                            uint8_t *val, uint32_t len);

int fdt_check(const void *fdt);                      /* 0 = header and bounds sane */
uint32_t fdt_totalsize(const void *fdt);
uint32_t fdt_used_size(const void *fdt);             /* end of the last block, 8-aligned */
void fdt_set_totalsize(void *fdt, uint32_t n);
int fdt_walk_props(void *fdt, fdt_prop_cb cb, void *ctx);

/* Hand-off patch (INTERFACES 3.2): (ram_end != 0: also cuts /memory at ram_end, the end the layer maps) trims [carve_base, carve_base+carve_size), which must
 * be the head of /memory, off it; /psci/method "smc" -> "hvc", reads /timer/interrupts. All edits
 * keep the blob size. Returns 0, or -1 if the blob is malformed. */
int fdt_patch_for_guest(void *fdt, uint64_t carve_base, uint64_t carve_size, uint64_t ram_end,
                        struct fdt_patch_info *info);

/* First /memory "reg" pair as a RAM range (wel2_ram_from_reg: rounded to 2 MB, end clamped to max_end).
 * 0 = found and usable, -1 = no /memory, unusable cells, or the blob is malformed. Reads only, and
 * is safe with the MMU off (stage1.c calls it before the EL2 map exists: this file is built with
 * -mstrict-align). */
int fdt_get_memory(void *fdt, struct wel2_ram *ram, uint64_t min_base, uint64_t max_end);

/* Looks for the Watari AIC node. 0 = blob walked (info->found tells whether the node is there),
 * -1 = malformed blob. Reads only. */
int fdt_find_aic(void *fdt, struct fdt_aic_info *info);

/* First node (any depth >= 1) whose "compatible" string list contains `compat`, with its first "reg" pair
 * (decoded with the parent's #address-cells/#size-cells; no address translation). 0 = blob walked
 * (info->found tells whether a node with both was there), -1 = malformed blob. Reads only; safe with the MMU
 * off (this file is built with -mstrict-align). Console UART: compat "apple,s5l-uart". */
struct fdt_reg_info {
    bool found;
    uint64_t base, size;
};
#define FDT_COMPAT_S5L_UART "apple,s5l-uart"
int fdt_find_compatible_reg(void *fdt, const char *compat, struct fdt_reg_info *info);

/* /chosen/bootargs (QEMU -append) copied into out (NUL-terminated, truncated to max-1).
 * Returns the string length, or -1 if there is none or the blob is malformed. */
int fdt_get_bootargs(void *fdt, char *out, unsigned max);

#endif
