# Watari EL2 - M0-C boot skeleton

First bootable skeleton of the Watari EL2 layer in QEMU TCG (no hardware, no downloads).

What it does (all **verified** by `run-qemu.sh`):
1. Enters at EL2 (`CurrentEL` printed), sets the stack and `VBAR_EL2`, prints via the PL011 at `0x09000000`.
2. Sets `HCR_EL2 = RW | IMO | FMO | AMO`, `SPSR_EL2` = EL1h with DAIF masked, `ELR_EL2`, and `eret`s to a tiny EL1 payload linked in the same image.
3. The EL1 payload prints, issues `HVC #0` with `PSCI_VERSION` (gets 1.1) and then `SYSTEM_OFF`.
4. The EL2 sync handler (lower EL, AArch64) recognizes `ESR_EL2.EC = 0x16`, implements `PSCI_VERSION` and `SYSTEM_OFF` (forwarded with `smc #0` to QEMU's PSCI: with `virtualization=on` the conduit is SMC), and answers anything else with `NOT_SUPPORTED`.
5. Any other exception prints `ESR/ELR/FAR/SPSR` and halts (never a silent hang, lesson F-14).

Not yet in this skeleton (later milestones): `E2H=1`/VHE, stage-2 (`VM=1`), `TSC=1`, `TID3`, vGIC, SMP.

## Files
- `start.S`: arm64 Image header, reset path, `enter_el1`, vector table + context save/restore.
- `main.c`: UART, exception handler, PSCI, EL1 payload, `el2_main`.
- `linker.ld`: image at `0x40080000` (RAM base + `text_offset` 0x80000), stacks after the image.
- `Makefile`: Homebrew LLVM (`clang --target=aarch64-none-elf`, `ld.lld`, `llvm-objcopy`).
- `run-qemu.sh`: builds, boots headless (20 s timeout) and checks the UART output; exit 0 = pass.

## Run
    ./run-qemu.sh

QEMU: `-M virt,virtualization=on,gic-version=3 -cpu max -accel tcg -nographic -m 512M -kernel build/wel2.bin`.
