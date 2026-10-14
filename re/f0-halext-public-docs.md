# A1: Windows HAL extensions in the public documentation (issue #4)

Date: 2026-10-13. Author: Sonnet executor (task A1), to be verified by Opus. Public sources only; no Microsoft binary or symbol read, no download to disk.
Legend: `[verified: source]` = read in the source; `[hypothesis]` = not verified.

## 1. What they are, how they are loaded

- Official definition: HAL extensions are SoC-specific modules, DLLs, that adapt the Windows HAL to a hardware interface of a "Core System Resource" (CSR) class. CSR = "shared hardware functions such as interrupt controllers, timers and DMA controllers" [verified: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables, CSRT section].
- In the Microsoft text, for "standard" interrupt controllers and timers Windows has built-in support based on MADT and GTDT. HAL extensions serve non-standard devices; the text explicitly cites only DMA as a case awaiting a standard [same source].
- Loading: the ACPI table "CSRT" (in the RSDT/XSDT, only if the platform uses non-standard CSRs) describes "resource groups". "Windows uses the resource group identifier to find and load the HAL extension" [same source]. The resource descriptors inside the group have format and use "defined by the extension author".
- Each resource group must also have a device in the ACPI namespace (DSDT); the IDs in the group header must match those of the node; this allows servicing via Windows Update [same source].
- ID -> DLL association in the registry: the i.MX6 INF writes `HKLM,"SYSTEM\CurrentControlSet\Control\OSExtensionDatabase\VEN_NXPI&DEV_0101","HALExtension",,"%13%\HalExtiMX6Timers.dll"` and pairs the device `ACPI\NXP0101` (class Computer, `ExcludeFromSelect=*`, `PnPLockdown=1`). The DLL goes in the Driver Store (dirid 13), not in System32 [verified: https://github.com/ms-iot/imx-iotcore/blob/public_preview/hals/halext/HalExtiMX6Timers/HalExtiMX6Timers.inf].
- Extension code: includes `<nthalext.h>`; entry point `AddResourceGroup(Handle, PCSRT_RESOURCE_GROUP_HEADER)`; enumerates the descriptors with `GetNextResourceDescriptor(Handle, Group, Prev, Type, Subtype, Uid)` and registers them with `RegisterResourceDescriptor(Handle, Group, &Desc->Header, &NewTimer)`. It also uses `HalMapIoSpace` and `HalRegisterPermanentAddressUsage` [verified: https://github.com/ms-iot/imx-iotcore/blob/public_preview/hals/halext/HalExtiMX6Timers/HalExtension.c lines ~237-310, 535-625, 868-947].
- Signing: the archived HCK 8.1 (Windows RT) page says that the extension "requires a signing certificate with a special EKU", that the CSRT must have a valid descriptor and that the `OSExtensionDatabase` registry must have the entry [verified: https://learn.microsoft.com/en-us/previous-versions/windows/hardware/hck/hh997835(v=vs.85)]. Related requirements cited: `Device.DevFund.HALExtension.HALSignatureAttributes`, `System.Fundamentals.HAL.IfCSRTPresent`.
- Does it work with test-signing? Not determinable from the public sources read: no documentation says whether the EKU check is ignored with test-signing or with a debug policy. [hypothesis: the check belongs to the kernel/HAL loader Code Integrity and test-signing might suffice, but the "special" EKU might be required anyway]. To be verified in V4.

## 2. CSRT resource types

- CSRT spec (Microsoft, 2011/2017) and open source implementations: type 0x0001 = Interrupt with subtypes 0x0000 = Interrupt Line and **0x0001 = Interrupt Controller**; type 0x0002 = Timer (subtype 0x0000); type 0x0003 = DMA (subtypes 0x0000 channel, 0x0001 controller) [verified: ACPICA `source/include/actbl2.h` lines 832-842 `ACPI_CSRT_TYPE_INTERRUPT/TIMER/DMA`, `ACPI_CSRT_XRUPT_CONTROLLER`; FWTS test https://github.com/ColinIanKing/fwts/blob/master/src/acpi/csrt/csrt.c lines 146-178]. The spec PDF (uefi.org CSRT v2.pdf, and intel cdrdv2 772760) is not readable with the tools available (403 / compressed PDF): the original text remains to be read by hand [hypothesis on unread fields].
- So **on paper the "Interrupt Controller" type exists** in the CSRT specification.
- Public evidence of a HAL extension that provides an interrupt controller: **none found**. Public HAL extensions found (all in ms-iot/imx-iotcore, branch `public_preview`, folder `hals/halext/`, MIT license):
  - `HalExtiMX6Timers` (GPT, EPIT; INF for `ACPI\NXP0101`)
  - `HalExtiMX7Timers`
  - `HalExtiMXDma` (SDMA, with headers for i.MX6Q/6SX/6UL/8M)
  [verified: repo tree listing with `gh api repos/ms-iot/imx-iotcore/git/trees/public_preview?recursive=1`]
- No HAL extension found in ms-iot/rpi-iotcore, raspberrypi/windows-drivers (only PWM/SDHC/LAN drivers, no halext/CSRT) and in the microsoft/Windows-driver-samples tree (no file with `halext` or `csrt` in the name) [verified: grep on the repo trees]. The hypothesized name `HalExtPL080`/`HalExtBcm*` does not appear public [verified by absence in the cited repos; does not exclude that they exist in other repos].
- No public ASL/CSRT file found in imx-iotcore (grep for `csrt` and `.asl` on the tree: nothing); the ACPI tables live in separate firmware repos, not examined [hypothesis].

## 3. HAL extensions for timers

- They exist and are public: the API is declared in `nthalext.h` (not read directly; the WDK is not in the Windows-driver-samples repo, so the header is known only from its use in the iMX sources) [verified indirectly: HalExtension.c above].
- Interface observed from usage, timer registration structure: `INITIALIZE_TIMER_HEADER`, `FunctionTable` with `Initialize`, `QueryCounter`, `AcknowledgeInterrupt`, `ArmTimer`, `Stop`; fields `CounterBitWidth`, `CounterFrequency`, `MaxDivisor`, `Capabilities` (`TIMER_PERIODIC_CAPABLE`, `TIMER_ONE_SHOT_CAPABLE`, `TIMER_GENERATES_LINE_BASED_INTERRUPTS`, `TIMER_ALWAYS_ON`, `TIMER_COUNTER_READABLE`), `KnownType`, `Identifier`, and **`Interrupt.Mode`, `Interrupt.Polarity`, `Interrupt.Gsi`** [verified: HalExtension.c lines 280-300, 570-615].
- Crucial point: the timer extension declares the interrupt as a **GSI** (global interrupt number) with mode and polarity. The timer does not handle the interrupt by itself: the GSI is routed by the interrupt controller already known to Windows. On the i.MX6 it is a line interrupt (level, active high) of the GIC [verified: lines 608-610]. A comment in the source also says that "Windows requires an always-on timer" [verified: lines ~595-600].
- Consequence for the M1 [hypothesis]: a timer extension could replace the generic ARM timer (which on the M1 arrives as FIQ, see [re/f0-m1-fiq-irq.md](f0-m1-fiq-irq.md)) with a timer that emits a line interrupt, but the GSI would still have to be delivered by a recognized controller (GIC) or by an extension acting as a controller.

## 4. Windows ARM64 and the GIC

- Official source read: "For Arm SoC platforms, ACPI 5.0 adds descriptors for GIC and GIC Distributor. Windows includes inbox support for GIC and GIC Distributor... the GIC corresponding to the boot processor must be the first entry" [verified: ACPI System Description Tables page, MADT section]. This documents the support; it does **not** say verbatim that other controllers are forbidden.
- An official statement that Windows ARM64 *requires* GIC/GICv3 (Windows on Arm requirements, SBSA/BSA): **not found** in the searches made [not determinable from the public sources read]. Arm SBSA/BSA put the GIC among the mandatory components (for SBSA; revision 5.0 forbids non-standard controllers) [verified for the generic content: search result on Linaro SystemReady Client https://linaro.atlassian.net/wiki/spaces/CLIENTPC/pages/28737798189/Arm+SystemReady+for+Client+Devices and SBSA notes on documentation-service.arm.com]; but BSA/SBSA constrain the hardware, they do not prove the behavior of Windows. Precise references to clauses not read [hypothesis].
- Indirect counterexample: Raspberry Pi 2/3 (BCM2836, without a GIC) had a Windows IoT BSP [verified: https://github.com/ms-iot/rpi-iotcore/blob/master/README.md]. How Windows handled the Pi's interrupt controller in the firmware (ACPI tables) is not documented in the sources read; it is the subject of task A4, and it **cannot** be concluded that it used a HAL extension [hypothesis].

## 5. Preliminary verdict

**Can a HAL extension provide a non-GIC interrupt controller? Not determinable from public sources.**
Confidence: low (about 30% yes, 20% no, 50% uncertainty).

In favor (slightly):
- The CSRT specification defines the "Interrupt Controller" subtype [verified: ACPICA/FWTS].
- Microsoft lists interrupt controllers among the CSRs and says that extensions serve non-standard devices [verified: Microsoft CSRT page].

Against / not proven:
- The same Microsoft page cites only DMA as a real case; the only public extensions are timer and DMA (imx-iotcore), none for an interrupt controller [verified by absence in the repos examined].
- The observed timer API delivers the GSI to a controller that Windows already knows (the GIC); there is no public example of the API for registering a controller [verified: HalExtension.c]. I have not seen in `nthalext.h` any controller registration function (I did not read the header) [hypothesis that it does not exist in the public version].
- Signing with a special EKU: whether it is blocking or not with test-signing is unknown.

To be verified experimentally (variant V4 in `docs/design/watariboot/EXPERIMENT-F0.md`):
1. Whether Windows loads an extension attached to one of our CSRT groups (ID, `OSExtensionDatabase` entry, paired ACPI device) with a test signature, and with which error messages if rejected.
2. Whether a resource group with type 0x0001/subtype 0x0001 is taken in charge by an extension and which registration interface is requested.
3. Whether Windows ARM64 proceeds without the GICD in the MADT when there is an extension (linked to V2).
4. Whether a timer registered by an extension replaces the GTDT one, and whether the "line-based" interrupt via GSI works without a GIC.

## Main sources

1. https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables (CSRT, HAL extension, inbox GIC support)
2. https://github.com/ms-iot/imx-iotcore/tree/public_preview/hals/halext (code and INF of timer and DMA HAL extensions)
3. https://learn.microsoft.com/en-us/previous-versions/windows/hardware/hck/hh997835(v=vs.85) (signing with special EKU, OSExtensionDatabase)
4. ACPICA `actbl2.h` and FWTS `csrt.c` (CSRT types and subtypes, including Interrupt Controller)
