# tools/m3a: m1n1 proxyclient scripts for the real M1 (M3a)

Run from the host Mac with the m1n1 proxyclient on the path and the target in m1n1 proxy mode:
`M1N1DEVICE=$(ls /dev/cu.usbmodem*1) PYTHONPATH=<m1n1>/proxyclient python3 <script> [out.txt]`.
The scripts import the external m1n1 proxyclient (GPL-2.0, not vendored here); run them from a separate m1n1 checkout as described in `docs/procedures/m3a-day1-hardware.md`.
None of them imports `m1n1.setup` (it writes the PMU). Results: `re/m3a-ehw0-measurements.md`.

| Script | What it does | Writes on the target |
|---|---|---|
| `ehw0.py`, `ehw0b.py` | E-HW0 rows 1-14: system registers, ADT nodes, AIC reads (never EVENT) | none |
| `ehw0c.py` | starts the secondaries, MPIDR/MIDR/WHOAMI on all 8 cores | secondary start (volatile) |
| `daif_check.py`, `el1_ctrl.py`, `ping.py` | DAIF per core, EL1 entry control, is m1n1 alive | none |
| `e2h_test.py` | HCR_EL2.E2H write-0 test on cpu7, restores | HCR_EL2 (volatile) |
| `aic_qd5.py` | AIC SW_SET / mask / EVENT behaviour on unused IRQ 891 routed to core 7, restores | AIC registers (volatile) |
| `vgic_test.py`, `vgic_test0.py`, `vgic_masked.py` | vGIC list-register delivery to EL1 (hangs m1n1 afterwards: power cycle) | ICH_* (volatile) |
| `build-m1-payload.sh`, `watari-load.py`, `test_watari_load.py`, `j293-minimal.dts` | day-2 bundle (layer, guest, minimal DTB, loader, `fdt_m1_dump` source, SHA256SUMS), the host loader (rungs L0-L5, `--dry-run`), its fake-proxy test (`python3 -I tools/m3a/test_watari_load.py`); see `README-m1-payload.md` | RAM only: DTB, images, m1n1's FDT copy; the jump rungs leave m1n1 |
| `demo/scimmietta.py` | screen demo of 2026-11-10: dancing monkey in a nappy eating bananas | framebuffer RAM |

## `build-winram-image.sh`: WinPE/Setup disk image for the RAM-disk boot (T22/T23)

Builds a raw disk image from the Windows 11 ARM64 ISO for the "Windows from RAM" path (no storage driver on the M1: EDK2 holds the image in RAM). Host-only, macOS built-ins (`hdiutil`, `diskutil`, `newfs_msdos`, `cp`, `shasum`); the ISO is attached read-only and only copied, never analysed.

- Contents: `efi\`, `boot\`, `bootmgfw.efi`, `bootmgr.efi` and `sources\boot.wim` only (no `install.wim`/`install.esd`, no other `sources\` files, no `setup.exe`/`support\`).
- Layouts: `--gpt` (default; GPT, one FAT32 partition typed as EFI System Partition), `--mbr` (MBR + one FAT32 partition), `--superfloppy` (FAT32 directly on the disk, no partition table).
- Options: `--iso PATH`, `--out PATH` (default `$WATARI_DATA/winram/winram.img`; any path inside the repo is refused), `--force` (overwrite; otherwise an existing output is an error), `--verify-only`.
- Size is computed from the copied files plus slack, rounded up to 1 MiB, and printed. Writes `<out>.sha256` and `<out>.manifest.txt` (relative paths and sizes only); finishes by re-attaching the image read-only, listing it and checking `sources\boot.wim` and `efi\boot\bootaa64.efi`.
- macOS does not auto-mount ESP-typed partitions from `hdiutil attach`; the script attaches with `-nomount` and mounts the partition by hand. All `diskutil` commands target only the `/dev/diskN` of the script's own image (checked against `hdiutil info` and "Disk Image").
- Output images are Microsoft content: they stay under `$WATARI_DATA`, never in the repo.

```
tools/m3a/build-winram-image.sh                                   # winram.img (GPT)
tools/m3a/build-winram-image.sh --superfloppy --out "$WATARI_DATA/winram/winram-sf.img"
```
