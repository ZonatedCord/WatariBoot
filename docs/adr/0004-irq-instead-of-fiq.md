# ADR-0004: Avoid FIQs, use IRQ for IPIs and timer

- Status: Accepted (from the plan), with unverified technical hypotheses
- Date: 2026-10-12 · Links: [ARCHITECTURE sec. 5](../design/watariboot/ARCHITECTURE.md), [RISKS R-03](../design/watariboot/RISKS.md)

## Context
[verified: Linux `drivers/irqchip/irq-apple-aic.c`] The AIC delivers hard-wired events (ARM timer, Fast IPI, performance counters) as FIQs; it also has two IPIs per CPU and 896 level-triggered IRQs. Windows may not handle FIQs [hypothesis: to be analyzed in the exception vector in Phase 0].

## Decision
- IPI: via the AIC IPI registers in IRQ mode (as Linux did at the beginning [hypothesis: from the plan]), not Fast IPI.
- Timer: IRQ source provided by a timer HAL extension (a mechanism believed to be documented by Microsoft [hypothesis]).
- FIQs stay masked.

## Alternatives (rejected)
- Handle FIQs in Windows: would require modifying the exception vector (not native).
- Intercept FIQs in an EL2 shim and re-inject IRQs: plan B, considered a hypervisor (ADR-0001).
- Timer polling: insufficient performance.

## Consequences
- [hypothesis] The M1 must be able to route the timer as an IRQ (not verified; R-03).
- IPI performance lower than Fast IPI (not measured).
- An unexpected masked FIQ stays invisible: diagnostics are needed (counters, Phase 3 tests).
