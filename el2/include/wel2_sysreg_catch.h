/* SPDX-License-Identifier: MIT */
/*
 * Watari EL2 "catch" mode (wel2.catch=1): find out which system register the guest touches that the machine does not
 * implement (M3a, bootmgr UNDEFINED at EL1, ESR 0x02000000). Pure logic, no hardware: classification of the trapped
 * encoding, the value a read returns, the table of distinct encodings with counts, and the formatting of the log lines.
 * The boot code (boot/catch.c) owns the traps and the sinks; tests/test_sysreg_catch.c drives this file on the host.
 *
 * What traps (set by the layer when catch mode is on; bit positions from the Arm ARM, from memory [hypothesis] until
 * compared with a copy of it: the layer reads MDCR_EL2 back and logs the value so the bench shows what stuck):
 *   MDCR_EL2.TPM (6)    EL0/EL1 accesses to the Performance Monitors registers (PMCR_EL0, PMCNTEN*, PMOVS*, PMEV*, ...)
 *   MDCR_EL2.TPMCR (5)  EL0/EL1 accesses to PMCR_EL0
 *   MDCR_EL2.TDA (9)    EL1/EL0 accesses to the debug registers (DBGB*, DBGW*, MDSCR_EL1, MDCCSR_EL0, OSDTR*, ...)
 *   MDCR_EL2.TDOSA (10) the OS-related debug registers (OSLAR_EL1, OSLSR_EL1, OSDLR_EL1, DBGPRCR_EL1, DBGCLAIM*)
 *   MDCR_EL2.TDRA (11)  the debug ROM address registers (MDRAR_EL1)
 *   HCR_EL2.TIDCP (20)  the IMPLEMENTATION DEFINED registers (op0 = 3, CRn = 11 or 15) and, per the Arm ARM description of TIDCP
 *                       ("Trap IMPLEMENTATION DEFINED functionality": EL1/EL0 accesses to the IMPLEMENTATION DEFINED System
 *                       registers and System instructions in the reserved encoding spaces, i.e. MSR/MRS op0 = 3 and
 *                       SYS/SYSL op0 = 1, in both cases CRn = 11 or 15) [hypothesis, from memory]
 *   HCR_EL2.TACR (21)   ACTLR_EL1
 * All of them arrive as EC 0x18 (MSR/MRS/system instruction), ISS as in Arm ARM D24.2 ESR_EL2.
 *
 * The policy: reads return 0 (OSLSR_EL1: OS Lock implemented and unlocked, see wel2_catch_value), writes are ignored.
 * An encoding this module does not own is never touched (the caller keeps its old path).
 */
#ifndef WEL2_SYSREG_CATCH_H
#define WEL2_SYSREG_CATCH_H

#include <stdbool.h>
#include <stdint.h>

#define WEL2_CATCH_TAB_N     64u    /* distinct encodings kept; later ones are counted in `overflow` and logged (rate limited) */
#define WEL2_CATCH_OVF_LINES 64u    /* at most this many "count=overflow" lines, then one "further lines suppressed" line */
#define WEL2_CATCH_NAME_MAX  24u
#define WEL2_CATCH_LINE_MAX  112u

/* MDCR_EL2 / HCR_EL2 trap bits set by catch mode. */
#define WEL2_MDCR_TPMCR  (1ULL << 5)
#define WEL2_MDCR_TPM    (1ULL << 6)
#define WEL2_MDCR_TDA    (1ULL << 9)
#define WEL2_MDCR_TDOSA  (1ULL << 10)
#define WEL2_MDCR_TDRA   (1ULL << 11)
#define WEL2_MDCR_CATCH  (WEL2_MDCR_TPMCR | WEL2_MDCR_TPM | WEL2_MDCR_TDA | WEL2_MDCR_TDOSA | WEL2_MDCR_TDRA)
#define WEL2_HCR_TIDCP   (1ULL << 20)
#define WEL2_HCR_TACR    (1ULL << 21)
#define WEL2_HCR_CATCH   (WEL2_HCR_TIDCP | WEL2_HCR_TACR)

enum wel2_catch_class {
	WEL2_CATCH_NONE = 0,         /* not ours */
	WEL2_CATCH_PMU,              /* Performance Monitors (op0 3, CRn 9 / CRn 14) */
	WEL2_CATCH_DEBUG,            /* debug (op0 2) */
	WEL2_CATCH_IMPDEF,           /* op0 3 (MSR/MRS) or op0 1 (SYS/SYSL), CRn 11 or 15 */
	WEL2_CATCH_ACTLR,            /* ACTLR_EL1 */
};

/* One trapped access. rd = 1 for MRS (a read), 0 for MSR. */
struct wel2_sysreg_enc {
	uint8_t op0, op1, crn, crm, op2, rd, rt;
};

/* Decode the ISS of an EC 0x18 trap. Returns false for op0 < 2 (system instructions SYS/SYSL: TLBI, DC, ...), which this
 * module never owns, except SYS/SYSL with op0 = 1 and CRn 11 or 15 (HCR_EL2.TIDCP, see above): rd = 1 is SYSL (a read). rt = 31
 * means XZR. */
bool wel2_catch_decode(uint32_t iss, struct wel2_sysreg_enc *e);

enum wel2_catch_class wel2_catch_classify(const struct wel2_sysreg_enc *e);

/* Name of the register when the architecture (or this table) knows it ("PMCR_EL0", "DBGBVR3_EL1", ...), written to buf
 * (capacity cap, NUL-terminated). Returns its length, 0 when unknown (buf is then ""). */
unsigned wel2_catch_name(const struct wel2_sysreg_enc *e, char *buf, unsigned cap);

/* The value a read of an owned encoding returns when the core's OS lock is clear. Meaningful only when classify() != NONE. */
uint64_t wel2_catch_value(const struct wel2_sysreg_enc *e);

/* Per-core state of the emulation: the OS Lock bit (OSLAR_EL1 sets / clears it, OSLSR_EL1.OSLK reads it back). Zero-initialise. */
struct wel2_catch_cpu {
	uint8_t oslk;
};
/* The value of a read on this core (OSLSR_EL1 = 0x8 | OSLK << 1, everything else as wel2_catch_value) and the effect of a write
 * (wval = the written register, 0 for XZR): OSLAR_EL1 bit 0 is the new OSLK [hypothesis: Arm ARM OSLAR_EL1.OSLK]. */
uint64_t wel2_catch_read(const struct wel2_catch_cpu *c, const struct wel2_sysreg_enc *e);
void wel2_catch_write(struct wel2_catch_cpu *c, const struct wel2_sysreg_enc *e, uint64_t wval);

/* The table of distinct (op0, op1, CRn, CRm, op2, direction) encodings. rt is not part of the key. Zero-initialise it. */
struct wel2_catch_ent {
	struct wel2_sysreg_enc enc;
	uint8_t core;                /* core of the first catch */
	uint32_t count;
	uint64_t first_pc;           /* ELR_EL2 of the first catch */
};
struct wel2_catch_tab {
	struct wel2_catch_ent e[WEL2_CATCH_TAB_N];
	unsigned n;                  /* entries used */
	uint64_t total;              /* all catches */
	uint64_t overflow;           /* catches of encodings that did not fit the table */
	/* encodings that did not fit, remembered (packed) so that each is logged once; at most WEL2_CATCH_OVF_LINES of them */
	uint32_t ovf_key[WEL2_CATCH_OVF_LINES];
	unsigned ovf_n;
	bool ovf_capped;             /* the "further lines suppressed" notice was handed out */
};
enum wel2_catch_rec {
	WEL2_CATCH_REC_OLD = 0,      /* seen before: only counted */
	WEL2_CATCH_REC_NEW,          /* first time, fits the table: log it (count=1) */
	WEL2_CATCH_REC_OVERFLOW,     /* first time, table full: log it with "count=overflow" (at most WEL2_CATCH_OVF_LINES times) */
	WEL2_CATCH_REC_OVERFLOW_CAP, /* the overflow lines are used up: say "further lines suppressed" (once) */
	WEL2_CATCH_REC_OVERFLOW_OLD, /* counted in `overflow`, nothing to log */
};
/* Count one catch. NEW = first time this encoding is seen (and it fits the table); *idx is its entry. */
enum wel2_catch_rec wel2_catch_record(struct wel2_catch_tab *t, const struct wel2_sysreg_enc *e, uint64_t pc,
				      unsigned core, unsigned *idx);

/* "<prefix> <R|W> S<op0>_<op1>_C<n>_C<m>_<op2> [<name>] PC=0x<hex> core=<n> count=<n>" into buf (cap includes the NUL;
 * no newline). The first-catch line uses prefix "CATCH" and count 1. Returns the length. */
unsigned wel2_catch_format(char *buf, unsigned cap, const char *prefix, const struct wel2_sysreg_enc *e, uint64_t pc,
			   unsigned core, uint32_t count);
/* The same line for an encoding that did not fit the table: "... PC=0x<hex> core=<n> count=overflow". */
unsigned wel2_catch_format_overflow(char *buf, unsigned cap, const struct wel2_sysreg_enc *e, uint64_t pc, unsigned core);
#define WEL2_CATCH_OVF_NOTICE "CATCH overflow: further lines suppressed"
/* "CATCH SUMMARY distinct=<n> total=<n> overflow=<n>" */
unsigned wel2_catch_format_summary(char *buf, unsigned cap, const struct wel2_catch_tab *t);

#endif
