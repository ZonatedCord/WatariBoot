# Watari EL2 - M0-C boot skeleton

First bootable skeleton of the Watari EL2 layer in QEMU TCG (no hardware, no downloads).

What it does (all **verified** by `run-qemu.sh`):
1. Enters at EL2 (`CurrentEL` printed), sets the stack and `VBAR_EL2`, prints via the PL011 at `0x09000000`.
2. Sets `HCR_EL2 = RW | IMO | FMO | AMO`, `SPSR_EL2` = EL1h with DAIF masked, `ELR_EL2`, and `eret`s to a tiny EL1 payload linked in the same image.
3. The EL1 payload prints, issues `HVC #0` with `PSCI_VERSION` (gets 1.1) and then `SYSTEM_OFF`.
4. The EL2 sync handler (lower EL, AArch64) recognizes `ESR_EL2.EC = 0x16`, implements `PSCI_VERSION` and `SYSTEM_OFF` (forwarded with `smc #0` to QEMU's PSCI: with `virtualization=on` the conduit is SMC), and answers anything else with `NOT_SUPPORTED`.
5. Any other exception prints `ESR/ELR/FAR/SPSR` and halts (never a silent hang, lesson F-14).

Not yet (later milestones): `E2H=1`/VHE, `TSC=1`, `TID3`, vGIC-to-physical-GIC effects, SMP.

## M1-1: stage-2 + virtual GIC (issue #50, first step)
- `HCR_EL2.VM=1`; identity stage-2 map, **4 KB granule** (simple; the real M1 is 16 KB only, so hardware needs different tables, same logic), 32-bit IPA, start level 1. RAM `0x40000000-0x7fffffff` Normal WB; the rest Device-nGnRnE (execute-never).
- The physical GICD (`0x08000000`, 64 KB) and the first 8 GICR frames (`0x080A0000`, 8 x 128 KB) are **unmapped**: guest accesses take a stage-2 translation fault to EL2.
- EL2 data-abort handler (`EC=0x24`): decodes ISV/SAS/SSE/SRT/SF/WnR, rebuilds the IPA from `HPFAR_EL2`+`FAR_EL2`, calls `vgic_mmio_read/write` (`../vgic/vgic.c` is linked into the image), writes the register back for loads and advances `ELR_EL2` by 4. ISV=0, non-translation faults or addresses outside the GIC windows print ESR/FAR/HPFAR/IPA and halt (F-14).
- The physical GIC is left untouched in this step; the vGIC `effect` hook only logs.
- EL1 payload reads GICD_CTLR/TYPER (`0x0148001d`)/IIDR, GICR_TYPER (Last bit set), does the GICR_WAKER handshake, enables SPI 33 and sets its priority, then PSCI `SYSTEM_OFF`.
- Build needed `-mstrict-align` (F-18); since #53 only the MMU-off code keeps it (see Hardening).
- Run: `./run-m1.sh` (M1-1 checks) and `./run-qemu.sh` (M0 checks); both pass on the same image.

## Files
- `start.S`: arm64 Image header, reset path, `enter_el1`, vector table + context save/restore.
- `main.c`: exception handler, stage-2, data-abort/vGIC glue, boot arguments, `el2_main`.
- `util.c` (#53): UART, big/UART locks, format helpers, `memcpy`/`memset`, `dcache_clean_range`; built with `-mstrict-align` (the EL1 test payload calls it with its MMU off).
- `stage1.c` (#53): EL2 stage-1 identity map (tables built MMU-off); `s1_enable` in `start.S` switches MMU + caches on, on every core.
- `../vgic/vcif.c`, `../include/wel2_vcif.h` (#54/#52): virtual CPU interface model (LR fill/evict/harvest, ICC_* emulation), host-tested in `../tests/test_vcif.c`.
- `run-m1-5.sh` (#54): LR-overflow test; `run-all.sh`: every test in both modes.
- `boot.h`: shared declarations (frame layout, GIC addresses, module APIs).
- `gic_phys.c` (M1-2): EL2 owns the physical GICv3: `ICC_SRE_EL2` (SRE+Enable), GICD ARE+Grp1, redistributor wake, PPI 25 (maintenance) and PPI 27 (CNTV) as Group 1 level, priority 0x40, `ICC_PMR_EL1`=0xFF, `ICC_IGRPEN1_EL1`=1.
- `cpuif.c` (M1-2): virtual CPU interface. `ICH_HCR_EL2.En=1`, LR fill from `vgic_next_pending()`/`vgic_ack()`, harvest on guest EOI (LR invalid, `vgic_eoi()`), virtual timer line, ICC_SGI*R trap decoder.
- `payload.c`/`payload.S`: the EL1 test payload (M0 PSCI, M1-1 MMIO view, M1-2 OS-like GIC bring-up + timer + SGI) and its EL1 vector table.
- `linker.ld`: image at `0x40080000` (RAM base + `text_offset` 0x80000), stacks after the image.
- `Makefile`: Homebrew LLVM (`clang --target=aarch64-none-elf`, `ld.lld`, `llvm-objcopy`).
- `run-m1.sh`: same harness with the M1-1 expectations (vGIC register values).
- `run-m1-2.sh`: M1-2 expectations (`timer irq 27 x5`, SGI trap, `sgi 1 received`, five INTID 27 lines).
- `psci.c` (M1-3): guest PSCI over HVC (`CPU_ON`, `CPU_OFF`, `AFFINITY_INFO`, `PSCI_FEATURES`, `MIGRATE_INFO_TYPE`, `PSCI_VERSION`, `SYSTEM_OFF`), firmware conduit = SMC to QEMU.
- `run-m1-3.sh`: M1-3 expectations on `-smp 4` (60 s timeout).
- `run-qemu.sh`: builds, boots headless (20 s timeout) and checks the UART output; exit 0 = pass.

## Run
    ./run-qemu.sh

QEMU: `-M virt,virtualization=on,gic-version=3 -cpu max -accel tcg -nographic -m 512M -kernel build/wel2.bin`.

## M1-2: virtual interrupts (QEMU TCG, verified by `run-m1-2.sh`)
Mode: **hardware list registers** (`ICH_HCR_EL2` = En only, TALL0/TALL1/TC = 0). With `HCR_EL2.IMO=1` the guest's `ICC_IAR1/EOIR1/PMR/IGRPEN1/BPR1` at EL1 are the virtual `ICV_*` registers and run without traps. Trapped by EL2 (EC 0x18, ISS decoded in `cpuif_sysreg_trap`): only `ICC_SGI1R_EL1` / `ICC_ASGI1R_EL1` / `ICC_SGI0R_EL1` writes, which `IMO=1` traps by itself (GIC spec 4.6.4 p.4-67). Nothing else needs TRAP_EMULATE in QEMU. QEMU reports `ICH_VTR_EL2.SEIS=0`, so the M1 SEIS erratum (RISKS R-01) cannot be exercised here: on M1 `TALL1`/`TC` must be set and `cpuif.c` extended with the ICC emulation path.
- Interrupt life cycle: physical IRQ at EL2 (vector 9) -> `cpuif_flush()`: `vgic_next_pending()` + `vgic_ack()` (model marks it active; the LR owns pending/active) -> LR written Pending, Group 1, EOI=1 (HW=0) -> guest ack/EOI in hardware -> LR invalid + maintenance interrupt (PPI 25) -> harvest: `vgic_eoi()`, LR freed, level lines re-pend if still high.
- Virtual timer: guest programs CNTV directly (`CNTVOFF_EL2`=0, `CNTHCTL_EL2`=3). The physical PPI 27 is level: on arrival EL2 masks it at the redistributor, EOIs it, and the model line follows `CNTV_CTL_EL0` (ENABLE && ISTATUS && !IMASK); when the guest has silenced the timer, EL2 unmasks PPI 27 again. Group is hardcoded to Group 1 in the LRs (Windows uses Group 1 only).
- Not done in M1-2 (SPI forwarding and SMP came in M1-3): LR overflow (`UIE` underflow interrupt), `HW=1` LRs for physical-interrupt passthrough, EL2 stage-1 MMU.

## M1-3: SMP and peripheral interrupts (QEMU TCG `-smp 4`, verified by `run-m1-3.sh`)
`./run-m1-3.sh` boots `-M virt,virtualization=on,gic-version=3 -cpu max -accel tcg -smp 4`; it also passes with `-accel tcg,thread=single`. `run-qemu.sh`, `run-m1.sh`, `run-m1-2.sh` (all `-smp 1`) still pass on the same image, and `make -C el2 test` has 5 suites (the new one is `test_vgic_smp.c`).

**Cores and vCPUs.** The number of cores is discovered by walking the physical `GICR_TYPER` frames (stride 0x20000) until the Last bit (`gic_phys_discover`); vCPU i = core i, affinities taken from `GICR_TYPER[63:32]`, so the vGIC has exactly as many redistributors as the machine has cores, and `GICR_TYPER.Last` is set only on the last one (host-tested). `TPIDR_EL2` holds the core index. `VPIDR_EL2`/`VMPIDR_EL2` are set from the physical MIDR/MPIDR on every core, so an MPIDR means the same to the guest and to PSCI.

**PSCI (`psci.c`, PSCI DEN0022F.b).** Both SMC32 and SMC64 function ids for CPU_ON (5.6) and AFFINITY_INFO (5.7). `CPU_ON`: target must be a valid MPIDR (bits outside Aff3:Aff2:Aff1:Aff0 clear, else `INVALID_PARAMETERS`; unknown core the same), entry 4-byte aligned inside guest RAM (else `INVALID_ADDRESS`), `ALREADY_ON` / `ON_PENDING` by core state; then SMC `CPU_ON` to the firmware with `secondary_entry` as the EL2 entry point. The secondary (`start.S` -> `el2_secondary_main`) sets its stack (slot = Aff0), `VBAR_EL2`, the shared stage 2 (same VTCR/VTTBR), `VPIDR/VMPIDR`, its own physical redistributor + CPU interface and `ICH_HCR_EL2`, marks itself ON and `eret`s to EL1h with all DAIF masked, MMU off and `x0` = context_id (6.4.3). `CPU_OFF` (5.5): the core resets its vCPU in the vGIC (redistributor asleep again, private IRQs dropped), clears its LRs, puts the physical redistributor to sleep, publishes OFF, releases the big lock and calls the firmware `CPU_OFF`; `DENIED` on the last ON core. `AFFINITY_INFO`: ON 0 / OFF 1 / ON_PENDING 2, level != 0 -> `INVALID_PARAMETERS` (allowed since PSCI 1.0). `PSCI_FEATURES`: 0 for what is implemented, `NOT_SUPPORTED` otherwise. Everything else (CPU_SUSPEND, SYSTEM_RESET, ...) returns `NOT_SUPPORTED`; EL2 never hangs silently (F-14).

**Locking.** All EL2 shared state (vGIC model, per-core CPU-interface state, core states) is under one big lock taken in `el2_exception`; UART output has its own lock so lines do not interleave (`puts_`, `put_kv`, `put_kn` write one line atomically). Spins are bounded and report. See F-20 for the caveat on real hardware (exclusives on Device memory).

**SGIs between vCPUs.** The guest's `ICC_SGI1R_EL1` write traps (`IMO=1`); `vgic_sgi_send_mask()` marks the SGI pending on every target vCPU and returns the target mask (target list, Aff1..3, IRM tested in `test_vgic_smp.c` / `test_vgic_sgi.c`). After any change of shared state, `cpuif_kick_others()` sends the **physical SGI 15** (`SGI_KICK`, reserved for EL2: guest SGIs are virtual and never reach the physical distributor, so INTID 15 of the physical GIC is free) to each other ON core that has a deliverable interrupt and a free LR. The target takes the IRQ at EL2 (also from WFI at EL1), runs `cpuif_flush()` and the interrupt lands in its list registers.

**SPIs.** The vGIC effect hook is wired to the physical distributor (`cpuif_vgic_effect`): `SET_TARGET` writes the physical `GICD_IROUTER` with the target core's affinity; `UNMASK_SPI` sets Group 1, priority 0x40 and enables it; `MASK_SPI` disables it. A forwarded SPI arrives on its core at EL2: EL2 masks it at the GICD, EOIs it and raises the model line; when the guest EOIs the injected vIRQ (harvest) the line is dropped, `vgic_eoi()` runs and the SPI is unmasked, so a still-asserted level interrupt fires again and a silenced one does not. SPIs are assumed level-sensitive at the physical GIC (as all QEMU virt device SPIs are). The test uses the **PL031 RTC alarm (SPI 2 = INTID 34)**: the guest programs `RTCMR`/`RTCIMSC` directly (the RTC is mapped device memory in stage 2), routes INTID 34 to the last core with a 64-bit `GICD_IROUTER` write and enables it; the handler clears `RTCICR`. No physical `GICD_ISPENDR` fallback was needed.

**Expected output (trimmed; boot core and one secondary shown).**

    Watari EL2: cores (redistributors) = 0x0000000000000004
    Watari EL1: GICR_TYPER = 0x0000000000000000          (Last clear on frame 0)
    Watari EL1: psci contract ok
    Watari EL2: PSCI CPU_ON cpu 1 accepted
    Watari EL2: cpu 1 up at EL2, eret to the guest entry point
    Watari EL1: cpu 1 online
    Watari EL1: cpu 1 timer irq 27 x3
    Watari EL2: ICC_SGI1R_EL1 write trapped, value = 0x0000000002000002
    Watari EL1: cpu 1 sgi 2
    Watari EL1: RTC alarm armed, SPI 34 routed to cpu 0x0000000000000003
    Watari EL1: cpu 3 spi 34 received
    Watari EL1: cpu 1 going off (CPU_OFF)
    Watari EL1: cpu 1 is OFF (AFFINITY_INFO = 1)
    Watari EL1: cpu 1 online again after CPU_OFF           (CPU_ON after CPU_OFF works)
    Watari EL2: PSCI SYSTEM_OFF -> forwarding to QEMU firmware (SMC)

**Not done / open (M1-3).** EL2 stage-1 MMU (needed for the locks on hardware, F-18/F-20); LR overflow (`UIE`) and a real fix for the kick-storm guard; secondary stack slot from Aff0 only (QEMU numbering); edge-triggered physical SPIs and `GICD_ICFGR` forwarding; SPI priority fixed at 0x40 at EL2 (the guest's priority only orders vIRQs); `HW=1` LRs; the TRAP_EMULATE path for the M1 SEIS erratum (F-19) is still untested; CPU_SUSPEND / SYSTEM_RESET.

## M1-4: EDK2 at EL1 on top of Watari EL2 (QEMU TCG `-smp 4 -m 2G`, verified by `run-m1-4.sh`)
`./run-m1-4.sh` builds the chain image (`make chain` -> `build-chain/wel2.bin`: `-DWEL2_CHAIN`, no EL1 test payload, `HCR_EL2.TSC=1`) and boots it together with `ArmVirtQemuKernel` (`tools/build-edk2-kernel.sh`, run once; output `$WATARI_DATA/build-kernel/QEMU_EFI-kernel.fd`):

    qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -accel tcg -m 2G -smp 4 -nic none \
        -kernel build-chain/wel2.bin -device loader,file=QEMU_EFI-kernel.fd,addr=0x40280000,force-raw=on

Success = EDK2 prints `BdsDxe: Press any key to enter the Boot Manager Menu` (about 5 s with `-nic none`; `WITH_NIC=1` keeps the virtio NIC and adds the PXE attempt `failed to load Boot0002`, about 60 s) and the layer log shows vGIC traps handled and timer interrupts injected. `run-qemu.sh`, `run-m1.sh`, `run-m1-2.sh`, `run-m1-3.sh` and `make -C el2 test` still pass (the default `make` image keeps the payload). `ArmVirtQemuKernel` booted alone with plain QEMU (`-kernel QEMU_EFI-kernel.fd`, no layer) also reaches the Boot Manager (checked first).

**Chain, step by step.** (1) QEMU loads the layer as an arm64 `Image` at `0x40080000`, the FDT at `0x48000000`, x0 = FDT, and enters at EL2. (2) The layer does its normal init (stage 2, vGIC, physical GIC, list registers). (3) `chain_to_edk2()` patches the FDT in place (`fdt.c`), copies it to `0x40000000`, wakes the boot vCPU in the vGIC and ERETs to the EDK2 image at `0x40280000`. (4) EDK2's header trampoline (`adr x1, .; b 0x8000`) lets the relocatable PrePi fix itself up at that address; the rest is a normal ArmVirtQemu boot, except that every GIC register access is a stage-2 fault handled by the vGIC and every PSCI call is an HVC.

**Boot protocol handed to EDK2 (Linux arm64 booting, entry state).** Image 4 KB aligned at `0x40280000` (the 2 MB after the layer's carve-out + the image's `text_offset` 0x80000); `x0` = physical address of the FDT (`0x40000000`), `x1 = x2 = x3 = 0` (`enter_el1` zeroes them); MMU off, D-cache off (`SCTLR_EL1` = RES1 only); PSTATE EL1h, DAIF masked. Differences from the plain protocol: entered at EL1 because the layer owns EL2; the FDT is patched; and it sits at the RAM base because of PrePi's early debug output (F-21).

**FDT patch (`fdt.c`, no libfdt, all in place).** `/memory/reg`: first 2 MB removed (base `0x40200000`, size `0x7fe00000`; the carve-out holds the FDT buffer `[0x40000000,0x40080000)` and the layer `[0x40080000,0x40200000)`); `/psci/method` `"smc"` -> `"hvc"`; `/timer/interrupts` is only read and logged.

**PSCI conduit: decision = patch the FDT to `hvc`** (INTERFACES 3.2/9). The guest then calls the layer directly (HVC traps to EL2 with `HCR_EL2.HCD=0`), nothing has to be emulated, and it is the conduit Windows will be told through the FADT. `HCR_EL2.TSC=1` stays on as a backstop: with `TSC=0` a stray SMC would go straight to QEMU's PSCI (with `virtualization=on` it is SMC), start a core at EL2 without layer init and break the machine silently. A trapped SMC gets `NOT_SUPPORTED`, `ELR` advanced (the preferred return address of a trapped SMC is the SMC itself) and a log line. `PSCI_SYSTEM_RESET` is forwarded to QEMU like `SYSTEM_OFF`.

**Timer: EDK2 uses the EL1 virtual timer, INTID 27 (PPI 11).** `ArmVirtPkg/ArmVirt.dsc.inc` selects `ArmGenericTimerVirtCounterLib`, so `TimerDxe` programs `CNTV_CVAL/CTL_EL0`; it registers handlers for all four timer PPIs but only 27 fires. That is the path the layer already delivers since M1-2 (physical PPI 27 -> mask -> list-register injection; `CNTHCTL_EL2` = 3 keeps EL1 access to counters and timers), so no change was needed in the timer code. A guest using the EL1 physical timer (PPI 30) would need PPI 30 enabled and forwarded like 27.

**Stage 2 changes.** IPA space 40 bits (T0SZ=24, start level 0): EDK2 touches the PCIe high ECAM (`0x4010000000`). RAM `0x80000000-0xbfffffff` is Normal (a `-m 2G` machine). In the chain image the layer's own pages (`__image_start` to the end of the 2 MB carve-out) are unmapped: a guest access takes a translation fault and EL2 prints ESR/FAR/IPA and x0-x30 (it never happened in the runs).

**Observed (excerpt of the last `run-m1-4.sh`).**

    Watari EL2: FDT from QEMU at 0x0000000048000000
    Watari EL2: FDT /memory base now 0x0000000040200000
    Watari EL2: FDT /psci method "smc" patched to "hvc"
    Watari EL2: eret to EDK2 at EL1, entry = 0x0000000040280000
    Found GIC v3 Distributor @ 0x8000000, Len 0x10000
    Watari EL2: vGIC GICD traps handled = 0x0000000000000064
    Watari EL2: cpu 0 virtual timer PPI 27 taken, injecting into the guest
    [Bds]BdsWait(1)..Zzzz...
    [Bds]Exit the waiting!
    BdsDxe: No bootable option or device was found.
    BdsDxe: Press any key to enter the Boot Manager Menu.

**Not done / open (M1-4).** EDK2 never calls `CPU_ON` in this firmware, so SMP under EDK2 is untested (the secondary-core wake-up is in the chain build but never ran); the UEFI Shell is not reached automatically (the firmware waits for a key at the Boot Manager prompt); PPI 27 stays masked if the model ever cannot deliver it (F-21); PCI devices with 64-bit BARs (high MMIO window) were not exercised.


## Hardening before M2 (#54, #53, #52; QEMU TCG, all verified by `./run-all.sh`, both modes)
`./run-all.sh` runs the host tests and `run-qemu`, `run-m1`, `run-m1-2`, `run-m1-3`, `run-m1-5` (plus its `noeoi` variant) and `run-m1-4` with `CPUIF=hw` and `CPUIF=trap` (`SKIP_M14=1` skips the EDK2 chain test, which needs the data disk). Dead ends and limits: `docs/FAILURES.md` F-22.

**Mode switch (#52).** The virtual CPU interface runs in `HW` mode (list registers only, no ICC traps; the default) or `TRAP_EMULATE`. Chosen at boot: build default `-DWEL2_CPUIF_TRAP_DEFAULT=1` (e.g. `make XFLAGS=-DWEL2_CPUIF_TRAP_DEFAULT=1`), overridden at run time by `/chosen/bootargs`: QEMU `-append "wel2.cpuif=trap"` (or `hw`). The test scripts take `CPUIF=trap`. A second boot argument, `wel2.test=noeoi`, is a test knob (SGI list registers without the EOI maintenance bit, so only the underflow interrupt refills them).

**LR overflow (#54).** `cpuif_flush()` = hardware LRs into the model (`vcif_*`), harvest, fill, write back what changed. More pending interrupts than LRs wait in the vGIC model and enter the LRs in priority order as they free up: by the EOI maintenance interrupt each LR carries, and by the underflow interrupt (`ICH_HCR_EL2.UIE`, armed only while interrupts overflow and at least two LRs are valid). `NPIE` is deliberately not used (F-22). A pending-only LR is evicted (`vgic_requeue`) when a strictly higher priority interrupt arrives and every LR is taken. `run-m1-5.sh`: with IRQs masked the EL1 payload sends 8 SGIs to itself (QEMU has 4 LRs; priorities shuffled against INTID order) and checks that all 8 arrive exactly once in priority order; then 4 low-priority SGIs plus one much better SGI: the better one must come first. Counters printed at SYSTEM_OFF: injections, evictions, maintenance and UIE interrupts. `NOEOI=1 ./run-m1-5.sh` requires UIE to have fired (3 times in the last run); in `trap` mode UIE is moot (EOIR traps, EL2 refills at once).

**EL2 stage 1 (#53).** Identity map, 4 KB granule, 39-bit VA (`T0SZ` 25, start level 1): `0-1 GB` Device-nGnRnE execute-never (2 MB blocks), RAM Normal WB Inner-Shareable (`MAIR_EL2` Attr1 = 0xff, `TCR_EL2` IRGN0/ORGN0 = WB WA, SH0 = ISH). In the first 2 MB of RAM (4 KB pages, where the layer lives): code read-only + executable, rodata read-only XN, FDT buffer / data / bss / stacks RW XN; the linker script page-aligns the sections and asserts that the layer fits the first 2 MB. `stage1_build()` (C, `-mstrict-align`, MMU off) runs on the boot core; `s1_enable` (assembly, registers only) sets MAIR/TCR/TTBR0, invalidates TLB + I-cache and sets `SCTLR_EL2.M/C/I` (A, WXN, EE cleared) on the boot core and, before any C, on every secondary; each checks the bits afterwards. **E2H: the layer runs with `HCR_EL2.E2H = 0`** and uses the matching E2H0 layout of `TCR_EL2` (PS[18:16], RES1 bits 31 and 23; Arm ARM D24.2.194); the E2H = 1 layout (needed on the real M1) is coded but not executed [hypothesis]. Consequences: the spin locks are exclusives on Normal Inner-Shareable memory (F-20); `-mstrict-align` is gone for EL2 code (`vgic.c` now contains the unaligned stores of F-18 and runs); it stays for `stage1.c`, `util.c`, `payload.c` because they run with the MMU off. The patched FDT is cleaned from the data cache before the guest starts with caches off. Not done: mapping only the RAM that exists (the map covers 0x40000000-0xbfffffff, fine for QEMU `virt`), cache maintenance for the page tables on real hardware, 16 KB granule.

**TRAP_EMULATE (#52).** `ICH_HCR_EL2` = `En | TALL0 | TALL1 | TC | TDIR` (`TDIR` only if `ICH_VTR_EL2.TDS`), i.e. variant A of INTERFACES 7.2. Every guest access to `ICC_IAR1/EOIR1/HPPIR1/BPR1/CTLR/IGRPEN0/1/DIR/RPR/PMR/AP0Rn/AP1Rn/IAR0/EOIR0/HPPIR0/BPR0` traps (EC 0x18); `cpuif_sysreg_trap()` decodes the ISS, loads LR + VMCR + AP into the model, `vcif_*` does what the `ICV_*` hardware would (signalling needs priority < PMR, Group 1 enabled, group priority < running priority; IAR: LR pending -> active + AP bit; EOIR: priority drop, deactivate if EOImode 0; PMR/BPR/IGRPEN/EOImode live in `ICH_VMCR_EL2` so the hardware's own vIRQ signalling follows), writes back what changed and advances `ELR_EL2`. The guest still gets its vIRQ from the hardware (pending LR + VMCR); only the registers it reads are emulated. `ICC_SGI*R_EL1` traps in both modes. QEMU has `SEIS = 0` (F-19): this tests the logic, not the M1 erratum. `ICC_PMR_EL1` writes are counted: **1 write, 998 IAR1/EOIR1 pairs during the whole M1-4 EDK2 boot** (single core; printed every 100th timer tick when something changed, and at SYSTEM_OFF; `run-m1-4.sh` prints the last report) - input for #43; the Windows figure is the M2 measurement.

## M2 run log: Windows 11 ARM64 on top of Watari EL2 (`run-m2.sh`)
`./run-m2.sh` boots the installed VM disk (`$WATARI_DATA/vm/win11.qcow2`, always `snapshot=on`, refuses to start if another QEMU already uses it) through the same EDK2 (`ArmVirtQemuKernel`, `-device loader,...,addr=0x40280000`) and the same layer image as `run-m1-4.sh`: `-M virt,virtualization=on,gic-version=3,its=on -cpu max -accel tcg -m 4G -smp 4`, ramfb, qemu-xhci + usb-kbd/tablet, NVMe (`bootindex=0`), no NIC. It is not a pass/fail test: it runs for `TIMEOUT` seconds (default 3600), takes a screenshot every `SHOT_EVERY` seconds and prints the layer statistics at the end. Everything goes to `$WATARI_DATA/logs/m2/<TAG>/` (`serial.log` = layer + EDK2 + Windows KD on COM1, `shot-NNNN.png`, `qemu.out`).
- Knobs: `CPUIF=trap` (TRAP_EMULATE), `SMP`, `MEM`, `TIMEOUT`, `SHOT_EVERY`, `TAG`, `ITS=on|off`, `STOP_ON_RESET=1` (stop at the first PSCI `SYSTEM_RESET`, i.e. at a bugcheck restart; use `SHOT_EVERY=6` to catch the blue screen), `TRACE=<n>` (boot argument `wel2.trace=<n>`).
- The monitor and the COM1 socket live in a short temp dir (path in `sockdir.txt`): `echo "sendkey shift" | nc -U <dir>/mon.sock` then `screendump <space-free path>` (the data disk path has a space, the dump silently fails there, F-23). Sending keys to COM1 reaches the EDK2 shell.
- Windows finds its boot loader through the NVMe fallback boot option; nothing to do in the EDK2 menus.
- Layer changes for M2: PSCI accepted over SMC as well as HVC (the FADT says SMC), LPI/ITS pass-through (`wel2.lpi=off` hides it), RAM maps up to 5 GB (stage 1 and 2) and `CPU_ON` entries accepted up to `0x140000000`, `wel2.trace` diagnostics (GICD/GICR accesses made from kernel addresses, SPI/LPI arrivals, guest IAR/EOIR of INTIDs >= 32 in trap mode, PSCI calls). All in F-23.
- Trap-mode PMR measurement (#43): the layer prints `trapped ICC_PMR_EL1 writes so far` every 100th timer tick of core 0, now together with `CNTVCT`/`CNTFRQ` so that the rate per second can be computed from two consecutive reports: `grep -a "CNTVCT\|CNTFRQ\|PMR_EL1 writes so far" serial.log`.
- **Soak test (#63, 2026-10-21; F-24).** `run-m2.sh` now wakes the display with `sendkey ctrl` (not Shift: no Sticky Keys) and `m2-login.sh <sockdir> login|keys|shot` logs in as the throwaway test account (`watari`) and sends keys / takes screenshots through the monitor. Protocol: boot (about 5 min to the lock screen), login (about 3 min of first-logon profile creation), then 32 min at the desktop with a screenshot every 150 s, Start menu (`meta_l`) after about 8 min and Explorer (`meta_l-e`) after about 18 min. Logs: `$WATARI_DATA/logs/m2/soak-trap/` and `soak-hw/` (`serial.log`, `soak-NN.png`, `act-startmenu.png`, `act-explorer.png`, `desktop-*.png`, `soak.txt`).
  - **`CPUIF=trap`:** desktop reached, 32 min soak plus the time before it (about 35 min at the desktop in total), Start menu and Explorer open and respond, no UNEXPECTED/FATAL/unhandled/stray line, no LPI drop, `trapped ICC_PMR_EL1 writes` stays at 9 for the whole run (Windows does not use PMR at runtime, so PMR rate = 0/s). Trapped IAR1 = EOIR1 (2,336,354 vs 2,336,354 at the end, they differ by at most the in-flight interrupt). Rates per guest second: **settled idle (guest t 2150-2600 s) about 115-165 IAR/s = EOIR/s, about 30-40 timer irqs/s on cpu 0**; during the first 20 minutes after the first logon (background work after profile creation) 500-1300/s; Explorer opening (guest 1776-1844 s) 925/s, against 287/s in the quiet window after it; Start menu not distinguishable from the surrounding settling (520-570/s). At about 1000 trapped IAR+EOIR pairs per second the cost of trapping is visible but the guest stays responsive (TCG).
  - **`CPUIF=hw`:** desktop reached, same 32 min soak, Start menu and Explorer responsive, no UNEXPECTED/FATAL/unhandled/stray/drop line. The ICC counters do not exist in this mode (no trap), so there are no rates to report.
  - Screenshot of the desktop in trap mode: `docs/img/m2-desktop-trap.png`.
