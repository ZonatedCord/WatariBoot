# Watari Boot: native Windows ARM64 on the 13" M1 MacBook Pro

## Plan B roadmap: Watari EL2 (current plan, since 2026-10-14)

> The sections below this one describe the original native plan (Plan A) and are kept for history. Since 2026-10-14 the project follows Plan B (`docs/adr/draft-plan-b-el2-layer.md`). **Every problem found is placed here, under the milestone where it must be solved, and has a GitHub issue.**

| Milestone | Goal | Status | Problems to solve in this milestone |
|---|---|---|---|
| **M0** | First spike: register check against the Arm specs, vGIC model with host tests, EL2 boots in QEMU | ✅ done (PR #49) | — |
| **M1** | vGIC wired in (stage-2 traps), list-register delivery (timer, SGI), SMP via PSCI, SPI forwarding, EDK2 at EL1 on top of the layer | ✅ done (PR #56, #57) | — |
| **M1.5: hardening** (before M2) | Make the layer robust enough for Windows | 🔄 in progress | #54 LR overflow · #53 EL2 stage-1 map (F-18, F-20) · #52 TRAP_EMULATE path for the M1 SEIS erratum (logic in QEMU) · #44 acpigen FADT/DBG2/GICR layouts · #45 testsigning / kernel debug in the VM |
| **M2** | Windows 11 ARM64 boots on top of Watari EL2 in QEMU (TCG), to the lock screen | ⏭️ next | #43 cost of ICC_PMR_EL1 traps · vGIC fidelity for Windows (F-14 lesson: a wrong register = silent hang) · SMP under Windows (EDK2 never called CPU_ON, F-21) |
| **M3a** | Watari EL2 on the real M1 MacBook Pro via m1n1, with EDK2 | needs hardware | #40 debug host · #55 M1 cluster MPIDRs · #52 validation on real SEIS hardware · #58 16 KB stage-2 granule · #59 AIC backend (QEMU has a GIC) · optional #42 QEMU AIC machine to test earlier |
| **M3b** | Windows on the real M1 to the lock screen (framebuffer, USB keyboard) | later | #48 drivers (ANS NVMe, DART, USB/Type-C) |
| **M4** | Usable system | later | #48 SPI keyboard/trackpad, power management, GPU · #47 AICv2 chips (M1 Pro/Max, M2+) |
| Side tracks | Not on the critical path | open to anyone | #41 report the HVF/GIC issue to EDK2 · #38/#39 native-route questions · #20/#46 hardware timer experiments |

## Context
The user's goal: **native dual boot** of Windows ARM64 on the MacBook Pro M1 (13", 2020), exploiting the full power of the hardware.
- No hypervisor, emulator or VM.
- Windows must use all 8 cores (4 P + 4 E), the full frequencies and, in the long term, the GPU.
- Then generalize to all Apple Silicon.

Nobody has done it before. Asahi ruled it out, because it believes the Windows kernel would have to be modified. This plan aims at a path **without modifying Windows**, using the extension mechanisms that Windows ARM64 provides:
- **HAL extensions**, with resources declared in the CSRT ACPI table;
- **ACPI tables**;
- **KMDF/StorPort drivers**;
- **PEP** (Platform Extension Plugin, the Windows plugin for power management).

User profile: knows C and C++, no firmware experience. I write the code and explain, they run it on the hardware. They have no second computer: phases 0 and 1 are done on the Mac only, from phase 2 a debug host is needed (a Raspberry Pi 5, about 80 €, is enough).

## The technical blockers and how we work around them natively
| Blocker | Native approach |
|---|---|
| Windows only knows the GIC interrupt controller, the M1 has the AIC | **"AIC" HAL extension** that registers the AIC as an interrupt controller in the Windows HAL. This is **the hypothesis to verify first** (Phase 0). |
| Apple timers and IPIs arrive as FIQ (Windows may not handle them) | Do not use FIQ: IPIs through the AIC's IPI registers (as IRQ, as Linux did at the beginning), and a timer source as IRQ provided by a timer HAL extension (a mechanism documented by Microsoft). FIQs stay masked. |
| The M1 has no EL3/PSCI to power on the secondary cores | **ACPI Multiprocessor Parking Protocol**, written by Microsoft and supported by Windows: EDK2 parks the cores in a mailbox. |
| UEFI and ACPI are missing | **EDK2** platform booted by m1n1 (only as a bootloader: it initializes and then steps aside). ACPI generated from the ADT. P/E cores described with PPTT and efficiency classes, so Windows' scheduler uses them well. |
| SSD (Apple ANS2, not on PCIe) | **Native StorPort miniport** for ANS. The NVMe part is reusable, the ANS and SART init is ported from the Asahi driver. |
| USB/Thunderbolt | m1n1/EDK2 initialize the PHY and DWC3 in host mode, then the controller is described as **xHCI via ACPI**: Windows' standard driver (UsbXhci, as on Qualcomm) handles it natively. |
| IOMMU (DART, 16K pages only, Windows uses 4K pages) | DART in bypass where the hardware allows it, otherwise mappings managed by our drivers. |
| Power: DVFS, P/E cores, sleep | **PEP** + `_CPC` in ACPI for the frequencies (PMGR registers, logic ported from Asahi). |
| AGX GPU | **WDDM driver**: long-term goal, with Asahi's Rust driver and Mesa (Honeykrisp/Asahi) as a reference. At first Basic Display on a framebuffer (GOP). |
| Internal keyboard and trackpad (SPI/MTP) | KMDF HID driver. The logic of the Asahi `spi-hid`/`apple-mtp` drivers is ported. |

Legal notes: reverse engineering for interoperability is permitted in the EU (Dir. 2009/24/EC, art. 6). We do not redistribute Microsoft code. Development uses `bcdedit /set testsigning on`; for public distribution, Microsoft signing via Partner Center is needed.

## Upstream components to reuse
> **ADR-0007:** independent project. Asahi is the source of truth about the hardware. NTASP (`NT-for-ASi`) is used only as a reference, without copying its code; its data is used only after verifying it against DTS/ADT. Audit in `re/ntasp-quality-audit.md`.
- `AsahiLinux/m1n1`:
  - `src/aic.c`, the AIC driver (registers, IPI);
  - `src/adt.c`, the ADT parser;
  - PMGR, framebuffer and USB init, and code to power on the CPUs.
- `AsahiLinux/linux`:
  - `drivers/irqchip/irq-apple-aic.c`;
  - `drivers/nvme/host/apple.c` (ANS);
  - `drivers/iommu/apple-dart.c`;
  - `drivers/soc/apple/*`;
  - Rust GPU driver;
  - DTS `t8103-j293` (the 13" M1 MacBook Pro).
- `tianocore/edk2` + `edk2-platforms`: `ArmVirtPkg` and `Platform/RaspberryPi` as a model (UEFI + ACPI + parking protocol + CSRT).
- `raspberrypi/windows-drivers` and `worproject`: real examples of a Windows ARM64 BSP on non-standard hardware.
- Microsoft kits: WDK (HAL extension, StorPort, KMDF, PEP samples) and public symbols of ARM64 `ntoskrnl.exe` from the symbol server.

## Structure (`~/Documents/02_PROGETTI/WatariBoot`)
```
re/            Ghidra analysis of ARM64 ntoskrnl/HAL and notes
edk2-apple/    AppleSiliconPkg: m1n1 → EDK2, ACPI generated from the ADT, parking protocol, CSRT
halext-aic/    HAL extension: AIC interrupt controller + IRQ timer
drivers/       ans-storport/, dart/, pep/, spi-hid/ (KMDF)
tools/         build, images, QEMU scripts
docs/          technical diary, ADRs, hardware map
```

## Phases (each with a verifiable milestone)

**Phase 0: the decisive spike (Mac only)**
- Download ARM64 `ntoskrnl.exe` (Windows 11 24H2) and its public PDBs, and analyze them with Ghidra.
- Map `HalpInterruptRegisterController`, `_INTERRUPT_FUNCTION_TABLE`, `KnownType` and the loading of HAL extensions (CSRT, `HalpInitializeHalExtensions` / `HalExtension` exports).
- Understand how Windows handled the non-GIC controller of the Raspberry Pi 3 (BCM2836).
- Understand what Windows' exception vector does with a FIQ.
- ✅ Milestone: a document that says **yes/no, with evidence**, on whether a HAL extension can register a non-GIC interrupt controller, and with which interface. **This phase decides the route.**

**Phase 1: prototype in QEMU (Mac only)**
- EDK2 with a MADT *without GIC*, CSRT and parking protocol.
- Stub of the HAL extension that registers a fake interrupt controller.
- Windows ARM64 booted in QEMU with kernel debugging (KDNET or serial) on macOS, watching how far the boot phase gets.
- ✅ Milestone: Windows loads our HAL extension and tries to use it, verified with WinDbg.

**Phase 2: on the MacBook (the debug host is needed)**
- Asahi installer, "UEFI environment only" option, on a partition of about 80 GB: macOS and recovery stay intact.
- m1n1 → native EDK2, with GOP, ACPI generated from the ADT and the cores parked.
- ✅ Milestone: EDK2 UEFI shell on the MacBook, seeing all 8 cores.

**Phase 3: the first native Windows boot**
- AIC HAL extension (ported from `irq-apple-aic.c`) + IRQ timer.
- Boot from a USB stick.
- Debug: framebuffer, then KDCOM on the debug UART via USB-C (debug cable or m1n1 through the debug host).
- ✅ Milestone: **the Windows kernel runs natively on the M1** and reaches the driver boot.

**Phase 4: desktop**
- StorPort miniport for ANS, so that Windows installs onto the internal SSD.
- xHCI via ACPI for USB keyboard and mouse.
- ✅ Milestone: **native Windows desktop on the MacBook Pro M1**, installed on the SSD.

**Phase 5: full power**
- PEP + `_CPC`/DVFS, efficiency classes of the P/E cores.
- Keyboard and trackpad (SPI), Wi-Fi, audio, battery (SMC), suspend.
- Start of the WDDM driver project for the AGX GPU.
- ✅ Milestone: benchmarks (Geekbench, Cinebench) compared with macOS on the same Mac.

**Phase 6: all Apple Silicon**
- ACPI generated from the ADT at every boot.
- AIC2 for M1 Pro/Max/Ultra and M2+, which m1n1 already supports.

## Risks and Plan B
- **Negative spike** (the HAL does not accept non-GIC controllers from extensions). The alternatives, in order:
  - (a) use the CPU GIC interface that the M1 implements for virtualization (`ICH_*` registers), with a minimal shim at EL2 that handles only interrupts: everything else stays native and passthrough, with an estimated overhead of around 1%;
  - (b) kernel patch for research, not distributable.
  If it comes to that, the choice is yours.
- **Brick**: everything goes through a separate Asahi partition. In the worst case a DFU restore from another Mac is needed.
- **Driver signing**: development uses test-signing; for distribution Microsoft signing is needed.

## Verification
- Phase 0: report in `re/` with addresses, functions and Ghidra pseudocode supporting every conclusion.
- Phase 1: WinDbg logs showing our HAL extension loaded in QEMU.
- Phases 2-5: photos or videos of each milestone on the hardware, debug host logs, and Windows vs macOS benchmarks.

## Organization and workflow (applies to all sessions)
- **Roles**:
  - I (Opus 5.5) am the boss: I reason, design, split the work, verify and answer the user;
  - the **Sonnet 5.5** agents (Agent tool, `model: "sonnet"`) execute the tasks and report the results to me, and I check before reporting;
  - the user talks only to me.
- **Persistence of the rule**:
  - in `~/.claude/CLAUDE.md` (global, so it applies to every future conversation);
  - in the repository's `CLAUDE.md`;
  - in a `feedback` memory.
  The current memory is tied to the path `Downloads/ProvaImpossibile`, which will be deleted: that is why the rule also goes in the two `CLAUDE.md` files.
- **Documentation** in `docs/`:
  - `docs/sessions/YYYY-MM-DD-NN.md`: diary of **every end of session** (what was done, decisions, results, next steps);
  - `docs/STATUS.md`: the current state, always up to date, and the point to resume from;
  - `docs/design/watariboot/`: output of the design session;
  - `docs/adr/`: the architectural decisions.

## Execution on leaving plan mode (in order)
1. **Move**: `~/Downloads/ProvaImpossibile` is empty. Create `~/Documents/02_PROGETTI/ProvaImpossibile` and delete the empty folder in Downloads.
2. **Repository skeleton**:
   - the folders of the structure above, plus `docs/sessions`, `docs/adr` and `docs/STATUS.md`;
   - `README.md` (vision and status);
   - the project's `CLAUDE.md` (boss/executors workflow, end-of-session diary rule, constraints: native, no hypervisor unless explicitly decided by the user, no Microsoft code in the repository);
   - `.gitignore`, which excludes Microsoft binaries (ntoskrnl, PDB, ISO), build output and Ghidra databases;
   - this plan copied to `docs/PLAN.md`.
3. **Global rule**: add to `~/.claude/CLAUDE.md` the section on the Opus boss + Sonnet executors workflow. Save the `feedback` memory.
4. **Design session** with `morasca-agent` (`model: sonnet`), starting from this plan: PRD, architecture with diagrams, risks, test strategy and ADRs in `docs/design/watariboot/`. Then I review the output and correct the technical inaccuracies.
5. **GitHub**: `git init`, first commit, `gh repo create ZonatedCord/ProvaImpossibile --private --source . --push`.
6. **Start of Phase 0**, delegated to Sonnet:
   - agent A: toolchain and Ghidra setup, and download of ARM64 ntoskrnl with the PDBs (outside the repository);
   - agent B: research on how Windows handled the Raspberry Pi 3 interrupt controller and on HAL extensions/CSRT.
   I synthesize the results in `re/` and in `STATUS.md`.
7. **End of session**: diary in `docs/sessions/`, update of `STATUS.md`, commit and push.

Meanwhile the user obtains a debug host (Raspberry Pi 5 or an old laptop) for Phase 2.
