# Watari Boot

**渡 (watari): to cross over, to pass from one shore to the other.**

> Watari Boot: Native Windows on Apple Silicon

**Windows ARM64 dual boot on Apple Silicon, without macOS underneath.** No classic VM (Parallels/UTM), and no modification to Windows: Windows runs on top of a minimal EL2 layer (vGIC, PSCI, timer, IPI), while everything else stays native (full-speed cores, real peripherals). This is Plan B, adopted on 2026-10-14 after the pure native route turned out to be closed (see [docs/STATUS.md](docs/STATUS.md)).

The boot chain: m1n1 (bootloader, from [Asahi Linux](https://github.com/AsahiLinux)) → minimal EL2 layer → EDK2 (UEFI + ACPI generated from Apple's ADT) → Windows ARM64. Windows learns to use the Apple hardware through its official extension mechanisms: HAL extensions, ACPI, KMDF/StorPort drivers and PEP.

First target: MacBook Pro 13" M1 (t8103).

## Status
Phase 0: the spike on the Windows HAL and non-GIC interrupt controllers. Windows 11 ARM64 already installs and boots in QEMU on our own EDK2 and our own ACPI tables (V0/V1); the pure native route is closed (GIC required, parking protocol deadlocks), so Plan B is the way forward. Details in [docs/STATUS.md](docs/STATUS.md).

## Tokens and costs
<!-- TOKEN-COUNTER:START -->
**Total tokens: 256,669,779 · Estimated cost: $109.79** (updated 2026-10-14; marco: 6 sessions, $109.79)

| Model | Input | Output | Cache written | Cache read | Estimated cost |
|---|---:|---:|---:|---:|---:|
| `claude-opus-5-5` | 1,006 | 406,659 | 4,350,727 | 164,643,061 | $75.87 |
| `claude-sonnet-5-5` | 1,670 | 390,759 | 5,302,241 | 81,573,656 | $33.92 |

<sub>Estimate at Anthropic API prices, computed from local Claude Code transcripts with `tools/token-usage.py`. With a subscription (Pro/Max) the real cost is the subscription fee: this is the equivalent pay-as-you-go cost. Subagent output may be under-reported in the logs.</sub>
<!-- TOKEN-COUNTER:END -->

## Documents
- [Plan](docs/PLAN.md)
- [Status](docs/STATUS.md)
- [Session diary](docs/sessions/)
- [How we collaborate](docs/CONTRIBUTING.md) · [Onboarding for a new collaborator](docs/ONBOARDING.md)
- [Architecture decisions](docs/adr/)
- [Design](docs/design/watariboot/) · [EL2 layer design](docs/design/watari-el2/)
- [Failures and lessons learned](docs/FAILURES.md)

## Disclaimer
Experimental research project. No Microsoft code or binaries are included in this repository.
