<!-- SPDX-License-Identifier: MIT -->
# M3b research: Windows storage on the M1 (ANS2 NVMe), issue #48, task T14

Status: public-docs research only (no hardware, no downloads, no Microsoft binaries). Date 2026-11-12.
Every claim is marked **[verified: source]** or **[hypothesis]**. Linux sources were read as facts only; no code is copied (GPL-2.0).
The Linux files were fetched through a summarising tool, so line numbers and constants should be re-checked against the files before being relied on.

## 0. Bottom line

- The inbox Windows NVMe driver (`stornvme`) cannot drive ANS2: ANS is not a standard NVMe controller (section 1). Windows needs either a new native StorPort miniport (option b) or a standard-looking NVMe device presented to it (option a).
- Recommendation: **(c) then (b)**. M3b gets to the lock screen first on a **USB-attached disk** (inbox xHCI + usbstor, needs only the T15 results), while the native ANS miniport is developed in parallel and is the long-term path. Option (a) (NVMe emulation inside the EL2 layer) is not recommended: it breaks the "minimal layer" decision and puts RTKit, SART and DART into EL2.
- Whatever path is taken, some code must boot ANS (RTKit handshake) before Windows can use it. Who does it (firmware stage or the Windows driver) is the main design choice (section 5).

## 1. How ANS2 works (facts)

### 1.1 It is not an NVMe controller on PCIe
- ANS is a platform device with two MMIO regions named `ans` (coprocessor) and `nvme` (controller), one IRQ for the driver, power domains, a SART phandle and reset controls [verified: Linux v6.12 `drivers/nvme/host/apple.c` probe, https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/nvme/host/apple.c].
- On our j293 the ADT node is `/arm-io/ans`, `compatible iop,ascwrap-v4`, `interrupts` 584, 583, 586, 585, 590; 590 is the level (NVMe) line, 583-586 are the mailbox lines which the layer's interrupt map currently denies [verified: `re/m3a-ehw0-measurements.md` Row 10, measured on j293 2026-11-10]. m1n1 itself enabled the PMGR parents of ANS2 [verified: same file, line ~81].
- PLAN.md already records "SSD (Apple ANS2, not on PCIe)" [verified: `docs/PLAN.md` line 40].

### 1.2 Coprocessor (ASC / RTKit) firmware boot
- ANS runs its own firmware on an ASC coprocessor, managed through RTKit [verified: apple.c].
- Boot: CPU control register at `ans+0x44`, RUN = bit 4; boot status register at `ans+0x1300` reads `0xde71ce55` when up; the driver waits up to 1 s polling every 1 ms [verified: apple.c].
- Reset sequence: shut RTKit down, clear CPU control, assert reset, re-init RTKit, deassert reset, set RUN, boot RTKit, poll boot status. An RTKit crash cannot be recovered without a reset [verified: apple.c, `apple_nvme_reset_work`, roughly lines 880-1000].
- A cleanly shut-down controller is expected by U-Boot and m1n1 (shutdown disables the controller and shuts RTKit down) [verified: apple.c].
- RTKit management protocol: HELLO type 1, HELLO_REPLY 2, STARTEP 5, endpoint number in bits 39:32 of STARTEP in the U-Boot implementation; the OS negotiates the lower of the two maximum versions; Linux supports versions 11 to 12 (a December 2025 series proposes 10) [verified: U-Boot `arch/arm/mach-apple/rtkit.c` via https://source.devboardsforandroid.linaro.org/platform/external/u-boot/+blame/3e38688ec6fd365c98a5a21fedc91631f90fc837/arch/arm/mach-apple/rtkit.c and https://lists.infradead.org/pipermail/linux-arm-kernel/2025-December/1089821.html].
- The full RTKit endpoint list (crashlog, syslog, ioreport, oslog) and which ones ANS requires the host to service: **not read** (the Linux `rtkit.c` fetch returned HTTP 429). [hypothesis: ANS needs the management endpoint plus its storage endpoint; syslog/crashlog may be ignorable.]
- Open question: does ANS stall if the host never services the optional endpoints or the mailbox after boot? [hypothesis: unknown, must be measured on hardware with m1n1.]

### 1.3 Command model: NVMMU, TCBs, linear submission queue
- Commands (64 bytes) are NVMe commands, but each also needs a 128-byte TCB (translation control block) in an NVMMU table, indexed by the command tag. The TCB holds opcode, DMA direction flags, command id, length, two PRP fields and an AES IV [verified: apple.c static asserts].
- "Linear SQ" mode: enable at `nvme+0x24908` bit 0; the SQ slot is indexed by tag, and the driver writes the tag (not a tail pointer) to the submission doorbell (admin `0x2490c`, I/O `0x24910`). Completion doorbells at `0x1004` (admin) and `0x100c` (I/O). Pending-command limit at `0x1210`. NVMMU registers: `0x28100` number of TCBs, `0x28108` admin TCB base, `0x28110` I/O TCB base, `0x28118` TCB invalidate, `0x28120` TCB status. Register `0x24008` bit 11 (PRP null check) must be cleared or zero-PRP commands fail [verified: apple.c].
- Limits: 64 tags shared by admin and I/O; admin depth 2; I/O depth 63; exactly one admin and one I/O queue (fixed queue id 1) sharing one IRQ; no asynchronous event requests; no command abort (a timeout means a full controller reset); submission is done under a lock to work around a suspected firmware race [verified: apple.c].
- Apple vendor admin commands (opcode 0xD8) cannot express their buffers as PRPs and rely on SART [verified: apple.c].
- Consequence: the inbox driver expects the standard NVMe BAR registers, tail-pointer doorbells and multiple queues [hypothesis for the exact inbox expectations; the NVMe base spec is public]. ANS matches none of that. `stornvme` is documented as the system miniport for NVMe devices [verified: https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/nvme-features-supported-by-stornvme]; that it only binds PCI class 01/08/02 devices and cannot be pointed at a platform device through ACPI is [hypothesis].

### 1.4 SART
- SART is "a simple address filter for some DMA transactions", it does not remap; not all DMA of a device is filtered [verified: Linux v6.12 `drivers/soc/apple/sart.c` header comment, https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/soc/apple/sart.c].
- t8103 uses SART v2, t6000 v3. 16 entries; granularity 4 KiB (shift 12); per entry CONFIG at `0x00+4*i`, PADDR at `0x40+4*i` (v3 adds SIZE at `0x80+4*i`); allow flag `0xff`. Entries the bootloader left programmed are treated as protected [verified: sart.c].
- For ANS: RTKit shared-memory buffers are allocated as coherent DMA and added to the SART allowed list [verified: apple.c]. The ANS firmware cannot DMA into a buffer that is not in a SART entry [hypothesis: inferred from the above].

### 1.5 DART (data-path IOMMU) and 16 KB pages
- The DART page size is read from hardware (`DART_PARAMS1` bits 27:24); the 16 KB figure for the M1 is a [hypothesis] (the code does not hard-code it; the repo carries it as a hypothesis in `re/m3a-ehw0-measurements.md` line 44). Linux refuses paging domains when the DART page is larger than the kernel page and defaults to identity [verified: Linux v6.12 `drivers/iommu/apple-dart.c`, https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/iommu/apple-dart.c].
- t8103 DART (`DART_T8020`): 16 streams, input address 32 bits, output 36 bits, 4 TTBRs per stream, TCR per stream at +0x100, hardware bypass advertised in `PARAMS2` bit 0 [verified: apple-dart.c].
- Whether ANS's data DMA goes through a DART stream on j293, whether that DART can be set to bypass, and whether bypass is identity: [hypothesis, not read].
- j293 RAM starts at 0x800000000 and ends at 0x9df610000 [verified: ehw0 line 81], within a 36-bit output address.

### 1.6 Power / PMGR
- The Linux driver attaches one or more `power-domains` and uses runtime PM [verified: apple.c]. m1n1 already enabled ANS2's PMGR parents [verified: ehw0]. The PMGR writes for ANS2 itself were not read [hypothesis: the driver should check PMGR status rather than assume it is on].

## 2. What Windows needs

### 2.1 Inbox stornvme vs a miniport
- `stornvme.sys` is the system-supplied StorPort miniport for NVMe devices since Windows 8.1 [verified: Microsoft Learn page above]. Storage controller drivers must be StorPort miniports, and non-PCI host controllers report an ACPI compatible ID instead of a PCI class code [verified, older page, seen only as a search snippet: https://msdn.microsoft.com/en-us/library/jj134356].
- Therefore inbox `stornvme` can only be used if something presents a standard NVMe register/queue interface (option a); otherwise a native StorPort miniport translates requests to ANS (option b) [hypothesis: logical consequence].
- Whether a StorPort miniport can bind to an ACPI-enumerated device with `_CRS` memory and interrupt resources: [hypothesis: yes via the ACPI compatible-ID route; no public sample was found]. Fallback: a WDM/KMDF function driver exposing a virtual StorPort HBA [hypothesis].

### 2.2 Boot
- Windows Setup and the boot path need the storage driver as a boot-start driver (injected into WinPE and into the installed image) [hypothesis: standard behaviour, not re-read]. Bootmgr/winload read the disk through UEFI `BlockIo` before any OS driver exists, so the firmware stage also needs an ANS block driver for the internal SSD [hypothesis].

### 2.3 DMA remapping
- Windows does IOMMU DMA remapping only for drivers that opt in via INF (`DMA Management\RemappingSupported`, Windows 11 24H2 and later; older `DmaRemappingCompatible`); the documentation is written for PCIe devices [verified: https://learn.microsoft.com/en-us/windows-hardware/drivers/pci/enabling-dma-remapping-for-device-drivers].
- `IOMMU_DEVICE_CREATION_CONFIGURATION_ACPI` (InputMappingBase, MappingsCount) "is required for ACPI devices on ARM64 systems" [verified: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-iommu_device_creation_configuration_acpi]. That suggests Windows learns stream mappings from ACPI (IORT) [hypothesis].
- Nothing public was found on Windows' IOMMU page-size assumptions on ARM64. Windows physical pages are 4 KB [hypothesis]. If the DART is not described in ACPI, Windows treats DMA addresses as physical, so the DART must be in bypass or pre-mapped identity. This is what `re/planB-feasibility.md` line 63 assumes [verified: the repo assumes it; untested].

### 2.4 DART 16 KB vs Windows 4 KB pages
1. Bypass: put the ANS DART stream in bypass (hardware supports it, 1.5). Best case: no 16 KB problem [hypothesis for j293; `docs/PLAN.md` line 42 says the same].
2. Driver-managed IOVA: the miniport programs the DART itself. A 4 KB PRP needs its containing 16 KB physical page mapped, exposing up to 12 KB of neighbours to the device [hypothesis].
3. Bounce buffers: copy through 16 KB-aligned contiguous buffers owned by the miniport; correct but costs bandwidth [hypothesis].

## 3. Options

### (a) Standard NVMe controller emulated by the layer
The layer presents a PCIe-ECAM NVMe controller (config space, BAR0 CAP/CC/CSTS/AQA/doorbells, MSI-X through the GICv2m frame of `docs/design/watari-el2-devirq/`), parses the guest SQ on each doorbell trap, builds TCBs, writes ANS linear-SQ doorbells, takes SPI 590, translates completions and injects an MSI. Inbox `stornvme` then works unchanged.
- The layer must also run: an RTKit host (mailbox, interrupts 583-586 now denied), SART and DART programming, PMGR, reset handling (no command abort), and map whatever queues `stornvme` creates onto one I/O queue of depth 63 [hypothesis for stornvme queue behaviour].
- Cost: each doorbell write is a stage-2 trap (the repo estimates 1-3 microseconds per exit [hypothesis: `re/planB-feasibility.md` line 77]), plus completion injection. Likely acceptable for a laptop SSD [hypothesis].
- Conflicts with the project rule "Only the minimal EL2 layer (vGIC, PSCI, timer, IPI) ... everything else stays native" [verified: project CLAUDE.md]. Needs an ADR and Marco's decision.
- Upside: testable in QEMU where `stornvme` already runs (M2, [verified: docs/STATUS.md]); no Windows driver signing.
- Effort [hypothesis]: 8-14 weeks.

### (b) Native StorPort miniport for ANS (what PLAN.md proposes)
- Components: RTKit client, SART setup, NVMMU/TCB and linear-SQ management, interrupt on SPI 590, SCSI-to-NVMe translation, PRP building under the chosen DART strategy, reset handling.
- Repo estimate: "ANS NVMe is a new driver, 6-10 weeks by itself" [verified: `re/planB-feasibility.md` line 95 contains it; the estimate itself is a hypothesis].
- Build requirements: Visual Studio plus the WDK, Windows-hosted tools [hypothesis: not re-verified]. Nothing is installed by this task; installing needs Marco's Time Machine backup confirmation. Plausible build host: the existing Windows 11 ARM64 QEMU VM [hypothesis].
- Signing: a test-signed driver loads only with `TESTSIGNING` on in the BCD and the test certificate installed; Windows shows a "Test Mode" watermark [verified: https://learn.microsoft.com/windows-hardware/drivers/install/the-testsigning-boot-configuration-option]. Since Windows 10 1607 retail kernel drivers need Microsoft signing (attestation or HLK) [verified via search results]; that attestation needs a Partner Center account and an EV certificate is third-party information [hypothesis]. A claim that Windows 11 24H2 ARM64 may require test signing even for validly signed drivers comes from a third-party page only [hypothesis, unverified]. Secure Boot normally blocks test signing [hypothesis; to check on our boot chain].
- A boot-start driver must be injected into the offline image before the first internal-SSD boot [hypothesis].
- There is no ANS in QEMU. The dev loop is the real M1 with the m1n1 proxy (`docs/procedures/m3a-day1-hardware.md`). Off-hardware testing of SCSI/PRP logic would need a software ANS model (host-side test model like the `el2/` vGIC tests; a QEMU device would need a QEMU source build, a download that needs the backup step) [hypothesis].

### (c) Others
- USB first (T15): boot Windows from a USB-attached disk. Needs DWC3 in host mode behind the ATC PHY so the inbox xHCI driver works via ACPI; `usbstor` is inbox. `re/planB-feasibility.md` line 95 already lists "USB mass storage with standard XHCI after PHY bring-up" as a storage path [verified: repo]. The firmware stage needs xHCI + mass storage in EDK2 (standard modules exist) [hypothesis for DWC3 compatibility]. Gives a lock screen without any ANS work; the USB keyboard comes from the same bring-up.
- Firmware-side ANS only: an EDK2 DXE driver exposing `BlockIo` so bootmgr can read the boot volume; Windows still needs a runtime driver, so this is a stepping stone to (b), not an alternative [hypothesis].
- Hybrid (b): firmware does RTKit/SART/NVMMU bring-up and hands over a running ANS; the miniport only manages queues. Needs a handoff contract, and depends on the 1.2 open question [hypothesis].

## 4. Comparison

| | a) layer emulates NVMe | b) native miniport | c) USB first |
|---|---|---|---|
| Inbox driver reused | yes | no | yes (usbstor, xHCI) |
| Layer grows | a lot (RTKit, SART, DART in EL2) | no | no |
| Testable in QEMU | mostly | no (needs ANS model) | no (needs DWC3 on hardware) |
| Windows signing issue | none | test-signing now, attestation later | none |
| Boots from internal SSD | yes | yes | no |
| Fits "minimal layer" | no | yes | yes |
| Estimate [hypothesis] | 8-14 weeks | 6-10 weeks | 3-6 weeks (inside T15) |

## 5. Recommendation and step list for M3b

Recommended: (c) for the first lock screen, (b) for the internal SSD; reject (a) unless (b) hits a wall (signing or boot-start problems).

1. Use the T15 result for USB feasibility; implement DWC3 host bring-up in the firmware stage and boot the Windows disk image from a USB disk (disk prepared from the existing QEMU image; no Microsoft binaries in the repo). Target: lock screen on hardware.
2. In parallel, read-only measurements with the m1n1 proxy: ANS `0x1300` boot status, SART entries left by iBoot, DART params (page size, bypass bit) and the ANS stream, PMGR state of ANS2. These answer the uncertainties in section 7.
3. Re-read Linux `rtkit.c` and `apple.c` in full (facts only) once the rate limit clears; write down the RTKit endpoint set ANS needs.
4. Decide the RTKit owner: firmware (EDK2 DXE, also gives `BlockIo`) with hand-off, or the miniport. Record as an ADR draft.
5. Write the EDK2 ANS `BlockIo` driver (reads the Windows ESP from the internal SSD); test on hardware.
6. After Marco's backup confirmation: set up the WDK build host, build a skeleton StorPort miniport bound to an ACPI `_HID` for ANS (add the device to `tools/acpigen/src/j293_model.c`), test-signed, `TESTSIGNING` on in the installed image.
7. Miniport v1 with polled completions and bounce buffers; then SPI 590 interrupts; then DART bypass or mapped IOVA; then performance.
8. Inject the miniport as boot-start driver in the offline image; test internal-SSD boot under the layer.
9. Distribution and signing plan (attestation) as a separate decision.

## 6. Risks
- R1 ANS needs host servicing of RTKit endpoints or mailbox interrupts that the layer currently denies (583-586): the interrupt map would have to change [verified: map denies them; the need is a hypothesis].
- R2 DART bypass may be unavailable or not identity for the ANS stream on j293, forcing driver-managed IOVA or bounce buffers.
- R3 A StorPort miniport may not bind to a non-PCI ACPI device without extra pieces (virtual miniport route).
- R4 Boot-start driver signing: test signing may be blocked by Secure Boot policy or by newer Windows ARM64 behaviour (third-party claim).
- R5 Hardware-only dev loop (no QEMU ANS model); an ANS crash needs a full reset and maybe a reboot; risk to the internal SSD (macOS and recovery must never be touched: use a scratch partition or the external disk only).
- R6 WDK and Visual Studio install and signing need downloads, hence Marco's backup confirmation first.
- R7 Option (a) enlarges the EL2 attack and bug surface, against the minimal-layer decision.
- R8 Licensing: Linux drivers are GPL-2.0; implement from register facts only, no copying.

## 7. Top 3 uncertainties
1. Which RTKit endpoints and what ongoing mailbox servicing ANS requires, and whether ANS can be left running after firmware boots it (decides who owns RTKit).
2. Whether ANS's data path goes through a DART stream on j293, whether bypass is allowed and identity, and what SART/DART state iBoot leaves.
3. Whether a StorPort miniport can bind to an ACPI-enumerated storage controller on ARM64 Windows and load as boot-start under test signing (public docs are silent).

## 8. Sources
- Linux v6.12 `drivers/nvme/host/apple.c`, `drivers/soc/apple/sart.c`, `drivers/iommu/apple-dart.c` (raw.githubusercontent.com/torvalds/linux/v6.12/...), facts only, read through a summarising fetch tool.
- Not read: Linux `drivers/soc/apple/rtkit.c` (HTTP 429); Asahi docs pages for ANS/ASC (404 or empty landing pages).
- Microsoft Learn: StorNVMe features, DMA remapping for drivers, `IOMMU_DEVICE_CREATION_CONFIGURATION_ACPI`, testsigning option, Storport driver, Device.Storage requirements (snippet only).
- Repo: `docs/PLAN.md`, `docs/STATUS.md`, `re/planB-feasibility.md`, `re/m3a-ehw0-measurements.md`, `docs/design/watari-el2-devirq/`.
