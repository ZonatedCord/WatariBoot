# M3b bench: Windows PE / Setup from a RAM disk on the M1-shaped QEMU bench (T22)

SPDX-License-Identifier: MIT

Date: 2026-11-14. Design: `docs/design/watari-winram/DESIGN.md`. Evidence (not in the repo): `$WATARI_DATA/logs/winram/<run>/`
(serial.log, pc.log, shot-NNNN.png). Image: `tools/m3a/build-winram-image.sh` (sha256 in `$WATARI_DATA/winram/winram.img.sha256`).
Launchers: `el2/boot/run-winram.sh` (M1-shaped bench), `el2/boot/run-winram-ref.sh` (image as NVMe on plain QEMU-virt),
`el2/boot/run-m2.sh` with `DISK=<img> DISKFMT=raw` (layer + stock EDK2, image as NVMe).
Reading rule: only on-screen text and runtime register state; no Microsoft binary was opened or analysed.

## Result in one paragraph

**Stage S5 reached: Windows Setup's first screen draws from a RAM disk on the M1-shaped bench** (r14, 4 vCPUs, layer in trap mode,
production-shaped firmware with the 8-bit GOP). Two defects of OUR side blocked it, in this order:
(1) the bench's ACPI tables described a machine that does not exist (the j293 model puts the GIC at `0xF_0000_0000`, the layer's
bench vGIC is at QEMU's `0x0800_0000`; the `qemu-virt` model puts the MSI frame at `0x0802_0000`, which the layer does not back):
the kernel stopped at one fixed place, silently (F-35 a); (2) the RAM disk claimed as `EfiBootServicesData` made the kernel stop with
`MEMORY_MANAGEMENT (0x1A)`; as `EfiReservedMemoryType` (NFIT + NVDIMM SSDT published by RamDiskDxe) it does not (F-35 b).

## Stages (DESIGN 5)

| Stage | What | Result | Evidence |
|---|---|---|---|
| S1 | bootmgr starts from the RAM disk | PASS [verified] | r1 `m01-as8.png` (boot logo), serial `FSOpen: Open 'BOOTAA64.EFI'` |
| S2 | BCD + SDI + boot.wim read | PASS [verified] | serial `FSOpen ... boot.sdi / boot.wim` in every run |
| S3 | winload + kernel load, ExitBootServices | PASS [verified] | kernel code runs (PC in `0xfffff8...`), r5 `pc.log` |
| S4 | kernel start, HAL, CPU bring-up | PASS from r9 on [verified]: HAL programs the vGIC, PSCI `CPU_ON` for cpu 1-3, IPIs (SGI traps) | r9 serial `PSCI CPU_ON cpu 1..3 accepted`, r14 `ICC_SGI1R_EL1 write trapped` |
| S5 | session, WinPE, Setup | **PASS** [verified]: "Attendere" then "Installa il driver per visualizzare l'hardware" (the same screen the reference shows) | r14 `shot-0004.png`, `shot-0009.png`; ref1 `shot-0002.png` |

## Runs

| Run | Config | Result | Evidence |
|---|---|---|---|
| ref1 | image as NVMe, plain QEMU-virt, stock EDK2, no layer, SMP 4, 3G | Setup first screen after about 4 min: the image is good | `ref1/shot-0002.png` |
| r1 | M1 path: layer + `bgr8` firmware (ACPI j293), SMP 4, 4G, image at 4 GiB | boot logo, then frozen 40 min; cpu0 `EL1t`, IRQ/FIQ masked, PC page offset `0x281280`; no `CPU_ON` | `r1/` (previous agent) |
| r2 | same, SMP 8 | identical hang, same offset (different KASLR base) | `r2/pc.log` |
| r3-hw | same, `CPUIF=hw` | identical hang at offset `0x281280` | `r3-hw/pc.log` |
| r4-layer-nvme | layer (trap) + stock EDK2 + image as NVMe (`run-m2.sh DISK=`) | Setup first screen in about 5 min: the layer is innocent | `r4-layer-nvme/shot-0006.png` |
| r5-regs | as r3, `info registers`: `PC == X8 == X15`, EL1t, DAIF masked | still the same hang | `r5-regs/pc.log` |
| r6-qv | firmware with ACPI model `qemu-virt` (GIC right, MSI frame wrong), PANEL=8 | identical hang | `r6-qv/pc.log` |
| r8-trace0 | as r6 with `wel2.trace=0` | the HAL's GICD writes appear (`TR` lines, pc `0xfffff802...aaa...`, last access GICD_CTLR = 0x52), then nothing, then the same hang | `r8-trace0/serial.log` |
| r9/r10/r11 | new model `qemu-virt-bench` (QEMU GIC, MSI frame 0xFFFFF000 SPI 928+32 in the MADT; the layer did NOT emulate the frame, see Conclusions), 3G, ramdisk at 0x80000000, BootServicesData | kernel runs, `CPU_ON` x3, then PSCI `SYSTEM_RESET` within seconds | `r9-bench/serial.txt` |
| r12-hold | same with `QEMU_EXTRA=-no-shutdown` (QEMU stays paused after the reset) | blue screen text: **Stop Code MEMORY_MANAGEMENT (0x1A)** | `r12-hold/shot-0004.png` |
| r13-4g | same, 4G, ramdisk at 0x100000000 | same stop code | `r13-4g/shot-0003.png` |
| r14-reserved | only change: `PcdWatariRamDiskType=0` (Reserved, NFIT + SSDT) | **Setup first screen** (about 340 s of host time, 4 vCPUs) | `r14-reserved/shot-0004.png`, `shot-0009.png` |
| r15-default | the repo default after this change (type 0 is now the default), `qemu-virt-bench` model, 3G | Setup first screen, same as r14 (about 250 s) | `r15-default/shot-0006.png` |

## Conclusions

- The M1 layer, vGIC (trap and hw CPU interface), timers, PSCI, IPIs and the fake-panel GOP carry a full WinPE boot to Setup with 4
  cores [verified: r14, r4]. The 8-bit BGR GOP is accepted by bootmgr and BasicDisplay, the text is legible [verified: r14 PNGs].
- A kernel that stops at a fixed place with IRQ/FIQ masked and does not start the secondaries is what a HAL/ACPI mismatch looks
  like here; the PC page offset (`0x281280`) was identical for three different ACPI/GIC configurations, so it is the common
  bugcheck-freeze path, not the cause [hypothesis: not disassembled]. The layer trace (`wel2.trace=0`) shows the HAL did reach the GIC
  (r8), so the mismatch hit after the distributor enable, on the MSI frame probe [hypothesis]. The next defect (0x1A) was invisible
  until the machine description was consistent.
- `EfiBootServicesData` for the RAM disk does not work with this Windows build; `EfiReservedMemoryType` does [verified: r12-r14].
  Why (the loader or the kernel treating the RamDisk device path's memory as in use) is [hypothesis]; R-W9 of the risk register
  is therefore **confirmed in effect**.
- The bench-only ACPI model `qemu-virt-bench` is NOT for the real M1: on the hardware the j293 model is the right one (GIC at 0xF_0000_0000, MSI frame at 0xFFFFF000).
  What the bench showed: a MADT that matches the machine (r6 -> r9) lets the HAL go on. Which of the three changes between `qemu-virt` and `qemu-virt-bench`
  (MSI frame address 0x08020000 -> 0xFFFFF000, FADT j293 instead of none, DBG2 dropped) fixed it was NOT isolated [hypothesis]. In particular the
  "MSI frame at 0xFFFFF000 is required" reading is NOT verified: in every run r9-r15 the layer did not emulate that frame (next point).
- On the bench the layer's MSI frame exists only when no guest RAM covers `0xFFFFC000` (`el2/boot/main.c` `v2m_page_free`). QEMU RAM starts at `0x4000_0000`, so `MEM=3G`
  ends exactly at 4 GiB and covers the page: every run r9-r15 (3G and 4G) printed "MSI frame page 0xFFFFC000 is guest RAM here: not emulated" [verified: serial logs]. So Setup was
  reached WITHOUT the layer's GICv2m frame (the MADT still advertised it; the HAL read plain RAM there). The frame path (`MEM=2G`) was not run on the bench [open]. On the real M1 the
  frame is emulated (the t8103 has nothing at that page). Setup was reached with `MEM=3G RDBASE=0x80000000` (r14, r15); 4G + type 0 was not run.
- The "Setup first screen" is the "Installa il driver per visualizzare l'hardware" page, the same as the reference run: the booted WinPE sees no install medium/driver; that is the S5 criterion, not an install.
- NFIT publication with type 0 is confirmed in the serial log (`RamDiskPublishNfit: ... will publish to NFIT` / `will create one`, r14, r15); the SSDT install has no log line of its own [not directly verified].
- Not done: input (no keyboard; Setup waits), install.wim, the 10-bit truthful GOP variant (R2), `trap-notc`, SMP 8 (R5), R6/R7 variants.

## Next step

1. Rebuild the hardware bundle (production firmware, j293 model) with the new `PcdWatariRamDiskType` default (type 0); on the bench the j293 firmware
   stops in the HAL by design (wrong GIC addresses for QEMU), so the bench's `qemu-virt-bench` build is the one that carries the evidence.
2. Day-4 rungs W1-W3 (T24): the expected screens are r14 `shot-0004.png` (logo, spinner), `shot-0009.png` (Setup).
3. Open question for Marco: whether NFIT + NVDIMM SSDT on the real M1 is acceptable (it is what Windows needs here).
