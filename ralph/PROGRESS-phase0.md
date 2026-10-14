# Phase 0: Ralph's diary

Ralph adds an entry for each task: date, task, files produced, what Opus verified, open doubts.

## Entries

### 2026-10-12: A3 completed (issue #6)
- File: `re/f0-m1-fiq-irq.md` (Asahi linux@asahi 77cb8f24c2, m1n1 main fbec52a81f).
- **Verified by Opus** on the DTS `t8103.dtsi@asahi`:
  - (1) the `arm,armv8-timer` timer has all 4 interrupts as `AIC_FIQ` (lines 369-375);
  - (2) the watchdog `watchdog@23d2b0000` has `AIC_IRQ 338` (lines 1223-1228).
- **Critical discovery:** the architectural timer is **FIQ only**, and there is no documented way to route it to IRQ. All the other FIQ sources (Fast IPI, PMU, uncore) can be turned off at the source. If Windows requires the architectural timer, the "no FIQ" strategy (ADR-0004) falls. Leads to verify:
  - (a) the `ACC_OVRD` bit "CONVERT_SIQ_TO_IRQ", defined in m1n1 but never used [hypothesis];
  - (b) the watchdog as the system timer through a timer HAL extension: a single comparator for the whole SoC, a 32-bit counter that wraps in about 179 s [hypothesis].
- **IPI:** the AIC's legacy ones in IRQ work on AICv1 (historic Linux code), but **AICv2 (M1 Pro/Max and later) does not have them**, so the strategy does not hold for all Apple Silicon.
- **Open doubts:** discrepancies on the IPI_RR fields between Linux and the Asahi doc; no latency measurement.

### 2026-10-12: A6 completed (issue #9)
- File: `tools/acpigen/` (C11 library + CLI + tests). Generates MADT (GICC/GICD), GTDT, PPTT and CSRT.
- **Verified by Opus:** `make -C tools/acpigen test` → `TUTTI I TEST OK` (re-run); 82-byte GICC consistent with ACPI 6.5.
- **Hypotheses** (marked in the sources): parked address/version = 0, efficiency classes, timer GSIVs (PPI placeholders), CSRT ID "APLE"/0x8103, MPIDR = DTS `reg`.
- **Note:** the CSRT layout comes from the PDF of the Microsoft specification (link from learn.microsoft.com), read by the agent. No copy of the PDF was left in the repository.

### 2026-10-12: A5 completed (issue #8)
- File: `docs/design/watariboot/EXPERIMENT-F0.md` (matrix V0–V5, debug, prerequisites, risks).
- **Verified by Opus** on the QEMU documentation:
  - (1) `gic-version` accepts only 2/3/4/x-5/host/max: the GIC cannot be removed, and "non-GIC" can only be simulated in the ACPI tables (qemu.org, system/arm/virt);
  - (2) `-acpitable` applies only to the i386 target (qemu.org, system/invocation). Our tables must be injected with a DXE driver in EDK2.
- **Consequences:**
  - `acpigen` must be extended (`qemu-virt` model, `--no-gicd`, `--parking`, GICR, FADT, SSDT): new work for Gate B;
  - the estimated space for Gate B is 75–115 GiB (today about 53–56 are free);
  - QEMU gives a strong negative verdict but only a weak positive one: it proves nothing about the AIC, FIQ or the real wake-up of the M1.
- **Open hypotheses:** syntax of `-serial`/`-chardev`/`-netdev`, and whether QEMU's `e1000e` is compatible with KDNET.

### 2026-10-12: A7 blocked (issue #10)
- Reason: **waiting for Ciani**. `re/f0-halext-public-docs.md`, `re/f0-acpi-windows-requirements.md` and `re/f0-rpi3-precedent.md` are not yet on `main` (checked on `origin/main`).
- Resume in a new session or with a new loop, after the merge of Ciani's PRs.

## Final summary of Gate A (this Ralph round)
- Completed: **A3** (#6), **A5** (#8), **A6** (#9).
- Blocked: **A7** (#10), waiting for A1, A2 and A4 (Ciani: #4, #5, #7).
- **Main discoveries:**
  1. The M1's architectural timer is FIQ only. It is a critical risk for ADR-0004 and must be verified with V5 in QEMU and with A1 (timer HAL extensions).
  2. In QEMU the GIC cannot be removed: the experiments give only a reliable "no", not a "yes".
  3. Gate B needs 75–115 GiB and the extensions to `acpigen` (qemu-virt model, FADT/SSDT, `--no-gicd`, `--parking`).

## Second round (branch `ralph/f0-gate-a2`, from 2026-10-13)
- Tasks: A8 (#16), A9 (#17), A10 (#18). A7 stays `[!]` waiting for Ciani.

### 2026-10-13: A8 completed (#16), AIC models in QEMU
- File: `re/f0-qemu-aic-models.md`.
- **Verified by Opus:**
  - `TrungNguyen1909/qemu-t8030` exists (archived, 2204 stars, last push 2022-10). Its `hw/intc/apple_aic.c` uses the AICv1 offsets (0x2004 IACK, 0x3000 DEST, 0x4100 MASK_SET).
  - `ChefKissInc/Inferno` is active (push 2026-09).
  - The t8030 repo's license shows as "NOASSERTION" on GitHub.
- **Recommendation:** write from scratch a minimal AICv1 model (250–350 lines) for a QEMU "watari-virt" machine without a GIC, with the timer wired directly to FIQ as on the real M1. Estimate: about 8–9 days [hypothesis]. Inferno is not reused (its AIC file is AGPL).
- **Note on the rules:** the agent saved a copy of the Linux source `aic.c` in the session's scratchpad. It is public GPL text and the backup was confirmed, but it goes against the rule "no downloads without confirmation". Reported to the user.

### 2026-10-13: A9 completed (#17), the SIQ bit and the hardware routes
- File: `re/f0-timer-irq-hw.md`, with the HW-1 protocol (#20).
- **Verified by Opus:**
  - `ACC_OVRD_CONVERT_SIQ_TO_IRQ` = BIT(22) of `SYS_IMP_APL_ACC_OVRD` sys_reg(3,5,15,6,0) in m1n1 `src/cpu_regs.h:541,563`;
  - in the AsahiLinux organization the only file that contains it is `src/cpu_regs.h` (code search), so it is not used anywhere;
  - XNU has `osfmk/arm/rtclock.c`.
- **Conclusions:** macOS itself uses the timer as FIQ (fleh_fiq → rtclock_intr) and does not touch the bit. The probability that HW-1 works is low (about 10%, the agent's estimate), but the test costs little and is easily undone. The watchdog remains the most concrete IRQ lead.

### 2026-10-13: A10 completed (#18), variant V6 in QEMU
- Section 4.4 added to `EXPERIMENT-F0.md`.
- **Verified by Opus:** in QEMU `hw/intc/arm_gicv3_cpuif.c` Group 0 (`GICV3_G0`) has a dedicated path (FIQ).
- **Method:**
  - first attempt: a DXE driver in our EDK2 that puts the timer PPIs in Group 0;
  - reference method: a patch of about 10 lines to QEMU `virt.c` that connects the timer output directly to `ARM_CPU_FIQ`, as on the M1.
  With HVF, `kernel-irqchip=off` must be forced [hypothesis].
- **Decisive outcome:** if Windows boots and the clock advances with the timer as FIQ, the timer problem is solved.

## Summary of the second round
- Completed: A8, A9, A10. A7 stays blocked (waiting for Ciani).
- **The timer picture:**
  1. Hardware route: unlikely (HW-1, about 10%).
  2. **Windows handling the FIQ (V6): the most important lead**, testable in QEMU with a small patch.
  3. Watchdog + timer HAL extension: concrete, but depends on A1/A11 (Ciani).
  4. Test bench without a GIC: AICv1 model in QEMU, to be written from scratch (about 8–9 days).

## Third round (branch `ralph/f0-gate-a3`, 2026-10-13)
- Marco reassigned Ciani's tasks (A1, A2, A4, A11) to Ralph so as not to block Phase 0. Order: A1, A2 and A4 in parallel → A11 → A7.

### 2026-10-13: A1 completed (#4), HAL extensions
- File: `re/f0-halext-public-docs.md`.
- **Verdict: "not determinable from public sources"** (about 30% yes / 20% no / 50% uncertain).
- **Verified by Opus:** the only public example of HAL extensions (`ms-iot/imx-iotcore@public_preview hals/halext`) contains only `HalExtiMX6Timers`, `HalExtiMX7Timers` and `HalExtiMXDma`, i.e. timer and DMA, no interrupt controller.
- The CSRT specification provides the "Interrupt Controller" subtype, but there is no public example that uses it.
- **Positive note:** HAL extensions for **timers** exist (i.MX), a concrete basis for A11.

### 2026-10-13: A2 completed (#5), ACPI requirements and parking protocol
- File: `re/f0-acpi-windows-requirements.md`.
- Mandatory tables: FADT (HW_REDUCED), MADT, DSDT, DBG2. The GICD is not declared mandatory by Microsoft.
- Parking protocol v1 (2014): 4 KB mailbox; wake-up with "an interrupt" unspecified; a WFE/polling loop is compatible [hypothesis].
- **Blocking discrepancies in `acpigen`:** parking version/address = 0 (must be set to 1 + an address); no placeholder GICD; the boot CPU must be first; GICR base = 1 as in the Pi3; FADT, DBG2, SPCR and DSDT are missing.
- **Note on the rules:** WebFetch saved the specification PDF in Claude's internal tool-results folder, not in the repository.

### 2026-10-13: A4 completed (#7), the Raspberry Pi 3 precedent (**critical result**)
- File: `re/f0-rpi3-precedent.md`.
- **Verified by Opus:** in `tianocore/edk2-platforms` `Platform/RaspberryPi/AcpiTables/Madt.aslc:51-52` there is the comment: "Use 1 for GICR Base Address below, since Windows 10 on Raspberry Pi 3 does not boot otherwise, and this is the value that Microsoft had in their IoT blobs."
- **Conclusion:** on the Pi 3 **no third party** provided the interrupt controller. The BCM2836 support was **inside Windows' HAL** (and the first ARM64 builds even needed a patch). The precedent therefore **does not prove** that a HAL extension can provide an interrupt controller.
- The ARM64 Pi 3 started the cores with PSCI (through ATF), not with the parking protocol.

### 2026-10-13: A11 completed (#19), timer HAL extension on the watchdog
- File: `docs/design/watariboot/HALEXT-TIMER-WDT.md`.
- **Verified by Opus:** `ms-iot/imx-iotcore@public_preview hals/halext/HalExtiMX7Timers` exists (`HalExtension.c`, `.inf`, `.def`).
- The timer interface observed in the i.MX example: `RegisterResourceDescriptor` + `TIMER_INITIALIZATION_BLOCK` + 5 callbacks. The interrupt's GSI is delivered by a controller that must be provided separately.
- **Critical dependency:** the watchdog's IRQ 338 belongs to the AIC. Without a non-GIC controller recognized by Windows the timer never fires. WD0's "bark" has never been used in public code (hardware test H1).
- **NEW LEAD (verified by Opus):** `Wack0/NtHalExts` (Unlicense, push 2024-09, "Windows 8.x HAL extensions") contains **`halextbcm2709`**, a third-party HAL extension for the BCM2709 interrupt controller (Raspberry Pi 2, **without GIC**) on Windows 8.x ARM32. **If confirmed, it is proof that an interrupt controller can be provided by a HAL extension** (at least on Windows 8 ARM32). To be studied first. Warning: it includes Microsoft DDK headers, so it is read without copying anything.

### 2026-10-13: A7 completed (#10), preliminary verdict
- File: `re/f0-preliminary-verdict.md`, `docs/adr/draft-phase0-preliminary-outcome.md`.
- Estimates (interrupts, timer and cores only): pure native **about 12%**, Plan B EL2 about 53%, other about 35%.
- **Opus's note:** the estimates were made **before** the discovery of `halextbcm2709`, which raises the probability of the branch "controller registrable via HAL extension" (estimated at 20% in A7). They must be recalculated after studying it.
- Recommendation: low-cost Gate B with priority on V4 and V6; read the WDK header `nthalext.h` (the user's OK is needed for the download); Plan B threshold decided by the user.

## Summary of the third round
- Gate A **complete**: A1–A11 all `[x]`.
- Next step: study `Wack0/NtHalExts/halextbcm2709`, then Gate B.
