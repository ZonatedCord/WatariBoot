# F0 / A4: the Raspberry Pi 3 precedent (Windows without a GIC)

Issue #7. Read-only research (`gh api`, WebSearch/WebFetch). No download, no Microsoft binary.
Convention: [verified: source] or [hypothesis].

## Summary

On the Pi 3 (BCM2837) Windows did not receive the interrupt controller from a third-party component.
The ACPI firmware describes the BCM2836 "ARM local" as if it were a GIC (GICC with base 0x40000000), and the **support
for the Broadcom controller is inside the Windows HAL** (arm32 for IoT Core, arm64 for the later ARM64 builds).
In the repositories examined there is no HAL extension, no third-party driver and no CSRT group for the interrupt controller.
The precedent therefore does NOT show that a third party can supply an interrupt controller to Windows ARM64.

## Evidence collected

### 1. The fake Pi 3 MADT (the main evidence)
- [verified: edk2-platforms `Platform/RaspberryPi/AcpiTables/Madt.aslc`] The Pi 3 MADT contains 4 GICC structures (one per core) and, only for Pi != 3,
  the GIC Distributor (`#if (RPI_MODEL != 3)`). So on the Pi 3 **there is no GICD**.
- [verified: same file + `RPi3.dsc` lines 345 and 424] The GICC "Physical Base Address" field is `PcdGicInterruptInterfaceBase = 0x40000000`,
  i.e. the address of the BCM2836 "ARM local" registers (not a GIC). `PcdInterruptBaseAddress` has the same value 0x40000000.
- [verified: same file, `RPi3.dsc` lines 425-428] `PerformanceInterruptGsiv` = 9 for all cores (in the BCM2836 local numbering, 9 = PMU_FAST;
  [verified: Linux binding brcm,bcm2836-l1-intc, reported by the web search]).
- [verified: comment in `Madt.aslc`] "Use 1 for GICR Base Address below, since Windows 10 on Raspberry Pi 3 does not boot otherwise, and this is
  the value that Microsoft had in their IoT blobs." That is, Windows reads the GICC fields and a wrong value prevents boot.
- [verified: andreiw/RaspberryPiPkg `AcpiTables/Madt.aslc`, binary blob taken from the MS-IoT tables] Comment: "Even though the BCM2836 doesn't contain a GIC, these
  GICC definitions enable multi-core support (with PSCI). Mind the signatures in the header, they must be kept."
  Byte decoding (mine): OEM ID "BC2836", OEM Table ID "EDK2    ", Creator ID "MSFT", revision 3, length 0x16C = 44 + 4 x 80;
  GICC with flags=1, PerfGsiv=9, ParkedAddress=0, ParkingVersion=0, PhysicalBase=0x40000000, GICV/GICH/VGIC=0.
- [hypothesis] The HAL recognizes the Pi 3 from the fields above (base 0x40000000, GICR=1 and/or OEM ID "BC2836") and not from a driver; I have no evidence on which field is decisive.
- [verified: pftf/RPi3 Readme, tianocore RPi3 Readme] Windows 10 ARM64 (full GUI) is reported as bootable with this firmware without other components.
  Linux in ACPI mode instead crashes in `acpi_register_gsi()` ([verified: commit message in the pftf/RPi3 Readme]), a sign that the "GIC" is fake.

### 2. No HAL extension or driver for the interrupt controller
- [verified: `gh api repos/ms-iot/rpi-iotcore/contents/drivers`] The drivers are: RpiLanPropertyChange, audio, gpio, i2c, mailbox, misc, pwm, sd, spi, uart. No HalExt,
  no interrupt controller driver. The `interrupt.c` files found belong to the mailbox, vchiq, PL011 and PWM/DMA drivers (device ISR handlers, not the controller).
  `raspberrypi/windows-drivers` has the same structure (same directory list).
- [verified: `worproject/RPi-Windows-Drivers`] The repository contains only a README and binary releases. The README talks about peripheral drivers and DSDT/SSDT overrides, never about an interrupt controller.
  Limit: the binaries of the releases were not inspected.
- [verified: CSRT in `Csrt.aslc`] The CSRT contains a single resource group, the DMA one (DeviceId DMA, controller + channels). No group for interrupt controller or timer.
  It is the "Microsoft" part (Microsoft copyright in the file), used for DMA (`EFI_ACPI_CSRT_DEVICE_ID_DMA`).
- [verified: `Pep.asl`] The PEP is a virtual `BCM2854` device without resources; it describes processor, coordinated states and devices for power management, not interrupts.
- [verified: `edk2-platforms/Platform/RaspberryPi/Drivers`] `InterruptDxe` (Bcm283x) serves only UEFI (only the 4 local timer interrupts, register +0x40); it disappears at ExitBootServices.

### 3. Who supported the BCM in the HAL
- [verified: issue andreiw/RaspberryPiPkg#12, andreiw, April 2018] "there appears to be a check in Hal validating PIC is not a BCM PIC. That's why you need a debugger attached
  to patch out the 'bls' following the subtraction and compare." So in the April 2018 ARM64 builds (17125-17134) the HAL **knew** the Broadcom PIC
  but rejected it; to boot, an in-memory patch with WinDbg was needed. Andreiw: "science project, given the need to hot-patch Win10 Hal today just to boot".
- [verified: same issue, thchi12] Quote from a user: "Windows has built in support for BCM interrupts" (from a zhihu post; not personally verified). Another user states that
  the RS5 builds above 17723 "should have official support for RPi3 boot" (third-party claim, not confirmed by Microsoft).
- [hypothesis, medium confidence] From the RS5 build onward the ARM64 HAL accepted the BCM PIC without a patch: in later sources (pftf/RPi3, WoR) there is no longer any trace of HAL patches,
  and the comment on the "GICR=1" value says that boot depends on the MADT of the MS blobs.
- [verified: andreiw readme] The HypDxe project (EL2 hypervisor that moves UEFI to EL1, intercepts SMC/PSCI, MMIO and kernel logs) was for debugging and research; its author says it
  was needed "to eventually support MMIO traps so I could emulate a GIC" (issue #130). HypDxe is **not** in edk2-platforms/pftf RPi3 ([verified: list `Platform/RaspberryPi/Drivers`]), so the final boot does not require it.

### 4. Secondary core startup
- [verified: issue #12, andreiw] The original MS-IoT ACPI blobs reported **MPPP** (Multiprocessor Parking Protocol) and not PSCI in the FADT (IoT Core was ARM32).
  For ARM64 andreiw used **PSCI**: "Apr 24th 2018: SMP support in WoA" ([verified: readme]).
- [verified: `Fadt.aslc`] FADT with `EFI_ACPI_6_3_ARM_PSCI_COMPLIANT` (ArmBootArchFlags) and `HW_REDUCED_ACPI`. The method is PSCI CPU_ON handled by ARM Trusted Firmware (BL31) in the firmware.
  In the MADT: `ParkedAddress=0`, `ParkingProtocolVersion=0`, MPIDR of each core present (`GET_MPID(0,n)`).
- Without a GIC the wake-up does not use GIC SGIs: it is the firmware (TF-A) that wakes the cores (on the Pi 3 with the VideoCore/ARM local mailbox/spin registers), invisible to Windows, which only calls PSCI_CPU_ON.
  [hypothesis] for the internal details of TF-A on the Pi 3 (not verified here).
- [verified: issue #12] The first bug with 4 cores was PROCESS1_INITIALIZATION_FAILED (0x6b) with secondary cores enabled, removed with "Processor Enabled=0", then resolved (SMP working).
- IPI: [hypothesis] Windows sends them through the per-core "mailbox" registers of the ARM local (offset 0x80+), i.e. the native BCM2836 path; I found no direct confirmation.

### 5. Timer
- [verified: `Gtdt.aslc`, `RPi3.dsc` lines 346-349] GTDT with GSIV: Secure PL1 = 0, NonSecure PL1 = 1, Virtual = 3, Hyp (NS PL2) = 2, with active-low polarity flags.
  These are the BCM2836 local source numbers (CNTPSIRQ=0, CNTPNSIRQ=1, CNTHPIRQ=2, CNTVIRQ=3 [verified: Linux binding]) and not GIC PPIs (PPIs would be 16-31; 0-3 in a GIC would be SGIs).
- So Windows uses the **ARM generic timer** (19.2 MHz counter, [verified: issue #12, andreiw "timer configured as expected (19.2Mhz)"]) and the interrupt arrives through
  the per-core "timer interrupt control" register of the ARM local (offset 0x40, [verified: `BCM2836_INTC_TIMER_CONTROL_OFFSET` in Bcm2836.h]).
- The fact that the GSIVs are 0-3 and are accepted is further evidence that the HAL interprets the numbers according to the BCM model, not GIC [hypothesis].
- Peripherals (DSDT, e.g. `Interrupt(ResourceConsumer, Level, ActiveHigh, Exclusive) { BCM2836_USB_INTERRUPT }`): [verified: `GpuDevs.asl`] use the standard Extended Interrupt form with numbers in the BCM domain.
  The numeric values were not read.

## What can be reused as a model for the M1

| Element | Reusable? | Note |
|---|---|---|
| CSRT structure (DMA resource group) | Yes, for DMA/DART | Contains no interrupts; it is the MS mechanism for system resources |
| MADT with "fake" GICC + no GICD | Only as an idea | It works **because the HAL already has the BCM code**; for the AIC there is no known equivalent [hypothesis] |
| PSCI via TF-A/firmware for secondary cores | Yes, the most portable part | Pi 3: PSCI_CPU_ON; for the M1 a firmware that implements it is needed (m1n1/EL2 stub) |
| Timer: GTDT with non-GIC GSIV | Partial | On the M1 the ARM timer arrives as FIQ/IRQ on the AIC (see work A8-A10) |
| HAL extension / ACPI CSRT for interrupt controller | **No** | Does not exist on the Pi 3: no precedent |
| HypDxe (MMIO traps to emulate a GIC) | As a test bench | Would violate the "native" constraint; it is a hypervisor (plan B) |

## Verdict

- On the Pi 3 the interrupt controller was **internal Windows support** (HAL, chosen from the MADT data), not a third-party component.
  Confidence: **medium-high (about 75%)**. Evidence: absence of HalExt/drivers in the ms-iot, raspberrypi and WoR repos; the comment
  "Hal validating PIC is not a BCM PIC"; MADT with base 0x40000000 and "Microsoft blob values"; timer GSIVs 0-3.
- The precedent does **not** show that Windows ARM64 runs without a GIC with a third-party component: it shows that the Microsoft HAL includes at least one
  non-GIC path (BCM2836), but only for that controller. For the M1 (AIC) an equivalent path is not replicable without modifying the HAL
  (binary patching, forbidden as a final solution) or without making the AIC appear as a GIC (emulation/hypervisor, plan B).
- Confidence that the retail ARM64 HAL still contains an active BCM branch without a patch: **medium (about 60-65%)**, because the strongest evidence (andreiw) is from 2018 and says that at the time it was rejected.
- Gaps: I did not read the binaries (forbidden), I have no access to worproject.com (the search did not return it) and I did not verify whether the most recent Windows builds
  keep the BCM branch. A HAL extension (HalExt*, GUID in CSRT with its own DeviceId) for an interrupt controller remains, for Windows ARM64, a mechanism **to be demonstrated** on the M1,
  not a precedent.

## Sources
- edk2-platforms: `Platform/RaspberryPi/AcpiTables/{Madt.aslc,Csrt.aslc,Gtdt.aslc,Fadt.aslc,Pep.asl,Dsdt.asl,GpuDevs.asl}`, `RPi3/RPi3.dsc`, `RPi3/Readme.md`,
  `Silicon/Broadcom/Bcm283x/{Include/IndustryStandard/Bcm2836.h,Drivers/InterruptDxe}`
- github.com/andreiw/RaspberryPiPkg (readme, `AcpiTables/Madt.aslc`, `Drivers/HypDxe`, issues #12, #88, #130), repo now archived in worproject/RaspberryPiPkg
- github.com/ms-iot/rpi-iotcore, github.com/raspberrypi/windows-drivers, github.com/worproject/RPi-Windows-Drivers, github.com/pftf/RPi3
- Linux binding `brcm,bcm2836-l1-intc` (numbering of the local sources)
- windowslatest.com 2018/02/09 (first Windows 10 ARM64 boot on the Pi 3 with a single core, random UNSUPPORTED_PROCESSOR)
