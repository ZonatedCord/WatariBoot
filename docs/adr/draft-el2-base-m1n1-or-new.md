# ADR (draft): Watari EL2 is a new layer; m1n1 remains only the loader

- Date: 2026-10-14 · Status: **proposed** (to be approved by Marco). The number is assigned at merge.
- Links: `docs/adr/draft-plan-b-el2-layer.md` (underlying decision), `docs/design/watari-el2/ARCHITECTURE.md` §2, `docs/adr/draft-el2-license.md`, ADR-0007 (path independent from NTASP).

## Context
Plan B requires an EL2 layer with vGICv3, PSCI, timer as an interrupt, and SGI→IPI. Three bases are possible: the m1n1 hypervisor, the NTASP fork `m1n1_windows`, or new code. Facts verified on 2026-10-14:
- m1n1 `hv` has stage-2, MMIO traps, multiple vCPUs and a guest timer via virtual FIQ, but **has no vGIC**; every unhandled exception goes to the Python proxy and the guest stops until the host answers; the exception path is slow and single-threaded [verified: `re/planB-feasibility.md` §2].
- m1n1 since 2.0.0 is **GPL-2.0-or-later** (MIT up to 1.9.9): README and files `src/hv/hv_vm.c`, `src/main.c` [verified].
- NTASP `hv_vgic.c` is incomplete (pending/active missing, 1 core, SGI "TODO"), has a single active maintainer and states it is inspired by KVM [verified: `re/ntasp-analysis.md`, `planB-feasibility.md` §3]; `re/ntasp-quality-audit.md` judges the UEFI side "to be rewritten".
- A core that runs only on Apple hardware cannot be tested in the QEMU TCG bench with `virtualization=on`, which is the only development environment without a debug host.

## Decision
**Watari EL2 is written as a new project** (freestanding C + AArch64 assembly, directory `el2/`), with:
- a **portable core** (vGIC, PSCI, timer, stage-2, traps) with no hardware dependencies, testable on the host;
- replaceable **backends** for the platform (`qemu-virt`, `t8103`) and for the interrupt/IPI/timer sources;
- **m1n1 as loader** (iBoot → m1n1 → Watari EL2 as payload in the style of the Linux `Image` with the FDT in `x0`), not as hypervisor;
- **m1n1 hv as a measurement tool**, read-only (observing how macOS programs the AIC/timer, verifying the semantics of IMP-DEF registers), never in the production path.

## Consequences
- Positive: testability (L0-L3 without the MacBook), autonomy (no proxy), clear boundaries, controlled license, consistency with ADR-0007.
- Negative: stage-2 and traps that m1n1 already has are rewritten (estimate 600-900 lines [hypothesis]); the vGIC has to be written anyway (2,500-4,000 lines [hypothesis]).
- The hand-off format from m1n1 and the state of the secondary cores are **assumptions to be verified** in M3a (R-10).
- A new `el2/` tree is created: update the "Structure" section of `CLAUDE.md` at merge.
- The `CLAUDE.md` constraint "no hypervisor" becomes "dual boot, no macOS underneath, minimal EL2 layer allowed" (already decided in the plan B draft).

## Alternatives (rejected)
1. **Extend m1n1 hv by adding the vGIC.** Rejected: GPL-2.0-or-later (binds the image license), proxy and slow path to be restructured (2-3 weeks [hypothesis, PB §2]) plus a vGIC to write anyway, no automated tests, impossible to run in QEMU.
2. **Fork NTASP `m1n1_windows`.** Rejected: incomplete, uncertain license status (MIT declared on code derived from GPL m1n1 and "inspired" by KVM), contradicts ADR-0007, dependency on a maintainer.
3. **Linux/KVM (Asahi) as host with a Windows guest and passthrough peripherals.** Rejected: an operating system underneath Windows contradicts "dual boot, no OS underneath"; it remains as an **experimental control** because Windows 11 already runs as a KVM guest on Asahi [verified: `windows-11-vm.md`, via PB §0].
4. **Rust `no_std`.** Rejected: requires a toolchain to be installed (backup rule) and there is no experience in the repository; C with host tests and `-Werror` is enough for the expected size. Can be reconsidered for the vGIC alone.
5. **Layer as an EDK2 driver (UEFI at EL2).** Rejected: EDK2 must run at EL1 under the layer; mixing the two makes it impossible to test the layer on its own.
