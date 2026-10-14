# Phase 0: tasks for Ralph

This list is edited only by Ralph, on the `ralph/f0-gate-a` branch. Proceed in order: take the first task that is not ticked. Third round: A1, A2 and A4 in parallel, then A11 and finally A7 (which depend on them).
Format: `- [ ]` to do, `- [x]` done and verified by Opus, `- [!]` blocked (reason in `ralph/PROGRESS-phase0.md`), `- [→]` assigned to Ciani: **Ralph does not touch it**.

## Gate A: no downloads, no installations (can be done RIGHT NOW)
Allowed tools: web reading (WebSearch and WebFetch), read-only `gh api`, writing files in the repo, and the tools already installed (`clang`, `make`, `python3 -I`, `git`).

- [x] *(issue #4, reassigned to Ralph)* **A1. HAL extensions: Microsoft public documentation.**
  What they are, how they are loaded (CSRT table, resource IDs), which interfaces exist (timer, DMA, *interrupt controller?*), signing and test-signing requirements.
  Allowed sources: Microsoft Learn, CSRT specification, WDK docs, HLK.
  Output: `re/f0-halext-public-docs.md`.
  Completion criterion: every claim has a source. The question *"can a HAL extension provide an interrupt controller?"* has a [verified] answer or is declared unresolved by public documentation.
- [x] *(issue #5, reassigned to Ralph)* **A2. Windows ARM64 ACPI requirements.**
  Mandatory tables and minimum fields: MADT/GICC, GTDT, FADT, DBG2, SPCR, PPTT. BSA/SBSA requirements that Windows imposes. Microsoft's specification of the *Multiprocessor Parking Protocol* (version, mailbox layout, how a core is woken).
  Output: `re/f0-acpi-windows-requirements.md`.
- [x] *(issue #6)* **A3. M1: FIQ sources and IRQ alternatives.**
  From the Asahi code (`irq-apple-aic.c`, m1n1 `aic.c`/`hv_*`, `arch_timer`): list of all the FIQ sources of the t8103 (physical and virtual timer, Fast IPI, PMU, uncore). For each: can it be disabled or routed to IRQ? Are there alternative MMIO timers on the SoC? How do the AIC IPIs work in IRQ?
  Output: `re/f0-m1-fiq-irq.md`, citing the Asahi files and lines.
- [x] *(issue #7, reassigned to Ralph)* **A4. The Raspberry Pi 3 precedent (no GIC).**
  How Windows (IoT Core / ARM64) ran on the BCM2836/7 without a GIC, reconstructed from public sources: `ms-iot/rpi-iotcore`, `raspberrypi/windows-drivers`, `pftf/RPi3`, `worproject`, forums, published ACPI tables (the Pi 3's MADT/CSRT).
  Output: `re/f0-rpi3-precedent.md`.
  Completion criterion: it is clear, with sources, whether a HAL extension for the interrupt controller existed, or it is declared undeterminable.
- [x] *(issue #8)* **A5. Design of the experiment in QEMU (Gate B).**
  Matrix of the ACPI variants to try: (1) baseline with GIC; (2) MADT without GICD; (3) GICC with the parking protocol; (4) CSRT with a resource group of our own. For each variant: what is observed (bugcheck code, debugger output, whether the extension is loaded) and what is concluded. Include the debug setup (KDNET/serial) and the environment to build a test HAL extension (WDK in a Windows ARM VM, only as a development tool).
  Output: `docs/design/watariboot/EXPERIMENT-F0.md`.
  If the files of A1, A2 and A4 (Ciani's) are not yet on `main`, write the experiment based on `docs/PLAN.md` and on `docs/design/watariboot/INTERFACES.md`, and mark as [hypothesis] the points that depend on them.
- [x] *(issue #9)* **A6. `tools/acpigen`: host-side ACPI table generator, with tests.**
  C11 library + CLI that, starting from a data model (C or JSON), produces MADT (GICC with the parking protocol fields), GTDT, PPTT and CSRT in binary, with correct checksums.
  - Unit tests built with `clang` + `make`, already on the system: layout and field offsets verified against the specification, checksum, edge cases.
  - t8103 data (cluster, MPIDR) taken from Asahi's DTS via `gh api` and cited.
  - SPDX header on every file.
  Completion criterion: `make -C tools/acpigen test` green.
- [x] *(issue #10)* **A7. Preliminary verdict.** **Prerequisite:** `re/f0-halext-public-docs.md`, `re/f0-acpi-windows-requirements.md` and `re/f0-rpi3-precedent.md` must exist in the current branch (after A1, A2 and A4). If missing, mark `[!]` with the reason "waiting for A1/A2/A4" and write nothing.
  Synthesis of A1–A4 in `re/f0-preliminary-verdict.md`: what can already be concluded, what remains to be shown only through the A5 experiment, with what probabilities. ADR proposal in `docs/adr/draft-phase0-preliminary-outcome.md`.

## Gate A, second round: the FIQ timer leads (after A3)
Same rules as Gate A: no downloads and no installations.

- [x] *(issue #16)* **A8. An AIC model in QEMU: does one already exist?**
  Search for and evaluate the public QEMU forks that emulate an Apple SoC with AIC (for example projects that emulate iPhones such as t8030). For each: repository, license, last commit, how much of the AIC they emulate (IRQ, IPI, timer FIQ), base QEMU version, possibility of grafting the AIC onto the aarch64 `virt` machine in place of the GIC.
  Sources: `gh api` and web.
  Output: `re/f0-qemu-aic-models.md`, with a recommendation: reuse, take only the AIC, or write from scratch, and an estimate of the work.
- [x] *(issue #17)* **A9. The "CONVERT_SIQ_TO_IRQ" bit and other hardware routes for the timer as IRQ.**
  Where it is defined in m1n1 (file and lines), whether it appears in Apple's open source code (XNU, repo `apple-oss-distributions/xnu`, read-only via `gh api`) or in other public code (Asahi, Corellium, documentation). Also look for other Apple IMP-DEF mechanisms to route or virtualize the timer (for example the registers XNU uses for the timer).
  Output: `re/f0-timer-irq-hw.md`, with the protocol of a **hardware test** to be done with the m1n1 proxy (HW-1, issue #20): registers to write, what to observe and how to safely go back.
- [x] *(issue #18)* **A10. Experiment V6: Windows with the timer delivered as FIQ.**
  Add the variant V6 to `docs/design/watariboot/EXPERIMENT-F0.md`. In QEMU `virt`, how to configure the timer PPI as Group 0 / FIQ (GICv3 with security, or the bit in the GTDT or in the emulation), with sources from the QEMU, Arm and Linux documentation. What to observe: Windows handles the FIQ, bugchecks or hangs. What to conclude for each outcome.
  Modify ONLY the new V6 section and the matrix table.

- [x] *(issue #19, reassigned to Ralph)* **A11. Design of the timer HAL extension based on the watchdog.**
  Depends on A1. The watchdog `23d2b0000` (AIC IRQ 338, 24 MHz, 32-bit counter; see `re/f0-m1-fiq-irq.md`) as the system clock for Windows through a timer HAL extension, with distribution to the other cores via IPI.
  Output: `docs/design/watariboot/HALEXT-TIMER-WDT.md`.

## Gate B: requires a confirmed backup + at least 100 GB free + human approval (Ralph does NOT execute them)
- [x] B1. Install qemu, acpica-tools and the EDK2 dependencies. [HUMAN confirms]
- [x] B2. Build of EDK2 `ArmVirtQemu` and boot up to the UEFI shell in QEMU.
- [ ] B3. Official Windows 11 ARM64 ISO from the Microsoft site. [HUMAN: download from the browser]
- [ ] B4. Baseline: Windows ARM64 booted in QEMU with our EDK2 and the `acpigen` tables.
- [ ] B5. The variants of the A5 matrix, with the evidence collected.
- [ ] B6. Windows ARM development VM with Visual Studio + WDK. Test HAL extension, test-signed. [HUMAN: setup and licenses]
