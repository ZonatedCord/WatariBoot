/* SPDX-License-Identifier: MIT */
/* Host tests: catch mode, the pure part (wel2_sysreg_catch.h, vgic/sysreg_catch.c): decoding of the EC 0x18 ISS, which
 * encodings are ours (PMU, debug, IMPDEF, ACTLR_EL1) and which are not (ICC_*, the ID registers, SCTLR_EL1, SYS), the values
 * reads return, the table of distinct encodings with its overflow, and the log lines. */
#include <stdbool.h>
#include <string.h>
#include "test.h"
#include "wel2_sysreg_catch.h"

/* Build the ISS of an EC 0x18 trap (Arm ARM D24.2: op0 [21:20], op2 [19:17], op1 [16:14], CRn [13:10], Rt [9:5], CRm [4:1],
 * direction [0], 1 = read). */
static uint32_t iss(unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt, unsigned rd)
{
    return (op0 << 20) | (op2 << 17) | (op1 << 14) | (crn << 10) | (rt << 5) | (crm << 1) | rd;
}

static enum wel2_catch_class cls(unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2)
{
    struct wel2_sysreg_enc e;

    if (!wel2_catch_decode(iss(op0, op1, crn, crm, op2, 0, 1), &e))
        return WEL2_CATCH_NONE;
    return wel2_catch_classify(&e);
}

static void t_decode(void)
{
    struct wel2_sysreg_enc e;

    CHECK(wel2_catch_decode(iss(3, 3, 9, 12, 0, 5, 1), &e));
    CHECK_EQ(e.op0, 3); CHECK_EQ(e.op1, 3); CHECK_EQ(e.crn, 9); CHECK_EQ(e.crm, 12); CHECK_EQ(e.op2, 0);
    CHECK_EQ(e.rt, 5); CHECK_EQ(e.rd, 1);
    CHECK(wel2_catch_decode(iss(2, 0, 1, 0, 4, 31, 0), &e));            /* OSLAR_EL1 write of XZR */
    CHECK_EQ(e.op0, 2); CHECK_EQ(e.rt, 31); CHECK_EQ(e.rd, 0);
    CHECK(!wel2_catch_decode(iss(1, 0, 8, 7, 0, 0, 0), &e));            /* TLBI: a system instruction, never ours */
    CHECK(!wel2_catch_decode(iss(1, 3, 7, 5, 1, 0, 0), &e));            /* DC CVAC / IC: neither */
    CHECK(wel2_catch_decode(iss(1, 0, 11, 2, 3, 7, 0), &e));            /* SYS with CRn 11: IMPLEMENTATION DEFINED (TIDCP) */
    CHECK_EQ(e.op0, 1); CHECK_EQ(e.crn, 11); CHECK_EQ(e.rd, 0);
    CHECK(wel2_catch_decode(iss(1, 0, 15, 2, 3, 7, 1), &e));            /* SYSL with CRn 15 */
    CHECK_EQ(e.op0, 1); CHECK_EQ(e.crn, 15); CHECK_EQ(e.rd, 1);
    CHECK(wel2_catch_decode(iss(3, 7, 15, 15, 7, 0, 1) | 0xfe000000u, &e));   /* bits above the ISS do not matter */
    CHECK_EQ(e.crn, 15); CHECK_EQ(e.crm, 15); CHECK_EQ(e.op2, 7); CHECK_EQ(e.op1, 7);
}

static void t_classify_pmu(void)
{
    /* every architected EL0/EL1 PMU register */
    CHECK_EQ(cls(3, 3, 9, 12, 0), WEL2_CATCH_PMU);          /* PMCR_EL0 */
    for (unsigned o = 0; o < 8; o++)
        CHECK_EQ(cls(3, 3, 9, 12, o), WEL2_CATCH_PMU);      /* PMCNTEN*, PMOVSCLR, PMSWINC, PMSELR, PMCEID0/1 */
    CHECK_EQ(cls(3, 3, 9, 13, 0), WEL2_CATCH_PMU);          /* PMCCNTR_EL0 */
    CHECK_EQ(cls(3, 3, 9, 13, 1), WEL2_CATCH_PMU);          /* PMXEVTYPER_EL0 */
    CHECK_EQ(cls(3, 3, 9, 13, 2), WEL2_CATCH_PMU);          /* PMXEVCNTR_EL0 */
    CHECK_EQ(cls(3, 3, 9, 14, 0), WEL2_CATCH_PMU);          /* PMUSERENR_EL0 */
    CHECK_EQ(cls(3, 3, 9, 14, 3), WEL2_CATCH_PMU);          /* PMOVSSET_EL0 */
    CHECK_EQ(cls(3, 0, 9, 14, 1), WEL2_CATCH_PMU);          /* PMINTENSET_EL1 */
    CHECK_EQ(cls(3, 0, 9, 14, 2), WEL2_CATCH_PMU);          /* PMINTENCLR_EL1 */
    CHECK_EQ(cls(3, 0, 9, 14, 6), WEL2_CATCH_PMU);          /* PMMIR_EL1 */
    for (unsigned crm = 8; crm < 16; crm++)
        for (unsigned o = 0; o < 8; o++)
            CHECK_EQ(cls(3, 3, 14, crm, o), WEL2_CATCH_PMU);   /* PMEVCNTR<n>, PMEVTYPER<n>, PMCCFILTR_EL0 */
    /* the timers share CRn 14: not ours */
    CHECK_EQ(cls(3, 3, 14, 2, 1), WEL2_CATCH_NONE);         /* CNTP_CTL_EL0 */
    CHECK_EQ(cls(3, 3, 14, 3, 2), WEL2_CATCH_NONE);         /* CNTV_CVAL_EL0 */
    CHECK_EQ(cls(3, 0, 14, 1, 0), WEL2_CATCH_NONE);         /* CNTKCTL_EL1 */
    /* CRn 9 outside the PMU (the statistical profiling / trace buffer registers are other traps) */
    CHECK_EQ(cls(3, 0, 9, 10, 0), WEL2_CATCH_NONE);
    CHECK_EQ(cls(3, 3, 9, 15, 0), WEL2_CATCH_NONE);
}

static void t_classify_debug_impdef_actlr(void)
{
    CHECK_EQ(cls(2, 0, 1, 0, 4), WEL2_CATCH_DEBUG);         /* OSLAR_EL1 */
    CHECK_EQ(cls(2, 0, 1, 1, 4), WEL2_CATCH_DEBUG);         /* OSLSR_EL1 */
    CHECK_EQ(cls(2, 0, 1, 3, 4), WEL2_CATCH_DEBUG);         /* OSDLR_EL1 */
    CHECK_EQ(cls(2, 0, 1, 4, 4), WEL2_CATCH_DEBUG);         /* DBGPRCR_EL1 */
    CHECK_EQ(cls(2, 0, 0, 2, 2), WEL2_CATCH_DEBUG);         /* MDSCR_EL1 */
    CHECK_EQ(cls(2, 3, 0, 1, 0), WEL2_CATCH_DEBUG);         /* MDCCSR_EL0 */
    CHECK_EQ(cls(2, 0, 1, 0, 0), WEL2_CATCH_DEBUG);         /* MDRAR_EL1 */
    for (unsigned n = 0; n < 16; n++)
        for (unsigned o = 4; o < 8; o++)
            CHECK_EQ(cls(2, 0, 0, n, o), WEL2_CATCH_DEBUG); /* DBGBVR/BCR/WVR/WCR<n> */
    CHECK_EQ(cls(2, 0, 7, 8, 6), WEL2_CATCH_DEBUG);         /* DBGCLAIMSET_EL1 */
    CHECK_EQ(cls(3, 0, 1, 0, 1), WEL2_CATCH_ACTLR);         /* ACTLR_EL1 */
    for (unsigned crm = 0; crm < 16; crm++) {
        CHECK_EQ(cls(1, 0, 11, crm, 3), WEL2_CATCH_IMPDEF);     /* SYS / SYSL */
        CHECK_EQ(cls(1, 7, 15, crm, 0), WEL2_CATCH_IMPDEF);
        CHECK_EQ(cls(3, 0, 11, crm, 3), WEL2_CATCH_IMPDEF);
        CHECK_EQ(cls(3, 1, 15, crm, 0), WEL2_CATCH_IMPDEF);
        CHECK_EQ(cls(3, 5, 15, crm, 7), WEL2_CATCH_IMPDEF);
    }
}

static void t_not_owned(void)
{
    CHECK_EQ(cls(3, 0, 12, 12, 0), WEL2_CATCH_NONE);        /* ICC_IAR1_EL1 */
    CHECK_EQ(cls(3, 0, 12, 12, 5), WEL2_CATCH_NONE);        /* ICC_SRE_EL1 */
    CHECK_EQ(cls(3, 0, 12, 11, 5), WEL2_CATCH_NONE);        /* ICC_SGI1R_EL1 */
    CHECK_EQ(cls(3, 0, 4, 6, 0), WEL2_CATCH_NONE);          /* ICC_PMR_EL1 */
    CHECK_EQ(cls(3, 0, 0, 4, 0), WEL2_CATCH_NONE);          /* ID_AA64PFR0_EL1 (TID3) */
    CHECK_EQ(cls(3, 0, 0, 0, 0), WEL2_CATCH_NONE);          /* MIDR_EL1 */
    CHECK_EQ(cls(3, 0, 1, 0, 0), WEL2_CATCH_NONE);          /* SCTLR_EL1 */
    CHECK_EQ(cls(3, 0, 1, 0, 2), WEL2_CATCH_NONE);          /* CPACR_EL1 */
    CHECK_EQ(cls(3, 0, 2, 0, 0), WEL2_CATCH_NONE);          /* TTBR0_EL1 */
    CHECK_EQ(cls(3, 3, 0, 0, 1), WEL2_CATCH_NONE);          /* CTR_EL0 */
    CHECK_EQ(cls(3, 0, 1, 0, 0), WEL2_CATCH_NONE);
    CHECK_EQ(cls(1, 0, 8, 7, 0), WEL2_CATCH_NONE);          /* TLBI */
    CHECK_EQ(cls(1, 3, 7, 5, 1), WEL2_CATCH_NONE);          /* DC */
    CHECK_EQ(cls(1, 0, 12, 0, 0), WEL2_CATCH_NONE);
    CHECK_EQ(cls(0, 0, 0, 0, 0), WEL2_CATCH_NONE);
}

static void t_values(void)
{
    struct wel2_sysreg_enc e;

    CHECK(wel2_catch_decode(iss(2, 0, 1, 1, 4, 0, 1), &e));         /* OSLSR_EL1: OS Lock implemented (OSLM = 0b10), unlocked */
    CHECK_EQ(wel2_catch_value(&e), 8);
    CHECK_EQ(wel2_catch_value(&e) & 2, 0);                          /* OSLK clear */
    CHECK(wel2_catch_decode(iss(3, 3, 9, 12, 0, 0, 1), &e));        /* PMCR_EL0 */
    CHECK_EQ(wel2_catch_value(&e), 0);
    CHECK(wel2_catch_decode(iss(2, 0, 0, 2, 2, 0, 1), &e));         /* MDSCR_EL1 */
    CHECK_EQ(wel2_catch_value(&e), 0);
    CHECK(wel2_catch_decode(iss(3, 0, 1, 0, 1, 0, 1), &e));         /* ACTLR_EL1 */
    CHECK_EQ(wel2_catch_value(&e), 0);
    CHECK(wel2_catch_decode(iss(3, 2, 15, 3, 1, 0, 1), &e));        /* an IMPDEF register */
    CHECK_EQ(wel2_catch_value(&e), 0);
    CHECK(wel2_catch_decode(iss(2, 0, 1, 3, 4, 0, 1), &e));         /* OSDLR_EL1: not locked */
    CHECK_EQ(wel2_catch_value(&e), 0);
}

/* OSLAR_EL1 / OSLSR_EL1: a per-core OS lock bit; SYSL reads 0. */
static void t_oslock(void)
{
    struct wel2_sysreg_enc oslar, oslsr, sysl;
    struct wel2_catch_cpu a = { 0 }, b = { 0 };

    CHECK(wel2_catch_decode(iss(2, 0, 1, 0, 4, 1, 0), &oslar));
    CHECK(wel2_catch_decode(iss(2, 0, 1, 1, 4, 1, 1), &oslsr));
    CHECK(wel2_catch_decode(iss(1, 0, 15, 4, 2, 1, 1), &sysl));
    CHECK_EQ(wel2_catch_read(&a, &oslsr), 8);                       /* clear at start */
    wel2_catch_write(&a, &oslar, 1);                                /* lock */
    CHECK_EQ(wel2_catch_read(&a, &oslsr), 0xa);                     /* OSLM[1] | OSLK */
    CHECK_EQ(wel2_catch_read(&b, &oslsr), 8);                       /* the other core is unaffected */
    wel2_catch_write(&a, &oslar, 0xfffffffeull);                    /* bit 0 clear: unlock */
    CHECK_EQ(wel2_catch_read(&a, &oslsr), 8);
    wel2_catch_write(&a, &oslsr, 1);                                /* a write to another register changes nothing */
    CHECK_EQ(wel2_catch_read(&a, &oslsr), 8);
    CHECK_EQ(wel2_catch_read(&a, &sysl), 0);
    CHECK_EQ(wel2_catch_value(&oslsr), 8);                          /* the stateless value is the unlocked one */
}

static const char *nm(unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2)
{
    static char buf[WEL2_CATCH_NAME_MAX];
    struct wel2_sysreg_enc e;

    wel2_catch_decode(iss(op0, op1, crn, crm, op2, 0, 1), &e);
    wel2_catch_name(&e, buf, sizeof(buf));
    return buf;
}

static void t_names(void)
{
    CHECK(!strcmp(nm(3, 3, 9, 12, 0), "PMCR_EL0"));
    CHECK(!strcmp(nm(3, 3, 9, 13, 0), "PMCCNTR_EL0"));
    CHECK(!strcmp(nm(3, 0, 9, 14, 1), "PMINTENSET_EL1"));
    CHECK(!strcmp(nm(3, 3, 14, 8, 0), "PMEVCNTR0_EL0"));
    CHECK(!strcmp(nm(3, 3, 14, 11, 6), "PMEVCNTR30_EL0"));
    CHECK(!strcmp(nm(3, 3, 14, 12, 3), "PMEVTYPER3_EL0"));
    CHECK(!strcmp(nm(3, 3, 14, 14, 6), "PMEVTYPER22_EL0"));
    CHECK(!strcmp(nm(3, 3, 14, 15, 7), "PMCCFILTR_EL0"));
    CHECK(!strcmp(nm(2, 0, 0, 3, 4), "DBGBVR3_EL1"));
    CHECK(!strcmp(nm(2, 0, 0, 15, 7), "DBGWCR15_EL1"));
    CHECK(!strcmp(nm(2, 0, 1, 0, 4), "OSLAR_EL1"));
    CHECK(!strcmp(nm(2, 0, 1, 1, 4), "OSLSR_EL1"));
    CHECK(!strcmp(nm(3, 0, 1, 0, 1), "ACTLR_EL1"));
    CHECK(!strcmp(nm(3, 0, 15, 3, 1), ""));                         /* an IMPDEF register has no architectural name */
    CHECK(!strcmp(nm(2, 0, 3, 3, 3), ""));
    CHECK(!strcmp(nm(1, 0, 11, 3, 3), "SYS-IMPDEF"));
    CHECK(!strcmp(nm(1, 2, 15, 0, 7), "SYS-IMPDEF"));
    /* a tiny buffer is cut, terminated and never overrun */
    {
        struct wel2_sysreg_enc e;
        char b[6] = "xxxxx";

        wel2_catch_decode(iss(3, 3, 9, 12, 0, 0, 1), &e);
        CHECK_EQ(wel2_catch_name(&e, b, sizeof(b)), 5);
        CHECK(!strcmp(b, "PMCR_"));
        CHECK_EQ(wel2_catch_name(&e, b, 0), 0);
    }
}

static void t_table(void)
{
    static struct wel2_catch_tab t;
    struct wel2_sysreg_enc e, w;
    unsigned idx = 99;

    memset(&t, 0, sizeof(t));
    wel2_catch_decode(iss(3, 3, 9, 12, 0, 1, 1), &e);               /* read PMCR_EL0 */
    wel2_catch_decode(iss(3, 3, 9, 12, 0, 2, 0), &w);               /* write PMCR_EL0: another entry (direction is part of the key) */
    CHECK_EQ(wel2_catch_record(&t, &e, 0x1000, 0, &idx), WEL2_CATCH_REC_NEW);
    CHECK_EQ(idx, 0);
    CHECK_EQ(wel2_catch_record(&t, &e, 0x1004, 1, &idx), WEL2_CATCH_REC_OLD);   /* same encoding, other Rt/PC/core */
    CHECK_EQ(wel2_catch_record(&t, &e, 0x1008, 1, NULL), WEL2_CATCH_REC_OLD);
    CHECK_EQ(wel2_catch_record(&t, &w, 0x1010, 2, &idx), WEL2_CATCH_REC_NEW);
    CHECK_EQ(idx, 1);
    CHECK_EQ(t.n, 2); CHECK_EQ(t.total, 4); CHECK_EQ(t.overflow, 0);
    CHECK_EQ(t.e[0].count, 3); CHECK_EQ(t.e[0].first_pc, 0x1000); CHECK_EQ(t.e[0].core, 0);
    CHECK_EQ(t.e[1].count, 1); CHECK_EQ(t.e[1].first_pc, 0x1010); CHECK_EQ(t.e[1].core, 2);
    /* fill the table with distinct encodings and past it: 2 + 62 fit, then the overflow is logged, each encoding once */
    for (unsigned i = 0; i < 62; i++) {
        struct wel2_sysreg_enc d = { .op0 = 3, .op1 = 3, .crn = 14, .crm = 8 + (i / 8), .op2 = i % 8, .rd = 1 };

        CHECK_EQ(wel2_catch_record(&t, &d, 0x2000 + i, 0, &idx), WEL2_CATCH_REC_NEW);
    }
    CHECK_EQ(t.n, WEL2_CATCH_TAB_N);
    CHECK_EQ(t.overflow, 0);
    /* a known one still counts when the table is full */
    CHECK_EQ(wel2_catch_record(&t, &e, 0x1100, 0, &idx), WEL2_CATCH_REC_OLD);
    CHECK_EQ(t.e[0].count, 4);
    t.total = 0;
    for (unsigned i = 0; i < WEL2_CATCH_OVF_LINES + 10; i++) {
        struct wel2_sysreg_enc d = { .op0 = 2, .op1 = 0, .crn = 0, .crm = i % 16, .op2 = 4 + (i / 16) % 4, .rd = 0 };
        enum wel2_catch_rec r;

        if (i >= 64)                                   /* only 64 distinct exist in this family: reuse with another crn */
            d.crn = 1 + (i - 64);
        r = wel2_catch_record(&t, &d, 0x3000 + i, 0, &idx);
        if (i < WEL2_CATCH_OVF_LINES)
            CHECK_EQ(r, WEL2_CATCH_REC_OVERFLOW);      /* each new distinct encoding past the table is reported ... */
        else if (i == WEL2_CATCH_OVF_LINES)
            CHECK_EQ(r, WEL2_CATCH_REC_OVERFLOW_CAP);  /* ... up to the limit, then once "suppressed" ... */
        else
            CHECK_EQ(r, WEL2_CATCH_REC_OVERFLOW_OLD);  /* ... then silence */
        /* the same encoding again is silent and counted */
        if (i < WEL2_CATCH_OVF_LINES)
            CHECK_EQ(wel2_catch_record(&t, &d, 0x3100, 0, &idx), WEL2_CATCH_REC_OVERFLOW_OLD);
    }
    CHECK_EQ(t.n, WEL2_CATCH_TAB_N);
    CHECK_EQ(t.overflow, 2u * WEL2_CATCH_OVF_LINES + 10u);
    CHECK_EQ(t.total, 2u * WEL2_CATCH_OVF_LINES + 10u);
    CHECK(t.ovf_capped);
}

static void t_format(void)
{
    struct wel2_sysreg_enc e;
    char b[WEL2_CATCH_LINE_MAX];
    static struct wel2_catch_tab t;

    wel2_catch_decode(iss(3, 3, 9, 12, 0, 0, 1), &e);
    CHECK_EQ(wel2_catch_format(b, sizeof(b), "CATCH", &e, 0xfffff0001234abcdull, 3, 1), strlen(b));
    CHECK(!strcmp(b, "CATCH R S3_3_C9_C12_0 PMCR_EL0 PC=0xfffff0001234abcd core=3 count=1"));
    wel2_catch_decode(iss(3, 0, 15, 4, 1, 0, 0), &e);              /* unnamed IMPDEF write: no name field */
    wel2_catch_format(b, sizeof(b), "CATCH", &e, 0x80, 0, 1);
    CHECK(!strcmp(b, "CATCH W S3_0_C15_C4_1 PC=0x80 core=0 count=1"));
    wel2_catch_decode(iss(2, 0, 1, 0, 4, 0, 0), &e);
    wel2_catch_format(b, sizeof(b), "CATCH TOTAL", &e, 0x0, 7, 123456);
    CHECK(!strcmp(b, "CATCH TOTAL W S2_0_C1_C0_4 OSLAR_EL1 PC=0x0 core=7 count=123456"));
    {
        char small[12];

        wel2_catch_format(small, sizeof(small), "CATCH", &e, 1, 0, 1);       /* cut, terminated, no overrun */
        CHECK_EQ(strlen(small), 11);
        CHECK(!strncmp(small, "CATCH W S2_", 11));
    }
    {
        struct wel2_sysreg_enc o = { .op0 = 3, .op1 = 3, .crn = 14, .crm = 9, .op2 = 2, .rd = 0 };

        wel2_catch_format_overflow(b, sizeof(b), &o, 0xfffff00012340000ull, 2);
        CHECK(!strcmp(b, "CATCH W S3_3_C14_C9_2 PMEVCNTR10_EL0 PC=0xfffff00012340000 core=2 count=overflow"));
        o.op0 = 1; o.op1 = 0; o.crn = 15; o.crm = 1; o.op2 = 0;
        wel2_catch_format_overflow(b, sizeof(b), &o, 0x40, 0);
        CHECK(!strcmp(b, "CATCH W S1_0_C15_C1_0 SYS-IMPDEF PC=0x40 core=0 count=overflow"));
        CHECK(!strcmp(WEL2_CATCH_OVF_NOTICE, "CATCH overflow: further lines suppressed"));
    }
    t.n = 3; t.total = 99; t.overflow = 5;
    wel2_catch_format_summary(b, sizeof(b), &t);
    CHECK(!strcmp(b, "CATCH SUMMARY distinct=3 total=99 overflow=5"));
}

static void t_bits(void)
{
    /* the trap bit positions this layer sets (Arm ARM, from memory [hypothesis]: MDCR_EL2 TPMCR 5, TPM 6, TDA 9, TDOSA 10, TDRA 11; HCR_EL2 TIDCP 20, TACR 21) */
    CHECK_EQ(WEL2_MDCR_CATCH, 0x0e60);
    CHECK_EQ(WEL2_HCR_CATCH, 0x300000);
    CHECK_EQ(WEL2_MDCR_CATCH & (1u << 8), 0);                       /* TDE (debug exceptions to EL2) stays clear */
    CHECK_EQ(WEL2_MDCR_CATCH & 0x1f, 0);                            /* HPMN untouched */
}

int main(void)
{
    RUN(t_decode);
    RUN(t_classify_pmu);
    RUN(t_classify_debug_impdef_actlr);
    RUN(t_not_owned);
    RUN(t_values);
    RUN(t_oslock);
    RUN(t_names);
    RUN(t_table);
    RUN(t_format);
    RUN(t_bits);
    TESTS_DONE();
}
