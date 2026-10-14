# TEST_STRATEGY

- Project: Watari Boot · Date: 2026-10-12 · Version: 0.1 · Status: draft
- Related documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [RISKS](RISKS.md)
- Marking: `[verified: source]` / `[hypothesis]`.

## 1. Levels (pyramid)
1. **Unit on macOS host**: pure logic (ADT parser, ACPI generator, table checksums, mailbox layout, AIC logic with simulated registers, DART/SART with a memory model). Compiled as C/C++ on macOS, without firmware or Windows.
2. **Table validation**: the generated ACPI tables are recompiled/decompiled and checked with standard ACPI tools (e.g. `iasl`) [hypothesis: suitable tool, to be pinned], checksums, lengths, cross-references.
3. **QEMU** (Phase 1): EDK2 firmware with a GIC-less MADT, CSRT and parking protocol; Windows ARM64 with kernel debugging. [hypothesis] QEMU does not emulate AIC: a test device (dummy controller) or stub will be needed; QEMU limitations to be documented. QEMU is only a test bench (CLAUDE.md).
4. **Hardware** (Phases 2-5): guided manual tests with evidence (photos, video, debug host logs).
5. **Benchmark** (Phase 5): Geekbench/Cinebench, comparison with macOS.

Hardware tests are the most expensive and risky: every step on the disk requires a backup and explicit confirmation (CLAUDE.md, NFR-5).

## 2. Reproducibility
- Pinned toolchains (EDK2, compiler, WDK, QEMU, Ghidra versions) recorded in `tools/` with hashes; builds in scripts/containers; no dependency on a "developer's machine".
- Microsoft inputs (ntoskrnl, PDB, ISO) outside the repo in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (external disk; it must be connected); in the repo only file name, version, SHA-256 [proposal].
- Fixed seeds for any test with randomness; ADT fixture: t8103/j293 ADT blob captured from the device and stored [hypothesis: license/privacy of the blob to be assessed; if not committable, a synthetic generator].
- Repeated outcome: every exit criterion requires N consecutive repetitions (N in the PRD).

## 3. Tests per phase

### Phase 0 (Mac only)
| What | How | Output |
|---|---|---|
| Yes/no verdict | Ghidra analysis of ntoskrnl/HAL with public PDBs | Report in `re/` with addresses/functions/pseudocode for every conclusion |
| Review | Opus checks that every claim has evidence | No conclusion without a reference |
| Mandatory questions | Non-GIC controller registration; loading via CSRT; FIQ handling; BCM2836 case; is a GIC-less MADT allowed?; how Windows sends the parking boot IPI | Each with a yes/no/undeterminable answer |
Exit criterion: document complete; decision to proceed or to adopt plan B taken by the user.

### Phase 1 (QEMU)
- Unit: M-ACPI-GEN with synthetic ADT; tables validated.
- Integration: boot Windows ARM64 in QEMU with kernel debugging (KDNET or serial) [hypothesis: transport works in QEMU].
- Criterion: WinDbg logs showing our extension loaded and invoked (breakpoint). Repeated 3 times. If Windows does not reach that point: collect bugcheck, analysis, return to Phase 0.

### Phase 2 (hardware)
- Unit: ADT parser with the real j293 ADT; mailbox layout.
- Hardware: m1n1 -> EDK2 -> UEFI shell; command that lists the cores; verify that macOS reboots.
- Criterion: UEFI shell with 8 cores visible; 3 boots; macOS bootable.
- Prerequisite: debug host present (Pi 5).

### Phase 3
- Unit: AIC logic (mask/unmask/IPI) with simulated registers; invariants: no access to FIQ registers.
- QEMU: Phase 1 regression.
- Hardware: boot from USB; serial/framebuffer; measure where the boot stops (bugcheck codes recorded).
- Criterion: native kernel up to driver loading, 3 boots out of 3.
- Edge cases: spurious FIQ (counter + log), timer that does not arrive (watchdog), core that does not wake from parking, interrupt storm.

### Phase 4
- Unit/host: ANS miniport with a queue model; SART/DART with simulated memory; edge cases (sizes that are not multiples of 16K, alignment).
- Hardware: installation on SSD, 5 reboots, file integrity (hashes of test files), I/O stress.
- Criterion: desktop with USB keyboard/mouse; installation from SSD.
- Data risk: use only the dedicated Asahi partition; backup and confirmation.

### Phase 5
- PEP/`_CPC`: verify that frequency and load are consistent with the counters; thermal tests; repeated sleep/resume (N cycles).
- SPI HID: input event recording; loss detection.
- Benchmark: Geekbench/Cinebench, at least 3 repetitions, temperature and power recorded, comparison with macOS.

### Phase 6
- Data-driven verification: same code, ADT of another SoC; diff showing only new data.

## 4. TDD: first test
First unit test (Phase 1): given a synthetic `ADT` with 8 CPUs (4 P, 4 E), `AcpiGenBuildAll()` produces a MADT with 8 valid GICC entries (correct checksum, unique UIDs, aligned parked addresses) and no GICD structure. The test must fail before the implementation.

## 5. General exit criteria
A phase is closed only if: the PRD criterion is met with evidence archived in `docs/sessions/`; no new unmitigated red risk; STATUS updated; macOS still bootable (from Phase 2).
