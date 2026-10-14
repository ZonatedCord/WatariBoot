# PRD: Watari Boot

- Project: Watari Boot (native Windows ARM64 on Apple Silicon, first target MacBook Pro 13" M1, SoC t8103, board j293)
- Date: 2026-10-12 · Version: 0.1 · Status: draft
- Related documents: [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md) · ADR: [0001](../../adr/0001-native-boot-without-hypervisor.md), [0002](../../adr/0002-edk2-instead-of-uboot.md), [0003](../../adr/0003-parking-protocol-instead-of-psci.md), [0004](../../adr/0004-irq-instead-of-fiq.md), [0005](../../adr/0005-no-microsoft-binaries-in-repo.md)

## Marking convention
Every hardware or Windows claim is marked `[verified: source]` or `[hypothesis]`. Claims that come only from `docs/PLAN.md` and have not been confirmed against independent sources are `[hypothesis]` (even if plausible). Almost everything concerning Windows internals remains `[hypothesis]` until Phase 0 is complete.

## 1. Problem
A 13" M1 MacBook Pro cannot boot Windows ARM64 natively. The only existing approaches use a VM/hypervisor, which does not give direct access to the hardware (GPU, all cores at full frequency). The Asahi project has ruled out native Windows boot because it considers modifying the Windows kernel necessary [hypothesis: claim reported by the plan, primary source not verified]. The project checks whether a path exists without modifying Windows, using only the extension mechanisms provided by the platform.

## 2. Goals
- O1. Native boot of unmodified Windows ARM64 on the M1 (m1n1 -> EDK2 -> Windows chain), without a hypervisor or emulator ([ADR-0001](../../adr/0001-native-boot-without-hypervisor.md)).
- O2. Use of all 8 cores (4 P + 4 E) with a scheduler aware of efficiency classes.
- O3. Installation and boot from the internal SSD (ANS2) with a usable desktop with USB peripherals.
- O4. Power management (frequencies, sleep) and internal peripherals (keyboard/trackpad, Wi-Fi, audio, battery).
- O5. Long term: GPU through a WDDM driver.
- O6. Generalization to other Apple Silicon (ACPI generated from the ADT at every boot).
- O7. Reproducibility: every milestone can be rebuilt from repo + instructions, with Microsoft data outside the repo ([ADR-0005](../../adr/0005-no-microsoft-binaries-in-repo.md)).

## 3. Non-goals
- NG1. Modifying ntoskrnl, the HAL or other Microsoft binaries (kernel patching only as a research plan B, not distributable, and only by explicit user decision).
- NG2. Hypervisor/VM as the final solution (except by explicit user decision on plan B).
- NG3. Redistributing Microsoft code or binaries.
- NG4. Touching macOS or the recovery; disk operations outside the Asahi installer.
- NG5. GPU performance or full Thunderbolt in the early phases (GPU: only framebuffer/Basic Display until Phase 5).
- NG6. Support for Windows x64 or Windows versions other than the one used for Phase 0 (Windows 11 24H2 ARM64) before phases 0-4 are completed.

## 4. Users and actors
| Actor | Description |
|---|---|
| Developer/owner | Marco: knows C/C++, not firmware; runs on the hardware, decides plan B |
| Lead agent (Opus) | Designs, verifies, reports |
| Executor agents (Sonnet) | Write code and documents on defined tasks |
| Debug host | Raspberry Pi 5 or laptop, required from Phase 2 |
| Future users (community) | Apple Silicon owners who are interested; relevant only from Phase 6 and from driver signing |

## 5. User stories
- US1. As a developer I want a yes/no verdict with proof on whether a non-GIC controller can be registered via an HAL extension, so that I can decide the path before writing firmware.
- US2. As a developer I want to try the HAL extension in QEMU with WinDbg, so that I can validate it without risking the hardware.
- US3. As a developer I want a UEFI shell on the MacBook with 8 cores visible, so that I have a stable base.
- US4. As a developer I want the Windows kernel to run natively and reach driver loading, so that I can test the central hypothesis.
- US5. As a user I want to install Windows on the internal SSD and use a USB keyboard/mouse, so that I have a desktop.
- US6. As a user I want full frequencies, internal keyboard/trackpad and suspend, so that I can use the Mac as a laptop.
- US7. As a developer I want macOS to remain bootable at all times, so that I do not put the device at risk.

## 6. Functional requirements
| ID | Requirement | Phase | Module ([ARCHITECTURE](ARCHITECTURE.md)) |
|---|---|---|---|
| FR-1 | Phase 0 report with a yes/no verdict and proof (addresses, functions, Ghidra pseudocode) | 0 | M-RE |
| FR-2 | EDK2 exposes ACPI tables (MADT, GTDT, CSRT, PPTT, DSDT, DBG2, ...) generated from the ADT | 1-2 | M-ACPI-GEN, M-EDK2 |
| FR-3 | Secondary cores bootable via the ACPI Multiprocessor Parking Protocol | 2 | M-PARK |
| FR-4 | GOP on the framebuffer provided by m1n1 | 2 | M-EDK2 |
| FR-5 | AIC HAL extension: controller registration, mask/unmask, EOI, routing, IPI | 3 | M-HALEXT |
| FR-6 | Timer source on IRQ (timer HAL extension); FIQ masked | 3 | M-HALEXT |
| FR-7 | Boot from USB up to driver loading | 3 | all of the above + M-XHCI |
| FR-8 | ANS2 StorPort miniport (with SART) for installation and boot from the internal SSD | 4 | M-ANS |
| FR-9 | xHCI described in ACPI, handled by the standard Windows driver | 4 | M-XHCI, M-ACPI-GEN |
| FR-10 | IOMMU (DART) handling: bypass where possible, otherwise mappings in the drivers | 3-4 | M-DART |
| FR-11 | PEP + `_CPC` for DVFS; P/E efficiency classes via PPTT | 5 | M-PEP, M-ACPI-GEN |
| FR-12 | KMDF HID driver for the internal keyboard and trackpad | 5 | M-SPIHID |
| FR-13 | Wi-Fi, audio, battery (SMC), suspend | 5 | future modules (see ARCHITECTURE, extension points) |
| FR-14 | Start of a WDDM project for the AGX GPU | 5+ | M-GPU (start only) |
| FR-15 | ACPI generation from the ADT at every boot, AIC2 support | 6 | M-ACPI-GEN, M-HALEXT |
| FR-16 | Non-destructive installation: macOS and recovery intact | 2+ | procedure (Asahi installer) |

## 7. Non-functional requirements
- NFR-1 Maintainability: one module = one responsibility; provenance header on Asahi code; every decision in an ADR. Modules: all.
- NFR-2 Reproducibility: toolchains with pinned versions, build in containers/scripts in `tools/`, hashes of the Microsoft binaries used are recorded (not the binaries), test seeds and fixtures fixed. Modules: tools, all.
- NFR-3 Modularity: explicit interfaces (INTERFACES.md), acyclic dependency graph, platform separated from the SoC model (ADT-driven data table). Modules: all.
- NFR-4 Extensibility: new SoC = new data description, not a change to core code; new driver = new module with an ACPI ID. Modules: M-ACPI-GEN, M-HALEXT.
- NFR-5 Hardware safety: no disk/boot operation without a backup and user confirmation; macOS/recovery not touched.
- NFR-6 Legal: no Microsoft binaries/code in the repo; Asahi code reused in compliance with its license.
- NFR-7 Traceability: every technical claim marked verified/hypothesis; every Phase 0 conclusion with evidence.
- NFR-8 Observability: every phase exposes a log channel (serial/framebuffer/m1n1 debug) specified in TEST_STRATEGY.

## 8. Measurable success criteria per phase
| Phase | Criterion | Evidence |
|---|---|---|
| 0 | Document in `re/` with a yes/no verdict on registering a non-GIC controller via an HAL extension, and the interface; every conclusion with address/function/pseudocode; findings also on: loading extensions via CSRT, FIQ handling in the exception vector, the BCM2836 case | File in `re/`, Opus review |
| 1 | In QEMU, with a GIC-less MADT, our HAL extension is loaded and invoked by Windows (not merely present on disk) | WinDbg log with module loaded and breakpoint hit; 3 consecutive boots with the same outcome |
| 2 | UEFI shell on the MacBook that enumerates 8 cores (4 P + 4 E) with parking mailboxes initialized; macOS still bootable | Photo/video, serial/m1n1 log, macOS boot check |
| 3 | Native Windows kernel reaches driver loading (start of phase 1 of kernel boot with boot-start drivers) | Debug host log, photos; boot time up to point X recorded; 3 boots out of 3 |
| 4 | Installation completed on the internal SSD and desktop reached with USB keyboard/mouse | Installation + desktop video; reboot from SSD succeeded 5 consecutive times |
| 5 | Geekbench and Cinebench run with all 8 cores; results reported relative to macOS on the same Mac (acceptance threshold to be defined after the first measurement: open) | Benchmark report with version, thermal conditions, repetitions |
| 6 | Second platform (e.g. M1 Pro/Max or M2) reaches the UEFI shell with ACPI generated from the ADT without changes to platform code | Log + diff showing only new data |

Numeric performance thresholds: not set in advance so as not to invent figures; they will be defined at the end of Phase 4 on real measurements (open question).

## 9. Scope and out-of-scope
In scope: phases 0-6 of the plan. Out of scope: signed public distribution (requires Partner Center), Linux/other OSes, Windows x64, full accelerated GPU before Phase 5+, full Thunderbolt/DisplayPort alt-mode.

## 10. Risks (summary)
Detail in [RISKS.md](RISKS.md). Main ones: R-01 negative spike (HAL does not accept a non-GIC controller); R-02 Windows requires a GIC in the MADT; R-03 FIQ-only timer; R-04 debugging without a second computer; R-05 brick.

## 11. ADR references
ADR-0001 native boot without hypervisor · ADR-0002 EDK2 instead of U-Boot · ADR-0003 parking protocol instead of PSCI · ADR-0004 IRQ instead of FIQ · ADR-0005 no Microsoft binaries in the repo. The overall stack (C for firmware/drivers, EDK2, WDK) is imposed by the plan; the language choice for the unfixed parts (e.g. Rust for host components) has not been made and is an open question.

## 12. Non-blocking open questions
- Performance thresholds for Phase 5 (see above).
- Exact Windows 11 ARM64 version and Phase 0 build to freeze (hash).
- Debug channel technology in Phase 3 (KDCOM over UART via DBG2 vs other) - see RISKS R-04.
