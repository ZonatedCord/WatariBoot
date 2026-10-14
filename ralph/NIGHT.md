# Overnight autonomous work (planned tasks)

Every overnight run starts **from scratch**: it reads this file and works on the first task not done.

## Rules
- First read `CLAUDE.md`, `docs/STATUS.md`, `docs/FAILURES.md`, `docs/INSTALLED.md` and this file.
- Branch: **`marco/notte-2026-10-14`** (`git switch marco/notte-2026-10-14 && git pull --rebase`; if it does not exist on the remote, create it from main). Never `main`, never merge, never force-push.
- **Downloads and installations:** only QEMU, acpica, llvm/lld/nasm, EDK2 (already present) and the ISO already downloaded are authorized. **No** new packages, virtio drivers, swtpm or Microsoft binaries. No analysis of Microsoft binaries.
- All data goes in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (use `/tmp/watari-data` via `tools/env.sh`). If the disk is not mounted, write it in PROGRESS and stop.
- Everything new must be recorded in `docs/INSTALLED.md`, so that it is easy to remove.
- Execution: delegate to **Sonnet** subagents (`model: "sonnet"`); you (Opus) verify at least 2 key claims and **look at the screenshots** before declaring a result.
- **Document everything, especially failures**: every failed attempt goes in `docs/FAILURES.md` (F-NN format), every result in `ralph/PROGRESS-night.md`.
- Notifications: **a single PR** for the whole night. The first run that has something to commit opens it (title "Night 2026-10-14: ..."); the following ones only push. No comments or assignments on GitHub.
- QEMU must always be closed at the end of the run, except in case N2 (see below).
- Before finishing: commit + push, update `ralph/PROGRESS-night.md` and the "Night" lines in `docs/STATUS.md`.

## Tasks (in order)
- [x] **N1. HVF.** *(solved shortly after midnight, see F-08)* Diagnosis of the HVF hang (F-08):
  - check `tools/edk2-timer-imask.patch` and the log of any attempts in `WatariBoot-data/logs/`;
  - try with the EDK2 firmware bundled with QEMU (`/opt/homebrew/share/qemu/edk2-aarch64-code.fd`), then with a RELEASE build, then with `gic-version=2`;
  - goal: with HVF reach at least the boot of the Windows CD. If after a serious attempt it is not solved, document it in F-08 and move on (continue with TCG).
- [x] **N2. Automatic Windows installation (no interaction).** *(succeeded 2026-10-14 ~02:35, see PROGRESS-night)*
  - Create `tools/autounattend/autounattend.xml`:
    - language it-IT;
    - GPT partitions on the NVMe disk;
    - bypass of the TPM, Secure Boot and RAM checks via `HKLM\SYSTEM\Setup\LabConfig` in windowsPE;
    - local account `watari` without network (BypassNRO);
    - **`bcdedit /set testsigning on` and kernel debugging over serial** in the `FirstLogonCommands` or in specialize.
  - Put it in a small ISO with `hdiutil makehybrid -iso -joliet` (included in macOS) and attach it as a second USB CD.
  - Launch the installation headless (HVF if N1 succeeded, otherwise TCG: with TCG it can take many hours). The disk `vm/win11.qcow2` is on the external disk.
  - Every ~20 minutes take a `screendump` into `logs/screens/n2-*.png`.
  - If the run is about to end and the installation is still in progress, **leave QEMU running** and write the PID and state in PROGRESS; the next run resumes from there (check the screenshots).
  - **Success:** the Windows desktop or the login screen in the screenshot.
- [x] **N3. Fixes to `tools/acpigen`** from `re/f0-acpi-windows-requirements.md` (discrepancies section):
  - parking version 1 + address;
  - boot CPU first;
  - GICR=1;
  - no placeholder GICD;
  - `qemu-virt` model (read from QEMU's ACPI: `-machine dumpdtb` or the tables dumped from EDK2/UEFI shell `acpiview`);
  - FADT and DBG2 generators.
  Criterion: `make -C tools/acpigen test` green.
- [x] **N4. V1 (preparation).** *(build succeeded; Windows does NOT boot with our tables: F-11)* DXE driver `WatariAcpiOverride` in a package `edk2-apple/WatariQemuPkg` (sources in the repo, built with the EDK2 on the external disk) that replaces MADT and GTDT with those from `acpigen`. Criterion: successful build and, if N2 is done, Windows booted with our tables (screenshot).
