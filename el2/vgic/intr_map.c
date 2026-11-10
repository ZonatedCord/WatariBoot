/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - interrupt map implementation. See include/wel2_intr_map.h.
 *
 * t8103 numbers: ARCHITECTURE.md sections 1 and 6 [verified: mainline Linux DTS as read by the
 * design session]; the Asahi downstream tree and the ADT of the real machine may differ, which
 * is why the final tables are generated from the FDT/ADT, not typed.
 */
#include "wel2_intr_map.h"

static const struct intr_entry t8103_allow[] = {
	/* ANS NVMe: one level interrupt. The four mailbox interrupts (583-586) stay denied: the
	 * driver polls the RTKit mailbox first (#48) [hypothesis]. */
	{ 590, INTR_TRIG_LEVEL, "ans-nvme" },
	/* DWC3 xHCI controllers (their DART interrupts 781/861 stay denied). */
	{ 777, INTR_TRIG_LEVEL, "dwc3_0" },
	{ 857, INTR_TRIG_LEVEL, "dwc3_1" },
	/* [hypothesis] 695/698/701 must be confirmed (E-HW0) before the AIC backend uses this table.
	 * PCIe root-port interrupts (INTx fallback, M4); exposing them is [hypothesis]: they
	 * are only listed so the arithmetic of ARCHITECTURE 6 is covered by one table. */
	{ 695, INTR_TRIG_LEVEL, "pcie-port0" },
	{ 698, INTR_TRIG_LEVEL, "pcie-port1" },
	{ 701, INTR_TRIG_LEVEL, "pcie-port2" },
};

/* Sources measured on the real j293 and kept DENIED by default. Every number is
 * [verified: re/m3a-ehw0-measurements.md, ADT read on 2026-11-10, m1n1 v1.6.1]. The trigger type
 * of none of them was measured (the ADT interrupt cells give numbers only), so each is
 * INTR_TRIG_UNKNOWN; that is also why they are not allow-listed: exposing needs the trigger. */
static const struct intr_entry t8103_denied[] = {
	/* ANS mailbox lines (row 10; the NVMe line 590 is allowed above) */
	{ 583, INTR_TRIG_UNKNOWN, "ans-mbox0" },
	{ 584, INTR_TRIG_UNKNOWN, "ans-mbox1" },
	{ 585, INTR_TRIG_UNKNOWN, "ans-mbox2" },
	{ 586, INTR_TRIG_UNKNOWN, "ans-mbox3" },
	/* usb-drd0: 777 (allowed above), then the four extra lines (row 11) */
	{ 778, INTR_TRIG_UNKNOWN, "usb-drd0-1" },
	{ 779, INTR_TRIG_UNKNOWN, "usb-drd0-2" },
	{ 780, INTR_TRIG_UNKNOWN, "usb-drd0-3" },
	{ 736, INTR_TRIG_UNKNOWN, "usb-drd0-4" },     /* first wired hwirq after the MSI pool: vINTID 768 */
	/* usb-drd1: 857 (allowed above) */
	{ 858, INTR_TRIG_UNKNOWN, "usb-drd1-1" },
	{ 859, INTR_TRIG_UNKNOWN, "usb-drd1-2" },
	{ 860, INTR_TRIG_UNKNOWN, "usb-drd1-3" },
	{ 816, INTR_TRIG_UNKNOWN, "usb-drd1-4" },
	/* Thunderbolt PCIe controllers apciec0 / apciec1 (row 12) and their pio-DMA lines */
	{ 770, INTR_TRIG_UNKNOWN, "apciec0" },
	{ 772, INTR_TRIG_UNKNOWN, "apciec0-piodma" },
	{ 850, INTR_TRIG_UNKNOWN, "apciec1" },
	{ 852, INTR_TRIG_UNKNOWN, "apciec1-piodma" },
	/* UARTs (row 13; uart0 is the Linux t8103 console UART) */
	{ 605, INTR_TRIG_UNKNOWN, "uart0" },
	{ 607, INTR_TRIG_UNKNOWN, "uart2" },
};

const struct intr_map INTR_MAP_T8103 = {
	.wired_max = 896,
	.msi_hwirq_base = 704,         /* msi-ranges = <&aic AIC_IRQ 704 EDGE_RISING 32> */
	.msi_count = 32,
	.msi_vintid_base = 928,        /* reserved by watari-el2 INTERFACES 4 */
	.msi_exposed = false,          /* level 1: the pool stays masked until msi-apple-pcie (M4) */
	.allow = t8103_allow,
	.n_allow = sizeof t8103_allow / sizeof t8103_allow[0],
	.denied = t8103_denied,
	.n_denied = sizeof t8103_denied / sizeof t8103_denied[0],
};

static bool in_pool(const struct intr_map *m, uint32_t hwirq)
{
	return m->msi_hwirq_base != INTR_INVALID && hwirq >= m->msi_hwirq_base &&
	       hwirq - m->msi_hwirq_base < m->msi_count;
}

uint32_t intr_wired_vintid(const struct intr_map *m, uint32_t hwirq)
{
	if (hwirq >= m->wired_max || in_pool(m, hwirq))
		return INTR_INVALID;
	return INTR_VINTID_SPI0 + hwirq;
}

uint32_t intr_msi_vintid(const struct intr_map *m, uint32_t hwirq)
{
	if (!in_pool(m, hwirq))
		return INTR_INVALID;
	return m->msi_vintid_base + (hwirq - m->msi_hwirq_base);
}

uint32_t intr_hwirq_of_vintid(const struct intr_map *m, uint32_t vintid, bool *is_msi)
{
	if (is_msi)
		*is_msi = false;
	if (m->msi_hwirq_base != INTR_INVALID && vintid >= m->msi_vintid_base &&
	    vintid - m->msi_vintid_base < m->msi_count) {
		if (is_msi)
			*is_msi = true;
		return m->msi_hwirq_base + (vintid - m->msi_vintid_base);
	}
	if (vintid < INTR_VINTID_SPI0 || vintid - INTR_VINTID_SPI0 >= m->wired_max)
		return INTR_INVALID;
	if (in_pool(m, vintid - INTR_VINTID_SPI0))
		return INTR_INVALID;       /* the hole: 32 + pool hwirq belongs to nobody */
	return vintid - INTR_VINTID_SPI0;
}

uint32_t intr_gsiv(const struct intr_map *m, uint32_t hwirq)
{
	uint32_t v = intr_wired_vintid(m, hwirq);

	return v != INTR_INVALID ? v : intr_msi_vintid(m, hwirq);
}

const struct intr_entry *intr_find(const struct intr_map *m, uint32_t hwirq)
{
	for (uint32_t i = 0; i < m->n_allow; i++)
		if (m->allow[i].hwirq == hwirq)
			return &m->allow[i];
	return 0;
}

const struct intr_entry *intr_find_denied(const struct intr_map *m, uint32_t hwirq)
{
	for (uint32_t i = 0; m->denied && i < m->n_denied; i++)
		if (m->denied[i].hwirq == hwirq)
			return &m->denied[i];
	return 0;
}

uint32_t intr_exposed_vintid(const struct intr_map *m, uint32_t hwirq)
{
	if (in_pool(m, hwirq))
		return m->msi_exposed ? intr_msi_vintid(m, hwirq) : INTR_INVALID;
	if (!intr_find(m, hwirq))
		return INTR_INVALID;
	return intr_wired_vintid(m, hwirq);
}

uint32_t intr_exposed_hwirq(const struct intr_map *m, uint32_t vintid)
{
	bool msi;
	uint32_t h = intr_hwirq_of_vintid(m, vintid, &msi);

	if (h == INTR_INVALID)
		return INTR_INVALID;
	if (msi)
		return m->msi_exposed ? h : INTR_INVALID;
	return intr_find(m, h) ? h : INTR_INVALID;
}

int intr_trigger(const struct intr_map *m, uint32_t hwirq)
{
	const struct intr_entry *e;

	if (in_pool(m, hwirq))
		return m->msi_exposed ? INTR_TRIG_EDGE : -1;
	e = intr_find(m, hwirq);
	return e && intr_wired_vintid(m, hwirq) != INTR_INVALID ? e->edge : -1;
}

int intr_map_validate(const struct intr_map *m)
{
	/* The vGIC model exposes INTIDs below 960 (ITLinesNumber = 29). */
	if (m->msi_hwirq_base != INTR_INVALID && m->wired_max + INTR_VINTID_SPI0 > m->msi_vintid_base)
		return INTR_E_LAYOUT;
	if (m->msi_hwirq_base != INTR_INVALID && m->msi_vintid_base + m->msi_count > 960)
		return INTR_E_LAYOUT;
	if (m->msi_hwirq_base == INTR_INVALID && m->wired_max + INTR_VINTID_SPI0 > 960)
		return INTR_E_LAYOUT;
	for (uint32_t i = 0; i < m->n_allow; i++) {
		uint32_t h = m->allow[i].hwirq;

		if (h >= m->wired_max)
			return INTR_E_RANGE;
		if (in_pool(m, h))
			return INTR_E_POOL;
		for (uint32_t j = 0; j < i; j++)
			if (m->allow[j].hwirq == h)
				return INTR_E_DUP;
	}
	for (uint32_t i = 0; m->denied && i < m->n_denied; i++) {
		uint32_t h = m->denied[i].hwirq;

		if (h >= m->wired_max)
			return INTR_E_RANGE;
		if (in_pool(m, h))
			return INTR_E_POOL;
		if (intr_find(m, h))
			return INTR_E_DUP;
		for (uint32_t j = 0; j < i; j++)
			if (m->denied[j].hwirq == h)
				return INTR_E_DUP;
	}
	return INTR_OK;
}
