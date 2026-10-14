# F0 / A7: preliminary verdict of Phase 0 (issue #10)

Date: 2026-10-13. Author: Sonnet executor (task A7), to be verified by Opus. Synthesis of the evidence from A1-A6, A8-A10 and of the log `ralph/PROGRESS-phase0.md`. **No new facts were collected**: this document weighs the evidence already present. No Microsoft binary or code was used.

Legend: **[V: file]** = stated and verified in the cited file (which in turn cites the primary source). **[H]** = hypothesis or my own judgment, not verified. The percentages are subjective judgments, not measurements.

Files cited by abbreviation: A1 = `re/f0-halext-public-docs.md`; A2 = `re/f0-acpi-windows-requirements.md`; A3 = `re/f0-m1-fiq-irq.md`; A4 = `re/f0-rpi3-precedent.md`; A5/A10 = `docs/design/watariboot/EXPERIMENT-F0.md`; A6 = `tools/acpigen/README.md`; A8 = `re/f0-qemu-aic-models.md`; A9 = `re/f0-timer-irq-hw.md`; PROG = `ralph/PROGRESS-phase0.md`.

## 0. Summary in five lines

1. No evidence collected shows that Windows ARM64 accepts a non-GIC interrupt controller supplied by a **third party**. The only non-GIC precedent (Pi 3) was code **inside the Microsoft HAL**.
2. No evidence shows the opposite either: the CSRT specification provides for the "Interrupt Controller" type, but there is no public example and the registration interface is not known.
3. The M1 timer is FIQ-only; the four avenues to avoid it are all unproven.
4. Gate B in QEMU (with the GIC always present) can give a reliable "no", but only a weak "yes". The definitive "yes" comes only from the hardware (Phase 3).
5. Estimate: pure native about 12%, plan B (EL2) about 53%, other/failure about 35%. Recommendation: proceed with Gate B at low cost, prioritizing V4 and V6, and fix the plan B threshold in advance (the user decides).

## 1. The critical questions

| # | Question | Answer given the evidence | Evidence | Confidence |
|---|---|---|---|---|
| 1 | Can a HAL extension register a non-GIC interrupt controller? | **Not determinable.** On paper the CSRT type "Interrupt Controller" (type 0x0001, subtype 0x0001) exists. But the only public examples of extensions are timer and DMA (i.MX); in the timer the GSI is delivered by a controller that Windows already knows. No controller registration function has been seen (the `nthalext.h` header has not been read). Signing with a "special EKU" is required by the HCK 8.1 documentation; the effect of test-signing is unknown. The Pi 3 precedent does not prove it (see sec. 2). P(yes) about 20% | A1 sec. 1, 2, 3, 5; A4 verdict; A2 sec. 1 (CSRT) | **Low** (the estimate is wide: 10-30%) |
| 2 | Can the MADT have a GICC without a GICD? | **Probable at the parsing level, unknown at the boot level.** Microsoft does not declare the GICD mandatory. The Pi 3 has a GICC without a GICD and Windows boots. But on the Pi 3 the "GICR base" field had to be 1 ("Windows 10 on Raspberry Pi 3 does not boot otherwise"), a sign that the HAL **chooses the controller driver from the MADT fields** [H: which field is decisive]. An unknown controller (AIC) with a MADT without a GICD might not be recognized. `acpigen` today emits a placeholder GICD with base 0: it must be removed | A2 sec. 2, 5 (rows 6, 7); A4 sec. 1 | **Medium** for "parsing does not reject" (about 60%); **low** for "Windows proceeds in a useful way" |
| 3 | Does the parking protocol work without a GIC? | **Compatible with the specification, not proven on Windows.** The 2014 specification does not forbid polling; a WFE loop on the mailbox is conformant [H, supported by U-Boot]. The "wake" interrupt is not specified and for Windows is decided by the HAL, so it goes through the controller driver (depends on question 1). No evidence that modern Windows ARM64 still uses parking; the Pi 3 used PSCI via ATF, the MS-IoT blobs declared MPPP only for ARM32. Core 0 must be the first GICC. Blocking discrepancies in `acpigen` (parking version and address at 0) | A2 sec. 3, 4, 5; A4 sec. 4; EXPERIMENT V3 | **Low-medium**: about 40% that Windows uses parking without a GIC. Fallback [H]: PSCI with HVC conduit served by a small resident stub at EL2 (the M1 has no EL3): still to be evaluated with the user |
| 4 | Does the timer work without FIQ? | **Not proven; four avenues, none closed.** The t8103 architectural timer is FIQ-only in the DT and in the Asahi documentation; XNU itself uses it in FIQ and there is no IRQ tick on Apple Silicon. Avenues in sec. 1.1 | A3 sec. 2, 3, 6; A9 sec. 1, 2, 4, 6; PROG | **Low**: about 28% that at least one AIC-independent avenue (HW-1, V6) succeeds; about 64% that the timer is resolved if question 1 is already a yes (the watchdog is added) |
| 5 | Do IPIs work without FIQ? | **As hardware yes, on AICv1 (t8103); as a Windows interface unknown.** The AIC legacy IPIs in IRQ made Linux SMP run on 8 cores on the t8103 in the first version (historical code, not exercised today on t8103). Limits: only one "other" IPI per CPU, with no sender or payload (a software multiplexer is needed), latency not measured, AIC index/MPIDR mapping to be confirmed, per-CPU views `0x5000+` not documented. How the Windows HAL sends IPIs (and which sources it expects) depends on the interface of question 1 | A3 sec. 4, 5, 6.2; A2 sec. 3.3 point 7 | **Medium** for the hardware (about 70%); **low** for the integration with Windows |
| 6 | Does the strategy hold for AICv2 (M1 Pro/Max and later)? | **No, not as it stands.** AICv2 does not have the legacy IPIs [V: Linux commit 2cf6821166 cited in A3]; it has a different register map (IRQ_CFG, masks at different offsets). Software hwirqs (`SW_SET`) or Fast IPI (i.e. FIQ) would be needed [H]. For the base M1 alone the problem does not arise, but it contradicts the goal "all Apple Silicon" | A3 sec. 4.1, 6.2 (risk 3); A8 sec. 2.1 | **High** that it does not hold as it stands; **low** on the alternative |

### 1.1 The four avenues on the timer

| Avenue | What is needed | Evidence | Probability (mine) |
|---|---|---|---|
| **HW-1**: bit 22 `CONVERT_SIQ_TO_IRQ` of `ACC_OVRD` | That the bit converts the EL0 timer from FIQ to IRQ | Only the name. Zero uses in the entire AsahiLinux organization and in XNU, bit at 0 under macOS as well, register dominated by power management bits, "SIQ" not defined. The Asahi text says that FIQs are internal to the core complexes, so an interrupt routed directly to the AIC might exclude them by construction | about 10% (A9 sec. 6) |
| **V6**: Windows handles a timer FIQ (QEMU) | That the Windows kernel has a useful FIQ vector and tolerates the source | No evidence. Method B (QEMU patch of about 10 lines) is interpretable; method A (DXE driver on the GIC groups) may be undone by the Windows GIC driver. Even a positive outcome is valid only for the FIQ → timer path, not for other FIQs | about 20% [H: my estimate, no evidence in the files] |
| **Watchdog + timer HAL extension** | IRQ 338 working, one comparator for the whole SoC, 32-bit counter that wraps in about 179 s, risk of reset in recovery if `RESET_EN` is wrong, tick distribution to the other cores with IPIs | Only a comment in the Linux driver, never used. **Depends on question 1** (a working AIC controller is needed to receive the IRQ) and on the existence of timer extensions on ARM64 (they exist for i.MX) | about 50% if question 1 is yes; not applicable otherwise |
| **PMU in AIC mode** (`IMODE=2`) | Which AIC hwirq, counts cycles and not time (DVFS) | The value is documented, the hwirq unknown | not estimated; weak candidate [H] |

A cross-cutting fact: if the timer stays FIQ, an EL2 shell that converts FIQ into a virtual IRQ is technically known (`hv_update_fiq` of m1n1) but it is exactly what ADR-0001 excludes.

## 2. The heaviest fact: the Pi 3 was internal HAL support

**What the evidence says.**
- On the Pi 3 the firmware describes the BCM2836 "ARM local" as if it were a GIC (GICC with base 0x40000000, no GICD). There is no HAL extension, third-party driver or CSRT group for the interrupt controller in the repositories examined; the CSRT contains only the DMA group [V: A4 sec. 2].
- In 2018 the ARM64 HAL **recognized and rejected** the Broadcom PIC; to boot, an in-memory patch with the debugger was needed [V: A4 sec. 3, andreiw issue]. After that there is no trace of patches [H, medium confidence].
- The only public HAL extensions are timer and DMA [V: A1 sec. 2].
- Confidence of A4: about 75% that the Broadcom controller support was internal to Windows.

**What it implies for the native strategy.**
1. **The central hypothesis of ADR-0001 and of the plan remains a hypothesis.** The plan writes "AIC HAL extension that registers the AIC as an interrupt controller" as a known mechanism. The evidence says that today there is no public case where a third party has done it. The Pi 3 shows that the HAL **can** host a non-GIC controller, not that **third parties** can add one.
2. **The HAL's non-GIC path is chosen from MADT data** (GICR=1 value, base 0x40000000, OEM ID "BC2836" [H: which field]). A CSRT extension is a different mechanism. If the HAL chooses the controller from the MADT with a closed list, an AIC would never be reached, whatever extension is loaded [H].
3. **The "fake" GIC is not replicable.** Making the HAL believe that the AIC is a GIC requires either emulating a GIC (EL2, plan B) or patching the HAL (forbidden as a final solution).
4. **The "black box" method has a limit.** If the registration interface is not public, the project "does not guess" (EXPERIMENT sec. 6): without reading at least one WDK header or doing reverse engineering, V4 can only say "loads / does not load". The original PLAN provided for Ghidra on `ntoskrnl`; the Phase 0 ADR draft replaced it with the black box. The consequence must be decided by the user (sec. 5).
5. **It adds doubt, not certainty of failure.** Windows has an internal controller abstraction, demonstrated by the existence of the BCM branch [H]; the question is only whether it is reachable from the outside.

## 3. Probability estimates

These are my judgments on the **state of knowledge**, referring **only** to interrupts, timer and core startup. They do not include the risks of the other phases (ANS storage, DART, GPU), which add up.

| Outcome | Estimate | Rationale |
|---|---|---|
| **(a) Pure native**: Windows ARM64 works with an AIC HAL extension, tables without a GIC, parking and without FIQ | **about 12%** (range 5-20%) | Product of several conditions, each uncertain and with some correlation: the extension registers a controller (about 20%) × the timer is resolved without EL2 (about 64% conditional on yes) × MADT, parking and IPI integrate (about 75%) ≈ 10%. Raised slightly for the positive correlation between conditions. No direct evidence in favor: the A1 verdict is "not determinable" |
| **(b) Plan B needed**: minimal EL2 shim presenting a vGIC. The M1 exposes GIC virtualization (`ICH_*`) | **about 53%** | It is the outcome that follows from the probable negative of question 1 (about 88% of "not pure native") times the feasibility of plan B (about 60% [H]). Partial evidence: Linux on M1 uses `ICH_HCR_EL2` and a virtual FIQ for the vGIC (`AIC_VGIC_MI`) [V: A3 sec. 1, row 8]; m1n1 knows how to convert FIQs into virtual IRQ/FIQ with `HCR_EL2.VF` [V: A3 sec. 2, avenue d]. **Caveat [H]**: the GICv3 virtual CPU interface is not enough; a guest sees GICD and GICR in MMIO, so the shim must also emulate the distributor and redistributors by intercepting the pages with stage 2 translation. It is not "minimal": it is a small hypervisor, contrary to ADR-0001. The "about 1%" overhead in the PLAN is not measured |
| **(c) Other** | **about 35%** | Groups: plan B not feasible or not acceptable to the user; Windows incompatible with the Apple CPU for reasons unrelated to interrupts (R-07); an unforeseen mechanism that works (for example the interface of an extension discovered by reading the WDK); project halt due to a legal or method block (no reverse engineering). Inevitable underestimate: we do not know the unknown unknowns |

Check: 12 + 53 + 35 = 100. The uncertainty on the estimates is large; the order (b) > (c) > (a) is less uncertain than the absolute values.

## 4. What remains to be shown only experimentally, and in what order

Principle: **first what can kill the native path at low cost, then what costs more**. Each step requires the Time Machine backup confirmed by the user before downloading or installing anything, and has stop criteria already defined in `EXPERIMENT-F0.md` sec. 6.

| Order | Experiment | What it closes | Outcome criterion | Cost (estimate A5/A10/A8) |
|---|---|---|---|---|
| 0 | **Reading `nthalext.h`** from the WDK (text, not binary) [H: the header is in the WDK] | Whether an API to register an interrupt controller exists. It is the only check that can lower (a) to about 3% or raise it sharply, without hardware | Registration function present / absent. Requires a WDK download (backup, license H7, and a decision on using a header as a "tool", consistent with the black box) | 1 day plus the download [H] |
| 1 | **V0**: QEMU + HVF + EDK2 baseline | Validity of the environment | Windows installed, debugger attached; stop after about 5 days | 1-3 days |
| 2 | **V1**: `acpigen` + DXE overrides equivalent to QEMU | Separates "Windows rejects" from "our table is wrong" (false negative) | Boot identical to V0; stop after about 3 days of divergence | 1-2 days plus work on `acpigen` |
| 3a | **V4**: CSRT + test HAL extension | **The nearest decisive question**: does Windows load the DLL from one of our CSRT groups, with test-signing, and which interface does it require (also for type 0x0001/0x0001) | DLL loaded and entry point executed / not loaded with cause identified / loaded with informative bugcheck. Limit: loading does not mean registering a controller | 5-10 days; depends on the outcome of step 0 and on a VM with the WDK |
| 3b | **V6**: timer as FIQ (method B, then A) | Does Windows tolerate a timer FIQ? | d (boot and clock advance with proven FIQ) resolves the timer; a/b/c leave it open | 3-4 days plus the QEMU build |
| 4 | **V2** (GICC without GICD) and **V3** (parking) | Quick kill-switch on R-02 and R-06. A clear negative V2 for controller-related causes is a strong indication against native | Reading rule: a "yes" in QEMU = "not rejected in QEMU", not "the M1 will work" | 2-4 and 4-7 days |
| 5 | **AICv1 model in QEMU** ("watari-virt" machine without a GIC) | Real proof of a non-GIC controller with our extension, and of the legacy IPI | Only if V4 and the header study indicate that a registration interface exists: otherwise the model has nothing to prove | 8-9 days [H, A8] |
| 6 | **HW-1** (`ACC_OVRD[22]` on the M1) | Closes the hardware avenue | Only if V6 is negative: if Windows handles the FIQ, HW-1 is not needed. Requires a debug host, m1n1 in proxy mode and user confirmation before touching boot | a few hours if the environment exists; about 10% chance of success |

**Criterion that closes Phase 0.** I distinguish two closures.

- **Positive closure (proceed native)**, all conditions: (1) V0 and V1 green; (2) a controller registration interface exists (step 0 or V4) and our extension is loaded and invoked for that resource type; (3) the AIC model in QEMU, with no GIC declared, takes Windows past HAL initialization with timer and IPIs delivered by our extension; (4) the timer has a closed path (V6 positive, or HW-1 positive, or watchdog tick proven in a hardware session). Even so, write "preconditions accepted in QEMU", **not** "the M1 will work": the definitive yes is Phase 3.
- **Negative closure (go to the plan B decision)**, one is enough: (1) the header study and V4 find no way to register a controller; (2) V2 and V4 give bugchecks attributable to Windows demanding a declared GIC; (3) V6 clearly negative and HW-1 negative and no workable timer extension. Plan B is decided by the **user** (ADR-0001), not by an automatic threshold.
- **Inconclusive closure**: beyond about 4 weeks of agent work (global stop of A5) or V1 not green. Reported to the user as is.

## 5. Recommendation

1. **Do not stop the native path now**, but do not take it for granted: the evidence leaves it at about 12%, and the A4 data worsened the picture compared with the PLAN.
2. **Before spending weeks in QEMU, do step 0**: read `nthalext.h`. It is cheap and can almost single-handedly decide the controller question. An explicit user decision is needed on two points: downloading the WDK (backup and license) and accepting the reading of a header of a public kit as a "tool" in compliance with ADR-0005 (no Microsoft binary or code in the repository: the header must not be copied into the repo).
3. **Proceed with Gate B giving priority to V4 and V6**, after V0 and V1. V4 answers the nearest decisive question; V6 can remove the timer risk without hardware. V2 and V3 are cheap kill-switches to do right after V1.
4. **Develop in parallel, at low cost, the parts also useful to plan B**: extension of `acpigen` (without GICD, parking with version 1 and valid address, FADT, DBG2, DSDT) and EDK2 firmware with parked cores. Do not write the AIC model for QEMU (8-9 days) until V4 or the header show that an interface exists to test.
5. **Fix now, before the results, the threshold that triggers the plan B discussion** (see ADR draft `docs/adr/draft-phase0-preliminary-outcome.md`), to avoid moving the bar after the fact. The user decides.
6. **Prepare plan B without adopting it**: honestly estimate the cost of an EL2 shim with GICD/GICR emulation (not "minimal") and evaluate as an alternative the PSCI/HVC stub alone for parking. These are design jobs, to be assigned to `morasca-agent`, not implementation.

## 6. Limits of this document

- It is based on read-only evidence (public repositories, documentation). No Microsoft binary or symbol was analyzed: by choice, but also a knowledge limit.
- The probabilities are subjective and correlated; the product in sec. 3 is a heuristic, not a calculation.
- The Pi 3 is from 2018-2019; it is not verified that current Windows 11 ARM64 builds retain the BCM branch (A4: confidence 60-65%).
- The text of the CSRT specification was not read directly (A1 sec. 2): the group fields and loading rules remain [H].
- The 20% estimate on question 1 and the 20% on V6 are mine, without a source.
- Evidence on parking and PSCI of modern Windows ARM64: absent.
