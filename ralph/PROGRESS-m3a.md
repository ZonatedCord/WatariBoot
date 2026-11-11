# M3a night: progress log

Written by Ralph (one entry per task: date and time, task, commits, what Opus verified, open doubts).

## Log

### 2026-11-10 16:25 - T1 EL2 stack per core on the M1: done
- Commits: e788418 (dense core index `Aff1*4 + Aff0`, valid only if Aff0 < 4, Aff1 < 2, Aff2 == Aff1, Aff3 == 0; out-of-range cores park without a stack and leave their MPIDR in `g_bad_mpidr`; PSCI CPU_ON refuses invalid targets), a69c227 (review fixes: marker in its own 128 B slot with `dc ivac` on the reader side, reported once at the start of CPU_ON; stack geometry in one header `el2/include/wel2_coreidx.h` with `_Static_assert` and a linker `ASSERT`; Makefile header deps; exhaustive host test).
- M1 mapping: E-cores 0x80000000..3 -> slots 0..3, P-cores 0x80010100..3 -> slots 4..7 [verified: re/m3a-ehw0-measurements.md, el2/tests/test_core_index.c].
- Independent review (Fable): no blocking defects; asm disassembled and checked against the C mirror.
- Opus verified: `make -C el2 test` ALL PASSED (test_core_index 6 tests, 4147 checks); `run-all.sh` 15/15 default and 15/15 with `wel2.e2h=0 wel2.gran=4k`; Windows boot `run-m2.sh` (trap, gicv2m, TAG=t1) reached the desktop after login (shot-0015).
- Open doubts: (1) the `bad_core` park path and the Aff1 = 1 branch never run in QEMU (`-smp 4` gives Aff1 = 0): first exercised on the M1. (2) The boot-core `reset` path parks any core whose Aff2:Aff0 is nonzero and always uses slot 0: fine if m1n1 starts us on E-core 0 (day 1: boot core MPIDR 0x80000000), to revisit in T4/T6. (3) The PSCI/vGIC cpu number still comes from `cpu_from_mpidr()` (redistributor discovery order), not from the stack index; T6 must make them agree on the M1. (4) QEMU with more than 4 cores per cluster is now refused (no script uses more than `-smp 4`). (5) The 128 B M1 cache line size is [hypothesis].

### 2026-11-11 03:45 - T2 AIC backend after Q-D5 and the ADT: done
- Commits: 2f96af0, bd41b7f. (a) SW_CLR at take time in `pirq_aic_poll()` only for bits the layer set (`sw_owned[]`, atomic; SW_SET written before the owner bit so no SW bit is ever stranded); fake AIC default `event_clears_sw = false` [verified: Q-D5]. (b) AIC windows 0x23b100000/0xc000 + 0x23b108000/0x1000 (inside the first) via new bounded `wel2_s2_add_hole()`. (c) t8103 denied list with `INTR_TRIG_UNKNOWN` (no trigger measured): DWC3 778-780/736, 858-860/816, uart2 607, TB 770/850, plus ANS 583-586, piodma 772/852, uart0 605; allow rows with an unknown trigger rejected.
- Review (Sonnet): no blocking defects; all numbers checked against the measurements; fixes applied in bd41b7f.
- Opus verified: `make -C el2 test` ALL PASSED; `run-all.sh` 15/15 in both configs; Windows boot (TAG=t2t3, together with T3) reached the desktop. The first T2 boot was killed by an outside SIGTERM (F-30).
- Open: the real M1 AIC hole is not wired in the boot path yet (`TODO(T6)` in main.c); `pirq_aic_sw_set` has no production caller yet.

### 2026-11-11 03:45 - T3 GICv2m frame in the 16 KB page (#74): done
- Commit: b57e405. The guest stage-2 hole is the whole page 0xFFFFC000-0xFFFFFFFF in both granules; v2m registers at page offset 0x3000, the rest RAZ/WI (first 8 logged); `v2m_page_free()` skips the frame when guest RAM covers the page (QEMU `-m 4G`). ARCHITECTURE.md records the decision with the E-HW0 evidence.
- Review (Sonnet): no blocking defects. Notes: ISV=0 accesses are still fatal (as for GICD/GICR); the frame is strict on access size; the pool 928/32 is hard-coded (T6 may derive it from the map); hole array cap 6 is exactly full with the real AIC hole.
- Opus verified: `make -C el2 test` ALL PASSED; `run-all.sh` 15/15 both configs; Windows boot TAG=t2t3 desktop reached (on QEMU the frame path is inert because of `-m 4G`).
