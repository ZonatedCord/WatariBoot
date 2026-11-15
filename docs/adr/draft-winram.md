# Draft ADR: Windows from a RAM disk on the M1 (image, transfer, guest FDT node, EDK2 driver)

- Status: **Accepted by Marco on 2026-11-15** (decisions: `docs/decisions/2026-11-15-marco-monday.md`); the file stays `draft-winram.md` until the PR merge, where it gets its number. Earlier: **draft** (proposed 2026-11-14 by Ralph task T20; to be numbered at merge; independently reviewed once on 2026-11-14 (fixes applied). The work of T21-T24 proceeds on the ralph branch on the strength of this draft; Marco reviews decisions 3 and 4 in the PR and may ask for changes, see the open questions)
- Design: `docs/design/watari-winram/` (README, DESIGN, RISKS, PLAN)
- Related: `docs/adr/draft-edk2-m1-platform.md` (decision 9: the first Windows attempt is a RAM-disk boot), ADR-0005 (no Microsoft binaries in the repository), `docs/adr/draft-phase0-experimental.md`, `re/m3b-usb-display-windows.md`
- Short source names as in DESIGN section 0.

## Context

The M1 has no storage driver in the layer, in EDK2 or in m1n1's hands for us; storage (ANS NVMe) and USB are separate milestones (T14, T15). Windows can run WinPE and the Setup GUI entirely from memory: the boot manager reads `boot.sdi` and `boot.wim` through the firmware and builds its own RAM disk [verified: MS-WALK, MS-PE, WIMBOOT]. That needs only a Block I/O device with a GPT/FAT partition [verified: MS-UEFI] and 512 MB or more of RAM [verified: MS-PE]. It exercises the layer's vGIC, timer, IPIs and PSCI under a real Windows kernel on the hardware, with no driver beyond the framebuffer.

## Decisions

1. **Image:** a GPT disk with one FAT32 (ESP) partition holding a subset of the official ISO: `efi\boot\bootaa64.efi`, `efi\microsoft\boot\*` (BCD unchanged, fonts, resources), `boot\boot.sdi`, `sources\boot.wim`; **no `install.wim`**. About 678 MB payload, an image of about 695-700 MiB (budget ceiling 704 MiB), built on the Mac by `tools/m3a/build-winram-image.sh` from a read-only mount (`hdiutil`, `newfs_msdos`), kept on the data disk, never in the repository. [DESIGN 1]
2. **Transfer:** the host loader's fourth block, written in 16 MiB slices over the m1n1 proxy, allocated first, with a CRC-32 trailer that EDK2 checks. The time is measured by a rate test (first step of hardware rung W1) before the rest; the plan stops if it exceeds a limit Marco sets (proposal: 15 minutes). No compression, no new transport. [DESIGN 2]
3. **Layer (a small extension of the guest FDT builder's scope; Marco reviews it in the PR, see open question 1):** boot argument `wel2.ramdisk=0x<base>,0x<size>`; the guest FDT gets a standard `/reserved-memory/ramdisk@<base>` node (`compatible = "watari,ramdisk"`, `no-map`) and the range is **removed from every `/memory` node**; stage 2 is unchanged (the range stays guest RAM). No new hypercall, no new device, no change to the vGIC, timer or PSCI. [DESIGN 3]
4. **EDK2 (production, FDT driven):** `WatariM1RamDiskDxe` claims the range with the same GCD calls as `HighMemDxe`, allocates it as **`EfiBootServicesData`** (so neither an NFIT nor the NVDIMM-root-device SSDT is published and Windows reclaims the memory after `ExitBootServices`), verifies the trailer's CRC, and registers a volatile virtual disk with `EFI_RAM_DISK_PROTOCOL`. `EfiReservedMemoryType` (which makes `RamDiskDxe` publish an NFIT and an SSDT) stays a debug switch. It replaces the bench-only RAM-disk driver of T16a. [DESIGN 4]
5. **GOP for Windows:** the `--bgr8` firmware first on hardware; the choice between truthful `PixelBitMask`, BGR8-on-10-bit and native 8-bit is **made on the QEMU bench** with the fake framebuffer (T22). The shadow framebuffer and the iBoot BGRA surface (re/m3b option c) stay undecided and are not needed unless both bench variants fail. [DESIGN 6]
6. **ACPI unchanged:** `acpigen -m j293` tables plus the eight-processor DSDT; no PCI host bridge, no DBG2, no NFIT. The bench run is the first Windows boot without a PCI host bridge. [DESIGN 7]
7. **Bench first:** WinPE from the RAM disk runs under the layer in QEMU TCG with the M1 firmware before any hardware day; the hardware day (day 4, after day 3's D3/D4 pass) repeats what passed. [DESIGN 9, PLAN]
8. **Provenance:** nothing from Microsoft's binaries is analysed or committed; the ISO is Marco's own copy, the BCD is not read; third-party sources (iPXE wimboot note, m1n1) are read as protocol and behaviour descriptions only, never copied (ADR-0007, `draft-el2-license.md`).

## Consequences

- **Good:** a path to a first Windows-on-hardware result without storage or USB; the bench answers the riskiest open question (Q-E1) first; the image tool and loader slices are reusable for any later RAM-resident payload.
- **Costs:** a 700 MiB copy to Ciani's Mac and a transfer of unknown length per attempt; 650-1400 MB of RAM in use at the peak; one more small piece of code in the layer (the carve and the node).
- **Not solved:** Windows Setup cannot install (no `install.wim`, no disk); no keyboard, so the first Setup screen is where it stops; no persistent anything.

## Open questions for Marco

**Answered on 2026-11-15: see `docs/decisions/2026-11-15-marco-monday.md`** (1 approved; 2 up to 15 min per attempt, rate test first; 3 8 GB; 5 image copy approved). Question 4 (GOP default) stays as decided in the ADR (`--bgr8`).

1. Confirm (or change) decisions 3 and 4, already being implemented on the ralph branch as a draft (the `wel2.ramdisk=` argument, the FDT node and carve in the layer; the production RAM-disk DXE driver)?
2. What transfer time is acceptable per attempt (proposal: 15 minutes), and may the rate test be the first thing run on the next hardware day?
3. How much RAM does your M1 have (8 or 16 GB)? It decides whether a larger WinPE ever fits.
4. Which GOP default for the first hardware boot: `--bgr8` (proposed) or the truthful mask, if the bench says the mask works?
5. Day 4 needs the image (about 700 MiB) copied to Ciani's Mac (a file copy of a Microsoft-derived file, deleted afterwards): confirm that this is allowed under your licence and your wishes for Ciani's Mac.
