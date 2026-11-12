/* SPDX-License-Identifier: MIT */
/* Watari EL2 test guest (T7a): shared declarations. Runs at EL1 with the MMU and caches off, so every access is
 * Device memory: no unaligned accesses (-mstrict-align), no exclusives, only plain volatile loads and stores. */
#ifndef GUEST_H
#define GUEST_H

#include <stdbool.h>
#include <stdint.h>
#include "wel2_hvc.h"
#include "wel2_guest_info.h"

typedef uint64_t u64;
typedef uint32_t u32;

#define MAX_CORES WEL2_GINFO_MAX_CORES
#ifndef STACK_SIZE
#define STACK_SIZE 0x2000u
#endif
#define SYSREG_RD(reg) ({ u64 _v; __asm__ volatile("mrs %0, " #reg : "=r"(_v)); _v; })
#define SYSREG_WR(reg, val) __asm__ volatile("msr " #reg ", %0" : : "r"((u64)(val)))

/* SMCCC call over HVC #0 */
static inline u64 hvc4(u64 fid, u64 a1, u64 a2, u64 a3)
{
    register u64 x0 __asm__("x0") = fid;
    register u64 x1 __asm__("x1") = a1;
    register u64 x2 __asm__("x2") = a2;
    register u64 x3 __asm__("x3") = a3;
    __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                     : : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
                       "memory");
    return x0;
}

#define PSCI_VERSION       0x84000000u
#define PSCI_CPU_OFF       0x84000002u
#define PSCI_CPU_ON_64     0xC4000003u
#define PSCI_AFFINITY_64   0xC4000004u
#define PSCI_SYSTEM_OFF    0x84000008u
#define PSCI_SYSTEM_RESET  0x84000009u
#define PSCI_FEATURES      0x8400000Au
#define PSCI_NOT_SUPPORTED (-1)
#define PSCI_ALREADY_ON    (-4)

static inline u64 mpidr_now(void) { return SYSREG_RD(mpidr_el1) & 0xff00ffffffull; }
static inline u64 cntvct(void) { return SYSREG_RD(cntvct_el0); }
static inline void irq_on(void) { __asm__ volatile("msr daifclr, #2" : : : "memory"); }
static inline void irq_off(void) { __asm__ volatile("msr daifset, #2" : : : "memory"); }

/* ---- main.c: log buffer and timing ------------------------------------------------------------------------ */
struct buf { char s[WEL2_LOG_MAX]; unsigned n; };
void b_init(struct buf *b);
void b_str(struct buf *b, const char *s);
void b_hex(struct buf *b, u64 v);
void b_dec(struct buf *b, u64 v);
void b_flush(struct buf *b);                    /* one WEL2_LOG line (QEMU fallback: the PL011) */
u64 ms_to_ticks(u64 ms);
extern u64 g_frq;

/* ---- gic.c --------------------------------------------------------------------------------------------------- */
extern u64 g_gicd, g_gicr, g_gicr_stride;
extern unsigned g_ncores;                        /* redistributors found (GICR_TYPER.Last) */
extern u64 g_mpidr[MAX_CORES];                   /* affinity of each redistributor, MPIDR layout */
unsigned gic_scan_cores(void);                   /* fills g_ncores / g_mpidr; returns the count */
int gic_core_of(u64 mpidr);                      /* index in g_mpidr or -1 */
bool gic_wake(unsigned core);                    /* clear ProcessorSleep and wait for ChildrenAsleep = 0 */
void gic_cpu_enable(void);                       /* ICC_SRE.SRE, PMR 0xF0, BPR1 max, IGRPEN1 */
void gic_priv_enable(unsigned core, unsigned intid);   /* Group 1, priority 0x80, level/edge default, enable */
void gic_send_sgi(unsigned intid, u64 target_mpidr);
u32 gic_rd32(u64 a);
void gic_wr32(u64 a, u32 v);

/* ---- IRQ bookkeeping (main.c handler, read by the checks) ----------------------------------------------------- */
extern volatile u32 g_irq_count[MAX_CORES][32];  /* INTIDs 0..31 per core */
extern volatile u32 g_irq_last, g_irq_other, g_exc, g_svc, g_fiq;

#endif
