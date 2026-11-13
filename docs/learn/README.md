# Learning path: Watari Boot through the lens of an operating-systems textbook

Who this is for: a contributor who wants to **understand** the project and be able to **explain it**, not only run the procedures. It maps what the project does to the chapters of Andrew S. Tanenbaum and Herbert Bos, *Modern Operating Systems* (5th edition, 2022; the 4th edition has the same chapters except the Windows case study, which covers Windows 8 instead of Windows 11). Chapter titles are cited for orientation; read the book for the theory, and use the files below to see the theory applied.

Marking as in the rest of the repository: **[verified: source]** or **[hypothesis]**.

---

## 1. The project in two minutes (what to say)

- **Goal.** Boot Windows 11 ARM64 on an Apple Silicon Mac (first the 13" M1, j293), chosen at power-on, with no macOS underneath and no classic VM (`docs/PLAN.md`).
- **The obstacle.** Windows on ARM expects an Arm **GIC** interrupt controller and Arm's standard timer wiring. The M1 has Apple's own interrupt controller (**AIC**) and delivers its timers as **FIQs**. Windows cannot be modified, and a Windows HAL extension for a non-GIC controller is not a public interface (`re/f0-nthalexts-lead.md`, Phase 0).
- **The solution (Plan B, 2026-10-14).** A very small **EL2 layer** ("Watari EL2"): a thin hypervisor that does only what Windows lacks: it **emulates a GICv3** in front of the AIC, routes the timers, implements **PSCI** for starting cores, and leaves everything else native (`docs/adr/draft-plan-b-el2-layer.md`). Windows runs at EL1 on the real cores at full speed.
- **Where we are (2026-11-13).** In QEMU, Windows 11 reaches the desktop on top of the layer (M2). On the **real M1**, the layer and an EL1 test guest pass every check: hand-over from m1n1, MMU, interrupt controller, virtual GIC (two trap configurations), timers, all 8 cores (`re/m3a-day2-results.md`). **Next:** UEFI firmware (EDK2) on top of the layer on the M1 (T11), then Windows on the M1 (M3b), which needs drivers for Apple's NVMe (ANS), IOMMU (DART) and USB.

## 2. Chapter by chapter

### Chapter 1, Introduction: hardware review, booting, OS structure
- **Book concepts:** CPU modes, interrupts, the boot sequence, monolithic vs layered vs virtual-machine structures.
- **In the project:** Arm exception levels are the "modes": EL0 apps, EL1 kernel (Windows), **EL2 hypervisor (our layer)**, EL3 firmware (not used on Apple Silicon **[hypothesis]**: iBoot plays its role). The boot chain on the M1 is iBoot -> m1n1 -> (our layer) -> EDK2 -> Windows Boot Manager (`docs/procedures/m3a-day1-hardware.md` 3.1, `el2/boot/README.md`).
- **Seen on the hardware:** `CurrentEL = 2` when m1n1 runs, `HCR_EL2.E2H` stuck at 1 (the M1 always runs EL2 in the "host" layout of the Virtualization Host Extensions) (`re/m3a-ehw0-measurements.md`).
- **Study question:** why does an OS that must stay unmodified force us to put new code *below* it rather than inside it?

### Chapter 3, Memory management: paging, TLB, multilevel page tables
- **Book concepts:** virtual vs physical addresses, page tables, page size trade-offs, TLB.
- **In the project:** two levels of translation. The layer's own **stage 1** (EL2 virtual -> physical) and the guest's **stage 2** (guest "physical" -> real physical), both built by `el2/vgic/pt.c` / `maps.c`. Page size: **16 KB granule** on the M1 (Apple's IOMMU is said to use 16 KB pages **[hypothesis]**; the CPU also supports 4 KB, measured 2026-11-10). Stage-2 "holes" make guest accesses to the emulated GIC fault into EL2, where they are emulated.
- **Seen on the hardware:** L2 checkpoint 1 `STAGE1_ON`, L3 checkpoint 6 `STAGE2_ON` (`re/m3a-day2-results.md`).
- **Study question:** the book's "nested page tables" for virtualization: which of our two stages corresponds to which table, and who fills each one?

### Chapter 5, Input/Output: interrupts, interrupt handlers, clocks, device drivers, DMA
- **Book concepts:** interrupt controllers, precise interrupts, interrupt handlers, clock hardware and clock software, device drivers, DMA.
- **In the project:** the **AIC** (`el2/include/wel2_aic.h`, `el2/vgic/pirq_aic.c`): 896 hardware lines, an `EVENT` register that acknowledges and masks, per-IRQ target-core registers. The **virtual GIC** the guest sees (`el2/vgic/`). The **timers**: on the M1 they raise FIQs, which the layer turns into virtual timer interrupts (PPI 27/30). **DMA through an IOMMU** (Apple **DART**) is the M3b driver problem.
- **Seen on the hardware:** `NR_IRQ = 896`, the EVENT read masks the source, software-triggered IRQs stay pending until cleared (day 1); G6 (SGI delivered, acknowledged, EOI'd) and G7/G8 (timers) pass (day 2).
- **Study question:** "trap-and-emulate" of an interrupt controller vs letting hardware deliver: what do the `trap` and `trap-notc` counters (14 vs 6 trapped accesses) tell you about the cost?

### Chapter 7, Virtualization and the cloud: the central chapter for this project
- **Book concepts:** Popek-Goldberg requirements, **type 1 vs type 2 hypervisors**, trap-and-emulate, paravirtualization, memory virtualization, I/O virtualization, hardware support for virtualization.
- **In the project:** the layer is a **type 1 hypervisor in miniature**, but with one guest and the explicit goal of *not* virtualizing most hardware ("everything else native"). It traps exactly the sensitive operations Windows needs changed: GIC system-register accesses (`ICH_HCR_EL2.TALL0/TALL1/TDIR/TC`), ID registers (`HCR_EL2.TID3`), PSCI calls (`HVC`/`SMC`), GIC MMIO via stage-2 faults. Hardware support: Arm's virtualization extensions and the GICv3 **virtual CPU interface with list registers** (8 on the M1, measured).
- **Seen on the hardware:** L3 passes in both trap configurations; 0x2E ID-register reads emulated (day 2).
- **Study question:** is Watari EL2 "full virtualization" or "paravirtualization" in the book's sense? (Windows is not modified, but ACPI tables are generated for it: where does that put us?)

### Chapter 8, Multiple processor systems
- **Book concepts:** multiprocessor hardware, cache coherence, multiprocessor OS types, inter-processor interrupts, booting the other CPUs, scheduling.
- **In the project:** 8 cores in two clusters (4 Icestorm E-cores, 4 Firestorm P-cores), different MPIDR affinity values (`0x80000000..03`, `0x80010100..03`, measured). Secondary cores are started through **PSCI `CPU_ON`** (the layer releases them from a spin table), IPIs via SGIs (virtual) and Apple's fast-IPI system registers (physical). Per-core stacks indexed by `Aff0 + 4 x Aff1` (`el2/boot/start.S`, `wel2_core_index`).
- **Seen on the hardware:** L5, all secondaries reach the layer and the guest's `CPU_ON` / `CPU_OFF` / `AFFINITY_INFO` checks pass.
- **Study question:** why did the first version of the stack index (Aff0 only) work in QEMU but would have broken on the M1?

### Chapter 11, Case study 2: Windows (11 in the 5th edition)
- **Book concepts:** Windows structure: HAL, kernel, executive, drivers; booting Windows; the I/O system and drivers.
- **In the project:** why the HAL matters (Phase 0, `re/f0-*`): Windows on ARM ships a HAL that speaks GIC and the Arm generic timer; the platform is described to it through **ACPI tables** (MADT, GTDT, ...), which `tools/acpigen` generates. M3b needs Windows **drivers** (storage, IOMMU, USB) that follow the Windows driver model (KMDF/StorPort). The project rule: no Microsoft code or binaries in the repository, and no disassembly of them (`docs/adr/0005-no-microsoft-binaries-in-repo.md`).
- **Study question:** which Windows component would have had to change if we had not built the layer, and why is that not possible?

### Chapter 9, Security (briefly)
- Apple Silicon boot security (Secure Boot policy per volume, "Permissive Security" for the Asahi volume only) and Arm Pointer Authentication (the layer sets `HCR_EL2.API/APK` so the guest owns its keys). Day 1 documents what was changed on the target and why it is limited to one volume (`docs/procedures/m3a-day1-hardware.md`).

### Chapters 2, 4, 6 (processes, file systems, deadlocks)
- Less central: the layer has no processes, no scheduler and no file system on purpose. Locking still matters: the layer uses spin locks on shared state between cores (`el2/boot/README.md`, F-20), which connects to the book's discussion of mutual exclusion on multiprocessors.

## 3. The direction, in the book's terms

| Next step | What it is in OS terms | Book chapters |
|---|---|---|
| T11: EDK2 (UEFI) on the layer on the M1 | firmware as a small OS that boots the real one; ACPI tables handed over | 1, 11 |
| M3b: Windows to the lock screen on the M1 | device drivers for storage (ANS NVMe), IOMMU (DART), USB; DMA isolation | 5, 7 (I/O virtualization), 11 |
| M4: usable system | power management, keyboard/trackpad over SPI, GPU | 5, 8 |
| Other Apple chips (AICv2, M1 Pro/Max, M2+) | portability of the platform layer | 12 (OS design) |

**[hypothesis]** The main risk has moved: the layer itself is proven on the hardware (2026-11-13); what is left is mostly **driver work** (chapter 5) for hardware that Windows does not know.

## 4. How to read the repository (suggested order)

1. `README.md`, then `docs/PLAN.md` (Plan B table at the top), `docs/STATUS.md`.
2. `docs/adr/draft-plan-b-el2-layer.md` (the key decision) and `re/planB-feasibility.md`.
3. `el2/boot/README.md`: the layer, in order (boot, stage 1, vGIC, PSCI, checkpoints, reading the screen).
4. The hardware results: `re/m3a-ehw0-measurements.md` (day 1, registers) and `re/m3a-day2-results.md` (day 2, the ladder).
5. `docs/FAILURES.md`: what went wrong and what was learned; the best way to see engineering judgement in practice.

## 5. Exercises that show competence (small, real)

- Read `el2/include/wel2_aic.h` and explain, with the day-1 measurements, the life of one interrupt from a device line to Windows' handler.
- Take the L3 counters (`trap` 0xE vs `trap-notc` 0x6 ICC accesses) and write one paragraph on what each configuration costs.
- Fix one of the F-32 follow-ups: update the expected lines with `wel2.hold=1` in `docs/procedures/m3a-day2-hardware.md`, or put the clean `PATH` into the `wl` function. A small PR with a clear description is a good first contribution.
