/* SPDX-License-Identifier: MIT */
/* Watari EL2 boot spike: shared declarations (M1-2). */
#ifndef WEL2_BOOT_H
#define WEL2_BOOT_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_vgic.h"

/* QEMU virt GICv3 windows (hw/arm/virt.c memmap). */
#define GICD_PHYS   0x08000000UL
#define GICD_SIZE   0x10000UL
#define GICR_PHYS   0x080A0000UL   /* redistributor frame of CPU 0 (RD_base) */
#define NR_VCPUS    1u

/* Exception frame saved by start.S (layout fixed there). */
struct frame {
    uint64_t x[31];
    uint64_t esr, elr, spsr, far;
};

/* main.c: UART and PSCI helpers. */
void putc_(char c);
void puts_(const char *s);
void puthex(uint64_t v);
void put_kv(const char *k, uint64_t v);

/* payload.c (runs at EL1) */
void el1_main(void) __attribute__((noreturn));

/* gic_phys.c: EL2 owns the physical GICv3 (GIC spec IHI 0069H.b). */
#define PPI_MAINT  25u   /* GIC maintenance interrupt (QEMU virt: PPI 25) */
#define PPI_VTIMER 27u   /* EL1 virtual timer (CNTV) */
void     gic_phys_init(void);
uint32_t gic_phys_ack(void);                 /* ICC_IAR1_EL1 at EL2 */
void     gic_phys_eoi(uint32_t intid);       /* ICC_EOIR1_EL1 at EL2 */
void     gic_phys_ppi_enable(unsigned ppi, bool on);

/* cpuif.c: virtual CPU interface (ICH_*) driven from the vGIC model. */
void cpuif_init(struct vgic *v);
void cpuif_flush(void);                      /* harvest LRs, fill from vGIC */
void cpuif_irq(void);                        /* physical IRQ taken at EL2 */
void cpuif_stats(void);
bool cpuif_sysreg_trap(struct frame *f);     /* ICC_SGI*R_EL1 writes */

#endif
