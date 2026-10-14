# What was installed on the Mac (Gate B)

Time Machine backup confirmed by the user before the downloads. State verified on 2026-10-13.

## Homebrew packages added
Requested: `qemu acpica llvm lld nasm`. The others are automatic dependencies.

| Package | Version | Role |
|---|---|---|
| qemu | 11.1.2 | emulator (test bench) |
| acpica | 20260408 | `iasl`, the ASL/ACPI compiler |
| llvm | 23.1.3_1 | clang + llvm-objcopy for the CLANGDWARF toolchain (keg-only, not added to the PATH) |
| lld | 23.1.3 | linker |
| nasm | 3.02 | assembler (not needed for AARCH64, kept for x86 targets) |
| capstone, dtc, libslirp, openssl@4, vde, z3 | 5.0.9, 1.8.1, 4.9.5, 4.0.3, 2.3.3, 5.1.0 | dependencies of qemu/llvm |

Reference list "before": `/Volumes/Arthur Fonzarelli/WatariBoot-data/brew-prima.txt`.
Space (Cellar): qemu 696M, llvm 1.8G, lld 5.8M, nasm 2.9M, acpica 3.3M.

## Toolchain
EDK2 `CLANGDWARF` (documented in `BaseTools/Conf/tools_def.template`: "Linux, Windows, Mac", requires Clang >= 9) with Homebrew's LLVM/lld. Target `-target aarch64-linux-gnu`.

## Downloaded data (external disk only)
Root: `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (total ~1.0G)
- `src/edk2/`: clone of `tianocore/edk2` tag `edk2-stable202608` (commit 2970e56, `--depth 1`, submodules included), 695M before the build.
- `src/edk2/Build/`: output of the ArmVirtQemu build, 355M.
- `logs/`: build and QEMU logs (`b2-uefi.log`, `b2-uefi-tcg.log`, `b2-uefi-hvf.log`, `b2-build.log`).
- `run/`: 64M copies of the firmware used by QEMU (`efi.img`, `vars.img`).
- `vm/` (Gate B, V0): `win11.qcow2` (NVMe disk of 64G virtual, sparse), `win11-efi.img` and `win11-vars.img` (64M pflash each; the variables are recreated by deleting the file). Order of magnitude of the space: qcow2 a few MB until installation, 128M for the two pflash files (sparse).
- `logs/screens/`: screenshots (PNG) of the headless tests; `logs/win-cmd.txt` (QEMU command), `win-qemu.log`, `win-serial.log`.
- `run/qemu-mon.sock`, `run/qemu.pid`: QEMU monitor and pid of the headless test.
- `vm/unattend.iso` (small, ~1 MB): contains `autounattend.xml` (from `tools/make-unattend-iso.sh`); it is attached with `run-qemu-windows.sh --unattend`.
- `iso/`: Windows 11 ARM64 it-it ISO (8.4G) + SHA256.txt.
- `brew-prima.txt`: brew packages present before.

Outside the external disk: only the symbolic link `/tmp/watari-data` -> data folder (created by `tools/env.sh`; it is needed because EDK2 does not accept spaces in paths; it disappears on reboot).

## Scripts
`tools/env.sh`, `tools/build-edk2.sh`, `tools/run-qemu-uefi.sh`, `tools/run-qemu-windows.sh`, `tools/test-qemu-windows.sh`, `tools/uninstall-watari.sh`.

## How to remove everything
With the script (default is dry-run, it deletes nothing):
```
tools/uninstall-watari.sh          # shows what it would do
tools/uninstall-watari.sh --yes    # runs it (asks for confirmation for the data folder)
```
By hand:
```
brew uninstall qemu acpica llvm lld nasm
brew autoremove        # check the proposed list before confirming
rm -f /tmp/watari-data
rm -rf "/Volumes/Arthur Fonzarelli/WatariBoot-data"
```
To compare: `comm -13 <(sort brew-prima.txt) <(brew list --formula | sort)` must return empty.

## Night of 2026-10-14 (N3/N4)
- Repo: `edk2-apple/WatariQemuPkg/`, `tools/build-edk2-watari.sh`, `tools/acpigen/src/{fadt,dbg2,qemu_virt_model}.c`.
- External disk: `WatariBoot-data/vm/QEMU_EFI-watari.fd`, `/tmp/watari-data/build-watari/` (separate EDK2 workspace), `win11.qcow2` (~14 GB, Windows 11 installed). Removal: delete these paths.
