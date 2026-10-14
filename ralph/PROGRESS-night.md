# Night 2026-10-14: diary

Every overnight run adds here the date and time, the task, what it did, the verified results and the failures (these also in docs/FAILURES.md).

## 2026-10-14 00:15, main session (before the overnight tasks)
- **N2, preparation completed** (not yet executed):
  - `tools/autounattend/autounattend.xml` + README, `tools/make-unattend-iso.sh`, option `--unattend` in `tools/run-qemu-windows.sh`;
  - ISO `WATARI_DATA/vm/unattend.iso` (900 KB).
  - **Verified by Opus:** `xmllint` OK; 6 components with `processorArchitecture="arm64"`; the ISO exists.
  - Hypothesis: LabConfig/BypassNRO keys (not documented by Microsoft), GVLK of Windows 11 Pro only to choose the edition.
  - Test VM credentials: `watari` / `watari`.
- **N1, in progress:** an agent of the main session was still diagnosing HVF (log `WatariBoot-data/logs/dbg-*.log`, `tools/edk2-timer-imask.patch`). If you do not find a result in this diary, redo N1 from scratch.
- **Next:** launch N2 with `tools/run-qemu-windows.sh --unattend` (HVF if N1 succeeded, otherwise `--tcg`).

## 2026-10-14 ~00:10: N1 SOLVED (main session)
- Cause: `ID_AA64PFR0_EL1.GIC=0` under HVF, so EDK2 chose the GICv2 driver (details in F-08). Patch: `tools/edk2-hvf-gicv3.patch`, applied by `build-edk2.sh`.
- **Verified by Opus:** screenshot `win-hvf-180s.png` = Windows 11 Setup, "Select language settings", with HVF.
- Also fixed `run-qemu-windows.sh` (empty array with `set -u` on bash 3.2).

## N2: automatic Windows 11 installation (HVF + unattend) - started 00:14
- State at 00:23: **in progress**, without questions. Setup's text is "Installing Windows 11", 13% (00:18) and then 22% (00:23). The unattend was read: partitioning and key accepted (**verified** from the screenshots).
- QEMU PID **1173** (file `run/n2-qemu.pid`), headless, HVF, monitor `run/qemu-mon.sock`, serial `logs/n2-serial.log`, command in `logs/n2-cmd.txt`. The disk `vm/win11.qcow2` was already almost empty (384 KB), it was not recreated.
- Screenshots: `logs/screens/n2-boot.png` (00:18, 13%), `logs/screens/n2-0023.png` (00:23, 22%) in `/tmp/watari-data/` (= WatariBoot-data).
- How to check: `echo "screendump /tmp/watari-data/logs/screens/n2-HHMM.ppm" | nc -U /tmp/watari-data/run/qemu-mon.sock -w1`, then `sips -s format png ... --out ...png`.
- Done when: login screen/desktop of user `watari` (password `watari`, VM only) and `C:\watari-ok.txt` created by the FirstLogonCommands (from inside the VM; there is no network). Multiple reboots expected. If it stays on "Press any key" after a reboot, press nothing: it boots from the disk.
- If it breaks: stop with `echo quit | nc -U /tmp/watari-data/run/qemu-mon.sock -w1` and document the screenshot in `docs/FAILURES.md`.

## 2026-10-14 00:25: N2 in progress (verified by Opus)
- The screenshot `logs/screens/n2-0023.png` shows "Installing Windows 11... Completing operation: 22%" with HVF, without any question: the unattend works. QEMU has PID 1173 (in `run/n2-qemu.pid`), is alive and detached from the session.

## 2026-10-14 02:30-02:45: N2 SUCCEEDED (overnight run 1)
- Resumption: QEMU PID 1173 alive after 2h15 of HVF. The screenshot `n2-0230.png` was **completely black**, the disk had not been written for minutes and the serial had been still since 01:29: it looked like a hang.
- It was instead Windows' **screen turned off for inactivity** (no real display driver, ramfb). A `sendkey shift` from the monitor woke it up: `n2-0236.png` (copy `n2-SUCCESSO-desktop.png`) shows the **Windows 11 desktop with Start open and user `watari`**, VM time 00:34, 14/10/2026. **Verified by Opus by looking at the screenshot.**
- Not verified: the existence of `C:\watari-ok.txt`, testsigning and serial debugging (there is no access to the VM). The serial file `n2-serial.log` contains only the EDK2 log (no kernel debugger output, hypothesis: verification needed in V1/N4).
- Shut down with `system_powerdown`: clean shutdown, QEMU terminated. `vm/win11.qcow2` ≈ 14 GB = Windows installed and ready to reboot.
- Total installation duration with HVF: about 1h30-2h (from 00:14 to ~01:30 desktop ready).
- **N3 delegated to a Sonnet agent** (acpigen), in progress.

## 2026-10-14 ~02:55: N3 DONE (Sonnet agent, verified by Opus)
- `make -C tools/acpigen clean && make test` re-run by Opus: **ALL TESTS OK** (7 tests). New: FADT (HW_REDUCED_ACPI, PSCI=0), DBG2 (PL011), `qemu-virt` model (CLI `-m`), parking v1 with distinct pages, boot CPU first, GICR=1, no placeholder GICD.
- Verified by the agent with `-machine dumpdtb`+`dtc`: MPIDR 0..3, GICD 0x08000000, GICR 0x080A0000, timer GSIV 29/30/27/26, PL011 0x09000000.
- **Open hypotheses:** FADT/DBG2/GICR Structure layout written from memory from the ACPI 6.5 spec (`Acpi65.h` not compared); qemu-virt GTDT flags only ALWAYS_ON (not active-low as requested: the DT says active-high); parking with PSCI=0 on qemu-virt even though QEMU uses PSCI.
- Agent's missteps: an overly broad `find /` (no damage), a failed `sed` that was redone.
- **Next: N4** (DXE driver WatariAcpiOverride in `edk2-apple/WatariQemuPkg`).

## 2026-10-14 ~03:30: N4 HALF DONE (Sonnet agent + Opus verification)
- **Build succeeded (verified):** `edk2-apple/WatariQemuPkg` + `tools/build-edk2-watari.sh`; firmware `WATARI_DATA/vm/QEMU_EFI-watari.fd`. The driver compiles acpigen's sources without copying them (`string.h` shim). Driver log verified in person in the serial.
- **Windows with our tables: does NOT boot** (F-11), while with the original firmware it reaches the desktop (verified with screenshots). The disk was not modified (`snapshot=on`).
- Agent incident: it accidentally overwrote the `build-edk2.sh` build and regenerated it (no damage, the VM uses copies).
- The serial shows repeated lines with `\SystemRoot\system32\ntoskrnl.exe`: hypothesis, kernel debug traffic over serial (testsigning/debug therefore active); not verified.
- **Resume from:** F-11, hypothesis 1 (parking disabled in the qemu-virt model).
- All QEMUs are closed. No downloads/installations. `INSTALLED.md`: new files in `edk2-apple/`, `tools/build-edk2-watari.sh`, `WATARI_DATA/build-watari/` and `vm/QEMU_EFI-watari.fd`.

## 2026-10-14 (overnight run 2): no open tasks
- N1-N4 all ticked in NIGHT.md; branch already pushed, PR #27 already open, no active QEMU. No action. Resume from F-11 (hypothesis 1: parking disabled in qemu-virt) in a session with Marco.

## 2026-10-14, overnight run 3 (closing): SUMMARY FOR MARCO
No open tasks, no QEMU running (verified with `pgrep`). No downloads.

**What succeeded (with evidence)**
- N1, HVF working: cause `ID_AA64PFR0_EL1.GIC=0` under HVF, patch `tools/edk2-hvf-gicv3.patch` (F-08). Screenshot `logs/screens/win-hvf-180s.png` (Windows 11 Setup).
- N2, automatic installation of Windows 11 ARM64 with HVF, without questions, ~1h30. Screenshots `n2-0023.png` (22%) and `n2-SUCCESSO-desktop.png` (desktop, user `watari`). Disk: `vm/win11.qcow2` (~14 GB).
- N3, `tools/acpigen` fixed (parking v1, boot CPU, GICR, FADT, DBG2, `qemu-virt` model): `make -C tools/acpigen test` green, 7 tests.
- N4, build of the `WatariAcpiOverride` driver (`edk2-apple/WatariQemuPkg`, `tools/build-edk2-watari.sh`): it compiles and runs (serial log).

**What failed / was not verified**
- **F-11:** with our MADT+GTDT Windows hangs after `ntoskrnl.exe` (screenshot `n4w-c.png`); with the original firmware it reaches the desktop (`n4w-base.png`). Main suspicion: parking protocol without our own FADT.
- **F-10:** black screen mistaken for a hang (it was the screen turned off for inactivity). **F-09:** agents' downloads outside the rules.
- Not verified: `C:\watari-ok.txt`, testsigning, kernel serial debugging; FADT/DBG2/GICR layout written from memory from the spec.

**Decisions for Marco**
1. Merge of PR #27 (not done by me).
2. Which F-11 hypothesis to try first (advice: parking disabled, then our own FADT).
3. Keep or remove `vm/win11.qcow2` (14 GB) and `WATARI_DATA/build-watari/`: list in `docs/INSTALLED.md`.

**Where to resume:** F-11, hypothesis 1, with `tools/build-edk2-watari.sh` and the N2 disk (use `snapshot=on`).
