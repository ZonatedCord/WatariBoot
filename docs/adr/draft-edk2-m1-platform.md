# Draft ADR: The EDK2 platform for the M1 on top of Watari EL2 (package, guest FDT, console, framebuffer, ACPI)

- Status: **draft** (proposed 2026-11-12 by Ralph task T11, to be numbered at merge; not yet reviewed by an independent agent; needs Marco's approval)
- Design: `docs/design/watari-edk2-m1/` (README, DESIGN, PLAN, RISKS)
- Related: ADR-0002 (EDK2 instead of U-Boot), ADR-0005 (no Microsoft binaries), ADR-0007 (independent from NTASP), `docs/adr/draft-m1-first-boot.md`, `docs/adr/draft-plan-b-el2-layer.md`, `docs/adr/draft-el2-license.md`, `tools/acpigen/README.md` (model `j293`)
- Marking and short source names as in DESIGN section 0 (`E:` = the EDK2 tree `edk2-stable202608` on the data disk, `MS-…` = Microsoft Learn pages, `A:`/`U:` = Asahi and U-Boot documentation, `N:` = NTASP's Project Mu tree, `HW` = day-1 measurements, `R:` = this repository).

## Context

EDK2 (`ArmVirtQemuKernel`) already boots at EL1 under the layer on the QEMU bench and carries Windows to the desktop [verified: R:docs/STATUS.md M1-4, M2]. That firmware depends on QEMU for its device tree, console, variables, ACPI/SMBIOS and every device. On the M1 the layer presents a GICv3, a generic timer and PSCI, and owns the only output that needs no extra hardware, the built-in display. No upstream EDK2 platform for Apple Silicon exists: Asahi uses U-Boot [verified: A:platform/open-os-interop/, U:m1]; NTASP has a Project Mu tree (BSD-2-Clause) that ADR-0007 lets us read but not copy [verified: `gh api repos/NT-for-ASi/apple_silicon_platforms_mu`].

## Decisions

1. **A new package `edk2-apple/WatariM1Pkg/` that keeps the `ArmVirtQemuKernel` structure**: the relocatable uni-core PrePi entered with the Linux arm64 boot protocol, FDT-driven GIC, timer and PSCI libraries from `ArmVirtPkg`/`ArmPkg`, the DXE core and console stack from `MdeModulePkg`. Everything fw_cfg, virtio, PCI, NOR flash, PL011 and PL031 is left out. Built from the EDK2 tree already on the data disk; no download. [DESIGN 3, 10]
2. **The layer builds a new guest FDT** rather than patching m1n1's: memory ranges, `arm,gic-v3` at the vGIC addresses, `arm,armv8-timer` with GIC-style PPIs, `psci` with method `hvc`, optionally a `simple-framebuffer`, and a `/chosen/watari,el2` node. No Apple node and no identifying property (serial number, MAC addresses, seeds) can reach the guest. This supersedes "copy everything verbatim except…" in `docs/design/watari-el2/INTERFACES.md` 3.2. [DESIGN 4]
3. **Loading:** the host loader puts the firmware image in its own block of guest RAM; `wel2.guest=<hex>` names it and `wel2.abi=fdt` selects "x0 = guest FDT" (the test guest keeps its info block). The range containing the image is the first `/memory` node, because PrePi takes its memory from the first range only [verified: E:ArmVirtPkg/PrePi/FdtParser.c:29-70]. The QEMU-only conventions `EDK2_ENTRY`, `EDK2_FDT_ADDR` and `fdt_patch_for_guest` stay as the bench default and are not used on the M1. [DESIGN 3.3]
4. **Interrupts, timer and PSCI are the bench's, unchanged:** `ArmGicDxe` on the vGIC, `TimerDxe` on the virtual timer (PPI 27), `ArmPsciResetSystemLib` over HVC. EDK2's GIC driver refuses GICv3 when `ID_AA64PFR0_EL1.GIC` reads 0 [verified: E:ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.c:22-58], which it does on the M1 [verified: HW row 5]: the layer's `TID3` emulation (T6g) is therefore a prerequisite of the first EDK2 run on hardware, and the existing local EDK2 patch stays as a second safeguard. [DESIGN 6]
5. **Console through the layer:** a `SerialPortLib` that sends bytes with a new vendor hypercall `WEL2_CONSOLE_WRITE` (`0xC600_0004`); the layer forwards to its UART, log ring and screen. EDK2 gets no UART driver and no UART mapping; the guest's stage 2 stays RAM plus the framebuffer. No console input in the first platform. [DESIGN 7]
6. **Framebuffer hand-over:** two modes chosen by boot argument. `wel2.fb=band`: the layer keeps the top band (48 rows, rounded to whole 16 KB pages) for its checkpoints and a one-line ticker, the guest gets the rest; `wel2.fb=guest`: the guest gets the whole panel at native resolution and the layer draws nothing more. In both, the guest part is mapped at stage 2 as Normal non-cacheable, the geometry travels in the guest FDT (standard `simple-framebuffer`) and in the info block (version 2), the switch happens at the `eret`, and only `fatal()` takes the panel back. [DESIGN 8.2, 8.3]
7. **Our own GOP driver** (`WatariSimpleFbDxe`, BSD-2-Clause-Patent, on EDK2's `FrameBufferBltLib`). The panel's 30-bit `x2r10g10b10` format is reported truthfully as `PixelBitMask`; a build switch reports 8-bit BGR instead, for the Windows experiments. [DESIGN 8.4, 8.5]
8. **ACPI:** MADT, GTDT, FADT and PPTT from `acpigen -m j293` (T10) and a **DSDT in ASL containing only the eight processor devices** (`ACPI0007`, `_UID` 0-7 = the MADT UIDs), compiled by the `iasl` already installed; installed through `EFI_ACPI_TABLE_PROTOCOL`, which also links the DSDT into the FADT [verified: E:MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableProtocol.c:737-761]. No DBG2, SPCR, CSRT, MCFG. Devices are added to the DSDT only with the milestone that needs them. [DESIGN 9]
9. **First platform without storage, PCIe, USB and keyboard.** Variables are RAM-backed, the clock is virtual, shutdown is a halt. Day 3 ends in a built-in self-test that draws colour bars, counts timer ticks and resets the machine. The first Windows attempt is planned as a RAM-disk boot; storage and USB stay in T14/T15. [DESIGN 9.3, 9.4, 10.3]

## Alternatives (rejected)

- **U-Boot's UEFI (what Asahi ships):** no useful runtime services and no variable store on this platform [verified: A:platform/open-os-interop/]; ADR-0002 already chose EDK2; its Apple drivers are GPL.
- **Fork or copy from NTASP's Mu tree:** against ADR-0007; it also targets a different hypervisor contract and its GOP driver does not handle the 30-bit panel [verified: N:Silicon/Apple/AppleSiliconPkg/Drivers/SimpleFbDxe/SimpleFbDxe.c:223-229].
- **Patch m1n1's FDT in place for the guest:** leaks identifying data, needs the Apple nodes rewritten anyway, and cannot guarantee the 32 KB bound of PrePi's FDT copy [verified: E:ArmVirtPkg/ArmVirtQemuKernel.fdf].
- **An S5L UART driver in EDK2:** needs the UART shared between the layer and the guest and helps only with the serial kit.
- **The layer keeps the whole display and offers a "draw" hypercall:** Windows needs a physical frame buffer [verified: MS-UEFI "Protocol requirements"].
- **An AML emitter in `acpigen` for the DSDT:** unnecessary, `iasl` is installed [verified: R:docs/INSTALLED.md].

## Status of the claims

Verified on 2026-11-12 by reading: the EDK2 modules named above, Microsoft's public requirement pages, the Asahi/U-Boot documentation, the NTASP file list. Hypotheses until the bench steps or hardware: everything in RISKS, above all **R-E1 / Q-E1 (Windows and the 30-bit framebuffer)**, R-E4 (PrePi with RAM above 4 GB), R-E13 (the minimal DSDT, testable on the bench first). Nothing was built or run for this ADR.

## Consequences

- New layer code, all host-testable: the FDT writer, the ABI switch, one hypercall, the framebuffer modes (a stage-2 mapping the layer did not have). The "minimal layer" grows by a display hand-over; a shadow framebuffer (RISKS R-E1, stage 3) would grow it further and needs a separate decision.
- New EDK2 code under BSD-2-Clause-Patent: three small libraries, three drivers, one application, one ASL file.
- The bench gains an "M1-shaped" run (FDT built by the layer, several memory ranges, hypercall console, fake framebuffer) that exercises most of this before hardware; what it cannot show is listed in PLAN.
- Day 3 on hardware requires day 2 to have passed completely.
