/* SPDX-License-Identifier: MIT */
/* Watari EL2 - GICv2m-style MSI frame model. See include/wel2_v2m.h (register sources there). */
#include "wel2_v2m.h"

int v2m_init(struct v2m *m, uint64_t base, uint32_t spi_base, uint32_t spi_count, struct vgic *v)
{
	if (spi_count == 0 || spi_count > V2M_TYPER_COUNT_MASK || spi_base < 32 ||
	    spi_base > V2M_TYPER_BASE_MASK || spi_base + spi_count > VGIC_NR_IRQS)
		return -1;
	if (base & (V2M_FRAME_SIZE - 1))
		return -1;
	m->base = base;
	m->page_base = base & ~(uint64_t)(V2M_PAGE_SIZE - 1);
	m->log_stray = 0;
	m->spi_base = spi_base;
	m->spi_count = spi_count;
	m->v = v;
	m->log = 0;
	m->log_ctx = 0;
	m->logged = false;
	m->st.doorbells = m->st.ignored = m->st.bad_access = m->st.stray = 0;
	return 0;
}

int v2m_init_from_map(struct v2m *m, uint64_t base, const struct intr_map *map, struct vgic *v)
{
	if (map->msi_hwirq_base == INTR_INVALID || map->msi_count == 0)
		return -1;
	return v2m_init(m, base, map->msi_vintid_base, map->msi_count, v);
}

bool v2m_contains(const struct v2m *m, uint64_t addr)
{
	return addr >= m->page_base && addr - m->page_base < V2M_PAGE_SIZE;
}

bool v2m_page_free(const struct v2m *m, uint64_t ram_base, uint64_t ram_end)
{
	return ram_end <= ram_base || ram_end <= m->page_base || ram_base >= m->page_base + V2M_PAGE_SIZE;
}

static bool in_frame(const struct v2m *m, uint64_t addr)
{
	return addr >= m->base && addr - m->base < V2M_FRAME_SIZE;
}

/* RAZ/WI byte of the trap page outside the frame: any size or alignment is accepted (a refusal would be
 * fatal for the guest at EL2), counted, and the first V2M_STRAY_LOG_MAX are logged. */
static void stray(struct v2m *m, uint64_t addr, bool write, unsigned size)
{
	if (m->st.stray++ < V2M_STRAY_LOG_MAX && m->log_stray)
		m->log_stray(m->log_ctx, (uint32_t)(addr - m->page_base), write, size);
}

int v2m_add_hole(const struct v2m *m, struct wel2_range *holes, unsigned *n, unsigned cap, unsigned gshift)
{
	return wel2_s2_add_hole(holes, n, cap, m->page_base, V2M_PAGE_SIZE, gshift);
}

uint64_t v2m_doorbell_addr(const struct v2m *m)
{
	return m->base + V2M_SETSPI_NS;
}

int v2m_mmio_read(struct v2m *m, uint64_t addr, unsigned size, uint64_t *val)
{
	uint32_t off;

	if (!v2m_contains(m, addr))
		return VGIC_ERR_RANGE;
	*val = 0;
	if (!in_frame(m, addr)) {
		stray(m, addr, false, size);
		return VGIC_OK;
	}
	off = (uint32_t)(addr - m->base);
	if (size != 4 || (off & 3)) {
		m->st.bad_access++;
		return VGIC_ERR_ACCESS;
	}
	if (off == V2M_TYPER)
		*val = ((uint64_t)m->spi_base << V2M_TYPER_BASE_SH) | m->spi_count;
	else if (off == V2M_IIDR)
		*val = V2M_IIDR_VALUE;
	return VGIC_OK;                 /* SETSPI_NS is WO: reads as zero; the rest RAZ */
}

int v2m_mmio_write(struct v2m *m, uint64_t addr, unsigned size, uint64_t val)
{
	uint32_t off, id;

	if (!v2m_contains(m, addr))
		return VGIC_ERR_RANGE;
	if (!in_frame(m, addr)) {
		stray(m, addr, true, size);
		return VGIC_OK;
	}
	off = (uint32_t)(addr - m->base);
	if (off != V2M_SETSPI_NS) {
		if (size != 4 || (off & 3)) {
			m->st.bad_access++;
			return VGIC_ERR_ACCESS;
		}
		return VGIC_OK;         /* TYPER, IIDR, reserved: write ignored */
	}
	/* 32-bit, or 16-bit on the low half (the spec requires 16-bit access to [15:0]) */
	if (size == 2) {
		val &= 0xFFFFu;
	} else if (size != 4) {
		m->st.bad_access++;
		return VGIC_ERR_ACCESS;
	}
	id = (uint32_t)val & V2M_SETSPI_ID_MASK;
	if (id < m->spi_base || id - m->spi_base >= m->spi_count) {
		m->st.ignored++;
		if (!m->logged) {
			m->logged = true;
			if (m->log)
				m->log(m->log_ctx, (uint32_t)val);
		}
		return VGIC_OK;
	}
	vgic_set_pending(m->v, 0, id);  /* an SPI: the vCPU argument is not used */
	m->st.doorbells++;
	return VGIC_OK;
}
