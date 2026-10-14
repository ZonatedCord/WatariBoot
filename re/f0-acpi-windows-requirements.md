# A2: Windows ARM64 ACPI requirements and the Multiprocessor Parking Protocol

Issue #5. Research from public sources only (Microsoft Learn, the Microsoft parking protocol specification, Linux, U-Boot, edk2-platforms). No Microsoft binaries or symbols. Every claim is marked **[verified: source]** or **[hypothesis]**.

Note on method: the Microsoft Learn pages on ACPI are written against ACPI 5.0 and do not explicitly list SPCR, PPTT, IORT. The actual behavior of Windows on these points is not documented in the sources found: where it is missing, this is stated.

## 1. ACPI tables

Sources: [MS-ACPI] https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables (updated 2025-04), [MS-DBG2] https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table, [LNX] https://docs.kernel.org/arch/arm64/acpi_object_usage.html (what Linux expects, NOT Windows).

| Table | Windows | Details |
|---|---|---|
| RSDP/XSDT | Required | RSDP found via the EFI system table; if both RSDT and XSDT are present, Windows prefers XSDT. [verified: MS-ACPI] |
| FADT | Required | On SoC platforms: `HARDWARE_REDUCED_ACPI` (Flags bit 20) must be set if there is no ACPI fixed hardware; with the flag, all ACPI hardware fields are ignored. [verified: MS-ACPI] The MS text says the sleep/resume fields "must be present" but are ignored. ARM flags: see below. |
| MADT | Required | See sec. 2. |
| GTDT | Required in practice | MS: "ACPI's GTDT can be used to leverage the built-in support" for the Arm generic timer (weak wording, not "required") [verified: MS-ACPI]. For Linux it is Required [verified: LNX]. For Windows, the explicit requirement is a [hypothesis]. |
| DBG2 | Required for Windows | "Microsoft requires a debug port on all systems"; the presence of DBG2 indicates the port. Revision 0; header 36 B + OffsetDbgDeviceInfo (off 36) + NumberDbgDeviceInfo (off 40). For each device: Revision 0, Port Type 0x8000 serial, subtype e.g. 0x0003 PL011, 0x000E SBSA UART, 0x0012 16550 with GAS; the NamespaceString must refer to a DSDT node or be "." if none exists. [verified: MS-DBG2]. There is no Apple subtype: for M1 a known subtype or a KD extension is needed (to be researched; [hypothesis]). |
| SPCR | Not named in the MS pages found | Required for Linux [verified: LNX]. For Windows not verified [hypothesis: needed for the console, debug goes through DBG2]. Pi3 examples in edk2-platforms: `SpcrPl011.aslc`, `SpcrMiniUart.aslc` [verified: file list tianocore/edk2-platforms Platform/RaspberryPi/AcpiTables]. |
| PPTT | Optional (not named by MS) | Optional for Linux [verified: LNX]. edk2-platforms RaspberryPi provides it (`Pptt.aslc`) [verified]. |
| CSRT | Required ONLY if non-standard CSRs are used | "must be included in the RSDT if non-standard CSRs are used"; each resource group must also have a device in the ACPI namespace (DSDT) with the same identifiers as the group header, so that the HAL extension can be updated through Windows Update. Windows uses the group ID to load the HAL extension. [verified: MS-ACPI]. Directly concerns `halext-aic`. Specification: https://cdrdv2.intel.com/v1/dl/getContent/772760. |
| IORT | Not named by MS; optional for Linux | Optional (required for SMMU/ITS) [verified: LNX]. For M1 (DART, not SMMU) it cannot be described with a standard IORT: [hypothesis]. It exists in edk2-platforms RaspberryPi (`Iort.aslc`) [verified: file list]. |
| DSDT | Required | Contains the devices and must be referenced from the FADT (X_DSDT). SSDTs are optional. [verified: MS-ACPI, LNX] |
| Others cited by MS | BGRT, FPDT optional; WSMT, iBFT for specific scenarios | [verified: MS-ACPI] |

### ARM FADT flags (ArmBootArchFlags, ACPI 5.1+)
- `PSCI_COMPLIANT` indicates that the hardware is PSCI; `PSCI_USE_HVC` chooses HVC instead of SMC as the conduit. [verified: ACPICA patch on lkml https://lkml.iu.edu/hypermail/linux/kernel/1407.3/00390.html]
- What happens if they are 0: Linux, with `PSCI_COMPLIANT=0`, does not use PSCI and falls back to the parking protocol (if described in the MADT) [verified: code `acpi_parking_protocol.c` and the LNX doc, which calls parking "possible but discouraged"]. For **Windows** the MS page does not say what happens with PSCI_COMPLIANT=0 and does not declare PSCI mandatory: [hypothesis] that Windows ARM64 selects the start method based on the MADT (valid parking address) and the flag, but I found no evidence.
- The Raspberry Pi 3 with edk2-platforms sets `HW_REDUCED_ACPI` and `EFI_ACPI_6_3_ARM_PSCI_COMPLIANT` (SMC conduit) [verified: Platform/RaspberryPi/AcpiTables/Fadt.aslc, Flags and ArmBootArchFlags lines]. So the Pi3 is NOT a precedent for Windows with parking.

## 2. MADT on Windows

- Source [MS-ACPI]: for Arm, ACPI 5.0 adds the GIC (5.2.12.14) and GIC Distributor (5.2.12.15) descriptors; Windows has inbox support for GIC and GICD. "For Arm platforms, one descriptor is listed for each GIC, followed by one for each GIC Distributor. The GIC corresponding to the boot processor must be the first entry." [verified]
- No official statement found that the GICD is mandatory, nor that the GICR or the ITS are. The MS text only describes what Windows knows how to interpret. [verified: absence in the page; the requirement is a [hypothesis]]
- How Windows enumerates CPUs: from the MADT GICCs. For parking: "The OS learns the Processor ID and mailbox physical address for each processor by consuming the GICC structures in the ACPI MADT" [verified: MP Startup specification, sec. 3.3]. "Systems that use ACPI must describe the processors through the ACPI-defined MADT table" (sec. 3.7) [verified].
- Precedent of GICC without GICD: the Pi3 MADT omits the GICD (`#if (RPI_MODEL != 3)`), uses GICC with physical base 0x40000000 (PcdGicInterruptInterfaceBase of RPi3.dsc), and comments that "Windows 10 on Raspberry Pi 3 does not boot" unless the GICR Base field is 1, a value taken from the Microsoft IoT blobs [verified: Platform/RaspberryPi/AcpiTables/Madt.aslc, RPi3/RPi3.dsc]. The Pi3 GTDT uses GSIV 0,1,3,2 (BCM2836 local interrupts, not GIC PPIs) with polarity flags [verified: Gtdt.aslc, RPi3.dsc PcdArmArchTimer*]. I did not find how Windows handles the Pi3 interrupt controller (HAL extension or otherwise): [hypothesis] that it is a HAL extension, i.e. exactly the scheme we need for the AIC.
- Linux on arm64 requires a MADT with only GIC structures 0x0A-0x0F and UIDs synchronized with the DSDT Processor Devices [verified: LNX].

## 3. Multiprocessor Parking Protocol

Official specification: Microsoft, "Multi-processor Startup for ARM Platforms", Version 1, 23 September 2014 (copy at https://cdrdv2-public.intel.com/772757/mp-startup-for-arm-platforms.pdf, read in full, 11 pages). Everything that follows is **[verified: MP Startup spec, section indicated]** unless marked otherwise. Revisions: 2010, 2011 (public release 14/11/2011), 20/12/2012 (barriers, strongly-ordered, mailbox correction), 23/9/2014 (Processor ID for ARMv8, updated MADT).

### 3.1 Mailbox (sec. 3.4, Fig. 3)
- **4 KB** block, **4 KB-aligned** address, one separate physical page per processor. Reserved by the firmware ("permanently in use" memory); the firmware cannot relocate it. All little endian.
- Divided into two 2 KB halves: OS section and firmware section.
- Layout (Fig. 3, offsets derived from the sizes; they match Linux's `struct parking_protocol_mailbox` and U-Boot's `ACPI_PP_CPU_ID_OFFSET 0`, `ACPI_PP_CPU_JMP_OFFSET 8`):
  - 0x000: Processor ID, 4 B
  - 0x004: Reserved, 4 B
  - 0x008: Jump Address, 8 B
  - 0x010 - 0x7FF: reserved for OS use (the firmware must not write it)
  - 0x800 - 0xFFF: reserved for the firmware (the OS does not access it)
- Only Processor ID and Jump Address may be written by the firmware in the OS section.
- Processor ID (sec. 1): for GICv1/v2 it is the value of the GICC **CPU Interface Number** field. GICv3 with compatibility mode: GICv1/v2 ID for the first eight, 0 for the others. GICv3 without compat mode: 0 for all.
- The MADT **Parking Protocol Version** field must match the document version ("1") (sec. 1).
- Jump Address is 64-bit; below 4 GB only on 32-bit Arm.

### 3.2 Parked state and firmware sequence (sec. 3.1, 3.2, 3.5)
- Before handing control to the OS the firmware initializes **Processor ID = all ones (0xFFFFFFFF)** and **Jump Address = 0** (even if the platform starts from uninitialized memory; if the content is preserved it must not be touched).
- AP state on entry (Tab. 1, AArch64): EL2 if virtualization is present, otherwise EL1; little endian (SCTLR_ELx.EE=0); DAIF all masked {1,1,1,1}; **GIC CPU interface enabled**; **MMU, cache, branch prediction disabled** (SCTLR_ELx.{I,C,M}=0). All processors in the same inner shareable domain; the firmware initializes non-architectural state; caches invalidated before entering coherency.
- AP algorithm (Fig. 2): checks whether Processor ID == its own and Jump Address != 0; otherwise **WFI** and on wake-up checks again. If OK: reads Jump Address, **writes 0 to Jump Address** (ack), puts the mailbox address in X0 and does `BR` to the Jump Address. If the two conditions do not hold it is a spurious wake-up and it returns to WFI. With interrupts masked, exiting WFI resumes at the following instruction.
- 2012 detail: a note that a core may also be powered off before the OS, provided it powers back on by itself on a software interrupt and follows the steps (sec. 3.1).

### 3.3 OS sequence (sec. 3.3, 3.5)
1. Reads Processor ID and mailbox address from the MADT GICCs.
2. Maps the mailbox as strongly-ordered (**Device-nGnRnE** on ARMv8). Linux instead uses `ioremap` (nGnRE) and declares it a "harmless" deviation [verified: acpi_parking_protocol.c].
3. Checks that the core is parked: Processor ID == all ones.
4. Writes Jump Address and Processor ID (in the order imposed by 3.5: if the Jump Address changes while the core is already armed: Processor ID to all ones, DSB, Jump Address, DSB, then the correct Processor ID). Linux writes entry_point and then cpu_id [verified: Linux].
5. **DSB after each write**.
6. Does not start APs before `ExitBootServices()`.
7. **"will send an interrupt to the target application processor to signal that the mailbox is ready"** (sec. 3.3). The spec does NOT specify which interrupt: no SGI number, no mention of SGI/GIC IPI, and in sec. 2.2 it states that "ARM architecture does not provide ... designated IPI". So the question "which SGI" has no answer in the specification: it is an OS choice. Linux uses the normal reschedule IPI (`smp_send_reschedule`) [verified: arch/arm64/kernel/smp.c, `arch_send_wakeup_ipi`, "we can safely handle spurious scheduler IPIs"]. For Windows the actual interrupt is decided by the HAL, not documented in the sources: [hypothesis] that it is an IPI/SGI mediated by the interrupt controller driver (in our case `halext-aic`).
8. BSP mailbox (sec. 3.6): the BSP has one too, used for resume from sleep states with retained memory.

### 3.4 Comparison with the implementations
- **Linux** `arch/arm64/kernel/acpi_parking_protocol.c` [verified: gh api torvalds/linux]: struct `{le32 cpu_id; le32 reserved; le64 entry_point;}`; valid only if `parked_address && parking_version` (i.e. version 0 or address 0 = parking absent); requires `cpu_id == ~0U` before starting, otherwise `-ENXIO`; writes `entry_point` (physical address of `secondary_entry`) then `cpu_id = cpu_interface_number` (NOT UID, NOT MPIDR); then IPI; in postboot it warns if `entry_point` was not zeroed.
- **U-Boot** `arch/arm/cpu/armv8/acpi_park_v8.S` and `arch/arm/include/asm/acpi_table.h` [verified: gh api u-boot/u-boot]: `ACPI_PP_CPU_ID_INVALID 0xffffffff`, `ACPI_PP_JMP_ADR_INVALID 0`, `ACPI_PP_PAGE_SIZE 4096`, `ACPI_PP_CPU_ID_OFFSET 0`, `ACPI_PP_CPU_JMP_OFFSET 8`, `ACPI_PP_CPU_CODE_OFFSET 2048` (spin code in the firmware half, +0x800), `ACPI_PP_VERSION 1`. The AP code: writes 0xFFFFFFFF to cpu_id, `dsb sy`, then a **`wfe`** loop (not WFI): reloads cpu_id; if == invalid retries; reads the jump address; if 0 retries; otherwise zeroes the jump address, `dsb sy`, `br x1`. It does not use atomics (MMU/cache off) and identifies the core by looking for its own MPIDR (aff3:aff2:aff1:aff0 compacted) among the pages: so U-Boot assumes that the cpu_id field contains the compacted MPIDR, which is NOT what the spec says (cpu interface number, or 0 on GICv3). A deviation to keep in mind.
- **edk2**: `DynamicTablesPkg/Library/Acpi/Arm/AcpiMadtLibArm/MadtGenerator.c` and the `ConfigurationManager.h` files of edk2-platforms (Juno, N1Sdp, Morello, VExpress, SGI, NXP) have the GICC fields `ParkingProtocolVersion` and `ParkedAddress`, but I only found table generation, **no firmware implementation of the parking loop** in searches on tianocore/edk2 and edk2-platforms [verified: gh api search/code]. The reference platforms use PSCI from ATF.
- **Pi3 pftf/edk2-platforms RPi3**: uses PSCI (ATF), not parking (see sec. 1) [verified].

## 4. Critical point: parking without a GIC (M1)

Answers, with confidence level:

1. **Does the specification require a GIC?** Not for the parking mechanism itself. The GIC appears in two places: (a) Tab. 1 lists "GIC CPU Interface: Enabled" in the entry state; (b) Processor ID is defined in terms of the GICC CPU Interface Number (with 0 for GICv3 without compat). The wake-up is described generically as "an interrupt" and "doorbell mechanism"; sec. 2.2 says that today the interrupt controller in use is the GIC but does not prescribe it. [verified: spec]. On M1 the line "GIC CPU interface enabled" must be read as "interrupt controller able to deliver the wake-up interrupt" (AIC): [hypothesis].
2. **Can the AP poll instead of waiting for an interrupt?** The spec describes WFI as the wait state (Fig. 1/2 and sec. 3.1: "they check their mailbox ... and then go into a wait for interrupt (WFI) state") but the only requirements observable by the OS are: Processor ID = all ones and Jump Address = 0 at rest; ack by writing 0 to the Jump Address; jump with X0 = mailbox address. There is no requirement forbidding polling and no declared timeout. A "spurious wake-up" page is explicitly tolerated ("assume it was a false wake and go return to the WFI state"). Conclusion: a **WFE** loop that rechecks the mailbox (like U-Boot, which does not rely on an OS SEV) is conformant in observable terms [hypothesis supported by the verified U-Boot implementation and by the text; there is no explicit "polling allowed" statement]. The spec does NOT provide for the OS to issue SEV: so a pure WFE depends on the fact that a pending interrupt (even masked, DAIF=1111) wakes WFE like WFI (ARM ARM behavior, recalled from memory, [hypothesis: to be verified in the ARM ARM]; and in any case for Linux/U-Boot the IPI is there). Robust alternatives on M1: (a) WFE with the timer event stream (`CNTKCTL_EL1.EVNTEN`) for a guaranteed periodic wake-up; or (b) pure polling with `yield`. Cost: power consumption and heat of the parked cores until they start (brief, only until OS start) [hypothesis].
3. **If Windows requires sending the interrupt anyway**: not a problem for a polling loop (the interrupt is only a doorbell). But the sending of the IPI by the HAL to a core still in the firmware goes through the AIC driver (`halext-aic`): if the HAL does not know how to send IPIs to the AIC before the core is online, or if Windows waits for a handshake on that channel, polling masks it. [hypothesis]
4. **Windows on Raspberry Pi 3 without a GIC**: the Pi3 is not a parking precedent. It uses PSCI from ATF (FADT `PSCI_COMPLIANT`), with a MADT that has the GICCs (base 0x40000000) but no GICD, and a GTDT with GSIV 0-3 [verified: edk2-platforms]. It is a useful precedent for "MADT without GICD and non-GIC GSIVs", not for "start via mailbox".
5. **Main risk**: there is no source saying that modern Windows ARM64 still supports the parking protocol (the spec is from 2014 for ARMv7/early ARMv8; the Linux page calls it "discouraged" and says that Arm servers support only PSCI). I found no known commercial Windows ARM64 platform that uses parking. [hypothesis: Windows supports it for compatibility, but it must be tried on QEMU or hardware with a real HAL]. Plan B if it does not work: emulate PSCI in the firmware (HVC conduit with a mini-hypervisor at EL2, or an SMC trap which is not possible without EL3), i.e. a decision out of scope for A2.

Verdict: parking without a GIC is compatible with the text of the specification with a WFE/poll loop that respects the mailbox protocol; the part not verifiable from public sources is (i) which interrupt/IPI Windows sends and (ii) whether the current ARM64 HAL still uses this method.

## 5. Comparison with `tools/acpigen`

Code read: `include/acpigen.h`, `src/madt.c`, `gtdt.c`, `pptt.c`, `csrt.c`, `t8103_model.c`.

| # | Element | Current state | Requirement / source | Action |
|---|---|---|---|---|
| 1 | GICC `parking_protocol_version` | 0 (`CPU()` in t8103_model.c) | Spec: version "1". Linux considers parking absent if version or address are 0. U-Boot `ACPI_PP_VERSION 1`. | Set it to 1. **Blocking discrepancy.** |
| 2 | GICC `parked_address` | 0 | 4 KB, 4 KB-aligned, a distinct physical page per CPU, reserved by the firmware ("reserved" memory, non-relocatable). | Allocate 8 pages (BSP too, sec. 3.6) and validate alignment in `acpigen_build_madt` (currently no check). |
| 3 | GICC `cpu_interface_number` | `= i` (0..7), UID = 0..7 | The mailbox Processor ID must match this value (GICv2); on GICv3 without compat the spec says 0 (ambiguous with several CPUs). Linux writes exactly `cpu_interface_number`. Value 0xFFFFFFFF is reserved (invalid). | OK as long as the values are unique and != 0xFFFFFFFF. Add a check. Document the choice (non-GIC: the spec is ambiguous). |
| 4 | GICC `mpidr` | DTS reg (0x0-0x3, 0x10100-0x10103) | MPIDR field of Aff3:Aff0 [verified: ACPI]. | Hypothesis: it coincides with the real MPIDR_EL1 register (already declared). |
| 5 | First GICC = boot processor | cpu0 = icestorm0 (UID 0) | "The GIC corresponding to the boot processor must be the first entry." [MS-ACPI] | Verify which core starts m1n1/UEFI on M1 and reorder accordingly, [hypothesis] that the boot CPU is not icestorm0. |
| 6 | GICC `physical_base_address` / `gicv` / `gich` / `gicr` | 0 | The Pi3 sets base = 0x40000000, GICR base = 1, and says Windows does not boot without the value 1 [verified: Madt.aslc]. | Try GICR=1 (or another non-zero value) in QEMU/test, [hypothesis]; evaluate the effect on the inbox GIC driver that Windows loads. |
| 7 | `acpi_gicd` | `t8103_gicd`: base 0, version 3, placeholder | MS does not say it is needed; the Pi3 omits it. A bogus GICD with base 0 may start the inbox GIC driver on non-existent hardware. | Pass `gicd = NULL` by default (parameter already optional). |
| 8 | GICR/ITS | not generated (struct types 0x0E/0x0F absent) | MS: only GIC and GICD described; no statement about GICR/ITS. | No action for now. |
| 9 | MADT header revision | 6, `local_apic_address` 0, flags 0 | Consistent with ACPI 6.5 and with the Pi3 (rev from `EFI_ACPI_6_3_...REVISION`). | OK. |
| 10 | GTDT | rev 3, cnt_*_base = ~0, GSIV 29/30/27/26, flags active-low, virtual_pl2 0 | Format OK against ACPI 6.5. The Pi3 uses GSIV 0,1,3,2 with polarity flags [verified] and no GIC PPI: Windows accepts arbitrary GSIVs if the interrupt controller resolves them. | The GSIVs are placeholders for the AIC (FIQ): to be realigned with `halext-aic` (tasks A8-A10). Not a generator bug. Consider setting `ALWAYS_ON` if the M1 timer is. |
| 11 | GTDT `virtual_pl2_gsiv` | 0 | Field introduced with rev 3 (ACPI 6.3); no problem. | OK. |
| 12 | PPTT | package -> cluster -> core, rev 3, `ID_VALID` on cores with UID | ACPI 6.5 5.2.30.1; optional for Windows (not named by MS) and Linux. | OK. `IDENTICAL_IMPL` flag not set: correct for mixed clusters. |
| 13 | CSRT | one `APLE` group, one Interrupt descriptor | MS: CSRT required if non-standard CSRs (AIC) are used and each group must have a device in the DSDT with the same identifiers. The Pi3 uses CSRT for DMA. | The device is missing in the DSDT (not handled by acpigen). The group header ID and the DSDT device must match. |
| 14 | Tables missing in the generator | n/a | FADT (HW_REDUCED_ACPI; ArmBootArchFlags), DBG2, SPCR, DSDT/SSDT, XSDT/RSDP are required/expected | New tasks: FADT with `HW_REDUCED_ACPI` and PSCI flags at 0 (consistent with parking), DBG2 with serial subtype, DSDT with CPU Devices (_UID = MADT UID), AIC device for the CSRT. |
| 15 | DSDT/MADT synchronization | not applicable | Processor Device `_UID` must match the MADT UIDs [verified: LNX]. | Keep in mind when writing the DSDT. |

## 6. Open questions (not to be assumed)

- Does current Windows ARM64 still use the parking protocol? Which interrupt does it send? (not documented; test on hardware or QEMU with a real HAL)
- What does Windows do if `PSCI_COMPLIANT=0` and `parking_protocol_version=0`? Not documented.
- Does Windows require a specific DBG2 subtype? For M1 a known subtype is needed (16550 with GAS, 0x0012, is the most flexible according to MS).
- Does a core in WFE with the AIC interrupt masked (DAIF=1111) leave the wait state? To be verified in the ARM ARM; the alternative is the event stream.
- AIC compatibility with the "GIC CPU interface enabled" of Tab. 1: the line is meaningless on M1 without a GIC.

## Sources
- Microsoft, Multi-processor Startup for ARM Platforms v1, 2014-09-23: https://cdrdv2-public.intel.com/772757/mp-startup-for-arm-platforms.pdf
- Microsoft Learn, ACPI System Description Tables: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables
- Microsoft Learn, DBG2: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table
- Linux ACPI Tables on arm64: https://docs.kernel.org/arch/arm64/acpi_object_usage.html
- Linux `arch/arm64/kernel/acpi_parking_protocol.c`, `arch/arm64/kernel/smp.c` (gh api torvalds/linux)
- U-Boot `arch/arm/cpu/armv8/acpi_park_v8.S`, `arch/arm/include/asm/acpi_table.h` (gh api u-boot/u-boot)
- edk2-platforms `Platform/RaspberryPi/AcpiTables/{Fadt,Madt,Gtdt,Csrt}.aslc`, `RPi3/RPi3.dsc`, `RPi3/Readme.md` (gh api tianocore/edk2-platforms)
- Introduction of the ARM Boot Architecture flags in the FADT: https://lkml.iu.edu/hypermail/linux/kernel/1407.3/00390.html
