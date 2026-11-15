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

## T16d: eight cores with the M1's MPIDRs on the bench (2026-11-14)

Question (open after T16b): do the all-eight-cores j293 tables boot WinPE when the bench really has eight cores named like the M1's? Answer: **yes**,
the plain j293 `--bgr8` firmware reaches Setup's first screen (language/driver page "Installa il driver per visualizzare l'hardware") with all seven
`CPU_ON` accepted [verified: run `t16d-1`]. So the T16b 0xA0 came from the missing cores, not from the j293 tables (F-37 updated).

**The option `wel2.vmpidr=m1`** (QEMU bench only, opt-in; `el2/include/wel2_vmpidr.h`, `boot/main.c`, `boot/psci.c`, `boot/start.S`):
- Physical side: QEMU virt `-smp 8` gives Aff0 = 0..7, Aff1 = Aff2 = 0 [hypothesis from QEMU's numbering, checked at start-up: the layer stops
  with "needs QEMU -smp 8 with Aff0 = 0..7 in one cluster" otherwise; on the log the eight redistributors were found in that order].
- Virtual side: vCPU i (= physical core i, as everywhere) reads `VMPIDR_EL2` = `0x80000000 | aff`, aff = 0..3 for E cores and `0x10100..0x10103` for P
  cores: exactly the real M1's values (bit 31 RES1, U = 0, MT = 0, [verified: re/m3a-ehw0-measurements.md]) and the MADT's affinity fields
  (`tools/acpigen/src/j293_model.c`). Same table feeds the vGIC (`cfg.affinity`: GICR_TYPER Affinity_Value, ICC_SGI1R decode with Aff2/Aff1 for the P
  cores, GICD_IROUTER match), the layer-built guest FDT and the test guest's info block.
- PSCI: CPU_ON / AFFINITY_INFO accept the guest's MPIDR (RES bits 31/30/24 stripped, as on the M1), find the vCPU by the virtual table and hand the
  PHYSICAL QEMU MPIDR of the same index to the firmware; the stack-slot check uses the flat rule.
- Stack slot of a QEMU core: `start.S core_sp` reads `g_core_idx8` (its own 128-byte line, cleaned to memory by the boot core, invalidated by the
  secondary before the MMU-off read); non-zero = idx = Aff0 (Aff0 < 8, rest 0), zero = the M1 formula Aff0 + 4*Aff1.
- Default and `plat_m1` unchanged: `g_vmpidr_m1` stays false (on `plat_m1` the option only logs "nothing to do"), `g_vaff` equals the physical affinity,
  `set_virtual_ids` writes the physical MPIDR as before, PSCI takes the old branches. The one change on the M1 path is a load of `g_core_idx8` (zero)
  in `core_sp` [hypothesis: harmless; not run on hardware].

Host tests: `el2/tests/test_vmpidr.c` (MPIDR table, PSCI lookup incl. non-M1 names, physical-rule, GICR_TYPER of eight frames with Last on the
last, SGIs to P cores via Aff2/Aff1 and to E cores, IROUTER). It caught one real bug before the bench: the flat-index mask had included bit 31.
Bench regression: `run-m1guest.sh` with `SMP=8 WEL2_ARGS=wel2.vmpidr=m1` (G1-G12 PASS in hw, trap, trap-notc; G9 "UP 7"; guest MPIDR 0x0..0x3,
0x10100..0x10103); now in `run-all.sh`.

| Run | Config | Result |
|---|---|---|
| t16d-1 | plain j293 firmware (`WATARI_M1_BUILD_TAG=t16b`), `-smp 8`, `PANEL=8 MEM=3G RDBASE=0x80000000`, trap, `wel2.vgicipa=m1 wel2.vmpidr=m1` | `CPU_ON` cpu 1-7 accepted, no core parked, no FATAL; kernel on all 8 vCPUs at 100 s (pc.log), Setup "Attendere" at 183 s (`shot-0003.png`), **Setup driver page at 368 s** (`shot-0005.png`), about 130 000 layer log lines. Stopped by us at 368 s. |

Evidence (not in the repo): `$WATARI_DATA/logs/winram/t16d-1/` (serial.log, pc.log, shot-NNNN.png). Run: `WATARI_M1_BUILD_TAG=t16b PANEL=8 MEM=3G
RDBASE=0x80000000 SMP=8 CPUIF=trap QEMU_EXTRA=-no-shutdown WEL2_ARGS="wel2.vgicipa=m1 wel2.vmpidr=m1" el2/boot/run-winram.sh`.

What it does not show: the M1's AIC/FIQ interrupt path, the real efficiency/performance class behaviour, and a timing difference of real hardware
[hypothesis]. It does show that the PPTT/MADT (E and P clusters, MPIDR 0x10100), the GICR frames for eight cores and 7 PSCI starts are accepted by
the Windows kernel.

## T16f soak: Setup idle for 55 minutes with the day-4 configuration (2026-11-15)

Question: does WinPE/Setup, sitting idle on its first screen with the day-4 configuration, keep running (timer, IPIs, layer) or does it stall,
bugcheck or lose cores over time? Answer: **it keeps running for the whole hour, no stall, no stop code, no layer fault** [verified: run `t16f-1`].

Config: plain j293 `--bgr8` firmware `WATARI_M1_EFI-bgr8.fd` (built from the branch; `.build.txt`: model=j293, bench=0, bgr8=1, no tag, commit 87eba25,
clean), `-smp 8`, `MEM=3G RDBASE=0x80000000 PANEL=8`, `CPUIF=trap-notc`, `wel2.vgicipa=m1 wel2.vmpidr=m1`, `QEMU_EXTRA=-no-shutdown`, `SHOT_EVERY=120`,
`STALL=0` (never stop on identical screens), `TIMEOUT=3700`. Command: `TAG=t16f-1 PANEL=8 MEM=3G RDBASE=0x80000000 SMP=8 CPUIF=trap-notc TIMEOUT=3700
SHOT_EVERY=120 STALL=0 QEMU_EXTRA=-no-shutdown WEL2_ARGS="wel2.vgicipa=m1 wel2.vmpidr=m1" el2/boot/run-winram.sh` (no script change needed).

| t (s) | layer log lines | ICC_SGI1R writes (IPIs) | timer irqs cpu 0 (hex) | notes |
|---|---|---|---|---|
| 378 | 137 k | 66 k | 0xb34c | Setup driver page on screen (first seen between 244 and 366 s) |
| 624 | 205 k | 99 k | 0xfac8 | |
| 995 | 270 k | 131 k | 0x14118 | |
| 1244 | 336 k | 164 k | 0x187cc | |
| 1613 | 403 k | 196 k | 0x1ce1c | |
| 2820 | 666 k | 325 k | 0x2e694 | |
| 3064 | 732 k | 357 k | 0x32e74 | |
| 3433 | 798 k | 390 k | 0x37460 | |
| 3716 (end) | ~850 k | 414 896 | 0x3ab10 | QEMU stopped by the script at TIMEOUT |

- Counters grow steadily (about 65 timer irqs/s on cpu 0 [0x3ab10 = 240 400 in 3716 s, the layer's own tally], about 110 SGIs/s). The
  layer log is 120-150 k lines per 10 min; no FATAL / abort / UNEXPECTED / bugcheck line in the whole log (0 matches).
- `pc.log` (31 snapshots, PC of all 8 vCPUs): at every snapshot most cores sit at the same kernel address `fffff802ba67fe4c` (EL1t, idle loop, PSTATE
  0x600031c4 [hypothesis: the idle loop]); cpu0 and one or two others were seen at other kernel addresses (`fffff802ba414a68`) or in the layer (EL2h,
  `0x4008...`: in the middle of an IPI/timer trap) in different snapshots, i.e. the cores keep moving and wake up. All eight cores stayed in the guest.
- Screenshots (31): `shot-0005` (about 400 s), `0013`, `0026`, `0032` (the end) read with the viewer: Setup page "Installa il driver per visualizzare
  l'hardware", language Italian, identical; the four PNGs have the same SHA-1 prefix, the whole run shows one static screen (Setup waits for a driver or
  a click: static by design, there is no cursor or clock). No stop code. The layer's band shows "CHECKPOINT 8 OK ENTER_GUEST, LAST ERROR: NONE".
- Liveness evidence therefore is the counters and the PCs, not the screen.

Verdict [verified: run `t16f-1`]: with the day-4 configuration on the bench Setup sits at its first screen for 55 minutes (about 50 min after it first
appeared) with timer irqs and IPIs flowing, 8 vCPUs alive and no fault. No F-NN. What it does not show: the M1's AIC/FIQ path, real timing, anything
needing input (no keyboard events were sent), and Setup beyond its first page.
Evidence (not in the repo): `$WATARI_DATA/logs/winram/t16f-1/` (serial.log, pc.log, shot-NNNN.png), `$WATARI_DATA/logs/t16f-1.out`.

## T16g: the fallback ladder for day 4 on the bench (2026-11-15)

Question: if W2/W3 fail on the M1 in the multi-core description, can Ciani switch firmware without a rebuild, and does each fallback reach Setup on the bench?
Answer: **yes, all three variants reach Setup's driver page** [verified: runs `t16g-j293`, `t16g-e4`, `t16g-e1`]. New: acpigen model **`j293-e1`** (the boot core only: one
GICC and one PPTT entry, MPIDR 0, a GICR range of one frame; everything else as j293; host test `tools/acpigen/tests/test_models.c`), the build option
`--model j293-e1`, and the bundle option `--fallback-efi e4=<fd> --fallback-efi e1=<fd>` (below).

Firmware (all from the clean commit 58f104f, manifest `dirty=0`): `tools/build-edk2-m1.sh --bgr8` (j293), `WATARI_M1_BUILD_TAG=e4 ... --bgr8 --model j293-e4` ->
`WATARI_M1_EFI-bgr8-e4.fd`, `WATARI_M1_BUILD_TAG=e1 ... --model j293-e1` -> `WATARI_M1_EFI-bgr8-e1.fd`. The tag only keeps names and workspaces apart; the manifest
says `model=j293-e4` / `j293-e1` and `build_tag=e4` / `e1`.

Common run line: `PANEL=8 MEM=3G RDBASE=0x80000000 CPUIF=trap-notc TIMEOUT=540 SHOT_EVERY=60 WEL2_ARGS="wel2.vgicipa=m1[ wel2.vmpidr=m1]" el2/boot/run-winram.sh` (day-4 W2 uses
`trap-notc`; a screenshot every ~60 s, so "time to Setup" has that resolution).

| Run | Firmware | `SMP` / layer options | Time to Setup (shots) | `CPU_ON` accepted | FATAL | After |
|---|---|---|---|---|---|---|
| t16g-j293 | j293 (8 cores) | 8, `vgicipa=m1 vmpidr=m1` | driver page on shot 6 (310 s) | cpu 1-7 (7) | 0 | `Setup` stable to 540 s, 149 500 layer lines at 499 s |
| t16g-e4 | j293-e4 (4 E cores) | 4, `vgicipa=m1` | "Attendere" on shot 5 (247 s), driver page on shot 6 (310 s) | cpu 1-3 (3) | 0 | stable to 543 s, 158 600 layer lines |
| t16g-e1 | j293-e1 (boot core) | 1, `vgicipa=m1` | driver page on shot 5 (245 s) | none (one core) | 0 | unchanged to 554 s, only 7 400 layer lines (almost no IPIs) |

Evidence (not in the repo): `$WATARI_DATA/logs/winram/t16g-{j293,e4,e1}/` (serial.log, pc.log, shot-NNNN.png). For the hardware the matching loader options are: **e4** `--smp`
(m1n1 starts all eight, the layer's `CPU_ON` path needs the spin-table secondaries; Windows only starts cpu 1-3, the P cores stay parked), **e1** `--no-smp` (m1n1 starts no
secondary, its tree lists one cpu) [hypothesis on the M1: the loader's own comment on `dt_set_cpus`]. On the bench e1 runs with QEMU `-smp 1`, which is the same shape.

**Guard** (`tools/m3a/build-m1-payload.sh`, test `tools/m3a/test_fallback_guard.sh`, 29 checks, all pass): the main `WATARI_M1_EFI.fd` is still the plain j293 (the T16e guard
is unchanged); a fallback is accepted only when declared as `e4=` or `e1=` and its manifest says model `j293-e4` / `j293-e1`, `bgr8` equal to the main firmware, no
bench, no RAM-disk stand-in, no PCD, build tag equal to its name, `dirty=0`, a commit, and the SHA-256 of the file; it lands as `WATARI_M1_EFI-e4.fd` / `-e1.fd` (+ `.build.txt`),
both in `SHA256SUMS`. Refused (verified): another name (`e8=`), no `NAME=`, a name twice, a missing file or manifest, the wrong model in a slot, bench / stand-in / PCD /
other tag / dirty / unknown commit / edited file, a fallback build as the MAIN firmware, a bench main firmware with fallbacks declared, `WATARI_M1_EFI_ALLOW_BENCH=1` or
`WATARI_NO_EDK2=1` together with fallbacks. `watari-load.py --edk2 WATARI_M1_EFI-e1.fd` needs no change: any file listed in `SHA256SUMS` is accepted (new loader test: D2 with each
fallback name, `--no-smp` starts no secondary). Rehearsal bundle `$WATARI_DATA/rehearsal-n5/day4-fb/bundle` (54 files in `SHA256SUMS`, `shasum -c` all OK, `--winram default`):
dry runs of both fallbacks exit 0 (`docs/procedures/m3a-day4-windows-ramdisk.md` 2.3).

Not shown: anything about the real M1 (AIC/FIQ, E/P-core differences, timing); the bench only proves that the machine descriptions with 1, 4 and 8 cores are accepted by the Windows kernel.
Using a fallback on the hardware: Marco approved e4 and then e1 in advance on 2026-11-15 (`docs/decisions/2026-11-15-marco-monday.md`; open question 4 below, Q-4D-8 in the procedure).

## Next step

1. Rebuild the hardware bundle (production firmware, j293 model) with the new `PcdWatariRamDiskType` default (type 0); on the bench the j293 firmware
   stops in the HAL by design (wrong GIC addresses for QEMU), so the bench's `qemu-virt-bench` build is the one that carries the evidence.
2. Day-4 rungs W1-W3 (T24): the expected screens are r14 `shot-0004.png` (logo, spinner), `shot-0009.png` (Setup).
3. ~~Open question for Marco: whether NFIT + NVDIMM SSDT on the real M1 is acceptable.~~ **Answered: ACCEPTED** (2026-11-15, `docs/decisions/2026-11-15-marco-monday.md`).
4. ~~Open question for Marco~~ **Answered: both fallbacks e4 then e1 approved in advance** (2026-11-15, `%s`; each use still needs the table-6.3 symptom and the recorded W2 FAIL/ABORT). Original question (T16b): if Windows on the real M1 stops with INTERNAL_POWER_ERROR (0xA0) or similar with the eight-core j293 tables, should the models `j293-e4` (the four Icestorm cores only, MPIDR 0..3) and, since T16g, `j293-e1` (boot core only) be allowed as explicit hardware fallbacks? Since T16g both are built, benched to Setup and can be carried in the day-4 bundle as DECLARED fallbacks (`build-m1-payload.sh --fallback-efi e4=.. e1=..`, manifest-guarded, never the main firmware); nothing selects them unless declared. Using it on the M1 would run Windows on 4 of the 8 cores (the P cores stay parked) and needs Marco's decision plus a bundle gate update.

## T16k: WinPE with all RAM above 4 GiB (M1 memory layout) on the bench (2026-11-15, audit item H3)

Result: **Setup's first screen (driver page) is reached with all RAM at 0x8_0000_0000 and nothing below 4 GiB**, on the plain j293 `--bgr8` firmware (`WATARI_M1_EFI-bgr8.fd`, model j293, bench 0, ramdisk_standin 0, no tag, sha256 867ea800...), `-smp 8`, `wel2.vgicipa=m1 wel2.vmpidr=m1`, `trap-notc`, PANEL=8, MEM=3G (RAM 0x800000000..0x8C0000000). Seven `CPU_ON` accepted, no FATAL. [verified: runs below]

New knob: `RAM_BASE=0x800000000 el2/boot/run-winram.sh` uses the patched QEMU (`/private/tmp/watari-qemu/build/qemu-system-aarch64`, `-M virt,...,ram-base=`), defaults `GUEST` (EDK2 load address) to RAM_BASE+0x280000 and `RDBASE` to RAM_BASE+0x42000000, and pads the RAM-disk range to 1 MiB with the trailer last (`RDPAD`). The default (RAM at 0x40000000) is unchanged. The fake panel and the pmemsave address come from the layer's own log line, so they follow the RAM base (panel at 0x8bfc18000). The layer stays where `-kernel` puts it (RAM_BASE+0x80000), not at the loader's 0x870000000 [not exercised: layer location].

| Run | Config | Result |
|---|---|---|
| t16k-1 | `RAM_BASE=0x800000000`, EDK2 at 0x800280000, RAM disk 0x842000000 (exact size 0x2c901000) | CRC verified, registered; kernel on all 8 vCPUs at 71 s, **Setup driver page at 245 s** (`shot-0005.png`). EDK2 ran above 4 GiB: `ArmVirtGetMemoryMap` 0x800200000, drivers loaded at 0x840xxxxxx, GOP buffer 0x8BFD1C000. |
| t16k-2 | hardware plan addresses: EDK2 at **0x874000000**, RAM disk 0x842000000, exact size 0x2c901000 | RAM disk NOT registered, EDK2 self-test, SYSTEM_RESET: our defect, F-38 (64 KiB claim rounding vs. an unpadded range). |
| t16k-3 | same as t16k-2 but the range padded to **0x2ca00000** (the loader's size) | RAM disk registered (CRC E3EB84C7 verified), 7 `CPU_ON`, kernel on all 8 vCPUs at 257 s, **Setup driver page** (`shot-0005.png`). |

So with the loader's address plan (EDK2 0x874000000, RAM disk 0x842000000 size 0x2ca00000) and no RAM below 4 GiB, EDK2 memory init, ACPI/SMBIOS installation (no allocation error), the RAM-disk driver, bootmgr, winload and the kernel all run [verified]. The mechanisms of H3: (1) 32-bit `CPU_ON`: all seven starts were accepted (the function id is still not logged) [hypothesis: Windows uses the 64-bit call]; (2) EDK2 below-4 GiB allocations: none failed; (3) Windows' own below-4 GiB needs: no device here, so not exposed, as on day 4. Not covered: the layer at 0x870000000, the real MMIO map, the 8 GB RAM extent.

Evidence (not in the repo): `$WATARI_DATA/logs/winram/t16k-{1,2,3}/` (serial.log, pc.log, shot-NNNN.png). Run: `GUEST=0x874000000 RAM_BASE=0x800000000 PANEL=8 MEM=3G SMP=8 CPUIF=trap-notc QEMU_EXTRA=-no-shutdown WEL2_ARGS="wel2.vgicipa=m1 wel2.vmpidr=m1" el2/boot/run-winram.sh`.

## T16l: the hardware layout with the post-mortem ring, and the ring read back after a reset (2026-11-15)

Result: **WinPE reaches Setup's driver page with the exact day-4 W2 layout, and the ring read out of RAM after a QEMU `system_reset` holds the whole boot up to Setup** [verified: run t16l-1].

Configuration (the day-4 W2 plan, all from a loader dry run of W2 against the rehearsal bundle `m-day4`: `wel2.ramdisk=0x842000000,0x2ca00000 wel2.pmlog=0x872000000,0x200000`, EDK2 at 0x874000000): plain j293 `--bgr8` firmware (the bundle's `WATARI_M1_EFI.fd`, manifest model j293, bench 0, no tag, commit 87eba25), patched QEMU `RAM_BASE=0x800000000` (all 3 GB above 4 GiB), `-smp 8`, `wel2.vgicipa=m1 wel2.vmpidr=m1`, `trap-notc`, PANEL 8, RAM disk at 0x842000000 padded to 0x2ca00000 (RDPAD), the 2 MiB ring at 0x872000000 (the loader's spacer-block place, below EDK2), `wel2.pmlog=0x872000000,0x200000` passed to the layer. Run: `FD=<bundle>/WATARI_M1_EFI.fd TAG=t16l-1 GUEST=0x874000000 RAM_BASE=0x800000000 PANEL=8 MEM=3G SMP=8 CPUIF=trap-notc TIMEOUT=330 QEMU_EXTRA=-no-shutdown PMLOG_READ=0x872000000,0x200000 WEL2_ARGS="wel2.vgicipa=m1 wel2.vmpidr=m1 wel2.pmlog=0x872000000,0x200000" el2/boot/run-winram.sh`.

- RAM disk: `WatariM1RamDisk: CRC-32 E3EB84C7 of 2C900000 bytes verified`, registered at 842000000; 7 `CPU_ON` accepted; Setup's driver page at the 224 s shot and again at the end (`shot-0004.png`, layer band `LAST ERROR: NONE`); no FATAL.
- The layer's guest FDT: `/memory` pieces at 0x800200000 and **0x872200000** (the ring cut out, 2 MB aligned after it, as the 2048 KiB default intends); `/reserved-memory/pmlog@872000000`.
- `run-winram.sh` new knob `PMLOG_READ=0x<base>,0x<size>` (needs `QEMU_EXTRA=-no-shutdown`): after the last shot the monitor sends `system_reset` (with `-no-reboot -no-shutdown` QEMU stops with its RAM intact, the guest does not run again), `pmemsave` of the ring to a relative path, `tools/m3a/pmlog_read.py` on it. Result: `status ok, capacity 2097088, boots 1, total 220507 bytes, crc ok`, 3667 lines, **7 `PSCI CPU_ON cpu n accepted`**, 0 FATAL, the last lines are the layer's stats block (ICC counters), i.e. the ring has everything from `post-mortem log ring at 0x872000000` to the last stats line before the reset.
- **RAM-disk reuse on the bench: not feasible as a real test.** QEMU's `-device loader` ROMs are written back into RAM at every machine reset (`rom_reset`), so after a `system_reset` the image is restored by QEMU itself, which would make a reuse test vacuous; and a reset without `-no-reboot` also re-writes the ring's neighbours. What is covered instead: the loader's decision logic against a fake proxy whose memory survives from one attempt to the next (`tools/m3a/test_reuse.py`: match, no record, other address, trailer magic/size/CRC/SHA mismatch, damaged samples, another image, wiped DRAM), and that EDK2's CRC-32 check runs and passes on every boot above (the guard that stays authoritative). Whether the real M1's DRAM keeps 713 MiB across the reset is Monday's PM0 measurement [hypothesis].
- Not covered: the layer at 0x870000000 (the bench layer is at 0x800080000 from `-kernel`), a real reset (guest cores, cache state), the m1n1 heap giving the same block address on a fresh boot (the loader compares it with the record and refuses reuse otherwise).

Evidence (not in the repo): `$WATARI_DATA/logs/winram/t16l-1/` (serial.log, pc.log, shot-NNNN.png, pmlog.txt, pmlog.bin).
