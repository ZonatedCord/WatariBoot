/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 - GICv2m-style MSI frame model (host-only until M3a/M4).
 *
 * A 4 KB register frame in the guest's address space that (a) tells the OS which SPI range
 * is its MSI pool (TYPER) and (b) turns a CPU write of an INTID to SETSPI_NS into a pending
 * SPI of the vGIC model. It is the frame the M3a design places at 0xFFFFF000 on the t8103
 * (ARCHITECTURE docs/design/watari-el2-devirq 5.1/5.2): there the device MSI never reaches
 * CPU-visible memory (Apple PCIe turns it into an AIC edge, pirq delivers it), so the frame
 * only has to exist and answer; the doorbell write here is the CPU-write path that the QEMU
 * bench exercises. This module is not linked into the boot path.
 *
 * Register offsets, frame-relative:
 *  0x008 TYPER       RO. Compatibility layout: SPI base [25:16], SPI count [9:0], upper bits 0.
 *                    [verified: IHI 0069H.b Table 12-26 gives the offset 0x008 (the register
 *                    description 12.9.45 says 0x004, an inconsistency inside the document) and
 *                    the GICv3 GICM_TYPER layout Valid[31] CLR[30] SR[29] INTID[28:16]
 *                    NumSPIs[10:0]. The GICv2m layout base[25:16] / count[9:0] is [hypothesis:
 *                    design source V2M (Linux irq-gic-v2m), not in the local spec set]. The two
 *                    agree while base and count fit 10 bits and bits 31:29 are 0 (Valid = 0
 *                    for a v3 reader), which holds for 928/32 and for QEMU's 80/64.]
 *  0x040 SETSPI_NS   WO. INTID in [12:0], [31:13] RES0; a 16-bit access to [15:0] is supported;
 *                    an INTID outside this frame's range is ignored (and counted).
 *                    [verified: IHI 0069H.b 12.9.43 GICM_SETSPI_NSR]
 *  0xFCC IIDR        RO. ProductID [31:24], Variant [19:16], Revision [15:12], Implementer
 *                    [11:0], all implementation defined. [verified: IHI 0069H.b 12.9.42]
 *  all else          RAZ/WI (CLRSPI 0x048 and the secure aliases are absent: TYPER.CLR = SR = 0;
 *                    the ID registers 0xFD0-0xFFC read zero).
 * A doorbell write latches the SPI pending, as an edge (the guest's acknowledge consumes it),
 * whatever the SPI's ICFGR says: the spec's level behaviour (pending until CLRSPI) needs a
 * CLRSPI register, which this frame does not offer [verified: 12.9.43 and TYPER.CLR].
 */
#ifndef WEL2_V2M_H
#define WEL2_V2M_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_intr_map.h"
#include "wel2_vgic.h"

#define V2M_FRAME_SIZE    0x1000u
#define V2M_TYPER         0x008u
#define V2M_SETSPI_NS     0x040u
#define V2M_IIDR          0xFCCu

#define V2M_TYPER_BASE_SH    16
#define V2M_TYPER_BASE_MASK  0x3FFu     /* [25:16] */
#define V2M_TYPER_COUNT_MASK 0x3FFu     /* [9:0] */
#define V2M_IIDR_VALUE       0x01000000u /* ProductID 1 [hypothesis: implementation defined] */
#define V2M_SETSPI_ID_MASK   0x1FFFu    /* [12:0] */

struct v2m_stats {
	uint32_t doorbells;     /* writes that made an SPI pending */
	uint32_t ignored;       /* INTID outside the frame's range */
	uint32_t bad_access;    /* size not supported on the register */
};

struct v2m {
	uint64_t base;          /* guest address of the frame */
	uint32_t spi_base;      /* first INTID of the pool */
	uint32_t spi_count;
	struct vgic *v;
	void (*log)(void *ctx, uint32_t value);   /* first ignored doorbell value; may be NULL */
	void *log_ctx;
	bool logged;
	struct v2m_stats st;
};

/* 0, or -1 when the range is not within the SPIs of the vGIC model or does not fit TYPER. */
int v2m_init(struct v2m *m, uint64_t base, uint32_t spi_base, uint32_t spi_count, struct vgic *v);
/* Frame whose pool is the MSI pool of an interrupt map (t8103: 928..959). -1 if the map
 * has no pool. */
int v2m_init_from_map(struct v2m *m, uint64_t base, const struct intr_map *map, struct vgic *v);

bool v2m_contains(const struct v2m *m, uint64_t addr);
/* Guest-visible MMIO. Return VGIC_OK, VGIC_ERR_ACCESS (size) or VGIC_ERR_RANGE (not ours). */
int v2m_mmio_read(struct v2m *m, uint64_t addr, unsigned size, uint64_t *val);
int v2m_mmio_write(struct v2m *m, uint64_t addr, unsigned size, uint64_t val);
/* Absolute address of the doorbell: what Windows programs into a device's MSI capability. */
uint64_t v2m_doorbell_addr(const struct v2m *m);

#endif
