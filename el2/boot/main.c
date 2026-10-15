// SPDX-License-Identifier: MIT
// Watari EL2 M0-C: first bootable skeleton (QEMU virt, TCG).
// Enters at EL2, installs vectors, drops to a tiny EL1 payload, answers its
// HVC (PSCI_VERSION / SYSTEM_OFF) per INTERFACES section 9.
// Written from the Arm ARM and PSCI DEN0022; no third-party code copied.

#include <stdint.h>

#define UART0_BASE   0x09000000UL
#define UART_DR      0x00
#define UART_FR      0x18
#define UART_FR_TXFF (1u << 5)

#define HCR_VM   (1ULL << 0)
#define HCR_FMO  (1ULL << 3)
#define HCR_IMO  (1ULL << 4)
#define HCR_AMO  (1ULL << 5)
#define HCR_RW   (1ULL << 31)

#define SPSR_EL1H_MASKED 0x3c5ULL   /* M=EL1h, DAIF all masked */

#define EC_HVC64 0x16
#define EC_SMC64 0x17

#define PSCI_VERSION     0x84000000u
#define PSCI_SYSTEM_OFF  0x84000008u
#define PSCI_VER_1_1     0x00010001u
#define PSCI_NOT_SUPPORTED (-1LL)

struct frame {
    uint64_t x[31];
    uint64_t esr, elr, spsr, far;
};

extern uint8_t __el1_stack_top[];
void enter_el1(uint64_t entry, uint64_t sp, uint64_t spsr, uint64_t hcr)
    __attribute__((noreturn));

/* ---- UART (PL011; QEMU needs no init) ---------------------------------- */
static void putc_(char c)
{
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    volatile uint32_t *dr = (volatile uint32_t *)(UART0_BASE + UART_DR);
    while (*fr & UART_FR_TXFF)
        ;
    *dr = (uint32_t)c;
}

static void puts_(const char *s)
{
    while (*s) {
        if (*s == '\n')
            putc_('\r');
        putc_(*s++);
    }
}

static void puthex(uint64_t v)
{
    puts_("0x");
    for (int i = 60; i >= 0; i -= 4)
        putc_("0123456789abcdef"[(v >> i) & 0xf]);
}

static void put_kv(const char *k, uint64_t v)
{
    puts_(k);
    puthex(v);
    puts_("\n");
}

/* ---- Exception handling -------------------------------------------------- */
static const char *const vec_names[16] = {
    "cur_el_sp0 sync", "cur_el_sp0 irq", "cur_el_sp0 fiq", "cur_el_sp0 serror",
    "cur_el_spx sync", "cur_el_spx irq", "cur_el_spx fiq", "cur_el_spx serror",
    "lower_a64 sync", "lower_a64 irq", "lower_a64 fiq", "lower_a64 serror",
    "lower_a32 sync", "lower_a32 irq", "lower_a32 fiq", "lower_a32 serror",
};

/* Never hang silently (lesson F-14): dump state, then halt. */
static void __attribute__((noreturn)) fatal(struct frame *f, unsigned idx)
{
    puts_("Watari EL2: UNEXPECTED EXCEPTION: ");
    puts_(vec_names[idx & 15]);
    puts_("\n");
    put_kv("  ESR_EL2  = ", f->esr);
    put_kv("  ELR_EL2  = ", f->elr);
    put_kv("  FAR_EL2  = ", f->far);
    put_kv("  SPSR_EL2 = ", f->spsr);
    puts_("Watari EL2: halted\n");
    for (;;)
        __asm__ volatile("wfe");
}

static void __attribute__((noreturn)) psci_system_off(void)
{
    /* QEMU virt with virtualization=on uses the SMC conduit for PSCI. */
    register uint64_t x0 __asm__("x0") = PSCI_SYSTEM_OFF;
    __asm__ volatile("smc #0" : "+r"(x0) : : "memory");
    puts_("Watari EL2: SYSTEM_OFF returned?!\n");
    for (;;)
        __asm__ volatile("wfe");
}

static void handle_hvc(struct frame *f)
{
    uint32_t imm = f->esr & 0xffff;
    uint32_t fid = (uint32_t)f->x[0];

    puts_("Watari EL2: HVC from EL1, imm=");
    puthex(imm);
    puts_(" fid=");
    puthex(fid);
    puts_("\n");

    int64_t ret = PSCI_NOT_SUPPORTED;
    if (imm == 0) {
        switch (fid) {
        case PSCI_VERSION:
            ret = PSCI_VER_1_1;
            break;
        case PSCI_SYSTEM_OFF:
            puts_("Watari EL2: PSCI SYSTEM_OFF -> forwarding to QEMU firmware (SMC)\n");
            psci_system_off();
        default:
            break;
        }
    }
    /* SMCCC: result in x0, x1-x3 zeroed (INTERFACES section 9). ELR already
     * points past the HVC. */
    f->x[0] = (uint64_t)ret;
    f->x[1] = f->x[2] = f->x[3] = 0;
}

void el2_exception(struct frame *f, unsigned idx)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    if (idx == 8 && ec == EC_HVC64) {
        handle_hvc(f);
        return;
    }
    fatal(f, idx);
}

/* ---- EL1 payload ---------------------------------------------------------- */
static uint64_t hvc(uint64_t fid)
{
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = 0;
    register uint64_t x2 __asm__("x2") = 0;
    register uint64_t x3 __asm__("x3") = 0;
    __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : : "memory");
    return x0;
}

static void __attribute__((noreturn)) el1_main(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    puts_("Watari EL1: hello from EL1 (CurrentEL=");
    putc_('0' + (char)((el >> 2) & 3));
    puts_(")\n");

    uint64_t v = hvc(PSCI_VERSION);
    puts_("Watari EL1: PSCI_VERSION returned ");
    putc_('0' + (char)((v >> 16) & 0xf));
    putc_('.');
    putc_('0' + (char)(v & 0xf));
    puts_("\n");

    puts_("Watari EL1: calling PSCI SYSTEM_OFF\n");
    hvc(PSCI_SYSTEM_OFF);
    puts_("Watari EL1: SYSTEM_OFF returned?!\n");
    for (;;)
        __asm__ volatile("wfe");
}

/* ---- EL2 main ---------------------------------------------------------------- */
void __attribute__((noreturn)) el2_main(void)
{
    uint64_t el, vbar;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, vbar_el2" : "=r"(vbar));

    puts_("Watari EL2: hello from EL2\n");
    put_kv("Watari EL2: CurrentEL = ", el >> 2);
    put_kv("Watari EL2: VBAR_EL2  = ", vbar);

    /* HCR_EL2: RW=1 (EL1 is AArch64), IMO/FMO/AMO=1 (physical IRQ/FIQ/SError
     * routed to EL2, as designed). VM=0, E2H=0, TSC=0 in this M0-C skeleton:
     * stage-2, VHE and SMC trapping arrive with later milestones. */
    uint64_t hcr = HCR_RW | HCR_IMO | HCR_FMO | HCR_AMO;
    puts_("Watari EL2: eret to EL1h payload\n");
    enter_el1((uint64_t)el1_main, (uint64_t)__el1_stack_top, SPSR_EL1H_MASKED, hcr);
}
