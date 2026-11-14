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
- Not done here (T16c later covers the rest, see its section): input (no keyboard; Setup waits), install.wim, R6/R7 variants. T16c: the 10-bit truthful GOP fails, `trap-notc` and MEM=2G work, SMP 8 is not testable on the bench.

## T16c matrix (2026-11-14, night 5): day-4 configurations on the bench

Common: `el2/boot/run-winram.sh`, firmware built from the branch (`tools/build-edk2-m1.sh --model qemu-virt-bench`, with `--bgr8` = tag `vb`, without = tag `vb10`), image `winram.img`, `RDBASE=0x80000000`, layer args as the script builds them. Evidence: `$WATARI_DATA/logs/winram/t16c-<run>/` (serial.log, pc.log, shot-NNNN.png). Times are host wall-clock of this run (a faster host than r14), shots every 60 s.

| Run | Config | Furthest stage | Time to Setup | `CPU_ON` | Evidence |
|---|---|---|---|---|---|
| m1 | `trap-notc`, SMP 4, 3G, BGR8 (8-bit fake panel) | **S5**: Setup "Attendere" GUI | about 2 min (shot 3) | 3 (cpu 1-3) | `t16c-m1/shot-0003.png`, `shot-0004.png` (stopped by me at 3 min) |
| m2 | `trap`, SMP 8, 3G, BGR8 | **S5**: Setup driver page "Installa il driver per visualizzare l'hardware" | "Attendere" at about 2 min, driver page by about 5 min | **3**: Windows used 4 CPUs only (see below) | `t16c-m2/shot-0003.png`, `shot-0007.png` |
| m3 | `trap`, SMP 4, 3G, **truthful 10-bit GOP** (vb10 firmware, PixelBitMask R3FF00000 G000FFC00 B000003FF, 10-bit fake panel) | **S4 only**: kernel and 3 secondaries up, then **no picture at all** (panel black under the layer band, no logo, no Setup, over 8 shots / 7.5 min); all 4 cores sit at one and the same kernel PC (`fffff8022bc7fe4c`, EL1t, DAIF masked) from about 200 s on | never | 3 | `t16c-m3/shot-0002..0008.png` (+ `-as8.png`), `pc.log` |
| m4 | `trap`, SMP 4, **MEM=2G** (layer emulates the MSI frame), BGR8 | **S5**: Setup "Attendere" GUI | about 3 min (shot 4) | 3 | `t16c-m4/shot-0004.png` |

Conclusions:

- **`trap-notc` works with Windows** (m1) [verified: run]: day 4's W2 flag `--cpuif trap-notc` does not change what the bench reaches. What it does not prove: the bench's QEMU GIC has a working CPU-interface trap path for both modes; the M1's own behaviour is the hardware's [hypothesis].
- **SMP 8 cannot be exercised on this bench** [verified: run]: the layer had 8 vCPUs (`cores (redistributors) = 8`, QEMU shows cpu0-7), but the `qemu-virt-bench` MADT lists only 4 GICCs (`QEMU_VIRT_NCPUS 4`, `tools/acpigen/include/acpigen.h`), so Windows started 3 secondaries and the other 4 vCPUs stayed parked. m2 therefore proves only that an 8-vCPU layer (vGIC with 8 redistributors, 4 unused) does not disturb a 4-CPU Windows. The 8-CPU MADT of the j293 model is the hardware day's first look [open]. A bench model with an 8-GICC MADT would close it.
- **The truthful 10-bit GOP (PixelBitMask) does NOT work with this Windows build on the bench** (m3) [verified: run]: firmware and layer are identical to m1 apart from the GOP format, and the result moves from Setup to a permanent black panel with every core frozen at the same kernel address. Same signature as the early r1-r5 freeze (kernel up, IRQ/FIQ masked, EL1t) but at another address, i.e. it looks like a bugcheck whose screen cannot be drawn; the cause being the PixelBitMask GOP (BasicDisplay refusing the format) is [hypothesis]: no stop code is visible. This answers Q-E1: **BGR8 firmware is mandatory for the day 4 Windows rungs; a truthful mask is not an option.** It also agrees with the Microsoft requirement quoted in DESIGN 5 (PixelFormat must be BGR8).
- **MEM=2G (MSI frame emulated by the layer) works** (m4) [verified: run]: no "MSI frame page 0xFFFFC000 is guest RAM here: not emulated" line (so the frame hole was in stage 2), RDBASE 0x80000000 + 0x2ca00000 fits below 3 GiB, Setup reached. This closes the "[open]" of the Conclusions above: the frame path does not break Windows on the bench. Whether Windows touched the frame is not logged [not verified]. The doorbell is not used by any device on the bench.
- All four runs saw `PSCI CPU_ON` for cpu 1, 2, 3 only [verified: serial.log].

## T16b: the j293 GIC addresses on the bench (2026-11-14)

Question: can the PRODUCTION tables (acpigen model `j293`, GIC at `0xF_0000_0000`) boot WinPE on the bench? Answer: the GIC addresses were
not the problem once the layer can present them; the 8-core MADT is [verified: r-t16b-2 vs -3]. Evidence (not in the repo):
`$WATARI_DATA/logs/winram/t16b-{1,2,3}/` (serial.log, pc.log, shot-NNNN.png). Firmware: `WATARI_M1_BUILD_TAG=t16b tools/build-edk2-m1.sh --bgr8`
(model j293, no `--model`) and `WATARI_M1_BUILD_TAG=t16be4 ... --model j293-e4`. Run: `WATARI_M1_BUILD_TAG=<tag> PANEL=8 MEM=3G RDBASE=0x80000000 SMP=4
WEL2_ARGS="wel2.vgicipa=m1" el2/boot/run-winram.sh` (CPUIF=trap).

**The option `wel2.vgicipa=m1`** (QEMU bench only; `el2/boot/main.c`, `plat_select.c plat_vgic_windows`): the guest-visible vGIC windows are the M1's
(GICD `0xF_0000_0000`, GICR `0xF_1000_0000`, 128 KB per core), the same constants the j293 MADT and `plat_m1` use. The physical GIC stays at QEMU's
`0x0800_0000`/`0x080A_0000`; stage 2 gets two holes over it (the guest cannot touch it) and leaves the M1 windows unmapped (they already are holes of
the vGIC), so every guest access faults to EL2 and `vgic_mmio_*` answers it, in `hw`, `trap` and `trap-notc` modes (the vGIC model only ever saw
the configured base; the CPU interface does not depend on the window). The layer-built guest FDT's `intc` node and the test guest's info block carry the
moved addresses; the option needs `wel2.abi=fdt wel2.gfdt=build` or the test guest (fatal otherwise: the patched QEMU FDT would say `0x0800_0000`).
Default unchanged; on `plat_m1` a no-op (logged). Host tests: `el2/tests/test_plat.c t_vgic_windows` (translation, M1 no-op, a vGIC on the
moved windows answers there and refuses QEMU's). Bench regression: `run-m1guest.sh` G1-G12 PASS with the option in hw, trap and trap-notc (now in `run-all.sh`).

| Run | Config | Result |
|---|---|---|
| t16b-1 | plain j293 firmware, `vgicipa=m1`, SMP 4, 3G | boot logo, `CPU_ON` cpu 1-3 accepted, then `PSCI SYSTEM_RESET` after about 60 s (a bugcheck restart) |
| t16b-2 | same with `QEMU_EXTRA=-no-shutdown` | `shot-0002.png`: "Your device ran into a problem and needs to restart", **Stop Code INTERNAL_POWER_ERROR (0xA0)** |
| t16b-3 | firmware with model `j293-e4` (j293 with only the four Icestorm cores, MPIDR 0..3, GICR range of 4 frames; everything else j293: GIC, MSI frame, GTDT, FADT, no DBG2), same option | **Setup first screen** ("Installa il driver per visualizzare l'hardware"), `shot-0007.png` at 370 s, 4 vCPUs, about 134 000 layer log lines |

What the j293 tables assume that the bench lacks (checked one by one):
- GIC addresses: handled by the option (above). GTDT PPIs 27/30 and the FADT (PSCI over HVC, HW-reduced): the layer answers both (the bench
  uses the same FADT and the QEMU GTDT lists the same PPIs); no change needed [verified: t16b-3 runs with them].
- MSI frame `0xFFFFF000`: emulated by the layer only when no guest RAM covers `0xFFFFC000`; with `MEM=3G` it is plain RAM ("not emulated", as r9-r15) and
  Setup was reached without it. The frame path (`MEM=2G`) with the j293 tables was NOT run here (T16c).
- DBG2/CSRT: none in either model. Memory map: the bench's RAM at `0x4000_0000` with the RAM disk at `0x8000_0000` (as T22).
- **Eight cores in the MADT/PPTT, four on the bench (and MPIDR `0x10100..3` for the P cores that QEMU cannot present):** the only blocking difference.
  With four GICC entries (`j293-e4`) everything else of j293 boots to Setup; with eight, Windows starts the three cores that exist and stops with
  INTERNAL_POWER_ERROR (0xA0). Why (a power policy over processors that never come online, the two efficiency classes, or the failed starts) is
  [hypothesis]; the parameters of the stop were not read. A bench with eight matching cores needs QEMU `-smp 8` plus a virtual-MPIDR remap and a
  stack-slot formula for QEMU Aff0 4..7 (`wel2_core_index` accepts Aff0 < 4 only; `start.S`): not done [open]; `wel2.vgicipa=m1` does not touch that.
- Consequence for the hardware day: the all-eight-cores j293 tables are untested on any bench; the first time they run is on the M1. The bench
  proves everything else of the production description (addresses, MSI, GTDT, FADT, GOP, RAM disk) with `j293-e4`.

## Next step

1. Rebuild the hardware bundle (production firmware, j293 model) with the new `PcdWatariRamDiskType` default (type 0); on the bench the j293 firmware
   stops in the HAL by design (wrong GIC addresses for QEMU), so the bench's `qemu-virt-bench` build is the one that carries the evidence.
2. Day-4 rungs W1-W3 (T24): the expected screens are r14 `shot-0004.png` (logo, spinner), `shot-0009.png` (Setup).
3. Open question for Marco: whether NFIT + NVDIMM SSDT on the real M1 is acceptable (it is what Windows needs here).
4. Open question for Marco (T16b, not decided here): if Windows on the real M1 stops with INTERNAL_POWER_ERROR (0xA0) or similar with the eight-core j293 tables, should the model `j293-e4` (the four Icestorm cores only, MPIDR 0..3) be allowed as an explicit hardware fallback? It was built for the bench only; the production build and the day-4 bundle exclude it, and nothing selects it unless `--model j293-e4` is given. Using it on the M1 would run Windows on 4 of the 8 cores (the P cores stay parked) and needs Marco's decision plus a bundle gate update.
