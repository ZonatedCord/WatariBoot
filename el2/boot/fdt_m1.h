/* SPDX-License-Identifier: MIT */
/* Watari EL2 T6b (PLAN S7): read-only FDT queries for the Apple M1 boot (see fdt_m1.c).
 * Plain C11, byte reads only: builds for the layer (-mstrict-align, MMU off safe) and for the host
 * (tests, tools/m3a/fdt_m1_dump.c). Nothing here keeps or reports a property that is not listed below. */
#ifndef WEL2_FDT_M1_H
#define WEL2_FDT_M1_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_fb.h"

#define M1_MAX_CPUS    8
#define M1_MAX_MEM     16       /* /memory reg pairs kept */
#define M1_MAX_RESV    64       /* /memreserve/ entries + /reserved-memory children kept */
#define M1_MAX_USABLE  64       /* pieces left of /memory after removing the reservations */
#define M1_MAX_AIC     4        /* reg pairs of the AIC node */
#define M1_PATH_MAX    80

struct m1_range {
	uint64_t base, size;
};

struct m1_cpu {
	uint64_t reg;               /* "reg" of /cpus/cpu@*: the MPIDR affinity fields (m1n1 wants reg == MPIDR & 0xFFFFFF) */
	uint64_t release_addr;      /* "cpu-release-addr" (8 bytes) */
	bool has_release;           /* present and non-zero (m1n1 leaves the placeholder <0 0> in the boot CPU's node) */
	bool spin_table;            /* enable-method = "spin-table" */
};

struct m1_dev {
	bool found;
	uint64_t base, size;
	bool from_stdout;           /* UART only: chosen through /chosen/stdout-path or the serial0 alias */
};

struct m1_platform {
	bool is_m1;                 /* an enabled node compatible with "apple,aic" and a reg: the platform switch */

	unsigned ncpu;
	struct m1_cpu cpu[M1_MAX_CPUS];     /* node order, disabled nodes skipped */
	bool cpus_truncated;                /* more than M1_MAX_CPUS enabled cpu nodes */

	unsigned nmem;
	struct m1_range mem[M1_MAX_MEM];    /* every /memory reg pair, as the blob has it */
	unsigned nresv;
	struct m1_range resv[M1_MAX_RESV];  /* /reserved-memory children (reg) and /memreserve/ entries (no order promised) */
	unsigned nusable;
	struct m1_range usable[M1_MAX_USABLE];  /* mem[] minus resv[], exact bytes */
	unsigned mem_wholly_reserved;       /* mem[] pairs with nothing left after the subtraction (design S-4) */
	bool lists_truncated;               /* mem[] or usable[] overflowed: what is listed is safe (never extra RAM), not complete */
	unsigned bad_ranges;                /* zero-sized or wrapping ranges that were ignored */

	unsigned naic;
	struct m1_range aic[M1_MAX_AIC];    /* reg pairs of the first enabled apple,aic node (the ADT has two) */
	struct m1_dev uart;                 /* apple,s5l-uart: stdout-path / serial0 first, else the first enabled one */
	struct m1_dev wdt;                  /* apple,wdt */
	struct wel2_fb fb;                  /* /chosen/framebuffer@*, fmt NONE when absent */
};

/* Fills *p. 0 = blob walked (p->is_m1 etc. tell what was there), -1 = malformed or truncated blob (*p zeroed), or
 * a reservation that cannot be recorded exactly (resv[] full at M1_MAX_RESV, a wrapping range, a reserved-memory
 * reg that is undecodable or has more pairs than fit): reserved memory is never allowed to show up as usable.
 * (Zero-sized reservations reserve nothing and are only counted in bad_ranges.)
 *
 * For consumers (T6c): usable[] holds exact byte ranges; round every piece INWARD to the granule you map with
 * (2 MB for the EL2 stage 1, 32 MB for the first guest stage 2) and drop what vanishes. Stack: fdt_m1_parse keeps
 * its working state (about 3 KB: candidates, aliases, a 65-entry scratch array of ranges, stdout path) on the
 * caller's stack, plus struct m1_platform itself (about 3.5 KB) wherever the caller puts it; the EL2 stacks are
 * 16 KB.
 * Every access is bounded by the header's totalsize. */
int fdt_m1_parse(const void *fdt, struct m1_platform *p);

/* Memory reservation block iterator (Devicetree Specification 5.3): entry `idx` into base and size.
 * 1 = entry returned, 0 = terminator reached (no such entry), -1 = malformed (bounds, no terminator). */
int fdt_reserve_entry(const void *fdt, unsigned idx, uint64_t *base, uint64_t *size);

/* Pure helper (tested on its own): out[] = mem[0..nmem) minus resv[0..nresv). Pieces that do not fit are dropped
 * (*truncated), never invented. */
void m1_subtract(const struct m1_range *mem, unsigned nmem, const struct m1_range *resv, unsigned nresv,
		 struct m1_range *out, unsigned out_max, unsigned *nout, unsigned *wholly, bool *truncated);

#endif
