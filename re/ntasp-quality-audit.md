# Review of NTASP (apple_silicon_platforms_mu) against our native-first goals, status as of 2026-10-12

NTASP is the pioneering public effort to run Windows on Apple Silicon; it is early-stage and actively developed, and this review is meant to record technical facts for our own design decisions, not to judge the project.

Method: read-only via `gh api` (contents, commits, trees, search) and no file saved in the repository. Repo: `NT-for-ASi/apple_silicon_platforms_mu`, HEAD `861dcc49` (2026-09-06, branch `main`).
Asahi references: `AsahiLinux/linux` `drivers/irqchip/irq-apple-aic.c`, `AsahiLinux/m1n1` `src/aic.c` + `src/aic_regs.h`, `arch/arm64/boot/dts/apple/t8103.dtsi`.
Labels: [verified] = read directly in the cited sources; [hypothesis] = deduction. Line numbers are those of the file at HEAD.
Abbreviated paths: `AIC/` = `Silicon/Apple/AppleSiliconPkg/Drivers/AppleAicDxe/`, `T810X/` = `Silicon/Apple/T810XFamilyPkg/`.

## 0. Summary verdict

| Component | Grade | Reason in one line |
|---|---|---|
| AppleAicDxe, AICv1 part (the one t8103 needs) | **substantial rework needed** | `AicV1/AppleAicV1Dxe.c` is a stub with `//TODO: this driver.`; the v1 path goes through the v2 code with wrong offsets |
| AppleAicDxe, AICv2 part and common glue | **substantial rework needed** (skeleton useful as a guide) | verified bugs in the register macros, in the event decode, in the FIQ/PMC handling |
| MADT_Static (T810X) | **substantial rework needed** | describes a virtual GICv3 (distributor, redistributor, ITS), hard-coded and invented values |
| GTDT (T810X and generic) | **substantial rework needed** | arbitrary GSIVs 17..20, watchdog address of another SoC, two GTDTs installed |
| FADT (j293) | **substantial rework needed** | PSCI via SMC "trapped" by the hypervisor; wrong minor revision field |
| CSRT (T810X) | **substantial rework needed (not yet written)** | the file is only comments and is not even in the INF |
| BERT (T810X) | **not usable as is** | does not compile (`BOOT_ERROR_REGION_BASE` undefined) and is not in the INF |
| PPTT (T810X) | **reusable with corrections** | topology consistent with the DTS; only what was intentionally removed (cache) is missing |
| DSDT j293 (CPU tree, UART, XHC) | **reusable with corrections** | valid structure, but UART IRQ differs from the DTS and XHCI HID is suspect |
| DBG2 j293 | **reusable with corrections** | correct in substance, alignment not verified |
| Package structure / DSC / FDF | **reusable with corrections** | standard Mu setup, but copy-paste leftovers and inconsistent configuration |
| Tests and CI | **not yet set up** | no tests, no workflows, 0 Actions runs |

Conclusion: the UEFI-side NTASP code was written for a hypervisor-first approach and would require substantial rework for our native path, so we do not reuse it directly. Ideas and data are reused (register list, CPU/cluster tree, PPTT, MMIO maps after checking them against the DTS), not the code. Details below.

## 1. AppleAicDxe

### 1.1 Declared status
- `AIC/Readme.md` lines 45-47: "AICv2 driver finished, AICv1 in progress". Line 7: "work in progress, bugs are expected". Line 33: it is recommended to use a hypervisor with vGIC "while the AIC support is experimental". [verified]
- The t8103 uses AICv1 (ADT `aic,1`, Linux compatible `apple,t8103-aic`, `aic1_local_fipi_info`; m1n1 `aic_init`: "aic,1" => v1). [verified in irq-apple-aic.c lines 265-330 and m1n1 aic.c]. So for our SoC the "finished" part (v2) is not the right one.

### 1.2 Verified defects (with evidence)

D1. AICv1 not implemented. `AIC/AicV1/AppleAicV1Dxe.c` lines 19-23: the function contains only `//TODO: this driver.` and `return EFI_SUCCESS`. `AIC/AicV1/AArch64/AppleAicV1.S` contains only an `#include`. In `AIC/AppleAicDxe.c` lines 41-43 `APPLE_AIC_VERSION_1` falls into the `case` of `AppleAicV2DxeInit(...)`: v1 runs on the v2 code. [verified]

D2. Wrong IRQ mask macro. `Include/Library/AppleAicLib.h` line 103: `#define AIC_MASK_BIT(num) BIT(num) & GENMASK(4, 0)`. Expansion: `(1UL << num) & 0x1f`, with no outer parentheses. For `num >= 5` the result is 0, so `MASK_SET`/`MASK_CLR` receive 0 and no IRQ beyond 4 can be masked or unmasked, and `AppleAicReadInterruptState` always returns FALSE. m1n1 `aic.c` line 10 gets it right: `BIT((x) & GENMASK(4, 0))`. [verified] Maximum severity: the driver's only per-IRQ control is inert.

D3. Wrong v1 event register. `AIC/AicV2/AppleAicV2Dxe.c` line 195: for v1 `mAicV2EventReg = AicV2Base;` (offset 0). The v1 event register is `base + 0x2004` (Linux `AIC_EVENT 0x2004`, m1n1 `AIC_EVENT 0x2004`). `AppleAicLib.h` defines `AIC_V1_EVENT_REG 0x2004` but it is never used. [verified] Every ack on t8103 reads `AIC_RESERVED/VERSION` instead of the event.

D4. Wrong enable/config on v1. `AppleAicV2Dxe.c` lines 535 and 478: `AIC_V2_CONFIG` (0x14) is also written on v1. Linux enables `AIC2_CONFIG` only for `version == 2 || 3` (irq-apple-aic.c, init). On v1 an undocumented offset is written. [verified]

D5. v1 target CPU never programmed. Linux writes `target_cpu + i*4 = 1` for all v1 IRQs at init (irq-apple-aic.c lines 1082-1084) and `BIT(cpu)` for each IRQ that gets enabled (line 461). In Mu `AIC_TARGET_CPU` is used only as a "start offset" to compute the offsets (line 194); there is no write. [verified by absence in the files read] Without this the v1 IRQs are not routed to any CPU [hypothesis about the effect, the Linux behaviour is verified].

D6. Event not decoded, index out of bounds. `AppleAicV2Dxe.c` lines 247-248: `AicInterrupt` (raw event: die[31:24], type[23:16], number[15:0]) is used directly as an index into `AicRegisteredInterruptHandlers[...]`. Linux (lines 401-419) extracts `type` and `irq` with `FIELD_GET` and distinguishes IRQ (1) from IPI (4). Moreover the array is allocated with `NumIrqs` elements (`AppleAicCommonDxe.c` lines 123-126) while the checks use `MaxIrqs` (`AppleAicCommonDxe.c` line 52): out-of-bounds write/read is possible. No "until event == 0" loop as in Linux, and no handling of event 0 (spurious). [verified]

D7. Ack in FIQ context. `AppleAicV2Dxe.c` line 247: the read of the event register happens BEFORE distinguishing FIQ from IRQ. Reading the event "acks and masks" (comment at line 132): in a FIQ it consumes and masks a pending IRQ that is then not delivered (lines 258-336 do not handle it). Linux does not read the event in the FIQ branch. [verified]

D8. Wrong Apple detection. `AIC/AppleAicLib.c` line 30: `if (Midr & 0x61000000)` is a bit test, not an implementer comparison: 0x41 (ARM) & 0x61 != 0 passes. It should have been `((Midr >> 24) == 0x61)`. [verified] Also lines 44-50: `aic,3` is treated as v2 (Readme line 9 says that v3 is different), and if the `aic` node is missing `ASSERT(FALSE)` is used and then the NULL pointer is dereferenced anyway in RELEASE builds (lines 34-42 of AppleAicLib.c, 147-151 of AppleAicV2Dxe.c). [verified]

D9. Non-conforming PMC FIQ handling. `AppleAicV2Dxe.c` lines 321-328: it checks only `BIT11` (IACT) without verifying IMODE == FIQ, then writes bits 18/17/16/0 of PMCR0. Linux (lines 602-603) checks `(PMCR0 & (IMODE|IACT)) == (IMODE_FIQ|IACT)`. With the Mu code the IMODE is never brought to OFF, so a FIQ storm is likely [hypothesis: I did not read the definition of the PMCR0 bits in `asm/apple_m1_pmu.h`]. Lines 329-335 (UPMCR0) instead follow Linux.

D10. IPIs not implemented. `AppleAicV2Dxe.c` lines 259-268: fast IPIs are only "ack"-ed with a `DEBUG` ("Fast IPIs not supported yet"). The v1 legacy IPIs (`AIC_IPI_SEND 0x2008`, `ACK 0x200c`, `MASK_SET 0x2024`, `MASK_CLR 0x2028`) are defined in `AppleAicLib.h` (lines 62-68) but never used. The Readme (line 25) states that with AICv1 it "will defer to using legacy IPIs": this is not true in the code. No SW IPI (`AIC_EVENT_TYPE_IPI`, `AIC_CPU_IPI_*` 0x5008...). [verified]

D11. Memory barriers absent. The driver has no `DsbSy`/`Isb`/`MemoryFence`. For device-nGnRnE MMIO, `MmioRead/Write32` are enough for ordering, but Linux uses non-relaxed `readl` in the ack precisely to order DMA reads after the IRQ, and does `isb()` after the sysreg writes of the FIQ timers. The Mu driver does not (lines 247, 128, 284). [verified for absence; the practical need is a hypothesis]

D12. Ignored errors. `Status = AppleAicV2CalculateRegisterOffsets();` (line 508) is never checked; `AllocatePool` of `AicInfoStruct` (line 501) is not checked for NULL; in `CpuArchProtocolNotify` (`AppleAicCommonDxe.c` lines 95-104) the `Status` of the first call is overwritten before the check; `AppleAicV2SetIrqTriggerType` returns `EFI_SUCCESS` without doing anything (lines 434-435). [verified]

D13. Layering. `AppleAicLib` is a *library* but uses `extern AIC_INFO_STRUCT *AicInfoStruct` defined in the *driver* (`AppleAicV2Dxe.c` line 25) and its own static state `mAicVersion` duplicated in two files (`AppleAicLib.c` line 19 and `AppleAicV2Dxe.c` line 30). The DSC itself has the TODO "merge AppleAicLib into AppleAicDxe" (`AppleSiliconPkg.dsc.inc` line 210). [verified]

D14. Hard-coded FIQ timer. Sources 17/18/19 are invented software numbers, filtered in 4 different places (`AppleAicLib.c` 161, 198; `AppleAicV2Dxe.c` 126, 283-305); `ExitBootServices` (lines 458-480) masks the IRQs and disables the AIC controller but does not touch the FIQ timers. Native Windows would expect a controller that is still enabled [hypothesis]. [verified for the code]

D15. Dead code and TODOs. `AppleAicDxe.h` line 50 "TODO: AICv1 API"; `AppleAicDxe.c` lines 45-48 "TODO: add AICv3"; `AppleAicV2Dxe.c` line 88 ("figure out if we need to read HW_STATE or IRQ_CFG"), 216 ("what is this reg meant for?"), 404 ("some interrupts are level low... account for these"), 551-561 (commented-out block for disabling IRQs on the secondary cores); `AppleAicLib.h` line 80 empty "AICv1 bitmasks" section; `AppleAicLib.c` line 233 `DEBUG` without `\n`. Multi-core explicitly not supported (line 502, "multi-core support not added yet"). [verified]

### 1.3 What is correct (to be fair)
- Computed offsets: for v1, after `AIC_TARGET_CPU 0x3000 + 4*1024` one obtains SW_SET 0x4000, SW_CLR 0x4080, MASK_SET 0x4100, MASK_CLR 0x4180, HW_STATE 0x4200, consistent with m1n1's `aic_regs.h` (lines `AIC_SW_SET`...`AIC_MASK_CLR`). The formula reproduces that of m1n1 `aic23_init` (lines 102-112) and Linux (lines 1007-1030). [verified]
- `AIC_INFO 0x0004` and `AIC_MAX_IRQ 0x400` for v1 are right; the v2 registers (INFO1/INFO3, DIE fields) are right. [verified]
- The timer handoff sequence towards `TimerDxe` (check of `CNTP_CTL`: ENABLE & !IMASK & ISTATUS) follows Linux's `TIMER_FIRING`. [verified]
- The protocol registration glue (`HardwareInterrupt`/`HardwareInterrupt2`, `ExitBootServices` event, notify on CpuArch) follows the `ArmGicDxe` scheme and is functional as a skeleton.

### 1.4 History
`git log` chronology on the path (12 commits): `a701dbed` 2023-01-22 (folder move; the earlier history was lost in the path), ... `474e0aac` 2023-11-13 "remove redundant code in AICv2 driver" (+2/-3), then only general maintenance commits (`8dbbcad5` 2026-05-29 "fix Mu build", 21 files). No functional change to the AIC driver after November 2023; `AicV1` is stuck at the headers of 2022-09-29. [verified] The author himself disabled AIC by default on M2 (`2c5ab300` 2025-11-15 "disable AIC_BUILD by default for MacBook Pro Early 2023"), but left it enabled by default on the MacBook Pro 2020 (`MacBookProLate2020.dsc` line 35: `AIC_BUILD = TRUE`). [verified]

### 1.5 Grade: substantial rework needed
For a driver we can trust, at least the following are needed: event decode, ack loop, TARGET_CPU programming, correct v1 init, v1 IPIs and fast IPIs, barriers. This is more than 50% of the driver. Better to write it new, looking at m1n1 `aic.c` (MIT/GPL, see license) and Linux as reference, and at most to take the glue skeleton (`AppleAicCommonDxe.c`) after fixing D6, D12.

## 2. T8103 / j293 ACPI tables

The platform "MacBook Pro 13 M1 (j293)" in the repo is `Platform/MacBookProLate2020Pkg` (SMBIOS `MacBookPro17,1`) which includes `T810XFamilyPkg` and `MacBookProTouchBarFamilyPkg`. Note: the whole j293 part was added on 2025-10-26/27 by external contributors (`7c469c3b`, `77a353e9`, `0a1985bf`, `eca3d937` by Jamison Smith) and polished with a series of fix commits. [verified]

### 2.1 MADT (`T810X/AcpiTables/MADT_Static.aslc`, `19df1750` 2025-10-27 "Add WIP SMP work", +114/-8)
- Header: "temporary implementation until dynamic allocations in AcpiPlatformDxe are implemented" and "for the Mac Mini platform" (lines 8-11), for a file also used on MacBook. [verified]
- Mixes types 6.3 and 6.4: `EFI_ACPI_6_3_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER`, `EFI_ACPI_6_3_GIC_STRUCTURE`, `EFI_ACPI_6_0_GIC_DISTRIBUTOR_STRUCTURE`, `EFI_ACPI_6_4_GICR_STRUCTURE`, `EFI_ACPI_6_4_GIC_ITS_STRUCTURE` (lines 53-59) with table revision 6.3. Allowed but inconsistent. [verified]
- Core MPIDRs: E-core `GET_MPIDR(0,0,0,i)` => 0x0..0x3, P-core `GET_MPIDR(0,1,1,i)` => 0x10100..0x10103 (macro in `Include/Acpi/AcpiHeader.h` lines 46-50). They coincide with `cpu@0..3` and `cpu@10100..10103` of `t8103.dtsi` (lines 63-161). [verified] This is the good part.
- Hard-coded values: PMU IRQ per core 50, 54, 54, 54, 58, 62, 34, 38 (lines 85-107) with no comment on their origin; the values 34/38 on cores 6 and 7 break the pattern. vGIC maintenance GSIV = 25 on all cores. Asahi's DTS does not use these numbers. [verified for the value; the origin is a hypothesis: numbers chosen for m1n1's vGIC]
- Copy-paste comments: "Icestorm-2" repeated for core 3 (line 93). [verified]
- Distributor `0xf00000000`, redistributor `0xf10000000` (from `T810XFamilyPkg.dsc.inc` lines 46-53, comment: "virtualized by trusted firmware... T8103 has no onboard GIC"), fixed ITS `0xF20000000` (line 113, not derived from a PCD). These are addresses invented by the hypervisor, not real MMIO. [verified]
- Build conflict: the DSC enables `AIC_BUILD = TRUE` for j293, and in that case `PcdGicDistributorBase/PcdGicRedistributorsBase` are not set (guard `!if $(AIC_BUILD) == FALSE`), but `CPUAcpiTables.inf` consumes them anyway and `MADT_Static` is inserted unconditionally in the FDF (`MacBookProLate2020.fdf` line 231). So with AIC the MADT describes a GIC at the PCD default value [hypothesis: the default in ArmPkg's `.dec` is 0]. [verified for the build structure]
- Checksum: `__ACPI_HEADER` (`AcpiHeader.h` lines 34-44) puts 0; the computation happens at installation (`AcpiPlatform.c` line ~440: `CalculateCheckSum8`) and by the `AcpiTableDxe` protocol [verified for `AcpiPlatform.c`; the effect on the static MADT is a standard EDK2 hypothesis].
- Parking protocol: no field populated (Parking Protocol Version and Parked Address not handled in `EFI_ACPI_6_3_GICC_STRUCTURE_INIT`; search for "park" in the repo = 0 results). [verified for the absence]
Grade: substantial rework needed. The GIC/ITS block must be removed, and the rest derived from the ADT/our design.

### 2.2 GTDT (`T810X/AcpiTables/GTDT.aslc`, `Silicon/Apple/AppleSiliconPkg/AcpiTables/GTDT.aslc`)
- Timer GSIVs: Secure PL1 = 19 (`T810XFamilyPkg.dsc.inc` line 14, "placeholder"), phys = 17, virt = 18, hyp = 20 ("this disables the HV timer"). These are software numbers: on a real GIC they would be PPI 1..4; the standard GIC physical timer is 30 (PPI14), the virtual one 27. Flags: level, active-high, `LOSE_CONTEXT`. [verified]
- Watchdog: `WATCHDOG_COUNT 0` (line 62), so not emitted, but the defined addresses are `0x2922B0010` / `0x2922B001C` and GSIV `0x277` (lines 65-67), while the t8103 watchdog is at `0x23d2b0000` (`t8103.dtsi` line 1223). The address is from another SoC (probably t600x). [verified for the comparison; the attribution to t600x is a hypothesis]
- The comment at line 48 cites the Linux Apple WDT driver; reset sequence (0x4 to +0x1c, zeros to +0x10/+0x14) not verified. [hypothesis]
- Duplicate: the `FDF` installs both `GenericAcpiTables.inf` (which has `GTDT.aslc`, `GenericAcpiTables.inf` lines 19-20) and `CPUAcpiTables.inf` (which has `GTDT.aslc`) (`MacBookProLate2020.fdf` lines 228 and 231): two GTDTs in the firmware. ACPI allows only one GTDT. [verified; effect on Windows is a hypothesis]
- History: `038ceb36` "Temporary use standard timer interrupts" and `e27ee083` "fix GTDT for T602x" show that the value was changed several times without explanation. [verified]
Grade: substantial rework needed.

### 2.3 CSRT and BERT (T810X)
- `T810X/AcpiTables/CSRT.aslc` (650 bytes): only the license header and the comment "Why do we need a CSRT?", no data structure, no `#include`. `CPUAcpiTables.inf` lines 19-22 lists only `GTDT.aslc`, `MADT_Static.aslc`, `PPTT.aslc`. So the CSRT is never generated for T810X. [verified] This contradicts the reading "the CSRT signals the AIC to the OS" in `ntasp-analysis.md` section 3, if intended as working for M1: it is only an intention. (T600X has a 1066-byte CSRT, not verified.)
- `T810X/AcpiTables/BERT.aslc`: uses `BOOT_ERROR_REGION_BASE` (line 36) while the `#define` is commented out (line 25): does not compile, is not in the INF. Comment "Original source file from AmpereAltraPkg", "Tonga/Sicily". [verified]
Grade: CSRT to be written from scratch; BERT not usable as is.

### 2.4 PPTT (`T810X/AcpiTables/PPTT.aslc`, `19df1750`/`44cd132c` Adrian Dumitru)
- Hierarchy package -> 2 clusters -> 4 cores, ACPI Processor ID = `Cluster*4 + Cpu` (line 32) consistent with the `AcpiCpuUid` 0..7 of the MADT and DSDT. Lengths 0x14 and `OFFSET_OF` for parents correct. Caches removed on purpose (`024391f5` "Simplify PPTT, remove cache info"). [verified]
- Depends on `PPTT_PROCESSOR_*_FLAGS` macros that come from `Acpi/SgiAcpiHeader.h` (header taken from Arm SGI) [verified]; the actual flags were not read [hypothesis that they are correct].
Grade: to be reused with corrections (verify the flags, add L1/L2 cache if needed).

### 2.5 DSDT j293 (`Platform/MacBookProLate2020Pkg/AcpiTables/DSDT.asl`)
- Structure: `DefinitionBlock("DSDT.aml","DSDT",0x02,"Apple","J293",0x8103)`; tree SOC > CLU0 (E: CPU0-3) > CLU1 (P: CPU4-7) with `ACPI0010` (processor container) and `ACPI0007` (processor), `_UID` consistent with the MADT. Valid. [verified]
- UART `COM0` (`APPL8900`) at `0x235200000` correct (DTS line 878), but interrupt `1097` (line 190) against `AIC_IRQ 605` of the DTS (line 883). For XHC1, instead, `857` is identical to the DTS (line 1612), with `0x502280000` correct. [verified] I do not know whether 1097 is a virtual GIC number (does m1n1 map IRQs with an offset?) [hypothesis]; in any case it is inconsistent with the rest.
- `_HID "PNP0D15"` for XHC1 (lines 140, 107): the standard ID for xHCI is `PNP0D10`; `PNP0D15` is AHCI [hypothesis, from memory of the PNP ID registry, to be checked]. Windows would bind the wrong driver.
- `CLPI`/`PLPI` (lines 26-103) defined but the `_LPI` methods are commented out; the FADT declares `LOW_POWER_S0_IDLE_CAPABLE` (FADT line 73): inconsistency. The XHC0 block is commented out (lines 105-137). [verified]
- Repeated fix commits: `e52e310a` "Fixed some mild DSDT issues" (+69/-13), `a119b0bc` "Add usb-drd1 to DSDT", `6d1a9f54` "Revert interrupts since they seem to work now". [verified]
Grade: to be reused with corrections.

### 2.6 FADT (`Platform/MacBookProLate2020Pkg/AcpiTables/FADT.aslc`)
- Hardware-reduced ACPI, `PM_PROFILE_MOBILE`. Correct as a setting. [verified]
- `ARM_PSCI_COMPLIANT` without `HVC` (line 76): SMC conduit. Comments lines 24-33: "PSCI ... SMC event despite not being implemented on Apple, as we can trap SMCs to EL2... the recipient will be our custom hypervisor". So the FADT presupposes a hypervisor that emulates PSCI. [verified] In the native path without PSCI something else is needed (parking protocol) and the flag must be removed.
- The "Minor revision of FADT" field (line 77) is populated with `EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_REVISION` (which is the major revision, 6) instead of the minor constant (4): it announces ACPI 6.6. [hypothesis on the constant: in EDK2 the two constants are distinct; verified that the file uses the `_REVISION` constant in that field]
- `EFI_ACPI_6_3_PWR_BUTTON` set (flag "power button as control method") without any button device in the DSDT. [verified]
- DSDT/FIRMWARE_CTRL addresses at 0: filled in by AcpiTableDxe (EDK2 practice) [hypothesis]. Reset register `NULL_GAS` with a TODO about the Apple watchdog. [verified]
Grade: substantial rework needed for the native path.

### 2.7 DBG2 j293
Follows the AmpereAltra scheme, subtype 0xA (in the DBG2 registry it is the Samsung-like type, consistent with the Apple S5L/Samsung UART) [hypothesis on the subtype name], namespace `\_SB.COM0`, base from `PcdAppleUartBase` (0x235200000, correct) and length 0x1000. `#pragma pack()` at line 101 without a matching `#pragma pack(1)` in the file: the `DBG2_DEBUG_DEVICE_INFORMATION` struct might have padding before `AddressSize` (the offsets use `OFFSET_OF`, so they stay self-consistent) [hypothesis]. Grade: to be reused with corrections.

## 3. General quality

### 3.1 Structure and style
- Standard Mu layout (Platform/, Silicon/Apple/{AppleSiliconPkg, T8xxxFamilyPkg}), with platforms derived through includes (`*.dsc.inc`, `*.fdf`). The idea of reuse by families is right (README: "strong emphasis on code reuse"). [verified]
- Evident copy-paste leftovers: `MacBookProLate2020.dsc` line 1 "Mac Mini 2020 UEFI DSC file" and line 48 `PcdAppleNumDwc3Controllers|2 # M1 Max case is hardcoded for now`; `MADT_Static.aslc` "for the Mac Mini platform"; `T810XFamilyPkg.dsc.inc` lines 33-37 and 39-43 identically duplicate `PcdCoreCount`/`PcdClusterCount`; CSRT/BERT/GTDT with "Tonga"/"Sicily"; `PlatformBuild.py` almost identical across platforms (sizes 10.8-11.2 KB). [verified]
- Declared incomplete coverage: `README.md` Status: "boots off external mass storage (not without hacks...), Windows loader initializes the RAMDisk but hangs at the handoff between bootloader and kernel". Disclaimer: "I probably do not know what I'm doing 100%". [verified]
- Code style: mixed indentation (2 and 4 spaces), blocks of commented-out code (D15), `Aic*` names that are renamed former `*GicDxe`. [verified]

### 3.2 Tests and CI
- No tests: no file with `test` in its path (search in the recursive tree). No `.github`, `.azurepipelines`, `.pipelines` folder; `gh api actions/runs` => `total_count: 0`. [verified] The build depends on Mu's `PlatformBuild.py` run by hand.
- There are 3 open issues, repo "size" ~12 MB (without submodules). [verified]

### 3.3 Licenses and provenance
- Repo license BSD-2-Clause (`LICENSE` with the copyrights of Linaro, Red Hat, ARM, Intel, Apple Inc., EFIDroid, Bingxing Wang, Renegade Project). New files use `BSD-2-Clause-Patent` (AIC) or `BSD-2-Clause-Patent OR MIT` (ACPI). [verified]
- Problems: `AppleAicLib.c`, `AppleAicDxe.h`, `AppleSysRegs.h` have no `SPDX-License-Identifier`. `ConvenienceMacros.h` line 19 "borrowed from m1n1" under BSD-2-Clause-Patent. `m1n1/src/aic.c` line 1 carries `SPDX-License-Identifier: GPL-2.0-or-later` and the whole driver follows the layout and logic of `aic.c`/`irq-apple-aic.c` (GPL-2.0) under a permissive license, with only one "Credits to the Asahi Linux contributors" in `AppleAicDxe.c` line 12 and no provenance header. [verified for the two licenses; the incompatibility risk is my assessment, not legal advice]. For us: do not copy the file; redo it from the specification and document the provenance (CLAUDE.md requires a provenance header).
- The `.dsc` file cites "WOA-Project/SurfaceDuoPkg" and "mu_andromeda_platforms" as origins. [verified]

### 3.4 Commit frequency and quality
- Contributors: 3 people (amarioguy 343 commits, JamiNeedsSleep 22, graphine27 21 according to `ntasp-analysis.md`); the M1 platforms were added by an external contributor in 2 days (2025-10-26/27) with 15 commits such as "Update DSDT.asl" x3, "Try 2", "Fixed ACPI stuffs", "Fixed ACPI tables (again) but i didnt loose my brain cells", "Fixed some weird acpi GUID errors", "Updated GUID so stuff wont freak out", then fixups by amarioguy. [verified in the messages]
- Maintainer's messages: "miscellaneous changes" (`3c4e19de`), "changes as of 6/11/2024 (GPIO and PCIe drivers verified somewhat working, working on USB-A controller bringup)" (`4f2cb26c`), "USB is being detected, bringup almost complete" (`e770157e`). Large and mixed commits: `5d12c63b` 2023-05-07 "add Apple Silicon ACPI platform driver" +543/-13 in 13 files; `8dbbcad5` 2026-05-29 "fix Mu build" 21 files. [verified]
- Activity: last commit `861dcc49` 2026-09-06 ("do not reset on memory type information change"); long gaps (2024-09 -> 2025-08; 2025-12 -> 2026-05). [verified] No review history (minimal PRs).

## 4. Dependence on the hypervisor

They presuppose emulated vGIC/PSCI [all verified in the indicated files]:
1. FADT: PSCI-compliant, SMC conduit "trapped to EL2", comment "recipient will be our custom hypervisor" (FADT lines 24-33, 76).
2. MADT: full GICv3 with ITS (distributor/redistributor/ITS at virtual addresses), 8 GICCs with PMU IRQ and vGIC GSIV (`MADT_Static.aslc`).
3. DSC: `PcdGicDistributorBase|0xf00000000`, `PcdGicRedistributorsBase|0xf10000000` (`T810XFamilyPkg.dsc.inc` 46-53); `HwResetSystemLib` = `ArmPsciResetSystemLib` (`AppleSiliconPkg.dsc.inc` line 356); maps `AppleArmGxfHvcLib` (`Library/AppleArmGxfHvcLib/*`).
4. GTDT: GSIV for the architectural timer PPIs on a GIC basis (`GTDT.aslc`, `T810XFamilyPkg.dsc.inc` 14-17).
5. `PrePi/PrePi.c` line 41 and `AppleArmGenericTimerPhyCounterLib.c` line 96: comments/assumptions "running inside m1n1 or another hypervisor" / "issue reboot command through hypervisor shell" (full context not read) [verified for the presence of the comments].
6. The AIC driver's README says to prefer the vGIC (`Readme.md` 33). The AIC path is the "experimental" one.

To rework for the native path: MADT (remove GICD/GICR/ITS, decide what Windows sees as the interrupt controller: HAL extension and CSRT topic, see `ntasp-analysis.md`), GTDT (no GIC, FIQ), FADT (no PSCI, a parking protocol or something else is needed), reset/shutdown, SMP. Can be kept: PPTT, the DSDT CPU tree, DBG2, the cores' MPIDR, the MMIO map (after checking against the DTS). The parking protocol exists neither as code nor as a populated MADT field (search "park": 0 results). [verified]

Rough estimate [hypothesis]: on the ACPI/firmware side, ~70% of the ACPI files would have to be rewritten for native; the AIC driver must be rewritten anyway for v1.

## 5. The 5 most significant issues for our use

1. **AICv1 (our SoC) is a stub**: `AicV1/AppleAicV1Dxe.c` 19-23 "TODO: this driver"; `AppleAicDxe.c` 41-43 sends v1 to the v2 code. (D1)
2. **Wrong `AIC_MASK_BIT`** (`AppleAicLib.h` 103): `BIT(num) & GENMASK(4,0)` nullifies masking/unmasking for all IRQs >= 5; m1n1 line 10 is correct. (D2)
3. **Wrong v1 event and config registers and missing event decode**: event=`base` instead of `base+0x2004`; CONFIG 0x14 instead of nothing; raw event used as an array index (OOB, also for `NumIrqs` vs `MaxIrqs`). (D3, D4, D6)
4. **Ack read in FIQ context** (line 247) with loss of IRQs, **PMC FIQ not cleared correctly**, IPIs not implemented (only ack and DEBUG), no v1 TARGET_CPU programming. (D5, D7, D9, D10)
5. **Hard-coded ACPI tied to the hypervisor, not verified against the DTS**: MADT/GTDT with virtual GIC and invented numbers; duplicated GTDT (generic + T810X); nonexistent CSRT and BERT that does not compile; DSDT UART IRQ 1097 vs 605 of the DTS; GTDT watchdog at another SoC's address; FADT PSCI/SMC-to-EL2 and wrong minor revision; tests and CI absent. (sec. 2, 3.2, 4)

Mention: `MIDR & 0x61000000` as the vendor test (D8), and `AIC_BUILD = TRUE` by default for j293 with a MADT that is always GIC (sec. 2.1).

## 6. Recommendations

1. Do not reuse the AppleAicDxe code; write our own AICv1 driver from `m1n1/src/aic_regs.h` and `irq-apple-aic.c` (evaluating the license first) with tests on QEMU or on the m1n1 hypervisor. The idea of the glue (`RegisterInterruptSource`, `CpuArchProtocolNotify`) can be copied from the `ArmGicDxe` model, not from Mu.
2. From the NTASP repo recover only the *data*: CPU/cluster structure (PPTT, DSDT tree), MPIDR, XHC/UART addresses already coinciding with the DTS, and compare every IRQ with Asahi's DTS before inserting it.
3. Write our own MADT/GTDT/FADT/CSRT, generated from the ADT (or with verified values), without a virtual GIC and with a parking protocol.
4. If the repo is reused as the base of Mu firmware, introduce CI (even just a build in Actions) and unit tests of the decoders (AIC event, `BIT`/`GENMASK` macros) before trusting any part.
5. Limits of this audit: I did not read `AcpiPlatform.c` in full, `PrePi.c`, `MemoryInitPeiLib.c`, `PlatformBuild.py`, DART/PCIe/USB/NVMe; I did not compile anything nor run tests; the judgments on `PNP0D15`, revision constants and PMCR0 bits remain hypotheses until the sources are checked (ACPI spec/PNP ID registry/`asm/apple_m1_pmu.h`).
