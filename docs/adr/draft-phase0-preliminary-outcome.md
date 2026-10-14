# ADR (draft): Preliminary outcome of Phase 0 and the plan B threshold

- Date: 2026-10-13 · Status: **proposed**. The number is assigned at merge. It does not modify ADR-0001 or ADR-0004: it clarifies their review conditions.
- Links: [re/f0-preliminary-verdict.md](../../re/f0-preliminary-verdict.md) · [ADR-0001](0001-native-boot-without-hypervisor.md) · [ADR-0004](0004-irq-instead-of-fiq.md) · [timer strategy draft](draft-timer-fiq-strategy.md) · [EXPERIMENT-F0](../design/watariboot/EXPERIMENT-F0.md) · [RISKS R-01, R-02, R-03, R-06](../design/watariboot/RISKS.md)

## Context
Research items A1-A6 and A8-A10 are concluded. The state of the evidence, strictly:
- No evidence shows that a third-party HAL extension can register a non-GIC interrupt controller. The CSRT specification provides for the "Interrupt Controller" type, but the only public examples of extensions are timers and DMA (A1).
- The Raspberry Pi 3 precedent indicates that support for the non-GIC controller was **inside the Microsoft HAL** (A4, confidence about 75%). It is not a precedent for a controller provided by third parties.
- The M1 architectural timer is FIQ-only; no route to get it as an IRQ is demonstrated (A3, A9). The parking protocol without a GIC is compatible with the specification, but it is not known whether modern Windows uses it (A2).
- The "no FIQ" strategy does not hold for AICv2 (M1 Pro/Max and later), which has no legacy IPIs (A3).
- QEMU cannot remove the GIC: Gate B gives a reliable "no" but only a weak "yes" (A5).
- Subjective estimate of the state of knowledge, for interrupts, timer and core bring-up only: pure native about 12%, plan B (EL2 shim with vGIC) about 53%, other or failure about 35%.

## Proposed decision
1. **Proceed with Gate B**, in this order: V0, V1, then **V4** (loading a HAL extension from CSRT) and **V6** (timer delivered as FIQ) as priorities, then V2 and V3 as quick checks. The AIC model for QEMU (A8, about 8-9 days) and the hardware test HW-1 are done only if the results justify them: the model only if an interface for registering a controller exists, HW-1 only if V6 is negative.
2. **Before V4, read the WDK header `nthalext.h`**, if the user consents to the download (Time Machine backup confirmed, license accepted by the user). It must not be copied into the repository (ADR-0005).
3. **Define now the threshold that triggers the plan B discussion**, before seeing the results. The discussion starts when **one** of these conditions occurs:
   - T1: neither the header nor V4 shows any way to register an interrupt controller;
   - T2: V2 and V4 produce bugchecks attributable to Windows requiring a declared GIC;
   - T3: V6 clearly negative, HW-1 negative (or not executable) and no viable timer extension;
   - T4: no closure within about 4 weeks of agent work (global stop of A5), to be reported as inconclusive.
4. **The user decides plan B.** The Phase 0 verdict is presented as it is, with no automatic change of strategy (ADR-0001). In the meantime, a plan B design study is prepared with `morasca-agent`, without adopting it, also including the less invasive alternative of a single PSCI/HVC stub at EL2 for core bring-up.
5. A positive outcome in QEMU is written as "preconditions not rejected in QEMU", never "the M1 will work": confirmation is Phase 3 on hardware.

## Alternatives (rejected)
- **A. Stop the native path immediately and move to plan B.** Pro: avoids weeks of experiments. Con: the evidence does not show that native is impossible (A1: about 20% yes), and plan B itself has a high cost and contradicts ADR-0001. Rejected for now, to be reconsidered if T1 occurs.
- **B. Continue with the original plan (full Gate B with the AIC model right away).** Pro: more faithful test bench. Con: 8-9 days of work before knowing whether there is an interface to test; QEMU's GIC remains present. Rejected in the anticipated form.
- **C. Reverse engineer `ntoskrnl` and the HAL with Ghidra (as in the original PLAN).** Pro: answers the decisive question directly. Con: contrary to the black-box choice of the Phase 0 ADR draft; legal and methodological risk (R-13); requires a Microsoft binary. Not proposed here; remains a user decision if step 0 and V4 are not enough.
- **D. HAL patch.** Not native as a final solution (ADR-0001, plan B (b)). Excluded.
- **E. Defer the judgment to the hardware (Phase 3).** Pro: the only definitive proof. Con: requires EDK2 firmware and host debugging first, and a failure at Phase 3 would cost much more than a failure now. Rejected as the sole strategy.

## Consequences
- Phase 0 has an explicit closing criterion (documented in `re/f0-preliminary-verdict.md` sec. 4) and a threshold fixed in advance, which reduces the risk of moving the goalposts afterwards.
- Gate B remains long (3-5 calendar weeks [hypothesis]) and with insufficient disk space (75-115 GiB needed, about 53 GiB free, A5): a user decision on an external disk or another debugger host is needed.
- Every download (WDK, QEMU, ISO, toolchain) requires the Time Machine backup confirmed by the user.
- If T1, T2 or T3 occur, the user will have to choose between plan B (EL2 with GICD and GICR emulation, which is not "minimal" and must be estimated [hypothesis]), a PSCI/HVC stub limited to core bring-up only, reverse engineering, or abandoning the native goal. The cost of plan B is not yet known.
- ADR-0004 remains "Accepted with unverified hypotheses": the FIQ-only timer makes it the most fragile decision.
- Documentation to update at the end of the session: `docs/STATUS.md`, session diary, `RISKS.md` (R-01 and R-03: probability goes from M to H-M [hypothesis]).
