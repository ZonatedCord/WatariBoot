/* SPDX-License-Identifier: MIT */
/* The QEMU virt physical GICv3 owned by EL2 (gic_phys.c; GIC spec IHI 0069H.b). T6c: only plat_qemu.c includes this;
 * the rest of the layer reaches the interrupt controller through g_plat (plat.h). */
#ifndef WEL2_GIC_PHYS_H
#define WEL2_GIC_PHYS_H

#include "boot.h"

unsigned gic_phys_discover(void);            /* fills g_pcpu[].aff/.gicr, returns count */
void     gic_phys_init_dist(void);           /* GICD, boot core only */
void     gic_phys_init_cpu(void);            /* redistributor + CPU interface of this core */
void     gic_phys_cpu_down(void);            /* redistributor to sleep, before CPU_OFF */
uint32_t gic_phys_ack(void);                 /* ICC_IAR1_EL1 at EL2 */
void     gic_phys_eoi(uint32_t intid);       /* ICC_EOIR1_EL1 at EL2 */
void     gic_phys_ppi_enable(unsigned ppi, bool on);   /* this core's redistributor */
unsigned gic_phys_idbits(void);              /* physical GICD_TYPER.IDbits + 1 */
void     gic_phys_kick(unsigned cpu);        /* physical SGI_KICK to another core */
void     gic_phys_spi_enable(uint32_t intid, bool on);
void     gic_phys_spi_route(uint32_t intid, int vcpu);
void     gic_phys_spi_trigger(uint32_t intid, bool edge);
bool     gic_phys_has_lpis(void);
void     gic_phys_lpi_reg(unsigned cpu, unsigned off, uint64_t val);

#endif
