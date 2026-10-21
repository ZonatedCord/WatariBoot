/* SPDX-License-Identifier: MIT */
/* Watari EL2 - GICv2m-style MSI frame model. See include/wel2_v2m.h (register sources there). */
#include "wel2_v2m.h"

int v2m_init(struct v2m *m, uint64_t base, uint32_t spi_base, uint32_t spi_count, struct vgic *v)
{
	if (spi_count == 0 || spi_count > V2M_TYPER_COUNT_MASK || spi_base < 32 ||
	    spi_base > V2M_TYPER_BASE_MASK || spi_base + spi_count > VGIC_NR_IRQS)
		return -1;
	m->base = base;
	m->spi_base = spi_base;
	m->spi_count = spi_count;
	m->v = v;
	m->log = 0;
	m->log_ctx = 0;
	m->logged = false;
	m->st.doorbells = m->st.ignored = m->st.bad_access = 0;
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
	return addr >= m->base && addr - m->base < V2M_FRAME_SIZE;
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
	off = (uint32_t)(addr - m->base);
	*val = 0;
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
