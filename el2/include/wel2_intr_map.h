/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - interrupt map: physical sources (AIC hwirq) <-> guest vINTID/GSIV.
 *
 * Pure arithmetic and tables, no hardware access, freestanding-compatible. Shared by
 * the layer (pirq), the host tests and tools/acpigen (GSIV generation) so that the
 * number the layer injects and the number ACPI tells Windows cannot drift apart
 * (docs/design/watari-el2-devirq/ARCHITECTURE.md sections 4.1 and 7.1, FR-D1, FR-D10).
 *
 * Rules (ARCHITECTURE 4.1):
 *  - a wired source with AIC hwirq h appears as vINTID/GSIV 32 + h (SPI numbering), for
 *    h < wired_max (896 on the t8103: the GICv3 model has ITLinesNumber = 29, INTIDs up
 *    to 959, and 928-959 are kept for MSIs);
 *  - the MSI pool (t8103: hwirq 704..735, edge, 32 shared vectors) is NOT a wired range:
 *    32 + 704 would be 736..767, so those vINTIDs are a hole that nobody owns, and the
 *    pool is exposed as vINTID msi_vintid_base + k (928 + k). This resolves the collision
 *    finding C4 without renumbering any wired source (hwirq 736 stays at 768);
 *  - default deny: a wired hwirq is visible to the guest only if it has a row in the
 *    allow-list. On a machine the table is generated from the FDT/ADT nodes that acpigen
 *    exposes to Windows (FR-D10); the t8103 table below is a static example.
 */
#ifndef WEL2_INTR_MAP_H
#define WEL2_INTR_MAP_H

#include <stdbool.h>
#include <stdint.h>

#define INTR_INVALID      0xFFFFFFFFu
#define INTR_VINTID_SPI0  32u          /* first SPI INTID */

#define INTR_TRIG_LEVEL   0
#define INTR_TRIG_EDGE    1

/* One allow-listed wired source. */
struct intr_entry {
	uint32_t hwirq;                /* AIC hwirq (physical source number) */
	uint8_t  edge;                 /* INTR_TRIG_LEVEL / INTR_TRIG_EDGE */
	const char *name;              /* for logs */
};

struct intr_map {
	uint32_t wired_max;            /* exclusive upper bound of exposed wired hwirq (896 on t8103) */
	uint32_t msi_hwirq_base;       /* first hwirq of the MSI pool, or INTR_INVALID: no pool */
	uint32_t msi_count;            /* size of the pool (32) */
	uint32_t msi_vintid_base;      /* vINTID of pool vector 0 (928) */
	bool     msi_exposed;          /* level 2 (msi-apple-pcie) has enabled the pool */
	const struct intr_entry *allow;/* default-deny allow-list of wired sources */
	uint32_t n_allow;
};

/* Validation errors of intr_map_validate(). */
#define INTR_OK            0
#define INTR_E_RANGE      (-1)   /* allow-listed hwirq >= wired_max (unsupported in v1) */
#define INTR_E_POOL       (-2)   /* allow-listed hwirq inside the MSI pool */
#define INTR_E_DUP        (-3)   /* hwirq listed twice */
#define INTR_E_LAYOUT     (-4)   /* pool or vINTID layout does not fit the vGIC model */

/* The static example table of the 13" M1 MacBook Pro (t8103, j293). */
extern const struct intr_map INTR_MAP_T8103;

/* hwirq -> vINTID for a wired source (pure arithmetic, no allow-list);
 * INTR_INVALID if reserved (MSI pool) or out of range. */
uint32_t intr_wired_vintid(const struct intr_map *m, uint32_t hwirq);
/* hwirq in the MSI pool -> vINTID; INTR_INVALID otherwise. */
uint32_t intr_msi_vintid(const struct intr_map *m, uint32_t hwirq);
/* Inverse over everything that has an arithmetic mapping (wired or MSI); *is_msi (optional)
 * tells which pool the vINTID belongs to. INTR_INVALID for the hole, SGI/PPI, >= 960. */
uint32_t intr_hwirq_of_vintid(const struct intr_map *m, uint32_t vintid, bool *is_msi);
/* GSIV for ACPI: equals the vINTID (GIC GSIV = INTID). INTR_INVALID if unmapped. */
uint32_t intr_gsiv(const struct intr_map *m, uint32_t hwirq);

/* Allow-list row of a wired hwirq, or NULL. */
const struct intr_entry *intr_find(const struct intr_map *m, uint32_t hwirq);
/* vINTID the guest may see for a physical hwirq: wired and allow-listed, or an MSI pool
 * vector while msi_exposed. INTR_INVALID = default deny. */
uint32_t intr_exposed_vintid(const struct intr_map *m, uint32_t hwirq);
/* Inverse restricted to exposed sources: hwirq of a vINTID the guest may use, else INTR_INVALID. */
uint32_t intr_exposed_hwirq(const struct intr_map *m, uint32_t vintid);
/* Trigger of an exposed source: INTR_TRIG_LEVEL/EDGE, or -1 if the hwirq is not exposed. */
int intr_trigger(const struct intr_map *m, uint32_t hwirq);
/* Check a (generated) table; returns INTR_OK or one of INTR_E_*. */
int intr_map_validate(const struct intr_map *m);

#endif
