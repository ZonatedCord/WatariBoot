# Failures and lessons learned

This file collects everything that did **not** work, why it did not work and what we learned. It is meant for us and for anyone who tries something similar: a documented dead end saves weeks for whoever comes next.

Format of each entry:
- **What we tried** and **what happened**, with the evidence (logs, files, links);
- **why** (verified or hypothesis);
- **lesson** for the reader.

---

## F-01 · Analyzing the Windows binaries (ntoskrnl/HAL) for Phase 0 (2026-10-12)
- **Tried:** delegating to an agent the download of ARM64 `ntoskrnl.exe` with the public symbols, to analyze it with Ghidra.
- **What happened:** the task was stopped by a safety filter while it was being written.
- **Lesson:** Phase 0 was rethought as experimental ("black box": public documentation + experiments in QEMU), see the draft ADR "experimental Phase 0". A similar project must account for this methodological limit from the start.

## F-02 · Restarting from NTASP's work (2026-10-12)
- **Tried:** evaluating the pioneering public project "NT for Apple Silicon" (NTASP) as a base.
- **What happened:** it is not native (it uses the m1n1 hypervisor with emulated vGIC and PSCI), which did not fit our native-first goals; the AIC HAL extension is not implemented yet ("Nothing's implemented yet"); the AICv1 driver is still a stub (early-stage, actively developed); we found an issue in `AIC_MASK_BIT` (reported as a courtesy: NT-for-ASi/apple_silicon_platforms_mu#23).
- **Lesson:** before building on other people's code, check that its design fits your goals and not only that it exists (`re/ntasp-quality-audit.md`). Decision: independent route (ADR-0007).

## F-03 · "Just don't use FIQs" (ADR-0004), 2026-10-12/13
- **Tried:** designing an M1 in which Windows receives everything as IRQ.
- **What happened:** the **M1's ARM architectural timer is wired to FIQ only** (DTS `t8103.dtsi`, `AIC_FIQ`). There is no documented way to get it as IRQ, and macOS itself uses it as FIQ. The `ACC_OVRD_CONVERT_SIQ_TO_IRQ` bit exists in m1n1 but nobody has ever used it (`re/f0-m1-fiq-irq.md`, `re/f0-timer-irq-hw.md`).
- **Lesson:** the "no FIQ" hypothesis was wrong from the start for the timer. The remaining leads are in `docs/adr/draft-timer-fiq-strategy.md`.

## F-04 · The Raspberry Pi 3 precedent as proof (2026-10-13)
- **Tried:** using Windows on the Pi 3, which has no GIC, as proof that a non-GIC controller can be supplied from outside.
- **What happened:** the BCM2836 support was **inside Microsoft's HAL** (`edk2-platforms` `Platform/RaspberryPi/AcpiTables/Madt.aslc:51-52`: "Windows 10 on Raspberry Pi 3 does not boot otherwise... value that Microsoft had in their IoT blobs").
- **Lesson:** "Windows runs on a chip without a GIC" does not mean "a third party can add a controller". See also F-05.

## F-05 · HAL extensions for interrupt controllers (2026-10-13)
- **Tried:** looking for an official mechanism by which a HAL extension provides the interrupt controller.
- **What happened:** according to the author of `Wack0/NtHalExts`, HAL extensions for interrupt controllers *"were added in 789x but removed again in 799x"*, i.e. removed before Windows 8 RTM. His `halextbcm2709` works by pattern-searching the internal non-exported functions, and is tested only on Windows 8 ARM32. The only modern public examples (i.MX) are timer and DMA.
- **Lesson:** there is no official route; what remains is the one "with internal functions", which is fragile (`re/f0-nthalexts-lead.md`).

## F-06 · Removing the GIC in QEMU for the experiments (2026-10-12)
- **Tried:** using QEMU `virt` without a GIC.
- **What happened:** `gic-version` accepts only 2/3/4/x-5/host/max; `-acpitable` applies only to i386.
- **Lesson:** in QEMU "non-GIC" can only be simulated in the ACPI tables, so the experiments give a reliable "no" but only a weak "yes". A real bench without a GIC requires an AIC model written by us (`re/f0-qemu-aic-models.md`).

## F-07 · Paths with spaces (2026-10-13)
- **What happened:** EDK2's `edksetup.sh` and the QEMU monitor's `screendump` command fail if the path contains spaces (the disk is called "Arthur Fonzarelli").
- **Lesson:** use the link `/tmp/watari-data` (created by `tools/env.sh`). Better to give working disks names without spaces.

## F-08 · HVF acceleration with our EDK2 (2026-10-13, **SOLVED** 2026-10-14)
- **Tried:** booting Windows in QEMU with `-accel hvf -cpu host`, `-M virt,gic-version=3` and our EDK2 `edk2-stable202608` (DEBUG, CLANGDWARF).
- **What happened:** it hangs on the TianoCore logo ("Start boot option"); the serial stops at `[Bds]BdsWait(5)` and one CPU stays at 100%. The same firmware with TCG works and reaches the Windows Setup.
- **Wrong hypotheses ruled out** (useful to anyone trying the same): RELEASE build, `kernel-irqchip=off`, `gic-version=2`, `highmem=off`, `-smp 1` and timer masking (IMASK); the provisional patch `edk2-timer-imask.patch` was discarded.
- **Cause (verified):** with `-cpu host` under HVF, the `ID_AA64PFR0_EL1.GIC` field is 0 (read in the firmware: pfr0=0x1101000010110011), even though the GICv3 interface is usable. `ArmGicDxe` (`GicV3Supported`) decides GICv2/v3 only from that field, so it loaded the **GICv2** driver on a GICv3: no IRQs (PMR=0, IGRPEN1=0), the virtual timer stayed pending and `BdsWait` waited forever. The EDK2 firmware bundled with Homebrew works instead (it is a different build).
- **Solution:** `tools/edk2-hvf-gicv3.patch`: GICv3 even if the platform declares a redistributor base (`PcdGicRedistributorsBase != 0`); it is applied by `tools/build-edk2.sh`. **Verified by Opus:** with HVF the Windows Setup appears (`logs/screens/win-hvf-180s.png`).
- **Lesson:** on Apple Silicon with HVF the CPU ID registers may not tell the truth about GIC features. The firmware must not trust only those. Possible report to EDK2 (to be decided with Marco).

## F-09 · Download rules violated by agents (2026-10-13)
- **What happened:** two subagents saved temporary files (a copy of the Linux source `aic.c`, the parking spec PDF) in Claude's internal folders, despite the download ban in the prompts. The backup was confirmed and nothing ended up in the repository.
- **Lesson:** bans in the agents' prompts are not enough. You must explicitly say "no temporary files" and check afterwards.

## F-10 · Black screen mistaken for a stuck installation (2026-10-14, false alarm)
- **Tried:** checking with `screendump` the automatic Windows installation (HVF, ramfb, no display).
- **What happened:** after ~2 hours the screenshot was completely black, the disk was no longer being written and the serial was silent: it looked like a hang. In reality Windows had finished and the screen had turned off due to inactivity.
- **Lesson:** before declaring a hang, send a key (`sendkey shift` from the QEMU monitor) and take the screenshot again. Serial silence says nothing about the Windows kernel (serial debugging was not yet active/verified).

## F-11 · Windows does not boot with acpigen's MADT+GTDT (qemu-virt model) (2026-10-14) — SOLVED
- **Tried:** firmware `QEMU_EFI-watari.fd` (driver `WatariAcpiOverride`, which at ReadyToBoot replaces MADT and GTDT with those from `acpigen -m qemu-virt`) and booting the installed Windows 11 (N2), with HVF, disk `snapshot=on`. Windows loaded `ntoskrnl.exe` and hung (CPU 100%, TianoCore logo). With the original firmware: desktop in under 2 minutes (`logs/screens/n4w-base.png`).
- **Cause (verified):** our MADT was missing the **GIC MSI Frame** (subtable type 0x0D, QEMU v2m at 0x08020000, SPI base 0x50, count 0x40). Without MSI, Windows cannot use the MSI interrupts of NVMe/xHCI and hangs after loading the kernel (explanation is a hypothesis; the effect is verified by the ablation below). The field-by-field comparison is in `re/f11-acpi-comparison.md` (original tables read from the guest RAM).
- **Outcomes of the hypotheses** (script `/tmp/watari-data/run-f11/t.sh`, 4 minutes, `snapshot=on`; "hang" = still on the TianoCore logo at 230 s):
  - H0 (repeating the problem, firmware as it was): hang. Reproduced.
  - H1 (parking disabled: version 0, address 0): hang on its own. Not the cause.
  - H2 (GICD before the GICCs like QEMU; GICD/GICR already identical in value): hang (H1+H2). Not the cause.
  - H3 (GTDT flags like the original: only NS EL1 ALWAYS_ON, rest 0): hang (H1+H2+H3). Not the cause.
  - H4 (layout/length errors): none in the GICC/GICD/GICR fields (same offsets and values). Benign differences: MADT revision 6 instead of 4, GICC of 82 bytes instead of 80, OEM ID.
  - H5 (new, found through the comparison): adding the GIC MSI Frame (and PMU GSIV 23 in the GICCs): **desktop/login screen in under 120 s**. Ablation: the MSI Frame alone (PMU 0) is enough (lock screen at 120 s). The MSI Frame with parking v1, GICD after the GICCs and all GTDT flags ALWAYS_ON (i.e. F-11's original model plus MSI) reaches the desktop: parking, ordering and GTDT flags are therefore irrelevant with PSCI on QEMU/HVF.
- **V1 succeeded:** `logs/screens/v1-ok.png` (desktop; the "Sticky Keys" dialog is caused by our `sendkey shift` to wake the screen, see F-10). Final model: parking 0, GICD first, PMU 23, GICR Structure, MSI Frame, GTDT like the original.
- **Lesson:** before hypothesizing, compare with the original tables. The dump is done with `pmemsave` from the QEMU monitor and a search for `RSD PTR ` in the RAM (no UEFI shell needed).

## F-12 · Driver build with a stale layout (2026-10-14)
- **Tried:** modified `acpigen_cpu`/`acpigen_madt_opts` in `acpigen.h` and re-ran `tools/build-edk2-watari.sh`.
- **Failed:** EDK2 does not detect changes to the acpigen headers (path outside PACKAGES_PATH): `WatariAcpiOverride.c` stayed compiled with the old layout of `acpigen_model`, so it read `Model->gtdt` at a wrong offset: "acpigen_build_gtdt error -2" or a wrong pointer and QEMU/HVF aborting (`Assertion failed: (isv)` in `hvf_handle_exception`, at about 7 s, during the firmware).
- **Remedy:** `tools/build-edk2-watari.sh` deletes the module's build folder before compiling. If you see the `isv` abort right after "MADT acpigen installata", first check whether the driver is stale.

## F-13 · V6: timer in Group 0 at ExitBootServices restored to Group 1 (2026-10-14)
- **Tried:** `WatariTimerFiqDxe` puts PPI 27/30 in Group 0 at ExitBootServices (IGROUPR0 -> 0xB7FFFFFF, `ICC_IGRPEN0`=1, `GICD_CTLR.EnableGrp0`); Windows 11 with `kernel-irqchip=off` and GIC trace.
- **What happened:** the write happens (serial and trace `0x10080 = 0xb7ffffff`).
- **Failed:** in the trace, right after (`IGRPEN1=0`, `ICENABLER0=0xffff00ff`) comes `IGROUPR0=0xffffffff`: PPIs back in Group 1, before the timer is enabled. Zero `hvf_inject_fiq`: no FIQ delivered, so it is not known whether Windows would tolerate the FIQ. In addition the control (V1 firmware) with `kernel-irqchip=off` does not reach the lock screen but Windows RE (slow boot, "automatic repair"), so the test bench is degraded.
- **Why (hypothesis):** a GIC re-init after ExitBootServices (firmware or Windows HAL) resets the groups; who does it has not been identified.
- **Lesson:** method A alone is not enough. First identify the writer of IGROUPR0 (e.g. a trace with timestamp and PC, or a hook in the monitor), and understand why `kernel-irqchip=off` degrades the Windows boot; then decide whether method B (QEMU patch, not to be built without Marco's go-ahead) is needed.

## F-14 · V2: MADT without GICD, Windows does not reach the session (2026-10-14)
- **Tried:** acpigen model `qemu-virt-nogicd` (like qemu-virt: 4 GICC + GICR Structure + MSI Frame, **no GICD Structure**; MADT 412 bytes instead of 436), firmware `QEMU_EFI-watari-nogicd.fd` (`tools/build-edk2-watari.sh --nogicd`). Windows 11, HVF, default kernel-irqchip, `-smp 4`, `snapshot=on`, 240 s. Script `/tmp/watari-data/run-g2/t.sh`.
- **Result (verified):** the serial shows "MADT generata da acpigen: 412 byte, 4 CPU" and the replacement; Windows loads `ntoskrnl.exe` and then the screen stays identical (same hash) from 20 s to 220 s: still on the UEFI text/TianoCore logo (ramfb is no longer updated), no lock screen (V1, with the GICD, reaches it at 120 s), no visible bugcheck, no reset. Screenshots `docs/img/v2-nogicd-100s.png` and `v2-nogicd-220s.png`. It is the same symptom as F-11 before the MSI Frame.
- **Interpretation:** Windows **requires the GICD in the MADT** (confirms risk R-02). Limit: a hang, not a bugcheck code, so it is not known what the kernel is doing (probably the HAL/GIC init fails without the distributor base; hypothesis). A single 240 s run, without a parallel control (the V1 control is F-11).
- **Lesson:** on a SoC without a GICD (AIC) you must either provide a dummy/compatible GICD or write the HAL extension that replaces the GIC; the MADT without a GICD alone is not enough.

## F-15 · V3: parking protocol without PSCI, Windows uses it but bugchecks with HAL_INITIALIZATION_FAILED (0x5C) (2026-10-14)
- **Tried:** firmware `QEMU_EFI-watari-park.fd` (WatariParkingDxe + MADT parking v1 + FADT with PSCI_COMPLIANT=0), `-smp 4`, Windows 11, HVF, `snapshot=on`.
- **Verified:** serial: 4 mailboxes (0x46300000, 0x462F0000, 0x462E0000, 0x462D0000), `CPU_ON` cpu1..3 -> PSCI 0, FADT installed with PSCI=0, MADT 436 bytes with parking v1. About 17 s after boot Windows **writes** to CPU1's mailbox: `ProcessorId = 1` and `JumpAddress = 0x110af7010` (seen by the stub, which copies it to +0xFF8 before zeroing it as an ack). The mailboxes of CPU2 and CPU3 are never touched. About 15 s later (at ~32 s) bugcheck **HAL_INITIALIZATION_FAILED (0x5C)** with automatic reboot, in a loop (then "Preparing automatic repair"). Screenshot `docs/img/v3-bugcheck-hal5c.png`. Evidence: `/tmp/watari-data/run-v3/` (mbs.log, mb-v3c-*.bin).
- **Decisive outcome:** Windows ARM64 **uses the parking protocol** (it does not ignore it) even without PSCI, but the start of the first AP fails and the HAL does not initialize. No lock screen, so no measurement of the visible CPUs.
- **Why (hypothesis, to be verified):** CPU1 jumps to Windows' address but does not complete the handshake: the stub left the AP in a state Windows does not expect (X0 = mailbox address and not something else; cache/MMU; GIC/system registers; Processor ID read differently by Windows), or Windows waits for a wake-up SGI/IPI that the stub does not handle. The trace does not yet have the AP's PC (`info cpus` under HVF does not show the PC).
- **Lesson:** the V3 test is useful and the parking is recognized; the next step is to understand what Windows expects from the AP after the jump (GIC trace for the SGI, disassembly of Windows' stub at 0x110af7010 in the RAM dump, register state required by Microsoft's parking v1 spec).

### F-15, attempt A (last time-boxed attempt, 6 QEMU tests, 2026-10-14, branch `marco/a-ultimo-tentativo`)
Tools: `WATARI_PMODE` bits (build `--parking`; 1 FADT PSCI+HVC, 2 MADT without parking, 4 no CPU_ON, 8 stub without GIC, 16 no redistributor wake, 32 GPRs zeroed at the jump), stub diagnostics in the mailbox (+0xF00..0xF30), QEMU's gdbstub (`-gdb`, works under HVF: it returns GPRs and PC, NOT the updated system registers) and GIC trace with `kernel-irqchip=off`. Scripts and evidence in `/tmp/watari-data/run-a/` (`t.sh`, `gdbregs.py`, `trace-t6.log`, `mb-*.bin`, `shots/`).
- **H1 control (T1), RULED OUT as the cause:** acpigen FADT with PSCI_COMPLIANT|USE_HVC (arm_boot_arch=3) + MADT with parking_version 0, no CPU_ON from us: Windows reaches the lock screen (`docs/img/a-t1-controllo-fadt-psci-lock.png`, ~100 s). Our FADT is good; the 0x5C is tied to the parking. Limit: it was not measured how many CPUs Windows sees.
- **H2 core state at the jump (T2), RULED OUT:** the stub records the entry: CurrentEL=EL1 (HVF without virtualization, as the spec requires), SCTLR_EL1=0x30900180 (MMU/cache off), DAIF=0x3c0, MPIDR 0x80000001/2, ICC_SRE_EL1=7 already at entry, PMR=0, IGRPEN1=0. All conformant to Table 1 of the spec. With X0 = mailbox Windows really jumps: the AP arrives in kernel code (VA 0xfffff80...), cpsr 0x800003c5 (EL1h, DAIF masked). The mailbox contains the AP boot block prepared by Windows (+0x10.. TTBR/TCR/MAIR, +0x330 = VA of the start routine): the AP's PC is always routine+0x200, in 4 different boots (T2, T3, T5), so it is stopped in a short wait (WFE/WFI or a short loop), not crashed in a random exception.
- **H3 stub's CPU interface/GIC (T3, T5), RULED OUT:** stub without touching ICC, redistributors not woken by us, GPRs zeroed (PMODE=56): the AP stays stopped at the same point (+0x200). So it is not our GIC init (PMR=0xFF/IGRPEN1) that blocks it. Beware, measurement artifact: stopping and resuming the VM with gdb (T3, T5) makes the bugcheck disappear (screen stuck on the UEFI text up to 220 s): the BSP's timeout cannot be observed with gdb.
- **H3b wake-up interrupt / in-kernel GIC (T6), the idea "bug in HVF's in-kernel GIC" RULED OUT:** with `kernel-irqchip=off` (GIC in userspace) the 0x5C repeats (0x5C screens and boot loop). GIC trace, repeated at every boot: (a) the BSP (cpu 0) does its ICC init (IGRPEN1=0, PMR=0, IGRPEN1=1, PMR=0xF0); (b) cpu 1, after the jump, does IGRPEN1=0, PMR=0, `GICR1 ICENABLER0=0xffff00ff`, `GICR1 IGROUPR0=0xffffffff`, IGRPEN1=1, and then **writes nothing more** (the PMR=0xF0 that the BSP writes right after IGRPEN1=1 is missing); (c) the only SGIs generated are SGI 0 from cpu 0 with targetlist 0x1 and affinity 0, i.e. to itself (1 per boot): **Windows sends no SGI/IPI to cpu 1**, neither as a parking "doorbell" nor afterwards.
- **H4 CPU2/3 never touched:** consistent with the above: Windows starts one AP at a time and waits for the first to complete the handshake; AP 1 stays stopped and after ~15 s the BSP bugchecks. Nothing says that CPU2/3 are the problem.
- **Cause NOT found.** Status: the parking is recognized and the AP enters the kernel, but it stops in a wait right after its GIC init, and the BSP sends it no interrupt. Main suspicion [hypothesis, not proven]: the AP waits for an event/flag from the BSP (WFE/SEV on memory, or an interrupt that in the PSCI path arrives through another channel) and the BSP waits for the AP: deadlock. Other live hypotheses: the mailbox page mapped by Windows as Device while the AP reads it cacheable (coherency); the AP block waiting for something that with PSCI is already in the context_id.
- **Next steps (require more tests, the budget of 6 is over):** (1) identical trace (`kernel-irqchip=off`, same events) with the T1 control firmware (PSCI FADT, the cores are started by Windows): if in that case cpu 1 writes PMR=0xF0 and proceeds, the comparison tells what is missing; (2) reading the instruction at the AP's PC (routine+0x200) to know whether it is WFE/WFI or a loop on memory: this requires looking at Microsoft code, hence Marco's decision (rule "no analysis of Microsoft binaries"); (3) try to send an SGI to cpu 1 ourselves from the BSP stub/firmware after the jump (hack: repeat SEV/SGI from a driver).
- **Lesson:** `info registers`/sysreg via gdb are not updated under HVF (only GPRs and PC), and stopping the VM distorts the guest's timeouts: for timing tests use only GIC traces and `pmemsave`.

## F-16 · WatariParkingDxe: ASSERT in InvalidateInstructionCache (2026-10-14)
- **Tried:** `--parking` firmware built by task G4; booted with DEBUG.
- **Failed:** `ASSERT [WatariParkingDxe] ArmCacheMaintenanceLib.c(47)`: in `ArmCacheMaintenanceLib` the `InvalidateInstructionCache()` without a range is an `ASSERT(FALSE)`; G4's driver called it after preparing the mailboxes, so the firmware stopped at the driver load (no boot).
- **Remedy:** `InvalidateInstructionCacheRange(P, SIZE_4KB)`. Build redone (FV always 48 bytes free, it fits).
- **Lesson:** G4 had been verified only by building; drivers must be booted at least once. In DEBUG, the "whole cache" functions of ArmCacheMaintenanceLib are ASSERTs: always use the Range variants.
