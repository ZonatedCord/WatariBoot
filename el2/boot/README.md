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
- Build needs `-mstrict-align` (F-18).
- Run: `./run-m1.sh` (M1-1 checks) and `./run-qemu.sh` (M0 checks); both pass on the same image.

## Files
- `start.S`: arm64 Image header, reset path, `enter_el1`, vector table + context save/restore.
- `main.c`: UART, exception handler, PSCI, stage-2, data-abort/vGIC glue, `el2_main`.
- `boot.h`: shared declarations (frame layout, GIC addresses, module APIs).
- `gic_phys.c` (M1-2): EL2 owns the physical GICv3: `ICC_SRE_EL2` (SRE+Enable), GICD ARE+Grp1, redistributor wake, PPI 25 (maintenance) and PPI 27 (CNTV) as Group 1 level, priority 0x40, `ICC_PMR_EL1`=0xFF, `ICC_IGRPEN1_EL1`=1.
- `cpuif.c` (M1-2): virtual CPU interface. `ICH_HCR_EL2.En=1`, LR fill from `vgic_next_pending()`/`vgic_ack()`, harvest on guest EOI (LR invalid, `vgic_eoi()`), virtual timer line, ICC_SGI*R trap decoder.
- `payload.c`/`payload.S`: the EL1 test payload (M0 PSCI, M1-1 MMIO view, M1-2 OS-like GIC bring-up + timer + SGI) and its EL1 vector table.
- `linker.ld`: image at `0x40080000` (RAM base + `text_offset` 0x80000), stacks after the image.
- `Makefile`: Homebrew LLVM (`clang --target=aarch64-none-elf`, `ld.lld`, `llvm-objcopy`).
- `run-m1.sh`: same harness with the M1-1 expectations (vGIC register values).
- `run-m1-2.sh`: M1-2 expectations (`timer irq 27 x5`, SGI trap, `sgi 1 received`, five INTID 27 lines).
- `run-qemu.sh`: builds, boots headless (20 s timeout) and checks the UART output; exit 0 = pass.

## Run
    ./run-qemu.sh

QEMU: `-M virt,virtualization=on,gic-version=3 -cpu max -accel tcg -nographic -m 512M -kernel build/wel2.bin`.

## M1-2: virtual interrupts (QEMU TCG, verified by `run-m1-2.sh`)
Mode: **hardware list registers** (`ICH_HCR_EL2` = En only, TALL0/TALL1/TC = 0). With `HCR_EL2.IMO=1` the guest's `ICC_IAR1/EOIR1/PMR/IGRPEN1/BPR1` at EL1 are the virtual `ICV_*` registers and run without traps. Trapped by EL2 (EC 0x18, ISS decoded in `cpuif_sysreg_trap`): only `ICC_SGI1R_EL1` / `ICC_ASGI1R_EL1` / `ICC_SGI0R_EL1` writes, which `IMO=1` traps by itself (GIC spec 4.6.4 p.4-67). Nothing else needs TRAP_EMULATE in QEMU. QEMU reports `ICH_VTR_EL2.SEIS=0`, so the M1 SEIS erratum (RISKS R-01) cannot be exercised here: on M1 `TALL1`/`TC` must be set and `cpuif.c` extended with the ICC emulation path.
- Interrupt life cycle: physical IRQ at EL2 (vector 9) -> `cpuif_flush()`: `vgic_next_pending()` + `vgic_ack()` (model marks it active; the LR owns pending/active) -> LR written Pending, Group 1, EOI=1 (HW=0) -> guest ack/EOI in hardware -> LR invalid + maintenance interrupt (PPI 25) -> harvest: `vgic_eoi()`, LR freed, level lines re-pend if still high.
- Virtual timer: guest programs CNTV directly (`CNTVOFF_EL2`=0, `CNTHCTL_EL2`=3). The physical PPI 27 is level: on arrival EL2 masks it at the redistributor, EOIs it, and the model line follows `CNTV_CTL_EL0` (ENABLE && ISTATUS && !IMASK); when the guest has silenced the timer, EL2 unmasks PPI 27 again. Group is hardcoded to Group 1 in the LRs (Windows uses Group 1 only).
- Not done: SPI forwarding (effects are only logged), SMP, LR overflow (`UIE` underflow interrupt), `HW=1` LRs for physical-interrupt passthrough, EL2 stage-1 MMU.
