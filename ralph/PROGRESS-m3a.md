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

### 2026-11-11 04:30 - T4 Design: first M1 boot of Watari EL2 via m1n1: done
- Commits: 5f49828, 1b4b30e (`docs/design/watari-el2-m1boot/` README, DESIGN, PLAN, RISKS; `docs/adr/draft-m1-first-boot.md`). Author: Fable. Sources read via `gh api`: m1n1 v1.6.1, Linux v6.12, Asahi docs.
- Key decisions: hand-over through the proxy `kboot_prepare_dt` + `kboot_boot` (EL2, MMU off, x0 = FDT; `chainload.py` rejected); PIE layer in a 32 MB aligned block; minimal j293 DTS filled by m1n1; timer FIQ taken at EL2 and injected as vPPI 27/30; PSCI fully in the layer (spin table `cpu-release-addr` + SEV, watchdog reset, never `smc`); output: framebuffer checkpoints first, UART second; ladder L0-L5 on hardware.
- Independent review (Fable): no blocking issues; claims checked line by line against m1n1/Linux. Fixed in 1b4b30e: KVM on the M1 does not use ICH_HCR_EL2.TC (TDS = 1), so TC is a [hypothesis] and a `wel2.cpuif=trap-notc` fallback is specified; guest results reach the screen through vendor HVCs; timer controls reset before the gate opens; a finer ladder (watchdog-only rung, MMU-off checkpoint, FDT read-back at L0). The revision itself was not re-reviewed (Opus checked it against the review list).
- New facts for Marco and Ciani: m1n1 shuts down the USB proxy link at hand-over, so after the jump the only output is the screen, or the serial kit over the DFU port (macvdmtool, needs `sudo` on Ciani's Mac: people decision, Q-11).
- T6 split into T6a-T6g, T5b added (framebuffer), per PLAN "Task mapping". Riskiest open question: whether guest ICC_* traps behave on the M1 as in QEMU (R-1a/R-1b, settled at rung L3).

### 2026-11-11 04:50 - T5 Apple UART driver in the layer: done
- Commit: 6494639. Console backend `struct wel2_uart` (`el2/vgic/uart.c`, host-built); S5L polled TX: UTRSTAT 0x10 bit 1 TXBE, UTXH 0x20, 32-bit accesses [verified: m1n1 v1.6.1 src/uart_regs.h, uart.c, facts only]; bounded wait with drop counting (a dead UART cannot hang the layer); selected in `stage1_build` (MMU off) by `fdt_find_compatible_reg("apple,s5l-uart")`; stage-2 hole for the UART window (HOLES_MAX 8, S2 pool 32 tables); PL011 path unchanged.
- Review (Sonnet): no blocking defects; offsets checked against m1n1 v1.6.1; no-copy rule respected. Notes carried to T6b: a property-less parent node leaves stale #address-cells/#size-cells; the first s5l node is taken, ignoring `status`, `/chosen/stdout-path` and the `serial0` alias.
- Opus verified: `make -C el2 test` ALL PASSED (test_uart_s5l 8 tests / 64 checks); `run-all.sh` 15/15 both configs; Windows boot TAG=t5 (see next entry if it fails).
- Open: TXBE semantics on the real UART are a [hypothesis] until day 2; the static `g_con` function pointers need relocation in T6a (PIE).

### 2026-11-11 09:30 - T6a Position-independent image: done
- Commits: 17a7aa7, 7b3792c. Layer linked at 0 as a PIE, self-relocating in `start.S` (41 `R_AARCH64_RELATIVE`, any other type parks), `adr_l` instead of every `ldr =sym`, `DC IVAC` over the whole block with the line size from CTR_EL0, run-time `g_layer_base`/`g_carve_base`/`g_carve_size`, `check-pie.sh` after every link, distinct early park codes `0xBAD0000n` in x0 (README), run-time fit check of the layer in its carve-out, per-core IVAC in `secondary_entry` [hypothesis: insurance].
- Review (Fable): no blocking defects (relocation loop, IVAC order and range, secondaries, Image header checked on the disassembly). Fixes applied in 7b3792c.
- Opus verified: `make -C el2 test` ALL PASSED; `run-all.sh` 15/15 both configs; `run-moved.sh` (same binary at 0x40100000) PASS; Windows boot TAG=t6a-t5b desktop reached.
- Open: the run-time layout is still QEMU-shaped (2 MB carve, `ram.base == g_carve_base`): T6c must do the M1 32 MB block. The layer alone must NOT be taken to the Mac yet. The Makefile does not rebuild when only flags change: `make -C el2/boot clean` after a merge that changes flags (one false `make test` failure seen).

### 2026-11-11 09:30 - T5b Framebuffer output: done
- Commits: fe5d8e8, 4281fa4. `/chosen/framebuffer@*` (simple-framebuffer) parsed MMU-off, formats x8r8g8b8 and x2r10g10b10 [verified: m1n1 v1.6.1 kboot.c dt_set_fb], original 5x7 font, text console mirrored from `putc_`/`puts_`, `checkpoint(n)` squares usable with the MMU off, `fatal()` paints the current square red, stage-1 map Normal non-cacheable (MAIR Attr2 0x44), stage-2 hole (the guest never sees the fb).
- Review (Sonnet): no blocking defects; fixes (reg size check, `dsb st`, bounded property names, strict status) in 4281fa4.
- Opus verified: tests and run-all 15/15 both configs after the merge (Makefile/README conflict with T6a resolved by Opus); Windows boot TAG=t6a-t5b desktop reached.
- Open: whether the panel keeps the drawing after m1n1's `fb_shutdown` is a [hypothesis] until day 2; EDK2 GOP later needs an explicit fb hand-over (T11).

### 2026-11-11 09:30 - Stop for today (Marco)
- T6b was started but its agent hit the usage limit before committing anything; it is still `[ ]`. Next: T6b, T6c, T6d, T6e, T6f, T6g, T7, T8, then Part 3/4.
- The Ralph stop hook never fired in the desktop app: the loop was driven by Opus chaining tasks and by session cron wake-ups.

### 2026-11-11 22:15 - T6b FDT queries for the M1: done
- Commits: c5e7a3a, d144527. `el2/boot/fdt_m1.c` `fdt_m1_parse()` -> `struct m1_platform`: cpus (MPIDR, spin-table release address, 0 = none), `/memory` pairs, `/reserved-memory` + `/memreserve/` subtracted into `usable[]`, AIC, UART (stdout-path -> serial0 -> first enabled), watchdog, framebuffer; per-node parent cells with DT defaults; `fdt_find_compatible_reg` rewritten (respects `status`, fixes the T5 cells note); `tools/m3a/fdt_m1_dump` for rung L0 (prints no identifying property); fuzz test. Not linked into the image yet (T6c).
- Review (Sonnet): compatibles and cell shapes checked against Linux v6.12 t8103.dtsi and m1n1 v1.6.1 kboot.c. One medium defect fixed in d144527: an overflowing or wrapping reservation now fails the parse (could otherwise have made m1n1's memory "usable").
- Opus verified: `make -C el2 test` ALL PASSED (test_fdt_m1 11 tests / 14985 checks); `run-all.sh` 15/15 both configs. No Windows boot: the boot path is unchanged.

### 2026-11-11 23:30 - State at the usage limit
- Merged on BRANCH, not ticked: T6f host module (`el2/vgic/cpu_start.c`, commits ccb4ce5 + 0eafb03, Fable review fixes applied; tests + run-all 15/15 both configs). Ticked when T6c wires it.
- In progress in worktrees (resume their agents after the reset): T6c seam + M1 maps (agent worktree `agent-a7aa2e060f60185f7`); T6e Apple FIQ (commit cfaf55a, Fable review D-1..D-3 fixes running in `agent-ac030ac3461b5970d`); T7a EL1 test guest (commit 8657894 in `agent-aba0fb822e096a6de`, run-m1guest 10 PASS / 1 SKIP on QEMU, Fable review running).

### 2026-11-12 02:15 - T10 ACPI tables for the real M1: done
- Commit: c239cb6. `tools/acpigen` model `j293`: MADT (GICD 0xF00000000, 8 GICC E-cores then P-cores matching `wel2_core_index`, MPIDR affinity-only, GICR range 0xF10000000/0x100000, GIC MSI frame 0xFFFFF000 SPI 928/32 cross-checked with `INTR_MAP_T8103`), GTDT (PPIs 27/30), FADT (HW-reduced, PSCI over HVC), PPTT (E and P clusters); no DBG2/SPCR on purpose (framebuffer only; README lists the PL011-emulation option for later).
- Review (Sonnet): no blocking defects; field layouts checked against ACPI 6.5 (from memory, marked). Open for T11/M3b: the FADT points at no DSDT yet (Windows needs one), PPI 30 is not injected yet, ALWAYS_ON only on the physical timer, a GICv2m frame may be ignored by Windows (ITS path).
- Opus verified: `make -C tools/acpigen test` TUTTI I TEST OK; `make -C el2 test` ALL PASSED.

### 2026-11-12 02:15 - T6c, T7a merged (ticked after the merged Windows boot)
- T6c commits bee92f9, b2b812b (Fable review: QEMU refactor pure; fixed M1 null console, 36-bit guest IPA on the M1, FDT RO mapping, a test bug). T7a commits 8657894, 7ca9980 (Fable review: storm bounds, no MMIO without positive QEMU evidence, real PAuth check, G5 counters, reset on fatal). After the merge: `make -C el2 test` ALL PASSED, `run-all.sh` 17/17 both configs (now incl. run-m1guest), run-moved PASS, run-m1guest G1-G11: 10 PASS, G8 SKIP.
- F-31: the agents' QEMU wait loop `pgrep -f "qemu-system.*win11.qcow2"` matches its own shell and never exits; use `pgrep -x qemu-system-aarch64`.
